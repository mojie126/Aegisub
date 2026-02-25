// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
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

/// @file video_provider_bestsource.cpp
/// @brief BestSource-based video provider
/// @ingroup video_input bestsource
///

#ifdef WITH_BESTSOURCE
#include "include/aegisub/video_provider.h"

#include "bestsource_common.h"

#include "videosource.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include "options.h"
#include "compat.h"
#include "video_frame.h"
namespace agi { class BackgroundRunner; }

#include <libaegisub/fs.h>
#include <libaegisub/path.h>
#include <libaegisub/dispatch.h>
#include <libaegisub/background_runner.h>
#include <libaegisub/log.h>
#include <libaegisub/format.h>
#include <libaegisub/scoped_ptr.h>

namespace {

/// @class BSVideoProvider
/// @brief Implements video loading through BestSource.
class BSVideoProvider final : public VideoProvider {
	std::map<std::string, std::string> bsopts;
	bool apply_rff;

	std::unique_ptr<BestVideoSource> bs;
	BSVideoProperties properties;

	std::vector<int> Keyframes;
	agi::vfr::Framerate Timecodes;
	AVPixelFormat pixfmt;
	std::string colorspace;
	int video_cs = -1;		// Reported or guessed color matrix of first frame
	int video_cr = -1;		// Reported or guessed color range of first frame
	bool has_audio = false;

	bool is_linear = false;

	HDRType detected_hdr_type_ = HDRType::SDR;  // 检测到的HDR类型

	agi::scoped_holder<SwsContext *> sws_context;

public:
	BSVideoProvider(agi::fs::path const& filename, std::string const& colormatrix, agi::BackgroundRunner *br);

	void GetFrame(int n, VideoFrame &out) override;

	void SetColorSpace(std::string const& matrix) override { colorspace = matrix; }

	int GetFrameCount() const override { return properties.NumFrames; };

	int GetWidth() const override { return properties.Width; };
	int GetHeight() const override { return properties.Height; };
	double GetDAR() const override { return ((double) properties.Width * properties.SAR.Num) / (properties.Height * properties.SAR.Den); };

	agi::vfr::Framerate GetFPS() const override { return Timecodes; };
	std::string GetColorSpace() const override { return colorspace; };
	std::string GetRealColorSpace() const override {
		std::string result = ColorMatrix::colormatrix_description(video_cs, video_cr);
		if (result == "") {
			return "None";
		}
		return result;
	};
	std::vector<int> GetKeyFrames() const override { return Keyframes; };
	std::string GetDecoderName() const override { return "BestSource"; };
	bool WantsCaching() const override { return false; };
	bool HasAudio() const override { return has_audio; };
	HDRType GetHDRType() const override { return detected_hdr_type_; };
	bool IsHWDecoding() const override {
		auto hw_name = OPT_GET("Provider/Video/BestSource/HW hw_name")->GetString();
		return !hw_name.empty() && hw_name != "none";
	};
};

BSVideoProvider::BSVideoProvider(agi::fs::path const& filename, std::string const& colormatrix, agi::BackgroundRunner *br) try
: apply_rff(OPT_GET("Provider/Video/BestSource/Apply RFF"))
, sws_context(nullptr, sws_freeContext)
{
	provider_bs::CleanBSCache();

	auto track_info = provider_bs::SelectTrack(filename, false);
	has_audio = track_info.second;

	if (track_info.first == provider_bs::TrackSelection::NoTracks)
		throw VideoNotSupported("no video tracks found");
	else if (track_info.first == provider_bs::TrackSelection::None)
		throw agi::UserCancelException("video loading cancelled by user");

	bool cancelled = false;
	// 读取硬件加速设备名称
	const auto bs_hw_name = OPT_GET("Provider/Video/BestSource/HW hw_name")->GetString();
	const std::string hw_device = (bs_hw_name.empty() || bs_hw_name == "none") ? "" : bs_hw_name;
	const int extra_hw_frames = hw_device.empty() ? 0 : 32;
	br->Run([&](agi::ProgressSink *ps) {
		ps->SetTitle(from_wx(_("Indexing")));
		ps->SetMessage(from_wx(_("Decoding the full track to ensure perfect frame accuracy. This will take a while!")));
		try {
			bs = std::make_unique<BestVideoSource>(filename.string(), hw_device, extra_hw_frames, static_cast<int>(track_info.first), 0, OPT_GET("Provider/Video/BestSource/Threads")->GetInt(), 1, provider_bs::GetCacheFile(filename), &bsopts, [=](int Track, int64_t Current, int64_t Total) {
				ps->SetProgress(Current, Total);
				return !ps->IsCancelled();
			});
		} catch (BestSourceException const& err) {
			if (std::string(err.what()) == "Indexing canceled by user")
				cancelled = true;
			else
				throw err;
		}
	});
	if (cancelled)
		throw agi::UserCancelException("video loading cancelled by user");

	bs->SetMaxCacheSize(OPT_GET("Provider/Video/BestSource/Max Cache Size")->GetInt() << 20);
	bs->SetSeekPreRoll(OPT_GET("Provider/Video/BestSource/Seek Preroll")->GetInt());

	properties = bs->GetVideoProperties();

	br->Run([&](agi::ProgressSink *ps) {
		ps->SetTitle(from_wx(_("Scanning")));
		ps->SetMessage(from_wx(_("Reading timecodes and frame/sample data")));

		std::vector<int> TimecodesVector;
		for (int n = 0; n < properties.NumFrames; n++) {
			const BestVideoSource::FrameInfo &info = bs->GetFrameInfo(n);
			if (info.KeyFrame) {
				Keyframes.push_back(n);
			}

			TimecodesVector.push_back(1000 * info.PTS * properties.TimeBase.Num / properties.TimeBase.Den);

			if (n % 16 == 0) {
				if (ps->IsCancelled())
					return;
				ps->SetProgress(n, properties.NumFrames);
			}
		}

		if (TimecodesVector.size() < 2 || TimecodesVector.front() == TimecodesVector.back()) {
			Timecodes = (double) properties.FPS.Num / properties.FPS.Den;
		} else {
			Timecodes = agi::vfr::Framerate(TimecodesVector);
		}
	});

	// Decode the first frame to get the color space and pixel format
	std::unique_ptr<BestVideoFrame> frame(bs->GetFrame(0));
	auto avframe = frame->GetAVFrame();
	video_cs = avframe->colorspace;
	video_cr = avframe->color_range;
	ColorMatrix::guess_colorspace(video_cs, video_cr, properties.Width, properties.Height);
	pixfmt = (AVPixelFormat) avframe->format;

	// 检测HDR类型：优先检测 Dolby Vision 帧级 RPU，再检测传输特性
	{
		// BestVideoFrame 构造时已将 AV_FRAME_DATA_DOVI_RPU_BUFFER 提取到 DolbyVisionRPU 字段
		bool has_dovi = (frame->DolbyVisionRPU != nullptr && frame->DolbyVisionRPUSize > 0);
		int trc = frame->Transfer;  // 直接使用 BestVideoFrame 的 Transfer 字段

		if (has_dovi) {
			detected_hdr_type_ = HDRType::DolbyVision;
			LOG_D("bestsource") << "HDR detection: DolbyVision (frame-level RPU, size=" << frame->DolbyVisionRPUSize << "), Transfer=" << trc;
		} else if (trc == 16) {
			detected_hdr_type_ = HDRType::PQ;
			LOG_D("bestsource") << "HDR detection: PQ (SMPTE ST 2084), color_trc=" << trc;
		} else if (trc == 18) {
			detected_hdr_type_ = HDRType::HLG;
			LOG_D("bestsource") << "HDR detection: HLG (ARIB STD-B67), color_trc=" << trc;
		} else {
			detected_hdr_type_ = HDRType::SDR;
			LOG_D("bestsource") << "HDR detection: SDR, color_trc=" << trc;
		}
	}

	sws_context = sws_getContext(
			properties.Width, properties.Height, pixfmt,
			properties.Width, properties.Height, AV_PIX_FMT_BGR0,
			SWS_BICUBIC, nullptr, nullptr, nullptr);

	if (sws_context == nullptr) {
		throw VideoDecodeError("Cannot convert frame to RGB!");
	}

	SetColorSpace(colormatrix);
}
catch (BestSourceException const& err) {
	throw VideoOpenError(agi::format("Failed to create BestVideoSource: %s",  + err.what()));
}

void BSVideoProvider::GetFrame(int n, VideoFrame &out) {
	std::unique_ptr<BestVideoFrame> bsframe(apply_rff ? bs->GetFrameWithRFF(n) : bs->GetFrame(n));
	if (bsframe == nullptr) {
		throw VideoDecodeError("Couldn't read frame!");
	}

	if (!is_linear && bs->GetLinearDecodingState()) {
		agi::dispatch::Main().Async([] {
			wxMessageBox(_("BestSource had to fall back to linear decoding. Seeking through the video will be very slow now. You may want to try a different video provider, but note that those are not guaranteed to be frame-exact."), _("Warning"), wxOK | wxICON_WARNING | wxCENTER);
		});

		is_linear = true;
	}

	const AVFrame *frame = bsframe->GetAVFrame();

	int cs = frame->colorspace;
	int cr = frame->color_range;
	ColorMatrix::override_colormatrix(cs, cr, colorspace, properties.Width, properties.Height);
	const int *coefficients = sws_getCoefficients(cs);

	if (frame->format != pixfmt || frame->width != properties.Width || frame->height != properties.Height)
		throw VideoDecodeError("Video has variable format!");

	sws_setColorspaceDetails(sws_context,
		coefficients, cr == AVCOL_RANGE_JPEG,
		coefficients, cr == AVCOL_RANGE_JPEG,
		0, 1 << 16, 1 << 16);

	out.data.resize(frame->width * frame->height * 4);
	uint8_t *data[1] = {&out.data[0]};
	int stride[1] = {frame->width * 4};
	sws_scale(sws_context, frame->data, frame->linesize, 0, frame->height, data, stride);

	out.width = frame->width;
	out.height = frame->height;
	out.pitch = stride[0];
	out.flipped = false; 		// TODO figure out flipped
}

}

std::unique_ptr<VideoProvider> CreateBSVideoProvider(std::filesystem::path const& path, std::string_view colormatrix, agi::BackgroundRunner *br) {
	return std::make_unique<BSVideoProvider>(path, std::string(colormatrix), br);
}

#endif /* WITH_BESTSOURCE */
