// Copyright (c) 2010, Amar Takhar <verm@aegisub.org>
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

#include "libaegisub/charset.h"

#include "libaegisub/file_mapping.h"
#include "libaegisub/scoped_ptr.h"

#ifdef WITH_UCHARDET
#include <uchardet.h>
#endif

#include <algorithm>

/// @brief 验证数据是否为合法 UTF-8 编码
/// @param data 待检测数据
/// @param len 数据长度
/// @param has_multibyte 输出：是否包含多字节序列
/// @return 所有字节是否符合 UTF-8 编码规则
static bool is_valid_utf8(const char* data, size_t len, bool& has_multibyte) {
	has_multibyte = false;
	for (size_t i = 0; i < len; ) {
		auto c = static_cast<unsigned char>(data[i]);
		int expected;
		if (c <= 0x7F) {
			expected = 0;
		} else if (c >= 0xC2 && c <= 0xDF) {
			expected = 1;
		} else if (c >= 0xE0 && c <= 0xEF) {
			expected = 2;
		} else if (c >= 0xF0 && c <= 0xF4) {
			expected = 3;
		} else {
			return false;
		}
		if (i + expected >= len) break;
		for (int j = 1; j <= expected; ++j) {
			auto next = static_cast<unsigned char>(data[i + j]);
			if (next < 0x80 || next > 0xBF) return false;
		}
		if (expected > 0) has_multibyte = true;
		i += 1 + expected;
	}
	return true;
}

namespace agi::charset {
std::string Detect(agi::fs::path const& file) {
	agi::read_file_mapping fp(file);

	// First check for known magic bytes which identify the file type
	if (fp.size() >= 4) {
		const char* header = fp.read(0, 4);
		if (!strncmp(header, "\xef\xbb\xbf", 3))
			return "utf-8";
		if (!strncmp(header, "\x00\x00\xfe\xff", 4))
			return "utf-32be";
		if (!strncmp(header, "\xff\xfe\x00\x00", 4))
			return "utf-32le";
		if (!strncmp(header, "\xfe\xff", 2))
			return "utf-16be";
		if (!strncmp(header, "\xff\xfe", 2))
			return "utf-16le";
		if (!strncmp(header, "\x1a\x45\xdf\xa3", 4))
			return "binary"; // Actually EBML/Matroska
	}

	// If it's over 100 MB it's either binary or big enough that we won't
	// be able to do anything useful with it anyway
	if (fp.size() > 100 * 1024 * 1024)
		return "binary";

	// 在调用 uchardet 之前预验证 UTF-8：检查前 64KB 是否为合法 UTF-8
	// 可避免 uchardet 对含 emoji 等多字节字符的 UTF-8 文件误判
	{
		auto check_len = std::min<uint64_t>(65536, fp.size());
		auto check_buf = fp.read(0, check_len);
		bool has_multibyte = false;
		if (is_valid_utf8(check_buf, check_len, has_multibyte) && has_multibyte)
			return "utf-8";
	}

	uint64_t binaryish = 0;

#ifdef WITH_UCHARDET
	agi::scoped_holder<uchardet_t> ud(uchardet_new(), uchardet_delete);
	for (uint64_t offset = 0; offset < fp.size(); ) {
		auto read = std::min<uint64_t>(4096, fp.size() - offset);
		auto buf = fp.read(offset, read);
		uchardet_handle_data(ud, buf, read);

		offset += read;

		// A dumb heuristic to detect binary files
		for (size_t i = 0; i < read; ++i) {
			if ((unsigned char)buf[i] < 32 && (buf[i] != '\r' && buf[i] != '\n' && buf[i] != '\t'))
				++binaryish;
		}

		if (binaryish > offset / 8)
			return "binary";
	}
	uchardet_data_end(ud);
	return uchardet_get_charset(ud);
#else
	auto read = std::min<uint64_t>(4096, fp.size());
	auto buf = fp.read(0, read);
	for (size_t i = 0; i < read; ++i) {
		if ((unsigned char)buf[i] < 32 && (buf[i] != '\r' && buf[i] != '\n' && buf[i] != '\t'))
			++binaryish;
	}

	if (binaryish > read / 8)
		return "binary";
	return "utf-8";
#endif
}
}
