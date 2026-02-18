// Copyright (c) 2024-2026, Aegisub contributors
// 运动处理器声明 - 高层次的行处理流程
// 对应 MoonScript 版 prepareLines / postprocLines / applyProcessor 流程

#pragma once

#include "motion_common.h"
#include "motion_data_handler.h"
#include "motion_handler.h"
#include "motion_line.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace agi { struct Context; }
class AssDialogue;
class AssStyle;

namespace mocha {

/// 运动处理器：协调数据解析、行预处理、运动应用和后处理的完整流程
class MotionProcessor {
public:
	/// 帧-时间转换函数类型
	using FrameFromMs = std::function<int(int)>;
	using MsFromFrame = std::function<int(int)>;

	MotionProcessor(const MotionOptions& options, int res_x, int res_y);

	/// 设置帧-时间转换函数
	void set_timing_functions(FrameFromMs frame_from_ms, MsFromFrame ms_from_frame);

	/// 设置样式查询函数
	void set_style_lookup(std::function<const AssStyle*(const std::string&)> lookup);

	/// 从 AssDialogue 构建 MotionLine
	MotionLine build_line(const AssDialogue* diag) const;

	/// 预处理行集合（tokenize, deduplicate, add missing tags 等）
	/// 对应 MoonScript prepareLines()
	void prepare_lines(std::vector<MotionLine>& lines);

	/// 后处理行集合（detokenize, combine identical 等）
	/// 对应 MoonScript postprocLines()
	void postprocess_lines(std::vector<MotionLine>& lines);

	/// 合并相邻相同行（文本和样式相同时扩展时间范围）
	/// 对应 MoonScript combineIdenticalLines()
	void combine_identical_lines(std::vector<MotionLine>& lines);

	/// 跨行合并：对来自不同源行的结果进行合并
	/// 对应 MoonScript combineWithLine 的跨行部分
	/// 调用者需确保 lines 已按时间排序
	void cross_line_combine(std::vector<MotionLine>& lines);

	/// 完整的运动应用流程
	/// @param lines 输入行集合
	/// @param main_data 主追踪数据
	/// @param clip_data clip 追踪数据（可选）
	/// @param clip_options clip 追踪选项（可选，有 clip_data 时使用）
	/// @param start_frame 选中行集合的起始帧号
	/// @return 处理后的新行集合
	std::vector<MotionLine> apply(
		std::vector<MotionLine>& lines,
		DataHandler& main_data,
		DataHandler* clip_data,
		const ClipTrackOptions* clip_options,
		int start_frame);

private:
	/// 获取行缺少的必要标签
	/// 对应 MoonScript getMissingTags()
	std::string get_missing_tags(const std::string& block,
	                             const std::map<std::string, double>& properties) const;

	/// 获取缺少的 alpha 标签
	/// 对应 MoonScript getMissingAlphas()
	std::string get_missing_alphas(const std::string& block,
	                               const std::map<std::string, double>& properties) const;

	/// 从 AssStyle 提取属性映射
	std::map<std::string, double> extract_style_properties(const AssStyle* style) const;

	MotionOptions options_;
	int res_x_;
	int res_y_;
	FrameFromMs frame_from_ms_;
	MsFromFrame ms_from_frame_;
	std::function<const AssStyle*(const std::string&)> style_lookup_;
};

} // namespace mocha
