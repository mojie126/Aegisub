// Copyright (c) 2026, mojie126
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

#include <main.h>
#include <util.h>

#include "ass_dialogue.h"

TEST(AssDialogue, ParseLayerAndTime) {
	AssDialogue line("Dialogue: 3,0:00:01.00,0:00:02.50,Default,,0,0,0,,Hello");
	EXPECT_EQ(line.Comment, false);
	EXPECT_EQ(line.Layer, 3);
	EXPECT_EQ(line.Start, 1000);
	EXPECT_EQ(line.End, 2500);
	EXPECT_EQ(line.Text.get(), "Hello");
}

TEST(AssDialogue, ParseCommentLine) {
	AssDialogue line("Comment: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,Note");
	EXPECT_EQ(line.Comment, true);
	EXPECT_EQ(line.Text.get(), "Note");
}

TEST(AssDialogue, ParseTextWithCommasKeptToLineEnd) {
	// 文本字段可含逗号，从第 10 个字段（Text）起点取到行尾，
	// 修复前混用跨视图迭代器构造 std::string，
	// MSVC debug 下 _Verify_range 断言 "string_view iterators in range are from different views"
	AssDialogue line("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,Hello, world, with commas");
	EXPECT_EQ(line.Text.get(), "Hello, world, with commas");
}

TEST(AssDialogue, ParseTextStartsWithOverrideBlock) {
	AssDialogue line("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,{\\an8}Text");
	EXPECT_EQ(line.Text.get(), "{\\an8}Text");
}

TEST(AssDialogue, ParseEmptyText) {
	AssDialogue line("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,,");
	EXPECT_EQ(line.Text.get(), "");
}

TEST(AssDialogue, ParseTextAfterEffectField) {
	// Effect 字段后的逗号之后为文本，文本可含逗号
	AssDialogue line("Dialogue: 0,0:00:00.00,0:00:01.00,Default,,0,0,0,effect,body text");
	EXPECT_EQ(line.Effect.get(), "effect");
	EXPECT_EQ(line.Text.get(), "body text");
}
