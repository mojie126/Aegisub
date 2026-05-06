// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪对话框声明
// 对应 MoonScript 版 arch.PerspectiveMotion 的 show_dialog()

#pragma once

#include "perspective_data.h"
#include "perspective_processor.h"

#include <string>

namespace agi {
	struct Context;
}

namespace mocha {
	/// 透视追踪对话框返回值
	struct PerspectiveDialogResult {
		bool accepted = false;
		PerspectiveOptions options;
		std::string raw_data; ///< Power-Pin 原始数据文本
	};

	/// @brief 显示透视追踪对话框
	/// 用户在对话框中粘贴 AE CC Power Pin 数据、选择追踪选项
	/// @param c Aegisub 上下文
	/// @return 对话框结果
	PerspectiveDialogResult ShowPerspectiveDialog(agi::Context *c);
} // namespace mocha
