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

/// @file ffmpegsource_common.cpp
/// @brief Shared code for ffms video and audio providers
/// @ingroup video_input audio_input ffms
///

#ifdef WITH_BESTSOURCE
#include "bestsource_common.h"

#include "options.h"
#include "utils.h"

#include <libaegisub/fs.h>
#include <libaegisub/path.h>

#include <boost/crc.hpp>
#include <boost/filesystem/path.hpp>


std::string GetBSCacheFile(agi::fs::path const& filename) {
	// BS can store all its index data in a single file, but we make a separate index file
	// for each video file to ensure that the old index is invalidated if the file is modified.
	// While BS does check the filesize of the files, it doesn't check the modification time.
	uintmax_t len = agi::fs::Size(filename);
	boost::crc_32_type hash;
	hash.process_bytes(filename.string().c_str(), filename.string().size());

	auto result = config::path->Decode("?local/bsindex/" + filename.filename().string() + "_" + std::to_string(hash.checksum()) + "_" + std::to_string(len) + "_" + std::to_string(agi::fs::ModifiedTime(filename)) + ".json");
	agi::fs::CreateDirectory(result.parent_path());

	return result.string();
}

void BSCleanCache() {
	CleanCache(config::path->Decode("?local/bsindex/"),
		"*.json",
		OPT_GET("Provider/BestSource/Cache/Size")->GetInt(),
		OPT_GET("Provider/BestSource/Cache/Files")->GetInt());
}

#endif // WITH_BESTSOURCE
