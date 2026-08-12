// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪处理器集成测试

#include <main.h>

#include "../src/perspective_motion/perspective_processor.h"
#include "../src/mocha_motion/motion_tags.h"

#include <cmath>
#include <algorithm>
#include <regex>

using namespace mocha;

class PerspectiveProcessorTest : public libagi {};

// ============================================================================
// PrepareForPerspective: 标签提取测试
// ============================================================================

TEST(PerspectiveProcessorTest, PrepareForPerspectiveReadsScalarTags) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\fscx150\\fscy200\\frz30\\frx10\\fry20}"
				"{\\fabcd}test text";
	line.style = "Default";
	line.x_position = 0;
	line.y_position = 0;

	double width, height;
	auto tags = processor.PrepareForPerspective(line, width, height);

	EXPECT_NEAR(tags.scale_x, 150, 0.01);
	EXPECT_NEAR(tags.scale_y, 200, 0.01);
	EXPECT_NEAR(tags.angle, 30, 0.01);
	EXPECT_NEAR(tags.angle_x, 10, 0.01);
	EXPECT_NEAR(tags.angle_y, 20, 0.01);
}

TEST(PerspectiveProcessorTest, PrepareForPerspectiveReadsShearAndBorder) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\fax0.5\\fay-0.3\\bord2\\xbord3\\shad4\\xshad5}"
				"test";
	line.style = "Default";
	line.x_position = 0;
	line.y_position = 0;

	double width, height;
	auto tags = processor.PrepareForPerspective(line, width, height);

	EXPECT_NEAR(tags.shear_x, 0.5, 0.01);
	EXPECT_NEAR(tags.shear_y, -0.3, 0.01);
	EXPECT_NEAR(tags.outline_x, 3, 0.01); // xbord 优先于 bord
	EXPECT_NEAR(tags.outline_y, 2, 0.01); // 无 ybord, 回退到 bord=2
	EXPECT_NEAR(tags.shadow_x, 5, 0.01); // xshad 优先于 shad
	EXPECT_NEAR(tags.shadow_y, 4, 0.01); // 无 yshad, 回退到 shad=4
}

TEST(PerspectiveProcessorTest, PrepareForPerspectiveReadsPosAndOrg) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\pos(100,200)\\org(50,25)}text";
	line.style = "Default";
	line.x_position = 0;
	line.y_position = 0;

	double width, height;
	auto tags = processor.PrepareForPerspective(line, width, height);

	EXPECT_NEAR(tags.pos_x, 100, 0.01);
	EXPECT_NEAR(tags.pos_y, 200, 0.01);
	EXPECT_NEAR(tags.org_x, 50, 0.01);
	EXPECT_NEAR(tags.org_y, 25, 0.01);
}

TEST(PerspectiveProcessorTest, PrepareForPerspectiveFallsBackToLinePosition) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{}nos pos tag here";
	line.style = "Default";
	line.x_position = 400;
	line.y_position = 300;

	double width, height;
	auto tags = processor.PrepareForPerspective(line, width, height);

	EXPECT_NEAR(tags.pos_x, 400, 0.01);
	EXPECT_NEAR(tags.pos_y, 300, 0.01);
}

// ============================================================================
// \fr 别名测试：TagRegistry 的 zrot 模式支持 \frz? 可选 z
// ============================================================================

TEST(PerspectiveProcessorTest, PrepareForPerspectiveHandlesFrAlias) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	// \fr 是 \frz 的 ASS 别名
	line.text = "{\\fr45}text";
	line.style = "Default";
	line.x_position = 0;
	line.y_position = 0;

	double width, height;
	auto tags = processor.PrepareForPerspective(line, width, height);

	// \fr 应被正确识别为 z-rotation
	EXPECT_NEAR(tags.angle, 45, 0.01);
}

TEST(PerspectiveProcessorTest, PrepareForPerspectiveHandlesFrzWithZ) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\frz45}text";
	line.style = "Default";
	line.x_position = 0;
	line.y_position = 0;

	double width, height;
	auto tags = processor.PrepareForPerspective(line, width, height);

	EXPECT_NEAR(tags.angle, 45, 0.01);
}

// ============================================================================
// ApplyTagsToLine: 输出格式测试
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyTagsToLineWritesScalarWithoutParens) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{}test text";

	PerspectiveTagVals tags;
	tags.pos_x = 100; tags.pos_y = 200;
	tags.org_x = 50; tags.org_y = 50;
	tags.angle = 30.5;
	tags.scale_x = 150;
	tags.scale_y = 75;

	processor.ApplyTagsToLine(line, {tags});

	// pos/org 应使用括号格式（多值标签）
	EXPECT_NE(line.text.find("\\pos(100,200)"), std::string::npos);
	EXPECT_NE(line.text.find("\\org(50,50)"), std::string::npos);

	// 标量标签应使用无括号格式
	EXPECT_NE(line.text.find("\\frz30.5"), std::string::npos);
	EXPECT_NE(line.text.find("\\fscx150"), std::string::npos);
	EXPECT_NE(line.text.find("\\fscy75"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyTagsToLineRemovesOldTags) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\pos(500,300)\\frz90\\fscx200}old text";

	PerspectiveTagVals tags;
	tags.pos_x = 100; tags.pos_y = 200;
	tags.org_x = 50; tags.org_y = 50;
	tags.angle = 30;
	tags.scale_x = 150;
	tags.scale_y = 75;

	processor.ApplyTagsToLine(line, {tags});

	// 旧标签应被移除
	EXPECT_EQ(line.text.find("\\pos(500,300)"), std::string::npos);
	EXPECT_EQ(line.text.find("\\frz90"), std::string::npos);
	EXPECT_EQ(line.text.find("\\fscx200"), std::string::npos);

	// 新标签应存在
	EXPECT_NE(line.text.find("\\pos(100,200)"), std::string::npos);
	EXPECT_NE(line.text.find("\\frz30"), std::string::npos);
	EXPECT_NE(line.text.find("\\fscx150"), std::string::npos);
}

// ============================================================================
// Width/Height 计算测试
// ============================================================================

TEST(PerspectiveProcessorTest, PrepareForPerspectiveEstimatesWidthHeight) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	// 4 个拉丁字符，\fscx100
	line.text = "{\\fs48\\fscx100\\fscy100}test";
	line.style = "Default";
	line.x_position = 0;
	line.y_position = 0;

	double width, height;
	processor.PrepareForPerspective(line, width, height);

	// 4 个拉丁字符 x 48px x 0.5 = 96, / 1.0 = 96
	EXPECT_GT(width, 50);
	EXPECT_LT(width, 500);
	EXPECT_GT(height, 20);
	EXPECT_LT(height, 200);
}

// ============================================================================
// ApplyTagsToLine: 多 override 块场景
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyTagsToLineHandlesMultipleOverrideBlocks) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\c&HFF0000&\\frz90}hello {\\fscx200}world";

	PerspectiveTagVals tags;
	tags.pos_x = 100; tags.pos_y = 200;
	tags.org_x = 50; tags.org_y = 50;
	tags.angle = 30;
	tags.scale_x = 150;
	tags.scale_y = 100;

	processor.ApplyTagsToLine(line, {tags});

	// 新标签应被添加到第一个 override 块
	EXPECT_NE(line.text.find("\\pos(100,200)"), std::string::npos);
	EXPECT_NE(line.text.find("\\frz30"), std::string::npos);

	// 非透视标签应保留（如 \c）
	EXPECT_NE(line.text.find("\\c&HFF0000&"), std::string::npos);

	// "hello world" 文本应保留
	EXPECT_NE(line.text.find("hello"), std::string::npos);
	EXPECT_NE(line.text.find("world"), std::string::npos);
}

TEST(PerspectiveProcessorTest, PrepareLinesPreservesPerBlockGlobalTags) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\pos(10,20)\\clip(0,0,10,10)}a{\\pos(30,40)\\clip(20,20,30,30)}b";
	line.style = "Default";
	std::vector<MotionLine> lines = {line};

	processor.PrepareLines(lines);

	ASSERT_EQ(lines.size(), 1u);
	EXPECT_NE(lines[0].text.find("\\pos(10,20)"), std::string::npos);
	EXPECT_NE(lines[0].text.find("\\pos(30,40)"), std::string::npos);
	EXPECT_NE(lines[0].text.find("\\clip(0,0,10,10)"), std::string::npos);
	EXPECT_NE(lines[0].text.find("\\clip(20,20,30,30)"), std::string::npos);
}

// ============================================================================
// PrepareForPerspective: 默认值测试
// ============================================================================

TEST(PerspectiveProcessorTest, PrepareForPerspectiveDefaultsWhenNoTags) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "plain text with no override tags";
	line.style = "Default";
	line.x_position = 0;
	line.y_position = 0;

	double width, height;
	auto tags = processor.PrepareForPerspective(line, width, height);

	// 默认值
	EXPECT_EQ(tags.align, 7);
	EXPECT_NEAR(tags.scale_x, 100, 0.01);
	EXPECT_NEAR(tags.scale_y, 100, 0.01);
	EXPECT_NEAR(tags.angle, 0, 0.01);
	EXPECT_NEAR(tags.shear_x, 0, 0.01);
	EXPECT_NEAR(tags.shear_y, 0, 0.01);
	EXPECT_NEAR(tags.outline_x, 0, 0.01);
	EXPECT_NEAR(tags.outline_y, 0, 0.01);
	EXPECT_NEAR(tags.shadow_x, 0, 0.01);
	EXPECT_NEAR(tags.shadow_y, 0, 0.01);
}

// ============================================================================
// ApplyTagsToLine: 空文本场景
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyTagsToLineHandlesEmptyText) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "";

	PerspectiveTagVals tags;
	tags.pos_x = 100; tags.pos_y = 200;
	tags.org_x = 50; tags.org_y = 50;

	processor.ApplyTagsToLine(line, {tags});

	// 应创建新的 override 块
	EXPECT_NE(line.text.find("\\pos(100,200)"), std::string::npos);
}

// ============================================================================
// ApplyTagsToLine: 多块各自独立标签测试
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyTagsToLineHandlesPerBlockTags) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\frz90}hello {\\fscx200}world";

	// 每个块独立的透视标签
	PerspectiveTagVals tags_a;
	tags_a.pos_x = 100; tags_a.pos_y = 200;
	tags_a.org_x = 50; tags_a.org_y = 50;
	tags_a.angle = 30;

	PerspectiveTagVals tags_b;
	tags_b.pos_x = 300; tags_b.pos_y = 400;
	tags_b.org_x = 150; tags_b.org_y = 150;
	tags_b.scale_x = 75;

	processor.ApplyTagsToLine(line, {tags_a, tags_b});

	// 每个块的旧透视标签应被移除
	EXPECT_EQ(line.text.find("\\frz90"), std::string::npos);
	EXPECT_EQ(line.text.find("\\fscx200"), std::string::npos);

	// 第一个块应有 tags_a 的标签（\pos/\org 为事件级标签，仅写回首块）
	EXPECT_NE(line.text.find("\\pos(100,200)"), std::string::npos);
	EXPECT_NE(line.text.find("\\org(50,50)"), std::string::npos);
	EXPECT_NE(line.text.find("\\frz30"), std::string::npos);

	// 第二个块只写回块级变换标签，\pos/\org 不重复写回
	EXPECT_NE(line.text.find("\\fscx75"), std::string::npos);
	EXPECT_EQ(line.text.find("\\pos(300,400)"), std::string::npos);
	EXPECT_EQ(line.text.find("\\org(150,150)"), std::string::npos);
	// \pos/\org 整行只出现一次
	EXPECT_EQ(tag_utils::count_tag(line.text, R"(\\pos\()"), 1);
	EXPECT_EQ(tag_utils::count_tag(line.text, R"(\\org\()"), 1);

	// "hello world" 文本应保留
	EXPECT_NE(line.text.find("hello"), std::string::npos);
	EXPECT_NE(line.text.find("world"), std::string::npos);
}

// ============================================================================
// ApplyTagsToLine: 某块无对应透视标签的场景
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyTagsToLineHandlesBlockWithoutPerspTags) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	// 三个块，只提供两组透视标签
	line.text = "{\\bord2}text1 {\\frz90}text2 {\\c&HFF0000&}text3";

	PerspectiveTagVals tags_a;
	tags_a.pos_x = 100; tags_a.pos_y = 200;
	tags_a.org_x = 50; tags_a.org_y = 50;
	tags_a.angle = 30;

	PerspectiveTagVals tags_b;
	tags_b.pos_x = 300; tags_b.pos_y = 400;
	tags_b.org_x = 150; tags_b.org_y = 150;
	tags_b.scale_x = 75;

	processor.ApplyTagsToLine(line, {tags_a, tags_b});

	// 第一个块：新标签 + 非透视标签应保留
	EXPECT_NE(line.text.find("\\pos(100,200)"), std::string::npos);
	EXPECT_NE(line.text.find("\\bord2"), std::string::npos);

	// 第二个块：只写回块级变换标签，\pos 不重复写回
	EXPECT_NE(line.text.find("\\fscx75"), std::string::npos);
	EXPECT_EQ(line.text.find("\\pos(300,400)"), std::string::npos);

	// 第三个块（无对应标签）：非透视标签应保留
	EXPECT_NE(line.text.find("\\c&HFF0000&"), std::string::npos);

	// 旧透视标签被移除
	EXPECT_EQ(line.text.find("\\frz90"), std::string::npos);

	// 所有文本保留
	EXPECT_NE(line.text.find("text1"), std::string::npos);
	EXPECT_NE(line.text.find("text2"), std::string::npos);
	EXPECT_NE(line.text.find("text3"), std::string::npos);
}

// ============================================================================
// ApplyTagsToLine: \t(...) 保护测试
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyTagsToLinePreservesTransformEffect) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\frz90\\t(0,500,\\fscx120\\fscy200)}text";

	PerspectiveTagVals tags;
	tags.pos_x = 100; tags.pos_y = 200;
	tags.org_x = 50; tags.org_y = 50;
	tags.angle = 30;
	tags.scale_x = 150;
	tags.scale_y = 100;

	processor.ApplyTagsToLine(line, {tags});

	// 新透视标签应存在
	EXPECT_NE(line.text.find("\\pos(100,200)"), std::string::npos);
	EXPECT_NE(line.text.find("\\frz30"), std::string::npos);

	// \t(...) 效果内容应完整保留（不被旧标签移除破坏）
	EXPECT_NE(line.text.find("\\t("), std::string::npos);
	EXPECT_NE(line.text.find("\\fscx120"), std::string::npos) << "\\t internal \\fscx was corrupted";
	EXPECT_NE(line.text.find("\\fscy200"), std::string::npos) << "\\t internal \\fscy was corrupted";

	// 文本应保留
	EXPECT_NE(line.text.find("text"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyTagsToLinePreservesTransformWithMultipleBlocks) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\frz90\\t(0,500,\\fscx120)}hello {\\fscx200\\t(0,300,\\fscy150)}world";

	PerspectiveTagVals tags_a;
	tags_a.pos_x = 100; tags_a.pos_y = 200;
	tags_a.org_x = 50; tags_a.org_y = 50;
	tags_a.angle = 30;
	tags_a.scale_x = 150;

	PerspectiveTagVals tags_b;
	tags_b.pos_x = 300; tags_b.pos_y = 400;
	tags_b.org_x = 150; tags_b.org_y = 150;
	tags_b.scale_x = 75;

	processor.ApplyTagsToLine(line, {tags_a, tags_b});

	// 每个块的 \t(...) 效果内容应完整保留
	EXPECT_NE(line.text.find("\\t("), std::string::npos);

	// 第一个块的 \t 内容应保留
	auto pos1 = line.text.find("\\t(");
	ASSERT_NE(pos1, std::string::npos);
	// 旧透视标签被移除
	EXPECT_EQ(line.text.find("\\frz90"), std::string::npos);

	// 文本应保留
	EXPECT_NE(line.text.find("hello"), std::string::npos);
	EXPECT_NE(line.text.find("world"), std::string::npos);
}

// ============================================================================
// ApplyTagsToLine: \t(...) 与 \fad 共存测试
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyTagsToLineWithTransformAndFad) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\fad(100,200)\\frz45\\t(0,500,\\fscx150)}text";

	PerspectiveTagVals tags;
	tags.pos_x = 200; tags.pos_y = 300;
	tags.org_x = 100; tags.org_y = 100;
	tags.angle = 20;
	tags.scale_x = 130;
	tags.scale_y = 130;

	processor.ApplyTagsToLine(line, {tags});

	// \t 效果内容应保留
	EXPECT_NE(line.text.find("\\t("), std::string::npos);
	EXPECT_NE(line.text.find("\\fscx150"), std::string::npos) << "\\t internal tag was corrupted";

	// \fad 应保留（不在 remove_tag_names 中，由 AdjustFadeInBlock 处理）
	// AdjustFadeInBlock 是在 Apply 帧循环中调用的，ApplyTagsToLine 仅处理透视标签
	// 所以在这里（单测直接调用 ApplyTagsToLine）时 \fad 应原样保留
	EXPECT_NE(line.text.find("\\fad("), std::string::npos) << "\\fad was incorrectly removed";

	// 新透视标签应存在
	EXPECT_NE(line.text.find("\\pos(200,300)"), std::string::npos);
	EXPECT_NE(line.text.find("\\frz20"), std::string::npos);
}

// ============================================================================
// ApplyTagsToLine: \move 移除测试
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyTagsToLineRemovesMove) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\move(0,0,100,100,0,1000)}text";

	PerspectiveTagVals tags;
	tags.pos_x = 50; tags.pos_y = 50;
	tags.org_x = 25; tags.org_y = 25;

	processor.ApplyTagsToLine(line, {tags});

	// \move 应被移除
	EXPECT_EQ(line.text.find("\\move("), std::string::npos) << "\\move was not removed";

	// 新 \pos 应存在
	EXPECT_NE(line.text.find("\\pos(50,50)"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyTagsToLineWithTransformInsideMoveNotCorrupted) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\move(0,0,100,100,0,1000)\\t(0,500,\\fscx120)}text";

	PerspectiveTagVals tags;
	tags.pos_x = 50; tags.pos_y = 50;
	tags.org_x = 25; tags.org_y = 25;
	tags.scale_x = 150;

	processor.ApplyTagsToLine(line, {tags});

	// \move 应被移除
	EXPECT_EQ(line.text.find("\\move("), std::string::npos);

	// \t 效果内容应保留
	EXPECT_NE(line.text.find("\\t("), std::string::npos);
	EXPECT_NE(line.text.find("\\fscx120"), std::string::npos) << "\\t internal tag was corrupted by \\move removal";
}

// ============================================================================
// PerspectiveDataHandler 数据解析测试
// ============================================================================

/// Power Pin 有效测试数据：2 帧，4 个角点
/// Frame 0: TL(0,0) TR(200,0) BR(200,200) BL(0,200)
/// Frame 1: TL(100,100) TR(300,100) BR(300,300) BL(100,300)
static const std::string POWERPIN_VALID_DATA =
	"Adobe After Effects 6.0 Keyframe Data\r\n"
	"\r\n"
	"\tUnits Per Second\t24\r\n"
	"\r\n"
	"Effects\tCC Power Pin #1\tCC Power Pin-0002\r\n"
	"\tFrame\tX\tY\r\n"
	"0\t0\t0\r\n"
	"1\t100\t100\r\n"
	"\r\n"
	"Effects\tCC Power Pin #1\tCC Power Pin-0003\r\n"
	"\tFrame\tX\tY\r\n"
	"0\t200\t0\r\n"
	"1\t300\t100\r\n"
	"\r\n"
	"Effects\tCC Power Pin #1\tCC Power Pin-0005\r\n"
	"\tFrame\tX\tY\r\n"
	"0\t200\t200\r\n"
	"1\t300\t300\r\n"
	"\r\n"
	"Effects\tCC Power Pin #1\tCC Power Pin-0004\r\n"
	"\tFrame\tX\tY\r\n"
	"0\t0\t200\r\n"
	"1\t100\t300\r\n"
	"\r\n"
	"End of Keyframe Data\r\n";

TEST(PerspectiveProcessorTest, DataHandlerParseValidPowerPin) {
	PerspectiveDataHandler dh;
	bool ok = dh.ParsePowerPin(POWERPIN_VALID_DATA);
	EXPECT_TRUE(ok);
	EXPECT_TRUE(dh.IsValid());
	EXPECT_EQ(dh.Length(), 2);
}

TEST(PerspectiveProcessorTest, DataHandlerParseInvalidData) {
	PerspectiveDataHandler dh;
	bool ok = dh.ParsePowerPin("not power pin data");
	EXPECT_FALSE(ok);
	EXPECT_FALSE(dh.IsValid());
}

TEST(PerspectiveProcessorTest, DataHandlerGetQuad) {
	PerspectiveDataHandler dh;
	ASSERT_TRUE(dh.ParsePowerPin(POWERPIN_VALID_DATA));

	// 第 1 帧：200x200 正方形
	auto quad1 = dh.GetQuad(1);
	ASSERT_NE(quad1, nullptr);
	EXPECT_EQ(quad1->size(), 4);
	EXPECT_NEAR((*quad1)[0].X(), 0, 0.01);  // TL
	EXPECT_NEAR((*quad1)[0].Y(), 0, 0.01);
	EXPECT_NEAR((*quad1)[1].X(), 200, 0.01); // TR
	EXPECT_NEAR((*quad1)[1].Y(), 0, 0.01);
	EXPECT_NEAR((*quad1)[2].X(), 200, 0.01); // BR
	EXPECT_NEAR((*quad1)[2].Y(), 200, 0.01);
	EXPECT_NEAR((*quad1)[3].X(), 0, 0.01);  // BL
	EXPECT_NEAR((*quad1)[3].Y(), 200, 0.01);

	// 第 2 帧：平移 100,100
	auto quad2 = dh.GetQuad(2);
	ASSERT_NE(quad2, nullptr);
	EXPECT_NEAR((*quad2)[0].X(), 100, 0.01);
	EXPECT_NEAR((*quad2)[0].Y(), 100, 0.01);
	EXPECT_NEAR((*quad2)[1].X(), 300, 0.01);
	EXPECT_NEAR((*quad2)[1].Y(), 100, 0.01);
	EXPECT_NEAR((*quad2)[2].X(), 300, 0.01);
	EXPECT_NEAR((*quad2)[2].Y(), 300, 0.01);
	EXPECT_NEAR((*quad2)[3].X(), 100, 0.01);
	EXPECT_NEAR((*quad2)[3].Y(), 300, 0.01);
}

TEST(PerspectiveProcessorTest, DataHandlerGetQuadOutOfRange) {
	PerspectiveDataHandler dh;
	ASSERT_TRUE(dh.ParsePowerPin(POWERPIN_VALID_DATA));

	EXPECT_EQ(dh.GetQuad(0), nullptr);   // 0-indexed 无效
	EXPECT_EQ(dh.GetQuad(3), nullptr);   // 超出范围
	EXPECT_EQ(dh.GetQuad(-1), nullptr);  // 负数
}

TEST(PerspectiveProcessorTest, DataHandlerCheckLength) {
	PerspectiveDataHandler dh;
	ASSERT_TRUE(dh.ParsePowerPin(POWERPIN_VALID_DATA));

	EXPECT_TRUE(dh.CheckLength(2));   // 匹配
	EXPECT_FALSE(dh.CheckLength(1));  // 不匹配
	EXPECT_FALSE(dh.CheckLength(3));  // 不匹配
}

TEST(PerspectiveProcessorTest, DataHandlerBestEffortParse) {
	PerspectiveDataHandler dh;
	bool ok = dh.BestEffortParse(POWERPIN_VALID_DATA);
	EXPECT_TRUE(ok);
	EXPECT_EQ(dh.Length(), 2);
}

TEST(PerspectiveProcessorTest, DataHandlerBestEffortParseInvalid) {
	PerspectiveDataHandler dh;
	bool ok = dh.BestEffortParse("completely invalid data");
	EXPECT_FALSE(ok);
}

TEST(PerspectiveProcessorTest, DataHandlerRejectsDuplicatePinHeader) {
	// 同一 marker 出现两次（损坏或拼接数据）必须拒绝
	std::string dup = POWERPIN_VALID_DATA;
	dup += "Effects\tCC Power Pin #1\tCC Power Pin-0002\r\n"
		"\tFrame\tX\tY\r\n"
		"2\t999\t999\r\n";
	PerspectiveDataHandler dh;
	EXPECT_FALSE(dh.ParsePowerPin(dup));
}

TEST(PerspectiveProcessorTest, DataHandlerRejectsMismatchedFrames) {
	// 四个角点 Frame 列不一致（如 0003 缺少一帧）必须拒绝
	std::string bad = POWERPIN_VALID_DATA;
	// 移除 0003 的 Frame 1 行
	const std::string pin3_block =
		"Effects\tCC Power Pin #1\tCC Power Pin-0003\r\n"
		"\tFrame\tX\tY\r\n"
		"0\t200\t0\r\n"
		"1\t300\t100\r\n";
	const std::string pin3_bad =
		"Effects\tCC Power Pin #1\tCC Power Pin-0003\r\n"
		"\tFrame\tX\tY\r\n"
		"0\t200\t0\r\n";
	auto pos = bad.find(pin3_block);
	ASSERT_NE(pos, std::string::npos);
	bad.replace(pos, pin3_block.size(), pin3_bad);
	PerspectiveDataHandler dh;
	EXPECT_FALSE(dh.ParsePowerPin(bad));
}

TEST(PerspectiveProcessorTest, DataHandlerRejectsNonFiniteCoordinate) {
	std::string bad = POWERPIN_VALID_DATA;
	const std::string valid_row = "1\t100\t100";
	auto pos = bad.find(valid_row);
	ASSERT_NE(pos, std::string::npos);
	bad.replace(pos, valid_row.size(), "1\tnan\t100");

	PerspectiveDataHandler dh;
	EXPECT_FALSE(dh.ParsePowerPin(bad));
}

// ============================================================================
// Apply 集成测试
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyWithIdentityQuads) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	// MotionLine spanning frames 0-2
	MotionLine line;
	line.text = "{\\pos(960,540)}hello world";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 1000;
	line.tokenize_transforms();

	// 3 frames of quads (all same = identity perspective)
	std::vector<Quad> quads;
	for (int i = 0; i < 3; i++) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	EXPECT_NE(result[0].text.find("\\pos("), std::string::npos);
	EXPECT_NE(result[0].text.find("hello world"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyAlignInLaterBlockAffectsFirstBlock) {
	// \an 是事件级标签，libass 渲染时整行首次出现的 \an 生效，
	// 首块无 \an、后续块有 \an8 时，首块的透视锚点也应使用 8 而非样式默认，
	// \pos 只写回首块，用户书写的 \an8 保留在后续块原位
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\i1}first{\\an8}second";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 1000;
	line.tokenize_transforms();

	// 3 frames of quads (all same = identity perspective)
	std::vector<Quad> quads;
	for (int i = 0; i < 3; i++) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	// \pos/\org 为事件级标签，整行只写回一次（首块）
	EXPECT_EQ(tag_utils::count_tag(result[0].text, R"(\\pos\()"), 1)
		<< "Global \\pos should be written once, got: " << result[0].text;
	EXPECT_EQ(tag_utils::count_tag(result[0].text, R"(\\org\()"), 1)
		<< "Global \\org should be written once, got: " << result[0].text;
	// 用户书写的 \an8 保留原位（后续块），不写回也不移动
	EXPECT_NE(result[0].text.find("\\an8"), std::string::npos)
		<< "User-written \\an should be kept in place, got: " << result[0].text;
	// 文本保留
	EXPECT_NE(result[0].text.find("first"), std::string::npos);
	EXPECT_NE(result[0].text.find("second"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyWritesGlobalTagsOnlyInFirstBlock) {
	// 多 override 块行：\pos/\org 为事件级标签，仅写回首块，
	// 后续块只写回块级变换标签，与 libass 首次生效语义一致
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\pos(960,540)}first {\\fscx200}second";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads(3,
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080)));
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	// \pos/\org 整行只写回一次（首块），后续块不重复写回
	EXPECT_EQ(tag_utils::count_tag(result[0].text, R"(\\pos\()"), 1)
		<< "Global \\pos should be written once, got: " << result[0].text;
	EXPECT_EQ(tag_utils::count_tag(result[0].text, R"(\\org\()"), 1)
		<< "Global \\org should be written once, got: " << result[0].text;
	// 两个块均保留块级变换标签
	EXPECT_EQ(tag_utils::count_tag(result[0].text, R"(\\fscx)"), 2)
		<< "Per-block \\fscx should be kept for both blocks, got: " << result[0].text;
	// 文本保留
	EXPECT_NE(result[0].text.find("first"), std::string::npos);
	EXPECT_NE(result[0].text.find("second"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyKeepsUserAlignTagInPlace) {
	// 用户显式 \an 保留原位不动，追踪不写回、不移动、不删除
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\an8\\pos(960,540)}text";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads(3,
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080)));
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	EXPECT_NE(result[0].text.find("\\an8"), std::string::npos)
		<< "User-written \\an should be kept in place, got: " << result[0].text;
	EXPECT_EQ(tag_utils::count_tag(result[0].text, R"(\\pos\()"), 1);
	EXPECT_NE(result[0].text.find("text"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyReferenceFrameDoesNotDropEarlierFrames) {
	PerspectiveOptions opts;
	opts.relframe = 3;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\pos(960,540)}text";
	line.style = "Default";
	line.start_time = 0;
	line.end_time = 3000;
	line.duration = 3000;
	line.tokenize_transforms();

	std::vector<Quad> quads(3,
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080)));
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	EXPECT_EQ(result.size(), 3u);
}

TEST(PerspectiveProcessorTest, ApplyUsesReferenceLineForNonOverlappingLines) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine reference_line;
	reference_line.number = 1;
	reference_line.text = "{\\pos(100,100)}same";
	reference_line.style = "Default";
	reference_line.start_time = 0;
	reference_line.end_time = 1000;
	reference_line.duration = 1000;

	MotionLine later_line;
	later_line.number = 2;
	later_line.text = "{\\pos(300,300)}same";
	later_line.style = "Default";
	later_line.start_time = 1000;
	later_line.end_time = 2000;
	later_line.duration = 1000;

	std::vector<MotionLine> lines = {reference_line, later_line};
	processor.PrepareLines(lines);
	std::vector<Quad> quads(2,
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080)));
	auto result = processor.Apply(lines, quads, 1920, 1080);
	PerspectiveProcessor::PostprocessLines(result);

	ASSERT_EQ(result.size(), 2u);
	auto reference_it = std::find_if(result.begin(), result.end(), [](const MotionLine &item) {
		return item.number == 1;
	});
	auto later_it = std::find_if(result.begin(), result.end(), [](const MotionLine &item) {
		return item.number == 2;
	});
	ASSERT_NE(reference_it, result.end());
	ASSERT_NE(later_it, result.end());

	const auto reference_pos = reference_it->text.find("\\pos(");
	ASSERT_NE(reference_pos, std::string::npos);
	const auto reference_pos_end = reference_it->text.find(')', reference_pos);
	ASSERT_NE(reference_pos_end, std::string::npos);
	const auto reference_pos_tag = reference_it->text.substr(
		reference_pos, reference_pos_end - reference_pos + 1
	);
	EXPECT_NE(later_it->text.find(reference_pos_tag), std::string::npos)
		<< "Non-overlapping lines should use the line at the reference frame, got: "
		<< later_it->text;
}

TEST(PerspectiveProcessorTest, ApplyWithTranslation) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	// 行位于第 0 帧画面中心
	MotionLine line;
	line.text = "{\\pos(960,540)}hello";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 1000;
	line.tokenize_transforms();

	// 四边形逐帧右移
	std::vector<Quad> quads;
	{
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}
	{
		Quad q;
		q.push_back(Vector2D(100, 0));
		q.push_back(Vector2D(2020, 0));
		q.push_back(Vector2D(2020, 1080));
		q.push_back(Vector2D(100, 1080));
		quads.push_back(std::move(q));
	}
	{
		Quad q;
		q.push_back(Vector2D(200, 0));
		q.push_back(Vector2D(2120, 0));
		q.push_back(Vector2D(2120, 1080));
		q.push_back(Vector2D(200, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	// 透视映射后的 pos 应与原始 pos 不同
	EXPECT_NE(result[0].text.find("\\pos("), std::string::npos);
	EXPECT_NE(result[0].text.find("hello"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyEmptyQuads) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\pos(960,540)}hello";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;

	std::vector<MotionLine> lines = {line};
	std::vector<Quad> quads;
	auto result = processor.Apply(lines, quads, 1920, 1080);

	EXPECT_TRUE(result.empty());
}

TEST(PerspectiveProcessorTest, ApplyEmptyLines) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	std::vector<MotionLine> lines;
	std::vector<Quad> quads;
	{
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	auto result = processor.Apply(lines, quads, 1920, 1080);
	EXPECT_TRUE(result.empty());
}

TEST(PerspectiveProcessorTest, ApplyIncludeExtraWritesAmbientPlane) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;
	// 开启 includeextra：每个输出行写入帧四边形到 ambient_plane
	opts.include_extra = true;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\pos(960,540)}hello";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	{
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}
	{
		Quad q;
		q.push_back(Vector2D(50, 0));
		q.push_back(Vector2D(1970, 0));
		q.push_back(Vector2D(1970, 1080));
		q.push_back(Vector2D(50, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_EQ(result.size(), 2u);
	// 每帧都应写入 4 点四边形，格式 "x;y|x;y|x;y|x;y"
	for (const auto &pl : result) {
		EXPECT_FALSE(pl.ambient_plane.empty());
		EXPECT_EQ(std::count(pl.ambient_plane.begin(), pl.ambient_plane.end(), '|'), 3);
		EXPECT_EQ(std::count(pl.ambient_plane.begin(), pl.ambient_plane.end(), ';'), 4);
	}
	// 关闭 include_extra 时不应写入
	opts.include_extra = false;
	PerspectiveProcessor processor_off(opts, 1920, 1080);
	processor_off.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);
	auto result_off = processor_off.Apply(lines, quads, 1920, 1080);
	ASSERT_EQ(result_off.size(), 2u);
	for (const auto &pl : result_off)
		EXPECT_TRUE(pl.ambient_plane.empty());
}

TEST(PerspectiveProcessorTest, ApplyPerspectiveRespectsExistingTags) {
	// 对应上游 205f3e2：已有标签（如 \frz 旋转的竖排文本）的参考行，
	// Apply Perspective 应尊重其标签，而不是将其清空
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;   // 关键：开启 apply perspective
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	// 参考行带 \frz90（旋转标签），apply_perspective 应保留该旋转语义
	MotionLine line;
	line.text = "{\\frz90\\pos(960,540)}vertical";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	{
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	// 输出仍应包含透视/位置标签，且不丢失文本
	EXPECT_NE(result[0].text.find("\\pos("), std::string::npos);
	EXPECT_NE(result[0].text.find("vertical"), std::string::npos);
	// 参考帧旋转语义不应被抹成完全无透视（保留至少一个透视标签）
	EXPECT_NE(result[0].text.find("\\frz"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyPerspectiveKeepsScaleAndRotationOnReference) {
	// 对应上游 205f3e2 respect-tags 的盲区验证：
	// 已有 \fscx80（非 100 缩放）与 \frz30（旋转）并存的参考行（竖排 @-font 场景），
	// 参考帧 Apply Perspective 后应保持既有 fscx/frz 语义（参考帧恒等），
	// 避免 uv 映射把 scale 二次压缩（untransformed 基准需与 source_quad 同宽）
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	// 参考行带 \fscx80 + \frz30（缩放与旋转并存，接近竖排 @-font 场景）
	MotionLine line;
	line.text = "{\\fscx80\\fscy80\\frz30\\pos(960,540)}vertical";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	// identity quads：全屏无透视，参考帧应恒等
	for (int i = 0; i < 2; ++i) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	// 参考帧（relframe=1 → 索引 0）应保留文本
	std::string ref_text = result[0].text;
	EXPECT_NE(ref_text.find("vertical"), std::string::npos);

	// fscx 应保持接近 80（不被 uv 映射二次压缩）
	auto fscx_pos = ref_text.find("\\fscx");
	ASSERT_NE(fscx_pos, std::string::npos);
	double fscx_val = std::stod(ref_text.substr(fscx_pos + 5));
	EXPECT_NEAR(fscx_val, 80.0, 5.0);

	// frz 应保持接近 30（旋转语义被尊重）
	auto frz_pos = ref_text.find("\\frz");
	ASSERT_NE(frz_pos, std::string::npos);
	double frz_val = std::stod(ref_text.substr(frz_pos + 4));
	EXPECT_NEAR(frz_val, 30.0, 5.0);

	// 标签数值一致不足以证明几何等价：对输入与参考帧输出分别做
	// TransformPoints 渲染四角对比（实施计划阶段 4 验收）
	double in_w = 0, in_h = 0;
	PerspectiveTagVals in_tags = processor.PrepareForPerspective(line, in_w, in_h);
	ASSERT_GT(in_w, 0);
	ASSERT_GT(in_h, 0);
	auto in_quad = PerspectiveMath::TransformPoints(in_tags, in_w, in_h, 1.0);
	ASSERT_TRUE(in_quad.has_value());

	MotionLine out_line = result[0];
	out_line.x_position = in_tags.pos_x;
	out_line.y_position = in_tags.pos_y;
	double out_w = 0, out_h = 0;
	PerspectiveTagVals out_tags = processor.PrepareForPerspective(out_line, out_w, out_h);
	ASSERT_GT(out_w, 0);
	ASSERT_GT(out_h, 0);
	auto out_quad = PerspectiveMath::TransformPoints(out_tags, out_w, out_h, 1.0);
	ASSERT_TRUE(out_quad.has_value());

	const double corner_tolerance = 5.0;
	for (size_t i = 0; i < 4; ++i) {
		EXPECT_NEAR((*out_quad)[i].X(), (*in_quad)[i].X(), corner_tolerance)
			<< "Corner " << i << " X mismatch: in=" << (*in_quad)[i].X() << " out=" << (*out_quad)[i].X();
		EXPECT_NEAR((*out_quad)[i].Y(), (*in_quad)[i].Y(), corner_tolerance)
			<< "Corner " << i << " Y mismatch: in=" << (*in_quad)[i].Y() << " out=" << (*out_quad)[i].Y();
	}
}

// ============================================================================
// PerspectiveMapClip 测试
// ============================================================================

TEST(PerspectiveProcessorTest, PerspectiveMapClipRectIdentity) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\clip(100,200,300,400)}text";

	Quad rel_quad  = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080));
	Quad frame_quad = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080));

	processor.PerspectiveMapClip(line, rel_quad, frame_quad);

	// 四边形相同：坐标应不变。矩形 clip 被转为多边形格式
	EXPECT_NE(line.text.find("\\clip("), std::string::npos);
	EXPECT_NE(line.text.find("text"), std::string::npos);
	// 原始的矩形格式(逗号分隔)应被替换
	EXPECT_EQ(line.text.find("100,200,300,400"), std::string::npos);
}

TEST(PerspectiveProcessorTest, PerspectiveMapClipRectTranslation) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\clip(0,0,100,100)}text";

	Quad rel_quad  = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080));
	Quad frame_quad = PerspectiveMath::MakeRect(Vector2D(100, 0), Vector2D(2020, 1080));

	processor.PerspectiveMapClip(line, rel_quad, frame_quad);

	// frame_quad 右移 100px：clip 坐标也应右移
	EXPECT_NE(line.text.find("\\clip("), std::string::npos);
	EXPECT_NE(line.text.find("text"), std::string::npos);
	EXPECT_EQ(line.text.find("\\clip(0,0,100,100)"), std::string::npos);
}

TEST(PerspectiveProcessorTest, PerspectiveMapClipVectorFormat) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	MotionLine line;
	line.text = "{\\clip(m 50 0 100 100 0 100)}text";

	Quad rel_quad  = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080));
	Quad frame_quad = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080));

	processor.PerspectiveMapClip(line, rel_quad, frame_quad);

	// 矢量 clip 格式：坐标应保留
	EXPECT_NE(line.text.find("\\clip("), std::string::npos);
	EXPECT_NE(line.text.find("text"), std::string::npos);
}

// ============================================================================
// Apply 管线：\move → \pos 测试
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyInterpolatesMove) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\move(0,0,100,100,0,1000)}text";
	line.style = "Default";
	line.x_position = 0;
	line.y_position = 0;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	{
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	// \move 应被插值为 \pos
	EXPECT_NE(result[0].text.find("\\pos("), std::string::npos);
	EXPECT_NE(result[0].text.find("text"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyInterpolatesMoveAtFrameMidpoint) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\move(0,0,100,100,0,1000)}text";
	line.style = "Default";
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads = {
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080))
	};
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);
	PerspectiveProcessor::PostprocessLines(result);

	ASSERT_EQ(result.size(), 1u);
	EXPECT_NE(result[0].text.find("\\pos(50,50)"), std::string::npos)
		<< "Move should use the precise frame midpoint, got: " << result[0].text;
}

// ============================================================================
// Apply 管线：\fad 调整测试
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyAdjustsFade) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\fad(100,200)\\pos(960,540)}text";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 2000;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	{
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	// \fad(100,200) → t1=0,t2=100,t3=1800,t4=2000
	// 双时间基准：帧1(0-2000ms)中点 td_shifted=td_original=1000
	// td_shifted=1000 >= t2=100 → fade-in 已结束
	// td_original=1000 < t3=1800 → 处于完全可见段 → alpha=0 (无 alpha 标签)
	EXPECT_EQ(result[0].text.find("\\alpha"), std::string::npos)
		<< "Frame 1 midpoint should have no alpha (fade-in complete)";
	// \fad/\fade 不应保留
	EXPECT_EQ(result[0].text.find("\\fade("), std::string::npos)
		<< "\\fade should not remain in output";
	EXPECT_EQ(result[0].text.find("\\fad("), std::string::npos)
		<< "\\fad should not remain in output";
	EXPECT_NE(result[0].text.find("text"), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyReportsMalformedFade) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\fade(255,0,255,0,100,200)\\pos(960,540)}text";
	line.style = "Default";
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads = {
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080))
	};
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_EQ(result.size(), 1u);
	EXPECT_TRUE(processor.HasMalformedFade());
	EXPECT_NE(result[0].text.find("\\fade("), std::string::npos);
}

TEST(PerspectiveProcessorTest, ApplyStaticizesMultipleFadesInBlock) {
	// 块内同时存在多个 fade 时必须全部静态化，不允许残留动态 fade
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	MotionLine line;
	line.text = "{\\fad(100,200)\\fad(300,400)\\pos(960,540)}text";
	line.style = "Default";
	line.start_time = 0;
	line.end_time = 1600;
	line.duration = 1600;
	line.tokenize_transforms();

	std::vector<Quad> quads(16,
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080)));
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	for (const auto &frame : result) {
		EXPECT_EQ(frame.text.find("\\fad("), std::string::npos)
			<< "All fade tags should be staticized, got: " << frame.text;
		EXPECT_EQ(frame.text.find("\\fade("), std::string::npos)
			<< "All fade tags should be staticized, got: " << frame.text;
	}
	// 首帧两个 fade 都在淡入阶段，应输出组合后的 alpha
	EXPECT_NE(result[0].text.find("\\alpha&H"), std::string::npos)
		<< "Combined fade alpha should be present, got: " << result[0].text;
}

TEST(PerspectiveProcessorTest, ApplyInheritsTagsAndAlphaAcrossOverrideBlocks) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\pos(100,200)\\frz30\\1a&H80&}a{\\fad(1000,0)\\i1}b";
	line.style = "Default";
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads = {
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080))
	};
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);
	PerspectiveProcessor::PostprocessLines(result);

	ASSERT_EQ(result.size(), 1u);
	const auto first_block = result[0].text.find('{');
	ASSERT_NE(first_block, std::string::npos);
	const auto second_block = result[0].text.find('{', first_block + 1);
	ASSERT_NE(second_block, std::string::npos);
	const auto second_end = result[0].text.find('}', second_block);
	ASSERT_NE(second_end, std::string::npos);
	const auto first_end = result[0].text.find('}', first_block);
	ASSERT_NE(first_end, std::string::npos);
	const auto first_pos = result[0].text.find("\\pos(", first_block);
	ASSERT_NE(first_pos, std::string::npos);
	const auto second_content = result[0].text.substr(
		second_block, second_end - second_block + 1
	);
	// \pos/\org 为事件级标签，仅写回首块，后续块不重复写回
	EXPECT_EQ(second_content.find("\\pos("), std::string::npos)
		<< "Global \\pos should not be written to later blocks, got: " << result[0].text;
	// 块级变换标签跨块继承
	EXPECT_NE(second_content.find("\\frz30"), std::string::npos)
		<< "Rotation should inherit across blocks, got: " << result[0].text;
	EXPECT_NE(second_content.find("\\1a&H"), std::string::npos)
		<< "Alpha should inherit across blocks, got: " << result[0].text;
}

// ============================================================================
// Apply 管线：\fad(500,0) 多帧 alpha 递进测试
// 验证逐帧 alpha 值是否与 MoonScript 参考行为一致（线性衰减）
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyFad500ConvertsToProgressiveAlpha) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },  // frame_from_ms: 每帧 100ms
		[](int frame) { return frame * 100; } // ms_from_frame
	);

	// \fad(500,0) + 普通文本，线长 14400ms（144帧×100ms）
	MotionLine line;
	line.text = "{\\fad(500,0)\\pos(960,540)}text";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 14400;
	line.duration = 14400;
	line.tokenize_transforms();

	// 标准四边形（无透视变形）
	std::vector<Quad> quads;
	for (int i = 0; i < 144; ++i) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);
	ASSERT_EQ(result.size(), 144u);

	// 所有帧都不应包含 \fad/\fade
	for (auto &frame : result) {
		EXPECT_EQ(frame.text.find("\\fad("), std::string::npos)
			<< "\\fad should not remain in output";
		EXPECT_EQ(frame.text.find("\\fade("), std::string::npos)
			<< "\\fade should not remain in output";
	}

	// 解析每帧 alpha 值验证递减趋势
	// \fad(500,0) → t1=0,t2=500,t3=14400,t4=14400
	// 帧内中点采样：每帧采样点为可见区间中点
	// 帧1(0-100ms): 采样点 50 → alpha=255-255*50/500=229.5 → \alpha&HE6&
	// 采样点 >= t2=500 时 fade 结束，不再输出 alpha 标签
	{
		auto &f0 = result[0];
		EXPECT_NE(f0.text.find("\\alpha&HE6&"), std::string::npos)
			<< "Frame 1 (0-100ms) midpoint alpha should be ~230";
	}

	// 验证 alpha 单调递减到消失
	int prev_alpha = 256;
	int fade_frames = 0;
	std::regex alpha_re(R"(\\alpha&H([0-9A-Fa-f]{2})&)");
	for (size_t i = 0; i < result.size(); ++i) {
		auto &frame = result[i];
		std::smatch m;
		bool has_alpha = std::regex_search(frame.text, m, alpha_re);
		if (!has_alpha) {
			// 找到第一个无 alpha 的帧 → fade 已结束
			// 之后所有帧都不应有 alpha
			for (size_t j = i; j < result.size(); ++j) {
				EXPECT_EQ(std::regex_search(result[j].text, m, alpha_re), false)
					<< "Frame " << j << " after fade end should have no alpha";
			}
			break;
		}
		++fade_frames;
		int alpha = std::stoi(m[1].str(), nullptr, 16);
		EXPECT_LE(alpha, prev_alpha)
			<< "Frame " << i << " alpha should be <= previous";
		prev_alpha = alpha;
	}

	// 500ms fade-in at 100ms/frame ≈ 5 frames
	EXPECT_GT(fade_frames, 3) << "Should have multiple fade frames";
	EXPECT_LT(fade_frames, 7) << "Fade should complete within expected range";
}

// ============================================================================
// Apply 管线：完整 \fade(7参) 逐帧 alpha 测试
// 验证完整 \fade(a1,a2,a3,t1,t2,t3,t4) 通过管道后变为静态 \alpha 标签
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyFadeFull7ParamConvertsToAlpha) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	// \fade(255,0,255,  0,500,13000,14400)
	// 淡入(透明→不透明,0-500ms) → 完全可见(500-13000ms) → 淡出(不透明→透明,13000-14400ms)
	MotionLine line;
	line.text = "{\\fade(255,0,255,0,500,13000,14400)\\pos(960,540)}text";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 14400;
	line.duration = 14400;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	for (int i = 0; i < 144; ++i) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);
	ASSERT_EQ(result.size(), 144u);

	// 所有帧都不应包含 \fad/\fade
	for (auto &frame : result) {
		EXPECT_EQ(frame.text.find("\\fad("), std::string::npos)
			<< "\\fad should not remain in output";
		EXPECT_EQ(frame.text.find("\\fade("), std::string::npos)
			<< "\\fade should not remain in output";
	}

	// 帧1(0-100ms): td_shifted=50，淡入进行中 alpha=255-(255*50/500)=230 → \alpha&HE6&
	{
		auto &f0 = result[0];
		EXPECT_NE(f0.text.find("\\alpha&HE6&"), std::string::npos)
			<< "Frame 1 fade-in midpoint alpha should be ~230";
	}

	// 帧30(2900-3000ms): td_original=2950 < t3=13000 → 完全不透明 → 无alpha标签
	{
		auto &f29 = result[29];
		EXPECT_EQ(f29.text.find("\\alpha&H"), std::string::npos)
			<< "Frame 30 fully opaque (td_original < t3) should have no alpha";
	}

	// 帧140(13900-14000ms): td_original=13950 淡出进行中
	// alpha=0+(255-0)*(13950-13000)/(14400-13000)=173 → \alpha&HAD&
	{
		auto &f139 = result[139];
		EXPECT_NE(f139.text.find("\\alpha&HAD&"), std::string::npos)
			<< "Frame 140 fade-out alpha should be ~173";
	}
}

TEST(PerspectiveProcessorTest, ApplyFad500WithTransformStaticizesBeforeFade) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	// \fad(500,0) + \t(0,14400,\fscx200) 共存
	// 对应上游 line2fbf：\t 先按当前帧静态化，fade 再处理 alpha，
	// fade 处理不得破坏已静态化的 transform 标签
	MotionLine line;
	line.text = "{\\fad(500,0)\\t(0,14400,\\fscx200)\\pos(960,540)}text";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 14400;
	line.duration = 14400;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	for (int i = 0; i < 10; ++i) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);
	ASSERT_EQ(result.size(), 10u);

	// \fad/\fade 不应保留
	for (auto &frame : result) {
		EXPECT_EQ(frame.text.find("\\fad("), std::string::npos);
		EXPECT_EQ(frame.text.find("\\fade("), std::string::npos);
	}

	// \t 已在帧内采样点静态化，不应残留 \t(...) 结构
	for (auto &frame : result) {
		EXPECT_EQ(frame.text.find("\\t("), std::string::npos)
			<< "\\t should be staticized per frame, got: " << frame.text;
	}

	// 首帧（0-100ms）采样点为帧中点 50ms：fade-in 未完成，应带 \alpha
	EXPECT_NE(result[0].text.find("\\alpha&H"), std::string::npos)
		<< "First frame should be mid fade-in, got: " << result[0].text;

	// 静态化后的 \fscx 标签应存在（fade 处理不得破坏 transform 结果）
	EXPECT_NE(result[0].text.find("\\fscx"), std::string::npos)
		<< "\\fscx was corrupted by fade processing, got: " << result[0].text;
}

// ============================================================================
// 阶段 3：fade 与已有 alpha 通道组合、scaled clip、多块 clip
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyFadeCombinesWithPerChannelAlpha) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	// \1a=0x80(128)、\2a=0x40(64)，fade-in 500ms
	// 帧1 采样点 50ms：fade_val=229.5，opacity=0.1
	// 1a: 255-0.1*(255-128)=242.3→242(F2)，2a: 255-0.1*(255-64)=235.9→236(EC)
	// 3a/4a 无样式默认 0：230(E6)，四通道不同，不得压缩为统一 \alpha
	MotionLine line;
	line.text = "{\\1a&H80&\\2a&H40&\\fad(500,0)\\pos(960,540)}text";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 14400;
	line.duration = 14400;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	for (int i = 0; i < 10; ++i) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);
	ASSERT_FALSE(result.empty());

	// 首帧应保留各通道组合后的 alpha，而不是统一 \alpha
	EXPECT_NE(result[0].text.find("\\1a&HF2&"), std::string::npos)
		<< "Primary alpha channel should be combined with fade, got: " << result[0].text;
	EXPECT_NE(result[0].text.find("\\2a&HEC&"), std::string::npos)
		<< "Secondary alpha channel should be combined with fade, got: " << result[0].text;
	EXPECT_EQ(result[0].text.find("\\alpha&H"), std::string::npos)
		<< "Per-channel alphas must not be collapsed into \\alpha, got: " << result[0].text;
}

TEST(PerspectiveProcessorTest, ApplyFadeModulatesLaterBlockAlpha) {
	// 分块场景：fade 出现在前块，后块显式 alpha 覆盖需乘以行级 fade 因子
	// （ASS 渲染语义：\fad/\fade 从出现位置持续调制到行尾）
	// 对应审查问题：AdjustFadeInBlock 原实现 fade 因子只作用于 fade 所在块
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	// 帧1(0-100ms)采样点 50ms：fade-in 500ms → fade_val=229.5，opacity=0.1
	// 块1（fade 所在块，样式默认 alpha 0）：255-0.1*255=229.5→230(E6)，四通道相同
	// 块2（\1a&H80& 覆盖通道0）：255-0.1*(255-128)=242.3→242(F2)，
	// 非覆盖通道沿用继承值 230(E6)，通道不均一 → 逐通道写回
	MotionLine line;
	line.text = "{\\fad(500,0)}text1{\\1a&H80&}text2";
	line.style = "Default";
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 2000;
	line.tokenize_transforms();

	std::vector<Quad> quads(20,
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080)));
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	EXPECT_NE(result[0].text.find("\\alpha&HE6&"), std::string::npos)
		<< "Fade block alpha should be emitted, got: " << result[0].text;
	EXPECT_NE(result[0].text.find("\\1a&HF2&"), std::string::npos)
		<< "Later block explicit alpha must be modulated by line-level fade, got: "
		<< result[0].text;
	EXPECT_EQ(result[0].text.find("\\1a&H80&"), std::string::npos)
		<< "Original later-block alpha should be replaced, got: " << result[0].text;
	EXPECT_EQ(result[0].text.find("\\fad("), std::string::npos)
		<< "Fade tags should be staticized, got: " << result[0].text;
}

TEST(PerspectiveProcessorTest, ApplyFadeInLaterBlockKeepsEarlierAlpha) {
	// 反向场景：fade 出现在后块，前块 alpha 覆盖不被调制
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	// 块1 \1a&H80& 在 fade 之前，不被调制，原样保留，
	// 块2 fade 与继承 alpha 组合：255-0.1*(255-128)=242(F2)，其余 230(E6)
	MotionLine line;
	line.text = "{\\1a&H80&}text1{\\fad(500,0)}text2";
	line.style = "Default";
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 2000;
	line.tokenize_transforms();

	std::vector<Quad> quads(20,
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080)));
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	EXPECT_NE(result[0].text.find("\\1a&H80&"), std::string::npos)
		<< "Earlier block alpha should be kept unchanged, got: " << result[0].text;
	EXPECT_NE(result[0].text.find("\\1a&HF2&"), std::string::npos)
		<< "Fade block should combine with inherited alpha, got: " << result[0].text;
	EXPECT_EQ(result[0].text.find("\\fad("), std::string::npos)
		<< "Fade tags should be staticized, got: " << result[0].text;
}

TEST(PerspectiveProcessorTest, ApplyFadeAcrossThreeBlocksPropagatesModulatedAlpha) {
	// 3 块链：块1 fade → 块2 显式 alpha 覆盖被调制 → 块3 再 fade，
	// 块3 必须基于块2 已调制的通道值组合（inherited_alpha 回写验证）
	// 帧1 采样点 50ms：fade-in 500ms → opacity=0.1
	// 块1：255-0.1*255=230(E6)，块2 通道0：255-0.1*(255-128)=242(F2)
	// 块3 通道0：255-0.1*(255-242)=254(FE)，其余：255-0.1*(255-230)=253(FD)
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	MotionLine line;
	line.text = "{\\fad(500,0)}text1{\\1a&H80&}text2{\\fad(500,0)}text3";
	line.style = "Default";
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 2000;
	line.tokenize_transforms();

	std::vector<Quad> quads(20,
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080)));
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	EXPECT_NE(result[0].text.find("\\1a&HFE&"), std::string::npos)
		<< "Third block must use modulated alpha from block 2, got: " << result[0].text;
	EXPECT_EQ(result[0].text.find("\\1a&H80&"), std::string::npos)
		<< "Original block-2 alpha should be replaced, got: " << result[0].text;
}

TEST(PerspectiveProcessorTest, ApplyOpaqueFadeKeepsModulatedOverrideInText) {
	// 早退分支：块2 的 \fad(0,0) 因子=1（完全不透明），但前面块 fade 已生效，
	// 块2 显式 \1a 覆盖必须写回调制值（文本与继承状态一致）
	// 帧1 采样点 50ms：fad(200,0) → fade_val=191.25，opacity=0.25
	// 块1：255-0.25*255=191(BF)；块2 通道0：255-0.25*(255-128)=223(DF)
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	MotionLine line;
	line.text = "{\\fad(200,0)}text1{\\fad(0,0)\\1a&H80&}text2";
	line.style = "Default";
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 2000;
	line.tokenize_transforms();

	std::vector<Quad> quads(20,
		PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080)));
	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	EXPECT_NE(result[0].text.find("\\1a&HDF&"), std::string::npos)
		<< "Opaque fade block must still write modulated override, got: " << result[0].text;
	EXPECT_EQ(result[0].text.find("\\1a&H80&"), std::string::npos)
		<< "Original alpha should be replaced, got: " << result[0].text;
	EXPECT_EQ(result[0].text.find("\\fad("), std::string::npos)
		<< "Fade tags should be staticized, got: " << result[0].text;
}

TEST(PerspectiveProcessorTest, PerspectiveMapClipScaledVectorClip) {
	PerspectiveProcessor processor(PerspectiveOptions{}, 1920, 1080);

	MotionLine line;
	line.text = "{\\clip(2,m 100 200 l 300 400)}text";

	Quad rel_quad  = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080));
	Quad frame_quad = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080));

	processor.PerspectiveMapClip(line, rel_quad, frame_quad);

	// scale=2 时坐标先按 2^(2-1)=2 还原，identity quad 下映射不变
	EXPECT_NE(line.text.find("m 50"), std::string::npos)
		<< "Scaled vector clip should be unscaled before mapping, got: " << line.text;
	EXPECT_NE(line.text.find("l 150"), std::string::npos)
		<< "Scaled vector clip should be unscaled before mapping, got: " << line.text;
	EXPECT_EQ(line.text.find("\\clip(2,"), std::string::npos);
}

TEST(PerspectiveProcessorTest, PerspectiveMapClipAllBlocks) {
	PerspectiveProcessor processor(PerspectiveOptions{}, 1920, 1080);

	MotionLine line;
	line.text = "{\\clip(10,20,110,120)}a{\\clip(30,40,130,140)}b";

	Quad rel_quad  = PerspectiveMath::MakeRect(Vector2D(0, 0), Vector2D(1920, 1080));
	// frame quad 右移 100：两个块的 clip 都应映射
	Quad frame_quad = PerspectiveMath::MakeRect(Vector2D(100, 0), Vector2D(2020, 1080));

	processor.PerspectiveMapClip(line, rel_quad, frame_quad);

	// 每个 override 块的 clip 都要被映射（矩形 clip 转为四点多边形，坐标右移 100）
	EXPECT_NE(line.text.find("\\clip(110 20 210 20 210 120 110 120)"), std::string::npos)
		<< "First block clip should be mapped, got: " << line.text;
	EXPECT_NE(line.text.find("\\clip(130 40 230 40 230 140 130 140)"), std::string::npos)
		<< "Second block clip should be mapped, got: " << line.text;
	EXPECT_NE(line.text.find("a"), std::string::npos);
	EXPECT_NE(line.text.find("b"), std::string::npos);
}

// ============================================================================
// 阶段 4：Apply Perspective 几何黄金测试
// identity quad 下，参考帧输出标签的渲染四角必须与输入行一致
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyPerspectiveIdentityQuadPreservesRenderedQuad) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	// 参考行带非默认缩放和旋转（竖排 @-font 场景）
	MotionLine line;
	line.text = "{\\fscx80\\fscy80\\frz30\\pos(960,540)}vertical";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 2000;
	line.tokenize_transforms();

	// 输入行渲染四角
	double in_w = 0, in_h = 0;
	PerspectiveTagVals in_tags = processor.PrepareForPerspective(line, in_w, in_h);
	ASSERT_GT(in_w, 0);
	ASSERT_GT(in_h, 0);
	auto in_quad = PerspectiveMath::TransformPoints(in_tags, in_w, in_h, 1.0);
	ASSERT_TRUE(in_quad.has_value());

	std::vector<Quad> quads;
	for (int i = 0; i < 2; ++i) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);
	ASSERT_EQ(result.size(), 2u);

	// 参考帧（relframe=1）输出标签渲染四角
	MotionLine out_line = result[0];
	out_line.x_position = in_tags.pos_x;
	out_line.y_position = in_tags.pos_y;
	double out_w = 0, out_h = 0;
	PerspectiveTagVals out_tags = processor.PrepareForPerspective(out_line, out_w, out_h);
	ASSERT_GT(out_w, 0);
	ASSERT_GT(out_h, 0);
	auto out_quad = PerspectiveMath::TransformPoints(out_tags, out_w, out_h, 1.0);
	ASSERT_TRUE(out_quad.has_value());

	// 渲染四角必须一致（输入宽度 360 量级，容差 5px）
	const double tolerance = 5.0;
	for (size_t i = 0; i < 4; ++i) {
		EXPECT_NEAR((*out_quad)[i].X(), (*in_quad)[i].X(), tolerance)
			<< "Corner " << i << " X mismatch: in=" << (*in_quad)[i].X() << " out=" << (*out_quad)[i].X();
		EXPECT_NEAR((*out_quad)[i].Y(), (*in_quad)[i].Y(), tolerance)
			<< "Corner " << i << " Y mismatch: in=" << (*in_quad)[i].Y() << " out=" << (*out_quad)[i].Y();
	}
}

TEST(PerspectiveProcessorTest, ApplyPerspectiveIdentityQuadPreservesShearedQuad) {
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	// 参考行带 \fax 剪切（上游注释明确 \fax 场景是已知盲区，验证当前行为可接受）
	MotionLine line;
	line.text = "{\\fscx120\\fax0.5\\frz15\\pos(1582.67,470.67)}sheared";
	line.style = "Default";
	line.x_position = 1582.67;
	line.y_position = 470.67;
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;
	line.tokenize_transforms();

	double in_w = 0, in_h = 0;
	PerspectiveTagVals in_tags = processor.PrepareForPerspective(line, in_w, in_h);
	ASSERT_GT(in_w, 0);
	auto in_quad = PerspectiveMath::TransformPoints(in_tags, in_w, in_h, 1.0);
	ASSERT_TRUE(in_quad.has_value());

	std::vector<Quad> quads;
	{
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);
	ASSERT_EQ(result.size(), 1u);

	MotionLine out_line = result[0];
	out_line.x_position = in_tags.pos_x;
	out_line.y_position = in_tags.pos_y;
	double out_w = 0, out_h = 0;
	PerspectiveTagVals out_tags = processor.PrepareForPerspective(out_line, out_w, out_h);
	ASSERT_GT(out_w, 0);
	auto out_quad = PerspectiveMath::TransformPoints(out_tags, out_w, out_h, 1.0);
	ASSERT_TRUE(out_quad.has_value());

	// \fax 剪切复合是上游已知盲区，本测试锁定当前实现的几何偏差上界，
	// 2026-08 复测实际四角偏差 < 5px，容差由 40px 收紧至 5px，
	// 若未来改动使偏差超过 5px，必须重新对照上游验证而非放宽本容差
	const double tolerance = 5.0;
	for (size_t i = 0; i < 4; ++i) {
		EXPECT_NEAR((*out_quad)[i].X(), (*in_quad)[i].X(), tolerance)
			<< "Corner " << i << " X mismatch: in=" << (*in_quad)[i].X() << " out=" << (*out_quad)[i].X();
		EXPECT_NEAR((*out_quad)[i].Y(), (*in_quad)[i].Y(), tolerance)
			<< "Corner " << i << " Y mismatch: in=" << (*in_quad)[i].Y() << " out=" << (*out_quad)[i].Y();
	}
}

// ============================================================================
// CalculateDrawingExtents: 绘图尺寸计算测试
// ============================================================================

TEST(PerspectiveProcessorTest, DrawingExtentsBasicRect) {
	double w = 0, h = 0;
	EXPECT_TRUE(mocha::CalculateDrawingExtents("m 0 0 l 100 0 l 100 50 l 0 50", 1, w, h));
	EXPECT_NEAR(w, 100.0, 0.1);
	EXPECT_NEAR(h, 50.0, 0.1);
}

TEST(PerspectiveProcessorTest, DrawingExtentsWithScale) {
	double w = 0, h = 0;
	EXPECT_TRUE(mocha::CalculateDrawingExtents("m 0 0 l 200 0 l 200 100 l 0 100", 2, w, h));
	EXPECT_NEAR(w, 100.0, 0.1);
	EXPECT_NEAR(h, 50.0, 0.1);
}

TEST(PerspectiveProcessorTest, DrawingExtentsOffset) {
	double w = 0, h = 0;
	EXPECT_TRUE(mocha::CalculateDrawingExtents("m 50 30 l 150 30 l 150 80 l 50 80", 1, w, h));
	EXPECT_NEAR(w, 100.0, 0.1);
	EXPECT_NEAR(h, 50.0, 0.1);
}

TEST(PerspectiveProcessorTest, DrawingExtentsBezier) {
	double w = 0, h = 0;
	EXPECT_TRUE(mocha::CalculateDrawingExtents("m 0 0 b 100 0 100 100 0 100", 1, w, h));
	EXPECT_GT(w, 0);
	EXPECT_GT(h, 0);
	EXPECT_NEAR(w, 100.0, 0.1);
	EXPECT_NEAR(h, 100.0, 0.1);
}

TEST(PerspectiveProcessorTest, DrawingExtentsInsufficientPoints) {
	double w = 0, h = 0;
	EXPECT_FALSE(mocha::CalculateDrawingExtents("m 0 0", 1, w, h));
}

TEST(PerspectiveProcessorTest, DrawingExtentsNegativeCoords) {
	double w = 0, h = 0;
	EXPECT_TRUE(mocha::CalculateDrawingExtents("m -10 -20 l 90 -20 l 90 30 l -10 30", 1, w, h));
	EXPECT_NEAR(w, 100.0, 0.1);
	EXPECT_NEAR(h, 50.0, 0.1);
}

TEST(PerspectiveProcessorTest, DrawingExtentsScale4) {
	double w = 0, h = 0;
	EXPECT_TRUE(mocha::CalculateDrawingExtents("m 0 0 l 800 0 l 800 400 l 0 400", 4, w, h));
	EXPECT_NEAR(w, 100.0, 0.1);
	EXPECT_NEAR(h, 50.0, 0.1);
}

// ============================================================================
// 宽高基准 fscx 不变性（对应上游 981ce33：drawing/fallback 尺寸不应被 \fscx 二次缩放）
// 通过真实 PrepareForPerspective 调用验证
// ============================================================================

TEST(PerspectiveProcessorTest, DrawingWidthIsScaleInvariant) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	// drawing 行：CalculateDrawingExtents 返回的宽高不含 \fscx，
	// 不应被无条件除法二次缩放（修复前 fscx200 会把宽高除 2）
	MotionLine line_base;
	line_base.text = "{\\p1}m 0 0 l 100 0 l 100 50 l 0 50";
	line_base.style = "Default";
	line_base.x_position = 0;
	line_base.y_position = 0;
	double w0 = 0, h0 = 0;
	processor.PrepareForPerspective(line_base, w0, h0);

	MotionLine line_scaled;
	line_scaled.text = "{\\p1\\fscx200\\fscy200}m 0 0 l 100 0 l 100 50 l 0 50";
	line_scaled.style = "Default";
	line_scaled.x_position = 0;
	line_scaled.y_position = 0;
	double w2 = 0, h2 = 0;
	processor.PrepareForPerspective(line_scaled, w2, h2);

	// drawing 宽高是未缩放基准，fscx 不应改变它
	EXPECT_NEAR(w2, w0, 0.1);
	EXPECT_NEAR(h2, h0, 0.1);
}

TEST(PerspectiveProcessorTest, FallbackTextWidthIsScaleInvariant) {
	PerspectiveOptions opts;
	PerspectiveProcessor processor(opts, 1920, 1080);

	// 无样式查找时走字符估算 fallback，其宽高为未缩放基准，
	// 不应被 \fscx 二次缩放（修复前 fscx200 会把宽高除 2）
	MotionLine line_base;
	line_base.text = "{\\fs48}test";
	line_base.style = "Default";
	double w0 = 0, h0 = 0;
	processor.PrepareForPerspective(line_base, w0, h0);

	MotionLine line_scaled;
	line_scaled.text = "{\\fs48\\fscx200\\fscy200}test";
	line_scaled.style = "Default";
	double w2 = 0, h2 = 0;
	processor.PrepareForPerspective(line_scaled, w2, h2);

	EXPECT_NEAR(w2, w0, 0.1);
	EXPECT_NEAR(h2, h0, 0.1);
}

// ============================================================================
// 阶段 2：无 \pos 回退、前缀文本保留、块内 transform 静态化
// ============================================================================

TEST(PerspectiveProcessorTest, ApplyFallsBackToStylePositionWithoutPos) {
	// 无显式 \pos 的普通字幕应使用样式对齐计算出的默认位置，而不是 (0,0)
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = false;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "{\\an7}plain text";
	line.style = "Default";
	// 样式对齐(an7)计算的默认位置
	line.x_position = 320;
	line.y_position = 48;
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	{
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	// identity quad 下输出 pos 应接近样式默认位置，而不是 (0,0)
	EXPECT_NE(result[0].text.find("\\pos(320,"), std::string::npos)
		<< "Expected style default position fallback, got: " << result[0].text;
}

TEST(PerspectiveProcessorTest, ApplyPreservesLeadingPlainText) {
	// 行首普通文本 + 内联样式块：重建时必须保留前缀文本
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	MotionLine line;
	line.text = "lead{\\i1\\pos(100,200)}tail";
	line.style = "Default";
	line.x_position = 100;
	line.y_position = 200;
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	{
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_FALSE(result.empty());
	// 前缀文本 "lead" 和块后文本 "tail" 都必须保留
	EXPECT_NE(result[0].text.find("lead"), std::string::npos)
		<< "Leading plain text was dropped, got: " << result[0].text;
	EXPECT_NE(result[0].text.find("tail"), std::string::npos)
		<< "Trailing text was dropped, got: " << result[0].text;
	// 非透视标签 \i1 应保留
	EXPECT_NE(result[0].text.find("\\i1"), std::string::npos)
		<< "Non-perspective tag was dropped, got: " << result[0].text;
}

TEST(PerspectiveProcessorTest, ApplyStaticizesTransformAtFrameMidpoint) {
	// 块内 \t 应在当前帧采样点静态化，不再保留 \t(...) 结构
	PerspectiveOptions opts;
	opts.relframe = 1;
	opts.start_frame = 1;
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_pos = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.org_mode = 2;
	opts.preview = false;

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	MotionLine line;
	line.text = "{\\fscx100\\t(0,100,\\fscx200)\\pos(960,540)}text";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 200;
	line.duration = 200;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	for (int i = 0; i < 2; ++i) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);

	ASSERT_EQ(result.size(), 2u);
	for (const auto &frame : result) {
		EXPECT_EQ(frame.text.find("\\t("), std::string::npos)
			<< "\\t should be staticized per frame, got: " << frame.text;
	}
	// 首帧（0-100ms）采样点 50ms：fscx = 100 + 0.5*100 = 150
	EXPECT_NE(result[0].text.find("\\fscx150"), std::string::npos)
		<< "First frame should sample transform at frame midpoint, got: " << result[0].text;
}


// ============================================================================
// ComputeEffectiveStartFrame: 反向追踪帧号换算测试（实施计划 5.1 验收）
// ============================================================================

TEST(PerspectiveProcessorTest, EffectiveStartFrameReverseFromLastReferenceFrame) {
	// 计划验收标准：总帧数 10、参考帧 10、反向追踪 → 正序输出起始帧为 1
	EXPECT_EQ(mocha::ComputeEffectiveStartFrame(
		/*start_frame=*/10, /*relative=*/true, /*reverse_tracking=*/true,
		/*relframe=*/10, /*total_frames=*/10, /*collection_start_frame=*/0), 1);
}

TEST(PerspectiveProcessorTest, EffectiveStartFrameReversePartialRange) {
	// 总帧数 20、参考帧 15、反向追踪 → 起始帧 20-15+1 = 6
	EXPECT_EQ(mocha::ComputeEffectiveStartFrame(
		5, true, true, 15, 20, 100), 6);
}

TEST(PerspectiveProcessorTest, EffectiveStartFrameAbsoluteToRelative) {
	// 绝对帧号换算只发生一次：绝对帧 105、集合起始 100 → 相对帧 6
	EXPECT_EQ(mocha::ComputeEffectiveStartFrame(
		105, false, false, 1, 10, 100), 6);
}

TEST(PerspectiveProcessorTest, EffectiveStartFrameRelativePassthrough) {
	// 相对帧号且非反向：原样透传
	EXPECT_EQ(mocha::ComputeEffectiveStartFrame(
		3, true, false, 1, 10, 100), 3);
}

TEST(PerspectiveProcessorTest, EffectiveStartFrameRelativeLastFrame) {
	// 相对帧号 -1 表示追踪数据最后一帧
	EXPECT_EQ(mocha::ComputeEffectiveStartFrame(
		-1, true, false, 1, 10, 100), 10);
}

TEST(PerspectiveProcessorTest, EffectiveStartFrameRelativeZeroUsesFirstFrame) {
	// 相对帧号 0 与对话框约定一致，自动调整为第一帧
	EXPECT_EQ(mocha::ComputeEffectiveStartFrame(
		0, true, false, 1, 10, 100), 1);
}

TEST(PerspectiveProcessorTest, ReverseTrackingOutputsFullFrameCount) {
	// 计划验收标准后半段：反向换算得到起始帧 1 时，处理器输出全部 10 帧
	PerspectiveOptions opts;
	opts.relframe = 10;
	opts.start_frame = mocha::ComputeEffectiveStartFrame(10, true, true, 10, 10, 0);
	opts.selection_start_frame = 0;
	opts.apply_perspective = true;
	opts.track_clip = false;
	opts.track_bord_shad = false;
	opts.preview = false;
	ASSERT_EQ(opts.start_frame, 1);

	PerspectiveProcessor processor(opts, 1920, 1080);
	processor.SetTimingFunctions(
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	MotionLine line;
	line.text = "{\\pos(960,540)}text";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<Quad> quads;
	for (int i = 0; i < 10; ++i) {
		Quad q;
		q.push_back(Vector2D(0, 0));
		q.push_back(Vector2D(1920, 0));
		q.push_back(Vector2D(1920, 1080));
		q.push_back(Vector2D(0, 1080));
		quads.push_back(std::move(q));
	}

	std::vector<MotionLine> lines = {line};
	auto result = processor.Apply(lines, quads, 1920, 1080);
	EXPECT_EQ(result.size(), 10u)
		<< "Reverse tracking with relframe=total should output all 10 frames in forward order";
	// 输出保持时间正序
	for (size_t i = 1; i < result.size(); ++i) {
		EXPECT_LE(result[i - 1].start_time, result[i].start_time)
			<< "Output lines must remain in forward time order";
	}
}

TEST(PerspectiveProcessorTest, DataHandlerRejectsMidBlockHole) {
	// 角点 0002 的两行数据之间插入空行（数据块中部空洞），
	// 不能静默截断后继续解析，必须判定为非法数据
	std::string holed =
		"Adobe After Effects 6.0 Keyframe Data\r\n"
		"\r\n"
		"Effects\tCC Power Pin #1\tCC Power Pin-0002\r\n"
		"\tFrame\tX\tY\r\n"
		"0\t0\t0\r\n"
		"\r\n"
		"1\t100\t100\r\n"
		"\r\n"
		"Effects\tCC Power Pin #1\tCC Power Pin-0003\r\n"
		"\tFrame\tX\tY\r\n"
		"0\t200\t0\r\n"
		"1\t300\t100\r\n"
		"\r\n"
		"Effects\tCC Power Pin #1\tCC Power Pin-0005\r\n"
		"\tFrame\tX\tY\r\n"
		"0\t200\t200\r\n"
		"1\t300\t300\r\n"
		"\r\n"
		"Effects\tCC Power Pin #1\tCC Power Pin-0004\r\n"
		"\tFrame\tX\tY\r\n"
		"0\t0\t200\r\n"
		"1\t100\t300\r\n"
		"\r\n"
		"End of Keyframe Data\r\n";
	PerspectiveDataHandler dh;
	EXPECT_FALSE(dh.ParsePowerPin(holed))
		<< "Mid-block hole must be rejected instead of silently truncated";
}

TEST(PerspectiveProcessorTest, DataHandlerAcceptsBlankLineBeforeNextSection) {
	// 块尾空行 + 下一节 header 是 After Effects 导出的正常格式，必须仍可解析
	PerspectiveDataHandler dh;
	EXPECT_TRUE(dh.ParsePowerPin(POWERPIN_VALID_DATA));
	EXPECT_EQ(dh.Length(), 2);
}
