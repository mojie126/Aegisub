/// @file abb_smart_test.cpp
/// @brief 智能黑边计算单元测试

#include <main.h>

#include "include/aegisub/video_provider.h"
#include "options.h"
#include "video_frame.h"

#include <libaegisub/option.h>

#include <memory>

std::unique_ptr<VideoProvider> CreateCacheVideoProvider(std::unique_ptr<VideoProvider> parent);

namespace {

class ScopedConfigOptions final {
	agi::Options options;
	agi::Options *previous = nullptr;

public:
	ScopedConfigOptions()
	: options("", R"({
		"Provider": {
			"Video": {
				"Cache": {
					"Size": 32
				}
			}
		}
	})", agi::Options::FLUSH_SKIP)
	, previous(config::opt) {
		config::opt = &options;
	}

	~ScopedConfigOptions() {
		config::opt = previous;
	}
};

class DummyPaddingVideoProvider final : public VideoProvider {
public:
	void GetFrame(int, VideoFrame &) override { }
	void SetColorSpace(std::string const&) override { }
	int GetFrameCount() const override { return 1; }
	int GetWidth() const override { return 1920; }
	int GetHeight() const override { return 1120; }
	double GetDAR() const override { return 16.0 / 9.0; }
	agi::vfr::Framerate GetFPS() const override { return agi::vfr::Framerate(24, 1); }
	std::vector<int> GetKeyFrames() const override { return {}; }
	std::string GetColorSpace() const override { return "TV.709"; }
	std::string GetDecoderName() const override { return "FFmpegSource"; }
	int GetPaddingTop() const override { return 20; }
	int GetPaddingBottom() const override { return 20; }
};

} // namespace

class SmartABBTest : public libagi { };

TEST(SmartABBTest, ZeroOrNegativeHeight) {
	auto result = CalculateSmartPadding(0);
	EXPECT_EQ(result.top, 0);
	EXPECT_EQ(result.bottom, 0);

	result = CalculateSmartPadding(-10);
	EXPECT_EQ(result.top, 0);
	EXPECT_EQ(result.bottom, 0);
}

TEST(SmartABBTest, ExactStandardNoPadding) {
	auto result = CalculateSmartPadding(1080);
	EXPECT_EQ(result.top, 0);
	EXPECT_EQ(result.bottom, 0);

	result = CalculateSmartPadding(720);
	EXPECT_EQ(result.top, 0);
	EXPECT_EQ(result.bottom, 0);
}

TEST(SmartABBTest, SinglePixelExtraAllocatedToTop) {
	auto result = CalculateSmartPadding(1079);
	EXPECT_EQ(result.top, 1);
	EXPECT_EQ(result.bottom, 0);
}

TEST(SmartABBTest, LargeMatch) {
	auto result = CalculateSmartPadding(1604);
	EXPECT_EQ(result.top, 278);
	EXPECT_EQ(result.bottom, 278);
	EXPECT_EQ(result.top + result.bottom + 1604, 2160);
}

TEST(SmartABBTest, AboveMaxStandardNoPadding) {
	auto result = CalculateSmartPadding(4321);
	EXPECT_EQ(result.top, 0);
	EXPECT_EQ(result.bottom, 0);
}

TEST(SmartABBTest, CacheWrapperPreservesPaddingMetadata) {
	ScopedConfigOptions scoped_options;
	auto provider = CreateCacheVideoProvider(std::make_unique<DummyPaddingVideoProvider>());
	EXPECT_EQ(provider->GetPaddingTop(), 20);
	EXPECT_EQ(provider->GetPaddingBottom(), 20);
	EXPECT_EQ(provider->GetDecoderName(), "FFmpegSource");
}
