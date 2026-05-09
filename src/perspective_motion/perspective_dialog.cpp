// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪对话框实现
// 对应 MoonScript 版 arch.PerspectiveMotion 的 main_dialog()
// 布局参照 motion_dialog.cpp

#include "perspective_dialog.h"
#include "perspective_data.h"
#include "perspective_config.h"

#include <aegisub/context.h>
#include "../selection_controller.h"
#include "../video_controller.h"
#include "../project.h"
#include "../async_video_provider.h"
#include "../ass_dialogue.h"
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
#include "../theme.h"

namespace mocha {
	namespace {
		struct PerspDialogImpl {
			explicit PerspDialogImpl(agi::Context *c);

			void OnOK(wxCommandEvent &);

			void OnPaste(wxCommandEvent &);

			void OnActivate(wxActivateEvent &);

			void OnWriteConfChanged(wxCommandEvent &);

			void UpdateDataStatus();

			void UpdateStartFrameLabel();

			void UpdateDependencies();

			wxDialog d;
			agi::Context *ctx;

			// 数据区
			wxTextCtrl *data_text = nullptr;
			wxStaticText *lbl_data_status = nullptr;

			// 追踪选项
			wxCheckBox *chk_track_pos = nullptr;
			wxCheckBox *chk_track_clip = nullptr;
			wxCheckBox *chk_track_bord_shad = nullptr;
			wxCheckBox *chk_apply_perspective = nullptr;

			// Org 模式
			wxRadioBox *radio_org_mode = nullptr;

			// 参考帧
			wxSpinCtrl *spin_relframe = nullptr;

			// 配置选项
			wxCheckBox *chk_relative = nullptr;
			wxSpinCtrl *spin_start_frame = nullptr;
			wxStaticText *lbl_start_frame = nullptr;
			wxCheckBox *chk_preview = nullptr;
			wxCheckBox *chk_reverse = nullptr;
			wxCheckBox *chk_write_conf = nullptr;

			// 结果
			PerspectiveDialogResult result;

			// 选中行帧范围（用于相对/绝对互转、反向追踪、参考帧计算）
			int collection_start_frame_ = 0;
			int collection_end_frame_ = 0;
			bool last_relative_ = true;
			bool last_reverse_ = false;
			// 正向追踪时的默认参考帧号（当前视频帧对应的相对帧号）
			int default_relframe_ = 1;
		};

		PerspDialogImpl::PerspDialogImpl(agi::Context *c)
			: d(
				c->parent, wxID_ANY, _("Apply Perspective Motion"),
				wxDefaultPosition, wxDefaultSize,
				wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER
			)
			, ctx(c) {
			wxToolTip::SetAutoPop(32767);
			wxToolTip::SetDelay(100);

			auto *main_sizer = new wxBoxSizer(wxVERTICAL);
			const int pad = d.FromDIP(6);
			const int inner_pad = d.FromDIP(4);
			const int compact_gap = d.FromDIP(4);

			// 计算选中行的帧范围
			auto selected_set = ctx->selectionController->GetSelectedSet();
			if (!selected_set.empty()) {
				collection_start_frame_ = INT_MAX;
				collection_end_frame_ = 0;
				for (const auto *line : selected_set) {
					int sf = ctx->videoController->FrameAtTime(line->Start, agi::vfr::START);
					int ef = ctx->videoController->FrameAtTime(line->End, agi::vfr::START);
					if (sf < collection_start_frame_) collection_start_frame_ = sf;
					if (ef > collection_end_frame_) collection_end_frame_ = ef;
				}
			}

			int selection_frames = collection_end_frame_ - collection_start_frame_;

			// 计算参考帧（当前视频帧对应的相对帧号）
			int current_video_frame = ctx->videoController->GetFrameN();
			int relframe = current_video_frame - collection_start_frame_ + 1;
			if (relframe < 1) relframe = 1;

			// ====== 数据输入区 ======
			auto *data_box = new wxStaticBox(&d, wxID_ANY, _("Tracking Data (CC Power Pin)"));
			auto *data_sizer = new wxStaticBoxSizer(data_box, wxVERTICAL);
			auto *data_label = new wxStaticText(
				&d, wxID_ANY,
				_("Paste AE CC Power Pin keyframe data:")
			);
			data_text = new wxTextCtrl(
				&d, wxID_ANY, "", wxDefaultPosition,
				wxSize(-1, d.FromDIP(100)), wxTE_MULTILINE
			);
			data_text->SetToolTip(_("Supports Adobe After Effects CC Power Pin corner pin tracking data."));
			lbl_data_status = new wxStaticText(&d, wxID_ANY, _("No data loaded"));
			data_sizer->Add(data_label, 0, wxLEFT | wxRIGHT | wxTOP, inner_pad);
			data_sizer->Add(data_text, 1, wxEXPAND | wxALL, inner_pad);
			data_sizer->Add(lbl_data_status, 0, wxLEFT | wxRIGHT | wxBOTTOM, inner_pad);

			// ====== 透视追踪选项 ======
			auto *track_box = new wxStaticBox(&d, wxID_ANY, _("Perspective Tracking"));
			auto *track_sizer = new wxStaticBoxSizer(track_box, wxVERTICAL);

			auto *track_opts_row1 = new wxBoxSizer(wxHORIZONTAL);
			chk_track_pos = new wxCheckBox(&d, wxID_ANY, _("Track Pos && Perspecti&ve"));
			chk_track_pos->SetValue(true);
			chk_track_pos->SetToolTip(_("Apply perspective tracking to \\pos and all transform tags."));
			chk_track_clip = new wxCheckBox(&d, wxID_ANY, _("Track &Clip"));
			chk_track_clip->SetValue(true);
			chk_track_clip->SetToolTip(_("Apply perspective mapping to \\clip coordinates."));
			chk_track_bord_shad = new wxCheckBox(&d, wxID_ANY, _("Scale &Border && Shadow"));
			chk_track_bord_shad->SetValue(true);
			chk_track_bord_shad->SetToolTip(_("Scale \\bord and \\shad with perspective scale ratio."));
			track_opts_row1->Add(chk_track_pos, 0, wxALL, inner_pad);
			track_opts_row1->Add(chk_track_clip, 0, wxALL, inner_pad);
			track_opts_row1->Add(chk_track_bord_shad, 0, wxALL, inner_pad);
			track_sizer->Add(track_opts_row1, 0, wxEXPAND);

			chk_apply_perspective = new wxCheckBox(&d, wxID_ANY, _("Apply perspective"));
			chk_apply_perspective->SetValue(true);
			chk_apply_perspective->SetToolTip(_("Pre-apply the reference frame's perspective to the reference line before tracking."));
			track_sizer->Add(chk_apply_perspective, 0, wxLEFT | wxRIGHT | wxBOTTOM, inner_pad);

			// 参考帧号
			{
				auto *relframe_row = new wxBoxSizer(wxHORIZONTAL);
				auto *lbl_relframe = new wxStaticText(&d, wxID_ANY, _("Reference frame:"));
				spin_relframe = new wxSpinCtrl(
					&d, wxID_ANY, "1",
					wxDefaultPosition, wxSize(d.FromDIP(80), -1),
					wxSP_ARROW_KEYS, 1, 9999999, 1
				);
				spin_relframe->SetToolTip(
					_("The 1-based frame index in the tracking data to use as the perspective reference.\n"
					  "When reverse tracking is enabled, this is automatically set to the last frame.")
				);
				relframe_row->Add(lbl_relframe, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
				relframe_row->Add(spin_relframe, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
				track_sizer->Add(relframe_row, 0, wxEXPAND);
			}

			// Org 模式
			const wxString org_choices[] = {
				_("Keep original"),
				_("Force center"),
				_("Try \\fax = 0"),
			};
			radio_org_mode = new wxRadioBox(
				&d, wxID_ANY, _("\\org Mode"),
				wxDefaultPosition, wxDefaultSize,
				3, org_choices, 0, wxRA_SPECIFY_COLS
			);
			radio_org_mode->SetToolTip(
				_(
					"How to determine the \\org anchor point.\n"
					"Keep original: use existing \\org value.\n"
					"Force center: set \\org to quad center.\n"
					"Try \\fax = 0: find \\org to minimize shear."
				)
			);
			radio_org_mode->SetSelection(1);
			track_sizer->Add(radio_org_mode, 0, wxALL, inner_pad);

			// ====== 配置选项 ======
			auto *config_box = new wxStaticBox(&d, wxID_ANY, _("Configuration"));
			auto *config_sizer = new wxStaticBoxSizer(config_box, wxVERTICAL);

			auto *cfg_row1 = new wxBoxSizer(wxHORIZONTAL);
			chk_relative = new wxCheckBox(&d, wxID_ANY, _("Relat&ive"));
			chk_relative->SetValue(true);
			chk_relative->SetToolTip(_("Relative: start frame is an index into tracking data (1=first). Absolute: start frame is a video frame number, auto-converted to relative."));
			lbl_start_frame = new wxStaticText(&d, wxID_ANY, _("Start Frame (relative):"));
			spin_start_frame = new wxSpinCtrl(
				&d, wxID_ANY, "1",
				wxDefaultPosition, wxSize(d.FromDIP(80), -1),
				wxSP_ARROW_KEYS, -9999999, 9999999, 1
			);
			spin_start_frame->SetToolTip(_("Relative mode: 1=first frame, -1=last frame, 0=auto-adjusted to 1.\nAbsolute mode: video frame number where tracking data starts."));
			cfg_row1->Add(chk_relative, 0, wxALIGN_CENTER_VERTICAL | wxALL, inner_pad);
			cfg_row1->Add(lbl_start_frame, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, compact_gap);
			cfg_row1->Add(spin_start_frame, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxTOP | wxBOTTOM, compact_gap);
			config_sizer->Add(cfg_row1, 0, wxEXPAND);

			auto *cfg_row2 = new wxBoxSizer(wxHORIZONTAL);
			chk_preview = new wxCheckBox(&d, wxID_ANY, _("Convenient preview"));
			chk_preview->SetValue(true);
			chk_preview->SetToolTip(_("Annotate original subtitle to preview tracking effect, then click [Play Current Line] to preview."));
			chk_reverse = new wxCheckBox(&d, wxID_ANY, _("Reverse tracking"));
			chk_reverse->SetToolTip(
				_("Reverse tracking: automatically set both the start frame and the reference frame to the last frame of the selected lines.\nWhen enabled, the reference perspective is taken from the end of the motion data.")
			);
			chk_write_conf = new wxCheckBox(&d, wxID_ANY, _("Save config"));
			chk_write_conf->SetValue(true);
			chk_write_conf->SetToolTip(_("Save current options to configuration file for next use."));
			cfg_row2->Add(chk_preview, 0, wxALL, inner_pad);
			cfg_row2->Add(chk_reverse, 0, wxALL, inner_pad);
			cfg_row2->Add(chk_write_conf, 0, wxALL, inner_pad);
			config_sizer->Add(cfg_row2, 0, wxEXPAND);

			// ====== 按钮 ======
			auto *btn_sizer = d.CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxAPPLY);
			btn_sizer->GetAffirmativeButton()->SetLabel(_("Apply"));
			btn_sizer->GetCancelButton()->SetLabel(_("Cancel"));
			btn_sizer->GetApplyButton()->SetLabel(_("Paste from Clipboard"));

			// ====== 组装总布局 ======
			main_sizer->Add(data_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(track_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(config_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
			main_sizer->Add(btn_sizer, 0, wxALIGN_RIGHT | wxALL, pad);

			d.SetSizerAndFit(main_sizer);

			// Ensure dialog is centered on screen for consistent UX
			d.CentreOnScreen();

			// 加载持久化配置
			PerspectiveConfig::Load(result.options);
			chk_track_pos->SetValue(result.options.track_pos);
			chk_track_clip->SetValue(result.options.track_clip);
			chk_track_bord_shad->SetValue(result.options.track_bord_shad);
			chk_apply_perspective->SetValue(result.options.apply_perspective);
			radio_org_mode->SetSelection(result.options.org_mode - 1);
			chk_relative->SetValue(result.options.relative);
			spin_start_frame->SetValue(result.options.start_frame);
			chk_preview->SetValue(result.options.preview);
			chk_reverse->SetValue(result.options.reverse_tracking);
			chk_write_conf->SetValue(result.options.write_conf);

			// 初始化参考帧号控件：范围 1..选中行帧数，值由当前视频帧自动计算
			spin_relframe->SetRange(1, std::max(1, selection_frames));
			spin_relframe->SetValue(relframe);
			result.options.relframe = relframe;
			default_relframe_ = relframe;

			// 自动粘贴剪贴板（初始化）
			wxCommandEvent evt;
			OnPaste(evt);

			// 在对话框获得焦点时尝试从剪贴板自动填充（仅当当前内容为空或无效时）
			d.Bind(wxEVT_ACTIVATE, &PerspDialogImpl::OnActivate, this);

			// 文本变化时实时验证数据并更新状态标签
			data_text->Bind(wxEVT_TEXT, [this](wxCommandEvent &) { UpdateDataStatus(); });

			// 事件绑定
			d.Bind(wxEVT_BUTTON, &PerspDialogImpl::OnOK, this, wxID_OK);
			d.Bind(wxEVT_BUTTON, &PerspDialogImpl::OnPaste, this, wxID_APPLY);
			d.Bind(wxEVT_CHECKBOX, &PerspDialogImpl::OnWriteConfChanged, this, chk_write_conf->GetId());

			// 相对/绝对和反向追踪通过统一 UpdateDependencies 联动
			chk_relative->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { UpdateDependencies(); });
			chk_reverse->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent &) { UpdateDependencies(); });

			last_relative_ = result.options.relative;
			// last_reverse_ 初始化为 false，使首次 UpdateDependencies()
			// 能检测到配置中已勾选的反向追踪并自动设置起始帧和参考帧
			last_reverse_ = false;

			// 初始联动状态（根据加载的配置设置标签和帧号）
			UpdateStartFrameLabel();
			UpdateDependencies();
		}

		void PerspDialogImpl::UpdateStartFrameLabel() {
			if (chk_relative->GetValue()) {
				lbl_start_frame->SetLabel(_("Start Frame (relative):"));
			} else {
				lbl_start_frame->SetLabel(_("Start Frame (absolute):"));
			}
		}

		void PerspDialogImpl::UpdateDataStatus() {
			std::string content = data_text->GetValue().ToStdString();
			PerspectiveDataHandler handler;
			if (content.empty()) {
				lbl_data_status->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
				lbl_data_status->SetLabel(_("No data loaded"));
				lbl_data_status->Refresh();
				return;
			}

			if (!handler.BestEffortParse(content)) {
				lbl_data_status->SetForegroundColour(GetSemanticErrorColour());
				lbl_data_status->SetLabel(_("Invalid data format or file path"));
				lbl_data_status->Refresh();
				return;
			}

			// 有视频时显示详细的帧数比较和视频分辨率信息
			if (collection_end_frame_ > collection_start_frame_ && ctx->project->VideoProvider()) {
				const int needed = collection_end_frame_ - collection_start_frame_;
				int video_w = ctx->project->VideoProvider()->GetWidth();
				int video_h = ctx->project->VideoProvider()->GetHeight();
				wxString msg = wxString::Format(
					_("Data frames: %d | Line needs: %d frames | Video: %dx%d"),
					handler.Length(), needed, video_w, video_h
				);
				if (handler.Length() == needed) {
					lbl_data_status->SetForegroundColour(GetSemanticSuccessColour());
				} else {
					lbl_data_status->SetForegroundColour(GetSemanticErrorColour());
				}
				lbl_data_status->SetLabel(msg);
			} else {
				lbl_data_status->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT));
				lbl_data_status->SetLabel(wxString::Format(_("Data frames: %d"), handler.Length()));
			}
			lbl_data_status->Refresh();
		}

		void PerspDialogImpl::OnPaste(wxCommandEvent &) {
			if (!wxTheClipboard->Open())
				return;
			if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
				wxTextDataObject data;
				wxTheClipboard->GetData(data);
				data_text->SetValue(data.GetText());
				UpdateDataStatus();
			}
			wxTheClipboard->Close();
		}

		void PerspDialogImpl::OnActivate(wxActivateEvent &event) {
			if (event.GetActive()) {
				bool should_auto_paste = false;
				const wxString current = data_text->GetValue();
				if (current.IsEmpty()) {
					should_auto_paste = true;
				} else {
					PerspectiveDataHandler temp;
					if (!temp.BestEffortParse(current.ToStdString())) {
						should_auto_paste = true;
					} else if (collection_end_frame_ > collection_start_frame_) {
						// 数据有效但帧数不匹配时也自动获取剪贴板
						const int needed = collection_end_frame_ - collection_start_frame_;
						if (temp.Length() != needed) {
							should_auto_paste = true;
						}
					}
				}
				if (should_auto_paste) {
					if (wxTheClipboard->Open()) {
						if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
							wxTextDataObject clipboard_data;
							wxTheClipboard->GetData(clipboard_data);
							const wxString text = clipboard_data.GetText();
							if (!text.IsEmpty() && text != current) {
								data_text->SetValue(text);
								UpdateDataStatus();
							}
						}
						wxTheClipboard->Close();
					}
				}
			}
			event.Skip();
		}

		void PerspDialogImpl::UpdateDependencies() {
			// 集中管理所有控件联动逻辑
			// 参照 mocha 的 motion_dialog.cpp UpdateDependencies()

			bool relative = chk_relative->GetValue();
			bool reverse = chk_reverse->GetValue();

			// 更新起始帧标签文字
			if (relative) {
				lbl_start_frame->SetLabel(_("Start Frame (relative):"));
			} else {
				lbl_start_frame->SetLabel(_("Start Frame (absolute):"));
			}

			// 检测相对/绝对模式是否发生了切换，切换时转换帧号数值
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
						const int effective_rel = (old_val <= 0) ? 1 : old_val;
						const int new_val = effective_rel + collection_start_frame_ - 1;
						spin_start_frame->SetValue(new_val);
					}
				}
			}

			// 检测反向追踪是否发生了切换，切换时调整起始帧和参考帧
			if (reverse != last_reverse_) {
				last_reverse_ = reverse;
				if (collection_end_frame_ > collection_start_frame_) {
					const int total_frames = collection_end_frame_ - collection_start_frame_;
					const int sel_frames = std::max(1, total_frames);
					if (reverse) {
						// 反向追踪：起始帧设为选中行最后一帧
						if (relative) {
							spin_start_frame->SetValue(std::max(1, total_frames));
						} else {
							int last_abs = collection_end_frame_ - 1;
							if (last_abs < collection_start_frame_) last_abs = collection_start_frame_;
							spin_start_frame->SetValue(last_abs);
						}
						// 参考帧也设为末帧（追踪数据中最后一帧）
						spin_relframe->SetRange(1, sel_frames);
						spin_relframe->SetValue(std::max(1, total_frames));
					} else {
						// 正向追踪：起始帧恢复为第一帧
						if (relative) {
							spin_start_frame->SetValue(1);
						} else {
							spin_start_frame->SetValue(collection_start_frame_);
						}
						// 参考帧恢复为当前视频帧位置
						spin_relframe->SetRange(1, sel_frames);
						int restored = default_relframe_;
						if (restored < 1) restored = 1;
						if (restored > sel_frames) restored = sel_frames;
						spin_relframe->SetValue(restored);
					}
				}
			}
		}

		void PerspDialogImpl::OnWriteConfChanged(wxCommandEvent &) {
			if (chk_write_conf->GetValue()) {
				PerspectiveOptions opts;
				opts.track_pos = chk_track_pos->GetValue();
				opts.track_clip = chk_track_clip->GetValue();
				opts.track_bord_shad = chk_track_bord_shad->GetValue();
				opts.apply_perspective = chk_apply_perspective->GetValue();
				opts.relframe = spin_relframe->GetValue();
				opts.org_mode = radio_org_mode->GetSelection() + 1;
				opts.relative = chk_relative->GetValue();
				opts.start_frame = spin_start_frame->GetValue();
				opts.preview = chk_preview->GetValue();
				opts.reverse_tracking = chk_reverse->GetValue();
				opts.write_conf = true;
				PerspectiveConfig::Save(opts);
			}
		}

		void PerspDialogImpl::OnOK(wxCommandEvent &) {
			result.options.track_pos = chk_track_pos->GetValue();
			result.options.track_clip = chk_track_clip->GetValue();
			result.options.track_bord_shad = chk_track_bord_shad->GetValue();
			result.options.apply_perspective = chk_apply_perspective->GetValue();
			result.options.relframe = spin_relframe->GetValue();
			result.options.org_mode = radio_org_mode->GetSelection() + 1;
			result.options.relative = chk_relative->GetValue();
			result.options.start_frame = spin_start_frame->GetValue();
			result.options.preview = chk_preview->GetValue();
			result.options.reverse_tracking = chk_reverse->GetValue();
			result.options.write_conf = chk_write_conf->GetValue();
			result.raw_data = data_text->GetValue().ToStdString();

			// 验证追踪数据是否存在且可解析
			if (result.raw_data.empty()) {
				wxMessageBox(
					_("No tracking data provided."),
					_("Error"), wxICON_ERROR
				);
				return;
			}

			PerspectiveDataHandler handler;
			if (!handler.BestEffortParse(result.raw_data)) {
				wxMessageBox(
					_("Failed to parse tracking data. Please check the data format or file path."),
					_("Error"), wxICON_ERROR
				);
				return;
			}

			// 检测追踪数据与选中行帧数的匹配度
			// 不匹配时自动从剪贴板获取新数据重试（参照 mocha OnActivate 模式）
			// 静默重试，不弹窗；最终错误由 subtitle.cpp 统一报告
			if (collection_end_frame_ > collection_start_frame_) {
				const int needed = collection_end_frame_ - collection_start_frame_;
				if (handler.Length() != needed) {
					if (wxTheClipboard->Open()) {
						if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
							wxTextDataObject clipboard_data;
							wxTheClipboard->GetData(clipboard_data);
							const wxString clip_text = clipboard_data.GetText();
							if (!clip_text.IsEmpty() && clip_text != wxString::FromUTF8(result.raw_data)) {
								PerspectiveDataHandler handler2;
								if (handler2.BestEffortParse(clip_text.ToStdString()) && handler2.Length() == needed) {
									result.raw_data = clip_text.ToStdString();
									data_text->SetValue(clip_text);
									UpdateDataStatus();
								}
							}
						}
						wxTheClipboard->Close();
					}
				}
			}

			if (result.options.write_conf)
				PerspectiveConfig::Save(result.options);

			result.accepted = true;

			d.EndModal(wxID_OK);
		}
	} // anonymous namespace

	PerspectiveDialogResult ShowPerspectiveDialog(agi::Context *c) {
		PerspDialogImpl impl(c);
		if (impl.d.ShowModal() != wxID_OK) {
			impl.result.accepted = false;
			return impl.result;
		}
		return impl.result;
	}
} // namespace mocha
