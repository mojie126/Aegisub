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
#include <fstream>
#include <cmath>
#include <memory>
#include <vector>

#include <libaegisub/log.h>
#include <libaegisub/fs.h>

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
#include "include/aegisub/video_provider.h"
#include "cube/lut.hpp"
#include "options.h"
#include "utils.h"

#include <libaegisub/path.h>
#include <wx/image.h>
#include "video_frame.h"

namespace {

/// 获取当前CPU侧缓存的LUT类型（与GetCpuCubeLut配对使用）
static HDRType &GetCpuCubeLutType() {
	static HDRType type = HDRType::SDR;
	return type;
}

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

struct ShaderFunctions {
	PFNGLCREATESHADERPROC CreateShader = nullptr;
	PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
	PFNGLCOMPILESHADERPROC CompileShader = nullptr;
	PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
	PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
	PFNGLDELETESHADERPROC DeleteShader = nullptr;

	PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
	PFNGLATTACHSHADERPROC AttachShader = nullptr;
	PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
	PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
	PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
	PFNGLUSEPROGRAMPROC UseProgram = nullptr;
	PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;

	PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
	PFNGLUNIFORM1IPROC Uniform1i = nullptr;
	PFNGLUNIFORM1FPROC Uniform1f = nullptr;

	PFNGLACTIVETEXTUREPROC ActiveTexture = nullptr;
	PFNGLTEXIMAGE3DPROC TexImage3D = nullptr;

	PFNGLGENFRAMEBUFFERSPROC GenFramebuffers = nullptr;
	PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers = nullptr;
	PFNGLBINDFRAMEBUFFERPROC BindFramebuffer = nullptr;
	PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D = nullptr;
	PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus = nullptr;

	bool initialized = false;
	/// shader + 纹理基础函数是否可用
	bool available = false;
	/// FBO函数是否可用（与available分离，避免FBO不可用时连带禁用shader）
	bool fboAvailable = false;
};

static ShaderFunctions &GetShaderFunctions() {
	static ShaderFunctions funcs;
	if (funcs.initialized)
		return funcs;

	funcs.initialized = true;
	funcs.CreateShader = reinterpret_cast<PFNGLCREATESHADERPROC>(glGetProc("glCreateShader"));
	funcs.ShaderSource = reinterpret_cast<PFNGLSHADERSOURCEPROC>(glGetProc("glShaderSource"));
	funcs.CompileShader = reinterpret_cast<PFNGLCOMPILESHADERPROC>(glGetProc("glCompileShader"));
	funcs.GetShaderiv = reinterpret_cast<PFNGLGETSHADERIVPROC>(glGetProc("glGetShaderiv"));
	funcs.GetShaderInfoLog = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(glGetProc("glGetShaderInfoLog"));
	funcs.DeleteShader = reinterpret_cast<PFNGLDELETESHADERPROC>(glGetProc("glDeleteShader"));

	funcs.CreateProgram = reinterpret_cast<PFNGLCREATEPROGRAMPROC>(glGetProc("glCreateProgram"));
	funcs.AttachShader = reinterpret_cast<PFNGLATTACHSHADERPROC>(glGetProc("glAttachShader"));
	funcs.LinkProgram = reinterpret_cast<PFNGLLINKPROGRAMPROC>(glGetProc("glLinkProgram"));
	funcs.GetProgramiv = reinterpret_cast<PFNGLGETPROGRAMIVPROC>(glGetProc("glGetProgramiv"));
	funcs.GetProgramInfoLog = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(glGetProc("glGetProgramInfoLog"));
	funcs.UseProgram = reinterpret_cast<PFNGLUSEPROGRAMPROC>(glGetProc("glUseProgram"));
	funcs.DeleteProgram = reinterpret_cast<PFNGLDELETEPROGRAMPROC>(glGetProc("glDeleteProgram"));

	funcs.GetUniformLocation = reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(glGetProc("glGetUniformLocation"));
	funcs.Uniform1i = reinterpret_cast<PFNGLUNIFORM1IPROC>(glGetProc("glUniform1i"));
	funcs.Uniform1f = reinterpret_cast<PFNGLUNIFORM1FPROC>(glGetProc("glUniform1f"));

	funcs.ActiveTexture = reinterpret_cast<PFNGLACTIVETEXTUREPROC>(glGetProc("glActiveTexture"));
	funcs.TexImage3D = reinterpret_cast<PFNGLTEXIMAGE3DPROC>(glGetProc("glTexImage3D"));

	funcs.GenFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(glGetProc("glGenFramebuffers"));
	if (!funcs.GenFramebuffers)
		funcs.GenFramebuffers = reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(glGetProc("glGenFramebuffersEXT"));
	funcs.DeleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(glGetProc("glDeleteFramebuffers"));
	if (!funcs.DeleteFramebuffers)
		funcs.DeleteFramebuffers = reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(glGetProc("glDeleteFramebuffersEXT"));
	funcs.BindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(glGetProc("glBindFramebuffer"));
	if (!funcs.BindFramebuffer)
		funcs.BindFramebuffer = reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(glGetProc("glBindFramebufferEXT"));
	funcs.FramebufferTexture2D = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(glGetProc("glFramebufferTexture2D"));
	if (!funcs.FramebufferTexture2D)
		funcs.FramebufferTexture2D = reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(glGetProc("glFramebufferTexture2DEXT"));
	funcs.CheckFramebufferStatus = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(glGetProc("glCheckFramebufferStatus"));
	if (!funcs.CheckFramebufferStatus)
		funcs.CheckFramebufferStatus = reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(glGetProc("glCheckFramebufferStatusEXT"));

	// shader + 纹理函数可用性（不包含FBO）
	funcs.available = funcs.CreateShader && funcs.ShaderSource && funcs.CompileShader && funcs.GetShaderiv && funcs.GetShaderInfoLog &&
		funcs.DeleteShader && funcs.CreateProgram && funcs.AttachShader && funcs.LinkProgram && funcs.GetProgramiv &&
		funcs.GetProgramInfoLog && funcs.UseProgram && funcs.DeleteProgram && funcs.GetUniformLocation && funcs.Uniform1i && funcs.Uniform1f &&
		funcs.ActiveTexture && funcs.TexImage3D;

	// FBO函数单独检测
	funcs.fboAvailable = funcs.GenFramebuffers && funcs.DeleteFramebuffers && funcs.BindFramebuffer &&
		funcs.FramebufferTexture2D && funcs.CheckFramebufferStatus;

	LOG_I("video/out/gl") << "Shader functions available: " << funcs.available << " FBO available: " << funcs.fboAvailable;

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

/// @brief Release构建仅执行GL命令，不调用glGetError（避免GPU管线同步）；
///        Debug构建保留逐调用错误检查便于调试定位。
#ifdef NDEBUG
#define CHECK_ERROR(cmd) cmd
#else
#define CHECK_ERROR(cmd) DO_CHECK_ERROR(cmd, VideoOutRenderException, #cmd)
#endif

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

static std::unique_ptr<octoon::image::flut> &GetCpuCubeLut() {
	static std::unique_ptr<octoon::image::flut> cpu_lut;
	return cpu_lut;
}

std::string VideoOutGL::GetLutFilename(HDRType type, int dvProfile) {
	switch (type) {
		case HDRType::DolbyVision:
			// [已知限制] DV Profile 5 内容使用静态 LUT 映射时不同场景间色彩可能存在差异。
			// 根因：DV P5 每帧 RPU 包含不同的 reshaping 曲线，静态 LUT 无法补偿场景级动态映射。
			// 当前 meson-ports FFmpeg 7.1 的 HEVC 解码器不支持 apply_dovi（AVOption 列表无此项），
			// RPU 虽被解析并附加为帧侧数据，但不执行像素级 reshape。
			// 可能的改进方向：
			//   1. 集成 libplacebo，利用其内置的 DV RPU 应用实现完整的色调映射；
			//   2. 手动解析 AV_FRAME_DATA_DOVI_METADATA reshaping 曲线，在 shader 中实现
			//      IPT-PQ-C2 → BT.2020 PQ 转换后改用 PQ2SDR.cube。
			//
			// DV Profile感知的LUT选择：
			//   P5: 纯IPT-PQ-C2单层，必须使用DV2SDR.cube
			//   P7: 双层，HDR10基层，解码器未应用reshape时像素为标准PQ
			//   P8.1: 单层HDR10兼容，像素数据为标准PQ编码
			//   P8.4: HLG兼容，像素数据为HLG编码
			if (dvProfile == 7 || dvProfile == 8) {
				// P7/P8.x: 解码器输出的基层为标准PQ（或HLG）编码
				// 无法区分P8.1和P8.4，默认采用PQ映射
				LOG_D("video/out/gl") << "DV profile " << dvProfile
					<< " detected, using PQ2SDR.cube (HDR10-compatible base layer)";
				return "PQ2SDR.cube";
			}
			// P5或未知Profile：使用专用DV LUT
			return "DV2SDR.cube";
		case HDRType::HLG:        return "HLG2SDR.cube";
		case HDRType::PQ:
		default:                   return "PQ2SDR.cube";
	}
}

std::string VideoOutGL::FindCubeLutPath(const std::string &filename) {
	// 从?data路径查找（安装版和便携版均可用）
	// cube文件安装到 bindir/data/cube/，?data指向exe所在目录
	if (config::path) {
		// 优先：?data/data/cube/（标准安装布局，exe同级data子目录）
		auto data_sub_path = config::path->Decode("?data/data/cube/" + filename);
		if (agi::fs::FileExists(data_sub_path))
			return data_sub_path.string();
		// 兼容：?data/cube/（直接放在exe同级cube子目录）
		auto data_path = config::path->Decode("?data/cube/" + filename);
		if (agi::fs::FileExists(data_path))
			return data_path.string();
	}
	// 开发环境回退路径
	std::vector<std::string> fallback_paths = {
		"data/cube/" + filename,
		"src/cube/" + filename,
		"../src/cube/" + filename,
		"../../src/cube/" + filename
	};
	for (const auto& p : fallback_paths) {
		std::ifstream f(p);
		if (f.good()) return p;
	}
	return "";
}

VideoOutGL::VideoOutGL() { }

void VideoOutGL::EnableHDRToneMapping(bool enable) {
	if (!enable) {
		hdrToneMappingEnabled = false;
		return;
	}

	// 先设置标志，GL资源在Render()中延迟初始化（此时GL上下文保证激活）
	hdrToneMappingEnabled = true;
}

void VideoOutGL::SetHDRInputHint(bool isHdr, HDRType type, int dvProfile) {
	hdrInputLikelyHdr = isHdr;
	if (hdrInputType != type || hdrDvProfile != dvProfile) {
		hdrInputType = type;
		hdrDvProfile = dvProfile;
		// HDR类型或DV Profile变更时，需重新加载对应LUT（GPU和CPU缓存都清除）
		// DV P5使用DV2SDR.cube，P7/P8使用PQ2SDR.cube，仅检查type不足以覆盖profile切换
		if (hdrLutLoaded) {
			ReleaseHDRLUT();
			LOG_I("video/out/gl") << "HDR type/profile changed to " << static_cast<int>(type)
				<< " (DV profile=" << dvProfile << "), LUT will be reloaded on next render";
		}
	}
}

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

	// 检测NPOT纹理支持（OpenGL 2.0核心功能或GL_ARB_texture_non_power_of_two扩展）
	// 若支持可跳过3D LUT纹理的POT扩展，节省显存
	supportsNpotTextures = HasOpenGLExtension("GL_ARB_texture_non_power_of_two");
	LOG_I("video/out/gl") << "NPOT texture support: " << (supportsNpotTextures ? "yes" : "no");
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
void VideoOutGL::InitTextures(int width, int height, GLenum format, int bpp, bool flipped, bool hflipped) {
	using namespace std;

	// Do nothing if the frame size and format are unchanged
	if (width == frameWidth && height == frameHeight && format == frameFormat && flipped == frameFlipped && hflipped == frameHFlipped) return;
	frameWidth  = width;
	frameHeight = height;
	frameFormat = format;
	frameFlipped = flipped;
	frameHFlipped = hflipped;
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
	{
		// 通过glOrtho投影参数实现水平/垂直翻转，无CPU开销
		double ortho_left   = frameHFlipped ? static_cast<double>(frameWidth)  : 0.0;
		double ortho_right  = frameHFlipped ? 0.0 : static_cast<double>(frameWidth);
		double ortho_bottom = frameFlipped  ? 0.0 : static_cast<double>(frameHeight);
		double ortho_top    = frameFlipped  ? static_cast<double>(frameHeight) : 0.0;
		CHECK_ERROR(glOrtho(ortho_left, ortho_right, ortho_bottom, ortho_top, -1000.0f, 1000.0f));
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

	// 存储原始翻转/旋转状态（用于FBO旋转路径的纹理坐标计算）
	frameRotation = frame.rotation;
	frameSourceVFlip = frame.flipped;
	frameSourceHFlip = frame.hflipped;

	// 90/270°旋转时，翻转推迟到FBO全屏四边形阶段处理，display list不应用翻转
	bool dl_flipped = frame.flipped;
	bool dl_hflipped = frame.hflipped;
	if (frame.rotation == 90 || frame.rotation == 270) {
		dl_flipped = false;
		dl_hflipped = false;
	}
	InitTextures(frame.width, frame.height, GL_BGRA_EXT, 4, dl_flipped, dl_hflipped);
	frameVideoPaddingTop = frame.padding_top;
	frameVideoPaddingBottom = frame.padding_bottom;

	// GPU HDR方案：始终上传原始帧数据，色调映射由Render阶段的FBO+shader完成
	const unsigned char *upload_data = frame.data.data();

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
		pbo.BufferSubData(GL_PIXEL_UNPACK_BUFFER, 0, static_cast<GLsizeiptr>(frame_bytes), upload_data);
		if (GLenum err = glGetError()) throw VideoOutRenderException("glBufferSubData", err);
#endif
	}

	if (needs_row_length)
		CHECK_ERROR(glPixelStorei(GL_UNPACK_ROW_LENGTH, frame.pitch / 4));

	for (auto& ti : textureList) {
		CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, ti.textureID));
		const void *upload_ptr = use_pbo
			? reinterpret_cast<void *>(static_cast<uintptr_t>(ti.dataOffset))
			: static_cast<const void *>(upload_data + ti.dataOffset);
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

/// @brief 非对称黑边padding在屏幕上的像素数
struct PaddingScreenPixels {
	int top;    ///< 屏幕空间顶部padding像素数
	int bottom; ///< 屏幕空间底部padding像素数
};

/// @brief 计算非对称黑边padding在屏幕上的像素数
/// @param viewport_height 显示区域像素高度
/// @param frame_height 原始帧内容高度（不含padding）
/// @param padding_top 顶部padding行数
/// @param padding_bottom 底部padding行数
/// @return 屏幕空间的上下padding像素数（已钳位到合法范围）
static PaddingScreenPixels CalculatePaddingPixels(int viewport_height, int frame_height, int padding_top, int padding_bottom) {
	if (padding_top <= 0 && padding_bottom <= 0) return {0, 0};
	const int total_padded_h = std::max(frame_height + padding_top + padding_bottom, 1);
	const int max_single = std::max(0, viewport_height / 2 - 1);
	auto clamp_px = [&](int pad) -> int {
		if (pad <= 0) return 0;
		int px = (viewport_height * pad) / total_padded_h;
		return std::max(0, std::min(px, max_single));
	};
	return {clamp_px(padding_top), clamp_px(padding_bottom)};
}

void VideoOutGL::Render(int client_width, int client_height, int x, int y, int width, int height) {
	// 参数含义：client_width/height为客户区尺寸，x,y为左下角坐标，width/height为显示区域尺寸
	if (width <= 0 || height <= 0)
		return;

	// 在首帧尚未上传成功前，显示列表可能尚未初始化。
	if (dl == 0)
		return;

	// 判断是否走GPU FBO后处理HDR路径
	bool use_hdr_gpu = false;
#if !defined(__APPLE__)
	if (hdrToneMappingEnabled) {
		// 延迟初始化：在GL上下文已激活的Render()中加载GPU资源
		const auto &shader = GetShaderFunctions();
		if (!hdrLutLoaded && shader.available) {
			try { LoadHDRLUT(); }
			catch (const std::exception &e) {
				LOG_E("video/out/gl") << "Deferred LUT load failed: " << e.what();
			}
		}
		if (!hdrShaderLoaded && shader.available) {
			try { EnsureHDRShader(); }
			catch (const std::exception &e) {
				LOG_E("video/out/gl") << "Deferred shader init failed: " << e.what();
			}
		}

		if (hdrShaderLoaded && hdrShaderProgram != 0 &&
			hdrLutLoaded && hdrLutTextureID != 0 &&
			shader.available && shader.fboAvailable) {
			use_hdr_gpu = true;
		}
	}
#endif

	// ===== FBO旋转路径（90°/270°，可叠加HDR） =====
	bool need_rotation = (frameRotation == 90 || frameRotation == 270);
	bool rotation_rendered = false;
	if (need_rotation) {
#if !defined(__APPLE__)
		const auto &shader = GetShaderFunctions();
		if (shader.fboAvailable) {
			bool rot_ok = true;
			// FBO尺寸使用原始数据维度，避免旋转后纵横比失真
			int fbo_w = frameWidth;
			int fbo_h = frameHeight;
			try {
				EnsureHDRFbo(fbo_w, fbo_h);
			}
			catch (const std::exception &e) {
				LOG_E("video/out/gl") << "FBO creation for rotation failed: " << e.what();
				rot_ok = false;
			}

			if (rot_ok) {
				// 1. 绑定FBO，渲染场景到FBO纹理（display list无翻转，原始数据方向）
				shader.BindFramebuffer(GL_FRAMEBUFFER, hdrFboId);
				if (GLenum err = glGetError()) {
					LOG_E("video/out/gl") << "glBindFramebuffer for rotation failed: " << err;
					shader.BindFramebuffer(GL_FRAMEBUFFER, 0);
					rot_ok = false;
				}
			}

			if (rot_ok) {
				glViewport(0, 0, fbo_w, fbo_h);
				glCallList(dl);

				// 2. 解绑FBO，切回默认帧缓冲
				shader.BindFramebuffer(GL_FRAMEBUFFER, 0);

				// 3. 设置显示viewport（含padding处理）
				// 旋转后，有效帧高度对应原始数据宽度（90/270°时宽高互换）
				if (frameVideoPaddingTop > 0 || frameVideoPaddingBottom > 0) {
					auto pp = CalculatePaddingPixels(height, frameWidth, frameVideoPaddingTop, frameVideoPaddingBottom);
					glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
					glViewport(x, y, width, height);
					glClear(GL_COLOR_BUFFER_BIT);
					int content_y = y + pp.bottom;
					int content_h = std::max(1, height - pp.top - pp.bottom);
					glViewport(x, content_y, width, content_h);
				} else {
					glViewport(x, y, width, height);
				}

				// 4. 计算旋转+翻转的纹理坐标（逆变换：显示坐标→源FBO纹理坐标）
				float tc_bl[2], tc_br[2], tc_tr[2], tc_tl[2];
				{
					struct { float x, y; } corners[4] = {{0,0},{1,0},{1,1},{0,1}};
					float (*tc_arr[])[2] = {&tc_bl, &tc_br, &tc_tr, &tc_tl};
					for (int i = 0; i < 4; ++i) {
						float dx = corners[i].x, dy = corners[i].y;
						// 逆旋转
						float s2, t2;
						switch (frameRotation) {
							case 90:  s2 = 1.0f - dy; t2 = dx; break;
							case 270: s2 = dy;        t2 = 1.0f - dx; break;
							default:  s2 = dx;        t2 = dy; break;
						}
						// 逆水平翻转
						float s1 = frameSourceHFlip ? 1.0f - s2 : s2;
						// 逆垂直翻转
						float t1 = frameSourceVFlip ? 1.0f - t2 : t2;
						(*tc_arr[i])[0] = s1;
						(*tc_arr[i])[1] = t1;
					}
				}

				// 5. 绘制旋转的全屏四边形
				if (use_hdr_gpu) {
					// HDR shader路径：绑定shader + LUT纹理
					shader.UseProgram(hdrShaderProgram);
					shader.ActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_3D, hdrLutTextureID);
					shader.Uniform1i(hdrLutSamplerLoc, 1);
					shader.ActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, hdrFboTexId);
					shader.Uniform1i(hdrSceneSamplerLoc, 0);

					// LUT坐标缩放和偏移（精确纹素中心映射）
					float lut_scale = 1.0f, lut_offset = 0.0f;
					if (hdrLutTextureSize > 0 && hdrLutSize > 1) {
						lut_scale = static_cast<float>(hdrLutSize - 1) / static_cast<float>(hdrLutTextureSize);
						lut_offset = 0.5f / static_cast<float>(hdrLutTextureSize);
					}
					shader.Uniform1f(hdrLutScaleLoc, lut_scale);
					shader.Uniform1f(hdrLutOffsetLoc, lut_offset);
					shader.Uniform1f(hdrUseLutLoc, 1.0f);
				} else {
					// 固定管线路径：仅绑定FBO纹理进行旋转渲染
					glEnable(GL_TEXTURE_2D);
					glBindTexture(GL_TEXTURE_2D, hdrFboTexId);
				}

				// 投影设为[0,1]正规化坐标
				glMatrixMode(GL_PROJECTION);
				glLoadIdentity();
				glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
				glMatrixMode(GL_MODELVIEW);
				glLoadIdentity();

				glBegin(GL_QUADS);
					glTexCoord2f(tc_bl[0], tc_bl[1]); glVertex2f(0.0f, 0.0f);
					glTexCoord2f(tc_br[0], tc_br[1]); glVertex2f(1.0f, 0.0f);
					glTexCoord2f(tc_tr[0], tc_tr[1]); glVertex2f(1.0f, 1.0f);
					glTexCoord2f(tc_tl[0], tc_tl[1]); glVertex2f(0.0f, 1.0f);
				glEnd();

				// 6. 清理shader和纹理绑定
				if (use_hdr_gpu) {
					shader.UseProgram(0);
					shader.ActiveTexture(GL_TEXTURE1);
					glBindTexture(GL_TEXTURE_3D, 0);
					shader.ActiveTexture(GL_TEXTURE0);
					glBindTexture(GL_TEXTURE_2D, 0);
				} else {
					glBindTexture(GL_TEXTURE_2D, 0);
					glDisable(GL_TEXTURE_2D);
				}

				// 批量检查GL错误（避免每次调用glGetError导致GPU管线同步）
				if (GLenum err = glGetError())
					LOG_E("video/out/gl") << "Rotation render path GL error: " << err;

				rotation_rendered = true;
			}
		}
#endif
		if (!rotation_rendered) {
			// FBO不可用时回退：直接渲染（旋转不生效，但至少能显示画面）
			LOG_E("video/out/gl") << "FBO unavailable for rotation, falling back to unrotated render";
			CHECK_ERROR(glViewport(x, y, width, height));
			CHECK_ERROR(glCallList(dl));
			rotation_rendered = true;
		}
	}

	if (!rotation_rendered && use_hdr_gpu) {
#if !defined(__APPLE__)
		// === FBO后处理路径 ===
		// 步骤：先将场景渲染到FBO纹理（不绑定shader），再用fullscreen quad + shader采样FBO进行色调映射
		const auto &shader = GetShaderFunctions();

		try {
			EnsureHDRFbo(width, height);
		}
		catch (const std::exception &e) {
			LOG_E("video/out/gl") << "FBO creation failed, falling back to normal render: " << e.what();
			use_hdr_gpu = false;
		}

		if (use_hdr_gpu) {
			// 1. 绑定FBO，将场景渲染到FBO纹理
			shader.BindFramebuffer(GL_FRAMEBUFFER, hdrFboId);
			if (GLenum err = glGetError()) {
				LOG_E("video/out/gl") << "glBindFramebuffer failed: " << err;
				shader.BindFramebuffer(GL_FRAMEBUFFER, 0);
				use_hdr_gpu = false;
			}
		}

		if (use_hdr_gpu) {
			// FBO内部viewport从(0,0)开始
			glViewport(0, 0, width, height);

			// 处理黑边padding
			if (frameVideoPaddingTop > 0 || frameVideoPaddingBottom > 0) {
				auto pp = CalculatePaddingPixels(height, frameHeight, frameVideoPaddingTop, frameVideoPaddingBottom);
				glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
				glClear(GL_COLOR_BUFFER_BIT);
				int content_y = pp.bottom;
				int content_height = std::max(1, height - pp.top - pp.bottom);
				glViewport(0, content_y, width, content_height);
			}

			// 无shader绑定状态调用display list（固定管线渲染）
			glCallList(dl);

			// 2. 解绑FBO，切回默认帧缓冲
			shader.BindFramebuffer(GL_FRAMEBUFFER, 0);

			// 3. 设置屏幕viewport
			glViewport(x, y, width, height);

			// 4. 绑定HDR shader
			shader.UseProgram(hdrShaderProgram);

			// 5. 绑定3D LUT到纹理单元1
			shader.ActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_3D, hdrLutTextureID);
			shader.Uniform1i(hdrLutSamplerLoc, 1);

			// 6. 绑定FBO纹理到纹理单元0
			shader.ActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, hdrFboTexId);
			shader.Uniform1i(hdrSceneSamplerLoc, 0);

			// 7. 设置LUT坐标缩放和偏移（精确纹素中心映射）
			// texcoord = input * (S-1)/T + 0.5/T 确保LUT各采样点精确命中纹素中心
			float lut_scale = 1.0f;
			float lut_offset = 0.0f;
			if (hdrLutTextureSize > 0 && hdrLutSize > 1) {
				lut_scale = static_cast<float>(hdrLutSize - 1) / static_cast<float>(hdrLutTextureSize);
				lut_offset = 0.5f / static_cast<float>(hdrLutTextureSize);
			}
			shader.Uniform1f(hdrLutScaleLoc, lut_scale);
			shader.Uniform1f(hdrLutOffsetLoc, lut_offset);

			// 使用3D LUT进行色调映射
			shader.Uniform1f(hdrUseLutLoc, 1.0f);

			// 8. 绘制全屏四边形，shader从FBO纹理采样并应用LUT
			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();

			glBegin(GL_QUADS);
				glTexCoord2f(0.0f, 0.0f); glVertex2f(0.0f, 0.0f);
				glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 0.0f);
				glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, 1.0f);
				glTexCoord2f(0.0f, 1.0f); glVertex2f(0.0f, 1.0f);
			glEnd();

			// 9. 清理shader和纹理绑定
			shader.UseProgram(0);
			shader.ActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_3D, 0);
			shader.ActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, 0);

			// 批量检查GL错误（避免每次调用glGetError导致GPU管线同步）
			if (GLenum err = glGetError())
				LOG_E("video/out/gl") << "HDR render path GL error: " << err;
		}
#endif
	}

	if (!rotation_rendered && !use_hdr_gpu) {
		// === 常规渲染路径（无HDR后处理）===
		if (frameVideoPaddingTop > 0 || frameVideoPaddingBottom > 0) {
			auto pp = CalculatePaddingPixels(height, frameHeight, frameVideoPaddingTop, frameVideoPaddingBottom);
			CHECK_ERROR(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
			CHECK_ERROR(glViewport(x, y, width, height));
			CHECK_ERROR(glClear(GL_COLOR_BUFFER_BIT));
			int content_y = y + pp.bottom;
			int content_height = std::max(1, height - pp.top - pp.bottom);
			CHECK_ERROR(glViewport(x, content_y, width, content_height));
		} else {
			CHECK_ERROR(glViewport(x, y, width, height));
		}

		CHECK_ERROR(glCallList(dl));
	}

	CHECK_ERROR(glMatrixMode(GL_MODELVIEW));
	CHECK_ERROR(glLoadIdentity());
}

void VideoOutGL::EnsureHDRFbo(int width, int height) {
	// 若FBO已存在且尺寸一致，无需重建
	if (hdrFboId != 0 && hdrFboWidth == width && hdrFboHeight == height)
		return;

	ReleaseHDRFbo();

#if !defined(__APPLE__)
	const auto &shader = GetShaderFunctions();
	if (!shader.fboAvailable)
		throw std::runtime_error("FBO functions not available");

	shader.GenFramebuffers(1, &hdrFboId);
	if (GLenum err = glGetError()) throw VideoOutRenderException("glGenFramebuffers", err);

	shader.BindFramebuffer(GL_FRAMEBUFFER, hdrFboId);
	if (GLenum err = glGetError()) throw VideoOutRenderException("glBindFramebuffer", err);

	// 创建FBO颜色附件纹理
	CHECK_ERROR(glGenTextures(1, &hdrFboTexId));
	CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, hdrFboTexId));
	CHECK_ERROR(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
	CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
	CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
	CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
	CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));

	shader.FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, hdrFboTexId, 0);
	if (GLenum err = glGetError()) throw VideoOutRenderException("glFramebufferTexture2D", err);

	GLenum status = shader.CheckFramebufferStatus(GL_FRAMEBUFFER);
	if (status != GL_FRAMEBUFFER_COMPLETE) {
		shader.BindFramebuffer(GL_FRAMEBUFFER, 0);
		ReleaseHDRFbo();
		throw VideoOutRenderException("FBO incomplete", static_cast<int>(status));
	}

	shader.BindFramebuffer(GL_FRAMEBUFFER, 0);
	CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, 0));

	hdrFboWidth = width;
	hdrFboHeight = height;
	LOG_I("video/out/gl") << "HDR FBO created: " << width << "x" << height;
#else
	throw std::runtime_error("FBO not supported on this platform");
#endif
}

void VideoOutGL::ReleaseHDRFbo() {
#if !defined(__APPLE__)
	const auto &shader = GetShaderFunctions();
	if (hdrFboId != 0 && shader.DeleteFramebuffers) {
		shader.DeleteFramebuffers(1, &hdrFboId);
		while (glGetError()) { }
	}
#endif
	hdrFboId = 0;
	if (hdrFboTexId != 0) {
		glDeleteTextures(1, &hdrFboTexId);
		while (glGetError()) { }
	}
	hdrFboTexId = 0;
	hdrFboWidth = 0;
	hdrFboHeight = 0;
}

VideoOutGL::~VideoOutGL() {
	// 检查 GL context 是否可用，无 context 时 GL 调用为未定义行为
#ifdef __WIN32__
	if (!wglGetCurrentContext()) return;
#elif !defined(__APPLE__)
	if (!glXGetCurrentContext()) return;
#endif
	ReleaseUploadPbo();
	ReleaseHDRFbo();
	ReleaseHDRShader();
	ReleaseHDRLUT();
	if (textureIdList.size() > 0) {
		glDeleteTextures(textureIdList.size(), &textureIdList[0]);
		glDeleteLists(dl, 1);
	}
}

void VideoOutGL::LoadHDRLUT() {
	// 根据hdrInputType选择LUT文件：HLG用HLG2SDR.cube，PQ用PQ2SDR.cube，DV用DV2SDR.cube
	// 使用octoon库解析.cube格式，上传为GPU 3D纹理供shader采样

	if (hdrLutLoaded) return;

	try {
#if !defined(__APPLE__)
		const auto &shader = GetShaderFunctions();
		if (!shader.available) {
			LOG_W("video/out/gl") << "GPU shader functions unavailable, cannot load HDR LUT";
			hdrLutLoaded = false;
			return;
		}

		// 根据HDR类型选择LUT文件名，DV根据Profile选择对应LUT
		HDRType currentType = hdrInputType;
		std::string lutFilename = GetLutFilename(currentType, hdrDvProfile);

		std::string lutPath = FindCubeLutPath(lutFilename);

		if (lutPath.empty()) {
			LOG_W("video/out/gl") << "HDR LUT file not found: " << lutFilename << ", HDR tone mapping disabled";
			hdrLutLoaded = false;
			return;
		}

		auto lut = octoon::image::flut::parse(lutPath);
		if (!lut.data || lut.channel < 3 || lut.height == 0 || lut.width != lut.height * lut.height)
			throw std::runtime_error("Invalid LUT layout from cube parser");

		GetCpuCubeLut() = std::make_unique<octoon::image::flut>(std::move(lut));
		GetCpuCubeLutType() = currentType;
		auto &cpu_lut = GetCpuCubeLut();
		if (!cpu_lut || !cpu_lut->data)
			throw std::runtime_error("Failed to cache CPU cube LUT");

		hdrLutSize = static_cast<int>(cpu_lut->height);
		std::vector<float> lut3d(static_cast<size_t>(hdrLutSize) * hdrLutSize * hdrLutSize * 3);

		// lut.hpp parse()内部变量命名：r=Blue(最慢轴), g=Green(中间轴), b=Red(最快轴)
		// 2D布局：idx2d = (g * width + r * size + b) = (Green * W + Blue * S + Red)
		// 重排为3D纹理：x轴=Red, y轴=Green, z轴=Blue
		// idx3d = Blue * S^2 + Green * S + Red = (r * S + g) * S + b
		for (int g = 0; g < hdrLutSize; ++g) {
			for (int r = 0; r < hdrLutSize; ++r) {
				for (int b = 0; b < hdrLutSize; ++b) {
					size_t src_idx = (static_cast<size_t>(g) * cpu_lut->width + (static_cast<size_t>(r) * hdrLutSize + b)) * cpu_lut->channel;
					size_t dst_idx = ((static_cast<size_t>(r) * hdrLutSize + g) * hdrLutSize + b) * 3;
					lut3d[dst_idx + 0] = cpu_lut->data[src_idx + 0];
					lut3d[dst_idx + 1] = cpu_lut->data[src_idx + 1];
					lut3d[dst_idx + 2] = cpu_lut->data[src_idx + 2];
				}
			}
		}

		// 当GPU不支持NPOT 3D纹理时，扩展为POT尺寸以保证兼容性
		if (!supportsNpotTextures && hdrLutSize != SmallestPowerOf2(hdrLutSize)) {
			hdrLutTextureSize = SmallestPowerOf2(hdrLutSize);
			LOG_D("video/out/gl") << "Expanding LUT from " << hdrLutSize << " to POT size " << hdrLutTextureSize;
			std::vector<float> lut3d_upload(static_cast<size_t>(hdrLutTextureSize) * hdrLutTextureSize * hdrLutTextureSize * 3, 0.0f);
			for (int z = 0; z < hdrLutTextureSize; ++z) {
				int src_z = std::min(z, hdrLutSize - 1);
				for (int y = 0; y < hdrLutTextureSize; ++y) {
					int src_y = std::min(y, hdrLutSize - 1);
					for (int x = 0; x < hdrLutTextureSize; ++x) {
						int src_x = std::min(x, hdrLutSize - 1);
						size_t src_idx = ((static_cast<size_t>(src_z) * hdrLutSize + src_y) * hdrLutSize + src_x) * 3;
						size_t dst_idx = ((static_cast<size_t>(z) * hdrLutTextureSize + y) * hdrLutTextureSize + x) * 3;
						lut3d_upload[dst_idx + 0] = lut3d[src_idx + 0];
						lut3d_upload[dst_idx + 1] = lut3d[src_idx + 1];
						lut3d_upload[dst_idx + 2] = lut3d[src_idx + 2];
					}
				}
			}

			if (hdrLutTextureID != 0) {
				CHECK_ERROR(glDeleteTextures(1, &hdrLutTextureID));
				hdrLutTextureID = 0;
			}
			CHECK_ERROR(glGenTextures(1, &hdrLutTextureID));
			if (hdrLutTextureID == 0)
				throw std::runtime_error("Failed to create HDR LUT texture");

			CHECK_ERROR(glBindTexture(GL_TEXTURE_3D, hdrLutTextureID));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
			shader.TexImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, hdrLutTextureSize, hdrLutTextureSize, hdrLutTextureSize, 0, GL_RGB, GL_FLOAT, lut3d_upload.data());
			if (GLenum err = glGetError()) throw VideoOutRenderException("glTexImage3D", err);
			CHECK_ERROR(glBindTexture(GL_TEXTURE_3D, 0));
		} else {
			// NPOT支持或LUT尺寸已是2的幂次，直接上传原始数据
			hdrLutTextureSize = hdrLutSize;

			if (hdrLutTextureID != 0) {
				CHECK_ERROR(glDeleteTextures(1, &hdrLutTextureID));
				hdrLutTextureID = 0;
			}
			CHECK_ERROR(glGenTextures(1, &hdrLutTextureID));
			if (hdrLutTextureID == 0)
				throw std::runtime_error("Failed to create HDR LUT texture");

			CHECK_ERROR(glBindTexture(GL_TEXTURE_3D, hdrLutTextureID));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE));
			CHECK_ERROR(glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE));
			shader.TexImage3D(GL_TEXTURE_3D, 0, GL_RGB16F, hdrLutTextureSize, hdrLutTextureSize, hdrLutTextureSize, 0, GL_RGB, GL_FLOAT, lut3d.data());
			if (GLenum err = glGetError()) throw VideoOutRenderException("glTexImage3D", err);
			CHECK_ERROR(glBindTexture(GL_TEXTURE_3D, 0));
		}

		hdrLutLoaded = true;
		LOG_I("video/out/gl") << "HDR LUT texture uploaded: lut=" << hdrLutSize << " tex=" << hdrLutTextureSize << " id=" << hdrLutTextureID;

#else
		LOG_W("video/out/gl") << "HDR LUT is not enabled on this platform path";
		hdrLutLoaded = false;
#endif

	} catch (const std::exception& e) {
		LOG_E("video/out/gl") << "Failed to load HDR LUT: " << e.what();
		hdrLutLoaded = false;
		hdrToneMappingEnabled = false;
	}
}

void VideoOutGL::ReleaseHDRLUT() {
	if (hdrLutTextureID != 0) {
		glDeleteTextures(1, &hdrLutTextureID);
		while (glGetError()) { }
		hdrLutTextureID = 0;
	}
	hdrLutSize = 0;
	hdrLutTextureSize = 0;
	GetCpuCubeLut().reset();
	GetCpuCubeLutType() = HDRType::SDR;
	hdrLutLoaded = false;
}

bool VideoOutGL::ApplyHDRLutToImage(wxImage& img, HDRType type) {
	if (!img.IsOk() || !img.GetData())
		return false;

	// 确保CPU侧LUT已加载，且LUT类型与当前请求匹配
	auto &cpu_lut = GetCpuCubeLut();
	auto &cached_type = GetCpuCubeLutType();
	if (cpu_lut && cpu_lut->data && cached_type != type) {
		// HDR类型变化，需重新加载LUT
		cpu_lut.reset();
	}
	if (!cpu_lut || !cpu_lut->data) {
		// 尝试从cube文件加载
		try {
			std::string lutFilename = GetLutFilename(type);
			std::string lutPath = FindCubeLutPath(lutFilename);

			if (lutPath.empty()) {
				LOG_W("video/out/gl") << "HDR LUT file not found for CPU export: " << lutFilename;
				return false;
			}
			auto parsed = octoon::image::flut::parse(lutPath);
			if (!parsed.data || parsed.channel < 3 || parsed.height == 0 || parsed.width != parsed.height * parsed.height) {
				LOG_E("video/out/gl") << "Invalid LUT layout from cube parser (CPU export)";
				return false;
			}
			cpu_lut = std::make_unique<octoon::image::flut>(std::move(parsed));
			cached_type = type;
		} catch (const std::exception& e) {
			LOG_E("video/out/gl") << "Failed to load HDR LUT for CPU export: " << e.what();
			return false;
		}
	}

	if (!cpu_lut || !cpu_lut->data)
		return false;

	const int S = static_cast<int>(cpu_lut->height);
	if (S < 2) return false;

	// lut.hpp parse()内部：r=Blue(最慢轴), g=Green(中间轴), b=Red(最快轴)
	// 2D布局：idx2d = (g * width + r * S + b) * channel
	// 即：(Green * W + Blue * S + Red) * Ch

	const int w = img.GetWidth();
	const int h = img.GetHeight();
	uint8_t* data = img.GetData();
	const float scale = static_cast<float>(S - 1) / 255.0f;

	for (int i = 0; i < w * h; ++i) {
		uint8_t* px = data + i * 3;
		const uint8_t R_in = px[0], G_in = px[1], B_in = px[2];

		// 归一化到 [0, 1] 并计算浮点LUT坐标
		const float fr = R_in * scale;
		const float fg = G_in * scale;
		const float fb = B_in * scale;

		// 三线性插值的8个角点索引
		const int r0 = std::min(static_cast<int>(fr), S - 1);
		const int g0 = std::min(static_cast<int>(fg), S - 1);
		const int b0 = std::min(static_cast<int>(fb), S - 1);
		const int r1 = std::min(r0 + 1, S - 1);
		const int g1 = std::min(g0 + 1, S - 1);
		const int b1 = std::min(b0 + 1, S - 1);

		const float dr = fr - r0;
		const float dg = fg - g0;
		const float db = fb - b0;

		const int ch = cpu_lut->channel;
		const int W = static_cast<int>(cpu_lut->width);

		// 宏：从2D布局读取LUT值 (Green * W + Blue * S + Red) * channel
		#define LUT_IDX(rr, gg, bb) (static_cast<size_t>((gg) * W + (bb) * S + (rr)) * ch)

		const float* d = cpu_lut->data.get();

		// 读取8个角点（浮点精度LUT，值域0.0~1.0）
		auto lerp = [](float a, float b, float t) -> float { return a + (b - a) * t; };

		for (int c = 0; c < 3; ++c) {
			float c000 = d[LUT_IDX(r0, g0, b0) + c];
			float c100 = d[LUT_IDX(r1, g0, b0) + c];
			float c010 = d[LUT_IDX(r0, g1, b0) + c];
			float c110 = d[LUT_IDX(r1, g1, b0) + c];
			float c001 = d[LUT_IDX(r0, g0, b1) + c];
			float c101 = d[LUT_IDX(r1, g0, b1) + c];
			float c011 = d[LUT_IDX(r0, g1, b1) + c];
			float c111 = d[LUT_IDX(r1, g1, b1) + c];

			// 三线性插值：沿Red轴→Green轴→Blue轴
			float c00 = lerp(c000, c100, dr);
			float c01 = lerp(c001, c101, dr);
			float c10 = lerp(c010, c110, dr);
			float c11 = lerp(c011, c111, dr);

			float c0 = lerp(c00, c10, dg);
			float c1 = lerp(c01, c11, dg);

			float result = lerp(c0, c1, db);
			// 浮点LUT值域0.0~1.0，映射到0~255输出
			px[c] = static_cast<uint8_t>(std::min(std::max(result * 255.0f, 0.0f), 255.0f));
		}

		#undef LUT_IDX
	}

	return true;
}

void VideoOutGL::EnsureHDRShader() {
	if (hdrShaderLoaded && hdrShaderProgram != 0)
		return;

#if !defined(__APPLE__)
	const auto &shader = GetShaderFunctions();
	if (!shader.available) {
		LOG_W("video/out/gl") << "HDR shader unavailable: OpenGL shader functions missing";
		hdrShaderLoaded = false;
		return;
	}

	const char *vertex_src =
		"void main() {\n"
		"  gl_TexCoord[0] = gl_MultiTexCoord0;\n"
		"  gl_Position = ftransform();\n"
		"}\n";

	const char *fragment_src =
		"uniform sampler2D sceneTex;\n"
		"uniform sampler3D lutTex;\n"
		"uniform float lutCoordScale;\n"
		"uniform float lutCoordOffset;\n"
		"uniform float useLut;\n"
		"void main() {\n"
		"  vec4 src = texture2D(sceneTex, gl_TexCoord[0].xy);\n"
		// [已知限制] Reinhard简易色调映射 x/(x+1) 作为LUT不可用时的回退方案。
		// 此算子压缩高光区域过于激进，导致HDR亮部细节丢失且整体偏灰。
		// 适用于预览目的，但色彩准确度远低于3D LUT映射。
		// 当LUT文件缺失时自动启用，建议用户确保cube文件可用。
		"  vec3 mapped = src.rgb / (src.rgb + vec3(1.0));\n"
		"  if (useLut > 0.5) {\n"
		"    vec3 lutCoord = clamp(src.rgb, 0.0, 1.0) * lutCoordScale + vec3(lutCoordOffset);\n"
		"    mapped = texture3D(lutTex, lutCoord).rgb;\n"
		"  }\n"
		"  gl_FragColor = vec4(mapped, src.a);\n"
		"}\n";

	GLuint vs = shader.CreateShader(GL_VERTEX_SHADER);
	GLuint fs = shader.CreateShader(GL_FRAGMENT_SHADER);
	if (vs == 0 || fs == 0)
		throw std::runtime_error("Failed to create HDR shader objects");

	shader.ShaderSource(vs, 1, &vertex_src, nullptr);
	shader.CompileShader(vs);
	GLint ok = GL_FALSE;
	shader.GetShaderiv(vs, GL_COMPILE_STATUS, &ok);
	if (ok != GL_TRUE) {
		char logbuf[1024] = {0};
		shader.GetShaderInfoLog(vs, sizeof(logbuf), nullptr, logbuf);
		shader.DeleteShader(vs);
		shader.DeleteShader(fs);
		throw std::runtime_error(std::string("HDR vertex shader compile failed: ") + logbuf);
	}

	shader.ShaderSource(fs, 1, &fragment_src, nullptr);
	shader.CompileShader(fs);
	shader.GetShaderiv(fs, GL_COMPILE_STATUS, &ok);
	if (ok != GL_TRUE) {
		char logbuf[1024] = {0};
		shader.GetShaderInfoLog(fs, sizeof(logbuf), nullptr, logbuf);
		shader.DeleteShader(vs);
		shader.DeleteShader(fs);
		throw std::runtime_error(std::string("HDR fragment shader compile failed: ") + logbuf);
	}

	hdrShaderProgram = shader.CreateProgram();
	if (hdrShaderProgram == 0) {
		shader.DeleteShader(vs);
		shader.DeleteShader(fs);
		throw std::runtime_error("Failed to create HDR shader program");
	}

	shader.AttachShader(hdrShaderProgram, vs);
	shader.AttachShader(hdrShaderProgram, fs);
	shader.LinkProgram(hdrShaderProgram);
	shader.GetProgramiv(hdrShaderProgram, GL_LINK_STATUS, &ok);
	shader.DeleteShader(vs);
	shader.DeleteShader(fs);

	if (ok != GL_TRUE) {
		char logbuf[1024] = {0};
		shader.GetProgramInfoLog(hdrShaderProgram, sizeof(logbuf), nullptr, logbuf);
		shader.DeleteProgram(hdrShaderProgram);
		hdrShaderProgram = 0;
		throw std::runtime_error(std::string("HDR shader program link failed: ") + logbuf);
	}

	hdrSceneSamplerLoc = shader.GetUniformLocation(hdrShaderProgram, "sceneTex");
	hdrLutSamplerLoc = shader.GetUniformLocation(hdrShaderProgram, "lutTex");
	hdrLutScaleLoc = shader.GetUniformLocation(hdrShaderProgram, "lutCoordScale");
	hdrLutOffsetLoc = shader.GetUniformLocation(hdrShaderProgram, "lutCoordOffset");
	hdrUseLutLoc = shader.GetUniformLocation(hdrShaderProgram, "useLut");
	if (hdrSceneSamplerLoc < 0 || hdrLutSamplerLoc < 0 || hdrLutScaleLoc < 0 || hdrLutOffsetLoc < 0 || hdrUseLutLoc < 0) {
		shader.DeleteProgram(hdrShaderProgram);
		hdrShaderProgram = 0;
		throw std::runtime_error("HDR shader uniform lookup failed");
	}

	hdrShaderLoaded = true;
	LOG_I("video/out/gl") << "HDR shader initialized";
#else
	hdrShaderLoaded = false;
#endif
}

void VideoOutGL::ReleaseHDRShader() {
#if !defined(__APPLE__)
	if (hdrShaderProgram != 0 && GetShaderFunctions().available) {
		GetShaderFunctions().DeleteProgram(hdrShaderProgram);
		while (glGetError()) { }
	}
#endif
	hdrShaderProgram = 0;
	hdrSceneSamplerLoc = -1;
	hdrLutSamplerLoc = -1;
	hdrLutScaleLoc = -1;
	hdrLutOffsetLoc = -1;
	hdrUseLutLoc = -1;
	hdrShaderLoaded = false;
}
