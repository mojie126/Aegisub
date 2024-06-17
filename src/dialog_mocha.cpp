#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/checkbox.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <cmath>
#include <dialogs.h>
#include <exception>
#include <aegisub/context.h>

std::vector<KeyframeData> final_data;
MochaData mocha_data;
bool onMochaOK = false;

std::vector<KeyframeData> parseData(const std::string &input) {
	std::istringstream stream(input), check_stream(input);
	std::string line;
	std::vector<KeyframeData> keyframeData;
	std::string section;

	if (std::getline(check_stream, line)) {
		if (line.find("Adobe After Effects ") == 0 && line.find(" Keyframe Data") != std::string::npos) {
			mocha_data.is_mocha_data = true;
		} else
			return keyframeData;
	}

	if (mocha_data.is_mocha_data) {
		while (std::getline(check_stream, line)) {
			if (line.find("Source Width") != std::string::npos) {
				mocha_data.source_width = std::stoi(line.substr(line.find_last_of('\t') + 1));
			} else if (line.find("Source Height") != std::string::npos) {
				mocha_data.source_height = std::stoi(line.substr(line.find_last_of('\t') + 1));
			} else if (line.find("Units Per Second") != std::string::npos) {
				mocha_data.frame_rate = std::stod(line.substr(line.find_last_of('\t') + 1));
			}
		}
	}

	while (std::getline(stream, line)) {
		std::istringstream ss(line);
		std::string word;
		ss >> word;

		if (word == "Position" || word == "Scale" || word == "Rotation") {
			section = word;
		} else if (isdigit(word[0]) || (word[0] == '-' && isdigit(word[1]))) {
			int frame = std::stoi(word);

			// 找到现有的 KeyframeData 对象，或者新建一个新的对象
			auto it = std::find_if(
				keyframeData.begin(), keyframeData.end(), [&](const KeyframeData &kf) {
					return kf.frame == frame;
				}
			);

			KeyframeData data{};
			if (it != keyframeData.end()) {
				data = *it;
			} else {
				data.frame = frame;
			}

			if (section == "Position") {
				ss >> data.x;
				ss >> data.y;
				ss >> data.z;
			} else if (section == "Scale") {
				ss >> data.scaleX;
				ss >> data.scaleY;
				ss >> data.scaleZ;
			} else if (section == "Rotation") {
				ss >> data.rotation;
			}

			// 更新或者插入新的对象
			if (it != keyframeData.end()) {
				*it = data;
			} else {
				keyframeData.push_back(data);
			}
		}
	}

	mocha_data.total_frame = keyframeData.size();
	return keyframeData;
}

namespace {
	struct DialogMochaUtil {
		void OnStart(wxCommandEvent &);

		void OnCancel(wxCommandEvent &);

		void OnPaste(wxCommandEvent &);

		void OnActivate(wxActivateEvent &);

		explicit DialogMochaUtil(agi::Context *c);

		wxDialog d;
		agi::Context *c;
		wxCheckBox *positionCheckBoxes;
		wxCheckBox *scaleCheckBoxes;
		wxCheckBox *rotationCheckBox;
		wxCheckBox *previewCheckBox;
		wxTextCtrl *logTextCtrl;
	};

	DialogMochaUtil::DialogMochaUtil(agi::Context *c)
		: d(c->parent, -1, _("Mocha Motion - Simple Version"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
		, c(c) {
		// 使用 wxDIP 来适配高 DPI 显示
		const wxSize dialogSize = wxDialog::FromDIP(wxSize(600, 400), &d);

		// 创建主垂直sizer
		auto *mainSizer = new wxBoxSizer(wxVERTICAL);

		// 创建包含所有控件的静态框和水平sizer
		auto *controlsBox = new wxStaticBox(&d, wxID_ANY, _("Options"));
		auto *controlsSizer = new wxStaticBoxSizer(controlsBox, wxHORIZONTAL);

		// 创建位移、缩放和角度的标签和复选框
		auto *positionLabel = new wxStaticText(&d, wxID_ANY, _("Position(\\pos):"));
		auto *positionCheckBoxes = new wxCheckBox(&d, wxID_ANY, wxEmptyString);
		positionCheckBoxes->SetToolTip(_("Applies the coordinate values of the tracking data to the selected caption line"));

		auto *scaleLabel = new wxStaticText(&d, wxID_ANY, _("Scale(\\fscx, \\fscy):"));
		auto *scaleCheckBoxes = new wxCheckBox(&d, wxID_ANY, wxEmptyString);
		scaleCheckBoxes->SetToolTip(_("Applies the zoom value of the tracking data to the selected caption line"));

		auto *rotationLabel = new wxStaticText(&d, wxID_ANY, _("Rotation(\\frz):"));
		auto *rotationCheckBox = new wxCheckBox(&d, wxID_ANY, wxEmptyString);
		rotationCheckBox->SetToolTip(_("Applies the rotation value of the tracking data to the selected caption line"));

		auto *previewLabel = new wxStaticText(&d, wxID_ANY, _("Convenient preview:"));
		auto *previewCheckBox = new wxCheckBox(&d, wxID_ANY, wxEmptyString);
		previewCheckBox->SetToolTip(_("Annotate the original subtitle line to preview the tracking effect, and click [Play Current Line] to preview it"));

		// 将标签和复选框添加到控制sizer中
		controlsSizer->Add(positionLabel, 0, wxALIGN_CENTER_VERTICAL);
		controlsSizer->Add(positionCheckBoxes, 0, wxALL, d.FromDIP(5));
		controlsSizer->AddStretchSpacer();
		controlsSizer->Add(scaleLabel, 0, wxALIGN_CENTER_VERTICAL);
		controlsSizer->Add(scaleCheckBoxes, 0, wxALL, d.FromDIP(5));
		controlsSizer->AddStretchSpacer();
		controlsSizer->Add(rotationLabel, 0, wxALIGN_CENTER_VERTICAL);
		controlsSizer->Add(rotationCheckBox, 0, wxALL, d.FromDIP(5));
		controlsSizer->AddStretchSpacer();
		controlsSizer->Add(previewLabel, 0, wxALIGN_CENTER_VERTICAL);
		controlsSizer->Add(previewCheckBox, 0, wxALL, d.FromDIP(5));

		// 保存复选框指针到类成员变量
		this->positionCheckBoxes = positionCheckBoxes;
		this->scaleCheckBoxes = scaleCheckBoxes;
		this->rotationCheckBox = rotationCheckBox;
		this->previewCheckBox = previewCheckBox;

		// 设置默认勾选状态
		this->positionCheckBoxes->SetValue(true);
		this->scaleCheckBoxes->SetValue(true);
		this->rotationCheckBox->SetValue(true);
		this->previewCheckBox->SetValue(true);

		// 创建多行文本框
		auto *logBox = new wxStaticBox(&d, wxID_ANY, _("Mocha Motion Data"));
		auto *logSizer = new wxStaticBoxSizer(logBox, wxVERTICAL);
		this->logTextCtrl = new wxTextCtrl(&d, wxID_ANY, "", wxDefaultPosition, wxSize(-1, d.FromDIP(150)), wxTE_MULTILINE);
		logSizer->Add(this->logTextCtrl, 1, wxEXPAND | wxALL, d.FromDIP(5));

		// 创建按钮
		wxStdDialogButtonSizer *buttonSizer = d.CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxAPPLY);
		wxButton *executeButton = buttonSizer->GetAffirmativeButton();
		executeButton->SetLabel(_("Apply"));
		wxButton *cancelButton = buttonSizer->GetCancelButton();
		cancelButton->SetLabel(_("Cancel"));
		wxButton *pasteButton = buttonSizer->GetApplyButton();
		pasteButton->SetLabel(_("Paste from Clipboard"));

		// 将所有sizer添加到主sizer中
		mainSizer->Add(controlsSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, d.FromDIP(10));
		mainSizer->Add(logSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, d.FromDIP(10));
		mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, d.FromDIP(10));

		// 获取剪贴板内容
		wxCommandEvent wx_command_event;
		OnPaste(wx_command_event);

		logTextCtrl->SetFocus();
		d.Refresh();

		d.SetSizerAndFit(mainSizer);
		d.SetSize(dialogSize);
		d.CentreOnScreen();

		// 绑定事件处理函数
		d.Bind(wxEVT_BUTTON, &DialogMochaUtil::OnStart, this, wxID_OK);
		d.Bind(wxEVT_BUTTON, &DialogMochaUtil::OnCancel, this, wxID_CANCEL);
		d.Bind(wxEVT_BUTTON, &DialogMochaUtil::OnPaste, this, wxID_APPLY);
		d.Bind(wxEVT_ACTIVATE, &DialogMochaUtil::OnActivate, this);
	}

	void DialogMochaUtil::OnStart(wxCommandEvent &) {
		// 获取复选框的状态
		mocha_data.get_position = positionCheckBoxes->IsChecked();
		mocha_data.get_scale = scaleCheckBoxes->IsChecked();
		mocha_data.get_rotation = rotationCheckBox->IsChecked();
		mocha_data.get_preview = previewCheckBox->IsChecked();

		// 获取文本框中的内容
		const wxString inputText = logTextCtrl->GetValue();

		// 解析数据
		try {
			final_data = parseData(inputText.ToStdString());
		} catch (const std::exception &e) {
			wxLogError("Error: %s", e.what());
		}
		d.EndModal(0);
		onMochaOK = true;
	}

	void DialogMochaUtil::OnCancel(wxCommandEvent &) {
		d.EndModal(0);
		onMochaOK = false;
	}

	void DialogMochaUtil::OnPaste(wxCommandEvent &) {
		// 获取剪贴板内容
		if (wxTheClipboard->Open()) {
			if (wxTheClipboard->IsSupported(wxDF_TEXT)) {
				wxTextDataObject data;
				wxTheClipboard->GetData(data);
				const wxString clipboardText = data.GetText();
				// 将剪贴板内容复制到文本框中
				logTextCtrl->SetValue(clipboardText);
			}
			wxTheClipboard->Close();
		}
	}

	void DialogMochaUtil::OnActivate(wxActivateEvent &event) {
		if (event.GetActive()) {
			// 当对话框重新获得焦点时，执行获取剪贴板并粘贴的行为
			wxCommandEvent wx_command_event;
			OnPaste(wx_command_event);
		}
		event.Skip(); // 确保事件继续被处理
	}
}

std::vector<KeyframeData> getMochaMotionParseData() {
	return final_data;
}

MochaData getMochaCheckData() {
	return mocha_data;
}

bool getMochaOK() {
	return onMochaOK;
}

void ShowMochaUtilDialog(agi::Context *c) {
	DialogMochaUtil(c).d.ShowModal();
}
