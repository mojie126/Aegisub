// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪处理器声明
// 对应 MoonScript 版 arch.PerspectiveMotion 的 track() 主流程
//
// 核心管线：
//   1. 构建 MotionLine 集合，FBF 展开选中行
//   2. 对参考帧行用 TransformPoints 计算屏幕四边形
//   3. 通过 XYToUV/UVToXY 将每行映射到目标帧
//   4. 调用 TagsFromQuad 生成新标签并写回

#pragma once

#include "../mocha_motion/motion_common.h"
#include "../mocha_motion/motion_line.h"
#include "perspective_math.h"
#include "perspective_data.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace agi {
	struct Context;
}

class AssDialogue;
class AssStyle;

namespace mocha {
	/// 透视追踪选项
	/// 对应 MoonScript 版对话框的 results table
	struct PerspectiveOptions {
		// 追踪参数
		int relframe = 1; ///< 参考帧号（1-based，自动从当前视频帧计算）
		bool apply_perspective = true; ///< 对参考行预先应用一次透视
		bool track_pos = true; ///< 追踪位置/透视
		bool track_clip = true; ///< 追踪 clip
		bool track_bord_shad = true; ///< 缩放 \bord 和 \shad
		int org_mode = 2; ///< \org 选择模式: 1=保持, 2=中心, 3=尝试\fax=0
		int selection_start_frame = 0; ///< 选中行的起始绝对帧号

		// 帧号与处理模式
		bool relative = true; ///< 相对帧号模式
		int start_frame = 1; ///< 起始帧号（相对或绝对）
		bool reverse_tracking = false; ///< 反向追踪
		int layout_res_y = 0; ///< LayoutResY（脚本布局分辨率 Y，0=未设置）
		bool preview = true; ///< 便捷预览（注释原始行）
		bool write_conf = true; ///< 保存配置
	};

	/// 透视追踪处理器
	/// 将 Power-Pin 追踪数据应用到字幕行集合
	class PerspectiveProcessor {
	public:
		/// 帧-时间转换函数类型
		using FrameFromMs = std::function<int(int)>;
		using MsFromFrame = std::function<int(int)>;

		/// @param options 透视追踪选项
		/// @param res_x 脚本水平分辨率
		/// @param res_y 脚本垂直分辨率
		PerspectiveProcessor(const PerspectiveOptions &options, int res_x, int res_y);

		/// 设置帧-时间转换函数
		void SetTimingFunctions(FrameFromMs frame_from_ms, MsFromFrame ms_from_frame);

		/// 设置样式查询函数
		void SetStyleLookup(std::function<const AssStyle*(const std::string &)> lookup);

		/// 从 AssDialogue 构建 MotionLine（复用 MotionProcessor 的静态方法）
		static MotionLine BuildLine(const AssDialogue *diag);

		/// 预处理行集合
		void PrepareLines(std::vector<MotionLine> &lines);

		/// 后处理行集合
		static void PostprocessLines(std::vector<MotionLine> &lines);

		/// @brief 核心追踪入口
		/// 将追踪数据应用到行集合上
		/// @param lines 输入行集合（会被修改）
		/// @param quads 每帧四边形序列
		/// @param video_width 视频宽度
		/// @param video_height 视频高度
		/// @return 处理后的新行集合
		std::vector<MotionLine> Apply(std::vector<MotionLine> &lines,
									const std::vector<Quad> &quads,
									int video_width, int video_height);

		/// 从 ASS 样式提取属性映射
		static std::map<std::string, double> ExtractStyleProperties(const AssStyle *style);

		/// @brief 从行文本和样式中提取透视标签值（公开用于单测）
		/// 对应 MoonScript prepareForPerspective()
		/// @param line 字幕行
		/// @param[out] width 文本/绘图原始宽度
		/// @param[out] height 文本/绘图原始高度
		/// @return 规范化标签值
		PerspectiveTagVals PrepareForPerspective(const MotionLine &line,
												double &width, double &height);

		/// @brief 将透视标签写回 MotionLine 的 override 文本（公开用于单测）
		/// 对应 MoonScript data.removeTags + data.insertTags + data.commit()
		void ApplyTagsToLine(MotionLine &line,
				const std::vector<PerspectiveTagVals> &per_block_tags);

		/// @brief 对 clip 坐标进行透视映射（公开用于单测）
		/// 对应 MoonScript track() 中的 clip 处理
		void PerspectiveMapClip(MotionLine &line,
								const Quad &rel_quad,
								const Quad &frame_quad);

	private:
		PerspectiveOptions options_;
		int res_x_;
		int res_y_;
		FrameFromMs frame_from_ms_;
		MsFromFrame ms_from_frame_;
		std::function<const AssStyle*(const std::string &)> style_lookup_;
	};
} // namespace mocha