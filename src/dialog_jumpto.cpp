// Copyright (c) 2005, Rodrigo Braz Monteiro
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

#include <ass_dialogue.h>
#include <ass_file.h>
#include <dialog_manager.h>
#include <initial_line_state.h>
#include <selection_controller.h>

#include "async_video_provider.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "project.h"
#include "timeedit_ctrl.h"
#include "validators.h"
#include "video_controller.h"
#include "video_frame.h"
#include "video_out_gl.h"

#include <libaegisub/ass/time.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>

#include <wx/dcbuffer.h>
#include <wx/dialog.h>
#include <wx/image.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>
#include <wx/tglbtn.h>

long sFrame;
long eFrame;
int startTime;
int endTime;
bool onOK = false;
long onGifQuality;

/// 裁剪区域坐标（视频实际像素坐标）
int cropX = 0;
int cropY = 0;
int cropW = 0;
int cropH = 0;
bool hasCropRegion = false;

/// 帧序列导出结果
long seqStartFrame = 0;
long seqEndFrame = 0;
bool seqOnOK = false;

namespace {
	struct DialogJumpTo {
		wxDialog d;
		agi::Context *c; ///< Project context
		long jumpframe; ///< Target frame to jump to
		TimeEdit *JumpTime; ///< Target time edit control
		wxTextCtrl *JumpFrame; ///< Target frame edit control

		/// Enter/OK button handler
		void OnOK(wxCommandEvent &event);

		/// Update target frame on target time changed
		void OnEditTime(wxCommandEvent &event);

		/// Update target time on target frame changed
		void OnEditFrame(wxCommandEvent &event);

		/// Dialog initializer to set default focus and selection
		void OnInitDialog(wxInitDialogEvent &);

		explicit DialogJumpTo(agi::Context *c);
	};

	DialogJumpTo::DialogJumpTo(agi::Context *c)
		: d(c->parent, -1, _("Jump to"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxWANTS_CHARS)
		, c(c)
		, jumpframe(c->videoController->GetFrameN()) {
		d.SetIcon(GETICON(jumpto_button_16));

		auto LabelFrame = new wxStaticText(&d, -1, _("Frame: "));
		auto LabelTime = new wxStaticText(&d, -1, _("Time: "));

		JumpFrame = new wxTextCtrl(&d, -1, "", wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER, IntValidator((int) jumpframe));
		JumpFrame->SetMaxLength(std::to_string(c->project->VideoProvider()->GetFrameCount() - 1).size());
		JumpTime = new TimeEdit(&d, -1, c, agi::Time(c->videoController->TimeAtFrame(jumpframe)).GetAssFormatted(), wxDefaultSize);

		auto TimesSizer = new wxGridSizer(2, 5, 5);

		TimesSizer->Add(LabelFrame, 1, wxALIGN_CENTER_VERTICAL);
		TimesSizer->Add(JumpFrame, wxEXPAND);

		TimesSizer->Add(LabelTime, 1, wxALIGN_CENTER_VERTICAL);
		TimesSizer->Add(JumpTime, wxEXPAND);

		auto ButtonSizer = d.CreateStdDialogButtonSizer(wxOK | wxCANCEL);

		// General layout
		auto MainSizer = new wxBoxSizer(wxVERTICAL);
		MainSizer->Add(TimesSizer, 0, wxALL | wxALIGN_CENTER, 5);
		MainSizer->Add(ButtonSizer, 0, wxEXPAND | wxLEFT | wxBOTTOM | wxRIGHT, 5);
		d.SetSizerAndFit(MainSizer);
		d.CenterOnParent();

		d.Bind(wxEVT_INIT_DIALOG, &DialogJumpTo::OnInitDialog, this);
		d.Bind(wxEVT_TEXT_ENTER, &DialogJumpTo::OnOK, this);
		d.Bind(wxEVT_BUTTON, &DialogJumpTo::OnOK, this, wxID_OK);
		JumpTime->Bind(wxEVT_TEXT, &DialogJumpTo::OnEditTime, this);
		JumpFrame->Bind(wxEVT_TEXT, &DialogJumpTo::OnEditFrame, this);
	}

	void DialogJumpTo::OnInitDialog(wxInitDialogEvent &) {
		d.TransferDataToWindow();
		d.UpdateWindowUI(wxUPDATE_UI_RECURSE);

		// This can't simply be done in the constructor as the value hasn't been set yet
		JumpFrame->SetFocus();
		JumpFrame->SelectAll();
	}

	void DialogJumpTo::OnOK(wxCommandEvent &) {
		d.EndModal(0);
		c->videoController->JumpToFrame(jumpframe);
	}

	void DialogJumpTo::OnEditTime(wxCommandEvent &) {
		long newframe = c->videoController->FrameAtTime(JumpTime->GetTime());
		if (jumpframe != newframe) {
			jumpframe = newframe;
			JumpFrame->ChangeValue(fmt_wx("%d", jumpframe));
		}
	}

	void DialogJumpTo::OnEditFrame(wxCommandEvent &event) {
		JumpFrame->GetValue().ToLong(&jumpframe);
		JumpTime->SetTime(c->videoController->TimeAtFrame(jumpframe));
	}

	/// 裁剪区域选择面板
	/// 显示当前视频帧的缩放预览，允许用户鼠标拖拽选定矩形导出区域
	/// 支持循环播放预览：在指定帧范围内自动切换帧
	class CropSelectionPanel : public wxPanel {
	public:
		CropSelectionPanel(wxWindow *parent, agi::Context *c, wxWindowID id = wxID_ANY)
			: wxPanel(parent, id, wxDefaultPosition, wxSize(-1, -1), wxFULL_REPAINT_ON_RESIZE)
			, ctx_(c)
			, timer_(this)
			, hdr_sub_(OPT_SUB("Video/HDR/Tone Mapping", &CropSelectionPanel::OnHDROptionChanged, this)) {
			wxWindowBase::SetBackgroundStyle(wxBG_STYLE_PAINT);

			// 获取当前视频帧作为预览
			if (ctx_->project->VideoProvider()) {
				video_w_ = ctx_->project->VideoProvider()->GetWidth();
				video_h_ = ctx_->project->VideoProvider()->GetHeight();
				int frame = ctx_->videoController->GetFrameN();
				auto vf = ctx_->project->VideoProvider()->GetFrame(
					frame,
					ctx_->project->Timecodes().TimeAtFrame(frame), false
				);
				if (vf) {
					preview_ = GetImage(*vf);
					// 为预览图添加 ABB 黑边填充，使其与 video_w_/video_h_ 一致
					const int img_padding = (video_h_ - preview_.GetHeight()) / 2;
					if (img_padding > 0)
						preview_ = AddPaddingToImage(preview_, img_padding);
					// 保存原始帧数据（未应用HDR），用于HDR选项切换时重新派生
					raw_preview_ = preview_.Copy();
					// 启用HDR时对预览帧应用色调映射
					if (OPT_GET("Video/HDR/Tone Mapping")->GetBool())
						VideoOutGL::ApplyHDRLutToImage(preview_);
					has_preview_ = true;
				}
			}

			Bind(wxEVT_PAINT, &CropSelectionPanel::OnPaint, this);
			Bind(wxEVT_LEFT_DOWN, &CropSelectionPanel::OnMouseDown, this);
			Bind(wxEVT_MOTION, &CropSelectionPanel::OnMouseMove, this);
			Bind(wxEVT_LEFT_UP, &CropSelectionPanel::OnMouseUp, this);
			Bind(wxEVT_RIGHT_UP, &CropSelectionPanel::OnRightClick, this);
			Bind(wxEVT_TIMER, &CropSelectionPanel::OnTimer, this);
		}

		~CropSelectionPanel() {
			if (timer_.IsRunning()) timer_.Stop();
			CancelDecode();
		}

		/// 获取选定的裁剪区域（视频实际像素坐标）
		[[nodiscard]] wxRect GetCropRect() const { return crop_rect_; }
		[[nodiscard]] bool HasSelection() const { return has_selection_; }

		/// 设置循环播放帧范围
		void SetFrameRange(long start, long end) {
			bool range_changed = (start_frame_ != start || end_frame_ != end);
			start_frame_ = start;
			end_frame_ = end;
			current_frame_ = 0;
			// 帧范围变化时，若正在播放则重新解码
			if (range_changed && playing_) {
				CancelDecode();
				total_frames_ = static_cast<size_t>(end_frame_ - start_frame_ + 1);
				decoded_count_ = 0; {
					std::lock_guard<std::mutex> lock(cache_mutex_);
					frame_cache_.clear();
					frame_cache_.reserve(total_frames_);
				}
				cancel_decode_ = false;
				decode_thread_ = std::thread(&CropSelectionPanel::DecodeFrames, this);
			}
		}

		/// 切换播放/暂停状态
		void TogglePlayback() {
			if (playing_) {
				StopPlayback();
			} else {
				StartPlayback();
			}
		}

		/// 开始循环播放（异步解码 + 预缓存帧）
		void StartPlayback() {
			if (start_frame_ >= end_frame_) return;
			if (!ctx_->project->VideoProvider()) return;

			CancelDecode();

			playing_ = true;
			current_frame_ = 0;
			total_frames_ = static_cast<size_t>(end_frame_ - start_frame_ + 1);
			decoded_count_ = 0; {
				std::lock_guard<std::mutex> lock(cache_mutex_);
				frame_cache_.clear();
				frame_cache_.reserve(total_frames_);
			}

			cancel_decode_ = false;
			decode_thread_ = std::thread(&CropSelectionPanel::DecodeFrames, this);

			// 约 10fps 预览播放速率
			timer_.Start(100);
		}

		/// 停止播放
		void StopPlayback() {
			playing_ = false;
			timer_.Stop();
			CancelDecode();
		}

		[[nodiscard]] bool IsPlaying() const { return playing_; }

	private:
		/// 将面板坐标转换为视频像素坐标
		[[nodiscard]] wxPoint PanelToVideo(const wxPoint &pt) const {
			if (video_w_ == 0 || video_h_ == 0) return pt;
			wxSize panel_size = GetClientSize();
			double scale_x = static_cast<double>(video_w_) / panel_size.GetWidth();
			double scale_y = static_cast<double>(video_h_) / panel_size.GetHeight();
			double scale = std::max(scale_x, scale_y);
			int display_w = static_cast<int>(video_w_ / scale);
			int display_h = static_cast<int>(video_h_ / scale);
			int offset_x = (panel_size.GetWidth() - display_w) / 2;
			int offset_y = (panel_size.GetHeight() - display_h) / 2;
			int vx = static_cast<int>((pt.x - offset_x) * scale);
			int vy = static_cast<int>((pt.y - offset_y) * scale);
			vx = std::max(0, std::min(vx, video_w_));
			vy = std::max(0, std::min(vy, video_h_));
			return {vx, vy};
		}

		/// 将视频像素坐标转换为面板坐标
		[[nodiscard]] wxPoint VideoToPanel(const wxPoint &pt) const {
			if (video_w_ == 0 || video_h_ == 0) return pt;
			wxSize panel_size = GetClientSize();
			double scale_x = static_cast<double>(video_w_) / panel_size.GetWidth();
			double scale_y = static_cast<double>(video_h_) / panel_size.GetHeight();
			double scale = std::max(scale_x, scale_y);
			int display_w = static_cast<int>(video_w_ / scale);
			int display_h = static_cast<int>(video_h_ / scale);
			int offset_x = (panel_size.GetWidth() - display_w) / 2;
			int offset_y = (panel_size.GetHeight() - display_h) / 2;
			int px = static_cast<int>(pt.x / scale) + offset_x;
			int py = static_cast<int>(pt.y / scale) + offset_y;
			return {px, py};
		}

		void OnPaint(wxPaintEvent &) {
			wxAutoBufferedPaintDC dc(this);
			wxSize size = GetClientSize();
			dc.SetBackground(*wxBLACK_BRUSH);
			dc.Clear();

			if (has_preview_ && preview_.IsOk()) {
				// 按比例缩放预览图
				double scale_x = static_cast<double>(video_w_) / size.GetWidth();
				double scale_y = static_cast<double>(video_h_) / size.GetHeight();
				double scale = std::max(scale_x, scale_y);
				int display_w = static_cast<int>(video_w_ / scale);
				int display_h = static_cast<int>(video_h_ / scale);
				int offset_x = (size.GetWidth() - display_w) / 2;
				int offset_y = (size.GetHeight() - display_h) / 2;

				wxImage scaled = preview_.Scale(display_w, display_h, wxIMAGE_QUALITY_BILINEAR);
				dc.DrawBitmap(wxBitmap(scaled), offset_x, offset_y);
			} else {
				dc.SetTextForeground(*wxWHITE);
				dc.DrawText(_("No video preview"), 10, size.GetHeight() / 2 - 8);
			}

			// 绘制选区矩形
			if (is_dragging_ || has_selection_) {
				wxPoint p1, p2;
				if (is_dragging_) {
					p1 = drag_start_panel_;
					p2 = drag_current_panel_;
				} else {
					p1 = VideoToPanel(wxPoint(crop_rect_.x, crop_rect_.y));
					p2 = VideoToPanel(
						wxPoint(
							crop_rect_.x + crop_rect_.width,
							crop_rect_.y + crop_rect_.height
						)
					);
				}

				wxRect display_rect(
					std::min(p1.x, p2.x), std::min(p1.y, p2.y),
					std::abs(p2.x - p1.x), std::abs(p2.y - p1.y)
				);

				// 半透明遮罩（选区外变暗）
				dc.SetBrush(wxBrush(wxColour(0, 0, 0, 128)));
				dc.SetPen(*wxTRANSPARENT_PEN);
				// 上方
				dc.DrawRectangle(0, 0, size.GetWidth(), display_rect.y);
				// 下方
				dc.DrawRectangle(
					0, display_rect.GetBottom(), size.GetWidth(),
					size.GetHeight() - display_rect.GetBottom()
				);
				// 左侧
				dc.DrawRectangle(0, display_rect.y, display_rect.x, display_rect.height);
				// 右侧
				dc.DrawRectangle(
					display_rect.GetRight(), display_rect.y,
					size.GetWidth() - display_rect.GetRight(), display_rect.height
				);

				// 选区边框
				dc.SetPen(wxPen(*wxGREEN, 2, wxPENSTYLE_DOT));
				dc.SetBrush(*wxTRANSPARENT_BRUSH);
				dc.DrawRectangle(display_rect);

				// 显示选区尺寸标注
				if (has_selection_) {
					wxString info = wxString::Format("%dx%d", crop_rect_.width, crop_rect_.height);
					dc.SetTextForeground(*wxGREEN);
					dc.DrawText(info, display_rect.x + 4, display_rect.y + 4);
				}
			}
		}

		void OnMouseDown(wxMouseEvent &evt) {
			drag_start_panel_ = evt.GetPosition();
			drag_current_panel_ = evt.GetPosition();
			is_dragging_ = true;
			has_selection_ = false;
			CaptureMouse();
			Refresh();
		}

		void OnMouseMove(wxMouseEvent &evt) {
			if (!is_dragging_) return;
			drag_current_panel_ = evt.GetPosition();
			Refresh();
		}

		void OnMouseUp(wxMouseEvent &evt) {
			if (!is_dragging_) return;
			is_dragging_ = false;
			if (HasCapture()) ReleaseMouse();

			drag_current_panel_ = evt.GetPosition();

			// 计算视频坐标下的裁剪矩形
			wxPoint v1 = PanelToVideo(drag_start_panel_);
			wxPoint v2 = PanelToVideo(drag_current_panel_);

			int x1 = std::min(v1.x, v2.x);
			int y1 = std::min(v1.y, v2.y);
			int x2 = std::max(v1.x, v2.x);
			int y2 = std::max(v1.y, v2.y);

			// 最小选区检查（避免误点击）
			if (x2 - x1 > 4 && y2 - y1 > 4) {
				crop_rect_ = wxRect(x1, y1, x2 - x1, y2 - y1);
				has_selection_ = true;
			} else {
				has_selection_ = false;
			}

			Refresh();
		}

		/// 右键清除选区
		void OnRightClick(wxMouseEvent &) {
			has_selection_ = false;
			crop_rect_ = wxRect();
			Refresh();
		}

		/// 定时器回调：从预缓存中取帧并刷新预览
		void OnTimer(wxTimerEvent &) {
			if (!playing_) return;

			size_t count = decoded_count_.load();
			if (count == 0) return; // 等待首帧解码完成

			{
				std::lock_guard<std::mutex> lock(cache_mutex_);
				if (current_frame_ < frame_cache_.size()) {
					preview_ = frame_cache_[current_frame_];
					has_preview_ = true;
				}
			}

			Refresh();

			// 循环递增帧索引
			current_frame_++;
			if (current_frame_ >= count) {
				current_frame_ = 0;
			}
		}

		/// 后台线程：预解码帧范围内所有帧到缓存
		void DecodeFrames() {
			const bool hdr_enabled = OPT_GET("Video/HDR/Tone Mapping")->GetBool();
			for (size_t i = 0; i < total_frames_ && !cancel_decode_.load(); ++i) {
				long frame = start_frame_ + static_cast<long>(i);
				auto vf = ctx_->project->VideoProvider()->GetFrame(
					frame,
					ctx_->project->Timecodes().TimeAtFrame(frame), false
				);
				if (vf && !cancel_decode_.load()) {
					wxImage img = GetImage(*vf);
					// 为解码帧添加 ABB 黑边填充，保持与 video_w_/video_h_ 一致
					int decode_padding = (video_h_ - img.GetHeight()) / 2;
					if (decode_padding > 0)
						img = AddPaddingToImage(img, decode_padding);
					// 启用HDR时对解码帧应用色调映射
					if (hdr_enabled)
						VideoOutGL::ApplyHDRLutToImage(img);
					std::lock_guard<std::mutex> lock(cache_mutex_);
					frame_cache_.push_back(std::move(img));
					decoded_count_.store(frame_cache_.size());
				}
			}
		}

		/// 取消异步解码并等待线程结束
		void CancelDecode() {
			cancel_decode_.store(true);
			if (decode_thread_.joinable()) decode_thread_.join();
			cancel_decode_.store(false);
		}

		agi::Context *ctx_;
		wxImage preview_;
		bool has_preview_ = false;
		int video_w_ = 0;
		int video_h_ = 0;
		wxPoint drag_start_panel_;
		wxPoint drag_current_panel_;
		bool is_dragging_ = false;
		wxRect crop_rect_;
		bool has_selection_ = false;

		/// 循环播放相关
		wxTimer timer_;
		long start_frame_ = 0;
		long end_frame_ = 0;
		size_t current_frame_ = 0;
		bool playing_ = false;

		/// 异步解码预缓存
		std::vector<wxImage> frame_cache_;
		std::thread decode_thread_;
		std::atomic<bool> cancel_decode_{false};
		std::mutex cache_mutex_;
		std::atomic<size_t> decoded_count_{0};
		size_t total_frames_ = 0;

		/// HDR选项监听
		wxImage raw_preview_;            ///< 未应用HDR的原始预览帧
		agi::signal::Connection hdr_sub_; ///< HDR选项变化订阅

		/// HDR选项变化回调：重新派生预览帧并刷新播放缓存
		void OnHDROptionChanged(agi::OptionValue const& opt) {
			const bool hdr_on = opt.GetBool();
			// 非播放状态时：从原始帧重新派生预览
			if (!playing_ && has_preview_ && raw_preview_.IsOk()) {
				preview_ = raw_preview_.Copy();
				if (hdr_on)
					VideoOutGL::ApplyHDRLutToImage(preview_);
				Refresh();
			}
			// 播放状态时：重新解码以应用/移除HDR
			if (playing_) {
				CancelDecode();
				current_frame_ = 0;
				decoded_count_ = 0; {
					std::lock_guard<std::mutex> lock(cache_mutex_);
					frame_cache_.clear();
					frame_cache_.reserve(total_frames_);
				}
				cancel_decode_ = false;
				decode_thread_ = std::thread(&CropSelectionPanel::DecodeFrames, this);
			}
		}
	};

	struct DialogJumpFrameTo {
		wxDialog d;
		agi::Context *c; ///< Project context
		long startFrame; ///< 起始帧号
		wxTextCtrl *editStartFrame; ///< 起始帧编辑控件
		long endFrame; ///< 结束帧号
		wxTextCtrl *editEndFrame; ///< 结束帧编辑控件
		long gifQuality;
		wxSpinCtrl *editGifQuality;
		CropSelectionPanel *cropPanel; ///< 裁剪区域选择面板
		wxToggleButton *playBtn; ///< 循环播放控制按钮

		/// Enter/OK button handler
		void OnOK(wxCommandEvent &event);

		void OnCANCEL(wxCommandEvent &event);

		/// Update target time on target frame changed
		void OnEditStartFrame(wxCommandEvent &event);

		void OnEditEndFrame(wxCommandEvent &event);

		void OnEditGifQuality(wxCommandEvent &event);

		/// Dialog initializer to set default focus and selection
		void OnInitDialog(wxInitDialogEvent &);

		explicit DialogJumpFrameTo(agi::Context *c);
	};

	DialogJumpFrameTo::DialogJumpFrameTo(agi::Context *c)
		: d(c->parent, -1, _("Export GIF"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxWANTS_CHARS)
		, c(c)
		, startFrame(LONG_MAX)
		, endFrame(LONG_MIN)
		, gifQuality(90) {
		d.SetIcon(GETICON(jumpto_button_16));

		// 从选中行计算起止帧
		for (const auto line : c->selectionController->GetSelectedSet()) {
			const int currentStartFrame = c->videoController->FrameAtTime(line->Start, agi::vfr::START);
			const int currentEndFrame = c->videoController->FrameAtTime(line->End, agi::vfr::END);
			if (currentStartFrame < startFrame) {
				startFrame = currentStartFrame;
				startTime = line->Start;
			}
			if (currentEndFrame > endFrame) {
				endFrame = currentEndFrame;
				endTime = line->End;
			}
		}

		auto *main_sizer = new wxBoxSizer(wxVERTICAL);
		const int pad = d.FromDIP(6);
		const int inner_pad = d.FromDIP(4);

		// ====== 帧范围组 ======
		auto *range_box = new wxStaticBox(&d, wxID_ANY, _("Frame Range"));
		auto *range_sizer = new wxStaticBoxSizer(range_box, wxVERTICAL);

		auto *range_grid = new wxFlexGridSizer(2, inner_pad, inner_pad);
		range_grid->AddGrowableCol(1, 1);

		range_grid->Add(
			new wxStaticText(&d, -1, _("Start Frame:")),
			0, wxALIGN_CENTER_VERTICAL
		);
		editStartFrame = new wxTextCtrl(
			&d, -1, "", wxDefaultPosition, wxSize(-1, -1),
			wxTE_PROCESS_ENTER, IntValidator(static_cast<int>(startFrame))
		);
		editStartFrame->SetMaxLength(std::to_string(c->project->VideoProvider()->GetFrameCount() - 1).size());
		range_grid->Add(editStartFrame, 1, wxEXPAND);

		range_grid->Add(
			new wxStaticText(&d, -1, _("End Frame:")),
			0, wxALIGN_CENTER_VERTICAL
		);
		editEndFrame = new wxTextCtrl(
			&d, -1, "", wxDefaultPosition, wxSize(-1, -1),
			wxTE_PROCESS_ENTER, IntValidator(static_cast<int>(endFrame))
		);
		editEndFrame->SetMaxLength(std::to_string(c->project->VideoProvider()->GetFrameCount() - 1).size());
		range_grid->Add(editEndFrame, 1, wxEXPAND);

		range_grid->Add(
			new wxStaticText(&d, -1, _("GIF Quality:")),
			0, wxALIGN_CENTER_VERTICAL
		);
		editGifQuality = new wxSpinCtrl(
			&d, -1, "", wxDefaultPosition, wxSize(-1, -1),
			wxSP_ARROW_KEYS, 1, 100, gifQuality
		);
		editGifQuality->SetToolTip(_("GIF image quality (1-100), higher values produce better quality but larger files"));
		range_grid->Add(editGifQuality, 1, wxEXPAND);

		range_sizer->Add(range_grid, 0, wxEXPAND | wxALL, inner_pad);

		// ====== 裁剪区域组 ======
		auto *crop_box = new wxStaticBox(&d, wxID_ANY, _("Crop Region"));
		auto *crop_sizer = new wxStaticBoxSizer(crop_box, wxVERTICAL);

		auto *crop_hint = new wxStaticText(
			&d, -1,
			_("Left-drag to select crop area, right-click to clear")
		);
		crop_hint->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

		// 按视频宽高比计算预览面板尺寸
		int preview_w = d.FromDIP(400);
		int preview_h = d.FromDIP(225);
		if (c->project->VideoProvider()) {
			int vw = c->project->VideoProvider()->GetWidth();
			int vh = c->project->VideoProvider()->GetHeight();
			if (vw > 0 && vh > 0) {
				preview_h = static_cast<int>(preview_w * static_cast<double>(vh) / vw);
			}
		}
		cropPanel = new CropSelectionPanel(&d, c);
		cropPanel->SetMinSize(wxSize(preview_w, preview_h));
		cropPanel->SetFrameRange(startFrame, endFrame);

		// 循环播放控制按钮
		playBtn = new wxToggleButton(&d, -1, _("Loop Preview"));
		playBtn->SetToolTip(_("Toggle loop playback within the frame range"));

		crop_sizer->Add(crop_hint, 0, wxLEFT | wxRIGHT | wxTOP, inner_pad);
		crop_sizer->Add(cropPanel, 1, wxEXPAND | wxALL, inner_pad);
		crop_sizer->Add(playBtn, 0, wxALIGN_CENTER | wxBOTTOM, inner_pad);

		// ====== 按钮 ======
		const auto ButtonSizer = d.CreateStdDialogButtonSizer(wxOK | wxCANCEL);

		// ====== 组装总布局 ======
		main_sizer->Add(range_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
		main_sizer->Add(crop_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
		main_sizer->Add(ButtonSizer, 0, wxEXPAND | wxALL, pad);
		d.SetSizerAndFit(main_sizer);
		d.SetSize(d.FromDIP(wxSize(480, 560)));
		d.CenterOnParent();

		d.Bind(wxEVT_INIT_DIALOG, &DialogJumpFrameTo::OnInitDialog, this);
		d.Bind(wxEVT_TEXT_ENTER, &DialogJumpFrameTo::OnOK, this);
		d.Bind(wxEVT_BUTTON, &DialogJumpFrameTo::OnOK, this, wxID_OK);
		d.Bind(wxEVT_BUTTON, &DialogJumpFrameTo::OnCANCEL, this, wxID_CANCEL);
		editStartFrame->Bind(wxEVT_TEXT, &DialogJumpFrameTo::OnEditStartFrame, this);
		editEndFrame->Bind(wxEVT_TEXT, &DialogJumpFrameTo::OnEditEndFrame, this);
		editGifQuality->Bind(wxEVT_TEXT, &DialogJumpFrameTo::OnEditGifQuality, this);
		playBtn->Bind(
			wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &) {
				// 切换播放前同步帧范围
				cropPanel->SetFrameRange(startFrame, endFrame);
				cropPanel->TogglePlayback();
				playBtn->SetLabel(cropPanel->IsPlaying() ? _("Stop Preview") : _("Loop Preview"));
			}
		);
	}

	void DialogJumpFrameTo::OnInitDialog(wxInitDialogEvent &) {
		d.TransferDataToWindow();
		d.UpdateWindowUI(wxUPDATE_UI_RECURSE);

		// This can't simply be done in the constructor as the value hasn't been set yet
		editEndFrame->SetFocus();
		editEndFrame->SelectAll();
	}

	void DialogJumpFrameTo::OnOK(wxCommandEvent &) {
		cropPanel->StopPlayback();
		d.EndModal(0);
		onOK = true;
		onGifQuality = gifQuality = editGifQuality->GetValue();

		// 保存裁剪区域到全局变量
		if (cropPanel->HasSelection()) {
			wxRect r = cropPanel->GetCropRect();
			cropX = r.x;
			cropY = r.y;
			cropW = r.width;
			cropH = r.height;
			hasCropRegion = true;
		} else {
			cropX = cropY = cropW = cropH = 0;
			hasCropRegion = false;
		}
	}

	void DialogJumpFrameTo::OnCANCEL(wxCommandEvent &) {
		cropPanel->StopPlayback();
		d.EndModal(0);
		onOK = false;
		// 取消时重置裁剪区域
		cropX = cropY = cropW = cropH = 0;
		hasCropRegion = false;
	}

	void DialogJumpFrameTo::OnEditStartFrame(wxCommandEvent &event) {
		editStartFrame->GetValue().ToLong(&startFrame);
		sFrame = startFrame = wxAtol(editStartFrame->GetValue());
		cropPanel->SetFrameRange(startFrame, endFrame);
	}

	void DialogJumpFrameTo::OnEditEndFrame(wxCommandEvent &event) {
		editEndFrame->GetValue().ToLong(&endFrame);
		eFrame = endFrame = wxAtol(editEndFrame->GetValue());
		cropPanel->SetFrameRange(startFrame, endFrame);
		if (endFrame <= startFrame) {
			wxMessageBox(_("The end frame cannot be less than or equal to the start frame"),_("Error"), wxICON_ERROR);
		}
	}

	void DialogJumpFrameTo::OnEditGifQuality(wxCommandEvent &event) {
		onGifQuality = gifQuality = editGifQuality->GetValue();
	}

	/// 帧序列导出对话框（仅帧范围，不含裁剪和GIF质量）
	struct DialogFrameSeqExport {
		wxDialog d;
		agi::Context *c;
		long startFrame;
		wxTextCtrl *editStartFrame;
		long endFrame;
		wxTextCtrl *editEndFrame;

		void OnOK(wxCommandEvent &event);

		void OnCANCEL(wxCommandEvent &event);

		void OnEditStartFrame(wxCommandEvent &event);

		void OnEditEndFrame(wxCommandEvent &event);

		void OnInitDialog(wxInitDialogEvent &);

		explicit DialogFrameSeqExport(agi::Context *c);
	};

	DialogFrameSeqExport::DialogFrameSeqExport(agi::Context *c)
		: d(c->parent, -1, _("Export image sequence"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxWANTS_CHARS)
		, c(c)
		, startFrame(LONG_MAX)
		, endFrame(LONG_MIN) {
		d.SetIcon(GETICON(jumpto_button_16));

		// 从选中行计算起止帧
		for (const auto line : c->selectionController->GetSelectedSet()) {
			const int currentStartFrame = c->videoController->FrameAtTime(line->Start, agi::vfr::START);
			const int currentEndFrame = c->videoController->FrameAtTime(line->End, agi::vfr::END);
			if (currentStartFrame < startFrame) {
				startFrame = currentStartFrame;
			}
			if (currentEndFrame > endFrame) {
				endFrame = currentEndFrame;
			}
		}

		auto *main_sizer = new wxBoxSizer(wxVERTICAL);
		const int pad = d.FromDIP(6);
		const int inner_pad = d.FromDIP(4);

		// ====== 帧范围组 ======
		auto *range_box = new wxStaticBox(&d, wxID_ANY, _("Frame Range"));
		auto *range_sizer = new wxStaticBoxSizer(range_box, wxVERTICAL);

		auto *range_grid = new wxFlexGridSizer(2, inner_pad, inner_pad);
		range_grid->AddGrowableCol(1, 1);

		range_grid->Add(
			new wxStaticText(&d, -1, _("Start Frame:")),
			0, wxALIGN_CENTER_VERTICAL
		);
		editStartFrame = new wxTextCtrl(
			&d, -1, "", wxDefaultPosition, wxSize(-1, -1),
			wxTE_PROCESS_ENTER, IntValidator(static_cast<int>(startFrame))
		);
		editStartFrame->SetMaxLength(std::to_string(c->project->VideoProvider()->GetFrameCount() - 1).size());
		range_grid->Add(editStartFrame, 1, wxEXPAND);

		range_grid->Add(
			new wxStaticText(&d, -1, _("End Frame:")),
			0, wxALIGN_CENTER_VERTICAL
		);
		editEndFrame = new wxTextCtrl(
			&d, -1, "", wxDefaultPosition, wxSize(-1, -1),
			wxTE_PROCESS_ENTER, IntValidator(static_cast<int>(endFrame))
		);
		editEndFrame->SetMaxLength(std::to_string(c->project->VideoProvider()->GetFrameCount() - 1).size());
		range_grid->Add(editEndFrame, 1, wxEXPAND);

		range_sizer->Add(range_grid, 0, wxEXPAND | wxALL, inner_pad);

		// ====== 按钮 ======
		const auto ButtonSizer = d.CreateStdDialogButtonSizer(wxOK | wxCANCEL);

		// ====== 组装总布局 ======
		main_sizer->Add(range_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, pad);
		main_sizer->Add(ButtonSizer, 0, wxEXPAND | wxALL, pad);
		d.SetSizerAndFit(main_sizer);
		d.CenterOnParent();

		d.Bind(wxEVT_INIT_DIALOG, &DialogFrameSeqExport::OnInitDialog, this);
		d.Bind(wxEVT_TEXT_ENTER, &DialogFrameSeqExport::OnOK, this);
		d.Bind(wxEVT_BUTTON, &DialogFrameSeqExport::OnOK, this, wxID_OK);
		d.Bind(wxEVT_BUTTON, &DialogFrameSeqExport::OnCANCEL, this, wxID_CANCEL);
		editStartFrame->Bind(wxEVT_TEXT, &DialogFrameSeqExport::OnEditStartFrame, this);
		editEndFrame->Bind(wxEVT_TEXT, &DialogFrameSeqExport::OnEditEndFrame, this);
	}

	void DialogFrameSeqExport::OnInitDialog(wxInitDialogEvent &) {
		d.TransferDataToWindow();
		d.UpdateWindowUI(wxUPDATE_UI_RECURSE);
		editEndFrame->SetFocus();
		editEndFrame->SelectAll();
	}

	void DialogFrameSeqExport::OnOK(wxCommandEvent &) {
		if (endFrame <= startFrame) {
			wxMessageBox(_("The end frame cannot be less than or equal to the start frame"), _("Error"), wxICON_ERROR);
			return;
		}
		d.EndModal(0);
		seqStartFrame = startFrame;
		seqEndFrame = endFrame;
		seqOnOK = true;
	}

	void DialogFrameSeqExport::OnCANCEL(wxCommandEvent &) {
		d.EndModal(0);
		seqOnOK = false;
	}

	void DialogFrameSeqExport::OnEditStartFrame(wxCommandEvent &event) {
		editStartFrame->GetValue().ToLong(&startFrame);
		seqStartFrame = startFrame = wxAtol(editStartFrame->GetValue());
	}

	void DialogFrameSeqExport::OnEditEndFrame(wxCommandEvent &event) {
		editEndFrame->GetValue().ToLong(&endFrame);
		seqEndFrame = endFrame = wxAtol(editEndFrame->GetValue());
	}
}

void ShowJumpToDialog(agi::Context *c) {
	DialogJumpTo(c).d.ShowModal();
}

long getStartFrame() {
	return sFrame;
}

long getEndFrame() {
	return eFrame;
}

int getStartTime() {
	return startTime;
}

int getEndTime() {
	return endTime;
}

bool getOnOK() {
	return onOK;
}

long getGifQuality() {
	return onGifQuality;
}

int getCropX() {
	return cropX;
}

int getCropY() {
	return cropY;
}

int getCropW() {
	return cropW;
}

int getCropH() {
	return cropH;
}

bool getHasCropRegion() {
	return hasCropRegion;
}

void ShowJumpFrameToDialog(agi::Context *c) {
	DialogJumpFrameTo(c).d.ShowModal();
}

void ShowFrameSeqExportDialog(agi::Context *c) {
	DialogFrameSeqExport(c).d.ShowModal();
}

long getSeqStartFrame() {
	return seqStartFrame;
}

long getSeqEndFrame() {
	return seqEndFrame;
}

bool getSeqOnOK() {
	return seqOnOK;
}
