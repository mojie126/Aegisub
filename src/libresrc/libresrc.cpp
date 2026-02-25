// Copyright (c) 2009, Amar Takhar <verm@aegisub.org>
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

#include "libresrc.h"

#include <map>
#include <tuple>

#ifdef _WIN32
#include <windows.h>
// GetDpiForSystem 需要 _WIN32_WINNT >= 0x0A00，此处手动声明以兼容当前 SDK 设置
extern "C" UINT WINAPI GetDpiForSystem(void);
#endif
#include <wx/bitmap.h>
#include <wx/bmpbndl.h>
#include <wx/icon.h>
#include <wx/iconbndl.h>
#include <wx/image.h>
#include <wx/intl.h>
#include <wx/mstream.h>

// 检查操作系统版本
bool IsWindows10OrGreater() {
	OSVERSIONINFOEXW osvi = {sizeof(osvi), 10, 0, 0, 0, {}, 0, 0};
	DWORDLONG const dwlConditionMask = VerSetConditionMask(
		VerSetConditionMask(
			VerSetConditionMask(
				0, VER_MAJORVERSION, VER_GREATER_EQUAL
			),
			VER_MINORVERSION, VER_GREATER_EQUAL
		),
		VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL
	);

	return VerifyVersionInfoW(&osvi, VER_MAJORVERSION | VER_MINORVERSION | VER_SERVICEPACKMAJOR, dwlConditionMask) != FALSE;
}

float getScaleFactor() {
	if (IsWindows10OrGreater())
		return static_cast<float>(static_cast<double>(GetDpiForSystem()) / 96.0);
	return 1;
}

wxBitmap libresrc_getimage(const unsigned char *buff, size_t size, double scale, int dir) {
	wxMemoryInputStream mem(buff, size);
	const auto wx_image = wxImage(mem);
	wxBitmap wx_bitmap;
	const float scaleFactor = getScaleFactor();
	if (dir != wxLayout_RightToLeft) {
		wx_bitmap = wxBitmap(wx_image, wxBITMAP_SCREEN_DEPTH, scale);
		if (scaleFactor > 1.5f)
			wx_bitmap.GetGDIImageData()->SetSize(wx_image.GetWidth() / 2, wx_image.GetHeight() / 2);
		return wx_bitmap;
	}
	wx_bitmap = wxBitmap(wx_image.Mirror(), wxBITMAP_SCREEN_DEPTH, scale);
	if (scaleFactor > 1.5f)
		wx_bitmap.GetGDIImageData()->SetSize(wx_image.GetWidth() / 2, wx_image.GetHeight() / 2);
	return wx_bitmap;
}

wxIcon libresrc_geticon(const unsigned char *buff, size_t size) {
	wxMemoryInputStream mem(buff, size);
	wxIcon icon;
	icon.CopyFromBitmap(wxBitmap(wxImage(mem)));
	return icon;
}

wxBitmapBundle libresrc_getbitmapbundle(const LibresrcBlob *images, size_t count, int height, int dir) {
	// 此函数应仅在GUI线程调用，使用thread_local确保安全
	thread_local std::map<std::tuple<const LibresrcBlob *, int, int>, wxBitmapBundle> cache;
	auto key = std::make_tuple(images, height, dir);

	if (auto cached = cache.find(key); cached != cache.end()) {
		return cached->second;
	}

	wxVector<wxBitmap> bitmaps;
	bitmaps.reserve(count);
	for (size_t i = 0; i < count; i++) {
		bitmaps.push_back(libresrc_getimage(images[i].data, images[i].size, 1.0, dir));
		bitmaps.back().SetScaleFactor(double(images[i].scale) / height);
	}

	auto bundle = wxBitmapBundle::FromBitmaps(bitmaps);
	cache[key] = bundle;

	return bundle;
}

wxIconBundle libresrc_geticonbundle(const LibresrcBlob *images, size_t count) {
	thread_local std::map<const LibresrcBlob *, wxIconBundle> cache;

	if (auto cached = cache.find(images); cached != cache.end()) {
		return cached->second;
	}

	wxIconBundle bundle;
	for (size_t i = 0; i < count; i++) {
		bundle.AddIcon(libresrc_geticon(images[i].data, images[i].size));
	}

	cache[images] = bundle;

	return bundle;
}
