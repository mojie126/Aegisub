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

/// @file video_out_gl.h
/// @brief OpenGL based video renderer
/// @ingroup video
///

#include <libaegisub/exception.h>

#include <vector>

// 前向声明HDRType枚举（定义在video_provider.h中）
enum class HDRType : int;

struct VideoFrame;
class wxImage;

/// @class VideoOutGL
/// @brief OpenGL based video renderer
class VideoOutGL {
	struct TextureInfo;

	/// The maximum texture size supported by the user's graphics card
	int maxTextureSize = 0;
	/// Whether rectangular textures are supported by the user's graphics card
	bool supportsRectangularTextures = false;
	/// 是否支持非2的幂次（NPOT）纹理，影响3D LUT纹理是否需要POT扩展
	bool supportsNpotTextures = false;
	/// The internalformat to use
	int internalFormat = 0;

	/// The frame height which the texture grid has been set up for
	int frameWidth = 0;
	/// The frame width which the texture grid has been set up for
	int frameHeight = 0;
	/// The frame format which the texture grid has been set up for
	GLenum frameFormat = 0;
	/// Whether the grid is set up for flipped video
	bool frameFlipped = false;
	/// Whether the grid is set up for horizontally flipped video
	bool frameHFlipped = false;
	/// 帧旋转角度(0/90/270)，由video provider设置
	int frameRotation = 0;
	/// 原始垂直翻转标志（用于FBO旋转路径的纹理坐标计算）
	bool frameSourceVFlip = false;
	/// 原始水平翻转标志（用于FBO旋转路径的纹理坐标计算）
	bool frameSourceHFlip = false;
	/// GPU顶部黑边行数（由video provider设置的padding信息）
	int frameVideoPaddingTop = 0;
	/// GPU底部黑边行数（由video provider设置的padding信息）
	int frameVideoPaddingBottom = 0;
	/// Whether HDR to SDR tone mapping is enabled
	bool hdrToneMappingEnabled = false;
	/// Whether current source is likely HDR (used to avoid applying PQ LUT on SDR)
	bool hdrInputLikelyHdr = false;
	/// 当前视频源的HDR类型（SDR/PQ/HLG/DolbyVision），用于选择对应LUT文件
	/// @note 默认零初始化对应HDRType::SDR(=0)
	HDRType hdrInputType{};
	/// 当前视频源的Dolby Vision Profile编号（0=无DV/未知），用于Profile感知的LUT选择
	int hdrDvProfile = 0;
	/// HDR LUT 3D texture ID (for tone mapping)
	GLuint hdrLutTextureID = 0;
	/// HDR LUT 3D size
	int hdrLutSize = 0;
	/// HDR LUT uploaded texture size (power-of-two for compatibility)
	int hdrLutTextureSize = 0;
	/// Whether HDR LUT is currently loaded
	bool hdrLutLoaded = false;
	/// List of OpenGL texture ids used in the grid
	std::vector<GLuint> textureIdList;
	/// List of precalculated texture display information
	std::vector<TextureInfo> textureList;
	/// OpenGL display list which draws the frames
	GLuint dl = 0;
	/// The total texture count
	int textureCount = 0;
	/// The number of rows of textures
	int textureRows = 0;
	/// The number of columns of textures
	int textureCols = 0;
	/// Whether pixel unpack buffers are supported by the current OpenGL context
	bool supportsPixelUnpackBuffer = false;
	/// Ring buffers used for asynchronous upload to textures
	std::vector<GLuint> uploadPboIds;
	/// Allocated byte size for each upload PBO
	size_t uploadPboSize = 0;
	/// Current PBO write index in uploadPboIds
	size_t uploadPboIndex = 0;
	/// FBO ID for HDR post-processing (render scene to FBO, then apply shader)
	GLuint hdrFboId = 0;
	/// FBO色彩附件纹理ID
	GLuint hdrFboTexId = 0;
	/// FBO当前宽度（与viewport同步，变化时需重建）
	int hdrFboWidth = 0;
	/// FBO当前高度
	int hdrFboHeight = 0;
	/// Whether HDR post shader is available and linked successfully
	bool hdrShaderLoaded = false;
	/// OpenGL program for HDR LUT mapping
	GLuint hdrShaderProgram = 0;
	/// Uniform location of scene texture sampler
	GLint hdrSceneSamplerLoc = -1;
	/// Uniform location of 3D LUT sampler
	GLint hdrLutSamplerLoc = -1;
	/// Uniform location of LUT coordinate scale (for POT expanded LUT)
	GLint hdrLutScaleLoc = -1;
	/// Uniform location of LUT coordinate offset (texel center correction for POT)
	GLint hdrLutOffsetLoc = -1;
	/// Uniform location of LUT usage switch (0=fallback tonemap, 1=use LUT)
	GLint hdrUseLutLoc = -1;

	void DetectOpenGLCapabilities();
	void InitTextures(int width, int height, GLenum format, int bpp, bool flipped, bool hflipped);
	void EnsureUploadPbo(size_t requiredSize);
	void ReleaseUploadPbo();
	void EnsureHDRFbo(int width, int height);
	void ReleaseHDRFbo();
	void LoadHDRLUT();
	void ReleaseHDRLUT();
	void EnsureHDRShader();
	void ReleaseHDRShader();

	VideoOutGL(const VideoOutGL &) = delete;
	VideoOutGL& operator=(const VideoOutGL&) = delete;
public:
	/// @brief Set the frame to be displayed when Render() is called
	/// @param frame The frame to be displayed
	void UploadFrameData(VideoFrame const& frame);

	/// @brief Render a frame
	/// @param client_width Width in physical pixels of client window
	/// @param client_height Height in physical pixels of client window
	/// @param x Bottom left x coordinate of the target area
	/// @param y Bottom left y coordinate of the target area
	/// @param width Width in pixels of the target area
	/// @param height Height in pixels of the target area
	void Render(int client_width, int client_height, int x, int y, int width, int height);

	/// @brief Enable or disable HDR to SDR tone mapping
	/// @param enable Whether to apply PQ EOTF inverse for HDR content
	void EnableHDRToneMapping(bool enable);

	/// @brief 对wxImage应用CPU侧HDR LUT色彩映射（用于截图/导出路径）
	/// @param img 输入输出图像，原地修改RGB像素
	/// @param type HDR类型，决定使用哪个LUT文件（PQ2SDR或DV2SDR）
	/// @return 是否成功应用了LUT色彩映射
	/// @details 使用三线性插值从3D LUT查表，将PQ编码的HDR像素映射到SDR色彩空间。
	///          如果LUT未加载或不可用，返回false且图像不变。
	static bool ApplyHDRLutToImage(wxImage& img, HDRType type);

	/// @brief 根据HDR类型返回LUT文件名
	/// @param type HDR类型
	/// @param dvProfile Dolby Vision Profile编号（0=未知，7/8等为具体Profile）
	/// @return 对应的cube文件名（如PQ2SDR.cube）
	static std::string GetLutFilename(HDRType type, int dvProfile = 0);

	/// @brief 查找cube LUT文件的完整路径
	/// @param filename cube文件名（如PQ2SDR.cube）
	/// @return 找到的文件完整路径，未找到时返回空字符串
	static std::string FindCubeLutPath(const std::string &filename);

	/// @brief Set whether current input appears to be HDR source and its type
	/// @param isHdr True when source colorspace suggests BT.2020/HDR workflow
	/// @param type HDR type detected from video metadata
	/// @param dvProfile Dolby Vision Profile编号（0=非DV或未知）
	void SetHDRInputHint(bool isHdr, HDRType type, int dvProfile = 0);

	VideoOutGL();
	~VideoOutGL();
};

/// Base class for all exceptions thrown by VideoOutGL
DEFINE_EXCEPTION(VideoOutException, agi::Exception);

/// An OpenGL error occurred while uploading or displaying a frame
class VideoOutRenderException final : public VideoOutException {
public:
	VideoOutRenderException(const char *func, int err)
	: VideoOutException(std::string(func) + " failed with error code " + std::to_string(err))
	{ }
};

/// An OpenGL error occurred while setting up the video display
class VideoOutInitException final : public VideoOutException {
public:
	VideoOutInitException(const char *func, int err)
	: VideoOutException(std::string(func) + " failed with error code " + std::to_string(err))
	{ }
	VideoOutInitException(const char *err) : VideoOutException(err) { }
};
