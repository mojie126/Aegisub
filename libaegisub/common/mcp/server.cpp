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

/// @file libaegisub/common/mcp/server.cpp
/// @brief MCP JSON-RPC over Streamable HTTP 服务器的实现
/// @ingroup mcp

#include "libaegisub/mcp/server.h"

#include "libaegisub/cajun/elements.h"
#include "libaegisub/cajun/reader.h"
#include "libaegisub/cajun/writer.h"
#include "libaegisub/dispatch.h"
#include "libaegisub/log.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
using json::Array;
using json::Object;
using json::UnknownElement;
using boost::asio::ip::tcp;

/// 快捷转换 ErrorCode 枚举到 int64_t
constexpr int64_t EC(agi::mcp::ErrorCode e) { return static_cast<int64_t>(e); }

struct ServerGuard {
	boost::asio::io_context io_ctx;
	boost::asio::ip::tcp::acceptor acceptor;
	std::atomic<bool> stopped{false};
	explicit ServerGuard()
		: io_ctx(), acceptor(io_ctx) {}
};
static std::atomic<ServerGuard*> g_server{nullptr};

/// 已注册工具集合: 工具名 -> {info, handler}
struct ToolEntry {
	agi::mcp::ToolInfo info;
	agi::mcp::ToolHandler handler;
};

std::mutex& tools_mutex() {
	static std::mutex m;
	return m;
}

std::map<std::string, ToolEntry, std::less<>>& tools() {
	static std::map<std::string, ToolEntry, std::less<>> t;
	return t;
}

/// 构造一个 JSON-RPC 2.0 错误响应
UnknownElement MakeError(UnknownElement id, int64_t code, std::string const& message,
                          UnknownElement data = json::Null{}) {
	Object err;
	err["code"] = static_cast<int64_t>(code);
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
UnknownElement const* GetField(Object const& obj, std::string_view key) {
	auto it = obj.find(key);
	if (it == obj.end()) return nullptr;
	return &it->second;
}

/// 深拷贝一个 UnknownElement，
/// cajun 的 UnknownElement 是 move-only 且不允许拷贝，
/// 这里通过 visitor 重建一份，对 Object/Array 也递归重建
struct ValueCloner final : json::ConstVisitor {
	UnknownElement cloned;
	void Visit(json::Array const& a) override {
		json::Array out;
		out.reserve(a.size());
		for (auto const& e : a) {
			ValueCloner sub;
			e.Accept(sub);
			out.emplace_back(std::move(sub.cloned));
		}
		cloned = std::move(out);
	}
	void Visit(json::Object const& o) override {
		json::Object out;
		for (auto const& [k, v] : o) {
			ValueCloner sub;
			v.Accept(sub);
			out.emplace(k, std::move(sub.cloned));
		}
		cloned = std::move(out);
	}
	void Visit(int64_t n) override { cloned = n; }
	void Visit(double n) override { cloned = n; }
	void Visit(json::String const& s) override { cloned = s; }
	void Visit(bool b) override { cloned = b; }
	void Visit(json::Null const&) override { cloned = json::Null{}; }
};

UnknownElement CloneValue(UnknownElement const& v) {
	ValueCloner cloner;
	v.Accept(cloner);
	return std::move(cloner.cloned);
}

/// 把 UnknownElement 序列化为 JSON 字符串
std::string ToJsonString(UnknownElement const& v) {
	std::ostringstream os;
	agi::JsonWriter::Write(v, os);
	return os.str();
}

/// 把一个 Object 作成"已注册工具"在 tools/list 中的条目
UnknownElement MakeToolEntry(agi::mcp::ToolInfo const& tool) {
	Object obj;
	obj["name"] = tool.name;
	obj["description"] = tool.description;
	obj["inputSchema"] = ::CloneValue(tool.inputSchema);
	return obj;
}

/// 处理 initialize 请求
UnknownElement HandleInitialize(Object const& params) {
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
UnknownElement HandleToolsList(Object const& /*params*/) {
	return agi::mcp::ListTools();
}

/// 处理 tools/call 请求：在 GUI 主线程上同步执行 tool
UnknownElement HandleToolsCall(Object const& params) {
	auto name = GetField(params, "name");
	if (!name) throw std::runtime_error("missing 'name' parameter");
	std::string tool_name;
	try {
		tool_name = static_cast<std::string const&>(*name);
	} catch (...) {
		throw std::runtime_error("'name' must be a string");
	}

	auto arguments_ptr = GetField(params, "arguments");
	Object const* arguments = nullptr;
	if (arguments_ptr) {
		try {
			arguments = &static_cast<Object const&>(*arguments_ptr);
		} catch (...) {
			throw std::invalid_argument("'arguments' must be a JSON object");
		}
	} else {
		throw std::invalid_argument("missing 'arguments'");
	}

	UnknownElement result;
	std::string err_msg;
	bool failed = false;

	agi::dispatch::Main().Sync([&] {
		std::lock_guard<std::mutex> lock(::tools_mutex());
		auto it = ::tools().find(tool_name);
		if (it == ::tools().end()) {
			failed = true;
			err_msg = "unknown tool: " + tool_name;
			return;
		}
		try {
			result = it->second.handler(*arguments);
		} catch (std::exception const& e) {
			failed = true;
			err_msg = e.what();
		} catch (...) {
			failed = true;
			err_msg = "tool threw non-std exception";
		}
	});

	if (failed)
		throw std::runtime_error(err_msg);

	// tools/call 标准返回结构: { content: [{type:text, text:...}], isError?:bool }
	Object content_item;
	content_item["type"] = std::string("text");
	content_item["text"] = ::ToJsonString(result);
	Array content;
	content.emplace_back(std::move(content_item));

	Object call_result;
	call_result["content"] = std::move(content);
	call_result["isError"] = false;
	return call_result;
}

/// 主分发：根据 method 字段路由到各 handler，
/// 返回非空字符串表示产生了应响应的消息，空字符串表示是通知不响应
std::string DispatchRequest(Object const& request) {
	auto id_ptr = GetField(request, "id");
	bool has_id = id_ptr != nullptr;
	UnknownElement id_entry = json::Null{};
	if (has_id) id_entry = ::CloneValue(*id_ptr);

	auto method_ptr = GetField(request, "method");
	if (!method_ptr) {
		if (has_id)
			return ::ToJsonString(::MakeError(std::move(id_entry), ::EC(agi::mcp::ErrorCode::InvalidRequest), "invalid Request: missing method"));
		return {};
	}
	std::string method;
	try {
		method = static_cast<std::string const&>(*method_ptr);
	} catch (...) {
		if (has_id)
			return ::ToJsonString(::MakeError(std::move(id_entry), ::EC(agi::mcp::ErrorCode::InvalidRequest), "method must be a string"));
		return {};
	}

	auto params_ptr = GetField(request, "params");
	Object const* params = nullptr;
	Object empty_params;
	if (params_ptr) {
		try {
			params = &static_cast<Object const&>(*params_ptr);
		} catch (...) {
			params = &empty_params;
		}
	} else {
		params = &empty_params;
	}

	try {
		UnknownElement result;
		if (method == "initialize")
			result = ::HandleInitialize(*params);
		else if (method == "initialized" || method == "notifications/initialized")
			return {}; // 客户端发来的通知,无需响应
		else if (method == "tools/list")
			result = ::HandleToolsList(*params);
		else if (method == "tools/call")
			result = ::HandleToolsCall(*params);
		else if (method == "shutdown" || method == "exit")
			result = json::Null{};
		else {
			if (has_id)
				return ::ToJsonString(::MakeError(std::move(id_entry), ::EC(agi::mcp::ErrorCode::MethodNotFound), "method not found: " + method));
			return {};
		}
		if (has_id)
			return ::ToJsonString(::MakeResult(std::move(id_entry), std::move(result)));
		return {};
	} catch (std::exception const& e) {
		if (has_id)
			return ::ToJsonString(::MakeError(std::move(id_entry), ::EC(agi::mcp::ErrorCode::InternalError), e.what()));
		return {};
	} catch (...) {
		if (has_id)
			return ::ToJsonString(::MakeError(std::move(id_entry), ::EC(agi::mcp::ErrorCode::InternalError), "internal error"));
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
size_t ParseHttpHeaders(std::string_view raw, size_t& body_start) {
	body_start = raw.find("\r\n\r\n");
	if (body_start == std::string_view::npos) return 0;
	body_start += 4;

	// 查找 Content-Length 头(不区分大小写)
	std::string headers(raw.substr(0, body_start));
	std::string lowered;
	lowered.reserve(headers.size());
	for (char c : headers) lowered += static_cast<char>(tolower(static_cast<unsigned char>(c)));

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
std::string MakeHttpResponse(int status, std::string const& body, std::string const& content_type = "application/json")
{
	std::string reason;
	switch (status) {
		case 200: reason = "OK"; break;
		case 202: reason = "Accepted"; break;
		case 204: reason = "No Content"; break;
		case 400: reason = "Bad Request"; break;
		case 404: reason = "Not Found"; break;
		case 500: reason = "Internal Server Error"; break;
		default:  reason = "Unknown"; break;
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

/// 尝试将 body 从系统 locale 编码转换为 UTF-8
/// 若 body 已是合法 UTF-8 则原样返回
static std::string EnsureUtf8(std::string const& body) {
	if (body.empty()) return body;

	// 先尝试以 UTF-8 解码，成功则说明 body 已是 UTF-8
	int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, body.data(), (int)body.size(), nullptr, 0);
	if (len > 0) return body;

	// 非法 UTF-8：从系统 locale 编码（GBK 等）转码到 UTF-8
	len = MultiByteToWideChar(CP_ACP, 0, body.data(), (int)body.size(), nullptr, 0);
	if (len <= 0) return body; // 转码失败，原样返回

	std::wstring wstr(static_cast<size_t>(len), L'\0');
	MultiByteToWideChar(CP_ACP, 0, body.data(), (int)body.size(), &wstr[0], len);

	len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
	if (len <= 0) return body;

	std::string utf8(static_cast<size_t>(len), '\0');
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &utf8[0], len, nullptr, nullptr);
	return utf8;
}

/// 处理一个 HTTP 连接：读取请求，派发 JSON-RPC，返回响应
void HandleSession(tcp::socket socket) {
	try {
		boost::asio::streambuf buffer;
		boost::asio::read_until(socket, buffer, "\r\n\r\n");

		auto buf_data = buffer.data();
		auto buf_begin = static_cast<const char*>(buf_data.data());
		std::string header_data(buf_begin, buf_data.size());
		size_t body_start = 0;
		size_t content_length = ::ParseHttpHeaders(header_data, body_start);

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
			boost::asio::write(socket, boost::asio::buffer(::MakeHttpResponse(204, "")));
			return;
		}

		// 只处理 POST /mcp(或任意 POST path)
		if (method != "POST" || body.empty()) {
			boost::asio::write(socket, boost::asio::buffer(
				::MakeHttpResponse(404, R"({"error":"not found"})")));
			return;
		}

		// 确保 body 是 UTF-8 编码（兼容 GBK 等非 UTF-8 客户端）
		body = ::EnsureUtf8(body);

		// 派发 JSON-RPC
		std::string response_body = agi::mcp::HandleJsonRpcRequest(body);
		int status = response_body.empty() ? 202 : 200;
		if (response_body.empty())
			response_body = "{}"; // 通知不产生响应,返回空 202
		boost::asio::write(socket, boost::asio::buffer(
			::MakeHttpResponse(status, response_body)));
	} catch (std::exception const& e) {
		LOG_D("mcp/http") << "session error: " << e.what();
		try {
			boost::asio::write(socket, boost::asio::buffer(
				::MakeHttpResponse(500, R"({"error":"internal server error"})")));
		} catch (...) {}
	}
}

} // anonymous namespace

namespace agi::mcp {

void RegisterTool(std::string name, std::string description,
                  json::UnknownElement inputSchema, ToolHandler handler) {
	std::lock_guard<std::mutex> lock(::tools_mutex());
	ToolEntry entry;
	entry.info.name = std::move(name);
	entry.info.description = std::move(description);
	entry.info.inputSchema = std::move(inputSchema);
	entry.handler = std::move(handler);
	::tools()[entry.info.name] = std::move(entry);
}

UnknownElement ListTools() {
	std::lock_guard<std::mutex> lock(::tools_mutex());
	Array tools_arr;
	for (auto const& [name, entry] : ::tools())
		tools_arr.emplace_back(::MakeToolEntry(entry.info));
	Object result;
	result["tools"] = std::move(tools_arr);
	return result;
}

UnknownElement CallTool(std::string const& name, Object const& arguments) {
	UnknownElement result;
	std::string err_msg;
	bool failed = false;
	agi::dispatch::Main().Sync([&] {
		std::lock_guard<std::mutex> lock(::tools_mutex());
		auto it = ::tools().find(name);
		if (it == ::tools().end()) {
			failed = true;
			err_msg = "unknown tool: " + name;
			return;
		}
		try {
			result = it->second.handler(arguments);
		} catch (std::exception const& e) {
			failed = true;
			err_msg = e.what();
		} catch (...) {
			failed = true;
			err_msg = "tool threw non-std exception";
		}
	});
	if (failed)
		throw std::runtime_error(err_msg);
	return result;
}

std::string HandleJsonRpcRequest(std::string_view request_body) {
	// 解析 JSON
	UnknownElement request;
	try {
		std::string body_str(request_body);
		std::istringstream iss(body_str);
		json::Reader::Read(request, iss);
	} catch (std::exception const& e) {
		LOG_E("mcp/server") << "failed to parse JSON: " << e.what();
		Object err;
		err["code"] = static_cast<int64_t>(::EC(agi::mcp::ErrorCode::ParseError));
		err["message"] = std::string("parse error: ") + e.what();
		Object resp;
		resp["jsonrpc"] = std::string("2.0");
		resp["id"] = json::Null{};
		resp["error"] = std::move(err);
		return ::ToJsonString(UnknownElement(std::move(resp)));
	}

	Object const* obj = nullptr;
	try {
		obj = &static_cast<Object const&>(request);
	} catch (...) {
		return ::ToJsonString(::MakeError(json::Null{}, ::EC(agi::mcp::ErrorCode::InvalidRequest), "request must be a JSON object"));
	}

	return ::DispatchRequest(*obj);
}

void RunHttpServer(std::string const& host, uint16_t port) {
	LOG_I("mcp/server") << "Aegisub MCP HTTP server starting on " << host << ":" << port;

	auto* guard = new ServerGuard;
	g_server.store(guard, std::memory_order_release);
	auto& io_ctx = guard->io_ctx;
	auto& acceptor = guard->acceptor;
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
		::HandleSession(std::move(socket));
	}

	LOG_I("mcp/server") << "Aegisub MCP HTTP server stopped";
	guard = g_server.load(std::memory_order_acquire);
	g_server.store(nullptr, std::memory_order_release);
	delete guard;
}

void StopHttpServer() {
	auto* guard = g_server.load(std::memory_order_acquire);
	if (guard) {
		guard->stopped.store(true, std::memory_order_release);
		boost::system::error_code ec;
		guard->acceptor.close(ec);
		if (ec)
			LOG_D("mcp/server") << "failed to close acceptor: " << ec.message();
	}
}

} // namespace agi::mcp
