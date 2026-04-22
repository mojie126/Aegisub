// Copyright (c) 2005-2010, Niels Martin Hansen
// Copyright (c) 2005-2010, Rodrigo Braz Monteiro
// Copyright (c) 2010, Amar Takhar
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

#include <wx/msgdlg.h>

#include "command.h"

#include <libaegisub/fs.h>
#include <libaegisub/log.h>
#include <libaegisub/path.h>

#include "../compat.h"
#include "../dialog_detached_video.h"
#include "../dialog_manager.h"
#include "../dialogs.h"
#include "../frame_main.h"
#include "../include/aegisub/context.h"
#include "../libresrc/libresrc.h"
#include "../main.h"
#include "../options.h"
#include "../project.h"
#include "../utils.h"

namespace {
	using cmd::Command;

struct app_about final : public Command {
	CMD_NAME("app/about")
	CMD_ICON(about_menu)
	STR_MENU("&About")
	STR_DISP("About")
	STR_HELP("About Aegisub")

	void operator()(agi::Context *c) override {
		ShowAboutDialog(c->parent);
	}
};

struct app_display_audio_subs final : public Command {
	CMD_NAME("app/display/audio_subs")
	STR_MENU("&Audio+Subs View")
	STR_DISP("Audio+Subs View")
	STR_HELP("Display audio and the subtitles grid only")
	CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

	void operator()(agi::Context *c) override {
		c->frame->SetDisplayMode(0,1);
	}

	bool Validate(const agi::Context *c) override {
		return !!c->project->AudioProvider();
	}

	bool IsActive(const agi::Context *c) override {
		return c->frame->IsAudioShown() && !c->frame->IsVideoShown();
	}
};

struct app_display_full final : public Command {
	CMD_NAME("app/display/full")
	STR_MENU("&Full view")
	STR_DISP("Full view")
	STR_HELP("Display audio, video and then subtitles grid")
	CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

	void operator()(agi::Context *c) override {
		c->frame->SetDisplayMode(1,1);
	}

	bool Validate(const agi::Context *c) override {
		return c->project->AudioProvider() && c->project->VideoProvider() && !c->dialog->Get<DialogDetachedVideo>();
	}

	bool IsActive(const agi::Context *c) override {
		return c->frame->IsAudioShown() && c->frame->IsVideoShown();
	}
};

struct app_display_subs final : public Command {
	CMD_NAME("app/display/subs")
	STR_MENU("S&ubs Only View")
	STR_DISP("Subs Only View")
	STR_HELP("Display the subtitles grid only")
	CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

	void operator()(agi::Context *c) override {
		c->frame->SetDisplayMode(0, 0);
	}

	bool IsActive(const agi::Context *c) override {
		return !c->frame->IsAudioShown() && !c->frame->IsVideoShown();
	}
};

struct app_display_video_subs final : public Command {
	CMD_NAME("app/display/video_subs")
	STR_MENU("&Video+Subs View")
	STR_DISP("Video+Subs View")
	STR_HELP("Display video and the subtitles grid only")
	CMD_TYPE(COMMAND_VALIDATE | COMMAND_RADIO)

	void operator()(agi::Context *c) override {
		c->frame->SetDisplayMode(1, 0);
	}

	bool Validate(const agi::Context *c) override {
		return c->project->VideoProvider() && !c->dialog->Get<DialogDetachedVideo>();
	}

	bool IsActive(const agi::Context *c) override {
		return !c->frame->IsAudioShown() && c->frame->IsVideoShown();
	}
};

struct app_exit final : public Command {
	CMD_NAME("app/exit")
	STR_MENU("E&xit")
	STR_DISP("Exit")
	STR_HELP("Exit the application")

	void operator()(agi::Context *) override {
		wxGetApp().CloseAll();
	}
};

struct app_language final : public Command {
	CMD_NAME("app/language")
	CMD_ICON(languages_menu)
	STR_MENU("&Language...")
	STR_DISP("Language")
	STR_HELP("Select Aegisub interface language")

	void operator()(agi::Context *c) override {
		// Get language
		auto new_language = wxGetApp().locale.PickLanguage();
		if (new_language.empty()) return;

		OPT_SET("App/Language")->SetString(new_language);

		// Ask to restart program
		int result = wxMessageBox(_("Aegisub needs to be restarted so that the new language can be applied. Restart now?"), _("Restart Aegisub?"), wxYES_NO | wxICON_QUESTION |  wxCENTER);
		if (result == wxYES) {
			// Restart Aegisub
			if (c->frame->Close()) {
				RestartAegisub();
			}
		}
	}
};

struct app_log final : public Command {
	CMD_NAME("app/log")
	CMD_ICON(about_menu)
	STR_MENU("&Log window")
	STR_DISP("Log window")
	STR_HELP("View the event log")

	void operator()(agi::Context *c) override {
		ShowLogWindow(c);
	}
};

struct app_new_window final : public Command {
	CMD_NAME("app/new_window")
	CMD_ICON(new_window_menu)
	STR_MENU("New &Window")
	STR_DISP("New Window")
	STR_HELP("Open a new application window")

	void operator()(agi::Context *) override {
		wxGetApp().NewProjectContext();
	}
};

struct app_options final : public Command {
	CMD_NAME("app/options")
	CMD_ICON(options_button)
	STR_MENU("&Options...")
	STR_DISP("Options")
	STR_HELP("Configure Aegisub")

	void operator()(agi::Context *c) override {
		try {
			ShowPreferences(c);
		} catch (agi::Exception& e) {
			LOG_E("config/init") << "Caught exception: " << e.GetMessage();
		}
	}
};

struct app_toggle_global_hotkeys final : public Command {
	CMD_NAME("app/toggle/global_hotkeys")
	CMD_ICON(toggle_audio_medusa)
	STR_MENU("Toggle global hotkey overrides")
	STR_DISP("Toggle global hotkey overrides")
	STR_HELP("Toggle global hotkey overrides (Medusa Mode)")
	CMD_TYPE(COMMAND_TOGGLE)

	bool IsActive(const agi::Context *) override {
		return OPT_GET("Audio/Medusa Timing Hotkeys")->GetBool();
	}

	void operator()(agi::Context *) override {
		agi::OptionValue *opt = OPT_SET("Audio/Medusa Timing Hotkeys");
		opt->SetBool(!opt->GetBool());
	}
};

struct app_toggle_toolbar final : public Command {
	CMD_NAME("app/toggle/toolbar")
	STR_HELP("Toggle the main toolbar")
	CMD_TYPE(COMMAND_DYNAMIC_NAME)

	wxString StrMenu(const agi::Context *) const override {
		return OPT_GET("App/Show Toolbar")->GetBool() ?
			_("Hide Toolbar") :
			_("Show Toolbar");
	}

	wxString StrDisplay(const agi::Context *) const override {
		return StrMenu(nullptr);
	}

	void operator()(agi::Context *) override {
		agi::OptionValue *opt = OPT_SET("App/Show Toolbar");
		opt->SetBool(!opt->GetBool());
	}
};

struct app_updates final : public Command {
	CMD_NAME("app/updates")
	STR_MENU("&Check for Updates...")
	STR_DISP("Check for Updates")
	STR_HELP("Check to see if there is a new version of Aegisub available")

	void operator()(agi::Context *) override {
		PerformVersionCheck(true);
	}
};

#ifdef __WXMAC__
struct app_minimize final : public Command {
	CMD_NAME("app/minimize")
	STR_MENU("Minimize")
	STR_DISP("Minimize")
	STR_HELP("Minimize the active window")

	void operator()(agi::Context *c) override {
		c->frame->Iconize();
	}
};

struct app_maximize final : public Command {
	CMD_NAME("app/maximize")
	STR_MENU("Zoom")
	STR_DISP("Zoom")
	STR_HELP("Maximize the active window")

	void operator()(agi::Context *c) override {
		c->frame->Maximize(!c->frame->IsMaximized());
	}
};

struct app_bring_to_front final : public Command {
	CMD_NAME("app/bring_to_front")
	STR_MENU("Bring All to Front")
	STR_DISP("Bring All to Front")
	STR_HELP("Bring forward all open documents to the front")

	void operator()(agi::Context *) override {
		osx::bring_to_front();
	}
};
#endif

struct app_clear_cache final : public Command {
	CMD_NAME("app/clear_cache")
	STR_MENU("Clear &Cache...")
	STR_DISP("Clear Cache")
	STR_HELP("Clear audio and video index cache files")

	void operator()(agi::Context *c) override {
		if (wxMessageBox(_("Clear all audio and video cache files?"), _("Confirm"), wxYES_NO | wxICON_QUESTION, c->parent) != wxYES)
			return;

		int count = 0;

		auto countAndRemove = [&](agi::fs::path const& dir, std::string const& pattern) {
			if (!agi::fs::DirectoryExists(dir)) return;
			for (auto const& file : agi::fs::DirectoryIterator(dir, pattern)) {
				try {
					agi::fs::Remove(dir / file);
					++count;
				}
				catch (agi::Exception const& e) {
					LOG_W("app/clear_cache") << "Failed to delete " << (dir / file) << ": " << e.GetMessage();
				}
			}
		};

		countAndRemove(config::path->Decode("?local/ffms2cache/"), "*.ffindex");
		countAndRemove(config::path->Decode("?local/bsindex/"), "*.bsindex");
		countAndRemove(config::path->Decode("?local/bsindex/"), "*.json");
		countAndRemove(config::path->Decode("?local/vscache/"), "");

		wxMessageBox(wxString::Format(_("Deleted %d cache files."), count), _("Clear Cache"), wxOK | wxICON_INFORMATION, c->parent);
	}
};

struct app_clear_log final : public Command {
	CMD_NAME("app/clear_log")
	STR_MENU("Clear &Log Files...")
	STR_DISP("Clear Log Files")
	STR_HELP("Clear accumulated log files")

	void operator()(agi::Context *c) override {
		if (wxMessageBox(_("Clear all log files?"), _("Confirm"), wxYES_NO | wxICON_QUESTION, c->parent) != wxYES)
			return;

		int count = 0;
		auto dir = config::path->Decode("?user/log/");
		if (agi::fs::DirectoryExists(dir)) {
			for (auto const& file : agi::fs::DirectoryIterator(dir, "*.json")) {
				try {
					agi::fs::Remove(dir / file);
					++count;
				}
				catch (agi::Exception const& e) {
					LOG_W("app/clear_log") << "Failed to delete " << (dir / file) << ": " << e.GetMessage();
				}
			}
		}

		wxMessageBox(wxString::Format(_("Deleted %d log files."), count), _("Clear Log Files"), wxOK | wxICON_INFORMATION, c->parent);
	}
};

struct app_clear_recent final : public Command {
	CMD_NAME("app/clear_recent")
	STR_MENU("Clear &Recent Lists...")
	STR_DISP("Clear Recent Lists")
	STR_HELP("Clear all recently opened file lists")

	void operator()(agi::Context *c) override {
		if (wxMessageBox(_("Clear all recently opened file lists?"), _("Confirm"), wxYES_NO | wxICON_QUESTION, c->parent) != wxYES)
			return;

		config::mru->Clear("Audio");
		config::mru->Clear("Keyframes");
		config::mru->Clear("Subtitle");
		config::mru->Clear("Timecodes");
		config::mru->Clear("Video");
	}
};

struct app_clear_autosave final : public Command {
	CMD_NAME("app/clear_autosave")
	STR_MENU("Clear Auto&save/Backup...")
	STR_DISP("Clear Autosave/Backup")
	STR_HELP("Clear autosave and automatic backup files")

	void operator()(agi::Context *c) override {
		if (wxMessageBox(_("Clear all autosave and automatic backup files?"), _("Confirm"), wxYES_NO | wxICON_QUESTION, c->parent) != wxYES)
			return;

		int count = 0;

		auto countAndRemove = [&](agi::fs::path const& dir, std::string const& pattern) {
			if (!agi::fs::DirectoryExists(dir)) return;
			for (auto const& file : agi::fs::DirectoryIterator(dir, pattern)) {
				try {
					agi::fs::Remove(dir / file);
					++count;
				}
				catch (agi::Exception const& e) {
					LOG_W("app/clear_autosave") << "Failed to delete " << (dir / file) << ": " << e.GetMessage();
				}
			}
		};

		countAndRemove(config::path->Decode(OPT_GET("Path/Auto/Save")->GetString()), "*.AUTOSAVE.ass");

		auto backup_str = OPT_GET("Path/Auto/Backup")->GetString();
		if (!backup_str.empty())
			countAndRemove(config::path->Decode(backup_str), "*.ORIGINAL.*");

		wxMessageBox(wxString::Format(_("Deleted %d autosave/backup files."), count), _("Clear Autosave/Backup"), wxOK | wxICON_INFORMATION, c->parent);
	}
};

}

namespace cmd {
	void init_app() {
		reg(std::make_unique<app_about>());
		reg(std::make_unique<app_clear_autosave>());
		reg(std::make_unique<app_clear_cache>());
		reg(std::make_unique<app_clear_log>());
		reg(std::make_unique<app_clear_recent>());
		reg(std::make_unique<app_display_audio_subs>());
		reg(std::make_unique<app_display_full>());
		reg(std::make_unique<app_display_subs>());
		reg(std::make_unique<app_display_video_subs>());
		reg(std::make_unique<app_exit>());
		reg(std::make_unique<app_language>());
		reg(std::make_unique<app_log>());
		reg(std::make_unique<app_new_window>());
		reg(std::make_unique<app_options>());
		reg(std::make_unique<app_toggle_global_hotkeys>());
		reg(std::make_unique<app_toggle_toolbar>());
#ifdef __WXMAC__
		reg(std::make_unique<app_minimize>());
		reg(std::make_unique<app_maximize>());
		reg(std::make_unique<app_bring_to_front>());
#endif
#ifdef WITH_UPDATE_CHECKER
		reg(std::make_unique<app_updates>());
#endif
	}
}
