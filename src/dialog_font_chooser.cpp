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

/// @file dialog_font_chooser.cpp
/// @brief 自定义字体选择对话框实现
/// @ingroup secondary_ui

#include "dialog_font_chooser.h"

#include "compat.h"
#include "font_list_load_mode.h"
#include "ini.h"
#include "options.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <map>
#include <mutex>
#include <set>

#include <libaegisub/dispatch.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dcclient.h>
#include <wx/fontenum.h>
#include <wx/listbox.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/combobox.h>

#ifdef __WXMSW__
#include <wx/msw/wrapwin.h>
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")
#endif

/// 常用字体大小列表
static const int kFontSizes[] = {
	6, 7, 8, 9, 10, 11, 12, 14, 16, 18,
	20, 22, 24, 26, 28, 32, 36, 40, 44, 48,
	54, 60, 66, 72, 80, 88, 96
};

namespace {

/// @brief 获取收藏字体 INI 文件路径
std::string FavoriteFontIniPath() {
	return std::string(wxGetHomeDir()) + "/favoriteFont.ini";
}

/// @brief 获取已排序的系统字体名称列表（带线程安全的静态缓存）
wxArrayString GetSortedSystemFontFaceList() {
	static std::mutex cache_mutex;
	static bool initialized = false;
	static wxArrayString cached_fonts;

	std::lock_guard<std::mutex> lock(cache_mutex);
	if (!initialized) {
		cached_fonts = wxFontEnumerator::GetFacenames();
		SortFontFaceList(cached_fonts);
		initialized = true;
	}

	return cached_fonts;
}

/// @brief 获取已翻译的基本字形名称列表
wxArrayString GetTranslatedStyleNames() {
	wxArrayString styles;
	styles.Add(_("Regular"));
	styles.Add(_("Italic"));
	styles.Add(_("Bold"));
	styles.Add(_("Bold Italic"));
	return styles;
}

/// @brief 根据字体属性构建本地化的字形显示名称
/// @param dw_weight DirectWrite 字体粗细值（100-900）
/// @param dw_stretch DirectWrite 字体宽度值（1-9）
/// @param dw_style DirectWrite 字体样式（0=Normal, 1=Oblique, 2=Italic）
/// @return 本地化的字形名称
wxString ConstructStyleDisplayName(int dw_weight, int dw_stretch, int dw_style) {
	bool is_italic = (dw_style == 2);  // DWRITE_FONT_STYLE_ITALIC
	bool is_oblique = (dw_style == 1); // DWRITE_FONT_STYLE_OBLIQUE
	bool is_normal_weight = (dw_weight >= 350 && dw_weight < 500);

	// 宽度前缀
	wxString stretch_prefix;
	switch (dw_stretch) {
		case 1: stretch_prefix = _("Ultra Condensed"); break;
		case 2: stretch_prefix = _("Extra Condensed"); break;
		case 3: stretch_prefix = _("Condensed"); break;
		case 4: stretch_prefix = _("Semi Condensed"); break;
		case 6: stretch_prefix = _("Semi Expanded"); break;
		case 7: stretch_prefix = _("Expanded"); break;
		case 8: stretch_prefix = _("Extra Expanded"); break;
		case 9: stretch_prefix = _("Ultra Expanded"); break;
		default: break; // 5 = Normal，无前缀
	}

	// 常规样式特殊处理
	if (is_normal_weight && !is_italic && !is_oblique && stretch_prefix.empty())
		return _("Regular");

	// 粗细名称
	wxString weight_name;
	if (dw_weight < 200) weight_name = _("Thin");
	else if (dw_weight < 300) weight_name = _("Extra Light");
	else if (dw_weight < 350) weight_name = wxGetTranslation(wxS("Font style: Light"));
	else if (dw_weight < 500) weight_name = wxEmptyString; // Regular/Normal 不显示粗细名
	else if (dw_weight < 600) weight_name = _("Medium");
	else if (dw_weight < 700) weight_name = _("Semibold");
	else if (dw_weight < 800) weight_name = _("Bold");
	else if (dw_weight < 900) weight_name = _("Extra Bold");
	else weight_name = _("Black");

	// 样式后缀
	wxString style_name;
	if (is_italic) style_name = _("Italic");
	else if (is_oblique) style_name = _("Oblique");

	// 组合：[宽度] [粗细] [样式]
	wxString result;
	if (!stretch_prefix.empty()) {
		result = stretch_prefix;
		if (!weight_name.empty()) result += " " + weight_name;
		if (!style_name.empty()) result += " " + style_name;
	} else if (!weight_name.empty()) {
		result = weight_name;
		if (!style_name.empty()) result += " " + style_name;
	} else {
		result = style_name;
	}

	return result.empty() ? _("Regular") : result;
}

/// @brief 获取默认的四种基本字形条目（Regular/Italic/Bold/Bold Italic）
std::vector<DialogFontChooser::FontStyleEntry> GetDefaultStyleEntries() {
	const wxArrayString style_names = GetTranslatedStyleNames();
	return {
		{ style_names[0], wxFONTWEIGHT_NORMAL, wxFONTSTYLE_NORMAL, 400, 5 },
		{ style_names[1], wxFONTWEIGHT_NORMAL, wxFONTSTYLE_ITALIC, 400, 5 },
		{ style_names[2], wxFONTWEIGHT_BOLD, wxFONTSTYLE_NORMAL, 700, 5 },
		{ style_names[3], wxFONTWEIGHT_BOLD, wxFONTSTYLE_ITALIC, 700, 5 },
	};
}

/// @brief 对字形条目列表按粗细和样式排序
/// @param entries 待排序的字形条目列表
void SortStyleEntries(std::vector<DialogFontChooser::FontStyleEntry> &entries) {
	std::stable_sort(entries.begin(), entries.end(), [](const auto &lhs, const auto &rhs) {
		if (lhs.lfWeight != rhs.lfWeight)
			return lhs.lfWeight < rhs.lfWeight;
		if (lhs.stretch != rhs.stretch)
			return lhs.stretch < rhs.stretch;
		return lhs.style < rhs.style;
	});
}

#ifdef __WXMSW__
/// @brief 将 DirectWrite 粗细值映射为 wxFontWeight
static int DWriteWeightToWx(int dw_weight) {
	if (dw_weight >= 700) return wxFONTWEIGHT_BOLD;
	return wxFONTWEIGHT_NORMAL;
}

struct SharedDWriteResources {
	IDWriteFactory *factory = nullptr;
	IDWriteFontCollection *collection = nullptr;

	~SharedDWriteResources() {
		if (collection)
			collection->Release();
		if (factory)
			factory->Release();
	}
};

SharedDWriteResources &GetSharedDWriteResourceStorage() {
	static SharedDWriteResources resources;
	return resources;
}

/// @brief 获取共享的 DirectWrite 工厂与系统字体集合
SharedDWriteResources &GetSharedDWriteResources() {
	static std::once_flag init_flag;

	std::call_once(init_flag, []() {
		SharedDWriteResources &resources = GetSharedDWriteResourceStorage();
		IDWriteFactory *factory = nullptr;
		HRESULT hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
			__uuidof(IDWriteFactory), reinterpret_cast<IUnknown **>(&factory));
		if (FAILED(hr) || !factory)
			return;

		IDWriteFontCollection *collection = nullptr;
		hr = factory->GetSystemFontCollection(&collection, FALSE);
		if (FAILED(hr) || !collection) {
			factory->Release();
			return;
		}

		resources.factory = factory;
		resources.collection = collection;
	});

	return GetSharedDWriteResourceStorage();
}

/// @brief 使用 DirectWrite 枚举指定字体族的所有字形
///
/// 枚举与 face_name 精确匹配的字体族中所有非模拟字形。
/// 显示名称通过 weight/stretch/style 属性使用 gettext 翻译构建，
/// 因为 DirectWrite 返回的字体面名通常只有英文（中文本地化由 GDI 子系统提供）。
std::vector<DialogFontChooser::FontStyleEntry> GetFontStyleEntriesForFace(const wxString &face_name) {
	if (face_name.empty())
		return GetDefaultStyleEntries();

	static std::mutex cache_mutex;
	static std::map<std::wstring, std::vector<DialogFontChooser::FontStyleEntry>> cache;
	const std::wstring cache_key = std::wstring(face_name.Lower().wc_str());
	{
		std::lock_guard<std::mutex> lock(cache_mutex);
		auto it = cache.find(cache_key);
		if (it != cache.end())
			return it->second;
	}

	SharedDWriteResources &dwrite = GetSharedDWriteResources();
	if (!dwrite.factory || !dwrite.collection) {
		auto entries = GetDefaultStyleEntries();
		std::lock_guard<std::mutex> lock(cache_mutex);
		cache[cache_key] = entries;
		return entries;
	}

	std::vector<DialogFontChooser::FontStyleEntry> entries;
	std::set<std::wstring> seen;

	/// @brief 枚举指定字体族中的所有非模拟字形
	auto enumerate_family = [&](IDWriteFontFamily *family) {
		UINT32 font_count = family->GetFontCount();
		for (UINT32 i = 0; i < font_count; ++i) {
			IDWriteFont *font = nullptr;
			if (FAILED(family->GetFont(i, &font)) || !font)
				continue;

			// 跳过模拟（合成）字形
			if (font->GetSimulations() != DWRITE_FONT_SIMULATIONS_NONE) {
				font->Release();
				continue;
			}

			DWRITE_FONT_WEIGHT w = font->GetWeight();
			DWRITE_FONT_STYLE s = font->GetStyle();
			DWRITE_FONT_STRETCH st = font->GetStretch();

			// 去重键：权重|宽度|样式
			const std::wstring key = std::to_wstring(w) + L"|" + std::to_wstring(st) + L"|" + std::to_wstring(s);
			if (!seen.insert(key).second) {
				font->Release();
				continue;
			}

			DialogFontChooser::FontStyleEntry entry;
			entry.name = ConstructStyleDisplayName(static_cast<int>(w), static_cast<int>(st), static_cast<int>(s));
			entry.lfWeight = static_cast<int>(w);
			entry.weight = DWriteWeightToWx(static_cast<int>(w));
			entry.style = (s != DWRITE_FONT_STYLE_NORMAL) ? wxFONTSTYLE_ITALIC : wxFONTSTYLE_NORMAL;
			entry.stretch = static_cast<int>(st);
			entries.push_back(entry);

			font->Release();
		}
	};

	// 枚举精确匹配的字体族
	UINT32 exact_index = 0;
	BOOL exact_exists = FALSE;
	if (SUCCEEDED(dwrite.collection->FindFamilyName(face_name.wc_str(), &exact_index, &exact_exists)) && exact_exists) {
		IDWriteFontFamily *family = nullptr;
		if (SUCCEEDED(dwrite.collection->GetFontFamily(exact_index, &family)) && family) {
			enumerate_family(family);
			family->Release();
		}
	}

	if (entries.empty())
		entries = GetDefaultStyleEntries();

	SortStyleEntries(entries);

	// 检测重复显示名称，追加字重值以区分
	std::map<wxString, int> name_count;
	for (const auto &e : entries)
		name_count[e.name]++;
	for (auto &e : entries) {
		if (name_count[e.name] > 1)
			e.name = wxString::Format("%s (%d)", e.name, e.lfWeight);
	}

	{
		std::lock_guard<std::mutex> lock(cache_mutex);
		cache[cache_key] = entries;
	}
	return entries;
}
#else
std::vector<DialogFontChooser::FontStyleEntry> GetFontStyleEntriesForFace(const wxString &) {
	return GetDefaultStyleEntries();
}
#endif

/// @brief 判断字体名是否为 @ 前缀竖排字体
bool IsAtFontFace(const wxString &font_name) {
	return !font_name.empty() && font_name[0] == '@';
}

/// @brief 构建列表中显示的字体名称（预留扩展点）
wxString MakeDisplayFontName(const wxString &font_name) {
	return font_name;
}

/// @brief 归一化字体列表的预览文本
/// @param previewText 输入的示例文本
/// @return 非空示例文本，若为空则返回默认预览文本
wxString NormalizePreviewText(const wxString &previewText) {
	return previewText.empty() ? wxString(wxS("AaBbYyZz")) : previewText;
}

/// @brief 判断是否需要刷新字体列表布局
/// @details 关闭便捷预览时，示例文本不会参与列表绘制，因此仅修改示例文本不应触发行高重算。
bool ShouldRefreshPreviewLayout(bool previousUsePreviewText, const wxString &previousPreviewText,
	bool currentUsePreviewText, const wxString &currentPreviewText) {
	if (previousUsePreviewText != currentUsePreviewText)
		return true;
	if (!currentUsePreviewText)
		return false;
	return previousPreviewText != currentPreviewText;
}

/// @brief 构建字体列表条目的显示文本
/// @param font_name 当前条目的字体名称
/// @param previewText 示例文本
/// @param usePreviewText 是否启用便捷预览
/// @return 实际绘制到字体列表中的文本；便捷预览时追加原始字体名称以保留 @ 区分
wxString MakeFontListItemText(const wxString &font_name, const wxString &previewText, bool usePreviewText) {
	if (!usePreviewText)
		return MakeDisplayFontName(font_name);

	return wxString::Format("%s (%s)", NormalizePreviewText(previewText), MakeDisplayFontName(font_name));
}

/// @brief 计算字体列表条目的推荐高度
/// @param minimumHeight 基础最小高度
/// @param textBoxHeight 实际文本占用高度
/// @param verticalPadding 额外垂直留白
/// @return 最终条目高度
int CalculatePreviewItemHeight(int minimumHeight, int textBoxHeight, int verticalPadding) {
	return std::max(minimumHeight, textBoxHeight + verticalPadding);
}

/// @brief 计算字体列表文本的绘制起始 Y 坐标
/// @param rectY 条目区域起始 Y 坐标
/// @param rectHeight 条目区域高度
/// @param textBoxHeight 实际文本占用高度
/// @param topPadding 文本超高时的顶部留白
/// @return 文本绘制起始 Y 坐标
int CalculatePreviewTextY(int rectY, int rectHeight, int textBoxHeight, int topPadding) {
	if (textBoxHeight >= rectHeight)
		return rectY + topPadding;
	return rectY + (rectHeight - textBoxHeight) / 2;
}

constexpr size_t kInitialMetricWarmupCount = 24;
constexpr size_t kMetricWarmupBatchSize = 48;
constexpr size_t kMetricWarmupPriorityRadius = 64;

void AppendMetricWarmupIndex(std::vector<size_t> &order, std::vector<unsigned char> &seen, size_t index) {
	if (index >= seen.size() || seen[index])
		return;
	seen[index] = 1;
	order.push_back(index);
}

/// @brief 构建字体度量预热顺序
/// @details 先预热当前选择附近的条目，再补齐剩余条目，降低首次拖动滚动条时的集中测量成本。
std::vector<size_t> BuildFontMetricWarmupOrder(size_t itemCount, size_t anchorIndex, size_t priorityRadius) {
	std::vector<size_t> order;
	order.reserve(itemCount);
	if (itemCount == 0)
		return order;

	anchorIndex = std::min(anchorIndex, itemCount - 1);
	std::vector<unsigned char> seen(itemCount, 0);
	AppendMetricWarmupIndex(order, seen, anchorIndex);

	for (size_t delta = 1; delta <= priorityRadius; ++delta) {
		if (anchorIndex + delta < itemCount)
			AppendMetricWarmupIndex(order, seen, anchorIndex + delta);
		if (anchorIndex >= delta)
			AppendMetricWarmupIndex(order, seen, anchorIndex - delta);
	}

	for (size_t i = 0; i < itemCount; ++i)
		AppendMetricWarmupIndex(order, seen, i);

	return order;
}

} // namespace

wxString GetFontPreviewFaceName(const wxString &font_name) {
	if (IsAtFontFace(font_name))
		return font_name.Mid(1);
	return font_name;
}

wxArrayString GetFavoriteFontList() {
	const int favorite_font_num = OPT_GET("Subtitle/Favorite Font Number")->GetInt();
	if (favorite_font_num <= 0)
		return {};

	mINI::INIFile favorite_file(FavoriteFontIniPath());
	mINI::INIStructure favorite_ini;
	favorite_file.read(favorite_ini);

	wxArrayString favorites;
	int count = 0;
	for (const auto &[section, values] : favorite_ini) {
		for (auto it = values.rbegin(); it != values.rend(); ++it) {
			const std::string favorite_value = it->second;
			const wxString favorite_font = to_wx(favorite_value);
			if (favorite_font.empty())
				continue;
			if (favorites.Index(favorite_font, false) == wxNOT_FOUND)
				favorites.Add(favorite_font);
			++count;
			if (count >= favorite_font_num)
				break;
		}
		if (count >= favorite_font_num)
			break;
	}
	return favorites;
}

wxArrayString GetPreferredFontFaceList() {
	wxArrayString font_list = GetSortedSystemFontFaceList();

	wxArrayString favorites = GetFavoriteFontList();

	for (int i = static_cast<int>(favorites.size()) - 1; i >= 0; --i) {
		const wxString &favorite = favorites[static_cast<size_t>(i)];
		int idx = font_list.Index(favorite, false);
		if (idx != wxNOT_FOUND)
			font_list.RemoveAt(static_cast<size_t>(idx));
		font_list.Insert(favorite, 0);
	}

	return font_list;
}

std::shared_future<wxArrayString> GetPreferredFontFaceListAsync() {
	// 系统字体枚举由 GetSortedSystemFontFaceList() 做静态缓存，这里仅异步封装
	// “收藏字体优先排序”这一步，避免收藏发生变化后继续复用过期结果。
	auto promise = std::make_shared<std::promise<wxArrayString>>();
	auto future = promise->get_future().share();
	agi::dispatch::Background().Async([promise]() {
		try {
			promise->set_value(GetPreferredFontFaceList());
		}
		catch (...) {
			try {
				promise->set_exception(std::current_exception());
			}
			catch (...) {
			}
		}
	});
	return future;
}

void RecordFavoriteFontFace(const wxString &font_name) {
	if (font_name.empty())
		return;

	const int favorite_font_num = OPT_GET("Subtitle/Favorite Font Number")->GetInt();
	if (favorite_font_num <= 0)
		return;

	mINI::INIFile favorite_file(FavoriteFontIniPath());
	mINI::INIStructure favorite_ini;
	favorite_file.read(favorite_ini);

	auto &favorites = favorite_ini["favoriteFont"];
	const std::string key = from_wx(font_name);
	// 已存在则先移除再重新插入，以刷新最近使用顺序
	if (favorites.has(key))
		favorites.remove(key);
	while (static_cast<int>(favorites.size()) >= favorite_font_num && favorites.size() > 0) {
		favorites.remove(favorites.begin()->first);
	}
	favorites[key] = key;
	favorite_file.write(favorite_ini);
}

void SortFontFaceList(wxArrayString &font_list) {
	std::vector<wxString> fonts;
	fonts.reserve(font_list.size());
	for (const auto &font : font_list)
		fonts.push_back(font);

	std::stable_sort(fonts.begin(), fonts.end(), [](const wxString &lhs, const wxString &rhs) {
		const bool lhs_at = IsAtFontFace(lhs);
		const bool rhs_at = IsAtFontFace(rhs);
		if (lhs_at != rhs_at)
			return !lhs_at;

		const int preview_cmp = GetFontPreviewFaceName(lhs).CmpNoCase(GetFontPreviewFaceName(rhs));
		if (preview_cmp != 0)
			return preview_cmp < 0;

		return lhs.CmpNoCase(rhs) < 0;
	});

	font_list.Clear();
	for (const auto &font : fonts)
		font_list.Add(font);
}

// ======================================================================
// FontPreviewListBox 实现
// ======================================================================

FontPreviewListBox::FontPreviewListBox(wxWindow *parent, wxWindowID id,
	const wxPoint &pos, const wxSize &size)
: wxVListBox(parent, id, pos, size, wxBORDER_SUNKEN)
, metricWarmupTimer_(this)
{
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
	SetBackgroundStyle(wxBG_STYLE_PAINT);
	SetDoubleBuffered(true);
	Bind(wxEVT_TIMER, &FontPreviewListBox::OnMetricWarmupTimer, this, metricWarmupTimer_.GetId());
	// 预览字体大小基于系统 DPI，行高为字体高度的 2.2 倍以保证可读性
	wxClientDC dc(this);
	wxFont sysFont = GetFont();
	previewFontSize_ = sysFont.GetPointSize() + 6;
	dc.SetFont(wxFont(previewFontSize_, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
	itemHeight_ = static_cast<int>(dc.GetCharHeight() * 2.2);
}

void FontPreviewListBox::SetFonts(const wxArrayString &fonts) {
	fonts_ = fonts;
	ResetPreviewMetricsCache();
	SetItemCount(fonts_.size());
	PrepareForSmoothScroll(0);
	Refresh();
}

void FontPreviewListBox::ConfigurePreviewText(bool usePreviewText, const wxString &previewText) {
	const wxString normalizedPreviewText = NormalizePreviewText(previewText);
	const bool needsRefresh = ShouldRefreshPreviewLayout(
		usePreviewText_, previewText_, usePreviewText, normalizedPreviewText);

	usePreviewText_ = usePreviewText;
	previewText_ = normalizedPreviewText;
	// 未影响布局时只更新内部状态，避免快速输入示例文本时整张列表反复重测。
	if (!needsRefresh)
		return;
	ResetPreviewMetricsCache();
	SetItemCount(fonts_.size());
	PrepareForSmoothScroll(GetSelection());
	Refresh();
}

void FontPreviewListBox::PrepareForSmoothScroll(int anchorRow) {
	if (fonts_.empty())
		return;

	StartMetricWarmup(anchorRow);
	WarmMetricBatch(kInitialMetricWarmupCount);
	if (metricWarmupCursor_ < metricWarmupOrder_.size() && !metricWarmupTimer_.IsRunning())
		metricWarmupTimer_.Start(1);
}

wxString FontPreviewListBox::GetDisplayText(const wxString &fontName) const {
	return MakeFontListItemText(fontName, previewText_, usePreviewText_);
}

const wxFont &FontPreviewListBox::GetPreviewFont(const wxString &previewFace) const {
	auto it = fontCache_.find(previewFace);
	if (it == fontCache_.end()) {
		wxFont newFont(previewFontSize_, wxFONTFAMILY_DEFAULT, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL, false, previewFace);
		if (!newFont.IsOk())
			newFont = GetFont();
		it = fontCache_.emplace(previewFace, std::move(newFont)).first;
	}
	return it->second;
}

const FontPreviewListBox::PreviewItemMetrics &FontPreviewListBox::GetPreviewMetrics(const wxString &fontName) const {
	const wxString displayText = GetDisplayText(fontName);
	const wxString cacheKey = GetFontPreviewFaceName(fontName) + wxS("\x1f") + displayText;
	auto it = itemMetricsCache_.find(cacheKey);
	if (it != itemMetricsCache_.end())
		return it->second;

	wxClientDC dc(const_cast<FontPreviewListBox *>(this));
	dc.SetFont(GetPreviewFont(GetFontPreviewFaceName(fontName)));
	wxCoord textWidth = 0;
	wxCoord textHeight = 0;
	wxCoord descent = 0;
	wxCoord externalLeading = 0;
	dc.GetTextExtent(displayText, &textWidth, &textHeight, &descent, &externalLeading);

	if (textWidth <= 0 || textHeight <= 0) {
		dc.SetFont(GetFont());
		dc.GetTextExtent(displayText, &textWidth, &textHeight, &descent, &externalLeading);
	}

	PreviewItemMetrics metrics;
	metrics.textBoxHeight = static_cast<int>(textHeight + externalLeading);
	metrics.itemHeight = CalculatePreviewItemHeight(itemHeight_, metrics.textBoxHeight, FromDIP(6));
	return itemMetricsCache_.emplace(cacheKey, metrics).first->second;
}

int FontPreviewListBox::MeasureItemHeight(const wxString &fontName) const {
	return GetPreviewMetrics(fontName).itemHeight;
}

void FontPreviewListBox::ResetPreviewMetricsCache() {
	if (metricWarmupTimer_.IsRunning())
		metricWarmupTimer_.Stop();
	itemMetricsCache_.clear();
	metricWarmupOrder_.clear();
	metricWarmupCursor_ = 0;
}

void FontPreviewListBox::StartMetricWarmup(int anchorRow) {
	if (fonts_.empty()) {
		metricWarmupOrder_.clear();
		metricWarmupCursor_ = 0;
		return;
	}

	size_t anchorIndex = 0;
	if (anchorRow != wxNOT_FOUND && anchorRow >= 0)
		anchorIndex = static_cast<size_t>(std::min(anchorRow, static_cast<int>(fonts_.size()) - 1));
	metricWarmupOrder_ = BuildFontMetricWarmupOrder(fonts_.size(), anchorIndex, kMetricWarmupPriorityRadius);
	metricWarmupCursor_ = 0;
}

void FontPreviewListBox::WarmMetricBatch(size_t batchSize) {
	if (metricWarmupCursor_ >= metricWarmupOrder_.size())
		return;

	const size_t end = std::min(metricWarmupOrder_.size(), metricWarmupCursor_ + batchSize);
	for (; metricWarmupCursor_ < end; ++metricWarmupCursor_) {
		const size_t index = metricWarmupOrder_[metricWarmupCursor_];
		if (index < fonts_.size())
			GetPreviewMetrics(fonts_[index]);
	}
}

void FontPreviewListBox::OnMetricWarmupTimer(wxTimerEvent &) {
	WarmMetricBatch(kMetricWarmupBatchSize);
	if (metricWarmupCursor_ >= metricWarmupOrder_.size())
		metricWarmupTimer_.Stop();
}

wxString FontPreviewListBox::GetFontName(size_t n) const {
	if (n < fonts_.size())
		return fonts_[n];
	return wxEmptyString;
}

int FontPreviewListBox::FindString(const wxString &s) const {
	for (size_t i = 0; i < fonts_.size(); ++i) {
		if (fonts_[i].CmpNoCase(s) == 0)
			return static_cast<int>(i);
	}
	return wxNOT_FOUND;
}

void FontPreviewListBox::OnDrawItem(wxDC &dc, const wxRect &rect, size_t n) const {
	if (n >= fonts_.size()) return;

	const wxString &name = fonts_[n];
	const wxString preview_face = GetFontPreviewFaceName(name);
	const wxString display_name = GetDisplayText(name);
	const PreviewItemMetrics &metrics = GetPreviewMetrics(name);

	// 使用缓存的字体对象，避免滚动时反复创建 wxFont 导致卡顿
	dc.SetFont(GetPreviewFont(preview_face));

	// 选中项使用系统高亮色
	if (IsSelected(n)) {
		dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHTTEXT));
		dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_HIGHLIGHT)));
		dc.SetPen(*wxTRANSPARENT_PEN);
		dc.DrawRectangle(rect);
	} else {
		dc.SetTextForeground(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
	}

	// 使用缓存的文本高度进行垂直定位，避免首次滚动时重复测量。
	const int textY = CalculatePreviewTextY(rect.y, rect.height, metrics.textBoxHeight, FromDIP(3));
	dc.DrawText(display_name, rect.x + 4, textY);
}

wxCoord FontPreviewListBox::OnMeasureItem(size_t n) const {
	if (n >= fonts_.size())
		return itemHeight_;
	return MeasureItemHeight(fonts_[n]);
}

// ======================================================================
// DialogFontChooser 实现
// ======================================================================

DialogFontChooser::DialogFontChooser(wxWindow *parent, const wxFont &initial, const wxArrayString &fontList,
	std::shared_future<wxArrayString> deferredFontList)
: wxDialog(parent, wxID_ANY, _("Font"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, deferredFontList_(std::move(deferredFontList))
, fontListTimer_(this)
, selectedFont_(initial)
{
	// 构建字体名称列表
	const auto font_list_mode = ResolveFontChooserFontListLoadMode(!fontList.empty(), deferredFontList_.valid());
	switch (font_list_mode) {
		case FontChooserFontListLoadMode::UseProvidedList:
			allFonts_ = fontList;
			break;
		case FontChooserFontListLoadMode::WaitForAsyncList:
			allFonts_.Clear();
			break;
		case FontChooserFontListLoadMode::EnumerateSynchronously:
		default:
			allFonts_ = GetPreferredFontFaceList();
			break;
	}
	filteredFonts_ = allFonts_;
	RebuildFontSearchCache();

	auto *mainSizer = new wxBoxSizer(wxVERTICAL);

	// --- 上半部分：三列布局（字体名称、大小/字形组合列、收藏字体） ---
	auto *topSizer = new wxBoxSizer(wxHORIZONTAL);

	// 字体名称列
	auto *fontNameSizer = new wxBoxSizer(wxVERTICAL);
	fontNameSizer->Add(new wxStaticText(this, wxID_ANY, _("Font name(&F):")), 0, wxBOTTOM, 4);
	auto *fontNameInputSizer = new wxBoxSizer(wxHORIZONTAL);
	fontNameInput_ = new wxTextCtrl(this, wxID_ANY);
	fontNameInputSizer->Add(fontNameInput_, 1, wxEXPAND | wxRIGHT, 8);
	quickPreviewCheck_ = new wxCheckBox(this, wxID_ANY, _("Convenient preview"));
	quickPreviewCheck_->SetValue(false);
	quickPreviewCheck_->SetToolTip(_("Use the sample text followed by the font name for each entry in the font list."));
	fontNameInputSizer->Add(quickPreviewCheck_, 0, wxALIGN_CENTER_VERTICAL);
	fontNameSizer->Add(fontNameInputSizer, 0, wxEXPAND | wxBOTTOM, 4);
	fontNameList_ = new FontPreviewListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(500, 280)));
	fontNameList_->SetFonts(filteredFonts_);
	fontNameSizer->Add(fontNameList_, 1, wxEXPAND);
	topSizer->Add(fontNameSizer, 10, wxEXPAND | wxRIGHT, 10);

	// 大小/字形组合列（上大小，下字形）
	auto *fontOptionSizer = new wxBoxSizer(wxVERTICAL);
	fontOptionSizer->Add(new wxStaticText(this, wxID_ANY, _("&Size:")), 0, wxBOTTOM, 4);
	fontSizeInput_ = new wxTextCtrl(this, wxID_ANY);
	fontOptionSizer->Add(fontSizeInput_, 0, wxEXPAND | wxBOTTOM, 4);
	fontSizeList_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(150, 96)));
	for (int size : kFontSizes)
		fontSizeList_->Append(std::to_wstring(size));
	fontOptionSizer->Add(fontSizeList_, 2, wxEXPAND | wxBOTTOM, 10);

	fontOptionSizer->Add(new wxStaticText(this, wxID_ANY, _("Font st&yle:")), 0, wxBOTTOM, 4);
	fontStyleInput_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
	fontOptionSizer->Add(fontStyleInput_, 0, wxEXPAND | wxBOTTOM, 4);
	fontStyleList_ = new wxListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(150, 132)));
	PopulateStyleList(initial.IsOk() ? initial.GetFaceName() : wxString());
	fontOptionSizer->Add(fontStyleList_, 3, wxEXPAND);
	topSizer->Add(fontOptionSizer, 2, wxEXPAND | wxRIGHT, 10);

	// 收藏字体列
	auto *favoriteSizer = new wxBoxSizer(wxVERTICAL);
	favoriteSizer->Add(new wxStaticText(this, wxID_ANY, _("Fa&vorites:")), 0, wxBOTTOM, 4);
	const int fontNameInputRowHeight = std::max(fontNameInput_->GetBestSize().GetHeight(), quickPreviewCheck_->GetBestSize().GetHeight());
	favoriteSizer->AddSpacer(fontNameInputRowHeight + 4);
	favoriteFontList_ = new FontPreviewListBox(this, wxID_ANY, wxDefaultPosition, FromDIP(wxSize(260, 280)));
	RefreshFavoriteFontList();
	favoriteSizer->Add(favoriteFontList_, 1, wxEXPAND);
	topSizer->Add(favoriteSizer, 4, wxEXPAND);

	mainSizer->Add(topSizer, 2, wxEXPAND | wxALL, 10);

	// --- 下半部分：效果区和预览区（跟随对话框缩放） ---
	auto *bottomSizer = new wxBoxSizer(wxHORIZONTAL);

	// 效果区
	auto *effectsBox = new wxStaticBoxSizer(wxVERTICAL, this, _("Effects"));
	strikeoutCheck_ = new wxCheckBox(effectsBox->GetStaticBox(), wxID_ANY, _("Stri&keout"));
	underlineCheck_ = new wxCheckBox(effectsBox->GetStaticBox(), wxID_ANY, _("&Underline"));
	effectsBox->Add(strikeoutCheck_, 0, wxALL, 10);
	effectsBox->Add(underlineCheck_, 0, wxLEFT | wxRIGHT | wxBOTTOM, 10);
	bottomSizer->Add(effectsBox, 0, wxEXPAND | wxRIGHT, 10);

	// 预览区
	auto *previewBox = new wxStaticBoxSizer(wxVERTICAL, this, _("Sample"));
	const std::string preview_text = OPT_GET("Tool/Font Chooser/Preview Text")->GetString();
	previewInput_ = new wxTextCtrl(previewBox->GetStaticBox(), wxID_ANY,
		to_wx(preview_text));
	previewBox->Add(previewInput_, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);
	previewPanel_ = new wxPanel(previewBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, FromDIP(wxSize(-1, 100)));
	previewPanel_->SetBackgroundColour(*wxWHITE);
	previewText_ = new wxStaticText(previewPanel_, wxID_ANY, "AaBbYyZz",
		wxDefaultPosition, wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL | wxST_NO_AUTORESIZE);
	auto *previewPanelSizer = new wxBoxSizer(wxVERTICAL);
	previewPanelSizer->AddStretchSpacer();
	previewPanelSizer->Add(previewText_, 0, wxEXPAND);
	previewPanelSizer->AddStretchSpacer();
	previewPanel_->SetSizer(previewPanelSizer);
	previewBox->Add(previewPanel_, 1, wxEXPAND | wxALL, 10);
	bottomSizer->Add(previewBox, 1, wxEXPAND);

	mainSizer->Add(bottomSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

	auto *buttonSizer = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	auto *footerSizer = new wxBoxSizer(wxHORIZONTAL);
	footerSizer->AddStretchSpacer();
	footerSizer->Add(buttonSizer, 0, wxEXPAND, 0);
	mainSizer->Add(footerSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

	SetSizerAndFit(mainSizer);
	SetMinSize(FromDIP(wxSize(1000, 520)));
	SetSize(FromDIP(wxSize(1200, 680)));

	// --- 设置初始值 ---
	if (initial.IsOk()) {
		fontNameInput_->SetValue(initial.GetFaceName());
		fontSizeInput_->SetValue(std::to_wstring(initial.GetPointSize()));

		// 选中字体名称列表中的匹配项
		int idx = fontNameList_->FindString(initial.GetFaceName());
		if (idx != wxNOT_FOUND) {
			fontNameList_->SetSelection(idx);
		}

		// 选中字体大小列表中的匹配项
		int sizeIdx = fontSizeList_->FindString(std::to_wstring(initial.GetPointSize()));
		if (sizeIdx != wxNOT_FOUND)
			fontSizeList_->SetSelection(sizeIdx);

		// 设置样式
		SelectMatchingStyle(initial);

		// 设置效果
		strikeoutCheck_->SetValue(initial.GetStrikethrough());
		underlineCheck_->SetValue(initial.GetUnderlined());
	} else {
		fontSizeInput_->SetValue("36");
		int sizeIdx = fontSizeList_->FindString("36");
		if (sizeIdx != wxNOT_FOUND)
			fontSizeList_->SetSelection(sizeIdx);
		SelectMatchingStyle(BuildFontFromControls());
	}

	UpdatePreview();

	// 延迟滚动，确保列表布局完成后定位
	CallAfter([this]() {
		int sel = fontNameList_->GetSelection();
		if (sel != wxNOT_FOUND) {
			fontNameList_->ScrollToRow(sel);
			fontNameList_->PrepareForSmoothScroll(sel);
		}
		int sizeSel = fontSizeList_->GetSelection();
		if (sizeSel != wxNOT_FOUND)
			fontSizeList_->EnsureVisible(sizeSel);
	});

	// --- 绑定事件 ---
	fontNameInput_->Bind(wxEVT_TEXT, &DialogFontChooser::OnFontNameInput, this);
	fontNameInput_->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &evt) {
		fontNameInput_->CallAfter([this]() { fontNameInput_->SelectAll(); });
		evt.Skip();
	});
	fontNameList_->Bind(wxEVT_LISTBOX, &DialogFontChooser::OnFontNameSelected, this);
	fontStyleList_->Bind(wxEVT_LISTBOX, &DialogFontChooser::OnFontStyleSelected, this);
	fontSizeInput_->Bind(wxEVT_TEXT, &DialogFontChooser::OnFontSizeInput, this);
	fontSizeList_->Bind(wxEVT_LISTBOX, &DialogFontChooser::OnFontSizeSelected, this);
	Bind(wxEVT_TIMER, &DialogFontChooser::OnDeferredFontListTimer, this, fontListTimer_.GetId());
	strikeoutCheck_->Bind(wxEVT_CHECKBOX, &DialogFontChooser::OnEffectChanged, this);
	underlineCheck_->Bind(wxEVT_CHECKBOX, &DialogFontChooser::OnEffectChanged, this);
	quickPreviewCheck_->Bind(wxEVT_CHECKBOX, &DialogFontChooser::OnEffectChanged, this);
	previewInput_->Bind(wxEVT_TEXT, &DialogFontChooser::OnEffectChanged, this);
	favoriteFontList_->Bind(wxEVT_LISTBOX, &DialogFontChooser::OnFavoriteFontSelected, this);

	Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		OPT_SET("Tool/Font Chooser/Preview Text")->SetString(from_wx(previewInput_->GetValue()));
		selectedFont_ = BuildFontFromControls();
		EndModal(wxID_OK);
	}, wxID_OK);

	Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
		OPT_SET("Tool/Font Chooser/Preview Text")->SetString(from_wx(previewInput_->GetValue()));
		EndModal(wxID_CANCEL);
	}, wxID_CANCEL);

	if (font_list_mode == FontChooserFontListLoadMode::WaitForAsyncList)
		StartDeferredFontListLoad();
}

DialogFontChooser::~DialogFontChooser() {
	if (fontListTimer_.IsRunning())
		fontListTimer_.Stop();
}

wxFont DialogFontChooser::GetSelectedFont() const {
	return selectedFont_;
}

void DialogFontChooser::StartDeferredFontListLoad() {
	if (!deferredFontList_.valid())
		return;

	if (deferredFontList_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
		if (!fontListTimer_.IsRunning())
			fontListTimer_.Start(100);
		return;
	}

	allFonts_ = deferredFontList_.get();
	filteredFonts_ = allFonts_;
	RebuildFontSearchCache();

	if (fontNameFilterDirty_) {
		FilterFontList();
	}
	else {
		fontNameList_->SetFonts(filteredFonts_);
		int best_idx = fontNameList_->FindString(fontNameInput_->GetValue());
		if (best_idx == wxNOT_FOUND)
			best_idx = FindBestFilteredFontMatchIndex(fontNameInput_->GetValue());
		if (best_idx != wxNOT_FOUND) {
			fontNameList_->SetSelection(best_idx);
			fontNameList_->ScrollToRow(best_idx);
		}
		fontNameList_->PrepareForSmoothScroll(best_idx);
	}

	PopulateStyleList(GetEffectiveFaceName());
	SelectMatchingStyle(BuildFontFromControls());
	UpdatePreview();
}

void DialogFontChooser::PopulateStyleList(const wxString &faceName) {
	const wxString normalized_face = GetFontPreviewFaceName(faceName);
	if (fontStyleList_->GetCount() > 0 && normalized_face.CmpNoCase(currentStyleFace_) == 0)
		return;

	currentStyleFace_ = normalized_face;
	fontStyleEntries_ = GetFontStyleEntriesForFace(normalized_face);
	if (fontStyleEntries_.empty())
		fontStyleEntries_ = GetDefaultStyleEntries();

	fontStyleList_->Clear();
	for (const auto &entry : fontStyleEntries_)
		fontStyleList_->Append(entry.name);
}

wxString DialogFontChooser::GetEffectiveFaceName() const {
	const int sel = fontNameList_->GetSelection();
	if (sel != wxNOT_FOUND)
		return fontNameList_->GetFontName(static_cast<size_t>(sel));
	if (!filteredFonts_.empty()) {
		const int best_idx = FindBestFilteredFontMatchIndex(fontNameInput_->GetValue());
		if (best_idx != wxNOT_FOUND && static_cast<size_t>(best_idx) < filteredFonts_.size())
			return filteredFonts_[static_cast<size_t>(best_idx)];
	}
	return fontNameInput_->GetValue();
}

void DialogFontChooser::RebuildFontSearchCache() {
	allFontLowerNames_.Clear();
	allPreviewLowerNames_.Clear();
	filteredFontIndices_.clear();
	filteredFontIndices_.reserve(allFonts_.size());

	for (size_t i = 0; i < allFonts_.size(); ++i) {
		const wxString &font = allFonts_[i];
		allFontLowerNames_.Add(font.Lower());
		allPreviewLowerNames_.Add(GetFontPreviewFaceName(font).Lower());
		filteredFontIndices_.push_back(i);
	}
}

int DialogFontChooser::FindBestFilteredFontMatchIndex(const wxString &input) const {
	if (filteredFontIndices_.empty())
		return wxNOT_FOUND;

	const wxString input_lower = input.Lower();

	for (size_t i = 0; i < filteredFontIndices_.size(); ++i) {
		const size_t original_index = filteredFontIndices_[i];
		if (allFontLowerNames_[original_index] == input_lower)
			return static_cast<int>(i);
	}

	for (size_t i = 0; i < filteredFontIndices_.size(); ++i) {
		const size_t original_index = filteredFontIndices_[i];
		if (allPreviewLowerNames_[original_index] == input_lower)
			return static_cast<int>(i);
	}

	for (size_t i = 0; i < filteredFontIndices_.size(); ++i) {
		const size_t original_index = filteredFontIndices_[i];
		if (allFontLowerNames_[original_index].StartsWith(input_lower))
			return static_cast<int>(i);
	}

	for (size_t i = 0; i < filteredFontIndices_.size(); ++i) {
		const size_t original_index = filteredFontIndices_[i];
		if (allPreviewLowerNames_[original_index].StartsWith(input_lower))
			return static_cast<int>(i);
	}

	return 0;
}

void DialogFontChooser::SelectMatchingStyle(const wxFont &font) {
	if (fontStyleEntries_.empty())
		PopulateStyleList(font.GetFaceName());

	const int target_weight = font.GetNumericWeight();
	const int target_style = font.GetStyle();
	int best_index = 0;
	int best_diff = INT_MAX;
	for (size_t i = 0; i < fontStyleEntries_.size(); ++i) {
		const auto &entry = fontStyleEntries_[i];
		int diff = std::abs(entry.lfWeight - target_weight);
		if (entry.style != target_style)
			diff += 1000; // 样式不匹配则大幅惩罚
		// 优先选择 stretch 最接近 Normal(5) 的变体
		diff += std::abs(entry.stretch - 5) * 10;
		if (diff < best_diff) {
			best_diff = diff;
			best_index = static_cast<int>(i);
		}
	}
	fontStyleList_->SetSelection(best_index);
	fontStyleInput_->SetValue(fontStyleList_->GetString(best_index));
}

void DialogFontChooser::FilterFontList() {
	wxString input = fontNameInput_->GetValue().Lower();

	filteredFonts_.Clear();
	filteredFontIndices_.clear();
	if (input.empty()) {
		filteredFonts_ = allFonts_;
		filteredFontIndices_.reserve(allFonts_.size());
		for (size_t i = 0; i < allFonts_.size(); ++i)
			filteredFontIndices_.push_back(i);
	} else {
		filteredFontIndices_.reserve(allFonts_.size());
		for (size_t i = 0; i < allFonts_.size(); ++i) {
			if (allFontLowerNames_[i].Contains(input) || allPreviewLowerNames_[i].Contains(input)) {
				filteredFonts_.Add(allFonts_[i]);
				filteredFontIndices_.push_back(i);
			}
		}
	}

	fontNameList_->SetFonts(filteredFonts_);

	// 优先选中非 @ 的最佳匹配项，其次再退回到 @ 字体
	if (!filteredFonts_.empty()) {
		int best_idx = FindBestFilteredFontMatchIndex(fontNameInput_->GetValue());
		if (best_idx != wxNOT_FOUND) {
			fontNameList_->SetSelection(best_idx);
			fontNameList_->ScrollToRow(best_idx);
		}
	}

	fontNameList_->PrepareForSmoothScroll(fontNameList_->GetSelection());
}

void DialogFontChooser::RefreshFavoriteFontList(const wxString &selectedFont) {
	const wxArrayString favorites = GetFavoriteFontList();
	favoriteFontList_->SetFonts(favorites);

	if (selectedFont.empty())
		return;

	const int index = favoriteFontList_->FindString(selectedFont);
	if (index != wxNOT_FOUND) {
		favoriteFontList_->SetSelection(index);
		favoriteFontList_->ScrollToRow(index);
	}
}

void DialogFontChooser::UpdatePreview() {
	const wxString text = NormalizePreviewText(previewInput_->GetValue());
	fontNameList_->ConfigurePreviewText(quickPreviewCheck_->GetValue(), text);

	wxFont font = BuildFontFromControls();
	if (font.IsOk()) {
		previewText_->SetLabel(text);
		previewText_->SetFont(font);
		previewText_->SetForegroundColour(*wxBLACK);
		previewPanel_->Layout();
		previewPanel_->Refresh();
	}
}

wxFont DialogFontChooser::BuildFontFromControls() const {
	wxString faceName = GetEffectiveFaceName();
	if (faceName.empty())
		faceName = "Arial";
	// 最终返回给调用方的字体名必须保留原始面名，避免 @ 竖排字体被静默剥离。

	long size = 12;
	fontSizeInput_->GetValue().ToLong(&size);
	if (size < 1) size = 1;
	if (size > 999) size = 999;

	int styleIdx = fontStyleList_->GetSelection();
	if (styleIdx == wxNOT_FOUND) styleIdx = 0;

	int lfWeight = 400;
	int style = wxFONTSTYLE_NORMAL;
	if (static_cast<size_t>(styleIdx) < fontStyleEntries_.size()) {
		lfWeight = fontStyleEntries_[static_cast<size_t>(styleIdx)].lfWeight;
		style = fontStyleEntries_[static_cast<size_t>(styleIdx)].style;
	}

	wxFont font(static_cast<int>(size), wxFONTFAMILY_DEFAULT,
		style,
		lfWeight >= 700 ? wxFONTWEIGHT_BOLD : wxFONTWEIGHT_NORMAL,
		underlineCheck_->GetValue(),
		faceName);
	font.SetNumericWeight(lfWeight);
	font.SetStrikethrough(strikeoutCheck_->GetValue());
	return font;
}

void DialogFontChooser::OnFontNameInput(wxCommandEvent &) {
	fontNameFilterDirty_ = true;
	FilterFontList();
	PopulateStyleList(GetEffectiveFaceName());
	SelectMatchingStyle(BuildFontFromControls());
	UpdatePreview();
}

void DialogFontChooser::OnFontNameSelected(wxCommandEvent &) {
	int sel = fontNameList_->GetSelection();
	if (sel != wxNOT_FOUND) {
		// 临时解绑防止递归触发 FilterFontList
		fontNameInput_->Unbind(wxEVT_TEXT, &DialogFontChooser::OnFontNameInput, this);
		fontNameInput_->SetValue(fontNameList_->GetFontName(sel));
		fontNameInput_->Bind(wxEVT_TEXT, &DialogFontChooser::OnFontNameInput, this);
		RecordFavoriteFontFace(fontNameList_->GetFontName(sel));
		RefreshFavoriteFontList(fontNameList_->GetFontName(sel));
		PopulateStyleList(fontNameList_->GetFontName(sel));
		SelectMatchingStyle(BuildFontFromControls());
	}
	UpdatePreview();
}

void DialogFontChooser::OnFontStyleSelected(wxCommandEvent &) {
	int sel = fontStyleList_->GetSelection();
	if (sel != wxNOT_FOUND)
		fontStyleInput_->SetValue(fontStyleList_->GetString(sel));
	UpdatePreview();
}

void DialogFontChooser::OnFontSizeInput(wxCommandEvent &) {
	// 尝试选中大小列表中的匹配项
	int idx = fontSizeList_->FindString(fontSizeInput_->GetValue());
	if (idx != wxNOT_FOUND)
		fontSizeList_->SetSelection(idx);
	else
		fontSizeList_->SetSelection(wxNOT_FOUND);
	UpdatePreview();
}

void DialogFontChooser::OnFontSizeSelected(wxCommandEvent &) {
	int sel = fontSizeList_->GetSelection();
	if (sel != wxNOT_FOUND) {
		fontSizeInput_->Unbind(wxEVT_TEXT, &DialogFontChooser::OnFontSizeInput, this);
		fontSizeInput_->SetValue(fontSizeList_->GetString(sel));
		fontSizeInput_->Bind(wxEVT_TEXT, &DialogFontChooser::OnFontSizeInput, this);
	}
	UpdatePreview();
}

void DialogFontChooser::OnDeferredFontListTimer(wxTimerEvent &) {
	if (!deferredFontList_.valid()) {
		fontListTimer_.Stop();
		return;
	}

	if (deferredFontList_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return;

	fontListTimer_.Stop();
	StartDeferredFontListLoad();
}

void DialogFontChooser::OnEffectChanged(wxCommandEvent &) {
	UpdatePreview();
}

void DialogFontChooser::OnFavoriteFontSelected(wxCommandEvent &) {
	int sel = favoriteFontList_->GetSelection();
	if (sel == wxNOT_FOUND) return;
	wxString fontName = favoriteFontList_->GetFontName(sel);
	RecordFavoriteFontFace(fontName);
	RefreshFavoriteFontList(fontName);
	fontNameFilterDirty_ = true;
	fontNameInput_->ChangeValue(fontName);
	FilterFontList();
	int idx = fontNameList_->FindString(fontName);
	if (idx != wxNOT_FOUND) {
		fontNameList_->SetSelection(idx);
		fontNameList_->ScrollToRow(idx);
		fontNameList_->PrepareForSmoothScroll(idx);
	}
	PopulateStyleList(fontName);
	SelectMatchingStyle(BuildFontFromControls());
	UpdatePreview();
}

wxFont GetFontFromUser(wxWindow *parent, const wxFont &initial, const wxArrayString &fontList,
	std::shared_future<wxArrayString> deferredFontList) {
	DialogFontChooser dlg(parent, initial, fontList, std::move(deferredFontList));
	if (dlg.ShowModal() == wxID_OK)
		return dlg.GetSelectedFont();
	return wxNullFont;
}
