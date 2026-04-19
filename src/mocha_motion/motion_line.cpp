// Copyright (c) 2024-2026, Aegisub contributors
// 字幕行处理实现
// 对应 MoonScript 版 a-mo.Line

#include "motion_line.h"
#include "motion_tags.h"

#include <regex>
#include <algorithm>
#include <cctype>
#include <cmath>

namespace {
	/**
	 * @brief PendingKkTag 表示一个未完成的 VSFilterMod `\kk(...)` 打字机标签的运行时状态
	 *
	 * 该结构用于在逐帧/时间偏移处理时保存 `\kk` 标签内部每个字符对应的出现时间序列、
	 * 当前处理位置（next_index）以及尚未显示的字符数量（remaining_chars）。
	 *
	 * 设计约束：
	 * - durations 存储每个字符的出现时间（默认 100cs），可按需要被 reduce_current_duration 修改。
	 * - consume_char 在字符被输出后前移索引并减少 remaining_chars。
	 * - format_tag 在需要将剩余未显示字符重新编码回 `\kk(...)` 标签时生成合适的标签文本（用于逐帧输出重建）。
	 */
	struct PendingKkTag {
		std::vector<int> durations;
		size_t next_index = 0;
		int remaining_chars = 0;

		[[nodiscard]] bool active() const {
			return remaining_chars > 0 && next_index < durations.size();
		}

		void reset() {
			durations.clear();
			next_index = 0;
			remaining_chars = 0;
		}

		void consume_char() {
			if (!active()) return;
			++next_index;
			--remaining_chars;
			if (!active()) reset();
		}

		[[nodiscard]] double current_duration() const {
			return active() ? durations[next_index] : 0.0;
		}

		void reduce_current_duration(const double shift) {
			if (!active()) return;
			durations[next_index] = std::max(0, static_cast<int>(durations[next_index] - shift));
		}

		[[nodiscard]] std::string format_tag() const {
			if (!active()) return "";

			std::string result = "{\\kk(" + std::to_string(remaining_chars);
			int last_non_default = -1;
			for (int i = 0; i < remaining_chars; ++i) {
				if (durations[next_index + i] != 100)
					last_non_default = i;
			}

			for (int i = 0; i <= last_non_default; ++i) {
				result += ",";
				const int duration = durations[next_index + i];
				if (duration != 100)
					result += std::to_string(duration);
			}

			result += ")}";
			return result;
		}
	};

	[[nodiscard]] std::string trim_copy(const std::string &value) {
		const auto first = std::find_if_not(
			value.begin(), value.end(),
			[](const unsigned char ch) { return std::isspace(ch) != 0; }
		);
		if (first == value.end()) return "";
		const auto last = std::find_if_not(
			value.rbegin(), value.rend(),
			[](const unsigned char ch) { return std::isspace(ch) != 0; }
		).base();
		return std::string(first, last);
	}

	[[nodiscard]] bool parse_int_value(const std::string &text, int &value) {
		const std::string trimmed = trim_copy(text);
		if (trimmed.empty()) return false;

		size_t parsed = 0;
		try {
			value = std::stoi(trimmed, &parsed);
		} catch (...) {
			return false;
		}
		return parsed == trimmed.size();
	}

	/**
	 * @brief 解析 `\kk(...)` 标签参数为 PendingKkTag
	 * @param params 标签内部参数字符串（不包括括号）
	 * @return 已解析的 PendingKkTag（解析失败返回空/非激活态对象）
	 *
	 * 语义：
	 * - 第一个参数为字符计数（必须为正整数）。
	 * - 随后的参数为每个字符的出现时间（可选），未指定的项使用默认值 100。
	 */
	[[nodiscard]] PendingKkTag parse_kk_tag(const std::string &params) {
		std::vector<std::string> parts;
		size_t start = 0;
		while (true) {
			const size_t comma = params.find(',', start);
			if (comma == std::string::npos) {
				parts.push_back(trim_copy(params.substr(start)));
				break;
			}
			parts.push_back(trim_copy(params.substr(start, comma - start)));
			start = comma + 1;
		}

		PendingKkTag kk;
		if (parts.empty()) return kk;

		int charcount = 0;
		if (!parse_int_value(parts[0], charcount) || charcount <= 0)
			return kk;

		kk.durations.assign(charcount, 100);
		kk.remaining_chars = charcount;
		for (int i = 1; i < static_cast<int>(parts.size()) && i <= charcount; ++i) {
			if (parts[i].empty()) continue;

			int duration = 0;
			if (!parse_int_value(parts[i], duration))
				continue;
			kk.durations[i - 1] = std::max(duration, 0);
		}

		return kk;
	}

	[[nodiscard]] size_t utf8_char_length(const unsigned char lead) {
		if ((lead & 0x80) == 0) return 1;
		if ((lead & 0xE0) == 0xC0) return 2;
		if ((lead & 0xF0) == 0xE0) return 3;
		if ((lead & 0xF8) == 0xF0) return 4;
		return 1;
	}

	struct PlainToken {
		std::string text;
		size_t length = 0;
		bool visible = true;
	};

	[[nodiscard]] PlainToken next_plain_token(const std::string &text, const size_t pos) {
		if (text[pos] == '\\' && pos + 1 < text.size()) {
			const char escaped = text[pos + 1];
			if (escaped == 'N' || escaped == 'n')
				return {text.substr(pos, 2), 2, false};
			if (escaped == 'h')
				return {text.substr(pos, 2), 2, true};
		}

		const size_t len = std::min(utf8_char_length(static_cast<unsigned char>(text[pos])), text.size() - pos);
		return {text.substr(pos, len), len, true};
	}

	[[nodiscard]] std::string shift_standard_karaoke_tag(const std::string &tag, const int time, double &shift) {
		if (shift <= 0)
			return tag + std::to_string(time);

		const double old_shift = -shift;
		const double new_time = time - shift;
		shift -= time;
		if (new_time <= 0)
			return "";

		char buf[64];
		if (tag == "\\kf") {
			std::snprintf(
				buf, sizeof(buf), "%s%d%s%d",
				tag.c_str(), static_cast<int>(old_shift),
				tag.c_str(), time
			);
		} else {
			std::snprintf(buf, sizeof(buf), "%s%d", tag.c_str(), static_cast<int>(new_time));
		}
		return buf;
	}

	[[nodiscard]] std::string shift_karaoke_start_tag(const int time, double &shift) {
		if (shift <= 0)
			return "\\kt" + std::to_string(time);

		const double new_time = time - shift;
		if (new_time > 0) {
			shift = 0;
			return "\\kt" + std::to_string(static_cast<int>(new_time));
		}

		shift = -new_time;
		return "";
	}

	/**
	 * @brief 处理 override 块（{...}）内与卡拉OK相关的标签并应用时间前移
	 * @param block override 块文本（不包含外层花括号）
	 * @param shift 要前移的时间量（毫秒），函数会根据标签更新并消费 shift
	 * @param pending_kk 引用形式传入/返回当前的 PendingKkTag 状态
	 * @return 处理后替换了被前移标签的 override 块文本
	 *
	 * 该函数额外处理的标签包括：\kk(...)（设置 pending_kk），\kt（逐字计时起始），以及标准 \k/\K/\kf/\ko 标签。
	 */
	[[nodiscard]] std::string process_override_block(const std::string &block, double &shift, PendingKkTag &pending_kk) {
		static const std::regex karaoke_re(R"(\\kk\(([^)]*)\)|\\kt(-?\d+)|(\\[kK][fo]?)(\d+))");

		std::string result;
		size_t last = 0;
		for (std::sregex_iterator it(block.begin(), block.end(), karaoke_re), end; it != end; ++it) {
			const auto &match = *it;
			result += block.substr(last, match.position() - last);

			if (match[1].matched) {
				pending_kk = parse_kk_tag(match[1].str());
			} else if (match[2].matched) {
				pending_kk.reset();
				result += shift_karaoke_start_tag(std::stoi(match[2].str()), shift);
			} else {
				pending_kk.reset();
				result += shift_standard_karaoke_tag(match[3].str(), std::stoi(match[4].str()), shift);
			}

			last = match.position() + match.length();
		}
		result += block.substr(last);
		return result;
	}

	/**
	 * @brief 处理纯文本区块（非 override）时的逐字符输出与 \kk 状态同步
	 * @param plain 纯文本片段
	 * @param shift 此次需要前移的时间（毫秒），会被消耗或用于调整 pending_kk
	 * @param pending_kk 当前的 PendingKkTag 状态引用
	 * @return 处理后的文本（可能包含重建的 \kk(...) 标签以保留未显示字符的剩余计时）
	 *
	 * 处理细节：
	 * - 使用 next_plain_token 识别普通字符与转义序列（如 \h, \N 等）。
	 * - 普通字符按 UTF-8 码点前进；这覆盖常见 BMP 文本，但与 VSFilterMod 的 UTF-16 码元边界并不完全一致。
	 * - 当 pending_kk 处于激活状态且遇到可见字符时，根据 shift 与当前字符的持续时间决定是消耗该字符、
	 *   还是根据剩余时间重写为新的 \kk(...) 标签以保留后续字符的定时信息。
	 */
	[[nodiscard]] std::string process_plain_text(const std::string &plain, double &shift, PendingKkTag &pending_kk) {
		std::string result;
		size_t pos = 0;
		while (pos < plain.size()) {
			const PlainToken token = next_plain_token(plain, pos);
			if (pending_kk.active() && token.visible) {
				if (shift > 0) {
					if (shift >= pending_kk.current_duration()) {
						shift -= pending_kk.current_duration();
						result += token.text;
						pending_kk.consume_char();
					} else {
						pending_kk.reduce_current_duration(shift);
						shift = 0;
						result += pending_kk.format_tag();
						pending_kk.reset();
						result += token.text;
					}
				} else {
					result += pending_kk.format_tag();
					pending_kk.reset();
					result += token.text;
				}
			} else {
				result += token.text;
			}
			pos += token.length;
		}
		return result;
	}
}

namespace mocha {
	void MotionLine::calculate_default_position(const int style_align, const int style_margin_l, const int style_margin_r, const int style_margin_t, const int res_x, const int res_y) {
		const int vert_margin = (margin_t == 0) ? style_margin_t : margin_t;
		const int left_margin = (margin_l == 0) ? style_margin_l : margin_l;
		const int right_margin = (margin_r == 0) ? style_margin_r : margin_r;
		const int alignment = (this->align != 0) ? this->align : style_align;

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
			default: ;
		}

		// Y 坐标（对应 MoonScript 的 defaultYPosition）
		switch (static_cast<int>(std::ceil(static_cast<double>(alignment) / 3.0))) {
			case 1: // 1, 2, 3 - 底部
				y_position = res_y - vert_margin;
				break;
			case 2: // 4, 5, 6 - 中间
				y_position = res_y * 0.5;
				break;
			case 3: // 7, 8, 9 - 顶部
				y_position = vert_margin;
				break;
			default: ;
		}
	}

	bool MotionLine::extract_metrics(const int style_align, const int style_margin_l, const int style_margin_r, const int style_margin_t, const int res_x, const int res_y) {
		// 提取 \an 对齐标签
		const std::regex align_re(R"(\\an([1-9]))");
		std::smatch match;
		const std::string search_text = text;

		if (std::regex_search(search_text, match, align_re)) {
			if (align == 0) {
				align = std::stoi(match[1].str());
			}
		}

		if (align == 0) {
			align = style_align;
		}

		// 提取 \pos 标签
		const std::regex pos_re(R"(\\pos\(([.\d\-]+),([.\d\-]+)\))");
		bool has_pos = false;
		if (std::regex_search(search_text, match, pos_re)) {
			if (!move.has_value()) {
				x_position = std::stod(match[1].str());
				y_position = std::stod(match[2].str());
				has_pos = true;
			}
		}

		// 提取 \move 标签（6 参数完整形式）
		const std::regex move_re(R"(\\move\(([.\d\-]+),([.\d\-]+),([.\d\-]+),([.\d\-]+),([.\d\-]+),([.\d\-]+)\))");
		if (!has_pos && std::regex_search(search_text, match, move_re)) {
			move = MoveData{
				std::stod(match[1].str()), std::stod(match[2].str()),
				std::stod(match[3].str()), std::stod(match[4].str()),
				std::stoi(match[5].str()), std::stoi(match[6].str())
			};
		}

		// 提取 \move 标签（4 参数省略形式，t1=0, t2=duration）
		// ASS 规范允许 \move(x1,y1,x2,y2)，省略时间参数时默认全程移动
		if (!has_pos && !move.has_value()) {
			const std::regex move4_re(R"(\\move\(([.\d\-]+),([.\d\-]+),([.\d\-]+),([.\d\-]+)\))");
			if (std::regex_search(search_text, match, move4_re)) {
				move = MoveData{
					std::stod(match[1].str()), std::stod(match[2].str()),
					std::stod(match[3].str()), std::stod(match[4].str()),
					0, duration
				};
			}
		}

		if (!has_pos && !move.has_value()) {
			calculate_default_position(
				style_align, style_margin_l, style_margin_r,
				style_margin_t, res_x, res_y
			);
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
		for (const auto &t : transforms) {
			const auto pos = result.find(t.token);
			if (pos != std::string::npos) {
				result.replace(pos, t.token.length(), "\\t" + t.raw_string);
			}
		}
		text = result;
		transforms_tokenized = false;
	}

	std::string MotionLine::interpolate_transforms_copy(const int start, const int res_x, const int res_y, const std::optional<int> alpha_shifted_time, const std::optional<int> alpha_original_time) const {
		if (!transforms_tokenized) return text;
		const auto prior_tags = collect_prior_inline_tags();
		return transform_utils::interpolate_transforms_copy(
			text, transforms, start - start_time, properties, prior_tags,
			res_x, res_y, alpha_shifted_time, alpha_original_time
		);
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
		const auto &registry = TagRegistry::instance();

		// 将捕获的字符串转换为 EffectTagValue 的辅助函数
		auto convert_capture = [](const TagDef *tag_def, const std::string &capture) -> Transform::EffectTagValue {
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
				int position{}; // 块内最后匹配的末尾位置
				std::string capture; // 最后匹配的捕获值
				const TagDef *def{}; // 标签定义
			};
			std::map<std::string, TagMatch> block_matches;

			// 对每个可变换标签进行匹配，记录位置
			for (const auto *tag_def : registry.transform_tags()) {
				const auto &tag_re = tag_def->compiled_pattern;
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
			for (auto &[name, tm] : block_matches) {
				// 先用自身值更新 result
				result[name] = convert_capture(tm.def, tm.capture);
			}

			// 检查 affectedBy 覆盖关系
			for (auto &[name, tm] : block_matches) {
				if (!tm.def->affected_by.empty()) {
					for (const auto &parent_name : tm.def->affected_by) {
						auto parent_it = block_matches.find(parent_name);
						if (parent_it != block_matches.end()) {
							// affectedBy 标签在块内也有匹配
							if (parent_it->second.position > tm.position) {
								// affectedBy 标签出现在本标签之后 → 覆盖
								// 对应 MoonScript: newTagBlock\gsub 从 primary 之后搜索 parent
								const TagDef *parent_def = parent_it->second.def;
								result[name] = convert_capture(parent_def, parent_it->second.capture);
							}
						}
					}
				}
			}
		}

		return result;
	}

	void MotionLine::deduplicate_tags() {
		// 临时合并相邻标签块：使用 \x06 作为分隔符保留块边界位置
		// 对应 MoonScript: @splitChar = "\\\6"
		//   text = text\gsub "}{", @splitChar
		//   ... 处理 ...
		//   text = text\gsub @splitChar, "}{"
		static constexpr std::string split_char = "\x06";
		text = std::regex_replace(text, std::regex(R"(\}\{)"), split_char);

		const auto &registry = TagRegistry::instance();

		// 处理全局标签（只保留第一个出现的）
		std::map<std::string, bool> seen_global;
		text = tag_utils::run_callback_on_overrides(
			text, [&](const std::string &tag_block, int major) {
				std::string result = tag_block;
				for (const auto *tag_def : registry.one_time_tags()) {
					const auto &re = tag_def->compiled_pattern;
					std::string filtered;
					std::sregex_iterator it(result.begin(), result.end(), re);
					std::sregex_iterator end;
					size_t last = 0;
					bool seen = seen_global.contains(tag_def->name);

					while (it != end) {
						const auto &match = *it;
						filtered += result.substr(last, match.position() - last);
						if (!seen) {
							filtered += match.str();
							seen = true;
							seen_global[tag_def->name] = true;
						}
						last = match.position() + match.length();
						++it;
					}

					if (last != 0) {
						filtered += result.substr(last);
						result = std::move(filtered);
					}
				}
				return result;
			}
		);

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
		for (const auto &[tag_a, tag_b] : conflicting_pairs) {
			const TagDef *def_a = registry.get(tag_a);
			const TagDef *def_b = registry.get(tag_b);
			if (!def_a || !def_b) continue;

			// 查找各自在行文本中第一次出现的位置
			std::smatch match_a, match_b;
			const auto &re_a = def_a->compiled_pattern;
			const auto &re_b = def_b->compiled_pattern;
			const bool found_a = std::regex_search(text, match_a, re_a);
			const bool found_b = std::regex_search(text, match_b, re_b);

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
		text = tag_utils::run_callback_on_overrides(
			text, [&](const std::string &tag_block, int major) {
				std::string result = tag_block;
				for (const auto *tag_def : registry.repeat_tags()) {
					result = tag_utils::deduplicate_tag(result, tag_def->compiled_pattern);
				}
				return result;
			}
		);

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
		text = tag_utils::clean_empty_clips(text);
		text = tag_utils::clean_empty_blocks(text);
	}

	void MotionLine::get_properties_from_style(const std::map<std::string, double> &style_props) {
		properties = style_props;
	}

	void MotionLine::ensure_leading_override_exists() {
		if (text.empty() || text[0] != '{') {
			text = "{}" + text;
		}
	}

	void MotionLine::run_callback_on_overrides(const std::function<std::string(const std::string &, int)> &callback) {
		text = tag_utils::run_callback_on_overrides(text, callback);
	}

	void MotionLine::run_callback_on_first_override(const std::function<std::string(const std::string &)> &callback) {
		text = tag_utils::run_callback_on_first_override(text, callback);
	}

	void MotionLine::shift_karaoke() {
		if (karaoke_shift == 0) return;

		double shift = karaoke_shift;
		PendingKkTag pending_kk;
		std::string result;
		size_t pos = 0;

		while (pos < text.size()) {
			if (text[pos] == '{') {
				const size_t end = text.find('}', pos);
				if (end == std::string::npos) {
					result += process_plain_text(text.substr(pos), shift, pending_kk);
					break;
				}

				result += "{";
				result += process_override_block(text.substr(pos + 1, end - pos - 1), shift, pending_kk);
				result += "}";
				pos = end + 1;
				continue;
			}

			const size_t next_block = text.find('{', pos);
			const size_t count = (next_block == std::string::npos) ? text.size() - pos : next_block - pos;
			result += process_plain_text(text.substr(pos, count), shift, pending_kk);
			if (next_block == std::string::npos) break;
			pos = next_block;
		}

		text = std::move(result);
	}
} // namespace mocha
