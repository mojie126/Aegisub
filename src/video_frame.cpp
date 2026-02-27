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

#include "video_frame.h"

#include <boost/gil.hpp>
#include <wx/image.h>

namespace {
	// We actually have bgr_, not bgra, so we need a custom converter which ignores the alpha channel
	struct color_converter {
		template <typename P1, typename P2>
		void operator()(P1 const& src, P2& dst) const {
			using namespace boost::gil;
			dst = rgb8_pixel_t(
				get_color(src, red_t()),
				get_color(src, green_t()),
				get_color(src, blue_t()));
		}
	};
}

wxImage GetImage(VideoFrame const& frame) {
	using namespace boost::gil;

	// 确定输出维度（90/270°旋转时宽高互换）
	const bool has_rotation = (frame.rotation == 90 || frame.rotation == 270);
	const bool has_transform = has_rotation || frame.hflipped || frame.flipped;
	const size_t out_w = has_rotation ? frame.height : frame.width;
	const size_t out_h = has_rotation ? frame.width : frame.height;

	wxImage img(out_w, out_h);

	if (has_transform) {
		// 有变换时，手动逐像素处理（截图/导出路径，性能非关键）
		uint8_t* dst = img.GetData();
		for (size_t oy = 0; oy < out_h; ++oy) {
			for (size_t ox = 0; ox < out_w; ++ox) {
				size_t sx, sy;
				// 逆旋转：输出坐标→源数据坐标
				if (frame.rotation == 90) {
					sx = oy; sy = frame.height - 1 - ox;
				} else if (frame.rotation == 270) {
					sx = frame.width - 1 - oy; sy = ox;
				} else {
					sx = ox; sy = oy;
				}
				// 逆翻转
				if (frame.hflipped) sx = frame.width - 1 - sx;
				if (frame.flipped) sy = frame.height - 1 - sy;

				const uint8_t* src_px = frame.data.data() + sy * frame.pitch + sx * 4;
				uint8_t* dst_px = dst + (oy * out_w + ox) * 3;
				dst_px[0] = src_px[2]; // R（源BGRA中offset 2）
				dst_px[1] = src_px[1]; // G
				dst_px[2] = src_px[0]; // B
			}
		}
	} else {
		// 无变换：使用boost::gil高效色彩转换
		auto src = interleaved_view(frame.width, frame.height, (bgra8_pixel_t*)frame.data.data(), frame.pitch);
		auto dst = interleaved_view(frame.width, frame.height, (rgb8_pixel_t*)img.GetData(), 3 * frame.width);
		copy_and_convert_pixels(src, dst, color_converter());
	}

	return img;
}

wxImage GetImageWithAlpha(VideoFrame const &frame) {
	wxImage img = GetImage(frame);
	img.InitAlpha();
	uint8_t *dst = img.GetAlpha();

	const bool has_rotation = (frame.rotation == 90 || frame.rotation == 270);
	const bool has_transform = has_rotation || frame.hflipped || frame.flipped;
	const size_t out_w = img.GetWidth();
	const size_t out_h = img.GetHeight();

	if (has_transform) {
		// 有变换时，从正确的源位置读取alpha通道
		for (size_t oy = 0; oy < out_h; ++oy) {
			for (size_t ox = 0; ox < out_w; ++ox) {
				size_t sx, sy;
				if (frame.rotation == 90) {
					sx = oy; sy = frame.height - 1 - ox;
				} else if (frame.rotation == 270) {
					sx = frame.width - 1 - oy; sy = ox;
				} else {
					sx = ox; sy = oy;
				}
				if (frame.hflipped) sx = frame.width - 1 - sx;
				if (frame.flipped) sy = frame.height - 1 - sy;

				dst[oy * out_w + ox] = frame.data[sy * frame.pitch + sx * 4 + 3];
			}
		}
	} else {
		// 无变换，直接线性读取alpha（按行处理 pitch 对齐）
		const uint8_t *row = frame.data.data();
		for (size_t y = 0; y < frame.height; y++) {
			const uint8_t *src = row + 3;
			for (size_t x = 0; x < frame.width; x++) {
				*(dst++) = *src;
				src += 4;
			}
			row += frame.pitch;
		}
	}

	return img;
}

wxImage AddPaddingToImage(const wxImage &img, const int padding) {
	if (padding <= 0 || !img.IsOk()) return img;

	const int src_w = img.GetWidth();
	const int src_h = img.GetHeight();
	const int dst_h = src_h + padding * 2;

	wxImage padded(src_w, dst_h);
	// 将整个图像初始化为黑色
	memset(padded.GetData(), 0, static_cast<size_t>(src_w) * dst_h * 3);

	// 将原始图像数据复制到垂直居中位置（跳过顶部 padding 行）
	const unsigned char *src_data = img.GetData();
	unsigned char *dst_data = padded.GetData() + static_cast<size_t>(padding) * src_w * 3;
	memcpy(dst_data, src_data, static_cast<size_t>(src_w) * src_h * 3);

	return padded;
}
