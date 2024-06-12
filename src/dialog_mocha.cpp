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
#include <dialog_manager.h>
#include <aegisub/context.h>

std::vector<KeyframeData> final_data;
MochaData mocha_data;
bool onMochaOK = false;

std::vector<KeyframeData> parseData(const std::string &input, bool getPosition, bool getScale, bool getRotation) {
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
		mocha_data.get_position = getPosition;
		mocha_data.get_scale = getScale;
		mocha_data.get_rotation = getRotation;
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
				if (getPosition) {
					ss >> data.x;
					ss >> data.y;
					ss >> data.z;
				}
			} else if (section == "Scale") {
				if (getScale) {
					ss >> data.scaleX;
					ss >> data.scaleY;
					ss >> data.scaleZ;
				}
			} else if (section == "Rotation") {
				if (getRotation) {
					ss >> data.rotation;
					data.rotation = std::fabs(std::fmod(data.rotation, 360.0));
				}
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

		explicit DialogMochaUtil(agi::Context *c);

		wxDialog d;
		agi::Context *c;
		wxCheckBox *positionCheckBoxes;
		wxCheckBox *scaleCheckBoxes;
		wxCheckBox *rotationCheckBox;
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
		struct ControlGroup {
			wxStaticText *label;
			wxCheckBox *checkBoxes;
		};

		ControlGroup positionGroup{}, scaleGroup{}, rotationGroup{};

		positionGroup.label = new wxStaticText(&d, wxID_ANY, _("Position: "));
		scaleGroup.label = new wxStaticText(&d, wxID_ANY, _("Scale: "));
		rotationGroup.label = new wxStaticText(&d, wxID_ANY, _("Rotation: "));

		positionGroup.checkBoxes = new wxCheckBox(&d, wxID_ANY, wxEmptyString);
		scaleGroup.checkBoxes = new wxCheckBox(&d, wxID_ANY, wxEmptyString);
		rotationGroup.checkBoxes = new wxCheckBox(&d, wxID_ANY, wxEmptyString);

		// 将标签和复选框添加到控制sizer中
		controlsSizer->Add(positionGroup.label, 0, wxALIGN_CENTER_VERTICAL);
		controlsSizer->Add(positionGroup.checkBoxes, 0, wxALL, d.FromDIP(5));
		controlsSizer->AddStretchSpacer();
		controlsSizer->Add(scaleGroup.label, 0, wxALIGN_CENTER_VERTICAL);
		controlsSizer->Add(scaleGroup.checkBoxes, 0, wxALL, d.FromDIP(5));
		controlsSizer->AddStretchSpacer();
		controlsSizer->Add(rotationGroup.label, 0, wxALIGN_CENTER_VERTICAL);
		controlsSizer->Add(rotationGroup.checkBoxes, 0, wxALL, d.FromDIP(5));

		// 保存复选框指针到类成员变量
		positionCheckBoxes = positionGroup.checkBoxes;
		scaleCheckBoxes = scaleGroup.checkBoxes;
		rotationCheckBox = rotationGroup.checkBoxes;

		// 设置默认勾选状态
		positionCheckBoxes->SetValue(true);
		scaleCheckBoxes->SetValue(true);
		rotationCheckBox->SetValue(true);

		// 创建多行文本框
		auto *logBox = new wxStaticBox(&d, wxID_ANY, _("Mocha Motion Data"));
		auto *logSizer = new wxStaticBoxSizer(logBox, wxVERTICAL);
		logTextCtrl = new wxTextCtrl(&d, wxID_ANY, "", wxDefaultPosition, wxSize(-1, d.FromDIP(150)), wxTE_MULTILINE);
		logSizer->Add(logTextCtrl, 1, wxEXPAND | wxALL, d.FromDIP(5));

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
	}

	void DialogMochaUtil::OnStart(wxCommandEvent &) {
		// 获取复选框的状态
		const bool getPosition = positionCheckBoxes->IsChecked();
		const bool getScale = scaleCheckBoxes->IsChecked();
		const bool getRotation = rotationCheckBox->IsChecked();

		// 获取文本框中的内容
		const wxString inputText = logTextCtrl->GetValue();

		// 解析数据
		try {
			final_data = parseData(inputText.ToStdString(), getPosition, getScale, getRotation);

			// // 整合并处理数据
			// std::cout << "Frame\t";
			// if (getX) std::cout << "X\t";
			// if (getY) std::cout << "Y\t";
			// if (getZ) std::cout << "Z\t";
			// if (getScaleX) std::cout << "ScaleX\t";
			// if (getScaleY) std::cout << "ScaleY\t";
			// if (getScaleZ) std::cout << "ScaleZ\t";
			// if (getRotation) std::cout << "Rotation\t";
			// std::cout << std::endl;
			//
			// for (const auto &[frame, x, y, z, scaleX, scaleY, scaleZ, rotation] : final_data) {
			// 	std::cout << frame << "\t";
			// 	if (getX) std::cout << x << "\t";
			// 	if (getY) std::cout << y << "\t";
			// 	if (getZ) std::cout << z << "\t";
			// 	if (getScaleX) std::cout << scaleX << "\t";
			// 	if (getScaleY) std::cout << scaleY << "\t";
			// 	if (getScaleZ) std::cout << scaleZ << "\t";
			// 	if (getRotation) std::cout << rotation << "\t";
			// 	std::cout << std::endl;
			// }
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
