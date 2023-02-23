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

#include "vapoursynth_wrap.h"
#include "options.h"
#include "utils.h"
#include <libaegisub/fs.h>
#include <libaegisub/path.h>

#include <boost/algorithm/string/replace.hpp>

void SetStringVar(const VSAPI *api, VSMap *map, std::string variable, std::string value) {
	if (api->mapSetData(map, variable.c_str(), value.c_str(), -1, dtUtf8, 1))
		throw VapoursynthError("Failed to set VSMap entry");
}

int OpenScriptOrVideo(const VSAPI *api, const VSSCRIPTAPI *sapi, VSScript *script, agi::fs::path const& filename, std::string default_script) {
	int result;
	if (agi::fs::HasExtension(filename, "py") || agi::fs::HasExtension(filename, "vpy")) {
		result = sapi->evaluateFile(script, filename.string().c_str());
	} else {
		VSMap *map = api->createMap();
		if (map == nullptr)
			throw VapoursynthError("Failed to create VSMap for script info");

		SetStringVar(api, map, "filename", filename.string());
		SetStringVar(api, map, "__aegi_vscache", config::path->Decode("?local/vscache").string());
		for (std::string dir : { "data", "dictionary", "local", "script", "temp", "user", })
			// Don't include ?audio and ?video in here since these only hold the paths to the previous audio/video files.
			SetStringVar(api, map, "__aegi_" + dir, config::path->Decode("?" + dir).string());

		if (sapi->setVariables(script, map))
			throw VapoursynthError("Failed to set script info variables");

		api->freeMap(map);

		std::string vscript;
		vscript += "import sys\n";
		vscript += "sys.path.append(f'{__aegi_data}/automation/vapoursynth')\n";
		vscript += "sys.path.append(f'{__aegi_user}/automation/vapoursynth')\n";
		vscript += default_script;
		result = sapi->evaluateBuffer(script, vscript.c_str(), "aegisub");
	}
	return result;
}

void VSCleanCache() {
	CleanCache(config::path->Decode("?local/vscache/"),
		"",
		OPT_GET("Provider/VapourSynth/Cache/Size")->GetInt(),
		OPT_GET("Provider/VapourSynth/Cache/Files")->GetInt());
}

#endif // WITH_VAPOURSYNTH
