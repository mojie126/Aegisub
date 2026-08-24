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

#pragma once

#include "visual_feature.h"
#include "visual_tool.h"
#include "spline.h"
#include "command/command.h"

#include <map>

class wxToolBar;

/// Button IDs
enum VisualToolVectorClipMode {
	VCLIP_DRAG = 0, // Assumed to be at the start
	VCLIP_LINE,
	VCLIP_BICUBIC,
	VCLIP_CONVERT,
	VCLIP_INSERT,
	VCLIP_REMOVE,
	VCLIP_FREEHAND,
	VCLIP_FREEHAND_SMOOTH,
	VCLIP_LAST // Leave this at the end and don't use it
};

/// @class VisualToolVectorClipDraggableFeature
/// @brief VisualDraggableFeature with information about a feature's location
///        in the spline
struct VisualToolVectorClipDraggableFeature final : public VisualDraggableFeature {
	/// Which curve in the spline this feature is a point on
	size_t idx = 0;
	/// 0-3; indicates which part of the curve this point is
	int point = 0;
};

class VisualToolVectorClip final : public VisualTool<VisualToolVectorClipDraggableFeature> {
	/// 单个对话行的矢量裁剪状态
	struct LineState {
		explicit LineState(VisualToolVectorClip *tool) : spline(tool) {}
		Spline spline; ///< 该行的裁剪路径
		bool inverse = false; /// is iclip?
	};

	/// 选中及活动可见对话行到矢量裁剪状态的映射
	std::map<AssDialogue*, LineState> line_states;

	wxToolBar *toolBar = nullptr; /// The subtoolbar
	int mode = VCLIP_DRAG; /// 0-7

	std::set<Feature *> box_added;

	void Save();
	void Commit(wxString message="") override;

	void AddTool(std::string command_name, VisualToolVectorClipMode mode);

	void MakeFeature(Spline& spline, AssDialogue *line, size_t idx);
	/// @brief 重建指定行的特征（spline 变化后调用）
	void RebuildLineFeatures(AssDialogue *line);
	/// @brief 重建所有选中及活动可见行的状态与特征
	void RebuildFeatures();

	bool InitializeHold() override;
	void UpdateHold() override;

	void UpdateDrag(Feature *feature) override;
	void EndDrag(Feature *feature) override { DoRefresh(); }
	bool InitializeDrag(Feature *feature) override;

	void DoRefresh() override;
	void OnSelectionChanged() override;
	// 活动行切换不改变特征集（由选中集决定），避免点击特征时重建销毁拖拽目标
	void OnLineChanged() override { }
	void OnFrameChanged() override { RebuildFeatures(); }
	void Draw() override;

public:
	VisualToolVectorClip(VideoDisplay *parent, agi::Context *context);
	void SetToolbar(wxToolBar *tb) override;

	void SetSubTool(int subtool) override;
	int GetSubTool() override;

protected:
	/// @brief 绘制类子工具允许方向键终止绘制并跳行；框选（VCLIP_DRAG）除外，
	///        避免终止流程使跳过保存保护失效而覆写选中行矢量数据
	bool CanFinishHold() const override { return mode != VCLIP_DRAG; }
};
