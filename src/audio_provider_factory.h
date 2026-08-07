// Copyright (c) 2014, Thomas Goyne <plorkyeran@aegisub.org>
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

#include <libaegisub/fs.h>
#include <memory>
#include <vector>

namespace agi {
	class AudioProvider;
	class BackgroundRunner;
	class Path;
}

/// @brief 创建音频提供者
/// @param interactive false 时（如 MCP 调用）首选 provider 失败不弹选择对话框，直接抛出异常
std::unique_ptr<agi::AudioProvider> GetAudioProvider(agi::fs::path const& filename,
                                                     agi::Path const& path_helper,
                                                     agi::BackgroundRunner *br,
                                                     bool interactive = true);
std::vector<std::string> GetAudioProviderNames();
