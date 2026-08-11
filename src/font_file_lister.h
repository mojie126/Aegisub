// Copyright (c) 2012, Thomas Goyne <plorkyeran@aegisub.org>
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

#include <libaegisub/scoped_ptr.h>
#include <libaegisub/fs.h>

#include <functional>
#include <map>
#include <string>
#include <tuple>
#include <vector>
#include <unordered_map>

#include <wx/string.h>

class AssDialogue;
class AssFile;
class AssOverrideTag;

/// @brief 行内 override 标签作用后的字体样式状态
/// @details 与 FontCollector 共用，\r 重置后 \b/\i/\fn/\fn0 以重置样式为回退基准 (上游 4d61325b0)
struct LineStyleState {
	/// 字体名
	std::string facename;
	/// ASS 字重
	int bold;
	/// 是否斜体
	bool italic;

	/// 按样式状态聚合使用信息的字典序比较
	bool operator<(LineStyleState const& rgt) const {
		return std::tie(facename, bold, italic) < std::tie(rgt.facename, rgt.bold, rgt.italic);
	}

	/// @brief 应用一个 override 块中的标签，更新当前样式与重置基准
	/// @param reset 最近一次 \r 重置后的样式基准，遇到 \r 时更新
	/// @param overriden 输出，本块是否设置了任意 override 标签
	/// @param tags 块内的标签列表
	/// @param resolve_style 按样式名解析样式定义的回调，\r 参数无效时以默认样式名调用
	/// @param default_style 行默认样式名
	void ApplyOverrideBlock(LineStyleState& reset, bool& overriden,
		std::vector<AssOverrideTag> const& tags,
		std::function<LineStyleState(std::string const&)> const& resolve_style,
		std::string const& default_style);
};

typedef std::function<void (wxString, int)> FontCollectorStatusCallback;

struct CollectionResult {
	/// Characters which could not be found in any font files
	wxString missing;
	/// Paths to the file(s) containing the requested font
	std::vector<agi::fs::path> paths;
	bool fake_bold = false;
	bool fake_italic = false;
	/// 字体通过 AddFontMemResourceEx 找到（脚本内嵌附件），无本地文件路径
	bool embedded = false;
};

#ifdef _WIN32
#include <dwrite.h>
class GdiFontFileLister {
	agi::scoped_holder<HDC> dc_sh;
	agi::scoped_holder<IDWriteFactory*> dwrite_factory_sh;
	agi::scoped_holder<IDWriteFontCollection*> font_collection_sh;
	agi::scoped_holder<IDWriteGdiInterop*> gdi_interop_sh;

public:
	/// Constructor
	/// @throws agi::EnvironmentError if an error occurs during construction.
	GdiFontFileLister(FontCollectorStatusCallback &);

	/// @brief Get the path to the font with the given styles
	/// @param facename Name of font face
	/// @param bold ASS font weight
	/// @param italic Italic?
	/// @param characters Characters in this style
	/// @return Path to the matching font file(s), or empty if not found
	CollectionResult GetFontPaths(std::string const& facename, int bold, bool italic, std::vector<int> const& characters);
};

using FontFileLister = GdiFontFileLister;

#elif defined(__APPLE__)

struct CoreTextFontFileLister {
	CoreTextFontFileLister(FontCollectorStatusCallback &) {}

	/// @brief Get the path to the font with the given styles
	/// @param facename Name of font face
	/// @param bold ASS font weight
	/// @param italic Italic?
	/// @param characters Characters in this style
	/// @return Path to the matching font file(s), or empty if not found
	CollectionResult GetFontPaths(std::string const& facename, int bold, bool italic, std::vector<int> const& characters);
};

using FontFileLister = CoreTextFontFileLister;

#else

typedef struct _FcConfig FcConfig;
typedef struct _FcFontSet FcFontSet;

/// @class FontConfigFontFileLister
/// @brief fontconfig powered font lister
class FontConfigFontFileLister {
	agi::scoped_holder<FcConfig*> config;

	/// @brief Case-insensitive match ASS/SSA font family against full name. (also known as "name for humans")
	/// @param family font fullname
	/// @param bold weight attribute
	/// @param italic italic attribute
	/// @return font set
	FcFontSet *MatchFullname(const char *family, int weight, int slant);
public:
	/// Constructor
	/// @param cb Callback for status logging
	FontConfigFontFileLister(FontCollectorStatusCallback &cb);

	/// @brief Get the path to the font with the given styles
	/// @param facename Name of font face
	/// @param bold ASS font weight
	/// @param italic Italic?
	/// @param characters Characters in this style
	/// @return Path to the matching font file(s), or empty if not found
	CollectionResult GetFontPaths(std::string const& facename, int bold, bool italic, std::vector<int> const& characters);
};

using FontFileLister = FontConfigFontFileLister;
#endif

/// @class FontCollector
/// @brief Class which collects the paths to all fonts used in a script
class FontCollector {
	/// All data needed to find the font file used to render text
	using StyleInfo = LineStyleState;

	/// Data about where each style is used
	struct UsageData {
		std::vector<int> chars;          ///< Characters used in this style which glyphs will be needed for
		bool drawing = false;            ///< Whether this style is used for a drawing
		std::vector<int> lines;          ///< Lines on which this style is used via overrides
		std::vector<std::string> styles; ///< ASS styles which use this style
	};

	/// Message callback provider by caller
	FontCollectorStatusCallback status_callback;

	FontFileLister lister;

	/// The set of all glyphs used in the file
	std::map<StyleInfo, UsageData> used_styles;
	/// Style name -> ASS style definition
	std::map<std::string, StyleInfo> styles;
	/// Paths to found required font files
	std::vector<agi::fs::path> results;
	/// Number of fonts which could not be found
	int missing = 0;
	/// Number of fonts which were found, but did not contain all used glyphs
	int missing_glyphs = 0;

	/// Gather all of the unique styles with text on a line
	void ProcessDialogueLine(const AssDialogue *line, int index);

	/// Get the font for a single style
	void ProcessChunk(std::pair<StyleInfo, UsageData> const& style);

	/// Print the lines and styles on which a missing font is used
	void PrintUsage(UsageData const& data);

public:
	/// Constructor
	/// @param status_callback Function to pass status updates to
	/// @param lister The actual font file lister
	FontCollector(FontCollectorStatusCallback status_callback);

	/// @brief Get a list of the locations of all font files used in the file
	/// @param file Lines in the subtitle file to check
	/// @param status Callback function for messages
	/// @return List of paths to fonts
	std::vector<agi::fs::path> GetFontPaths(const AssFile *file);
};
