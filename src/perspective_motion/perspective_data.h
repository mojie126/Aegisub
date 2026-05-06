// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪数据解析器声明
// 对应 MoonScript 版 arch.PerspectiveMotion 中的 parse_powerpin_data
//
// 负责解析 Power-Pin / AE 角点导出格式，提供每帧四边形序列

#pragma once

#include "../vector2d.h"

#include <string>
#include <vector>

namespace mocha {
	/// 透视追踪数据解析器
	/// 支持 Adobe After Effects CC Power Pin 导出格式
	/// 数据格式：文本中的 Effects 段定义每帧四角点坐标
	class PerspectiveDataHandler {
	public:
		PerspectiveDataHandler() = default;

		/// @brief 解析 Power-Pin 格式文本数据
		/// Power Pin 的角点顺序：
		///   0002: TopLeft, 0003: TopRight, 0005: BottomRight, 0004: BottomLeft
		/// @param raw_data Power-Pin 导出文本
		/// @return 解析是否成功
		bool ParsePowerPin(const std::string &raw_data);

		/// 从文件路径读取并解析数据
		/// @param file_path 文件路径
		/// @return 解析是否成功
		bool ParseFile(const std::string &file_path);

		/// 尝试自动检测格式并解析
		/// @param input 数据文本或文件路径
		/// @return 解析是否成功
		bool BestEffortParse(const std::string &input);

		/// 获取数据总帧数
		[[nodiscard]] int Length() const { return static_cast<int>(quads_.size()); }

		/// 获取四边形序列（每帧一个）
		[[nodiscard]] const std::vector<std::vector<Vector2D>> &Quads() const { return quads_; }

		/// 获取指定帧的四边形（1-based 索引）
		[[nodiscard]] const std::vector<Vector2D> *GetQuad(int frame) const;

		/// 检查数据长度是否匹配
		[[nodiscard]] bool CheckLength(int total_frames) const;

		/// 是否成功解析到数据
		[[nodiscard]] bool IsValid() const { return !quads_.empty(); }

	private:
		/// 解析单个 Power Pin 标记的数据
		/// @param lines 所有文本行
		/// @param marker 标记号（如 "0002"）
		/// @param[out] coords 坐标值列表（X 或 Y）
		static bool ParseSinglePin(const std::vector<std::string> &lines,
									const std::string &marker,
									std::vector<double> &coords,
									int coord_index);

		std::vector<std::vector<Vector2D>> quads_;
	};
} // namespace mocha
