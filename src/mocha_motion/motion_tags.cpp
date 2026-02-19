// Copyright (c) 2024-2026, Aegisub contributors
// ASS 标签定义和处理实现
// 对应 MoonScript 版 a-mo.Tags

#include "motion_tags.h"
#include "motion_math.h"

#include <algorithm>
#include <regex>
#include <cstdio>

namespace mocha {
// ============================================================
// TagDef 格式化方法
// ============================================================

	std::string TagDef::format_int(int value) const {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%s%d", tag.c_str(), value);
		return buf;
	}

	std::string TagDef::format_float(double value) const {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%s%g", tag.c_str(), value);
		return buf;
	}

	std::string TagDef::format_alpha(int value) const {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%s&H%02X&", tag.c_str(), value & 0xFF);
		return buf;
	}

	std::string TagDef::format_color(const ColorValue &color) const {
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%s&H%02X%02X%02X&", tag.c_str(), color.b & 0xFF, color.g & 0xFF, color.r & 0xFF);
		return buf;
	}

	std::string TagDef::format_multi(const std::vector<double> &values) const {
		// 对应 MoonScript: table.concat value, ','
		// 使用 %g 格式以匹配 MoonScript 的数值输出精度
		std::string result = tag + "(";
		char buf[64];
		for (size_t i = 0; i < values.size(); ++i) {
			if (i > 0) result += ",";
			std::snprintf(buf, sizeof(buf), "%g", values[i]);
			result += buf;
		}
		result += ")";
		return result;
	}

	std::string TagDef::format_string(const std::string &value) const {
		return tag + value;
	}

// ============================================================
// TagRegistry 单例和标签注册
// ============================================================

	TagRegistry &TagRegistry::instance() {
		static TagRegistry registry;
		return registry;
	}

	TagRegistry::TagRegistry() {
		register_tags();
	}

	const TagDef *TagRegistry::get(const std::string &name) const {
		auto it = all_tags_.find(name);
		return (it != all_tags_.end()) ? &it->second : nullptr;
	}

	void TagRegistry::register_tags() {
		// 注册所有 ASS 标签定义
		// 对应 MoonScript Tags.moon 中的 allTags 表

		// --- 字体相关 ---
		all_tags_["fontName"] = {"fontName", R"(\\fn([^\\}]+))", "\\fn", false, false, "fontname", TagType::STRING};
		all_tags_["fontSize"] = {"fontSize", R"(\\fs(\d+))", "\\fs", true, false, "fontsize"};
		all_tags_["fontSp"] = {"fontSp", R"(\\fsp([.\d\-]+))", "\\fsp", true, false, "spacing"};

		// --- 缩放 ---
		all_tags_["xscale"] = {"xscale", R"(\\fscx([\d.]+))", "\\fscx", true, false, "scale_x"};
		all_tags_["yscale"] = {"yscale", R"(\\fscy([\d.]+))", "\\fscy", true, false, "scale_y"};

		// --- 旋转 ---
		all_tags_["zrot"] = {"zrot", R"(\\frz?([\-\d.]+))", "\\frz", true, false, "angle"};
		all_tags_["xrot"] = {"xrot", R"(\\frx([\-\d.]+))", "\\frx", true, false, ""};
		all_tags_["yrot"] = {"yrot", R"(\\fry([\-\d.]+))", "\\fry", true, false, ""};

		// --- 边框 ---
		all_tags_["border"] = {"border", R"(\\bord([\d.]+))", "\\bord", true, false, "outline"};
		all_tags_["xborder"] = {"xborder", R"(\\xbord([\d.]+))", "\\xbord", true, false, ""};
		all_tags_["yborder"] = {"yborder", R"(\\ybord([\d.]+))", "\\ybord", true, false, ""};

		// --- 阴影 ---
		all_tags_["shadow"] = {"shadow", R"(\\shad([\-\d.]+))", "\\shad", true, false, "shadow"};
		all_tags_["xshadow"] = {"xshadow", R"(\\xshad([\-\d.]+))", "\\xshad", true, false, ""};
		all_tags_["yshadow"] = {"yshadow", R"(\\yshad([\-\d.]+))", "\\yshad", true, false, ""};

		// --- 重置 ---
		// 双重防御机制防止第三方扩展标签被误识别：
		//   1. 负向前瞻 (?!nd[sxyz\d])：静态排除已知 VSFilterMod \rnd 系列扩展
		//   2. motion_processor.cpp step 8 运行时校验样式名是否存在于样式集合
		// 此处保留正则级别的过滤，与 step 8 的运行时校验形成互补。
		all_tags_["reset"] = {"reset", R"(\\r(?!nd[sxyz\d])([^\\}]*))", "\\r", false, false, "", TagType::STRING};

		// --- Alpha ---
		// 对应 MoonScript: type: "alpha"
		all_tags_["alpha"] = {"alpha", R"(\\alpha&H([0-9A-Fa-f]{2})&)", "\\alpha", true, false, "", TagType::ALPHA};
		// alpha1-4 的 affectedBy 列表在后面设置
		all_tags_["alpha1"] = {"alpha1", R"(\\1a&H([0-9A-Fa-f]{2})&)", "\\1a", true, false, "color1", TagType::ALPHA};
		all_tags_["alpha2"] = {"alpha2", R"(\\2a&H([0-9A-Fa-f]{2})&)", "\\2a", true, false, "color2", TagType::ALPHA};
		all_tags_["alpha3"] = {"alpha3", R"(\\3a&H([0-9A-Fa-f]{2})&)", "\\3a", true, false, "color3", TagType::ALPHA};
		all_tags_["alpha4"] = {"alpha4", R"(\\4a&H([0-9A-Fa-f]{2})&)", "\\4a", true, false, "color4", TagType::ALPHA};

		// 设置 affectedBy：\alpha 影响 \1a-\4a
		// 对应 MoonScript: affectedBy: { "alpha" }
		all_tags_["alpha1"].affected_by = {"alpha"};
		all_tags_["alpha2"].affected_by = {"alpha"};
		all_tags_["alpha3"].affected_by = {"alpha"};
		all_tags_["alpha4"].affected_by = {"alpha"};

		// --- 颜色 ---
		// 对应 MoonScript: type: "color"，使用 convertColorValue 解析 BGR 十六进制
		all_tags_["color1"] = {"color1", R"(\\1?c&H([0-9A-Fa-f]+)&)", "\\1c", true, false, "color1", TagType::COLOR};
		all_tags_["color2"] = {"color2", R"(\\2c&H([0-9A-Fa-f]+)&)", "\\2c", true, false, "color2", TagType::COLOR};
		all_tags_["color3"] = {"color3", R"(\\3c&H([0-9A-Fa-f]+)&)", "\\3c", true, false, "color3", TagType::COLOR};
		all_tags_["color4"] = {"color4", R"(\\4c&H([0-9A-Fa-f]+)&)", "\\4c", true, false, "color4", TagType::COLOR};

		// --- 模糊 ---
		// \be 使用整数格式（对应 MoonScript: formatInt）
		all_tags_["be"] = {"be", R"(\\be([\d.]+))", "\\be", true, false, "", TagType::NUMBER, {}, {}, true};
		all_tags_["blur"] = {"blur", R"(\\blur([\d.]+))", "\\blur", true, false, ""};

		// --- 变形 ---
		all_tags_["xshear"] = {"xshear", R"(\\fax([\-\d.]+))", "\\fax", true, false, ""};
		all_tags_["yshear"] = {"yshear", R"(\\fay([\-\d.]+))", "\\fay", true, false, ""};

		// --- 其他属性 ---
		all_tags_["align"] = {"align", R"(\\an([1-9]))", "\\an", false, true, "align"};
		all_tags_["bold"] = {"bold", R"(\\b(\d+))", "\\b", false, false, "bold"};
		all_tags_["underline"] = {"underline", R"(\\u([01]))", "\\u", false, false, "underline"};
		all_tags_["italic"] = {"italic", R"(\\i([01]))", "\\i", false, false, "italic"};
		all_tags_["strike"] = {"strike", R"(\\s([01]))", "\\s", false, false, "strikeout"};
		all_tags_["drawing"] = {"drawing", R"(\\p(\d+))", "\\p", false, false, ""};

		// ==================================================================
		// VSFilterMod 扩展标签
		// 参考文档：docs/AssRocket-VSFilterMod-使用文档.md
		// 注册这些第三方渲染器扩展标签，确保在去重和 \r 重置处理中
		// 不会被误匹配或误处理。标签本身不参与运动变换。
		// ==================================================================

		// --- 随机偏移系列 (patch m003) ---
		// 以 \rnd 开头，必须注册以防止被 \r 重置模式误匹配
		all_tags_["rnd"] = {"rnd", R"(\\rnd(\d+))", "\\rnd", false, false, ""};
		all_tags_["rndx"] = {"rndx", R"(\\rndx([\d.]+))", "\\rndx", false, false, ""};
		all_tags_["rndy"] = {"rndy", R"(\\rndy([\d.]+))", "\\rndy", false, false, ""};
		all_tags_["rndz"] = {"rndz", R"(\\rndz([\d.]+))", "\\rndz", false, false, ""};
		all_tags_["rnds"] = {"rnds", R"(\\rnds&H([0-9A-Fa-f]+)&)", "\\rnds", false, false, "", TagType::STRING};

		// --- 渐变颜色 (patch m004) ---
		// \1vc-\4vc：四角渐变颜色，与 \1c-\4c 冲突（不可同时使用）
		all_tags_["grad_color1"] = {"grad_color1", R"(\\1vc\(([^)]+)\))", "\\1vc", false, false, "", TagType::STRING};
		all_tags_["grad_color2"] = {"grad_color2", R"(\\2vc\(([^)]+)\))", "\\2vc", false, false, "", TagType::STRING};
		all_tags_["grad_color3"] = {"grad_color3", R"(\\3vc\(([^)]+)\))", "\\3vc", false, false, "", TagType::STRING};
		all_tags_["grad_color4"] = {"grad_color4", R"(\\4vc\(([^)]+)\))", "\\4vc", false, false, "", TagType::STRING};

		// --- 渐变透明度 (patch m004) ---
		// \1va-\4va：四角渐变透明度，与 \1a-\4a 及 \alpha 冲突
		all_tags_["grad_alpha1"] = {"grad_alpha1", R"(\\1va\(([^)]+)\))", "\\1va", false, false, "", TagType::STRING};
		all_tags_["grad_alpha2"] = {"grad_alpha2", R"(\\2va\(([^)]+)\))", "\\2va", false, false, "", TagType::STRING};
		all_tags_["grad_alpha3"] = {"grad_alpha3", R"(\\3va\(([^)]+)\))", "\\3va", false, false, "", TagType::STRING};
		all_tags_["grad_alpha4"] = {"grad_alpha4", R"(\\4va\(([^)]+)\))", "\\4va", false, false, "", TagType::STRING};

		// --- PNG 图像纹理 (patch m010) ---
		all_tags_["img1"] = {"img1", R"(\\1img\(([^)]+)\))", "\\1img", false, false, "", TagType::STRING};
		all_tags_["img2"] = {"img2", R"(\\2img\(([^)]+)\))", "\\2img", false, false, "", TagType::STRING};
		all_tags_["img3"] = {"img3", R"(\\3img\(([^)]+)\))", "\\3img", false, false, "", TagType::STRING};
		all_tags_["img4"] = {"img4", R"(\\4img\(([^)]+)\))", "\\4img", false, false, "", TagType::STRING};
		all_tags_["img5"] = {"img5", R"(\\5img\(([^)]+)\))", "\\5img", false, false, "", TagType::STRING};
		all_tags_["img6"] = {"img6", R"(\\6img\(([^)]+)\))", "\\6img", false, false, "", TagType::STRING};
		all_tags_["img7"] = {"img7", R"(\\7img\(([^)]+)\))", "\\7img", false, false, "", TagType::STRING};

		// --- Z 轴与 3D (patch m002, vpatch v001) ---
		all_tags_["zdepth"] = {"zdepth", R"(\\z([\-\d.]+))", "\\z", false, false, ""};
		all_tags_["ortho"] = {"ortho", R"(\\ortho([01]))", "\\ortho", false, false, ""};

		// --- 符号旋转 (patch m007) ---
		all_tags_["frs"] = {"frs", R"(\\frs([\-\d.]+))", "\\frs", false, false, ""};

		// --- 统一缩放 ---
		// \fsc 同时设置 \fscx 和 \fscy，不会被 \fscx/\fscy 模式误匹配
		all_tags_["fsc"] = {"fsc", R"(\\fsc([\d.]+))", "\\fsc", false, false, ""};

		// --- 混合模式 (vpatch v003) ---
		// 参数可为数字（0-8）或关键字（normal, add, mult 等）
		all_tags_["blend"] = {"blend", R"(\\blend(\w+))", "\\blend", false, false, "", TagType::STRING};

		// --- 扩展间距 (patch m001, vpatch v002) ---
		all_tags_["fsvp"] = {"fsvp", R"(\\fsvp([\-\d.]+))", "\\fsvp", false, false, ""};
		all_tags_["fshp"] = {"fshp", R"(\\fshp([\-\d.]+))", "\\fshp", false, false, ""};

		// --- 扩展移动 (patch m005) ---
		all_tags_["mover"] = {"mover", R"(\\mover\(([^)]+)\))", "\\mover", false, true, "", TagType::STRING};
		all_tags_["moves3"] = {"moves3", R"(\\moves3\(([^)]+)\))", "\\moves3", false, true, "", TagType::STRING};
		all_tags_["moves4"] = {"moves4", R"(\\moves4\(([^)]+)\))", "\\moves4", false, true, "", TagType::STRING};

		// --- 可移动矢量裁剪 (patch m006) ---
		all_tags_["movevc"] = {"movevc", R"(\\movevc\(([^)]+)\))", "\\movevc", false, true, "", TagType::STRING};

		// --- 扭曲变形 (patch m008) ---
		all_tags_["distort"] = {"distort", R"(\\distort\(([^)]+)\))", "\\distort", false, true, "", TagType::STRING};

		// --- 抖动效果 (patch m011) ---
		all_tags_["jitter"] = {"jitter", R"(\\jitter\(([^)]+)\))", "\\jitter", false, true, "", TagType::STRING};

		// --- 卡拉 OK 标签 ---
		// 对应 MoonScript: karaoke: { pattern: "(\\[kK][fo]?)(%d+)", convert: convertKaraoke }
		// 匹配 \k, \K, \kf, \ko 等变体。不可变换，不是全局标签
		// 注：shift_karaoke() 使用独立的硬编码正则处理卡拉 OK 偏移，
		//     此注册仅用于 TagRegistry 完整性和 repeat_tags 去重
		all_tags_["karaoke"] = {"karaoke", R"(\\[kK][fo]?(\d+))", "\\k", false, false, ""};

		// --- 变换标签 ---
		all_tags_["transform"] = {"transform", R"(\\t(\(.*?\)))", "\\t", false, false, "", TagType::TRANSFORM};

		// --- 全局标签（位置相关） ---
		// 对应 MoonScript: output: "multi"
		all_tags_["pos"] = {"pos", R"(\\pos\(([.\d\-]+,[.\d\-]+)\))", "\\pos", false, true, "", TagType::MULTI, {}, {"x", "y"}};
		all_tags_["org"] = {"org", R"(\\org\(([.\d\-]+,[.\d\-]+)\))", "\\org", false, true, "", TagType::MULTI, {}, {"x", "y"}};
		all_tags_["fad"] = {"fad", R"(\\fade?\((\d+,\d+)\))", "\\fad", false, true, "", TagType::MULTI, {}, {"in", "out"}};
		all_tags_["move"] = {"move", R"(\\move\(([.\d\-]+,[.\d\-]+,[.\d\-]+,[.\d\-]+,[\d\-]+,[\d\-]+)\))", "\\move", false, true, "", TagType::MULTI, {}, {"x1", "y1", "x2", "y2", "start", "end"}};
		all_tags_["fade"] = {"fade", R"(\\fade\((\d+,\d+,\d+,[\d\-]+,[\d\-]+,[\d\-]+,[\d\-]+)\))", "\\fade", false, true, "", TagType::MULTI, {}, {"a1", "a2", "a3", "t1", "t2", "t3", "t4"}};

		// --- Clip ---
		// rectClip 是可变换的多值标签
		all_tags_["rectClip"] = {"rectClip", R"(\\clip\(([\-\d.]+,[\-\d.]+,[\-\d.]+,[\-\d.]+)\))", "\\clip", true, true, "", TagType::MULTI, {}, {"xLeft", "yTop", "xRight", "yBottom"}};
		all_tags_["rectiClip"] = {"rectiClip", R"(\\iclip\(([\-\d.]+,[\-\d.]+,[\-\d.]+,[\-\d.]+)\))", "\\iclip", true, true, "", TagType::MULTI, {}, {"xLeft", "yTop", "xRight", "yBottom"}};
		all_tags_["vectClip"] = {"vectClip", R"(\\clip\((\d+,)?([^,]*?)\))", "\\clip", false, true, "", TagType::MULTI, {}, {"scale", "shape"}};
		all_tags_["vectiClip"] = {"vectiClip", R"(\\iclip\((\d+,)?([^,]*?)\))", "\\iclip", false, true, "", TagType::MULTI, {}, {"scale", "shape"}};

		// 分类标签
		for (auto &[name, tag_def] : all_tags_) {
			if (tag_def.global) {
				one_time_tags_.push_back(&tag_def);
			} else {
				repeat_tags_.push_back(&tag_def);
			}

			if (!tag_def.style_field.empty()) {
				style_tags_.push_back(&tag_def);
			}

			if (tag_def.transformable) {
				transform_tags_.push_back(&tag_def);
			}
		}
	}

// ============================================================
// tag_utils 命名空间实现
// ============================================================

	namespace tag_utils {
		std::string find_tag_value(const std::string &text, const std::string &pattern) {
			std::regex re(pattern);
			std::smatch match;
			if (std::regex_search(text, match, re) && match.size() > 1) {
				return match[1].str();
			}
			return "";
		}

		std::string replace_tag(const std::string &text, const std::string &pattern, const std::string &replacement) {
			return std::regex_replace(text, std::regex(pattern), replacement);
		}

		std::string remove_tag(const std::string &text, const std::string &pattern) {
			return std::regex_replace(text, std::regex(pattern), "");
		}

		int count_tag(const std::string &text, const std::string &pattern) {
			std::regex re(pattern);
			auto begin = std::sregex_iterator(text.begin(), text.end(), re);
			auto end = std::sregex_iterator();
			return static_cast<int>(std::distance(begin, end));
		}

/// @brief 去重同一标签块内的重复标签（保留最后一个）
///
/// 对应 MoonScript deduplicateTag()。
/// 在运动处理管线中，回调函数可能多次为同一标签块添加值，
/// 导致同一标签出现多次。此函数保留最后一次出现的值（最新值）。
///
/// 与 Issue #69 的关系：
///   本函数只处理已经提取了 \t 标签的文本，因此不会误删
///   \t(\c) 内部的颜色标签。参见 extract_transforms() 的文档。
///
/// @param tag_block 单个 ASS 标签块（如 "{\pos(1,2)\fscx100\fscx200}"）
/// @param pattern 匹配目标标签的正则表达式
/// @return 去重后的标签块
		std::string deduplicate_tag(const std::string &tag_block, const std::string &pattern) {
			std::regex re(pattern);
			int count = count_tag(tag_block, pattern);
			if (count <= 1) return tag_block;

			// 保留最后一个，移除前面的（对应 MoonScript 的 gsub pattern, "", num - 1）
			std::string result = tag_block;
			int removed = 0;
			int to_remove = count - 1;

			while (removed < to_remove) {
				std::smatch match;
				if (std::regex_search(result, match, re)) {
					result = match.prefix().str() + match.suffix().str();
					++removed;
				} else {
					break;
				}
			}
			return result;
		}

/// @brief 提取并移除文本中的所有 \t 变换标签
///
/// 这是 Issue #69 修复的关键函数。
///
/// 问题背景 (Issue #69)：
///   当字幕行同时存在 \c（颜色标签）和 \t(\c)（变换中的颜色标签）时，
///   如果不先将 \t 标签整体提取出来，deduplicate_tag() 在去重 \c 时
///   会错误地将 \t(\c) 内部的 \c 也作为独立标签处理，导致外层的 \c
///   被移除（因为去重保留最后一个出现的）。
///
/// 解决方案：
///   1. 先用本函数将所有 \t 标签完整提取出来（保存到 t_data_list）
///   2. 用占位符替换 \t 标签位置（tokenize_transforms）
///   3. 在清理后的文本上安全执行 deduplicate_tag()
///   4. 最后将 \t 标签还原回去（detokenize_transforms）
///
/// 正则表达式设计：
///   - 使用 (?:[^,()]|\([^)]*\)) 模式匹配参数，可正确处理嵌套括号
///   - 先尝试4参数格式 \t(t1,t2,accel,effect)，再尝试3参数格式 \t(t1,t2,effect)
///   - 通过 raw_string 比较避免3参数格式重复匹配已被4参数格式捕获的标签
///
/// @param text 原始 ASS 行文本
/// @param t_data_list 输出：提取到的变换数据列表
/// @return 移除 \t 标签后的文本
		std::string extract_transforms(const std::string &text, std::vector<TransformData> &t_data_list) {
			t_data_list.clear();

			// 匹配 \t(t1,t2,accel,effect) 或 \t(t1,t2,effect)
			// 使用精确的正则表达式处理嵌套括号（对应 issue_69 修复）
			const std::regex t_regex_4(
				R"(\\t\(((?:[^,()]|\([^)]*\))+?),((?:[^,()]|\([^)]*\))+?),((?:[^,()]|\([^)]*\))+?),((?:[^()]|\([^)]*\))*)\))"
			);
			const std::regex t_regex_3(
				R"(\\t\(((?:[^,()]|\([^)]*\))+?),((?:[^,()]|\([^)]*\))+?),((?:[^()]|\([^)]*\))*)\))"
			);

			std::string result = text;
			std::smatch match;

			// 先尝试4参数格式
			std::string temp = result;
			while (std::regex_search(temp, match, t_regex_4)) {
				try {
					TransformData td;
					td.t1 = std::stoi(match[1].str());
					td.t2 = std::stoi(match[2].str());
					td.accel = std::stod(match[3].str());
					td.effect = match[4].str();
					td.raw_string = match[0].str();
					t_data_list.push_back(td);
				} catch (...) {
					// 解析失败则跳过
				}
				temp = match.suffix().str();
			}

			// 再尝试3参数格式（不重复已匹配的）
			temp = result;
			while (std::regex_search(temp, match, t_regex_3)) {
				try {
					TransformData td;
					td.t1 = std::stoi(match[1].str());
					td.t2 = std::stoi(match[2].str());
					td.accel = 1.0;
					td.effect = match[3].str();
					td.raw_string = match[0].str();

					// 检查是否已被4参数格式匹配
					bool already_matched = false;
					for (const auto &existing : t_data_list) {
						if (existing.raw_string == td.raw_string) {
							already_matched = true;
							break;
						}
					}
					if (!already_matched) {
						t_data_list.push_back(td);
					}
				} catch (...) {
					// 解析失败则跳过
				}
				temp = match.suffix().str();
			}

			// 移除所有 \t 标签
			result = std::regex_replace(result, t_regex_4, "");
			result = std::regex_replace(result, t_regex_3, "");

			return result;
		}

		std::string extract_move(const std::string &text, std::optional<MoveData> &move_data) {
			const std::regex move_regex(
				R"(\\move\(\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*,\s*([-.0-9]+)\s*\))"
			);
			std::smatch match;
			std::string result = text;

			if (std::regex_search(text, match, move_regex)) {
				move_data = MoveData{
					std::stod(match[1]), std::stod(match[2]),
					std::stod(match[3]), std::stod(match[4]),
					std::stoi(match[5]), std::stoi(match[6])
				};
				result = std::regex_replace(result, move_regex, "");
			}

			return result;
		}

		std::string extract_fade(const std::string &text,
								std::optional<FadeData> &fade_data,
								std::optional<FullFadeData> &full_fade_data) {
			std::string result = text;

			// 先尝试完整 \fade(a1,a2,a3,t1,t2,t3,t4)
			const std::regex fade7_regex(
				R"(\\fade\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*([\d\-]+)\s*,\s*([\d\-]+)\s*,\s*([\d\-]+)\s*,\s*([\d\-]+)\s*\))"
			);
			std::smatch match;
			if (std::regex_search(text, match, fade7_regex)) {
				full_fade_data = FullFadeData{
					std::stoi(match[1]), std::stoi(match[2]), std::stoi(match[3]),
					std::stoi(match[4]), std::stoi(match[5]), std::stoi(match[6]), std::stoi(match[7])
				};
				result = std::regex_replace(result, fade7_regex, "");
				return result;
			}

			// 尝试简单 \fad(in,out) 或 \fade(in,out)
			const std::regex fad_regex(R"(\\fad(?:e)?\(\s*(\d+)\s*,\s*(\d+)\s*\))");
			if (std::regex_search(text, match, fad_regex)) {
				fade_data = FadeData{std::stoi(match[1]), std::stoi(match[2])};
				result = std::regex_replace(result, fad_regex, "");
			}

			return result;
		}

		std::string clean_empty_blocks(const std::string &text) {
			return std::regex_replace(text, std::regex(R"(\{\})"), "");
		}

		std::string merge_adjacent_blocks(const std::string &text) {
			// 合并 }{ 为连续的标签块（用分隔符替换后再还原）
			return std::regex_replace(text, std::regex(R"(\}\{)"), "");
		}

		std::string run_callback_on_overrides(const std::string &text, const std::function<std::string(const std::string &, int)> &callback) {
			// 匹配所有标签块 {xxx}
			std::string result;
			std::regex override_re(R"(\{[^}]*\})");
			int major = 0;
			std::sregex_iterator it(text.begin(), text.end(), override_re);
			std::sregex_iterator end;
			size_t last_pos = 0;

			while (it != end) {
				const std::smatch &match = *it;
				// 添加标签块之前的文本
				result += text.substr(last_pos, match.position() - last_pos);

				// 对标签块执行回调
				++major;
				std::string block_content = match.str();
				std::string processed = callback(block_content, major);
				result += processed;

				last_pos = match.position() + match.length();
				++it;
			}
			// 添加剩余文本
			result += text.substr(last_pos);
			return result;
		}

		std::string run_callback_on_first_override(const std::string &text, const std::function<std::string(const std::string &)> &callback) {
			// 仅处理第一个标签块
			std::regex first_re(R"(^\{[^}]*\})");
			std::smatch match;
			if (std::regex_search(text, match, first_re)) {
				std::string processed = callback(match.str());
				return processed + match.suffix().str();
			}
			return text;
		}

		std::string ensure_leading_override(const std::string &text) {
			if (text.empty() || text[0] != '{') {
				return "{}" + text;
			}
			return text;
		}

		std::string rect_clip_to_vect_clip(const std::string &clip) {
			// 将矩形 clip 坐标转换为矢量 clip 的绘图命令
			std::regex rect_re(R"(([\-\d.]+)\s*,\s*([\-\d.]+)\s*,\s*([\-\d.]+)\s*,\s*([\-\d.]+))");
			std::smatch match;
			if (std::regex_search(clip, match, rect_re)) {
				double l = std::stod(match[1]);
				double t = std::stod(match[2]);
				double r = std::stod(match[3]);
				double b = std::stod(match[4]);

				char buf[256];
				std::snprintf(buf, sizeof(buf), "m %g %g l %g %g %g %g %g %g", l, t, r, t, r, b, l, b);
				return buf;
			}
			return clip;
		}

		std::string convert_clip_to_fp(const std::string &clip) {
			// 将带缩放因子的矢量 clip 转换为浮点坐标
			// 仅处理矢量 clip（不包含逗号分隔的矩形坐标）
			std::regex rect_check(R"([\-\d.]+\s*,\s*[\-\d.]+)");
			if (std::regex_search(clip, rect_check)) {
				return clip; // 矩形 clip，不处理
			}

			// 匹配 (\d,shape) 格式的缩放因子
			std::regex scale_re(R"(\((\d+),([^)]+)\))");
			std::smatch match;
			if (std::regex_search(clip, match, scale_re)) {
				int scale_factor = std::stoi(match[1]);
				std::string points = match[2];
				double divisor = std::pow(2.0, scale_factor - 1);

				// 替换所有坐标
				std::regex coord_re(R"(([.\d\-]+)\s+([.\d\-]+))");
				std::string result;
				std::sregex_iterator it(points.begin(), points.end(), coord_re);
				std::sregex_iterator end;
				size_t last = 0;

				while (it != end) {
					const auto &m = *it;
					result += points.substr(last, m.position() - last);
					double x = math::round(std::stod(m[1]) / divisor, 2);
					double y = math::round(std::stod(m[2]) / divisor, 2);
					char buf[64];
					std::snprintf(buf, sizeof(buf), "%g %g", x, y);
					result += buf;
					last = m.position() + m.length();
					++it;
				}
				result += points.substr(last);
				return "(" + result + ")";
			}

			return clip;
		}
	} // namespace tag_utils
} // namespace mocha
