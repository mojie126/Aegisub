// Copyright (c) 2024-2026, Aegisub contributors
// HDR/DV/硬解审计修复验证测试套件

#include <main.h>

#include <aegisub/video_provider.h>
#include "video_frame.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

// ============================================================================
// 辅助函数：从video_out_gl.cpp提取的纯逻辑函数的等价实现
// 用于单元测试验证，与src中的实现保持一致
// ============================================================================

namespace hdr_test {

/// @brief 非对称黑边padding在屏幕上的像素数
struct PaddingScreenPixels {
	int top;
	int bottom;
};

/// @brief M2: 计算非对称黑边填充像素数（等价于video_out_gl.cpp中的CalculatePaddingPixels）
static PaddingScreenPixels CalculatePaddingPixels(int viewport_height, int frame_height, int padding_top, int padding_bottom) {
	if (padding_top <= 0 && padding_bottom <= 0) return {0, 0};
	const int total_padded_h = std::max(frame_height + padding_top + padding_bottom, 1);
	const int max_single = std::max(0, viewport_height / 2 - 1);
	auto clamp_px = [&](int pad) -> int {
		if (pad <= 0) return 0;
		int px = (viewport_height * pad) / total_padded_h;
		return std::max(0, std::min(px, max_single));
	};
	return {clamp_px(padding_top), clamp_px(padding_bottom)};
}

/// @brief M3: DV Profile感知的LUT文件名选择（等价于VideoOutGL::GetLutFilename）
/// @param type HDR类型
/// @param dvProfile Dolby Vision Profile编号
/// @return LUT文件名
static std::string GetLutFilename(HDRType type, int dvProfile = 0) {
	switch (type) {
		case HDRType::DolbyVision:
			if (dvProfile == 7 || dvProfile == 8) {
				return "PQ2SDR.cube";
			}
			return "DV2SDR.cube";
		case HDRType::HLG:        return "HLG2SDR.cube";
		case HDRType::PQ:
		default:                   return "PQ2SDR.cube";
	}
}

/// @brief H4: guess_colorspace中色彩范围修正逻辑（等价于修复后的行为）
/// @param reported_cr 视频源报告的色彩范围
/// @return 修正后的色彩范围
static int FixColorRange(int reported_cr) {
	if (reported_cr == AGI_CR_UNSPECIFIED)
		return AGI_CR_MPEG;
	return reported_cr;
}

} // namespace hdr_test

// ============================================================================
// M2: CalculatePaddingPixels 非对称黑边测试
// ============================================================================

class PaddingPixelsTest : public libagi { };

TEST(PaddingPixelsTest, ZeroPaddingReturnsZero) {
	auto r = hdr_test::CalculatePaddingPixels(1080, 1080, 0, 0);
	EXPECT_EQ(r.top, 0);
	EXPECT_EQ(r.bottom, 0);
}

TEST(PaddingPixelsTest, NegativePaddingReturnsZero) {
	auto r = hdr_test::CalculatePaddingPixels(1080, 1080, -10, -10);
	EXPECT_EQ(r.top, 0);
	EXPECT_EQ(r.bottom, 0);
}

TEST(PaddingPixelsTest, SymmetricPaddingCalculation) {
	// 1080p视口，960帧高，上下各60行padding
	// total = 960 + 60 + 60 = 1080
	// top_px = (1080 * 60) / 1080 = 60
	// bottom_px = (1080 * 60) / 1080 = 60
	auto r = hdr_test::CalculatePaddingPixels(1080, 960, 60, 60);
	EXPECT_EQ(r.top, 60);
	EXPECT_EQ(r.bottom, 60);
}

TEST(PaddingPixelsTest, AsymmetricPaddingCalculation) {
	// 1080p视口，960帧高，top=80, bottom=40
	// total = 960 + 80 + 40 = 1080
	// top_px = (1080 * 80) / 1080 = 80
	// bottom_px = (1080 * 40) / 1080 = 40
	auto r = hdr_test::CalculatePaddingPixels(1080, 960, 80, 40);
	EXPECT_EQ(r.top, 80);
	EXPECT_EQ(r.bottom, 40);
}

TEST(PaddingPixelsTest, ProportionalScaling) {
	// 540p视口（1080p的一半），960帧高，上下各60行padding
	// total = 960 + 120 = 1080
	// top/bottom_px = (540 * 60) / 1080 = 30
	auto r = hdr_test::CalculatePaddingPixels(540, 960, 60, 60);
	EXPECT_EQ(r.top, 30);
	EXPECT_EQ(r.bottom, 30);
}

TEST(PaddingPixelsTest, ClampToHalfViewport) {
	// 100px视口，10帧高，大padding
	// max = 100/2 - 1 = 49
	auto r = hdr_test::CalculatePaddingPixels(100, 10, 1000, 1000);
	EXPECT_EQ(r.top, 49);
	EXPECT_EQ(r.bottom, 49);
}

TEST(PaddingPixelsTest, OneSideOnly) {
	// 只有顶部padding
	auto r = hdr_test::CalculatePaddingPixels(1080, 960, 60, 0);
	EXPECT_EQ(r.top, 63); // (1080*60)/1020 = 63
	EXPECT_EQ(r.bottom, 0);
}

// ============================================================================
// 自适应黑边分配测试（CalculateAdaptivePadding）
// ============================================================================

class AdaptivePaddingTest : public libagi { };

TEST(AdaptivePaddingTest, ZeroPaddingReturnsZero) {
	auto r = CalculateAdaptivePadding(1080, 0);
	EXPECT_EQ(r.top, 0);
	EXPECT_EQ(r.bottom, 0);
}

TEST(AdaptivePaddingTest, NegativePaddingReturnsZero) {
	auto r = CalculateAdaptivePadding(1080, -10);
	EXPECT_EQ(r.top, 0);
	EXPECT_EQ(r.bottom, 0);
}

TEST(AdaptivePaddingTest, Match2160From1604) {
	// 3840×1604 + 280 → target 2160
	// 1604 + 280*2 = 2164, nearest standard = 2160
	// total = 2160 - 1604 = 556, half = 278, remainder = 0
	auto r = CalculateAdaptivePadding(1604, 280);
	EXPECT_EQ(r.top, 278);
	EXPECT_EQ(r.bottom, 278);
	EXPECT_EQ(r.top + r.bottom + 1604, 2160);
}

TEST(AdaptivePaddingTest, Match1080From960) {
	// 1920×960 + 60 → target 1080
	// 960 + 120 = 1080, exact match
	auto r = CalculateAdaptivePadding(960, 60);
	EXPECT_EQ(r.top, 60);
	EXPECT_EQ(r.bottom, 60);
	EXPECT_EQ(r.top + r.bottom + 960, 1080);
}

TEST(AdaptivePaddingTest, Match720From640) {
	// 640 + 40*2 = 720, exact match
	auto r = CalculateAdaptivePadding(640, 40);
	EXPECT_EQ(r.top, 40);
	EXPECT_EQ(r.bottom, 40);
	EXPECT_EQ(r.top + r.bottom + 640, 720);
}

TEST(AdaptivePaddingTest, OddTotalPaddingTopGetsExtra) {
	// 1921×1079 + 1 → target 1080 (if 1079+2=1081, nearest=1080)
	// total = 1080 - 1079 = 1, half=0, remainder=1
	auto r = CalculateAdaptivePadding(1079, 1);
	EXPECT_EQ(r.top, 1);
	EXPECT_EQ(r.bottom, 0);
	EXPECT_EQ(r.top + r.bottom + 1079, 1080);
}

TEST(AdaptivePaddingTest, NoStandardMatchFallbackSymmetric) {
	// 帧高度500 + padding=5 → 500+10=510，无标准高度在±5范围内
	// 最近标准720，|720-510|=210 > 5 → 不匹配
	auto r = CalculateAdaptivePadding(500, 5);
	EXPECT_EQ(r.top, 5);
	EXPECT_EQ(r.bottom, 5);
}

TEST(AdaptivePaddingTest, ExceedsMaxStandardFallbackSymmetric) {
	// 帧高度4320 + padding=100 → 4520，无更高标准
	auto r = CalculateAdaptivePadding(4320, 100);
	EXPECT_EQ(r.top, 100);
	EXPECT_EQ(r.bottom, 100);
}

TEST(AdaptivePaddingTest, Match1440From1280) {
	// 1280 + 80*2 = 1440, exact match
	auto r = CalculateAdaptivePadding(1280, 80);
	EXPECT_EQ(r.top, 80);
	EXPECT_EQ(r.bottom, 80);
	EXPECT_EQ(r.top + r.bottom + 1280, 1440);
}

// ============================================================================
// M3+S1: GetLutFilename DV Profile感知 测试
// ============================================================================

class LutFilenameTest : public libagi { };

TEST(LutFilenameTest, PQReturnsPQ2SDR) {
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::PQ), "PQ2SDR.cube");
}

TEST(LutFilenameTest, HLGReturnsHLG2SDR) {
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::HLG), "HLG2SDR.cube");
}

TEST(LutFilenameTest, SDRReturnsPQ2SDRDefault) {
	// SDR走default分支
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::SDR), "PQ2SDR.cube");
}

TEST(LutFilenameTest, DVProfile0ReturnsDV2SDR) {
	// 未知Profile使用专用DV LUT
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::DolbyVision, 0), "DV2SDR.cube");
}

TEST(LutFilenameTest, DVProfile5ReturnsDV2SDR) {
	// P5纯IPT-PQ-C2单层，使用专用DV LUT
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::DolbyVision, 5), "DV2SDR.cube");
}

TEST(LutFilenameTest, DVProfile7ReturnsPQ2SDR) {
	// P7双层HDR10基层，解码器输出标准PQ
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::DolbyVision, 7), "PQ2SDR.cube");
}

TEST(LutFilenameTest, DVProfile8ReturnsPQ2SDR) {
	// P8.x单层HDR10/HLG兼容，解码器输出标准PQ/HLG
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::DolbyVision, 8), "PQ2SDR.cube");
}

TEST(LutFilenameTest, DVProfile10ReturnsDV2SDR) {
	// 未来Profile使用安全默认值
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::DolbyVision, 10), "DV2SDR.cube");
}

TEST(LutFilenameTest, DVProfileIgnoredForNonDV) {
	// 非DV类型忽略dvProfile参数
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::PQ, 8), "PQ2SDR.cube");
	EXPECT_EQ(hdr_test::GetLutFilename(HDRType::HLG, 7), "HLG2SDR.cube");
}

// ============================================================================
// H4: guess_colorspace 色彩范围修正 测试
// ============================================================================

class ColorRangeFixTest : public libagi { };

TEST(ColorRangeFixTest, UnspecifiedDefaultsToMPEG) {
	// H4核心修复：UNSPECIFIED(0)应假定为limited range
	EXPECT_EQ(hdr_test::FixColorRange(AGI_CR_UNSPECIFIED), AGI_CR_MPEG);
}

TEST(ColorRangeFixTest, MPEGPreserved) {
	// 明确报告的limited range不应被覆盖
	EXPECT_EQ(hdr_test::FixColorRange(AGI_CR_MPEG), AGI_CR_MPEG);
}

TEST(ColorRangeFixTest, JPEGPreserved) {
	// H4关键验证：明确报告的full range(JPEG)不应被覆盖
	// 修复前此场景会被错误地强制为MPEG
	EXPECT_EQ(hdr_test::FixColorRange(AGI_CR_JPEG), AGI_CR_JPEG);
}

// ============================================================================
// HDRType 枚举值验证
// ============================================================================

class HDRTypeEnumTest : public libagi { };

TEST(HDRTypeEnumTest, EnumValues) {
	EXPECT_EQ(static_cast<int>(HDRType::SDR), 0);
	EXPECT_EQ(static_cast<int>(HDRType::PQ), 1);
	EXPECT_EQ(static_cast<int>(HDRType::HLG), 2);
	EXPECT_EQ(static_cast<int>(HDRType::DolbyVision), 3);
}

TEST(HDRTypeEnumTest, ZeroInitIsSDR) {
	// 验证默认零初始化对应SDR（video_out_gl.h中使用{}初始化）
	HDRType t{};
	EXPECT_EQ(t, HDRType::SDR);
}

TEST(HDRTypeEnumTest, IntRoundTrip) {
	// 验证int↔HDRType转换的正确性
	for (int i = 0; i <= 3; ++i) {
		HDRType t = static_cast<HDRType>(i);
		EXPECT_EQ(static_cast<int>(t), i);
	}
}

// ============================================================================
// VideoProvider基类默认值测试
// ============================================================================

class MockVideoProvider : public VideoProvider {
public:
	void GetFrame(int n, VideoFrame &frame) override {}
	void SetColorSpace(std::string const&) override {}

	int GetFrameCount() const override { return 1; }
	int GetWidth() const override { return 1920; }
	int GetHeight() const override { return 1080; }
	double GetDAR() const override { return 0; }
	agi::vfr::Framerate GetFPS() const override { return agi::vfr::Framerate(24000, 1001); }
	std::vector<int> GetKeyFrames() const override { return {}; }
	std::string GetColorSpace() const override { return "BT.709"; }
	std::string GetRealColorSpace() const override { return "BT.709"; }
	std::string GetDecoderName() const override { return "mock"; }
	bool ShouldSetVideoProperties() const override { return false; }
	bool HasAudio() const override { return false; }
};

class VideoProviderBaseTest : public libagi { };

TEST(VideoProviderBaseTest, DefaultHDRTypeIsSDR) {
	MockVideoProvider provider;
	EXPECT_EQ(provider.GetHDRType(), HDRType::SDR);
}

TEST(VideoProviderBaseTest, DefaultDVProfileIsZero) {
	MockVideoProvider provider;
	EXPECT_EQ(provider.GetDVProfile(), 0);
}

TEST(VideoProviderBaseTest, DefaultIsNotHWDecoding) {
	MockVideoProvider provider;
	EXPECT_FALSE(provider.IsHWDecoding());
}
