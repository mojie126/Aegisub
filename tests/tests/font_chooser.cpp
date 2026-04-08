/// @file font_chooser.cpp
/// @brief 字体选择器子串过滤逻辑的单元测试
///
/// @warning 本文件中的 FilterFontList 是对实际实现逻辑的**镜像副本**。
///          修改实际代码后，必须同步更新此处对应的副本函数。
///          请在修改实际实现后搜索 "MIRROR-OF:" 标记来定位需同步的副本。

#include <main.h>

#include <chrono>
#include <future>
#include <thread>

#include <string>
#include <vector>
#include <algorithm>

namespace {

bool IsAtFontFace(const std::string &font_name) {
	return !font_name.empty() && font_name.front() == '@';
}

std::string GetFontPreviewFaceName(const std::string &font_name) {
	if (IsAtFontFace(font_name))
		return font_name.substr(1);
	return font_name;
}

void SortFontFaceList(std::vector<std::string> &font_list) {
	std::stable_sort(font_list.begin(), font_list.end(), [](const std::string &lhs, const std::string &rhs) {
		const bool lhs_at = IsAtFontFace(lhs);
		const bool rhs_at = IsAtFontFace(rhs);
		if (lhs_at != rhs_at)
			return !lhs_at;

		std::string lhs_preview = GetFontPreviewFaceName(lhs);
		std::string rhs_preview = GetFontPreviewFaceName(rhs);
		std::transform(lhs_preview.begin(), lhs_preview.end(), lhs_preview.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::transform(rhs_preview.begin(), rhs_preview.end(), rhs_preview.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (lhs_preview != rhs_preview)
			return lhs_preview < rhs_preview;

		std::string lhs_lower = lhs;
		std::string rhs_lower = rhs;
		std::transform(lhs_lower.begin(), lhs_lower.end(), lhs_lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::transform(rhs_lower.begin(), rhs_lower.end(), rhs_lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return lhs_lower < rhs_lower;
	});
}

struct FilteredFontSearchCache {
	std::vector<std::string> all_font_lower_names;
	std::vector<std::string> all_preview_lower_names;
	std::vector<size_t> filtered_font_indices;
};

FilteredFontSearchCache RebuildFontSearchCache(const std::vector<std::string> &fonts) {
	auto lower = [](std::string value) {
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	};

	FilteredFontSearchCache cache;
	cache.all_font_lower_names.reserve(fonts.size());
	cache.all_preview_lower_names.reserve(fonts.size());
	cache.filtered_font_indices.reserve(fonts.size());

	for (size_t i = 0; i < fonts.size(); ++i) {
		cache.all_font_lower_names.push_back(lower(fonts[i]));
		cache.all_preview_lower_names.push_back(lower(GetFontPreviewFaceName(fonts[i])));
		cache.filtered_font_indices.push_back(i);
	}

	return cache;
}

int FindBestFilteredFontMatchIndex(const FilteredFontSearchCache &cache, const std::string &input) {
	if (cache.filtered_font_indices.empty())
		return -1;

	std::string input_lower = input;
	std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	for (size_t i = 0; i < cache.filtered_font_indices.size(); ++i) {
		const size_t original_index = cache.filtered_font_indices[i];
		if (cache.all_font_lower_names[original_index] == input_lower)
			return static_cast<int>(i);
	}

	for (size_t i = 0; i < cache.filtered_font_indices.size(); ++i) {
		const size_t original_index = cache.filtered_font_indices[i];
		if (cache.all_preview_lower_names[original_index] == input_lower)
			return static_cast<int>(i);
	}

	for (size_t i = 0; i < cache.filtered_font_indices.size(); ++i) {
		const size_t original_index = cache.filtered_font_indices[i];
		if (cache.all_font_lower_names[original_index].rfind(input_lower, 0) == 0)
			return static_cast<int>(i);
	}

	for (size_t i = 0; i < cache.filtered_font_indices.size(); ++i) {
		const size_t original_index = cache.filtered_font_indices[i];
		if (cache.all_preview_lower_names[original_index].rfind(input_lower, 0) == 0)
			return static_cast<int>(i);
	}

	return 0;
}

std::vector<std::string> RecordFavoriteFontList(std::vector<std::string> favorites, const std::string &font_name, int favorite_font_num) {
	if (font_name.empty() || favorite_font_num <= 0)
		return favorites;

	auto it = std::find(favorites.begin(), favorites.end(), font_name);
	if (it != favorites.end())
		favorites.erase(it);

	while (static_cast<int>(favorites.size()) >= favorite_font_num && !favorites.empty())
		favorites.erase(favorites.begin());

	favorites.push_back(font_name);
	return favorites;
}

std::vector<std::string> ApplyFavoriteFontFaceOrdering(std::vector<std::string> font_list,
                                                       const std::vector<std::string> &favorites) {
	for (auto it = favorites.rbegin(); it != favorites.rend(); ++it) {
		auto existing = std::find(font_list.begin(), font_list.end(), *it);
		if (existing != font_list.end())
			font_list.erase(existing);
		font_list.insert(font_list.begin(), *it);
	}

	return font_list;
}

std::string NormalizePreviewText(const std::string &preview_text) {
	return preview_text.empty() ? "AaBbYyZz" : preview_text;
}

bool ShouldRefreshPreviewLayout(bool previous_use_preview_text, const std::string &previous_preview_text,
                                bool current_use_preview_text, const std::string &current_preview_text) {
	if (previous_use_preview_text != current_use_preview_text)
		return true;
	if (!current_use_preview_text)
		return false;
	return previous_preview_text != current_preview_text;
}

std::string MakeFontListItemText(const std::string &font_name, const std::string &preview_text, bool use_preview_text) {
	if (!use_preview_text)
		return font_name;

	return NormalizePreviewText(preview_text) + " (" + font_name + ")";
}

std::string MakeFontListItemMeasureText(const std::string &font_name, const std::string &preview_text, bool use_preview_text) {
	if (!use_preview_text)
		return font_name;

	return NormalizePreviewText(preview_text);
}

std::string MakeFontListItemSharedMetricsCacheKey(const std::string &font_name, const std::string &preview_text,
	bool use_preview_text, int preview_font_size, int item_height) {
	return std::to_string(preview_font_size) + '\x1f' + std::to_string(item_height) + '\x1f'
		+ GetFontPreviewFaceName(font_name) + '\x1f' + MakeFontListItemMeasureText(font_name, preview_text, use_preview_text);
}

bool ShouldShowInitialPreparationProgress(bool quick_preview_enabled, size_t missing_main_metrics,
	size_t missing_favorite_metrics, bool has_deferred_font_list, bool all_fonts_empty) {
	const bool waiting_deferred_font_list = has_deferred_font_list && all_fonts_empty;
	if (!quick_preview_enabled)
		return waiting_deferred_font_list;
	return waiting_deferred_font_list || missing_main_metrics > 0 || missing_favorite_metrics > 0;
}

bool NeedsPreviewMetricPreparation(size_t missing_main_metrics, size_t missing_favorite_metrics) {
	return missing_main_metrics != 0 || missing_favorite_metrics != 0;
}

bool ShouldSchedulePendingPreviewListRefresh(bool quick_preview_enabled, bool refresh_pending,
	bool refresh_scheduled, bool refresh_in_progress) {
	return quick_preview_enabled && refresh_pending && !refresh_scheduled && !refresh_in_progress;
}

struct PendingPreviewRefreshApplyResult {
	bool should_apply = false;
	bool refresh_pending = false;
	bool refresh_in_progress = false;
	std::string pending_text;
	std::string applied_text;
};

PendingPreviewRefreshApplyResult ApplyPendingPreviewListRefresh(bool quick_preview_enabled,
	bool refresh_pending, bool refresh_in_progress, std::string pending_text, const std::string &current_text) {
	if (!quick_preview_enabled)
		return { false, false, refresh_in_progress, "", "" };
	if (!refresh_pending || refresh_in_progress)
		return { false, refresh_pending, refresh_in_progress, pending_text, "" };

	PendingPreviewRefreshApplyResult result;
	result.should_apply = true;
	result.refresh_pending = false;
	result.refresh_in_progress = true;
	result.pending_text.clear();
	result.applied_text = pending_text.empty() ? NormalizePreviewText(current_text) : pending_text;
	return result;
}

int CalculatePreviewItemHeight(int minimum_height, int text_box_height, int vertical_padding) {
	return std::max(minimum_height, text_box_height + vertical_padding);
}

int CalculatePreviewTextY(int rect_y, int rect_height, int text_box_height, int top_padding) {
	if (text_box_height >= rect_height)
		return rect_y + top_padding;
	return rect_y + (rect_height - text_box_height) / 2;
}

void AppendMetricWarmupIndex(std::vector<size_t> &order, std::vector<unsigned char> &seen, size_t index) {
	if (index >= seen.size() || seen[index])
		return;
	seen[index] = 1;
	order.push_back(index);
}


std::vector<size_t> BuildFontMetricWarmupOrder(size_t item_count, size_t anchor_index, size_t priority_radius) {
	std::vector<size_t> order;
	order.reserve(item_count);
	if (item_count == 0)
		return order;

	anchor_index = std::min(anchor_index, item_count - 1);
	std::vector<unsigned char> seen(item_count, 0);
	AppendMetricWarmupIndex(order, seen, anchor_index);

	for (size_t delta = 1; delta <= priority_radius; ++delta) {
		if (anchor_index + delta < item_count)
			AppendMetricWarmupIndex(order, seen, anchor_index + delta);
		if (anchor_index >= delta)
			AppendMetricWarmupIndex(order, seen, anchor_index - delta);
	}

	for (size_t i = 0; i < item_count; ++i)
		AppendMetricWarmupIndex(order, seen, i);

	return order;
}

std::vector<std::string> ResolveDeferredLoadedFonts(const std::vector<std::string> &all_fonts,
                                                    const std::string &input,
                                                    bool filter_dirty) {
	if (!filter_dirty)
		return all_fonts;

	std::vector<std::string> result;
	std::string input_lower = input;
	std::transform(input_lower.begin(), input_lower.end(), input_lower.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	for (const auto &font : all_fonts) {
		std::string font_lower = font;
		std::transform(font_lower.begin(), font_lower.end(), font_lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::string preview_lower = GetFontPreviewFaceName(font);
		std::transform(preview_lower.begin(), preview_lower.end(), preview_lower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (font_lower.find(input_lower) != std::string::npos || preview_lower.find(input_lower) != std::string::npos)
			result.push_back(font);
	}

	return result;
}

std::vector<std::string> ResolveReadyPreferredFontFaceList(
	const std::shared_future<std::vector<std::string>> &deferred_font_list,
	const std::vector<std::string> &fallback_fonts) {
	if (!deferred_font_list.valid())
		return fallback_fonts;

	try {
		return deferred_font_list.get();
	}
	catch (...) {
		return fallback_fonts;
	}
}

} // namespace

// ============================================================
// 字体名称子串过滤逻辑
// 对照: src/dialog_font_chooser.cpp :: DialogFontChooser::FilterFontList()
//       src/dialog_style_editor.cpp :: DialogStyleEditor::OnFontNameText()
// ============================================================

/// @brief 对字体列表执行大小写不敏感的子串过滤
///
/// MIRROR-OF: src/dialog_font_chooser.cpp :: DialogFontChooser::FilterFontList()
/// @param allFonts  完整字体列表
/// @param input     用户输入的搜索文本
/// @return 匹配的字体名称列表
static std::vector<std::string> FilterFontList(const std::vector<std::string> &allFonts,
                                                const std::string &input) {
	std::vector<std::string> result;
	if (input.empty()) {
		return allFonts;
	}

	// 转小写
	std::string inputLower = input;
	std::transform(inputLower.begin(), inputLower.end(), inputLower.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	for (const auto &font : allFonts) {
		std::string fontLower = font;
		std::transform(fontLower.begin(), fontLower.end(), fontLower.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::string previewLower = GetFontPreviewFaceName(font);
		std::transform(previewLower.begin(), previewLower.end(), previewLower.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (fontLower.find(inputLower) != std::string::npos || previewLower.find(inputLower) != std::string::npos)
			result.push_back(font);
	}
	return result;
}

/// @brief 在字体列表中查找精确匹配的索引（不区分大小写）
///
/// MIRROR-OF: src/dialog_font_chooser.cpp :: FontPreviewListBox::FindString()
/// @param fonts  字体列表
/// @param name   要查找的字体名称
/// @return 匹配的索引，未找到返回 -1
static int FindFontString(const std::vector<std::string> &fonts, const std::string &name) {
	std::string nameLower = name;
	std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	for (size_t i = 0; i < fonts.size(); ++i) {
		std::string fontLower = fonts[i];
		std::transform(fontLower.begin(), fontLower.end(), fontLower.begin(),
		               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		if (fontLower == nameLower)
			return static_cast<int>(i);
	}
	return -1;
}

// ============================================================
// 测试用例
// ============================================================

static const std::vector<std::string> kTestFonts = {
	"Arial",
	"@Arial",
	"Arial Black",
	"Consolas",
	"Courier New",
	"@Microsoft YaHei",
	"Microsoft YaHei",
	"SimHei",
	"SimSun",
	"Tahoma",
	"Times New Roman",
	"Verdana",
};

// --- FilterFontList 测试 ---

TEST(FontChooserTest, EmptyInputReturnsAll) {
	auto result = FilterFontList(kTestFonts, "");
	EXPECT_EQ(result.size(), kTestFonts.size());
}

TEST(FontChooserTest, PrefixMatch) {
	auto result = FilterFontList(kTestFonts, "Ari");
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "Arial");
	EXPECT_EQ(result[1], "@Arial");
	EXPECT_EQ(result[2], "Arial Black");
}

TEST(FontChooserTest, SubstringMatch) {
	auto result = FilterFontList(kTestFonts, "New");
	ASSERT_EQ(result.size(), 2u);
	EXPECT_EQ(result[0], "Courier New");
	EXPECT_EQ(result[1], "Times New Roman");
}

TEST(FontChooserTest, CaseInsensitive) {
	auto result = FilterFontList(kTestFonts, "arial");
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "Arial");
	EXPECT_EQ(result[1], "@Arial");
	EXPECT_EQ(result[2], "Arial Black");
}

TEST(FontChooserTest, MiddleSubstring) {
	// "onsol" 应匹配 "Consolas"
	auto result = FilterFontList(kTestFonts, "onsol");
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0], "Consolas");
}

TEST(FontChooserTest, NoMatch) {
	auto result = FilterFontList(kTestFonts, "zzzzz");
	EXPECT_TRUE(result.empty());
}

TEST(FontChooserTest, SingleCharMatch) {
	// "H" 应匹配包含 h/H 的字体（SimHei, Microsoft YaHei, Tahoma）
	auto result = FilterFontList(kTestFonts, "H");
	EXPECT_GE(result.size(), 3u);
}

TEST(FontChooserTest, AtFontCanBeFoundByBaseName) {
	auto result = FilterFontList(kTestFonts, "YaHei");
	EXPECT_NE(std::find(result.begin(), result.end(), "Microsoft YaHei"), result.end());
	EXPECT_NE(std::find(result.begin(), result.end(), "@Microsoft YaHei"), result.end());
}

TEST(FontChooserTest, ExactFullMatch) {
	auto result = FilterFontList(kTestFonts, "Consolas");
	ASSERT_EQ(result.size(), 1u);
	EXPECT_EQ(result[0], "Consolas");
}

// --- FindFontString 测试 ---

TEST(FontChooserTest, FindExactMatch) {
	int idx = FindFontString(kTestFonts, "Arial");
	EXPECT_EQ(idx, 0);
}

TEST(FontChooserTest, FindCaseInsensitive) {
	int idx = FindFontString(kTestFonts, "arial");
	EXPECT_EQ(idx, 0);
}

TEST(FontChooserTest, FindNotFound) {
	int idx = FindFontString(kTestFonts, "NonExistent");
	EXPECT_EQ(idx, -1);
}

TEST(FontChooserTest, FindPartialDoesNotMatch) {
	// FindString 应该是精确匹配，子串不应匹配
	int idx = FindFontString(kTestFonts, "Ari");
	EXPECT_EQ(idx, -1);
}

TEST(FontChooserTest, EmptyFontList) {
	std::vector<std::string> empty;
	auto result = FilterFontList(empty, "test");
	EXPECT_TRUE(result.empty());
	EXPECT_EQ(FindFontString(empty, "test"), -1);
}

TEST(FontChooserTest, PreviewFaceStripsAtPrefix) {
	EXPECT_EQ(GetFontPreviewFaceName("@Microsoft YaHei"), "Microsoft YaHei");
	EXPECT_EQ(GetFontPreviewFaceName("Consolas"), "Consolas");
}

TEST(FontChooserTest, EmptyPreviewTextFallsBackToDefaultSample) {
	EXPECT_EQ(NormalizePreviewText(""), "AaBbYyZz");
}

TEST(FontChooserTest, NonEmptyPreviewTextIsPreserved) {
	EXPECT_EQ(NormalizePreviewText("Sample Text"), "Sample Text");
}

TEST(FontChooserTest, DisabledQuickPreviewIgnoresPreviewTextChangesForLayout) {
	EXPECT_FALSE(ShouldRefreshPreviewLayout(false, "AaBbYyZz", false, "Sample Text"));
}

TEST(FontChooserTest, EnabledQuickPreviewRefreshesWhenPreviewTextChanges) {
	EXPECT_TRUE(ShouldRefreshPreviewLayout(true, "AaBbYyZz", true, "Sample Text"));
}

TEST(FontChooserTest, TogglingQuickPreviewRefreshesLayout) {
	EXPECT_TRUE(ShouldRefreshPreviewLayout(false, "AaBbYyZz", true, "AaBbYyZz"));
	EXPECT_TRUE(ShouldRefreshPreviewLayout(true, "AaBbYyZz", false, "AaBbYyZz"));
}

TEST(FontChooserTest, QuickPreviewAppendsFontName) {
	EXPECT_EQ(MakeFontListItemText("Arial", "Sample Text", true), "Sample Text (Arial)");
}

TEST(FontChooserTest, QuickPreviewPreservesAtFontDistinction) {
	EXPECT_EQ(MakeFontListItemText("@Microsoft YaHei", "", true), "AaBbYyZz (@Microsoft YaHei)");
}

TEST(FontChooserTest, QuickPreviewMeasureTextIgnoresFontNameSuffix) {
	EXPECT_EQ(MakeFontListItemMeasureText("@Microsoft YaHei", "示例文本", true), "示例文本");
	EXPECT_EQ(MakeFontListItemMeasureText("Arial", "", true), "AaBbYyZz");
}

TEST(FontChooserTest, NonQuickPreviewMeasureTextKeepsFontName) {
	EXPECT_EQ(MakeFontListItemMeasureText("Arial", "示例文本", false), "Arial");
}

TEST(FontChooserTest, SharedMetricsCacheKeySeparatesPreviewSize) {
	EXPECT_NE(
		MakeFontListItemSharedMetricsCacheKey("Arial", "示例文本", true, 16, 40),
		MakeFontListItemSharedMetricsCacheKey("Arial", "示例文本", true, 18, 40));
	EXPECT_NE(
		MakeFontListItemSharedMetricsCacheKey("Arial", "示例文本", true, 16, 40),
		MakeFontListItemSharedMetricsCacheKey("Arial", "示例文本", true, 16, 44));
}

TEST(FontChooserTest, SharedMetricsHitSkipsInitialProgress) {
	EXPECT_FALSE(ShouldShowInitialPreparationProgress(true, 0, 0, false, false));
	EXPECT_TRUE(ShouldShowInitialPreparationProgress(true, 1, 0, false, false));
	EXPECT_TRUE(ShouldShowInitialPreparationProgress(true, 0, 1, false, false));
	EXPECT_TRUE(ShouldShowInitialPreparationProgress(false, 0, 0, true, true));
	EXPECT_TRUE(ShouldShowInitialPreparationProgress(true, 0, 0, true, true));
}

TEST(FontChooserTest, PreviewPreparationTracksFavoriteListCacheMisses) {
	EXPECT_FALSE(NeedsPreviewMetricPreparation(0, 0));
	EXPECT_TRUE(NeedsPreviewMetricPreparation(1, 0));
	EXPECT_TRUE(NeedsPreviewMetricPreparation(0, 1));
}

TEST(FontChooserTest, PendingPreviewRefreshRequiresDirtyStateToSchedule) {
	EXPECT_FALSE(ShouldSchedulePendingPreviewListRefresh(true, false, false, false));
	EXPECT_FALSE(ShouldSchedulePendingPreviewListRefresh(true, true, true, false));
	EXPECT_FALSE(ShouldSchedulePendingPreviewListRefresh(true, true, false, true));
	EXPECT_FALSE(ShouldSchedulePendingPreviewListRefresh(false, true, false, false));
	EXPECT_TRUE(ShouldSchedulePendingPreviewListRefresh(true, true, false, false));
}

TEST(FontChooserTest, PendingPreviewRefreshConsumesPendingStateBeforeWork) {
	auto result = ApplyPendingPreviewListRefresh(true, true, false, "示例文本", "AaBbYyZz");
	EXPECT_TRUE(result.should_apply);
	EXPECT_FALSE(result.refresh_pending);
	EXPECT_TRUE(result.refresh_in_progress);
	EXPECT_TRUE(result.pending_text.empty());
	EXPECT_EQ(result.applied_text, "示例文本");
}

TEST(FontChooserTest, PendingPreviewRefreshFallsBackToCurrentTextWhenPendingTextMissing) {
	auto result = ApplyPendingPreviewListRefresh(true, true, false, "", "");
	EXPECT_TRUE(result.should_apply);
	EXPECT_EQ(result.applied_text, "AaBbYyZz");
}

TEST(FontChooserTest, PendingPreviewRefreshDoesNotReenterWhileApplying) {
	auto result = ApplyPendingPreviewListRefresh(true, true, true, "示例文本", "AaBbYyZz");
	EXPECT_FALSE(result.should_apply);
	EXPECT_TRUE(result.refresh_pending);
	EXPECT_TRUE(result.refresh_in_progress);
	EXPECT_EQ(result.pending_text, "示例文本");
}

TEST(FontChooserTest, TallPreviewItemExpandsHeight) {
	EXPECT_EQ(CalculatePreviewItemHeight(40, 52, 6), 58);
	EXPECT_EQ(CalculatePreviewItemHeight(40, 20, 6), 40);
}

TEST(FontChooserTest, OverflowingPreviewUsesTopPadding) {
	EXPECT_EQ(CalculatePreviewTextY(10, 40, 50, 3), 13);
	EXPECT_EQ(CalculatePreviewTextY(10, 40, 20, 3), 20);
}

TEST(FontChooserTest, MetricWarmupPrioritizesAnchorNeighbors) {
	auto order = BuildFontMetricWarmupOrder(8, 4, 2);
	ASSERT_GE(order.size(), 5u);
	EXPECT_EQ(order[0], 4u);
	EXPECT_EQ(order[1], 5u);
	EXPECT_EQ(order[2], 3u);
	EXPECT_EQ(order[3], 6u);
	EXPECT_EQ(order[4], 2u);
}

TEST(FontChooserTest, DeferredLoadKeepsFullListBeforeUserFiltering) {
	auto result = ResolveDeferredLoadedFonts(kTestFonts, "Arial", false);
	EXPECT_EQ(result.size(), kTestFonts.size());
}

TEST(FontChooserTest, DeferredLoadAppliesFilterAfterUserInput) {
	auto result = ResolveDeferredLoadedFonts(kTestFonts, "Arial", true);
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "Arial");
	EXPECT_EQ(result[1], "@Arial");
	EXPECT_EQ(result[2], "Arial Black");
}

TEST(FontChooserTest, ReadyAsyncFontListUsesResolvedValue) {
	std::promise<std::vector<std::string>> promise;
	auto future = promise.get_future().share();
	const std::vector<std::string> async_fonts = {"SimHei", "SimSun"};
	promise.set_value(async_fonts);

	const std::vector<std::string> fallback = {"Arial", "Consolas"};
	auto result = ResolveReadyPreferredFontFaceList(future, fallback);
	EXPECT_EQ(result, async_fonts);
}

TEST(FontChooserTest, ExceptionalAsyncFontListFallsBackToSynchronousList) {
	std::promise<std::vector<std::string>> promise;
	auto future = promise.get_future().share();
	promise.set_exception(std::make_exception_ptr(std::runtime_error("font list failed")));

	const std::vector<std::string> fallback = {"Arial", "Consolas"};
	auto result = ResolveReadyPreferredFontFaceList(future, fallback);
	EXPECT_EQ(result, fallback);
}

TEST(FontChooserTest, InvalidAsyncFontListFallsBackToSynchronousList) {
	std::shared_future<std::vector<std::string>> future;
	const std::vector<std::string> fallback = {"Arial", "Consolas"};
	auto result = ResolveReadyPreferredFontFaceList(future, fallback);
	EXPECT_EQ(result, fallback);
}

TEST(FontChooserTest, AsyncSharedFutureLastOwnerCanWaitForTask) {
	using namespace std::chrono_literals;
	std::promise<void> gate_promise;
	auto gate = gate_promise.get_future().share();
	auto future = std::async(std::launch::async, [gate]() {
		gate.wait();
		return 1;
	}).share();
	std::thread release_thread([&gate_promise]() {
		std::this_thread::sleep_for(30ms);
		gate_promise.set_value();
	});

	auto start = std::chrono::steady_clock::now();
	future = std::shared_future<int>();
	auto elapsed = std::chrono::steady_clock::now() - start;

	EXPECT_GE(elapsed, 20ms);
	release_thread.join();
}

TEST(FontChooserTest, PromiseSharedFutureDoesNotWaitOnDestroy) {
	using namespace std::chrono_literals;
	std::promise<int> promise;
	auto future = promise.get_future().share();
	auto start = std::chrono::steady_clock::now();
	future = std::shared_future<int>();
	auto elapsed = std::chrono::steady_clock::now() - start;

	EXPECT_LT(elapsed, 20ms);
	promise.set_value(1);
}

TEST(FontChooserTest, SortPlacesNonAtFontsFirst) {
	std::vector<std::string> fonts = {
		"@Arial",
		"Verdana",
		"Arial",
		"@Verdana"
	};
	SortFontFaceList(fonts);
	ASSERT_EQ(fonts.size(), 4u);
	EXPECT_EQ(fonts[0], "Arial");
	EXPECT_EQ(fonts[1], "Verdana");
	EXPECT_EQ(fonts[2], "@Arial");
	EXPECT_EQ(fonts[3], "@Verdana");
}

TEST(FontChooserTest, BestMatchPrefersNonAtFont) {
	std::vector<std::string> fonts = {
		"Arial",
		"@Arial",
		"Arial Black"
	};
	auto cache = RebuildFontSearchCache(fonts);
	EXPECT_EQ(FindBestFilteredFontMatchIndex(cache, "Arial"), 0);
}

TEST(FontChooserTest, FilteredBestMatchUsesFilteredIndices) {
	std::vector<std::string> fonts = {
		"Consolas",
		"Arial",
		"@Arial",
		"Arial Black"
	};
	auto cache = RebuildFontSearchCache(fonts);
	cache.filtered_font_indices = {1, 2, 3};
	EXPECT_EQ(FindBestFilteredFontMatchIndex(cache, "Arial"), 0);
}

TEST(FontChooserTest, ExistingFavoriteMovesToMostRecent) {
	std::vector<std::string> favorites = {
		"Arial",
		"Consolas",
		"Tahoma"
	};

	auto result = RecordFavoriteFontList(favorites, "Consolas", 3);
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "Arial");
	EXPECT_EQ(result[1], "Tahoma");
	EXPECT_EQ(result[2], "Consolas");
}

TEST(FontChooserTest, NewFavoriteEvictsOldest) {
	std::vector<std::string> favorites = {
		"Arial",
		"Consolas",
		"Tahoma"
	};

	auto result = RecordFavoriteFontList(favorites, "Verdana", 3);
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "Consolas");
	EXPECT_EQ(result[1], "Tahoma");
	EXPECT_EQ(result[2], "Verdana");
}

TEST(FontChooserTest, PreferredFontListPromotesFavoritesWithoutDuplicatingEntries) {
	std::vector<std::string> fonts = {
		"Arial",
		"Consolas",
		"Tahoma"
	};
	std::vector<std::string> favorites = {
		"Tahoma",
		"Arial"
	};

	auto result = ApplyFavoriteFontFaceOrdering(fonts, favorites);
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "Tahoma");
	EXPECT_EQ(result[1], "Arial");
	EXPECT_EQ(result[2], "Consolas");
}

TEST(FontChooserTest, PreferredFontListRetainsMissingFavoriteAtFront) {
	std::vector<std::string> fonts = {
		"Arial",
		"Consolas"
	};
	std::vector<std::string> favorites = {
		"Missing Font",
		"Consolas"
	};

	auto result = ApplyFavoriteFontFaceOrdering(fonts, favorites);
	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0], "Missing Font");
	EXPECT_EQ(result[1], "Consolas");
	EXPECT_EQ(result[2], "Arial");
}
