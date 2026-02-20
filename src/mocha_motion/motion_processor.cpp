// Copyright (c) 2024-2026, Aegisub contributors
// 运动处理器实现 - 高层次的行处理流程
// 对应 MoonScript 版 prepareLines / postprocLines / applyProcessor

#include "motion_processor.h"
#include "motion_tags.h"
#include "motion_transform.h"

#include "../ass_dialogue.h"
#include "../ass_style.h"

#include <algorithm>
#include <regex>
#include <wx/log.h>
#include <wx/intl.h>
#include <libaegisub/log.h>

namespace mocha {
	MotionProcessor::MotionProcessor(const MotionOptions &options, int res_x, int res_y)
		: options_(options)
		, res_x_(res_x)
		, res_y_(res_y) {}

	void MotionProcessor::set_timing_functions(FrameFromMs frame_from_ms, MsFromFrame ms_from_frame) {
		frame_from_ms_ = std::move(frame_from_ms);
		ms_from_frame_ = std::move(ms_from_frame);
	}

	void MotionProcessor::set_style_lookup(std::function<const AssStyle*(const std::string &)> lookup) {
		style_lookup_ = std::move(lookup);
	}

	MotionLine MotionProcessor::build_line(const AssDialogue *diag) const {
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

	std::map<std::string, double> MotionProcessor::extract_style_properties(const AssStyle *style) const {
		if (!style) return {};

		std::map<std::string, double> props;
		// 对应 MoonScript Line.getPropertiesFromStyle()
		// 从样式中提取各标签的默认值
		props["xscale"] = style->scalex;
		props["yscale"] = style->scaley;
		props["xrot"] = 0;
		props["yrot"] = 0;
		props["zrot"] = style->angle;
		props["zdepth"] = 0;
		props["border"] = style->outline_w;
		props["xborder"] = style->outline_w;
		props["yborder"] = style->outline_w;
		props["shadow"] = style->shadow_w;
		props["xshadow"] = style->shadow_w;
		props["yshadow"] = style->shadow_w;
		props["alignment"] = static_cast<double>(style->alignment);

		// Alpha 值（从样式颜色中提取透明度）
		// 对应 MoonScript Tags.styleTags 中 alpha1-4 的 style 字段
		props["alpha"] = 0;
		props["alpha1"] = static_cast<double>(style->primary.a);
		props["alpha2"] = static_cast<double>(style->secondary.a);
		props["alpha3"] = static_cast<double>(style->outline.a);
		props["alpha4"] = static_cast<double>(style->shadow.a);

		return props;
	}

	std::string MotionProcessor::get_missing_alphas(const std::string &block,
													const std::map<std::string, double> &properties) const {
		// 对应 MoonScript getMissingAlphas()
		// 如果已有 \alpha 标签则不需要添加
		if (std::regex_search(block, std::regex(R"(\\alpha&H[0-9A-Fa-f]{2}&)")))
			return "";

		auto get_prop = [&](const std::string &key) -> double {
			auto it = properties.find(key);
			return (it != properties.end()) ? it->second : 0;
		};

		double a1 = get_prop("alpha1");
		double a2 = get_prop("alpha2");
		double a3 = get_prop("alpha3");
		double a4 = get_prop("alpha4");

		// 快捷方式：如果所有样式透明度都为0且块内没有任何单独的alpha标签，使用 \alpha&H00&
		// 修复：避免在已有 \1a-\4a 单独标签时仍插入 \alpha 导致冗余
		if (a1 == 0 && a2 == 0 && a3 == 0 && a4 == 0) {
			// 检查是否已存在任何单独的 alpha 标签
			bool has_any_alpha = std::regex_search(block, std::regex(R"(\\[1234]a&H[0-9A-Fa-f]{2}&)"));
			if (!has_any_alpha) {
				return "\\alpha&H00&";
			}
			// 如果已有单独的 alpha 标签，走后续逐个检查逻辑
		}

		std::string result;
		char buf[32];

		// 逐个检查并添加缺少的 alpha
		if (!std::regex_search(block, std::regex(R"(\\1a&H[0-9A-Fa-f]{2}&)"))) {
			std::snprintf(buf, sizeof(buf), "\\1a&H%02X&", static_cast<int>(a1));
			result += buf;
		}
		// \2a 仅在有卡拉 OK 标签时添加
		if (!std::regex_search(block, std::regex(R"(\\2a&H[0-9A-Fa-f]{2}&)"))
			&& std::regex_search(block, std::regex(R"(\\[kK][fo]?\d)"))) {
			std::snprintf(buf, sizeof(buf), "\\2a&H%02X&", static_cast<int>(a2));
			result += buf;
		}
		// \3a 仅在有边框标签或样式边框 > 0 时添加
		if (!std::regex_search(block, std::regex(R"(\\3a&H[0-9A-Fa-f]{2}&)"))
			&& (std::regex_search(block, std::regex(R"(\\[xy]?bord[\d.]+)"))
				|| get_prop("border") > 0)) {
			std::snprintf(buf, sizeof(buf), "\\3a&H%02X&", static_cast<int>(a3));
			result += buf;
		}
		// \4a 仅在有阴影标签或样式阴影 > 0 时添加
		// 注意：MoonScript 原始使用 [%d%.]+（不含负号），与 Tags.moon 中 shadow.pattern 不同
		if (!std::regex_search(block, std::regex(R"(\\4a&H[0-9A-Fa-f]{2}&)"))
			&& (std::regex_search(block, std::regex(R"(\\[xy]?shad[.0-9]+)"))
				|| get_prop("shadow") > 0)) {
			std::snprintf(buf, sizeof(buf), "\\4a&H%02X&", static_cast<int>(a4));
			result += buf;
		}

		return result;
	}

	std::string MotionProcessor::get_missing_tags(const std::string &block,
												const std::map<std::string, double> &properties) const {
		// 对应 MoonScript getMissingTags()
		// 需要检查的重要标签映射：tag_key -> (option_name, skip_value)
		struct ImportantTag {
			std::string key; // 标签注册名
			std::string pattern; // 正则检查模式
			std::string format; // 格式化字符串
			bool check_option; // 对应的选项是否启用
			double skip_value; // 当属性为此值时跳过
		};

		auto get_prop = [&](const std::string &key) -> double {
			auto it = properties.find(key);
			return (it != properties.end()) ? it->second : 0;
		};

		std::vector<ImportantTag> important_tags = {
			{"xscale", R"(\\fscx[\d.]+)", "\\fscx%g", options_.x_scale, 0},
			{"yscale", R"(\\fscy[\d.]+)", "\\fscy%g", options_.x_scale, 0},
			{"border", R"(\\bord[\d.]+)", "\\bord%g", options_.border, 0},
			{"shadow", R"(\\shad[-.0-9]+)", "\\shad%g", options_.shadow, 0},
			{"xrot", R"(\\frx[-.0-9]+)", "\\frx%g", options_.x_rotation, -1e9}, // 无 skip 值
			{"yrot", R"(\\fry[-.0-9]+)", "\\fry%g", options_.y_rotation, -1e9}, // 无 skip 值
			{"zrot", R"(\\frz[-.0-9]+|\\fr[-.0-9]+)", "\\frz%g", options_.z_rotation, -1e9}, // 无 skip 值
			{"zdepth", R"(\\z[-.0-9]+)", "\\z%g", options_.z_position, -1e9}, // 无 skip 值
		};

		std::string result;
		char buf[64];

		for (const auto &tag : important_tags) {
			if (!tag.check_option) continue;

			// 检查标签是否已存在于块中
			if (std::regex_search(block, std::regex(tag.pattern))) continue;

			double value = get_prop(tag.key);
			// 如果属性值等于 skip 值，跳过
			if (tag.skip_value > -1e8 && std::abs(value - tag.skip_value) < 0.001) continue;

			std::snprintf(buf, sizeof(buf), tag.format.c_str(), value);
			result += buf;
		}

		// 如果启用了变换插值，添加缺少的 alpha 标签
		if (options_.kill_trans) {
			result += get_missing_alphas(block, properties);
		}

		return result;
	}

/// @brief 预处理行集合：为运动应用做准备
///
/// 对应 MoonScript prepareLines()。
/// 这是一个包含 11 个步骤的管线，确保行文本在运动回调应用前
/// 具有完整且规范的标签结构：
///
///   步骤 1: tokenize_transforms - 将 \t 标签替换为占位符
///           （核心：避免 Issue #69 中 \t 内标签被误处理）
///   步骤 2: 将 \fad(in,out) 转为 \fade(255,0,255,0,in,dur-out,dur)
///           有利于逐帧精确计算淡入淡出
///   步骤 3: 重新 tokenize（步骤2可能修改了 \t 相关内容）
///   步骤 4: 去重标签（deduplicate_tags 保留最后值）
///   步骤 5: 提取行的对齐方式和位置信息
///   步骤 6: 如果行没有 \pos 标签，根据对齐方式添加默认位置
///   步骤 7: 在首个 override 块补充缺少的必要标签
///           （如 \fscx, \fscy, \bord, \shad, \frz, \alpha 等）
///   步骤 8: 处理 \r 重置标签（重置后重新添加缺失标签）
///   步骤 9: 标记 \org 存在性（影响线性/非线性模式选择）
///   步骤 10: 转换 clip 坐标为浮点，可选矩形转矢量
///   步骤 11: 如果没有 clip，添加空 \clip() 占位符
///
/// @param lines 待预处理的行集合（就地修改）
	void MotionProcessor::prepare_lines(std::vector<MotionLine> &lines) {
		// 对应 MoonScript prepareLines()
		for (auto &line : lines) {
			// 获取样式属性
			const AssStyle *style = nullptr;
			if (style_lookup_) {
				style = style_lookup_(line.style);
			}
			auto props = extract_style_properties(style);
			line.get_properties_from_style(props);

			// 填充样式标签默认值（包含颜色，用于 \t 插值的起始值回退）
			// 对应 MoonScript: line.properties[tag] 中的颜色值
			if (style) {
				auto make_color = [](const agi::Color &c) {
					Transform::EffectTagValue etv;
					etv.type = Transform::EffectTagValue::COL;
					etv.color = {static_cast<int>(c.b), static_cast<int>(c.g), static_cast<int>(c.r)};
					return etv;
				};
				line.style_tag_defaults["color1"] = make_color(style->primary);
				line.style_tag_defaults["color2"] = make_color(style->secondary);
				line.style_tag_defaults["color3"] = make_color(style->outline);
				line.style_tag_defaults["color4"] = make_color(style->shadow);
			}

			int style_align = style ? style->alignment : 2;
			int style_margin_l = style ? style->Margin[0] : 0;
			int style_margin_r = style ? style->Margin[1] : 0;
			int style_margin_t = style ? style->Margin[2] : 0;

			// 1. tokenize transforms
			line.tokenize_transforms();

			// 2. 在 override 块中将 \fad(in,out) 转为 \fade(255,0,255,0,in,dur-out,dur)
			line.run_callback_on_overrides(
				[&line](const std::string &block, int) {
					std::string result = block;
					std::regex fad_re(R"(\\fade?\((\d+),(\d+)\))");
					std::smatch m;
					if (std::regex_search(result, m, fad_re)) {
						int fade_in = std::stoi(m[1].str());
						int fade_out = std::stoi(m[2].str());
						char buf[128];
						std::snprintf(
							buf, sizeof(buf), "\\fade(255,0,255,0,%d,%d,%d)",
							fade_in, line.duration - fade_out, line.duration
						);
						result = std::regex_replace(result, fad_re, buf);
					}
					return result;
				}
			);

			// 3. 重新标记变换以简化后续处理
			line.dont_touch_transforms();
			line.tokenize_transforms();

			// 4. 去重标签
			line.deduplicate_tags();

			// 5. 提取行位置和对齐信息
			bool has_position = line.extract_metrics(
				style_align, style_margin_l, style_margin_r, style_margin_t,
				res_x_, res_y_
			);

			// 6. 如果行没有显式位置，添加 \pos
			if (!has_position) {
				line.ensure_leading_override_exists();
				line.run_callback_on_first_override(
					[&line](const std::string &block) {
						char buf[64];
						std::snprintf(buf, sizeof(buf), "{\\pos(%g,%g)", line.x_position, line.y_position);
						return buf + block.substr(1);
					}
				);
			}

			// 7. 在首个 override 块中添加缺少的必要标签
			line.run_callback_on_first_override(
				[this, &line](const std::string &block) {
					std::string tags = get_missing_tags(block, line.properties);
					if (tags.empty()) return block;
					return "{" + tags + block.substr(1);
				}
			);

			// 8. 处理 \r 重置标签（重新获取样式属性并添加缺少的标签）
			// 对应 MoonScript: \r 后对该块使用重置样式的属性添加缺失标签
			//
			// 双重防御机制，防止第三方扩展标签被误识别为 \r 重置：
			//
			//   第一道防线 - 正则负向前瞻（静态过滤）：
			//     (?!nd[sxyz\d]) 排除已知的 VSFilterMod \rnd 系列扩展标签
			//     （\rnd、\rndx、\rndy、\rndz、\rnds），使正则直接不匹配。
			//
			//   第二道防线 - 样式名校验（运行时验证）：
			//     当正则匹配到 \r<text> 且 text 非空时，查询样式集合验证
			//     该文本是否为合法样式名。若样式不存在，判定为非 \r 重置
			//     （可能是未知的第三方扩展标签），跳过缺失标签补全。
			//     此机制解决负向前瞻无法预见所有未来扩展标签的局限性。
			//
			// 与 MoonScript 原版行为的差异：
			//   MoonScript 中 getMissingTags 在 if styles[resetStyle] 之外调用，
			//   即使样式不存在也会使用行属性补全标签。此处有意偏离该行为，
			//   因为误将扩展标签当作 \r 重置而插入补全标签（如 \alpha&H00&）
			//   会触发 affectedBy 覆盖，造成不可逆的标签值错误。
			//   遗漏对不存在样式名的 \r 补全影响极小（罕见的拼写错误场景），
			//   而误识别扩展标签的后果严重（透明度等属性丢失）。
			line.run_callback_on_overrides(
				[this, &line, style](const std::string &block, int) {
					std::string result = block;
					std::regex reset_re(R"(\\r(?!nd[sxyz\d])([^\\}]*)(.*))");
					std::smatch m;
					if (std::regex_search(result, m, reset_re)) {
						std::string reset_style = m[1].str();
						std::string remainder = m[2].str();

						const AssStyle *rs = nullptr;
						if (style_lookup_) {
							if (!reset_style.empty()) {
								// \r<stylename>：查询样式集合验证
								rs = style_lookup_(reset_style);
								if (!rs) {
									// 样式不存在：判定为非 \r 重置，可能是第三方扩展标签
									LOG_D("mocha/processor") << "\\r tag skipped: style '"
										<< reset_style << "' not found in style collection, "
										<< "likely a third-party extension tag";
									return result;
								}
							} else {
								// \r（无参数）：重置为行的原始样式
								rs = style;
							}
						}
						// 提取重置目标样式的属性用于标签补全
						// style_lookup_ 未设置时回退为行的当前属性
						std::map<std::string, double> reset_props;
						if (rs) {
							reset_props = extract_style_properties(rs);
						} else {
							reset_props = line.properties;
						}
						std::string missing = get_missing_tags(remainder, reset_props);
						if (!missing.empty()) {
							result = std::regex_replace(
								result, reset_re,
								"\\r" + reset_style + missing + remainder
							);
						}
					}
					return result;
				}
			);

			// 9. 处理 \org（标记存在性）
			line.run_callback_on_overrides(
				[&line](const std::string &block, int) {
					if (std::regex_search(block, std::regex(R"(\\org\([-.0-9]+,[-.0-9]+\))"))) {
						line.has_org = true;
					}
					return block;
				}
			);

			// 10. 处理 clip 标签（转浮点坐标、rect→vect 转换）
			if (options_.rect_clip || options_.vect_clip) {
				line.run_callback_on_overrides(
					[this, &line](const std::string &block, int) {
						std::string result = block;
						std::regex clip_re(R"((\\i?clip\([^)]+\)))");
						std::smatch m;
						if (std::regex_search(result, m, clip_re)) {
							line.has_clip = true;
							std::string clip = m[1].str();

							// 转换 clip 缩放因子为浮点坐标
							clip = tag_utils::convert_clip_to_fp(clip);

							// 矩形 clip 转矢量 clip
							if (options_.rc_to_vc) {
								clip = tag_utils::rect_clip_to_vect_clip(clip);
							}

							result = std::regex_replace(result, clip_re, clip);
						}
						return result;
					}
				);
			}

			// 11. 如果行没有 clip，添加空的 \clip()
			if (!line.has_clip) {
				line.run_callback_on_first_override(
					[](const std::string &block) {
						return "{\\clip()" + block.substr(1);
					}
				);
			}
		}
	}

	void MotionProcessor::postprocess_lines(std::vector<MotionLine> &lines) {
		// 对应 MoonScript postprocLines()
		for (auto &line : lines) {
			if (line.was_linear) {
				// 线性模式：直接还原变换标签
				line.dont_touch_transforms();
			} else {
				// 非线性模式：去重标签
				line.deduplicate_tags();
			}

			// 移位卡拉 OK 标签
			line.shift_karaoke();

			// 清理空标签块
			line.text = std::regex_replace(line.text, std::regex(R"(\{\})"), "");
		}

		// 合并相邻相同行
		combine_identical_lines(lines);
	}

	void MotionProcessor::combine_identical_lines(std::vector<MotionLine> &lines) {
		// 对应 MoonScript combineIdenticalLines()
		// 合并文本和样式完全相同的相邻行
		if (lines.size() < 2) return;

		auto it = lines.begin();
		while (it != lines.end() && std::next(it) != lines.end()) {
			auto next = std::next(it);

			// 检查文本和样式是否相同，且时间必须相邻才合并
			// 对应 MoonScript: @text == line.text and @style == line.style
			//   and (@start_time == line.end_time or @end_time == line.start_time)
			if (it->text == next->text && it->style == next->style &&
				(it->start_time == next->end_time || it->end_time == next->start_time)) {
				// 扩展时间范围
				it->end_time = std::max(it->end_time, next->end_time);
				it->start_time = std::min(it->start_time, next->start_time);
				lines.erase(next);
			} else {
				++it;
			}
		}
	}

	void MotionProcessor::cross_line_combine(std::vector<MotionLine> &lines) {
		// 跨行合并：与 combine_identical_lines 算法相同
		// 对应 MoonScript combineWithLine 中跨行的合并逻辑
		// 调用方已确保 lines 按时间排序
		combine_identical_lines(lines);
	}

/// @brief 完整的运动应用流程
///
/// 这是模块的顶层入口函数，对应 MoonScript applyProcessor()。
/// 四阶段流程：
///   阶段 1 - 预处理：prepare_lines 补齐标签、tokenize 变换
///   阶段 2 - 数据配置：设置参考帧、根据选项裁剪数据字段
///   阶段 3 - 运动应用：MotionHandler 根据模式（线性/逐帧）
///            应用回调函数替换标签值
///   阶段 4 - 后处理：去重、移位卡拉OK、清理空块、合并相同行
///
/// clip 数据策略：
///   - 如果提供了独立 clip 数据，clip 回调使用该数据
///   - 否则 clip 回调复用主追踪数据
///   - 取决于 rect_clip / vect_clip 选项的启用状态
///
/// @param lines 输入行集合
/// @param main_data 主追踪数据（位置/缩放/旋转）
/// @param clip_data clip 追踪数据（可为 nullptr）
/// @param clip_options clip 追踪选项（可为 nullptr）
/// @param start_frame 选中行集合的起始帧号（绝对帧号）
/// @return 处理后的新行集合
	std::vector<MotionLine> MotionProcessor::apply(
		std::vector<MotionLine> &lines,
		DataHandler &main_data,
		DataHandler *clip_data,
		const ClipTrackOptions *clip_options,
		int start_frame) {
		// 完整的运动应用流程

		// 1. 预处理行
		prepare_lines(lines);

		// 2. 配置追踪数据的参考帧
		// 对应 MoonScript prepareConfig 中的 startFrame 调整
		int ref_frame = options_.start_frame;
		if (options_.relative) {
			// 相对模式：0 自动调整为 1，负数表示从数据末尾倒数
			if (ref_frame == 0)
				ref_frame = 1;
			else if (ref_frame < 0)
				ref_frame = main_data.length() + ref_frame + 1;
		} else {
			// 绝对模式：用户输入的是视频绝对帧号，需转换为相对帧号
			// 对应 MoonScript: startFrame = startFrame - lineCollection.startFrame + 1
			ref_frame = ref_frame - start_frame + 1;
			if (ref_frame <= 0) {
				wxLogError(_("Absolute start frame is out of range (before selection start)"));
				ref_frame = 1;
			}
			if (ref_frame > main_data.length()) {
				wxLogError(_("Absolute start frame is out of range (beyond tracking data end)"));
				ref_frame = main_data.length();
			}
		}
		main_data.add_reference_frame(ref_frame);
		main_data.strip_fields(options_);

		if (clip_data) {
			// clip 数据使用独立的参考帧和字段过滤
			// 对应 MoonScript: clipData.dataObject\addReferenceFrame config.clip.startFrame
			//                   clipData.dataObject\stripFields config.clip
			int clip_ref = ref_frame;
			if (clip_options) {
				clip_ref = clip_options->start_frame;
				if (clip_options->relative) {
					if (clip_ref == 0) clip_ref = 1;
					else if (clip_ref < 0) clip_ref = clip_data->length() + clip_ref + 1;
				} else {
					clip_ref = clip_ref - start_frame + 1;
					if (clip_ref <= 0) clip_ref = 1;
					if (clip_ref > clip_data->length()) clip_ref = clip_data->length();
				}
			}
			clip_data->add_reference_frame(clip_ref);

			// 使用 clip 选项构建 MotionOptions 子集进行字段过滤
			if (clip_options && !clip_data->is_srs()) {
				MotionOptions clip_strip_opts;
				clip_strip_opts.x_position = clip_options->x_position;
				clip_strip_opts.y_position = clip_options->y_position;
				clip_strip_opts.x_scale = clip_options->x_scale;
				clip_strip_opts.z_rotation = clip_options->z_rotation;
				clip_data->strip_fields(clip_strip_opts);
			}
		}

		// 3. 构建 MotionHandler 并应用运动
		// 对应 MoonScript prepareConfig 中的 rectClipData/vectClipData 分配逻辑
		// 当有独立 clip 数据时，使用 clip_options 的 rect_clip/vect_clip 决定分配
		// 否则使用 main options 的 rect_clip/vect_clip 并复用主数据
		DataHandler *rect_data = nullptr;
		DataHandler *vect_data = nullptr;

		if (clip_data && clip_options) {
			// 有独立 clip 数据：由 clip_options 决定使用
			if (clip_options->rect_clip) {
				rect_data = clip_data;
			}
			if (clip_options->vect_clip) {
				vect_data = clip_data;
			}
			// 主数据的 rect_clip/vect_clip 仍可作为后备
			if (!rect_data && options_.rect_clip) {
				rect_data = &main_data;
			}
			if (!vect_data && options_.vect_clip) {
				vect_data = &main_data;
			}
		} else {
			// 无独立 clip 数据：复用主数据
			if (options_.rect_clip) {
				rect_data = &main_data;
			}
			if (options_.vect_clip) {
				vect_data = &main_data;
			}
		}

		MotionHandler handler(options_, &main_data, rect_data, vect_data);
		auto result = handler.apply_motion(lines, start_frame, frame_from_ms_, ms_from_frame_);

		// 4. 后处理
		postprocess_lines(result);

		return result;
	}
} // namespace mocha
