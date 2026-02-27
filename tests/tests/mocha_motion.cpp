// Copyright (c) 2024-2026, Aegisub contributors
// 摩卡追踪模块完整测试套件

#include <main.h>

#include "motion_math.h"
#include "motion_tags.h"
#include "motion_transform.h"
#include "motion_line.h"
#include "motion_data_handler.h"
#include "motion_handler.h"

#include <cmath>
#include <regex>

using namespace mocha;

// ============================================================================
// math 工具测试
// ============================================================================

class MochaMath : public libagi { };

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

class MochaTags : public libagi { };

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
	// 整数坐标应转为浮点字符串
	std::string clip = R"(\clip(100,200,300,400))";
	auto result = tag_utils::convert_clip_to_fp(clip);
	EXPECT_NE(result.find("100"), std::string::npos);
}

// ============================================================================
// Transform 变换标签测试
// ============================================================================

class MochaTransform : public libagi { };

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

// ============================================================================
// MotionLine 行处理测试
// ============================================================================

class MochaLine : public libagi { };

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
	line.run_callback_on_first_override([](const std::string &block) {
		return "{\\fscx200}";
	});
	EXPECT_NE(line.text.find("\\fscx200"), std::string::npos);
}

TEST(MochaLine, RunCallbackOnOverrides) {
	MotionLine line;
	line.text = R"({first}text{second}more)";
	line.run_callback_on_overrides([](const std::string &block, int) {
		return "{replaced}";
	});
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

// ============================================================================
// DataHandler 数据解析器测试
// ============================================================================

class MochaDataHandler : public libagi { };

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
	EXPECT_NO_THROW({
		bool ok = dh.parse(malformed, 1920, 1080);
		EXPECT_FALSE(ok);
	});
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
	EXPECT_NO_THROW({
		bool ok = dh.parse(malformed, 1920, 1080);
		EXPECT_FALSE(ok);
	});
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

class MochaHandler : public libagi { };

/// @brief 辅助函数：创建含有效数据的 DataHandler
static DataHandler make_test_data_handler() {
	DataHandler dh;
	dh.parse(AE_VALID_DATA, 1920, 1080);
	dh.add_reference_frame(1);
	return dh;
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

	EXPECT_NO_THROW({
		auto result = handler.cb_position("abc,def", 2);
		// 无法匹配坐标正则，应返回原值
		EXPECT_EQ(result, "(abc,def)");
	});
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

	EXPECT_NO_THROW({
		auto result = handler.cb_absolute_position("960,540", 1);
		EXPECT_EQ(result, "(960,540)");
	});
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

	EXPECT_NO_THROW({
		auto result = handler.cb_origin("not,numbers", 2);
		EXPECT_EQ(result, "(not,numbers)");
	});
}

TEST(MochaHandler, CbScaleBasic) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.x_scale = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_scale("100", 2);
	// 缩放值应为有效数字
	EXPECT_NO_THROW({ std::stod(result); });
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
	EXPECT_NO_THROW({ std::stod(result); });
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
	EXPECT_NO_THROW({ std::stod(result); });
}

TEST(MochaHandler, CbRotateY) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.y_rotation = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_rotate_y("0", 2);
	EXPECT_NO_THROW({ std::stod(result); });
}

TEST(MochaHandler, CbRotateZ) {
	auto dh = make_test_data_handler();
	dh.calculate_current_state(2);

	MotionOptions opts;
	opts.z_rotation = true;
	MotionHandler handler(opts, &dh, nullptr, nullptr);

	auto result = handler.cb_rotate_z("0", 2);
	EXPECT_NO_THROW({ std::stod(result); });
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
	EXPECT_NO_THROW({ std::stod(result); });
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

	EXPECT_NO_THROW({
		auto result = handler.cb_rect_clip("abc,def,ghi,jkl", 2);
	});
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
	EXPECT_NO_THROW({
		auto result = handler.apply_callbacks(text, 2);
		EXPECT_FALSE(result.empty());
	});
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
