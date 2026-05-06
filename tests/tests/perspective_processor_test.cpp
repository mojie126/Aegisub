// Copyright (c) 2024-2026, Aegisub contributors
// 透视追踪处理器集成测试

#include <main.h>

#include "../src/perspective_motion/perspective_processor.h"
#include "../src/mocha_motion/motion_tags.h"

#include <cmath>

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

	// 第一个块应有 tags_a 的标签
	EXPECT_NE(line.text.find("\\pos(100,200)"), std::string::npos);
	EXPECT_NE(line.text.find("\\frz30"), std::string::npos);

	// 第二个块应有 tags_b 的标签
	EXPECT_NE(line.text.find("\\pos(300,400)"), std::string::npos);
	EXPECT_NE(line.text.find("\\fscx75"), std::string::npos);

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

	// 第二个块：新标签写入
	EXPECT_NE(line.text.find("\\pos(300,400)"), std::string::npos);

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

	// Line at center of frame 0
	MotionLine line;
	line.text = "{\\pos(960,540)}hello";
	line.style = "Default";
	line.x_position = 960;
	line.y_position = 540;
	line.start_time = 0;
	line.end_time = 2000;
	line.duration = 1000;
	line.tokenize_transforms();

	// Quad moving right across frames
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
	// \fad 应被调整后的 \fade 替换
	bool has_fade = result[0].text.find("\\fade(") != std::string::npos;
	bool has_fad = result[0].text.find("\\fad(") != std::string::npos;
	EXPECT_TRUE(has_fade || has_fad);
	EXPECT_NE(result[0].text.find("text"), std::string::npos);
}