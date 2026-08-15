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

/// @file src/mcp_server.h
/// @brief 主进程内嵌 MCP 工具注册入口
/// @ingroup mcp

#pragma once

#include <vector>

namespace agi {
	struct Context;
}

class FrameMain;

namespace agi::mcp {
	/// 注册所有由 Aegisub 主工程提供的 MCP 工具，
	/// 应在 agi::Context 创建好之后调用一次
	void RegisterMcpTools();

	/// 设置 Aegisub 窗口容器指针，ActiveContext() 动态从中获取当前活动 Context
	/// @param frames AegisubApp::frames 向量地址，在 AegisubApp::OnExit 中设为 nullptr
	void SetFrames(std::vector<FrameMain *> *frames);

	/// 取当前活动 agi::Context，从 frames 首窗口动态获取，可能为空
	Context *ActiveContext();
} // namespace agi::mcp
