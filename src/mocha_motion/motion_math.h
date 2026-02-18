// Copyright (c) 2024-2026, Aegisub contributors
// 运动追踪模块数学工具（仅头文件，无对应 .cpp）
// 对应 MoonScript 版 a-mo.Math
//
// 核心用途：
//   - round()     : 标签值的精度控制，避免输出过长小数
//   - d_cos/d_sin : position_math() 极坐标变换中的角度制三角函数
//   - d_atan      : position_math() 中计算偏移向量的角度
//   - clamp       : 插值进度和 alpha 值的范围限制

#pragma once

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace mocha {
namespace math {

/// 四舍五入到指定小数位
/// @param num 待四舍五入的数值
/// @param decimal_places 小数位数，默认 0
inline double round(double num, int decimal_places = 0) {
	double mult = std::pow(10.0, decimal_places);
	return std::floor(num * mult + 0.5) / mult;
}

/// 角度余弦（输入为角度制）
inline double d_cos(double angle_deg) {
	return std::cos(angle_deg * M_PI / 180.0);
}

/// 角度正弦（输入为角度制）
inline double d_sin(double angle_deg) {
	return std::sin(angle_deg * M_PI / 180.0);
}

/// 角度反正切（返回角度制）
inline double d_atan(double y, double x) {
	return std::atan2(y, x) * 180.0 / M_PI;
}

/// 将值限制在 [min_val, max_val] 范围内
inline double clamp(double value, double min_val, double max_val) {
	return std::max(min_val, std::min(max_val, value));
}

} // namespace math
} // namespace mocha
