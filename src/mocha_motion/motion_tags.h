// Copyright (c) 2024-2026, Aegisub contributors
// ASS 标签定义和处理声明
// 对应 MoonScript 版 a-mo.Tags

#pragma once

#include "motion_common.h"

#include <string>
#include <vector>
#include <regex>
#include <functional>
#include <map>

namespace mocha {
	/// ASS 标签类型
	enum class TagType {
		NUMBER, // 数值类型（大多数标签）
		STRING, // 字符串类型（如 \fn, \r）
		ALPHA, // 透明度类型（\alpha, \1a-\4a），十六进制值
		COLOR, // 颜色类型（\1c-\4c），BGR 十六进制值
		MULTI, // 多值类型（\pos, \clip 等坐标值）
		TRANSFORM // 变换类型（\t）
	};

	/// ASS 标签定义
	struct TagDef {
		std::string name; // 标签名称标识
		std::string pattern; // 匹配正则表达式
		std::string tag; // ASS 标签前缀
		bool transformable = false; // 是否可用于 \t 内部
		bool global = false; // 是否只能出现一次（如 \pos, \an 等）
		std::string style_field; // 对应的样式字段名
		TagType type = TagType::NUMBER; // 标签值类型
		std::vector<std::string> affected_by; // 受影响标签列表（如 \alpha 影响 \1a-\4a）
		std::vector<std::string> field_names; // 多值标签的字段名列表
		bool is_integer = false; // 是否使用整数格式输出（如 \be）

		/// 预编译的正则表达式对象（由 TagRegistry 初始化时统一编译）
		/// 避免运行时每次使用 std::regex(pattern) 反复编译
		std::regex compiled_pattern;

		/// 格式化标签值为字符串
		[[nodiscard]] std::string format_int(int value) const;

		[[nodiscard]] std::string format_float(double value) const;

		[[nodiscard]] std::string format_alpha(int value) const;

		[[nodiscard]] std::string format_color(const ColorValue &color) const;

		[[nodiscard]] std::string format_multi(const std::vector<double> &values) const;

		[[nodiscard]] std::string format_string(const std::string &value) const;
	};

/// 标签注册表
	class TagRegistry {
	public:
		static TagRegistry &instance();

		/// 获取所有标签定义
		[[nodiscard]] const std::map<std::string, TagDef> &all_tags() const { return all_tags_; }

		/// 获取可重复标签列表
		[[nodiscard]] const std::vector<const TagDef *> &repeat_tags() const { return repeat_tags_; }

		/// 获取全局标签列表
		[[nodiscard]] const std::vector<const TagDef *> &one_time_tags() const { return one_time_tags_; }

		/// 获取样式相关标签列表
		[[nodiscard]] const std::vector<const TagDef *> &style_tags() const { return style_tags_; }

		/// 获取可变换标签列表
		[[nodiscard]] const std::vector<const TagDef *> &transform_tags() const { return transform_tags_; }

		/// 根据名称获取标签定义
		[[nodiscard]] const TagDef *get(const std::string &name) const;

	private:
		TagRegistry();

		void register_tags();

		std::map<std::string, TagDef> all_tags_;
		std::vector<const TagDef *> repeat_tags_;
		std::vector<const TagDef *> one_time_tags_;
		std::vector<const TagDef *> style_tags_;
		std::vector<const TagDef *> transform_tags_;
	};

	/// ASS 标签处理工具
	namespace tag_utils {
		/// 从标签块中提取标签值
		/// @param text 标签块文本
		/// @param pattern 标签的正则表达式
		/// @return 匹配到的值字符串，未找到返回空
		std::string find_tag_value(const std::string &text, const std::string &pattern);

		/// 替换标签块中指定标签的值
		/// @param text 标签块文本
		/// @param pattern 标签的正则表达式
		/// @param replacement 替换文本
		/// @return 替换后的文本
		std::string replace_tag(const std::string &text, const std::string &pattern, const std::string &replacement);

		/// 移除标签块中指定标签
		/// @param text 标签块文本
		/// @param pattern 标签的正则表达式
		/// @return 移除后的文本
		std::string remove_tag(const std::string &text, const std::string &pattern);

		/// 统计标签出现次数
		/// @param text 标签块文本
		/// @param pattern 标签的正则表达式
		/// @return 出现次数
		int count_tag(const std::string &text, const std::string &pattern);

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
		std::string deduplicate_tag(const std::string &tag_block, const std::string &pattern);

		/// 去除重复标签，保留最后一个（使用预编译正则）
		/// @param tag_block 标签块文本
		/// @param re 预编译的正则表达式
		/// @return 去重后的文本
		std::string deduplicate_tag(const std::string &tag_block, const std::regex &re);

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
		std::string extract_transforms(const std::string &text, std::vector<TransformData> &t_data_list);

		/// 提取 \move 标签
		/// @param text 原始行文本
		/// @param move_data 用于存储提取的 \move 数据
		/// @return 移除 \move 后的文本
		std::string extract_move(const std::string &text, std::optional<MoveData> &move_data);

		/// 提取 \fad / \fade 标签
		/// @param text 原始行文本
		/// @param fade_data 用于存储提取的 \fad 数据
		/// @param full_fade_data 用于存储提取的完整 \fade 数据
		/// @return 移除淡入淡出标签后的文本
		std::string extract_fade(const std::string &text,
								std::optional<FadeData> &fade_data,
								std::optional<FullFadeData> &full_fade_data);

		/// 清理空的标签块 {}
		std::string clean_empty_blocks(const std::string &text);

		/// 清理空的 \clip() / \iclip() 占位标签
		std::string clean_empty_clips(const std::string &text);

		/// 对标签块执行回调操作
		/// @param text 完整行文本
		/// @param callback 回调函数，参数为标签块内容，返回修改后的内容
		/// @return 处理后的完整文本
		std::string run_callback_on_overrides(const std::string &text, const std::function<std::string(const std::string &, int)> &callback);

		/// 对首个标签块执行回调操作
		std::string run_callback_on_first_override(const std::string &text, const std::function<std::string(const std::string &)> &callback);

		/// 确保行文本以标签块开头
		std::string ensure_leading_override(const std::string &text);

		/// 将矩形 clip 转换为矢量 clip
		std::string rect_clip_to_vect_clip(const std::string &clip);

		/// 将带缩放因子的矢量 clip 转换为浮点坐标
		std::string convert_clip_to_fp(const std::string &clip);
	} // namespace tag_utils
} // namespace mocha
