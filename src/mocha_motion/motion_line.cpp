// Copyright (c) 2024-2026, Aegisub contributors
// 字幕行处理实现
// 对应 MoonScript 版 a-mo.Line

#include "motion_line.h"
#include "motion_tags.h"
#include "motion_math.h"

#include <regex>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace mocha {

void MotionLine::calculate_default_position(int style_align, int style_margin_l, int style_margin_r,
	int style_margin_t, int res_x, int res_y)
{
	int vert_margin = (margin_t == 0) ? style_margin_t : margin_t;
	int left_margin = (margin_l == 0) ? style_margin_l : margin_l;
	int right_margin = (margin_r == 0) ? style_margin_r : margin_r;
	int alignment = (this->align != 0) ? this->align : style_align;

	// X 坐标（对应 MoonScript 的 defaultXPosition）
	switch (alignment % 3) {
		case 0: // 3, 6, 9 - 右对齐
			x_position = res_x - right_margin;
			break;
		case 1: // 1, 4, 7 - 左对齐
			x_position = left_margin;
			break;
		case 2: // 2, 5, 8 - 居中
			x_position = res_x * 0.5;
			break;
	}

	// Y 坐标（对应 MoonScript 的 defaultYPosition）
	int row = static_cast<int>(std::ceil(static_cast<double>(alignment) / 3.0));
	switch (row) {
		case 1: // 1, 2, 3 - 底部
			y_position = res_y - vert_margin;
			break;
		case 2: // 4, 5, 6 - 中间
			y_position = res_y * 0.5;
			break;
		case 3: // 7, 8, 9 - 顶部
			y_position = vert_margin;
			break;
	}
}

bool MotionLine::extract_metrics(int style_align, int style_margin_l, int style_margin_r,
	int style_margin_t, int res_x, int res_y)
{
	// 提取 \an 对齐标签
	std::regex align_re(R"(\\an([1-9]))");
	std::smatch match;
	std::string search_text = text;

	if (std::regex_search(search_text, match, align_re)) {
		if (align == 0) {
			align = std::stoi(match[1].str());
		}
	}

	if (align == 0) {
		align = style_align;
	}

	// 提取 \pos 标签
	std::regex pos_re(R"(\\pos\(([.\d\-]+),([.\d\-]+)\))");
	bool has_pos = false;
	if (std::regex_search(search_text, match, pos_re)) {
		if (!has_pos && !move.has_value()) {
			x_position = std::stod(match[1].str());
			y_position = std::stod(match[2].str());
			has_pos = true;
		}
	}

	// 提取 \move 标签
	std::regex move_re(R"(\\move\(([.\d\-]+),([.\d\-]+),([.\d\-]+),([.\d\-]+),([.\d\-]+),([.\d\-]+)\))");
	if (!has_pos && std::regex_search(search_text, match, move_re)) {
		move = MoveData{
			std::stod(match[1].str()), std::stod(match[2].str()),
			std::stod(match[3].str()), std::stod(match[4].str()),
			std::stoi(match[5].str()), std::stoi(match[6].str())
		};
	}

	if (!has_pos && !move.has_value()) {
		calculate_default_position(style_align, style_margin_l, style_margin_r,
			style_margin_t, res_x, res_y);
		return false;
	}

	return true;
}

void MotionLine::tokenize_transforms() {
	if (transforms_tokenized) return;

	transforms.clear();
	text = transform_utils::tokenize_transforms(text, transforms, duration);
	transforms_tokenized = true;
}

void MotionLine::detokenize_transforms(int shift, int line_dur) {
	if (!transforms_tokenized) return;
	text = transform_utils::detokenize_transforms(text, transforms, shift, line_dur);
	transforms_tokenized = false;
}

std::string MotionLine::detokenize_transforms_copy(int shift, int line_dur) const {
	if (!transforms_tokenized) return text;
	return transform_utils::detokenize_transforms_copy(text, transforms, shift, line_dur);
}

void MotionLine::dont_touch_transforms() {
	if (!transforms_tokenized) return;

	// 使用原始字符串还原（不修改时间）
	std::string result = text;
	for (const auto& t : transforms) {
		auto pos = result.find(t.token);
		if (pos != std::string::npos) {
			result.replace(pos, t.token.length(), "\\t" + t.raw_string);
		}
	}
	text = result;
	transforms_tokenized = false;
}

std::string MotionLine::interpolate_transforms_copy(int shift, int start, int res_x, int res_y) const {
	if (!transforms_tokenized) return text;
	auto prior_tags = collect_prior_inline_tags();
	return transform_utils::interpolate_transforms_copy(text, transforms, shift, start - start_time, properties, prior_tags, res_x, res_y);
}

std::map<std::string, Transform::EffectTagValue> MotionLine::collect_prior_inline_tags() const {
	// 对应 MoonScript Transform.moon: collectPriorState
	// 遍历行文本中的所有 override 块（{...}），收集可变换标签的当前值
	// 这些值用作 \t 插值的起始状态，优先于样式默认值
	//
	// 先以样式默认值为基础（包含颜色等非数值类型），
	// 再用内联标签值覆盖，确保颜色标签的回退与 MoonScript 一致
	//
	// affectedBy 机制：如 \1a 受 \alpha 影响，在同一块内
	// 如果 \alpha 出现在 \1a 之后，则 \alpha 的值覆盖 \1a
	// 对应 MoonScript 中 gsub ".-"..tag.pattern 的位置感知逻辑
	std::map<std::string, Transform::EffectTagValue> result = style_tag_defaults;
	const auto& registry = TagRegistry::instance();

	// 将捕获的字符串转换为 EffectTagValue 的辅助函数
	auto convert_capture = [](const TagDef* tag_def, const std::string& capture) -> Transform::EffectTagValue {
		Transform::EffectTagValue etv;
		switch (tag_def->type) {
		case TagType::COLOR: {
			etv.type = Transform::EffectTagValue::COL;
			std::string hex = capture;
			while (hex.size() < 6) hex = "0" + hex;
			try {
				etv.color.b = std::stoi(hex.substr(0, 2), nullptr, 16);
				etv.color.g = std::stoi(hex.substr(2, 2), nullptr, 16);
				etv.color.r = std::stoi(hex.substr(4, 2), nullptr, 16);
			} catch (...) {
				etv.color = {0, 0, 0};
			}
			break;
		}
		case TagType::ALPHA: {
			etv.type = Transform::EffectTagValue::ALP;
			try {
				etv.alpha = std::stoi(capture, nullptr, 16);
			} catch (...) {
				etv.alpha = 0;
			}
			break;
		}
		case TagType::MULTI: {
			etv.type = Transform::EffectTagValue::MULTI;
			std::regex coord_re(R"([.\d\-]+)");
			auto cit = std::sregex_iterator(capture.begin(), capture.end(), coord_re);
			auto cend = std::sregex_iterator();
			while (cit != cend) {
				try {
					etv.multi_values.push_back(std::stod((*cit)[0].str()));
				} catch (...) {
					etv.multi_values.push_back(0);
				}
				++cit;
			}
			break;
		}
		default: {
			etv.type = Transform::EffectTagValue::NUM;
			try {
				etv.number = std::stod(capture);
			} catch (...) {
				etv.number = 0;
			}
			break;
		}
		}
		return etv;
	};

	// 提取所有 override 块内容（不含变换占位符中的内容）
	std::regex block_re(R"(\{([^}]*)\})");
	auto it = std::sregex_iterator(text.begin(), text.end(), block_re);
	auto end = std::sregex_iterator();

	for (; it != end; ++it) {
		std::string block = (*it)[1].str();

		// 记录每个标签在当前块内的最后出现位置和捕获值
		struct TagMatch {
			int position;             // 块内最后匹配的末尾位置
			std::string capture;      // 最后匹配的捕获值
			const TagDef* def;        // 标签定义
		};
		std::map<std::string, TagMatch> block_matches;

		// 对每个可变换标签进行匹配，记录位置
		for (const auto* tag_def : registry.transform_tags()) {
			std::regex tag_re(tag_def->pattern);
			std::smatch match;
			std::string search_str = block;
			int offset = 0;
			int last_pos = -1;
			std::string last_capture;

			while (std::regex_search(search_str, match, tag_re)) {
				last_pos = offset + static_cast<int>(match.position()) + static_cast<int>(match.length());
				last_capture = match[1].str();
				offset += static_cast<int>(match.position()) + static_cast<int>(match.length());
				search_str = match.suffix().str();
			}

			if (last_pos >= 0 && !last_capture.empty()) {
				block_matches[tag_def->name] = {last_pos, last_capture, tag_def};
			}
		}

		// 应用 affectedBy 逻辑：
		// 对应 MoonScript: 对有 affectedBy 的标签，如果 affectedBy 标签
		// 在块内出现在该标签之后，则 affectedBy 标签的值覆盖该标签
		for (auto& [name, tm] : block_matches) {
			// 先用自身值更新 result
			result[name] = convert_capture(tm.def, tm.capture);
		}

		// 检查 affectedBy 覆盖关系
		for (auto& [name, tm] : block_matches) {
			if (!tm.def->affected_by.empty()) {
				for (const auto& parent_name : tm.def->affected_by) {
					auto parent_it = block_matches.find(parent_name);
					if (parent_it != block_matches.end()) {
						// affectedBy 标签在块内也有匹配
						if (parent_it->second.position > tm.position) {
							// affectedBy 标签出现在本标签之后 → 覆盖
							// 对应 MoonScript: newTagBlock\gsub 从 primary 之后搜索 parent
							const TagDef* parent_def = parent_it->second.def;
							result[name] = convert_capture(parent_def, parent_it->second.capture);
						}
					}
				}
			}
		}
	}

	return result;
}

/// @brief 去除行文本中的重复标签
///
/// 对应 MoonScript deduplicateTags()。
/// 分三类处理：
///
///   1. 全局标签（one_time_tags）：如 \an, \pos, \move, \org 等
///      整行内只能出现一次。保留第一个，移除后续出现的。
///
///   2. 冲突标签对：如 \move 与 \pos、\fade 与 \fad
///      同一行中两者只能取一。（当前通过全局标签机制隐式处理）
///
///   3. 可重复标签（repeat_tags）：如 \fscx, \fscy, \bord, \c, \1a 等
///      同一标签块内可能重复。调用 deduplicate_tag() 保留最后一个。
///      注意：此操作在 \t 标签已被 tokenize 之后执行，
///      确保 \t 内部的同名标签不会被当作重复（Issue #69）。
///
/// 最后清理空的标签块 {} 和空的 \clip()
void MotionLine::deduplicate_tags() {
	// 临时合并相邻标签块：使用 \x06 作为分隔符保留块边界位置
	// 对应 MoonScript: @splitChar = "\\\6"
	//   text = text\gsub "}{", @splitChar
	//   ... 处理 ...
	//   text = text\gsub @splitChar, "}{"
	static const std::string split_char = "\x06";
	text = std::regex_replace(text, std::regex(R"(\}\{)"), split_char);

	const auto& registry = TagRegistry::instance();

	// 处理全局标签（只保留第一个出现的）
	std::map<std::string, bool> seen_global;
	text = tag_utils::run_callback_on_overrides(text, [&](const std::string& tag_block, int major) {
		std::string result = tag_block;
		for (const auto* tag_def : registry.one_time_tags()) {
			std::regex re(tag_def->pattern);
			std::smatch match;
			std::string temp = result;
			if (std::regex_search(temp, match, re)) {
				if (seen_global.count(tag_def->name)) {
					// 已出现过，移除此实例
					result = std::regex_replace(result, re, "");
				} else {
					seen_global[tag_def->name] = true;
				}
			}
		}
		return result;
	});

	// 处理冲突标签对
	// 对应 MoonScript: { "move", "pos" }, { "fade", "fad" },
	//                  { "rectClip", "rectiClip" }, { "vectClip", "vectiClip" }
	// 当两者共存时，保留先出现的，移除后出现的
	static const std::vector<std::pair<std::string, std::string>> conflicting_pairs = {
		{"move", "pos"},
		{"fade", "fad"},
		{"rectClip", "rectiClip"},
		{"vectClip", "vectiClip"},
	};
	for (const auto& [tag_a, tag_b] : conflicting_pairs) {
		const TagDef* def_a = registry.get(tag_a);
		const TagDef* def_b = registry.get(tag_b);
		if (!def_a || !def_b) continue;

		// 查找各自在行文本中第一次出现的位置
		std::smatch match_a, match_b;
		std::regex re_a(def_a->pattern);
		std::regex re_b(def_b->pattern);
		bool found_a = std::regex_search(text, match_a, re_a);
		bool found_b = std::regex_search(text, match_b, re_b);

		if (found_a && found_b) {
			// 两者都存在，移除后出现的那个
			if (match_a.position() < match_b.position()) {
				text = std::regex_replace(text, re_b, "");
			} else {
				text = std::regex_replace(text, re_a, "");
			}
		}
	}

	// 处理可重复标签（保留最后一个）
	text = tag_utils::run_callback_on_overrides(text, [&](const std::string& tag_block, int major) {
		std::string result = tag_block;
		for (const auto* tag_def : registry.repeat_tags()) {
			result = tag_utils::deduplicate_tag(result, tag_def->pattern);
		}
		return result;
	});

	// 还原块边界分隔符
	// 对应 MoonScript: text = text\gsub @splitChar, "}{"
	{
		std::string::size_type pos = 0;
		while ((pos = text.find(split_char, pos)) != std::string::npos) {
			text.replace(pos, split_char.size(), "}{");
			pos += 2;
		}
	}

	// 清理空标签块和无用内容
	text = std::regex_replace(text, std::regex(R"(\{\})"), "");
	text = std::regex_replace(text, std::regex(R"(\\clip\(\))"), "");
}

void MotionLine::get_properties_from_style(const std::map<std::string, double>& style_props) {
	properties = style_props;
}

void MotionLine::ensure_leading_override_exists() {
	if (text.empty() || text[0] != '{') {
		text = "{}" + text;
	}
}

void MotionLine::run_callback_on_overrides(std::function<std::string(const std::string&, int)> callback) {
	text = tag_utils::run_callback_on_overrides(text, callback);
}

void MotionLine::run_callback_on_first_override(std::function<std::string(const std::string&)> callback) {
	text = tag_utils::run_callback_on_first_override(text, callback);
}

void MotionLine::convert_fad_to_fade() {
	// 将 \fad(in,out) 转换为 \fade(255,0,255,0,in,duration-out,duration)
	// 对应 MoonScript: \\fade?%((%d+),(%d+)%) -> \\fade(255,0,255,0,%d,%d,%d)
	std::regex fad_re(R"(\\fade?\((\d+),(\d+)\))");
	std::smatch match;
	std::string search_text = text;

	if (std::regex_search(search_text, match, fad_re)) {
		int fade_in = std::stoi(match[1].str());
		int fade_out = std::stoi(match[2].str());

		char buf[128];
		std::snprintf(buf, sizeof(buf), "\\fade(255,0,255,0,%d,%d,%d)",
			fade_in, duration - fade_out, duration);

		text = std::regex_replace(text, fad_re, buf);
	}
}

void MotionLine::shift_karaoke() {
	if (karaoke_shift == 0) return;

	double shift = karaoke_shift;
	std::regex k_re(R"((\\[kK][fo]?)(\d+))");

	text = tag_utils::run_callback_on_overrides(text, [&](const std::string& tag_block, int) {
		std::string result;
		std::sregex_iterator it(tag_block.begin(), tag_block.end(), k_re);
		std::sregex_iterator end;
		size_t last = 0;

		while (it != end) {
			const auto& m = *it;
			result += tag_block.substr(last, m.position() - last);

			std::string k_tag = m[1].str();
			double time = std::stod(m[2].str());

			if (shift > 0) {
				double old_shift = -shift;
				double new_time = time - shift;
				shift -= time;
				if (new_time > 0) {
					if (k_tag == "\\kf") {
						char buf[64];
						// 卡拉OK时间值为整数厘秒，使用 %d 格式
						// 对应 MoonScript: formatInt
						std::snprintf(buf, sizeof(buf), "%s%d%s%d", k_tag.c_str(), static_cast<int>(old_shift), k_tag.c_str(), static_cast<int>(time));
						result += buf;
					} else {
						char buf[64];
						std::snprintf(buf, sizeof(buf), "%s%d", k_tag.c_str(), static_cast<int>(new_time));
						result += buf;
					}
				}
				// 如果 new_time <= 0，移除该标签
			} else {
				result += m.str();
			}

			last = m.position() + m.length();
			++it;
		}
		result += tag_block.substr(last);
		return result;
	});
}

} // namespace mocha
