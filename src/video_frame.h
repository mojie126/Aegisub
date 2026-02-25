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

#include <vector>

class wxImage;

struct VideoFrame {
	std::vector<unsigned char> data;
	int width;
	int height;
	int pitch;
	bool flipped;
	bool hflipped = false;   // GPU水平翻转标志，由视频提供者设置，渲染时glOrtho投影变换处理
	int rotation = 0;        // GPU旋转角度(0/90/270)，由视频提供者设置，渲染时FBO后处理
	int padding = 0;         // GPU黑边上下各padding行像素，由视频提供者设置，GPU侧渲染处理
};

wxImage GetImage(VideoFrame const& frame);
wxImage GetImageWithAlpha(VideoFrame const& frame);

/// 为图像添加上下黑边填充（用于 ABB 黑边功能）
/// @param img 原始图像
/// @param padding 上下各添加的黑色像素行数
/// @return 添加黑边后的新图像；padding <= 0 时返回原图副本
wxImage AddPaddingToImage(const wxImage &img, int padding);
