// Copyright (c) 2026, arch1t3cht <arch1t3cht@gmail.com>
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
// Aegisub Project https://aegisub.org/

#pragma once

#include <optional>
#include <string>
#include <variant>

namespace agi {

/// 色彩矩阵常量，与 ffmpeg (libavutil AVColorSpace) 及 H.273 标准对齐
enum class ycbcr_matrix : char {
	RGB = 0,
	BT709 = 1,
	Unspecified = 2,
	FCC = 4,
	BT470BG = 5,
	SMPTE170M = 6,
	SMPTE240M = 7,
	YCoCg = 8,
	BT2020_NCL = 9,
	BT2020_CL = 10,
	SMPTE2085 = 11,
	ChromacityDerivedNCL = 12,
	ChromacityDerivedCL = 13,
	ICtCp = 14,
};

/// 色彩范围常量，与 ffmpeg (libavutil AVColorRange) 及 H.273 标准对齐
enum class ycbcr_range : char {
	Unspecified = 0,
	MPEG = 1,	// TV / Limited
	JPEG = 2,	// PC / Full
};

namespace ycbcr {

#define EQOP(structname) bool operator==(const structname &) const = default

struct header_missing { EQOP(header_missing); };
struct header_invalid { EQOP(header_invalid); };
struct header_none { EQOP(header_none); };
struct header_colorspace {
	ycbcr_matrix matrix;
	ycbcr_range range;

	static header_colorspace unspecified() { return {ycbcr_matrix::Unspecified, ycbcr_range::Unspecified}; }

	EQOP(header_colorspace);
};

#undef EQOP

using header_variant = std::variant<header_missing, header_invalid, header_none, header_colorspace>;

/// @brief 字幕文件 YCbCr Matrix 头的值表示
///
/// Header 在非 missing/invalid 状态下为 None 或 matrix+range 对。
/// 不保证该 matrix+range 对一定有对应的 YCbCr Matrix 头字符串值。
struct Header : header_variant {
	Header(header_variant v);

	/// 便捷构造
	Header(ycbcr_matrix cm, ycbcr_range cr) : Header(header_colorspace{cm, cr}) {}

	/// @brief 解析 YCbCr Matrix 头字符串
	explicit Header(std::string const& matrix);

	/// @brief 该色彩空间是否能编码为合法的 YCbCr Matrix 头（包括缺失值）
	bool valid() const;

	/// @brief 渲染器实际使用的等效 Header
	///
	/// 用于确定给定 YCbCr Matrix 头的文件应以何种色彩矩阵/范围解码视频
	/// (@see override_colorspace)。
	Header to_effective() const;

	/// @brief 最匹配的合法 Header
	///
	/// 用于确定给定视频色彩矩阵和范围时，文件中应设置的 YCbCr Matrix 值，
	/// 假设所有实现均理想行为。也会输出 TV.FCC 或 PC.240M 等值。
	Header to_existing() const;

	/// @brief 推荐使用的最佳实践 Header
	///
	/// 根据 libass 文件格式指南的最佳实践推荐。
	/// 仅在 601/709 矩阵时返回对应头，其余返回 None。
	/// @see https://github.com/libass/libass/wiki/ASS-File-Format-Guide
	Header to_best_practice() const;

	/// @brief 将已解析的 Header 转换为字符串，无效时返回 std::nullopt
	std::optional<std::string> to_string() const;

	std::string to_existing_string() const { return to_existing().to_string().value(); };

	std::string to_best_practice_string() const { return to_best_practice().to_string().value(); };

	/// @brief 确定色彩蒙版使用的色彩空间
	///
	/// 若 Header 为 None 且输入矩阵或范围未指定，则进行猜测。
	void override_colorspace(ycbcr_matrix &CM, ycbcr_range &CR, int Width, int Height) const;
};

void guess_colorspace(ycbcr_matrix &CM, ycbcr_range &CR, int Width, int Height);

}
}
