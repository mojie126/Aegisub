// Copyright (c) 2024-2026, Aegisub contributors
// 字幕行处理声明
// 对应 MoonScript 版 a-mo.Line

#pragma once

#include "motion_common.h"
#include "motion_transform.h"

#include <string>
#include <vector>
#include <map>
#include <functional>

namespace mocha {

/// 字幕行数据（包含追踪所需的元信息）
struct MotionLine {
	// 基本 ASS 行属性
	std::string text;           // 行文本
	std::string style;          // 样式名
	std::string actor;          // 角色名
	std::string effect;         // 效果
	bool comment = false;       // 注释行
	int layer = 0;              // 图层
	int margin_l = 0, margin_r = 0, margin_t = 0; // 边距
	int start_time = 0;         // 开始时间（毫秒）
	int end_time = 0;           // 结束时间（毫秒）
	int number = 0;             // 行号

	// 计算属性
	int duration = 0;           // 持续时间
	int start_frame = 0;        // 开始帧
	int end_frame = 0;          // 结束帧
	int relative_start = 0;     // 相对起始帧
	int relative_end = 0;       // 相对结束帧

	// 位置和对齐
	int align = 0;              // 对齐方式
	double x_position = 0;      // X 坐标
	double y_position = 0;      // Y 坐标
	bool has_org = false;       // 是否有 \org
	bool has_clip = false;      // 是否有 clip

	// \move 数据
	std::optional<MoveData> move;

	// 变换标签
	std::vector<Transform> transforms;
	bool transforms_tokenized = false;

	// 样式属性（数值类型，如 xscale, zrot, border 等）
	std::map<std::string, double> properties;

	// 样式标签默认值（支持颜色、透明度等非数值类型）
	// 对应 MoonScript: line.properties[tag] 中的颜色回退
	std::map<std::string, Transform::EffectTagValue> style_tag_defaults;

	// 处理方法
	LineMethod method = LineMethod::NONLINEAR;

	// 用于逐帧卡拉OK偏移
	double karaoke_shift = 0;

	// 标记
	bool was_linear = false;
	bool inserted = false;
	bool has_been_deleted = false;
	bool selected = true;
	bool retrack = false;

	/// 计算默认位置（基于对齐方式和边距）
	/// @param style_align 样式对齐方式
	/// @param style_margin_l 样式左边距
	/// @param style_margin_r 样式右边距
	/// @param style_margin_t 样式垂直边距
	/// @param res_x 脚本水平分辨率
	/// @param res_y 脚本垂直分辨率
	void calculate_default_position(int style_align, int style_margin_l, int style_margin_r,
		int style_margin_t, int res_x, int res_y);

	/// 提取行的对齐方式和位置信息
	/// @param style_align 样式默认对齐方式
	/// @param style_margin_l 样式左边距
	/// @param style_margin_r 样式右边距
	/// @param style_margin_t 样式垂直边距
	/// @param res_x 脚本水平分辨率
	/// @param res_y 脚本垂直分辨率
	/// @return 是否已有 \pos 标签
	bool extract_metrics(int style_align, int style_margin_l, int style_margin_r,
		int style_margin_t, int res_x, int res_y);

	/// 标记化变换标签（将 \t 替换为占位符）
	void tokenize_transforms();

	/// 还原变换标签（将占位符替换回 \t）
	/// @param shift 时间偏移量
	/// @param line_dur 行的持续时间（毫秒），用于抑制超出范围的变换
	void detokenize_transforms(int shift = 0, int line_dur = 0);

	/// 还原变换但不修改原始数据（返回副本）
	/// @param shift 时间偏移量
	/// @return 还原后的文本
	std::string detokenize_transforms_copy(int shift = 0, int line_dur = 0) const;

	/// 不修改变换标签内容，直接还原
	void dont_touch_transforms();

	/// 插值变换标签并返回文本副本
	/// @param shift 时间偏移量
	/// @param start 当前帧的起始时间
	/// @param res_x 脚本水平分辨率
	/// @param res_y 脚本垂直分辨率
	/// @return 插值后的文本
	std::string interpolate_transforms_copy(int shift, int start, int res_x = 0, int res_y = 0) const;

	/// 从行内联标签中收集先前状态值
	/// 对应 MoonScript Transform.moon: collectPriorState
	/// 遍历行文本中的 override 块，提取每个可变换标签的当前值，
	/// 用于作为 \t 插值的起始状态（优先于样式默认值）
	/// @return 标签名 -> 先前状态值 的映射
	std::map<std::string, Transform::EffectTagValue> collect_prior_inline_tags() const;

	/// 去除重复标签
	void deduplicate_tags();

	/// 从样式属性中获取标签默认值
	/// @param style_props 样式属性映射表 {属性名 -> 值}
	void get_properties_from_style(const std::map<std::string, double>& style_props);

	/// 确保行文本以标签块开头
	void ensure_leading_override_exists();

	/// 对所有标签块执行回调
	void run_callback_on_overrides(std::function<std::string(const std::string&, int)> callback);

	/// 对第一个标签块执行回调
	void run_callback_on_first_override(std::function<std::string(const std::string&)> callback);

	/// 将 \fad(in,out) 转换为 \fade(255,0,255,0,in,duration-out,duration)
	void convert_fad_to_fade();

	/// 移位卡拉OK计时标签
	void shift_karaoke();
};

} // namespace mocha
