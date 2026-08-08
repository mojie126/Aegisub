// Copyright (c) 2010, Amar Takhar <verm@aegisub.org>
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

/// @file preferences.cpp
/// @brief Preferences dialogue
/// @ingroup configuration_ui

#include "preferences.h"

#include "ass_style_storage.h"
#include "async_video_provider.h"
#include "audio_provider_factory.h"
#include "audio_renderer_waveform.h"
#include "command/command.h"
#include "compat.h"
#include "help_button.h"
#include "include/aegisub/context.h"
#include "hotkey_data_view_model.h"
#include "include/aegisub/audio_player.h"
#include "include/aegisub/hotkey.h"
#include "include/aegisub/subtitles_provider.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "preferences_base.h"
#include "project.h"
#include "video_frame.h"
#include "video_provider_manager.h"

#ifdef WITH_PORTAUDIO
#include "audio_player_portaudio.h"
#endif

#ifdef WITH_FFMS2
#include <ffms.h>
#endif

#include <libaegisub/hotkey.h>

#include <algorithm>

#include <wx/combobox.h>
#include <unordered_set>

#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/dc.h>
#include <wx/dirdlg.h>
#include <wx/dataview.h>
#include <wx/event.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/listctrl.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/srchctrl.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/treebook.h>

namespace {
/// @brief 当前视频提供者是否支持智能黑边预览
static bool SupportsSmartAbbPreview(const AsyncVideoProvider *provider) {
	if (!provider) return false;
	const auto decoder = provider->GetDecoderName();
	return decoder == "FFmpegSource" || decoder == "BestSource" || decoder == "VapourSynth";
}

/// @brief 生成智能黑边状态文案
static wxString BuildSmartAbbStatus(const AsyncVideoProvider *provider, bool enabled) {
	if (!enabled)
		return {};
	if (!provider)
		return _("Open a video to preview smart black borders.");
	if (!SupportsSmartAbbPreview(provider))
		return _("Current video provider does not support smart black borders.");

	const int current_top = std::max(0, provider->GetPaddingTop());
	const int current_bottom = std::max(0, provider->GetPaddingBottom());
	if (current_top <= 0 && current_bottom <= 0)
		return _("Current video: no black borders added.");

	return wxString::Format(
		_("Current video: top %d px, bottom %d px"),
		current_top,
		current_bottom
	);
}

/// @brief 获取已注册命令的排序列表
static wxArrayString get_registered_commands() {
	wxArrayString commands = to_wx(cmd::get_registered_commands());
	commands.Sort();
	return commands;
}

#ifdef __APPLE__
static void add_current_hotkey_commands(wxArrayString& commands, HotkeyDataViewModel *model, wxDataViewItem const& parent) {
	wxDataViewItemArray children;
	model->GetChildren(parent, children);

	for (auto const& child : children) {
		wxVariant value;
		model->GetValue(value, child, 1);
		wxString command = value.GetString();
		if (commands.Index(command) == wxNOT_FOUND)
			commands.Add(command);

		if (model->IsContainer(child))
			add_current_hotkey_commands(commands, model, child);
	}
}

static wxArrayString get_hotkey_command_choices(HotkeyDataViewModel *model) {
	wxArrayString commands = get_registered_commands();
	if (commands.Index("") == wxNOT_FOUND)
		commands.Add("");

	add_current_hotkey_commands(commands, model, wxDataViewItem(nullptr));
	commands.Sort();
	return commands;
}
#endif

/// General preferences page
void General(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("General"));

	auto general = p->PageSizer(_("General"));
	p->OptionAdd(general, _("Check for updates on startup"), "App/Auto/Check For Updates");
	p->OptionAdd(general, _("Check for automation script updates on startup"), "App/Auto/Dependency Check")->SetToolTip(_("Check for automation script updates on startup"));
	p->OptionAdd(general, _("Show main toolbar"), "App/Show Toolbar");
	p->OptionAdd(general, _("Save UI state in subtitles files"), "App/Save UI State");

	{
		const wxString icon_sizes_str[] = { "16 × 16", "24 × 24", "32 × 32", "48 × 48", "64 × 64" };
		const int icon_sizes_val[] = { 16, 24, 32, 48, 64 };
		const int n_icon_sizes = 5;
		parent->AddChangeableOption("App/Toolbar Icon Size");
		int cur = OPT_GET("App/Toolbar Icon Size")->GetInt();
		int sel = 0;
		for (int i = 0; i < n_icon_sizes; i++) {
			if (icon_sizes_val[i] == cur) { sel = i; break; }
		}
		wxArrayString icon_sizes_arr(n_icon_sizes, icon_sizes_str);
		auto icon_size_cb = new wxComboBox(general.box, -1, wxEmptyString, wxDefaultPosition, wxDefaultSize, icon_sizes_arr, wxCB_READONLY | wxCB_DROPDOWN);
		icon_size_cb->Select(sel);
		icon_size_cb->Bind(wxEVT_COMBOBOX, [parent](wxCommandEvent& evt) {
			evt.Skip();
			static const int sizes[] = { 16, 24, 32, 48, 64 };
			int idx = evt.GetInt();
			if (idx >= 0 && idx < 5)
				parent->SetOption(std::make_unique<agi::OptionValueInt>("App/Toolbar Icon Size", sizes[idx]));
		});
		general.sizer->Add(new wxStaticText(general.box, -1, _("Toolbar Icon Size")), 1, wxALIGN_CENTRE_VERTICAL);
		general.sizer->Add(icon_size_cb, wxSizerFlags().Expand());
	}
	wxString autoload_modes[] = { _("Never"), _("Always"), _("Ask") };
	wxArrayString autoload_modes_arr(3, autoload_modes);
	p->OptionChoice(general, _("Automatically load linked files"), autoload_modes_arr, "App/Auto/Load Linked Files");
	p->OptionAdd(general, _("Undo Levels"), "Limits/Undo Levels", {.min = 2, .max = 10000});

	auto recent = p->PageSizer(_("Recently Used Lists"));
	p->OptionAdd(recent, _("Files"), "Limits/MRU", {.min = 0, .max = 16});
	p->OptionAdd(recent, _("Find/Replace"), "Limits/Find Replace");

	p->SetSizerAndFit(p->sizer);
}

void General_DefaultStyles(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Default styles"), OptionPage::PAGE_SUB);

	auto staticbox = new wxStaticBoxSizer(wxVERTICAL, p, _("Default style catalogs"));
	p->sizer->Add(staticbox, 0, wxEXPAND, 5);
	p->sizer->AddSpacer(8);

	auto instructions = new wxStaticText(staticbox->GetStaticBox(), wxID_ANY, _("The chosen style catalogs will be loaded when you start a new file or import files in the various formats.\n\nYou can set up style catalogs in the Style Manager."));
	instructions->Wrap(book->FromDIP(460));
	p->sizer->Fit(p);
	staticbox->Add(instructions, 0, wxALL, 5);
	staticbox->AddSpacer(16);

	auto general = new wxFlexGridSizer(2, 5, 5);
	general->AddGrowableCol(0, 1);
	staticbox->Add(general, 1, wxEXPAND, 5);

	// Build a list of available style catalogs, and wished-available ones
	auto const& avail_catalogs = AssStyleStorage::GetCatalogs();
	std::unordered_set<std::string> catalogs_set(begin(avail_catalogs), end(avail_catalogs));
	// Always include one named "Default" even if it doesn't exist (ensure there is at least one on the list)
	catalogs_set.insert("Default");
	// Include all catalogs named in the existing configuration
	static const char *formats[] = { "ASS", "MicroDVD", "SRT", "TTXT", "TXT" };
	for (auto formatname : formats)
		catalogs_set.insert(OPT_GET("Subtitle Format/" + std::string(formatname) + "/Default Style Catalog")->GetString());
	// Sorted version
	wxArrayString catalogs;
	for (auto const& cn : catalogs_set)
		catalogs.Add(to_wx(cn));
	catalogs.Sort();

	PageSection section = {general, staticbox->GetStaticBox()};

	p->OptionChoice(section, _("New files"), catalogs, "Subtitle Format/ASS/Default Style Catalog");
	p->OptionChoice(section, _("MicroDVD import"), catalogs, "Subtitle Format/MicroDVD/Default Style Catalog");
	p->OptionChoice(section, _("SRT import"), catalogs, "Subtitle Format/SRT/Default Style Catalog");
	p->OptionChoice(section, _("TTXT import"), catalogs, "Subtitle Format/TTXT/Default Style Catalog");
	p->OptionChoice(section, _("Plain text import"), catalogs, "Subtitle Format/TXT/Default Style Catalog");

	p->SetSizerAndFit(p->sizer);
}

/// Audio preferences page
void Audio(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Audio"));

	auto general = p->PageSizer(_("Options"));
	p->OptionAdd(general, _("Default mouse wheel to zoom"), "Audio/Wheel Default to Zoom");
	p->OptionAdd(general, _("Lock scroll on cursor"), "Audio/Lock Scroll on Cursor");
	p->OptionAdd(general, _("Snap markers by default"), "Audio/Snap/Enable");
	p->OptionAdd(general, _("Auto-focus on mouse over"), "Audio/Auto/Focus");
	p->OptionAdd(general, _("Play audio when stepping in video"), "Audio/Plays When Stepping Video");
	p->OptionAdd(general, _("Left-click-drag moves end marker"), "Audio/Drag Timing");
	p->OptionAdd(general, _("Default timing length (ms)"), "Timing/Default Duration", {.min = 0, .max = 36000});
	p->OptionAdd(general, _("Default lead-in length (ms)"), "Audio/Lead/IN", {.min = 0, .max = 36000});
	p->OptionAdd(general, _("Default lead-out length (ms)"), "Audio/Lead/OUT", {.min = 0, .max = 36000});

	p->OptionAdd(general, _("Marker drag-start sensitivity (px)"), "Audio/Start Drag Sensitivity", {.min = 1, .max = 15});
	p->OptionAdd(general, _("Line boundary thickness (px)"), "Audio/Line Boundaries Thickness", {.min = 1, .max = 5});
	p->OptionAdd(general, _("Maximum snap distance (px)"), "Audio/Snap/Distance", {.min = 0, .max = 25});

	const wxString dtl_arr[] = { _("Don't show"), _("Show previous"), _("Show previous and next"), _("Show all") };
	wxArrayString choice_dtl(4, dtl_arr);
	p->OptionChoice(general, _("Show inactive lines"), choice_dtl, "Audio/Inactive Lines Display Mode");
	p->CellSkip(general);
	p->OptionAdd(general, _("Include commented inactive lines"), "Audio/Display/Draw/Inactive Comments");

	auto display = p->PageSizer(_("Display Visual Options"));
	p->OptionAdd(display, _("Keyframes in dialogue mode"), "Audio/Display/Draw/Keyframes in Dialogue Mode");
	p->OptionAdd(display, _("Keyframes in karaoke mode"), "Audio/Display/Draw/Keyframes in Karaoke Mode");
	p->OptionAdd(display, _("Cursor time"), "Audio/Display/Draw/Cursor Time");
	p->OptionAdd(display, _("Video position"), "Audio/Display/Draw/Video Position");
	p->OptionAdd(display, _("Seconds boundaries"), "Audio/Display/Draw/Seconds");
	p->CellSkip(display);
	p->OptionChoice(display, _("Waveform Style"), AudioWaveformRenderer::GetWaveformStyles(), "Audio/Display/Waveform Style");

	auto label = p->PageSizer(_("Audio labels"));
	p->OptionFont(label, "Audio/Karaoke/");

	p->SetSizerAndFit(p->sizer);
}

/// Video preferences page
void Video(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Video"));

	auto general = p->PageSizer(_("Options"));
	p->OptionAdd(general, _("Show keyframes in slider"), "Video/Slider/Show Keyframes");
	p->CellSkip(general);
	p->OptionAdd(general, _("Only show visual tools when mouse is over video"), "Tool/Visual/Autohide");
	p->CellSkip(general);
	p->OptionAdd(general, _("Seek video to line start on selection change"), "Video/Subtitle Sync");
	p->CellSkip(general);
	p->OptionAdd(general, _("Follow subtitle line when video position changes"), "Video/Seek Follow Line");
	p->CellSkip(general);
	p->OptionAdd(general, _("Automatically open audio when opening video"), "Video/Open Audio");
	p->OptionAdd(general, _("Default to Video Zoom"), "Video/Default to Video Zoom")
		->SetToolTip(_("Reverses the behavior of Ctrl while scrolling the video display. If not set, scrolling will default to UI zoom and Ctrl+scrolling will zoom the video. If set, this will be reversed."));
	p->OptionAdd(general, _("Disable zooming with scroll bar"), "Video/Disable Scroll Zoom")
		->SetToolTip(_("Makes the scroll bar not zoom the video. Useful when using a track pad that often scrolls accidentally."));
	p->OptionAdd(general, _("Reverse zoom direction"), "Video/Reverse Zoom");
	p->OptionAdd(general, _("Prefetch subtitles after first frame render"), "Video/Prefetch")
		->SetToolTip(_("When enabled, preloads the full subtitle file into the renderer after the first frame is displayed. This can improve seek performance for subtitle-heavy projects, but may increase memory usage."));
	p->CellSkip(general);

	const wxString cscroll_arr[] = {_("Resizes the video box"), _("Resizes the video box (reversed)"), _("Zooms the video"), _("Zooms the video (reversed)"), _("Pans the video"), _("Pans the video (X/Y swapped)"), _("Does nothing")};
	wxArrayString choice_scroll(7, cscroll_arr);
	p->OptionChoice(general, _("Scrolling on the video display"), choice_scroll, "Video/Scroll Action");
	p->OptionChoice(general, _("Ctrl+Scrolling on the video display"), choice_scroll, "Video/Ctrl Scroll Action");
	p->OptionChoice(general, _("Shift+Scrolling on the video display"), choice_scroll, "Video/Shift Scroll Action");

	const wxString czoom_arr[24] = { "12.5%", "25%", "37.5%", "50%", "62.5%", "75%", "87.5%", "100%", "112.5%", "125%", "137.5%", "150%", "162.5%", "175%", "187.5%", "200%", "212.5%", "225%", "237.5%", "250%", "262.5%", "275%", "287.5%", "300%" };
	wxArrayString choice_zoom(24, czoom_arr);
	p->OptionChoice(general, _("Default Zoom"), choice_zoom, "Video/Default Zoom");

	p->OptionAdd(general, _("Fast jump step in frames"), "Video/Slider/Fast Jump Step");

	const wxString cscr_arr[3] = { "?video", "?script", "." };
	wxArrayString scr_res(3, cscr_arr);
	p->OptionChoice(general, _("Screenshot save path"), scr_res, "Path/Screenshot");
	const wxString image_suffix[2] = { "png", "jpg" };
	const wxArrayString image_suffix_res(2, image_suffix);
	p->OptionChoice(general, _("Image Suffix"), image_suffix_res, "Path/ImageSuffix");
	p->OptionBrowse(general, _("GIF export path"), "Path/GifExport");
	p->OptionBrowse(general, _("Clip export path"), "Path/ClipExport");

	auto resolution = p->PageSizer(_("Script Resolution"));
	wxControl *autocb = p->OptionAdd(resolution, _("Use resolution of first video opened"), "Subtitle/Default Resolution/Auto");
	p->CellSkip(resolution);
	p->DisableIfChecked(autocb,
		p->OptionAdd(resolution, _("Default width"), "Subtitle/Default Resolution/Width"));
	p->DisableIfChecked(autocb,
		p->OptionAdd(resolution, _("Default height"), "Subtitle/Default Resolution/Height"));

	const wxString cres_arr[] = {_("Never"), _("Ask"), _("Always set"), _("Always resample")};
	wxArrayString choice_res(4, cres_arr);
	p->OptionChoice(resolution, _("Match video resolution on open"), choice_res, "Video/Script Resolution Mismatch");

	p->SetSizerAndFit(p->sizer);
}

/// Interface preferences page
void Interface(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Interface"));

	auto grid = p->PageSizer(_("Grid"));
	p->OptionAdd(grid, _("Focus grid on click"), "Subtitle/Grid/Focus Allow");
	p->OptionAdd(grid, _("Highlight visible subtitles"), "Subtitle/Grid/Highlight Subtitles in Frame");
	p->OptionAdd(grid, _("Hide overrides symbol"), "Subtitle/Grid/Hide Overrides Char");
	p->OptionFont(grid, "Subtitle/Grid/");

	auto tl_assistant = p->PageSizer(_("Translation Assistant"));
	p->OptionAdd(tl_assistant, _("Skip over whitespace"), "Tool/Translation Assistant/Skip Whitespace");

	auto visual_tools = p->PageSizer(_("Visual Tools"));
	p->OptionAdd(visual_tools, _("Shape handle size"), "Tool/Visual/Shape Handle Size");
	p->OptionFont(visual_tools, "Tool/Visual/");

	auto color_picker = p->PageSizer(_("Colour Picker"));
	p->OptionAdd(color_picker, _("Restrict Screen Picker to Window"), "Tool/Colour Picker/Restrict to Window");

#if defined(__WXMSW__) && wxVERSION_NUMBER >= 3300
	auto dark_mode = p->PageSizer(_("Dark Mode"));
	wxString dark_modes[] = { _("Follow System"), _("Light"), _("Dark") };
	wxArrayString dark_modes_arr(3, dark_modes);
	p->OptionChoice(dark_mode, _("Theme mode (restart required)"), dark_modes_arr, "App/Dark Mode");
#endif

	p->SetSizerAndFit(p->sizer);
}

/// Interface Edit Box preferences subpage
void Interface_EditBox(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Edit Box"), OptionPage::PAGE_SUB);

	auto edit_box = p->PageSizer(_("Edit Box"));
	p->OptionAdd(edit_box, _("Enable call tips"), "App/Call Tips");
	p->OptionAdd(edit_box, _("Overwrite in time boxes"), "Subtitle/Time Edit/Insert Mode");
	p->OptionAdd(edit_box, _("Shift+Enter adds \\n"), "Subtitle/Edit Box/Soft Line Break")->SetToolTip(_("When enabled, Shift+Enter add \\n, when disabled, add \\N"));
	p->OptionAdd(edit_box, _("Enable syntax highlighting"), "Subtitle/Highlight/Syntax");
	p->OptionBrowse(edit_box, _("Dictionaries path"), "Path/Dictionary");
	p->OptionFont(edit_box, "Subtitle/Edit Box/");

	p->SetSizerAndFit(p->sizer);
}

/// Interface Character Counter preferences subpage
void Interface_CharCounter(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Character Counter"), OptionPage::PAGE_SUB);

	auto character_count = p->PageSizer(_("Character Counter"));
	p->OptionAdd(character_count, _("Maximum characters per line"), "Subtitle/Character Limit", {.min = 0, .max = 1000});
	p->OptionAdd(character_count, _("Characters Per Second Warning Threshold"), "Subtitle/Character Counter/CPS Warning Threshold", {.min = 0.1, .max = 1000., .inc = 0.1});
	p->OptionAdd(character_count, _("Characters Per Second Error Threshold"), "Subtitle/Character Counter/CPS Error Threshold", {.min = 0.1, .max = 1000., .inc = 0.1});
	p->OptionAdd(character_count, _("Ignore whitespace"), "Subtitle/Character Counter/Ignore Whitespace");
	p->OptionAdd(character_count, _("Ignore punctuation"), "Subtitle/Character Counter/Ignore Punctuation");

	const wxString ccpsf_arr[3] = {_("Nearest integer"), _("Nearest 0.1"), _("2 sig figs")};
	wxArrayString cpsf_res(3, ccpsf_arr);
	p->OptionChoice(character_count, _("CPS display format"), cpsf_res, "Subtitle/Character Counter/Display Format");

	const wxString calign_arr[2] = {_("Center"), _("Center with virtual 0")};
	wxArrayString calign_res(2, calign_arr);
	p->OptionChoice(character_count, _("CPS column alignment"), calign_res, "Subtitle/Character Counter/Column Alignment");

	p->SetSizerAndFit(p->sizer);
}

/// Interface Colours preferences subpage
void Interface_Colours(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Colors"), OptionPage::PAGE_SCROLL|OptionPage::PAGE_SUB);

	delete p->sizer;
	wxSizer *main_sizer = new wxBoxSizer(wxHORIZONTAL);

	p->sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(p->sizer, 1);

	auto audio = p->PageSizer(_("Audio Display"));
	p->OptionAdd(audio, _("Current frame range"), "Colour/Audio Display/Current Frame Range", {.alpha = true});
	p->OptionAdd(audio, _("Previous frame range"), "Colour/Audio Display/Previous Frame Range", {.alpha = true});
	p->OptionAdd(audio, _("Play cursor"), "Colour/Audio Display/Play Cursor");
	p->OptionAdd(audio, _("Line boundary start"), "Colour/Audio Display/Line boundary Start");
	p->OptionAdd(audio, _("Line boundary end"), "Colour/Audio Display/Line boundary End");
	p->OptionAdd(audio, _("Line boundary inactive line"), "Colour/Audio Display/Line Boundary Inactive Line");
	p->OptionAdd(audio, _("Syllable boundaries"), "Colour/Audio Display/Syllable Boundaries");
	p->OptionAdd(audio, _("Seconds boundaries"), "Colour/Audio Display/Seconds Line");

	auto syntax = p->PageSizer(_("Syntax Highlighting"));
	p->OptionAdd(syntax, _("Background"), "Colour/Subtitle/Background");
	p->OptionAdd(syntax, _("Normal"), "Colour/Subtitle/Syntax/Normal");
	p->OptionAdd(syntax, _("Comments"), "Colour/Subtitle/Syntax/Comment");
	p->OptionAdd(syntax, _("Drawing Commands"), "Colour/Subtitle/Syntax/Drawing Command");
	p->OptionAdd(syntax, _("Drawing X Coords"), "Colour/Subtitle/Syntax/Drawing X");
	p->OptionAdd(syntax, _("Drawing Y Coords"), "Colour/Subtitle/Syntax/Drawing Y");
	p->OptionAdd(syntax, _("Underline Spline Endpoints"), "Colour/Subtitle/Syntax/Underline/Drawing Endpoint");
	p->CellSkip(syntax);
	p->OptionAdd(syntax, _("Brackets"), "Colour/Subtitle/Syntax/Brackets");
	p->OptionAdd(syntax, _("Slashes and Parentheses"), "Colour/Subtitle/Syntax/Slashes");
	p->OptionAdd(syntax, _("Tags"), "Colour/Subtitle/Syntax/Tags");
	p->OptionAdd(syntax, _("Parameters"), "Colour/Subtitle/Syntax/Parameters");
	p->OptionAdd(syntax, _("Error"), "Colour/Subtitle/Syntax/Error");
	p->OptionAdd(syntax, _("Error Background"), "Colour/Subtitle/Syntax/Background/Error");
	p->OptionAdd(syntax, _("Line Break"), "Colour/Subtitle/Syntax/Line Break");
	p->OptionAdd(syntax, _("Karaoke templates"), "Colour/Subtitle/Syntax/Karaoke Template");
	p->OptionAdd(syntax, _("Karaoke variables"), "Colour/Subtitle/Syntax/Karaoke Variable");

	p->sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->AddSpacer(5);
	main_sizer->Add(p->sizer, 1);

	auto color_schemes = p->PageSizer(_("Audio Color Schemes"));
	wxArrayString schemes = to_wx(OPT_GET("Audio/Colour Schemes")->GetListString());
	p->OptionChoice(color_schemes, _("Spectrum"), schemes, "Colour/Audio Display/Spectrum", true);
	p->OptionChoice(color_schemes, _("Waveform"), schemes, "Colour/Audio Display/Waveform", true);

	auto grid = p->PageSizer(_("Subtitle Grid"));
	p->OptionAdd(grid, _("Standard foreground"), "Colour/Subtitle Grid/Standard");
	p->OptionAdd(grid, _("Standard background"), "Colour/Subtitle Grid/Background/Background");
	p->OptionAdd(grid, _("Selection foreground"), "Colour/Subtitle Grid/Selection");
	p->OptionAdd(grid, _("Selection background"), "Colour/Subtitle Grid/Background/Selection");
	p->OptionAdd(grid, _("Collision foreground"), "Colour/Subtitle Grid/Collision");
	p->OptionAdd(grid, _("In frame background"), "Colour/Subtitle Grid/Background/Inframe");
	p->OptionAdd(grid, _("Comment background"), "Colour/Subtitle Grid/Background/Comment");
	p->OptionAdd(grid, _("Selected comment background"), "Colour/Subtitle Grid/Background/Selected Comment");
	p->OptionAdd(grid, _("Open fold background"), "Colour/Subtitle Grid/Background/Open Fold");
	p->OptionAdd(grid, _("Closed fold background"), "Colour/Subtitle Grid/Background/Closed Fold");
	p->OptionAdd(grid, _("Header background"), "Colour/Subtitle Grid/Header");
	p->OptionAdd(grid, _("Left Column"), "Colour/Subtitle Grid/Left Column");
	p->OptionAdd(grid, _("Active Line Border"), "Colour/Subtitle Grid/Active Border");
	p->OptionAdd(grid, _("Lines"), "Colour/Subtitle Grid/Lines");
	p->OptionAdd(grid, _("CPS Error"), "Colour/Subtitle Grid/CPS Error");

	auto visual_tools = p->PageSizer(_("Visual Typesetting Tools"));
	p->OptionAdd(visual_tools, _("Primary Lines"), "Colour/Visual Tools/Lines Primary");
	p->OptionAdd(visual_tools, _("Secondary Lines"), "Colour/Visual Tools/Lines Secondary");
	p->OptionAdd(visual_tools, _("Primary Highlight"), "Colour/Visual Tools/Highlight Primary");
	p->OptionAdd(visual_tools, _("Secondary Highlight"), "Colour/Visual Tools/Highlight Secondary");

	// Separate sizer to prevent the colors in the visual tools section from getting resized
	auto visual_tools_alpha = p->PageSizer(_("Visual Typesetting Tools Alpha"));
	p->OptionAdd(visual_tools_alpha, _("Shaded Area"), "Colour/Visual Tools/Shaded Area Alpha", {.min = 0, .max = 1, .inc = 0.1});

	p->sizer = main_sizer;

	p->SetSizerAndFit(p->sizer);
}

/// Backup preferences page
void Backup(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Backup"));

	auto save = p->PageSizer(_("Automatic Save"));
	wxControl *cb = p->OptionAdd(save, _("Enable"), "App/Auto/Save");
	p->CellSkip(save);
	p->EnableIfChecked(cb,
		p->OptionAdd(save, _("Interval in seconds"), "App/Auto/Save Every Seconds", {.min = 1}));
	p->OptionBrowse(save, _("Path"), "Path/Auto/Save", cb, true);
	p->OptionAdd(save, _("Autosave after every change"), "App/Auto/Save on Every Change");

	auto backup = p->PageSizer(_("Automatic Backup"));
	cb = p->OptionAdd(backup, _("Enable"), "App/Auto/Backup");
	p->CellSkip(backup);
	p->OptionBrowse(backup, _("Path"), "Path/Auto/Backup", cb, true);

	p->SetSizerAndFit(p->sizer);
}

/// Automation preferences page
void Automation(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Automation"));

	auto general = p->PageSizer(_("General"));

	p->OptionAdd(general, _("Base path"), "Path/Automation/Base");
	p->OptionAdd(general, _("Include path"), "Path/Automation/Include");
	p->OptionAdd(general, _("Auto-load path"), "Path/Automation/Autoload");

	const wxString tl_arr[6] = { _("0: Fatal"), _("1: Error"), _("2: Warning"), _("3: Hint"), _("4: Debug"), _("5: Trace") };
	wxArrayString tl_choice(6, tl_arr);
	p->OptionChoice(general, _("Trace level"), tl_choice, "Automation/Trace Level");

	const wxString ar_arr[4] = { _("No scripts"), _("Subtitle-local scripts"), _("Global autoload scripts"), _("All scripts") };
	wxArrayString ar_choice(4, ar_arr);
	p->OptionChoice(general, _("Autoreload on Export"), ar_choice, "Automation/Autoreload Mode");

	p->SetSizerAndFit(p->sizer);
}

/// Advanced preferences page
void Advanced(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Advanced"));

	auto general = p->PageSizer(_("General"));

	auto warning = new wxStaticText(general.box, wxID_ANY ,_("Changing these settings might result in bugs and/or crashes.  Do not touch these unless you know what you're doing."));
	auto font = parent->GetFont().MakeBold();
	font.SetPointSize(12);
	warning->SetFont(font);
	p->sizer->Fit(p);
	warning->Wrap(book->FromDIP(400));
	general.sizer->Add(warning, 0, wxALL, 5);

	p->SetSizerAndFit(p->sizer);
}

/// MCP Server 偏好设置页面
	void MCP(wxTreebook *book, Preferences *parent) {
		auto p = new OptionPage(book, parent, _("MCP Server"));

		// 功能说明置于分组框之上，跨整行宽度
		std::string host = OPT_GET("App/MCP/Host")->GetString();
		int port = OPT_GET("App/MCP/Port")->GetInt();
		auto hint = new wxStaticText(
			p, wxID_ANY, wxString::Format(
				_(
					"Enable MCP to allow AI clients (Claude Desktop, Cursor, etc.) to connect via HTTP.\n"
					"AI and user operate the same Aegisub instance, changes visible in GUI in real-time.\n\n"
					"Client config: AI side fill in http://%s:%d/mcp"
				),
				host.c_str(), port
			)
		);
		p->sizer->Add(hint, 0, wxALL | wxEXPAND, 5);

		// 实时刷新提示文本
		auto update_hint = [hint, book]() {
			std::string h = OPT_GET("App/MCP/Host")->GetString();
			int pt = OPT_GET("App/MCP/Port")->GetInt();
			hint->SetLabel(
				wxString::Format(
					_(
						"Enable MCP to allow AI clients (Claude Desktop, Cursor, etc.) to connect via HTTP.\n"
						"AI and user operate the same Aegisub instance, changes visible in GUI in real-time.\n\n"
						"Client config: AI side fill in http://%s:%d/mcp"
					),
					h.c_str(), pt
				)
			);
			hint->Wrap(book->FromDIP(480));
		};
		update_hint();

		auto options = p->PageSizer(_("Options"));

		// 启用复选框，跨整行显示
		auto cb = new wxCheckBox(options.box, -1, _("Enable MCP server (requires restart)"));
		cb->SetValue(OPT_GET("App/MCP/Enabled")->GetBool());
		cb->SetToolTip(_("Enable MCP server, requires restart to take effect"));
		options.sizer->Add(cb, 1, wxEXPAND, 5);
		p->CellSkip(options);
		parent->AddChangeableOption("App/MCP/Enabled");
		cb->Bind(
			wxEVT_CHECKBOX, [parent](wxCommandEvent &evt) {
				parent->SetOption(std::make_unique<agi::OptionValueBool>("App/MCP/Enabled", !!evt.GetInt()));
			}
		);

		// 监听地址
		auto host_ctrl = p->OptionAdd(options, _("MCP listen address"), "App/MCP/Host");
		host_ctrl->SetToolTip(_("MCP server listen address, default 127.0.0.1 allows local access only"));
		host_ctrl->Bind(wxEVT_TEXT, [update_hint](wxCommandEvent &) { update_hint(); });

		// 监听端口
		parent->AddChangeableOption("App/MCP/Port");
		auto port_txt = new wxTextCtrl(options.box, -1, std::to_wstring(OPT_GET("App/MCP/Port")->GetInt()));
		port_txt->SetToolTip(_("MCP server listen port, default 7878"));
		port_txt->Bind(
			wxEVT_TEXT, [parent, port_txt, update_hint](wxCommandEvent &) {
				try {
					int val = std::stoi(port_txt->GetValue().ToStdString());
					if (val < 1 || val > 65535) {
						wxBell();
						return;
					}
					parent->SetOption(std::make_unique<agi::OptionValueInt>("App/MCP/Port", val));
					update_hint();
				} catch (...) {
					wxBell();
				}
			}
		);
		options.sizer->Add(new wxStaticText(options.box, -1, _("MCP listen port")), 1, wxALIGN_CENTRE_VERTICAL);
		options.sizer->Add(port_txt, 1, wxEXPAND);

		p->SetSizerAndFit(p->sizer);
	}

/// Advanced Audio preferences subpage
void Advanced_Audio(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Audio"), OptionPage::PAGE_SUB);

	auto expert = p->PageSizer(_("Expert"));

	wxArrayString ap_choice = to_wx(GetAudioProviderNames());
	p->OptionChoice(expert, _("Audio provider"), ap_choice, "Audio/Provider");

	wxArrayString apl_choice = to_wx(AudioPlayerFactory::GetClasses());
	p->OptionChoice(expert, _("Audio player"), apl_choice, "Audio/Player");

	auto cache = p->PageSizer(_("Cache"));
	const wxString ct_arr[3] = { _("None (NOT RECOMMENDED)"), _("RAM"), _("Hard Disk") };
	wxArrayString ct_choice(3, ct_arr);
	p->OptionChoice(cache, _("Cache type"), ct_choice, "Audio/Cache/Type");
	p->OptionBrowse(cache, _("Path"), "Audio/Cache/HD/Location");

	auto spectrum = p->PageSizer(_("Spectrum"));

	const wxString sq_arr[4] = { _("Regular quality"), _("Better quality"), _("High quality"), _("Insane quality") };
	wxArrayString sq_choice(4, sq_arr);
	p->OptionChoice(spectrum, _("Quality"), sq_choice, "Audio/Renderer/Spectrum/Quality");

	const wxString sc_arr[5] = { _("Linear"), _("Extended"), _("Medium"), _("Compressed"), _("Logarithmic") };
	wxArrayString sc_choice(5, sc_arr);
	p->OptionChoice(spectrum, _("Frequency mapping"), sc_choice, "Audio/Renderer/Spectrum/FreqCurve");

	p->OptionAdd(spectrum, _("Cache memory max (MB)"), "Audio/Renderer/Spectrum/Memory Max", {.min = 2, .max = 1024});

#ifdef WITH_AVISYNTH
	auto avisynth = p->PageSizer("Avisynth");
	const wxString adm_arr[4] = { _("None"), _("ConvertToMono"), _("GetLeftChannel"), _("GetRightChannel") };
	wxArrayString adm_choice(4, adm_arr);
	p->OptionChoice(avisynth, _("Avisynth down-mixer"), adm_choice, "Audio/Downmixer");
	p->OptionAdd(avisynth, _("Force sample rate"), "Provider/Audio/AVS/Sample Rate");
#endif

#ifdef WITH_FFMS2
	auto ffms = p->PageSizer("FFmpegSource");

	const wxString error_modes[] = { _("Ignore"), _("Clear"), _("Stop"), _("Abort") };
	wxArrayString error_modes_choice(4, error_modes);
	p->OptionChoice(ffms, _("Audio indexing error handling mode"), error_modes_choice, "Provider/Audio/FFmpegSource/Decode Error Handling");

	p->OptionAdd(ffms, _("Always index all audio tracks"), "Provider/FFmpegSource/Index All Tracks");
	wxControl* stereo = p->OptionAdd(ffms, _("Downmix to stereo"), "Provider/Audio/FFmpegSource/Downmix");
	stereo->SetToolTip(_("Reduces memory usage on surround audio, but may cause audio tracks to sound blank in specific circumstances. This will not affect audio with two channels or less."));
#endif

#ifdef WITH_BESTSOURCE
	auto bs = p->PageSizer("BestSource");
	p->OptionAdd(bs, _("Max BS cache size (MB)"), "Provider/Audio/BestSource/Max Cache Size");
	p->OptionAdd(bs, _("Use Aegisub's Cache"), "Provider/Audio/BestSource/Aegisub Cache");
#endif


#ifdef WITH_PORTAUDIO
	auto portaudio = p->PageSizer("Portaudio");
	p->OptionChoice(portaudio, _("Portaudio device"), PortAudioPlayer::GetOutputDevices(), "Player/Audio/PortAudio/Device Name");
#endif

#ifdef WITH_OSS
	auto oss = p->PageSizer("OSS");
	p->OptionBrowse(oss, _("OSS Device"), "Player/Audio/OSS/Device");
#endif

#ifdef WITH_DIRECTSOUND
	auto dsound = p->PageSizer("DirectSound");
	p->OptionAdd(dsound, _("Buffer latency"), "Player/Audio/DirectSound/Buffer Latency", {.min = 1, .max = 1000});
	p->OptionAdd(dsound, _("Buffer length"), "Player/Audio/DirectSound/Buffer Length", {.min = 1, .max = 100});
#endif

	p->SetSizerAndFit(p->sizer);
}

/// Advanced Video preferences subpage
void Advanced_Video(wxTreebook *book, Preferences *parent) {
	auto p = new OptionPage(book, parent, _("Video"), OptionPage::PAGE_SUB);

	auto expert = p->PageSizer(_("Expert"));

	wxArrayString vp_choice = to_wx(VideoProviderFactory::GetClasses());
	p->OptionChoice(expert, _("Video provider"), vp_choice, "Video/Provider");

	wxArrayString sp_choice = to_wx(SubtitlesProviderFactory::GetClasses());
	p->OptionChoice(expert, _("Subtitles provider"), sp_choice, "Subtitle/Provider");

	constexpr char smart_abb_opt[] = "Provider/Video/Smart ABB";
	parent->AddChangeableOption(smart_abb_opt);
	auto *smart_abb = new wxCheckBox(expert.box, -1, _("Smart add black borders"));
	smart_abb->SetValue(OPT_GET(smart_abb_opt)->GetBool());
	smart_abb->SetToolTip(_("Automatically add top/bottom black borders to match a standard resolution for FFmpegSource, BestSource, and VapourSynth. When enabled, the manual black border values below are ignored. Changes persist in preferences and reload the current video."));

	auto *smart_abb_status = new wxStaticText(expert.box, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
	smart_abb_status->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
	smart_abb_status->SetMinSize(expert.box->FromDIP(wxSize(280, -1)));

	auto *smart_abb_row = new wxBoxSizer(wxHORIZONTAL);
	smart_abb_row->Add(smart_abb, 0, wxALIGN_CENTER_VERTICAL);
	smart_abb_row->AddSpacer(expert.box->FromDIP(8));
	smart_abb_row->Add(smart_abb_status, 1, wxALIGN_CENTER_VERTICAL);
	expert.sizer->Add(smart_abb_row, 1, wxEXPAND);
	expert.sizer->AddSpacer(0);

	auto update_smart_abb_status = [p, parent, smart_abb, smart_abb_status] {
		auto *context = parent->GetContext();
		auto *provider = context && context->project ? context->project->VideoProvider() : nullptr;
		const wxString status = BuildSmartAbbStatus(provider, smart_abb->GetValue());
		smart_abb_status->SetLabel(status);
		smart_abb_status->Show(!status.empty());
		if (auto *row = smart_abb_status->GetContainingSizer())
			row->Layout();
		p->Layout();
		p->FitInside();
	};

	smart_abb->Bind(wxEVT_CHECKBOX, [parent, smart_abb_opt, update_smart_abb_status](wxCommandEvent &evt) mutable {
		evt.Skip();
		parent->SetOption(std::make_unique<agi::OptionValueBool>(smart_abb_opt, !!evt.GetInt()));
		update_smart_abb_status();
	});
	update_smart_abb_status();

	if (auto *context = parent->GetContext(); context && context->project) {
		parent->AddSignalConnection(context->project->AddVideoProviderListener(
			[update_smart_abb_status](AsyncVideoProvider *) mutable { update_smart_abb_status(); }
		));
	}


#ifdef WITH_AVISYNTH
	auto avisynth = p->PageSizer("Avisynth");
	p->OptionAdd(avisynth, _("Avisynth memory limit"), "Provider/Avisynth/Memory Max");
#endif

#ifdef WITH_FFMS2
	auto ffms = p->PageSizer("FFmpegSource");

	const wxString log_levels[] = { wxTRANSLATE("Quiet"), wxTRANSLATE("Panic"), wxTRANSLATE("Fatal"), wxTRANSLATE("Error"), wxTRANSLATE("Warning"), wxTRANSLATE("Info"), wxTRANSLATE("Verbose"), wxTRANSLATE("Debug") };
	wxArrayString log_levels_choice(8, log_levels);
	p->OptionChoice(ffms, _("Debug log verbosity"), log_levels_choice, "Provider/FFmpegSource/Log Level", true);

	// 添加黑边
	auto *ffms_abb = p->OptionAdd(ffms, _("Add black borders"), "Provider/Video/FFmpegSource/ABB", {.min = 0});
	ffms_abb->SetToolTip(
			_("Expands logical video height with black borders for script alignment/resolution. Decoded frame data is unchanged.")
		);
	p->DisableIfChecked(smart_abb, ffms_abb);

	// 硬件加速选项
	const wxString h_wx_string[] = {"cuda", "d3d11va", "dxva2", "none"};
	const wxArrayString h_wx_string_choice(4, h_wx_string);
	p->OptionChoice(ffms, _("H/W acceleration"), h_wx_string_choice, "Provider/Video/FFmpegSource/HW hw_name");

	p->OptionAdd(ffms, _("Decoding threads"), "Provider/Video/FFmpegSource/Decoding Threads", {.min = -1});
	p->OptionAdd(ffms, _("Enable unsafe seeking"), "Provider/Video/FFmpegSource/Unsafe Seeking");
#endif

#ifdef WITH_BESTSOURCE
	auto bs = p->PageSizer("BestSource");
	auto *bs_abb = p->OptionAdd(bs, _("Add black borders"), "Provider/Video/BestSource/ABB", {.min = 0});
	bs_abb->SetToolTip(
			_("Expands logical video height with black borders for script alignment/resolution. Decoded frame data is unchanged.")
		);
	p->DisableIfChecked(smart_abb, bs_abb);
	p->OptionAdd(bs, _("Max cache size (MB)"), "Provider/Video/BestSource/Max Cache Size");
	p->OptionAdd(bs, _("Decoder Threads (0 to autodetect)"), "Provider/Video/BestSource/Threads");
	p->OptionAdd(bs, _("Seek preroll (Frames)"), "Provider/Video/BestSource/Seek Preroll");

	// 硬件加速选项（BestSource 不支持 CUDA，仅提供 DXVA 系列）
	const wxString bs_hw_choices[] = {"d3d11va", "dxva2", "none"};
	const wxArrayString bs_hw_choice_arr(3, bs_hw_choices);
	p->OptionChoice(bs, _("H/W acceleration"), bs_hw_choice_arr, "Provider/Video/BestSource/HW hw_name");

	// Bool选项放在最后，避免单cell导致后续控件错行
	p->OptionAdd(bs, _("Apply RFF"), "Provider/Video/BestSource/Apply RFF");
#endif

	#ifdef WITH_VAPOURSYNTH
	const auto vs = p->PageSizer("VapourSynth");
	auto *vs_abb = p->OptionAdd(vs, _("Add black borders"), "Provider/Video/VapourSynth/ABB", {.min = 0});
	p->DisableIfChecked(smart_abb, vs_abb);
	#endif

	p->SetSizerAndFit(p->sizer);
}

void VapourSynth(wxTreebook *book, Preferences *parent) {
#ifdef WITH_VAPOURSYNTH
	auto p = new OptionPage(book, parent, _("VapourSynth"), OptionPage::PAGE_SUB);
	auto general = p->PageSizer(_("General"));

	const wxString log_levels[] = { wxTRANSLATE("Quiet"), wxTRANSLATE("Fatal"), wxTRANSLATE("Critical"), wxTRANSLATE("Warning"), wxTRANSLATE("Information"), wxTRANSLATE("Debug") };
	wxArrayString log_levels_choice(6, log_levels);
	p->OptionChoice(general, _("Log level"), log_levels_choice, "Provider/Video/VapourSynth/Log Level", true);
	p->CellSkip(general);
	p->OptionAdd(general, _("Load user plugins"), "Provider/VapourSynth/Autoload User Plugins");

	auto video = p->PageSizer(_("Default Video Script"));

	auto make_default_button = [](std::string optname, wxTextCtrl *ctrl, wxWindow *parent_box) {
		auto showdefault = new wxButton(parent_box, -1, _("Set to Default"));
		showdefault->Bind(wxEVT_BUTTON, [ctrl, optname](auto e) {
			ctrl->SetValue(OPT_GET(optname)->GetDefaultString());
		});
		return showdefault;
	};

	auto vhint = new wxStaticText(video.box, wxID_ANY, _("This script will be executed to load video files that aren't\nVapourSynth scripts (i.e. end in .py or .vpy).\nThe filename variable stores the path to the file."));
	p->sizer->Fit(p);
	vhint->Wrap(book->FromDIP(400));
	video.sizer->Add(vhint, 0, wxALL, 5);
	p->CellSkip(video);

	auto vdef = p->OptionAddMultiline(video, "Provider/Video/VapourSynth/Default Script");
	p->CellSkip(video);

	video.sizer->Add(make_default_button("Provider/Video/VapourSynth/Default Script", vdef, video.box), wxSizerFlags().Right());

	auto audio = p->PageSizer(_("Default Audio Script"));
	auto ahint = new wxStaticText(audio.box, wxID_ANY, _("This script will be executed to load audio files that aren't\nVapourSynth scripts (i.e. end in .py or .vpy).\nThe filename variable stores the path to the file."));
	p->sizer->Fit(p);
	ahint->Wrap(book->FromDIP(400));
	audio.sizer->Add(ahint, 0, wxALL, 5);
	p->CellSkip(audio);

	auto adef = p->OptionAddMultiline(audio, "Provider/Audio/VapourSynth/Default Script");
	p->CellSkip(audio);

	audio.sizer->Add(make_default_button("Provider/Audio/VapourSynth/Default Script", adef, audio.box), wxSizerFlags().Right());

	p->SetSizerAndFit(p->sizer);
#endif
}

/// @brief 命令名称自动补全 + 图标渲染
class CommandRenderer final : public wxDataViewCustomRenderer {
	wxArrayString autocomplete;
	wxDataViewIconText value;
	static const int icon_width = 20;

	wxDataViewIconText MakeValue(wxString const& text) const {
		wxBitmap icon;
		try {
			icon = cmd::get(from_wx(text))->Icon(icon_width);
		}
		catch (agi::Exception const&) {
			// 无效命令名由描述列报告
		}
		return wxDataViewIconText(text, icon);
	}

public:
	CommandRenderer()
	: wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_EDITABLE)
	, autocomplete(get_registered_commands())
	{
	}

	wxWindow *CreateEditorCtrl(wxWindow *parent, wxRect label_rect, wxVariant const& value) override {
		wxString text;
		if (value.GetType() == "wxDataViewIconText") {
			wxDataViewIconText iconText;
			iconText << value;
			text = iconText.GetText();
		}
		else {
			text = value.GetString();
		}

		// adjust the label rect to take the width of the icon into account
		label_rect.x += icon_width;
		label_rect.width -= icon_width;

		wxTextCtrl* ctrl = new wxTextCtrl(parent, -1, text, label_rect.GetPosition(), label_rect.GetSize(), wxTE_PROCESS_ENTER);
		ctrl->SetInsertionPointEnd();
		ctrl->SelectAll();
		ctrl->AutoComplete(autocomplete);
		return ctrl;
	}

	bool SetValue(wxVariant const& var) override {
		if (var.GetType() == "wxDataViewIconText") {
			value << var;
			return true;
		}
		if (var.GetType() == "string") {
			value = MakeValue(var.GetString());
			return true;
		}
		return false;
	}

	bool Render(wxRect rect, wxDC *dc, int state) override {
		wxIcon const& icon = value.GetIcon();
		if (icon.IsOk())
			dc->DrawIcon(icon, rect.x, rect.y + (rect.height - icon.GetHeight()) / 2);

		RenderText(value.GetText(), icon_width, rect, dc, state);

		return true;
	}

	wxSize GetSize() const override {
		if (!value.GetText().empty()) {
			wxSize size = GetTextExtent(value.GetText());
			size.x += GetView()->FromDIP(icon_width);
			return size;
		}
		return wxSize(80,20);
	}

	bool GetValueFromEditorCtrl(wxWindow* editor, wxVariant &var) override {
		wxTextCtrl *text = static_cast<wxTextCtrl*>(editor);
		var = text->GetValue();
		return true;
	}

	bool GetValue(wxVariant &) const override { return false; }
	bool HasEditorCtrl() const override { return true; }
};

class HotkeyRenderer final : public wxDataViewCustomRenderer {
	wxString value;
	wxTextCtrl *ctrl = nullptr;

public:
	HotkeyRenderer()
	: wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_EDITABLE)
	{ }

	wxWindow *CreateEditorCtrl(wxWindow *parent, wxRect label_rect, wxVariant const& var) override {
		ctrl = new wxTextCtrl(parent, -1, var.GetString(), label_rect.GetPosition(), label_rect.GetSize(), wxTE_PROCESS_ENTER);
		ctrl->SetInsertionPointEnd();
		ctrl->SelectAll();
		ctrl->Bind(wxEVT_CHAR_HOOK, &HotkeyRenderer::OnKeyDown, this);
		return ctrl;
	}

	/// @brief 热键编辑器的按键处理
	///
	/// 捕获按键组合并显示对应字符串，同时正确处理确认/取消/修饰键
	void OnKeyDown(wxKeyEvent &evt) {
		int key = evt.GetKeyCode();
		int mod = evt.GetModifiers();

		// 忽略独立修饰键，等待用户输入完整组合键
		if (key == WXK_SHIFT || key == WXK_ALT || key == WXK_CONTROL || key == WXK_RAW_CONTROL) {
			return;
		}

		// 无修饰键的Enter确认编辑，Escape取消编辑，交由框架处理
		if (mod == wxMOD_NONE && (key == WXK_RETURN || key == WXK_ESCAPE)) {
			evt.Skip();
			return;
		}

		ctrl->ChangeValue(to_wx(hotkey::keypress_to_str(key, mod)));
	}

	bool SetValue(wxVariant const& var) override {
		value = var.GetString();
		return true;
	}

	bool Render(wxRect rect, wxDC *dc, int state) override {
		RenderText(value, 0, rect, dc, state);
		return true;
	}

	bool GetValueFromEditorCtrl(wxWindow*, wxVariant &var) override {
		var = ctrl->GetValue();
		return true;
	}

	bool GetValue(wxVariant &) const override { return false; }
	wxSize GetSize() const override { return !value ? wxSize(80, 20) : GetTextExtent(value); }
	bool HasEditorCtrl() const override { return true; }
};

class DescriptionRenderer final : public wxDataViewCustomRenderer {
	wxString value;

public:
	DescriptionRenderer()
	: wxDataViewCustomRenderer("string", wxDATAVIEW_CELL_INERT)
	{
	}

	bool SetValue(wxVariant const& var) override {
		value = var.GetString();
		return true;
	}

	bool Render(wxRect rect, wxDC *dc, int state) override {
		if (value.empty())
			return true;

		const int line_height = GetTextExtent("Mg").y + 2;
		size_t start = 0;
		int y = rect.y;
		while (true) {
			size_t end = value.find('\n', start);
			wxString line = end == wxString::npos ? value.Mid(start) : value.Mid(start, end - start);
			if (line.empty())
				line = " ";

			wxRect line_rect(rect.x, y, rect.width, line_height);
			RenderText(line, 0, line_rect, dc, state);
			y += line_height;

			if (end == wxString::npos || y >= rect.GetBottom())
				break;
			start = end + 1;
			if (start > value.length())
				break;
		}

		return true;
	}

	wxSize GetSize() const override {
		if (value.empty())
			return wxSize(80, 20);

		const int line_height = GetTextExtent("Mg").y + 2;
		int max_width = 0;
		int lines = 0;
		size_t start = 0;
		while (true) {
			size_t end = value.find('\n', start);
			wxString line = end == wxString::npos ? value.Mid(start) : value.Mid(start, end - start);
			if (line.empty())
				line = " ";

			wxSize line_size = GetTextExtent(line);
			if (line_size.x > max_width)
				max_width = line_size.x;
			++lines;

			if (end == wxString::npos)
				break;
			start = end + 1;
			if (start > value.length())
				break;
		}

		return wxSize(max_width, lines * line_height);
	}

	bool GetValue(wxVariant &) const override { return false; }
};

static void edit_item(wxDataViewCtrl *dvc, wxDataViewItem item) {
	dvc->EditItem(item, dvc->GetColumn(0));
}

/// @brief 用户字体路径列表的数据模型（支持搜索过滤、可编辑路径）
class UserFontPathModel final : public wxDataViewModel {
	/// 全量路径（原始顺序）
	std::vector<std::string> paths;
	/// 过滤后对应的全量索引（0 表示根节点，行索引从 1 开始编码，避免与根冲突）
	std::vector<size_t> filtered;
	wxString filter;

	/// 由行号得到对应的 DataViewItem（行号从 0 开始）
	static wxDataViewItem ItemForRow(size_t row) {
		return wxDataViewItem(reinterpret_cast<void *>(row + 1));
	}

public:
	/// 由 DataViewItem 得到行号（-1 表示无效）
	static int RowForItem(wxDataViewItem item) {
		if (!item.IsOk()) return -1;
		return static_cast<int>(reinterpret_cast<uintptr_t>(item.GetID()) - 1);
	}

	/// 由过滤后行号映射回全量索引（-1 表示无效），供外部读取
	int GetRealIndex(int row) const {
		return static_cast<int>(RealIndex(row));
	}

	void SetPaths(std::vector<std::string> const& value) {
		paths.clear();
		std::unordered_set<std::string> seen;
		for (auto const& p : value) {
			if (p.empty())
				continue;
			std::string key = p;
			std::transform(key.begin(), key.end(), key.begin(), ::tolower);
			if (seen.insert(key).second)
				paths.push_back(p);
		}
		RebuildFilter();
	}
	std::vector<std::string> const& GetPaths() const { return paths; }

	void SetFilter(wxString const& f) {
		filter = f.Lower();
		RebuildFilter();
	}

private:
	void RebuildFilter() {
		filtered.clear();
		for (size_t i = 0; i < paths.size(); ++i) {
			if (filter.empty() || wxString(paths[i]).Lower().Contains(filter))
				filtered.push_back(i);
		}
		// 通知整棵树重建（Cleared 后重加，覆盖初始填充与过滤变更两种场景）
		Cleared();
		for (size_t i = 0; i < filtered.size(); ++i)
			ItemAdded(wxDataViewItem(), ItemForRow(i));
	}

	/// 由过滤后行号映射回全量索引
	size_t RealIndex(int row) const {
		if (row < 0 || static_cast<size_t>(row) >= filtered.size())
			return static_cast<size_t>(-1);
		return filtered[row];
	}

public:
	unsigned int GetColumnCount() const override { return 1; }
	wxString GetColumnType(unsigned int) const override { return "string"; }

	void GetValue(wxVariant &variant, wxDataViewItem const& item, unsigned int col) const override {
		int row = RowForItem(item);
		if (row < 0) return;
		size_t real = RealIndex(row);
		if (real == static_cast<size_t>(-1)) return;
		if (col == 0)
			variant = wxString(paths[real]);
	}

	bool SetValue(wxVariant const& variant, wxDataViewItem const& item, unsigned int col) override {
		int row = RowForItem(item);
		if (row < 0) return false;
		size_t real = RealIndex(row);
		if (real == static_cast<size_t>(-1)) return false;
		if (col == 0) {
			wxString val = variant.GetString();
			val.Trim();
			if (val.empty()) return false;
			paths[real] = std::string(val.utf8_str());
			return true;
		}
		return false;
	}

	bool IsContainer(wxDataViewItem const&) const override { return false; }
	wxDataViewItem GetParent(wxDataViewItem const&) const override { return wxDataViewItem(); }

	unsigned int GetChildren(wxDataViewItem const& item, wxDataViewItemArray &children) const override {
		if (item.IsOk()) return 0;
		for (size_t i = 0; i < filtered.size(); ++i)
			children.Add(ItemForRow(i));
		return static_cast<unsigned int>(filtered.size());
	}

	/// 在末尾追加一条路径（按小写路径去重），返回其行号（过滤后）；重复返回已存在行号
	int AppendPath(std::string const& p) {
		if (p.empty())
			return -1;
		std::string key = p;
		std::transform(key.begin(), key.end(), key.begin(), ::tolower);
		for (size_t i = 0; i < paths.size(); ++i) {
			std::string exist = paths[i];
			std::transform(exist.begin(), exist.end(), exist.begin(), ::tolower);
			if (exist == key) {
				for (size_t r = 0; r < filtered.size(); ++r)
					if (filtered[r] == i)
						return static_cast<int>(r);
				return -1;
			}
		}
		paths.push_back(p);
		if (filter.empty() || wxString(p).Lower().Contains(filter))
			filtered.push_back(paths.size() - 1);
		ItemAdded(wxDataViewItem(), ItemForRow(static_cast<size_t>(filtered.size()) - 1));
		return static_cast<int>(filtered.size()) - 1;
	}

};

class Interface_Fonts final : public OptionPage {
	wxDataViewCtrl *dvc;
	wxObjectDataPtr<UserFontPathModel> model;
	wxSearchCtrl *quick_search;

	std::vector<std::string> CollectPaths() const { return model->GetPaths(); }
	void MarkDirty();

	void OnAdd(wxCommandEvent&);
	void OnAddDir(wxCommandEvent&);
	void OnDelete(wxCommandEvent&);
	void OnUpdateFilter(wxCommandEvent&);

public:
	Interface_Fonts(wxTreebook *book, Preferences *parent);
};

class Interface_Hotkeys final : public OptionPage {
	wxDataViewCtrl *dvc;
	wxObjectDataPtr<HotkeyDataViewModel> model;
	wxSearchCtrl *quick_search;

	void OnNewButton(wxCommandEvent&);
	void OnUpdateFilter(wxCommandEvent&);
public:
	Interface_Hotkeys(wxTreebook *book, Preferences *parent);
};

/// Interface Hotkeys preferences subpage
Interface_Hotkeys::Interface_Hotkeys(wxTreebook *book, Preferences *parent)
: OptionPage(book, parent, _("Hotkeys"), OptionPage::PAGE_SUB)
, model(new HotkeyDataViewModel(parent))
{
	quick_search = new wxSearchCtrl(this, -1);
	auto new_button = new wxButton(this, -1, _("&New"));
	auto edit_button = new wxButton(this, -1, _("&Edit"));
	auto delete_button = new wxButton(this, -1, _("&Delete"));

	new_button->Bind(wxEVT_BUTTON, &Interface_Hotkeys::OnNewButton, this);
	edit_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { edit_item(dvc, dvc->GetSelection()); });
	delete_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) { model->Delete(dvc->GetSelection()); });

	quick_search->Bind(wxEVT_TEXT, &Interface_Hotkeys::OnUpdateFilter, this);
	quick_search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) { quick_search->SetValue(""); });

	long dvc_style = wxDV_ROW_LINES | wxDV_VERT_RULES;
#ifdef wxDV_VARIABLE_LINE_HEIGHT
	dvc_style |= wxDV_VARIABLE_LINE_HEIGHT;
#endif
	dvc = new wxDataViewCtrl(this, -1, wxDefaultPosition, wxDefaultSize, dvc_style);
	dvc->AssociateModel(model.get());
#ifndef __APPLE__
	dvc->AppendColumn(new wxDataViewColumn(_("Hotkey"), new HotkeyRenderer, 0, book->FromDIP(125), wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));
	dvc->AppendColumn(new wxDataViewColumn(_("Command"), new CommandRenderer, 1, book->FromDIP(250), wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));
#else
	auto col = new wxDataViewColumn("Hotkey", new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_EDITABLE), 0, 150, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE);
	col->SetMinWidth(150);
	dvc->AppendColumn(col);
	dvc->AppendColumn(new wxDataViewColumn("Command", new wxDataViewChoiceRenderer(get_hotkey_command_choices(model.get()), wxDATAVIEW_CELL_EDITABLE), 1, 250, wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));
#endif
	dvc->AppendColumn(new wxDataViewColumn(_("Description"), new DescriptionRenderer, 2, book->FromDIP(300), wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));

	// 热键列编辑完成后，若命令名为空则自动跳转到命令列编辑
	dvc->Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE, [this](wxDataViewEvent& evt) {
		if (evt.GetColumn() == 0 && !evt.IsEditCancelled()) {
			wxDataViewItem item = evt.GetItem();
			wxVariant value;
			model->GetValue(value, item, 1);
			wxString cmdText;
			if (value.GetType() == "wxDataViewIconText") {
				wxDataViewIconText iconText;
				iconText << value;
				cmdText = iconText.GetText();
			} else {
				cmdText = value.GetString();
			}
			if (cmdText.empty()) {
				CallAfter([this, item] {
					if (item.IsOk())
						dvc->EditItem(item, dvc->GetColumn(1));
				});
			}
		}
		evt.Skip();
	});

	wxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
	buttons->Add(quick_search, wxSizerFlags(1).CenterVertical().Border());
	buttons->Add(new_button, wxSizerFlags().Border());
	buttons->Add(edit_button, wxSizerFlags().Border());
	buttons->Add(delete_button, wxSizerFlags().Border());

	sizer->Add(buttons, wxSizerFlags().Expand());
	sizer->Add(dvc, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT));

	SetSizerAndFit(sizer);
}

void Interface_Hotkeys::OnNewButton(wxCommandEvent&) {
	wxDataViewItem sel = dvc->GetSelection();
	dvc->ExpandAncestors(sel);
	dvc->Expand(sel);

	wxDataViewItem new_item = model->New(sel);
	if (new_item.IsOk()) {
		dvc->Select(new_item);
		dvc->EnsureVisible(new_item);
		edit_item(dvc, new_item);
	}
}

void Interface_Hotkeys::OnUpdateFilter(wxCommandEvent&) {
	model->SetFilter(quick_search->GetValue());

	if (!quick_search->GetValue().empty()) {
		wxDataViewItemArray contexts;
		model->GetChildren(wxDataViewItem(nullptr), contexts);
		for (auto const& context : contexts)
			dvc->Expand(context);
	}
}

/// 字体路径列表变更后，将最新路径集合（去重）写入待应用选项（OK/Apply 时落盘）
void Interface_Fonts::MarkDirty() {
	std::vector<std::string> raw = model->GetPaths();
	std::vector<std::string> paths;
	std::unordered_set<std::string> seen;
	for (auto const& p : raw) {
		if (p.empty())
			continue;
		std::string key = p;
		std::transform(key.begin(), key.end(), key.begin(), ::tolower);
		if (seen.insert(key).second)
			paths.push_back(p);
	}
	if (paths.empty())
		paths.emplace_back("");
	auto ov = std::make_unique<agi::OptionValueListString>(
		"App/User Font Paths", paths);
	parent->SetOption(std::move(ov));
}

/// 字体管理器偏好设置子页
Interface_Fonts::Interface_Fonts(wxTreebook *book, Preferences *parent)
: OptionPage(book, parent, _("Font Manager"), OptionPage::PAGE_SUB)
, model(new UserFontPathModel())
{
	quick_search = new wxSearchCtrl(this, -1);
	auto add_button = new wxButton(this, -1, _("Add File (&A)..."));
	auto add_dir_button = new wxButton(this, -1, _("Add Dir (&D)..."));
	auto delete_button = new wxButton(this, -1, _("&Remove"));

	add_button->Bind(wxEVT_BUTTON, &Interface_Fonts::OnAdd, this);
	add_dir_button->Bind(wxEVT_BUTTON, &Interface_Fonts::OnAddDir, this);
	delete_button->Bind(wxEVT_BUTTON, &Interface_Fonts::OnDelete, this);

	quick_search->Bind(wxEVT_TEXT, &Interface_Fonts::OnUpdateFilter, this);
	quick_search->Bind(wxEVT_SEARCHCTRL_CANCEL_BTN, [this](wxCommandEvent&) {
		quick_search->SetValue("");
	});

	long dvc_style = wxDV_ROW_LINES | wxDV_VERT_RULES | wxDV_NO_HEADER | wxDV_MULTIPLE;
#ifdef wxDV_VARIABLE_LINE_HEIGHT
	dvc_style |= wxDV_VARIABLE_LINE_HEIGHT;
#endif
	dvc = new wxDataViewCtrl(this, -1, wxDefaultPosition, wxDefaultSize, dvc_style);
	dvc->AssociateModel(model.get());
	dvc->AppendColumn(new wxDataViewColumn(
		_("Font Path"),
		new wxDataViewTextRenderer("string", wxDATAVIEW_CELL_EDITABLE),
		0, book->FromDIP(440), wxALIGN_LEFT, wxCOL_SORTABLE | wxCOL_RESIZABLE));

	// 模型与视图关联后再填充数据，确保初始行正确显示
	model->SetPaths(OPT_GET("App/User Font Paths")->GetListString());

	dvc->Bind(wxEVT_DATAVIEW_ITEM_EDITING_DONE, [this](wxDataViewEvent& evt) {
		if (!evt.IsEditCancelled())
			MarkDirty();
		evt.Skip();
	});

	wxSizer *buttons = new wxBoxSizer(wxHORIZONTAL);
	buttons->Add(quick_search, wxSizerFlags(1).CenterVertical().Border());
	buttons->Add(add_button, wxSizerFlags().Border());
	buttons->Add(add_dir_button, wxSizerFlags().Border());
	buttons->Add(delete_button, wxSizerFlags().Border());

	// Ctrl+A 全选
	dvc->Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent& evt) {
		if (evt.ControlDown() && evt.GetKeyCode() == 'A') {
			wxDataViewItemArray children;
			model->GetChildren(wxDataViewItem(), children);
			for (auto const& child : children)
				dvc->Select(child);
			return;
		}
		evt.Skip();
	});

	auto hint_text = new wxStaticText(this, -1,
		_("Add fonts or folders containing fonts to expand the font selection, "
		  "used for fonts that are not installed in the system."));
	hint_text->Wrap(book->FromDIP(460));
	sizer->Add(hint_text, wxSizerFlags().Expand().Border());

	sizer->Add(buttons, wxSizerFlags().Expand().Border());
	sizer->Add(dvc, wxSizerFlags(1).Expand().Border(wxLEFT | wxRIGHT));
	sizer->AddSpacer(10);

	// 迁移：收藏字体 / 字体预览 分区
	auto favoriteFont = PageSizer(_("Favorite Font"));
	OptionAdd(favoriteFont, _("Favorite Font Number"),
		"Subtitle/Favorite Font Number")
		->SetToolTip(_("Sets the maximum number of favorite fonts"));

	auto fontPreview = PageSizer(_("Font Preview"));
	OptionAdd(fontPreview, _("Font Preview Size"), "App/Font Preview Size")
		->SetToolTip(_("Sets the font size used for rendering preview in font list"));

	SetSizerAndFit(sizer);
}

/// 添加字体文件（多选）
void Interface_Fonts::OnAdd(wxCommandEvent&) {
	wxFileDialog fdlg(this, _("Add font files"), wxEmptyString, wxEmptyString,
		_("Font files (*.ttf;*.otf;*.ttc;*.woff;*.fon;*.otc;*.pfb)|"
		  "*.ttf;*.otf;*.ttc;*.woff;*.fon;*.otc;*.pfb|All files (*.*)|*.*"),
		wxFD_OPEN | wxFD_MULTIPLE | wxFD_FILE_MUST_EXIST);
	if (fdlg.ShowModal() == wxID_OK) {
		wxArrayString files;
		fdlg.GetPaths(files);
		if (!files.empty()) {
			const size_t old_count = model->GetPaths().size();
			for (auto const& f : files)
				model->AppendPath(std::string(f.utf8_str()));
			if (model->GetPaths().size() > old_count)
				MarkDirty();
		}
	}
}

/// 添加字体目录
void Interface_Fonts::OnAddDir(wxCommandEvent&) {
	wxDirDialog ddlg(this, _("Add a directory of fonts"), wxEmptyString,
		wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
	if (ddlg.ShowModal() == wxID_OK) {
		const wxString dir = ddlg.GetPath();
		if (!dir.empty()) {
			const size_t old_count = model->GetPaths().size();
			model->AppendPath(std::string(dir.utf8_str()));
			if (model->GetPaths().size() > old_count)
				MarkDirty();
		}
	}
}
void Interface_Fonts::OnDelete(wxCommandEvent&) {
	wxDataViewItemArray selections;
	if (dvc->GetSelections(selections) == 0) return;

	std::vector<int> real_indices;
	for (auto const& sel : selections) {
		int row = UserFontPathModel::RowForItem(sel);
		if (row < 0) continue;
		int real = model->GetRealIndex(row);
		if (real >= 0)
			real_indices.push_back(real);
	}
	if (real_indices.empty()) return;

	auto paths = model->GetPaths();
	std::sort(real_indices.begin(), real_indices.end(), std::greater<>());
	for (int real : real_indices)
		paths.erase(paths.begin() + real);
	model->SetPaths(paths);
	MarkDirty();
}

void Interface_Fonts::OnUpdateFilter(wxCommandEvent&) {
	model->SetFilter(quick_search->GetValue());
}
}

void Preferences::SetOption(std::unique_ptr<agi::OptionValue> new_value) {
	pending_changes[new_value->GetName()] = std::move(new_value);
	applyButton->Enable(true);
}

void Preferences::AddPendingChange(Thunk const& callback) {
	pending_callbacks.push_back(callback);
	applyButton->Enable(true);
}

void Preferences::AddChangeableOption(std::string const& name) {
	option_names.push_back(name);
}

void Preferences::AddSignalConnection(agi::signal::Connection connection) {
	signal_connections.push_back(std::move(connection));
}

void Preferences::OnOK(wxCommandEvent &event) {
	OnApply(event);
	EndModal(0);
}

void Preferences::OnApply(wxCommandEvent &) {
	for (auto const& change : pending_changes)
		OPT_SET(change.first)->Set(change.second.get());
	pending_changes.clear();

	for (auto const& thunk : pending_callbacks)
		thunk();
	pending_callbacks.clear();

	applyButton->Enable(false);
	config::opt->Flush();
}

void Preferences::OnResetDefault(wxCommandEvent&) {
	if (wxYES != wxMessageBox(_("Are you sure that you want to restore the defaults? All your settings will be overridden."), _("Restore defaults?"), wxYES_NO))
		return;

	for (auto const& opt_name : option_names) {
		agi::OptionValue *opt = OPT_SET(opt_name);
		if (!opt->IsDefault())
			opt->Reset();
	}
	config::opt->Flush();

	agi::hotkey::Hotkey def_hotkeys("", GET_DEFAULT_CONFIG(default_hotkey));
	hotkey::inst->SetHotkeyMap(def_hotkeys.GetHotkeyMap());

	// Close and reopen the dialog to update all the controls with the new values
	OPT_SET("Tool/Preferences/Page")->SetInt(book->GetSelection());
	EndModal(-1);
}

Preferences::Preferences(wxWindow *parent, agi::Context *context)
: wxDialog(parent, -1, _("Preferences"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
, context(context) {
	SetIcon(GETICON(options_button_16));

	// 冻结窗口以抑制创建大量控件时的中间重绘和布局计算
	Freeze();

	book = new wxTreebook(this, -1, wxDefaultPosition, wxDefaultSize);
	General(book, this);
	General_DefaultStyles(book, this);
	Audio(book, this);
	Video(book, this);
	Interface(book, this);
	Interface_EditBox(book, this);
	Interface_CharCounter(book, this);
	new Interface_Fonts(book, this);
	Interface_Colours(book, this);
	new Interface_Hotkeys(book, this);
	Backup(book, this);
	Automation(book, this);
	MCP(book, this);
	Advanced(book, this);
	Advanced_Audio(book, this);
	Advanced_Video(book, this);
	VapourSynth(book, this);

	book->Fit();

	book->ChangeSelection(OPT_GET("Tool/Preferences/Page")->GetInt());
	book->Bind(wxEVT_TREEBOOK_PAGE_CHANGED, [](wxBookCtrlEvent &evt) {
		OPT_SET("Tool/Preferences/Page")->SetInt(evt.GetSelection());
	});

	// Bottom Buttons
	auto stdButtonSizer = CreateStdDialogButtonSizer(wxOK | wxCANCEL | wxAPPLY | wxHELP);
	stdButtonSizer->GetHelpButton()->SetLabel(_("Help"));
	applyButton = stdButtonSizer->GetApplyButton();
	wxSizer *buttonSizer = new wxBoxSizer(wxHORIZONTAL);
	auto defaultButton = new wxButton(this, -1, _("&Restore Defaults"));
	buttonSizer->Add(defaultButton, wxSizerFlags(0).Expand());
	buttonSizer->AddStretchSpacer(1);
	buttonSizer->Add(stdButtonSizer, wxSizerFlags(0).Expand());

	// Main Sizer
	wxSizer *mainSizer = new wxBoxSizer(wxVERTICAL);
	mainSizer->Add(book, wxSizerFlags(1).Expand().Border());
	mainSizer->Add(buttonSizer, wxSizerFlags(0).Expand().Border(wxALL & ~wxTOP));

	SetSizerAndFit(mainSizer);
	CenterOnParent();

	// 解冻窗口，允许一次性重绘
	Thaw();

	applyButton->Enable(false);

	Bind(wxEVT_BUTTON, &Preferences::OnOK, this, wxID_OK);
	Bind(wxEVT_BUTTON, &Preferences::OnApply, this, wxID_APPLY);
	Bind(wxEVT_BUTTON, std::bind(&HelpButton::OpenPage, "Options"), wxID_HELP);
	defaultButton->Bind(wxEVT_BUTTON, &Preferences::OnResetDefault, this);
}

void ShowPreferences(agi::Context *context) {
	while (Preferences(context->parent, context).ShowModal() < 0);
}
