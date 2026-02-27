// Copyright (c) 2024-2026, Aegisub contributors
// 运动应用引擎声明
// 对应 MoonScript 版 a-mo.MotionHandler

#pragma once

#include "motion_common.h"
#include "motion_data_handler.h"
#include "motion_line.h"

#include <functional>
#include <string>
#include <vector>
#include <regex>

namespace mocha {

/// 运动追踪数据应用引擎
/// 将解析后的追踪数据应用到字幕行上，支持线性和非线性两种模式
class MotionHandler {
public:
	/// 标签回调函数类型：接受匹配值和帧号，返回新值
	using TagCallback = std::function<std::string(const std::string&, int)>;

	/// 回调条目：正则模式 + 回调函数
	struct CallbackEntry {
		std::string pattern_str;  // 正则表达式字符串
		std::regex pattern;       // 编译的正则
		TagCallback callback;     // 回调函数
	};

	/// @param options 运动选项
	/// @param main_data 主追踪数据（位置/缩放/旋转）
	/// @param rect_clip_data 矩形 clip 追踪数据（可选，为 nullptr 时使用 main_data）
	/// @param vect_clip_data 矢量 clip 追踪数据（可选，为 nullptr 时使用 main_data）
	MotionHandler(const MotionOptions& options,
	              DataHandler* main_data,
	              DataHandler* rect_clip_data = nullptr,
	              DataHandler* vect_clip_data = nullptr);

	/// 对行集合应用运动追踪数据
	/// @param lines 待处理的字幕行集合
	/// @param collection_start_frame 选中行集合的起始帧号
	/// @param frame_from_ms 毫秒→帧号转换函数
	/// @param ms_from_frame 帧号→毫秒转换函数
	/// @return 生成的新字幕行集合
	std::vector<MotionLine> apply_motion(
		std::vector<MotionLine>& lines,
		int collection_start_frame,
		std::function<int(int)> frame_from_ms,
		std::function<int(int)> ms_from_frame);

	// --- 公开供测试的回调和计算接口 ---

	/// 相对位置回调：positionMath 变换
	std::string cb_position(const std::string& value, int frame);

	/// 绝对位置回调：直接使用追踪数据坐标
	std::string cb_absolute_position(const std::string& value, int frame);

	/// 原点回调：positionMath 变换应用到 \org
	std::string cb_origin(const std::string& value, int frame);

	/// 缩放回调：乘以 xRatio
	std::string cb_scale(const std::string& value, int frame);

	/// 模糊回调：缩放比例 * 衰减系数
	std::string cb_blur(const std::string& value, int frame);

	/// X 轴旋转回调：加上 xRotationDiff
	std::string cb_rotate_x(const std::string& value, int frame);

	/// Y 轴旋转回调：加上 yRotationDiff
	std::string cb_rotate_y(const std::string& value, int frame);

	/// Z 轴旋转回调：加上 zRotationDiff
	std::string cb_rotate_z(const std::string& value, int frame);

	/// 深度回调：加上 zPositionDiff
	std::string cb_z_position(const std::string& value, int frame);

	/// 矩形 clip 回调
	std::string cb_rect_clip(const std::string& value, int frame);

	/// 矢量 clip 回调（TSR 模式：逐坐标 positionMath 变换）
	std::string cb_vect_clip(const std::string& value, int frame);

	/// 矢量 clip 回调（SRS 模式：直接替换为预生成的 ASS 绘图字符串）
	/// 对应 MoonScript vectorClipSRS()
	std::string cb_vect_clip_srs(const std::string& value, int frame);

	/// 位置数学核心：坐标变换（旋转补偿 + 缩放）
	/// 对应 MoonScript positionMath()
	std::pair<double, double> position_math(double x, double y, DataHandler* data);

	/// 对文本应用所有回调（正则匹配并替换）
	/// @param text 待处理文本
	/// @param frame 当前帧（在追踪数据中的索引）
	/// @return 处理后的文本
	std::string apply_callbacks(const std::string& text, int frame);

private:
	/// 根据选项配置回调列表
	void setup_callbacks();

	/// 线性模式：用 \move + \t 实现运动过渡
	void apply_linear(MotionLine& line,
	                  int collection_start_frame,
	                  std::function<int(int)>& frame_from_ms,
	                  std::function<int(int)>& ms_from_frame,
	                  std::vector<MotionLine>& result);

	/// 非线性模式：逐帧生成独立字幕行
	void apply_nonlinear(const MotionLine& line,
	                     int collection_start_frame,
	                     std::function<int(int)>& ms_from_frame,
	                     std::vector<MotionLine>& result);

	/// 对文本应用线性回调（生成 \t 标签）
	std::string apply_callbacks_linear(const std::string& text,
	                                   int start_frame, int end_frame,
	                                   int begin_time, int end_time);

	std::vector<CallbackEntry> callbacks_;
	DataHandler* main_data_;
	DataHandler* rect_clip_data_;
	DataHandler* vect_clip_data_;
	MotionOptions options_;

	// 绝对位置模式下的坐标偏移量
	double x_delta_ = 0;
	double y_delta_ = 0;
};

} // namespace mocha
