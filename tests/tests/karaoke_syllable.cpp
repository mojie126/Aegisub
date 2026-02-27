// Copyright (c) 2025, Aegisub contributors
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
//
// Aegisub Project http://www.aegisub.org/

/// @file karaoke_syllable.cpp
/// @brief KaraokeSyllable::GetText 的单元测试
/// @ingroup tests

#include <libaegisub/ass/karaoke.h>

#include <main.h>

using agi::ass::KaraokeSyllable;

/// 基本测试：无覆写标签的音节
TEST(lagi_karaoke_syllable, basic_no_ovr_tags) {
	KaraokeSyllable syl;
	syl.start_time = 0;
	syl.duration = 200;
	syl.text = "hello";
	syl.tag_type = "\\k";

	EXPECT_EQ("{\\k20}hello", syl.GetText(true));
	EXPECT_EQ("hello", syl.GetText(false));
}

/// 测试：中间位置的覆写标签不受影响
TEST(lagi_karaoke_syllable, mid_position_ovr_tags) {
	KaraokeSyllable syl;
	syl.start_time = 0;
	syl.duration = 300;
	syl.text = "ab";
	syl.tag_type = "\\k";
	syl.ovr_tags[1] = "{\\c&HFF0000&}";

	EXPECT_EQ("{\\k30}a{\\c&HFF0000&}b", syl.GetText(true));
}

/// TypesettingTools/Aegisub#351: 位置 0 的内联标签应与 k 标签合并到同一 {} 块
TEST(lagi_karaoke_syllable, inline_tags_merged_with_k_tag) {
	KaraokeSyllable syl;
	syl.start_time = 0;
	syl.duration = 200;
	syl.text = "text";
	syl.tag_type = "\\k";
	syl.ovr_tags[0] = "{\\c&HFF0000&}";

	// 修复前: "{\k20}{\c&HFF0000&}text" — 标签被分离
	// 修复后: "{\k20\c&HFF0000&}text" — 标签在同一块
	EXPECT_EQ("{\\k20\\c&HFF0000&}text", syl.GetText(true));
}

/// #351: 多个相邻 {} 块的内联标签也应合并
TEST(lagi_karaoke_syllable, multiple_inline_tags_merged) {
	KaraokeSyllable syl;
	syl.start_time = 0;
	syl.duration = 200;
	syl.text = "text";
	syl.tag_type = "\\k";
	syl.ovr_tags[0] = "{\\c&HFF0000&}{\\alpha&HFF&}";

	EXPECT_EQ("{\\k20\\c&HFF0000&\\alpha&HFF&}text", syl.GetText(true));
}

/// #351: k_tag=false 时，位置 0 的标签正常输出（不合并）
TEST(lagi_karaoke_syllable, no_merge_without_k_tag) {
	KaraokeSyllable syl;
	syl.start_time = 0;
	syl.duration = 200;
	syl.text = "text";
	syl.tag_type = "\\k";
	syl.ovr_tags[0] = "{\\c&HFF0000&}";

	EXPECT_EQ("{\\c&HFF0000&}text", syl.GetText(false));
}

/// #351: 同时存在位置 0 和中间位置的覆写标签
TEST(lagi_karaoke_syllable, mixed_positions_ovr_tags) {
	KaraokeSyllable syl;
	syl.start_time = 0;
	syl.duration = 200;
	syl.text = "ab";
	syl.tag_type = "\\kf";
	syl.ovr_tags[0] = "{\\c&HFF0000&}";
	syl.ovr_tags[1] = "{\\alpha&H80&}";

	EXPECT_EQ("{\\kf20\\c&HFF0000&}a{\\alpha&H80&}b", syl.GetText(true));
}

/// 无内联标签时的基本 k 标签输出不受影响
TEST(lagi_karaoke_syllable, empty_ovr_tags_at_position_zero) {
	KaraokeSyllable syl;
	syl.start_time = 0;
	syl.duration = 100;
	syl.text = "x";
	syl.tag_type = "\\ko";

	EXPECT_EQ("{\\ko10}x", syl.GetText(true));
}

/// 时长的四舍五入：duration=5 → \k1, duration=14 → \k1
TEST(lagi_karaoke_syllable, duration_rounding) {
	KaraokeSyllable syl;
	syl.start_time = 0;
	syl.text = "a";
	syl.tag_type = "\\k";

	syl.duration = 5;
	EXPECT_EQ("{\\k1}a", syl.GetText(true));

	syl.duration = 14;
	EXPECT_EQ("{\\k1}a", syl.GetText(true));

	syl.duration = 15;
	EXPECT_EQ("{\\k2}a", syl.GetText(true));
}
