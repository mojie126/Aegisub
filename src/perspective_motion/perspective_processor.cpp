// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪处理器实现
// 对应 MoonScript 版 arch.PerspectiveMotion 的 track() 主流程

#include "perspective_processor.h"

#include "../ass_dialogue.h"
#include "../ass_style.h"
#include "../auto4_base.h"

#include "../mocha_motion/motion_tags.h"

#include <libaegisub/log.h>

#include <regex>
#include <sstream>
#include <cstdio>
#include <mutex>
#include <algorithm>
#include <cmath>

namespace mocha {
// ============================================================================
// 构造 / 设置
// ============================================================================

	PerspectiveProcessor::PerspectiveProcessor(const PerspectiveOptions &options,
												int res_x, int res_y)
		: options_(options), res_x_(res_x), res_y_(res_y) {}

	void PerspectiveProcessor::SetTimingFunctions(FrameFromMs frame_from_ms,
												MsFromFrame ms_from_frame) {
		frame_from_ms_ = std::move(frame_from_ms);
		ms_from_frame_ = std::move(ms_from_frame);
	}

	void PerspectiveProcessor::SetStyleLookup(
		std::function<const AssStyle*(const std::string &)> lookup
	) {
		style_lookup_ = std::move(lookup);
	}

	MotionLine PerspectiveProcessor::BuildLine(const AssDialogue *diag) {
		MotionLine line;
		line.text = diag->Text.get();
		line.style = diag->Style.get();
		line.actor = diag->Actor.get();
		line.effect = diag->Effect.get();
		line.layer = diag->Layer;
		line.start_time = diag->Start;
		line.end_time = diag->End;
		line.duration = diag->End - diag->Start;
		line.margin_l = diag->Margin[0];
		line.margin_r = diag->Margin[1];
		line.margin_t = diag->Margin[2];
		line.comment = diag->Comment;
		return line;
	}

	std::map<std::string, double> PerspectiveProcessor::ExtractStyleProperties(const AssStyle *style) {
		if (!style) return {};

		std::map<std::string, double> props;
		props["xscale"] = style->scalex;
		props["yscale"] = style->scaley;
		props["zrot"] = style->angle;
		props["border"] = style->outline_w;
		props["xborder"] = style->outline_w;
		props["yborder"] = style->outline_w;
		props["shadow"] = style->shadow_w;
		props["xshadow"] = style->shadow_w;
		props["yshadow"] = style->shadow_w;
		props["alignment"] = static_cast<double>(style->alignment);
		return props;
	}

// ============================================================================
// 标签解析辅助函数
// ============================================================================

	namespace {
		/// 提取行文本中的 override 标签块（花括号内容）
		std::string ExtractOverrideText(const std::string &line_text) {
			std::string result;
			std::regex override_re(R"(\{([^}]*)\})");
			auto begin = std::sregex_iterator(line_text.begin(), line_text.end(), override_re);
			auto end = std::sregex_iterator();
			for (auto it = begin; it != end; ++it) {
				result += (*it)[1].str();
			}
			return result;
		}

			/// @brief 检查 override 文本中指定标签的出现次数
			int CountTagOccurrences(const TagRegistry &registry,
			                        const std::string &ovr_text,
			                        const std::string &tag_name) {
				const TagDef *td = registry.get(tag_name);
				if (!td) return 0;
				int count = 0;
				std::string search_str = ovr_text;
				std::smatch m;
				while (std::regex_search(search_str, m, td->compiled_pattern)) {
					++count;
					search_str = m.suffix().str();
				}
				return count;
			}

		/// @brief 通过 TagRegistry 从 override 文本中提取 double 标签值
		double GetTagDouble(const TagRegistry &registry,
							const std::string &ovr_text,
							const std::string &tag_name,
							double default_val = 0) {
			const TagDef *td = registry.get(tag_name);
			if (!td) return default_val;
			std::string val = tag_utils::find_tag_value(ovr_text, td->pattern);
			if (val.empty()) return default_val;
			try { return std::stod(val); } catch (...) { return default_val; }
		}

		/// 判断 override 文本中是否存在指定标签（匹配即返回 true）
		bool HasTag(const TagRegistry &registry,
					const std::string &ovr_text,
					const std::string &tag_name) {
			const TagDef *td = registry.get(tag_name);
			if (!td) return false;
			return !tag_utils::find_tag_value(ovr_text, td->pattern).empty();
		}

		/// @brief 将行文本分割为逐块段（override 块 + 后续可见文本）
		/// @param text 完整行文本
		/// @return 段列表，每段包含块内容和对应的可见文本
		std::vector<std::pair<std::string, std::string>> ExtractOverrideSegments(const std::string &text) {
			std::vector<std::pair<std::string, std::string>> segments;
			std::regex override_re(R"(\{([^}]*)\})");
			auto begin = std::sregex_iterator(text.begin(), text.end(), override_re);
			auto end = std::sregex_iterator();
			size_t prev_end = 0;
			for (auto it = begin; it != end; ++it) {
				std::string block_content = (*it)[1].str();
				size_t block_start = static_cast<size_t>(it->position());
				size_t block_end = block_start + it->length();
				// 提取块后面的文本直到下一个 override 块
				std::string visible;
				auto next = std::next(it);
				size_t text_end = (next != end)
					? static_cast<size_t>(next->position())
					: text.size();
				if (block_end < text_end) {
					visible = text.substr(block_end, text_end - block_end);
				}
				segments.emplace_back(block_content, visible);
				prev_end = block_end;
			}
			// 处理开头无 override 块的纯文本
			if (segments.empty() && !text.empty()) {
				segments.emplace_back("", text);
			}
			return segments;
		}

		/// @brief 从单块 override 内容和可见文本中提取透视标签值
		/// @param block_content override 块内容（不含 {}）
		/// @param visible_text 可见文本
		/// @param style_lookup 样式查询函数
		/// @param style_name 样式名
		/// @param[out] width 文本宽度
		/// @param[out] height 文本高度
		/// @return 透视标签值
		PerspectiveTagVals ExtractBlockTags(
			const std::string &block_content,
			const std::string &visible_text,
			const std::function<const AssStyle*(const std::string &)> &style_lookup,
			const std::string &style_name,
			double &width, double &height) {

			PerspectiveTagVals tags;
			const auto &registry = TagRegistry::instance();

			// 样式默认值
			struct StyleDefaults {
				double scalex = 100, scaley = 100;
				double angle = 0;
				double border = 0, shadow = 0;
				int alignment = 7;
				double fontsize = 48;
			} style;
			if (style_lookup) {
				if (auto *s = style_lookup(style_name)) {
					style.scalex = s->scalex;
					style.scaley = s->scaley;
					style.angle = s->angle;
					style.border = s->outline_w;
					style.shadow = s->shadow_w;
					style.alignment = s->alignment;
					style.fontsize = s->fontsize;
				}
			}

			auto get_val = [&](const std::string &tag_name, double default_val) -> double {
				const TagDef *td = registry.get(tag_name);
				if (!td) return default_val;
				std::string val = tag_utils::find_tag_value(block_content, td->pattern);
				if (val.empty()) return default_val;
				try { return std::stod(val); } catch (...) { return default_val; }
			};
			auto has_tag = [&](const std::string &tag_name) -> bool {
				const TagDef *td = registry.get(tag_name);
				if (!td) return false;
				return !tag_utils::find_tag_value(block_content, td->pattern).empty();
			};
			auto get_ovr_or_style = [&](const std::string &tag_name, double style_val) -> double {
				double val = get_val(tag_name, style_val);
				return has_tag(tag_name) ? val : style_val;
			};

			// 对齐
			{
				const TagDef *td = registry.get("align");
				if (td) {
					std::string val = tag_utils::find_tag_value(block_content, td->pattern);
					if (!val.empty()) {
						try { tags.align = std::stoi(val); } catch (...) {}
					} else {
						tags.align = style.alignment;
					}
				}
			}

			// 位置
			{
				const TagDef *td = registry.get("pos");
				if (td) {
					std::string val = tag_utils::find_tag_value(block_content, td->pattern);
					if (!val.empty()) {
						auto comma = val.find(',');
						if (comma != std::string::npos) {
							try {
								tags.pos_x = std::stod(val.substr(0, comma));
								tags.pos_y = std::stod(val.substr(comma + 1));
							} catch (...) {}
						}
					}
				}
			}

			// 原点
			tags.org_x = tags.pos_x;
			tags.org_y = tags.pos_y;
			{
				const TagDef *td = registry.get("org");
				if (td) {
					std::string val = tag_utils::find_tag_value(block_content, td->pattern);
					if (!val.empty()) {
						auto comma = val.find(',');
						if (comma != std::string::npos) {
							try {
								tags.org_x = std::stod(val.substr(0, comma));
								tags.org_y = std::stod(val.substr(comma + 1));
							} catch (...) {}
						}
					}
				}
			}

			// 缩放
			tags.scale_x = get_ovr_or_style("xscale", style.scalex);
			tags.scale_y = get_ovr_or_style("yscale", style.scaley);

			// 旋转
			tags.angle = get_ovr_or_style("zrot", style.angle);
			tags.angle_x = get_ovr_or_style("xrot", 0);
			tags.angle_y = get_ovr_or_style("yrot", 0);

			// 剪切
			tags.shear_x = get_val("xshear", 0);
			tags.shear_y = get_val("yshear", 0);

			// 边框和阴影
			double bord = get_val("border", style.border);
			if (!has_tag("border")) bord = style.border;
			tags.outline_x = get_val("xborder", bord);
			tags.outline_y = get_val("yborder", bord);
			double shad = get_val("shadow", style.shadow);
			if (!has_tag("shadow")) shad = style.shadow;
			tags.shadow_x = get_val("xshadow", shad);
			tags.shadow_y = get_val("yshadow", shad);

			// 字号
			double font_size = get_val("fontSize", style.fontsize);

			// 宽高计算
			// 优先检查是否为绘图，通过 TagRegistry 获取 scale
			int p_scale = 0;
			{
				const TagDef *draw_td = registry.get("drawing");
				if (draw_td) {
					std::string p_val = tag_utils::find_tag_value(block_content, draw_td->pattern);
					if (!p_val.empty()) {
						try { p_scale = std::stoi(p_val); } catch (...) {}
					}
				}
			}

			std::string draw_text;
			if (p_scale >= 1) {
				size_t ppos = block_content.find("\\p" + std::to_string(p_scale));
				if (ppos != std::string::npos)
					draw_text = block_content.substr(ppos);
				draw_text += visible_text;
			}

			bool extents_ok = false;
			if (p_scale >= 1 && !draw_text.empty()) {
				extents_ok = CalculateDrawingExtents(draw_text, p_scale, width, height);
			}
			if (!extents_ok && style_lookup && font_size > 0 && !visible_text.empty()) {
				auto *s = style_lookup(style_name);
				if (s) {
					AssStyle temp_style = *s;
					temp_style.fontsize = static_cast<int>(font_size + 0.5);
					double descent, extlead;
					if (Automation4::CalculateTextExtents(&temp_style, visible_text,
						width, height, descent, extlead)) {
						extents_ok = true;
					}
				}
			}

			if (!extents_ok) {
				if (font_size <= 0) font_size = 48;
				int char_count = 0, cjk_count = 0;
				for (auto it = visible_text.begin(); it != visible_text.end();) {
					unsigned char c = static_cast<unsigned char>(*it);
					int cp = 0, len = 1;
					if ((c & 0x80) == 0) { cp = c; } else if ((c & 0xE0) == 0xC0) {
						cp = c & 0x1F; len = 2;
					} else if ((c & 0xF0) == 0xE0) {
						cp = c & 0x0F; len = 3;
					} else if ((c & 0xF8) == 0xF0) {
						cp = c & 0x07; len = 4;
					}
					for (int k = 1; k < len && (it + k) != visible_text.end(); ++k)
						cp = (cp << 6) | (static_cast<unsigned char>(*(it + k)) & 0x3F);
					if (cp > 0) {
						++char_count;
						if ((cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0x3000 && cp <= 0x303F) ||
							(cp >= 0xFF00 && cp <= 0xFFEF) || (cp >= 0x3040 && cp <= 0x309F) ||
							(cp >= 0x30A0 && cp <= 0x30FF) || (cp >= 0xAC00 && cp <= 0xD7AF))
							++cjk_count;
					}
					std::advance(it, len);
				}
				double width_factor = (cjk_count > char_count / 2) ? 0.9 : 0.5;
				if (char_count == 0) char_count = 1;
				width = std::max(0.01, font_size * char_count * width_factor);
				height = std::max(0.01, font_size * 1.2);
			}

			width /= (tags.scale_x / 100.0);
			height /= (tags.scale_y / 100.0);

			// 标签验证警告
			{
				const char* relevant[] = {"pos", "org", "xscale", "yscale", "zrot",
					"xrot", "yrot", "xshear", "yshear", "border", "xborder", "yborder",
					"shadow", "xshadow", "yshadow", "fontSize"};
				for (const auto* tn : relevant) {
					if (CountTagOccurrences(registry, block_content, tn) >= 2)
						LOG_D("perspective_motion") << "Multiple " << tn << " tags in block";
				}
				if (HasTag(registry, block_content, "move"))
					LOG_D("perspective_motion") << "\\move tag present in block";
				if (width <= 0.01 || height <= 0.01)
					LOG_D("perspective_motion") << "Zero or near-zero text/drawing size";
			}

			return tags;
		}

		/// @brief 保护 \t(...) 块，用占位符替换
		/// 注意：此正则仅支持最多一层嵌套括号（如 \t(0,500,\move(...))），
		/// 不支持 \t 嵌套 \t。但此限制非问题，因为 \t(...) 的插值求值已由
		/// detokenize_transforms_copy 在调用此函数之前处理完毕，此处仅为安全网。
		std::string protect_t_blocks(const std::string &content,
									std::vector<std::string> &saved_blocks) {
			static const std::regex t_regex(R"(\\t\([^()]*(?:\([^()]*\)[^()]*)*\))");
			std::string result;
			auto begin = std::sregex_iterator(content.begin(), content.end(), t_regex);
			auto end = std::sregex_iterator();
			size_t last = 0;
			int idx = 0;
			for (auto it = begin; it != end; ++it) {
				result += content.substr(last, it->position() - last);
				saved_blocks.push_back(it->str());
				result += "\x01" + std::to_string(idx) + "\x01";
				++idx;
				last = it->position() + it->length();
			}
			result += content.substr(last);
			return result;
		}

		/// @brief 恢复 \t(...) 块
		std::string restore_t_blocks(const std::string &content,
									const std::vector<std::string> &saved_blocks) {
			std::string result = content;
			for (int i = static_cast<int>(saved_blocks.size()) - 1; i >= 0; --i) {
				std::string ph = "\x01" + std::to_string(i) + "\x01";
				size_t pos = result.find(ph);
				if (pos != std::string::npos)
					result.replace(pos, ph.length(), saved_blocks[i]);
			}
			return result;
		}

		/// @brief 将块内容中的 \move 插值替换为 \pos
		/// @param block_content 块内容
		/// @param time_delta 当前帧相对行起始的时间偏移
		/// @param line_duration 行总时长（用于 4 参数 \move 默认 t2 填充）
		/// @return 调整后的块内容（\move 被替换为 \pos）
		std::string InterpolateMoveInBlock(const std::string &block_content,
											int time_delta, int line_duration) {
			std::vector<std::string> saved_t;
			std::string protected_content = protect_t_blocks(block_content, saved_t);

			std::optional<MoveData> move_data;
			std::string stripped = tag_utils::extract_move(protected_content, move_data);

			if (!move_data.has_value())
				return block_content;

			auto move = move_data.value();
			// 4 参数 \move(x1,y1,x2,y2)：默认 t1=0, t2=line_duration
			if (move.t1 == -1 && move.t2 == -1) {
				move.t1 = 0;
				move.t2 = line_duration;
			}

			double progress = 0;
			if (move.t2 != move.t1)
				progress = static_cast<double>(time_delta - move.t1) / (move.t2 - move.t1);
			progress = std::max(0.0, std::min(1.0, progress));
			double px = move.x1 + (move.x2 - move.x1) * progress;
			double py = move.y1 + (move.y2 - move.y1) * progress;

			char buf[64];
			std::snprintf(buf, sizeof(buf), "\\pos(%g,%g)", px, py);

			stripped = restore_t_blocks(stripped, saved_t);
			return stripped + buf;
		}

		/// @brief 将 \fad/\fade 逐帧静态化为 \alpha&Hxx& 标签
		/// 使用双时间基准：fade-in 段用 td_shifted（前移采样），fade-out 段用 td_original（中点采样）
		/// @param block_content 块内容
		/// @param line_duration 行总时长
		/// @param td_shifted 前移采样偏移（用于 fade-in 段）
		/// @param td_original 中点采样偏移（用于 fade-out 段）
		/// @return 调整后的块内容（淡入淡出标签被替换为静态 \alpha&Hxx&）
		std::string AdjustFadeInBlock(const std::string &block_content,
									int line_duration, int td_shifted, int td_original) {
			// 先保护 \t(...) 块，防止 extract_fade 错误匹配内部的 \fad/\fade
			std::vector<std::string> saved_t;
			std::string protected_content = protect_t_blocks(block_content, saved_t);

			std::optional<FadeData> fade_data;
			std::optional<FullFadeData> full_fade_data;
			std::string stripped = tag_utils::extract_fade(protected_content, fade_data, full_fade_data);

			if (!fade_data.has_value() && !full_fade_data.has_value())
				return block_content;

			// \fad(fade_in, fade_out) 转完整 \fade，clamp t2/t3 防重叠段负数
			if (!full_fade_data.has_value() && fade_data.has_value()) {
				auto [t2_clamped, t3_clamped] = ClampFadeTimes(line_duration, fade_data->fade_in, fade_data->fade_out);
				full_fade_data = FullFadeData{
					255, 0, 255,
					0,
					t2_clamped,
					t3_clamped,
					line_duration
				};
			}

			if (!full_fade_data.has_value())
				return block_content;

			const FullFadeData &f = full_fade_data.value();

			// 移除残留的 alpha 标签（t(...) 受占位符保护）
			static const std::regex alpha_remove_re(R"(\\(?:alpha|1a|2a|3a|4a)&H[0-9A-Fa-f]{2}&)");
			stripped = std::regex_replace(stripped, alpha_remove_re, "");

			// 恢复 \t(...)
			stripped = restore_t_blocks(stripped, saved_t);

			// 双时间基准 alpha 求值（与 FadeSampler::evaluate_fade 一致）
			double alpha = 0;
			if (td_shifted < f.t1) {
				alpha = static_cast<double>(f.a1);
			} else if (td_shifted < f.t2) {
				double seg = static_cast<double>(td_shifted - f.t1);
				double dur = static_cast<double>(f.t2 - f.t1);
				alpha = (dur > 0)
					? f.a1 + (f.a2 - f.a1) * seg / dur
					: static_cast<double>(f.a2);
			} else if (td_original < f.t3) {
				alpha = static_cast<double>(f.a2);
			} else if (td_original < f.t4) {
				double seg = static_cast<double>(td_original - f.t3);
				double dur = static_cast<double>(f.t4 - f.t3);
				alpha = (dur > 0)
					? f.a2 + (f.a3 - f.a2) * seg / dur
					: static_cast<double>(f.a3);
			} else {
				alpha = static_cast<double>(f.a3);
			}

			// alpha=0（完全不透明）时不输出标签
			int alpha_int = static_cast<int>(std::round(alpha));
			if (alpha_int <= 0)
				return stripped;

			alpha_int = std::min(255, alpha_int);
			char buf[16];
			std::snprintf(buf, sizeof(buf), "\\alpha&H%02X&", alpha_int);

			stripped += buf;
			return stripped;
		}
	} // anonymous namespace

/// @brief 解析 ASS 绘图指令的坐标范围以计算尺寸
/// 对应 MoonScript DrawingBase:getExtremePoints()
bool CalculateDrawingExtents(const std::string &draw_text, int p_scale,
                             double &width, double &height) {
	std::vector<double> values;
	std::regex num_re(R"([-+]?[0-9]*\.?[0-9]+)");
	auto begin = std::sregex_iterator(draw_text.begin(), draw_text.end(), num_re);
	auto end = std::sregex_iterator();
	for (auto it = begin; it != end; ++it) {
		try { values.push_back(std::stod((*it)[0].str())); } catch (...) {}
	}
	if (values.size() < 4) return false;

	double min_x = values[0], max_x = values[0];
	double min_y = values[1], max_y = values[1];
	for (size_t j = 0; j + 1 < values.size(); j += 2) {
		double x = values[j], y = values[j + 1];
		if (x < min_x) min_x = x;
		if (x > max_x) max_x = x;
		if (y < min_y) min_y = y;
		if (y > max_y) max_y = y;
	}

	double raw_w = std::max(0.01, max_x - min_x);
	double raw_h = std::max(0.01, max_y - min_y);
	double scale_div = std::pow(2.0, std::max(0, p_scale - 1));
	width = raw_w / scale_div;
	height = raw_h / scale_div;
	return true;
}

// ============================================================================
// 从行文本中提取标签值
// ============================================================================

	PerspectiveTagVals PerspectiveProcessor::PrepareForPerspective(
		const MotionLine &line, double &width, double &height) {

	// 委托 ExtractBlockTags：利用 ExtractOverrideSegments 合并所有块内容
	// 消除与 ExtractBlockTags 的代码重复（B4 修复）
	auto segments = ExtractOverrideSegments(line.text);
	std::string combined_block;
	std::string combined_visible;
	for (const auto &seg : segments) {
		combined_block += seg.first;
		combined_visible += seg.second;
	}

	PerspectiveTagVals tags = ExtractBlockTags(combined_block, combined_visible,
		style_lookup_, line.style, width, height);

	// Position fallback from line data (仅当无 \pos 标签时)
	if (combined_block.find("\\pos(") == std::string::npos) {
		tags.pos_x = line.x_position;
		tags.pos_y = line.y_position;
	}

		return tags;
	}

// ============================================================================
// 标签写回
// 使用 tag_utils::run_callback_on_overrides 逐块处理，
// 确保多 override 块的标签隔离，非透视标签不受影响。
// ============================================================================

	void PerspectiveProcessor::ApplyTagsToLine(MotionLine &line,
												const std::vector<PerspectiveTagVals> &per_block_tags) {
		const auto &registry = TagRegistry::instance();

		// 预构建每个块的标签字符串
		auto build_tag_string = [&](const PerspectiveTagVals &tags) -> std::string {
			std::ostringstream oss;
			const TagDef *pos_def = registry.get("pos");
			if (pos_def)
				oss << pos_def->format_multi({tags.pos_x, tags.pos_y});
			const TagDef *org_def = registry.get("org");
			if (org_def)
				oss << org_def->format_multi({tags.org_x, tags.org_y});

			auto fmt_scalar = [&](const std::string &name, double val) {
				const TagDef *td = registry.get(name);
				if (td) oss << td->format_float(val);
			};
			fmt_scalar("zrot", tags.angle);
			fmt_scalar("xrot", tags.angle_x);
			fmt_scalar("yrot", tags.angle_y);
			fmt_scalar("xscale", tags.scale_x);
			fmt_scalar("yscale", tags.scale_y);
			fmt_scalar("xshear", tags.shear_x);
			fmt_scalar("yshear", tags.shear_y);
			fmt_scalar("xborder", tags.outline_x);
			fmt_scalar("yborder", tags.outline_y);
			fmt_scalar("xshadow", tags.shadow_x);
			fmt_scalar("yshadow", tags.shadow_y);
			return oss.str();
		};

		// 预构建所有块的标签文本
		std::vector<std::string> block_tag_strings;
		for (const auto &tag_vals : per_block_tags)
			block_tag_strings.push_back(build_tag_string(tag_vals));

		// 需要移除的透视标签列表
		const std::vector<const char*> remove_tag_names = {
			"pos", "org", "xscale", "yscale",
			"zrot", "xrot", "yrot",
			"xshear", "yshear",
			"xborder", "yborder",
			"xshadow", "yshadow",
			"move",
		};

		// 逐块处理：每个块移除旧透视标签，写入对应的新标签
		// 注意：run_callback_on_overrides 传递的 block_idx 是 1-based
		std::string text = tag_utils::run_callback_on_overrides(line.text, [&](const std::string &block, int block_idx) {
			std::string content = block.substr(1, block.size() - 2);
			int tag_idx = block_idx - 1; // 转为 0-based

			// 保护 \t(...) 内部标签不被旧标签移除误伤
			std::vector<std::string> saved_t;
			std::string protected_content = protect_t_blocks(content, saved_t);
			// 在受保护的内容上移除旧透视标签
			for (const auto &name : remove_tag_names) {
				const TagDef *td = registry.get(name);
				if (td)
					protected_content = tag_utils::remove_tag(protected_content, td->pattern);
			}
			// 安全网：移除 4 参数 \move(x1,y1,x2,y2)（TagDef 只匹配 6 参）
			static const std::regex move4_rem(R"(\\move\(\s*[.\d\-]+\s*,\s*[.\d\-]+\s*,\s*[.\d\-]+\s*,\s*[.\d\-]+\s*\))");
			protected_content = std::regex_replace(protected_content, move4_rem, "");
			// 恢复 \t(...)
			content = restore_t_blocks(protected_content, saved_t);

			// 写入对应块的新标签（如果存在）
			if (tag_idx >= 0 && tag_idx < static_cast<int>(block_tag_strings.size())
				&& !block_tag_strings[tag_idx].empty()) {
				content = block_tag_strings[tag_idx] + content;
			}

			if (content.empty()) return std::string();
			return "{" + content + "}";
		});

		// 清理空标签块
		text = tag_utils::clean_empty_blocks(text);

		// 如果文本开头没有 override 块且有第一个块的标签，在最前面插入
		if (!block_tag_strings.empty() && !block_tag_strings[0].empty()
			&& (text.empty() || text[0] != '{')) {
			text = "{" + block_tag_strings[0] + "}" + text;
		}

		line.text = text;
	}

// ============================================================================
// Clip 透视映射
// ============================================================================

	void PerspectiveProcessor::PerspectiveMapClip(MotionLine &line,
												const Quad &rel_quad,
												const Quad &frame_quad) {
		std::string text = line.text;
		std::smatch m;

		// 矢量 clip 映射
		auto map_clip_points = [&](const std::string &coord_text) -> std::string {
			std::regex coord_re(R"(([-+]?[0-9]*\.?[0-9]+)\s+([-+]?[0-9]*\.?[0-9]+))");
			std::string result;
			auto begin = std::sregex_iterator(coord_text.begin(), coord_text.end(), coord_re);
			auto end = std::sregex_iterator();
			size_t last = 0;
			for (auto it = begin; it != end; ++it) {
				if (last < static_cast<size_t>(it->position()))
					result += coord_text.substr(last, static_cast<size_t>(it->position()) - last);
				float x = std::stof((*it)[1].str());
				float y = std::stof((*it)[2].str());
				Vector2D uv = PerspectiveMath::XYToUV(rel_quad, Vector2D(x, y));
				Vector2D q = PerspectiveMath::UVToXY(frame_quad, uv);
				result += std::to_string(q.X()) + " " + std::to_string(q.Y());
				last = static_cast<size_t>(it->position() + it->length());
			}
			if (last < coord_text.size())
				result += coord_text.substr(last);
			return result;
		};

		// 先尝试矢量格式
		std::regex clip_vect_re(R"(\\i?clip\s*\(((?:[^()]|\([^)]*\))*)\))");

		if (std::regex_search(text, m, clip_vect_re)) {
			// 检查是否是矩形格式（4个逗号分隔的数值）
			bool is_inverse = (m[0].str().find("\\iclip") != std::string::npos);
			std::string tag_prefix = is_inverse ? "\\iclip(" : "\\clip(";

			// 参考 mocha_motion rect_clip_to_vect_clip 的做法，
			// 用 regex_match 精确匹配 4 个逗号分隔数值的矩形 clip
			static const std::regex rect_re(
				R"((\\i?clip)\(\s*([\-\d.]+)\s*,\s*([\-\d.]+)\s*,\s*([\-\d.]+)\s*,\s*([\-\d.]+)\s*\))"
			);
			std::string rect_str = m[0].str();
			std::smatch rect_match;
			if (std::regex_match(rect_str, rect_match, rect_re)) {
				float x1 = std::stof(rect_match[2]);
				float y1 = std::stof(rect_match[3]);
				float x2 = std::stof(rect_match[4]);
				float y2 = std::stof(rect_match[5]);
				// 矩形格式: clip(x1,y1,x2,y2) -> 多边形 (x1,y1)-(x2,y1)-(x2,y2)-(x1,y2)
				std::ostringstream oss;
				auto mp = [&](float x, float y) {
					Vector2D uv = PerspectiveMath::XYToUV(rel_quad, Vector2D(x, y));
					Vector2D q = PerspectiveMath::UVToXY(frame_quad, uv);
					oss << q.X() << " " << q.Y() << " ";
				};
				mp(x1, y1); mp(x2, y1); mp(x2, y2); mp(x1, y2);
				std::string new_tag = tag_prefix + oss.str().substr(0, oss.str().size() - 1) + ")";
				line.text = std::regex_replace(text, clip_vect_re, new_tag,
					std::regex_constants::format_first_only);
				return;
			}

			// 矢量格式：逐坐标映射
			std::string mapped = map_clip_points(m[1].str());
			std::string new_tag = tag_prefix + mapped + ")";
			line.text = std::regex_replace(text, clip_vect_re, new_tag,
				std::regex_constants::format_first_only);
		}
	}

// ============================================================================
// 预处理 / 后处理
// ============================================================================

	void PerspectiveProcessor::PrepareLines(std::vector<MotionLine> &lines) {
		for (auto &line : lines) {
			if (style_lookup_) {
				auto *style = style_lookup_(line.style);
				if (style) {
					auto props = ExtractStyleProperties(style);
					line.get_properties_from_style(props);

					int style_align = style->alignment;
					line.extract_metrics(
						style_align,
						line.margin_l, line.margin_r, line.margin_t,
						res_x_, res_y_
					);
				}
			}

			line.tokenize_transforms();
			line.deduplicate_tags();
			line.ensure_leading_override_exists();
		}
	}

	void PerspectiveProcessor::PostprocessLines(std::vector<MotionLine> &lines) {
		for (auto &line : lines) {
			line.detokenize_transforms();
			line.deduplicate_tags();
		}
	}

// ============================================================================
// Apply: 核心追踪管线
// ============================================================================

	std::vector<MotionLine> PerspectiveProcessor::Apply(
		std::vector<MotionLine> &lines,
		const std::vector<Quad> &quads,
		int video_width, int video_height) {
		std::vector<MotionLine> result;

		if (quads.empty() || lines.empty())
			return result;

		int start_frame = options_.start_frame;
		if (start_frame < 1) start_frame = 1;
		int quads_available = static_cast<int>(quads.size());
		if (quads_available <= 0) return result;

		int relframe = options_.relframe;
		if (relframe < 1 || relframe > quads_available)
			relframe = 1;

		const auto &rel_quad = quads[static_cast<size_t>(relframe - 1)];

		double layout_scale = 1.0;
		// 匹配 MoonScript: PlayResY / (LayoutResY or videoH)
		if (options_.layout_res_y > 0) {
			layout_scale = static_cast<double>(res_y_) / options_.layout_res_y;
		} else if (video_height > 0) {
			layout_scale = static_cast<double>(res_y_) / video_height;
		}

		// layout_scale != 1 告警（对应 MoonScript complained_about_layout_res）
		if (layout_scale != 1.0) {
			static std::once_flag layout_warned;
			std::call_once(layout_warned, [&]() {
				if (options_.layout_res_y > 0)
					LOG_D("perspective_motion") << "LayoutResY (" << options_.layout_res_y
						<< ") != PlayResY (" << res_y_ << "); tracking may be off";
				else
					LOG_D("perspective_motion") << "No LayoutResY set, PlayResY (" << res_y_
						<< ") != video height (" << video_height << ")";
			});
		}

		int collection_start = options_.selection_start_frame;

		// 逐块透视数据的中间结构
		struct BlockData {
			PerspectiveTagVals tagvals;
			double width = 0;
			double height = 0;
			Quad uv_quad;
			bool valid = true;
		};

		for (auto &line : lines) {
			int line_frame_start = frame_from_ms_ ? frame_from_ms_(line.start_time) : 0;
			int line_frame_end = frame_from_ms_ ? frame_from_ms_(line.end_time) : 0;

			int rel_start = std::max(start_frame, std::max(1, line_frame_start - collection_start + 1));
			int rel_end = std::min(
				quads_available,
				line_frame_end - collection_start
			);

			if (rel_start > rel_end)
				continue;

			int ref_frame_ms = ms_from_frame_ ? ms_from_frame_(collection_start + relframe - 1) : line.start_time;
			int ref_time_delta = ref_frame_ms - line.start_time;
			int ref_frame_duration = ms_from_frame_
										? ms_from_frame_(collection_start + relframe) - ms_from_frame_(collection_start + relframe - 1)
										: line.duration;

			std::string ref_text = line.detokenize_transforms_copy(ref_time_delta, ref_frame_duration);

			// ---------------------------------------------------------------
			// 参考帧：逐块提取标签并计算 UV 四边形
			// ---------------------------------------------------------------
			auto ref_segments = ExtractOverrideSegments(ref_text);
			std::vector<BlockData> ref_blocks;

			for (const auto &seg : ref_segments) {
				const auto &block_content = seg.first;
				const auto &visible_text = seg.second;

				BlockData bd;
				bd.tagvals = ExtractBlockTags(block_content, visible_text,
					style_lookup_, line.style, bd.width, bd.height);

				auto seg_quad_opt = PerspectiveMath::TransformPoints(
					bd.tagvals, bd.width, bd.height, layout_scale
				);
				if (!seg_quad_opt) {
					bd.valid = false;
					ref_blocks.push_back(bd);
					continue;
				}

				for (const auto &p : *seg_quad_opt)
					bd.uv_quad.push_back(PerspectiveMath::XYToUV(rel_quad, p));

				ref_blocks.push_back(bd);
			}

			// ---------------------------------------------------------------
			// apply_perspective：逐块进行透视预校正
			// ---------------------------------------------------------------
			if (options_.apply_perspective) {
				static const double an_xshift[] = {0, 0.5, 1, 0, 0.5, 1, 0, 0.5, 1};
				static const double an_yshift[] = {1, 1, 1, 0.5, 0.5, 0.5, 0, 0, 0};

				for (auto &bd : ref_blocks) {
					if (!bd.valid) continue;

					int align = bd.tagvals.align;
					if (align < 1 || align > 9) align = 7;
					int an_idx = align - 1;

					auto rect_at_pos = [&](double w, double h) -> Quad {
						Vector2D pos_uv = PerspectiveMath::XYToUV(
							rel_quad,
							Vector2D(
								static_cast<float>(bd.tagvals.pos_x),
								static_cast<float>(bd.tagvals.pos_y)
							)
						);
						Quad rect = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1, 1));
						for (auto &p : rect) {
							p = Vector2D(
								(p.X() - static_cast<float>(an_xshift[an_idx])) * static_cast<float>(w),
								(p.Y() - static_cast<float>(an_yshift[an_idx])) * static_cast<float>(h)
							);
							p = p + pos_uv;
						}
						Quad result_quad;
						for (const auto &uv : rect)
							result_quad.push_back(PerspectiveMath::UVToXY(rel_quad, uv));
						return result_quad;
					};

					PerspectiveTagVals persp_tagvals = bd.tagvals;
					PerspectiveMath::TagsFromQuad(
						persp_tagvals, rect_at_pos(1, 1),
						bd.width, bd.height, options_.org_mode, layout_scale
					);

					double adj_w = bd.tagvals.scale_x / persp_tagvals.scale_x;
					double adj_h = bd.tagvals.scale_y / persp_tagvals.scale_y;
					PerspectiveMath::TagsFromQuad(
						persp_tagvals, rect_at_pos(adj_w, adj_h),
						bd.width, bd.height, options_.org_mode, layout_scale
					);

					bd.tagvals = persp_tagvals;
				}

				// 将预校正后的标签写回参考帧文本，重新计算 UV
				MotionLine persp_line = line;
				persp_line.text = ref_text;
				std::vector<PerspectiveTagVals> persp_tags;
				for (const auto &bd : ref_blocks)
					persp_tags.push_back(bd.tagvals);
				ApplyTagsToLine(persp_line, persp_tags);

				// 重读预校正后的标签
				std::string new_ref_text = persp_line.text;
				auto new_segments = ExtractOverrideSegments(new_ref_text);
				std::vector<BlockData> new_ref_blocks;

				for (size_t si = 0; si < new_segments.size() && si < ref_blocks.size(); ++si) {
					const auto &seg = new_segments[si];
					BlockData bd;
					bd.tagvals = ExtractBlockTags(seg.first, seg.second,
						style_lookup_, line.style, bd.width, bd.height);

					auto seg_quad_opt = PerspectiveMath::TransformPoints(
						bd.tagvals, bd.width, bd.height, layout_scale
					);
					if (!seg_quad_opt) {
						bd.valid = false;
						new_ref_blocks.push_back(bd);
						continue;
					}

					for (const auto &p : *seg_quad_opt)
						bd.uv_quad.push_back(PerspectiveMath::XYToUV(rel_quad, p));

					new_ref_blocks.push_back(bd);
				}

				ref_blocks = std::move(new_ref_blocks);
			}

			result.reserve(result.size() + (rel_end - rel_start + 1));

			// ---------------------------------------------------------------
			// 帧循环：逐帧逐块处理
			// ---------------------------------------------------------------

			// FadeSampler 用于双时间基准 fade 求值
			FadeSampler fade_sampler;
			if (ms_from_frame_) {
				fade_sampler = FadeSampler::create(collection_start, rel_start, ms_from_frame_);
			}

			for (int frame_idx = rel_start; frame_idx <= rel_end; ++frame_idx) {
				int raw_start_ms = ms_from_frame_ ? ms_from_frame_(collection_start + frame_idx - 1) : line.start_time;
				int raw_end_ms = ms_from_frame_ ? ms_from_frame_(collection_start + frame_idx) : line.end_time;
				int new_start = (std::max(0, raw_start_ms) / 10) * 10;
				int new_end = (std::max(0, raw_end_ms) / 10) * 10;

				int time_delta = new_start - line.start_time;
				int frame_duration = new_end - new_start;

				// 双时间基准偏移（用于 fade 求值）
				int fade_td_shifted = time_delta;
				int fade_td_original = time_delta;
				if (ms_from_frame_) {
					fade_sampler.compute(new_start, new_end,
						line.start_time, line.end_time,
						fade_td_original, fade_td_shifted);
				}

				MotionLine frame_line = line;
				frame_line.text = line.detokenize_transforms_copy(time_delta, frame_duration);
				frame_line.start_time = new_start;
				frame_line.end_time = new_end;
				frame_line.duration = frame_duration;

				const auto &frame_quad = quads[static_cast<size_t>(frame_idx - 1)];

				// 将帧文本分割为逐块段
				auto frame_segments = ExtractOverrideSegments(frame_line.text);

				// 对每段进行动效调整（fade→alpha, move→pos）并收集透视标签
				std::vector<PerspectiveTagVals> per_block_tags;
				std::vector<std::string> adjusted_blocks;
				size_t num_blocks = std::min(frame_segments.size(), ref_blocks.size());

				for (size_t seg_i = 0; seg_i < num_blocks; ++seg_i) {
					const auto &seg = frame_segments[seg_i];
					const auto &ref_bd = ref_blocks[seg_i];
					std::string block_content = seg.first;
					const std::string &visible_text = seg.second;

					// \fad/\fade 逐帧静态化为 \alpha
					block_content = AdjustFadeInBlock(block_content, line.duration,
														fade_td_shifted, fade_td_original);
					// \move 插值替换为 \pos
					block_content = InterpolateMoveInBlock(block_content, time_delta, line.duration);

					adjusted_blocks.push_back(block_content);

					// 当前块的标签值和文本尺寸
					double block_width, block_height;
					PerspectiveTagVals block_tags = ExtractBlockTags(block_content, visible_text,
						style_lookup_, line.style, block_width, block_height);

					double old_scale_x = block_tags.scale_x;
					double old_scale_y = block_tags.scale_y;

					// UV 调整（track_pos 控制）
					Quad adjusted_uv = ref_bd.uv_quad;
					if (ref_bd.valid && !options_.track_pos) {
						Vector2D pos_current(
							static_cast<float>(block_tags.pos_x),
							static_cast<float>(block_tags.pos_y)
						);
						Vector2D pos_ref(
							static_cast<float>(ref_bd.tagvals.pos_x),
							static_cast<float>(ref_bd.tagvals.pos_y)
						);
						Vector2D offset = PerspectiveMath::XYToUV(frame_quad, pos_current)
										- PerspectiveMath::XYToUV(rel_quad, pos_ref);
						for (auto &uv : adjusted_uv)
							uv = uv + offset;
					}

					Quad target_quad;
					for (const auto &uv : adjusted_uv)
						target_quad.push_back(PerspectiveMath::UVToXY(frame_quad, uv));

					PerspectiveMath::TagsFromQuad(
						block_tags, target_quad, block_width, block_height,
						options_.org_mode, layout_scale
					);

					if (options_.track_bord_shad) {
						double sx = old_scale_x != 0 ? block_tags.scale_x / old_scale_x : 0;
						double sy = old_scale_y != 0 ? block_tags.scale_y / old_scale_y : 0;
						block_tags.outline_x *= sx;
						block_tags.outline_y *= sy;
						block_tags.shadow_x *= sx;
						block_tags.shadow_y *= sy;
					}

					per_block_tags.push_back(block_tags);
				}

				// 对超出 ref_blocks 数量的多余段：同样应用动效调整
				for (size_t seg_i = num_blocks; seg_i < frame_segments.size(); ++seg_i) {
					const auto &seg = frame_segments[seg_i];
					std::string block_content = seg.first;
					block_content = AdjustFadeInBlock(block_content, line.duration,
														fade_td_shifted, fade_td_original);
					block_content = InterpolateMoveInBlock(block_content, time_delta, line.duration);
					adjusted_blocks.push_back(block_content);

					double bw, bh;
					PerspectiveTagVals bt = ExtractBlockTags(block_content, seg.second,
						style_lookup_, line.style, bw, bh);
					per_block_tags.push_back(bt);
				}

				// 将动效调整后的块内容写回 frame_line.text
				if (!adjusted_blocks.empty()) {
					std::string rebuilt;
					for (size_t i = 0; i < frame_segments.size(); ++i) {
						rebuilt += "{"
							+ (i < adjusted_blocks.size() ? adjusted_blocks[i] : frame_segments[i].first)
							+ "}";
						rebuilt += frame_segments[i].second;
					}
					frame_line.text = rebuilt;
				}

				if (options_.track_clip)
					PerspectiveMapClip(frame_line, rel_quad, frame_quad);

				ApplyTagsToLine(frame_line, per_block_tags);

				result.push_back(frame_line);
			}
		}

		return result;
	}
} // namespace mocha
