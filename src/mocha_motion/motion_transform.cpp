// Copyright (c) 2024-2026, Aegisub contributors
// 变换标签 (\t) 处理实现
// 对应 MoonScript 版 a-mo.Transform

#include "motion_transform.h"

#include <regex>
#include <sstream>
#include <algorithm>

#include "motion_tags.h"

namespace mocha {
// ============================================================
// Transform 类实现
// ============================================================

/// 从 \t 标签的括号内容解析出 Transform 对象
/// @param transform_string 括号内的完整内容，如 "(0,1000,1.5,\\1c&HFF0000&)"
/// @param line_duration 所属行的持续时间（毫秒），用于默认 end_time
/// @param tag_index 该 \t 在行内的出现次序
/// @return 解析完成的 Transform 对象
///
/// 解析策略优先级（从最完整到最简）：
///   (t1,t2,accel,effect) - 完全指定
///   (t1,t2,effect)       - 默认 accel=1.0
///   (accel,effect)       - 默认 t1=0, t2=line_duration
///   (effect)             - 全部使用默认值
/// 解析完成后自动调用 gather_tags_in_effect() 收集效果中的标签
	Transform Transform::from_string(const std::string &transform_string, int line_duration, int tag_index) {
		Transform t;
		t.raw_string = transform_string;
		t.index = tag_index;

		// 解析 (t1,t2,accel,effect) 或 (t1,t2,effect) 或 (accel,effect) 或 (effect)
		std::regex re4(R"(^\(([\-\d]*),?([\-\d]*),?([\d.]*),?(.+)\)$)");
		std::smatch match;
		const std::string &content = transform_string;

		if (std::regex_match(content, match, re4)) {
			std::string s_start = match[1].str();
			std::string s_end = match[2].str();
			std::string s_accel = match[3].str();
			std::string s_effect = match[4].str();

			// 处理 \t(accel,\1c&H0000FF&) 的情况：
			// 数字2被匹配到 transStart，.345被匹配到 transEnd
			if (!s_start.empty()) {
				try {
					int start_val = std::stoi(s_start);
					// 检查 s_end 是否合法
					if (s_end.empty() || (!s_end.empty() && s_end.find_first_not_of("-0123456789") != std::string::npos)) {
						// s_end 不是有效数字，将 s_start 视为 accel 的一部分
						s_accel = s_start + s_accel;
						s_start = "";
						s_end = "";
					} else {
						(void) start_val;
					}
				} catch (...) {
					s_start = "";
				}
			}

			if (!s_accel.empty()) {
				try { t.accel = std::stod(s_accel); } catch (...) { t.accel = 1.0; }
			} else {
				t.accel = 1.0;
			}

			if (!s_start.empty()) {
				try { t.start_time = std::stoi(s_start); } catch (...) { t.start_time = 0; }
			}

			if (!s_end.empty()) {
				try { t.end_time = std::stoi(s_end); } catch (...) { t.end_time = 0; }
			}

			if (t.end_time == 0) {
				t.end_time = line_duration;
			}

			t.effect = s_effect;
		}

		t.gather_tags_in_effect();
		return t;
	}

	std::string Transform::to_string(int line_duration) const {
		if (effect.empty()) return "";
		if (end_time <= 0) return effect;
		if (line_duration > 0 && (start_time > line_duration || end_time < start_time)) return "";

		std::ostringstream oss;
		if (accel == 1.0) {
			oss << "\\t(" << start_time << "," << end_time << "," << effect << ")";
		} else {
			oss << "\\t(" << start_time << "," << end_time << "," << accel << "," << effect << ")";
		}
		return oss.str();
	}

	void Transform::gather_tags_in_effect() {
		effect_tags.clear();
		typed_effect_tags.clear();
		const auto &registry = TagRegistry::instance();
		for (const auto *tag_def : registry.transform_tags()) {
			std::regex re(tag_def->pattern);
			std::sregex_iterator it(effect.begin(), effect.end(), re);
			std::sregex_iterator end;
			while (it != end) {
				const std::string captured = (*it)[1].str();
				EffectTagValue etv;

				switch (tag_def->type) {
					case TagType::COLOR: {
						// 颜色标签：BGR 十六进制，如 "0000FF" → B=0,G=0,R=255
						// 对应 MoonScript: convertColorValue
						etv.type = EffectTagValue::COL;
						std::string hex = captured;
						// 补齐到6位
						while (hex.size() < 6) hex = "0" + hex;
						try {
							etv.color.b = std::stoi(hex.substr(0, 2), nullptr, 16);
							etv.color.g = std::stoi(hex.substr(2, 2), nullptr, 16);
							etv.color.r = std::stoi(hex.substr(4, 2), nullptr, 16);
						} catch (...) {
							etv.color = {0, 0, 0};
						}
						typed_effect_tags[tag_def->name].push_back(etv);
						break;
					}
					case TagType::ALPHA: {
						// 透明度标签：十六进制，如 "FF" → 255
						// 对应 MoonScript: convertHexValue
						etv.type = EffectTagValue::ALP;
						try {
							etv.alpha = std::stoi(captured, nullptr, 16);
						} catch (...) {
							etv.alpha = 0;
						}
						typed_effect_tags[tag_def->name].push_back(etv);
						// 同时存入 effect_tags 以保持兼容
						effect_tags[tag_def->name].push_back(etv.alpha);
						break;
					}
					case TagType::MULTI: {
						// 多值标签：逗号分隔的坐标值
						// 对应 MoonScript: convertMultiValue
						etv.type = EffectTagValue::MULTI;
						std::regex coord_re(R"([.\d\-]+)");
						std::sregex_iterator cit(captured.begin(), captured.end(), coord_re);
						std::sregex_iterator cend;
						while (cit != cend) {
							try {
								etv.multi_values.push_back(std::stod((*cit)[0].str()));
							} catch (...) {
								etv.multi_values.push_back(0);
							}
							++cit;
						}
						typed_effect_tags[tag_def->name].push_back(etv);
						break;
					}
					default: {
						// 数值标签
						etv.type = EffectTagValue::NUM;
						try {
							etv.number = std::stod(captured);
						} catch (...) {
							etv.number = 0;
						}
						effect_tags[tag_def->name].push_back(etv.number);
						typed_effect_tags[tag_def->name].push_back(etv);
						break;
					}
				}
				++it;
			}
		}
	}

/// 在指定时间点对 \t 变换进行插值计算
/// @param text 包含占位符的行文本
/// @param placeholder 本 Transform 对应的占位符字符串（如 __TRANSFORM_0__）
/// @param time 当前时间点（毫秒），用于计算插值进度
/// @param line_properties 行的样式默认属性值，作为插值的后备起始状态
/// @param prior_inline_tags 从行内联标签收集的先前状态值（优先于 line_properties）
/// @param res_x, res_y 脚本分辨率（留作 clip 等的坐标计算）
///
/// 插值公式：
///   linear_progress = (time - start_time) / (end_time - start_time)
///   progress = linear_progress ^ accel  （accel 控制曲线形状）
///   result = before * (1 - progress) + after * progress
/// 边界处理：progress <= 0 取起始值，>= 1 取结束值
///
/// 对应 MoonScript Transform.moon: collectPriorState + interpolate
/// prior_inline_tags 实现了 MoonScript 中 collectPriorState 的功能：
/// 先从行内联标签扫描实际当前值，再回退到样式默认值
	std::string Transform::interpolate(const std::string &text, const std::string &placeholder,
										int time, const std::map<std::string, double> &line_properties,
										const std::map<std::string, EffectTagValue> &prior_inline_tags,
										int res_x, int res_y) const {
		double linear_progress = (end_time > start_time)
									? static_cast<double>(time - start_time) / (end_time - start_time)
									: 0.0;
		double progress = std::pow(linear_progress, accel);

		std::string result = text;
		auto pos = result.find(placeholder);
		if (pos == std::string::npos) return result;

		std::string replacement;
		const auto &registry = TagRegistry::instance();

		for (const auto &[tag_name, end_values] : typed_effect_tags) {
			const TagDef *tag_def = registry.get(tag_name);
			if (!tag_def || end_values.empty()) continue;

			// 获取先前状态值：优先从内联标签获取，再从样式属性获取
			// 对应 MoonScript: collectPriorState 中的扫描逻辑
			// affectedBy 机制：如 \1a 受 \alpha 影响，先查 \1a，再查 alpha
			auto find_prior = [&](const std::string &name) -> const EffectTagValue * {
				auto it = prior_inline_tags.find(name);
				if (it != prior_inline_tags.end()) return &it->second;
				// 查询 affectedBy 链
				const TagDef *td = registry.get(name);
				if (td) {
					for (const auto &parent_name : td->affected_by) {
						auto pit = prior_inline_tags.find(parent_name);
						if (pit != prior_inline_tags.end()) return &pit->second;
					}
				}
				return nullptr;
			};

			switch (end_values.back().type) {
				case EffectTagValue::COL: {
					// 颜色插值（链式：遍历所有结束值，依次插值）
					// 对应 MoonScript: for endValue in *endValues → tag\interpolate value, endValue, progress
					ColorValue value{0, 0, 0};
					const EffectTagValue *prior = find_prior(tag_name);
					if (prior && prior->type == EffectTagValue::COL) {
						value = prior->color;
					}

					if (linear_progress <= 0) {
						replacement += tag_def->format_color(value);
					} else if (linear_progress >= 1) {
						replacement += tag_def->format_color(end_values.back().color);
					} else {
						for (const auto &end_val : end_values) {
							if (end_val.type == EffectTagValue::COL)
								value = interpolate_color(value, end_val.color, progress);
						}
						replacement += tag_def->format_color(value);
					}
					break;
				}
				case EffectTagValue::ALP: {
					// 透明度插值（链式遍历所有结束值）
					int value = 0;
					const EffectTagValue *prior = find_prior(tag_name);
					if (prior && prior->type == EffectTagValue::ALP) {
						value = prior->alpha;
					} else {
						auto prop_it = line_properties.find(tag_name);
						if (prop_it != line_properties.end()) {
							value = static_cast<int>(prop_it->second);
						}
					}

					if (linear_progress <= 0) {
						replacement += tag_def->format_alpha(value);
					} else if (linear_progress >= 1) {
						replacement += tag_def->format_alpha(end_values.back().alpha);
					} else {
						for (const auto &end_val : end_values) {
							if (end_val.type == EffectTagValue::ALP)
								value = static_cast<int>(interpolate_number(value, end_val.alpha, progress));
						}
						replacement += tag_def->format_alpha(value);
					}
					break;
				}
				case EffectTagValue::MULTI: {
					// 多值插值（链式遍历所有结束值）
					// 对应 MoonScript: interpolateMulti
					const auto &last_end = end_values.back();
					std::vector<double> value(last_end.multi_values.size(), 0);

					// rectClip/rectiClip 的默认先前值为 {0, 0, ResX, ResY}
					// 对应 MoonScript: collectPriorState 中的 {0, 0, PlayResX, PlayResY}
					if ((tag_name == "rectClip" || tag_name == "rectiClip") &&
						value.size() == 4) {
						value = {0, 0, static_cast<double>(res_x), static_cast<double>(res_y)};
					}

					const EffectTagValue *prior = find_prior(tag_name);
					if (prior && prior->type == EffectTagValue::MULTI &&
						prior->multi_values.size() == value.size()) {
						value = prior->multi_values;
					}

					if (linear_progress <= 0) {
						replacement += tag_def->format_multi(value);
					} else if (linear_progress >= 1) {
						replacement += tag_def->format_multi(last_end.multi_values);
					} else {
						for (const auto &end_val : end_values) {
							if (end_val.type == EffectTagValue::MULTI &&
								end_val.multi_values.size() == value.size()) {
								for (size_t i = 0; i < value.size(); ++i) {
									value[i] = interpolate_number(value[i], end_val.multi_values[i], progress);
								}
							}
						}
						replacement += tag_def->format_multi(value);
					}
					break;
				}
				default: {
					// 数值插值（链式遍历所有结束值）
					double value = 0;
					const EffectTagValue *prior = find_prior(tag_name);
					if (prior && prior->type == EffectTagValue::NUM) {
						value = prior->number;
					} else {
						auto prop_it = line_properties.find(tag_name);
						if (prop_it != line_properties.end()) {
							value = prop_it->second;
						}
					}

					// 根据标签定义选择整数或浮点格式
					// 对应 MoonScript: formatInt（如 \be）vs formatNumber
					auto format_value = [&](double v) {
						if (tag_def->is_integer)
							return tag_def->format_int(static_cast<int>(std::round(v)));
						return tag_def->format_float(v);
					};

					if (linear_progress <= 0) {
						replacement += format_value(value);
					} else if (linear_progress >= 1) {
						replacement += format_value(end_values.back().number);
					} else {
						for (const auto &end_val : end_values) {
							if (end_val.type == EffectTagValue::NUM)
								value = interpolate_number(value, end_val.number, progress);
						}
						replacement += format_value(value);
					}
					break;
				}
			}
		}

		result.replace(pos, placeholder.length(), replacement);
		return result;
	}

	double Transform::interpolate_number(double before, double after, double progress) {
		return (1.0 - progress) * before + progress * after;
	}

	ColorValue Transform::interpolate_color(const ColorValue &before, const ColorValue &after, double progress) {
		ColorValue result;
		result.b = static_cast<int>(interpolate_number(before.b, after.b, progress));
		result.g = static_cast<int>(interpolate_number(before.g, after.g, progress));
		result.r = static_cast<int>(interpolate_number(before.r, after.r, progress));
		return result;
	}

// ============================================================
// transform_utils 命名空间实现
// ============================================================

	namespace transform_utils {
		const std::string placeholder_pattern = R"(\\\x03(\d+)\\\x03)";

		std::string make_placeholder(int count) {
			// 对应 MoonScript: "\\\3#{count}\\\3"
			std::string result;
			result += "\\\x03";
			result += std::to_string(count);
			result += "\\\x03";
			return result;
		}

/// 将文本中的 \t 标签替换为占位符（标记化）
/// @param text 原始标签文本
/// @param transforms [输出] 收集到的所有 Transform 对象
/// @param line_duration 行持续时间，传给 Transform::from_string
/// @return 所有 \t 被替换为占位符后的文本
///
/// 这是 Issue #69 修复的核心机制之一：
///   标记化后，\t(\c&H0000FF&) 中的 \c 不再暴露在外层文本中，
///   后续的 deduplicate_tag("\\c") 不会误删该 \c。
/// 实现细节：
///   1. 从后向前替换（避免位置偏移）
///   2. 替换后反转 transforms 数组使其恢复出现顺序
///   3. 正则 R"(\\t(\([^()]*(?:\([^()]*\)[^()]*)*\)))" 支持一层嵌套括号
		std::string tokenize_transforms(const std::string &text,
										std::vector<Transform> &transforms, int line_duration) {
			transforms.clear();
			std::string result = text;

			// 匹配 \t 标签（需处理嵌套括号）
			// 对应 MoonScript: tags.allTags.transform.pattern = "\\t(%(.-%))"
			std::regex t_re(R"(\\t(\([^()]*(?:\([^()]*\)[^()]*)*\)))");
			std::smatch match;
			int count = 0;

			// 追踪标签块索引
			std::string working = result;
			std::string output;

			// 简化处理：遍历整个文本
			auto it = std::sregex_iterator(result.begin(), result.end(), t_re);
			auto end = std::sregex_iterator();

			// 收集所有匹配位置
			struct MatchInfo {
				size_t pos;
				size_t len;
				std::string content;
			};
			std::vector<MatchInfo> matches;

			for (; it != end; ++it) {
				matches.push_back(
					{
						static_cast<size_t>(it->position()),
						static_cast<size_t>(it->length()),
						(*it)[1].str()
					}
				);
			}

			// 从后向前替换（避免位置偏移）
			for (auto rit = matches.rbegin(); rit != matches.rend(); ++rit) {
				++count;
				std::string placeholder = make_placeholder(count);

				Transform transform = Transform::from_string(rit->content, line_duration, 0);
				transform.token = placeholder;
				transforms.push_back(transform);

				result.replace(rit->pos, rit->len, placeholder);
			}

			// 反转 transforms 使其按出现顺序排列
			std::reverse(transforms.begin(), transforms.end());

			return result;
		}

/// 将占位符还原为 \t 标签（反标记化），用于线性模式输出
/// @param text 包含占位符的文本
/// @param transforms 标记化时收集的 Transform 对象
/// @param time_shift 时间偏移量（毫秒），用于调整 \t 的时间参数
/// @return 还原后的文本，\t 标签的时间参数已减去 time_shift
		std::string detokenize_transforms(const std::string &text,
										const std::vector<Transform> &transforms, int time_shift, int line_duration) {
			std::string result = text;

			for (const auto &t : transforms) {
				auto pos = result.find(t.token);
				if (pos != std::string::npos) {
					Transform shifted = t;
					shifted.start_time -= time_shift;
					shifted.end_time -= time_shift;
					// 传递 line_duration 以抑制超出行持续时间的变换
					std::string replacement = shifted.to_string(line_duration);
					result.replace(pos, t.token.length(), replacement);
				}
			}

			return result;
		}

		std::string detokenize_transforms_copy(const std::string &text,
												const std::vector<Transform> &transforms, int time_shift, int line_duration) {
			// 与 detokenize_transforms 相同，但不修改原始 transforms
			return detokenize_transforms(text, transforms, time_shift, line_duration);
		}

/// 在指定时间点对所有 \t 占位符进行插值并输出结果，用于逐帧模式
/// @param text 包含占位符的文本
/// @param transforms 标记化时收集的 Transform 对象
/// @param time_shift 时间偏移量
/// @param time 当前帧的绝对时间（毫秒）
/// @param line_properties 行的当前标签属性，作为插值起始值
/// @param prior_inline_tags 从行内联标签收集的先前状态值
/// @param res_x, res_y 脚本分辨率
/// @return 所有占位符被替换为插值结果后的文本
///
/// 与 detokenize 的区别：detokenize 保留 \t 标签结构，
/// 而 interpolate 直接计算出该时间点的标签值
		std::string interpolate_transforms_copy(const std::string &text,
												const std::vector<Transform> &transforms, int time_shift,
												int time, const std::map<std::string, double> &line_properties,
												const std::map<std::string, Transform::EffectTagValue> &prior_inline_tags,
												int res_x, int res_y) {
			std::string result = text;

			for (const auto &t : transforms) {
				Transform shifted = t;
				shifted.start_time -= time_shift;
				shifted.end_time -= time_shift;
				result = shifted.interpolate(result, t.token, time, line_properties, prior_inline_tags, res_x, res_y);
			}

			return result;
		}
	} // namespace transform_utils
} // namespace mocha
