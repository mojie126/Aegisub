// Copyright (c) 2024-2026, Aegisub contributors
// 摩卡追踪模块完整测试套件

#include <main.h>

#include "motion_math.h"
#include "motion_tags.h"
#include "motion_transform.h"
#include "motion_line.h"
#include "motion_data_handler.h"
#include "motion_handler.h"
#include "motion_processor.h"

#include <algorithm>
#include <regex>

using namespace mocha;

// ============================================================================
// math 工具测试
// ============================================================================

class MochaMath : public libagi {};

TEST(MochaMath, RoundZeroDecimalPlaces) {
	EXPECT_DOUBLE_EQ(math::round(3.7), 4.0);
	EXPECT_DOUBLE_EQ(math::round(3.4), 3.0);
	EXPECT_DOUBLE_EQ(math::round(3.5), 4.0);
	EXPECT_DOUBLE_EQ(math::round(0.0), 0.0);
}

TEST(MochaMath, RoundWithDecimalPlaces) {
	EXPECT_DOUBLE_EQ(math::round(3.456, 2), 3.46);
	EXPECT_DOUBLE_EQ(math::round(3.454, 2), 3.45);
	EXPECT_DOUBLE_EQ(math::round(100.0, 3), 100.0);
}

TEST(MochaMath, RoundNegativeNumbers) {
	// floor(x + 0.5) 行为：-0.5 → floor(0) = 0（非标准 round 的 -1）
	EXPECT_DOUBLE_EQ(math::round(-0.5), 0.0);
	EXPECT_DOUBLE_EQ(math::round(-1.5), -1.0);
	EXPECT_DOUBLE_EQ(math::round(-3.7), -4.0);
}

TEST(MochaMath, DCosBasic) {
	EXPECT_NEAR(math::d_cos(0), 1.0, 1e-10);
	EXPECT_NEAR(math::d_cos(90), 0.0, 1e-10);
	EXPECT_NEAR(math::d_cos(180), -1.0, 1e-10);
	EXPECT_NEAR(math::d_cos(360), 1.0, 1e-10);
}

TEST(MochaMath, DSinBasic) {
	EXPECT_NEAR(math::d_sin(0), 0.0, 1e-10);
	EXPECT_NEAR(math::d_sin(90), 1.0, 1e-10);
	EXPECT_NEAR(math::d_sin(180), 0.0, 1e-10);
	EXPECT_NEAR(math::d_sin(270), -1.0, 1e-10);
}

TEST(MochaMath, DAtanBasic) {
	EXPECT_NEAR(math::d_atan(0, 1), 0.0, 1e-10);
	EXPECT_NEAR(math::d_atan(1, 0), 90.0, 1e-10);
	EXPECT_NEAR(math::d_atan(0, -1), 180.0, 1e-10);
	EXPECT_NEAR(math::d_atan(-1, 0), -90.0, 1e-10);
}

TEST(MochaMath, ClampInRange) {
	EXPECT_DOUBLE_EQ(math::clamp(5.0, 0.0, 10.0), 5.0);
}

TEST(MochaMath, ClampBelowMin) {
	EXPECT_DOUBLE_EQ(math::clamp(-1.0, 0.0, 10.0), 0.0);
}

TEST(MochaMath, ClampAboveMax) {
	EXPECT_DOUBLE_EQ(math::clamp(15.0, 0.0, 10.0), 10.0);
}

// ============================================================================
// TagRegistry 标签注册表测试
// ============================================================================

class MochaTags : public libagi {};

TEST(MochaTags, RegistrySingleton) {
	const auto &r1 = TagRegistry::instance();
	const auto &r2 = TagRegistry::instance();
	EXPECT_EQ(&r1, &r2);
}

TEST(MochaTags, OneTimeTagsNotEmpty) {
	const auto &registry = TagRegistry::instance();
	EXPECT_FALSE(registry.one_time_tags().empty());
}

TEST(MochaTags, TransformTagsNotEmpty) {
	const auto &registry = TagRegistry::instance();
	EXPECT_FALSE(registry.transform_tags().empty());
}

TEST(MochaTags, FindTagByName) {
	const auto &registry = TagRegistry::instance();
	const auto *tag = registry.get("xscale");
	EXPECT_NE(tag, nullptr);
	if (tag) {
		EXPECT_FALSE(tag->pattern.empty());
	}
}

TEST(MochaTags, CompiledPatternMatches) {
	const auto &registry = TagRegistry::instance();
	const auto *tag = registry.get("xscale");
	ASSERT_NE(tag, nullptr);

	// compiled_pattern 应与 pattern 字符串生成的正则行为一致
	std::string test = R"({\fscx150})";
	std::regex manual_re(tag->pattern);
	bool compiled_result = std::regex_search(test, tag->compiled_pattern);
	bool manual_result = std::regex_search(test, manual_re);
	EXPECT_EQ(compiled_result, manual_result);
}

TEST(MochaTags, FindTagValue) {
	std::string block = R"({\pos(320,240)\fscx150})";
	auto val = tag_utils::find_tag_value(block, R"(\\fscx([\d.]+))");
	EXPECT_EQ(val, "150");
}

TEST(MochaTags, FindTagValueNotFound) {
	std::string block = R"({\pos(320,240)})";
	auto val = tag_utils::find_tag_value(block, R"(\\fscx([\d.]+))");
	EXPECT_TRUE(val.empty());
}

TEST(MochaTags, ReplaceTag) {
	std::string block = R"({\pos(320,240)\fscx150})";
	auto result = tag_utils::replace_tag(block, R"(\\fscx[\d.]+)", R"(\fscx200)");
	EXPECT_NE(result.find("\\fscx200"), std::string::npos);
	EXPECT_EQ(result.find("\\fscx150"), std::string::npos);
}

TEST(MochaTags, RemoveTag) {
	std::string block = R"({\pos(320,240)\fscx150})";
	auto result = tag_utils::remove_tag(block, R"(\\fscx[\d.]+)");
	EXPECT_EQ(result.find("fscx"), std::string::npos);
}

TEST(MochaTags, CountTag) {
	std::string text = R"({\fscx100}text{\fscx200})";
	int count = tag_utils::count_tag(text, R"(\\fscx[\d.]+)");
	EXPECT_EQ(count, 2);
}

TEST(MochaTags, CleanEmptyBlocks) {
	std::string text = R"({}text{})";
	auto result = tag_utils::clean_empty_blocks(text);
	EXPECT_EQ(result, "text");
}

TEST(MochaTags, CleanEmptyClips) {
	std::string text = R"({\clip()}text{\iclip()})";
	auto result = tag_utils::clean_empty_clips(text);
	EXPECT_EQ(result.find(R"(\clip())"), std::string::npos);
	EXPECT_EQ(result.find(R"(\iclip())"), std::string::npos);
}

TEST(MochaTags, CleanEmptyClipsThenBlocks) {
	std::string text = R"({\clip()}text{\iclip()})";
	auto result = tag_utils::clean_empty_blocks(tag_utils::clean_empty_clips(text));
	EXPECT_EQ(result, "text");
}

TEST(MochaTags, DeduplicateTag) {
	std::string text = R"({\fscx100\fscx200}text)";
	auto result = tag_utils::deduplicate_tag(text, R"(\\fscx[\d.]+)");
	int count = tag_utils::count_tag(result, R"(\\fscx[\d.]+)");
	EXPECT_EQ(count, 1);
	// 应保留最后一个值
	EXPECT_NE(result.find("\\fscx200"), std::string::npos);
}

TEST(MochaTags, RunCallbackOnOverrides) {
	std::string text = R"({first}text{second}more)";
	auto result = tag_utils::run_callback_on_overrides(
		text, [](const std::string &block, int idx) {
			return "[" + block + "]";
		}
	);
	EXPECT_NE(result.find("[{first}]"), std::string::npos);
	EXPECT_NE(result.find("[{second}]"), std::string::npos);
}

TEST(MochaTags, FormatColor) {
	// format_color 是 TagDef 的成员方法，输出包含标签前缀
	const auto *tag = TagRegistry::instance().get("color1");
	ASSERT_NE(tag, nullptr);
	ColorValue cv{0, 128, 255}; // BGR 顺序：B=0, G=128, R=255
	auto result = tag->format_color(cv);
	// 颜色格式应为 \1c&HBBGGRR& 形式
	EXPECT_FALSE(result.empty());
	EXPECT_NE(result.find("&H"), std::string::npos);
	EXPECT_EQ(result.back(), '&');
}

TEST(MochaTags, ConvertClipToFP) {
	std::string clip = R"(\clip(2,m 100 200 l 300 400))";
	auto result = tag_utils::convert_clip_to_fp(clip);
	EXPECT_EQ(result, R"(\clip(m 50 100 l 150 200))");
}

TEST(MochaTags, ConvertClipToFPPreservesInverseClipTag) {
	std::string clip = R"(\iclip(3,m 80 40 l 160 80))";
	auto result = tag_utils::convert_clip_to_fp(clip);
	EXPECT_EQ(result, R"(\iclip(m 20 10 l 40 20))");
}

TEST(MochaTags, RectClipToVectClipPreservesTagPrefix) {
	std::string clip = R"(\clip(100,200,300,400))";
	auto result = tag_utils::rect_clip_to_vect_clip(clip);
	EXPECT_EQ(result, R"(\clip(m 100 200 l 300 200 300 400 100 400))");
}

TEST(MochaTags, RectIClipToVectClipPreservesTagPrefix) {
	std::string clip = R"(\iclip(10,20,30,40))";
	auto result = tag_utils::rect_clip_to_vect_clip(clip);
	EXPECT_EQ(result, R"(\iclip(m 10 20 l 30 20 30 40 10 40))");
}

TEST(MochaTags, RectClipToVectClipNonRectInput) {
	// 矢量 clip 输入不匹配矩形格式，应原样返回
	std::string clip = R"(\clip(m 100 200 l 300 400))";
	auto result = tag_utils::rect_clip_to_vect_clip(clip);
	EXPECT_EQ(result, clip);
}

TEST(MochaTags, ConvertClipToFPRectClipPassthrough) {
	// 矩形 clip 应原样返回而非转换
	std::string clip = R"(\clip(10,20,30,40))";
	auto result = tag_utils::convert_clip_to_fp(clip);
	EXPECT_EQ(result, clip);
}

TEST(MochaTags, ConvertClipToFPNoScalePassthrough) {
	// 无缩放因子的矢量 clip 应原样返回
	std::string clip = R"(\clip(m 100 200 l 300 400))";
	auto result = tag_utils::convert_clip_to_fp(clip);
	EXPECT_EQ(result, clip);
}

TEST(MochaTags, ExtractFadeFullTag) {
	std::optional<FadeData> fade_data;
	std::optional<FullFadeData> full_fade_data;
	auto stripped = tag_utils::extract_fade(
		R"({\fade(255,0,255,0,200,800,1000)\alpha&H00&})",
		fade_data,
		full_fade_data
	);

	EXPECT_FALSE(fade_data.has_value());
	ASSERT_TRUE(full_fade_data.has_value());
	EXPECT_EQ(full_fade_data->a1, 255);
	EXPECT_EQ(full_fade_data->a2, 0);
	EXPECT_EQ(full_fade_data->a3, 255);
	EXPECT_EQ(full_fade_data->t1, 0);
	EXPECT_EQ(full_fade_data->t2, 200);
	EXPECT_EQ(full_fade_data->t3, 800);
	EXPECT_EQ(full_fade_data->t4, 1000);
	EXPECT_EQ(stripped, R"({\alpha&H00&})");
}

TEST(MochaTags, ExtractFadeShortTag) {
	std::optional<FadeData> fade_data;
	std::optional<FullFadeData> full_fade_data;
	auto stripped = tag_utils::extract_fade(
		R"({\fad(200,300)\alpha&H00&})",
		fade_data,
		full_fade_data
	);

	ASSERT_TRUE(fade_data.has_value());
	EXPECT_EQ(fade_data->fade_in, 200);
	EXPECT_EQ(fade_data->fade_out, 300);
	EXPECT_FALSE(full_fade_data.has_value());
	EXPECT_EQ(stripped, R"({\alpha&H00&})");
}

// ============================================================================
// Transform 变换标签测试
// ============================================================================

class MochaTransform : public libagi {};

TEST(MochaTransform, FromStringBasic) {
	auto t = Transform::from_string("(0,1000,\\fscx200)", 5000, 0);
	EXPECT_EQ(t.start_time, 0);
	EXPECT_EQ(t.end_time, 1000);
	EXPECT_DOUBLE_EQ(t.accel, 1.0);
	EXPECT_EQ(t.effect, "\\fscx200");
}

TEST(MochaTransform, FromStringWithAccel) {
	auto t = Transform::from_string("(0,1000,2.5,\\fscx200)", 5000, 0);
	EXPECT_EQ(t.start_time, 0);
	EXPECT_EQ(t.end_time, 1000);
	EXPECT_DOUBLE_EQ(t.accel, 2.5);
	EXPECT_EQ(t.effect, "\\fscx200");
}

TEST(MochaTransform, FromStringNoTiming) {
	auto t = Transform::from_string("(\\fscx200)", 5000, 0);
	EXPECT_EQ(t.start_time, 0);
	EXPECT_EQ(t.end_time, 5000);
	EXPECT_EQ(t.effect, "\\fscx200");
}

TEST(MochaTransform, FromStringAccelOnly) {
	auto t = Transform::from_string("(1.5,\\fscx200)", 5000, 0);
	EXPECT_DOUBLE_EQ(t.accel, 1.5);
	EXPECT_EQ(t.effect, "\\fscx200");
}

TEST(MochaTransform, ToStringBasic) {
	Transform t;
	t.start_time = 0;
	t.end_time = 1000;
	t.accel = 1.0;
	t.effect = "\\fscx200";
	auto result = t.to_string(5000);
	EXPECT_EQ(result, "\\t(0,1000,\\fscx200)");
}

TEST(MochaTransform, ToStringWithAccel) {
	Transform t;
	t.start_time = 0;
	t.end_time = 1000;
	t.accel = 2.0;
	t.effect = "\\fscx200";
	auto result = t.to_string(5000);
	EXPECT_EQ(result, "\\t(0,1000,2,\\fscx200)");
}

TEST(MochaTransform, ToStringSuppressOutOfRange) {
	Transform t;
	t.start_time = 6000;
	t.end_time = 7000;
	t.accel = 1.0;
	t.effect = "\\fscx200";
	// start_time > line_duration 时应返回空字符串
	auto result = t.to_string(5000);
	EXPECT_TRUE(result.empty());
}

TEST(MochaTransform, GatherTagsInEffect) {
	auto t = Transform::from_string("(0,1000,\\fscx200\\fscy150)", 5000, 0);
	// 应识别到 xscale 和 yscale 标签
	EXPECT_FALSE(t.effect_tags.empty());
}

TEST(MochaTransform, InterpolateTextBasic) {
	// interpolate 是文本级别的替换：在 text 中查找 placeholder 并替换为插值结果
	auto t = Transform::from_string("(0,1000,\\fscx200)", 1000, 0);
	t.gather_tags_in_effect();

	std::map<std::string, double> line_props;
	line_props["xscale"] = 100.0;

	std::string text = "\\fscx100";
	auto result = t.interpolate(text, t.token, 500, line_props);
	// 插值后文本不应为空
	EXPECT_FALSE(result.empty());
}

TEST(MochaTransform, InterpolateAtStartTime) {
	auto t = Transform::from_string("(0,1000,\\fscx200)", 1000, 0);
	t.gather_tags_in_effect();

	std::map<std::string, double> line_props;
	line_props["xscale"] = 100.0;

	std::string text = "\\fscx100";
	auto result = t.interpolate(text, t.token, 0, line_props);
	EXPECT_FALSE(result.empty());
}

TEST(MochaTransform, InterpolateAtEndTime) {
	auto t = Transform::from_string("(0,1000,\\fscx200)", 1000, 0);
	t.gather_tags_in_effect();

	std::map<std::string, double> line_props;
	line_props["xscale"] = 100.0;

	std::string text = "\\fscx100";
	auto result = t.interpolate(text, t.token, 1000, line_props);
	EXPECT_FALSE(result.empty());
}

// 颜色插值四舍五入测试：验证 interpolate_color 不会因截断丢失精度
TEST(MochaTransform, InterpolateColorRounding) {
	// B=0→255, G=0→255, R=0→255, progress=0.999
	// 0 + (255-0)*0.999 = 254.745 → 截断为 254，四舍五入为 255
	ColorValue before{0, 0, 0};
	ColorValue after{255, 255, 255};
	auto result = interpolate_color(before, after, 0.999);
	EXPECT_EQ(result.b, 255);
	EXPECT_EQ(result.g, 255);
	EXPECT_EQ(result.r, 255);
}

TEST(MochaTransform, InterpolateColorMidpoint) {
	// B=0→255, progress=0.5 → 127.5 → 四舍五入为 128（非截断的 127）
	ColorValue before{0, 0, 0};
	ColorValue after{255, 255, 255};
	auto result = interpolate_color(before, after, 0.5);
	EXPECT_EQ(result.b, 128);
	EXPECT_EQ(result.g, 128);
	EXPECT_EQ(result.r, 128);
}

TEST(MochaTransform, InterpolateColorNoChange) {
	// 起止相同，任何 progress 都应返回相同值
	ColorValue c{100, 150, 200};
	auto result = interpolate_color(c, c, 0.5);
	EXPECT_EQ(result.b, 100);
	EXPECT_EQ(result.g, 150);
	EXPECT_EQ(result.r, 200);
}

TEST(MochaTransform, InterpolateColorBoundary) {
	// progress=0 应返回 before，progress=1 应返回 after
	ColorValue before{10, 20, 30};
	ColorValue after{200, 210, 220};
	auto r0 = interpolate_color(before, after, 0.0);
	EXPECT_EQ(r0.b, 10);
	EXPECT_EQ(r0.g, 20);
	EXPECT_EQ(r0.r, 30);
	auto r1 = interpolate_color(before, after, 1.0);
	EXPECT_EQ(r1.b, 200);
	EXPECT_EQ(r1.g, 210);
	EXPECT_EQ(r1.r, 220);
}

// 数值插值测试：验证 interpolate_number 线性插值正确性
TEST(MochaTransform, InterpolateNumberBasic) {
	EXPECT_DOUBLE_EQ(interpolate_number(0.0, 100.0, 0.0), 0.0);
	EXPECT_DOUBLE_EQ(interpolate_number(0.0, 100.0, 1.0), 100.0);
	EXPECT_DOUBLE_EQ(interpolate_number(0.0, 100.0, 0.5), 50.0);
	EXPECT_DOUBLE_EQ(interpolate_number(10.0, 20.0, 0.3), 13.0);
	// 反向插值
	EXPECT_DOUBLE_EQ(interpolate_number(100.0, 0.0, 0.25), 75.0);
	// 相同值
	EXPECT_DOUBLE_EQ(interpolate_number(42.0, 42.0, 0.7), 42.0);
}

TEST(MochaTransform, InterpolateNumberNearBoundary) {
	// progress 接近 1 时精度验证
	double result = interpolate_number(0.0, 255.0, 0.999);
	EXPECT_NEAR(result, 254.745, 1e-9);
}

// ============================================================================
// MotionLine 行处理测试
// ============================================================================

class MochaLine : public libagi {};

TEST(MochaLine, BasicConstruction) {
	MotionLine line;
	line.text = R"({\pos(320,240)}hello)";
	EXPECT_EQ(line.text, R"({\pos(320,240)}hello)");
	EXPECT_FALSE(line.has_org);
	EXPECT_FALSE(line.has_clip);
}

TEST(MochaLine, TokenizeTransforms) {
	MotionLine line;
	line.text = R"({\t(0,1000,\fscx200)}hello)";
	line.tokenize_transforms();
	EXPECT_TRUE(line.transforms_tokenized);
	// \t 标签应被替换为占位符
	EXPECT_EQ(line.text.find("\\t("), std::string::npos);
}

TEST(MochaLine, DeduplicateTags) {
	MotionLine line;
	line.text = R"({\fscx100\fscx200}hello)";
	line.deduplicate_tags();
	// 去重后应只保留一个 \fscx
	int count = tag_utils::count_tag(line.text, R"(\\fscx[\d.]+)");
	EXPECT_EQ(count, 1);
}

TEST(MochaLine, DeduplicateOneTimeTagsKeepsFirstInstance) {
	MotionLine line;
	line.text = R"({\pos(10,20)\pos(30,40)\move(1,2,3,4,0,100)}hello)";
	line.deduplicate_tags();

	EXPECT_NE(line.text.find(R"(\pos(10,20))"), std::string::npos);
	EXPECT_EQ(line.text.find(R"(\pos(30,40))"), std::string::npos);
	EXPECT_EQ(line.text.find(R"(\move(1,2,3,4,0,100))"), std::string::npos);
}

TEST(MochaLine, ExtractMetricsWithPos) {
	MotionLine line;
	line.text = R"({\pos(320,240)}hello)";
	bool has_pos = line.extract_metrics(2, 10, 10, 10, 1920, 1080);
	EXPECT_TRUE(has_pos);
	EXPECT_NEAR(line.x_position, 320, 0.01);
	EXPECT_NEAR(line.y_position, 240, 0.01);
}

TEST(MochaLine, ExtractMetricsNoPos) {
	MotionLine line;
	line.text = R"({\fscx100}hello)";
	bool has_pos = line.extract_metrics(2, 10, 10, 10, 1920, 1080);
	EXPECT_FALSE(has_pos);
}

TEST(MochaLine, EnsureLeadingOverrideExists) {
	MotionLine line;
	line.text = "hello";
	line.ensure_leading_override_exists();
	EXPECT_EQ(line.text.front(), '{');
}

TEST(MochaLine, RunCallbackOnFirstOverride) {
	MotionLine line;
	line.text = R"({\fscx100}hello)";
	line.run_callback_on_first_override(
		[](const std::string &block) {
			return "{\\fscx200}";
		}
	);
	EXPECT_NE(line.text.find("\\fscx200"), std::string::npos);
}

TEST(MochaLine, RunCallbackOnOverrides) {
	MotionLine line;
	line.text = R"({first}text{second}more)";
	line.run_callback_on_overrides(
		[](const std::string &block, int) {
			return "{replaced}";
		}
	);
	// 所有 override 块都应被替换
	EXPECT_EQ(line.text.find("{first}"), std::string::npos);
	EXPECT_EQ(line.text.find("{second}"), std::string::npos);
}

TEST(MochaLine, ShiftKaraoke) {
	MotionLine line;
	line.text = R"({\k50}hello{\k30}world)";
	line.karaoke_shift = 2.0; // 20ms
	line.shift_karaoke();
	// 卡拉 OK 标签时间应被调整
	EXPECT_NE(line.text.find("\\k"), std::string::npos);
}

TEST(MochaLine, ShiftTypingKaraokeAcrossOverrideBlocks) {
	MotionLine line;
	line.text = R"({\bord1\kk(4,3,4,5,6)}AB{\i1}CD)";
	line.karaoke_shift = 5.0;
	line.shift_karaoke();
	line.text = tag_utils::clean_empty_blocks(line.text);

	EXPECT_EQ(line.text, R"({\bord1}A{\kk(3,2,5,6)}B{\i1}CD)");
}

TEST(MochaLine, ShiftTypingKaraokeCountsHardSpaceButSkipsLineBreak) {
	MotionLine line;
	line.text = R"({\kk(3,2,2,2)}A\h\N B)";
	line.karaoke_shift = 4.0;
	line.shift_karaoke();
	line.text = tag_utils::clean_empty_blocks(line.text);

	EXPECT_EQ(line.text, R"(A\h\N{\kk(1,2)} B)");
}

TEST(MochaLine, ShiftKaraokeHonorsKtAnchor) {
	MotionLine line;
	line.text = R"({\kt5\k10}AB)";
	line.karaoke_shift = 7.0;
	line.shift_karaoke();
	line.text = tag_utils::clean_empty_blocks(line.text);

	EXPECT_EQ(line.text, R"({\k8}AB)");
}

// ============================================================================
// DataHandler 数据解析器测试
// ============================================================================

class MochaDataHandler : public libagi {};

// AE 关键帧有效测试数据
static const std::string AE_VALID_DATA =
	"Adobe After Effects 6.0 Keyframe Data\r\n"
	"\r\n"
	"\tUnits Per Second\t24\r\n"
	"\tSource Width\t1920\r\n"
	"\tSource Height\t1080\r\n"
	"\r\n"
	"Anchor Point\r\n"
	"\tFrame\tX pixels\tY pixels\tZ pixels\r\n"
	"\t0\t960\t540\t0\r\n"
	"\t1\t965\t542\t0\r\n"
	"\t2\t970\t545\t0\r\n"
	"\r\n"
	"Position\r\n"
	"\tFrame\tX pixels\tY pixels\tZ pixels\r\n"
	"\t0\t960\t540\t0\r\n"
	"\t1\t965\t542\t0\r\n"
	"\t2\t970\t545\t0\r\n"
	"\r\n"
	"Scale\r\n"
	"\tFrame\tX percent\tY percent\tZ percent\r\n"
	"\t0\t100\t100\t100\r\n"
	"\t1\t101\t101\t100\r\n"
	"\t2\t102\t102\t100\r\n"
	"\r\n"
	"Rotation\r\n"
	"\tFrame\tDegrees\r\n"
	"\t0\t0\r\n"
	"\t1\t1\r\n"
	"\t2\t2\r\n"
	"\r\n"
	"End of Keyframe Data\r\n";

TEST(MochaDataHandler, ParseValidAEData) {
	DataHandler dh;
	bool ok = dh.parse(AE_VALID_DATA, 1920, 1080);
	EXPECT_TRUE(ok);
	EXPECT_EQ(dh.length(), 3);
	EXPECT_FALSE(dh.is_srs());
}

TEST(MochaDataHandler, ParseDataArraySizes) {
	DataHandler dh;
	ASSERT_TRUE(dh.parse(AE_VALID_DATA, 1920, 1080));
	EXPECT_EQ(static_cast<int>(dh.x_position.size()), dh.length());
	EXPECT_EQ(static_cast<int>(dh.y_position.size()), dh.length());
	EXPECT_EQ(static_cast<int>(dh.x_scale.size()), dh.length());
	EXPECT_EQ(static_cast<int>(dh.z_rotation.size()), dh.length());
}

TEST(MochaDataHandler, ParseInvalidHeader) {
	DataHandler dh;
	bool ok = dh.parse("Not AE data\nfoo\nbar\nbaz\n", 1920, 1080);
	EXPECT_FALSE(ok);
}

TEST(MochaDataHandler, ParseMalformedHeaderField_H4) {
	// H4 修复验证：头部字段值非法时应返回 false 而非崩溃
	std::string malformed =
		"Adobe After Effects 6.0 Keyframe Data\r\n"
		"\r\n"
		"\tUnits Per Second\tNOT_A_NUMBER\r\n"
		"\tSource Width\t1920\r\n"
		"\tSource Height\t1080\r\n"
		"\r\n"
		"End of Keyframe Data\r\n";
	DataHandler dh;
	EXPECT_NO_THROW(
		{
		bool ok = dh.parse(malformed, 1920, 1080);
		EXPECT_FALSE(ok);
		}
	);
}

TEST(MochaDataHandler, ParseMalformedDimension_H4) {
	// H4 修复验证：Source Width/Height 值非法
	std::string malformed =
		"Adobe After Effects 6.0 Keyframe Data\r\n"
		"\r\n"
		"\tUnits Per Second\t24\r\n"
		"\tSource Width\tABC\r\n"
		"\tSource Height\t1080\r\n"
		"\r\n"
		"End of Keyframe Data\r\n";
	DataHandler dh;
	EXPECT_NO_THROW(
		{
		bool ok = dh.parse(malformed, 1920, 1080);
		EXPECT_FALSE(ok);
		}
	);
}

TEST(MochaDataHandler, ParseEmptyData) {
	DataHandler dh;
	bool ok = dh.parse("", 1920, 1080);
	EXPECT_FALSE(ok);
}

TEST(MochaDataHandler, CalculateReferenceFrame) {
	DataHandler dh;
	ASSERT_TRUE(dh.parse(AE_VALID_DATA, 1920, 1080));
	// 参考帧默认为第 1 帧
	dh.add_reference_frame(1);
	// 验证起始位置被设置
	EXPECT_NE(dh.x_start_position, 0);
}

TEST(MochaDataHandler, CalculateCurrentState) {
	DataHandler dh;
	ASSERT_TRUE(dh.parse(AE_VALID_DATA, 1920, 1080));
	dh.add_reference_frame(1);
	dh.calculate_current_state(2);
	// 当前位置应被更新
	EXPECT_NE(dh.x_current_position, 0);
}

TEST(MochaDataHandler, BestEffortParse) {
	DataHandler dh;
	bool ok = dh.best_effort_parse(AE_VALID_DATA, 1920, 1080);
	EXPECT_TRUE(ok);
}

TEST(MochaDataHandler, BestEffortParseInvalid) {
	DataHandler dh;
	bool ok = dh.best_effort_parse("random text that is neither AE nor SRS", 1920, 1080);
	EXPECT_FALSE(ok);
}

// SRS 测试数据
static const std::string SRS_VALID_DATA =
	"shake_shape_data 4.0\n"
	"num_shapes 1\n"
	"vertex_data 100 200 100 200 100 200 0 0 0 0 0 0 150 250 150 250 150 250 0 0 0 0 0 0 200 300 200 300 200 300 0 0 0 0 0 0\n"
	"vertex_data 105 205 105 205 105 205 0 0 0 0 0 0 155 255 155 255 155 255 0 0 0 0 0 0 205 305 205 305 205 305 0 0 0 0 0 0\n";

TEST(MochaDataHandler, ParseValidSRS) {
	DataHandler dh;
	bool ok = dh.parse_srs(SRS_VALID_DATA, 1080);
	EXPECT_TRUE(ok);
	EXPECT_TRUE(dh.is_srs());
	EXPECT_EQ(dh.length(), 2);
}

TEST(MochaDataHandler, SRSDrawings) {
	DataHandler dh;
	ASSERT_TRUE(dh.parse_srs(SRS_VALID_DATA, 1080));
	std::string drawing = dh.get_srs_drawing(1);
	EXPECT_FALSE(drawing.empty());
	// 绘图命令应包含 "m"（移动命令）
	EXPECT_NE(drawing.find("m "), std::string::npos);
}

TEST(MochaDataHandler, SRSInvalidHeader) {
	DataHandler dh;
	bool ok = dh.parse_srs("not_srs_data\n", 1080);
	EXPECT_FALSE(ok);
}

TEST(MochaDataHandler, SRSOutOfRangeFrame) {
	DataHandler dh;
	ASSERT_TRUE(dh.parse_srs(SRS_VALID_DATA, 1080));
	std::string drawing = dh.get_srs_drawing(999);
	EXPECT_TRUE(drawing.empty());
}

// ============================================================================
// MotionHandler 回调与运动计算测试
// ============================================================================

class MochaHandler : public libagi {};

/// @brief 辅助函数：创建含有效数据的 DataHandler
static DataHandler make_test_data_handler() {
	DataHandler dh;
	dh.parse(AE_VALID_DATA, 1920, 1080);
	dh.add_reference_frame(1);
	return dh;
}

static int extract_alpha_value(const std::string &text) {
	static const std::regex alpha_re(R"(\\(alpha|[1234]a)&H([0-9A-Fa-f]{2})&)");
	int alpha = -1;
	auto begin = std::sregex_iterator(text.begin(), text.end(), alpha_re);
	auto end = std::sregex_iterator();
	for (auto it = begin; it != end; ++it) {
		alpha = std::stoi((*it)[2].str(), nullptr, 16);
	}
	return alpha;
}

TEST(MochaHandler, PositionMath) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.x_position = true;
	opts.y_position = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto [nx, ny] = handler.position_math(960, 540, &dh);
	// 结果应为有效坐标
	EXPECT_FALSE(std::isnan(nx));
	EXPECT_FALSE(std::isnan(ny));
}

TEST(MochaHandler, CbPositionValid) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.x_position = true;
	opts.y_position = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_position("960,540", 2);
	// 应返回 "(x,y)" 格式
	EXPECT_EQ(result.front(), '(');
	EXPECT_EQ(result.back(), ')');
	EXPECT_NE(result.find(","), std::string::npos);
}

TEST(MochaHandler, CbPositionInvalidInput_H3) {
	// H3 修复验证：畸形数字输入不崩溃
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.x_position = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	EXPECT_NO_THROW(
		{
		auto result = handler.cb_position("abc,def", 2);
		// 无法匹配坐标正则，应返回原值
		EXPECT_EQ(result, "(abc,def)");
		}
	);
}

TEST(MochaHandler, CbAbsolutePositionValid) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.x_position = true;
	opts.y_position = true;
	opts.abs_pos = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_absolute_position("960,540", 2);
	EXPECT_EQ(result.front(), '(');
	EXPECT_EQ(result.back(), ')');
}

TEST(MochaHandler, CbAbsolutePositionEmptyData_H1) {
	// H1 修复验证：空数据时返回原值而非崩溃
	DataHandler empty_dh;
	// 不解析任何数据，x_position 为空

	MotionOptions opts;
	opts.x_position = true;
	opts.abs_pos = true;
	MotionHandler handler(opts, &empty_dh, nullptr, nullptr);

	EXPECT_NO_THROW(
		{
		auto result = handler.cb_absolute_position("960,540", 1);
		EXPECT_EQ(result, "(960,540)");
		}
	);
}

TEST(MochaHandler, CbOriginValid) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.origin = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_origin("960,540", 2);
	EXPECT_EQ(result.front(), '(');
	EXPECT_EQ(result.back(), ')');
}

TEST(MochaHandler, CbOriginInvalidInput_H3) {
	// H3 修复验证
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.origin = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	EXPECT_NO_THROW(
		{
		auto result = handler.cb_origin("not,numbers", 2);
		EXPECT_EQ(result, "(not,numbers)");
		}
	);
}

TEST(MochaHandler, CbScaleBasic) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.x_scale = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_scale("100", 2);
	// 缩放值应为有效数字
	EXPECT_NO_THROW({ static_cast<void>(std::stod(result)); });
}

TEST(MochaHandler, CbScaleInvalid) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.x_scale = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_scale("abc", 2);
	EXPECT_EQ(result, "abc");
}

TEST(MochaHandler, CbBlurBasic) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.blur = true;
	opts.blur_scale = 1.0;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_blur("5.0", 2);
	EXPECT_NO_THROW({ static_cast<void>(std::stod(result)); });
}

TEST(MochaHandler, CbBlurInvalid) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.blur = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_blur("xyz", 2);
	EXPECT_EQ(result, "xyz");
}

TEST(MochaHandler, CbRotateX) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.x_rotation = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_rotate_x("0", 2);
	EXPECT_NO_THROW({ static_cast<void>(std::stod(result)); });
}

TEST(MochaHandler, CbRotateY) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.y_rotation = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_rotate_y("0", 2);
	EXPECT_NO_THROW({ static_cast<void>(std::stod(result)); });
}

TEST(MochaHandler, CbRotateZ) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.z_rotation = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_rotate_z("0", 2);
	EXPECT_NO_THROW({ static_cast<void>(std::stod(result)); });
}

TEST(MochaHandler, CbRotateInvalid) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.z_rotation = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	EXPECT_EQ(handler.cb_rotate_x("abc", 2), "abc");
	EXPECT_EQ(handler.cb_rotate_y("abc", 2), "abc");
	EXPECT_EQ(handler.cb_rotate_z("abc", 2), "abc");
}

TEST(MochaHandler, CbZPosition) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.z_position = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_z_position("0", 2);
	EXPECT_NO_THROW({ static_cast<void>(std::stod(result)); });
}

TEST(MochaHandler, CbZPositionInvalid) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.z_position = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	EXPECT_EQ(handler.cb_z_position("xyz", 2), "xyz");
}

TEST(MochaHandler, CbRectClipValid) {
	auto main_dh = make_test_data_handler();
	auto clip_dh = make_test_data_handler();
	clip_dh.calculate_current_state(2);

	MotionOptions opts;
	opts.rect_clip = true;
	MotionHandler handler(opts, &main_dh, &clip_dh, nullptr);

	auto result = handler.cb_rect_clip("100,200,300,400", 2);
	EXPECT_EQ(result.front(), '(');
	EXPECT_EQ(result.back(), ')');
}

TEST(MochaHandler, CbRectClipInvalidCoords_H3) {
	// H3 修复验证：畸形坐标不崩溃
	auto main_dh = make_test_data_handler();
	auto clip_dh = make_test_data_handler();
	clip_dh.calculate_current_state(2);

	MotionOptions opts;
	opts.rect_clip = true;
	MotionHandler handler(opts, &main_dh, &clip_dh, nullptr);

	EXPECT_NO_THROW(
		{
		auto result = handler.cb_rect_clip("abc,def,ghi,jkl", 2);
		}
	);
}

TEST(MochaHandler, CbVectClipValid) {
	auto main_dh = make_test_data_handler();
	auto clip_dh = make_test_data_handler();
	clip_dh.calculate_current_state(2);

	MotionOptions opts;
	opts.vect_clip = true;
	MotionHandler handler(opts, &main_dh, nullptr, &clip_dh);

	auto result = handler.cb_vect_clip("m 100 200 l 300 400 500 600", 2);
	EXPECT_EQ(result.front(), '(');
	EXPECT_EQ(result.back(), ')');
}

TEST(MochaHandler, CbVectClipNoData) {
	auto main_dh = make_test_data_handler();

	MotionOptions opts;
	MotionHandler handler(opts, &main_dh, nullptr, nullptr);

	auto result = handler.cb_vect_clip("m 100 200", 2);
	EXPECT_EQ(result, "(m 100 200)");
}

TEST(MochaHandler, CbVectClipSRS) {
	auto main_dh = make_test_data_handler();
	DataHandler srs_dh;
	ASSERT_TRUE(srs_dh.parse_srs(SRS_VALID_DATA, 1080));

	MotionOptions opts;
	opts.vect_clip = true;
	MotionHandler handler(opts, &main_dh, nullptr, &srs_dh);

	auto result = handler.cb_vect_clip_srs("m 100 200", 1);
	EXPECT_EQ(result.front(), '(');
	EXPECT_EQ(result.back(), ')');
}

TEST(MochaHandler, ApplyCallbacksVectorClipSRSEmptyPlaceholder) {
	auto main_dh = make_test_data_handler();
	DataHandler srs_dh;
	ASSERT_TRUE(srs_dh.parse_srs(SRS_VALID_DATA, 1080));

	MotionOptions opts;
	opts.vect_clip = true;
	MotionHandler handler(opts, &main_dh, nullptr, &srs_dh);

	auto result = handler.apply_callbacks(R"({\clip()})", 1);
	EXPECT_NE(result, R"({\clip()})");
	EXPECT_EQ(result.find("\\clip()"), std::string::npos);
	EXPECT_NE(result.find("\\clip("), std::string::npos);
	EXPECT_NE(result.find("m "), std::string::npos);
}

TEST(MochaHandler, ApplyCallbacksDoesNotCrash) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.x_position = true;
	opts.y_position = true;
	opts.x_scale = true;
	opts.z_rotation = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	std::string text = R"({\pos(960,540)\fscx100\fscy100\frz0\clip()}hello)";
	EXPECT_NO_THROW(
		{
		auto result = handler.apply_callbacks(text, 2);
		EXPECT_FALSE(result.empty());
		}
	);
}

TEST(MochaHandler, SetupCallbacksClipOnly) {
	auto dh = make_test_data_handler();
	DataHandler clip_dh;
	clip_dh.parse(AE_VALID_DATA, 1920, 1080);

	MotionOptions opts;
	opts.clip_only = true;
	opts.rect_clip = true;
	MotionHandler handler(opts, &dh, &clip_dh, nullptr);

	// clip_only 模式下不应有位置/缩放/旋转回调
	std::string text = R"({\pos(960,540)\fscx100}hello)";
	dh.calculate_current_state(1);
	auto result = handler.apply_callbacks(text, 1);
	// \pos 和 \fscx 应保持不变（无位置/缩放回调）
	EXPECT_NE(result.find("\\pos(960,540)"), std::string::npos);
	EXPECT_NE(result.find("\\fscx100"), std::string::npos);
}

// ============================================================================
// interpolate_transforms_copy 回归测试
// ============================================================================

TEST(MochaTransform, InterpolateTransformsCopyNoShift) {
	// 回归点：time_shift 不应改变 Transform 自身的时间窗口。
	auto t = Transform::from_string("(1000,2000,\\fscx200)", 3000, 0);
	t.gather_tags_in_effect();
	t.token = "__T0__";

	std::vector<Transform> transforms = {t};
	std::map<std::string, double> line_props;
	line_props["xscale"] = 100.0;

	std::map<std::string, Transform::EffectTagValue> prior_tags;
	Transform::EffectTagValue etv;
	etv.type = Transform::EffectTagValue::NUM;
	etv.number = 100.0;
	prior_tags["xscale"] = etv;

	std::string text = t.token + "\\pos(0,0)";
	auto result = transform_utils::interpolate_transforms_copy(
		text, transforms, 500, line_props, prior_tags, 0, 0
	);

	EXPECT_NE(result.find("\\fscx100"), std::string::npos)
		<< "Expected start value before transform window, got: " << result;
}

TEST(MochaTransform, InterpolateTransformsCopyAtEnd) {
	auto t = Transform::from_string("(100,500,\\fscx200)", 1000, 0);
	t.gather_tags_in_effect();
	t.token = "__T1__";

	std::vector<Transform> transforms = {t};
	std::map<std::string, double> line_props;
	line_props["xscale"] = 100.0;

	std::map<std::string, Transform::EffectTagValue> prior_tags;
	Transform::EffectTagValue etv;
	etv.type = Transform::EffectTagValue::NUM;
	etv.number = 100.0;
	prior_tags["xscale"] = etv;

	std::string text = t.token + "\\pos(0,0)";
	auto result = transform_utils::interpolate_transforms_copy(
		text, transforms, 800, line_props, prior_tags, 0, 0
	);

	EXPECT_NE(result.find("\\fscx200"), std::string::npos)
		<< "Expected end value after transform window, got: " << result;
}

TEST(MochaTransform, InterpolateTransformsCopyMidFrame) {
	// 中间帧插值：progress=0.5 时应得到起止值的中间值
	auto t = Transform::from_string("(0,1000,\\fscx200)", 2000, 0);
	t.gather_tags_in_effect();
	t.token = "__T2__";

	std::vector<Transform> transforms = {t};
	std::map<std::string, double> line_props;
	line_props["xscale"] = 100.0;

	std::map<std::string, Transform::EffectTagValue> prior_tags;
	Transform::EffectTagValue etv;
	etv.type = Transform::EffectTagValue::NUM;
	etv.number = 100.0;
	prior_tags["xscale"] = etv;

	std::string text = t.token + "\\pos(0,0)";
	auto result = transform_utils::interpolate_transforms_copy(
		text, transforms, 500, line_props, prior_tags, 0, 0
	);

	// progress=0.5，从 100 到 200 的中间值应为 150
	EXPECT_NE(result.find("\\fscx150"), std::string::npos)
		<< "Expected mid-point value at progress=0.5, got: " << result;
}

TEST(MochaHandler, ApplyMotionClipTransformUsesScriptResolutionDefault) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr, 640, 360);

	MotionLine line;
	line.text = R"({\t(0,1000,\clip(10,20,110,120))}hello)";
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;
	line.tokenize_transforms();

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		0,
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	ASSERT_EQ(result.size(), 1u);
	EXPECT_NE(result[0].text.find(R"(\clip(0,0,640,360))"), std::string::npos)
		<< "Expected script-resolution rect clip default, got: " << result[0].text;
}

TEST(MochaHandler, ApplyMotionClipTransformWithoutKillTransRebasesCurrentRectClip) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = false;
	opts.rect_clip = true;
	MotionHandler handler(opts, &dh, &dh, nullptr, 1920, 1080);

	MotionLine line;
	line.text = R"({\clip(10,20,110,120)\t(0,2000,\clip(20,40,120,140))}hello)";
	line.start_time = 0;
	line.end_time = 3000;
	line.duration = 3000;
	line.tokenize_transforms();
	line.has_clip = true;

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		0,
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	std::sort(
		result.begin(), result.end(), [](const MotionLine &a, const MotionLine &b) {
			return a.start_time < b.start_time;
		}
	);

	auto mid_it = std::find_if(
		result.begin(), result.end(), [](const MotionLine &item) {
			return item.start_time == 1000;
		}
	);
	ASSERT_NE(mid_it, result.end());

	const std::string tracked_initial = "\\clip" + handler.cb_rect_clip("10,20,110,120", 2);
	const std::string tracked_mid = "\\clip" + handler.cb_rect_clip("15,30,115,130", 2);

	EXPECT_NE(mid_it->text.find(tracked_mid), std::string::npos)
		<< "Expected current clip state rebased at line start, got: " << mid_it->text;
	EXPECT_EQ(mid_it->text.find(tracked_initial), std::string::npos)
		<< "Clip base state should not reset to the original pre-transform value: " << mid_it->text;
	EXPECT_NE(mid_it->text.find("\\t(0,1000,\\clip("), std::string::npos)
		<< "Expected active clip transform to restart from the sub-line origin: " << mid_it->text;
	EXPECT_EQ(mid_it->text.find("\\t(-1000,1000,\\clip("), std::string::npos)
		<< "Negative-start clip transform should be clamped after rebasing: " << mid_it->text;
}

TEST(MochaHandler, ApplyMotionInverseClipTransformWithoutKillTransRebasesCurrentRectClip) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = false;
	opts.rect_clip = true;
	MotionHandler handler(opts, &dh, &dh, nullptr, 1920, 1080);

	MotionLine line;
	line.text = R"({\iclip(10,20,110,120)\t(0,2000,\iclip(20,40,120,140))}hello)";
	line.start_time = 0;
	line.end_time = 3000;
	line.duration = 3000;
	line.tokenize_transforms();
	line.has_clip = true;

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		0,
		[](int ms) { return ms / 1000; },
		[](int frame) { return frame * 1000; }
	);

	std::sort(
		result.begin(), result.end(), [](const MotionLine &a, const MotionLine &b) {
			return a.start_time < b.start_time;
		}
	);

	auto mid_it = std::find_if(
		result.begin(), result.end(), [](const MotionLine &item) {
			return item.start_time == 1000;
		}
	);
	ASSERT_NE(mid_it, result.end());

	const std::string tracked_initial = "\\iclip" + handler.cb_rect_clip("10,20,110,120", 2);
	const std::string tracked_mid = "\\iclip" + handler.cb_rect_clip("15,30,115,130", 2);

	EXPECT_NE(mid_it->text.find(tracked_mid), std::string::npos)
		<< "Expected current inverse clip state rebased at line start, got: " << mid_it->text;
	EXPECT_EQ(mid_it->text.find(tracked_initial), std::string::npos)
		<< "Inverse clip base state should not reset to the original pre-transform value: " << mid_it->text;
	EXPECT_NE(mid_it->text.find("\\t(0,1000,\\iclip("), std::string::npos)
		<< "Expected active inverse clip transform to restart from the sub-line origin: " << mid_it->text;
	EXPECT_EQ(mid_it->text.find("\\t(-1000,1000,\\iclip("), std::string::npos)
		<< "Negative-start inverse clip transform should be clamped after rebasing: " << mid_it->text;
}

TEST(MochaHandler, ApplyMotionFadeKillTransFirstVisibleFrameShouldNotBeFullyTransparent) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	MotionLine line;
	line.text = R"({\fade(255,0,255,0,100,200,300)\alpha&H00&}hello)";
	line.start_time = 150;
	line.end_time = 450;
	line.duration = line.end_time - line.start_time;

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		1,
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	std::sort(
		result.begin(), result.end(), [](const MotionLine &a, const MotionLine &b) {
			return a.start_time < b.start_time;
		}
	);

	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result[0].start_time, 100);
	int first_alpha = extract_alpha_value(result[0].text);
	ASSERT_GE(first_alpha, 0);
	// 首帧不应全透明（采样原点已前移到前一帧，fade-in 有可见进度）
	EXPECT_LT(first_alpha, 255);
}

TEST(MochaHandler, ApplyMotionFadeKillTransLastVisibleFrameShouldNotBeFullyTransparent) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	MotionLine line;
	line.text = R"({\fade(255,0,255,0,100,200,300)\alpha&H00&}hello)";
	line.start_time = 150;
	line.end_time = 450;
	line.duration = line.end_time - line.start_time;

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		1,
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	std::sort(
		result.begin(), result.end(), [](const MotionLine &a, const MotionLine &b) {
			return a.start_time < b.start_time;
		}
	);

	ASSERT_EQ(result.size(), 3u);
	EXPECT_EQ(result.back().start_time, 300);
	int last_alpha = extract_alpha_value(result.back().text);
	ASSERT_GE(last_alpha, 0);
	EXPECT_LT(last_alpha, 255);
}

TEST(MochaHandler, ApplyMotionFadeWithoutKillTransKeepsFadeInsideOverrideBlock) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = false;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	MotionLine line;
	line.text = R"({\fade(255,0,255,0,200,400,600)\alpha&H00&}hello)";
	line.start_time = 150;
	line.end_time = 349;
	line.duration = line.end_time - line.start_time;

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		1,
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	std::sort(
		result.begin(), result.end(), [](const MotionLine &a, const MotionLine &b) {
			return a.start_time < b.start_time;
		}
	);

	ASSERT_FALSE(result.empty());
	EXPECT_TRUE(std::regex_search(result[0].text, std::regex(R"(\{[^}]*\\fade\()")));
	EXPECT_EQ(result[0].text.find("}\\fade("), std::string::npos);
}

TEST(MochaHandler, ApplyMotionTransformAlphaKillTransFirstVisibleFrameShouldNotBeFullyTransparent) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	MotionLine line;
	line.text = R"({\alpha&HFF&\t(0,200,\alpha&H00&)}hello)";
	line.start_time = 150;
	line.end_time = 349;
	line.duration = line.end_time - line.start_time;
	line.tokenize_transforms();

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		1,
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	std::sort(
		result.begin(), result.end(), [](const MotionLine &a, const MotionLine &b) {
			return a.start_time < b.start_time;
		}
	);

	ASSERT_EQ(result.size(), 2u);
	int alpha = extract_alpha_value(result[0].text);
	ASSERT_GE(alpha, 0);
	EXPECT_LT(alpha, 255);
}

TEST(MochaHandler, ApplyMotionTransformPrimaryAlphaKillTransFirstVisibleFrameShouldNotBeFullyTransparent) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	MotionLine line;
	line.text = R"({\1a&HFF&\t(0,200,\1a&H00&)}hello)";
	line.start_time = 150;
	line.end_time = 349;
	line.duration = line.end_time - line.start_time;
	line.tokenize_transforms();

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		1,
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	std::sort(
		result.begin(), result.end(), [](const MotionLine &a, const MotionLine &b) {
			return a.start_time < b.start_time;
		}
	);

	ASSERT_EQ(result.size(), 2u);
	int alpha = extract_alpha_value(result[0].text);
	ASSERT_GE(alpha, 0);
	EXPECT_LT(alpha, 255);
}

TEST(MochaHandler, ApplyMotionTransformAlphaKillTransAtFrameZeroShouldNotShift) {
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	MotionLine line;
	line.text = R"({\alpha&HFF&\t(0,200,\alpha&H00&)}hello)";
	line.start_time = 0;
	line.end_time = 149;
	line.duration = line.end_time - line.start_time;
	line.tokenize_transforms();

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		0,
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	ASSERT_EQ(result.size(), 1u);
	int alpha = extract_alpha_value(result[0].text);
	ASSERT_GE(alpha, 0);
	EXPECT_LT(alpha, 255);
}

// ============================================================================
// FadeSampler 单元测试
// ============================================================================

TEST(FadeSampler, CreateShiftsOriginBackOneFrame) {
	auto sampler = FadeSampler::create(
		1, 1,
		std::function<int(int)>([](int f) { return f * 100; })
	);
	// first_vis_frame_abs = 1+1-1 = 1, origin_frame = 0, ms(0)=0
	EXPECT_EQ(sampler.fade_origin, 0);
}

TEST(FadeSampler, CreateClampsAtFrameZero) {
	auto sampler = FadeSampler::create(
		0, 1,
		std::function<int(int)>([](int f) { return f * 100; })
	);
	// first_vis_frame_abs = 0, origin_frame = max(0,-1) = 0
	EXPECT_EQ(sampler.fade_origin, 0);
}

TEST(FadeSampler, ComputeShiftedGreaterThanOriginal) {
	auto sampler = FadeSampler::create(
		1, 1,
		std::function<int(int)>([](int f) { return f * 100; })
	);

	int td_orig, td_shift;
	sampler.compute(100, 200, 150, 450, td_orig, td_shift);
	EXPECT_GT(td_shift, td_orig);
}

TEST(FadeSampler, ComputeNoShiftAtFrameZero) {
	auto sampler = FadeSampler::create(
		0, 1,
		std::function<int(int)>([](int f) { return f * 100; })
	);

	int td_orig, td_shift;
	sampler.compute(0, 100, 0, 300, td_orig, td_shift);
	// fade_origin = 0, line.start_time = 0, shift = 0-0 = 0
	EXPECT_EQ(td_shift, td_orig);
}

TEST(FadeSampler, EvaluateFadeInUsesShifted) {
	// \fade(255,0,255,0,100,200,300)
	FullFadeData f{255, 0, 255, 0, 100, 200, 300};
	// td_shifted 已过 fade-in 端点，td_original 仍在 fade-in 窗口内
	// evaluate 应返回 a2（完全不透明），因为 shifted >= t2
	double factor = FadeSampler::evaluate_fade(f, 175, 25);
	EXPECT_DOUBLE_EQ(factor, 0.0); // a2 = 0
}

TEST(FadeSampler, EvaluateFadeOutUsesOriginal) {
	FullFadeData f{255, 0, 255, 0, 100, 200, 300};
	// 尾帧：shifted 已越界 >= t4，但 original 仍在 [t3,t4) 内
	double factor = FadeSampler::evaluate_fade(f, 350, 200);
	// td_original=200 在 [t3=200, t4=300) 内，起始端: a2 + (a3-a2)*(200-200)/100 = 0
	EXPECT_DOUBLE_EQ(factor, 0.0);
}

TEST(FadeSampler, EvaluateFadeOutLastFrameNotFullyTransparent) {
	FullFadeData f{255, 0, 255, 0, 100, 200, 300};
	// td_original=250 应在 fade-out 中间
	double factor = FadeSampler::evaluate_fade(f, 400, 250);
	// a2 + (a3-a2)*(250-200)/100 = 0 + 255*50/100 = 127.5
	EXPECT_GT(factor, 0.0);
	EXPECT_LT(factor, 255.0);
}

TEST(FadeSampler, EvaluateFadeZeroLengthFadeInSkipsDivision) {
	// t1==t2 时分支 2 不可达：td_shifted>=t1 意味着 td_shifted>=t2，跳过除法
	FullFadeData f{255, 0, 255, 100, 100, 200, 300};
	// td_shifted=50 < t1=100 → 返回 a1
	EXPECT_DOUBLE_EQ(FadeSampler::evaluate_fade(f, 50, 50), 255.0);
	// td_shifted=100 >= t1=t2=100 → 跳过 branch1 和 branch2，进入 a2 段
	EXPECT_DOUBLE_EQ(FadeSampler::evaluate_fade(f, 100, 100), 0.0);
}

TEST(FadeSampler, EvaluateFadeZeroLengthFadeOutSkipsDivision) {
	// t3==t4 时分支 4 不可达：td_original>=t3 意味着 td_original>=t4，跳过除法
	FullFadeData f{255, 0, 255, 0, 100, 200, 200};
	// td_original=150 < t3=200 → 返回 a2
	EXPECT_DOUBLE_EQ(FadeSampler::evaluate_fade(f, 250, 150), 0.0);
	// td_original=200 >= t3=t4=200 → 跳过 branch3 和 branch4，返回 a3
	EXPECT_DOUBLE_EQ(FadeSampler::evaluate_fade(f, 250, 200), 255.0);
}

// ============================================================================
// alpha 尾段回归测试
// ============================================================================

TEST(MochaHandler, ApplyMotionFadeOutLastFrameShouldNotBeFullyTransparentWithShift) {
	// 验证 fade-out 尾帧在前移采样下仍不越界为全透明
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	// fade-out 窗口 [600, 800] 覆盖到行末尾，确保尾帧仍在 fade-out 区间内
	MotionLine line;
	line.text = R"({\fade(255,0,255,0,100,600,800)\alpha&H00&}hello)";
	line.start_time = 150;
	line.end_time = 950;
	line.duration = line.end_time - line.start_time;

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		1,
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	std::sort(
		result.begin(), result.end(), [](const MotionLine &a, const MotionLine &b) {
			return a.start_time < b.start_time;
		}
	);

	ASSERT_GE(result.size(), 2u);
	int last_alpha = extract_alpha_value(result.back().text);
	ASSERT_GE(last_alpha, 0);
	// 尾帧应在 fade-out 区间但不应越界为完全透明
	EXPECT_LT(last_alpha, 255);
}

TEST(MochaHandler, ApplyMotionTransformAlphaFadeOutTailShouldNotOvershoot) {
	// 验证 \t(alpha) opaque→transparent 时，晚期帧不应因统一使用 shifted 时间而提前到达完全透明
	auto dh = make_test_data_handler();

	MotionOptions opts;
	opts.kill_trans = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	// \t(0,600,\alpha&HFF&) — 从不透明到完全透明，持续600ms
	MotionLine line;
	line.text = R"({\alpha&H00&\t(0,600,\alpha&HFF&)}hello)";
	line.start_time = 150;
	line.end_time = 950;
	line.duration = line.end_time - line.start_time;
	line.tokenize_transforms();

	std::vector<MotionLine> lines = {line};
	auto result = handler.apply_motion(
		lines,
		1,
		[](int ms) { return ms / 100; },
		[](int frame) { return frame * 100; }
	);

	std::sort(
		result.begin(), result.end(), [](const MotionLine &a, const MotionLine &b) {
			return a.start_time < b.start_time;
		}
	);

	ASSERT_GE(result.size(), 6u);
	auto late_it = std::find_if(
		result.begin(), result.end(), [](const MotionLine &line) {
			return line.start_time == 600;
		}
	);
	ASSERT_NE(late_it, result.end());
	int late_alpha = extract_alpha_value(late_it->text);
	ASSERT_GE(late_alpha, 0);
	EXPECT_LT(late_alpha, 255);
}

class MochaProcessor : public libagi {};

TEST(MochaProcessor, PostprocessRemovesEmptyClipInLinear) {
	MotionOptions opts;
	MotionProcessor processor(opts, 640, 360);

	MotionLine line;
	line.text = R"({\clip()}hello)";
	line.was_linear = true;

	std::vector<MotionLine> lines = {line};
	processor.postprocess_lines(lines);

	ASSERT_EQ(lines.size(), 1u);
	EXPECT_EQ(lines[0].text, "hello");
}

TEST(MochaProcessor, PostprocessShiftsTypingKaraokeAndCleansEmptyBlocks) {
	MotionOptions opts;
	MotionProcessor processor(opts, 640, 360);

	MotionLine line;
	line.text = R"({\kk(4,3,4,5,6)}ABCD)";
	line.was_linear = true;
	line.karaoke_shift = 5.0;

	std::vector<MotionLine> lines = {line};
	processor.postprocess_lines(lines);

	ASSERT_EQ(lines.size(), 1u);
	EXPECT_EQ(lines[0].text, R"(A{\kk(3,2,5,6)}BCD)");
}

TEST(MochaProcessor, PrepareLinesHonorsIndependentClipOptions) {
	MotionOptions opts;
	opts.rect_clip = false;
	opts.vect_clip = false;

	MotionProcessor processor(opts, 640, 360);

	MotionLine line;
	line.text = R"({\clip(0,0,100,100)}hello)";
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;

	std::vector<MotionLine> lines = {line};
	ClipTrackOptions clip_opts;
	clip_opts.rect_clip = true;
	clip_opts.vect_clip = true;
	clip_opts.rc_to_vc = true;

	processor.prepare_lines(lines, &clip_opts);

	ASSERT_EQ(lines.size(), 1u);
	EXPECT_TRUE(lines[0].has_clip);
	EXPECT_NE(lines[0].text.find(R"(\clip(m 0 0 l 100 0 100 100 0 100))"), std::string::npos)
		<< "Expected clip preprocessing to honor independent clip options, got: " << lines[0].text;
	EXPECT_EQ(lines[0].text.find(R"(\clip())"), std::string::npos)
		<< "Independent clip options should prevent injecting an empty clip placeholder";
}

TEST(MochaProcessor, PrepareLinesConvertsFadToFullFade) {
	MotionOptions opts;
	MotionProcessor processor(opts, 640, 360);

	MotionLine line;
	line.text = R"({\fad(100,200)}hello)";
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;

	std::vector<MotionLine> lines = {line};
	processor.prepare_lines(lines, nullptr);

	ASSERT_EQ(lines.size(), 1u);
	EXPECT_NE(lines[0].text.find(R"(\fade(255,0,255,0,100,800,1000))"), std::string::npos)
		<< "Expected short fad to be normalized, got: " << lines[0].text;
	EXPECT_EQ(lines[0].text.find(R"(\fad(100,200))"), std::string::npos);
}

TEST(MochaProcessor, PrepareLinesShortFadBeforeFullFadeKeepsNormalizedFirstFade) {
	MotionOptions opts;
	MotionProcessor processor(opts, 640, 360);

	MotionLine line;
	line.text = R"({\fad(100,200)\fade(255,0,255,0,300,400,500)}hello)";
	line.start_time = 0;
	line.end_time = 1000;
	line.duration = 1000;

	std::vector<MotionLine> lines = {line};
	processor.prepare_lines(lines, nullptr);

	ASSERT_EQ(lines.size(), 1u);
	EXPECT_NE(lines[0].text.find(R"(\fade(255,0,255,0,100,800,1000))"), std::string::npos)
		<< "Expected first short fad to be normalized into the surviving full fade, got: " << lines[0].text;
	EXPECT_EQ(lines[0].text.find(R"(\fad(100,200))"), std::string::npos)
		<< "Short fad should not survive preprocessing once normalized";
	EXPECT_EQ(lines[0].text.find(R"(\fade(255,0,255,0,300,400,500))"), std::string::npos)
		<< "Later conflicting full fade should be removed after normalization, got: " << lines[0].text;
}
