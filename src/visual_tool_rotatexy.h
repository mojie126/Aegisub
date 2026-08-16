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

/// @file visual_tool_rotatexy.h
/// @see visual_tool_rotatexy.cpp
/// @ingroup visual_ts
///

#include "visual_feature.h"
#include "visual_tool.h"

#include <map>

class VisualToolRotateXY final : public VisualTool<VisualDraggableFeature> {
	/// 单个对话行的 XY 轴旋转状态
	struct LineState {
		float angle_x = 0.f; /// Current x rotation
		float angle_y = 0.f; /// Current y rotation
		float angle_z = 0.f; /// Current z rotation

		float fax = 0.f;
		float fay = 0.f;
		Vector2D fsc;

		float orig_x = 0.f; ///< x rotation at the beginning of the current hold
		float orig_y = 0.f; ///< y rotation at the beginning of the current hold

		Feature *org = nullptr;
	};

	/// 选中及活动可见对话行到旋转状态的映射
	std::map<AssDialogue*, LineState> line_states;

	/// @brief 重建所有选中及活动可见行的特征与状态
	void RebuildFeatures();

	void DoRefresh() override;
	void OnSelectionChanged() override;
	// 活动行切换不改变特征集（由选中集决定），避免点击特征时重建销毁拖拽目标
	void OnLineChanged() override { }
	void OnFrameChanged() override { RebuildFeatures(); }
	void Draw() override;
	void UpdateDrag(Feature *feature) override;
	void EndDrag(Feature *feature) override { DoRefresh(); }
	bool InitializeHold() override;
	void UpdateHold() override;
public:
	VisualToolRotateXY(VideoDisplay *parent, agi::Context *context);
};
