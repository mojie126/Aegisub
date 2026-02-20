// Copyright (c) 2012, Thomas Goyne <plorkyeran@aegisub.org>
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

/// @file video_out_gl.cpp
/// @brief OpenGL based video renderer
/// @ingroup video
///

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

#include <libaegisub/log.h>

// These must be included before local headers.
#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#ifdef __WIN32__
#define glGetProc(name) wglGetProcAddress(name)
#elif !defined(__APPLE__)
#include <GL/glx.h>
#define glGetProc(name) glXGetProcAddress((const GLubyte *)(name))
#endif

#include "video_out_gl.h"
#include "utils.h"
#include "video_frame.h"

namespace {
template<typename Exception>
BOOST_NOINLINE void throw_error(GLenum err, const char *msg) {
	LOG_E("video/out/gl") << msg << " failed with error code " << err;
	throw Exception(msg, err);
}

#if !defined(__APPLE__)
struct PboFunctions {
	PFNGLBINDBUFFERPROC BindBuffer = nullptr;
	PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
	PFNGLGENBUFFERSPROC GenBuffers = nullptr;
	PFNGLBUFFERDATAPROC BufferData = nullptr;
	PFNGLBUFFERSUBDATAPROC BufferSubData = nullptr;
	bool initialized = false;
	bool available = false;
};

static PboFunctions &GetPboFunctions() {
	static PboFunctions funcs;
	if (funcs.initialized)
		return funcs;

	funcs.initialized = true;
	funcs.BindBuffer = reinterpret_cast<PFNGLBINDBUFFERPROC>(glGetProc("glBindBuffer"));
	funcs.DeleteBuffers = reinterpret_cast<PFNGLDELETEBUFFERSPROC>(glGetProc("glDeleteBuffers"));
	funcs.GenBuffers = reinterpret_cast<PFNGLGENBUFFERSPROC>(glGetProc("glGenBuffers"));
	funcs.BufferData = reinterpret_cast<PFNGLBUFFERDATAPROC>(glGetProc("glBufferData"));
	funcs.BufferSubData = reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(glGetProc("glBufferSubData"));
	funcs.available = funcs.BindBuffer && funcs.DeleteBuffers && funcs.GenBuffers && funcs.BufferData && funcs.BufferSubData;
	return funcs;
}
#endif
}

#define DO_CHECK_ERROR(cmd, Exception, msg) \
	do { \
		cmd; \
		GLenum err = glGetError(); \
		if (BOOST_UNLIKELY(err)) \
			throw_error<Exception>(err, msg); \
	} while(0);
#define CHECK_INIT_ERROR(cmd) DO_CHECK_ERROR(cmd, VideoOutInitException, #cmd)
#define CHECK_ERROR(cmd) DO_CHECK_ERROR(cmd, VideoOutRenderException, #cmd)

/// @brief Structure tracking all precomputable information about a subtexture
struct VideoOutGL::TextureInfo {
	GLuint textureID = 0;
	int dataOffset = 0;
	int sourceH = 0;
	int sourceW = 0;
};

/// @brief Test if a texture can be created
/// @param width The width of the texture
/// @param height The height of the texture
/// @param format The texture's format
/// @return Whether the texture could be created.
static bool TestTexture(int width, int height, GLint format) {
	glTexImage2D(GL_PROXY_TEXTURE_2D, 0, format, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
	while (glGetError()) { } // Silently swallow all errors as we don't care why it failed if it did

	LOG_I("video/out/gl") << "VideoOutGL::TestTexture: " << width << "x" << height;
	return format != 0;
}

/// @brief Checks if a specific OpenGL extension is available in the current context.
static bool HasOpenGLExtension(const char *extension_name) {
	if (!extension_name || !*extension_name)
		return false;

	const char *extensions = reinterpret_cast<const char *>(glGetString(GL_EXTENSIONS));
	if (!extensions)
		return false;

	const size_t needle_len = std::strlen(extension_name);
	const char *cursor = extensions;
	while ((cursor = std::strstr(cursor, extension_name)) != nullptr) {
		const char before = cursor == extensions ? ' ' : cursor[-1];
		const char after = cursor[needle_len];
		if (before == ' ' && (after == ' ' || after == '\0'))
			return true;
		cursor += needle_len;
	}

	return false;
}

VideoOutGL::VideoOutGL() { }

/// @brief Runtime detection of required OpenGL capabilities
void VideoOutGL::DetectOpenGLCapabilities() {
	if (maxTextureSize != 0) return;

	// Test for supported internalformats
	if (TestTexture(64, 64, GL_RGBA8)) internalFormat = GL_RGBA8;
	else if (TestTexture(64, 64, GL_RGBA)) internalFormat = GL_RGBA;
	else throw VideoOutInitException("Could not create a 64x64 RGB texture in any format.");

	// Test for the maximum supported texture size
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
	while (maxTextureSize > 64 && !TestTexture(maxTextureSize, maxTextureSize, internalFormat)) maxTextureSize >>= 1;
	LOG_I("video/out/gl") << "Maximum texture size is " << maxTextureSize << "x" << maxTextureSize;

	// Test for rectangular texture support
	supportsRectangularTextures = TestTexture(maxTextureSize, maxTextureSize >> 1, internalFormat);

	// PBO is used as the first step of the direct-GPU upload architecture.
#if defined(__APPLE__)
	supportsPixelUnpackBuffer = false;
#else
	const auto &pbo = GetPboFunctions();
	supportsPixelUnpackBuffer = HasOpenGLExtension("GL_ARB_pixel_buffer_object");
	supportsPixelUnpackBuffer = supportsPixelUnpackBuffer && pbo.available;
#endif
	LOG_I("video/out/gl") << "Pixel unpack buffer support: " << (supportsPixelUnpackBuffer ? "yes" : "no");
}

void VideoOutGL::ReleaseUploadPbo() {
	if (uploadPboIds.empty())
		return;

#if !defined(__APPLE__)
	const auto &pbo = GetPboFunctions();
	if (pbo.available)
		pbo.DeleteBuffers(static_cast<GLsizei>(uploadPboIds.size()), uploadPboIds.data());
#endif
	while (glGetError()) { }
	uploadPboIds.clear();
	uploadPboSize = 0;
	uploadPboIndex = 0;
}

void VideoOutGL::EnsureUploadPbo(size_t requiredSize) {
	if (!supportsPixelUnpackBuffer || requiredSize == 0)
		return;

	if (!uploadPboIds.empty() && uploadPboSize == requiredSize)
		return;

	ReleaseUploadPbo();
	uploadPboIds.resize(2, 0);
#if !defined(__APPLE__)
	const auto &funcs = GetPboFunctions();
	if (!funcs.available)
		throw VideoOutInitException("Pixel unpack buffer functions are unavailable.");
	funcs.GenBuffers(static_cast<GLsizei>(uploadPboIds.size()), uploadPboIds.data());
	if (GLenum err = glGetError()) throw VideoOutInitException("glGenBuffers", err);
	for (GLuint pbo_id : uploadPboIds) {
		funcs.BindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_id);
		if (GLenum err = glGetError()) throw VideoOutInitException("glBindBuffer", err);
		funcs.BufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(requiredSize), nullptr, GL_STREAM_DRAW);
		if (GLenum err = glGetError()) throw VideoOutInitException("glBufferData", err);
	}
	funcs.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
	if (GLenum err = glGetError()) throw VideoOutInitException("glBindBuffer", err);
#endif
	uploadPboSize = requiredSize;
	uploadPboIndex = 0;
}

/// @brief If needed, create the grid of textures for displaying frames of the given format
/// @param width The frame's width
/// @param height The frame's height
/// @param format The frame's format
/// @param bpp The frame's bytes per pixel
void VideoOutGL::InitTextures(int width, int height, GLenum format, int bpp, bool flipped) {
	using namespace std;

	// Do nothing if the frame size and format are unchanged
	if (width == frameWidth && height == frameHeight && format == frameFormat && flipped == frameFlipped) return;
	frameWidth  = width;
	frameHeight = height;
	frameFormat = format;
	frameFlipped = flipped;
	LOG_I("video/out/gl") << "Video size: " << width << "x" << height;

	DetectOpenGLCapabilities();

	// Clean up old textures
	if (textureIdList.size() > 0) {
		CHECK_INIT_ERROR(glDeleteTextures(textureIdList.size(), &textureIdList[0]));
		textureIdList.clear();
		textureList.clear();
	}
	ReleaseUploadPbo();

	// Create the textures
	int textureArea = maxTextureSize - 2;
	textureRows  = (int)ceil(double(height) / textureArea);
	textureCols  = (int)ceil(double(width) / textureArea);
	textureCount = textureRows * textureCols;
	textureIdList.resize(textureCount);
	textureList.resize(textureCount);
	CHECK_INIT_ERROR(glGenTextures(textureIdList.size(), &textureIdList[0]));
	vector<pair<int, int>> textureSizes;
	textureSizes.reserve(textureCount);

	/* Unfortunately, we can't simply use one of the two standard ways to do
	 * tiled textures to work around texture size limits in OpenGL, due to our
	 * need to support Microsoft's OpenGL emulation for RDP/VPC/video card
	 * drivers that don't support OpenGL (such as the ones which Windows
	 * Update pushes for ATI cards in Windows 7). GL_CLAMP_TO_EDGE requires
	 * OpenGL 1.2, but the emulation only supports 1.1. GL_CLAMP + borders has
	 * correct results, but takes several seconds to render each frame. As a
	 * result, the code below essentially manually reimplements borders, by
	 * just not using the edge when mapping the texture onto a quad. The one
	 * exception to this is the texture edges which are also frame edges, as
	 * there does not appear to be a trivial way to mirror the edges, and the
	 * nontrivial ways are more complex that is worth to avoid a single row of
	 * slightly discolored pixels along the edges at zooms over 100%.
	 *
	 * Given a 64x64 maximum texture size:
	 *     Quads touching the top of the frame are 63 pixels tall
	 *     Quads touching the bottom of the frame are up to 63 pixels tall
	 *     All other quads are 62 pixels tall
	 *     Quads not on the top skip the first row of the texture
	 *     Quads not on the bottom skip the last row of the texture
	 *     Width behaves in the same way with respect to left/right edges
	 */

	// Set up the display list
	CHECK_ERROR(dl = glGenLists(1));
	CHECK_ERROR(glNewList(dl, GL_COMPILE));

	CHECK_ERROR(glClearColor(0,0,0,0));
	CHECK_ERROR(glClearStencil(0));
	CHECK_ERROR(glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT));

	CHECK_ERROR(glShadeModel(GL_FLAT));
	CHECK_ERROR(glDisable(GL_BLEND));

	// Switch to video coordinates
	CHECK_ERROR(glMatrixMode(GL_PROJECTION));
	CHECK_ERROR(glLoadIdentity());
	CHECK_ERROR(glPushMatrix());
	if (frameFlipped) {
		CHECK_ERROR(glOrtho(0.0f, frameWidth, 0.0f, frameHeight, -1000.0f, 1000.0f));
	}
	else {
		CHECK_ERROR(glOrtho(0.0f, frameWidth, frameHeight, 0.0f, -1000.0f, 1000.0f));
	}

	CHECK_ERROR(glEnable(GL_TEXTURE_2D));

	// Calculate the position information for each texture
	int lastRow = textureRows - 1;
	int lastCol = textureCols - 1;
	for (int row = 0; row < textureRows; ++row) {
		for (int col = 0; col < textureCols; ++col) {
			TextureInfo& ti = textureList[row * textureCols + col];

			// Width and height of the area read from the frame data
			int sourceX = col * textureArea;
			int sourceY = row * textureArea;
			ti.sourceW  = std::min(frameWidth  - sourceX, maxTextureSize);
			ti.sourceH  = std::min(frameHeight - sourceY, maxTextureSize);

			// Used instead of GL_PACK_SKIP_ROWS/GL_PACK_SKIP_PIXELS due to
			// performance issues with the emulation
			ti.dataOffset = sourceY * frameWidth * bpp + sourceX * bpp;

			int textureHeight = SmallestPowerOf2(ti.sourceH);
			int textureWidth  = SmallestPowerOf2(ti.sourceW);
			if (!supportsRectangularTextures) {
				textureWidth = textureHeight = std::max(textureWidth, textureHeight);
			}

			// Location where this texture is placed
			// X2/Y2 will be offscreen unless the video frame happens to
			// exactly use all of the texture
			float x1 = sourceX + (col != 0);
			float y1 = sourceY + (row != 0);
			float x2 = sourceX + textureWidth - (col != lastCol);
			float y2 = sourceY + textureHeight - (row != lastRow);

			// Portion of the texture actually used
			float top    = row == 0 ? 0 : 1.0f / textureHeight;
			float left   = col == 0 ? 0 : 1.0f / textureWidth;
			float bottom = row == lastRow ? 1.0f : 1.0f - 1.0f / textureHeight;
			float right  = col == lastCol ? 1.0f : 1.0f - 1.0f / textureWidth;

			// Store the stuff needed later
			ti.textureID = textureIdList[row * textureCols + col];
			textureSizes.push_back(make_pair(textureWidth, textureHeight));

			CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, ti.textureID));
			CHECK_ERROR(glColor4f(1.0f, 1.0f, 1.0f, 1.0f));

			// Place the texture
			glBegin(GL_QUADS);
				glTexCoord2f(left,  top);     glVertex2f(x1, y1);
				glTexCoord2f(right, top);     glVertex2f(x2, y1);
				glTexCoord2f(right, bottom);  glVertex2f(x2, y2);
				glTexCoord2f(left,  bottom);  glVertex2f(x1, y2);
			glEnd();
			if (GLenum err = glGetError()) throw VideoOutRenderException("GL_QUADS", err);
		}
	}
	CHECK_ERROR(glDisable(GL_TEXTURE_2D));
	CHECK_ERROR(glPopMatrix());

	glEndList();

	// Create the textures outside of the display list as there's no need to
	// remake them on every frame
	for (int i = 0; i < textureCount; ++i) {
		LOG_I("video/out/gl") << "Using texture size: " << textureSizes[i].first << "x" << textureSizes[i].second;
		CHECK_INIT_ERROR(glBindTexture(GL_TEXTURE_2D, textureIdList[i]));
		CHECK_INIT_ERROR(glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, textureSizes[i].first, textureSizes[i].second, 0, format, GL_UNSIGNED_BYTE, nullptr));
		CHECK_INIT_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		CHECK_INIT_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
		CHECK_INIT_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP));
		CHECK_INIT_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP));
	}
}

void VideoOutGL::UploadFrameData(VideoFrame const& frame) {
	if (frame.height == 0 || frame.width == 0) return;

	InitTextures(frame.width, frame.height, GL_BGRA_EXT, 4, frame.flipped);

	// Set row length only when pitch differs from tightly packed BGRA.
	const int tight_pitch = static_cast<int>(frame.width) * 4;
	const bool needs_row_length = static_cast<int>(frame.pitch) != tight_pitch;
	const size_t frame_bytes = frame.pitch * frame.height;
	const bool can_use_pbo = supportsPixelUnpackBuffer && frame_bytes > 0 && frame.data.size() >= frame_bytes;
	const bool use_pbo = can_use_pbo;
	if (use_pbo) {
		EnsureUploadPbo(frame_bytes);
#if !defined(__APPLE__)
		const auto &pbo = GetPboFunctions();
		pbo.BindBuffer(GL_PIXEL_UNPACK_BUFFER, uploadPboIds[uploadPboIndex]);
		if (GLenum err = glGetError()) throw VideoOutRenderException("glBindBuffer", err);
		// Orphan previous storage to avoid CPU/GPU sync stalls.
		pbo.BufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(frame_bytes), nullptr, GL_STREAM_DRAW);
		if (GLenum err = glGetError()) throw VideoOutRenderException("glBufferData", err);
		pbo.BufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, static_cast<GLsizeiptr>(frame_bytes), frame.data.data());
		if (GLenum err = glGetError()) throw VideoOutRenderException("glBufferSubData", err);
#endif
	}

	if (needs_row_length)
		CHECK_ERROR(glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.pitch / 4));

	for (auto& ti : textureList) {
		CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, ti.textureID));
		const void *upload_ptr = use_pbo
			? reinterpret_cast<void *>(static_cast<uintptr_t>(ti.dataOffset))
			: static_cast<const void *>(&frame.data[ti.dataOffset]);
		CHECK_ERROR(glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ti.sourceW,
			ti.sourceH, GL_BGRA_EXT, GL_UNSIGNED_BYTE, upload_ptr));
	}

	if (needs_row_length)
		CHECK_ERROR(glPixelStorei(GL_UNPACK_ROW_LENGTH, 0));
	if (use_pbo) {
#if !defined(__APPLE__)
		const auto &pbo = GetPboFunctions();
		pbo.BindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
		if (GLenum err = glGetError()) throw VideoOutRenderException("glBindBuffer", err);
#endif
		uploadPboIndex = (uploadPboIndex + 1) % uploadPboIds.size();
	}
}

void VideoOutGL::Render(int dx1, int dy1, int dx2, int dy2) {
	CHECK_ERROR(glViewport(dx1, dy1, dx2, dy2));
	CHECK_ERROR(glCallList(dl));
	CHECK_ERROR(glMatrixMode(GL_MODELVIEW));
	CHECK_ERROR(glLoadIdentity());

}

VideoOutGL::~VideoOutGL() {
	ReleaseUploadPbo();
	if (textureIdList.size() > 0) {
		glDeleteTextures(textureIdList.size(), &textureIdList[0]);
		glDeleteLists(dl, 1);
	}
}
