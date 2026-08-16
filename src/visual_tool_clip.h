// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
// Aegisub Project http://www.aegisub.org/

/// @file visual_tool_clip.h
/// @see visual_tool_clip.cpp
/// @ingroup visual_ts
///

#include "visual_feature.h"
#include "visual_tool.h"

#include <map>

/// VisualDraggableFeature with siblings
struct ClipCorner final : public VisualDraggableFeature {
	ClipCorner *horiz = nullptr; ///< Other corner on this corner's horizontal line
	ClipCorner *vert = nullptr;  ///< Other corner on this corner's vertical line
	ClipCorner() { type = DRAG_SMALL_CIRCLE; }
};

class VisualToolClip final : public VisualTool<ClipCorner> {
	/// 单个对话行的裁剪状态：矩形对角 + 4 个角点特征
	struct LineState {
		Vector2D cur_1;
		Vector2D cur_2;
		bool inverse = false; ///< Is this currently in iclip mode?
		ClipCorner *corners[4] = {}; ///< TL/TR/BL/BR 四个角点特征
	};

	/// 选中及活动可见对话行到裁剪状态的映射
	std::map<AssDialogue*, LineState> line_states;

	/// @brief 重建所有选中及活动可见行的特征与状态
	void RebuildFeatures();

	/// @brief 从行的 \clip 标签刷新矩形状态并更新角点位置
	/// @param state 目标行状态
	/// @param line 所属对话行
	void RefreshLineState(LineState& state, AssDialogue *line);

	bool InitializeHold() override;
	void UpdateHold() override;
	void CommitHold(LineState& state);

	void DoRefresh() override;
	void OnSelectionChanged() override;
	// 活动行切换不改变特征集（由选中集决定），避免点击特征时重建销毁拖拽目标
	void OnLineChanged() override { }
	void OnFrameChanged() override { RebuildFeatures(); }
	void SetFeaturePositions(LineState& state);

	bool InitializeDrag([[maybe_unused]] ClipCorner *feature) override { return true; }
	void UpdateDrag(ClipCorner *feature) override;
	void EndDrag(ClipCorner *feature) override { DoRefresh(); }

	void Draw() override;
public:
	VisualToolClip(VideoDisplay *parent, agi::Context *context);
};
