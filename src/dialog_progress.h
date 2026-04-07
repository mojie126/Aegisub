// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
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

/// @file dialog_progress.h
/// @see dialog_progress.cpp
/// @ingroup utility
///

#include <chrono>
#include <wx/dialog.h>
#include <wx/timer.h>

#include <libaegisub/background_runner.h>

class DialogProgressSink;
class wxButton;
class wxGauge;
class wxStaticText;
class wxTextCtrl;

/// @class DialogProgress
/// @brief Progress-bar dialog box for displaying during long operations
class DialogProgress final : public wxDialog, public agi::BackgroundRunner {
	friend class DialogProgressSink;
	DialogProgressSink *ps;

	wxStaticText *title;
	wxStaticText *text;
	wxGauge *gauge;
	wxButton *cancel_button;
	wxTextCtrl *log_output;

	wxTimer pulse_timer;

	wxString pending_log;
	int progress_anim_start_value = 0;
	int progress_current = 0;
	int progress_target = 0;
	std::chrono::steady_clock::time_point progress_anim_start_time;
	int progress_anim_duration = 0;
	std::function<void(agi::ProgressSink *)> pending_task;
	bool task_started = false;
	bool start_task_after_shown_ = false;
	bool immediate_progress_update_ = false;

	void OnShow(wxShowEvent&);
	void OnCancel(wxCommandEvent &);
	void OnIdle(wxIdleEvent&);

	void StartTask();
	void SetProgress(int target);

public:
	/// Constructor
	/// @param parent Parent window of the dialog
	/// @param title Initial title of the dialog
	/// @param message Initial message of the dialog
	DialogProgress(wxWindow *parent, wxString const& title="", wxString const& message="");

	/// @brief 是否在窗口显示后再启动后台任务
	/// @param value 为 true 时等待窗口显示完成后再启动任务
	void SetStartTaskAfterShown(bool value = true) { start_task_after_shown_ = value; }

	/// @brief 是否在收到进度更新时立即刷新进度条
	/// @param value 为 true 时跳过 idle 动画，直接刷新 gauge
	void SetImmediateProgressUpdate(bool value = true) { immediate_progress_update_ = value; }

	/// BackgroundWorker implementation
	void Run(std::function<void(agi::ProgressSink *)> task) override;
};
