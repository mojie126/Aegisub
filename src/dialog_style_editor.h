// Copyright(c) 2005, Rodrigo Braz Monteiro
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
// CONSEQUENTIAL DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

#include <memory>
#include <string>
#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/popupwin.h>
#include "ini.h"

class AssStyle;
class AssStyleStorage;
class PersistLocation;
class SubtitlesPreview;
class wxArrayString;
class wxButton;
class wxCheckBox;
class wxChildFocusEvent;
class wxCommandEvent;
class wxFocusEvent;
class wxKeyEvent;
class wxRadioBox;
class wxSpinCtrl;
class wxTextCtrl;
class wxThreadEvent;
class wxWindow;
class FontPreviewListBox;
namespace agi { struct Context; struct Color; }
template<typename T> class ValueEvent;

class FontSelectionPopupWindow;

/// @class FontSelectionControl
/// @brief 样式编辑器使用的字体选择控件，支持输入过滤与弹出预览列表
class FontSelectionControl final : public wxPanel {
public:
	FontSelectionControl(wxWindow *parent, wxWindowID id, const wxString &value,
		const wxPoint &pos, const wxSize &size, long style = 0);
	~FontSelectionControl();

	/// @brief 设置可选字体列表
	void SetFontList(const wxArrayString &fonts);
	/// @brief 清空字体列表
	void Clear();
	void Freeze() {}
	void Thaw() {}

	/// @brief 获取当前输入的字体名称
	wxString GetValue() const;
	/// @brief 设置字体名称
	void SetValue(const wxString &value);
	long GetInsertionPoint() const;
	void SetInsertionPoint(long pos);
	/// @brief 弹出过滤后的字体列表
	void Popup();
	/// @brief 弹出全部字体列表
	void PopupAll();
	/// @brief 关闭弹出列表
	void Dismiss();
	/// @brief 弹出列表是否可见
	bool HasPopup() const;
	/// @brief 确认弹出列表中的当前选中项
	void AcceptPopupSelection();
	/// @brief 移动弹出列表选中项
	void MovePopupSelection(int delta);
	void SelectAllText();
	wxTextCtrl *GetTextCtrl() const;
	/// @brief 设置控件整体高度，使其与相邻控件对齐
	void SetControlHeight(int height);

private:
	friend class FontSelectionPopupWindow;

	wxTextCtrl *textCtrl_;
	wxButton *dropButton_;
	FontSelectionPopupWindow *popup_;
	wxArrayString allFonts_;
	wxArrayString filteredFonts_;
	bool suppressTextEvent_ = false;

	void UpdateFilteredFonts(bool show_popup);
	void EmitEvent(wxEventType type, const wxString &value = wxString());
	void OnText(wxCommandEvent &event);
	void OnTextEnter(wxCommandEvent &event);
	void OnButton(wxCommandEvent &event);
	void OnKillFocus(wxFocusEvent &event);
	void OnTextKeyDown(wxKeyEvent &event);
};

class DialogStyleEditor final : public wxDialog {
	agi::Context *c;
	std::unique_ptr<PersistLocation> persist;

	/// If true, the style was just created and so the user should not be
	/// asked if they want to change any existing lines should they rename
	/// the style
	bool is_new = false;

	bool updating = false;

	/// The style currently being edited
	AssStyle *style;

	/// Copy of style passed to the subtitles preview to avoid making changes
	/// before Apply is clicked
	std::unique_ptr<AssStyle> work;

	/// The style storage style is in, if applicable
	AssStyleStorage *store;

	wxTextCtrl *StyleName;
	FontSelectionControl *FontName;
	wxArrayString fontList_; ///< 完整字体列表，用于子串过滤
	wxCheckBox *BoxBold;
	wxCheckBox *BoxItalic;
	wxCheckBox *BoxUnderline;
	wxCheckBox *BoxStrikeout;
	wxSpinCtrl *margin[3]{};
	wxRadioBox *Alignment;
	wxComboBox *OutlineType;
	wxComboBox *Encoding;
	wxTextCtrl *PreviewText;
	SubtitlesPreview *SubsPreview;

	void SetBitmapColor(int n,wxColour color);
	int AlignToControl(int n);
	int ControlToAlign(int n);
	int BorderStyleToControl(int n);
	int ControlToBorderStyle(int n);
	void UpdateWorkStyle();

	void OnChildFocus(wxChildFocusEvent &event);
	void OnCommandPreviewUpdate(wxCommandEvent &event);
	void OnFontNameText(wxCommandEvent &event);

	void OnPreviewTextChange(wxCommandEvent &event);
	void OnPreviewColourChange(ValueEvent<agi::Color> &event);

	/// @brief Maybe apply changes and maybe close the dialog
	/// @param apply Should changes be applied?
	/// @param close Should the dialog be closed?
	void Apply(bool apply,bool close);
	/// @brief Sets color for one of the four color buttons
	void OnSetColor(ValueEvent<agi::Color>& evt);

public:
	DialogStyleEditor(wxWindow *parent, AssStyle *style, agi::Context *c, AssStyleStorage *store, std::string const& new_name, wxArrayString const& font_list);
	~DialogStyleEditor();

	std::string GetStyleName() const;
};
