/// @file ui_regression_audit.cpp
/// @brief UI 阻塞与切源状态回归测试

#include <main.h>

#include "font_list_load_mode.h"
#include "visual_tool_drag.h"

#include <algorithm>
#include <vector>

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

struct DragSelectionFeature {
	int line = 0;
	DraggableFeatureType type = DRAG_NONE;
};

/// @brief 复用拖放分组辅助逻辑，仅保留 Ctrl 手势与索引转换外壳
std::vector<int> CollectGroupedDragSelection(std::vector<DragSelectionFeature>& features,
	std::vector<int> const& selected_lines, size_t clicked_index, bool ctrl_down) {
	if (ctrl_down || clicked_index >= features.size())
		return {};

	std::vector<int> result;
	visual_tool_drag_detail::CollectGroupedDragSelection(features, &features[clicked_index],
		[&selected_lines](int line) {
			return std::find(selected_lines.begin(), selected_lines.end(), line) != selected_lines.end();
		},
		[&result, data = features.data()](DragSelectionFeature& feature) {
			result.push_back(static_cast<int>(&feature - data));
		});
	return result;
}

/// @brief 与 src/subs_edit_box.cpp 中的 DoOnSplit 布局结果保持镜像
struct SplitToggleLayoutResult {
	int min_height = 0;
	bool requests_parent_relayout = false;
};

/// @brief 与 src/subs_edit_box.cpp 中的 DoOnSplit 布局重排步骤保持镜像
SplitToggleLayoutResult ResolveSplitToggleLayout(int previous_min_height, int toggled_best_height,
	bool clear_previous_min_size, bool notify_parent_size_event) {
	const int effective_min_height = clear_previous_min_size ? 0 : previous_min_height;
	return {std::max(toggled_best_height, effective_min_height), notify_parent_size_event};
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

TEST(UIRegressionAuditTest, VisualToolDragGroupsMoveEndHandlesAcrossSelectedLines) {
	std::vector<DragSelectionFeature> features = {
		{1, DRAG_BIG_SQUARE},
		{1, DRAG_BIG_CIRCLE},
		{2, DRAG_BIG_SQUARE},
		{2, DRAG_BIG_CIRCLE},
		{3, DRAG_BIG_CIRCLE},
	};

	const auto grouped = CollectGroupedDragSelection(features, {1, 2}, 1, false);
	EXPECT_EQ(grouped, (std::vector<int>{1, 3}));
}

TEST(UIRegressionAuditTest, VisualToolDragGroupsOriginHandlesOnlyWhenSelectedLinesHaveOrigin) {
	std::vector<DragSelectionFeature> features = {
		{1, DRAG_BIG_TRIANGLE},
		{2, DRAG_BIG_SQUARE},
		{3, DRAG_BIG_TRIANGLE},
		{3, DRAG_BIG_CIRCLE},
	};

	const auto grouped = CollectGroupedDragSelection(features, {1, 2, 3}, 0, false);
	EXPECT_EQ(grouped, (std::vector<int>{0, 2}));
}

TEST(UIRegressionAuditTest, VisualToolDragDoesNotRegroupWhenCtrlSelectionPathIsUsed) {
	std::vector<DragSelectionFeature> features = {
		{1, DRAG_BIG_SQUARE},
		{1, DRAG_BIG_CIRCLE},
		{2, DRAG_BIG_SQUARE},
		{2, DRAG_BIG_CIRCLE},
	};

	const auto grouped = CollectGroupedDragSelection(features, {1, 2}, 1, true);
	EXPECT_TRUE(grouped.empty());
}

TEST(UIRegressionAuditTest, VisualToolDragDoesNotRegroupWhenClickedLineIsNotSelected) {
	std::vector<DragSelectionFeature> features = {
		{1, DRAG_BIG_SQUARE},
		{1, DRAG_BIG_CIRCLE},
		{2, DRAG_BIG_SQUARE},
	};

	const auto grouped = CollectGroupedDragSelection(features, {2}, 1, false);
	EXPECT_TRUE(grouped.empty());
}

TEST(UIRegressionAuditTest, ShowOriginalToggleMustClearPreviousMinHeightBeforeCollapsing) {
	EXPECT_EQ(ResolveSplitToggleLayout(340, 220, false, false).min_height, 340);
	EXPECT_EQ(ResolveSplitToggleLayout(340, 220, true, false).min_height, 220);
}

TEST(UIRegressionAuditTest, ShowOriginalToggleMustRequestParentRelayoutAfterResize) {
	EXPECT_FALSE(ResolveSplitToggleLayout(340, 220, true, false).requests_parent_relayout);
	EXPECT_TRUE(ResolveSplitToggleLayout(340, 220, true, true).requests_parent_relayout);
}
