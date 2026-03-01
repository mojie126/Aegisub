// Copyright (c) 2025, mojie126
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

/// @file text_file_writer.cpp
/// @brief TextFileWriter 的 BOM 写入参数单元测试
/// @ingroup tests

#include <main.h>

#include "text_file_writer.h"

#include <libaegisub/option.h>
#include <libaegisub/mru.h>
#include <libaegisub/path.h>

#include <fstream>
#include <sstream>
#include <string>
#include <cstdio>

// 提供 config::opt 的最小化桩定义，避免链接错误。
// 测试中始终传入显式 encoding 参数，不会实际解引用此指针。
namespace config {
	agi::Options *opt = nullptr;
	agi::MRUManager *mru = nullptr;
	agi::Path *path = nullptr;
}

namespace {

/// @brief 读取文件的全部原始字节
std::string ReadFileBytes(const std::string &path) {
	std::ifstream ifs(path, std::ios::binary);
	std::ostringstream oss;
	oss << ifs.rdbuf();
	return oss.str();
}

/// @brief UTF-8 BOM 字节序列
const std::string kUtf8Bom = "\xEF\xBB\xBF";

} // namespace

/// 验证默认行为（writeBom=true）写入 UTF-8 BOM
TEST(lagi_text_file_writer, write_with_bom_default) {
	const std::string path = "data/text_file_writer_bom_default.txt";
	{
		TextFileWriter writer(path, "utf-8");
		writer.WriteLineToFile("hello");
	}
	std::string content = ReadFileBytes(path);
	EXPECT_GE(content.size(), kUtf8Bom.size());
	EXPECT_EQ(kUtf8Bom, content.substr(0, 3));
	std::remove(path.c_str());
}

/// 验证显式 writeBom=true 写入 UTF-8 BOM
TEST(lagi_text_file_writer, write_with_bom_explicit_true) {
	const std::string path = "data/text_file_writer_bom_true.txt";
	{
		TextFileWriter writer(path, "utf-8", true);
		writer.WriteLineToFile("hello");
	}
	std::string content = ReadFileBytes(path);
	EXPECT_GE(content.size(), kUtf8Bom.size());
	EXPECT_EQ(kUtf8Bom, content.substr(0, 3));
	std::remove(path.c_str());
}

/// 验证 writeBom=false 不写入 BOM
TEST(lagi_text_file_writer, write_without_bom) {
	const std::string path = "data/text_file_writer_no_bom.txt";
	{
		TextFileWriter writer(path, "utf-8", false);
		writer.WriteLineToFile("hello");
	}
	std::string content = ReadFileBytes(path);
	// 文件不应以 BOM 开头
	if (content.size() >= 3) {
		EXPECT_NE(kUtf8Bom, content.substr(0, 3));
	}
	// 首字节应为 'h'（内容起始）
	ASSERT_FALSE(content.empty());
	EXPECT_EQ('h', content[0]);
	std::remove(path.c_str());
}

/// 验证 writeBom=false 时文件内容完整性
TEST(lagi_text_file_writer, no_bom_content_integrity) {
	const std::string path = "data/text_file_writer_integrity.txt";
	{
		TextFileWriter writer(path, "utf-8", false);
		writer.WriteLineToFile("line1");
		writer.WriteLineToFile("line2");
	}
	std::string content = ReadFileBytes(path);
#ifdef _WIN32
	const std::string expected = "line1\r\nline2\r\n";
#else
	const std::string expected = "line1\nline2\n";
#endif
	EXPECT_EQ(expected, content);
	std::remove(path.c_str());
}

/// 验证带 BOM 时内容在 BOM 之后
TEST(lagi_text_file_writer, bom_then_content) {
	const std::string path = "data/text_file_writer_bom_content.txt";
	{
		TextFileWriter writer(path, "utf-8", true);
		writer.WriteLineToFile("test");
	}
	std::string content = ReadFileBytes(path);
#ifdef _WIN32
	const std::string expected = kUtf8Bom + "test\r\n";
#else
	const std::string expected = kUtf8Bom + "test\n";
#endif
	EXPECT_EQ(expected, content);
	std::remove(path.c_str());
}
