// Copyright (c) 2026, Aegisub Project
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

/// @file font_list_load_mode.h
/// @brief 字体列表加载模式判定共享实现
/// @ingroup secondary_ui

#pragma once

/// @brief 字体列表加载模式
/// @details 先使用调用方直接提供的字体列表；若仅有异步来源，则等待异步补齐；
///          两者都没有时再回退到同步枚举。
enum class DeferredFontListLoadMode {
	UseProvidedList,
	EnumerateSynchronously,
	WaitForAsyncList,
};

using FontChooserFontListLoadMode = DeferredFontListLoadMode;
using StyleEditorFontListLoadMode = DeferredFontListLoadMode;

/// @brief 解析字体列表加载策略
/// @param hasProvidedFontList 是否已直接提供字体列表
/// @param hasDeferredFontList 是否提供异步字体来源
/// @return 对应的字体列表加载模式
inline DeferredFontListLoadMode ResolveDeferredFontListLoadMode(bool hasProvidedFontList, bool hasDeferredFontList) {
	if (hasProvidedFontList)
		return DeferredFontListLoadMode::UseProvidedList;
	if (hasDeferredFontList)
		return DeferredFontListLoadMode::WaitForAsyncList;
	return DeferredFontListLoadMode::EnumerateSynchronously;
}

/// @brief 解析字体对话框的字体列表加载策略
inline FontChooserFontListLoadMode ResolveFontChooserFontListLoadMode(bool hasProvidedFontList, bool hasDeferredFontList) {
	return ResolveDeferredFontListLoadMode(hasProvidedFontList, hasDeferredFontList);
}

/// @brief 解析样式编辑器的字体列表加载策略
inline StyleEditorFontListLoadMode ResolveStyleEditorFontListLoadMode(bool hasProvidedFontList, bool hasDeferredFontList) {
	return ResolveDeferredFontListLoadMode(hasProvidedFontList, hasDeferredFontList);
}
