/// @file video_provider_image.h
/// @see video_provider_image.cpp
/// @ingroup video_input
///
/// @brief 图片视频提供者，支持加载单张图片或图片序列作为视频源

#pragma once

#include "include/aegisub/video_provider.h"

#include <string>
#include <vector>

/// @class ImageVideoProvider
/// @brief 将静态图片或图片序列作为视频帧序列提供
///
/// 通过 URI 格式 ?image:fps:filepath 接收参数，自动扫描同目录下的图片序列。
/// 使用 libavformat/libavcodec 解码图片为 BGRA8 像素数据。
class ImageVideoProvider final : public VideoProvider {
	int width = 0;
	int height = 0;
	agi::vfr::Framerate fps;

	/// 序列中所有图片文件的完整路径（按序号排序）
	std::vector<agi::fs::path> image_files;

	/// 当前缓存的帧索引（-1表示无缓存）
	int cached_frame_index = -1;
	/// 当前缓存的帧像素数据
	std::vector<unsigned char> cached_data;

	/// @brief 使用 FFmpeg 解码单张图片为 BGRA8 数据
	/// @param filepath 图片文件路径
	/// @param[out] out_data BGRA8 像素数据
	/// @param[out] out_width 图片宽度
	/// @param[out] out_height 图片高度
	void DecodeImage(agi::fs::path const& filepath,
	                 std::vector<unsigned char>& out_data,
	                 int& out_width, int& out_height);

public:
	/// @brief 扫描指定图片所在目录的图片序列
	/// @param selected_file 用户选择的图片文件路径
	/// @return 排序后的图片文件路径列表
	static std::vector<agi::fs::path> ScanImageSequence(agi::fs::path const& selected_file);

	/// @brief 构造图片视频提供者
	/// @param fps 帧率
	/// @param selected_file 用户选择的图片文件路径（自动扫描序列）
	ImageVideoProvider(agi::vfr::Framerate fps, agi::fs::path const& selected_file);

	/// @brief 生成 URI 字符串
	/// @param fps 帧率字符串
	/// @param filepath 图片文件路径
	/// @return ?image:fps:filepath 格式的 URI
	static std::string MakeFilename(std::string const& fps, std::string const& filepath);

	void GetFrame(int n, VideoFrame &frame) override;
	void SetColorSpace(std::string const&) override { }

	int GetFrameCount()             const override { return static_cast<int>(image_files.size()); }
	int GetWidth()                  const override { return width; }
	int GetHeight()                 const override { return height; }
	double GetDAR()                 const override { return 0; }
	agi::vfr::Framerate GetFPS()    const override { return fps; }
	std::vector<int> GetKeyFrames() const override { return {}; }
	std::string GetColorSpace()     const override { return "None"; }
	std::string GetDecoderName()    const override { return "Image Video Provider"; }
	bool WantsCaching()             const override { return true; }
	bool ShouldSetVideoProperties() const override { return false; }
};
