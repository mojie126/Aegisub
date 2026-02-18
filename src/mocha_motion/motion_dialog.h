// Copyright (c) 2024-2026, Aegisub contributors
// Mocha Motion 对话框声明
// 从 dialog_mocha.cpp 迁移并增强，使用新的 DataHandler 解析数据

#pragma once

#include "motion_common.h"
#include "motion_data_handler.h"

#include <functional>
#include <string>
#include <vector>

namespace agi { struct Context; }

namespace mocha {

/// Mocha Motion 对话框返回结果
struct MotionDialogResult {
	bool accepted = false;          // 用户是否确认
	MotionOptions options;          // 运动选项
	DataHandler main_data;          // 主追踪数据
	DataHandler clip_data;          // clip 追踪数据（可选）
	ClipTrackOptions clip_options;  // clip 追踪选项
	bool has_clip_data = false;     // 是否有独立 clip 数据
	int script_res_x = 0;          // 脚本水平分辨率
	int script_res_y = 0;          // 脚本垂直分辨率
};

/// 显示 Mocha Motion 对话框
/// @param c Aegisub 上下文
/// @return 对话框结果（包含选项和解析后的数据）
MotionDialogResult ShowMotionDialog(agi::Context* c);

} // namespace mocha
