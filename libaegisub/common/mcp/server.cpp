// Copyright (c) 2026, Aegisub contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file ../libaegisub/common/mcp/server.cpp
/// @brief MCP JSON-RPC 服务器的实现，支持无状态 HTTP 与 HTTP+SSE 传输
/// @ingroup mcp

#include "libaegisub/mcp/server.h"

#include "libaegisub/cajun/elements.h"
#include "libaegisub/cajun/reader.h"
#include "libaegisub/cajun/writer.h"
#include "libaegisub/dispatch.h"
#include "libaegisub/exception.h"
#include "libaegisub/log.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// Windows 的 GetMessage 宏与 agi::Exception::GetMessage() 冲突，
// 且 libaegisub 不依赖 wx 的 winundef.h，需在此显式取消宏
#ifdef GetMessage
#undef GetMessage
#endif

namespace {
	using json::Array;
	using json::Object;
	using json::UnknownElement;
	using boost::asio::ip::tcp;

	/// 快捷转换 ErrorCode 枚举到 int64_t
	constexpr int64_t EC(agi::mcp::ErrorCode e) { return static_cast<int64_t>(e); }

	struct ServerGuard {
		boost::asio::io_context io_ctx;
		tcp::acceptor acceptor;
		std::atomic<bool> stopped{false};

		explicit ServerGuard()
			: acceptor(io_ctx) {}
	};

	std::atomic<ServerGuard *> g_server{nullptr};

	/// SSE 会话上限，防止长连接耗尽资源
	constexpr size_t kMaxSseSessions = 16;

	/// SSE 会话：对应一个 GET /sse 长连接，POST /message 的响应经它推送
	struct SseSession {
		std::string id; ///< 会话唯一 ID，即 endpoint 事件中的 sessionId
		tcp::socket socket; ///< SSE 连接 socket
		std::mutex write_mutex; ///< 序列化对 socket 的并发写
		std::atomic<bool> closed{false}; ///< 连接已关闭
		explicit SseSession(std::string id_, tcp::socket s)
			: id(std::move(id_)), socket(std::move(s)) {}
	};

	/// SSE 会话表：sessionId -> 会话，POST /message 与 StopHttpServer 均需访问
	std::mutex &sessions_mutex() {
		static std::mutex m;
		return m;
	}

	std::map<std::string, std::shared_ptr<SseSession>> &sessions() {
		static std::map<std::string, std::shared_ptr<SseSession>> s;
		return s;
	}

	/// 服务器级停止标志，由 StopHttpServer 置位，
	/// SSE 长连接线程检查它退出，避免直接引用 ServerGuard 造成 use-after-free
	std::atomic g_sse_stopped{false};

	/// sessionId 递增序号，保证唯一
	std::atomic<uint64_t> g_session_seq{0};

	/// 已注册工具集合: 工具名 -> {info, handler}
	struct ToolEntry {
		agi::mcp::ToolInfo info;
		agi::mcp::ToolHandler handler;
	};

	std::mutex &tools_mutex() {
		static std::mutex m;
		return m;
	}

	std::map<std::string, ToolEntry, std::less<>> &tools() {
		static std::map<std::string, ToolEntry, std::less<>> t;
		return t;
	}

	/// 构造一个 JSON-RPC 2.0 错误响应
	UnknownElement MakeError(UnknownElement id, const int64_t code, std::string const &message, UnknownElement data = json::Null{}) {
		Object err;
		err["code"] = code;
		err["message"] = message;
		err["data"] = std::move(data);

		Object resp;
		resp["jsonrpc"] = std::string("2.0");
		resp["id"] = std::move(id);
		resp["error"] = std::move(err);
		return resp;
	}

	/// 构造一个 JSON-RPC 2.0 成功响应
	UnknownElement MakeResult(UnknownElement id, UnknownElement result) {
		Object resp;
		resp["jsonrpc"] = std::string("2.0");
		resp["id"] = std::move(id);
		resp["result"] = std::move(result);
		return resp;
	}

	/// 取出对象字段，有则返回引用，无则返回 nullptr
	UnknownElement const *GetField(Object const &obj, const std::string_view key) {
		const auto it = obj.find(key);
		if (it == obj.end()) return nullptr;
		return &it->second;
	}

	/// 深拷贝一个 UnknownElement，
	/// cajun 的 UnknownElement 是 move-only 且不允许拷贝，
	/// 这里通过 visitor 重建一份，对 Object/Array 也递归重建
	struct ValueCloner final : json::ConstVisitor {
		UnknownElement cloned;

		void Visit(Array const &a) override {
			Array out;
			out.reserve(a.size());
			for (auto const &e : a) {
				ValueCloner sub;
				e.Accept(sub);
				out.emplace_back(std::move(sub.cloned));
			}
			cloned = std::move(out);
		}

		void Visit(Object const &o) override {
			Object out;
			for (auto const &[k, v] : o) {
				ValueCloner sub;
				v.Accept(sub);
				out.emplace(k, std::move(sub.cloned));
			}
			cloned = std::move(out);
		}

		void Visit(int64_t n) override { cloned = n; }
		void Visit(double n) override { cloned = n; }
		void Visit(json::String const &s) override { cloned = s; }
		void Visit(bool b) override { cloned = b; }
		void Visit(json::Null const &) override { cloned = json::Null{}; }
	};

	UnknownElement CloneValue(UnknownElement const &v) {
		ValueCloner cloner;
		v.Accept(cloner);
		return std::move(cloner.cloned);
	}

	/// 把 UnknownElement 序列化为 JSON 字符串
	std::string ToJsonString(UnknownElement const &v) {
		std::ostringstream os;
		agi::JsonWriter::Write(v, os);
		return os.str();
	}

	/// 把一个 Object 作成"已注册工具"在 tools/list 中的条目
	UnknownElement MakeToolEntry(agi::mcp::ToolInfo const &tool) {
		Object obj;
		obj["name"] = tool.name;
		obj["description"] = tool.description;
		obj["inputSchema"] = CloneValue(tool.inputSchema);
		return obj;
	}

	/// 处理 initialize 请求
	UnknownElement HandleInitialize(Object const & /*params*/) {
		Object server_info;
		server_info["name"] = std::string("aegisub");
		server_info["version"] = std::string("0.1.0");

		Object capabilities;
		Object tools_cap;
		tools_cap["listChanged"] = false;
		capabilities["tools"] = std::move(tools_cap);

		Object result;
		// protocolVersion 为 MCP 协议规范版本日期(2024-11-05),不是 Aegisub 版本号，
		// 参见 https://spec.modelcontextprotocol.io/
		result["protocolVersion"] = std::string("2024-11-05");
		result["capabilities"] = std::move(capabilities);
		result["serverInfo"] = std::move(server_info);
		result["instructions"] = std::string(
			"Aegisub MCP server. Read-only tools parse .ass directly; "
			"write tools execute on the GUI main thread."
		);
		return result;
	}

	/// 处理 tools/list 请求
	UnknownElement HandleToolsList(Object const & /*params*/) {
		return agi::mcp::ListTools();
	}

	/// 处理 tools/call 请求：在 GUI 主线程上同步执行 tool
	UnknownElement HandleToolsCall(Object const &params) {
		const auto name = GetField(params, "name");
		if (!name) throw std::runtime_error("missing 'name' parameter");
		std::string tool_name;
		try {
			tool_name = static_cast<std::string const &>(*name);
		} catch (...) {
			throw std::runtime_error("'name' must be a string");
		}

		const auto arguments_ptr = GetField(params, "arguments");
		Object const *arguments = nullptr;
		if (arguments_ptr) {
			try {
				arguments = &static_cast<Object const &>(*arguments_ptr);
			} catch (...) {
				throw std::invalid_argument("'arguments' must be a JSON object");
			}
		} else {
			throw std::invalid_argument("missing 'arguments'");
		}

		UnknownElement result;
		std::string err_msg;
		bool failed = false;

		agi::dispatch::Main().Sync(
			[&] {
				std::lock_guard lock(tools_mutex());
				const auto it = tools().find(tool_name);
				if (it == tools().end()) {
					failed = true;
					err_msg = "unknown tool: " + tool_name;
					return;
				}
				try {
					result = it->second.handler(*arguments);
				} catch (agi::Exception const &e) {
					// agi::Exception 不继承 std::exception，需单独捕获
					failed = true;
					err_msg = e.GetMessage();
				} catch (std::exception const &e) {
					failed = true;
					err_msg = e.what();
				} catch (...) {
					failed = true;
					err_msg = "tool threw non-std exception";
				}
			}
		);

		if (failed)
			throw std::runtime_error(err_msg);

		// tools/call 标准返回结构: { content: [...] , isError?:bool }
		// handler 返回的对象若自带 "content" 数组(如同时含 text 与 image 项)则直接使用，
		// 否则默认包装为单个 text 项
		Array content;
		UnknownElement const *custom_content = nullptr;
		try {
			auto const &obj = static_cast<Object const &>(result);
			custom_content = GetField(obj, "content");
		} catch (...) {}
		if (custom_content) {
			try {
				for (auto const &arr = static_cast<Array const &>(*custom_content); auto const &item : arr)
					content.emplace_back(CloneValue(item));
			} catch (...) {
				custom_content = nullptr;
			}
		}
		if (!custom_content) {
			Object content_item;
			content_item["type"] = std::string("text");
			content_item["text"] = ToJsonString(result);
			content.emplace_back(std::move(content_item));
		}

		Object call_result;
		call_result["content"] = std::move(content);
		call_result["isError"] = false;
		return call_result;
	}

	/// 主分发：根据 method 字段路由到各 handler，
	/// 返回非空字符串表示产生了应响应的消息，空字符串表示是通知不响应
	std::string DispatchRequest(Object const &request) {
		const auto id_ptr = GetField(request, "id");
		const bool has_id = id_ptr != nullptr;
		UnknownElement id_entry = json::Null{};
		if (has_id) id_entry = CloneValue(*id_ptr);

		const auto method_ptr = GetField(request, "method");
		if (!method_ptr) {
			if (has_id)
				return ToJsonString(MakeError(std::move(id_entry), EC(agi::mcp::ErrorCode::InvalidRequest), "invalid Request: missing method"));
			return {};
		}
		std::string method;
		try {
			method = static_cast<std::string const &>(*method_ptr);
		} catch (...) {
			if (has_id)
				return ToJsonString(MakeError(std::move(id_entry), EC(agi::mcp::ErrorCode::InvalidRequest), "method must be a string"));
			return {};
		}

		const auto params_ptr = GetField(request, "params");
		Object const *params = nullptr;
		const Object empty_params;
		if (params_ptr) {
			try {
				params = &static_cast<Object const &>(*params_ptr);
			} catch (...) {
				params = &empty_params;
			}
		} else {
			params = &empty_params;
		}

		try {
			UnknownElement result;
			if (method == "initialize")
				result = HandleInitialize(*params);
			else if (method == "initialized" || method == "notifications/initialized")
				return {}; // 客户端发来的通知,无需响应
			else if (method == "tools/list")
				result = HandleToolsList(*params);
			else if (method == "tools/call")
				result = HandleToolsCall(*params);
			else if (method == "shutdown" || method == "exit")
				result = json::Null{};
			else {
				if (has_id)
					return ToJsonString(MakeError(std::move(id_entry), EC(agi::mcp::ErrorCode::MethodNotFound), "method not found: " + method));
				return {};
			}
			if (has_id)
				return ToJsonString(MakeResult(std::move(id_entry), std::move(result)));
			return {};
		} catch (agi::Exception const &e) {
			if (has_id)
				return ToJsonString(MakeError(std::move(id_entry), EC(agi::mcp::ErrorCode::InternalError), e.GetMessage()));
			return {};
		} catch (std::exception const &e) {
			if (has_id)
				return ToJsonString(MakeError(std::move(id_entry), EC(agi::mcp::ErrorCode::InternalError), e.what()));
			return {};
		} catch (...) {
			if (has_id)
				return ToJsonString(MakeError(std::move(id_entry), EC(agi::mcp::ErrorCode::InternalError), "internal error"));
			return {};
		}
	}

// --------------------------------------------------------------------------- //
// HTTP 传输层: 基于 boost::asio 的最小 HTTP/1.1 server
// --------------------------------------------------------------------------- //

	/// 解析 HTTP 请求头，提取 Content-Length 和 body 起始位置
	/// @param raw 原始请求数据
	/// @param body_start 输出：body 在 raw 中的起始偏移
	/// @return Content-Length 值，0 表示无 body 或解析失败
	size_t ParseHttpHeaders(std::string_view raw, size_t &body_start) {
		body_start = raw.find("\r\n\r\n");
		if (body_start == std::string_view::npos) return 0;
		body_start += 4;

		// 查找 Content-Length 头(不区分大小写)
		const std::string headers(raw.substr(0, body_start));
		std::string lowered;
		lowered.reserve(headers.size());
		for (const char c : headers) lowered += static_cast<char>(tolower(static_cast<unsigned char>(c)));

		size_t pos = lowered.find("content-length:");
		if (pos == std::string::npos) return 0;
		pos += 15; // skip "content-length:"
		while (pos < lowered.size() && (lowered[pos] == ' ' || lowered[pos] == '\t')) pos++;
		size_t end = pos;
		while (end < lowered.size() && lowered[end] >= '0' && lowered[end] <= '9') end++;
		if (end == pos) return 0;
		return std::stoul(std::string(lowered.substr(pos, end - pos)));
	}

	/// 构造一个 HTTP 响应
	std::string MakeHttpResponse(const int status, std::string const &body, std::string const &content_type = "application/json") {
		std::string reason;
		switch (status) {
			case 200: reason = "OK";
				break;
			case 202: reason = "Accepted";
				break;
			case 204: reason = "No Content";
				break;
			case 400: reason = "Bad Request";
				break;
			case 404: reason = "Not Found";
				break;
			case 500: reason = "Internal Server Error";
				break;
			default: reason = "Unknown";
				break;
		}
		std::ostringstream resp;
		resp << "HTTP/1.1 " << status << " " << reason << "\r\n";
		resp << "Content-Type: " << content_type << "\r\n";
		resp << "Content-Length: " << body.size() << "\r\n";
		resp << "Access-Control-Allow-Origin: *\r\n";
		resp << "Access-Control-Allow-Methods: POST, GET, OPTIONS, DELETE\r\n";
		resp << "Access-Control-Allow-Headers: Content-Type, Accept, MCP-Session-Id\r\n";
		resp << "Connection: close\r\n";
		resp << "\r\n";
		resp << body;
		return resp.str();
	}

	/// 构造一个 SSE 事件文本，data 中的 JSON 若为多行格式化输出则压缩为单行，
	/// 兼容按行解析 data 的简单 SSE 客户端
	std::string MakeSseEvent(std::string const &event, std::string const &data) {
		std::string single_line;
		single_line.reserve(data.size());
		for (const char c : data) {
			if (c != '\r' && c != '\n' && c != '\t') single_line += c;
		}

		std::string msg = "event: ";
		msg += event;
		msg += "\ndata: ";
		msg += single_line;
		msg += "\n\n";
		return msg;
	}

	/// 向 SSE 会话推送 message 事件，连接异常时标记关闭并返回 false
	bool SsePush(std::shared_ptr<SseSession> const &sess, std::string const &data) {
		std::string msg = MakeSseEvent("message", data);
		boost::system::error_code ec;
		size_t written = 0;
		{
			std::lock_guard lock(sess->write_mutex);
			// 非阻塞 socket 发送缓冲满时返回 would_block 且只写入部分数据，
			// 按返回值推进偏移并短延时重试，避免大响应（如图片 base64）被误判断开
			for (int attempt = 0; attempt < 50 && written < msg.size(); ++attempt) {
				ec.clear();
				written += boost::asio::write(
					sess->socket,
					boost::asio::buffer(msg.data() + written, msg.size() - written),
					ec);
				if (!ec) continue;
				if (ec == boost::asio::error::would_block) {
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
					continue;
				}
				break;
			}
		}
		if (written < msg.size()) {
			// 写入未完成（连接异常或缓冲长时间满），标记关闭
			LOG_D("mcp/server") << "sse push to session " << sess->id << " incomplete: "
			                    << written << "/" << msg.size() << " " << ec.message();
			sess->closed.store(true, std::memory_order_release);
			return false;
		}
		return true;
	}

	/// 从会话表移除指定会话（若仍存在）
	void RemoveSseSession(std::string const &session_id) {
		std::lock_guard lock(sessions_mutex());
		sessions().erase(session_id);
	}

	/// 处理 GET /sse：建立 SSE 长连接，保持到客户端断开或服务器停止，
	/// 期间 POST /message 的 JSON-RPC 响应通过该连接推送
	void HandleSse(tcp::socket socket) {
		// 服务器已在停止中则直接关闭连接
		if (g_sse_stopped.load(std::memory_order_acquire)) {
			boost::system::error_code ec;
			socket.close(ec);
			return;
		}

		// 关闭 Nagle 算法，避免事件被延迟合并发送
		boost::system::error_code nd_ec;
		socket.set_option(tcp::no_delay(true), nd_ec);

		std::string session_id = "s" + std::to_string(g_session_seq.fetch_add(1));
		auto sess = std::make_shared<SseSession>(session_id, std::move(socket));
		{
			std::lock_guard lock(sessions_mutex());
			if (sessions().size() >= kMaxSseSessions) {
				// 会话数超限，直接通过该连接写回错误后关闭
				boost::asio::write(
					sess->socket, boost::asio::buffer(
						MakeHttpResponse(503, R"({"error":"too many sessions"})")
					)
				);
				return;
			}
			sessions().emplace(session_id, sess);
		}

		// 发送 SSE 响应头与 endpoint 事件，告知客户端消息端点
		{
			std::ostringstream head;
			head << "HTTP/1.1 200 OK\r\n";
			head << "Content-Type: text/event-stream\r\n";
			head << "Cache-Control: no-cache\r\n";
			head << "Connection: keep-alive\r\n";
			head << "Access-Control-Allow-Origin: *\r\n";
			head << "Access-Control-Allow-Methods: POST, GET, OPTIONS, DELETE\r\n";
			head << "Access-Control-Allow-Headers: Content-Type, Accept, MCP-Session-Id\r\n";
			head << "\r\n";
			head << MakeSseEvent("endpoint", "/message?sessionId=" + session_id);
			boost::system::error_code ec;
			boost::asio::write(sess->socket, boost::asio::buffer(head.str()), ec);
			if (ec) {
				LOG_D("mcp/server") << "session " << session_id << " write endpoint event failed: " << ec.message();
				RemoveSseSession(session_id);
				return;
			}
		}

		// 非阻塞轮询，用读操作检测客户端断开，周期性发心跳注释保活
		boost::system::error_code nb_ec;
		sess->socket.non_blocking(true, nb_ec);
		if (nb_ec) {
			LOG_D("mcp/server") << "session " << session_id << " set non-blocking failed: " << nb_ec.message();
			RemoveSseSession(session_id);
			return;
		}
		std::vector<char> probe(256);
		auto last_keepalive = std::chrono::steady_clock::now();
		while (!g_sse_stopped.load(std::memory_order_acquire) && !sess->closed.load(std::memory_order_acquire)) {
			boost::system::error_code rec;
			sess->socket.read_some(boost::asio::buffer(probe), rec);
			if (rec == boost::asio::error::would_block) {
				if (auto now = std::chrono::steady_clock::now(); now - last_keepalive >= std::chrono::seconds(30)) {
					boost::system::error_code hec;
					{
						std::lock_guard lock(sess->write_mutex);
						boost::asio::write(sess->socket, boost::asio::buffer(": ping\n\n"), hec);
					}
					if (hec) break;
					last_keepalive = now;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				continue;
			}
			if (rec) break; // EOF 或错误，视为客户端断开
			// 收到客户端数据（SSE 客户端一般不发送），忽略并让出 CPU 防止忙等
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
		}

		LOG_D("mcp/server") << "session " << session_id << " closed";
		RemoveSseSession(session_id);
	}

	/// 处理 POST /message?sessionId=xxx：解析 query 中的 sessionId，
	/// 派发 JSON-RPC 请求，将响应通过对应 SSE 连接推送
	void HandleMessagePost(tcp::socket &socket, std::string const &path, std::string const &body) {
		// 从 query 中提取 sessionId
		std::string session_id;
		{
			if (const size_t q = path.find('?'); q != std::string::npos) {
				std::string_view query(path.data() + q + 1, path.size() - q - 1);
				if (const size_t p = query.find("sessionId="); p != std::string_view::npos) {
					std::string_view val(query.substr(p + 10));
					if (const size_t amp = val.find('&'); amp != std::string_view::npos) val = val.substr(0, amp);
					session_id = std::string(val);
				}
			}
		}
		if (session_id.empty()) {
			boost::asio::write(
				socket, boost::asio::buffer(
					MakeHttpResponse(400, R"({"error":"missing sessionId"})")
				)
			);
			return;
		}

		std::shared_ptr<SseSession> sess;
		{
			std::lock_guard lock(sessions_mutex());
			const auto it = sessions().find(session_id);
			if (it != sessions().end() && !it->second->closed.load(std::memory_order_acquire))
				sess = it->second;
		}
		if (!sess) {
			boost::asio::write(
				socket, boost::asio::buffer(
					MakeHttpResponse(404, R"({"error":"session not found"})")
				)
			);
			return;
		}

		if (const std::string response_body = agi::mcp::HandleJsonRpcRequest(body); !response_body.empty() && !SsePush(sess, response_body)) {
			// 推送失败说明连接已断，清理该会话
			std::lock_guard lock(sessions_mutex());
			if (const auto it = sessions().find(session_id); it != sessions().end() && it->second == sess)
				sessions().erase(it);
		}

		// 按 MCP 规范，响应经 SSE 事件推送，此处返回 202 Accepted
		boost::asio::write(socket, boost::asio::buffer(MakeHttpResponse(202, "")));
	}

	/// 尝试将 body 从系统 locale 编码转换为 UTF-8
	/// 若 body 已是合法 UTF-8 则原样返回
	std::string EnsureUtf8(std::string const &body) {
		if (body.empty()) return body;

		// 先尝试以 UTF-8 解码，成功则说明 body 已是 UTF-8
		int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, body.data(), static_cast<int>(body.size()), nullptr, 0);
		if (len > 0) return body;

		// 非法 UTF-8：从系统 locale 编码（GBK 等）转码到 UTF-8
		len = MultiByteToWideChar(CP_ACP, 0, body.data(), static_cast<int>(body.size()), nullptr, 0);
		if (len <= 0) return body; // 转码失败，原样返回

		std::wstring wstr(static_cast<size_t>(len), L'\0');
		MultiByteToWideChar(CP_ACP, 0, body.data(), static_cast<int>(body.size()), &wstr[0], len);

		len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
		if (len <= 0) return body;

		std::string utf8(static_cast<size_t>(len), '\0');
		WideCharToMultiByte(CP_UTF8, 0, wstr.data(), static_cast<int>(wstr.size()), &utf8[0], len, nullptr, nullptr);
		return utf8;
	}

	/// 处理一个 HTTP 连接：读取请求，派发 JSON-RPC，返回响应
	void HandleSession(tcp::socket socket) {
		try {
			boost::asio::streambuf buffer;
			boost::asio::read_until(socket, buffer, "\r\n\r\n");

			auto buf_data = buffer.data();
			auto buf_begin = static_cast<const char *>(buf_data.data());
			std::string header_data(buf_begin, buf_data.size());
			size_t body_start = 0;
			size_t content_length = ParseHttpHeaders(header_data, body_start);

			// 读取剩余 body
			std::string body;
			if (body_start < header_data.size())
				body = header_data.substr(body_start);
			if (content_length > body.size()) {
				std::vector<char> extra(content_length - body.size());
				boost::asio::read(socket, boost::asio::buffer(extra), boost::asio::transfer_exactly(extra.size()));
				body.append(extra.data(), extra.size());
			}

			// 提取请求方法行
			std::string method = "POST";
			std::string path = "/";
			{
				size_t sp1 = header_data.find(' ');
				size_t sp2 = header_data.find(' ', sp1 + 1);
				if (sp1 != std::string::npos && sp2 != std::string::npos) {
					method = header_data.substr(0, sp1);
					path = header_data.substr(sp1 + 1, sp2 - sp1 - 1);
				}
			}

			// CORS 预检
			if (method == "OPTIONS") {
				boost::asio::write(socket, boost::asio::buffer(MakeHttpResponse(204, "")));
				return;
			}

			// SSE 长连接: GET /sse
			if (method == "GET") {
				if (path.rfind("/sse", 0) == 0) {
					HandleSse(std::move(socket));
					return;
				}
				boost::asio::write(
					socket, boost::asio::buffer(
						MakeHttpResponse(404, R"({"error":"not found"})")
					)
				);
				return;
			}

			// 只处理 POST /mcp(或任意 POST path)
			if (method != "POST" || body.empty()) {
				boost::asio::write(
					socket, boost::asio::buffer(
						MakeHttpResponse(404, R"({"error":"not found"})")
					)
				);
				return;
			}

			// 确保 body 是 UTF-8 编码（兼容 GBK 等非 UTF-8 客户端）
			body = EnsureUtf8(body);

			// SSE 消息端点: POST /message?sessionId=xxx
			if (path.rfind("/message", 0) == 0) {
				HandleMessagePost(socket, path, body);
				return;
			}

			// 无状态 POST: 派发 JSON-RPC
			std::string response_body = agi::mcp::HandleJsonRpcRequest(body);
			int status = response_body.empty() ? 202 : 200;
			if (response_body.empty())
				response_body = "{}"; // 通知不产生响应,返回空 202
			boost::asio::write(
				socket, boost::asio::buffer(
					MakeHttpResponse(status, response_body)
				)
			);
		} catch (std::exception const &e) {
			LOG_D("mcp/http") << "session error: " << e.what();
			try {
				boost::asio::write(
					socket, boost::asio::buffer(
						MakeHttpResponse(500, R"({"error":"internal server error"})")
					)
				);
			} catch (...) {}
		}
	}
} // anonymous namespace

namespace agi::mcp {
	void RegisterTool(std::string name, std::string description, UnknownElement inputSchema, ToolHandler handler) {
		std::lock_guard lock(tools_mutex());
		ToolEntry entry;
		entry.info.name = std::move(name);
		entry.info.description = std::move(description);
		entry.info.inputSchema = std::move(inputSchema);
		entry.handler = std::move(handler);
		tools()[entry.info.name] = std::move(entry);
	}

	UnknownElement ListTools() {
		std::lock_guard lock(tools_mutex());
		Array tools_arr;
		for (const auto &[info, handler] : tools() | std::views::values)
			tools_arr.emplace_back(MakeToolEntry(info));
		Object result;
		result["tools"] = std::move(tools_arr);
		return result;
	}

	UnknownElement CallTool(std::string const &name, Object const &arguments) {
		UnknownElement result;
		std::string err_msg;
		bool failed = false;
		dispatch::Main().Sync(
			[&] {
				std::lock_guard lock(tools_mutex());
				const auto it = tools().find(name);
				if (it == tools().end()) {
					failed = true;
					err_msg = "unknown tool: " + name;
					return;
				}
				try {
					result = it->second.handler(arguments);
				} catch (Exception const &e) {
					// agi::Exception 不继承 std::exception，需单独捕获
					failed = true;
					err_msg = e.GetMessage();
				} catch (std::exception const &e) {
					failed = true;
					err_msg = e.what();
				} catch (...) {
					failed = true;
					err_msg = "tool threw non-std exception";
				}
			}
		);
		if (failed)
			throw std::runtime_error(err_msg);
		return result;
	}

	std::string HandleJsonRpcRequest(const std::string_view request_body) {
		// 解析 JSON
		UnknownElement request;
		try {
			const std::string body_str(request_body);
			std::istringstream iss(body_str);
			json::Reader::Read(request, iss);
		} catch (std::exception const &e) {
			LOG_E("mcp/server") << "failed to parse JSON: " << e.what();
			Object err;
			err["code"] = EC(ErrorCode::ParseError);
			err["message"] = std::string("parse error: ") + e.what();
			Object resp;
			resp["jsonrpc"] = std::string("2.0");
			resp["id"] = json::Null{};
			resp["error"] = std::move(err);
			return ToJsonString(UnknownElement(std::move(resp)));
		}

		Object const *obj = nullptr;
		try {
			obj = &static_cast<Object const &>(request);
		} catch (...) {
			return ToJsonString(MakeError(json::Null{}, EC(ErrorCode::InvalidRequest), "request must be a JSON object"));
		}

		return DispatchRequest(*obj);
	}

	void RunHttpServer(std::string const &host, uint16_t port) {
		LOG_I("mcp/server") << "Aegisub MCP HTTP server starting on " << host << ":" << port;

		// 复位停止标志，支持服务器停止后重新启动
		g_sse_stopped.store(false, std::memory_order_release);

		auto *guard = new ServerGuard;
		g_server.store(guard, std::memory_order_release);
		[[maybe_unused]] auto &io_ctx = guard->io_ctx;
		auto &acceptor = guard->acceptor;
		tcp::endpoint endpoint(boost::asio::ip::make_address(host), port);

		boost::system::error_code ec;
		acceptor.open(endpoint.protocol(), ec);
		if (ec) {
			LOG_E("mcp/server") << "failed to open acceptor: " << ec.message();
			delete guard;
			g_server.store(nullptr, std::memory_order_release);
			return;
		}
		acceptor.set_option(boost::asio::socket_base::reuse_address(true), ec);
		acceptor.bind(endpoint, ec);
		if (ec) {
			LOG_E("mcp/server") << "failed to bind " << host << ":" << port << ": " << ec.message();
			delete guard;
			g_server.store(nullptr, std::memory_order_release);
			return;
		}
		acceptor.listen(boost::asio::socket_base::max_listen_connections, ec);
		if (ec) {
			LOG_E("mcp/server") << "failed to listen: " << ec.message();
			delete guard;
			g_server.store(nullptr, std::memory_order_release);
			return;
		}

		LOG_I("mcp/server") << "Aegisub MCP HTTP server listening on " << host << ":" << port;

		while (true) {
			guard = g_server.load(std::memory_order_acquire);
			if (!guard || guard->stopped.load()) break;
			tcp::socket socket(guard->io_ctx);
			boost::system::error_code accept_ec;
			guard->acceptor.accept(socket, accept_ec);
			guard = g_server.load(std::memory_order_acquire);
			if (!guard || guard->stopped.load()) break;
			if (accept_ec) {
				if (accept_ec == boost::asio::error::operation_aborted) break;
				LOG_D("mcp/server") << "accept error: " << accept_ec.message();
				continue;
			}
			// 每连接一个线程处理，SSE 长连接不会阻塞 accept 循环
			std::thread(HandleSession, std::move(socket)).detach();
		}

		LOG_I("mcp/server") << "Aegisub MCP HTTP server stopped";
		guard = g_server.load(std::memory_order_acquire);
		g_server.store(nullptr, std::memory_order_release);
		delete guard;
	}

	void StopHttpServer() {
		g_sse_stopped.store(true, std::memory_order_release);
		if (auto *guard = g_server.load(std::memory_order_acquire)) {
			guard->stopped.store(true, std::memory_order_release);
			boost::system::error_code ec;
			guard->acceptor.close(ec);
			if (ec)
				LOG_D("mcp/server") << "failed to close acceptor: " << ec.message();
		}
		// 关闭所有 SSE 会话连接，让长连接线程退出
		std::lock_guard lock(sessions_mutex());
		for (const auto &sess : sessions() | std::views::values) {
			boost::system::error_code ec;
			sess->socket.close(ec);
			sess->closed.store(true, std::memory_order_release);
		}
		sessions().clear();
	}
} // namespace agi::mcp
