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

#include <cstdlib>
#include <string_view>

/// @brief 资源数据块，用于位图多分辨率分组
struct LibresrcBlob {
	const unsigned char *data;
	size_t size;
	int scale;
};

#include "bitmap.h"
#include "default_config.h"

#include <wx/version.h>

class wxBitmap;
class wxBitmapBundle;
class wxIcon;
class wxIconBundle;

bool IsWindows10OrGreater();
float getScaleFactor();
wxBitmap libresrc_getimage(const unsigned char *image, size_t size, double scale=1.0, int dir=0);
wxIcon libresrc_geticon(const unsigned char *image, size_t size);
wxBitmapBundle libresrc_getbitmapbundle(const LibresrcBlob *images, size_t count, int height, int dir=0);
wxIconBundle libresrc_geticonbundle(const LibresrcBlob *images, size_t count);
#define GETIMAGE(a) libresrc_getimage(a, sizeof(a))
#define GETIMAGEDIR(a, s, d) libresrc_getimage(a, sizeof(a), s, d)
#define GETICON(a) libresrc_geticon(a, sizeof(a))
#define GETBUNDLE(a, h) libresrc_getbitmapbundle(a, sizeof(a) / sizeof(*a), h)
#define GETBUNDLEDIR(a, h, d) libresrc_getbitmapbundle(a, sizeof(a) / sizeof(*a), h, d)
#define GETICONS(a) libresrc_geticonbundle(a, sizeof(a) / sizeof(*a))
#define ICON(name) ( \
	OPT_GET("App/Toolbar Icon Size")->GetInt() >= 64 ? GETIMAGE(name##_64) : \
	OPT_GET("App/Toolbar Icon Size")->GetInt() >= 48 ? GETIMAGE(name##_48) : \
	OPT_GET("App/Toolbar Icon Size")->GetInt() >= 32 ? GETIMAGE(name##_32) : \
	OPT_GET("App/Toolbar Icon Size")->GetInt() >= 24 ? GETIMAGE(name##_24) : \
	GETIMAGE(name##_16) \
)

#define GET_DEFAULT_CONFIG(a) std::string_view(reinterpret_cast<const char *>(a), sizeof(a))
