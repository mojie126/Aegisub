// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
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
//
// Aegisub Project http://www.aegisub.org/

#ifdef WITH_VAPOURSYNTH
#include "vapoursynth_common.h"

#include "compat.h"
#include "vapoursynth_wrap.h"
#include "options.h"
#include "utils.h"
#include <libaegisub/background_runner.h>
#include <libaegisub/format.h>
#include <libaegisub/fs.h>
#include <libaegisub/path.h>
#include <libaegisub/scoped_ptr.h>
#include <libaegisub/util.h>

#include <boost/algorithm/string/replace.hpp>
#include <cstring>

namespace {
	std::string TranslateProgressMessage(std::string const &message) {
		if (message == "Loading video file")
			return from_wx(_("Loading video file"));
		if (message == "Getting timecodes and keyframes from the index file")
			return from_wx(_("Getting timecodes and keyframes from the index file"));
		if (message == "Generating keyframes")
			return from_wx(_("Generating keyframes"));
		if (message == "Asking whether to generate keyframes")
			return from_wx(_("Asking whether to generate keyframes"));
		if (message == "Looking for keyframes")
			return from_wx(_("Looking for keyframes"));
		if (message == "Checking if the file has an audio track")
			return from_wx(_("Checking if the file has an audio track"));
		return message;
	}

	std::string TranslatePrefixedMessage(std::string const &message, char const *prefix, std::string const &translated_prefix) {
		const size_t prefix_size = strlen(prefix);
		if (message.compare(0, prefix_size, prefix) != 0)
			return "";
		return translated_prefix + message.substr(prefix_size);
	}
}

std::string TranslateVapourSynthErrorMessage(std::string const &message) {
	if (message == "Error creating core")
		return from_wx(_("Error creating core"));
	if (message == "Error creating script API")
		return from_wx(_("Error creating script API"));
	if (message == "Failed to set VSMap entry")
		return from_wx(_("Failed to set VSMap entry"));
	if (message == "Failed to create VSMap for script info")
		return from_wx(_("Failed to create VSMap for script info"));
	if (message == "Failed to set script info variables")
		return from_wx(_("Failed to set script info variables"));
	if (message == "No output node set")
		return from_wx(_("No output node set"));
	if (message == "Output node isn't an audio node")
		return from_wx(_("Output node isn't an audio node"));
	if (message == "Output node isn't a video node")
		return from_wx(_("Output node isn't a video node"));
	if (message == "Couldn't create map")
		return from_wx(_("Couldn't create map"));
	if (message == "Couldn't get video info")
		return from_wx(_("Couldn't get video info"));
	if (message == "Couldn't get frame properties")
		return from_wx(_("Couldn't get frame properties"));
	if (message == "Couldn't find std plugin")
		return from_wx(_("Couldn't find std plugin"));
	if (message == "Couldn't find resize plugin")
		return from_wx(_("Couldn't find resize plugin"));
	if (message == "Video doesn't have constant format")
		return from_wx(_("Video doesn't have constant format"));
	if (message == "Error getting keyframes from returned VSMap")
		return from_wx(_("Error getting keyframes from returned VSMap"));
	if (message == "Error getting size of keyframes path")
		return from_wx(_("Error getting size of keyframes path"));
	if (message == "Error getting timecodes from returned map")
		return from_wx(_("Error getting timecodes from returned map"));
	if (message == "Number of returned timecodes does not match number of frames")
		return from_wx(_("Number of returned timecodes does not match number of frames"));
	if (message == "Failed to create argument map")
		return from_wx(_("Failed to create argument map"));
	if (message == "Audio frame too short")
		return from_wx(_("Audio frame too short"));
	if (message == "Audio format is not constant")
		return from_wx(_("Audio format is not constant"));
	if (message == "Failed to read audio channel")
		return from_wx(_("Failed to read audio channel"));
	if (message == "Frame not in RGB24 format")
		return from_wx(_("Frame not in RGB24 format"));
	if (message == "Failed to get VapourSynth ScriptAPI. Make sure VapourSynth is installed correctly.")
		return from_wx(_("Failed to get VapourSynth ScriptAPI. Make sure VapourSynth is installed correctly."));
	if (message == "Failed to get VapourSynth API")
		return from_wx(_("Failed to get VapourSynth API"));

	auto translated = TranslatePrefixedMessage(message, "Error executing VapourSynth script: ", from_wx(_("Error executing VapourSynth script: ")));
	if (!translated.empty())
		return translated;

	translated = TranslatePrefixedMessage(message, "Failed to get first frame: ", from_wx(_("Failed to get first frame: ")));
	if (!translated.empty())
		return translated;

	translated = TranslatePrefixedMessage(message, "Error getting frame: ", from_wx(_("Error getting frame: ")));
	if (!translated.empty())
		return translated;

	translated = TranslatePrefixedMessage(message, "Failed to open timecodes file specified by script: ", from_wx(_("Failed to open timecodes file specified by script: ")));
	if (!translated.empty())
		return translated;

	constexpr char load_prefix[] = "Could not load ";
	constexpr char load_suffix[] = ". Make sure VapourSynth is installed correctly.";
	if (message.size() > sizeof(load_prefix) + sizeof(load_suffix) - 2
		&& message.compare(0, sizeof(load_prefix) - 1, load_prefix) == 0
		&& message.compare(message.size() - (sizeof(load_suffix) - 1), sizeof(load_suffix) - 1, load_suffix) == 0) {
		auto name = message.substr(sizeof(load_prefix) - 1, message.size() - (sizeof(load_prefix) - 1) - (sizeof(load_suffix) - 1));
		auto translated_message = from_wx(_("Could not load %s. Make sure VapourSynth is installed correctly."));
		boost::replace_first(translated_message, "%s", name);
		return translated_message;
	}

	constexpr char api_addr_prefix[] = "Failed to get address of getVSScriptAPI from ";
	if (message.compare(0, sizeof(api_addr_prefix) - 1, api_addr_prefix) == 0) {
		auto name = message.substr(sizeof(api_addr_prefix) - 1);
		auto translated_message = from_wx(_("Failed to get address of getVSScriptAPI from %s"));
		boost::replace_first(translated_message, "%s", name);
		return translated_message;
	}

	return message;
}

void SetStringVar(const VSAPI *api, VSMap *map, std::string variable, std::string value) {
	if (api->mapSetData(map, variable.c_str(), value.c_str(), -1, dtUtf8, 1))
		throw VapourSynthError("Failed to set VSMap entry");
}

int OpenScriptOrVideo(const VSAPI *api, const VSSCRIPTAPI *sapi, VSScript *script, agi::fs::path const& filename, std::string default_script) {
	int result;
	if (agi::fs::HasExtension(filename, "py") || agi::fs::HasExtension(filename, "vpy")) {
		result = sapi->evaluateFile(script, filename.string().c_str());
	} else {
		agi::scoped_holder<VSMap *> map(api->createMap(), api->freeMap);
		if (map == nullptr)
			throw VapourSynthError("Failed to create VSMap for script info");

		SetStringVar(api, map, "filename", filename.string());
		// 设置黑边
		SetStringVar(api, map, "padding", std::to_string(OPT_GET("Provider/Video/VapourSynth/ABB")->GetInt()));
		const auto vscache = config::path->Decode("?local/vscache");
		agi::fs::CreateDirectory(vscache);
		SetStringVar(api, map, "__aegi_vscache", vscache.string());
		SetStringVar(api, map, "__aegi_locale", OPT_GET("App/Language")->GetString());
#ifdef WIN32
		SetStringVar(api, map, "__aegi_vsplugins", config::path->Decode("?data/vapoursynth").string());
#else
		SetStringVar(api, map, "__aegi_vsplugins", "");
#endif
		for (std::string dir : { "data", "dictionary", "local", "script", "temp", "user", })
			// Don't include ?audio and ?video in here since these only hold the paths to the previous audio/video files.
			SetStringVar(api, map, "__aegi_" + dir, config::path->Decode("?" + dir).string());

		if (sapi->setVariables(script, map))
			throw VapourSynthError("Failed to set script info variables");

		std::string vscript;
		vscript += "import sys\n";
		vscript += "sys.path.append(f'{__aegi_user}/automation/vapoursynth')\n";
		vscript += "sys.path.append(f'{__aegi_data}/automation/vapoursynth')\n";
		vscript += default_script;
		result = sapi->evaluateBuffer(script, vscript.c_str(), "aegisub");
	}
	return result;
}

void VSLogToProgressSink(int msgType, const char *msg, void *userData) {
	auto sink = reinterpret_cast<agi::ProgressSink *>(userData);

	std::string msgStr(msg);
	size_t commaPos = msgStr.find(',');
	if (commaPos != std::string::npos) {
		std::string command = msgStr.substr(0, commaPos);
		std::string tail = msgStr.substr(commaPos + 1);

		// We don't allow setting the title since that should stay as "Executing VapourSynth Script".
		if (command == "__aegi_set_message") {
			sink->SetMessage(TranslateProgressMessage(tail));
		} else if (command == "__aegi_set_progress") {
			double percent;
			if (!agi::util::try_parse(tail, &percent)) {
				msgType = 2;
				msgStr = agi::format("Warning: Invalid argument to __aegi_set_progress: %s\n", tail);
			} else {
				sink->SetProgress(percent, 100);
			}
		} else if (command == "__aegi_set_indeterminate") {
			sink->SetIndeterminate();
		}
	}

	int loglevel = 0;
	std::string loglevel_str = OPT_GET("Provider/Video/VapourSynth/Log Level")->GetString();
	if (loglevel_str == "Quiet")
		loglevel = 5;
	else if (loglevel_str == "Fatal")
		loglevel = 4;
	else if (loglevel_str == "Critical")
		loglevel = 3;
	else if (loglevel_str == "Warning")
		loglevel = 2;
	else if (loglevel_str == "Information")
		loglevel = 1;
	else if (loglevel_str == "Debug")
		loglevel = 0;

	if (msgType < loglevel)
		return;

	sink->Log(msgStr);
}

void VSCleanCache() {
	CleanCache(config::path->Decode("?local/vscache/"),
		"",
		OPT_GET("Provider/VapourSynth/Cache/Size")->GetInt(),
		OPT_GET("Provider/VapourSynth/Cache/Files")->GetInt());
}

#endif // WITH_VAPOURSYNTH
