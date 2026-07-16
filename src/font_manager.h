// Copyright (c) 2026, Aegisub Project
// All rights reserved.

/// @file font_manager.h
/// @brief 用户字体管理接口
/// @ingroup secondary_ui

#pragma once

#include <vector>

#include <libaegisub/fs.h>

namespace font_manager {

/// @brief 加载用户配置中指定路径的字体并注册到进程字体表
void LoadUserFonts();

/// @brief 反注册全部已加载的用户字体
void UnloadUserFonts();

/// @brief 获取当前已加载字体文件列表
std::vector<agi::fs::path> GetLoadedFontFiles();

/// @brief 设置验证探针日志开关
void SetProbeEnabled(bool enable);

} // namespace font_manager
