// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
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

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

class wxImage;

/// @brief 自适应黑边计算结果
struct AdaptivePadding {
	int top;     ///< 顶部黑边行数
	int bottom;  ///< 底部黑边行数
};

/// @brief 标准显示高度列表（升序）
static constexpr std::array<int, 6> kStandardHeights = {480, 720, 1080, 1440, 2160, 4320};

/// @brief 计算自适应黑边分配
///
/// 根据帧高度和用户期望的单侧黑边值，自动匹配最近的标准分辨率高度，
/// 将总黑边平均分配到上下两侧。若帧高度已超过最大标准高度或无法匹配，
/// 则回退为对称分配（top = bottom = user_padding）。
///
/// @param frame_height 原始帧高度（不含黑边）
/// @param user_padding 用户期望的单侧黑边行数
/// @return 自适应分配后的上下黑边行数
inline AdaptivePadding CalculateAdaptivePadding(int frame_height, int user_padding) {
	if (user_padding <= 0 || frame_height <= 0)
		return {0, 0};

	const int symmetric_total_h = frame_height + user_padding * 2;

	// 查找最近的标准高度（必须 >= frame_height 且与 symmetric_total_h 偏差在 user_padding 以内）
	int best_standard = 0;
	int best_diff = std::numeric_limits<int>::max();
	for (int sh : kStandardHeights) {
		if (sh < frame_height)
			continue;
		int diff = std::abs(sh - symmetric_total_h);
		if (diff < best_diff && diff <= user_padding) {
			best_diff = diff;
			best_standard = sh;
		}
	}

	if (best_standard > 0 && best_standard > frame_height) {
		int total_padding = best_standard - frame_height;
		int half = total_padding / 2;
		int remainder = total_padding % 2;
		// 奇数像素差额分配到顶部
		return {half + remainder, half};
	}

	// 无匹配标准高度，回退为对称分配
	return {user_padding, user_padding};
}

struct VideoFrame {
	std::vector<unsigned char> data;
	int width;
	int height;
	int pitch;
	bool flipped;
	bool hflipped = false;   // GPU水平翻转标志，由视频提供者设置，渲染时glOrtho投影变换处理
	int rotation = 0;        // GPU旋转角度(0/90/270)，由视频提供者设置，渲染时FBO后处理
	int padding_top = 0;     ///< GPU顶部黑边行数，由视频提供者设置，GPU侧渲染处理
	int padding_bottom = 0;  ///< GPU底部黑边行数，由视频提供者设置，GPU侧渲染处理
};

wxImage GetImage(VideoFrame const& frame);
wxImage GetImageWithAlpha(VideoFrame const& frame);

/// @brief 为图像添加上下黑边填充（用于 ABB 黑边功能）
/// @param img 原始图像
/// @param padding_top 顶部添加的黑色像素行数
/// @param padding_bottom 底部添加的黑色像素行数
/// @return 添加黑边后的新图像；padding均 <= 0 时返回原图副本
wxImage AddPaddingToImage(const wxImage &img, int padding_top, int padding_bottom);
