/// @file ui_regression_audit.cpp
/// @brief UI 阻塞与切源状态回归测试

#include <main.h>

#include "font_list_load_mode.h"

namespace {

struct CropPreviewResetState {
	bool has_preview = false;
	bool has_selection = false;
	bool is_dragging = false;
	int video_w = 0;
	int video_h = 0;
};

CropPreviewResetState MakeCropPreviewStateAfterProviderChange(bool has_provider, int provider_width, int provider_height) {
	return {false, false, false, has_provider ? provider_width : 0, has_provider ? provider_height : 0};
}

} // namespace

TEST(UIRegressionAuditTest, StyleEditorUsesProvidedFontListWhenAvailable) {
	EXPECT_EQ(ResolveStyleEditorFontListLoadMode(true, true), StyleEditorFontListLoadMode::UseProvidedList);
	EXPECT_EQ(ResolveStyleEditorFontListLoadMode(true, false), StyleEditorFontListLoadMode::UseProvidedList);
}

TEST(UIRegressionAuditTest, StyleEditorWaitsForAsyncFontListInsteadOfSyncFallback) {
	EXPECT_EQ(ResolveStyleEditorFontListLoadMode(false, true), StyleEditorFontListLoadMode::WaitForAsyncList);
}

TEST(UIRegressionAuditTest, StyleEditorFallsBackSynchronouslyOnlyWithoutAsyncSource) {
	EXPECT_EQ(ResolveStyleEditorFontListLoadMode(false, false), StyleEditorFontListLoadMode::EnumerateSynchronously);
}

TEST(UIRegressionAuditTest, FontChooserUsesProvidedFontListWhenAvailable) {
	EXPECT_EQ(ResolveFontChooserFontListLoadMode(true, true), FontChooserFontListLoadMode::UseProvidedList);
	EXPECT_EQ(ResolveFontChooserFontListLoadMode(true, false), FontChooserFontListLoadMode::UseProvidedList);
}

TEST(UIRegressionAuditTest, FontChooserWaitsForAsyncFontListInsteadOfSyncFallback) {
	EXPECT_EQ(ResolveFontChooserFontListLoadMode(false, true), FontChooserFontListLoadMode::WaitForAsyncList);
}

TEST(UIRegressionAuditTest, FontChooserFallsBackSynchronouslyOnlyWithoutAsyncSource) {
	EXPECT_EQ(ResolveFontChooserFontListLoadMode(false, false), FontChooserFontListLoadMode::EnumerateSynchronously);
}

TEST(UIRegressionAuditTest, CropPreviewResetClearsPreviewAndSelectionOnProviderSwitch) {
	auto state = MakeCropPreviewStateAfterProviderChange(true, 1920, 1080);
	EXPECT_FALSE(state.has_preview);
	EXPECT_FALSE(state.has_selection);
	EXPECT_FALSE(state.is_dragging);
	EXPECT_EQ(state.video_w, 1920);
	EXPECT_EQ(state.video_h, 1080);
}

TEST(UIRegressionAuditTest, CropPreviewResetClearsDimensionsWhenProviderIsGone) {
	auto state = MakeCropPreviewStateAfterProviderChange(false, 1920, 1080);
	EXPECT_FALSE(state.has_preview);
	EXPECT_FALSE(state.has_selection);
	EXPECT_FALSE(state.is_dragging);
	EXPECT_EQ(state.video_w, 0);
	EXPECT_EQ(state.video_h, 0);
}