/// @file theme.h
/// @brief 深色模式主题工具函数
/// @ingroup main_ui
///
// Copyright (c) 2025, Aegisub Contributors
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

#pragma once

#include <string>
#include <wx/colour.h>

namespace agi { class OptionValue; }

/// @brief 判断当前是否启用深色模式
bool IsDarkMode();

/// @brief 获取主题感知的配置颜色
/// @param path 颜色配置路径 (如 "Colour/Subtitle Grid/Background/Background")
/// @details 深色模式下将 "Colour/X" 映射到 "Colour/Dark/X" 并读取对应颜色值，
///          否则读取原始路径的颜色值
wxColour GetThemeColour(const std::string& path);

/// @brief 获取主题感知的配置值指针
/// @param path 配置路径 (如 "Colour/Subtitle/Syntax/Background/Normal")
/// @details 深色模式下将 "Colour/X" 映射到 "Colour/Dark/X" 并返回对应 OptionValue，
///          否则返回原始路径的 OptionValue。用于需要检查值类型的场景
const agi::OptionValue* GetThemeOptValue(const std::string& path);

/// @brief 获取深色模式感知的错误色（用于前景文字）
/// @return 浅色模式 rgb(255,0,0) / 深色模式 rgb(255,80,80)
wxColour GetSemanticErrorColour();

/// @brief 获取深色模式感知的成功色（用于前景文字）
/// @return 浅色模式 rgb(0,128,0) / 深色模式 rgb(80,220,80)
wxColour GetSemanticSuccessColour();

/// @brief 获取深色模式感知的警告色（用于前景文字）
/// @return 浅色模式 rgb(200,100,0) / 深色模式 rgb(255,160,50)
wxColour GetSemanticWarningColour();

/// @brief 获取深色模式感知的错误背景色（用于行/区域背景高亮）
/// @return 浅色模式 rgb(255,128,128) / 深色模式 rgb(80,30,30)
wxColour GetSemanticErrorBgColour();

/// @brief 获取深色模式感知的警告背景色（用于行/区域背景高亮）
/// @return 浅色模式 rgb(255,255,128) / 深色模式 rgb(80,60,20)
wxColour GetSemanticWarningBgColour();

class wxWindow;

/// @brief 初始化深色模式全局窗口创建钩子
/// @details 在应用初始化时调用，注册 wxEVT_CREATE 事件处理器，
///          自动为新创建的 wxPanel 设置深色背景色，
///          解决 wxPanel 的 WM_ERASEBKGND 不使用深色画刷的问题
void InitDarkThemeHook();

/// @brief 递归应用深色主题背景到窗口及其所有子窗口
/// @param window 目标窗口，对其中的 wxPanel 实例设置深色背景
void ApplyDarkThemeToWindow(wxWindow* window);

/// @brief 解析主题感知的颜色配置路径
/// @param path 原始颜色配置路径 (如 "Colour/Subtitle Grid/Standard")
/// @return 深色模式下返回已验证存在的深色路径 (如 "Colour/Dark/Subtitle Grid/Standard")，
///         若深色路径不存在或非深色模式则返回原路径
std::string ResolveThemeColourPath(const std::string& path);
