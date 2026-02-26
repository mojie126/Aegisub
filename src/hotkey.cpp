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

#include <libaegisub/hotkey.h>

#include "include/aegisub/hotkey.h"

#include "libresrc/libresrc.h"
#include "command/command.h"
#include "compat.h"
#include "options.h"

#include <libaegisub/path.h>

#include <array>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/iterator_range.hpp>
#include <wx/intl.h>
#include <wx/msgdlg.h>

namespace {
	using agi::hotkey::Combo;
	static const std::array<Combo, 1> added_hotkeys_cj = {
		Combo("Video", "time/align", "KP_TAB"),
	};
	static const std::array<Combo, 1> added_hotkeys_video_space = {
		Combo("Video", "play/toggle/av", "Space"),
	};
	static const std::array<Combo, 1> added_hotkeys_7035 = {
		Combo("Audio", "audio/play/line", "R"),
	};
	static const std::array<Combo, 4> added_hotkeys_7070 = {
		Combo("Subtitle Edit Box", "edit/color/primary", "Alt-1"),
		Combo("Subtitle Edit Box", "edit/color/secondary", "Alt-2"),
		Combo("Subtitle Edit Box", "edit/color/outline", "Alt-3"),
		Combo("Subtitle Edit Box", "edit/color/shadow", "Alt-4"),
	};
	static const std::array<Combo, 1> added_hotkeys_shift_back = {
		Combo("Default", "edit/line/duplicate/shift_back", "Ctrl-Shift-D"),
	};
#ifdef __WXMAC__
	static const std::array<Combo, 1> added_hotkeys_minimize = {
		Combo("Default", "app/minimize", "Ctrl-M"),
	};
#endif

	template<std::size_t SIZ>
	void migrate_hotkeys(const std::array<Combo, SIZ> &added) {
		auto hk_map = hotkey::inst->GetHotkeyMap();
		bool changed = false;

		for (auto item: added) {
			if (hotkey::inst->HasHotkey(item.Context(), item.Str()))
				continue;
			hk_map.insert(make_pair(item.CmdName(), std::move(item)));
			changed = true;
		}

		if (changed)
			hotkey::inst->SetHotkeyMap(std::move(hk_map));
	}

	void migrate_space_to_play_toggle_av() {
		auto hk_map = hotkey::inst->GetHotkeyMap();
		bool changed = false;

		auto remap_space = [&](const char *source_command, const char *context) {
			bool has_target_space = false;
			for (auto const& hotkey : boost::make_iterator_range(hk_map.equal_range("play/toggle/av"))) {
				if (hotkey.second.Context() == context && hotkey.second.Str() == "Space") {
					has_target_space = true;
					break;
				}
			}

			for (auto it = hk_map.lower_bound(source_command); it != hk_map.upper_bound(source_command); ) {
				if (it->second.Context() == context && it->second.Str() == "Space") {
					if (!has_target_space) {
						hk_map.insert({"play/toggle/av", agi::hotkey::Combo(context, "play/toggle/av", "Space")});
						has_target_space = true;
					}
					it = hk_map.erase(it);
					changed = true;
				}
				else {
					++it;
				}
			}
		};

		remap_space("audio/play/selection", "Audio");
		remap_space("video/play", "Video");

		if (changed)
			hotkey::inst->SetHotkeyMap(std::move(hk_map));
	}
}

namespace hotkey {

agi::hotkey::Hotkey *inst = nullptr;
void init() {
	inst = new agi::hotkey::Hotkey(
		config::path->Decode("?user/hotkey.json"),
		GET_DEFAULT_CONFIG(default_hotkey));

	auto migrations = OPT_GET("App/Hotkey Migrations")->GetListString();

	if (boost::find(migrations, "cj") == end(migrations)) {
		migrate_hotkeys(added_hotkeys_cj);
		migrations.emplace_back("cj");
	}

	if (boost::find(migrations, "7035") == end(migrations)) {
		migrate_hotkeys(added_hotkeys_7035);
		migrations.emplace_back("7035");
	}

	if (boost::find(migrations, "7070") == end(migrations)) {
		migrate_hotkeys(added_hotkeys_7070);
		migrations.emplace_back("7070");
	}

	if (boost::find(migrations, "edit/line/duplicate/shift_back") == end(migrations)) {
		migrate_hotkeys(added_hotkeys_shift_back);
		migrations.emplace_back("edit/line/duplicate/shift_back");
	}

	if (boost::find(migrations, "duplicate -> split") == end(migrations)) {
		auto hk_map = hotkey::inst->GetHotkeyMap();
		for (auto const& hotkey : boost::make_iterator_range(hk_map.equal_range("edit/line/duplicate/shift"))) {
			auto combo = agi::hotkey::Combo(hotkey.second.Context(), "edit/line/split/before", hotkey.second.Str());
			hk_map.emplace(combo.CmdName(), combo);
		}
		for (auto const& hotkey : boost::make_iterator_range(hk_map.equal_range("edit/line/duplicate/shift_back"))) {
			auto combo = agi::hotkey::Combo(hotkey.second.Context(), "edit/line/split/after", hotkey.second.Str());
			hk_map.emplace(combo.CmdName(), combo);
		}

		hk_map.erase("edit/line/duplicate/shift");
		hk_map.erase("edit/line/duplicate/shift_back");

		hotkey::inst->SetHotkeyMap(std::move(hk_map));
		migrations.emplace_back("duplicate -> split");
	}

#ifdef __WXMAC__
	if (boost::find(migrations, "app/minimize") == end(migrations)) {
		migrate_hotkeys(added_hotkeys_minimize);
		migrations.emplace_back("app/minimize");
	}
#endif

	if (boost::find(migrations, "space -> play/toggle/av") == end(migrations)) {
		migrate_space_to_play_toggle_av();
		migrations.emplace_back("space -> play/toggle/av");
	}

	// 确保 Video 上下文中存在 Space → play/toggle/av 快捷键
	if (boost::find(migrations, "video_space_play") == end(migrations)) {
		migrate_hotkeys(added_hotkeys_video_space);
		migrations.emplace_back("video_space_play");
	}

	OPT_SET("App/Hotkey Migrations")->SetListString(std::move(migrations));
}

void clear() {
	delete inst;
}

static const char *keycode_name(int code) {
	switch (code) {
	case WXK_BACK:            return "Backspace";
	case WXK_TAB:             return "Tab";
	case WXK_RETURN:          return "Enter";
	case WXK_ESCAPE:          return "Escape";
	case WXK_SPACE:           return "Space";
	case WXK_DELETE:          return "Delete";
	case WXK_SHIFT:           return "Shift";
	case WXK_ALT:             return "Alt";
	case WXK_CONTROL:         return "Control";
	case WXK_PAUSE:           return "Pause";
	case WXK_END:             return "End";
	case WXK_HOME:            return "Home";
	case WXK_LEFT:            return "Left";
	case WXK_UP:              return "Up";
	case WXK_RIGHT:           return "Right";
	case WXK_DOWN:            return "Down";
	case WXK_PRINT:           return "Print";
	case WXK_INSERT:          return "Insert";
	case WXK_NUMPAD0:         return "KP_0";
	case WXK_NUMPAD1:         return "KP_1";
	case WXK_NUMPAD2:         return "KP_2";
	case WXK_NUMPAD3:         return "KP_3";
	case WXK_NUMPAD4:         return "KP_4";
	case WXK_NUMPAD5:         return "KP_5";
	case WXK_NUMPAD6:         return "KP_6";
	case WXK_NUMPAD7:         return "KP_7";
	case WXK_NUMPAD8:         return "KP_8";
	case WXK_NUMPAD9:         return "KP_9";
	case WXK_MULTIPLY:        return "Asterisk";
	case WXK_ADD:             return "Plus";
	case WXK_SUBTRACT:        return "Hyphen";
	case WXK_DECIMAL:         return "Period";
	case WXK_DIVIDE:          return "Slash";
	case WXK_F1:              return "F1";
	case WXK_F2:              return "F2";
	case WXK_F3:              return "F3";
	case WXK_F4:              return "F4";
	case WXK_F5:              return "F5";
	case WXK_F6:              return "F6";
	case WXK_F7:              return "F7";
	case WXK_F8:              return "F8";
	case WXK_F9:              return "F9";
	case WXK_F10:             return "F10";
	case WXK_F11:             return "F11";
	case WXK_F12:             return "F12";
	case WXK_F13:             return "F13";
	case WXK_F14:             return "F14";
	case WXK_F15:             return "F15";
	case WXK_F16:             return "F16";
	case WXK_F17:             return "F17";
	case WXK_F18:             return "F18";
	case WXK_F19:             return "F19";
	case WXK_F20:             return "F20";
	case WXK_F21:             return "F21";
	case WXK_F22:             return "F22";
	case WXK_F23:             return "F23";
	case WXK_F24:             return "F24";
	case WXK_NUMLOCK:         return "Num_Lock";
	case WXK_SCROLL:          return "Scroll_Lock";
	case WXK_PAGEUP:          return "PageUp";
	case WXK_PAGEDOWN:        return "PageDown";
	case WXK_NUMPAD_SPACE:    return "KP_Space";
	case WXK_NUMPAD_TAB:      return "KP_Tab";
	case WXK_NUMPAD_ENTER:    return "KP_Enter";
	case WXK_NUMPAD_F1:       return "KP_F1";
	case WXK_NUMPAD_F2:       return "KP_F2";
	case WXK_NUMPAD_F3:       return "KP_F3";
	case WXK_NUMPAD_F4:       return "KP_F4";
	case WXK_NUMPAD_HOME:     return "KP_Home";
	case WXK_NUMPAD_LEFT:     return "KP_Left";
	case WXK_NUMPAD_UP:       return "KP_Up";
	case WXK_NUMPAD_RIGHT:    return "KP_Right";
	case WXK_NUMPAD_DOWN:     return "KP_Down";
	case WXK_NUMPAD_PAGEUP:   return "KP_PageUp";
	case WXK_NUMPAD_PAGEDOWN: return "KP_PageDown";
	case WXK_NUMPAD_END:      return "KP_End";
	case WXK_NUMPAD_BEGIN:    return "KP_Begin";
	case WXK_NUMPAD_INSERT:   return "KP_insert";
	case WXK_NUMPAD_DELETE:   return "KP_Delete";
	case WXK_NUMPAD_EQUAL:    return "KP_Equal";
	case WXK_NUMPAD_MULTIPLY: return "KP_Multiply";
	case WXK_NUMPAD_ADD:      return "KP_Add";
	case WXK_NUMPAD_SUBTRACT: return "KP_Subtract";
	case WXK_NUMPAD_DECIMAL:  return "KP_Decimal";
	case WXK_NUMPAD_DIVIDE:   return "KP_Divide";
	default: return "";
	}
}

std::string keypress_to_str(int key_code, int modifier) {
	std::string combo;
	if ((modifier != wxMOD_NONE)) {
		if ((modifier & wxMOD_CMD) != 0) combo.append("Ctrl-");
		if ((modifier & wxMOD_ALT) != 0) combo.append("Alt-");
		if ((modifier & wxMOD_SHIFT) != 0) combo.append("Shift-");
	}

	if (key_code > 32 && key_code < 127)
		combo += (char)key_code;
	else
		combo += keycode_name(key_code);

	return combo;
}

static bool check(std::string_view context, agi::Context *c, int key_code, int modifier) {
	std::string combo = keypress_to_str(key_code, modifier);
	if (combo.empty()) return false;

	auto command = inst->Scan(context, combo, OPT_GET("Audio/Medusa Timing Hotkeys")->GetBool());
	if (!command.empty()) {
		cmd::call(command, c);
		return true;
	}
	return false;
}

bool check(std::string_view context, agi::Context *c, wxKeyEvent &evt) {
	try {
		if (!check(context, c, evt.GetKeyCode(), evt.GetModifiers())) {
			evt.Skip();
			return false;
		}
		return true;
	}
	catch (cmd::CommandNotFound const& e) {
		wxMessageBox(to_wx(e.GetMessage()), _("Invalid command name for hotkey"),
			wxOK | wxICON_ERROR | wxCENTER | wxSTAY_ON_TOP);
		return true;
	}
}

std::vector<std::string> get_hotkey_strs(std::string_view context, std::string_view command) {
	return inst->GetHotkeys(context, command);
}

std::string_view get_hotkey_str_first(std::string_view context, std::string_view command) {
	return inst->GetHotkey(context, command);
}

} // namespace hotkey
