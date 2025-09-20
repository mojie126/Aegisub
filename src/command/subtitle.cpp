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
#include "../dialog_search_replace.h"
#include "../dialogs.h"
#include "../frame_main.h"
#include "../include/aegisub/context.h"
#include "../libresrc/libresrc.h"
#include "../main.h"
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
#include <libaegisub/make_unique.h>

#include <boost/range/algorithm/copy.hpp>
#include <wx/msgdlg.h>
#include <wx/choicdlg.h>

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

	c->selectionController->SetSelectionAndActive({ def }, def);
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
		c->selectionController->SetSelectionAndActive({ new_line }, new_line);
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

struct MoveData {
	double x1{}, y1{}, x2{}, y2{};
	int t1{}, t2{};
};

struct FadeData {
	int in{}, out{};
};

struct TransformData {
	int t1{}, t2{};
	double accel{};
	std::string effect;
};

/// @brief 计算 \fade 标签在特定时间的 alpha 乘数
/// @param fade \fade(t1, t2) 数据
/// @param timeDelta 当前帧相对于行开始的时间
/// @param line_duration 行的总持续时间
/// @return alpha 乘数 (0.0 到 1.0)
double calculate_fade_multiplier(const FadeData& fade, const int timeDelta, const int line_duration) {
	if (fade.in > 0 && timeDelta < fade.in) {
		return static_cast<double>(timeDelta) / fade.in;
	}
	if (fade.out > 0 && timeDelta > fade.out) {
		const double fade_out_duration = line_duration - fade.out;
		if (fade_out_duration <= 0) {
			return 0.0; // 如果淡出开始时间晚于或等于行结束时间，则立即完全透明
		}
		const double elapsed_since_fade_out_start = timeDelta - fade.out;
		return std::max(0.0, 1.0 - elapsed_since_fade_out_start / fade_out_duration);
	}
	return 1.0;
}

/// @brief 计算 \move 标签在特定时间的 \pos 坐标
/// @param move \move(x1, y1, x2, y2, t1, t2) 数据
/// @param timeDelta 相对于行开始的时间
/// @return std::pair<double, double> 包含 x 和 y 坐标
std::pair<double, double> calculate_move_position(const MoveData& move, const int timeDelta) {
	if (move.t2 <= move.t1) return {move.x1, move.y1};
	double progress = static_cast<double>(timeDelta - move.t1) / (move.t2 - move.t1);
	progress = std::max(0.0, std::min(1.0, progress));
	double x = move.x1 + (move.x2 - move.x1) * progress;
	double y = move.y1 + (move.y2 - move.y1) * progress;
	return {x, y};
}

/// @brief 移除文本中的 \t, \move, \fad 标签并返回它们的动画效果
/// @param text 原始行文本
/// @param t_data 用于存储 \t 数据的结构体
/// @param move_data 用于存储 \move 数据的结构体
/// @param fade_data 用于存储 \fad 数据的结构体
/// @return 清理了动画标签后的文本
std::string extract_and_remove_animations(
	std::string text,
	std::optional<TransformData>& t_data,
	std::optional<MoveData>& move_data,
	std::optional<FadeData>& fade_data)
{
	// 提取 \t
	// 这个正则表达式可以正确处理嵌套括号，精确匹配从 \t( 开始到对应的 ) 结束
	// 它首先尝试匹配4个参数（t1, t2, accel, effect），然后是3个参数（t1, t2, effect）
	const std::regex t_regex(R"(\\t\(((?:[^,()]|\([^)]*\))+?),((?:[^,()]|\([^)]*\))+?),((?:[^,()]|\([^)]*\))+?),((?:[^()]|\([^)]*\))*)\))");
	const std::regex t_regex_simple(R"(\\t\(((?:[^,()]|\([^)]*\))+?),((?:[^,()]|\([^)]*\))+?),((?:[^()]|\([^)]*\))*)\))");
	std::smatch t_match;
	try {
		if (std::regex_search(text, t_match, t_regex)) {
			t_data = TransformData{std::stoi(t_match[1]), std::stoi(t_match[2]), std::stod(t_match[3]), t_match[4]};
			text = std::regex_replace(text, t_regex, "");
		} else if (std::regex_search(text, t_match, t_regex_simple)) {
			t_data = TransformData{std::stoi(t_match[1]), std::stoi(t_match[2]), 1.0, t_match[3].str()};
			text = std::regex_replace(text, t_regex_simple, "");
		}
	} catch (const std::invalid_argument& e) {
		// 如果时间或加速度参数不是有效的数字，则忽略此标签
		t_data.reset();
	} catch (const std::out_of_range& e) {
		t_data.reset();
	}

	// 提取 \move
	// MoonScript: \\move%(([%.%d%-]+,[%.%d%-]+,[%.%d%-]+,[%.%d%-]+,[%d%-]+,[%d%-]+)%)
	// 增加了对参数前后可能存在的空格的容忍
	const std::regex move_regex(R"(\\move\(\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*\))");
	std::smatch move_match;
	if (std::regex_search(text, move_match, move_regex)) {
		move_data = MoveData{
			std::stod(move_match[1]), std::stod(move_match[2]),
			std::stod(move_match[3]), std::stod(move_match[4]),
			std::stoi(move_match[5]), std::stoi(move_match[6])
		};
		text = std::regex_replace(text, move_regex, "");
	}

	// 提取 \fad
	// MoonScript: \\fade?%((%d+,%d+)%)
	// 匹配 \fad(t1,t2) 或 \fade(t1,t2)，并容忍空格
	const std::regex fad_regex(R"(\\fad(?:e)?\(\s*(\d+)\s*,\s*(\d+)\s*\))");
	std::smatch fad_match;
	if (std::regex_search(text, fad_match, fad_regex)) {
		fade_data = FadeData{std::stoi(fad_match[1]), std::stoi(fad_match[2])};
		text = std::regex_replace(text, fad_regex, "");
	}

	// 清理空的标签块
	text = std::regex_replace(text, std::regex(R"(\{\})"), "");
	return text;
}

struct subtitle_apply_mocha final : public validate_nonempty_selection {
	CMD_NAME("subtitle/apply/mocha")
	STR_MENU("Apply Mocha-Motion")
	STR_DISP("Apply Mocha-Motion")
	STR_HELP("Apply mocha tracking data to the current subtitle entry")
	CMD_TYPE(COMMAND_VALIDATE)

	bool Validate(const agi::Context *c) override {
		return c->project->VideoProvider() && !c->selectionController->GetSelectedSet().empty();
	}

	void operator()(agi::Context *c) override {
		c->videoController->Stop();
		ShowMochaUtilDialog(c);
		bool log = false;
		if (getMochaOK()) {
			const auto [total_frame, frame_rate, source_width, source_height, is_mocha_data, get_position, get_scale, get_rotation, get_preview, get_reverse_tracking] = getMochaCheckData();
			std::vector<KeyframeData> final_data = getMochaMotionParseData();
			AssDialogue *last_inserted_line = nullptr;
			int current_process_frame = 0;
			AssDialogue *active_line = c->selectionController->GetActiveLine();

			std::optional<TransformData> t_data;
			std::optional<MoveData> move_data;
			std::optional<FadeData> fade_data;
			const std::string temp_text = extract_and_remove_animations(active_line->Text, t_data, move_data, fade_data);

			const int startFrame = c->videoController->FrameAtTime(active_line->Start, agi::vfr::START);
			const int endFrame = c->videoController->FrameAtTime(active_line->End, agi::vfr::END);
			int _reverse_tracking_i = endFrame;
			if (int dur_frame = endFrame - startFrame + 1; total_frame != dur_frame) {
				wxMessageBox(agi::wxformat(_("The trace data is asymmetrical with the selected row data and requires %d frames"), dur_frame),_("Error"), wxICON_ERROR);
				return;
			}
			for (auto it = c->ass->Events.begin(); it != c->ass->Events.end(); ++it) {
				if (const AssDialogue *diag = &*it; diag == active_line) {
					++it;
					if (!get_preview) {
						// 删除原始行
						c->ass->Events.erase(c->ass->Events.iterator_to(*active_line));
					} else {
						// 注释原始行，方便预览应用追踪的效果
						active_line->Comment = true;
						last_inserted_line = active_line;
					}

					const int line_start_ms = active_line->Start;
					const int line_end_ms = active_line->End;

					for (int i = startFrame; i <= endFrame; ++i) {
						const auto new_line = new AssDialogue;
						// 获取当前行的基本样式信息
						/*
						* 如果有手动定义fscx, fscy, frz/fr, pos，则以手动定义为准，忽略Style里的定义
						* Margin[0] = 左边距，Margin[1] = 右边距， Margin[2] = 垂直边距
						* an1 X = Margin[0], Y = height - Margin[2]
						* an2 X = width / 2, Y = height - Margin[2]
						* an3 X = width - Margin[1], Y = height - Margin[2]
						* an4 X = Margin[0], Y = height / 2
						* an5 X = width / 2, Y = height / 2
						* an6 X = width - Margin[1], Y = height / 2
						* an7 X = Margin[0], Y = Margin[2]
						* an8 X = width / 2, Y = Margin[2]
						* an9 X = width - Margin[1], Y = Margin[2]
						* xRatio = Mocha_scaleX / Style_scaleX
						* yRatio = Mocha_scaleY / Style_scaleY
						* zRotationDiff = Mocha_rotation - Style_rotation
						*/
						auto [frame, x, y, z, scaleX, scaleY, scaleZ, rotation] = final_data[current_process_frame];
						const int current_frame_start_ms = c->videoController->TimeAtFrame(i, agi::vfr::Time::START);
						// timeDelta 是当前帧相对于原始行动画开始的时间（毫秒）
						const int timeDelta = current_frame_start_ms - line_start_ms;

						AssStyle const *const style = c->ass->GetStyle(active_line->Style);
						double _x, _y, xStartPosition, yStartPosition, xStartScale, yStartScale, zStartRotation, xRatio, yRatio, zRotationDiff, radius, angle, temp_x, temp_y, xCurrentPosition = x, yCurrentPosition = y, default_scale_x = 100., default_scale_y = 100.;
						const double Vertical_margins = style->Margin[2], Right_margin = style->Margin[1], Left_margin = style->Margin[0], Style_scaleX = style->scalex, Style_scaleY = style->scaley, Style_rotation = style->angle;
						if (current_process_frame == 0) {
							double X, Y;
							xStartPosition = x;
							yStartPosition = y;
							xStartScale = scaleX;
							yStartScale = scaleY;
							zStartRotation = rotation;
							if (const int an = style->alignment; an == 1) {
								X = Left_margin;
								Y = source_height - Vertical_margins;
							} else if (an == 2) {
								X = static_cast<double>(source_width) / 2;
								Y = source_height - Vertical_margins;
							} else if (an == 3) {
								X = source_width - Right_margin;
								Y = source_height - Vertical_margins;
							} else if (an == 4) {
								X = Left_margin;
								Y = static_cast<double>(source_height) / 2;
							} else if (an == 5) {
								X = static_cast<double>(source_width) / 2;
								Y = static_cast<double>(source_height) / 2;
							} else if (an == 6) {
								X = source_width - Right_margin;
								Y = static_cast<double>(source_height) / 2;
							} else if (an == 7) {
								X = Left_margin;
								Y = Right_margin;
							} else if (an == 8) {
								X = static_cast<double>(source_width) / 2;
								Y = Right_margin;
							} else if (an == 9) {
								X = source_width - Right_margin;
								Y = Right_margin;
							}
							_x = X;
							_y = Y;
						}
						// 匹配是否激活行有定义
						const std::regex pos_regex(R"(\\pos\((-?\d+(\.\d+)?),\s*(-?\d+(\.\d+)?)\))");
						const std::regex frz_regex(R"(\\frz?(-?\d+(\.\d+)?))");
						const std::regex fscx_regex(R"(\\fscx(-?\d+(\.\d+)?))");
						const std::regex fscy_regex(R"(\\fscy(-?\d+(\.\d+)?))");

						std::smatch pos_match, frz_match, fscx_match, fscy_match;
						auto searchStart(temp_text.cbegin());
						bool find_pos = std::regex_search(searchStart, temp_text.cend(), pos_match, pos_regex);
						bool find_frz = std::regex_search(searchStart, temp_text.cend(), frz_match, frz_regex);
						bool find_fscx = std::regex_search(searchStart, temp_text.cend(), fscx_match, fscx_regex);
						bool find_fscy = std::regex_search(searchStart, temp_text.cend(), fscy_match, fscy_regex);
						/*
						 * xStartPosition, yStartPosition永远是追踪数据的首帧数据，不可变
						 * xStartScale, yStartScale永远是追踪数据的首帧数据，不可变
						 * xRatio, yRatio是追踪数据当前帧缩放值 / 追踪数据首帧缩放值
						 * zRotationDiff是追踪数据当前帧旋转值 - 追踪数据首帧旋转值
						 */
						if (find_pos) {
							_x = wxAtof(pos_match[1].str());
							_y = wxAtof(pos_match[3].str());
						}
						// 如果原始行有 \move 标签，用其插值结果覆盖 \pos
						if (move_data) {
							auto [move_x, move_y] = calculate_move_position(*move_data, timeDelta);
							_x = move_x; _y = move_y;
						}
						if (get_scale) {
							xRatio = scaleX / xStartScale;
							yRatio = scaleY / yStartScale;
						} else {
							xRatio = Style_scaleX / default_scale_x;
							yRatio = Style_scaleY / default_scale_y;
						}
						if (get_rotation) {
							zRotationDiff = rotation - zStartRotation;
							if (zRotationDiff <= 0) {
								zRotationDiff = std::fabs(zRotationDiff);
							} else {
								zRotationDiff = -zRotationDiff;
							}
						} else {
							zRotationDiff = 0;
						}
						temp_x = (_x - xStartPosition) * xRatio;
						temp_y = (_y - yStartPosition) * yRatio;
						// log = true;
						if (log) {
							std::cout << std::setprecision(15) << "frame:\t" << current_process_frame << std::endl;
							std::cout << std::setprecision(15) << "_x:\t" << _x << std::endl;
							std::cout << std::setprecision(15) << "_y:\t" << _y << std::endl;
							std::cout << std::setprecision(15) << "temp_x:\t" << temp_x << std::endl;
							std::cout << std::setprecision(15) << "temp_y:\t" << temp_y << std::endl;
						}
						radius = std::sqrt(temp_x * temp_x + temp_y * temp_y);
						angle = std::atan2(temp_y, temp_x) * 180 / M_PI;
						x = xCurrentPosition + radius * std::cos((angle - zRotationDiff) * M_PI / 180);
						y = yCurrentPosition + radius * std::sin((angle - zRotationDiff) * M_PI / 180);

						if (log) {
							std::cout << std::setprecision(15) << "xStartPosition:\t" << xStartPosition << std::endl;
							std::cout << std::setprecision(15) << "yStartPosition:\t" << yStartPosition << std::endl;
							std::cout << std::setprecision(15) << "xRatio:\t" << xRatio << std::endl;
							std::cout << std::setprecision(15) << "yRatio:\t" << yRatio << std::endl;
							std::cout << std::setprecision(15) << "radius:\t" << radius << std::endl;
							std::cout << std::setprecision(15) << "angle:\t" << angle << std::endl;
							std::cout << std::setprecision(15) << "xCurrentPosition:\t" << xCurrentPosition << std::endl;
							std::cout << std::setprecision(15) << "yCurrentPosition:\t" << yCurrentPosition << std::endl;
							std::cout << std::setprecision(15) << "zRotationDiff:\t" << zRotationDiff << std::endl;
							std::cout << std::setprecision(15) << "angle - zRotationDiff:\t" << angle - zRotationDiff << std::endl;
							std::cout << std::setprecision(15) << "cos:\t" << std::cos((angle - zRotationDiff) * M_PI / 180) << std::endl;
							std::cout << std::setprecision(15) << "sin:\t" << std::sin((angle - zRotationDiff) * M_PI / 180) << std::endl;
							std::cout << std::setprecision(15) << "x:\t" << x << std::endl;
							std::cout << std::setprecision(15) << "y:\t" << y << std::endl;
							std::cout << "===================" << std::endl;
						}

						if (find_frz) {
							rotation = zRotationDiff + wxAtof(frz_match[1].str());
						} else {
							rotation = zRotationDiff;
						}
						if (find_fscx) {
							scaleX = wxAtof(fscx_match[1].str()) * xRatio;
						} else {
							scaleX = Style_scaleX * xRatio;
						}
						if (find_fscy) {
							scaleY = wxAtof(fscy_match[1].str()) * yRatio;
						} else {
							scaleY = Style_scaleY * yRatio;
						}
						new_line->Style = active_line->Style;
						if (get_reverse_tracking) {
							new_line->Start = c->videoController->TimeAtFrame(_reverse_tracking_i, agi::vfr::Time::START);
							new_line->End = c->videoController->TimeAtFrame(_reverse_tracking_i, agi::vfr::Time::END);
							--_reverse_tracking_i;
						} else {
							new_line->Start = c->videoController->TimeAtFrame(i, agi::vfr::Time::START);
							new_line->End = c->videoController->TimeAtFrame(i, agi::vfr::Time::END);
						}
						// 字幕内容处理 -- 开始
						std::string ass_tag_str;

						// 处理 \t
						if (t_data) {
							// timeDelta 是当前帧相对于原始行起始时间的偏移
							int new_t1 = t_data->t1 - timeDelta;
							int new_t2 = t_data->t2 - timeDelta;

							// 重新构建 \t 标签
							if (t_data->accel == 1.0) {
								ass_tag_str.append(agi::wxformat(R"(\t(%d,%d,%s))", new_t1, new_t2, t_data->effect));
							} else {
								ass_tag_str.append(agi::wxformat(R"(\t(%d,%d,%g,%s))", new_t1, new_t2, t_data->accel, t_data->effect));
							}
						}

						// 处理 \fad
						if (fade_data) {
							const int line_duration = line_end_ms - line_start_ms;
							double fade_multiplier = calculate_fade_multiplier(*fade_data, timeDelta, line_duration);
							int alpha = 255 - static_cast<int>(fade_multiplier * 255.0);
							ass_tag_str.append(agi::wxformat(R"(\alpha&H%02X&)", alpha));
						}

						// 直接在原始文本（已移除\t,\move,\fad）上进行原地替换，以保持标签的相对位置
						std::string final_text = temp_text;

						// 替换 \pos 标签
						if (get_position) {
							// 如果原始文本中存在 \pos，则替换它
							if (find_pos) {
								final_text = std::regex_replace(final_text, pos_regex, agi::wxformat(R"(\pos(%lf, %lf))", x, y).ToStdString());
							} else {
								// 如果不存在，则在第一个标签块的开头添加
								if (final_text.empty() || final_text.front() != '{') final_text.insert(0, "{}");
								final_text.insert(1, agi::wxformat(R"(\pos(%lf, %lf))", x, y).ToStdString());
							}
						}

						// 替换 \fscx 和 \fscy 标签
						if (get_scale) {
							if (find_fscx)
								final_text = std::regex_replace(final_text, fscx_regex, agi::wxformat(R"(\fscx%lf)", scaleX).ToStdString());
							else {
								if (final_text.empty() || final_text.front() != '{') final_text.insert(0, "{}");
								final_text.insert(1, agi::wxformat(R"(\fscx%lf)", scaleX).ToStdString());
							}

							if (find_fscy)
								final_text = std::regex_replace(final_text, fscy_regex, agi::wxformat(R"(\fscy%lf)", scaleY).ToStdString());
							else {
								if (final_text.empty() || final_text.front() != '{') final_text.insert(0, "{}");
								final_text.insert(1, agi::wxformat(R"(\fscy%lf)", scaleY).ToStdString());
							}
						}

						// 替换 \frz 标签
						if (get_rotation) {
							if (find_frz)
								final_text = std::regex_replace(final_text, frz_regex, agi::wxformat(R"(\frz%lf)", rotation).ToStdString());
							else {
								if (final_text.empty() || final_text.front() != '{') final_text.insert(0, "{}");
								final_text.insert(1, agi::wxformat(R"(\frz%lf)", rotation).ToStdString());
							}
						}

						// 将其他动态生成的标签（如\t, \alpha）插入到第一个标签块的末尾
						if (final_text.empty() || final_text.front() != '{') final_text.insert(0, "{}");
						final_text.insert(final_text.find('}') , ass_tag_str);
						
						new_line->Text = final_text;
						// 字幕内容处理 -- 结束
						c->ass->Events.insert(it, *new_line);
						if (current_process_frame == 0 && !get_preview)
							last_inserted_line = new_line;
						ass_tag_str.clear();
						++current_process_frame;
					}
					--it;
				}
			}

			c->ass->Commit(_("line insertion"), AssFile::COMMIT_DIAG_ADDREM);
			c->selectionController->SetSelectionAndActive({last_inserted_line}, last_inserted_line);
		}
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
		c->selectionController->SetSelectionAndActive({ new_line }, new_line);
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

void load_subtitles(agi::Context *c, agi::fs::path const& path, std::string const& encoding="") {
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

		auto filename = OpenFileSelector(_("Open subtitles file"), "Path/Last/Subtitles", "","", SubtitleFormat::GetWildcards(0), c->parent);
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

		auto filename = OpenFileSelector(_("Open subtitles file"), "Path/Last/Subtitles", "","", SubtitleFormat::GetWildcards(0), c->parent);
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
		filename = SaveFileSelector(_("Save subtitles file"), "Path/Last/Subtitles",
			c->subsController->Filename().stem().string() + ".ass", "ass",
			"Advanced Substation Alpha (*.ass)|*.ass", c->parent);
		if (filename.empty()) return;
	}

	try {
		c->subsController->Save(filename);
	}
	catch (const agi::Exception& err) {
		wxMessageBox(to_wx(err.GetMessage()), "Error", wxOK | wxICON_ERROR | wxCENTER, c->parent);
	}
	catch (...) {
		wxMessageBox("Unknown error", "Error", wxOK | wxICON_ERROR | wxCENTER, c->parent);
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

		for (auto& diag : c->ass->Events) {
			if (c->videoController->FrameAtTime(diag.Start, agi::vfr::START) <= frame &&
				c->videoController->FrameAtTime(diag.End, agi::vfr::END) >= frame)
			{
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
		reg(agi::make_unique<subtitle_attachment>());
		reg(agi::make_unique<subtitle_find>());
		reg(agi::make_unique<subtitle_find_next>());
		reg(agi::make_unique<subtitle_insert_after>());
		reg(agi::make_unique<subtitle_insert_after_videotime>());
		reg(agi::make_unique<subtitle_apply_mocha>());
		reg(agi::make_unique<subtitle_insert_before>());
		reg(agi::make_unique<subtitle_insert_before_videotime>());
		reg(agi::make_unique<subtitle_new>());
		reg(agi::make_unique<subtitle_close>());
		reg(agi::make_unique<subtitle_open>());
		reg(agi::make_unique<subtitle_open_autosave>());
		reg(agi::make_unique<subtitle_open_charset>());
		reg(agi::make_unique<subtitle_open_video>());
		reg(agi::make_unique<subtitle_properties>());
		reg(agi::make_unique<subtitle_save>());
		reg(agi::make_unique<subtitle_save_as>());
		reg(agi::make_unique<subtitle_select_all>());
		reg(agi::make_unique<subtitle_select_visible>());
		reg(agi::make_unique<subtitle_spellcheck>());
	}
}
