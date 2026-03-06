/// @file style_rename.cpp
/// @brief 样式重命名覆写块替换逻辑的单元测试 (TypesettingTools/Aegisub#446)
///
/// @warning 本文件中的 ReplaceStyleInOverrides 是对实际实现逻辑的**镜像副本**。
///          修改实际代码后，必须同步更新此处对应的副本函数。
///          请在修改实际实现后搜索 "MIRROR-OF:" 标记来定位需同步的副本。

#include <main.h>

#include <string>

// ============================================================
// 样式重命名：覆写块内 \r 标签的定向替换
// 对照: src/dialog_style_editor.cpp :: StyleRenamer::ReplaceStyleInOverrides()
// ============================================================

/// @brief 在文本中直接替换覆写块内的 \r 标签样式引用
///
/// MIRROR-OF: src/dialog_style_editor.cpp :: StyleRenamer::ReplaceStyleInOverrides()
/// @param text        对话行原始文本
/// @param source_name 需要替换的源样式名
/// @param new_name    替换目标样式名
/// @param replace     是否执行替换（false 时仅检测是否存在引用）
/// @param[out] found_any 是否找到引用（仅 replace=false 时有意义）
/// @return 替换后的文本（replace=false 时返回值无意义）
static std::string ReplaceStyleInOverrides(const std::string& text,
                                           const std::string& source_name,
                                           const std::string& new_name,
                                           bool replace,
                                           bool& found_any) {
	std::string result;
	result.reserve(text.size());
	std::string target = "\\r" + source_name;

	size_t pos = 0;
	while (pos < text.size()) {
		if (text[pos] == '{') {
			size_t end = text.find('}', pos);
			if (end == std::string::npos) {
				result += text.substr(pos);
				break;
			}

			// 在覆写块内搜索 \rSourceName
			size_t bpos = pos;
			size_t block_end = end + 1;
			while (bpos < block_end) {
				size_t found = text.find(target, bpos);
				if (found == std::string::npos || found >= block_end) {
					result += text.substr(bpos, block_end - bpos);
					break;
				}

				// 验证匹配位置：\r 标签参数以 \ 或 } 结束
				size_t after = found + target.size();
				if (after >= text.size() || text[after] == '\\' || text[after] == '}') {
					if (replace) {
						result += text.substr(bpos, found - bpos);
						result += "\\r" + new_name;
						bpos = after;
					}
					else {
						found_any = true;
						return {};
					}
				}
				else {
					result += text.substr(bpos, after - bpos);
					bpos = after;
				}
			}
			pos = block_end;
		}
		else {
			size_t next = text.find('{', pos);
			if (next == std::string::npos) {
				result += text.substr(pos);
				break;
			}
			result += text.substr(pos, next - pos);
			pos = next;
		}
	}
	return result;
}

/// 辅助函数：执行替换并返回结果
static std::string DoReplace(const std::string& text, const std::string& src, const std::string& dst) {
	bool found = false;
	return ReplaceStyleInOverrides(text, src, dst, true, found);
}

/// 辅助函数：检测是否存在对 src 样式的引用
static bool HasReference(const std::string& text, const std::string& src) {
	bool found = false;
	ReplaceStyleInOverrides(text, src, "", false, found);
	return found;
}

// --- 基本替换 ---

/// 覆写块末尾的 \r 标签应被替换
TEST(StyleRenameTest, BasicReplace_AtBlockEnd) {
	EXPECT_EQ("{\\rNewStyle}", DoReplace("{\\rOldStyle}", "OldStyle", "NewStyle"));
}

/// \r 标签后跟其他覆写标签时应被替换
TEST(StyleRenameTest, BasicReplace_WithFollowingTag) {
	EXPECT_EQ("{\\rNewStyle\\fs20}", DoReplace("{\\rOldStyle\\fs20}", "OldStyle", "NewStyle"));
}

/// \r 标签前有其他标签时应被替换
TEST(StyleRenameTest, BasicReplace_WithPrecedingTag) {
	EXPECT_EQ("{\\b1\\rNewStyle}", DoReplace("{\\b1\\rOldStyle}", "OldStyle", "NewStyle"));
}

/// 前后都有标签时应被替换
TEST(StyleRenameTest, BasicReplace_MiddleOfBlock) {
	EXPECT_EQ("{\\b1\\rNewStyle\\fs20}", DoReplace("{\\b1\\rOldStyle\\fs20}", "OldStyle", "NewStyle"));
}

// --- 不应替换的情况 ---

/// 样式名是被搜索样式的前缀时不应替换
TEST(StyleRenameTest, NoReplace_LongerStyleName) {
	EXPECT_EQ("{\\rOldStyleExtra}", DoReplace("{\\rOldStyleExtra}", "OldStyle", "NewStyle"));
}

/// 完全不同的样式名
TEST(StyleRenameTest, NoReplace_DifferentStyle) {
	EXPECT_EQ("{\\rOtherStyle}", DoReplace("{\\rOtherStyle}", "OldStyle", "NewStyle"));
}

/// 无 \r 标签
TEST(StyleRenameTest, NoReplace_NoRTag) {
	EXPECT_EQ("{\\fs20\\b1}", DoReplace("{\\fs20\\b1}", "OldStyle", "NewStyle"));
}

// --- 多个覆写块 ---

/// 多个块中的 \r 都应被替换
TEST(StyleRenameTest, MultiBlock_AllReplaced) {
	EXPECT_EQ("{\\rNew}text{\\rNew\\b1}",
	          DoReplace("{\\rOld}text{\\rOld\\b1}", "Old", "New"));
}

/// 只有匹配的块被替换，其他保持不变
TEST(StyleRenameTest, MultiBlock_PartialMatch) {
	EXPECT_EQ("{\\rNew}text{\\rOther}",
	          DoReplace("{\\rOld}text{\\rOther}", "Old", "New"));
}

// --- 同一块内多个 \r ---

/// 同一覆写块内多个 \r 标签引用同一样式
TEST(StyleRenameTest, MultipleRTagsInOneBlock) {
	EXPECT_EQ("{\\rNew\\b1\\rNew}", DoReplace("{\\rOld\\b1\\rOld}", "Old", "New"));
}

// --- 非覆写文本保留 ---

/// 覆写块外的普通文本应原样保留
TEST(StyleRenameTest, PlainTextPreserved) {
	EXPECT_EQ("hello{\\rNew}world", DoReplace("hello{\\rOld}world", "Old", "New"));
}

/// 纯文本（无覆写块）应原样返回
TEST(StyleRenameTest, PureText_NoBlocks) {
	EXPECT_EQ("just plain text", DoReplace("just plain text", "Old", "New"));
}

/// 空字符串
TEST(StyleRenameTest, EmptyText) {
	EXPECT_EQ("", DoReplace("", "Old", "New"));
}

// --- Lua 模板代码保护 ---

/// Lua 模板块 {!...!} 中不含目标 \r 时应原样保留
TEST(StyleRenameTest, LuaBlock_NoMatch) {
	EXPECT_EQ("{!retime(\"line\",0,200)!}", DoReplace("{!retime(\"line\",0,200)!}", "OldStyle", "NewStyle"));
}

/// 混合覆写块和 Lua 块：仅覆写块被修改
TEST(StyleRenameTest, LuaBlock_MixedWithOverride) {
	EXPECT_EQ("{\\rNew}{!code()!}text",
	          DoReplace("{\\rOld}{!code()!}text", "Old", "New"));
}

// --- 边界情况 ---

/// 未闭合的覆写块（无 }）应原样保留
TEST(StyleRenameTest, MalformedBlock_NoBrace) {
	EXPECT_EQ("{\\rOldStyle text", DoReplace("{\\rOldStyle text", "OldStyle", "NewStyle"));
}

/// 空覆写块
TEST(StyleRenameTest, EmptyOverrideBlock) {
	EXPECT_EQ("{}", DoReplace("{}", "Old", "New"));
}

/// \r 标签位于文本末尾且覆写块正常闭合
TEST(StyleRenameTest, RTagAtEndOfBlock) {
	EXPECT_EQ("{\\fs20\\rNew}", DoReplace("{\\fs20\\rOld}", "Old", "New"));
}

/// 样式名包含特殊字符（ASS 允许的范围内）
TEST(StyleRenameTest, StyleNameWithSpecialChars) {
	EXPECT_EQ("{\\rNew-Name (2)}", DoReplace("{\\rOld-Name (1)}", "Old-Name (1)", "New-Name (2)"));
}

/// 反斜杠在纯文本中不应触发替换
TEST(StyleRenameTest, BackslashInPlainText) {
	std::string text = "C:\\rOldStyle\\path{\\rOld}";
	EXPECT_EQ("C:\\rOldStyle\\path{\\rNew}", DoReplace(text, "Old", "New"));
}

// --- 检测模式 ---

/// 检测到覆写块中存在引用
TEST(StyleRenameTest, Detect_Found) {
	EXPECT_TRUE(HasReference("{\\rOldStyle}", "OldStyle"));
}

/// 覆写块中无引用
TEST(StyleRenameTest, Detect_NotFound) {
	EXPECT_FALSE(HasReference("{\\fs20}", "OldStyle"));
}

/// 纯文本中即使包含 \rStyleName 也不应检测到（不在覆写块内）
TEST(StyleRenameTest, Detect_NotInOverride) {
	EXPECT_FALSE(HasReference("text\\rOldStyle", "OldStyle"));
}

/// 样式名为更长名称的前缀时不应检测到
TEST(StyleRenameTest, Detect_PrefixNotMatched) {
	EXPECT_FALSE(HasReference("{\\rOldStyleExtra}", "OldStyle"));
}
