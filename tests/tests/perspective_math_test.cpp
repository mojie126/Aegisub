// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪数学模块单元测试

#include <main.h>

#include "../src/perspective_motion/perspective_math.h"

#include <cmath>
#include <limits>
#include <string>

using namespace mocha;

// ============================================================================
// Solve2x2 测试
// ============================================================================

class PerspectiveMathTest : public libagi {};

TEST(PerspectiveMathTest, Solve2x2Simple) {
	double x1, x2;
	// 2*x1 + 3*x2 = 8
	// 1*x1 + 1*x2 = 3
	// 解：x1=1, x2=2
	PerspectiveMath::Solve2x2(2, 3, 1, 1, 8, 3, x1, x2);
	EXPECT_NEAR(x1, 1.0, 1e-9);
	EXPECT_NEAR(x2, 2.0, 1e-9);
}

TEST(PerspectiveMathTest, Solve2x2Pivot) {
	double x1, x2;
	// 需要主元选择的场景
	// 1*x1 + 1*x2 = 5
	// 3*x1 + 2*x2 = 12
	PerspectiveMath::Solve2x2(1, 1, 3, 2, 5, 12, x1, x2);
	EXPECT_NEAR(x1, 2.0, 1e-9);
	EXPECT_NEAR(x2, 3.0, 1e-9);
}

// ============================================================================
// QuadMidpoint 测试
// ============================================================================

TEST(PerspectiveMathTest, QuadMidpointRect) {
	auto q = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(100, 100));
	auto mid = PerspectiveMath::QuadMidpoint(q);
	EXPECT_NEAR(mid.X(), 50.0, 1e-5);
	EXPECT_NEAR(mid.Y(), 50.0, 1e-5);
}

TEST(PerspectiveMathTest, QuadMidpointConvex) {
	Quad q = {
		Vector2D(100, 200),
		Vector2D(300, 180),
		Vector2D(320, 320),
		Vector2D(80, 310),
	};
	auto mid = PerspectiveMath::QuadMidpoint(q);
	// 对角线交点应在四边形内
	EXPECT_GT(mid.X(), 80);
	EXPECT_LT(mid.X(), 320);
	EXPECT_GT(mid.Y(), 180);
	EXPECT_LT(mid.Y(), 320);
}

// ============================================================================
// MakeRect 测试
// ============================================================================

TEST(PerspectiveMathTest, MakeRectBasic) {
	auto r = PerspectiveMath::MakeRect(Vector2D(10, 20), Vector2D(110, 120));
	EXPECT_FLOAT_EQ(r[0].X(), 10);
	EXPECT_FLOAT_EQ(r[0].Y(), 20);
	EXPECT_FLOAT_EQ(r[1].X(), 110);
	EXPECT_FLOAT_EQ(r[1].Y(), 20);
	EXPECT_FLOAT_EQ(r[2].X(), 110);
	EXPECT_FLOAT_EQ(r[2].Y(), 120);
	EXPECT_FLOAT_EQ(r[3].X(), 10);
	EXPECT_FLOAT_EQ(r[3].Y(), 120);
}

// ============================================================================
// XY ↔ UV 往返测试
// ============================================================================

TEST(PerspectiveMathTest, XYToUV_Roundtrip) {
	// 标准矩形四边形
	Quad quad = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080));

	// 在四边形内选若干测试点
	std::vector<Vector2D> test_points = {
		Vector2D(480, 270),
		Vector2D(960, 540),
		Vector2D(100, 100),
		Vector2D(1820, 980),
		Vector2D(960, 100),
	};

	for (const auto &xy : test_points) {
		auto uv = PerspectiveMath::XYToUV(quad, xy);
		auto xy2 = PerspectiveMath::UVToXY(quad, uv);

		EXPECT_NEAR(xy.X(), xy2.X(), 1e-5)
			<< "XYToUV->UVToXY roundtrip failed for (" << xy.X() << ", " << xy.Y() << ")";
		EXPECT_NEAR(xy.Y(), xy2.Y(), 1e-5)
			<< "XYToUV->UVToXY roundtrip failed for (" << xy.X() << ", " << xy.Y() << ")";
	}
}

TEST(PerspectiveMathTest, XYToUV_RoundtripPerspective) {
	// 透视四边形（模拟3D旋转后的投影）
	Quad quad = {
		Vector2D(200, 300),
		Vector2D(1700, 250),
		Vector2D(1800, 900),
		Vector2D(150, 850),
	};

	std::vector<Vector2D> test_points = {
		Vector2D(960, 540),   // 中心
		Vector2D(500, 400),
		Vector2D(1400, 600),
		Vector2D(800, 700),
	};

	for (const auto &xy : test_points) {
		auto uv = PerspectiveMath::XYToUV(quad, xy);
		auto xy2 = PerspectiveMath::UVToXY(quad, uv);

		EXPECT_NEAR(xy.X(), xy2.X(), 1e-4)
			<< "Perspective roundtrip X failed for (" << xy.X() << ", " << xy.Y() << ")";
		EXPECT_NEAR(xy.Y(), xy2.Y(), 1e-4)
			<< "Perspective roundtrip Y failed for (" << xy.X() << ", " << xy.Y() << ")";
	}
}

TEST(PerspectiveMathTest, UVToXY_Corners) {
	// 四边形四角映射到单位方块四角
	Quad quad = {
		Vector2D(100, 200),
		Vector2D(400, 180),
		Vector2D(420, 500),
		Vector2D(80, 480),
	};

	// uv=(0,0) -> 左上角 q0
	auto xy0 = PerspectiveMath::UVToXY(quad, Vector2D(0, 0));
	EXPECT_NEAR(xy0.X(), quad[0].X(), 1e-4);
	EXPECT_NEAR(xy0.Y(), quad[0].Y(), 1e-4);

	// uv=(1,0) -> 右上角 q1
	auto xy1 = PerspectiveMath::UVToXY(quad, Vector2D(1, 0));
	EXPECT_NEAR(xy1.X(), quad[1].X(), 1e-4);
	EXPECT_NEAR(xy1.Y(), quad[1].Y(), 1e-4);

	// uv=(1,1) -> 右下角 q2
	auto xy2 = PerspectiveMath::UVToXY(quad, Vector2D(1, 1));
	EXPECT_NEAR(xy2.X(), quad[2].X(), 1e-4);
	EXPECT_NEAR(xy2.Y(), quad[2].Y(), 1e-4);

	// uv=(0,1) -> 左下角 q3
	auto xy3 = PerspectiveMath::UVToXY(quad, Vector2D(0, 1));
	EXPECT_NEAR(xy3.X(), quad[3].X(), 1e-4);
	EXPECT_NEAR(xy3.Y(), quad[3].Y(), 1e-4);
}

// ============================================================================
// 雅可比测试：解析 vs 数值差分
// ============================================================================

TEST(PerspectiveMathTest, JacobianAnalyticVsNumerical) {
	Quad quad = {
		Vector2D(150, 250),
		Vector2D(1600, 200),
		Vector2D(1700, 950),
		Vector2D(100, 900),
	};
	Vector2D uv(0.3f, 0.6f);

	double dxdu, dxdv, dydu, dydv;
	PerspectiveMath::DUVToXY(quad, uv, dxdu, dxdv, dydu, dydv);

	// 数值差分（中心差分）
	double eps = 1e-4;
	auto xy = PerspectiveMath::UVToXY(quad, uv);
	auto xy_up = PerspectiveMath::UVToXY(quad, Vector2D(uv.X() + static_cast<float>(eps), uv.Y()));
	auto xy_um = PerspectiveMath::UVToXY(quad, Vector2D(uv.X() - static_cast<float>(eps), uv.Y()));
	auto xy_vp = PerspectiveMath::UVToXY(quad, Vector2D(uv.X(), uv.Y() + static_cast<float>(eps)));
	auto xy_vm = PerspectiveMath::UVToXY(quad, Vector2D(uv.X(), uv.Y() - static_cast<float>(eps)));

	double num_dxdu = (xy_up.X() - xy_um.X()) / (2 * eps);
	double num_dxdv = (xy_vp.X() - xy_vm.X()) / (2 * eps);
	double num_dydu = (xy_up.Y() - xy_um.Y()) / (2 * eps);
	double num_dydv = (xy_vp.Y() - xy_vm.Y()) / (2 * eps);

	EXPECT_NEAR(dxdu, num_dxdu, 1.5)  // 解析 vs 数值差分：大值相对误差 <0.01%
		<< "Analytic dxdu differs from numerical";
	EXPECT_NEAR(dxdv, num_dxdv, 0.1)
		<< "Analytic dxdv differs from numerical";
	EXPECT_NEAR(dydu, num_dydu, 0.15)
		<< "Analytic dydu differs from numerical";
	EXPECT_NEAR(dydv, num_dydv, 0.1)
		<< "Analytic dydv differs from numerical";
}

// ============================================================================
// TransformPoints 测试
// ============================================================================

TEST(PerspectiveMathTest, TransformPointsIdentity) {
	// 无变换：所有标签为默认值
	PerspectiveTagVals tags;
	tags.pos_x = 0;
	tags.pos_y = 0;
	tags.org_x = 0;
	tags.org_y = 0;

	auto result = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(result.has_value());

	auto &quad = *result;
	// 无变换时应得到原矩形
	EXPECT_NEAR(quad[0].X(), 0, 1e-4);
	EXPECT_NEAR(quad[0].Y(), 0, 1e-4);
	EXPECT_NEAR(quad[2].X(), 100, 1e-4);
	EXPECT_NEAR(quad[2].Y(), 50, 1e-4);
}

TEST(PerspectiveMathTest, TransformPointsTranslation) {
	PerspectiveTagVals tags;
	tags.pos_x = 200;
	tags.pos_y = 100;
	tags.org_x = 200;
	tags.org_y = 100;

	auto result = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(result.has_value());

	auto &quad = *result;
	EXPECT_NEAR(quad[0].X(), 200, 1e-4);
	EXPECT_NEAR(quad[0].Y(), 100, 1e-4);
	EXPECT_NEAR(quad[2].X(), 300, 1e-4);
	EXPECT_NEAR(quad[2].Y(), 150, 1e-4);
}

TEST(PerspectiveMathTest, TransformPointsScale) {
	PerspectiveTagVals tags;
	tags.scale_x = 200;
	tags.scale_y = 150;

	auto result = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(result.has_value());

	auto &quad = *result;
	// 宽度 100 * 2 = 200, 高度 50 * 1.5 = 75
	EXPECT_NEAR(quad[2].X() - quad[0].X(), 200, 1e-4);
	EXPECT_NEAR(quad[2].Y() - quad[0].Y(), 75, 1e-4);
}

TEST(PerspectiveMathTest, TransformPointsZRotation) {
	PerspectiveTagVals tags;
	tags.angle = 90; // 绕 Z 轴旋转 90 度

	auto result = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(result.has_value());

	auto &quad = *result;
	// 旋转 90 度后，原右上角 (100,0) 在屏幕坐标系中变为 (0,-100)
	EXPECT_NEAR(quad[1].X(), 0, 0.1);
	EXPECT_NEAR(quad[1].Y(), -100, 0.1);
}

// ============================================================================
// TagsFromQuad ↔ TransformPoints 往返测试
// ============================================================================

TEST(PerspectiveMathTest, TagsFromQuad_RoundtripIdentity) {
	// 从无变换标签出发
	PerspectiveTagVals tags;
	tags.pos_x = 500;
	tags.pos_y = 300;
	tags.org_x = 500;
	tags.org_y = 300;
	tags.align = 7;

	auto quad_opt = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(quad_opt.has_value());

	// 从四边形恢复标签
	PerspectiveTagVals out_tags = tags; // 保留 align 和旧值
	bool ok = PerspectiveMath::TagsFromQuad(out_tags, *quad_opt, 100, 50, 2, 1.0);
	ASSERT_TRUE(ok);

	// 关键量应在容差内
	EXPECT_NEAR(out_tags.pos_x, tags.pos_x, 0.5);
	EXPECT_NEAR(out_tags.pos_y, tags.pos_y, 0.5);
	EXPECT_NEAR(out_tags.scale_x, tags.scale_x, 1e-3);
	EXPECT_NEAR(out_tags.scale_y, tags.scale_y, 1e-3);
	EXPECT_NEAR(out_tags.angle, tags.angle, 0.01);
}

TEST(PerspectiveMathTest, TagsFromQuad_RoundtripScaled) {
	PerspectiveTagVals tags;
	tags.pos_x = 600;
	tags.pos_y = 400;
	tags.org_x = 600;
	tags.org_y = 400;
	tags.scale_x = 150;
	tags.scale_y = 80;
	tags.align = 7;

	auto quad_opt = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(quad_opt.has_value());

	PerspectiveTagVals out_tags = tags;
	bool ok = PerspectiveMath::TagsFromQuad(out_tags, *quad_opt, 100, 50, 2, 1.0);
	ASSERT_TRUE(ok);

	EXPECT_NEAR(out_tags.scale_x, tags.scale_x, 0.01);
	EXPECT_NEAR(out_tags.scale_y, tags.scale_y, 0.01);
}

TEST(PerspectiveMathTest, TagsFromQuad_RoundtripRotated) {
	PerspectiveTagVals tags;
	tags.pos_x = 960;
	tags.pos_y = 540;
	tags.org_x = 960;
	tags.org_y = 540;
	tags.angle = 30;
	tags.align = 7;

	auto quad_opt = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(quad_opt.has_value());

	PerspectiveTagVals out_tags = tags;
	bool ok = PerspectiveMath::TagsFromQuad(out_tags, *quad_opt, 100, 50, 2, 1.0);
	ASSERT_TRUE(ok);

	EXPECT_NEAR(out_tags.angle, tags.angle, 0.1);
	EXPECT_NEAR(out_tags.scale_x, 100, 1);  // 旋转不应改变缩放
	EXPECT_NEAR(out_tags.scale_y, 100, 1);
}

// ============================================================================
// angle_x / angle_y 往返测试
// ============================================================================

TEST(PerspectiveMathTest, TagsFromQuad_RoundtripAngleX) {
	PerspectiveTagVals tags;
	tags.pos_x = 960;
	tags.pos_y = 540;
	tags.org_x = 960;
	tags.org_y = 540;
	tags.angle_x = 20;
	tags.align = 7;

	auto quad_opt = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(quad_opt.has_value());

	PerspectiveTagVals out_tags = tags;
	bool ok = PerspectiveMath::TagsFromQuad(out_tags, *quad_opt, 100, 50, 2, 1.0);
	ASSERT_TRUE(ok);

	EXPECT_NEAR(out_tags.angle_x, tags.angle_x, 1.0);
}

TEST(PerspectiveMathTest, TagsFromQuad_RoundtripAngleY) {
	PerspectiveTagVals tags;
	tags.pos_x = 960;
	tags.pos_y = 540;
	tags.org_x = 960;
	tags.org_y = 540;
	tags.angle_y = 15;
	tags.align = 7;

	auto quad_opt = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(quad_opt.has_value());

	PerspectiveTagVals out_tags = tags;
	bool ok = PerspectiveMath::TagsFromQuad(out_tags, *quad_opt, 100, 50, 2, 1.0);
	ASSERT_TRUE(ok);

	EXPECT_NEAR(out_tags.angle_y, tags.angle_y, 1.0);
}

TEST(PerspectiveMathTest, TagsFromQuad_RoundtripCombinedRotation) {
	PerspectiveTagVals tags;
	tags.pos_x = 960;
	tags.pos_y = 540;
	tags.org_x = 960;
	tags.org_y = 540;
	tags.angle = 10;
	tags.angle_x = 8;
	tags.angle_y = 12;
	tags.align = 7;

	auto quad_opt = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(quad_opt.has_value());

	PerspectiveTagVals out_tags = tags;
	bool ok = PerspectiveMath::TagsFromQuad(out_tags, *quad_opt, 100, 50, 2, 1.0);
	ASSERT_TRUE(ok);

	// 组合旋转中各轴存在分解串扰，使用较宽松容差
	EXPECT_NEAR(out_tags.angle, tags.angle, 2.0);
	EXPECT_NEAR(out_tags.angle_x, tags.angle_x, 3.0);
	EXPECT_NEAR(out_tags.angle_y, tags.angle_y, 3.0);
}

// ============================================================================
// OrgMode 测试
// ============================================================================

TEST(PerspectiveMathTest, TagsFromQuad_OrgMode1KeepsOrg) {
	PerspectiveTagVals tags;
	tags.org_x = 300;
	tags.org_y = 200;
	tags.pos_x = 500;
	tags.pos_y = 400;
	tags.align = 7;

	auto quad = PerspectiveMath::TransformPoints(tags, 100, 50);
	ASSERT_TRUE(quad.has_value());

	PerspectiveTagVals out_tags = tags;
	bool ok = PerspectiveMath::TagsFromQuad(out_tags, *quad, 100, 50, 1, 1.0);
	ASSERT_TRUE(ok);

	// orgMode=1 应保持原 org
	EXPECT_NEAR(out_tags.org_x, 300, 0.01);
	EXPECT_NEAR(out_tags.org_y, 200, 0.01);
}

TEST(PerspectiveMathTest, TagsFromQuad_OrgMode2ForceCenter) {
	Quad quad = {
		Vector2D(200, 300),
		Vector2D(400, 300),
		Vector2D(400, 400),
		Vector2D(200, 400),
	};
	PerspectiveTagVals tags;
	tags.align = 7;

	bool ok = PerspectiveMath::TagsFromQuad(tags, quad, 100, 50, 2, 1.0);
	ASSERT_TRUE(ok);

	// orgMode=2 应将 org 设为中心 (300, 350)
	EXPECT_NEAR(tags.org_x, 300, 0.01);
	EXPECT_NEAR(tags.org_y, 350, 0.01);
}

// ============================================================================
// 退化/边界情形测试
// ============================================================================

TEST(PerspectiveMathTest, ZeroSizeRect) {
	// 零尺寸矩形不应导致 NaN
	PerspectiveTagVals tags;
	auto result = PerspectiveMath::TransformPoints(tags, 0.01, 0.01);
	ASSERT_TRUE(result.has_value());
	for (const auto &p : *result) {
		EXPECT_TRUE(std::isfinite(p.X()));
		EXPECT_TRUE(std::isfinite(p.Y()));
	}
}

TEST(PerspectiveMathTest, DegenerateQuad_Collinear) {
	// 共线四边形（所有点在一条直线上）
	Quad quad = {
		Vector2D(0, 0),
		Vector2D(100, 0),
		Vector2D(200, 0),
		Vector2D(300, 0),
	};
	PerspectiveTagVals tags;
	tags.align = 7;

	bool ok = PerspectiveMath::TagsFromQuad(tags, quad, 100, 50, 2, 1.0);
	// 可能成功也可能失败，取决于数值表现
	// 无论结果如何，不应有未定义行为
	if (ok) {
		EXPECT_TRUE(std::isfinite(tags.pos_x));
		EXPECT_TRUE(std::isfinite(tags.pos_y));
	}
}

TEST(PerspectiveMathTest, DegenerateQuadZeroArea) {
	// 重复点
	Quad quad = {
		Vector2D(100, 100),
		Vector2D(100, 100),
		Vector2D(100, 100),
		Vector2D(100, 100),
	};
	PerspectiveTagVals tags;
	tags.align = 7;

	// 不应崩溃
	bool ok = PerspectiveMath::TagsFromQuad(tags, quad, 100, 50, 2, 1.0);
	// 重复点四边形可能成功产生零缩放输出
	if (ok) {
		EXPECT_TRUE(std::isfinite(tags.pos_x));
		EXPECT_TRUE(std::isfinite(tags.pos_y));
	}
}