// Copyright (c) 2005-2010, Niels Martin Hansen
// Copyright (c) 2005-2010, Rodrigo Braz Monteiro
// Copyright (c) 2010, Amar Takhar
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

#include <ass_style.h>
#include <dialog_manager.h>
#include <format.h>
#include <iomanip>
#include <regex>

#include "command.h"

#include "../ass_dialogue.h"
#include "../ass_file.h"
#include "../compat.h"
#include "../dialogs.h"
#include "../frame_main.h"
#include "../include/aegisub/context.h"
#include "../libresrc/libresrc.h"
#include "../main.h"
#include "../mocha_motion/motion_dialog.h"
#include "../mocha_motion/motion_processor.h"
#include "../options.h"
#include "../project.h"
#include "../search_replace_engine.h"
#include "../selection_controller.h"
#include "../subs_controller.h"
#include "../subtitle_format.h"
#include "../utils.h"
#include "../video_controller.h"

#include <libaegisub/address_of_adaptor.h>
#include <libaegisub/charset_conv.h>
#include <libaegisub/fs.h>

#include <boost/range/algorithm/copy.hpp>
#include <wx/msgdlg.h>
#include <wx/choicdlg.h>
#include <wx/progdlg.h>

namespace {
	using cmd::Command;

	struct validate_nonempty_selection : public Command {
		CMD_TYPE(COMMAND_VALIDATE)

		bool Validate(const agi::Context *c) override {
			return !c->selectionController->GetSelectedSet().empty();
		}
	};

	struct validate_nonempty_selection_video_loaded : public Command {
		CMD_TYPE(COMMAND_VALIDATE)

		bool Validate(const agi::Context *c) override {
			return c->project->VideoProvider() && !c->selectionController->GetSelectedSet().empty();
		}
	};

	struct subtitle_attachment final : public Command {
		CMD_NAME("subtitle/attachment")
		CMD_ICON(attach_button)
		STR_MENU("A&ttachments...")
		STR_DISP("Attachments")
		STR_HELP("Open the attachment manager dialog")

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			ShowAttachmentsDialog(c->parent, c->ass.get());
		}
	};

	struct subtitle_find final : public Command {
		CMD_NAME("subtitle/find")
		CMD_ICON(find_button)
		STR_MENU("&Find...")
		STR_DISP("Find")
		STR_HELP("Search for text in the subtitles")

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			ShowSearchReplaceDialog(c, false);
		}
	};

	struct subtitle_find_next final : public Command {
		CMD_NAME("subtitle/find/next")
		CMD_ICON(find_next_menu)
		STR_MENU("Find &Next")
		STR_DISP("Find Next")
		STR_HELP("Find next match of last search")

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			if (!c->search->FindNext())
				ShowSearchReplaceDialog(c, false);
		}
	};

	static void insert_subtitle_at_video(agi::Context *c, bool after) {
		auto def = new AssDialogue;
		int video_ms = c->videoController->TimeAtFrame(c->videoController->GetFrameN(), agi::vfr::START);
		def->Start = video_ms;
		def->End = video_ms + OPT_GET("Timing/Default Duration")->GetInt();
		def->Style = c->selectionController->GetActiveLine()->Style;

		auto pos = c->ass->iterator_to(*c->selectionController->GetActiveLine());
		if (after) ++pos;

		c->ass->Events.insert(pos, *def);
		c->ass->Commit(_("line insertion"), AssFile::COMMIT_DIAG_ADDREM);

		c->selectionController->SetSelectionAndActive({def}, def);
	}

	struct subtitle_insert_after final : public validate_nonempty_selection {
		CMD_NAME("subtitle/insert/after")
		STR_MENU("&After Current")
		STR_DISP("After Current")
		STR_HELP("Insert a new line after the current one")

		void operator()(agi::Context *c) override {
			AssDialogue *active_line = c->selectionController->GetActiveLine();

			auto new_line = new AssDialogue;
			new_line->Style = active_line->Style;
			new_line->Start = active_line->End;
			new_line->End = new_line->Start + OPT_GET("Timing/Default Duration")->GetInt();

			for (auto it = c->ass->Events.begin(); it != c->ass->Events.end(); ++it) {
				AssDialogue *diag = &*it;

				// Limit the line to the available time
				if (diag->Start >= new_line->Start)
					new_line->End = std::min(new_line->End, diag->Start);

				// If we just hit the active line, insert the new line after it
				if (diag == active_line) {
					++it;
					c->ass->Events.insert(it, *new_line);
					--it;
				}
			}

			c->ass->Commit(_("line insertion"), AssFile::COMMIT_DIAG_ADDREM);
			c->selectionController->SetSelectionAndActive({new_line}, new_line);
		}
	};

	struct subtitle_insert_after_videotime final : public validate_nonempty_selection_video_loaded {
		CMD_NAME("subtitle/insert/after/videotime")
		STR_MENU("After Current, at Video Time")
		STR_DISP("After Current, at Video Time")
		STR_HELP("Insert a new line after the current one, starting at video time")

		void operator()(agi::Context *c) override {
			insert_subtitle_at_video(c, true);
		}
	};

/// @brief 应用 Mocha Motion 追踪数据到字幕行
/// 使用 mocha_motion 模块的完整运动追踪管线替代旧版内联处理逻辑
/// 支持：位置/缩放/旋转/clip/原点/边框/阴影/模糊等标签的追踪应用
///       线性(\move+\t)和非线性(逐帧)两种模式
///       反向追踪、便捷预览、变换标签插值等高级功能
	struct subtitle_apply_mocha final : public validate_nonempty_selection {
		CMD_NAME("subtitle/apply/mocha")
		STR_MENU("Apply Mocha-Motion")
		STR_DISP("Apply Mocha-Motion")
		STR_HELP("Apply mocha tracking data to the current subtitle entry")
		CMD_TYPE(COMMAND_VALIDATE)

		/// 验证条件：视频已加载且有选中行
		bool Validate(const agi::Context *c) override {
			return c->project->VideoProvider() && !c->selectionController->GetSelectedSet().empty();
		}

		void operator()(agi::Context *c) override {
			// 暂停视频播放以避免在修改字幕时产生冲突
			c->videoController->Stop();

			// 显示新版 Mocha Motion 对话框，获取用户配置和追踪数据
			mocha::MotionDialogResult result = mocha::ShowMotionDialog(c);
			if (!result.accepted) return;

			// 获取所有选中的字幕行（对应 MoonScript LineCollection.collectLines）
			auto selected_set = c->selectionController->GetSelectedSet();
			if (selected_set.empty()) return;

			// 将选中行按出现顺序排列后反转（对应 MoonScript: for i = #sel, 1, -1）
			// 反向处理确保插入新行后不影响后续行的索引定位
			std::vector<AssDialogue *> selected_lines(selected_set.begin(), selected_set.end());
			std::sort(
				selected_lines.begin(), selected_lines.end(),
				[&](AssDialogue *a, AssDialogue *b) {
					// 按在事件列表中的位置排序
					for (auto &Event : c->ass->Events) {
						if (&Event == a) return true;
						if (&Event == b) return false;
					}
					return false;
				}
			);
			std::reverse(selected_lines.begin(), selected_lines.end());

			// 计算行集合的帧范围（遍历所有选中行的最早起始帧和最晚结束帧）
			// 对应 MoonScript: LineCollection.startFrame / .endFrame / .totalFrames
			int collection_start_frame = c->videoController->FrameAtTime(selected_lines.back()->Start, agi::vfr::START);
			int collection_end_frame = c->videoController->FrameAtTime(selected_lines.back()->End, agi::vfr::START);
			for (const AssDialogue *line : selected_lines) {
				const int sf = c->videoController->FrameAtTime(line->Start, agi::vfr::START);
				const int ef = c->videoController->FrameAtTime(line->End, agi::vfr::START);
				if (sf < collection_start_frame) collection_start_frame = sf;
				if (ef > collection_end_frame) collection_end_frame = ef;
			}
			const int total_frames = collection_end_frame - collection_start_frame;

			// 验证追踪数据帧数是否匹配
			// 对应 MoonScript: mainData.dataObject\checkLength lineCollection.totalFrames
			if (!result.main_data.check_length(total_frames)) {
				wxMessageBox(
					agi::wxformat(_("The trace data is asymmetrical with the selected row data and requires %d frames"), total_frames),
					_("Error"), wxICON_ERROR
				);
				return;
			}

			// 验证 clip 数据帧数
			if (result.has_clip_data && !result.clip_data.check_length(total_frames)) {
				wxMessageBox(
					agi::wxformat(_("The clip tracking data is asymmetrical with the selected row data and requires %d frames"), total_frames),
					_("Error"), wxICON_ERROR
				);
				return;
			}

			// 如果启用反向追踪，先反转追踪数据数组
			if (result.options.reverse_tracking) {
				result.main_data.reverse_data();
				if (result.has_clip_data) {
					result.clip_data.reverse_data();
				}
			}

			// 构建运动处理器，传入用户选项和脚本分辨率
			mocha::MotionProcessor processor(result.options, result.script_res_x, result.script_res_y);

			// 设置帧-时间双向转换函数（通过视频控制器实现）
			processor.set_timing_functions(
				[c](int ms) { return c->videoController->FrameAtTime(ms, agi::vfr::START); },
				[c](int frame) { return c->videoController->TimeAtFrame(frame, agi::vfr::START); }
			);

			// 设置样式查询回调（处理器需要从样式获取默认标签值）
			processor.set_style_lookup(
				[c](const std::string &name) -> const AssStyle * {
					return c->ass->GetStyle(name);
				}
			);

			// 两阶段处理：先处理所有行收集结果，再统一跨行合并后插入
			// 对应 MoonScript combineWithLine：跨源行的结果也可以合并

			// 阶段 1：处理所有选中行，收集结果（带进度和取消支持）
			// 对应 MoonScript: aegisub.progress.set, aegisub.progress.is_cancelled
			std::vector<mocha::MotionLine> all_result_lines;
			const int total_lines = static_cast<int>(selected_lines.size());
			bool cancelled = false;

			// 仅多行时显示进度对话框
			std::unique_ptr<wxProgressDialog> progress;
			if (total_lines > 1) {
				progress = std::make_unique<wxProgressDialog>(
					_("Applying Mocha-Motion"),
					_("Processing lines..."),
					total_lines,
					c->parent,
					wxPD_APP_MODAL | wxPD_CAN_ABORT | wxPD_AUTO_HIDE | wxPD_SMOOTH
				);
			}

			int line_index = 0;
			for (AssDialogue *active_line : selected_lines) {
				// 取消检查
				if (progress && !progress->Update(
						line_index,
						wxString::Format(_("Processing line %d / %d ..."), line_index + 1, total_lines)
					)) {
					cancelled = true;
					break;
				}
				// 从 AssDialogue 构建模块内部使用的 MotionLine 数据结构
				mocha::MotionLine motion_line = processor.build_line(active_line);

				// 构建行集合
				std::vector<mocha::MotionLine> lines = {motion_line};

				// 执行完整的运动应用管线：预处理 → 回调应用 → 后处理
				// 使用行集合的起始帧（所有选中行中最早的），使得行内的
				// 相对帧索引正确映射到追踪数据中的位置
				// 对应 MoonScript: lineCollection.startFrame 传给 MotionHandler
				std::vector<mocha::MotionLine> new_lines = processor.apply(
					lines,
					result.main_data,
					result.has_clip_data ? &result.clip_data : nullptr,
					result.has_clip_data ? &result.clip_options : nullptr,
					collection_start_frame
				);

				// 如果启用反向追踪，反转输出行的时间分配
				if (result.options.reverse_tracking && new_lines.size() > 1) {
					std::vector<std::pair<int, int>> times;
					times.reserve(new_lines.size());
					for (const auto &nl : new_lines) {
						times.emplace_back(nl.start_time, nl.end_time);
					}
					std::reverse(times.begin(), times.end());
					for (size_t i = 0; i < new_lines.size(); ++i) {
						new_lines[i].start_time = times[i].first;
						new_lines[i].end_time = times[i].second;
					}
				}

				all_result_lines.insert(
					all_result_lines.end(),
					std::make_move_iterator(new_lines.begin()),
					std::make_move_iterator(new_lines.end())
				);

				++line_index;
			}

			// 关闭进度对话框
			progress.reset();

			// 用户取消时不修改字幕
			if (cancelled || all_result_lines.empty()) return;

			// 按起始时间排序后执行跨行合并
			std::sort(
				all_result_lines.begin(), all_result_lines.end(),
				[](const mocha::MotionLine &a, const mocha::MotionLine &b) {
					return a.start_time < b.start_time;
				}
			);
			processor.cross_line_combine(all_result_lines);

			// 阶段 2：记录选中行之后的插入位置
			// selected_lines 已按反序排列（最晚行在前），最后一个元素是正序中最早的行
			// 在该行的位置插入新行（删除原始行后，插入位置自动调整到正确位置）
			AssDialogue *last_selected = selected_lines.back();

			// 使用行的索引而非迭代器，避免删除行时迭代器失效
			int insert_index = -1;
			int idx = 0;
			for (const auto &event : c->ass->Events) {
				if (&event == last_selected) {
					insert_index = idx + 1; // 该行之后
					break;
				}
				++idx;
			}

			if (insert_index == -1) {
				insert_index = static_cast<int>(c->ass->Events.size()); // 默认在末尾
			}

			// 反向删除/注释所有原始行（selected_lines 已反序，直接遍历即为反向删除）
			// 从后往前删除可保持索引有效性，避免频繁更新 insert_index
			for (AssDialogue *line : selected_lines) {
				if (result.options.preview) {
					// 便捷预览模式：保留原始行但注释掉
					line->Comment = true;
				} else {
					// 正式应用模式：删除原始行
					// 找到该行在列表中的位置
					int idx1 = 0;
					for (auto it = c->ass->Events.begin(); it != c->ass->Events.end(); ++it, ++idx1) {
						if (&*it == line) {
							// 如果删除的行在插入位置之前，则插入索引需调整
							// （这种情况比较少见，因为 insert_index 是激活行之后）
							if (idx1 < insert_index) {
								--insert_index;
							}
							c->ass->Events.erase(it);
							break;
						}
					}
				}
			}

			// 将索引转换为迭代器，在指定位置插入所有合并后的结果行
			auto insert_pos = c->ass->Events.end();
			if (insert_index <= static_cast<int>(c->ass->Events.size())) {
				int idx2 = 0;
				for (auto it = c->ass->Events.begin(); it != c->ass->Events.end(); ++it, ++idx2) {
					if (idx2 == insert_index) {
						insert_pos = it;
						break;
					}
				}
			}


			// 所有行已按时间排序（正向或反向追踪都已通过时间调整体现）
			// 直接顺序插入到选中行之后
			for (const auto &ml : all_result_lines) {
				auto *new_diag = new AssDialogue;
				new_diag->Text = ml.text;
				new_diag->Style = ml.style;
				new_diag->Start = ml.start_time;
				new_diag->End = ml.end_time;
				new_diag->Comment = ml.comment;
				new_diag->Layer = ml.layer;
				new_diag->Margin[0] = ml.margin_l;
				new_diag->Margin[1] = ml.margin_r;
				new_diag->Margin[2] = ml.margin_t;
				new_diag->Actor = ml.actor;
				new_diag->Effect = ml.effect;

				c->ass->Events.insert(insert_pos, *new_diag);
			}

			// 提交修改（保持原来的选中状态）
			c->ass->Commit(_("line insertion"), AssFile::COMMIT_DIAG_ADDREM);

			// 配置已在对话框 OnOK 中保存（每次点击应用按钮即保存，不论追踪是否成功）
		}
	};

	struct subtitle_insert_before final : public validate_nonempty_selection {
		CMD_NAME("subtitle/insert/before")
		STR_MENU("&Before Current")
		STR_DISP("Before Current")
		STR_HELP("Insert a new line before the current one")

		void operator()(agi::Context *c) override {
			AssDialogue *active_line = c->selectionController->GetActiveLine();

			auto new_line = new AssDialogue;
			new_line->Style = active_line->Style;
			new_line->End = active_line->Start;
			new_line->Start = new_line->End - OPT_GET("Timing/Default Duration")->GetInt();

			for (auto it = c->ass->Events.begin(); it != c->ass->Events.end(); ++it) {
				auto diag = &*it;

				// Limit the line to the available time
				if (diag->End <= new_line->End)
					new_line->Start = std::max(new_line->Start, diag->End);

				// If we just hit the active line, insert the new line before it
				if (diag == active_line)
					c->ass->Events.insert(it, *new_line);
			}

			c->ass->Commit(_("line insertion"), AssFile::COMMIT_DIAG_ADDREM);
			c->selectionController->SetSelectionAndActive({new_line}, new_line);
		}
	};

	struct subtitle_insert_before_videotime final : public validate_nonempty_selection_video_loaded {
		CMD_NAME("subtitle/insert/before/videotime")
		STR_MENU("Before Current, at Video Time")
		STR_DISP("Before Current, at Video Time")
		STR_HELP("Insert a new line before the current one, starting at video time")

		void operator()(agi::Context *c) override {
			insert_subtitle_at_video(c, false);
		}
	};

	bool is_okay_to_close_subtitles(agi::Context *c) {
		#ifdef __APPLE__
		return true;
		#else
		return c->subsController->TryToClose() != wxCANCEL;
		#endif
	}

	void load_subtitles(agi::Context *c, agi::fs::path const &path, std::string const &encoding = "") {
		#ifdef __APPLE__
		wxGetApp().NewProjectContext().project->LoadSubtitles(path, encoding);
		#else
		c->project->LoadSubtitles(path, encoding);
		#endif
	}

	struct subtitle_new final : public Command {
		CMD_NAME("subtitle/new")
		CMD_ICON(new_toolbutton)
		STR_MENU("&New Subtitles")
		STR_DISP("New Subtitles")
		STR_HELP("New subtitles")

		void operator()(agi::Context *c) override {
			#ifdef __APPLE__
			wxGetApp().NewProjectContext();
			#else
			if (is_okay_to_close_subtitles(c))
				c->project->CloseSubtitles();
			#endif
		}
	};

	struct subtitle_close final : public Command {
		CMD_NAME("subtitle/close")
		CMD_ICON(new_toolbutton)
		STR_MENU("Close")
		STR_DISP("Close")
		STR_HELP("Close")

		void operator()(agi::Context *c) override {
			c->frame->Close();
		}
	};

	struct subtitle_open final : public Command {
		CMD_NAME("subtitle/open")
		CMD_ICON(open_toolbutton)
		STR_MENU("&Open Subtitles...")
		STR_DISP("Open Subtitles")
		STR_HELP("Open a subtitles file")

		void operator()(agi::Context *c) override {
			if (!is_okay_to_close_subtitles(c)) return;

			auto filename = OpenFileSelector(_("Open Subtitles File"), "Path/Last/Subtitles", "", "", SubtitleFormat::GetWildcards(0), c->parent);
			if (!filename.empty())
				load_subtitles(c, filename);
		}
	};

	struct subtitle_open_autosave final : public Command {
		CMD_NAME("subtitle/open/autosave")
		STR_MENU("Open A&utosaved Subtitles...")
		STR_DISP("Open Autosaved Subtitles")
		STR_HELP("Open a previous version of a file which was autosaved by Aegisub")

		void operator()(agi::Context *c) override {
			if (!is_okay_to_close_subtitles(c)) return;
			auto filename = PickAutosaveFile(c->parent);
			if (!filename.empty())
				load_subtitles(c, filename);
		}
	};

	struct subtitle_open_charset final : public Command {
		CMD_NAME("subtitle/open/charset")
		CMD_ICON(open_with_toolbutton)
		STR_MENU("Open Subtitles with &Charset...")
		STR_DISP("Open Subtitles with Charset")
		STR_HELP("Open a subtitles file with a specific file encoding")

		void operator()(agi::Context *c) override {
			if (!is_okay_to_close_subtitles(c)) return;

			auto filename = OpenFileSelector(_("Open Subtitles File"), "Path/Last/Subtitles", "", "", SubtitleFormat::GetWildcards(0), c->parent);
			if (filename.empty()) return;

			wxString charset = wxGetSingleChoice(_("Choose charset code:"), _("Charset"), agi::charset::GetEncodingsList<wxArrayString>(), c->parent, -1, -1, true, 250, 200);
			if (charset.empty()) return;

			load_subtitles(c, filename, from_wx(charset));
		}
	};

	struct subtitle_open_video final : public Command {
		CMD_NAME("subtitle/open/video")
		STR_MENU("Open Subtitles from &Video")
		STR_DISP("Open Subtitles from Video")
		STR_HELP("Open the subtitles from the current video file")
		CMD_TYPE(COMMAND_VALIDATE)

		void operator()(agi::Context *c) override {
			if (c->subsController->TryToClose() == wxCANCEL) return;
			c->project->LoadSubtitles(c->project->VideoName(), "binary", false);
		}

		bool Validate(const agi::Context *c) override {
			return c->project->CanLoadSubtitlesFromVideo();
		}
	};

	struct subtitle_properties final : public Command {
		CMD_NAME("subtitle/properties")
		CMD_ICON(properties_toolbutton)
		STR_MENU("&Properties...")
		STR_DISP("Properties")
		STR_HELP("Open script properties window")

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			ShowPropertiesDialog(c);
		}
	};

	static void save_subtitles(agi::Context *c, agi::fs::path filename) {
		if (filename.empty()) {
			c->videoController->Stop();
			filename = SaveFileSelector(
				_("Save Subtitles File"), "Path/Last/Subtitles",
				c->subsController->Filename().stem().string() + ".ass", "ass",
				"Advanced Substation Alpha (*.ass)|*.ass", c->parent
			);
			if (filename.empty()) return;
		}

		try {
			c->subsController->Save(filename);
		} catch (const agi::Exception &err) {
			wxMessageBox(to_wx(err.GetMessage()), _("Error"), wxOK | wxICON_ERROR | wxCENTER, c->parent);
		} catch (...) {
			wxMessageBox(_("Unknown error"), _("Error"), wxOK | wxICON_ERROR | wxCENTER, c->parent);
		}
	}

	struct subtitle_save final : public Command {
		CMD_NAME("subtitle/save")
		CMD_ICON(save_toolbutton)
		STR_MENU("&Save Subtitles")
		STR_DISP("Save Subtitles")
		STR_HELP("Save the current subtitles")
		CMD_TYPE(COMMAND_VALIDATE)

		void operator()(agi::Context *c) override {
			save_subtitles(c, c->subsController->CanSave() ? c->subsController->Filename() : "");
		}

		bool Validate(const agi::Context *c) override {
			return c->subsController->IsModified();
		}
	};

	struct subtitle_save_as final : public Command {
		CMD_NAME("subtitle/save/as")
		CMD_ICON(save_as_toolbutton)
		STR_MENU("Save Subtitles &as...")
		STR_DISP("Save Subtitles as")
		STR_HELP("Save subtitles with another name")

		void operator()(agi::Context *c) override {
			save_subtitles(c, "");
		}
	};

	struct subtitle_select_all final : public Command {
		CMD_NAME("subtitle/select/all")
		STR_MENU("Select &All")
		STR_DISP("Select All")
		STR_HELP("Select all dialogue lines")

		void operator()(agi::Context *c) override {
			Selection sel;
			boost::copy(c->ass->Events | agi::address_of, inserter(sel, sel.end()));
			c->selectionController->SetSelectedSet(std::move(sel));
		}
	};

	struct subtitle_select_visible final : public Command {
		CMD_NAME("subtitle/select/visible")
		CMD_ICON(select_visible_button)
		STR_MENU("Select Visible")
		STR_DISP("Select Visible")
		STR_HELP("Select all dialogue lines that are visible on the current video frame")
		CMD_TYPE(COMMAND_VALIDATE)

		void operator()(agi::Context *c) override {
			c->videoController->Stop();

			Selection new_selection;
			int frame = c->videoController->GetFrameN();

			for (auto &diag : c->ass->Events) {
				if (c->videoController->FrameAtTime(diag.Start, agi::vfr::START) <= frame &&
					c->videoController->FrameAtTime(diag.End, agi::vfr::END) >= frame) {
					if (new_selection.empty())
						c->selectionController->SetActiveLine(&diag);
					new_selection.insert(&diag);
				}
			}

			c->selectionController->SetSelectedSet(std::move(new_selection));
		}

		bool Validate(const agi::Context *c) override {
			return !!c->project->VideoProvider();
		}
	};

	struct subtitle_spellcheck final : public Command {
		CMD_NAME("subtitle/spellcheck")
		CMD_ICON(spellcheck_toolbutton)
		STR_MENU("Spell &Checker...")
		STR_DISP("Spell Checker")
		STR_HELP("Open spell checker")

		void operator()(agi::Context *c) override {
			c->videoController->Stop();
			ShowSpellcheckerDialog(c);
		}
	};
}

namespace cmd {
	void init_subtitle() {
		reg(std::make_unique<subtitle_attachment>());
		reg(std::make_unique<subtitle_find>());
		reg(std::make_unique<subtitle_find_next>());
		reg(std::make_unique<subtitle_insert_after>());
		reg(std::make_unique<subtitle_insert_after_videotime>());
		reg(std::make_unique<subtitle_apply_mocha>());
		reg(std::make_unique<subtitle_insert_before>());
		reg(std::make_unique<subtitle_insert_before_videotime>());
		reg(std::make_unique<subtitle_new>());
		reg(std::make_unique<subtitle_close>());
		reg(std::make_unique<subtitle_open>());
		reg(std::make_unique<subtitle_open_autosave>());
		reg(std::make_unique<subtitle_open_charset>());
		reg(std::make_unique<subtitle_open_video>());
		reg(std::make_unique<subtitle_properties>());
		reg(std::make_unique<subtitle_save>());
		reg(std::make_unique<subtitle_save_as>());
		reg(std::make_unique<subtitle_select_all>());
		reg(std::make_unique<subtitle_select_visible>());
		reg(std::make_unique<subtitle_spellcheck>());
	}
}
