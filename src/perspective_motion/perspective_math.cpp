// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪数学核心模块实现
// 对照 MoonScript 版 arch.Perspective 移植

#include "perspective_math.h"

#include <cmath>
#include <algorithm>

namespace mocha {
// ============================================================================
// 基础数学工具
// ============================================================================

	void PerspectiveMath::Solve2x2(double a11, double a12, double a21, double a22,
									double b1, double b2, double &x1, double &x2) {
		// 简单主元选择
		if (std::abs(a11) < std::abs(a21)) {
			std::swap(b1, b2);
			std::swap(a11, a21);
			std::swap(a12, a22);
		}
		// LU 分解 i=1
		a21 = a21 / a11;
		// i=2
		a22 = a22 - a21 * a12;
		// 前向代入
		double z1 = b1;
		double z2 = b2 - a21 * z1;
		// 后向代入
		x2 = z2 / a22;
		x1 = (z1 - a12 * x2) / a11;
	}

	Vector2D PerspectiveMath::QuadMidpoint(const Quad &quad) {
		Vector2D diag1 = quad[2] - quad[0];
		Vector2D diag2 = quad[1] - quad[3];
		Vector2D b = quad[3] - quad[0];
		double center_la1, center_la2;
		Solve2x2(
			diag1.X(), diag2.X(), diag1.Y(), diag2.Y(),
			b.X(), b.Y(), center_la1, center_la2
		);
		return quad[0] + center_la1 * diag1;
	}

	void PerspectiveMath::UnwrapQuadRel(const Quad &quad,
										double &x1, double &x2, double &x3, double &x4,
										double &y1, double &y2, double &y3, double &y4) {
		x1 = quad[0].X();
		x2 = quad[1].X() - x1;
		x3 = quad[2].X() - x1;
		x4 = quad[3].X() - x1;
		y1 = quad[0].Y();
		y2 = quad[1].Y() - y1;
		y3 = quad[2].Y() - y1;
		y4 = quad[3].Y() - y1;
	}

	Quad PerspectiveMath::MakeRect(Vector2D a, Vector2D b) {
		return Quad{
			Vector2D(a.X(), a.Y()),
			Vector2D(b.X(), a.Y()),
			Vector2D(b.X(), b.Y()),
			Vector2D(a.X(), b.Y()),
		};
	}

// ============================================================================
// 透视映射：四边形 ↔ 单位方块
// ============================================================================

	Vector2D PerspectiveMath::XYToUV(const Quad &quad, Vector2D xy) {
		double x1, x2, x3, x4, y1, y2, y3, y4;
		UnwrapQuadRel(quad, x1, x2, x3, x4, y1, y2, y3, y4);
		double x = xy.X() - x1;
		double y = xy.Y() - y1;

		// Mathematica 推导的闭式分式
		double u = -(((x3 * y2 - x2 * y3) * (x4 * y - x * y4) * (x4 * (-y2 + y3) + x3 * (y2 - y4) + x2 * (-y3 + y4))) /
					(x3 * x3 * (x4 * y2 * y2 * (-y + y4) + y4 * (x * y2 * (y2 - y4) + x2 * (y - y2) * y4)) +
					x3 * (x4 * x4 * y2 * y2 * (y - y3) + 2 * x4 * (x2 * y * y3 * (y2 - y4) + x * y2 * (-y2 + y3) * y4) +
						x2 * y4 * (x2 * (-y + y3) * y4 + 2 * x * y2 * (-y3 + y4))) +
					y3 * (x * x4 * x4 * y2 * (y2 - y3) + x2 * x4 * x4 * (y2 * y3 + y * (-2 * y2 + y3)) -
						x2 * x2 * (x4 * y * (y3 - 2 * y4) + x4 * y3 * y4 + x * y4 * (-y3 + y4)))));

		double v = ((x2 * y - x * y2) * (x4 * y3 - x3 * y4) * (x4 * (y2 - y3) + x2 * (y3 - y4) + x3 * (-y2 + y4))) /
					(x3 * (x4 * x4 * y2 * y2 * (-y + y3) + x2 * y4 * (2 * x * y2 * (y3 - y4) + x2 * (y - y3) * y4) -
							2 * x4 * (x2 * y * y3 * (y2 - y4) + x * y2 * (-y2 + y3) * y4)) +
					x3 * x3 * (x4 * y2 * y2 * (y - y4) + y4 * (x2 * (-y + y2) * y4 + x * y2 * (-y2 + y4))) +
					y3 * (x * x4 * x4 * y2 * (-y2 + y3) + x2 * x4 * x4 * (2 * y * y2 - y * y3 - y2 * y3) +
						x2 * x2 * (x4 * y * (y3 - 2 * y4) + x4 * y3 * y4 + x * y4 * (-y3 + y4))));

		return Vector2D(static_cast<float>(u), static_cast<float>(v));
	}

	Vector2D PerspectiveMath::UVToXY(const Quad &quad, Vector2D uv) {
		double x1, x2, x3, x4, y1, y2, y3, y4;
		UnwrapQuadRel(quad, x1, x2, x3, x4, y1, y2, y3, y4);
		double u = uv.X();
		double v = uv.Y();

		// Mathematica 推导的闭式分式
		double d = (x4 * ((-1 + u + v) * y2 + y3 - v * y3) +
					x3 * (y2 - u * y2 + (-1 + v) * y4) +
					x2 * ((-1 + u) * y3 - (-1 + u + v) * y4));
		double x = (v * x4 * (x3 * y2 - x2 * y3) + u * x2 * (x4 * y3 - x3 * y4)) / d;
		double y = (v * y4 * (x3 * y2 - x2 * y3) + u * y2 * (x4 * y3 - x3 * y4)) / d;

		return Vector2D(static_cast<float>(x + x1), static_cast<float>(y + y1));
	}

	void PerspectiveMath::DUVToXY(const Quad &quad, Vector2D uv,
								double &dxdu, double &dxdv,
								double &dydu, double &dydv) {
		double x1, x2, x3, x4, y1, y2, y3, y4;
		UnwrapQuadRel(quad, x1, x2, x3, x4, y1, y2, y3, y4);
		double u = uv.X();
		double v = uv.Y();

		double d = (x4 * ((-1 + u + v) * y2 + y3 - v * y3) +
					x3 * (y2 - u * y2 + (-1 + v) * y4) +
					x2 * ((-1 + u) * y3 - (-1 + u + v) * y4));
		double d2 = d * d;

		// 解析雅可比（对应 MoonScript d_uv_to_xy）
		double num_x = v * x4 * (x3 * y2 - x2 * y3) + u * x2 * (x4 * y3 - x3 * y4);

		dxdu = (x2 * (x4 * y3 - x3 * y4) * d + (x3 * y2 - x4 * y2 + x2 * (-y3 + y4)) * num_x) / d2;
		dxdv = (x4 * (x3 * y2 - x2 * y3) * d - (x4 * (y2 - y3) + (-x2 + x3) * y4) * num_x) / d2;

		double num_y = v * y4 * (x3 * y2 - x2 * y3) + u * y2 * (x4 * y3 - x3 * y4);

		dydu = ((-1 + v) * x3 * x3 * y2 * (y2 - y4) * y4 +
				y3 * ((-1 + v) * x4 * x4 * y2 * (y2 - y3) + v * x2 * x2 * (y3 - y4) * y4 + x2 * x4 * y2 * (-y3 + y4)) +
				x3 * y2 * (2 * (-1 + v) * x4 * y3 * y4 - (-1 + 2 * v) * x2 * (y3 - y4) * y4 + x4 * y2 * (y3 + y4 - 2 * v * y4))) / d2;
		dydv = ((x3 * y2 - x2 * y3) * y4 * (-(x4 * y2) - x2 * y3 + x4 * y3 + x3 * (y2 - y4) + x2 * y4) +
				u * (x4 * x4 * y2 * y3 * (-y2 + y3) + 2 * x3 * x4 * y2 * (y2 - y3) * y4 +
					y4 * (2 * x2 * x3 * y2 * (y3 - y4) + x3 * x3 * y2 * (-y2 + y4) + x2 * x2 * y3 * (-y3 + y4)))) / d2;
	}

// ============================================================================
// TransformPoints: 标签 → 屏幕四边形
// ============================================================================

	std::optional<Quad> PerspectiveMath::TransformPoints(const PerspectiveTagVals &tags,
														double width, double height,
														double layout_scale) {
		double scaled_screen_z = kScreenZ * layout_scale;

		// 构造矩形点
		Quad rect = MakeRect(Vector2D(0, 0), Vector2D(static_cast<float>(width), static_cast<float>(height)));
		std::vector<Vector3D> points;
		for (const auto &p : rect)
			points.emplace_back(p);

		// 计算对齐偏移
		double shiftx = 0.0, shifty = 0.0; {
			int an = tags.align;
			if (an < 1 || an > 9) an = 7;
			int col = (an - 1) % 3; // 0=左, 1=中, 2=右
			int row = (an - 1) / 3; // 0=下, 1=中, 2=上

			switch (col) {
				case 1: shiftx = -width / 2.0;
					break;
				case 2: shiftx = -width;
					break;
				default: break;
			}
			switch (row) {
				case 0: shifty = -height;
					break;
				case 1: shifty = -height / 2.0;
					break;
				default: break;
			}
		}

		Vector3D pos(static_cast<float>(tags.pos_x), static_cast<float>(tags.pos_y), 0);
		Vector3D org(static_cast<float>(tags.org_x), static_cast<float>(tags.org_y), 0);

		for (auto &p : points) {
			// Shear（\fax, \fay）
			double px = p.X() + p.Y() * tags.shear_x;
			double py = p.X() * tags.shear_y + p.Y();
			p = Vector3D(static_cast<float>(px), static_cast<float>(py), 0);

			// 平移到对齐点
			p = p + Vector3D(static_cast<float>(shiftx), static_cast<float>(shifty), 0);

			// 缩放
			px = p.X() * tags.scale_x / 100.0;
			py = p.Y() * tags.scale_y / 100.0;
			p = Vector3D(static_cast<float>(px), static_cast<float>(py), 0);

			// 平移 pos - org
			p = p + pos - org;

			// 3D 旋转 ZXY
			p = p.RotateZ(static_cast<float>(-tags.angle * M_PI / 180.0));
			p = p.RotateX(static_cast<float>(-tags.angle_x * M_PI / 180.0));
			p = p.RotateY(static_cast<float>(tags.angle_y * M_PI / 180.0));

			// 透视投影
			double proj_factor = scaled_screen_z / (p.Z() + scaled_screen_z);
			p = Vector3D(p.XY() * static_cast<float>(proj_factor), 0);

			// 回加 org
			p = p + org;
		}

		Quad result;
		for (const auto &p : points)
			result.push_back(p.XY());

		return result;
	}

// ============================================================================
// SolveOrgMode3: orgMode==3 的圆/直线求解
// ============================================================================

	Vector2D PerspectiveMath::SolveOrgMode3(Vector2D q0, Vector2D v1, Vector2D v3,
											double z1, double z3,
											double screen_z, Vector2D midpoint) {
		double a = (1 - z1) * (1 - z3);
		// b = z1*v1 + z3*v3 - z1*z3*(v1+v3)
		Vector2D b = v1 * static_cast<float>(z1) +
					v3 * static_cast<float>(z3) -
					(v1 + v3) * static_cast<float>(z1 * z3);
		double c = z1 * z3 * v1.Dot(v3) + (z1 - 1) * (z3 - 1) * screen_z * screen_z;

		// 默认 t = q0 - midpoint
		Vector2D t = q0 - midpoint;

		if (std::abs(a) < 1e-10) {
			// 线性情形：方程为 b·t = c，求直线上距原始 t 最近的点
			if (b.SquareLen() > 1e-15) {
				double factor = (c - t.Dot(b)) / b.SquareLen();
				t = t + b * static_cast<float>(factor);
			}
			// 若 b=0，保持原 org
		} else {
			// 圆情形：a*(x²+y²) - b·x + c = 0
			Vector2D circle_center = b / static_cast<float>(2 * a);
			double sqradius = (b.SquareLen() / (4 * a) - c) / a;

			if (sqradius <= 0) {
				// 圆退化为点，保持 t = q0 - midpoint 不变（与 MoonScript 原版行为一致）
			} else {
				double radius = std::sqrt(sqradius);
				Vector2D center2t = t - circle_center;
				if (center2t.Len() == 0) {
					t = circle_center + Vector2D(static_cast<float>(radius), 0);
				} else {
					t = circle_center + center2t / center2t.Len() * static_cast<float>(radius);
				}
			}
		}

		Vector2D result = q0 - t;
		// if computed org is too far from quad, fall back to midpoint
		double quad_size = std::sqrt(v1.SquareLen() + v3.SquareLen());
		if (quad_size > 0 && (result - midpoint).Len() > 100.0 * quad_size)
			return midpoint;
		return result;
	}

// ============================================================================
// TagsFromQuad: 屏幕四边形 → 标签
// ============================================================================

	bool PerspectiveMath::TagsFromQuad(PerspectiveTagVals &out_tags,
										const Quad &quad,
										double width, double height,
										int org_mode, double layout_scale,
										std::vector<std::string> *warnings) {
		double scaled_screen_z = kScreenZ * layout_scale;

		Vector2D q0 = quad[0];
		Vector2D q1 = quad[1];
		Vector2D q2 = quad[2];
		Vector2D q3 = quad[3];

		// 步骤1：找到投影到该四边形的平行四边形
		// 求解 z1, z3 使得 q0 + z1*(q1-q0) 和 q0 + z3*(q3-q0) 投影后形成平行四边形
		double z1, z3; {
			Vector2D diag = q2 - q0;
			Vector2D side2 = q1 - q2;
			Vector2D side3 = q3 - q2;
			Solve2x2(
				side2.X(), side3.X(), side2.Y(), side3.Y(),
				-diag.X(), -diag.Y(), z1, z3
			);
		}

		// 步骤2：确定 \org
		Vector2D org;
		Vector2D midpoint = QuadMidpoint(quad);

		if (org_mode == 2) {
			// 强制中心 \org
			org = midpoint;
		} else if (org_mode == 3) {
			// 尝试令 \fax 接近 0
			Vector2D v1 = q1 - q0;
			Vector2D v3 = q3 - q0;
			org = SolveOrgMode3(q0, v1, v3, z1, z3, scaled_screen_z, midpoint);
		} else {
			// org_mode == 1: 保持原有 \org，不修改
			org = Vector2D(
				static_cast<float>(out_tags.org_x),
				static_cast<float>(out_tags.org_y)
			);
		}

		// 步骤3：相对 org 归一化并反投影
		q0 = q0 - org;
		q1 = q1 - org;
		q2 = q2 - org;
		q3 = q3 - org;

		Vector3D r0(q0, static_cast<float>(scaled_screen_z));
		Vector3D r1(static_cast<float>(z1) * Vector3D(q1, static_cast<float>(scaled_screen_z)));
		Vector3D r2(static_cast<float>(z1 + z3 - 1) * Vector3D(q2, static_cast<float>(scaled_screen_z)));
		Vector3D r3(static_cast<float>(z3) * Vector3D(q3, static_cast<float>(scaled_screen_z)));

		// 找到投影到原点的点的 z 坐标
		double orgla0, orgla1; {
			Vector3D side0 = r1 - r0;
			Vector3D side1 = r3 - r0;
			Solve2x2(
				side0.X(), side1.X(), side0.Y(), side1.Y(),
				-r0.X(), -r0.Y(), orgla0, orgla1
			);
		}
		double orgz = (r0 + static_cast<float>(orgla0) * (r1 - r0) + static_cast<float>(orgla1) * (r3 - r0)).Z();

		// 归一化使原点处 z=screenZ，并将屏幕平面移至 z=0
		float scale_factor = static_cast<float>(scaled_screen_z / orgz);
		r0 = r0 * scale_factor - Vector3D(0, 0, static_cast<float>(scaled_screen_z));
		r1 = r1 * scale_factor - Vector3D(0, 0, static_cast<float>(scaled_screen_z));
		r2 = r2 * scale_factor - Vector3D(0, 0, static_cast<float>(scaled_screen_z));
		r3 = r3 * scale_factor - Vector3D(0, 0, static_cast<float>(scaled_screen_z));

		// 步骤4：分解旋转
		// 计算法向量
		Vector3D n = (r1 - r0).Cross(r3 - r0);
		double roty = std::atan2(n.X(), n.Z());
		// 匹配 MoonScript: 对法向量先做 Y 旋转再算 rotx
		n = n.RotateY(static_cast<float>(roty));
		double rotx = std::atan2(n.Y(), n.Z());

		// 旋转到 z=0 平面
		auto rotate_all = [&](double ry, double rx) {
			r0 = r0.RotateY(static_cast<float>(ry)).RotateX(static_cast<float>(rx));
			r1 = r1.RotateY(static_cast<float>(ry)).RotateX(static_cast<float>(rx));
			r2 = r2.RotateY(static_cast<float>(ry)).RotateX(static_cast<float>(rx));
			r3 = r3.RotateY(static_cast<float>(ry)).RotateX(static_cast<float>(rx));
		};
		rotate_all(roty, rotx);

		// 绕 Z 旋转使顶边水平
		Vector3D ab = r1 - r0;
		double rotz = std::atan2(ab.Y(), ab.X());

		r0 = r0.RotateZ(static_cast<float>(-rotz));
		r1 = r1.RotateZ(static_cast<float>(-rotz));
		r2 = r2.RotateZ(static_cast<float>(-rotz));
		r3 = r3.RotateZ(static_cast<float>(-rotz));

		// 步骤5：在水平平行四边形中求解 scale 和 shear
		ab = r1 - r0;
		Vector3D ad = r3 - r0;
		double rawfax = ad.X() / ad.Y();

		double quadwidth = ab.Len();
		double quadheight = std::abs(ad.Y());

		width = std::max(width, 0.01);
		height = std::max(height, 0.01);
		double scalex = quadwidth / width;
		double scaley = quadheight / height;

		// 步骤6：计算 \pos（对应 MoonScript tagsFromQuad 中 quad[1]\project(2) + alignment）
		double shiftv = (out_tags.align <= 3) ? 1.0 : ((out_tags.align <= 6) ? 0.5 : 0.0);
		double shifth = (out_tags.align % 3 == 0) ? 1.0 : ((out_tags.align % 3 == 2) ? 0.5 : 0.0);

		Vector2D pos = org + r0.XY() +
						Vector2D(
							static_cast<float>(quadwidth * shifth),
							static_cast<float>(quadheight * shiftv)
						);

		// 步骤7：填充输出标签
		double angle_x_deg = rotx * 180.0 / M_PI;
		double angle_y_deg = -roty * 180.0 / M_PI;
		double angle_z_deg = -rotz * 180.0 / M_PI;

		out_tags.pos_x = pos.X();
		out_tags.pos_y = pos.Y();
		out_tags.org_x = org.X();
		out_tags.org_y = org.Y();
		out_tags.angle = angle_z_deg;
		out_tags.angle_x = angle_x_deg;
		out_tags.angle_y = angle_y_deg;
		out_tags.scale_x = 100.0 * scalex;
		out_tags.scale_y = 100.0 * scaley;
		out_tags.shear_x = rawfax * scaley / scalex;
		out_tags.shear_y = 0;

		// 步骤8：数值验证
		std::vector<double> allvalues = {
			out_tags.shear_x, out_tags.scale_x, out_tags.scale_y,
			out_tags.angle, out_tags.angle_x, out_tags.angle_y,
			out_tags.org_x, out_tags.org_y,
			out_tags.pos_x, out_tags.pos_y
		};
		for (double v : allvalues) {
			if (!std::isfinite(v)) {
				if (warnings)
					warnings->push_back("Non-finite value in TagsFromQuad output");
				return false;
			}
		}

		return true;
	}
} // namespace mocha
