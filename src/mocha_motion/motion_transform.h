// Copyright (c) 2024-2026, Aegisub contributors
// 变换标签 (\t) 处理声明
// 对应 MoonScript 版 a-mo.Transform
//
// \t 标签处理是运动追踪模块中最复杂的部分之一。
// 核心设计思路：
//   标记化 (tokenize)：将 \t(...) 整体替换为如 __TRANSFORM_0__ 的唯一占位符
//   这样做的好处：
//     1. 避免 \t 内部标签被外部处理误伤（Issue #69 核心修复策略）
//     2. 在逐帧模式下可以精确计算每帧的 \t 插值结果
//     3. 在线性模式下可以安全地直接还原 \t 标签
//
// 插值算法（interpolate）：
//   progress = ((time - t1) / (t2 - t1)) ^ accel
//   result = before + (after - before) * progress
//   支持数值、颜色、多值（如 \clip 坐标）的插值

#pragma once

#include "motion_common.h"
#include "motion_tags.h"

#include <string>
#include <vector>
#include <map>

namespace mocha {

/// 变换标签 (\t) 管理器
/// 负责解析、插值和重建 \t 标签
class Transform {
public:
	Transform() = default;

	/// 从字符串构造（标准工厂方法）
	/// @param transform_string \t 标签内容（括号内）
	/// @param line_duration 行的总持续时间（毫秒）
	/// @param tag_index 标签块索引
	/// @return Transform 实例
	static Transform from_string(const std::string& transform_string, int line_duration, int tag_index = 0);

	/// 转换为字符串
	/// @param line_duration 行的总持续时间
	/// @return 格式化后的 \t 标签字符串（含 \t 前缀）
	std::string to_string(int line_duration = 0) const;

	/// 收集变换效果中的标签
	void gather_tags_in_effect();

	/// 标签类型感知的 effect 内标签值
	struct EffectTagValue {
		enum Type { NUM, COL, ALP, MULTI } type = NUM;
		double number = 0;
		ColorValue color;
		int alpha = 0;
		std::vector<double> multi_values;
	};

	/// 在指定时间点插值变换效果
	/// @param text 当前行文本
	/// @param placeholder 占位符文本
	/// @param time 相对于行开始的时间（毫秒）
	/// @param line_properties 行的样式属性值 {标签名 -> 数值}
	/// @param prior_inline_tags 从行内联标签中收集的先前状态值
	/// @param res_x 脚本水平分辨率
	/// @param res_y 脚本垂直分辨率
	/// @return 插值后替换了占位符的文本
	std::string interpolate(const std::string& text, const std::string& placeholder,
		int time, const std::map<std::string, double>& line_properties,
		const std::map<std::string, EffectTagValue>& prior_inline_tags = {},
		int res_x = 0, int res_y = 0) const;

	// 公开属性
	int start_time = 0;      // 变换开始时间
	int end_time = 0;        // 变换结束时间
	double accel = 1.0;      // 加速度
	std::string effect;      // 效果内容
	std::string raw_string;  // 原始字符串
	int index = 0;           // 标签块索引
	std::string token;       // 标记化后的占位符

	/// 效果中的标签 {标签名 -> 结束值列表}（向后兼容，仅数值标签）
	std::map<std::string, std::vector<double>> effect_tags;

	/// 类型感知的 effect 标签值 {标签名 -> 值列表}
	/// 对应 MoonScript: collectPriorState + affectedBy 机制
	std::map<std::string, std::vector<EffectTagValue>> typed_effect_tags;

private:
	/// 插值数值
	static double interpolate_number(double before, double after, double progress);

	/// 插值颜色
	static ColorValue interpolate_color(const ColorValue& before, const ColorValue& after, double progress);
};

/// 变换标签标记化管理
namespace transform_utils {

/// 生成占位符字符串
/// @param count 占位符编号
std::string make_placeholder(int count);

/// 占位符正则匹配模式
extern const std::string placeholder_pattern;

/// 将行文本中的 \t 标签替换为占位符
/// @param text 行文本
/// @param transforms 输出：提取的 Transform 列表
/// @param line_duration 行的持续时间
/// @return 标记化后的文本
std::string tokenize_transforms(const std::string& text,
	std::vector<Transform>& transforms, int line_duration);

/// 将占位符还原为 \t 标签
/// @param text 标记化的文本
/// @param transforms Transform 列表
/// @param time_shift 时间偏移量
/// @param line_duration 行的持续时间（毫秒），用于抑制超出范围的变换
/// @return 还原后的文本
std::string detokenize_transforms(const std::string& text,
	const std::vector<Transform>& transforms, int time_shift = 0, int line_duration = 0);

/// 创建还原后文本的副本（不修改原始 transforms）
std::string detokenize_transforms_copy(const std::string& text,
	const std::vector<Transform>& transforms, int time_shift = 0, int line_duration = 0);

/// 在指定时间点插值所有变换并替换占位符
/// @param text 标记化的文本
/// @param transforms Transform 列表
/// @param time_shift 时间偏移量
/// @param time 当前时间点
/// @param line_properties 行的样式属性
/// @param prior_inline_tags 从行内联标签收集的先前状态值
/// @param res_x 脚本水平分辨率
/// @param res_y 脚本垂直分辨率
/// @return 插值后的文本
std::string interpolate_transforms_copy(const std::string& text,
	const std::vector<Transform>& transforms, int time_shift,
	int time, const std::map<std::string, double>& line_properties,
	const std::map<std::string, Transform::EffectTagValue>& prior_inline_tags = {},
	int res_x = 0, int res_y = 0);

} // namespace transform_utils
} // namespace mocha
