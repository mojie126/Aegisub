// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "ass_entry.h"

#include <boost/flyweight.hpp>
#include <libaegisub/fs.h>

/// @class AssAttachment
class AssAttachment final : public AssEntry {
	/// ASS uuencoded entry data, including header.
	boost::flyweight<std::string> entry_data;

	/// 中间缓冲区，用于高效逐行追加数据，避免 flyweight 的 O(n²) 复制
	std::string data_buffer;

	/// Name of the attached file, with SSA font mangling if it is a ttf
	boost::flyweight<std::string> filename;

	AssEntryGroup group;

public:
	/// Get the size of the attached file in bytes
	size_t GetSize() const;

	/// Add a line of data (without newline) read from a subtitle file
	void AddData(std::string const& data) {
		if (data_buffer.empty())
			data_buffer = entry_data.get();
		data_buffer += data;
		data_buffer += "\r\n";
	}

	/// @brief 将中间缓冲区数据写入 flyweight，在附件完成构建后调用
	void Finalize() {
		if (!data_buffer.empty()) {
			entry_data = std::move(data_buffer);
			data_buffer.clear();
			data_buffer.shrink_to_fit();
		}
	}

	/// Extract the contents of this attachment to a file
	/// @param filename Path to save the attachment to
	void Extract(agi::fs::path const& filename) const;

	/// Get the name of the attached file
	/// @param raw If false, remove the SSA filename mangling
	std::string GetFileName(bool raw=false) const;

	std::string const& GetEntryData() const { return entry_data; }
	AssEntryGroup Group() const override;

	AssAttachment(std::string const& header, AssEntryGroup group);
	AssAttachment(agi::fs::path const& name, AssEntryGroup group);
};
