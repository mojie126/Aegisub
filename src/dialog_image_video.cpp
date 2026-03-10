/// @file dialog_image_video.cpp
/// @brief 图片视频配置对话框
/// @ingroup secondary_ui
///
/// 提供文件选择、帧率设置、图片序列信息显示的对话框

#include "compat.h"
#include "format.h"
#include "help_button.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "validators.h"
#include "video_provider_dummy.h"
#include "video_provider_image.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/fs.h>
#include <libaegisub/log.h>

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/filedlg.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/valgen.h>
#include <wx/valtext.h>

namespace {
struct DialogImageVideo {
	wxDialog d;

	wxString fps      = OPT_GET("Video/Image/FPS String")->GetString();
	wxString filepath;

	wxStaticText *sequence_info;
	wxStaticText *duration_display;
	wxTextCtrl *file_display;
	wxFlexGridSizer *sizer;

	int detected_count = 0;

	template<typename T>
	void AddCtrl(wxString const& label, T *ctrl);

	void OnBrowse(wxCommandEvent &evt);
	void UpdateInfo();

	DialogImageVideo(wxWindow *parent);
};

DialogImageVideo::DialogImageVideo(wxWindow *parent)
: d(parent, -1, _("Image video options"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
	d.SetIcons(GETICONS(use_dummy_video_menu));

	// 文件选择行
	auto file_sizer = new wxBoxSizer(wxHORIZONTAL);
	file_display = new wxTextCtrl(&d, -1, "", wxDefaultPosition, d.FromDIP(wxSize(400, -1)), wxTE_READONLY);
	auto browse_btn = new wxButton(&d, -1, _("Browse..."));
	file_sizer->Add(file_display, wxSizerFlags(1).Expand());
	file_sizer->Add(browse_btn, wxSizerFlags().Border(wxLEFT, d.FromDIP(5)));

	// 帧率输入
	wxTextValidator fpsVal(wxFILTER_INCLUDE_CHAR_LIST, &fps);
	fpsVal.SetCharIncludes("0123456789./");

	sizer = new wxFlexGridSizer(2, d.FromDIP(5), d.FromDIP(5));
	sizer->AddGrowableCol(1, 1);
	AddCtrl(_("Image file:"), file_sizer);
	AddCtrl(_("Frame rate (fps):"), new wxTextCtrl(&d, -1, "", wxDefaultPosition, wxDefaultSize, 0, fpsVal));
	AddCtrl(_("Sequence info:"), sequence_info = new wxStaticText(&d, -1, _("No image selected")));
	AddCtrl("", duration_display = new wxStaticText(&d, -1, ""));

	auto btn_sizer = d.CreateStdDialogButtonSizer(wxOK | wxCANCEL);
	btn_sizer->GetAffirmativeButton()->Enable(false);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(sizer, wxSizerFlags(1).Border().Expand());
	main_sizer->Add(new wxStaticLine(&d, wxHORIZONTAL), wxSizerFlags().HorzBorder().Expand());
	main_sizer->Add(btn_sizer, wxSizerFlags().Expand().Border());

	d.SetSizerAndFit(main_sizer);
	d.SetMinSize(d.GetSize());
	d.CenterOnParent();

	browse_btn->Bind(wxEVT_BUTTON, &DialogImageVideo::OnBrowse, this);
	d.Bind(wxEVT_TEXT, [&, btn_sizer](wxCommandEvent&) {
		d.TransferDataFromWindow();
		UpdateInfo();
		// 启用条件：有图片文件、filepath 非空且帧率有效
		agi::vfr::Framerate fr;
		bool fps_valid = DummyVideoProvider::TryParseFramerate(from_wx(fps), fr);
		btn_sizer->GetAffirmativeButton()->Enable(detected_count > 0 && !filepath.empty() && fps_valid);
	});
}

static void add_label(wxWindow *parent, wxSizer *sizer, wxString const& label) {
	if (!label)
		sizer->AddStretchSpacer();
	else
		sizer->Add(new wxStaticText(parent, -1, label), wxSizerFlags().Center().Left());
}

template<typename T>
void DialogImageVideo::AddCtrl(wxString const& label, T *ctrl) {
	add_label(&d, sizer, label);
	sizer->Add(ctrl, wxSizerFlags().Expand().Center().Left());
}

void DialogImageVideo::OnBrowse(wxCommandEvent &) {
	wxFileDialog dlg(&d, _("Select image file"), "", "",
		_("Image files") + " (*.png;*.jpg;*.jpeg;*.bmp;*.tiff;*.tif;*.webp)|*.png;*.jpg;*.jpeg;*.bmp;*.tiff;*.tif;*.webp|"
		+ _("All Files") + " (*.*)|*.*",
		wxFD_OPEN | wxFD_FILE_MUST_EXIST);

	if (dlg.ShowModal() == wxID_OK) {
		filepath = dlg.GetPath();
		file_display->SetValue(filepath);
		UpdateInfo();
	}
}

void DialogImageVideo::UpdateInfo() {
	detected_count = 0;

	if (filepath.empty()) {
		sequence_info->SetLabel(_("No image selected"));
		duration_display->SetLabel("");
		return;
	}

	agi::fs::path path(from_wx(filepath));
	if (!agi::fs::FileExists(path)) {
		sequence_info->SetLabel(_("File not found"));
		duration_display->SetLabel("");
		return;
	}

	auto files = ImageVideoProvider::ScanImageSequence(path);
	detected_count = static_cast<int>(files.size());

	if (detected_count == 1) {
		sequence_info->SetLabel(fmt_tl("Single image: %s", path.filename().string()));
	} else {
		sequence_info->SetLabel(fmt_tl("%d images: %s ... %s",
			detected_count,
			files.front().filename().string(),
			files.back().filename().string()));
	}

	// 计算时长
	agi::vfr::Framerate fr;
	if (DummyVideoProvider::TryParseFramerate(from_wx(fps), fr)) {
		auto dur = agi::Time(fr.TimeAtFrame(detected_count));
		duration_display->SetLabel(fmt_tl("Duration: %s (%d frames)", dur.GetAssFormatted(true), detected_count));
	} else {
		duration_display->SetLabel(fmt_tl("Duration: - (%d frames)", detected_count));
	}

	d.GetSizer()->Layout();
	d.Fit();
}
}

std::string CreateImageVideo(wxWindow *parent) {
	DialogImageVideo dlg(parent);
	if (dlg.d.ShowModal() != wxID_OK)
		return "";

	OPT_SET("Video/Image/FPS String")->SetString(from_wx(dlg.fps));

	return ImageVideoProvider::MakeFilename(from_wx(dlg.fps), from_wx(dlg.filepath));
}
