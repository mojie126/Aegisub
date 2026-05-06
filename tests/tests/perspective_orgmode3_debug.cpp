// org_mode=3 tests
#include <main.h>

#include "../src/perspective_motion/perspective_math.h"

#include <cmath>
#include <iostream>
#include <iomanip>

using namespace mocha;

class PerspectiveOrgMode3Test : public libagi {};

static std::ostream& operator<<(std::ostream& os, const Vector2D& v) {
    os << "(" << v.X() << ", " << v.Y() << ")";
    return os;
}

static Quad MakeRectAtUV(const Quad &rel_quad, Vector2D pos_uv,
                         double w, double h, int align) {
    static const double an_xshift[] = {0, 0.5, 1, 0, 0.5, 1, 0, 0.5, 1};
    static const double an_yshift[] = {1, 1, 1, 0.5, 0.5, 0.5, 0, 0, 0};
    int an_idx = align - 1;
    if (an_idx < 0 || an_idx > 8) an_idx = 6;
    Quad rect = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1, 1));
    for (auto &p : rect) {
        p = Vector2D(
            (p.X() - static_cast<float>(an_xshift[an_idx])) * static_cast<float>(w),
            (p.Y() - static_cast<float>(an_yshift[an_idx])) * static_cast<float>(h)
        );
        p = p + pos_uv;
    }
    Quad result_quad;
    for (const auto &uv : rect)
        result_quad.push_back(PerspectiveMath::UVToXY(rel_quad, uv));
    return result_quad;
}

static Quad BuildFrame0Quad() {
    Quad q;
    q.push_back(Vector2D(930.764f, 363.002f));
    q.push_back(Vector2D(1554.07f, 303.691f));
    q.push_back(Vector2D(1546.52f, 370.727f));
    q.push_back(Vector2D(923.214f, 430.039f));
    return q;
}

TEST(PerspectiveOrgMode3Test, SolveOrgMode3_Debug) {
    PerspectiveTagVals tags;
    tags.pos_x = 960; tags.pos_y = 540;
    tags.org_x = 960; tags.org_y = 540;
    tags.angle = 10; tags.align = 7;

    auto quad_opt = PerspectiveMath::TransformPoints(tags, 100, 50, 1.0);
    ASSERT_TRUE(quad_opt.has_value());
    auto& quad = *quad_opt;

    for (int mode : {2, 3}) {
        PerspectiveTagVals out_tags = tags;
        bool ok = PerspectiveMath::TagsFromQuad(out_tags, quad, 100, 50, mode, 1.0);
        ASSERT_TRUE(ok);
        if (mode == 3) {
            EXPECT_GT(out_tags.pos_x, -10000);
            EXPECT_LT(out_tags.pos_x, 10000);
            EXPECT_GT(out_tags.pos_y, -10000);
            EXPECT_LT(out_tags.pos_y, 10000);
        }
    }
}

TEST(PerspectiveOrgMode3Test, SolveOrgMode3_PerspectiveQuad) {
    PerspectiveTagVals tags;
    tags.pos_x = 960; tags.pos_y = 540;
    tags.org_x = 960; tags.org_y = 540;
    tags.angle = 5; tags.angle_x = 10; tags.angle_y = 8;
    tags.align = 7;

    auto quad_opt = PerspectiveMath::TransformPoints(tags, 100, 50, 1.0);
    ASSERT_TRUE(quad_opt.has_value());
    auto& quad = *quad_opt;

    PerspectiveTagVals out_tags = tags;
    bool ok = PerspectiveMath::TagsFromQuad(out_tags, quad, 100, 50, 3, 1.0);
    ASSERT_TRUE(ok);
    EXPECT_GT(out_tags.pos_x, -100000);
    EXPECT_LT(out_tags.pos_x, 100000);
    EXPECT_GT(out_tags.pos_y, -100000);
    EXPECT_LT(out_tags.pos_y, 100000);
}

TEST(PerspectiveOrgMode3Test, FallbackWhenZNearOne) {
    Quad rel_quad = BuildFrame0Quad();
    PerspectiveTagVals tags;
    tags.pos_x = 789.45; tags.pos_y = 612.67;
    tags.org_x = 789.45; tags.org_y = 612.67;
    tags.angle = 0.306; tags.align = 2;

    Vector2D pos_uv = PerspectiveMath::XYToUV(
        rel_quad, Vector2D(static_cast<float>(tags.pos_x), static_cast<float>(tags.pos_y))
    );
    Quad test_quad = MakeRectAtUV(rel_quad, pos_uv, 1.0, 1.0, tags.align);

    PerspectiveTagVals out;
    bool ok = PerspectiveMath::TagsFromQuad(out, test_quad, 96, 45, 3, 1.0);
    ASSERT_TRUE(ok);
    EXPECT_GT(out.pos_x, 0); EXPECT_LT(out.pos_x, 1920);
    EXPECT_GT(out.pos_y, 0); EXPECT_LT(out.pos_y, 1080);
}

TEST(PerspectiveOrgMode3Test, FullPipelineWithRealQuad) {
    Quad rel_quad = BuildFrame0Quad();
    PerspectiveTagVals tags;
    tags.pos_x = 789.45; tags.pos_y = 612.67;
    tags.org_x = 789.45; tags.org_y = 612.67;
    tags.angle = 0.306; tags.align = 2;

    Vector2D pos_uv = PerspectiveMath::XYToUV(
        rel_quad, Vector2D(static_cast<float>(tags.pos_x), static_cast<float>(tags.pos_y))
    );

    auto rect_at_pos = [&](double w, double h) {
        return MakeRectAtUV(rel_quad, pos_uv, w, h, tags.align);
    };

    PerspectiveTagVals pt = tags;
    ASSERT_TRUE(PerspectiveMath::TagsFromQuad(pt, rect_at_pos(1, 1), 96, 45, 3, 1.0));

    double adj_w = 100.0 / pt.scale_x;
    double adj_h = 100.0 / pt.scale_y;
    ASSERT_TRUE(PerspectiveMath::TagsFromQuad(pt, rect_at_pos(adj_w, adj_h), 96, 45, 3, 1.0));

    EXPECT_GT(pt.pos_x, 0); EXPECT_LT(pt.pos_x, 1920);
    EXPECT_GT(pt.pos_y, 0); EXPECT_LT(pt.pos_y, 1080);
    EXPECT_GT(pt.org_x, 0); EXPECT_LT(pt.org_x, 1920);
    EXPECT_GT(pt.org_y, 0); EXPECT_LT(pt.org_y, 1080);
}