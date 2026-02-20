// Copyright (c) 2024-2026, Aegisub contributors
// Mocha Motion 对话框实现
// 从 dialog_mocha.cpp 迁移，使用 DataHandler 替代 parseData
// 相比原始简易版对话框，增加了更多追踪选项和实时数据验证功能

#include "motion_dialog.h"
#include "motion_config.h"

#include <aegisub/context.h>
#include "../ass_file.h"
#include "../ass_dialogue.h"
#include "../selection_controller.h"
#include "../video_controller.h"
#include "../project.h"
#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/spinctrl.h>
#include <wx/clipbrd.h>
#include <wx/dataobj.h>
#include <wx/tooltip.h>
#include <wx/valtext.h>

namespace mocha {
	namespace {
/// Mocha Motion 对话框 UI 实现
/// 负责构建 UI 布局、收集用户选项、解析追踪数据
/// 包含实时数据验证功能：自动检测追踪数据帧数是否匹配选中行的帧数
		struct MotionDialogImpl {
			explicit MotionDialogImpl(agi::Context *c, int script_res_x, int script_res_y);

			void OnOK(wxCommandEvent &);

			void OnCancel(wxCommandEvent &);

			void OnPaste(wxCommandEvent &);

			void OnActivate(wxActivateEvent &);

			/// 保存配置选项勾选状态变化时即时持久化
			void OnWriteConfChanged(wxCommandEvent &);

			/// 打开独立 clip 追踪数据对话框
			void OnClipSeparate(wxCommandEvent &);

			/// 从当前 UI 控件状态收集所有选项到 MotionOptions
			void GatherCurrentOptions(MotionOptions &opts) const;

			/// 实时更新数据状态：解析追踪数据并与选中行帧数比较
			void UpdateDataStatus();

			/// 更新 UI 联动状态
			void UpdateDependencies();

			wxDialog d;
			agi::Context *ctx;
			/// 脚本水平分辨率，用于坐标缩放计算
			int script_res_x;
			/// 脚本垂直分辨率，用于坐标缩放计算
			int script_res_y;

			// 主数据文本框：用户粘贴 AE 关键帧数据的输入区域
			wxTextCtrl *data_text = nullptr;
			/// 数据状态标签：显示帧数匹配信息和解析结果
			wxStaticText *lbl_status = nullptr;

			// 位置选项复选框
			wxCheckBox *chk_x_pos = nullptr; // X 位置
			wxCheckBox *chk_y_pos = nullptr; // Y 位置
			wxCheckBox *chk_origin = nullptr; // 移动原点
			wxCheckBox *chk_abs_pos = nullptr; // 绝对位置

			// 缩放选项复选框
			wxCheckBox *chk_scale = nullptr; // 缩放
			wxCheckBox *chk_border = nullptr; // 边框随缩放
			wxCheckBox *chk_shadow = nullptr; // 阴影随缩放
			wxCheckBox *chk_blur = nullptr; // 模糊随缩放
			wxTextCtrl *txt_blur_scale = nullptr; // 模糊衰减系数
			wxStaticText *lbl_blur_scale = nullptr;

			// 3D 通道
			wxCheckBox *chk_x_rotation = nullptr;
			wxCheckBox *chk_y_rotation = nullptr;
			wxCheckBox *chk_z_rotation = nullptr;
			wxCheckBox *chk_z_position = nullptr;

			// Clip 选项复选框
			wxCheckBox *chk_rect_clip = nullptr;
			wxCheckBox *chk_vect_clip = nullptr;
			wxCheckBox *chk_rc_to_vc = nullptr;

			// 处理模式选项
			wxCheckBox *chk_kill_trans = nullptr;
			wxCheckBox *chk_linear = nullptr;
			wxCheckBox *chk_clip_only = nullptr;

			// 配置选项
			wxCheckBox *chk_relative = nullptr;
			wxSpinCtrl *spin_start_frame = nullptr;
			wxStaticText *lbl_start_frame = nullptr;
			wxCheckBox *chk_preview = nullptr;
			wxCheckBox *chk_reverse = nullptr;
			wxCheckBox *chk_write_conf = nullptr;

			// 独立 clip 追踪数据
			wxButton *btn_clip_sep = nullptr; // "Track \\clip separately" 按钮
			wxStaticText *lbl_clip_status = nullptr; // clip 数据状态标签
			ClipTrackOptions clip_options_; // clip 选项
			std::string clip_data_text_; // clip 数据文本（由 clip 对话框设置）
			bool has_clip_data_ = false; // 是否有独立 clip 数据

			/// 选中行集合的起始帧号（绝对），用于相对/绝对帧号互转
			int collection_start_frame_ = 0;
			/// 上一次的 relative 状态，用于检测切换
			bool last_relative_ = true;

			// 结果
			MotionDialogResult result;
		};

		MotionDialogImpl::MotionDialogImpl(agi::Context *c, int res_x, int res_y)
			: d(
				c->parent, -1, _("Mocha Motion"), wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
			)
			, ctx(c)
			, script_res_x(res_x)
			, script_res_y(res_y) {
			// 设置工具提示自动消失时间为 32767ms（~32秒）
			// Windows API TTM_SETDELAYTIME 使用 lParam 的 LOWORD（16位有符号）
			// 32767 是 signed short 最大正值，超过此值会被截断或解释为重置默认
			wxToolTip::SetAutoPop(32767);
			// 设置提示显示延迟为 100ms（默认 500ms）
			wxToolTip::SetDelay(100);

			auto *main_sizer = new wxBoxSizer(wxVERTICAL);
			const int pad = d.FromDIP(6);
			const int inner_pad = d.FromDIP(4);

			// ====== 数据输入区 ======
			auto *data_box = new wxStaticBox(&d, wxID_ANY, _("Tracking Data"));
			auto *data_sizer = new wxStaticBoxSizer(data_box, wxVERTICAL);
			auto *data_label = new wxStaticText(
				&d, wxID_ANY,
				_("Paste AE keyframe data or enter file path (no quotes):")
			);
			data_text = new wxTextCtrl(
				&d, wxID_ANY, "", wxDefaultPosition,
				wxSize(-1, d.FromDIP(100)), wxTE_MULTILINE
			);
			data_text->SetToolTip(
				_(
					"Supports Adobe After Effects keyframe data. "
					"You can also enter a file path to load data from file."
				)
			);
			// 状态标签：显示追踪数据帧数与选中行帧数的匹配状态
			lbl_status = new wxStaticText(&d, wxID_ANY, _("No data loaded"));
			data_sizer->Add(data_label, 0, wxLEFT | wxRIGHT | wxTOP, inner_pad);
			data_sizer->Add(data_text, 1, wxEXPAND | wxALL, inner_pad);
			data_sizer->Add(lbl_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, inner_pad);

			// ====== 位置选项 (\\pos) ======
			auto *pos_box = new wxStaticBox(&d, wxID_ANY, _("Position (\\pos)"));
			auto *pos_sizer = new wxStaticBoxSizer(pos_box, wxHORIZONTAL);

			chk_x_pos = new wxCheckBox(&d, wxID_ANY, _("&X"));
			chk_x_pos->SetValue(true);
			chk_x_pos->SetToolTip(_("Apply X position tracking data (\\pos X component)"));

			chk_y_pos = new wxCheckBox(&d, wxID_ANY, _("&Y"));
			chk_y_pos->SetValue(true);
			chk_y_pos->SetToolTip(_("Apply Y position tracking data (\\pos Y component)"));

			chk_origin = new wxCheckBox(&d, wxID_ANY, _("&Origin(\\org)"));
			chk_origin->SetToolTip(_("Move the origin point along with position data"));

			chk_abs_pos = new wxCheckBox(&d, wxID_ANY, _("Absolut&e"));
			chk_abs_pos->SetToolTip(_("Set position to exactly that of the tracking data with no processing"));

			pos_sizer->Add(chk_x_pos, 0, wxALL, inner_pad);
			pos_sizer->Add(chk_y_pos, 0, wxALL, inner_pad);
			pos_sizer->Add(chk_origin, 0, wxALL, inner_pad);
			pos_sizer->Add(chk_abs_pos, 0, wxALL, inner_pad);

			// ====== 缩放选项 (\\fscx, \\fscy) ======
			auto *scale_box = new wxStaticBox(&d, wxID_ANY, _("Scale (\\fscx, \\fscy)"));
			auto *scale_sizer = new wxStaticBoxSizer(scale_box, wxVERTICAL);

			auto *scale_row1 = new wxBoxSizer(wxHORIZONTAL);
			chk_scale = new wxCheckBox(&d, wxID_ANY, _("&Scale"));
			chk_scale->SetValue(true);
			chk_scale->SetToolTip(
				_(
					"Apply scaling data to the selected lines. "
					"When unchecked, Border/Shadow/Blur options are also disabled."
				)
			);

			chk_border = new wxCheckBox(&d, wxID_ANY, _("&Border(\\bord)"));
			chk_border->SetValue(true);
			chk_border->SetToolTip(_("Scale border width with the line (requires Scale)"));

			chk_shadow = new wxCheckBox(&d, wxID_ANY, _("Shado&w(\\shad)"));
			chk_shadow->SetValue(true);
			chk_shadow->SetToolTip(_("Scale shadow offset with the line (requires Scale)"));

			scale_row1->Add(chk_scale, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);

			auto *scale_row2 = new wxBoxSizer(wxHORIZONTAL);
			chk_blur = new wxCheckBox(&d, wxID_ANY, _("Bl&ur(\\blur)"));
			chk_blur->SetValue(true);
			chk_blur->SetToolTip(_("Scale blur with the line (requires Scale, does not scale \\be)"));

			lbl_blur_scale = new wxStaticText(&d, wxID_ANY, _("Factor:"));
			// 输入验证：仅允许数字、负号和小数点
			wxTextValidator numericValidator(wxFILTER_INCLUDE_CHAR_LIST);
			numericValidator.SetCharIncludes("0123456789-.");
			txt_blur_scale = new wxTextCtrl(
				&d, wxID_ANY, "1.00",
				wxDefaultPosition, wxSize(d.FromDIP(50), -1),
				0, numericValidator
			);
			txt_blur_scale->SetToolTip(
				_(
					"Factor to attenuate (or amplify) blur scale ratio. "
					"1.0 = full tracking ratio, 0.5 = half effect."
				)
			);

			const int compact_gap = d.FromDIP(4);
			scale_row2->Add(chk_border, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
			scale_row2->Add(chk_shadow, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
			scale_row2->Add(chk_blur, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
			scale_row2->Add(lbl_blur_scale, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, compact_gap);
			scale_row2->Add(txt_blur_scale, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxTOP | wxBOTTOM, compact_gap);

			scale_sizer->Add(scale_row1, 0, wxEXPAND);
			scale_sizer->Add(scale_row2, 0, wxEXPAND);

			// 3D 通道
			auto *rot_box = new wxStaticBox(&d, wxID_ANY, _("3D (\\frx, \\fry, \\frz, \\z)"));
			auto *rot_sizer = new wxStaticBoxSizer(rot_box, wxHORIZONTAL);
			chk_x_rotation = new wxCheckBox(&d, wxID_ANY, _("X Rot(\\frx)"));
			chk_x_rotation->SetToolTip(_("Apply X-axis rotation data to the selected lines"));
			chk_y_rotation = new wxCheckBox(&d, wxID_ANY, _("Y Rot(\\fry)"));
			chk_y_rotation->SetToolTip(_("Apply Y-axis rotation data to the selected lines"));
			chk_z_rotation = new wxCheckBox(&d, wxID_ANY, _("Z Rot(\\frz)"));
			chk_z_rotation->SetToolTip(_("Apply Z-axis rotation data to the selected lines"));
			chk_z_position = new wxCheckBox(&d, wxID_ANY, _("Z Pos(\\z)"));
			chk_z_position->SetToolTip(_("Apply Z position (depth) data to the selected lines"));

			rot_sizer->Add(chk_x_rotation, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
			rot_sizer->Add(chk_y_rotation, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
			rot_sizer->Add(chk_z_rotation, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
			rot_sizer->Add(chk_z_position, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);

			// Clip
			auto *clip_box = new wxStaticBox(&d, wxID_ANY, _("Clip (\\clip)"));
			auto *clip_sizer = new wxStaticBoxSizer(clip_box, wxHORIZONTAL);

			chk_rect_clip = new wxCheckBox(&d, wxID_ANY, _("Rect C&lip"));
			chk_rect_clip->SetValue(true);
			chk_rect_clip->SetToolTip(_("Apply tracking data to rectangular \\clip in the line"));

			chk_vect_clip = new wxCheckBox(&d, wxID_ANY, _("&Vect Clip"));
			chk_vect_clip->SetValue(true);
			chk_vect_clip->SetToolTip(_("Apply tracking data to vector \\clip in the line"));

			chk_rc_to_vc = new wxCheckBox(&d, wxID_ANY, _("R->V"));
			chk_rc_to_vc->SetToolTip(
				_(
					"Convert rectangular clip to vector clip before tracking. "
					"Automatically enables both Rect Clip and Vect Clip."
				)
			);

			clip_sizer->Add(chk_rect_clip, 0, wxALL, inner_pad);
			clip_sizer->Add(chk_vect_clip, 0, wxALL, inner_pad);
			clip_sizer->Add(chk_rc_to_vc, 0, wxALL, inner_pad);

			// ====== 独立 clip 追踪 ======
			// 对应 MoonScript: "Track \\clip separately" 按钮切换到 clip 对话框
			auto *clip_sep_sizer = new wxBoxSizer(wxHORIZONTAL);

			// 使用固定 ID 避免和标准按钮冲突
			static const int ID_CLIP_SEP = wxID_HIGHEST + 100;
			btn_clip_sep = new wxButton(&d, ID_CLIP_SEP, _("Track \\clip separately"));
			btn_clip_sep->SetToolTip(
				_(
					"Open a separate dialog to provide independent tracking data for clips. "
					"This allows clips to move independently from the main subtitle."
				)
			);

			lbl_clip_status = new wxStaticText(&d, wxID_ANY, _("No separate clip data"));
			lbl_clip_status->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

			clip_sep_sizer->Add(btn_clip_sep, 0, wxALL, inner_pad);
			clip_sep_sizer->Add(lbl_clip_status, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, inner_pad);

			// ====== 处理模式 ======
			auto *mode_box = new wxStaticBox(&d, wxID_ANY, _("Processing Mode"));
			auto *mode_sizer = new wxStaticBoxSizer(mode_box, wxHORIZONTAL);

			chk_kill_trans = new wxCheckBox(&d, wxID_ANY, _("&Multi-line(\\t)"));
			chk_kill_trans->SetValue(true);
			chk_kill_trans->SetToolTip(
				_(
					"Generate per-frame subtitle lines with interpolated \\t tags. "
					"When enabled, \\t tags are evaluated and replaced at each frame; "
					"when disabled, \\t times are shifted."
				)
			);

			chk_linear = new wxCheckBox(&d, wxID_ANY, _("Si&ngle-line(\\move)"));
			chk_linear->SetToolTip(
				_(
					"Use \\move + \\t to create linear transition (single output line), "
					"instead of generating per-frame lines."
				)
			);

			chk_clip_only = new wxCheckBox(&d, wxID_ANY, _("&Clip Only"));
			chk_clip_only->SetToolTip(
				_(
					"Only apply tracking to clip tags. Ignores position, scale, rotation, "
					"border, shadow, and blur."
				)
			);

			mode_sizer->Add(chk_kill_trans, 0, wxALL, inner_pad);
			mode_sizer->Add(chk_linear, 0, wxALL, inner_pad);
			mode_sizer->Add(chk_clip_only, 0, wxALL, inner_pad);

			// ====== 配置选项 ======
			auto *config_box = new wxStaticBox(&d, wxID_ANY, _("Configuration"));
			auto *config_sizer = new wxStaticBoxSizer(config_box, wxVERTICAL);

			auto *cfg_row1 = new wxBoxSizer(wxHORIZONTAL);
			chk_relative = new wxCheckBox(&d, wxID_ANY, _("Relat&ive"));
			chk_relative->SetValue(true);
			chk_relative->SetToolTip(_("Relative: start frame is an index into tracking data (1=first). Absolute: start frame is a video frame number, auto-converted to relative."));

			lbl_start_frame = new wxStaticText(&d, wxID_ANY, _("Start Frame (relative):"));
			spin_start_frame = new wxSpinCtrl(
				&d, wxID_ANY, "1", wxDefaultPosition,
				wxSize(d.FromDIP(70), -1), wxSP_ARROW_KEYS, -99999, 99999, 1
			);
			spin_start_frame->SetToolTip(
				_("Relative mode: 1=first frame, -1=last frame, 0=auto-adjusted to 1.\nAbsolute mode: video frame number where tracking data starts.")
			);

			cfg_row1->Add(chk_relative, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
			cfg_row1->Add(lbl_start_frame, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, compact_gap);
			cfg_row1->Add(spin_start_frame, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxTOP | wxBOTTOM, compact_gap);

			auto *cfg_row2 = new wxBoxSizer(wxHORIZONTAL);
			chk_preview = new wxCheckBox(&d, wxID_ANY, _("Convenient preview"));
			chk_preview->SetValue(true);
			chk_preview->SetToolTip(
				_(
					"Annotate original subtitle to preview tracking effect, "
					"then click [Play Current Line] to preview."
				)
			);

			chk_reverse = new wxCheckBox(&d, wxID_ANY, _("Reverse tracking"));
			chk_reverse->SetToolTip(
				_(
					"Reverse tracking data order. Use when Mocha tracked from "
					"last frame to first frame."
				)
			);

			chk_write_conf = new wxCheckBox(&d, wxID_ANY, _("Save config"));
			chk_write_conf->SetValue(true);
			chk_write_conf->SetToolTip(_("Save current options to configuration file for next use."));

			cfg_row2->Add(chk_preview, 0, wxALL, inner_pad);
			cfg_row2->Add(chk_reverse, 0, wxALL, inner_pad);
			cfg_row2->Add(chk_write_conf, 0, wxALL, inner_pad);

			config_sizer->Add(cfg_row1, 0, wxEXPAND);
			config_sizer->Add(cfg_row2, 0, wxEXPAND);

			// ====== 按钮 ======
			auto *btn_sizer = d.CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxAPPLY);
			btn_sizer->GetAffirmativeButton()->SetLabel(_("Apply"));
			btn_sizer->GetCancelButton()->SetLabel(_("Cancel"));
			btn_sizer->GetApplyButton()->SetLabel(_("Paste from Clipboard"));

			// ====== 组装总布局（单列垂直排列，更紧凑） ======
			main_sizer->Add(data_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(pos_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(scale_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(rot_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(clip_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(clip_sep_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(mode_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(config_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(btn_sizer, 0, wxALIGN_RIGHT | wxALL, pad);

			// 加载持久化配置（对应 MoonScript: options\read!, options\updateInterface）
			MotionConfig::Load(result.options);
			chk_x_pos->SetValue(result.options.x_position);
			chk_y_pos->SetValue(result.options.y_position);
			chk_origin->SetValue(result.options.origin);
			chk_abs_pos->SetValue(result.options.abs_pos);
			chk_scale->SetValue(result.options.x_scale);
			chk_border->SetValue(result.options.border);
			chk_shadow->SetValue(result.options.shadow);
			chk_blur->SetValue(result.options.blur);
			txt_blur_scale->SetValue(wxString::Format("%.2f", result.options.blur_scale));
			chk_x_rotation->SetValue(result.options.x_rotation);
			chk_y_rotation->SetValue(result.options.y_rotation);
			chk_z_rotation->SetValue(result.options.z_rotation);
			chk_z_position->SetValue(result.options.z_position);
			chk_rect_clip->SetValue(result.options.rect_clip);
			chk_vect_clip->SetValue(result.options.vect_clip);
			chk_rc_to_vc->SetValue(result.options.rc_to_vc);
			chk_kill_trans->SetValue(result.options.kill_trans);
			chk_linear->SetValue(result.options.linear);
			chk_clip_only->SetValue(result.options.clip_only);
			chk_relative->SetValue(result.options.relative);
			spin_start_frame->SetValue(result.options.start_frame);
			chk_preview->SetValue(result.options.preview);
			chk_reverse->SetValue(result.options.reverse_tracking);
			chk_write_conf->SetValue(result.options.write_conf);

			// 加载 clip 持久化配置
			MotionConfig::LoadClip(clip_options_);

			// 自动粘贴剪贴板（仅一次，对应 MoonScript fetchDataFromClipboard）
			wxCommandEvent evt;
			OnPaste(evt);

			// 起始帧自动设置：根据视频光标位置计算
			// 对应 MoonScript: relativeFrame = currentVideoFrame - lineCollection.startFrame + 1
			// 仅当计算结果在有效范围 (0, totalFrames] 内时设置，否则保留配置默认值
			if (ctx->project->VideoProvider()) {
				int current_video_frame = ctx->videoController->GetFrameN();

				// 始终计算选中行集合的起始帧，用于后续相对/绝对帧号互转
				auto selected_set = ctx->selectionController->GetSelectedSet();
				if (!selected_set.empty()) {
					int coll_start = INT_MAX;
					int coll_end = 0;
					for (const auto *line : selected_set) {
						int sf = ctx->videoController->FrameAtTime(line->Start, agi::vfr::START);
						int ef = ctx->videoController->FrameAtTime(line->End, agi::vfr::END);
						if (sf < coll_start) coll_start = sf;
						if (ef > coll_end) coll_end = ef;
					}
					collection_start_frame_ = coll_start;

					if (result.options.relative) {
						// 相对模式：计算选中行集合的起始帧和结束帧，得到相对偏移
						int total_frames = coll_end - coll_start;
						int relative_frame = current_video_frame - coll_start + 1;
						// 对应 MoonScript: if relativeFrame > 0 and relativeFrame <= lineCollection.totalFrames
						if (relative_frame > 0 && relative_frame <= total_frames) {
							spin_start_frame->SetValue(relative_frame);
							// 对应 MoonScript: interface.clip.startFrame.value = relativeFrame
							clip_options_.start_frame = relative_frame;
						}
					} else {
						// 绝对模式：直接使用视频光标帧号
						spin_start_frame->SetValue(current_video_frame);
						// 对应 MoonScript: interface.clip.startFrame.value = currentVideoFrame
						clip_options_.start_frame = current_video_frame;
					}
				} else if (!result.options.relative) {
					// 没有选中行但绝对模式：直接使用视频光标帧号
					spin_start_frame->SetValue(current_video_frame);
					clip_options_.start_frame = current_video_frame;
				}
			}

			// 记录初始 relative 状态，用于后续切换检测
			last_relative_ = result.options.relative;

			// 绑定文本变化事件，实时更新数据验证状态
			data_text->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { UpdateDataStatus(); });

			// --- UI 联动禁用逻辑 ---
			// 所有联动集中在 UpdateDependencies()，每次选项变化时统一刷新
			auto bind_update = [this](wxCheckBox *cb) {
				cb->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { UpdateDependencies(); });
			};

			bind_update(chk_scale);
			bind_update(chk_blur);
			bind_update(chk_relative);
			bind_update(chk_rc_to_vc);
			bind_update(chk_clip_only);
			bind_update(chk_x_pos);
			bind_update(chk_y_pos);

			// 保存配置勾选状态变化时即时持久化
			chk_write_conf->Bind(wxEVT_CHECKBOX, &MotionDialogImpl::OnWriteConfChanged, this);

			// 设置初始联动状态
			UpdateDependencies();

			d.SetSizerAndFit(main_sizer);
			d.SetSize(d.FromDIP(wxSize(480, -1)));
			d.CentreOnScreen();

			d.Bind(wxEVT_BUTTON, &MotionDialogImpl::OnOK, this, wxID_OK);
			d.Bind(wxEVT_BUTTON, &MotionDialogImpl::OnCancel, this, wxID_CANCEL);
			d.Bind(wxEVT_BUTTON, &MotionDialogImpl::OnPaste, this, wxID_APPLY);
			d.Bind(wxEVT_BUTTON, &MotionDialogImpl::OnClipSeparate, this, ID_CLIP_SEP);
			d.Bind(wxEVT_ACTIVATE, &MotionDialogImpl::OnActivate, this);
		}

		void MotionDialogImpl::GatherCurrentOptions(MotionOptions &opts) const {
			opts.x_position = chk_x_pos->IsChecked();
			opts.y_position = chk_y_pos->IsChecked();
			opts.origin = chk_origin->IsChecked();
			opts.abs_pos = chk_abs_pos->IsChecked();
			opts.x_scale = chk_scale->IsChecked();
			opts.border = chk_border->IsChecked() && opts.x_scale;
			opts.shadow = chk_shadow->IsChecked() && opts.x_scale;
			opts.blur = chk_blur->IsChecked() && opts.x_scale;
			opts.x_rotation = chk_x_rotation->IsChecked();
			opts.y_rotation = chk_y_rotation->IsChecked();
			opts.z_rotation = chk_z_rotation->IsChecked();
			opts.z_position = chk_z_position->IsChecked();
			opts.rect_clip = chk_rect_clip->IsChecked();
			opts.vect_clip = chk_vect_clip->IsChecked();
			opts.rc_to_vc = chk_rc_to_vc->IsChecked();
			// 对应 MoonScript: if configField.rcToVc then rectClip=true, vectClip=true
			if (opts.rc_to_vc) {
				opts.rect_clip = true;
				opts.vect_clip = true;
			}
			opts.kill_trans = chk_kill_trans->IsChecked();
			opts.linear = chk_linear->IsChecked();
			opts.clip_only = chk_clip_only->IsChecked();
			opts.relative = chk_relative->IsChecked();
			opts.start_frame = spin_start_frame->GetValue();
			opts.preview = chk_preview->IsChecked();
			opts.reverse_tracking = chk_reverse->IsChecked();

			// 解析模糊衰减因子
			double blur_scale = 1.0;
			txt_blur_scale->GetValue().ToDouble(&blur_scale);
			opts.blur_scale = blur_scale;

			// 写入配置开关
			opts.write_conf = chk_write_conf->IsChecked();
		}

		void MotionDialogImpl::OnOK(wxCommandEvent &) {
			// 收集选项
			MotionOptions &opts = result.options;
			GatherCurrentOptions(opts);

			// 每次点击应用按钮都保存配置（不论后续追踪是否成功）
			// 除非用户未勾选"保存配置"选项
			if (opts.write_conf) {
				MotionConfig::Save(opts);
				if (has_clip_data_) {
					MotionConfig::SaveClip(clip_options_);
				}
			}

			// 使用 DataHandler 解析追踪数据
			// 对应 MoonScript: unless config.main.data or config.clip.data then windowError
			// 允许仅提供 clip 数据（clip-only 追踪场景）
			std::string raw = data_text->GetValue().ToStdString();
			bool has_main_data = false;
			if (!raw.empty()) {
				if (!result.main_data.best_effort_parse(raw, script_res_x, script_res_y)) {
					wxMessageBox(
						_("Failed to parse tracking data. Please check the data format or file path."),
						_("Error"), wxICON_ERROR
					);
					return;
				}
				has_main_data = true;
			}

			// 处理独立 clip 数据
			// 对应 MoonScript prepareConfig 中 clip section 的解析流程
			bool has_clip = false;
			if (has_clip_data_ && !clip_data_text_.empty()) {
				if (result.clip_data.best_effort_parse(clip_data_text_, script_res_x, script_res_y)) {
					has_clip = true;
					result.has_clip_data = true;
					result.clip_options = clip_options_;
					// 对应 MoonScript: if configField.rcToVc then rectClip=true, vectClip=true
					if (result.clip_options.rc_to_vc) {
						result.clip_options.rect_clip = true;
						result.clip_options.vect_clip = true;
					}
				}
			}

			// 主数据和 clip 数据都为空时报错
			if (!has_main_data && !has_clip) {
				wxMessageBox(
					_("No tracking data provided."),
					_("Error"), wxICON_ERROR
				);
				return;
			}

			// 仅有 clip 数据时，禁用所有主追踪选项，强制 clip-only 模式
			if (!has_main_data && has_clip) {
				opts.x_position = false;
				opts.y_position = false;
				opts.origin = false;
				opts.x_scale = false;
				opts.border = false;
				opts.shadow = false;
				opts.blur = false;
				opts.x_rotation = false;
				opts.y_rotation = false;
				opts.z_rotation = false;
				opts.z_position = false;
				opts.clip_only = true;
			}

			result.script_res_x = script_res_x;
			result.script_res_y = script_res_y;
			result.accepted = true;

			d.EndModal(wxID_OK);
		}

		void MotionDialogImpl::UpdateDependencies() {
			// 集中管理所有控件联动逻辑
			// 对应 MoonScript ConfigHandler 中各选项之间的依赖关系

			bool scale_on = chk_scale->IsChecked();
			bool blur_on = chk_blur->IsChecked();
			bool rc_to_vc = chk_rc_to_vc->IsChecked();
			bool clip_only = chk_clip_only->IsChecked();
			bool relative = chk_relative->IsChecked();

			// 缩放关闭时，禁用边框/阴影/模糊/倍率
			// 对应 MoonScript: border/shadow/blur 依赖 scale
			chk_border->Enable(scale_on);
			chk_shadow->Enable(scale_on);
			chk_blur->Enable(scale_on);
			txt_blur_scale->Enable(scale_on && blur_on);
			lbl_blur_scale->Enable(scale_on && blur_on);

			// 矩形裁剪转矢量裁剪时，强制勾选两个裁剪选项
			// 对应 MoonScript: rcToVc → rectClip=true, vectClip=true
			if (rc_to_vc) {
				chk_rect_clip->SetValue(true);
				chk_vect_clip->SetValue(true);
				chk_rect_clip->Enable(false);
				chk_vect_clip->Enable(false);
			} else {
				chk_rect_clip->Enable(true);
				chk_vect_clip->Enable(true);
			}

			// 仅裁剪模式：禁用位置/缩放/旋转等控件
			chk_x_pos->Enable(!clip_only);
			chk_y_pos->Enable(!clip_only);
			chk_origin->Enable(!clip_only);
			chk_abs_pos->Enable(!clip_only);
			chk_scale->Enable(!clip_only);
			chk_x_rotation->Enable(!clip_only);
			chk_y_rotation->Enable(!clip_only);
			chk_z_rotation->Enable(!clip_only);
			chk_z_position->Enable(!clip_only);
			// 仅裁剪模式下也要级联禁用缩放子项
			if (clip_only) {
				chk_border->Enable(false);
				chk_shadow->Enable(false);
				chk_blur->Enable(false);
				txt_blur_scale->Enable(false);
				lbl_blur_scale->Enable(false);
			}

			// 相对/绝对模式切换：更新标签文字并转换帧号数值
			// - 相对模式：起始帧表示从追踪数据的第几帧开始（默认 1, 支持负数倒数）
			// - 绝对模式：起始帧表示视频的绝对帧号，处理时自动转换为相对帧号
			if (relative) {
				lbl_start_frame->SetLabel(_("Start Frame (relative):"));
			} else {
				lbl_start_frame->SetLabel(_("Start Frame (absolute):"));
			}

			// 检测相对/绝对模式是否发生了切换，若切换则转换帧号数值
			if (relative != last_relative_) {
				last_relative_ = relative;
				if (collection_start_frame_ > 0) {
					const int old_val = spin_start_frame->GetValue();
					if (relative) {
						// 绝对 → 相对: relative = absolute - collection_start + 1
						int new_val = old_val - collection_start_frame_ + 1;
						if (new_val < 1) new_val = 1;
						spin_start_frame->SetValue(new_val);
					} else {
						// 相对 → 绝对: absolute = relative + collection_start - 1
						// 负数和 0 视为从第一帧开始
						const int effective_rel = (old_val <= 0) ? 1 : old_val;
						const int new_val = effective_rel + collection_start_frame_ - 1;
						spin_start_frame->SetValue(new_val);
					}
				}
			}
		}

		void MotionDialogImpl::OnCancel(wxCommandEvent &) {
			result.accepted = false;
			d.EndModal(wxID_CANCEL);
		}

		void MotionDialogImpl::OnWriteConfChanged(wxCommandEvent &) {
			// 保存配置勾选状态变化时，即时持久化当前所有选项
			// 确保 writeConf 本身的勾选/取消状态也被记住
			MotionOptions opts;
			GatherCurrentOptions(opts);
			MotionConfig::Save(opts);
		}

		void MotionDialogImpl::OnPaste(wxCommandEvent &) {
			if (wxTheClipboard->Open()) {
				if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
					wxTextDataObject data;
					wxTheClipboard->GetData(data);
					data_text->SetValue(data.GetText());
				}
				wxTheClipboard->Close();
			}
			// 粘贴后立即更新数据状态
			UpdateDataStatus();
		}

		void MotionDialogImpl::OnActivate(wxActivateEvent &event) {
			// 对话框获得焦点时自动获取剪切板内容到追踪数据输入框
			// 每次获得前台焦点都更新，方便用户在 Mocha 和 Aegisub 之间切换
			if (event.GetActive()) {
				if (wxTheClipboard->Open()) {
					if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
						wxTextDataObject clipboard_data;
						wxTheClipboard->GetData(clipboard_data);
						const wxString text = clipboard_data.GetText();
						// 仅当剪切板内容与当前不同时才更新，避免不必要的重解析
						if (!text.IsEmpty() && text != data_text->GetValue()) {
							data_text->SetValue(text);
							UpdateDataStatus();
						}
					}
					wxTheClipboard->Close();
				}
			}
			event.Skip();
		}

		void MotionDialogImpl::OnClipSeparate(wxCommandEvent &) {
			// 对应 MoonScript 的 clip 对话框
			// 弹出独立对话框，让用户提供独立的 clip 追踪数据和选项

			wxDialog clip_dlg(
				&d, wxID_ANY, _("Clip Tracking Data"),
				wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
			);

			auto *sizer = new wxBoxSizer(wxVERTICAL);
			const int pad = clip_dlg.FromDIP(6);
			const int inner_pad = clip_dlg.FromDIP(4);

			// 数据输入区
			auto *data_box = new wxStaticBox(&clip_dlg, wxID_ANY, _("Clip Tracking Data"));
			auto *data_sizer = new wxStaticBoxSizer(data_box, wxVERTICAL);
			auto *data_label = new wxStaticText(
				&clip_dlg, wxID_ANY,
				_("Paste clip tracking data or enter file path:")
			);
			auto *clip_text = new wxTextCtrl(
				&clip_dlg, wxID_ANY,
				wxString::FromUTF8(clip_data_text_),
				wxDefaultPosition, wxSize(-1, clip_dlg.FromDIP(100)), wxTE_MULTILINE
			);
			clip_text->SetToolTip(
				_(
					"Tracking data for clips, independent from main tracking data. "
					"Supports AE keyframe and Shake Rotoshape formats."
				)
			);
			// clip 数据状态标签：实时显示解析结果和帧数匹配信息
			auto *clip_lbl_status = new wxStaticText(&clip_dlg, wxID_ANY, _("No data loaded"));
			data_sizer->Add(data_label, 0, wxLEFT | wxRIGHT | wxTOP, inner_pad);
			data_sizer->Add(clip_text, 1, wxEXPAND | wxALL, inner_pad);
			data_sizer->Add(clip_lbl_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, inner_pad);

			// 数据通道选项
			auto *opt_box = new wxStaticBox(&clip_dlg, wxID_ANY, _("Data to apply:"));
			auto *opt_sizer = new wxStaticBoxSizer(opt_box, wxHORIZONTAL);

			auto *chk_cx = new wxCheckBox(&clip_dlg, wxID_ANY, _("X"));
			chk_cx->SetValue(clip_options_.x_position);
			chk_cx->SetToolTip(_("Apply X position data to clip"));

			auto *chk_cy = new wxCheckBox(&clip_dlg, wxID_ANY, _("Y"));
			chk_cy->SetValue(clip_options_.y_position);
			chk_cy->SetToolTip(_("Apply Y position data to clip"));

			auto *chk_cs = new wxCheckBox(&clip_dlg, wxID_ANY, _("Scale"));
			chk_cs->SetValue(clip_options_.x_scale);
			chk_cs->SetToolTip(_("Apply scale data to clip"));

			auto *chk_cr = new wxCheckBox(&clip_dlg, wxID_ANY, _("Z Rotation"));
			chk_cr->SetValue(clip_options_.z_rotation);
			chk_cr->SetToolTip(_("Apply Z-axis rotation data to clip"));

			opt_sizer->Add(chk_cx, 0, wxALL, inner_pad);
			opt_sizer->Add(chk_cy, 0, wxALL, inner_pad);
			opt_sizer->Add(chk_cs, 0, wxALL, inner_pad);
			opt_sizer->Add(chk_cr, 0, wxALL, inner_pad);

			// clip 类型选项
			auto *type_box = new wxStaticBox(&clip_dlg, wxID_ANY, _("Clip Type:"));
			auto *type_sizer = new wxStaticBoxSizer(type_box, wxHORIZONTAL);

			auto *chk_crc = new wxCheckBox(&clip_dlg, wxID_ANY, _("Rect Clip"));
			chk_crc->SetValue(clip_options_.rect_clip);

			auto *chk_cvc = new wxCheckBox(&clip_dlg, wxID_ANY, _("Vect Clip"));
			chk_cvc->SetValue(clip_options_.vect_clip);

			auto *chk_crv = new wxCheckBox(&clip_dlg, wxID_ANY, _("R->V"));
			chk_crv->SetValue(clip_options_.rc_to_vc);
			chk_crv->SetToolTip(_("Convert rectangular clip to vector clip"));

			type_sizer->Add(chk_crc, 0, wxALL, inner_pad);
			type_sizer->Add(chk_cvc, 0, wxALL, inner_pad);
			type_sizer->Add(chk_crv, 0, wxALL, inner_pad);

			// 起始帧
			auto *frame_sizer = new wxBoxSizer(wxHORIZONTAL);
			auto *chk_crel = new wxCheckBox(&clip_dlg, wxID_ANY, _("Relative"));
			chk_crel->SetValue(clip_options_.relative);

			auto *lbl_csf = new wxStaticText(&clip_dlg, wxID_ANY, _("Start Frame:"));
			auto *spin_csf = new wxSpinCtrl(
				&clip_dlg, wxID_ANY,
				wxString::Format("%d", clip_options_.start_frame),
				wxDefaultPosition, wxSize(clip_dlg.FromDIP(70), -1),
				wxSP_ARROW_KEYS, -99999, 99999, clip_options_.start_frame
			);

			frame_sizer->Add(chk_crel, 0, wxALL, inner_pad);
			frame_sizer->Add(lbl_csf, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, clip_dlg.FromDIP(8));
			frame_sizer->Add(spin_csf, 0, wxALL, inner_pad);

			// 按钮
			auto *btn_row = new wxBoxSizer(wxHORIZONTAL);
			auto *btn_clear = new wxButton(&clip_dlg, wxID_CLEAR, _("Clear Data"));
			btn_clear->SetToolTip(_("Remove independent clip tracking data"));
			auto *btn_ok = new wxButton(&clip_dlg, wxID_OK, _("OK"));
			auto *btn_cancel = new wxButton(&clip_dlg, wxID_CANCEL, _("Cancel"));
			btn_row->Add(btn_clear, 0, wxALL, inner_pad);
			btn_row->AddStretchSpacer();
			btn_row->Add(btn_ok, 0, wxALL, inner_pad);
			btn_row->Add(btn_cancel, 0, wxALL, inner_pad);

			sizer->Add(data_sizer, 1, wxEXPAND | wxALL, pad);
			sizer->Add(opt_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, pad);
			sizer->Add(type_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			sizer->Add(frame_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			sizer->Add(btn_row, 0, wxEXPAND | wxALL, pad);

			clip_dlg.SetSizerAndFit(sizer);
			clip_dlg.SetSize(clip_dlg.FromDIP(wxSize(420, -1)));
			clip_dlg.CentreOnParent();

			// rcToVc 联动
			auto update_clip_deps = [&]() {
				if (chk_crv->IsChecked()) {
					chk_crc->SetValue(true);
					chk_cvc->SetValue(true);
					chk_crc->Enable(false);
					chk_cvc->Enable(false);
				} else {
					chk_crc->Enable(true);
					chk_cvc->Enable(true);
				}
			};
			chk_crv->Bind(wxEVT_CHECKBOX, [&](wxCommandEvent &) { update_clip_deps(); });
			update_clip_deps();

			// 实时数据验证：文本变化时解析数据并更新状态标签
			// 与主对话框 UpdateDataStatus() 功能一致
			auto update_clip_status = [&]() {
				std::string raw = clip_text->GetValue().ToStdString();
				if (raw.empty()) {
					clip_lbl_status->SetForegroundColour(
						wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)
					);
					clip_lbl_status->SetLabel(_("No data loaded"));
					clip_lbl_status->Refresh();
					return;
				}

				DataHandler temp;
				if (!temp.best_effort_parse(raw, script_res_x, script_res_y)) {
					clip_lbl_status->SetForegroundColour(*wxRED);
					clip_lbl_status->SetLabel(_("Invalid data format or file path"));
					clip_lbl_status->Refresh();
					return;
				}

				// 与选中行帧数比较
				AssDialogue *active = ctx->selectionController->GetActiveLine();
				if (active && ctx->project->VideoProvider()) {
					int sf = ctx->videoController->FrameAtTime(active->Start, agi::vfr::START);
					int ef = ctx->videoController->FrameAtTime(active->End, agi::vfr::END);
					int needed = ef - sf;

					wxString msg = wxString::Format(
						_("Data frames: %d | Line needs: %d frames | Source: %dx%d"),
						temp.length(), needed, temp.source_width(), temp.source_height()
					);

					if (temp.length() == needed) {
						clip_lbl_status->SetForegroundColour(wxColour(0, 128, 0));
					} else {
						clip_lbl_status->SetForegroundColour(*wxRED);
					}
					clip_lbl_status->SetLabel(msg);
				} else {
					wxString msg = wxString::Format(
						_("Data frames: %d | Source: %dx%d"),
						temp.length(), temp.source_width(), temp.source_height()
					);
					clip_lbl_status->SetForegroundColour(
						wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)
					);
					clip_lbl_status->SetLabel(msg);
				}
				clip_lbl_status->Refresh();
			};

			clip_text->Bind(wxEVT_TEXT, [&](wxCommandEvent &) { update_clip_status(); });

			// 自动粘贴剪贴板：当没有已有数据时，自动将剪贴板内容填入
			// 与主对话框的 OnPaste() 行为一致
			if (clip_data_text_.empty()) {
				if (wxTheClipboard->Open()) {
					if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
						wxTextDataObject cb_data;
						wxTheClipboard->GetData(cb_data);
						clip_text->SetValue(cb_data.GetText());
					}
					wxTheClipboard->Close();
				}
			}
			// 初始状态更新
			update_clip_status();

			// 清除数据按钮
			clip_dlg.Bind(
				wxEVT_BUTTON, [&](wxCommandEvent &) {
					clip_text->Clear();
					has_clip_data_ = false;
					clip_data_text_.clear();
					lbl_clip_status->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
					lbl_clip_status->SetLabel(_("No separate clip data"));
					lbl_clip_status->Refresh();
					clip_dlg.EndModal(wxID_CLEAR);
				}, wxID_CLEAR
			);

			int ret = clip_dlg.ShowModal();

			if (ret == wxID_OK) {
				// 收集 clip 选项
				clip_options_.x_position = chk_cx->IsChecked();
				clip_options_.y_position = chk_cy->IsChecked();
				clip_options_.x_scale = chk_cs->IsChecked();
				clip_options_.z_rotation = chk_cr->IsChecked();
				clip_options_.rect_clip = chk_crc->IsChecked();
				clip_options_.vect_clip = chk_cvc->IsChecked();
				clip_options_.rc_to_vc = chk_crv->IsChecked();
				clip_options_.start_frame = spin_csf->GetValue();
				clip_options_.relative = chk_crel->IsChecked();

				clip_data_text_ = clip_text->GetValue().ToStdString();

				if (!clip_data_text_.empty()) {
					// 尝试解析验证
					DataHandler temp;
					if (temp.best_effort_parse(clip_data_text_, script_res_x, script_res_y)) {
						has_clip_data_ = true;
						wxString type_str = temp.is_srs() ? "SRS" : "TSR";
						lbl_clip_status->SetForegroundColour(wxColour(0, 128, 0));
						lbl_clip_status->SetLabel(
							wxString::Format(
								_("Clip data loaded: %d frames (%s)"), temp.length(), type_str
							)
						);
					} else {
						has_clip_data_ = false;
						lbl_clip_status->SetForegroundColour(*wxRED);
						lbl_clip_status->SetLabel(_("Clip data invalid"));
					}
				} else {
					has_clip_data_ = false;
					lbl_clip_status->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
					lbl_clip_status->SetLabel(_("No separate clip data"));
				}
				lbl_clip_status->Refresh();

				// 如果主对话框 writeConf 已勾选，则立即持久化 clip 配置
				// 避免用户设置 clip 选项后主对话框取消导致配置丢失
				if (chk_write_conf->IsChecked()) {
					MotionConfig::SaveClip(clip_options_);
				}
			}
		}

		void MotionDialogImpl::UpdateDataStatus() {
			// 实时解析追踪数据并与选中行帧数进行比较
			// 当文本框内容变化时自动触发，帮助用户在应用前检查数据有效性
			std::string raw = data_text->GetValue().ToStdString();
			if (raw.empty()) {
				lbl_status->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
				lbl_status->SetLabel(_("No data loaded"));
				lbl_status->Refresh();
				return;
			}

			// 尝试解析追踪数据（自动检测格式：TSR/SRS/文件路径）
			DataHandler temp;
			if (!temp.best_effort_parse(raw, script_res_x, script_res_y)) {
				lbl_status->SetForegroundColour(*wxRED);
				lbl_status->SetLabel(_("Invalid data format or file path"));
				lbl_status->Refresh();
				return;
			}

			// 检查视频是否已加载，以计算选中行帧数
			const AssDialogue *active = ctx->selectionController->GetActiveLine();
			if (active && ctx->project->VideoProvider()) {
				// 计算选中行的起止帧和所需帧数
				const int sf = ctx->videoController->FrameAtTime(active->Start, agi::vfr::START);
				const int ef = ctx->videoController->FrameAtTime(active->End, agi::vfr::START);
				// 帧数计算对应 MoonScript: totalFrames = endFrame - startFrame
				// MoonScript frame_from_ms 内部使用 agi::vfr::START 模式
				const int needed = ef - sf;

				const wxString msg = wxString::Format(
					_("Data frames: %d | Line needs: %d frames | Source: %dx%d"),
					temp.length(), needed, temp.source_width(), temp.source_height()
				);

				if (temp.length() == needed) {
					// 帧数匹配：显示绿色提示
					lbl_status->SetForegroundColour(wxColour(0, 128, 0));
				} else {
					// 帧数不匹配：显示红色警告
					lbl_status->SetForegroundColour(*wxRED);
				}
				lbl_status->SetLabel(msg);
			} else {
				// 无视频时只显示数据信息
				const wxString msg = wxString::Format(
					_("Data frames: %d | Source: %dx%d | FPS: %.2f"),
					temp.length(), temp.source_width(), temp.source_height(), temp.frame_rate()
				);
				lbl_status->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
				lbl_status->SetLabel(msg);
			}
			lbl_status->Refresh();
		}
	} // namespace

	MotionDialogResult ShowMotionDialog(agi::Context *c) {
		// 获取脚本分辨率
		int res_x = c->ass->GetScriptInfoAsInt("PlayResX");
		int res_y = c->ass->GetScriptInfoAsInt("PlayResY");
		if (res_x <= 0) res_x = 1920;
		if (res_y <= 0) res_y = 1080;

		MotionDialogImpl dlg(c, res_x, res_y);
		dlg.d.ShowModal();
		return dlg.result;
	}
} // namespace mocha
