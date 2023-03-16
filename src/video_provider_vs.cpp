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
	VSScript *script = nullptr;
	VSNode *node = nullptr;
	const VSVideoInfo *vi = nullptr;

	double dar = 0;
	agi::vfr::Framerate fps;
	std::vector<int> keyframes;
	std::string colorspace;
	std::string real_colorspace;
	bool has_audio = false;

	const VSFrame *GetVSFrame(int n);
	void SetResizeArg(VSMap *args, const VSMap *props, const char *arg_name, const char *prop_name, int64_t deflt, int64_t unspecified = -1);

public:
	VapourSynthVideoProvider(agi::fs::path const& filename, std::string const& colormatrix, agi::BackgroundRunner *br);
	~VapourSynthVideoProvider();

	void GetFrame(int n, VideoFrame &frame) override;

	void SetColorSpace(std::string const& matrix) override { }

	int GetFrameCount() const override             { return vi->numFrames; }
	agi::vfr::Framerate GetFPS() const override    { return fps; }
	int GetWidth() const override                  { return vi->width; }
	int GetHeight() const override                 { return vi->height; }
	double GetDAR() const override                 { return dar; }
	std::vector<int> GetKeyFrames() const override { return keyframes; }
	std::string GetColorSpace() const override     { return GetRealColorSpace(); }
	std::string GetRealColorSpace() const override { return colorspace == "Unknown" ? "None" : colorspace; }
	bool HasAudio() const override                 { return has_audio; }
	bool WantsCaching() const override             { return true; }
	std::string GetDecoderName() const override    { return "VapourSynth"; }
	bool ShouldSetVideoProperties() const override { return colorspace != "Unknown"; }
};

std::string colormatrix_description(int colorFamily, int colorRange, int matrix) {
	if (colorFamily != cfYUV) {
		return "None";
	}
	// Assuming TV for unspecified
	std::string str = colorRange == VSC_RANGE_FULL ? "PC" : "TV";

	switch (matrix) {
		case VSC_MATRIX_RGB:
			return "None";
		case VSC_MATRIX_BT709:
			return str + ".709";
		case VSC_MATRIX_FCC:
			return str + ".FCC";
		case VSC_MATRIX_BT470_BG:
		case VSC_MATRIX_ST170_M:
			return str + ".601";
		case VSC_MATRIX_ST240_M:
			return str + ".240M";
		default:
			return "Unknown"; 	// Will return "None" in GetColorSpace
	}
}

// Adds an argument to the rescaler if the corresponding frameprop does not exist or is set as unspecified
void VapourSynthVideoProvider::SetResizeArg(VSMap *args, const VSMap *props, const char *arg_name, const char *prop_name, int64_t deflt, int64_t unspecified) {
	int err;
	int64_t result = vs.GetAPI()->mapGetInt(props, prop_name, 0, &err);
	if (err != 0 || result == unspecified) {
		result = deflt;
		if (!strcmp(arg_name, "range_in")) {
			result = result == VSC_RANGE_FULL ? 1 : 0;
		}
		vs.GetAPI()->mapSetInt(args, arg_name, result, maAppend);
	}
}

VapourSynthVideoProvider::VapourSynthVideoProvider(agi::fs::path const& filename, std::string const& colormatrix, agi::BackgroundRunner *br) try { try {
	std::lock_guard<std::mutex> lock(vs.GetMutex());

	VSCleanCache();

	int err1, err2;
	VSCore *core = vs.GetAPI()->createCore(0);
	if (core == nullptr) {
		throw VapourSynthError("Error creating core");
	}
	script = vs.GetScriptAPI()->createScript(core);
	if (script == nullptr) {
		vs.GetAPI()->freeCore(core);
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
	node = vs.GetScriptAPI()->getOutputNode(script, 0);
	if (node == nullptr)
		throw VapourSynthError("No output node set");

	if (vs.GetAPI()->getNodeType(node) != mtVideo) {
		throw VapourSynthError("Output node isn't a video node");
	}
	vi = vs.GetAPI()->getVideoInfo(node);
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
	VSMap *clipinfo = vs.GetAPI()->createMap();
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

	if (numtc != -1) {
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
	vs.GetAPI()->freeMap(clipinfo);

	// Find the first frame Of the video to get some info
	const VSFrame *frame = GetVSFrame(0);

	const VSMap *props = vs.GetAPI()->getFramePropertiesRO(frame);
	if (props == nullptr)
		throw VapourSynthError("Couldn't get frame properties");
	int64_t sarn = vs.GetAPI()->mapGetInt(props, "_SARNum", 0, &err1);
	int64_t sard = vs.GetAPI()->mapGetInt(props, "_SARDen", 0, &err2);
	if (!err1 && !err2) {
		dar = double(vi->width * sarn) / (vi->height * sard);
	}

	int64_t range = vs.GetAPI()->mapGetInt(props, "_ColorRange", 0, &err1);
	int64_t matrix = vs.GetAPI()->mapGetInt(props, "_Matrix", 0, &err2);
	colorspace = colormatrix_description(vi->format.colorFamily, err1 == 0 ? range : -1, err2 == 0 ? matrix : -1);

	vs.GetAPI()->freeFrame(frame);

	if (vi->format.colorFamily != cfRGB || vi->format.bitsPerSample != 8) {
		// Convert to RGB24 format
		VSPlugin *resize = vs.GetAPI()->getPluginByID(VSH_RESIZE_PLUGIN_ID, vs.GetScriptAPI()->getCore(script));
		if (resize == nullptr)
			throw VapourSynthError("Couldn't find resize plugin");

		VSMap *args = vs.GetAPI()->createMap();
		if (args == nullptr)
			throw VapourSynthError("Failed to create argument map");

		vs.GetAPI()->mapSetNode(args, "clip", node, maAppend);
		vs.GetAPI()->mapSetInt(args, "format", pfRGB24, maAppend);
		if (vi->format.colorFamily != cfGray)
			SetResizeArg(args, props, "matrix_in", "_Matrix", VSC_MATRIX_BT709, VSC_MATRIX_UNSPECIFIED);
		SetResizeArg(args, props, "transfer_in", "_Transfer", VSC_TRANSFER_BT709, VSC_TRANSFER_UNSPECIFIED);
		SetResizeArg(args, props, "primaries_in", "_Primaries", VSC_PRIMARIES_BT709, VSC_PRIMARIES_UNSPECIFIED);
		SetResizeArg(args, props, "range_in", "_ColorRange", VSC_RANGE_LIMITED);
		SetResizeArg(args, props, "chromaloc_in", "_ChromaLocation", VSC_CHROMA_LEFT);

		VSMap *result = vs.GetAPI()->invoke(resize, "Bicubic", args);
		vs.GetAPI()->freeMap(args);
		const char *error = vs.GetAPI()->mapGetError(result);
		if (error) {
			vs.GetAPI()->freeMap(result);
			vs.GetAPI()->freeNode(node);
			vs.GetScriptAPI()->freeScript(script);
			throw VideoProviderError(agi::format("Failed to convert to RGB24: %s", error));
		}
		int err;
		vs.GetAPI()->freeNode(node);
		node = vs.GetAPI()->mapGetNode(result, "clip", 0, &err);
		vs.GetAPI()->freeMap(result);
		if (err) {
			vs.GetScriptAPI()->freeScript(script);
			throw VideoProviderError("Failed to get resize output node");
		}

		// Finally, try to get the first frame again, so if the filter does crash, it happens before loading finishes
		const VSFrame *rgbframe = GetVSFrame(0);
		vs.GetAPI()->freeFrame(rgbframe);
	}
} catch (VapourSynthError const& err) {     // for try inside of function. We need both here since we need to catch errors from the VapourSynthWrap constructor.
	if (node != nullptr)
		vs.GetAPI()->freeNode(node);
	if (script != nullptr)
		vs.GetScriptAPI()->freeScript(script);
	throw err;
}
}
catch (VapourSynthError const& err) {    // for the entire constructor
	throw VideoProviderError(agi::format("VapourSynth error: %s", err.GetMessage()));
}

const VSFrame *VapourSynthVideoProvider::GetVSFrame(int n) {
	char errorMsg[1024];
	const VSFrame *frame = vs.GetAPI()->getFrame(n, node, errorMsg, sizeof(errorMsg));
	if (frame == nullptr) {
		throw VapourSynthError(agi::format("Error getting frame: %s", errorMsg));
	}
	return frame;
}

void VapourSynthVideoProvider::GetFrame(int n, VideoFrame &out) {
	std::lock_guard<std::mutex> lock(vs.GetMutex());

	const VSFrame *frame = GetVSFrame(n);

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

	vs.GetAPI()->freeFrame(frame);
}

VapourSynthVideoProvider::~VapourSynthVideoProvider() {
	if (node != nullptr) {
		vs.GetAPI()->freeNode(node);
	}
	if (script != nullptr) {
		vs.GetScriptAPI()->freeScript(script);
	}
}
}

namespace agi { class BackgroundRunner; }
std::unique_ptr<VideoProvider> CreateVapourSynthVideoProvider(agi::fs::path const& path, std::string const& colormatrix, agi::BackgroundRunner *br) {
	return agi::make_unique<VapourSynthVideoProvider>(path, colormatrix, br);
}
#endif // WITH_VAPOURSYNTH
