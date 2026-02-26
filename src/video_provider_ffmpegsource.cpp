// Copyright (c) 2008-2009, Karl Blomster
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file video_provider_ffmpegsource.cpp
/// @brief FFmpegSource2-based video provider
/// @ingroup video_input ffms
///

#ifdef WITH_FFMS2
#include "ffmpegsource_common.h"
#include "include/aegisub/video_provider.h"

#include "options.h"
#include "utils.h"
#include "video_frame.h"

#include <libaegisub/fs.h>
#include <libaegisub/log.h>

#include <algorithm>
#include <string_view>

#include <cstring>

namespace {
/// @class FFmpegSourceVideoProvider
/// @brief Implements video loading through the FFMS library.
class FFmpegSourceVideoProvider final : public VideoProvider, FFmpegSourceProvider {
	/// video source object
	agi::scoped_holder<FFMS_VideoSource*, void (FFMS_CC*)(FFMS_VideoSource*)> VideoSource;
	const FFMS_VideoProperties *VideoInfo = nullptr; ///< video properties

	int Width = -1;                 ///< width in pixels
	int Height = -1;                ///< height in pixels
	int VideoCS = -1;               ///< Reported colorspace of first frame (or guessed if unspecified)
	int VideoCR = -1;               ///< Reported colorrange of first frame (or guessed if unspecified)
	int VideoTransfer = -1;         ///< 传输特性（PQ=16, HLG=18等，对应AVColorTransferCharacteristic）
	int VideoColorPrimaries = -1;   ///< 色域原色（BT.2020=9，对应AVColorPrimaries）
	bool hasDolbyVision = false;    ///< 是否检测到Dolby Vision（帧级RPU或流级DOVI_CONF）
	bool hasFrameLevelRPU = false;  ///< 首帧是否包含帧级Dolby Vision RPU元数据
	double DAR;                     ///< display aspect ratio
	std::vector<int> KeyFramesList; ///< list of keyframes
	agi::vfr::Framerate Timecodes;  ///< vfr object
	std::string ColorSpace;         ///< Colorspace name
	int Padding = 0;                ///< vertical black border size in pixels (top and bottom)

	char FFMSErrMsg[1024];          ///< FFMS error message
	FFMS_ErrorInfo ErrInfo;         ///< FFMS error codes/messages
	bool has_audio = false;

	void LoadVideo(agi::fs::path const& filename, std::string_view colormatrix);

public:
	FFmpegSourceVideoProvider(agi::fs::path const& filename, std::string_view colormatrix, agi::BackgroundRunner *br);

	void GetFrame(int n, VideoFrame &out) override;

	void SetColorSpace(std::string const& matrix) override {
		if (matrix == ColorSpace) return;

		int CS = VideoCS;
		int CR = VideoCR;
		ColorMatrix::override_colormatrix(CS, CR, matrix, Width, Height);

		if (FFMS_SetInputFormatV(VideoSource, CS, CR, FFMS_GetPixFmt(""), &ErrInfo))
			throw VideoOpenError(std::string("Failed to set input format: ") + ErrInfo.Buffer);

		ColorSpace = matrix;
	}

	int GetFrameCount() const override             { return VideoInfo->NumFrames; }

#if FFMS_VERSION >= ((2 << 24) | (24 << 16) | (0 << 8) | 0)
	int GetWidth() const override  { return (VideoInfo->Rotation % 180 == 90 || VideoInfo->Rotation % 180 == -90) ? Height : Width; }
	int GetHeight() const override { return ((VideoInfo->Rotation % 180 == 90 || VideoInfo->Rotation % 180 == -90) ? Width : Height) + Padding * 2; }
	double GetDAR() const override {
		// SAR 未定义或为 1:1 时返回 0，使用 Default AR（自动跟随像素尺寸），
		// 避免 ABB 变更后旧 Custom AR 值导致显示比例失真
		if (VideoInfo->SARDen <= 0 || VideoInfo->SARNum <= 0 || VideoInfo->SARNum == VideoInfo->SARDen)
			return 0;
		const bool rotated = VideoInfo->Rotation % 180 == 90 || VideoInfo->Rotation % 180 == -90;
		const double sar = static_cast<double>(VideoInfo->SARNum) / VideoInfo->SARDen;
		if (rotated)
			return static_cast<double>(Height) / ((static_cast<double>(Width + Padding * 2)) * sar);
		return (static_cast<double>(Width) * sar) / static_cast<double>(Height + Padding * 2);
	}
#else
	int GetWidth() const override                  { return Width; }
	int GetHeight() const override                 { return Height + Padding * 2; }
	double GetDAR() const override {
		// SAR 未定义或为 1:1 时返回 0，使用 Default AR
		if (VideoInfo->SARDen <= 0 || VideoInfo->SARNum <= 0 || VideoInfo->SARNum == VideoInfo->SARDen)
			return 0;
		const double sar = static_cast<double>(VideoInfo->SARNum) / VideoInfo->SARDen;
		return (static_cast<double>(Width) * sar) / static_cast<double>(Height + Padding * 2);
	}
#endif

	agi::vfr::Framerate GetFPS() const override    { return Timecodes; }
	std::string GetColorSpace() const override     { return ColorSpace; }
	std::string GetRealColorSpace() const override {
		std::string result = ColorMatrix::colormatrix_description(VideoCS, VideoCR);
		if (result == "") {
			return "None";
		}
		return result;
	}
	std::vector<int> GetKeyFrames() const override { return KeyFramesList; };
	std::string GetDecoderName() const override    { return "FFmpegSource"; }
	bool WantsCaching() const override             { return true; }
	bool HasAudio() const override                 { return has_audio; }
	bool IsHWDecoding() const override {
		auto hw_name = OPT_GET("Provider/Video/FFmpegSource/HW hw_name")->GetString();
		return !hw_name.empty() && hw_name != "none";
	}
	HDRType GetHDRType() const override {
		// 帧级RPU存在 → 返回DolbyVision（可使用DV专用LUT）
		if (hasFrameLevelRPU) return HDRType::DolbyVision;
		// 流级DOVI_CONF存在但无帧级RPU → 仍视为DolbyVision，使用DV2SDR.cube
		if (hasDolbyVision) return HDRType::DolbyVision;
		// 检测传输特性：PQ (SMPTE ST 2084) = 16, HLG (ARIB STD-B67) = 18
		if (VideoTransfer == 16) return HDRType::PQ;
		if (VideoTransfer == 18) return HDRType::HLG;
		return HDRType::SDR;
	}
};

FFmpegSourceVideoProvider::FFmpegSourceVideoProvider(agi::fs::path const& filename, std::string_view colormatrix, agi::BackgroundRunner *br) try
: FFmpegSourceProvider(br)
, VideoSource(nullptr, FFMS_DestroyVideoSource)
{
	ErrInfo.Buffer		= FFMSErrMsg;
	ErrInfo.BufferSize	= sizeof(FFMSErrMsg);
	ErrInfo.ErrorType	= FFMS_ERROR_SUCCESS;
	ErrInfo.SubType		= FFMS_ERROR_SUCCESS;

	SetLogLevel();

	LoadVideo(filename, colormatrix);
}
catch (agi::EnvironmentError const& err) {
	throw VideoOpenError(err.GetMessage());
}

void FFmpegSourceVideoProvider::LoadVideo(agi::fs::path const& filename, std::string_view colormatrix) {
	FFMS_Indexer *Indexer = FFMS_CreateIndexer(filename.string().c_str(), &ErrInfo);
	if (!Indexer) {
		if (ErrInfo.SubType == FFMS_ERROR_FILE_READ)
			throw agi::fs::FileNotFound(std::string(ErrInfo.Buffer));
		else
			throw VideoNotSupported(ErrInfo.Buffer);
	}

	std::map<int, std::string> TrackList = GetTracksOfType(Indexer, FFMS_TYPE_VIDEO);
	if (TrackList.size() <= 0)
		throw VideoNotSupported("no video tracks found");

	int TrackNumber = -1;
	if (TrackList.size() > 1) {
		auto Selection = AskForTrackSelection(TrackList, FFMS_TYPE_VIDEO);
		if (Selection == TrackSelection::None)
			throw agi::UserCancelException("video loading cancelled by user");
		TrackNumber = static_cast<int>(Selection);
	}

	// generate a name for the cache file
	auto CacheName = GetCacheFilename(filename);

	// try to read index
	agi::scoped_holder<FFMS_Index*, void (FFMS_CC*)(FFMS_Index*)>
		Index(FFMS_ReadIndex(CacheName.string().c_str(), &ErrInfo), FFMS_DestroyIndex);

	if (Index && FFMS_IndexBelongsToFile(Index, filename.string().c_str(), &ErrInfo))
		Index = nullptr;

	// time to examine the index and check if the track we want is indexed
	// technically this isn't really needed since all video tracks should always be indexed,
	// but a bit of sanity checking never hurt anyone
	if (Index && TrackNumber >= 0) {
		FFMS_Track *TempTrackData = FFMS_GetTrackFromIndex(Index, TrackNumber);
		if (FFMS_GetNumFrames(TempTrackData) <= 0)
			Index = nullptr;
	}

	// moment of truth
	if (!Index) {
		auto TrackMask = TrackSelection::None;
		if (OPT_GET("Provider/FFmpegSource/Index All Tracks")->GetBool() || OPT_GET("Video/Open Audio")->GetBool())
			TrackMask = TrackSelection::All;
		Index = DoIndexing(Indexer, CacheName, TrackMask, GetErrorHandlingMode());
	}
	else {
		FFMS_CancelIndexing(Indexer);
	}

	// update access time of index file so it won't get cleaned away
	agi::fs::Touch(CacheName);

	// we have now read the index and may proceed with cleaning the index cache
	CleanCache();

	// track number still not set?
	if (TrackNumber < 0) {
		// just grab the first track
		TrackNumber = FFMS_GetFirstIndexedTrackOfType(Index, FFMS_TYPE_VIDEO, &ErrInfo);
		if (TrackNumber < 0)
			throw VideoNotSupported(std::string("Couldn't find any video tracks: ") + ErrInfo.Buffer);
	}

	// Check if there's an audio track
	has_audio = FFMS_GetFirstTrackOfType(Index, FFMS_TYPE_AUDIO, nullptr) != -1;

	// set thread count
	int Threads = OPT_GET("Provider/Video/FFmpegSource/Decoding Threads")->GetInt();
#if FFMS_VERSION < ((2 << 24) | (30 << 16) | (0 << 8) | 0)
	if (FFMS_GetVersion() < ((2 << 24) | (17 << 16) | (2 << 8) | 1) && FFMS_GetSourceType(Index) == FFMS_SOURCE_LAVF)
		Threads = 1;
#endif

	// set seekmode
	// TODO: give this its own option?
	int SeekMode;
	if (OPT_GET("Provider/Video/FFmpegSource/Unsafe Seeking")->GetBool())
		SeekMode = FFMS_SEEK_UNSAFE;
	else
		SeekMode = FFMS_SEEK_NORMAL;

	const auto hw_name_str = OPT_GET("Provider/Video/FFmpegSource/HW hw_name")->GetString();
	const auto hw_name = hw_name_str.c_str();
	Padding = OPT_GET("Provider/Video/FFmpegSource/ABB")->GetInt();
	if (Padding < 0)
		Padding = 0;
	VideoSource = FFMS_CreateVideoSource(filename.string().c_str(), TrackNumber, Index, Threads, SeekMode, &ErrInfo, hw_name, 0);
	if (!VideoSource)
		throw VideoOpenError(std::string("Failed to open video track: ") + ErrInfo.Buffer);

	// load video properties
	VideoInfo = FFMS_GetVideoProperties(VideoSource);

	const FFMS_Frame *TempFrame = FFMS_GetFrame(VideoSource, 0, &ErrInfo);
	if (!TempFrame)
		throw VideoOpenError(std::string("Failed to decode first frame: ") + ErrInfo.Buffer);

	Width  = TempFrame->EncodedWidth;
	Height = TempFrame->EncodedHeight;
	if (VideoInfo->SARDen > 0 && VideoInfo->SARNum > 0)
		DAR = double(Width) * VideoInfo->SARNum / ((double)Height * VideoInfo->SARDen);
	else
		DAR = double(Width) / Height;

	VideoCS = TempFrame->ColorSpace;
	VideoCR = TempFrame->ColorRange;
	VideoTransfer = TempFrame->TransferCharateristics;
	VideoColorPrimaries = TempFrame->ColorPrimaries;
	hasFrameLevelRPU = (TempFrame->DolbyVisionRPUSize > 0);
	hasDolbyVision = hasFrameLevelRPU;

	// 硬件解码时帧级色彩属性可能为 UNSPECIFIED，从流参数回退
	if ((VideoTransfer <= 0 || VideoTransfer == 2 /*AVCOL_TRC_UNSPECIFIED*/)
		&& VideoInfo->StreamTransferCharacteristics > 0
		&& VideoInfo->StreamTransferCharacteristics != 2)
		VideoTransfer = VideoInfo->StreamTransferCharacteristics;
	if ((VideoCS <= 0 || VideoCS == 2 /*AVCOL_SPC_UNSPECIFIED*/)
		&& VideoInfo->StreamColorSpace > 0
		&& VideoInfo->StreamColorSpace != 2)
		VideoCS = VideoInfo->StreamColorSpace;
	if ((VideoColorPrimaries <= 0 || VideoColorPrimaries == 2 /*AVCOL_PRI_UNSPECIFIED*/)
		&& VideoInfo->StreamColorPrimaries > 0
		&& VideoInfo->StreamColorPrimaries != 2)
		VideoColorPrimaries = VideoInfo->StreamColorPrimaries;

	// 流级别 Dolby Vision 配置记录检测（帧级 RPU 在硬件解码时可能缺失）
	if (!hasDolbyVision && VideoInfo->HasDolbyVision)
		hasDolbyVision = true;

	LOG_D("provider/video/ffms") << "HDR detection: TransferCharateristics=" << VideoTransfer
		<< " ColorSpace=" << VideoCS << " ColorRange=" << VideoCR
		<< " ColorPrimaries=" << VideoColorPrimaries
		<< " FrameColorPrimaries=" << TempFrame->ColorPrimaries
		<< " DolbyVisionRPUSize=" << TempFrame->DolbyVisionRPUSize
		<< " hasDolbyVision=" << hasDolbyVision
		<< " hasFrameLevelRPU=" << hasFrameLevelRPU
		<< " StreamTransfer=" << VideoInfo->StreamTransferCharacteristics
		<< " StreamColorSpace=" << VideoInfo->StreamColorSpace
		<< " StreamColorPrimaries=" << VideoInfo->StreamColorPrimaries
		<< " HasDV=" << VideoInfo->HasDolbyVision
		<< " DVProfile=" << VideoInfo->DolbyVisionProfile;

	// 从 FFMS_VideoProperties 获取 HDR 元数据辅助检测
	LOG_D("provider/video/ffms") << "VideoProperties: HasMasteringDisplayPrimaries=" << VideoInfo->HasMasteringDisplayPrimaries
		<< " HasMasteringDisplayLuminance=" << VideoInfo->HasMasteringDisplayLuminance
		<< " HasContentLightLevel=" << VideoInfo->HasContentLightLevel
		<< " MaxLuminance=" << VideoInfo->MasteringDisplayMaxLuminance
		<< " ContentLightLevelMax=" << VideoInfo->ContentLightLevelMax;

	ColorMatrix::guess_colorspace(VideoCS, VideoCR, Width, Height);

	SetColorSpace(std::string(colormatrix));

	int output_resizer = FFMS_RESIZER_BICUBIC;
	const bool hw_enabled = !hw_name_str.empty() && hw_name_str != "none";
	// For HW decode + black border workflow, prefer faster colorspace conversion.
	if (hw_enabled && Padding > 0)
		output_resizer = FFMS_RESIZER_FAST_BILINEAR;

	const int TargetFormat[] = { FFMS_GetPixFmt("bgra"), -1 };
	if (FFMS_SetOutputFormatV2(VideoSource, TargetFormat, Width, Height, output_resizer, &ErrInfo))
		throw VideoOpenError(std::string("Failed to set output format: ") + ErrInfo.Buffer);

	// get frame info data
	FFMS_Track *FrameData = FFMS_GetTrackFromVideo(VideoSource);
	if (FrameData == nullptr)
		throw VideoOpenError("failed to get frame data");
	const FFMS_TrackTimeBase *TimeBase = FFMS_GetTimeBase(FrameData);
	if (TimeBase == nullptr)
		throw VideoOpenError("failed to get track time base");

	// build list of keyframes and timecodes
	std::vector<int> TimecodesVector;
	for (int CurFrameNum = 0; CurFrameNum < VideoInfo->NumFrames; CurFrameNum++) {
		const FFMS_FrameInfo *CurFrameData = FFMS_GetFrameInfo(FrameData, CurFrameNum);
		if (!CurFrameData)
			throw VideoOpenError("Couldn't get info about frame " + std::to_string(CurFrameNum));

		// keyframe?
		if (CurFrameData->KeyFrame)
			KeyFramesList.push_back(CurFrameNum);

		// calculate timestamp and add to timecodes vector
		// 使用四舍五入而非截断，避免亚毫秒精度导致帧时间码偏移
		int Timestamp = std::lround(CurFrameData->PTS * TimeBase->Num / TimeBase->Den);
		TimecodesVector.push_back(Timestamp);
	}
	if (TimecodesVector.size() < 2)
		Timecodes = 25.0;
	else
		Timecodes = agi::vfr::Framerate(TimecodesVector);
}

void FFmpegSourceVideoProvider::GetFrame(int n, VideoFrame &out) {
	n = std::clamp(n, 0, GetFrameCount() - 1);

	const auto frame = FFMS_GetFrame(VideoSource, n, &ErrInfo);
	if (!frame)
		throw VideoDecodeError(std::string("Failed to retrieve frame: ") +  ErrInfo.Buffer);

	const size_t row_bytes = static_cast<size_t>(Width) * 4;
	const size_t tight_frame_bytes = row_bytes * static_cast<size_t>(Height);
	const uint8_t *src_base = frame->Data[0];
	int src_pitch = frame->Linesize[0];

	// Normalize negative line size to top-down order.
	if (src_pitch < 0) {
		src_base += static_cast<ptrdiff_t>(Height - 1) * static_cast<ptrdiff_t>(src_pitch);
		src_pitch = -src_pitch;
	}
	if (src_pitch < static_cast<int>(row_bytes))
		throw VideoDecodeError("Retrieved frame pitch is smaller than expected row size.");

	if (out.data.size() != tight_frame_bytes)
		out.data.resize(tight_frame_bytes);

	if (tight_frame_bytes > 0) {
		if (src_pitch == static_cast<int>(row_bytes)) {
			std::memcpy(out.data.data(), src_base, tight_frame_bytes);
		}
		else {
			for (int y = 0; y < Height; ++y) {
				std::memcpy(
					out.data.data() + row_bytes * y,
					src_base + static_cast<ptrdiff_t>(src_pitch) * static_cast<ptrdiff_t>(y),
					row_bytes
				);
			}
		}
	}
	out.flipped = false;
	out.hflipped = false;
	out.rotation = 0;
	out.width = Width;
	out.height = Height;
	out.pitch = row_bytes;
#if FFMS_VERSION >= ((2 << 24) | (31 << 16) | (0 << 8) | 0)
	// GPU翻转：通过标志位传递到渲染阶段，由glOrtho投影变换处理，避免CPU逐像素交换
	if (VideoInfo->Flip > 0)
		out.hflipped = true;
	else if (VideoInfo->Flip < 0)
		out.flipped = true;
#endif
#if FFMS_VERSION >= ((2 << 24) | (24 << 16) | (0 << 8) | 0)
	{
		// GPU旋转：通过标志位传递到渲染阶段，由FBO后处理或glOrtho投影处理
		int rot = VideoInfo->Rotation % 360;
		if (rot < 0) rot += 360;
		if (rot == 180) {
			// 180°旋转等价于水平+垂直翻转的组合
			out.hflipped = !out.hflipped;
			out.flipped = !out.flipped;
		} else if (rot == 90 || rot == 270) {
			out.rotation = rot;
			// 90/270°旋转不在CPU端交换宽高，保持原始数据维度
			// GetWidth/GetHeight已提供旋转后的显示维度给viewport计算
		}
	}
#endif

	// GPU黑边（ABB）处理：不再CPU嵌入黑边数据，只在GPU侧通过glViewport和glClear渲染
	// 这样避免CPU memcpy，始终走硬解GPU直通路径
	out.padding = Padding;
	// 注意：out.width、out.height保持原内容尺寸，GPU侧Render()会根据padding调整viewport绘制黑边
}
}

std::unique_ptr<VideoProvider> CreateFFmpegSourceVideoProvider(agi::fs::path const& path, std::string_view colormatrix, agi::BackgroundRunner *br) {
	return std::make_unique<FFmpegSourceVideoProvider>(path, colormatrix, br);
}

#endif /* WITH_FFMS2 */
