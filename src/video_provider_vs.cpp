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

#ifdef WITH_VAPOURSYNTH
#include "include/aegisub/video_provider.h"

#include "compat.h"
#include "options.h"
#include "video_frame.h"

#include <libaegisub/access.h>
#include <libaegisub/background_runner.h>
#include <libaegisub/format.h>
#include <libaegisub/keyframe.h>
#include <libaegisub/log.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/path.h>
#include <libaegisub/scoped_ptr.h>

#include <mutex>

#include "vapoursynth_wrap.h"
#include "vapoursynth_common.h"
#include "VSScript4.h"
#include "VSHelper4.h"
#include "VSConstants4.h"

static const char *kf_key = "__aegi_keyframes";
static const char *tc_key = "__aegi_timecodes";
static const char *audio_key = "__aegi_hasaudio";

namespace {
class VapourSynthVideoProvider: public VideoProvider {
	VapourSynthWrapper vs;
	agi::scoped_holder<VSScript *> script;
	agi::scoped_holder<VSNode *> source_node;
	agi::scoped_holder<VSNode *> prepared_node;
	const VSVideoInfo *vi = nullptr;

	double dar = 0;
	agi::vfr::Framerate fps;
	std::vector<int> keyframes;
	std::string colorspace;
	int video_cs = -1;		// Reported or guessed color matrix of first frame
	int video_cr = -1;		// Reported or guessed color range of first frame
	bool has_audio = false;

	agi::scoped_holder<const VSFrame *, void (*)(const VSFrame *) noexcept> GetVSFrame(VSNode *node, int n);
	void SetResizeArg(VSMap *args, const VSMap *props, const char *arg_name, const char *prop_name, int64_t deflt, int64_t unspecified = -1);

public:
	VapourSynthVideoProvider(agi::fs::path const& filename, std::string const& colormatrix, agi::BackgroundRunner *br);

	void GetFrame(int n, VideoFrame &frame) override;

	void SetColorSpace(std::string const& matrix) override;

	int GetFrameCount() const override             { return vi->numFrames; }
	agi::vfr::Framerate GetFPS() const override    { return fps; }
	int GetWidth() const override                  { return vi->width; }
	int GetHeight() const override                 { return vi->height; }
	double GetDAR() const override                 { return dar; }
	std::vector<int> GetKeyFrames() const override { return keyframes; }
	std::string GetColorSpace() const override     { return colorspace; }
	std::string GetRealColorSpace() const override {
		std::string result = ColorMatrix::colormatrix_description(video_cs, video_cr);
		if (result == "") {
			return "None";
		}
		return result;
	}
	bool HasAudio() const override                 { return has_audio; }
	bool WantsCaching() const override             { return true; }
	std::string GetDecoderName() const override    { return "VapourSynth"; }
	bool ShouldSetVideoProperties() const override { return colorspace != "Unknown"; }
};

VapourSynthVideoProvider::VapourSynthVideoProvider(agi::fs::path const& filename, std::string const& colormatrix, agi::BackgroundRunner *br) try
: vs()
, script(nullptr, vs.GetScriptAPI()->freeScript)
, source_node(nullptr, vs.GetAPI()->freeNode)
, prepared_node(nullptr, vs.GetAPI()->freeNode) {
	std::lock_guard<std::mutex> lock(vs.GetMutex());

	VSCleanCache();

	int err1, err2;
	// createScript takes ownership of the core so no need for a scoped_holder here
	VSCore *core = vs.GetAPI()->createCore(OPT_GET("Provider/VapourSynth/Autoload User Plugins")->GetBool() ? 0 : VSCoreCreationFlags::ccfDisableAutoLoading);
	if (core == nullptr) {
		throw VapourSynthError("Error creating core");
	}
	script = vs.GetScriptAPI()->createScript(core);
	if (script == nullptr) {
		throw VapourSynthError("Error creating script API");
	}
	vs.GetScriptAPI()->evalSetWorkingDir(script, 1);
	br->Run([&](agi::ProgressSink *ps) {
		ps->SetTitle(from_wx(_("Executing VapourSynth Script")));
		ps->SetMessage("");
		ps->SetIndeterminate();

		VSLogHandle *logger = vs.GetAPI()->addLogHandler(VSLogToProgressSink, nullptr, ps, core);
		err1 = OpenScriptOrVideo(vs.GetAPI(), vs.GetScriptAPI(), script, filename, OPT_GET("Provider/Video/VapourSynth/Default Script")->GetString());
		vs.GetAPI()->removeLogHandler(logger, core);

		ps->SetStayOpen(bool(err1));
		if (err1)
			ps->SetMessage(from_wx(_("Failed to execute script! Press \"Close\" to continue.")));
	});
	if (err1) {
		std::string msg = agi::format("Error executing VapourSynth script: %s", vs.GetScriptAPI()->getError(script));
		throw VapourSynthError(msg);
	}
	source_node = vs.GetScriptAPI()->getOutputNode(script, 0);
	if (source_node == nullptr)
		throw VapourSynthError("No output node set");

	if (vs.GetAPI()->getNodeType(source_node) != mtVideo) {
		throw VapourSynthError("Output node isn't a video node");
	}
	vi = vs.GetAPI()->getVideoInfo(source_node);
	if (vi == nullptr)
		throw VapourSynthError("Couldn't get video info");
	if (!vsh::isConstantVideoFormat(vi))
		throw VapourSynthError("Video doesn't have constant format");

	int fpsNum = vi->fpsNum;
	int fpsDen = vi->fpsDen;
	if (fpsDen == 0) {
		fpsNum = 25;
		fpsDen = 1;
	}
	fps = agi::vfr::Framerate(fpsNum, fpsDen);

	// Get timecodes and/or keyframes if provided
	agi::scoped_holder<VSMap *> clipinfo(vs.GetAPI()->createMap(), vs.GetAPI()->freeMap);
	if (clipinfo == nullptr)
		throw VapourSynthError("Couldn't create map");
	vs.GetScriptAPI()->getVariable(script, kf_key, clipinfo);
	vs.GetScriptAPI()->getVariable(script, tc_key, clipinfo);
	vs.GetScriptAPI()->getVariable(script, audio_key, clipinfo);

	int numkf = vs.GetAPI()->mapNumElements(clipinfo, kf_key);
	int numtc = vs.GetAPI()->mapNumElements(clipinfo, tc_key);

	int64_t audio = vs.GetAPI()->mapGetInt(clipinfo, audio_key, 0, &err1);
	if (!err1)
		has_audio = bool(audio);

	if (numkf > 0) {
		const int64_t *kfs = vs.GetAPI()->mapGetIntArray(clipinfo, kf_key, &err1);
		const char *kfs_path = vs.GetAPI()->mapGetData(clipinfo, kf_key, 0, &err2);
		if (err1 && err2)
			throw VapourSynthError("Error getting keyframes from returned VSMap");

		if (!err1) {
			keyframes.reserve(numkf);
			for (int i = 0; i < numkf; i++)
				keyframes.push_back(int(kfs[i]));
		} else {
			int kfs_path_size = vs.GetAPI()->mapGetDataSize(clipinfo, kf_key, 0, &err1);
			if (err1)
				throw VapourSynthError("Error getting size of keyframes path");

			try {
				keyframes = agi::keyframe::Load(config::path->Decode(std::string(kfs_path, size_t(kfs_path_size))));
			} catch (agi::Exception const& e) {
				LOG_E("vapoursynth/video/keyframes") << "Failed to open keyframes file specified by script: " << e.GetMessage();
			}
		}
	}

	if (numtc != -1 && vi->numFrames > 1) {
		const int64_t *tcs = vs.GetAPI()->mapGetIntArray(clipinfo, tc_key, &err1);
		const char *tcs_path = vs.GetAPI()->mapGetData(clipinfo, tc_key, 0, &err2);
		if (err1 && err2)
			throw VapourSynthError("Error getting timecodes from returned map");

		if (!err1) {
			if (numtc != vi->numFrames)
				throw VapourSynthError("Number of returned timecodes does not match number of frames");

			std::vector<int> timecodes;
			timecodes.reserve(numtc);
			for (int i = 0; i < numtc; i++)
				timecodes.push_back(int(tcs[i]));

			fps = agi::vfr::Framerate(timecodes);
		} else {
			int tcs_path_size = vs.GetAPI()->mapGetDataSize(clipinfo, tc_key, 0, &err1);
			if (err1)
				throw VapourSynthError("Error getting size of keyframes path");

			try {
				fps = agi::vfr::Framerate(config::path->Decode(std::string(tcs_path, size_t(tcs_path_size))));
			} catch (agi::Exception const& e) {
				// Throw an error here unlike with keyframes since the timecodes not being loaded might not be immediately noticeable
				throw VapourSynthError("Failed to open timecodes file specified by script: " + e.GetMessage());
			}
		}
	}

	// Find the first frame Of the video to get some info
	auto frame = GetVSFrame(source_node, 0);

	const VSMap *props = vs.GetAPI()->getFramePropertiesRO(frame);
	if (props == nullptr)
		throw VapourSynthError("Couldn't get frame properties");
	int64_t sarn = vs.GetAPI()->mapGetInt(props, "_SARNum", 0, &err1);
	int64_t sard = vs.GetAPI()->mapGetInt(props, "_SARDen", 0, &err2);
	if (!err1 && !err2) {
		dar = double(vi->width * sarn) / (vi->height * sard);
	}

	int video_cr_vs = vs.GetAPI()->mapGetInt(props, "_ColorRange", 0, &err1);
	switch (video_cr_vs) {
		case VSC_RANGE_FULL:
			video_cr = AGI_CR_JPEG;
		case VSC_RANGE_LIMITED:
			video_cr = AGI_CR_MPEG;
		default:
			video_cr = AGI_CR_UNSPECIFIED;
	}
	video_cs = vs.GetAPI()->mapGetInt(props, "_Matrix", 0, &err2);
	ColorMatrix::guess_colorspace(video_cs, video_cr, vi->width, vi->height);

	SetColorSpace(colormatrix);
}
catch (VapourSynthError const& err) {
	throw VideoOpenError(err.GetMessage());
}

void VapourSynthVideoProvider::SetColorSpace(std::string const& matrix) {
	if (vi->format.colorFamily != cfRGB || vi->format.bitsPerSample != 8) {
		if (matrix == colorspace && prepared_node != nullptr) {
			return;
		}

		agi::scoped_holder<VSNode *> intermediary(vs.GetAPI()->addNodeRef(source_node), vs.GetAPI()->freeNode);

		auto [force_cs, force_cr] = ColorMatrix::parse_colormatrix(matrix);
		if (force_cs != AGI_CS_UNSPECIFIED && force_cr != AGI_CR_UNSPECIFIED) {
			// Override the _Matrix and _Range frame props to force the color space
			VSPlugin *std = vs.GetAPI()->getPluginByID(VSH_STD_PLUGIN_ID, vs.GetScriptAPI()->getCore(script));
			if (std == nullptr)
				throw VapourSynthError("Couldn't find std plugin");

			agi::scoped_holder<VSMap *> args(vs.GetAPI()->createMap(), vs.GetAPI()->freeMap);
			if (args == nullptr)
				throw VapourSynthError("Failed to create argument map");

			vs.GetAPI()->mapSetNode(args, "clip", source_node, maAppend);
			vs.GetAPI()->mapSetInt(args, "_Matrix", force_cs, maAppend);
			vs.GetAPI()->mapSetInt(args, "_ColorRange", force_cr == AGI_CR_JPEG ? VSC_RANGE_FULL : VSC_RANGE_LIMITED, maAppend);

			VSMap *result = vs.GetAPI()->invoke(std, "SetFrameProps", args);
			const char *error = vs.GetAPI()->mapGetError(result);
			if (error) {
				throw VideoOpenError(agi::format("Failed set color space frame props: %s", error));
			}
			int err;
			intermediary = vs.GetAPI()->mapGetNode(result, "clip", 0, &err);
			if (err) {
				throw VideoOpenError("Failed to get SetFrameProps output node");
			}
		}

		// Convert to RGB24 format
		VSPlugin *resize = vs.GetAPI()->getPluginByID(VSH_RESIZE_PLUGIN_ID, vs.GetScriptAPI()->getCore(script));
		if (resize == nullptr)
			throw VapourSynthError("Couldn't find resize plugin");

		agi::scoped_holder<VSMap *> args(vs.GetAPI()->createMap(), vs.GetAPI()->freeMap);
		if (args == nullptr)
			throw VapourSynthError("Failed to create argument map");

		vs.GetAPI()->mapSetNode(args, "clip", intermediary, maAppend);
		vs.GetAPI()->mapSetInt(args, "format", pfRGB24, maAppend);

		// Set defaults for the colorspace parameters.
		// If the video node has frame props (like if the video is tagged with
		// some color space), these will override these arguments.
		vs.GetAPI()->mapSetInt(args, "matrix_in", video_cs, maAppend);
		vs.GetAPI()->mapSetInt(args, "range_in", video_cr == AGI_CR_JPEG, maAppend);
		vs.GetAPI()->mapSetInt(args, "chromaloc_in", VSC_CHROMA_LEFT, maAppend);

		VSMap *result = vs.GetAPI()->invoke(resize, "Bicubic", args);
		const char *error = vs.GetAPI()->mapGetError(result);
		if (error) {
			throw VideoOpenError(agi::format("Failed to convert to RGB24: %s", error));
		}
		int err;
		prepared_node = vs.GetAPI()->mapGetNode(result, "clip", 0, &err);
		if (err) {
			throw VideoOpenError("Failed to get resize output node");
		}

		// Finally, try to get the first frame again, so if the filter does crash, it happens before loading finishes
		GetVSFrame(prepared_node, 0);
	} else {
		prepared_node = vs.GetAPI()->addNodeRef(source_node);
	}
	colorspace = matrix;
}

agi::scoped_holder<const VSFrame *, void (*)(const VSFrame *) noexcept> VapourSynthVideoProvider::GetVSFrame(VSNode *node, int n) {
	char errorMsg[1024];
	const VSFrame *frame = vs.GetAPI()->getFrame(n, node, errorMsg, sizeof(errorMsg));
	if (frame == nullptr) {
		throw VapourSynthError(agi::format("Error getting frame: %s", errorMsg));
	}
	return agi::scoped_holder(frame, vs.GetAPI()->freeFrame);
}

void VapourSynthVideoProvider::GetFrame(int n, VideoFrame &out) {
	std::lock_guard<std::mutex> lock(vs.GetMutex());

	auto frame = GetVSFrame(prepared_node, n);

	const VSVideoFormat *format = vs.GetAPI()->getVideoFrameFormat(frame);
	if (format->colorFamily != cfRGB || format->numPlanes != 3 || format->bitsPerSample != 8 || format->subSamplingH != 0 || format->subSamplingW != 0) {
		throw VapourSynthError("Frame not in RGB24 format");
	}

	out.width = vs.GetAPI()->getFrameWidth(frame, 0);
	out.height = vs.GetAPI()->getFrameHeight(frame, 0);
	out.pitch = out.width * 4;
	out.flipped = false;

	out.data.resize(out.pitch * out.height);

	for (int p = 0; p < format->numPlanes; p++) {
		ptrdiff_t stride = vs.GetAPI()->getStride(frame, p);
		const uint8_t *readPtr = vs.GetAPI()->getReadPtr(frame, p);
		uint8_t *writePtr = &out.data[2 - p];
		int rows = vs.GetAPI()->getFrameHeight(frame, p);
		int cols = vs.GetAPI()->getFrameWidth(frame, p);

		for (int row = 0; row < rows; row++) {
			const uint8_t *rowPtr = readPtr;
			uint8_t *rowWritePtr = writePtr;
			for (int col = 0; col < cols; col++) {
				*rowWritePtr = *rowPtr++;
				rowWritePtr += 4;
			}
			readPtr += stride;
			writePtr += out.pitch;
		}
	}
}

}

namespace agi { class BackgroundRunner; }
std::unique_ptr<VideoProvider> CreateVapourSynthVideoProvider(agi::fs::path const& path, std::string const& colormatrix, agi::BackgroundRunner *br) {
	return agi::make_unique<VapourSynthVideoProvider>(path, colormatrix, br);
}
#endif // WITH_VAPOURSYNTH
