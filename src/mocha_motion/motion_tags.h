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
	NUMBER,     // 数值类型（大多数标签）
	STRING,     // 字符串类型（如 \fn, \r）
	ALPHA,      // 透明度类型（\alpha, \1a-\4a），十六进制值
	COLOR,      // 颜色类型（\1c-\4c），BGR 十六进制值
	MULTI,      // 多值类型（\pos, \clip 等坐标值）
	TRANSFORM   // 变换类型（\t）
};

/// ASS 标签定义
struct TagDef {
	std::string name;           // 标签名称标识
	std::string pattern;        // 匹配正则表达式
	std::string tag;            // ASS 标签前缀
	bool transformable = false; // 是否可用于 \t 内部
	bool global = false;        // 是否只能出现一次（如 \pos, \an 等）
	std::string style_field;    // 对应的样式字段名
	TagType type = TagType::NUMBER; // 标签值类型
	std::vector<std::string> affected_by; // 受影响标签列表（如 \alpha 影响 \1a-\4a）
	std::vector<std::string> field_names; // 多值标签的字段名列表
	bool is_integer = false;    // 是否使用整数格式输出（如 \be）

	/// 格式化标签值为字符串
	std::string format_int(int value) const;
	std::string format_float(double value) const;
	std::string format_alpha(int value) const;
	std::string format_color(const ColorValue& color) const;
	std::string format_multi(const std::vector<double>& values) const;
	std::string format_string(const std::string& value) const;
};

/// 标签注册表
class TagRegistry {
public:
	static TagRegistry& instance();

	/// 获取所有标签定义
	const std::map<std::string, TagDef>& all_tags() const { return all_tags_; }

	/// 获取可重复标签列表
	const std::vector<const TagDef*>& repeat_tags() const { return repeat_tags_; }

	/// 获取全局标签列表
	const std::vector<const TagDef*>& one_time_tags() const { return one_time_tags_; }

	/// 获取样式相关标签列表
	const std::vector<const TagDef*>& style_tags() const { return style_tags_; }

	/// 获取可变换标签列表
	const std::vector<const TagDef*>& transform_tags() const { return transform_tags_; }

	/// 根据名称获取标签定义
	const TagDef* get(const std::string& name) const;

private:
	TagRegistry();
	void register_tags();

	std::map<std::string, TagDef> all_tags_;
	std::vector<const TagDef*> repeat_tags_;
	std::vector<const TagDef*> one_time_tags_;
	std::vector<const TagDef*> style_tags_;
	std::vector<const TagDef*> transform_tags_;
};

/// ASS 标签处理工具
namespace tag_utils {

/// 从标签块中提取标签值
/// @param text 标签块文本
/// @param pattern 标签的正则表达式
/// @return 匹配到的值字符串，未找到返回空
std::string find_tag_value(const std::string& text, const std::string& pattern);

/// 替换标签块中指定标签的值
/// @param text 标签块文本
/// @param pattern 标签的正则表达式
/// @param replacement 替换文本
/// @return 替换后的文本
std::string replace_tag(const std::string& text, const std::string& pattern, const std::string& replacement);

/// 移除标签块中指定标签
/// @param text 标签块文本
/// @param pattern 标签的正则表达式
/// @return 移除后的文本
std::string remove_tag(const std::string& text, const std::string& pattern);

/// 统计标签出现次数
/// @param text 标签块文本
/// @param pattern 标签的正则表达式
/// @return 出现次数
int count_tag(const std::string& text, const std::string& pattern);

/// 去除重复标签，保留最后一个
/// @param tag_block 标签块文本
/// @param pattern 标签的正则表达式
/// @return 去重后的文本
std::string deduplicate_tag(const std::string& tag_block, const std::string& pattern);

/// 提取 \t 变换标签
/// @param text 原始行文本
/// @param t_data_list 用于存储提取的 \t 数据
/// @return 移除 \t 后的文本
std::string extract_transforms(const std::string& text, std::vector<TransformData>& t_data_list);

/// 提取 \move 标签
/// @param text 原始行文本
/// @param move_data 用于存储提取的 \move 数据
/// @return 移除 \move 后的文本
std::string extract_move(const std::string& text, std::optional<MoveData>& move_data);

/// 提取 \fad / \fade 标签
/// @param text 原始行文本
/// @param fade_data 用于存储提取的 \fad 数据
/// @param full_fade_data 用于存储提取的完整 \fade 数据
/// @return 移除淡入淡出标签后的文本
std::string extract_fade(const std::string& text,
	std::optional<FadeData>& fade_data,
	std::optional<FullFadeData>& full_fade_data);

/// 清理空的标签块 {}
std::string clean_empty_blocks(const std::string& text);

/// 合并相邻的标签块 }{
std::string merge_adjacent_blocks(const std::string& text);

/// 对标签块执行回调操作
/// @param text 完整行文本
/// @param callback 回调函数，参数为标签块内容，返回修改后的内容
/// @return 处理后的完整文本
std::string run_callback_on_overrides(const std::string& text,
	const std::function<std::string(const std::string&, int)>& callback);

/// 对首个标签块执行回调操作
std::string run_callback_on_first_override(const std::string& text,
	const std::function<std::string(const std::string&)>& callback);

/// 确保行文本以标签块开头
std::string ensure_leading_override(const std::string& text);

/// 将矩形 clip 转换为矢量 clip
std::string rect_clip_to_vect_clip(const std::string& clip);

/// 将带缩放因子的矢量 clip 转换为浮点坐标
std::string convert_clip_to_fp(const std::string& clip);

} // namespace tag_utils
} // namespace mocha
