/// @file font_chooser.cpp
/// @brief 字体选择器子串过滤逻辑的单元测试
///
/// @warning 本文件中的 FilterFontList 是对实际实现逻辑的**镜像副本**。
///          修改实际代码后，必须同步更新此处对应的副本函数。
///          请在修改实际实现后搜索 "MIRROR-OF:" 标记来定位需同步的副本。

#include <main.h>

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

int FindBestFontMatchIndex(const std::vector<std::string> &fonts, const std::string &input) {
	if (fonts.empty())
		return -1;

	auto lower = [](std::string value) {
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	};

	const std::string input_lower = lower(input);

	for (size_t i = 0; i < fonts.size(); ++i) {
		if (lower(fonts[i]) == input_lower)
			return static_cast<int>(i);
	}

	for (size_t i = 0; i < fonts.size(); ++i) {
		if (lower(GetFontPreviewFaceName(fonts[i])) == input_lower)
			return static_cast<int>(i);
	}

	for (size_t i = 0; i < fonts.size(); ++i) {
		if (lower(fonts[i]).rfind(input_lower, 0) == 0)
			return static_cast<int>(i);
	}

	for (size_t i = 0; i < fonts.size(); ++i) {
		if (lower(GetFontPreviewFaceName(fonts[i])).rfind(input_lower, 0) == 0)
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
	EXPECT_EQ(FindBestFontMatchIndex(fonts, "Arial"), 0);
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
