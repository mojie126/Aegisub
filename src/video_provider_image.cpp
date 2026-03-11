/// @file video_provider_image.cpp
/// @brief 图片视频提供者实现
/// @ingroup video_input

#include "video_provider_image.h"

#include "video_frame.h"
#include "video_provider_dummy.h"

#include <libaegisub/fs.h>
#include <libaegisub/log.h>
#include <libaegisub/format.h>
#include <libaegisub/split.h>
#include <libaegisub/util.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <regex>

ImageVideoProvider::ImageVideoProvider(agi::vfr::Framerate fps, agi::fs::path const& selected_file)
: fps(std::move(fps))
{
	image_files = ScanImageSequence(selected_file);
	if (image_files.empty())
		throw VideoOpenError("No image files found");

	// 解码第一帧以获取分辨率
	std::vector<unsigned char> first_data;
	DecodeImage(image_files[0], first_data, width, height);

	// 缓存第一帧
	cached_frame_index = 0;
	cached_data = std::move(first_data);
}

void ImageVideoProvider::DecodeImage(agi::fs::path const& filepath,
                                     std::vector<unsigned char>& out_data,
                                     int& out_width, int& out_height)
{
	AVFormatContext *fmt_ctx = nullptr;
	int ret = avformat_open_input(&fmt_ctx, filepath.string().c_str(), nullptr, nullptr);
	if (ret < 0)
		throw VideoOpenError("Failed to open image file: " + filepath.string());

	ret = avformat_find_stream_info(fmt_ctx, nullptr);
	if (ret < 0) {
		avformat_close_input(&fmt_ctx);
		throw VideoOpenError("Failed to find stream info: " + filepath.string());
	}

	// 查找视频流（图片文件通常只有一个视频流）
	int video_idx = -1;
	for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
		if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_idx = static_cast<int>(i);
			break;
		}
	}

	if (video_idx < 0) {
		avformat_close_input(&fmt_ctx);
		throw VideoOpenError("No video stream in image file: " + filepath.string());
	}

	const AVCodecParameters *codecpar = fmt_ctx->streams[video_idx]->codecpar;
	const AVCodec *codec = avcodec_find_decoder(codecpar->codec_id);
	if (!codec) {
		avformat_close_input(&fmt_ctx);
		throw VideoOpenError("No decoder found for image: " + filepath.string());
	}

	AVCodecContext *codec_ctx = avcodec_alloc_context3(codec);
	if (!codec_ctx) {
		avformat_close_input(&fmt_ctx);
		throw VideoOpenError("Failed to allocate codec context");
	}

	avcodec_parameters_to_context(codec_ctx, codecpar);
	ret = avcodec_open2(codec_ctx, codec, nullptr);
	if (ret < 0) {
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&fmt_ctx);
		throw VideoOpenError("Failed to open codec for: " + filepath.string());
	}

	// 读取一帧
	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();
	bool decoded = false;

	while (av_read_frame(fmt_ctx, pkt) >= 0) {
		if (pkt->stream_index != video_idx) {
			av_packet_unref(pkt);
			continue;
		}

		ret = avcodec_send_packet(codec_ctx, pkt);
		av_packet_unref(pkt);
		if (ret < 0) break;

		ret = avcodec_receive_frame(codec_ctx, frame);
		if (ret == 0) {
			decoded = true;
			break;
		}
	}

	if (!decoded) {
		// 尝试 flush 解码器
		avcodec_send_packet(codec_ctx, nullptr);
		if (avcodec_receive_frame(codec_ctx, frame) == 0)
			decoded = true;
	}

	if (!decoded) {
		av_frame_free(&frame);
		av_packet_free(&pkt);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&fmt_ctx);
		throw VideoOpenError("Failed to decode image: " + filepath.string());
	}

	out_width = frame->width;
	out_height = frame->height;

	// 使用 swscale 转换为 BGRA8
	SwsContext *sws_ctx = sws_getContext(
		frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
		frame->width, frame->height, AV_PIX_FMT_BGRA,
		SWS_BILINEAR, nullptr, nullptr, nullptr);

	if (!sws_ctx) {
		av_frame_free(&frame);
		av_packet_free(&pkt);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&fmt_ctx);
		throw VideoOpenError("Failed to create swscale context");
	}

	const size_t row_bytes = static_cast<size_t>(frame->width) * 4;
	out_data.resize(row_bytes * frame->height);
	uint8_t *dst_data[1] = { out_data.data() };
	int dst_linesize[1] = { static_cast<int>(row_bytes) };

	sws_scale(sws_ctx, frame->data, frame->linesize, 0, frame->height,
	          dst_data, dst_linesize);

	sws_freeContext(sws_ctx);
	av_frame_free(&frame);
	av_packet_free(&pkt);
	avcodec_free_context(&codec_ctx);
	avformat_close_input(&fmt_ctx);
}

std::vector<agi::fs::path> ImageVideoProvider::ScanImageSequence(agi::fs::path const& selected_file)
{
	std::vector<agi::fs::path> result;

	if (!agi::fs::FileExists(selected_file)) {
		result.push_back(selected_file);
		return result;
	}

	auto dir = selected_file.parent_path();
	auto filename = selected_file.filename().string();

	// 提取文件名中最后一组连续数字
	// 使用前缀以非数字结尾的约束，确保数字组完整捕获
	// 例如 "img_0042.png" → prefix="img_", digits="0042", suffix=".png"
	// 如果文件名以数字开头无前缀如 "0042.png" → prefix="", digits="0042", suffix=".png"
	std::regex digit_regex("^(.*\\D)?(\\d+)(\\.[^.]+)$");
	std::smatch match;

	if (!std::regex_match(filename, match, digit_regex)) {
		// 没有数字序列，作为单张图片
		result.push_back(selected_file);
		return result;
	}

	std::string prefix = match[1].str();
	std::string digits = match[2].str();
	std::string suffix = match[3].str();
	size_t digit_width = digits.size();

	// 构建匹配模式：同前缀 + 同位数数字 + 同后缀
	// 扫描同目录下所有文件
	std::string filter = "*" + suffix;
	for (auto it = agi::fs::DirectoryIterator(dir, filter); it != agi::fs::DirectoryIterator(); ++it) {
		std::string name = *it;
		std::smatch file_match;
		if (!std::regex_match(name, file_match, digit_regex))
			continue;

		// 前缀和后缀必须匹配
		if (file_match[1].str() != prefix || file_match[3].str() != suffix)
			continue;

		result.push_back(dir / name);
	}

	// 按数字部分的数值自然排序
	std::sort(result.begin(), result.end(), [&](auto const& a, auto const& b) {
		std::string na = a.filename().string();
		std::string nb = b.filename().string();
		std::smatch ma, mb;
		if (std::regex_match(na, ma, digit_regex) && std::regex_match(nb, mb, digit_regex)) {
			auto da = ma[2].str();
			auto db = mb[2].str();
			auto va = std::stoull(da);
			auto vb = std::stoull(db);
			if (va != vb) return va < vb;
			if (da.size() != db.size()) return da.size() > db.size();
			return na < nb;
		}
		return na < nb;
	});

	// 如果扫描结果为空（不应发生），至少包含选中的文件
	if (result.empty())
		result.push_back(selected_file);

	LOG_D("image_provider") << "Scanned " << result.size()
	                        << " image(s) with pattern: " << prefix << "[" << digit_width << " digits]" << suffix;

	return result;
}

void ImageVideoProvider::GetFrame(int n, VideoFrame &frame) {
	n = std::clamp(n, 0, GetFrameCount() - 1);

	if (n != cached_frame_index) {
		DecodeImage(image_files[n], cached_data, width, height);
		cached_frame_index = n;
	}

	frame.data = cached_data;
	frame.width = width;
	frame.height = height;
	frame.pitch = width * 4;
	frame.flipped = false;
}

std::string ImageVideoProvider::MakeFilename(std::string const& fps, std::string const& filepath) {
	return "?image:" + fps + ":" + filepath;
}

namespace agi { class BackgroundRunner; }
std::unique_ptr<VideoProvider> CreateImageVideoProvider(agi::fs::path const& filename, std::string_view, agi::BackgroundRunner *) {
	auto generic = filename.generic_string();
	if (!generic.starts_with("?image:"))
		return {};

	// URI 格式: ?image:fps:filepath
	// fps 和 filepath 之间用第一个 ':' 分隔（filepath 可能包含 ':' 如 C:\...）
	auto fields = generic.substr(7); // 去掉 "?image:"
	auto first_colon = fields.find(':');
	if (first_colon == std::string::npos)
		throw VideoOpenError("Invalid image video URI: missing filepath");

	std::string fps_str = fields.substr(0, first_colon);
	std::string filepath = fields.substr(first_colon + 1);

	agi::vfr::Framerate fps;
	if (!DummyVideoProvider::TryParseFramerate(fps_str, fps))
		throw VideoOpenError("Unable to parse fps in image video URI");

	return std::make_unique<ImageVideoProvider>(fps, agi::fs::path(filepath));
}
