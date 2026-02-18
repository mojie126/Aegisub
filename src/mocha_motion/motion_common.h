// Copyright (c) 2024-2026, Aegisub contributors
// 运动追踪模块公共数据结构和类型定义
// 对应 MoonScript 版 Aegisub-Motion 的公共数据结构
//
// 本文件定义了模块内所有共享的数据类型，被以下文件引用：
//   - motion_handler.h/cpp : 使用 MotionOptions 控制追踪行为，MoveData/FadeData 等存储解析结果
//   - motion_processor.h/cpp : 使用 MotionOptions 配置处理流水线
//   - motion_line.h/cpp :    使用 MoveData/FadeData/ClipType 等存储行级数据
//   - motion_data_handler.h :  使用 DataType 区分数据源格式
//   - motion_dialog.h/cpp :   使用 MotionOptions 传递用户选择
//   - subtitle.cpp :          使用 MotionOptions 配置追踪参数

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <map>

namespace mocha {

/// 运动追踪配置选项
/// 对应 MoonScript 版的 config table，由对话框填充，传递给 handler 和 processor
/// 每个布尔值对应对话框中的一个复选框
struct MotionOptions {
	// 位置相关
	bool x_position = true;      // 应用 X 位置数据
	bool y_position = true;      // 应用 Y 位置数据
	bool origin = false;         // 移动原点
	bool abs_pos = false;        // 使用绝对位置

	// 缩放相关
	bool x_scale = true;         // 应用缩放数据
	bool border = true;          // 随缩放调整边框
	bool shadow = true;          // 随缩放调整阴影
	bool blur = true;            // 随缩放调整模糊
	double blur_scale = 1.0;     // 模糊衰减因子

	// 旋转
	bool z_rotation = false;     // 应用旋转数据

	// Clip 相关
	bool rect_clip = true;       // 应用矩形 clip
	bool vect_clip = true;       // 应用矢量 clip
	bool rc_to_vc = false;       // 矩形 clip 转矢量 clip

	// 其他
	bool kill_trans = true;      // 插值变换标签
	bool linear = false;         // 使用线性模式（\move + \t）
	bool clip_only = false;      // 仅应用到 clip
	bool relative = true;        // 起始帧相对于选择区域
	int start_frame = 1;         // 起始帧编号
	bool write_conf = true;      // 写入配置
	bool preview = false;        // 便捷预览模式
	bool reverse_tracking = false; // 反向追踪
};

/// 独立 clip 追踪配置选项
/// 对应 MoonScript config.clip section
/// 当用户提供独立的 clip 追踪数据时，使用此选项控制 clip 数据的应用行为
struct ClipTrackOptions {
	bool x_position = true;      // 应用 X 位置数据到 clip
	bool y_position = true;      // 应用 Y 位置数据到 clip
	bool x_scale = true;         // 应用缩放数据到 clip
	bool z_rotation = false;     // 应用旋转数据到 clip
	bool rect_clip = true;       // 对矩形 clip 应用追踪
	bool vect_clip = true;       // 对矢量 clip 应用追踪
	bool rc_to_vc = false;       // 矩形 clip 转矢量 clip
	int start_frame = 1;         // clip 数据起始帧
	bool relative = true;        // 起始帧相对/绝对模式
};

/// \move 标签数据
/// 存储从 ASS 行解析出的 \move(x1,y1,x2,y2,t1,t2) 参数
/// 在 motion_line.cpp 中解析，在 motion_handler.cpp 中用于位置计算
struct MoveData {
	double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
	int t1 = 0, t2 = 0;
};

/// \fad / \fade 标签数据（简版）
/// 存储 \fad(fade_in, fade_out) 参数
/// 用于 handler 中计算淡入淡出乘子，影响透明度和模糊等标签的缩放
struct FadeData {
	int fade_in = 0;    // 淡入时间（毫秒）
	int fade_out = 0;   // 淡出时间（毫秒）
};

/// \fade(a1,a2,a3,t1,t2,t3,t4) 完整版淡入淡出数据
/// 包含 3 个透明度值和 4 个时间点，表达更复杂的淡入淡出曲线
struct FullFadeData {
	int a1 = 0, a2 = 0, a3 = 0;
	int t1 = 0, t2 = 0, t3 = 0, t4 = 0;
};

/// \t 变换标签数据
struct TransformData {
	int t1 = 0;              // 变换开始时间
	int t2 = 0;              // 变换结束时间
	double accel = 1.0;      // 加速度因子
	std::string effect;      // 变换效果内容
	std::string raw_string;  // 原始字符串
	int tag_index = 0;       // 标签块索引
};

/// 帧级追踪数据（单帧）
struct FrameTrackData {
	int frame = 0;
	double x = 0, y = 0, z = 0;
	double scale_x = 0, scale_y = 0, scale_z = 0;
	double rotation = 0;
};

/// 数据源类型
/// 用于 DataHandler 判断输入数据的格式，目前主要支持 AE 关键帧格式
enum class DataType {
	NONE,
	AE_KEYFRAME,   // Adobe After Effects 关键帧数据
	SHAKE_SHAPE    // Shake Shape 数据
};

/// 行处理方法
/// LINEAR: 使用 \move + \t 标签，输出单行
/// NONLINEAR: 逐帧生成，每帧一行，精度更高但行数更多
enum class LineMethod {
	LINEAR,       // 线性模式 (\move + \t)
	NONLINEAR     // 逐帧模式
};

/// 颜色值
struct ColorValue {
	int b = 0, g = 0, r = 0;
};

/// 标签值的通用联合类型
struct TagValue {
	enum Type { NUMBER, STRING, COLOR, MULTI, TRANSFORM } type = NUMBER;
	double number = 0;
	std::string str;
	ColorValue color;
	std::vector<double> multi;
};

/// Clip 类型
enum class ClipType {
	NONE,
	RECT,        // 矩形 clip
	VECTOR,      // 矢量 clip
	RECT_I,      // 反向矩形 clip
	VECTOR_I     // 反向矢量 clip
};

} // namespace mocha
