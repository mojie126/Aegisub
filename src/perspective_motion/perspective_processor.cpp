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
#include <array>
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
		[[maybe_unused]] std::string ExtractOverrideText(const std::string &line_text) {
			std::string result;
			static const std::regex override_re(R"(\{([^}]*)\})");
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
		[[maybe_unused]] double GetTagDouble(const TagRegistry &registry,
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

		/// @brief 检查向量坐标是否全部有限
		bool IsFiniteVector(const Vector2D &point) {
			return std::isfinite(point.X()) && std::isfinite(point.Y());
		}

		/// @brief 检查四边形四点坐标是否全部有限
		/// 尺寸不足或任一坐标含 NaN/Inf 时返回 false
		bool IsFiniteQuad(const Quad &quad) {
			if (quad.size() != 4)
				return false;
			for (const auto &point : quad) {
				if (!IsFiniteVector(point))
					return false;
			}
			return true;
		}

		using AlphaChannels = std::array<int, 4>;

		/// @brief 获取样式默认的四通道透明度
		AlphaChannels GetStyleAlpha(
			const std::function<const AssStyle*(const std::string &)> &style_lookup,
			const std::string &style_name) {
			AlphaChannels result{0, 0, 0, 0};
			if (style_lookup) {
				if (auto *style = style_lookup(style_name)) {
					result = {
						style->primary.a, style->secondary.a,
						style->outline.a, style->shadow.a
					};
				}
			}
			return result;
		}

		/// @brief 按标签出现顺序更新有效的四通道透明度
		AlphaChannels UpdateAlphaChannels(const std::string &content,
										AlphaChannels channels) {
			static const std::regex alpha_re(
				R"(\\(?:alpha|1a|2a|3a|4a)&H([0-9A-Fa-f]{2})&)"
			);
			for (std::sregex_iterator it(content.begin(), content.end(), alpha_re),
					end; it != end; ++it) {
				int value = 0;
				try {
					value = std::stoi((*it)[1].str(), nullptr, 16);
				} catch (...) {
					continue;
				}

				const std::string tag = (*it)[0].str();
				if (tag.find("\\alpha") == 0) {
					channels.fill(value);
				} else if (tag.find("\\1a") == 0) {
					channels[0] = value;
				} else if (tag.find("\\2a") == 0) {
					channels[1] = value;
				} else if (tag.find("\\3a") == 0) {
					channels[2] = value;
				} else {
					channels[3] = value;
				}
			}
			return channels;
		}

		/// 逐块段：前缀文本 + override 块内容 + 块后可见文本
		struct OverrideSegment {
			std::string prefix; ///< 位于该块之前的文本（仅首个块可为非空）
			std::string block; ///< override 块内容（不含花括号）
			std::string visible; ///< 该块之后、下一个块之前的可见文本
		};

		/// @brief 将行文本分割为逐块段（前缀文本 + override 块 + 后续可见文本）
		/// @param text 完整行文本
		/// @return 段列表，首段可能带非空前缀文本
		std::vector<OverrideSegment> ExtractOverrideSegments(const std::string &text) {
			std::vector<OverrideSegment> segments;
			static const std::regex override_re(R"(\{([^}]*)\})");
			auto begin = std::sregex_iterator(text.begin(), text.end(), override_re);
			auto end = std::sregex_iterator();
			for (auto it = begin; it != end; ++it) {
				OverrideSegment seg;
				size_t block_start = static_cast<size_t>(it->position());
				size_t block_end = block_start + it->length();
				// 第一个 override 块之前的普通文本必须保留（行首文本 + 内联样式场景）
				if (segments.empty() && block_start > 0)
					seg.prefix = text.substr(0, block_start);
				seg.block = (*it)[1].str();
				// 提取块后面的文本直到下一个 override 块
				auto next = std::next(it);
				size_t text_end = (next != end)
									? static_cast<size_t>(next->position())
									: text.size();
				if (block_end < text_end)
					seg.visible = text.substr(block_end, text_end - block_end);
				segments.push_back(std::move(seg));
			}
			// 处理开头无 override 块的纯文本
			if (segments.empty() && !text.empty()) {
				OverrideSegment seg;
				seg.prefix = text;
				segments.push_back(std::move(seg));
			}
			return segments;
		}

		/// @brief 从整行 override 块中提取指定标签的首次匹配值
		/// 对应 libass 事件级标签首次生效语义（\pos/\org/\an 等）
		/// @param line_text 完整行文本
		/// @param td 标签定义
		/// @return 首次匹配的捕获值，未找到返回空
		std::string ExtractFirstTagValue(const std::string &line_text, const TagDef *td) {
			if (!td) return "";
			static const std::regex block_re(R"(\{([^}]*)\})");
			auto bit = std::sregex_iterator(line_text.begin(), line_text.end(), block_re);
			auto bend = std::sregex_iterator();
			for (; bit != bend; ++bit) {
				const std::string block = (*bit)[1].str();
				auto it = std::sregex_iterator(block.begin(), block.end(), td->compiled_pattern);
				auto end = std::sregex_iterator();
				for (; it != end; ++it)
					return (*it)[1].str();
			}
			return "";
		}

		/// @brief 从单块 override 内容和可见文本中提取透视标签值
		/// @param block_content override 块内容（不含 {}）
		/// @param visible_text 可见文本
		/// @param style_lookup 样式查询函数
		/// @param style_name 样式名
		/// @param[out] width 文本宽度
		/// @param[out] height 文本高度
		/// @param fallback_pos_x 无 \pos 标签时使用的默认 X 位置（样式对齐计算值）
		/// @param fallback_pos_y 无 \pos 标签时使用的默认 Y 位置（样式对齐计算值）
		/// @param inherited 前一个 override 块的有效标签状态
		/// @param line_text 完整行文本（静态化 \t 后），用于首块无 \an 时回退整行的 \an 值
		/// @return 透视标签值
		PerspectiveTagVals ExtractBlockTags(
			const std::string &block_content,
			const std::string &visible_text,
			const std::function<const AssStyle*(const std::string &)> &style_lookup,
			const std::string &style_name,
			double &width, double &height,
			double fallback_pos_x = 0, double fallback_pos_y = 0,
			const PerspectiveTagVals *inherited = nullptr,
			const std::string *line_text = nullptr) {
			PerspectiveTagVals tags = inherited ? *inherited : PerspectiveTagVals{};
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

			// 获取块内最后一个匹配值（ASS 语义：同一块内后出现的标签覆盖前面的）
			auto get_last_val = [&](const TagDef *td) -> std::string {
				std::string result;
				std::sregex_iterator it(block_content.begin(), block_content.end(), td->compiled_pattern);
				std::sregex_iterator end;
				for (; it != end; ++it)
					result = (*it)[1].str();
				return result;
			};

			auto get_val = [&](const std::string &tag_name, double default_val) -> double {
				const TagDef *td = registry.get(tag_name);
				if (!td) return default_val;
				std::string val = get_last_val(td);
				if (val.empty()) return default_val;
				try { return std::stod(val); } catch (...) { return default_val; }
			};
			auto has_tag = [&](const std::string &tag_name) -> bool {
				const TagDef *td = registry.get(tag_name);
				if (!td) return false;
				return !get_last_val(td).empty();
			};
			auto get_ovr_or_style = [&](const std::string &tag_name, double style_val) -> double {
				double val = get_val(tag_name, style_val);
				return has_tag(tag_name) ? val : style_val;
			};

			// 对齐（\an 为事件级标签，libass 仅首次出现生效，后续块忽略块内 \an）
			{
				const TagDef *td = registry.get("align");
				if (td) {
					std::string val = get_last_val(td);
					if (!val.empty() && !inherited) {
						try { tags.align = std::stoi(val); } catch (...) {}
					} else if (!inherited) {
						// 块内无 \an：取整行首次出现的 \an 作为统一锚点，
						// 无 \an 时使用样式对齐，用户书写的 \an 保留原位不动
						tags.align = style.alignment;
						if (line_text) {
							const std::string line_val = ExtractFirstTagValue(*line_text, td);
							if (!line_val.empty()) {
								try { tags.align = std::stoi(line_val); } catch (...) {}
							}
						}
					}
				}
			}

			// 位置（\pos 为事件级标签，仅首块生效，后续块忽略块内 \pos 并继承首块值）
			// 无 \pos 标签时回退到行级默认位置，避免普通字幕被当作 (0,0)
			if (!inherited) {
				tags.pos_x = fallback_pos_x;
				tags.pos_y = fallback_pos_y;
				const TagDef *td = registry.get("pos");
				if (td) {
					std::string val = get_last_val(td);
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

			// 原点（\org 为事件级标签，仅首块生效，后续块继承首块值）
			if (!inherited) {
				tags.org_x = tags.pos_x;
				tags.org_y = tags.pos_y;
				const TagDef *td = registry.get("org");
				if (td) {
					std::string val = get_last_val(td);
					if (!val.empty()) {
						auto comma = val.find(',');
						if (comma != std::string::npos) {
							try {
								tags.org_x = std::stod(val.substr(0, comma));
								tags.org_y = std::stod(val.substr(comma + 1));
							} catch (...) {}
						}
					} else if (line_text) {
						// 首块无 \org 时回退整行首次出现的 \org（libass 事件级语义）
						const std::string line_val = ExtractFirstTagValue(*line_text, td);
						if (!line_val.empty()) {
							auto comma = line_val.find(',');
							if (comma != std::string::npos) {
								try {
									tags.org_x = std::stod(line_val.substr(0, comma));
									tags.org_y = std::stod(line_val.substr(comma + 1));
								} catch (...) {}
							}
						}
					}
				}
			}

			// 缩放
			tags.scale_x = get_ovr_or_style("xscale", inherited ? inherited->scale_x : style.scalex);
			tags.scale_y = get_ovr_or_style("yscale", inherited ? inherited->scale_y : style.scaley);

			// 旋转
			tags.angle = get_ovr_or_style("zrot", inherited ? inherited->angle : style.angle);
			tags.angle_x = get_ovr_or_style("xrot", inherited ? inherited->angle_x : 0);
			tags.angle_y = get_ovr_or_style("yrot", inherited ? inherited->angle_y : 0);

			// 剪切
			tags.shear_x = get_val("xshear", inherited ? inherited->shear_x : 0);
			tags.shear_y = get_val("yshear", inherited ? inherited->shear_y : 0);

			// 边框和阴影
			const bool has_bord = has_tag("border");
			const bool has_xbord = has_tag("xborder");
			const bool has_ybord = has_tag("yborder");
			double bord = get_val("border", inherited ? inherited->outline_x : style.border);
			if (!has_bord)
				bord = inherited ? inherited->outline_x : style.border;
			tags.outline_x = has_xbord ? get_val("xborder", bord) : bord;
			tags.outline_y = has_ybord
								? get_val("yborder", bord)
								: (has_bord ? bord : (inherited ? inherited->outline_y : bord));

			const bool has_shad = has_tag("shadow");
			const bool has_xshad = has_tag("xshadow");
			const bool has_yshad = has_tag("yshadow");
			double shad = get_val("shadow", inherited ? inherited->shadow_x : style.shadow);
			if (!has_shad)
				shad = inherited ? inherited->shadow_x : style.shadow;
			tags.shadow_x = has_xshad ? get_val("xshadow", shad) : shad;
			tags.shadow_y = has_yshad
								? get_val("yshadow", shad)
								: (has_shad ? shad : (inherited ? inherited->shadow_y : shad));

			// 字号
			double font_size = get_val("fontSize", inherited ? inherited->font_size : style.fontsize);
			tags.font_size = font_size;

			// 宽高计算
			// 优先检查是否为绘图，通过 TagRegistry 获取 scale
			int p_scale = inherited ? inherited->drawing_scale : 0;
			{
				const TagDef *draw_td = registry.get("drawing");
				if (draw_td) {
					std::string p_val = get_last_val(draw_td);
					if (!p_val.empty()) {
						try { p_scale = std::stoi(p_val); } catch (...) {}
					}
				}
			}
			tags.drawing_scale = p_scale;

			std::string draw_text;
			if (p_scale >= 1) {
				size_t ppos = block_content.find("\\p" + std::to_string(p_scale));
				if (ppos != std::string::npos)
					draw_text = block_content.substr(ppos);
				draw_text += visible_text;

				// 剔除 \pN 之后的其他标签（如 \fscx/\fscy/\bord 等），
				// 否则其中的数字会被 CalculateDrawingExtents 误当作坐标，
				// 只保留绘图指令关键字（m/l/b/s/c/n）、坐标数字与空白，
				// 对应上游 DrawingBase:getExtremePoints 只解析绘图命令
				std::string filtered;
				filtered.reserve(draw_text.size());
				bool in_tag = false;
				for (char ch : draw_text) {
					if (ch == '\\') {
						in_tag = true;
						continue;
					}
					if (in_tag) {
						// 标签体：字母+数字，遇到绘图关键字或分隔则结束标签
						if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
							in_tag = false;
						// 标签名（字母）不保留，数字也跳过直到空格结束标签
						continue;
					}
					if (ch == 'm' || ch == 'M' || ch == 'l' || ch == 'L' ||
						ch == 'b' || ch == 'B' || ch == 's' || ch == 'S' ||
						ch == 'c' || ch == 'C' || ch == 'n' || ch == 'N' ||
						ch == 'p' || ch == 'P' || std::isdigit(static_cast<unsigned char>(ch)) ||
						ch == '.' || ch == '-' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
						filtered += ch;
				}
				draw_text = std::move(filtered);
			}

			bool extents_ok = false;
			// 文本测量得到的宽高内含有效 \fscx/\fscy，需要在还原为未缩放基准时除以 scale
			bool text_extents_scaled = false;
			const bool is_drawing_block = (p_scale >= 1 && !draw_text.empty());
			if (is_drawing_block) {
				extents_ok = CalculateDrawingExtents(draw_text, p_scale, width, height);
			}
			if (!extents_ok && style_lookup && font_size > 0 && !visible_text.empty()) {
				auto *s = style_lookup(style_name);
				if (s) {
					AssStyle temp_style = *s;
					temp_style.fontsize = static_cast<int>(font_size + 0.5);
					// 用与透视标签一致的有效缩放覆盖样式默认值（行内 \fscx/\fscy 优先，
					// 否则回退样式值），保证测量结果与除回基准的 scale 一致
					temp_style.scalex = tags.scale_x;
					temp_style.scaley = tags.scale_y;
					double descent, extlead;
					if (Automation4::CalculateTextExtents(
						&temp_style, visible_text,
						width, height, descent, extlead
					)) {
						extents_ok = true;
						// 绘图块的测量兜底不应除回缩放（对应上游 981ce33：
						// 绘图的 \fscx/\fscy 不重复缩放），仅文本块标记
						text_extents_scaled = !is_drawing_block;
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
						cp = c & 0x1F;
						len = 2;
					} else if ((c & 0xF0) == 0xE0) {
						cp = c & 0x0F;
						len = 3;
					} else if ((c & 0xF8) == 0xF0) {
						cp = c & 0x07;
						len = 4;
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

			// 文本测量路径已内含 \fscx/\fscy 缩放，除回未缩放基准，
			// drawing 尺寸与字符估算路径不含缩放，不再除（对应上游 981ce33 修复）
			if (text_extents_scaled) {
				width /= (tags.scale_x / 100.0);
				height /= (tags.scale_y / 100.0);
			}

			// 标签验证警告
			{
				const char *relevant[] = {
					"pos", "org", "xscale", "yscale", "zrot",
					"xrot", "yrot", "xshear", "yshear", "border", "xborder", "yborder",
					"shadow", "xshadow", "yshadow", "fontSize"
				};
				for (const auto *tn : relevant) {
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

		/// @brief 检查块内是否存在无法解析的 fade 标签
		bool HasMalformedFadeTag(const std::string &content) {
			std::vector<std::string> saved_t;
			std::string remaining = protect_t_blocks(content, saved_t);
			static const std::regex fade_start(R"(\\(?:fad|fade)\s*\()");

			while (std::regex_search(remaining, fade_start)) {
				std::optional<FadeData> fade_data;
				std::optional<FullFadeData> full_fade_data;
				std::string stripped = tag_utils::extract_fade(
					remaining, fade_data, full_fade_data
				);
				if (!fade_data.has_value() && !full_fade_data.has_value())
					return true;
				if (stripped == remaining)
					return true;
				remaining = std::move(stripped);
			}
			return false;
		}

		/// @brief 将块内容中的 \move 插值替换为 \pos
		/// @param block_content 块内容
		/// @param sample_time 当前采样点相对行起始的时间偏移
		/// @param line_duration 行总时长（用于 4 参数 \move 默认 t2 填充）
		/// @return 调整后的块内容（\move 被替换为 \pos）
		std::string InterpolateMoveInBlock(const std::string &block_content,
											int sample_time, int line_duration) {
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
				progress = static_cast<double>(sample_time - move.t1) / (move.t2 - move.t1);
			progress = std::max(0.0, std::min(1.0, progress));
			double px = move.x1 + (move.x2 - move.x1) * progress;
			double py = move.y1 + (move.y2 - move.y1) * progress;

			const std::string pos_tag = "\\pos(" + format_compact_float(px)
										+ "," + format_compact_float(py) + ")";

			stripped = restore_t_blocks(stripped, saved_t);
			return stripped + pos_tag;
		}

		/// @brief 在参考帧文本中逐块静态化 \move
		std::string InterpolateMovesInText(const std::string &text,
											int sample_time, int line_duration) {
			if (text.find("\\move") == std::string::npos)
				return text;

			auto segments = ExtractOverrideSegments(text);
			if (segments.empty())
				return text;

			std::string result;
			for (size_t i = 0; i < segments.size(); ++i) {
				if (i == 0)
					result += segments[i].prefix;
				result += "{" + InterpolateMoveInBlock(
					segments[i].block, sample_time, line_duration
				) + "}";
				result += segments[i].visible;
			}
			return result;
		}

		/// @brief 仅在各 override 块内部去重，保留不同块的独立标签
		/// 全局标签（\pos/\move/\org/\an）按 libass 首次生效语义保留块内首个，
		/// 块级可重复标签保留最后一个，跨块全局标签在写回阶段统一到首块
		std::string DeduplicatePerspectiveBlocks(const std::string &text) {
			const auto &registry = TagRegistry::instance();
			return tag_utils::run_callback_on_overrides(
				text, [&](const std::string &block, int) {
					if (block.size() < 2)
						return block;
					std::string content = block.substr(1, block.size() - 2);
					static const std::vector<std::pair<const char *, const char *>> conflicts = {
						{"move", "pos"}, {"fade", "fad"},
						{"vectClip", "vectiClip"}
					};
					for (const auto &[first_name, second_name] : conflicts) {
						const auto *first = registry.get(first_name);
						const auto *second = registry.get(second_name);
						if (!first || !second)
							continue;
						std::smatch first_match, second_match;
						const bool has_first = std::regex_search(
							content, first_match, first->compiled_pattern
						);
						const bool has_second = std::regex_search(
							content, second_match, second->compiled_pattern
						);
						if (has_first && has_second) {
							content = tag_utils::remove_tag(
								content,
								first_match.position() < second_match.position()
									? second->pattern
									: first->pattern
							);
						}
					}
					// 全局标签首次生效（libass 事件级语义）：块内重复时保留第一个
					for (const auto *tag_def : registry.one_time_tags()) {
						const auto &re = tag_def->compiled_pattern;
						std::sregex_iterator it(content.begin(), content.end(), re);
						const std::sregex_iterator end;
						int count = 0;
						for (auto cit = it; cit != end; ++cit)
							++count;
						if (count <= 1)
							continue;
						std::string filtered;
						size_t last = 0;
						bool kept_first = false;
						for (; it != end; ++it) {
							filtered += content.substr(last, static_cast<size_t>(it->position()) - last);
							if (!kept_first) {
								filtered += it->str();
								kept_first = true;
							}
							last = static_cast<size_t>(it->position() + it->length());
						}
						filtered += content.substr(last);
						content = std::move(filtered);
					}
					for (const auto *tag_def : registry.repeat_tags())
						content = tag_utils::deduplicate_tag(content, tag_def->compiled_pattern);
					return "{" + content + "}";
				}
			);
		}

		/// 移除显式 alpha 标签（始终在保护态下执行，避免误删 \t(...) 内部的 alpha）
		static const std::regex alpha_remove_re(
			R"(\\(?:alpha|1a|2a|3a|4a)&H[0-9A-Fa-f]{2}&)"
		);

		/// @brief 对块内显式 alpha 覆盖应用行级 fade 并回写通道状态
		/// 覆盖通道乘以 fade 因子，非覆盖通道沿用继承值（已含 fade 效果），
		/// 无覆盖或因子为 1 时不做任何修改
		/// @param content 保护态块内容
		/// @param line_opacity 行级累计 fade 因子（进入本块前）
		/// @param[in,out] channels 继承通道值，函数内覆盖通道被调制并整体回写
		/// @return 是否存在被调制的显式覆盖
		bool ApplyLineFadeToOverrides(const std::string &content, double line_opacity,
									AlphaChannels &channels) {
			if (line_opacity >= 1.0 - 1e-9)
				return false;
			AlphaChannels raw_override = UpdateAlphaChannels(
				content, AlphaChannels{-1, -1, -1, -1}
			);
			bool has_override = false;
			for (int i = 0; i < 4; ++i)
				has_override = has_override || raw_override[i] != -1;
			if (!has_override)
				return false;
			int out_alpha[4];
			for (int i = 0; i < 4; ++i) {
				if (raw_override[i] == -1) {
					out_alpha[i] = channels[i];
				} else {
					double combined = 255.0 - line_opacity * (255.0 - raw_override[i]);
					out_alpha[i] = std::max(0, std::min(255, static_cast<int>(std::round(combined))));
				}
			}
			// 回写组合结果，保证后续块（含再出现的 fade）基于已调制的通道值
			channels = {out_alpha[0], out_alpha[1], out_alpha[2], out_alpha[3]};
			return true;
		}

		/// @brief 移除原 alpha 标签并追加指定通道值（保护态下进行）
		/// 四通道相同时压缩为统一 \alpha，否则逐通道写回
		/// @param content 保护态块内容
		/// @param values 要写回的通道值
		/// @return 调整后的块内容（alpha 标签为保护态占位）
		std::string RewriteAlphaTags(const std::string &content, const AlphaChannels &values) {
			std::string adjusted = std::regex_replace(content, alpha_remove_re, "");
			bool uniform = values[0] == values[1]
							&& values[0] == values[2] && values[0] == values[3];
			char buf[64];
			if (uniform) {
				std::snprintf(buf, sizeof(buf), "\\alpha&H%02X&", values[0]);
			} else {
				std::snprintf(
					buf, sizeof(buf), "\\1a&H%02X&\\2a&H%02X&\\3a&H%02X&\\4a&H%02X&",
					values[0], values[1], values[2], values[3]
				);
			}
			adjusted += buf;
			return adjusted;
		}

		/// @brief 将 \fad/\fade 逐帧静态化为 alpha 标签
		/// 淡入淡出 envelope 与块内已有 alpha 通道（\alpha、\1a-\4a）逐通道相乘，
		/// 不丢失通道信息，四通道结果相同时压缩为统一 \alpha
		/// @param block_content 块内容
		/// @param line_duration 行总时长
		/// @param td_shifted 前移采样偏移（兼容回退，fade-in 段）
		/// @param td_original 中点采样偏移（兼容回退，fade-out 段）
		/// @param[in,out] inherited_alpha 前面 override 块的有效 alpha 通道
		/// @param[in,out] line_opacity 行级累计 fade 透明度因子（乘性，1 = 无 fade），
		///   跨块传递：fade 出现在前面块时，后续块显式 alpha 覆盖也需被调制
		/// @param vis_rel_start 帧内可见区间起点（相对行起始，-1 表示非帧内采样模式）
		/// @param vis_rel_end 帧内可见区间终点（相对行起始，半开区间）
		/// @return 调整后的块内容（淡入淡出标签被替换为静态 alpha 标签）
		std::string AdjustFadeInBlock(const std::string &block_content,
									int line_duration, int td_shifted, int td_original,
									AlphaChannels &inherited_alpha,
									double &line_opacity,
									int vis_rel_start = -1, int vis_rel_end = -1) {
			// 先保护 \t(...) 块，防止 extract_fade 错误匹配内部的 \fad/\fade
			std::vector<std::string> saved_t;
			std::string protected_content = protect_t_blocks(block_content, saved_t);

			// 块内可能同时存在多个 fade 标签，逐帧全部静态化并累计透明度，
			// 注意：与上游 line2fbf（取首个 \fad）和 a-mo MotionHandler（取末个）不同，
			// 本地为乘性累计，ASS 渲染语义为后出现者覆盖先出现者（last-wins），
			// 此差异属 corner case，正常台词极少同块多 fade
			std::string remaining = protected_content;
			double total_opacity = 1.0;
			bool any_fade = false;
			while (true) {
				std::optional<FadeData> fade_data;
				std::optional<FullFadeData> full_fade_data;
				std::string stripped = tag_utils::extract_fade(remaining, fade_data, full_fade_data);
				if (!fade_data.has_value() && !full_fade_data.has_value())
					break;
				any_fade = true;

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
					break;

				const FullFadeData &f = full_fade_data.value();

				// 帧内采样模式下，采样点恰好落在 fade 完全透明端点时
				// 向可见区间内部取最小安全偏移（不跨帧、不引用前一帧）
				int eval_shifted = td_shifted;
				int eval_original = td_original;
				if (vis_rel_start >= 0 && vis_rel_end > vis_rel_start) {
					const int nudged = FrameIntervalSampler::nudge_off_fade_endpoint(
						f, td_original, vis_rel_start, vis_rel_end
					);
					eval_shifted = eval_original = nudged;
				}

				// fade 因子（0 = 完全不透明, 255 = 完全透明），各 fade 透明度累计相乘
				const double fade_val = FadeSampler::evaluate_fade(f, eval_shifted, eval_original);
				total_opacity *= (255.0 - fade_val) / 255.0;
				remaining = std::move(stripped);
			}

			if (!any_fade) {
				// 用保护后的文本扫描 alpha，避免残留 \t 内部的 alpha 污染跨块继承状态
				inherited_alpha = UpdateAlphaChannels(protected_content, inherited_alpha);
				// 前面块出现 fade 时，本块显式 alpha 覆盖需乘以行级累计 fade，
				// ASS 渲染语义中 \fad/\fade 从出现位置持续调制到行尾
				if (ApplyLineFadeToOverrides(protected_content, line_opacity, inherited_alpha)) {
					std::string adjusted = RewriteAlphaTags(protected_content, inherited_alpha);
					return restore_t_blocks(adjusted, saved_t);
				}
				return block_content;
			}

			// 本块显式 alpha 继承前一块的有效值，再应用本块标签，
			// 在保护态下扫描，避免 \t(...) 内部 alpha 污染通道继承
			AlphaChannels channel_base = UpdateAlphaChannels(remaining, inherited_alpha);
			// 本块显式 alpha 覆盖出现在前面块 fade 之后，需乘以行级累计 fade
			// （此时 line_opacity 尚不含本块 fade 因子），
			// 继承通道已含前面块 fade 效果，不受影响
			ApplyLineFadeToOverrides(remaining, line_opacity, channel_base);

			// 本块 fade 因子并入行级累计（供后续块显式 alpha 覆盖使用）
			line_opacity *= total_opacity;

			// 所有 fade 均完全不透明时不输出 alpha
			if (total_opacity >= 1.0 - 1e-9) {
				inherited_alpha = channel_base;
				// 前面块 fade 已生效时，本块显式 alpha 覆盖需写回调制值，
				// 使输出文本与继承状态一致（上游整行统一乘因子无此问题，
				// 本地有跨块状态模型，早退分支必须同步文本）
				if (ApplyLineFadeToOverrides(remaining, line_opacity, channel_base)) {
					std::string adjusted = RewriteAlphaTags(remaining, channel_base);
					return restore_t_blocks(adjusted, saved_t);
				}
				return restore_t_blocks(remaining, saved_t);
			}

			// 逐通道与 fade envelope 相乘（对应上游 MotionHandler/Util 的透明度组合）
			int out_alpha[4];
			for (int i = 0; i < 4; ++i) {
				double combined = 255.0 - total_opacity * (255.0 - channel_base[i]);
				out_alpha[i] = std::max(0, std::min(255, static_cast<int>(std::round(combined))));
			}
			inherited_alpha = {out_alpha[0], out_alpha[1], out_alpha[2], out_alpha[3]};

			// 移除原 alpha 标签（保护态下进行，避免误删 \t(...) 内部的 alpha），
			// 追加 fade 组合后的静态值
			remaining = RewriteAlphaTags(remaining, inherited_alpha);
			// 恢复 \t(...)
			remaining = restore_t_blocks(remaining, saved_t);
			return remaining;
		}
	} // anonymous namespace

/// @brief 解析 ASS 绘图指令的坐标范围以计算尺寸
/// 对应 MoonScript DrawingBase:getExtremePoints()
	bool CalculateDrawingExtents(const std::string &draw_text, int p_scale,
								double &width, double &height) {
		std::vector<double> values;
		static const std::regex num_re(R"([-+]?[0-9]*\.?[0-9]+)");
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
			combined_block += seg.block;
			// 前缀文本同样属于可见文本，参与宽度测量
			combined_visible += seg.prefix;
			combined_visible += seg.visible;
		}

		PerspectiveTagVals tags = ExtractBlockTags(
			combined_block, combined_visible,
			style_lookup_, line.style, width, height,
			0, 0, nullptr, &line.text
		);

		// 全局标签按 libass 事件级语义统一为整行首次出现值，
		// \pos/\org/\an 均只取整行首次出现，无 \pos 时回退行级默认位置，
		// 无 \org 时等于 \pos，无 \an 时使用样式对齐
		const auto &registry = TagRegistry::instance();
		{
			const std::string val = ExtractFirstTagValue(line.text, registry.get("pos"));
			if (!val.empty()) {
				auto comma = val.find(',');
				if (comma != std::string::npos) {
					try {
						tags.pos_x = std::stod(val.substr(0, comma));
						tags.pos_y = std::stod(val.substr(comma + 1));
					} catch (...) {}
				}
			} else {
				tags.pos_x = line.x_position;
				tags.pos_y = line.y_position;
			}
		}
		{
			const std::string val = ExtractFirstTagValue(line.text, registry.get("org"));
			if (!val.empty()) {
				auto comma = val.find(',');
				if (comma != std::string::npos) {
					try {
						tags.org_x = std::stod(val.substr(0, comma));
						tags.org_y = std::stod(val.substr(comma + 1));
					} catch (...) {}
				}
			} else {
				tags.org_x = tags.pos_x;
				tags.org_y = tags.pos_y;
			}
		}
		{
			const std::string val = ExtractFirstTagValue(line.text, registry.get("align"));
			if (!val.empty()) {
				try { tags.align = std::stoi(val); } catch (...) {}
			} else if (style_lookup_) {
				if (auto *s = style_lookup_(line.style))
					tags.align = s->alignment;
			}
		}

		return tags;
	}

// ============================================================================
// 标签写回
// 使用 tag_utils::run_callback_on_overrides 逐块处理，
// 确保多 override 块的标签隔离，非透视标签不受影响，
// \pos/\org 为事件级标签，仅写回首个 override 块，后续块只写回块级变换标签
// ============================================================================

	void PerspectiveProcessor::ApplyTagsToLine(MotionLine &line,
												const std::vector<PerspectiveTagVals> &per_block_tags) {
		const auto &registry = TagRegistry::instance();

		// 预构建每个块的标签字符串
		// \pos/\org 为事件级标签，仅写回首个 override 块，后续块只写回块级变换标签
		auto build_global_tags = [&](const PerspectiveTagVals &tags) -> std::string {
			std::ostringstream oss;
			const TagDef *pos_def = registry.get("pos");
			if (pos_def)
				oss << pos_def->format_multi({tags.pos_x, tags.pos_y});
			const TagDef *org_def = registry.get("org");
			if (org_def)
				oss << org_def->format_multi({tags.org_x, tags.org_y});
			return oss.str();
		};
		auto build_transform_tags = [&](const PerspectiveTagVals &tags) -> std::string {
			std::ostringstream oss;
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

		// 预构建所有块的标签文本，\pos/\org 仅出现在首个块
		std::vector<std::string> block_tag_strings;
		for (size_t i = 0; i < per_block_tags.size(); ++i) {
			std::string tags_text = build_transform_tags(per_block_tags[i]);
			if (i == 0)
				tags_text = build_global_tags(per_block_tags[i]) + tags_text;
			block_tag_strings.push_back(std::move(tags_text));
		}

		// 需要移除的透视标签列表
		const std::vector<const char *> remove_tag_names = {
			"pos", "org", "xscale", "yscale",
			"zrot", "xrot", "yrot",
			"xshear", "yshear",
			"xborder", "yborder",
			"xshadow", "yshadow",
			"move",
		};

		// 逐块处理：每个块移除旧透视标签，写入对应的新标签
		// 注意：run_callback_on_overrides 传递的 block_idx 是 1-based
		std::string text = tag_utils::run_callback_on_overrides(
			line.text, [&](const std::string &block, int block_idx) {
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
			}
		);

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
		if (!IsFiniteQuad(rel_quad) || !IsFiniteQuad(frame_quad))
			return;

		// 矢量 clip 映射
		auto map_clip_points = [&](const std::string &coord_text) -> std::string {
			static const std::regex coord_re(R"(([-+]?[0-9]*\.?[0-9]+)\s+([-+]?[0-9]*\.?[0-9]+))");
			std::string result;
			auto begin = std::sregex_iterator(coord_text.begin(), coord_text.end(), coord_re);
			auto end = std::sregex_iterator();
			size_t last = 0;
			for (auto it = begin; it != end; ++it) {
				if (last < static_cast<size_t>(it->position()))
					result += coord_text.substr(last, static_cast<size_t>(it->position()) - last);
				float x = 0;
				float y = 0;
				try {
					x = std::stof((*it)[1].str());
					y = std::stof((*it)[2].str());
				} catch (...) {
					return coord_text;
				}
				Vector2D uv = PerspectiveMath::XYToUV(rel_quad, Vector2D(x, y));
				Vector2D q = PerspectiveMath::UVToXY(frame_quad, uv);
				if (!IsFiniteVector(uv) || !IsFiniteVector(q))
					return coord_text;
				result += format_compact_float(q.X()) + " " + format_compact_float(q.Y());
				last = static_cast<size_t>(it->position() + it->length());
			}
			if (last < coord_text.size())
				result += coord_text.substr(last);
			return result;
		};

		// 单个 clip 标签的映射：支持矩形、缩放矢量 clip、无缩放矢量 clip、\iclip
		auto map_clip_tag = [&](const std::string &clip) -> std::string {
			try {
				static const std::regex rect_re(
					R"((\\i?clip)\(\s*([\-\d.]+)\s*,\s*([\-\d.]+)\s*,\s*([\-\d.]+)\s*,\s*([\-\d.]+)\s*\))"
				);
				std::smatch rect_match;
				if (std::regex_match(clip, rect_match, rect_re)) {
					const std::string tag_prefix = rect_match[1].str();
					float x1 = std::stof(rect_match[2]);
					float y1 = std::stof(rect_match[3]);
					float x2 = std::stof(rect_match[4]);
					float y2 = std::stof(rect_match[5]);
					// 矩形格式: clip(x1,y1,x2,y2) -> 多边形 (x1,y1)-(x2,y1)-(x2,y2)-(x1,y2)
					std::ostringstream oss;
					bool valid = true;
					auto mp = [&](float x, float y) {
						Vector2D uv = PerspectiveMath::XYToUV(rel_quad, Vector2D(x, y));
						Vector2D q = PerspectiveMath::UVToXY(frame_quad, uv);
						if (!IsFiniteVector(uv) || !IsFiniteVector(q)) {
							valid = false;
							return;
						}
						oss << format_compact_float(q.X()) << " " << format_compact_float(q.Y()) << " ";
					};
					mp(x1, y1);
					mp(x2, y1);
					mp(x2, y2);
					mp(x1, y2);
					if (!valid)
						return clip;
					std::string result = oss.str();
					if (!result.empty())
						result.pop_back();
					return tag_prefix + "(" + result + ")";
				}

				// 缩放矢量 clip：\clip(scale,drawing)，先按 2^(scale-1) 还原坐标
				static const std::regex scaled_vect_re(R"((\\i?clip)\((\d+),([^)]+)\))");
				std::smatch sm;
				if (std::regex_match(clip, sm, scaled_vect_re)) {
					int scale_factor = std::stoi(sm[2]);
					double divisor = std::pow(2.0, std::max(0, scale_factor - 1));
					std::string drawing = sm[3];
					// 还原缩放后的坐标
					static const std::regex coord_re(R"(([-+]?[0-9]*\.?[0-9]+)\s+([-+]?[0-9]*\.?[0-9]+))");
					std::string unscaled;
					std::sregex_iterator it(drawing.begin(), drawing.end(), coord_re);
					std::sregex_iterator end;
					size_t last = 0;
					for (; it != end; ++it) {
						unscaled += drawing.substr(last, static_cast<size_t>(it->position()) - last);
						double x = std::stod((*it)[1].str()) / divisor;
						double y = std::stod((*it)[2].str()) / divisor;
						unscaled += format_compact_float(x) + " " + format_compact_float(y);
						last = static_cast<size_t>(it->position() + it->length());
					}
					unscaled += drawing.substr(last);
					// 注意：输出有意丢弃 scale 前缀，坐标已还原到脚本分辨率，
					// 再带 scale 会被渲染器二次缩放（几何错误），上游 assf 保留 scale
					// 是因为其坐标未还原，此处形式不同但几何等价（实施计划 3.2）
					return sm[1].str() + "(" + map_clip_points(unscaled) + ")";
				}

				// 无缩放矢量 clip
				static const std::regex vect_re(R"((\\i?clip)\(([^)]+)\))");
				std::smatch vm;
				if (std::regex_match(clip, vm, vect_re)) {
					return vm[1].str() + "(" + map_clip_points(vm[2].str()) + ")";
				}

				return clip;
			} catch (...) {
				return clip;
			}
		};

		// 逐 override 块映射 clip（多块场景每个块独立处理）
		line.text = tag_utils::run_callback_on_overrides(
			line.text, [&](const std::string &block, int) {
				std::string content = block.substr(1, block.size() - 2);
				// 保护 \t(...) 内部 clip 不被误映射（静态化后通常无 \t，安全网保留）
				std::vector<std::string> saved_t;
				std::string protected_content = protect_t_blocks(content, saved_t);
				bool replaced = false;
				std::string result;
				static const std::regex clip_re(R"(\\i?clip\s*\([^)]*\))");
				std::sregex_iterator it(protected_content.begin(), protected_content.end(), clip_re);
				std::sregex_iterator end;
				size_t last = 0;
				for (; it != end; ++it) {
					result += protected_content.substr(last, static_cast<size_t>(it->position()) - last);
					std::string mapped = map_clip_tag((*it)[0].str());
					if (mapped != (*it)[0].str())
						replaced = true;
					result += mapped;
					last = static_cast<size_t>(it->position() + it->length());
				}
				result += protected_content.substr(last);
				content = restore_t_blocks(result, saved_t);
				if (!replaced)
					return block;
				return "{" + content + "}";
			}
		);
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
				} else if (!line.style.empty()) {
					LOG_D("perspective_motion") << "Line " << line.number
						<< " uses the nonexistent style \"" << line.style << "\"";
					missing_style_warnings_.push_back(
						"Line " + std::to_string(line.number)
						+ " uses the nonexistent style \"" + line.style + "\""
					);
				}
			}

			line.tokenize_transforms();
			// 透视追踪保留不同 override 块的独立标签，只在块内去重
			// \pos/\org/\an 为事件级标签，仅首块提取与写回，后续块忽略并继承首块值
			line.text = DeduplicatePerspectiveBlocks(line.text);
			line.text = tag_utils::clean_empty_clips(line.text);
			line.text = tag_utils::clean_empty_blocks(line.text);
			line.ensure_leading_override_exists();
		}
	}

	void PerspectiveProcessor::PostprocessLines(std::vector<MotionLine> &lines) {
		for (auto &line : lines) {
			// 去重前先还原并重新标记化 transform，确保 \t 内外标签作用域隔离
			// （对应上游 Aegisub-Motion #78 与 Line.deduplicateTags 两阶段规则），
			// 注意：dont_touch→tokenize→dont_touch 的往返是有意的——第一次还原
			// 占位符，tokenize 重新建立 \t 作用域标记，去重后再次还原为文本，
			// 调整此顺序会破坏 \t 内外同名标签的隔离（上游 issue69 场景）
			line.dont_touch_transforms();
			line.tokenize_transforms();
			line.text = DeduplicatePerspectiveBlocks(line.text);
			line.dont_touch_transforms();
			line.text = tag_utils::clean_empty_clips(line.text);
			line.text = tag_utils::clean_empty_blocks(line.text);
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
		if (!IsFiniteQuad(rel_quad))
			return result;

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
			std::call_once(
				layout_warned, [&]() {
					if (options_.layout_res_y > 0)
						LOG_D("perspective_motion") << "LayoutResY (" << options_.layout_res_y
							<< ") != PlayResY (" << res_y_ << "); tracking may be off";
					else
						LOG_D("perspective_motion") << "No LayoutResY set, PlayResY (" << res_y_
							<< ") != video height (" << video_height << ")";
				}
			);
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

		const MotionLine *shared_reference_line = nullptr;
		bool all_lines_contain_reference = true;
		if (frame_from_ms_) {
			const int absolute_reference_frame = collection_start + relframe - 1;
			for (const auto &candidate : lines) {
				const int candidate_start = frame_from_ms_(candidate.start_time);
				const int candidate_end = frame_from_ms_(candidate.end_time);
				const bool contains_reference = candidate_start <= absolute_reference_frame
												&& absolute_reference_frame < candidate_end;
				all_lines_contain_reference = all_lines_contain_reference && contains_reference;
				if (contains_reference && !shared_reference_line)
					shared_reference_line = &candidate;
			}
		}

		for (auto &line : lines) {
			const MotionLine &reference_line =
				(!all_lines_contain_reference && shared_reference_line)
					? *shared_reference_line
					: line;
			int line_frame_start = frame_from_ms_ ? frame_from_ms_(line.start_time) : 0;
			int line_frame_end = frame_from_ms_ ? frame_from_ms_(line.end_time) : 0;

			int rel_start = std::max(start_frame, std::max(1, line_frame_start - collection_start + 1));
			int rel_end = std::min(
				quads_available,
				line_frame_end - collection_start
			);

			if (rel_start > rel_end)
				continue;

			int ref_frame_ms = ms_from_frame_ ? ms_from_frame_(collection_start + relframe - 1) : reference_line.start_time;
			int ref_time_delta = ref_frame_ms - reference_line.start_time;

			// 参考帧采样时间：帧区间 ∩ 行区间 的中点（FrameIntervalSampler）
			int ref_sample_rel = 0;
			const bool has_ref_sample = ms_from_frame_
											? FrameIntervalSampler::compute(
												collection_start, relframe, ms_from_frame_,
												reference_line.start_time, reference_line.end_time, ref_sample_rel
											)
											: false;
			if (!has_ref_sample)
				ref_sample_rel = ref_time_delta;

			// 参考帧：先在当前采样点静态化所有 \t 变换，再提取逐块标签
			std::string ref_text = reference_line.interpolate_transforms_copy(ref_sample_rel, res_x_, res_y_);
			ref_text = InterpolateMovesInText(ref_text, ref_sample_rel, reference_line.duration);

			// ---------------------------------------------------------------
			// 参考帧：逐块提取标签并计算 UV 四边形
			// ---------------------------------------------------------------
			auto ref_segments = ExtractOverrideSegments(ref_text);
			std::vector<BlockData> ref_blocks;
			std::optional<PerspectiveTagVals> previous_ref_tags;

			for (size_t si = 0; si < ref_segments.size(); ++si) {
				const auto &seg = ref_segments[si];
				const auto &block_content = seg.block;
				// 首个块之前的前缀文本并入可见文本，参与尺寸估算
				const auto &visible_text = (si == 0 && !seg.prefix.empty())
												? seg.prefix + seg.visible
												: seg.visible;

				BlockData bd;
				bd.tagvals = ExtractBlockTags(
					block_content, visible_text,
					style_lookup_, reference_line.style, bd.width, bd.height,
					reference_line.x_position, reference_line.y_position,
					previous_ref_tags ? &*previous_ref_tags : nullptr,
					&ref_text
				);
				previous_ref_tags = bd.tagvals;

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
				if (!IsFiniteQuad(bd.uv_quad))
					bd.valid = false;

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
						if (!std::isfinite(w) || !std::isfinite(h) || w <= 0 || h <= 0)
							return {};
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
						return IsFiniteQuad(result_quad) ? result_quad : Quad{};
					};

					PerspectiveTagVals persp_tagvals = bd.tagvals;

					// 已有标签(含 \frz 旋转、\fscx/\fscy 缩放)变换后的屏幕四边形，
					// 用于在目标四边形中保留其变换效果(对应上游 205f3e2 respect tags)
					auto source_quad_opt = PerspectiveMath::TransformPoints(
						bd.tagvals, bd.width, bd.height, layout_scale
					);
					if (!source_quad_opt) {
						bd.valid = false;
						continue;
					}
					const auto &source_quad = *source_quad_opt;

					Quad normalized_quad = rect_at_pos(1, 1);
					if (!PerspectiveMath::TagsFromQuad(
						persp_tagvals, normalized_quad,
						bd.width, bd.height, options_.org_mode, layout_scale
					)) {
						bd.valid = false;
						continue;
					}

					// 目标四边形：保持原有 \fscx/\fscy 不变(scale 归一化)
					if (!std::isfinite(persp_tagvals.scale_x) ||
						!std::isfinite(persp_tagvals.scale_y) ||
						std::abs(persp_tagvals.scale_x) < 1e-12 ||
						std::abs(persp_tagvals.scale_y) < 1e-12) {
						bd.valid = false;
						continue;
					}
					double adj_w = bd.tagvals.scale_x / persp_tagvals.scale_x;
					double adj_h = bd.tagvals.scale_y / persp_tagvals.scale_y;
					Quad target_quad = rect_at_pos(adj_w, adj_h);
					if (!IsFiniteQuad(target_quad)) {
						bd.valid = false;
						continue;
					}

					// 无透视变换(仅位置/对齐/缩放基准)的矩形屏幕四边形，
					// 基准尺寸需按当前 \fscx/\fscy 放大，使其与 source_quad 的宽度一致
					// （source_quad 是带已有 fscx/旋转的行渲染结果），否则 uv 映射会
					// 把 scale 再次压缩（对应上游 205f3e2 respect tags 的意图：
					// 保持已有标签，而不是让参考帧的 \fscx/\fscy 被重算）
					//
					// 注意（对上游的有意偏离）：上游 205f3e2 的 untransformedSourceQuad
					// 使用未缩放的 width×height，在 \fscx/\fscy≠100 时会把缩放施加两次
					// （恒等 rel_quad + 纯 \fscx200 的输入会输出 \fscx400），此处用
					// 缩放后的 base_w/base_h 吸收缩放因子，fsc=100 时与上游公式严格等价，
					// fsc≠100 时修正上游的缩放翻倍缺陷，已用 TransformPoints 四角
					// 黄金测试验证（ApplyPerspectiveIdentityQuadPreservesRenderedQuad）
					double base_w = bd.width * bd.tagvals.scale_x / 100.0;
					double base_h = bd.height * bd.tagvals.scale_y / 100.0;
					Quad untransformed_source =
						PerspectiveMath::MakeRect(
							Vector2D(0, 0),
							Vector2D(static_cast<float>(base_w), static_cast<float>(base_h))
						);
					for (auto &p : untransformed_source) {
						p = Vector2D(
							p.X() - static_cast<float>(an_xshift[an_idx] * base_w),
							p.Y() - static_cast<float>(an_yshift[an_idx] * base_h)
						);
						p = p + Vector2D(
								static_cast<float>(bd.tagvals.pos_x),
								static_cast<float>(bd.tagvals.pos_y)
							);
					}

					// 将已有标签的变换效果经 UV 空间映射到目标四边形
					Quad transformed_target;
					for (const auto &p : source_quad) {
						Vector2D uv = PerspectiveMath::XYToUV(untransformed_source, p);
						if (!IsFiniteVector(uv)) {
							transformed_target.clear();
							break;
						}
						transformed_target.push_back(PerspectiveMath::UVToXY(target_quad, uv));
					}
					if (!IsFiniteQuad(transformed_target)) {
						bd.valid = false;
						continue;
					}

					if (!PerspectiveMath::TagsFromQuad(
						persp_tagvals, transformed_target,
						bd.width, bd.height, options_.org_mode, layout_scale
					)) {
						bd.valid = false;
						continue;
					}

					bd.tagvals = persp_tagvals;
				}

				// 将预校正后的标签写回参考帧文本，重新计算 UV
				MotionLine persp_line = reference_line;
				persp_line.text = ref_text;
				std::vector<PerspectiveTagVals> persp_tags;
				for (const auto &bd : ref_blocks)
					persp_tags.push_back(bd.tagvals);
				ApplyTagsToLine(persp_line, persp_tags);

				// 重读预校正后的标签
				std::string new_ref_text = persp_line.text;
				auto new_segments = ExtractOverrideSegments(new_ref_text);
				std::vector<BlockData> new_ref_blocks;
				std::optional<PerspectiveTagVals> previous_new_ref_tags;

				for (size_t si = 0; si < new_segments.size() && si < ref_blocks.size(); ++si) {
					const auto &seg = new_segments[si];
					const auto &visible_text = (si == 0 && !seg.prefix.empty())
													? seg.prefix + seg.visible
													: seg.visible;
					BlockData bd;
					bd.tagvals = ExtractBlockTags(
						seg.block, visible_text,
						style_lookup_, reference_line.style, bd.width, bd.height,
						reference_line.x_position, reference_line.y_position,
						previous_new_ref_tags ? &*previous_new_ref_tags : nullptr,
						&new_ref_text
					);
					previous_new_ref_tags = bd.tagvals;

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
					if (!IsFiniteQuad(bd.uv_quad))
						bd.valid = false;

					new_ref_blocks.push_back(bd);
				}

				ref_blocks = std::move(new_ref_blocks);
			}

			result.reserve(result.size() + (rel_end - rel_start + 1));

			// ---------------------------------------------------------------
			// 帧循环：逐帧逐块处理
			// ---------------------------------------------------------------

			// FadeSampler 仅作为无法取得精确帧交集时的兼容回退
			FadeSampler fade_sampler;
			if (ms_from_frame_) {
				fade_sampler = FadeSampler::create(collection_start, rel_start, ms_from_frame_);
			}

			for (int frame_idx = rel_start; frame_idx <= rel_end; ++frame_idx) {
				int raw_start_ms = ms_from_frame_ ? ms_from_frame_(collection_start + frame_idx - 1) : line.start_time;
				int raw_end_ms = ms_from_frame_ ? ms_from_frame_(collection_start + frame_idx) : line.end_time;
				// 输出时间按 ASS 百分秒精度四舍五入到 10ms（对应上游 #87）
				int new_start = (std::max(0, raw_start_ms) + 5) / 10 * 10;
				int new_end = (std::max(0, raw_end_ms) + 5) / 10 * 10;

				int time_delta = new_start - line.start_time;
				int frame_duration = new_end - new_start;

				// 帧内统一采样：帧区间 ∩ 行区间 的中点（FrameIntervalSampler）
				int frame_sample_rel = 0;
				int vis_rel_start = 0, vis_rel_end = 0;
				const bool has_frame_sample = ms_from_frame_
												? FrameIntervalSampler::compute(
													collection_start, frame_idx, ms_from_frame_,
													line.start_time, line.end_time, frame_sample_rel,
													&vis_rel_start, &vis_rel_end
												)
												: false;
				if (!has_frame_sample)
					frame_sample_rel = time_delta;

				// 双时间基准仅作为精确帧时间不可用时的兼容回退
				int fade_td_shifted = time_delta;
				int fade_td_original = time_delta;
				if (ms_from_frame_) {
					fade_sampler.compute(
						new_start, new_end,
						line.start_time, line.end_time,
						fade_td_original, fade_td_shifted
					);
				}
				if (has_frame_sample)
					fade_td_shifted = fade_td_original = frame_sample_rel;

				MotionLine frame_line = line;
				// 先在当前采样点静态化所有 \t 变换，再分割逐块段
				frame_line.text = line.interpolate_transforms_copy(
					frame_sample_rel, res_x_, res_y_
				);
				frame_line.start_time = new_start;
				frame_line.end_time = new_end;
				frame_line.duration = frame_duration;

				const auto &frame_quad = quads[static_cast<size_t>(frame_idx - 1)];
				if (!IsFiniteQuad(frame_quad))
					continue;

				// 将帧文本分割为逐块段
				auto frame_segments = ExtractOverrideSegments(frame_line.text);

				// 对每段进行动效调整（fade→alpha, move→pos）并收集透视标签
				std::vector<PerspectiveTagVals> per_block_tags;
				std::vector<std::string> adjusted_blocks;
				AlphaChannels alpha_state = GetStyleAlpha(style_lookup_, line.style);
				// 行级累计 fade 透明度（乘性），跨块传递
				double line_opacity = 1.0;
				std::optional<PerspectiveTagVals> previous_frame_tags;
				size_t num_blocks = std::min(frame_segments.size(), ref_blocks.size());

				for (size_t seg_i = 0; seg_i < num_blocks; ++seg_i) {
					const auto &seg = frame_segments[seg_i];
					const auto &ref_bd = ref_blocks[seg_i];
					std::string block_content = seg.block;
					// 首个块之前的前缀文本并入可见文本，参与尺寸估算
					const std::string &visible_text = (seg_i == 0 && !seg.prefix.empty())
														? seg.prefix + seg.visible
														: seg.visible;
					if (HasMalformedFadeTag(block_content))
						malformed_fade_ = true;
					// \fad/\fade 逐帧静态化为 alpha（与已有 alpha 通道逐通道组合）
					block_content = AdjustFadeInBlock(
						block_content, line.duration,
						fade_td_shifted, fade_td_original,
						alpha_state, line_opacity,
						has_frame_sample ? vis_rel_start : -1,
						has_frame_sample ? vis_rel_end : -1
					);
					// \move 插值替换为 \pos
					block_content = InterpolateMoveInBlock(block_content, frame_sample_rel, line.duration);

					adjusted_blocks.push_back(block_content);

					// 当前块的标签值和文本尺寸
					double block_width, block_height;
					PerspectiveTagVals block_tags = ExtractBlockTags(
						block_content, visible_text,
						style_lookup_, line.style, block_width, block_height,
						line.x_position, line.y_position,
						previous_frame_tags ? &*previous_frame_tags : nullptr,
						&frame_line.text
					);
					previous_frame_tags = block_tags;

					double old_scale_x = block_tags.scale_x;
					double old_scale_y = block_tags.scale_y;

					// UV 调整（track_pos 控制）
					Quad adjusted_uv = ref_bd.uv_quad;
					bool can_map = ref_bd.valid && IsFiniteQuad(adjusted_uv);
					if (can_map && !options_.track_pos) {
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
						if (!IsFiniteVector(offset))
							can_map = false;
						if (can_map)
							for (auto &uv : adjusted_uv)
								uv = uv + offset;
					}

					Quad target_quad;
					if (can_map) {
						for (const auto &uv : adjusted_uv)
							target_quad.push_back(PerspectiveMath::UVToXY(frame_quad, uv));
						can_map = IsFiniteQuad(target_quad);
					}

					// 退化四边形时保留提取值，避免 NaN 标签写回
					if (can_map) {
						can_map = PerspectiveMath::TagsFromQuad(
							block_tags, target_quad, block_width, block_height,
							options_.org_mode, layout_scale
						);
					}

					if (can_map && options_.track_bord_shad) {
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
					std::string block_content = seg.block;
					if (HasMalformedFadeTag(block_content))
						malformed_fade_ = true;
					block_content = AdjustFadeInBlock(
						block_content, line.duration,
						fade_td_shifted, fade_td_original,
						alpha_state, line_opacity,
						has_frame_sample ? vis_rel_start : -1,
						has_frame_sample ? vis_rel_end : -1
					);
					block_content = InterpolateMoveInBlock(block_content, frame_sample_rel, line.duration);
					adjusted_blocks.push_back(block_content);

					double bw, bh;
					const auto &visible_text = (seg_i == 0 && !seg.prefix.empty())
													? seg.prefix + seg.visible
													: seg.visible;
					PerspectiveTagVals bt = ExtractBlockTags(
						block_content, visible_text,
						style_lookup_, line.style, bw, bh,
						line.x_position, line.y_position,
						previous_frame_tags ? &*previous_frame_tags : nullptr,
						&frame_line.text
					);
					previous_frame_tags = bt;
					per_block_tags.push_back(bt);
				}

				// 将动效调整后的块内容写回 frame_line.text，保留首个块之前的前缀文本
				if (!adjusted_blocks.empty()) {
					std::string rebuilt;
					for (size_t i = 0; i < frame_segments.size(); ++i) {
						if (i == 0)
							rebuilt += frame_segments[i].prefix;
						rebuilt += "{"
							+ (i < adjusted_blocks.size() ? adjusted_blocks[i] : frame_segments[i].block)
							+ "}";
						rebuilt += frame_segments[i].visible;
					}
					frame_line.text = rebuilt;
				}

				if (options_.track_clip)
					PerspectiveMapClip(frame_line, rel_quad, frame_quad);

				ApplyTagsToLine(frame_line, per_block_tags);

				// includeextra：把当前帧四边形写入 extradata（对应上游
				// line.extra["_aegi_perspective_ambient_plane"]），
				// 使视觉透视工具能将结果贴回原平面
				if (options_.include_extra) {
					std::string desc;
					for (size_t vi = 0; vi < frame_quad.size() && vi < 4; ++vi) {
						if (vi > 0) desc += "|";
						char buf[64];
						std::snprintf(
							buf, sizeof(buf), "%.15g;%.15g",
							frame_quad[vi].X(), frame_quad[vi].Y()
						);
						desc += buf;
					}
					frame_line.ambient_plane = desc;
				}

				result.push_back(frame_line);
			}
		}

		return result;
	}

	int ComputeEffectiveStartFrame(int start_frame, bool relative,
									bool reverse_tracking, int relframe,
									int total_frames, int collection_start_frame) {
		// start_frame 需要正序 1-based 帧号，
		// 旧命令层实现先执行 reverse 换算、再对非相对帧号做相对换算，
		// collection_start_frame > 1 时会对正序帧号重复施加偏移得到错误值，
		// 此处修正为先换算后覆盖：absolute + reverse 组合下换算结果被
		// reverse 分支覆盖，保持正序帧号语义
		int effective = start_frame;
		if (relative) {
			if (effective == 0)
				effective = 1;
			else if (effective < 0)
				effective = total_frames + effective + 1;
		} else {
			effective = effective - collection_start_frame + 1;
		}
		if (reverse_tracking)
			effective = total_frames - relframe + 1;
		return effective;
	}
} // namespace mocha
