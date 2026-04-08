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

/// @file dialog_style_editor.cpp
/// @brief Style Editor dialogue box
/// @ingroup style_editor
///

#include "dialog_style_editor.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "ass_style_storage.h"
#include "colour_button.h"
#include "compat.h"
#include "dialog_font_chooser.h"
#include "font_list_load_mode.h"
#include "help_button.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "persist_location.h"
#include "selection_controller.h"
#include "subs_preview.h"
#include "theme.h"
#include "utils.h"
#include "validators.h"

#include <algorithm>
#include <chrono>
#include <freetype/freetype.h>

#include <wx/bmpbuttn.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/dir.h>
#include <wx/msgdlg.h>
#include <wx/popupwin.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

// ======================================================================
// FontSelectionControl 实现
// ======================================================================

/// @class FontSelectionPopupWindow
/// @brief 字体选择弹出窗口，以带字体预览的列表展示可选字体
class FontSelectionPopupWindow final : public wxPopupWindow {
	FontSelectionControl *owner_;
	FontPreviewListBox *list_;
	wxArrayString fonts_;

public:
	FontSelectionPopupWindow(FontSelectionControl *owner)
	: wxPopupWindow(owner, wxBORDER_SIMPLE)
	, owner_(owner)
	{
		auto *sizer = new wxBoxSizer(wxVERTICAL);
		list_ = new FontPreviewListBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
		sizer->Add(list_, 1, wxEXPAND, 0);
		SetSizer(sizer);

		list_->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &evt) {
			if (list_->GetSelection() != wxNOT_FOUND)
				owner_->AcceptPopupSelection();
			evt.Skip();
		});
	}

	void SetFonts(const wxArrayString &fonts) {
		fonts_ = fonts;
		list_->SetFonts(fonts_, false);
	}

	void ShowBelow(wxWindow *anchor) {
		const wxSize anchor_size = anchor->GetSize();
		const int row_count = static_cast<int>(std::min<size_t>(fonts_.size(), 10));
		const int popup_height = row_count > 0 ? row_count * anchor->FromDIP(28) + anchor->FromDIP(6) : anchor->FromDIP(60);
		SetSize(anchor->ClientToScreen(wxPoint(0, anchor_size.y)).x,
			anchor->ClientToScreen(wxPoint(0, anchor_size.y)).y,
			anchor_size.x,
			popup_height);
		Show();
		Raise();
	}

	bool IsShownPopup() const { return IsShown(); }

	void SelectBestMatch(const wxString &input) {
		if (fonts_.empty()) return;
		int idx = 0;
		for (size_t i = 0; i < fonts_.size(); ++i) {
			if (fonts_[i].CmpNoCase(input) == 0 || GetFontPreviewFaceName(fonts_[i]).CmpNoCase(input) == 0) {
				idx = static_cast<int>(i);
				break;
			}
		}
		list_->SetSelection(idx);
		list_->ScrollToRow(idx);
		list_->PrepareForSmoothScroll(idx);
	}

	void MoveSelection(int delta) {
		if (fonts_.empty()) return;
		int sel = list_->GetSelection();
		if (sel == wxNOT_FOUND) sel = 0;
		sel = std::max(0, std::min(sel + delta, static_cast<int>(fonts_.size()) - 1));
		list_->SetSelection(sel);
		list_->ScrollToRow(sel);
	}

	wxString GetSelectedFont() const {
		int sel = list_->GetSelection();
		if (sel == wxNOT_FOUND || static_cast<size_t>(sel) >= fonts_.size())
			return wxString();
		return fonts_[sel];
	}
};

FontSelectionControl::FontSelectionControl(wxWindow *parent, wxWindowID id, const wxString &value,
	const wxPoint &pos, const wxSize &size, long style)
	: wxPanel(parent, id, pos, size, style)
	, popup_(nullptr)
{
	auto *sizer = new wxBoxSizer(wxHORIZONTAL);
	textCtrl_ = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
	const int control_height = textCtrl_->GetBestSize().GetHeight();
	dropButton_ = new wxButton(this, wxID_ANY, wxS("▼"), wxDefaultPosition, wxSize(FromDIP(26), control_height), wxBU_EXACTFIT);
	dropButton_->SetMinSize(wxSize(FromDIP(26), control_height));
	sizer->Add(textCtrl_, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL, 0);
	sizer->Add(dropButton_, 0, wxEXPAND | wxALIGN_CENTER_VERTICAL, 0);
	SetSizer(sizer);
	SetMinSize(size.GetWidth() > 0 ? wxSize(size.GetWidth(), control_height) : wxSize(-1, control_height));
	popup_ = new FontSelectionPopupWindow(this);

	textCtrl_->Bind(wxEVT_TEXT, &FontSelectionControl::OnText, this);
	textCtrl_->Bind(wxEVT_TEXT_ENTER, &FontSelectionControl::OnTextEnter, this);
	textCtrl_->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &evt) {
		textCtrl_->CallAfter([this]() { SelectAllText(); });
		evt.Skip();
	});
	textCtrl_->Bind(wxEVT_KILL_FOCUS, &FontSelectionControl::OnKillFocus, this);
	textCtrl_->Bind(wxEVT_KEY_DOWN, &FontSelectionControl::OnTextKeyDown, this);
	dropButton_->Bind(wxEVT_BUTTON, &FontSelectionControl::OnButton, this);
	dropButton_->Bind(wxEVT_KILL_FOCUS, &FontSelectionControl::OnKillFocus, this);
}

FontSelectionControl::~FontSelectionControl() {
	if (popup_)
		popup_->Destroy();
}

void FontSelectionControl::SetFontList(const wxArrayString &fonts) {
	allFonts_ = fonts;
	filteredFonts_ = allFonts_;
}

void FontSelectionControl::Clear() {
	allFonts_.Clear();
	filteredFonts_.Clear();
	Dismiss();
}

wxString FontSelectionControl::GetValue() const {
	return textCtrl_->GetValue();
}

void FontSelectionControl::SetValue(const wxString &value) {
	suppressTextEvent_ = true;
	textCtrl_->SetValue(value);
	suppressTextEvent_ = false;
}

long FontSelectionControl::GetInsertionPoint() const {
	return textCtrl_->GetInsertionPoint();
}

void FontSelectionControl::SetInsertionPoint(long pos) {
	textCtrl_->SetInsertionPoint(pos);
}

void FontSelectionControl::Popup() {
	UpdateFilteredFonts(true);
}

void FontSelectionControl::PopupAll() {
	filteredFonts_ = allFonts_;
	if (!popup_ || filteredFonts_.empty()) {
		Dismiss();
		return;
	}
	popup_->SetFonts(filteredFonts_);
	popup_->SelectBestMatch(textCtrl_->GetValue());
	popup_->ShowBelow(this);
	textCtrl_->SetFocus();
	textCtrl_->SetInsertionPointEnd();
}

void FontSelectionControl::Dismiss() {
	if (popup_ && popup_->IsShown())
		popup_->Hide();
}

bool FontSelectionControl::HasPopup() const {
	return popup_ && popup_->IsShownPopup();
}

void FontSelectionControl::AcceptPopupSelection() {
	if (!popup_) return;
	const wxString selected = popup_->GetSelectedFont();
	if (selected.empty()) return;
	SetValue(selected);
	Dismiss();
	textCtrl_->SetFocus();
	textCtrl_->SetInsertionPointEnd();
	EmitEvent(wxEVT_COMBOBOX, selected);
	EmitEvent(wxEVT_TEXT, selected);
}

void FontSelectionControl::MovePopupSelection(int delta) {
	if (popup_ && popup_->IsShownPopup())
		popup_->MoveSelection(delta);
}

void FontSelectionControl::SelectAllText() {
	textCtrl_->SelectAll();
}

void FontSelectionControl::SetControlHeight(int height) {
	textCtrl_->SetMinSize(wxSize(-1, height));
	textCtrl_->SetMaxSize(wxSize(-1, height));
	dropButton_->SetMinSize(wxSize(FromDIP(26), height));
	dropButton_->SetMaxSize(wxSize(-1, height));
	SetMinSize(wxSize(GetMinSize().GetWidth(), height));
	SetMaxSize(wxSize(-1, height));
	Layout();
}

wxTextCtrl *FontSelectionControl::GetTextCtrl() const {
	return textCtrl_;
}

void FontSelectionControl::UpdateFilteredFonts(bool show_popup) {
	const wxString input = textCtrl_->GetValue();
	const wxString input_lower = input.Lower();
	filteredFonts_.Clear();

	if (input.empty()) {
		filteredFonts_ = allFonts_;
	}
	else {
		for (const auto &font : allFonts_) {
			if (font.Lower().Contains(input_lower) || GetFontPreviewFaceName(font).Lower().Contains(input_lower))
				filteredFonts_.Add(font);
		}
	}

	if (!show_popup || filteredFonts_.empty()) {
		Dismiss();
		return;
	}

	popup_->SetFonts(filteredFonts_);
	popup_->SelectBestMatch(input);
	popup_->ShowBelow(this);
}

void FontSelectionControl::EmitEvent(wxEventType type, const wxString &value) {
	wxCommandEvent evt(type, GetId());
	evt.SetEventObject(this);
	evt.SetString(value.empty() ? GetValue() : value);
	GetEventHandler()->ProcessEvent(evt);
}

void FontSelectionControl::OnText(wxCommandEvent &event) {
	if (!suppressTextEvent_)
		UpdateFilteredFonts(true);
	EmitEvent(wxEVT_TEXT, event.GetString());
}

void FontSelectionControl::OnTextEnter(wxCommandEvent &) {
	if (HasPopup())
		AcceptPopupSelection();
	EmitEvent(wxEVT_TEXT_ENTER, GetValue());
}

void FontSelectionControl::OnButton(wxCommandEvent &) {
	if (HasPopup()) Dismiss();
	else PopupAll();
	textCtrl_->SetFocus();
}

void FontSelectionControl::OnKillFocus(wxFocusEvent &event) {
	CallAfter([this]() {
		wxWindow *focus = wxWindow::FindFocus();
		if (focus != textCtrl_ && focus != dropButton_ && focus != popup_ && !(focus && popup_ && popup_->IsDescendant(focus)))
			Dismiss();
	});
	event.Skip();
}

void FontSelectionControl::OnTextKeyDown(wxKeyEvent &event) {
	switch (event.GetKeyCode()) {
	case WXK_DOWN:
		if (!HasPopup()) PopupAll();
		else MovePopupSelection(1);
		break;
	case WXK_UP:
		if (HasPopup()) MovePopupSelection(-1);
		break;
	case WXK_ESCAPE:
		Dismiss();
		break;
	default:
		event.Skip();
		return;
	}
}

// 从指定文件夹获取字体信息，仅仅是方便找到字体，字体还是需要安装注册在系统上的
wxArrayString LoadFontsFromDirectory(const wxString &directory) {
	wxArrayString fontNames;
	const wxDir dir(directory);
	if (!dir.IsOpened()) {
		return fontNames;
	}

	FT_Library library;
	if (FT_Init_FreeType(&library)) {
		return fontNames;
	}
	auto ft_cleanup = [&library]() { FT_Done_FreeType(library); };
	struct FTGuard {
		decltype(ft_cleanup)& fn;
		~FTGuard() { fn(); }
	} ft_guard{ft_cleanup};

	const auto use_font_filename = OPT_GET("Subtitle/Use Font Filename")->GetBool();
	wxString filename;
	bool cont = dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES);
	while (cont) {
		wxString filePath = directory + wxFileName::GetPathSeparator() + filename;

		FT_Face face;
		if (FT_New_Face(library, filePath.mb_str(), 0, &face)) {} else {
			if (face->family_name) {
				if (!use_font_filename) {
					wxString faceName = to_wx(face->family_name);
					fontNames.Add(faceName);
				} else {
					fontNames.Add(wxFileName(filename).GetName());
				}
			}
			FT_Done_Face(face);
		}

		cont = dir.GetNext(&filename);
	}

	SortFontFaceList(fontNames);
	return fontNames;
}

/// Style rename helper that walks a file searching for a style and optionally
/// updating references to it
class StyleRenamer {
	agi::Context *c;
	bool found_any = false;
	std::string source_name;
	std::string new_name;

	/// @brief 在文本中直接替换覆写块内的 \r 标签样式引用
	///
	/// 使用字符串级别的定向替换而非 ParseTags/UpdateText 往返，
	/// 避免对含 Lua 模板代码等非标准内容造成误修改。
	/// @param text 对话行原始文本
	/// @param replace 是否执行替换（false 时仅检测是否存在引用）
	/// @return 替换后的文本（replace=false 时返回值无意义）
	std::string ReplaceStyleInOverrides(const std::string& text, bool replace) {
		std::string result;
		result.reserve(text.size());
		std::string target = "\\r" + source_name;

		size_t pos = 0;
		while (pos < text.size()) {
			if (text[pos] == '{') {
				size_t end = text.find('}', pos);
				if (end == std::string::npos) {
					result += text.substr(pos);
					break;
				}

				// 仅处理标准 ASS 覆写块，跳过 {!...!} 等模板/脚本块
				if (pos + 1 >= text.size() || text[pos + 1] != '\\') {
					result += text.substr(pos, end - pos + 1);
					pos = end + 1;
					continue;
				}

				// 在覆写块内搜索 \rSourceName
				size_t bpos = pos;
				size_t block_end = end + 1;
				while (bpos < block_end) {
					size_t found = text.find(target, bpos);
					if (found == std::string::npos || found >= block_end) {
						result += text.substr(bpos, block_end - bpos);
						break;
					}

					// 验证匹配位置：\r 标签参数以 \ 或 } 结束
					size_t after = found + target.size();
					if (after >= text.size() || text[after] == '\\' || text[after] == '}') {
						if (replace) {
							result += text.substr(bpos, found - bpos);
							result += "\\r" + new_name;
							bpos = after;
						}
						else {
							found_any = true;
							return {};
						}
					}
					else {
						result += text.substr(bpos, after - bpos);
						bpos = after;
					}
				}
				pos = block_end;
			}
			else {
				size_t next = text.find('{', pos);
				if (next == std::string::npos) {
					result += text.substr(pos);
					break;
				}
				result += text.substr(pos, next - pos);
				pos = next;
			}
		}
		return result;
	}

	void Walk(bool replace) {
		found_any = false;

		for (auto& diag : c->ass->Events) {
			if (diag.Style == source_name) {
				if (replace)
					diag.Style = new_name;
				else
					found_any = true;
			}

			const std::string& text = diag.Text.get();
			if (text.find("\\r") != std::string::npos) {
				std::string new_text = ReplaceStyleInOverrides(text, replace);
				if (found_any) return;
				if (replace && new_text != text)
					diag.Text = std::move(new_text);
			}

			if (found_any) return;
		}
	}

public:
	StyleRenamer(agi::Context *c, std::string source_name, std::string new_name)
	: c(c)
	, source_name(std::move(source_name))
	, new_name(std::move(new_name))
	{
	}

	/// Check if there are any uses of the original style name in the file
	bool NeedsReplace() {
		Walk(false);
		return found_any;
	}

	/// Replace all uses of the original style name with the new one
	void Replace() {
		Walk(true);
	}
};

DialogStyleEditor::DialogStyleEditor(wxWindow *parent, AssStyle *style, agi::Context *c, AssStyleStorage *store,
	std::string const& new_name, wxArrayString const& font_list,
	std::shared_future<wxArrayString> deferred_font_list)
: wxDialog (parent, -1, _("Style Editor"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, c(c)
, style(style)
, store(store)
, deferredFontList_(std::move(deferred_font_list))
, fontListTimer_(this)
{
	if (new_name.size()) {
		is_new = true;
		style = this->style = new AssStyle(*style);
		style->name = new_name;
	}
	else if (!style) {
		is_new = true;
		style = this->style = new AssStyle;
	}

	work = std::make_unique<AssStyle>(*style);

	SetIcons(GETICONS(style_toolbutton));

	auto add_with_label = [&](wxSizer *sizer, wxWindow *labelParent, wxString const& label, wxWindow *ctrl) {
		sizer->Add(new wxStaticText(labelParent, -1, label), wxSizerFlags().Center().Border(wxLEFT | wxRIGHT));
		sizer->Add(ctrl, wxSizerFlags(ctrl->GetBestSize().GetWidth()).Left().Expand());
	};
	auto num_text_ctrl = [&](wxWindow *parent, double *value, double min, double max, double step, int precision) -> wxSpinCtrlDouble * {
		auto scd = new wxSpinCtrlDouble(parent, -1, "", wxDefaultPosition,
			wxDefaultSize, wxSP_ARROW_KEYS, min, max, *value, step);
		scd->SetDigits(precision);
		scd->SetValidator(DoubleSpinValidator(value));
		scd->Bind(wxEVT_SPINCTRLDOUBLE, [=, this](wxSpinDoubleEvent &evt) {
			evt.Skip();
			if (updating) return;

			bool old = updating;
			updating = true;
			scd->GetValidator()->TransferFromWindow();
			updating = old;
			SubsPreview->SetStyle(*work);
		});
		return scd;
	};

	// Prepare control values
	wxString EncodingValue = std::to_wstring(style->encoding);
	wxString alignValues[9] = { "7", "8", "9", "4", "5", "6", "1", "2", "3" };
	wxString borderStyleValues[3] = { _("Outline"), _("Border boxes"), _("Shadow box (libass only)") };

	// Encoding options
	wxArrayString encodingStrings;
	AssStyle::GetEncodings(encodingStrings);

	// Create sizers
	auto *NameSizer = new wxStaticBoxSizer(wxHORIZONTAL, this, _("Style Name"));
	auto *FontSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Font"));
	auto *ColorsSizer = new wxStaticBoxSizer(wxHORIZONTAL, this, _("Colors"));
	auto *MarginSizer = new wxStaticBoxSizer(wxHORIZONTAL, this, _("Margins"));
	auto *OutlineSizer = new wxStaticBoxSizer(wxHORIZONTAL, this, _("Outline"));
	auto *MiscSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Miscellaneous"));
	auto *PreviewSizer = new wxStaticBoxSizer(wxVERTICAL, this, _("Preview"));

	auto *NameSizerBox = NameSizer->GetStaticBox();
	auto *FontSizerBox = FontSizer->GetStaticBox();
	auto *ColorsSizerBox = ColorsSizer->GetStaticBox();
	auto *MarginSizerBox = MarginSizer->GetStaticBox();
	auto *OutlineSizerBox = OutlineSizer->GetStaticBox();
	auto *MiscSizerBox = MiscSizer->GetStaticBox();
	auto *PreviewSizerBox = PreviewSizer->GetStaticBox();

	// Create controls
	StyleName = new wxTextCtrl(NameSizerBox, -1, to_wx(style->name));
	FontName = new FontSelectionControl(FontSizerBox, -1, to_wx(style->font), wxDefaultPosition, this->FromDIP(wxSize(220, -1)));
	auto FontSize = num_text_ctrl(FontSizerBox, &work->fontsize, 0, 10000.0, 1.0, 0);
	FontName->SetControlHeight(FontSize->GetBestSize().GetHeight());
	BoxBold = new wxCheckBox(FontSizerBox, -1, _("&Bold"));
	BoxItalic = new wxCheckBox(FontSizerBox, -1, _("&Italic"));
	BoxUnderline = new wxCheckBox(FontSizerBox, -1, _("&Underline"));
	BoxStrikeout = new wxCheckBox(FontSizerBox, -1, _("&Strikeout"));
	ColourButton *colorButton[] = {
		new ColourButton(ColorsSizerBox, this->FromDIP(wxSize(55, 16)), true, style->primary, ColorValidator(&work->primary)),
		new ColourButton(ColorsSizerBox, this->FromDIP(wxSize(55, 16)), true, style->secondary, ColorValidator(&work->secondary)),
		new ColourButton(ColorsSizerBox, this->FromDIP(wxSize(55, 16)), true, style->outline, ColorValidator(&work->outline)),
		new ColourButton(ColorsSizerBox, this->FromDIP(wxSize(55, 16)), true, style->shadow, ColorValidator(&work->shadow))
	};
	for (int i = 0; i < 3; i++) {
		margin[i] = new wxSpinCtrl(MarginSizerBox, -1, std::to_wstring(style->Margin[i]),
			wxDefaultPosition, wxDefaultSize,
			wxSP_ARROW_KEYS, -9999, 9999, style->Margin[i]);
		margin[i]->SetInitialSize(margin[i]->GetSizeFromTextSize(GetTextExtent(wxS("0000"))));
	}

	Alignment = new wxRadioBox(this, -1, _("Alignment"), wxDefaultPosition, wxDefaultSize, 9, alignValues, 3, wxRA_SPECIFY_COLS);
	auto Outline = num_text_ctrl(OutlineSizerBox, &work->outline_w, 0.0, 1000.0, 0.1, 2);
	auto Shadow = num_text_ctrl(OutlineSizerBox, &work->shadow_w, 0.0, 1000.0, 0.1, 2);
	OutlineType = new wxComboBox(OutlineSizerBox, -1, "", wxDefaultPosition, wxDefaultSize, 3, borderStyleValues, wxCB_READONLY);
	auto ScaleX = num_text_ctrl(MiscSizerBox, &work->scalex, 0.0, 10000.0, 1, 2);
	auto ScaleY = num_text_ctrl(MiscSizerBox, &work->scaley, 0.0, 10000.0, 1, 2);
	auto Angle = num_text_ctrl(MiscSizerBox, &work->angle, -360.0, 360.0, 1.0, 2);
	auto Spacing = num_text_ctrl(MiscSizerBox, &work->spacing, 0.0, 1000.0, 0.1, 3);
	Encoding = new wxComboBox(MiscSizerBox, -1, "", wxDefaultPosition, wxDefaultSize, encodingStrings, wxCB_READONLY);

	// Set control tooltips
	StyleName->SetToolTip(_("Style name"));
	FontName->SetToolTip(_("Font face"));
	FontSize->SetToolTip(_("Font size"));
	colorButton[0]->SetToolTip(_("Choose primary color"));
	colorButton[1]->SetToolTip(_("Choose secondary color"));
	colorButton[2]->SetToolTip(_("Choose outline color"));
	colorButton[3]->SetToolTip(_("Choose shadow color"));
	margin[0]->SetToolTip(_("Distance from left edge, in pixels"));
	margin[1]->SetToolTip(_("Distance from right edge, in pixels"));
	margin[2]->SetToolTip(_("Distance from top/bottom edge, in pixels"));
	OutlineType->SetToolTip(_("Whether to draw a normal outline or opaque boxes around the text"));
	Outline->SetToolTip(_("Outline width, in pixels"));
	Shadow->SetToolTip(_("Shadow distance, in pixels"));
	ScaleX->SetToolTip(_("Scale X, in percentage"));
	ScaleY->SetToolTip(_("Scale Y, in percentage"));
	Angle->SetToolTip(_("Angle to rotate in Z axis, in degrees"));
	Encoding->SetToolTip(_("Encoding, only useful in unicode if the font doesn't have the proper unicode mapping"));
	Spacing->SetToolTip(_("Character spacing, in pixels"));
	Alignment->SetToolTip(_("Alignment in screen, in numpad style"));

	// Set up controls
	BoxBold->SetValue(style->bold);
	BoxItalic->SetValue(style->italic);
	BoxUnderline->SetValue(style->underline);
	BoxStrikeout->SetValue(style->strikeout);
	OutlineType->SetSelection(BorderStyleToControl(style->borderstyle));
	Alignment->SetSelection(AlignToControl(style->alignment));
	// Fill font face list box
	const auto font_list_mode = ResolveStyleEditorFontListLoadMode(!font_list.empty(), deferredFontList_.valid());
	switch (font_list_mode) {
		case StyleEditorFontListLoadMode::UseProvidedList:
			fontList_ = font_list;
			break;
		case StyleEditorFontListLoadMode::WaitForAsyncList:
			fontList_.Clear();
			break;
		case StyleEditorFontListLoadMode::EnumerateSynchronously:
		default:
			fontList_ = GetPreferredFontFaceList();
			break;
	}
	FontName->Freeze();
	FontName->SetFontList(fontList_);
	FontName->SetValue(to_wx(style->font));
	FontName->Thaw();

	// Set encoding value
	bool found = false;
	for (size_t i=0;i<encodingStrings.Count();i++) {
		if (encodingStrings[i].StartsWith(EncodingValue)) {
			Encoding->Select(i);
			found = true;
			break;
		}
	}
	if (!found) Encoding->Select(2);

	// Style name sizer
	NameSizer->Add(StyleName, 1, 0, 0);

	// Font sizer
	wxSizer *FontSizerTop = new wxBoxSizer(wxHORIZONTAL);
	wxSizer *FontSizerBottom = new wxBoxSizer(wxHORIZONTAL);
	FontSizerTop->Add(FontName, 1, wxALIGN_CENTER_VERTICAL, 0);
	FontSizerTop->Add(FontSize, 0, wxLEFT | wxALIGN_CENTER_VERTICAL, 5);
	FontSizerBottom->AddStretchSpacer(1);
	FontSizerBottom->Add(BoxBold, 0, 0, 0);
	FontSizerBottom->Add(BoxItalic, 0, wxLEFT, 5);
	FontSizerBottom->Add(BoxUnderline, 0, wxLEFT, 5);
	FontSizerBottom->Add(BoxStrikeout, 0, wxLEFT, 5);
	FontSizerBottom->AddStretchSpacer(1);
	FontSizer->Add(FontSizerTop, 1, wxEXPAND, 0);
	FontSizer->Add(FontSizerBottom, 1, wxTOP | wxEXPAND, 5);

	// Colors sizer
	wxString colorLabels[] = { _("Primary"), _("Secondary"), _("Outline"), _("Shadow") };
	ColorsSizer->AddStretchSpacer(1);
	for (int i = 0; i < 4; ++i) {
		auto sizer = new wxBoxSizer(wxVERTICAL);
		sizer->Add(new wxStaticText(ColorsSizerBox, -1, colorLabels[i]), 0, wxBOTTOM | wxALIGN_CENTER, 5);
		sizer->Add(colorButton[i], 0, wxBOTTOM | wxALIGN_CENTER, 5);
		ColorsSizer->Add(sizer, 0, wxLEFT, i?5:0);
	}
	ColorsSizer->AddStretchSpacer(1);

	// Margins
	wxString marginLabels[] = { _("Left"), _("Right"), _("Vert") };
	MarginSizer->AddStretchSpacer(1);
	for (int i=0;i<3;i++) {
		auto sizer = new wxBoxSizer(wxVERTICAL);
		sizer->AddStretchSpacer(1);
		sizer->Add(new wxStaticText(MarginSizerBox, -1, marginLabels[i]), 0, wxCENTER, 0);
		sizer->Add(margin[i], 0, wxTOP | wxCENTER, 5);
		sizer->AddStretchSpacer(1);
		MarginSizer->Add(sizer, 0, wxEXPAND | wxLEFT, i?5:0);
	}
	MarginSizer->AddStretchSpacer(1);

	// Margins+Alignment
	wxSizer *MarginAlign = new wxBoxSizer(wxHORIZONTAL);
	MarginAlign->Add(MarginSizer, 1, wxEXPAND, 0);
	MarginAlign->Add(Alignment, 0, wxLEFT | wxEXPAND, 5);

	// Outline
	add_with_label(OutlineSizer, OutlineSizerBox, _("Outline:"), Outline);
	add_with_label(OutlineSizer, OutlineSizerBox, _("Shadow:"), Shadow);
	add_with_label(OutlineSizer, OutlineSizerBox, _("Border style:"), OutlineType);

	// Misc
	auto MiscBoxTop = new wxFlexGridSizer(2, 4, 5, 5);
	add_with_label(MiscBoxTop, MiscSizerBox, _("Scale X%:"), ScaleX);
	add_with_label(MiscBoxTop, MiscSizerBox, _("Scale Y%:"), ScaleY);
	add_with_label(MiscBoxTop, MiscSizerBox, _("Rotation:"), Angle);
	add_with_label(MiscBoxTop, MiscSizerBox, _("Spacing:"), Spacing);

	wxSizer *MiscBoxBottom = new wxBoxSizer(wxHORIZONTAL);
	add_with_label(MiscBoxBottom, MiscSizerBox, _("Encoding:"), Encoding);

	MiscSizer->Add(MiscBoxTop, wxSizerFlags().Expand());
	MiscSizer->Add(MiscBoxBottom, wxSizerFlags().Expand().Border(wxTOP));

	// Preview
	const auto previewButton = new ColourButton(PreviewSizerBox, this->FromDIP(wxSize(45, 16)), false, GetThemeOptValue("Colour/Style Editor/Background/Preview")->GetColor());
	PreviewText = new wxTextCtrl(PreviewSizerBox, -1, to_wx(OPT_GET("Tool/Style Editor/Preview Text")->GetString()));
	SubsPreview = new SubtitlesPreview(PreviewSizerBox, this->FromDIP(wxSize(100, 60)), (IsDarkMode() ? wxBORDER_SIMPLE : wxSUNKEN_BORDER), GetThemeOptValue("Colour/Style Editor/Background/Preview")->GetColor());

	SubsPreview->SetToolTip(_("Preview of current style"));
	SubsPreview->SetStyle(*style);
	SubsPreview->SetText(from_wx(PreviewText->GetValue()));
	PreviewText->SetToolTip(_("Text to be used for the preview"));
	previewButton->SetToolTip(_("Color of preview background"));

	wxSizer *PreviewBottomSizer = new wxBoxSizer(wxHORIZONTAL);
	PreviewBottomSizer->Add(PreviewText, 1, wxEXPAND | wxRIGHT, 5);
	PreviewBottomSizer->Add(previewButton, 0, wxEXPAND, 0);
	PreviewSizer->Add(SubsPreview, 1, wxEXPAND | wxBOTTOM, 5);
	PreviewSizer->Add(PreviewBottomSizer, 0, wxEXPAND, 0);

	// Buttons
	auto ButtonSizer = CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxAPPLY | wxHELP);
	ButtonSizer->GetHelpButton()->SetLabel(_("Help"));

	// Left side sizer
	wxSizer *LeftSizer = new wxBoxSizer(wxVERTICAL);
	LeftSizer->Add(NameSizer, 0, wxBOTTOM | wxEXPAND, 5);
	LeftSizer->Add(FontSizer, 0, wxBOTTOM | wxEXPAND, 5);
	LeftSizer->Add(ColorsSizer, 0, wxBOTTOM | wxEXPAND, 5);
	LeftSizer->Add(MarginAlign, 0, wxEXPAND, 0);

	// Right side sizer
	wxSizer *RightSizer = new wxBoxSizer(wxVERTICAL);
	RightSizer->Add(OutlineSizer, wxSizerFlags().Expand().Border(wxBOTTOM));
	RightSizer->Add(MiscSizer, wxSizerFlags().Expand().Border(wxBOTTOM));
	RightSizer->Add(PreviewSizer, wxSizerFlags(1).Expand());

	// Controls Sizer
	wxSizer *ControlSizer = new wxBoxSizer(wxHORIZONTAL);
	ControlSizer->Add(LeftSizer, 0, wxEXPAND, 0);
	ControlSizer->Add(RightSizer, 1, wxLEFT | wxEXPAND, 5);

	// General Layout
	wxSizer *MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(ControlSizer, 1, wxALL | wxEXPAND, 5);
	MainSizer->Add(ButtonSizer, 0, wxBOTTOM | wxEXPAND, 5);

	SetSizerAndFit(MainSizer);

	// Force the style name text field to scroll based on its final size, rather
	// than its initial size
	StyleName->SetInsertionPoint(0);
	StyleName->SetInsertionPoint(-1);

	persist = std::make_unique<PersistLocation>(this, "Tool/Style Editor", true);

	Bind(wxEVT_CHILD_FOCUS, &DialogStyleEditor::OnChildFocus, this);

	Bind(wxEVT_CHECKBOX, &DialogStyleEditor::OnCommandPreviewUpdate, this);
	Bind(wxEVT_COMBOBOX, &DialogStyleEditor::OnCommandPreviewUpdate, this);
	Bind(wxEVT_SPINCTRL, &DialogStyleEditor::OnCommandPreviewUpdate, this);

	previewButton->Bind(EVT_COLOR, &DialogStyleEditor::OnPreviewColourChange, this);
	FontName->Bind(wxEVT_TEXT_ENTER, &DialogStyleEditor::OnCommandPreviewUpdate, this);
	FontName->Bind(wxEVT_COMBOBOX, &DialogStyleEditor::OnCommandPreviewUpdate, this);
	FontName->Bind(wxEVT_TEXT, &DialogStyleEditor::OnFontNameText, this);
	PreviewText->Bind(wxEVT_TEXT, &DialogStyleEditor::OnPreviewTextChange, this);

	Bind(wxEVT_BUTTON, std::bind(&DialogStyleEditor::Apply, this, true, true), wxID_OK);
	Bind(wxEVT_BUTTON, std::bind(&DialogStyleEditor::Apply, this, true, false), wxID_APPLY);
	Bind(wxEVT_BUTTON, std::bind(&DialogStyleEditor::Apply, this, false, true), wxID_CANCEL);
	Bind(wxEVT_BUTTON, std::bind(&HelpButton::OpenPage, "Style Editor"), wxID_HELP);
	Bind(wxEVT_TIMER, &DialogStyleEditor::OnDeferredFontListTimer, this, fontListTimer_.GetId());
	if (font_list_mode == StyleEditorFontListLoadMode::WaitForAsyncList)
		StartDeferredFontListLoad();

	for (auto const& elem : colorButton)
		elem->Bind(EVT_COLOR, &DialogStyleEditor::OnSetColor, this);
}

DialogStyleEditor::~DialogStyleEditor() {
	if (fontListTimer_.IsRunning())
		fontListTimer_.Stop();
	if (is_new)
		delete style;
}

void DialogStyleEditor::StartDeferredFontListLoad() {
	if (!deferredFontList_.valid())
		return;

	if (deferredFontList_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
		fontList_ = ResolveReadyPreferredFontFaceList(deferredFontList_);
		FontName->SetFontList(fontList_);
		return;
	}

	if (!fontListTimer_.IsRunning())
		fontListTimer_.Start(100);
}

void DialogStyleEditor::OnDeferredFontListTimer(wxTimerEvent &) {
	if (!deferredFontList_.valid()) {
		fontListTimer_.Stop();
		return;
	}

	if (deferredFontList_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
		return;

	fontListTimer_.Stop();
	fontList_ = ResolveReadyPreferredFontFaceList(deferredFontList_);
	FontName->Freeze();
	FontName->SetFontList(fontList_);
	FontName->Thaw();
}

std::string DialogStyleEditor::GetStyleName() const {
	return style->name;
}

void DialogStyleEditor::Apply(bool apply, bool close) {
	if (apply) {
		std::string new_name = from_wx(StyleName->GetValue());

		// Get list of existing styles
		std::vector<std::string> styles = store ? store->GetNames() : c->ass->GetStyles();

		// Check if style name is unique
		AssStyle *existing = store ? store->GetStyle(new_name) : c->ass->GetStyle(new_name);
		if (existing && existing != style) {
			wxMessageBox(_("There is already a style with this name. Please choose another name."), _("Style name conflict"), wxOK | wxICON_ERROR | wxCENTER);
			return;
		}

		// Style name change
		bool did_rename = false;
		if (work->name != new_name) {
			if (!store && !is_new) {
				StyleRenamer renamer(c, work->name, new_name);
				if (renamer.NeedsReplace()) {
					// See if user wants to update style name through script
					int answer = wxMessageBox(
						_("Do you want to change all instances of this style in the script to this new name?"),
						_("Update script?"),
						wxYES_NO | wxCANCEL);

					if (answer == wxCANCEL) return;

					if (answer == wxYES) {
						did_rename = true;
						renamer.Replace();
					}
				}
			}

			work->name = new_name;
		}

		UpdateWorkStyle();

		*style = *work;
		style->UpdateData();
		if (is_new) {
			if (store)
				store->push_back(std::unique_ptr<AssStyle>(style));
			else
				c->ass->Styles.push_back(*style);
			is_new = false;
		}
		if (!store)
			c->ass->Commit(_("style change"), AssFile::COMMIT_STYLES | (did_rename ? AssFile::COMMIT_DIAG_FULL : 0));

		// Update preview
		if (!close) SubsPreview->SetStyle(*style);
	}

	if (close) {
		EndModal(apply);
		if (PreviewText)
			OPT_SET("Tool/Style Editor/Preview Text")->SetString(from_wx(PreviewText->GetValue()));
	}
}

void DialogStyleEditor::UpdateWorkStyle() {
	updating = true;
	TransferDataFromWindow();
	updating = false;

	work->font = from_wx(FontName->GetValue());

	wxString encoding_selection = Encoding->GetValue();
	wxString encoding_num = encoding_selection.substr(0, 1) + encoding_selection.substr(1).BeforeFirst('-');	// Have to account for -1
	long templ = 0;
	encoding_num.ToLong(&templ);
	work->encoding = templ;

	work->borderstyle = ControlToBorderStyle(OutlineType->GetSelection());

	work->alignment = ControlToAlign(Alignment->GetSelection());

	for (size_t i = 0; i < 3; ++i)
		work->Margin[i] = margin[i]->GetValue();

	work->bold = BoxBold->IsChecked();
	work->italic = BoxItalic->IsChecked();
	work->underline = BoxUnderline->IsChecked();
	work->strikeout = BoxStrikeout->IsChecked();
}

void DialogStyleEditor::OnSetColor(ValueEvent<agi::Color>&) {
	TransferDataFromWindow();
	SubsPreview->SetStyle(*work);
}

void DialogStyleEditor::OnChildFocus(wxChildFocusEvent &event) {
	UpdateWorkStyle();
	SubsPreview->SetStyle(*work);
	event.Skip();
}

void DialogStyleEditor::OnFontNameText(wxCommandEvent& event) {
	if (updating) return;
	UpdateWorkStyle();
	SubsPreview->SetStyle(*work);
	event.Skip();
}

void DialogStyleEditor::OnPreviewTextChange (wxCommandEvent &event) {
	SubsPreview->SetText(from_wx(PreviewText->GetValue()));
	event.Skip();
}

void DialogStyleEditor::OnPreviewColourChange(ValueEvent<agi::Color> &evt) {
	SubsPreview->SetColour(evt.Get());
	OPT_SET("Colour/Style Editor/Background/Preview")->SetColor(evt.Get());
}

void DialogStyleEditor::OnCommandPreviewUpdate(wxCommandEvent &event) {
	if (event.GetEventType() == wxEVT_COMBOBOX)
		RecordFavoriteFontFace(event.GetString());

	UpdateWorkStyle();
	SubsPreview->SetStyle(*work);
	event.Skip();
}

int DialogStyleEditor::ControlToAlign(int n) {
	switch (n) {
		case 0: return 7;
		case 1: return 8;
		case 2: return 9;
		case 3: return 4;
		case 4: return 5;
		case 5: return 6;
		case 6: return 1;
		case 7: return 2;
		case 8: return 3;
		default: return 2;
	}
}

int DialogStyleEditor::AlignToControl(int n) {
	switch (n) {
		case 7: return 0;
		case 8: return 1;
		case 9: return 2;
		case 4: return 3;
		case 5: return 4;
		case 6: return 5;
		case 1: return 6;
		case 2: return 7;
		case 3: return 8;
		default: return 7;
	}
}

int DialogStyleEditor::ControlToBorderStyle(int choice) {
	switch (choice) {
		case 1: return 3;
		case 2: return 4;
		case 0:
		default:
			return 1;
	}
}

int DialogStyleEditor::BorderStyleToControl(int border_style) {
	switch (border_style) {
		case 3: return 1;
		case 4: return 2;
		case 1:
		default:
			return 0;
	}
}
