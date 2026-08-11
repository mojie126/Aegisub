/// @file font_tag_state.cpp
/// @brief LineStyleState::ApplyOverrideBlock 的实现
/// @details 独立成源文件以便单测直接链接，
///          \r 重置后 \b/\i/\fn/\fn0 以重置样式为回退基准 (上游 4d61325b0)

#include "font_file_lister.h"

#include "ass_override.h"

#include <string>
#include <vector>

void LineStyleState::ApplyOverrideBlock(LineStyleState& reset, bool& overriden,
	std::vector<AssOverrideTag> const& tags,
	std::function<LineStyleState(std::string const&)> const& resolve_style,
	std::string const& default_style) {
	for (auto const& tag : tags) {
		if (tag.Name == "\\r") {
			*this = resolve_style(tag.Params[0].Get(default_style));
			reset = *this;
			overriden = false;
		}
		else if (tag.Name == "\\b") {
			bold = tag.Params[0].Get(reset.bold);
			overriden = true;
		}
		else if (tag.Name == "\\i") {
			italic = tag.Params[0].Get(reset.italic);
			overriden = true;
		}
		else if (tag.Name == "\\fn") {
			facename = tag.Params[0].Get(reset.facename);
			// \fn0 在 libass/VSFilter 中等同于 \fn，重置为重置后样式的默认字体
			if (facename == "0")
				facename = reset.facename;
			overriden = true;
		}
	}
}
