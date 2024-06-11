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
#include <exception>
#include <dialog_manager.h>
#include <aegisub/context.h>

struct KeyframeData {
	int frame;
	double x;
	double y;
	double z;
	double scaleX;
	double scaleY;
	double scaleZ;
	double rotation;
};

std::vector<KeyframeData> parseData(const std::string &input, bool getX, bool getY, bool getZ, bool getScaleX, bool getScaleY, bool getScaleZ, bool getRotation) {
	std::istringstream stream(input);
	std::string line;
	std::vector<KeyframeData> keyframeData;
	std::string section;

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
				if (getX) ss >> data.x;
				if (getY) ss >> data.y;
				if (getZ) ss >> data.z;
			} else if (section == "Scale") {
				if (getScaleX) ss >> data.scaleX;
				if (getScaleY) ss >> data.scaleY;
				if (getScaleZ) ss >> data.scaleZ;
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

	return keyframeData;
}

namespace {
	class DialogMochaUtil final : public wxDialog {
		void OnStart(wxCommandEvent &);

	public:
		explicit DialogMochaUtil(const agi::Context *c);

	private:
		wxCheckBox *displacementCheckBoxes[3]{};
		wxCheckBox *scaleCheckBoxes[3]{};
		wxCheckBox *rotationCheckBox;
		wxTextCtrl *logTextCtrl;
	};

	DialogMochaUtil::DialogMochaUtil(const agi::Context *c)
		: wxDialog(c->parent, wxID_ANY, _("Mocha Motion - Simple Version"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER) {
		// 使用 wxDIP 来适配高 DPI 显示
		const wxSize dialogSize = FromDIP(wxSize(600, 400), this);

		// 创建主垂直sizer
		auto *mainSizer = new wxBoxSizer(wxVERTICAL);

		// 创建包含所有控件的静态框和水平sizer
		auto *controlsBox = new wxStaticBox(this, wxID_ANY, _("Controls"));
		auto *controlsSizer = new wxStaticBoxSizer(controlsBox, wxHORIZONTAL);

		// 创建位移、缩放和角度的标签和复选框
		struct ControlGroup {
			wxStaticText *label;
			wxCheckBox *checkBoxes[3];
		};

		ControlGroup displacementGroup{}, scaleGroup{}, angleGroup{};

		displacementGroup.label = new wxStaticText(this, wxID_ANY, _("Position:"));
		scaleGroup.label = new wxStaticText(this, wxID_ANY, _("Scale:"));
		angleGroup.label = new wxStaticText(this, wxID_ANY, _("Rotation:"));

		for (int i = 0; i < 3; ++i) {
			wxString checkBoxLabel = wxString::Format("%c", 'X' + i);
			displacementGroup.checkBoxes[i] = new wxCheckBox(this, wxID_ANY, checkBoxLabel);
			scaleGroup.checkBoxes[i] = new wxCheckBox(this, wxID_ANY, checkBoxLabel);
		}
		angleGroup.checkBoxes[0] = new wxCheckBox(this, wxID_ANY, wxEmptyString);

		// 将标签和复选框添加到控制sizer中
		controlsSizer->Add(displacementGroup.label, 0, wxALIGN_CENTER_VERTICAL);
		for (const auto checkBox : displacementGroup.checkBoxes) {
			controlsSizer->Add(checkBox, 0, wxALL, FromDIP(5));
		}
		controlsSizer->AddStretchSpacer(2);
		controlsSizer->Add(scaleGroup.label, 0, wxALIGN_CENTER_VERTICAL);
		for (const auto checkBox : scaleGroup.checkBoxes) {
			controlsSizer->Add(checkBox, 0, wxALL, FromDIP(5));
		}
		controlsSizer->AddStretchSpacer(2);
		controlsSizer->Add(angleGroup.label, 0, wxALIGN_CENTER_VERTICAL);
		controlsSizer->Add(angleGroup.checkBoxes[0], 0, wxALL, FromDIP(5));

		// 保存复选框指针到类成员变量
		for (int i = 0; i < 3; ++i) {
			displacementCheckBoxes[i] = displacementGroup.checkBoxes[i];
			scaleCheckBoxes[i] = scaleGroup.checkBoxes[i];
		}
		rotationCheckBox = angleGroup.checkBoxes[0];

		// 设置默认勾选状态
		for (int i = 0; i < 3; ++i) {
			displacementCheckBoxes[i]->SetValue(true);
			scaleCheckBoxes[i]->SetValue(true);
		}

		// 创建多行文本框
		auto *logBox = new wxStaticBox(this, wxID_ANY, _("Mocha Motion Data"));
		auto *logSizer = new wxStaticBoxSizer(logBox, wxVERTICAL);
		logTextCtrl = new wxTextCtrl(this, wxID_ANY, "", wxDefaultPosition, wxSize(-1, FromDIP(150)), wxTE_MULTILINE);
		logSizer->Add(logTextCtrl, 1, wxEXPAND | wxALL, FromDIP(5));

		// 创建按钮
		wxStdDialogButtonSizer *buttonSizer = CreateStdDialogButtonSizer(wxOK | wxCANCEL);
		wxButton *executeButton = buttonSizer->GetAffirmativeButton();
		executeButton->SetLabel(_("Apply"));
		executeButton->Bind(wxEVT_BUTTON, &DialogMochaUtil::OnStart, this);
		wxButton *cancelButton = buttonSizer->GetCancelButton();
		cancelButton->SetLabel(_("Cancel"));

		// 将所有sizer添加到主sizer中
		mainSizer->Add(controlsSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
		mainSizer->Add(logSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(10));
		mainSizer->Add(buttonSizer, 0, wxALIGN_RIGHT | wxALL, FromDIP(10));

		SetSizerAndFit(mainSizer);
		SetSize(dialogSize);
		CentreOnScreen();
	}

	void DialogMochaUtil::OnStart(wxCommandEvent &) {
		// 获取复选框的状态
		const bool getX = displacementCheckBoxes[0]->IsChecked();
		const bool getY = displacementCheckBoxes[1]->IsChecked();
		const bool getZ = displacementCheckBoxes[2]->IsChecked();
		const bool getScaleX = scaleCheckBoxes[0]->IsChecked();
		const bool getScaleY = scaleCheckBoxes[1]->IsChecked();
		const bool getScaleZ = scaleCheckBoxes[2]->IsChecked();
		const bool getRotation = rotationCheckBox->IsChecked();

		// 获取文本框中的内容
		const wxString inputText = logTextCtrl->GetValue();

		// 解析数据
		try {
			const std::vector<KeyframeData> data = parseData(inputText.ToStdString(), getX, getY, getZ, getScaleX, getScaleY, getScaleZ, getRotation);

			// 整合并处理数据
			std::cout << "Frame\t";
			if (getX) std::cout << "X\t";
			if (getY) std::cout << "Y\t";
			if (getZ) std::cout << "Z\t";
			if (getScaleX) std::cout << "ScaleX\t";
			if (getScaleY) std::cout << "ScaleY\t";
			if (getScaleZ) std::cout << "ScaleZ\t";
			if (getRotation) std::cout << "Rotation\t";
			std::cout << std::endl;

			for (const auto &[frame, x, y, z, scaleX, scaleY, scaleZ, rotation] : data) {
				std::cout << frame << "\t";
				if (getX) std::cout << x << "\t";
				if (getY) std::cout << y << "\t";
				if (getZ) std::cout << z << "\t";
				if (getScaleX) std::cout << scaleX << "\t";
				if (getScaleY) std::cout << scaleY << "\t";
				if (getScaleZ) std::cout << scaleZ << "\t";
				if (getRotation) std::cout << rotation << "\t";
				std::cout << std::endl;
			}
		} catch (const std::exception &e) {
			wxLogError("Error: %s", e.what());
		}
	}
}

void ShowMochaUtilDialog(agi::Context *c) {
	c->dialog->Show<DialogMochaUtil>(c);
}
