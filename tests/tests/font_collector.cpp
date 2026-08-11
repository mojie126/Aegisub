/// @file font_collector.cpp
/// @brief 字体收集器行内样式状态机的单元测试
/// @details 覆盖 \r 重置后 \b/\i/\fn/\fn0 以重置样式为回退基准的行为 (上游 4d61325b0)

#include <gtest/gtest.h>

#include "ass_dialogue.h"
#include "font_file_lister.h"

#include <functional>
#include <map>
#include <string>

namespace {

LineStyleState MakeState(std::string const& facename, int bold, bool italic) {
	return {facename, bold, italic};
}

/// 按样式名解析样式定义的回调
std::function<LineStyleState(std::string const&)> MakeResolver(std::map<std::string, LineStyleState> const& styles) {
	return [&](std::string const& name) { return styles.at(name); };
}

/// 构造 override 块并解析其中的标签
AssDialogueBlockOverride MakeBlock(std::string const& text) {
	AssDialogueBlockOverride block(text);
	block.ParseTags();
	return block;
}

/// 默认样式与非默认样式字重/斜体不同的样式表
std::map<std::string, LineStyleState> MakeStyleTable() {
	return {
		{"Default", MakeState("FontA", 0, false)},
		{"StyleB",  MakeState("FontB", 1, true)},
	};
}

/// 以初始样式应用一个块并返回是否设置了 override 标签
bool Apply(std::string const& block_text, std::map<std::string, LineStyleState> const& styles,
	LineStyleState& style, LineStyleState& reset) {
	bool overriden = false;
	style.ApplyOverrideBlock(reset, overriden, MakeBlock(block_text).Tags,
		MakeResolver(styles), "Default");
	return overriden;
}

} // namespace

/// \r 之后无参数 \b 应回退到重置样式的字重，而非行初始样式的字重
TEST(FontCollectorStyle, BoldAfterResetUsesResetBaseline) {
	auto styles = MakeStyleTable();
	auto style = styles.at("Default");
	auto reset = style;

	EXPECT_TRUE(Apply("\\rStyleB\\b", styles, style, reset));
	EXPECT_EQ(style.facename, "FontB");
	EXPECT_EQ(style.bold, styles.at("StyleB").bold);
	EXPECT_TRUE(style.italic);
}

/// \r 之后无参数 \i 应回退到重置样式的斜体状态
TEST(FontCollectorStyle, ItalicAfterResetUsesResetBaseline) {
	auto styles = MakeStyleTable();
	auto style = styles.at("Default");
	auto reset = style;

	EXPECT_TRUE(Apply("\\rStyleB\\i", styles, style, reset));
	EXPECT_EQ(style.italic, styles.at("StyleB").italic);
}

/// \r 之后 \fn0 应重置为重置样式的字体
TEST(FontCollectorStyle, Fn0AfterResetUsesResetFacename) {
	auto styles = MakeStyleTable();
	auto style = styles.at("Default");
	auto reset = style;

	EXPECT_TRUE(Apply("\\rStyleB\\fn0", styles, style, reset));
	EXPECT_EQ(style.facename, "FontB");
}

/// \r 之后无参数 \fn 应回退到重置样式的字体
TEST(FontCollectorStyle, FnWithoutParamAfterResetUsesResetFacename) {
	auto styles = MakeStyleTable();
	auto style = styles.at("Default");
	auto reset = style;

	EXPECT_TRUE(Apply("\\rStyleB\\fn", styles, style, reset));
	EXPECT_EQ(style.facename, "FontB");
}

/// 无 \r 时 \b/\fn 仍以行初始样式为回退基准
TEST(FontCollectorStyle, WithoutResetUsesInitialBaseline) {
	auto styles = MakeStyleTable();
	auto style = styles.at("Default");
	auto reset = style;

	EXPECT_TRUE(Apply("\\b", styles, style, reset));
	EXPECT_EQ(style.bold, styles.at("Default").bold);

	style = styles.at("Default");
	EXPECT_TRUE(Apply("\\fn0", styles, style, reset));
	EXPECT_EQ(style.facename, "FontA");
}

/// 无参数 \r 应重置为行默认样式
TEST(FontCollectorStyle, BareResetFallsBackToDefaultStyle) {
	auto styles = MakeStyleTable();
	auto style = styles.at("Default");
	auto reset = style;

	EXPECT_FALSE(Apply("\\r", styles, style, reset));
	EXPECT_EQ(style.facename, "FontA");
	EXPECT_EQ(style.bold, styles.at("Default").bold);
	EXPECT_FALSE(style.italic);
}

/// \fn 带显式字体名时优先使用显式字体名
TEST(FontCollectorStyle, FnWithExplicitNameTakesPrecedence) {
	auto styles = MakeStyleTable();
	auto style = styles.at("Default");
	auto reset = style;

	EXPECT_TRUE(Apply("\\rStyleB\\fnFontC", styles, style, reset));
	EXPECT_EQ(style.facename, "FontC");
}

/// 多个 override 块按顺序应用，\r 更新后续块的回退基准
TEST(FontCollectorStyle, MultipleBlocksApplySequentially) {
	auto styles = MakeStyleTable();
	auto style = styles.at("Default");
	auto reset = style;

	ASSERT_TRUE(Apply("\\b1", styles, style, reset));
	EXPECT_EQ(style.bold, 1);

	EXPECT_TRUE(Apply("\\rStyleB\\b", styles, style, reset));
	EXPECT_EQ(style.facename, "FontB");
	EXPECT_EQ(style.bold, styles.at("StyleB").bold);
}
