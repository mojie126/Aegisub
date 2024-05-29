// Copyright (c) 2005-2010, Niels Martin Hansen
// Copyright (c) 2005-2010, Rodrigo Braz Monteiro
// Copyright (c) 2010, Amar Takhar
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//	 this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//	 this list of conditions and the following disclaimer in the documentation
//	 and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//	 may be used to endorse or promote products derived from this software
//	 without specific prior written permission.
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

#include "dialog_progress.h"

#include "command.h"

#include "../ass_dialogue.h"
#include "../async_video_provider.h"
#include "../compat.h"
#include "../dialog_detached_video.h"
#include "../dialog_manager.h"
#include "../dialogs.h"
#include "../format.h"
#include "../frame_main.h"
#include "../include/aegisub/context.h"
#include "../include/aegisub/subtitles_provider.h"
#include "../libresrc/libresrc.h"
#include "../options.h"
#include "../project.h"
#include "../selection_controller.h"
#include "../utils.h"
#include "../video_controller.h"
#include "../video_display.h"
#include "../video_frame.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/fs.h>
#include <libaegisub/path.h>
#include <libaegisub/make_unique.h>
#include <libaegisub/util.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/filesystem.hpp>

#include <wx/dir.h>
#include <wx/msgdlg.h>
#include <wx/textdlg.h>

extern "C" {
	#include <libavformat/avformat.h>
	#include <libavcodec/avcodec.h>
	#include <libavfilter/avfilter.h>
	#include <libavfilter/buffersink.h>
	#include <libavfilter/buffersrc.h>
	#include <libavutil/imgutils.h>
	#include <libavutil/time.h>
	#include <libavutil/opt.h>
}

namespace {
	using cmd::Command;

	struct validator_video_loaded : public Command {
		CMD_TYPE(COMMAND_VALIDATE)

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}
	};

	struct validator_video_attached : public Command {
		CMD_TYPE(COMMAND_VALIDATE)

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider() && !c->dialog->Get<DialogDetachedVideo>();
		}
	};

	struct video_aspect_cinematic final : public validator_video_loaded {
		CMD_NAME("video/aspect/cinematic")
		STR_MENU("&Cinematic (2.35)")
		STR_DISP("Cinematic (2.35)")
		STR_HELP("Force video to 2.35 aspect ratio")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool IsActive(const agi::Context *c) override {
			return c->videoController->GetAspectRatioType() == AspectRatio::Cinematic;
		}

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			c->videoController->SetAspectRatio(AspectRatio::Cinematic);
			c->frame->SetDisplayMode(1, -1);
		}
	};

	struct video_aspect_custom final : public validator_video_loaded {
		CMD_NAME("video/aspect/custom")
		STR_MENU("C&ustom...")
		STR_DISP("Custom")
		STR_HELP("Force video to a custom aspect ratio")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool IsActive(const agi::Context *c) override {
			return c->videoController->GetAspectRatioType() == AspectRatio::Custom;
		}

		void operator()(agi::Context *c) override {
			c->videoController->Stop();

			std::string value = from_wx(
				wxGetTextFromUser(
					_("Enter aspect ratio in either:\n  decimal (e.g. 2.35)\n  fractional (e.g. 16:9)\n  specific resolution (e.g. 853x480)"),
					_("Enter aspect ratio"),
					std::to_wstring(c->videoController->GetAspectRatioValue())
				)
			);
			if (value.empty()) return;

			double numval = 0;
			if (agi::util::try_parse(value, &numval)) {
				//Nothing to see here, move along
			} else {
				std::vector<std::string> chunks;
				split(chunks, value, boost::is_any_of(":/xX"));
				if (chunks.size() == 2) {
					double num, den;
					if (agi::util::try_parse(chunks[0], &num) && agi::util::try_parse(chunks[1], &den))
						numval = num / den;
				}
			}

			if (numval < 0.5 || numval > 5.0)
				wxMessageBox(_("Invalid value! Aspect ratio must be between 0.5 and 5.0."),_("Invalid Aspect Ratio"),wxOK | wxICON_ERROR | wxCENTER);
			else {
				c->videoController->SetAspectRatio(numval);
				c->frame->SetDisplayMode(1, -1);
			}
		}
	};

	struct video_aspect_default final : public validator_video_loaded {
		CMD_NAME("video/aspect/default")
		STR_MENU("&Default")
		STR_DISP("Default")
		STR_HELP("Use video's original aspect ratio")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool IsActive(const agi::Context *c) override {
			return c->videoController->GetAspectRatioType() == AspectRatio::Default;
		}

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			c->videoController->SetAspectRatio(AspectRatio::Default);
			c->frame->SetDisplayMode(1, -1);
		}
	};

	struct video_aspect_full final : public validator_video_loaded {
		CMD_NAME("video/aspect/full")
		STR_MENU("&Fullscreen (4:3)")
		STR_DISP("Fullscreen (4:3)")
		STR_HELP("Force video to 4:3 aspect ratio")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool IsActive(const agi::Context *c) override {
			return c->videoController->GetAspectRatioType() == AspectRatio::Fullscreen;
		}

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			c->videoController->SetAspectRatio(AspectRatio::Fullscreen);
			c->frame->SetDisplayMode(1, -1);
		}
	};

	struct video_aspect_wide final : public validator_video_loaded {
		CMD_NAME("video/aspect/wide")
		STR_MENU("&Widescreen (16:9)")
		STR_DISP("Widescreen (16:9)")
		STR_HELP("Force video to 16:9 aspect ratio")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool IsActive(const agi::Context *c) override {
			return c->videoController->GetAspectRatioType() == AspectRatio::Widescreen;
		}

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			c->videoController->SetAspectRatio(AspectRatio::Widescreen);
			c->frame->SetDisplayMode(1, -1);
		}
	};

	struct video_close final : public validator_video_loaded {
		CMD_NAME("video/close")
		CMD_ICON(close_video_menu)
		STR_MENU("&Close Video")
		STR_DISP("Close Video")
		STR_HELP("Close the currently open video file")

		void operator()(agi::Context *c) override {
			c->project->CloseVideo();
		}
	};

	struct video_copy_coordinates final : public validator_video_loaded {
		CMD_NAME("video/copy_coordinates")
		STR_MENU("Copy coordinates to Clipboard")
		STR_DISP("Copy coordinates to Clipboard")
		STR_HELP("Copy the current coordinates of the mouse over the video to the clipboard")

		void operator()(agi::Context *c) override {
			SetClipboard(c->videoDisplay->GetMousePosition().Str());
		}
	};

	struct video_cycle_subtitles_provider final : public cmd::Command {
		CMD_NAME("video/subtitles_provider/cycle")
		STR_MENU("Cycle active subtitles provider")
		STR_DISP("Cycle active subtitles provider")
		STR_HELP("Cycle through the available subtitles providers")

		void operator()(agi::Context *c) override {
			auto providers = SubtitlesProviderFactory::GetClasses();
			if (providers.empty()) return;

			auto it = find(begin(providers), end(providers), OPT_GET("Subtitle/Provider")->GetString());
			if (it != end(providers)) ++it;
			if (it == end(providers)) it = begin(providers);

			OPT_SET("Subtitle/Provider")->SetString(*it);
			c->frame->StatusTimeout(fmt_tl("Subtitles provider set to %s", *it), 5000);
		}
	};

	struct video_reload_subtitles_provider final : public cmd::Command {
		CMD_NAME("video/subtitles_provider/reload")
		STR_MENU("Reload active subtitles provider")
		STR_DISP("Reload active subtitles provider")
		STR_HELP("Reloads the current subtitles provider")

		void operator()(agi::Context *c) override {
			auto providers = SubtitlesProviderFactory::GetClasses();
			if (providers.empty()) return;

			auto it = find(begin(providers), end(providers), OPT_GET("Subtitle/Provider")->GetString());

			OPT_SET("Subtitle/Provider")->SetString(*it);
			c->frame->StatusTimeout(fmt_tl("Subtitles provider set to %s", *it), 5000);
		}
	};

	struct video_detach final : public validator_video_loaded {
		CMD_NAME("video/detach")
		CMD_ICON(detach_video_menu)
		STR_MENU("&Detach Video")
		STR_DISP("Detach Video")
		STR_HELP("Detach the video display from the main window, displaying it in a separate Window")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_TOGGLE)

		bool IsActive(const agi::Context *c) override {
			return !!c->dialog->Get<DialogDetachedVideo>();
		}

		void operator()(agi::Context *c) override {
			if (DialogDetachedVideo *d = c->dialog->Get<DialogDetachedVideo>())
				d->Close();
			else
				c->dialog->Show<DialogDetachedVideo>(c);
		}
	};

	struct video_details final : public validator_video_loaded {
		CMD_NAME("video/details")
		CMD_ICON(show_video_details_menu)
		STR_MENU("Show &Video Details")
		STR_DISP("Show Video Details")
		STR_HELP("Show video details")

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			ShowVideoDetailsDialog(c);
		}
	};

	struct video_focus_seek final : public validator_video_loaded {
		CMD_NAME("video/focus_seek")
		STR_MENU("Toggle video slider focus")
		STR_DISP("Toggle video slider focus")
		STR_HELP("Toggle focus between the video slider and the previous thing to have focus")

		void operator()(agi::Context *c) override {
			wxWindow *curFocus = wxWindow::FindFocus();
			if (curFocus == c->videoSlider) {
				if (c->previousFocus) c->previousFocus->SetFocus();
			} else {
				c->previousFocus = curFocus;
				c->videoSlider->SetFocus();
			}
		}
	};

	wxImage get_image(agi::Context *c, bool raw, bool subsonly = false) {
		auto frame = c->videoController->GetFrameN();
		if (subsonly) {
			return GetImageWithAlpha(c->project->VideoProvider()->GetSubtitles(c->project->Timecodes().TimeAtFrame(frame)));
		} else {
			return GetImage(*c->project->VideoProvider()->GetFrame(frame, c->project->Timecodes().TimeAtFrame(frame), raw));
		}
	}

	struct video_frame_copy final : public validator_video_loaded {
		CMD_NAME("video/frame/copy")
		STR_MENU("Copy image to Clipboard")
		STR_DISP("Copy image to Clipboard")
		STR_HELP("Copy the currently displayed frame to the clipboard")

		void operator()(agi::Context *c) override {
			SetClipboard(wxBitmap(get_image(c, false), 24));
		}
	};

	struct video_frame_copy_raw final : public validator_video_loaded {
		CMD_NAME("video/frame/copy/raw")
		STR_MENU("Copy image to Clipboard (no subtitles)")
		STR_DISP("Copy image to Clipboard (no subtitles)")
		STR_HELP("Copy the currently displayed frame to the clipboard, without the subtitles")

		void operator()(agi::Context *c) override {
			SetClipboard(wxBitmap(get_image(c, true), 24));
		}
	};

	struct video_frame_copy_subs final : public validator_video_loaded {
		CMD_NAME("video/frame/copy/subs")
		STR_MENU("Copy image to Clipboard (only subtitles)")
		STR_DISP("Copy image to Clipboard (only subtitles)")
		STR_HELP("Copy the currently displayed subtitles to the clipboard, with transparent background")

		void operator()(agi::Context *c) override {
			SetClipboard(wxBitmap(get_image(c, false, true), 32));
		}
	};

	struct video_frame_next final : public validator_video_loaded {
		CMD_NAME("video/frame/next")
		STR_MENU("Next Frame")
		STR_DISP("Next Frame")
		STR_HELP("Seek to the next frame")

		void operator()(agi::Context *c) override {
			c->videoController->NextFrame();
		}
	};

	struct video_frame_next_boundary final : public validator_video_loaded {
		CMD_NAME("video/frame/next/boundary")
		STR_MENU("Next Boundary")
		STR_DISP("Next Boundary")
		STR_HELP("Seek to the next beginning or end of a subtitle")

		void operator()(agi::Context *c) override {
			AssDialogue *active_line = c->selectionController->GetActiveLine();
			if (!active_line) return;

			int target = c->videoController->FrameAtTime(active_line->Start, agi::vfr::START);
			if (target > c->videoController->GetFrameN()) {
				c->videoController->JumpToFrame(target);
				return;
			}

			target = c->videoController->FrameAtTime(active_line->End, agi::vfr::END);
			if (target > c->videoController->GetFrameN()) {
				c->videoController->JumpToFrame(target);
				return;
			}

			c->selectionController->NextLine();
			AssDialogue *new_line = c->selectionController->GetActiveLine();
			if (new_line != active_line)
				c->videoController->JumpToTime(new_line->Start);
		}
	};

	struct video_frame_next_keyframe final : public validator_video_loaded {
		CMD_NAME("video/frame/next/keyframe")
		STR_MENU("Next Keyframe")
		STR_DISP("Next Keyframe")
		STR_HELP("Seek to the next keyframe")

		void operator()(agi::Context *c) override {
			auto const &kf = c->project->Keyframes();
			auto pos = lower_bound(kf.begin(), kf.end(), c->videoController->GetFrameN() + 1);

			c->videoController->JumpToFrame(pos == kf.end() ? c->project->VideoProvider()->GetFrameCount() - 1 : *pos);
		}
	};

	struct video_frame_next_large final : public validator_video_loaded {
		CMD_NAME("video/frame/next/large")
		STR_MENU("Fast jump forward")
		STR_DISP("Fast jump forward")
		STR_HELP("Fast jump forward")

		void operator()(agi::Context *c) override {
			c->videoController->JumpToFrame(
				c->videoController->GetFrameN() +
				OPT_GET("Video/Slider/Fast Jump Step")->GetInt()
			);
		}
	};

	struct video_frame_prev final : public validator_video_loaded {
		CMD_NAME("video/frame/prev")
		STR_MENU("Previous Frame")
		STR_DISP("Previous Frame")
		STR_HELP("Seek to the previous frame")

		void operator()(agi::Context *c) override {
			c->videoController->PrevFrame();
		}
	};

	struct video_frame_prev_boundary final : public validator_video_loaded {
		CMD_NAME("video/frame/prev/boundary")
		STR_MENU("Previous Boundary")
		STR_DISP("Previous Boundary")
		STR_HELP("Seek to the previous beginning or end of a subtitle")

		void operator()(agi::Context *c) override {
			AssDialogue *active_line = c->selectionController->GetActiveLine();
			if (!active_line) return;

			int target = c->videoController->FrameAtTime(active_line->End, agi::vfr::END);
			if (target < c->videoController->GetFrameN()) {
				c->videoController->JumpToFrame(target);
				return;
			}

			target = c->videoController->FrameAtTime(active_line->Start, agi::vfr::START);
			if (target < c->videoController->GetFrameN()) {
				c->videoController->JumpToFrame(target);
				return;
			}

			c->selectionController->PrevLine();
			AssDialogue *new_line = c->selectionController->GetActiveLine();
			if (new_line != active_line)
				c->videoController->JumpToTime(new_line->End, agi::vfr::END);
		}
	};

	struct video_frame_prev_keyframe final : public validator_video_loaded {
		CMD_NAME("video/frame/prev/keyframe")
		STR_MENU("Previous Keyframe")
		STR_DISP("Previous Keyframe")
		STR_HELP("Seek to the previous keyframe")

		void operator()(agi::Context *c) override {
			auto const &kf = c->project->Keyframes();
			if (kf.empty()) {
				c->videoController->JumpToFrame(0);
				return;
			}

			auto pos = lower_bound(kf.begin(), kf.end(), c->videoController->GetFrameN());

			if (pos != kf.begin())
				--pos;

			c->videoController->JumpToFrame(*pos);
		}
	};

	struct video_frame_prev_large final : public validator_video_loaded {
		CMD_NAME("video/frame/prev/large")
		STR_MENU("Fast jump backwards")
		STR_DISP("Fast jump backwards")
		STR_HELP("Fast jump backwards")

		void operator()(agi::Context *c) override {
			c->videoController->JumpToFrame(
				c->videoController->GetFrameN() -
				OPT_GET("Video/Slider/Fast Jump Step")->GetInt()
			);
		}
	};

	static void save_snapshot(agi::Context *c, bool raw, bool subsonly = false) {
		auto option = OPT_GET("Path/Screenshot")->GetString();
		agi::fs::path basepath;

		auto videoname = c->project->VideoName();
		bool is_dummy = boost::starts_with(videoname.string(), "?dummy");

		// Is it a path specifier and not an actual fixed path?
		if (option[0] == '?') {
			// If dummy video is loaded, we can't save to the video location
			if (boost::starts_with(option, "?video") && is_dummy) {
				// So try the script location instead
				option = "?script";
			}
			// Find out where the ?specifier points to
			basepath = c->path->Decode(option);
			// If where ever that is isn't defined, we can't save there
			if ((basepath == "\\") || (basepath == "/")) {
				// So save to the current user's home dir instead
				basepath = std::string(wxGetHomeDir());
			}
		}
		// Actual fixed (possibly relative) path, decode it
		else
			basepath = c->path->MakeAbsolute(option, "?user/");

		basepath /= is_dummy ? "dummy" : videoname.stem();

		// Get full path
		int session_shot_count = 1;
		std::string path;
		do {
			path = agi::format("%s_%03d_%d.png", basepath.string(), session_shot_count++, c->videoController->GetFrameN());
		} while (agi::fs::FileExists(path));

		get_image(c, raw, subsonly).SaveFile(to_wx(path), wxBITMAP_TYPE_PNG);
	}

	struct video_frame_save final : public validator_video_loaded {
		CMD_NAME("video/frame/save")
		STR_MENU("Save PNG snapshot")
		STR_DISP("Save PNG snapshot")
		STR_HELP("Save the currently displayed frame to a PNG file in the video's directory")

		void operator()(agi::Context *c) override {
			save_snapshot(c, false);
		}
	};

	struct video_frame_save_raw final : public validator_video_loaded {
		CMD_NAME("video/frame/save/raw")
		STR_MENU("Save PNG snapshot (no subtitles)")
		STR_DISP("Save PNG snapshot (no subtitles)")
		STR_HELP("Save the currently displayed frame without the subtitles to a PNG file in the video's directory")

		void operator()(agi::Context *c) override {
			save_snapshot(c, true);
		}
	};

	struct video_frame_save_subs final : public validator_video_loaded {
		CMD_NAME("video/frame/save/subs")
		STR_MENU("Save PNG snapshot (only subtitles)")
		STR_DISP("Save PNG snapshot (only subtitles)")
		STR_HELP("Save the currently displayed subtitles with transparent background to a PNG file in the video's directory")

		void operator()(agi::Context *c) override {
			save_snapshot(c, false, true);
		}
	};

	bool extract_video_segment(
		const agi::Context *c, agi::ProgressSink *ps, const char *input_filename, const char *output_filename,
		long start_frame, long end_frame,
		const int start_time, const int end_time, const bool output_images, const char *img_path
	) {
		AVFormatContext *input_format_context = nullptr;
		AVCodecContext *decoder_context = nullptr;
		AVCodecContext *jpgCodecContext = nullptr;
		const AVStream *input_stream = nullptr;
		AVFilterContext *buffersink_ctx = nullptr;
		AVFilterContext *buffersrc_ctx = nullptr;
		AVFrame *frame = av_frame_alloc();
		AVPacket *jpgPacket = av_packet_alloc();
		AVPacket *packet = av_packet_alloc();
		AVFilterGraph *filter_graph = avfilter_graph_alloc();
		// av_log_set_level(AV_LOG_DEBUG);

		if (!frame) {
			std::cerr << "Could not allocate frame." << std::endl;
			return false;
		}

		// Open input file and find stream info
		if (avformat_open_input(&input_format_context, input_filename, nullptr, nullptr) < 0) {
			std::cerr << "Could not open input file." << std::endl;
			return false;
		}

		if (avformat_find_stream_info(input_format_context, nullptr) < 0) {
			std::cerr << "Could not find stream info." << std::endl;
			return false;
		}

		// Find the video stream
		const int video_stream_index = av_find_best_stream(input_format_context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
		if (video_stream_index < 0) {
			std::cerr << "Could not find video stream in the input file." << std::endl;
			return false;
		}

		input_stream = input_format_context->streams[video_stream_index];
		const AVCodec *decoder = avcodec_find_decoder(input_stream->codecpar->codec_id);
		if (!decoder) {
			std::cerr << "Could not find decoder." << std::endl;
			return false;
		}

		// Initialize the decoder context
		decoder_context = avcodec_alloc_context3(decoder);
		if (!decoder_context) {
			std::cerr << "Could not allocate decodec context." << std::endl;
			return false;
		}
		if (avcodec_parameters_to_context(decoder_context, input_stream->codecpar) < 0) {
			std::cerr << "Could not copy codec parameters." << std::endl;
			return false;
		}

		if (avcodec_open2(decoder_context, decoder, nullptr) < 0) {
			std::cerr << "Could not open decoder." << std::endl;
			return false;
		}

		const AVCodec *jpgCodec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
		jpgCodecContext = avcodec_alloc_context3(jpgCodec);
		if (!jpgCodecContext) {
			std::cerr << "Could not allocate jpg codec context." << std::endl;
			return false;
		}

		const auto video_provider = OPT_GET("Video/Provider")->GetString();
		int padding = 0;
		if (video_provider == "FFmpegSource") {
			const auto hw_name = OPT_GET("Provider/Video/FFmpegSource/HW hw_name")->GetString();
			if (hw_name == "none") {
				padding = OPT_GET("Provider/Video/FFmpegSource/ABB")->GetInt();
			}
		} else if (video_provider == "VapourSynth") {
			padding = OPT_GET("Provider/Video/VapourSynth/ABB")->GetInt();
		}

		// 配置过滤器
		if (!filter_graph) {
			std::cerr << "Could not allocate filter." << std::endl;
			return false;
		}

		// 创建 buffer 滤镜，用于作为输入
		const AVFilter *buffersrc = avfilter_get_by_name("buffer");
		const AVFilter *buffersink = avfilter_get_by_name("buffersink");
		char args[512];
		snprintf(
			args, sizeof(args),
			"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
			decoder_context->width, decoder_context->height, decoder_context->pix_fmt,
			input_stream->time_base.num, input_stream->time_base.den,
			decoder_context->sample_aspect_ratio.num, decoder_context->sample_aspect_ratio.den
		);

		// 创建 buffer 滤镜
		if (avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", args, nullptr, filter_graph) < 0) {
			std::cerr << "Failed to create buffer filter." << std::endl;
			return false;
		}

		// 创建 buffer sink 滤镜，用于作为输出
		if (avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out", nullptr, nullptr, filter_graph) < 0) {
			std::cerr << "Failed to create buffer sink filter." << std::endl;
			return false;
		}

		AVFilterInOut *outputs = avfilter_inout_alloc();
		AVFilterInOut *inputs = avfilter_inout_alloc();

		outputs->name = av_strdup("in");
		outputs->filter_ctx = buffersrc_ctx;
		outputs->pad_idx = 0;
		outputs->next = nullptr;

		inputs->name = av_strdup("out");
		inputs->filter_ctx = buffersink_ctx;
		inputs->pad_idx = 0;
		inputs->next = nullptr;

		char filter_spec[512];
		if (padding != 0) {
			jpgCodecContext->height = decoder_context->height + padding * 2;
			snprintf(
				filter_spec, sizeof(filter_spec), "format=yuv420p, pad=width=%d:height=%d:x=0:y=%d:color=black",
				decoder_context->width, decoder_context->height + padding * 2, padding
			);
		} else {
			jpgCodecContext->height = decoder_context->height;
			snprintf(
				filter_spec, sizeof(filter_spec), "format=yuv420p"
			);
		}

		if (avfilter_graph_parse_ptr(filter_graph, filter_spec, &inputs, &outputs, nullptr) < 0) {
			std::cerr << "Could not parse filter graph." << std::endl;
			return false;
		}

		// 配置滤镜图表
		if (avfilter_graph_config(filter_graph, nullptr) < 0) {
			std::cerr << "Failed to configure filter." << std::endl;
			return false;
		}

		avfilter_inout_free(&inputs);
		avfilter_inout_free(&outputs);

		jpgCodecContext->pix_fmt = AV_PIX_FMT_YUVJ420P;
		jpgCodecContext->width = decoder_context->width;
		jpgCodecContext->sample_aspect_ratio = decoder_context->sample_aspect_ratio;
		jpgCodecContext->time_base = input_stream->time_base;
		jpgCodecContext->qmin = jpgCodecContext->qmax = 1;
		if (avcodec_open2(jpgCodecContext, jpgCodec, nullptr) < 0) {
			std::cerr << "Could not open mjpeg encoder." << std::endl;
			return false;
		}

		int current_frame = 1;
		std::string output_path;
		auto clip_export_path = OPT_GET("Path/ClipExport")->GetString();
		clip_export_path = wxString(clip_export_path.c_str(), wxConvUTF8).ToStdString();
		if (clip_export_path.empty()) {
			output_path = agi::wxformat("%s [%ld-%ld]", output_filename, start_frame, end_frame);
			_mkdir(output_path.c_str());
		} else {
			output_path = clip_export_path;
			wxString filename;
			const wxDir dir(output_path);
			bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);
			while (cont) {
				const std::string _tmp_file{output_path + wxFileName::GetPathSeparator() + filename};
				wxRemoveFile(_tmp_file);
				cont = dir.GetNext(&filename);
			}
		}

		// Seek to the start time in milliseconds
		// const auto start_pts = static_cast<int64_t>(floor(start_time / (av_q2d(input_stream->time_base) * AV_TIME_BASE)));
		// const auto end_pts = static_cast<int64_t>(ceil(end_time / (av_q2d(input_stream->time_base) * AV_TIME_BASE)));
		const int64_t start_pts = av_rescale_q(start_time, input_stream->time_base, input_stream->time_base);
		const int64_t end_pts = av_rescale_q(end_time, input_stream->time_base, input_stream->time_base);
		if (avformat_seek_file(input_format_context, video_stream_index, INT64_MIN, start_pts, INT64_MAX, 0) < 0) {
			std::cerr << "Error seeking to the specified start time." << std::endl;
			return false;
		}

		// Flush decoder after seeking
		avcodec_flush_buffers(decoder_context);
		av_packet_unref(packet);
		bool seeking = true;
		int duration_frame = end_frame - start_frame + 1;

		// Read frames from the file
		while (av_read_frame(input_format_context, packet) >= 0) {
			if (packet->stream_index == video_stream_index) {
				if (seeking) {
					// 丢弃非关键帧，确保从关键帧开始解码以避免花屏
					if (!(packet->flags & AV_PKT_FLAG_KEY)) {
						av_packet_unref(packet);
						continue;
					}
					if (packet->pts >= start_pts) {
						seeking = false;
					} else {
						av_packet_unref(packet);
						continue;
					}
				}

				if (avcodec_send_packet(decoder_context, packet) < 0) {
					std::cerr << "Error sending a packet for decoding." << std::endl;
					break;
				}

				if (avcodec_receive_frame(decoder_context, frame) >= 0) {
					// 向滤镜添加帧
					if (av_buffersrc_add_frame(buffersrc_ctx, frame) < 0) {
						std::cerr << "Failed to add frame to filter graph." << std::endl;
						break;
					}
					// 取出经过滤镜的帧
					if (av_buffersink_get_frame(buffersink_ctx, frame) < 0) {
						std::cerr << "Failed to get frame from filter graph." << std::endl;
						break;
					}

					std::string image_filename{output_path + "/" + agi::wxformat(std::string(img_path) + "_[%ld-%ld]_%05d.jpg", getStartFrame(), getEndFrame(), current_frame)};

					int ret = avcodec_send_frame(jpgCodecContext, frame);
					if (ret < 0) {
						std::cerr << "Error encoding jpg frame: " << ret << std::endl;
						break;
					}
					ret = avcodec_receive_packet(jpgCodecContext, jpgPacket);
					if (ret >= 0) {
						FILE *file = fopen(image_filename.c_str(), "wb");
						if (file != nullptr) {
							fwrite(jpgPacket->data, 1, jpgPacket->size, file);
							fclose(file);
						}
						av_packet_unref(jpgPacket);
					}
					current_frame++;
					ps->SetMessage(std::string(agi::wxformat(_("Exporting video clips, frame: [%ld ~ %ld], total: %d, please later"), start_frame, end_frame, duration_frame).mb_str()));
					ps->SetProgress(current_frame, duration_frame);
					if (ps->IsCancelled()) break;
					if (current_frame > duration_frame) break;
				}
			}
			av_packet_unref(packet);
		}

		// Clean up
		av_packet_free(&jpgPacket);
		av_packet_free(&packet);
		avcodec_free_context(&decoder_context);
		avcodec_free_context(&jpgCodecContext);
		avformat_close_input(&input_format_context);
		av_frame_free(&frame);
		avfilter_graph_free(&filter_graph);
		if (ps->IsCancelled()) return false;
		return true;
	}

	void export_clip(agi::Context *c) {
		auto option = OPT_GET("Path/Screenshot")->GetString();
		agi::fs::path basepath;

		auto videoname = c->project->VideoName();
		bool is_dummy = boost::starts_with(videoname.string(), "?dummy");

		// Is it a path specifier and not an actual fixed path?
		if (option[0] == '?') {
			// If dummy video is loaded, we can't save to the video location
			if (boost::starts_with(option, "?video") && is_dummy) {
				// So try the script location instead
				option = "?script";
			}
			// Find out where the ?specifier points to
			basepath = c->path->Decode(option);
			// If where ever that is isn't defined, we can't save there
			if ((basepath == "\\") || (basepath == "/")) {
				// So save to the current user's home dir instead
				basepath = std::string(wxGetHomeDir());
			}
		}
		// Actual fixed (possibly relative) path, decode it
		else
			basepath = c->path->MakeAbsolute(option, "?user/");

		basepath /= is_dummy ? "dummy" : videoname.stem();

		// 设置帧到帧
		c->videoController->Stop();
		ShowJumpFrameToDialog(c);
		c->videoSlider->SetFocus();

		// Get full path
		std::string path;
		std::string img_path;
		if (!getOutputImg()) {
			path = agi::format("%s_[%ld-%ld].mp4", basepath.string(), getStartFrame(), getEndFrame());
		} else {
			auto clip_export_path = OPT_GET("Path/ClipExport")->GetString();
			clip_export_path = wxString(clip_export_path.c_str(), wxConvUTF8).ToStdString();
			if (clip_export_path.empty())
				path = agi::format("%s", basepath.string());
			else
				path = agi::format("%s", clip_export_path);
			const wxFileName fName(videoname.c_str());
			img_path = fName.GetName();
		}

		if (getOnOK()) {
			DialogProgress progress(nullptr, _("Export the clip"), wxEmptyString);
			try {
				progress.Run(
					[&](agi::ProgressSink *ps) {
						extract_video_segment(
							c, ps, c->project->VideoName().string().c_str(), path.c_str(),
							getStartFrame(), getEndFrame(),
							getStartTime(), getEndTime(), getOutputImg(), img_path.c_str()
						);
					}
				);
			} catch (agi::Exception &err) {
				std::cerr << err.GetMessage() << std::endl;
			}
		}
	}

	struct video_frame_export final : public validator_video_loaded {
		CMD_NAME("video/frame/save/export")
		STR_MENU("Export the clip")
		STR_DISP("Export the clip")
		STR_HELP("Export video clips from frame to frame at a specified time")

		void operator()(agi::Context *c) override {
			export_clip(c);
		}
	};

	struct video_jump final : public validator_video_loaded {
		CMD_NAME("video/jump")
		CMD_ICON(jumpto_button)
		STR_MENU("&Jump to...")
		STR_DISP("Jump to")
		STR_HELP("Jump to frame or time")

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			ShowJumpToDialog(c);
			c->videoSlider->SetFocus();
		}
	};

	struct video_jump_end final : public validator_video_loaded {
		CMD_NAME("video/jump/end")
		CMD_ICON(video_to_subend)
		STR_MENU("Jump Video to &End")
		STR_DISP("Jump Video to End")
		STR_HELP("Jump the video to the end frame of current subtitle")

		void operator()(agi::Context *c) override {
			if (auto active_line = c->selectionController->GetActiveLine())
				c->videoController->JumpToTime(active_line->End, agi::vfr::END);
		}
	};

	struct video_jump_start final : public validator_video_loaded {
		CMD_NAME("video/jump/start")
		CMD_ICON(video_to_substart)
		STR_MENU("Jump Video to &Start")
		STR_DISP("Jump Video to Start")
		STR_HELP("Jump the video to the start frame of current subtitle")

		void operator()(agi::Context *c) override {
			if (auto active_line = c->selectionController->GetActiveLine())
				c->videoController->JumpToTime(active_line->Start);
		}
	};

	struct video_open final : public Command {
		CMD_NAME("video/open")
		CMD_ICON(open_video_menu)
		STR_MENU("&Open Video...")
		STR_DISP("Open Video")
		STR_HELP("Open a video file")

		void operator()(agi::Context *c) override {
			auto str = from_wx(
				_("Video Formats") + " (*.asf,*.avi,*.avs,*.d2v,*.h264,*.hevc,*.m2ts,*.m4v,*.mkv,*.mov,*.mp4,*.mpeg,*.mpg,*.ogm,*.webm,*.wmv,*.ts,*.vpy,*.y4m,*.yuv)|*.asf;*.avi;*.avs;*.d2v;*.h264;*.hevc;*.m2ts;*.m4v;*.mkv;*.mov;*.mp4;*.mpeg;*.mpg;*.ogm;*.webm;*.wmv;*.ts;*.vpy;*.y4m;*.yuv|"
				+ _("All Files") + " (*.*)|*.*"
			);
			auto filename = OpenFileSelector(_("Open video file"), "Path/Last/Video", "", "", str, c->parent);
			if (!filename.empty())
				c->project->LoadVideo(filename);
		}
	};

	struct video_open_dummy final : public Command {
		CMD_NAME("video/open/dummy")
		CMD_ICON(use_dummy_video_menu)
		STR_MENU("&Use Dummy Video...")
		STR_DISP("Use Dummy Video")
		STR_HELP("Open a placeholder video clip with solid color")

		void operator()(agi::Context *c) override {
			std::string fn = CreateDummyVideo(c->parent);
			if (!fn.empty())
				c->project->LoadVideo(fn);
		}
	};

	struct video_reload final : public Command {
		CMD_NAME("video/reload")
		STR_MENU("Reload Video")
		STR_DISP("Reload Video")
		STR_HELP("Reload the current video file")

		void operator()(agi::Context *c) override {
			c->project->ReloadVideo();
		}
	};

	struct video_opt_autoscroll final : public Command {
		CMD_NAME("video/opt/autoscroll")
		CMD_ICON(toggle_video_autoscroll)
		STR_MENU("Toggle autoscroll of video")
		STR_DISP("Toggle autoscroll of video")
		STR_HELP("Toggle automatically seeking video to the start time of selected lines")
		CMD_TYPE(COMMAND_TOGGLE)

		bool IsActive(const agi::Context *) override {
			return OPT_GET("Video/Subtitle Sync")->GetBool();
		}

		void operator()(agi::Context *) override {
			OPT_SET("Video/Subtitle Sync")->SetBool(!OPT_GET("Video/Subtitle Sync")->GetBool());
		}
	};

	struct video_pan_reset final : public validator_video_loaded {
		CMD_NAME("video/pan_reset")
		STR_MENU("Reset Video Pan")
		STR_DISP("Reset Video Pan")
		STR_HELP("Reset the video pan to the original value")

		void operator()(agi::Context *c) override {
			c->videoDisplay->ResetPan();
		}
	};

	struct video_play final : public validator_video_loaded {
		CMD_NAME("video/play")
		CMD_ICON(button_play)
		STR_MENU("Play")
		STR_DISP("Play")
		STR_HELP("Play video starting on this position")

		void operator()(agi::Context *c) override {
			c->videoController->Play();
		}
	};

	struct video_play_line final : public validator_video_loaded {
		CMD_NAME("video/play/line")
		CMD_ICON(button_playline)
		STR_MENU("Play line")
		STR_DISP("Play line")
		STR_HELP("Play current line")

		void operator()(agi::Context *c) override {
			c->videoController->PlayLine();
		}
	};

	struct video_show_overscan final : public validator_video_loaded {
		CMD_NAME("video/show_overscan")
		STR_MENU("Show &Overscan Mask")
		STR_DISP("Show Overscan Mask")
		STR_HELP("Show a mask over the video, indicating areas that might get cropped off by overscan on televisions")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_TOGGLE)

		bool IsActive(const agi::Context *) override {
			return OPT_GET("Video/Overscan Mask")->GetBool();
		}

		void operator()(agi::Context *c) override {
			OPT_SET("Video/Overscan Mask")->SetBool(!OPT_GET("Video/Overscan Mask")->GetBool());
			c->videoDisplay->Render();
		}
	};

	class video_zoom_100 : public validator_video_attached {
	public:
		CMD_NAME("video/zoom/100")
		STR_MENU("&100%")
		STR_DISP("100%")
		STR_HELP("Set zoom to 100%")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool IsActive(const agi::Context *c) override {
			return c->videoDisplay->GetZoom() == 1.;
		}

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			c->videoDisplay->SetWindowZoom(1.);
		}
	};

	class video_stop : public validator_video_loaded {
	public:
		CMD_NAME("video/stop")
		CMD_ICON(button_pause)
		STR_MENU("Stop video")
		STR_DISP("Stop video")
		STR_HELP("Stop video playback")

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
		}
	};

	class video_zoom_200 : public validator_video_attached {
	public:
		CMD_NAME("video/zoom/200")
		STR_MENU("&200%")
		STR_DISP("200%")
		STR_HELP("Set zoom to 200%")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool IsActive(const agi::Context *c) override {
			return c->videoDisplay->GetZoom() == 2.;
		}

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			c->videoDisplay->SetWindowZoom(2.);
		}
	};

	class video_zoom_50 : public validator_video_attached {
	public:
		CMD_NAME("video/zoom/50")
		STR_MENU("&50%")
		STR_DISP("50%")
		STR_HELP("Set zoom to 50%")
		CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

		bool IsActive(const agi::Context *c) override {
			return c->videoDisplay->GetZoom() == .5;
		}

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			c->videoDisplay->SetWindowZoom(.5);
		}
	};

	struct video_zoom_in final : public validator_video_attached {
		CMD_NAME("video/zoom/in")
		CMD_ICON(zoom_in_button)
		STR_MENU("Zoom In")
		STR_DISP("Zoom In")
		STR_HELP("Zoom video in")

		void operator()(agi::Context *c) override {
			c->videoDisplay->SetWindowZoom(c->videoDisplay->GetZoom() + .125);
		}
	};

	struct video_zoom_out final : public validator_video_attached {
		CMD_NAME("video/zoom/out")
		CMD_ICON(zoom_out_button)
		STR_MENU("Zoom Out")
		STR_DISP("Zoom Out")
		STR_HELP("Zoom video out")

		void operator()(agi::Context *c) override {
			c->videoDisplay->SetWindowZoom(c->videoDisplay->GetZoom() - .125);
		}
	};
}

namespace cmd {
	void init_video() {
		reg(agi::make_unique<video_aspect_cinematic>());
		reg(agi::make_unique<video_aspect_custom>());
		reg(agi::make_unique<video_aspect_default>());
		reg(agi::make_unique<video_aspect_full>());
		reg(agi::make_unique<video_aspect_wide>());
		reg(agi::make_unique<video_close>());
		reg(agi::make_unique<video_copy_coordinates>());
		reg(agi::make_unique<video_cycle_subtitles_provider>());
		reg(agi::make_unique<video_reload_subtitles_provider>());
		reg(agi::make_unique<video_detach>());
		reg(agi::make_unique<video_details>());
		reg(agi::make_unique<video_focus_seek>());
		reg(agi::make_unique<video_frame_copy>());
		reg(agi::make_unique<video_frame_copy_raw>());
		reg(agi::make_unique<video_frame_copy_subs>());
		reg(agi::make_unique<video_frame_next>());
		reg(agi::make_unique<video_frame_next_boundary>());
		reg(agi::make_unique<video_frame_next_keyframe>());
		reg(agi::make_unique<video_frame_next_large>());
		reg(agi::make_unique<video_frame_prev>());
		reg(agi::make_unique<video_frame_prev_boundary>());
		reg(agi::make_unique<video_frame_prev_keyframe>());
		reg(agi::make_unique<video_frame_prev_large>());
		reg(agi::make_unique<video_frame_save>());
		reg(agi::make_unique<video_frame_save_raw>());
		reg(agi::make_unique<video_frame_save_subs>());
		reg(agi::make_unique<video_frame_export>());
		reg(agi::make_unique<video_jump>());
		reg(agi::make_unique<video_jump_end>());
		reg(agi::make_unique<video_jump_start>());
		reg(agi::make_unique<video_open>());
		reg(agi::make_unique<video_open_dummy>());
		reg(agi::make_unique<video_reload>());
		reg(agi::make_unique<video_opt_autoscroll>());
		reg(agi::make_unique<video_pan_reset>());
		reg(agi::make_unique<video_play>());
		reg(agi::make_unique<video_play_line>());
		reg(agi::make_unique<video_show_overscan>());
		reg(agi::make_unique<video_stop>());
		reg(agi::make_unique<video_zoom_100>());
		reg(agi::make_unique<video_zoom_200>());
		reg(agi::make_unique<video_zoom_50>());
		reg(agi::make_unique<video_zoom_in>());
		reg(agi::make_unique<video_zoom_out>());
	}
}
