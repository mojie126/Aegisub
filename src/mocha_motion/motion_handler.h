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
		using TagCallback = std::function<std::string(const std::string &, int)>;

		/// 回调条目：正则模式 + 回调函数
		struct CallbackEntry {
			std::regex pattern;
			TagCallback callback;
		};

		/// @param options 运动选项
		/// @param main_data 主追踪数据（位置/缩放/旋转）
		/// @param rect_clip_data 矩形 clip 追踪数据（可选，为 nullptr 时使用 main_data）
		/// @param vect_clip_data 矢量 clip 追踪数据（可选，为 nullptr 时使用 main_data）
		/// @param res_x 脚本水平分辨率（用于 \t(\clip) 默认值插值）
		/// @param res_y 脚本垂直分辨率（用于 \t(\clip) 默认值插值）
		MotionHandler(const MotionOptions &options,
					DataHandler *main_data,
					DataHandler *rect_clip_data = nullptr,
					DataHandler *vect_clip_data = nullptr,
					int res_x = 0,
					int res_y = 0);

		/// 对行集合应用运动追踪数据
		/// @param lines 待处理的字幕行集合
		/// @param collection_start_frame 选中行集合的起始帧号
		/// @param frame_from_ms 毫秒→帧号转换函数
		/// @param ms_from_frame 帧号→毫秒转换函数
		/// @return 生成的新字幕行集合
		std::vector<MotionLine> apply_motion(
			std::vector<MotionLine> &lines,
			int collection_start_frame,
			const std::function<int(int)> &frame_from_ms,
			std::function<int(int)> ms_from_frame);

		// --- 公开供测试的回调和计算接口 ---

		/// 相对位置回调：positionMath 变换
		[[nodiscard]] std::string cb_position(const std::string &value, int frame) const;

		/// 绝对位置回调：直接使用追踪数据坐标
		std::string cb_absolute_position(const std::string &value, int frame);

		/// 原点回调：positionMath 变换应用到 \org
		[[nodiscard]] std::string cb_origin(const std::string &value, int frame) const;

		/// 缩放回调：乘以 xRatio
		[[nodiscard]] std::string cb_scale(const std::string &value, int frame) const;

		/// 模糊回调：缩放比例 * 衰减系数
		[[nodiscard]] std::string cb_blur(const std::string &value, int frame) const;

		/// X 轴旋转回调：加上 xRotationDiff
		[[nodiscard]] std::string cb_rotate_x(const std::string &value, int frame) const;

		/// Y 轴旋转回调：加上 yRotationDiff
		[[nodiscard]] std::string cb_rotate_y(const std::string &value, int frame) const;

		/// Z 轴旋转回调：加上 zRotationDiff
		[[nodiscard]] std::string cb_rotate_z(const std::string &value, int frame) const;

		/// 深度回调：加上 zPositionDiff
		[[nodiscard]] std::string cb_z_position(const std::string &value, int frame) const;

		/// 矩形 clip 回调
		[[nodiscard]] std::string cb_rect_clip(const std::string &value, int frame) const;

		/// 矢量 clip 回调（TSR 模式：逐坐标 positionMath 变换）
		[[nodiscard]] std::string cb_vect_clip(const std::string &value, int frame) const;

		/// 矢量 clip 回调（SRS 模式：直接替换为预生成的 ASS 绘图字符串）
		/// 对应 MoonScript vectorClipSRS()
		[[nodiscard]] std::string cb_vect_clip_srs(const std::string &value, int frame) const;

		/// @brief 位置数学核心算法 - 坐标变换（旋转补偿 + 缩放）
		///
		/// 对应 MoonScript positionMath()。
		/// 算法原理：
		///   1. 计算当前坐标 (x, y) 相对于追踪数据起始位置 (start_x, start_y) 的偏移量
		///   2. 将偏移量乘以缩放比例 (x_ratio, y_ratio)
		///   3. 转换为极坐标 (radius, alpha)
		///   4. 减去旋转差值 (z_rotation_diff)，实现旋转补偿
		///   5. 转换回直角坐标并加上追踪数据当前位置
		///
		/// 数学公式：
		///   dx = (x - x_start) * x_ratio
		///   dy = (y - y_start) * y_ratio
		///   r  = sqrt(dx² + dy²)
		///   α  = atan2(dy, dx)
		///   new_x = x_current + r * cos(α - Δθ)
		///   new_y = y_current + r * sin(α - Δθ)
		///
		/// @param x 原始 X 坐标（可来自 \pos、\org 等标签）
		/// @param y 原始 Y 坐标
		/// @param data 追踪数据（包含起始/当前位置、缩放比、旋转差值）
		/// @return 变换后的 (new_x, new_y) 坐标对
		static std::pair<double, double> position_math(double x, double y, const DataHandler *data);

		/// 对文本应用所有回调（正则匹配并替换）
		/// @param text 待处理文本
		/// @param frame 当前帧（在追踪数据中的索引）
		/// @return 处理后的文本
		[[nodiscard]] std::string apply_callbacks(const std::string &text, int frame) const;

	private:
		/// @brief 根据用户选项配置标签回调列表
		///
		/// 回调架构说明：
		///   - callbacks_ 是一个 {正则模式, 回调函数} 的有序列表
		///   - 在 apply_callbacks() 中，对文本逐个匹配正则并调用回调函数替换值
		///   - 回调函数接收标签的当前值和帧号，返回计算后的新值
		///   - 顺序重要：位置回调必须在 clip 回调之前，因为绝对位置模式下
		///     位置回调会计算 x_delta_/y_delta_，clip 回调会使用这些偏移量
		///
		/// 回调配置根据 MotionOptions 决定启用哪些：
		///   - clip_only=false 时才启用位置/缩放/旋转回调
		///   - 位置：abs_pos 决定使用绝对位置回调还是相对位置回调
		///   - 缩放：启用 x_scale 后还可附带 border/shadow/blur
		///   - clip：取决于 rect_clip_data_/vect_clip_data_ 是否提供
		void setup_callbacks();

		/// 线性模式：用 \move + \t 实现运动过渡
		void apply_linear(const MotionLine &line,
						int collection_start_frame,
						const std::function<int(int)> &frame_from_ms,
						const std::function<int(int)> &ms_from_frame,
						std::vector<MotionLine> &result);

		/// @brief 非线性模式：逐帧生成独立字幕行
		///
		/// 对应 MoonScript nonlinear()。
		/// 核心流程：
		///   1. 逆序遍历帧（从 relativeEnd 到 relativeStart），输出为逆时序
		///      调用方需对输出排序以恢复时间正序
		///   2. 每帧计算精确的起止时间（10ms 对齐，避免渲染空隙）
		///   3. 处理变换标签：kill_trans=true 时逐帧插值 \t，否则仅偏移时间
		///   4. 处理 \fade：转换为逐帧 \alpha 值或偏移 \fade 时间参数
		///   5. 处理 \move：插值为当前帧的 \pos
		///   6. 计算追踪数据当前帧状态（position_math 依赖此状态）
		///   7. 应用全部回调（位置/缩放/旋转/clip 标签替换）
		///   8. 生成新行并设置卡拉OK偏移量
		///
		/// @param line 原始行（已预处理，包含 tokenized 变换标签）
		/// @param collection_start_frame 行集合的绝对起始帧号
		/// @param ms_from_frame 帧号→毫秒转换函数
		/// @param result 输出行集合（追加到末尾）
		void apply_nonlinear(const MotionLine &line,
							int collection_start_frame,
							std::function<int(int)> &ms_from_frame,
							std::vector<MotionLine> &result) const;

		/// 对文本应用线性回调（生成 \t 标签）
		std::string apply_callbacks_linear(const std::string &text,
											int start_frame, int end_frame,
											int begin_time, int end_time);

		std::vector<CallbackEntry> callbacks_;
		DataHandler *main_data_;
		DataHandler *rect_clip_data_;
		DataHandler *vect_clip_data_;
		MotionOptions options_;
		int res_x_ = 0;
		int res_y_ = 0;

		// 绝对位置模式下的坐标偏移量
		double x_delta_ = 0;
		double y_delta_ = 0;
	};
} // namespace mocha
