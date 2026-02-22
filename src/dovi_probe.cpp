/// @file dovi_probe.cpp
/// @brief 流级Dolby Vision探测工具实现
/// @ingroup video_input

#include "dovi_probe.h"

#ifdef WITH_FFMPEG

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/dovi_meta.h>
#include <libavcodec/defs.h>
}

#include <libaegisub/log.h>

DoviProbeResult ProbeDolbyVision(const std::string &filepath) {
	DoviProbeResult result;

	AVFormatContext *fmt_ctx = nullptr;
	int ret = avformat_open_input(&fmt_ctx, filepath.c_str(), nullptr, nullptr);
	if (ret < 0) {
		LOG_W("dovi_probe") << "Failed to open file for DV probe: " << filepath;
		return result;
	}

	ret = avformat_find_stream_info(fmt_ctx, nullptr);
	if (ret < 0) {
		LOG_W("dovi_probe") << "Failed to find stream info for DV probe: " << filepath;
		avformat_close_input(&fmt_ctx);
		return result;
	}

	// 查找第一条视频流
	int video_idx = -1;
	for (unsigned i = 0; i < fmt_ctx->nb_streams; ++i) {
		if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_idx = static_cast<int>(i);
			break;
		}
	}

	if (video_idx < 0) {
		LOG_D("dovi_probe") << "No video stream found in: " << filepath;
		avformat_close_input(&fmt_ctx);
		return result;
	}

	const AVStream *vstream = fmt_ctx->streams[video_idx];
	result.transfer = vstream->codecpar->color_trc;
	result.color_primaries = vstream->codecpar->color_primaries;

	// 检查 AV_PKT_DATA_DOVI_CONF（Dolby Vision配置记录）
	const AVPacketSideData *dovi_sd = av_packet_side_data_get(
		vstream->codecpar->coded_side_data,
		vstream->codecpar->nb_coded_side_data,
		AV_PKT_DATA_DOVI_CONF);

	if (dovi_sd && dovi_sd->data && dovi_sd->size >= sizeof(AVDOVIDecoderConfigurationRecord)) {
		const AVDOVIDecoderConfigurationRecord *dovi =
			reinterpret_cast<const AVDOVIDecoderConfigurationRecord *>(dovi_sd->data);
		result.has_dovi = true;
		result.dv_profile = dovi->dv_profile;
		LOG_D("dovi_probe") << "DV probe: found DOVI config, profile=" << result.dv_profile
			<< " transfer=" << result.transfer
			<< " primaries=" << result.color_primaries;
	} else {
		LOG_D("dovi_probe") << "DV probe: no DOVI config found"
			<< " transfer=" << result.transfer
			<< " primaries=" << result.color_primaries;
	}

	avformat_close_input(&fmt_ctx);
	return result;
}

#endif // WITH_FFMPEG
