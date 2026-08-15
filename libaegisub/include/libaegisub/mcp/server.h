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

/// @file ../libaegisub/include/libaegisub/mcp/server.h
/// @brief 主进程内嵌的 Model Context Protocol 服务器，支持无状态 HTTP 与 HTTP+SSE 传输
/// @ingroup mcp
///
/// 该服务器随 Aegisub 主进程启动，监听 127.0.0.1 上的 HTTP 端口，
/// 接受 MCP JSON-RPC 2.0 请求，在 GUI 主线程上同步派发 tool 调用，
/// 让外部 AI 客户端（Claude Desktop 等）全自动接管 Aegisub 的字幕编辑能力，
/// 用户在同一实例中可以看到 AI 所做的实时修改
///
/// 传输方式（两者并存）：
///   - 无状态 POST：客户端直接 POST /mcp（或任意路径），请求-响应单次完成
///   - HTTP+SSE：客户端 GET /sse 建立长连接，经 endpoint 事件取得
///     /message?sessionId=xxx 消息端点，响应经 SSE message 事件推送

#pragma once

#include <libaegisub/cajun/elements.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace agi::mcp {
	/// MCP JSON-RPC 错误码枚举，遵循 JSON-RPC 2.0 规范保留 -32700 ~ -32000 范围，
	/// 自定义错误码使用 -32000 ~ -32099
	enum class ErrorCode : int64_t {
		ParseError = -32700, ///< JSON 解析失败
		InvalidRequest = -32600, ///< 请求不是合法的 JSON-RPC 对象
		MethodNotFound = -32601, ///< 请求的方法不存在
		InvalidParams = -32602, ///< 参数无效
		InternalError = -32603, ///< 服务器内部错误

		ToolNotFound = -32000, ///< 指定的工具未注册
		MissingArg = -32001, ///< 缺少必需参数
		InvalidArg = -32002, ///< 参数值格式错误
		NoContext = -32003, ///< 无活动 Aegisub 工程上下文
		ToolError = -32004, ///< 工具执行时抛出异常
	};

	/// 单个 MCP 工具的元信息
	struct ToolInfo {
		std::string name; ///< 工具唯一名称
		std::string description; ///< 一句话用途说明
		json::UnknownElement inputSchema; ///< JSON Schema 描述参数（move-only）
	};

	/// Tool 处理器签名：接收 JSON 参数对象，返回 JSON 结果对象，
	/// 所有调用均在 GUI 主线程上执行，可以安全访问 agi::Context
	using ToolHandler = std::function<json::UnknownElement(json::Object const &)>;

	/// 注册一个工具：header 暴露元信息，handler 在主线程上被调用
	void RegisterTool(std::string name, std::string description, json::UnknownElement inputSchema, ToolHandler handler);

	/// 把当前已注册的全部工具元信息序列化为一个 JSON 数组，
	/// 返回 json::Array 而非 vector<ToolInfo>，因为 UnknownElement 是 move-only，
	/// 无法塞进可拷贝的 vector
	json::UnknownElement ListTools();

	/// 在主线程上同步执行某个工具，返回 JSON 结果或抛出异常
	json::UnknownElement CallTool(std::string const &name, json::Object const &arguments);

	/// 处理一条 JSON-RPC 请求字符串，返回 JSON-RPC 响应字符串，
	/// 通知（id 缺失）返回空字符串，调用方不应回 HTTP body
	/// @param request_body 原始 JSON-RPC 请求文本
	/// @return JSON-RPC 响应文本，或空字符串（通知）
	std::string HandleJsonRpcRequest(std::string_view request_body);

	/// 启动 MCP HTTP 服务器主循环，阻塞至 server 停止，
	/// 同时提供无状态 POST 与 HTTP+SSE 两种传输，
	/// 该函数会启动后台线程接受 TCP 连接，tool 调用通过
	/// agi::dispatch::Main().Sync 派发到 GUI 线程执行并同步等待结果返回给客户端，
	/// 应在 Aegisub 启动后于独立线程中调用此函数
	/// @param host 监听地址，通常 "127.0.0.1"
	/// @param port 监听端口
	void RunHttpServer(std::string const &host, uint16_t port);

	/// 在 Aegisub 退出路径中调用，关闭 acceptor 使 RunHttpServer 返回
	void StopHttpServer();
} // namespace agi::mcp
