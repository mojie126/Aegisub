// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪数学核心模块
// 对应 MoonScript 版 arch.Perspective
//
// 从 visual_tool_perspective.cpp 提取并泛化，提供：
//   - 四边形 ↔ 单位方块 透视映射（xy↔uv）
//   - transformPoints：根据 ASS 标签计算屏幕四角
//   - tagsFromQuad：根据屏幕四角反推 ASS 标签
//   - orgMode==3 的圆/直线求解

#pragma once

#include "../vector2d.h"
#include "../vector3d.h"

#include <vector>
#include <string>
#include <optional>
#include <cmath>

namespace mocha {
	/// 四边形的四个角点（屏幕坐标）
	/// 顺序：左上→右上→右下→左下（屏幕 Y 轴向下）
	using Quad = std::vector<Vector2D>;

	/// 标签值结构体，对应 prepareForPerspective 返回的规范化标签
	struct PerspectiveTagVals {
		double pos_x = 0;
		double pos_y = 0;
		double org_x = 0;
		double org_y = 0;
		double angle = 0; // \frz (度)
		double angle_x = 0; // \frx (度)
		double angle_y = 0; // \fry (度)
		double scale_x = 100; // \fscx
		double scale_y = 100; // \fscy
		double shear_x = 0; // \fax
		double shear_y = 0; // \fay
		double outline_x = 0; // \xbord
		double outline_y = 0; // \ybord
		double shadow_x = 0; // \xshad
		double shadow_y = 0; // \yshad
		int align = 7; // \an (默认 7: 左上)
	};

	/// 透视追踪数学工具类
	/// 所有函数均为静态方法，无状态
	class PerspectiveMath {
	public:
		/// 屏幕常量，用于透视投影深度计算
		static constexpr double kScreenZ = 312.5;

		/// @name 对齐偏移表
		/// @brief an_xshift/an_yshift：对应 ASS \an 对齐标签的 X/Y 偏移比例
		static constexpr double an_xshift[9] = {0, 0.5, 1, 0, 0.5, 1, 0, 0.5, 1};
		static constexpr double an_yshift[9] = {1, 1, 1, 0.5, 0.5, 0.5, 0, 0, 0};

		// ========================================================================
		// 基础数学工具
		// ========================================================================

		/// @brief 解 2x2 线性方程组
		/// a11*x1 + a12*x2 = b1
		/// a21*x1 + a22*x2 = b2
		/// 使用带简单主元选择的 LU 分解
		static void Solve2x2(double a11, double a12, double a21, double a22,
							double b1, double b2, double &x1, double &x2);

		/// @brief 计算四边形对角线交点
		static Vector2D QuadMidpoint(const Quad &quad);

		/// @brief 获取四边形相对于第一个角点的相对坐标
		/// 返回 x1,y1（第一个角点坐标）和 x2..x4, y2..y4（相对坐标）
		static void UnwrapQuadRel(const Quad &quad,
								double &x1, double &x2, double &x3, double &x4,
								double &y1, double &y2, double &y3, double &y4);

		/// @brief 创建矩形四角
		/// @param a 左上角
		/// @param b 右下角
		static Quad MakeRect(Vector2D a, Vector2D b);

		// ========================================================================
		// 透视映射：四边形 ↔ 单位方块
		// ========================================================================

		/// @brief 将屏幕坐标点映射到单位方块 (uv) 空间
		/// 对应 MoonScript Quad:xy_to_uv()
		/// 使用 Mathematica 推导的闭式分式表达式
		static Vector2D XYToUV(const Quad &quad, Vector2D xy);

		/// @brief 将单位方块坐标点映射回屏幕坐标
		/// 对应 MoonScript Quad:uv_to_xy()
		static Vector2D UVToXY(const Quad &quad, Vector2D uv);

		/// @brief uv_to_xy 的雅可比矩阵（2x2 解析形式）
		/// 对应 MoonScript Quad:d_uv_to_xy()
		/// @param quad 四边形
		/// @param uv uv 坐标点
		/// @return {dxdu, dxdv, dydu, dydv} 四个分量
		static void DUVToXY(const Quad &quad, Vector2D uv,
							double &dxdu, double &dxdv,
							double &dydu, double &dydv);

		// ========================================================================
		// 核心变换：标签 ↔ 四边形
		// ========================================================================

		/// @brief 根据 ASS 标签计算文本矩形在屏幕上的四角投影
		/// 对应 MoonScript transformPoints()
		///
		/// 变换步骤：
		///   1. 构造矩形点 (0,0)-(w,0)-(w,h)-(0,h)
		///   2. 应用 shear（\fax, \fay）
		///   3. 平移到对齐点（根据 \an）
		///   4. 缩放（\fscx, \fscy）
		///   5. 平移到 pos - org
		///   6. 3D 旋转 Z→X→Y 顺序
		///   7. 透视投影
		///   8. 回加 org 偏移
		///
		/// @param tags 标签值
		/// @param width 文本/绘图的原始宽度
		/// @param height 文本/绘图的原始高度
		/// @param layout_scale PlayResY / LayoutResY 缩放因子
		/// @return 屏幕四边形，失败返回 std::nullopt
		static std::optional<Quad> TransformPoints(const PerspectiveTagVals &tags,
													double width, double height,
													double layout_scale = 1.0);

		/// @brief 根据屏幕四边形反推 ASS 标签值
		/// 对应 MoonScript tagsFromQuad()
		///
		/// 步骤：
		///   1. 以 quad 为屏幕四角，先求解平行四边形化（z24 参数）
		///   2. 根据 orgMode 选择 \org 计算策略
		///   3. 反投影（unproject）到 3D 空间
		///   4. 分解旋转分量（Y→X→Z 顺序）
		///   5. 在水平平行四边形中求解 scale 和 shear
		///   6. 计算 \pos
		///
		/// @param[out] out_tags 输出的标签值
		/// @param quad 屏幕上的四边形
		/// @param width 文本/绘图的原始宽度
		/// @param height 文本/绘图的原始高度
		/// @param org_mode \org 选择策略：1=保持原值, 2=中心, 3=尝试令\fax=0
		/// @param layout_scale PlayResY / LayoutResY 缩放因子
		/// @param[out] warnings 警告信息列表（可选）
		/// @return 成功返回 true，数值异常返回 false
		static bool TagsFromQuad(PerspectiveTagVals &out_tags,
								const Quad &quad,
								double width, double height,
								int org_mode, double layout_scale,
								std::vector<std::string> *warnings = nullptr);

	private:
		/// @brief orgMode==3 圆/直线求解
		/// 寻找使反投影四边形的 \fax 接近 0 的 org 偏移
		/// @param q0 四边形第一个角点
		/// @param v1 q1 - q0
		/// @param v3 q3 - q0
		/// @param z1 平行四边形参数 1
		/// @param z3 平行四边形参数 3
		/// @param screen_z 缩放后的屏幕深度
		/// @param midpoint 四边形中心点
		/// @return 计算得到的 org 坐标
		static Vector2D SolveOrgMode3(Vector2D q0, Vector2D v1, Vector2D v3,
									double z1, double z3,
									double screen_z, Vector2D midpoint);
	};
} // namespace mocha
