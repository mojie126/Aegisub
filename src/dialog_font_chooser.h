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

/// @file dialog_font_chooser.h
/// @brief 自定义字体选择对话框，复刻系统字体对话框外观，支持子串搜索和字体预览
/// @ingroup secondary_ui

#pragma once

#include <future>
#include <map>
#include <vector>

#include <wx/dialog.h>
#include <wx/font.h>
#include <wx/arrstr.h>
#include <wx/timer.h>
#include <wx/vlbox.h>

/// @brief 获取用于列表预览的字体名称
/// @param font_name 原始字体名称
/// @return 去除 @ 前缀后的可预览字体名称
wxString GetFontPreviewFaceName(const wxString &font_name);

/// @brief 对字体列表排序，使非 @ 字体优先，@ 字体排在后面
/// @param font_list 待排序的字体列表
void SortFontFaceList(wxArrayString &font_list);

/// @brief 获取带收藏字体优先顺序的字体列表
/// @return 已按收藏和普通排序整理后的字体列表
wxArrayString GetPreferredFontFaceList();

/// @brief 异步获取当前偏好顺序的字体列表
/// @return 可供调用方等待或轮询的共享字体列表 future
/// @details 系统字体枚举本身已有静态缓存；此处每次重新封装异步任务，
///          以便收藏字体顺序变化能即时反映到新打开的对话框中。
std::shared_future<wxArrayString> GetPreferredFontFaceListAsync();

/// @brief 记录一个收藏字体
/// @param font_name 字体名称
void RecordFavoriteFontFace(const wxString &font_name);

/// @brief 获取收藏字体列表
/// @return 收藏字体名称列表
wxArrayString GetFavoriteFontList();

class wxTextCtrl;
class wxListBox;
class wxCheckBox;
class wxStaticText;
class wxPanel;

/// @class FontPreviewListBox
/// @brief 自绘字体列表控件，每个字体条目用该字体自身的字样渲染
class FontPreviewListBox final : public wxVListBox {
public:
	FontPreviewListBox(wxWindow *parent, wxWindowID id,
		const wxPoint &pos = wxDefaultPosition,
		const wxSize &size = wxDefaultSize);

	/// @brief 设置字体列表内容
	void SetFonts(const wxArrayString &fonts);

	/// @brief 配置列表预览文本
	/// @param usePreviewText 是否使用示例文本作为列表预览内容
	/// @param previewText 示例文本内容
	/// @details 仅在预览文本真正影响条目布局时才清空行高缓存，避免关闭便捷预览时
	///          因输入框文本变化而重测整张字体列表。
	void ConfigurePreviewText(bool usePreviewText, const wxString &previewText);

	/// @brief 预热当前选择附近的字体度量缓存，降低首次滚动卡顿
	/// @param anchorRow 优先预热的锚点行，传 wxNOT_FOUND 时从列表起始处开始
	void PrepareForSmoothScroll(int anchorRow = wxNOT_FOUND);

	/// @brief 获取指定位置的字体名称
	wxString GetFontName(size_t n) const;

	/// @brief 获取当前列表中的字体数量
	size_t GetFontCount() const { return fonts_.size(); }

	/// @brief 查找字体名称的索引
	/// @return 找到返回索引，否则返回 wxNOT_FOUND
	int FindString(const wxString &s) const;

private:
	struct PreviewItemMetrics {
		int itemHeight = 0;   ///< 条目行高
		int textBoxHeight = 0; ///< 文本占用高度
	};

	wxArrayString fonts_;
	int itemHeight_ = 0;  ///< 缓存的行高（DPI 感知）
	int previewFontSize_ = 0; ///< 缓存的预览字号
	bool usePreviewText_ = false; ///< 是否使用示例文本作为列表预览内容
	wxString previewText_ = wxS("AaBbYyZz"); ///< 列表预览文本
	mutable std::map<wxString, wxFont> fontCache_; ///< 缓存已创建的预览字体，减少滚动时重复创建开销
	mutable std::map<wxString, PreviewItemMetrics> itemMetricsCache_; ///< 缓存按字体实测得到的条目高度与文本尺寸
	wxTimer metricWarmupTimer_;       ///< 分批预热字体度量，避免首次滚动集中测量
	std::vector<size_t> metricWarmupOrder_; ///< 当前预热顺序
	size_t metricWarmupCursor_ = 0;   ///< 当前预热进度

	wxString GetDisplayText(const wxString &fontName) const;
	const wxFont &GetPreviewFont(const wxString &previewFace) const;
	const PreviewItemMetrics &GetPreviewMetrics(const wxString &fontName) const;
	int MeasureItemHeight(const wxString &fontName) const;
	void ResetPreviewMetricsCache();
	void StartMetricWarmup(int anchorRow);
	void WarmMetricBatch(size_t batchSize);
	void OnMetricWarmupTimer(wxTimerEvent &event);

	void OnDrawItem(wxDC &dc, const wxRect &rect, size_t n) const override;
	wxCoord OnMeasureItem(size_t n) const override;
};

/// @class DialogFontChooser
/// @brief 自定义字体选择对话框
///
/// 复刻Windows系统字体对话框的布局和功能，增强字体名称搜索为子串匹配。
/// 三列布局：字体名称、大小/字形组合列、收藏字体，底部为效果区和预览区。
class DialogFontChooser final : public wxDialog {
public:
	struct FontStyleEntry {
		wxString name;
		int weight = wxFONTWEIGHT_NORMAL;
		int style = wxFONTSTYLE_NORMAL;
		int lfWeight = 400;   ///< 实际字体粗细值（100-900）
		int stretch = 5;      ///< 字体宽度（DWRITE_FONT_STRETCH，1-9，5为正常）
	};

	/// @brief 构造函数
	/// @param parent 父窗口
	/// @param initial 初始字体
	/// @param fontList 可用字体名称列表（为空时自动枚举系统字体）
	DialogFontChooser(wxWindow *parent, const wxFont &initial, const wxArrayString &fontList = wxArrayString(),
		std::shared_future<wxArrayString> deferredFontList = {});
	~DialogFontChooser();

	/// @brief 获取用户选择的字体
	/// @return 用户最终选择的字体对象
	wxFont GetSelectedFont() const;

private:
	wxArrayString allFonts_;          ///< 完整字体名称列表
	wxArrayString allFontLowerNames_; ///< 完整字体名称列表的小写缓存
	wxArrayString allPreviewLowerNames_; ///< 去除 @ 前缀后的字体名称小写缓存
	wxArrayString filteredFonts_;     ///< 过滤后的字体名称列表
	std::vector<size_t> filteredFontIndices_; ///< 过滤结果对应的原始字体索引

	wxTextCtrl *fontNameInput_;       ///< 字体名称搜索输入框
	wxCheckBox *quickPreviewCheck_;   ///< 便捷预览复选框
	FontPreviewListBox *fontNameList_; ///< 字体名称列表（自绘预览）
	wxTextCtrl *fontStyleInput_;      ///< 字体样式输入框
	wxListBox *fontStyleList_;        ///< 字体样式列表
	wxTextCtrl *fontSizeInput_;       ///< 字体大小输入框
	wxListBox *fontSizeList_;         ///< 字体大小列表
	wxCheckBox *strikeoutCheck_;      ///< 删除线复选框
	wxCheckBox *underlineCheck_;      ///< 下划线复选框
	wxTextCtrl *previewInput_;        ///< 预览文本输入框
	wxPanel *previewPanel_;           ///< 预览面板
	wxStaticText *previewText_;       ///< 预览文字
	FontPreviewListBox *favoriteFontList_; ///< 收藏字体列表
	std::vector<FontStyleEntry> fontStyleEntries_; ///< 当前字体可用的字形列表
	wxString currentStyleFace_;          ///< 当前已填充字形列表对应的字体名称
	bool fontNameFilterDirty_ = false;  ///< 延迟字体列表到达前用户是否已主动修改过滤条件
	std::shared_future<wxArrayString> deferredFontList_; ///< 异步枚举中的字体列表
	wxTimer fontListTimer_;              ///< 轮询异步字体枚举结果，避免首次打开时阻塞 UI

	wxFont selectedFont_;             ///< 当前选中的字体

	/// @brief 填充字体样式列表
	void PopulateStyleList(const wxString &faceName = wxString());

	/// @brief 根据当前输入解析实际使用的字体名称
	/// @return 优先使用当前列表选中项，否则回退到输入框内容
	wxString GetEffectiveFaceName() const;

	/// @brief 重建字体搜索缓存
	void RebuildFontSearchCache();
	void StartDeferredFontListLoad();

	/// @brief 在当前过滤结果中查找最佳匹配字体
	/// @param input 用户输入的字体名称
	/// @return 最佳匹配索引；若无更佳匹配则回退到当前过滤结果的第 0 项
	int FindBestFilteredFontMatchIndex(const wxString &input) const;

	/// @brief 根据字体对象选择匹配的字形项
	/// @param font 需要匹配的字体对象
	void SelectMatchingStyle(const wxFont &font);

	/// @brief 根据搜索文本过滤字体名称列表
	void FilterFontList();

	/// @brief 刷新收藏字体列表并尽量保持指定字体的选中状态
	/// @param selectedFont 刷新后优先选中的字体名称
	void RefreshFavoriteFontList(const wxString &selectedFont = wxString());

	/// @brief 根据当前控件状态更新预览
	void UpdatePreview();

	/// @brief 从控件状态构建字体对象
	/// @return 根据当前控件选择构建的字体
	wxFont BuildFontFromControls() const;

	// 事件处理
	void OnFontNameInput(wxCommandEvent &event);
	void OnFontNameSelected(wxCommandEvent &event);
	void OnFontStyleSelected(wxCommandEvent &event);
	void OnFontSizeInput(wxCommandEvent &event);
	void OnFontSizeSelected(wxCommandEvent &event);
	void OnDeferredFontListTimer(wxTimerEvent &event);
	void OnEffectChanged(wxCommandEvent &event);
	void OnFavoriteFontSelected(wxCommandEvent &event);
};

/// @brief 显示自定义字体选择对话框
/// @param parent 父窗口
/// @param initial 初始字体
/// @param fontList 可用字体名称列表（为空时自动枚举系统字体）
/// @return 用户选择的字体，取消时返回无效字体（IsOk() == false）
wxFont GetFontFromUser(wxWindow *parent, const wxFont &initial, const wxArrayString &fontList = wxArrayString(),
	std::shared_future<wxArrayString> deferredFontList = {});
