/// @file theme.cpp
/// @brief 深色模式主题工具函数实现
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

#include "theme.h"

#include "compat.h"
#include "options.h"

#include <libaegisub/exception.h>
#include <libaegisub/log.h>

#include <wx/app.h>
#include <wx/panel.h>
#include <wx/settings.h>

/// 颜色配置路径前缀
static const std::string kColourPrefix = "Colour/";
/// 深色模式颜色配置路径前缀
static const std::string kDarkColourPrefix = "Colour/Dark/";

/// @brief 将颜色配置路径映射为深色模式路径
/// @param path 原始路径 (如 "Colour/Subtitle Grid/Standard")
/// @return 深色路径 (如 "Colour/Dark/Subtitle Grid/Standard")，
///         若路径不以 "Colour/" 开头则返回原路径
static std::string MapToDarkPath(const std::string& path) {
	if (path.compare(0, kColourPrefix.size(), kColourPrefix) == 0) {
		return kDarkColourPrefix + path.substr(kColourPrefix.size());
	}
	return path;
}

bool IsDarkMode() {
	return OPT_GET("App/Dark Mode")->GetBool();
}

wxColour GetThemeColour(const std::string& path) {
	if (IsDarkMode()) {
		std::string darkPath = MapToDarkPath(path);
		try {
			return to_wx(OPT_GET(darkPath.c_str())->GetColor());
		} catch (const agi::InternalError&) {
			LOG_W("theme/colour") << "Dark path not found, falling back to light: " << darkPath;
		}
	}
	return to_wx(OPT_GET(path.c_str())->GetColor());
}

const agi::OptionValue* GetThemeOptValue(const std::string& path) {
	if (IsDarkMode()) {
		std::string darkPath = MapToDarkPath(path);
		try {
			return OPT_GET(darkPath.c_str());
		} catch (const agi::InternalError&) {
			LOG_W("theme/option") << "Dark path not found, falling back to light: " << darkPath;
		}
	}
	return OPT_GET(path.c_str());
}

wxColour GetSemanticErrorColour() {
	return IsDarkMode() ? wxColour(255, 80, 80) : wxColour(255, 0, 0);
}

wxColour GetSemanticSuccessColour() {
	return IsDarkMode() ? wxColour(80, 220, 80) : wxColour(0, 128, 0);
}

wxColour GetSemanticWarningColour() {
	return IsDarkMode() ? wxColour(255, 160, 50) : wxColour(200, 100, 0);
}

wxColour GetSemanticErrorBgColour() {
	return IsDarkMode() ? wxColour(80, 30, 30) : wxColour(255, 128, 128);
}

wxColour GetSemanticWarningBgColour() {
	return IsDarkMode() ? wxColour(80, 60, 20) : wxColour(255, 255, 128);
}

void ApplyDarkThemeToWindow(wxWindow* window) {
	if (!window || !IsDarkMode()) return;

	if (dynamic_cast<wxPanel*>(window)) {
		window->SetBackgroundColour(
			wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
		window->SetForegroundColour(
			wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
	}

	for (auto* child : window->GetChildren()) {
		ApplyDarkThemeToWindow(child);
	}
}

void InitDarkThemeHook() {
	if (!IsDarkMode()) return;

	/// wxPanel 的 WM_ERASEBKGND 处理依赖 MSWGetBgBrush()，
	/// 该方法仅在 m_hasBgCol 为 true 时返回深色画刷。
	/// 通过 wxEVT_CREATE 钩子在面板创建时显式设置背景色和前景色：
	/// - 背景色确保面板使用深色背景绘制
	/// - 前景色确保子控件继承正确的文字颜色，避免初次渲染时文字不可见
	wxTheApp->Bind(wxEVT_CREATE, [](wxWindowCreateEvent& evt) {
		wxWindow* win = evt.GetWindow();
		if (win && dynamic_cast<wxPanel*>(win)) {
			win->SetBackgroundColour(
				wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
			win->SetForegroundColour(
				wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
		}
		evt.Skip();
	});
}
