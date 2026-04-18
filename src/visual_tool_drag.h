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

/// @file visual_tool_drag.h
/// @see visual_tool_drag.cpp
/// @ingroup visual_ts
///

#include "visual_feature.h"
#include "visual_tool.h"

/// @class VisualToolDragDraggableFeature
/// @brief VisualDraggableFeature with a time value
class VisualToolDragDraggableFeature final : public VisualDraggableFeature {
public:
	int time = 0;
	VisualToolDragDraggableFeature *parent = nullptr;
};

class wxBitmapButton;
class wxCommandEvent;
class wxToolBar;

enum class DragFeatureRole {
	None,
	Start,
	End,
	Origin,
};

/// @brief 将拖拽句柄类型归一化为联动分组角色
inline DragFeatureRole GetDragFeatureRole(DraggableFeatureType type) {
	switch (type) {
		case DRAG_BIG_SQUARE:
			return DragFeatureRole::Start;
		case DRAG_BIG_CIRCLE:
			return DragFeatureRole::End;
		case DRAG_BIG_TRIANGLE:
			return DragFeatureRole::Origin;
		default:
			return DragFeatureRole::None;
	}
}

namespace visual_tool_drag_detail {

template <typename FeatureRange, typename FeaturePtr, typename IsLineSelected, typename OnMatched>
bool CollectGroupedDragSelection(FeatureRange& features, FeaturePtr clicked_feature,
	IsLineSelected&& is_line_selected, OnMatched&& on_matched) {
	if (!clicked_feature || !is_line_selected(clicked_feature->line))
		return false;

	const DragFeatureRole role = GetDragFeatureRole(clicked_feature->type);
	if (role == DragFeatureRole::None)
		return false;

	for (auto& candidate : features) {
		if (is_line_selected(candidate.line) && GetDragFeatureRole(candidate.type) == role)
			on_matched(candidate);
	}
	return true;
}

}

/// @class VisualToolDrag
/// @brief Moveable features for the positions of each visible line
class VisualToolDrag final : public VisualTool<VisualToolDragDraggableFeature> {
	/// The subtoolbar for the move/pos conversion button
	wxToolBar *toolbar;
	/// The feature last clicked on for the double-click handler
	/// Equal to curFeature during drags; possibly different at all other times
	/// nullptr if no features have been clicked on or the last clicked on one no
	/// longer exists
	Feature *primary = nullptr;
	/// The last announced selection set
	std::set<AssDialogue *> selection;

	/// When the button is pressed, will it convert the line to a move (vs. from
	/// move to pos)? Used to avoid changing the button's icon unnecessarily
	bool button_is_move = false;

	/// @brief Create the features for a line
	/// @param diag Line to create the features for
	/// @param pos Insertion point in the feature list
	void MakeFeatures(AssDialogue *diag, feature_list::iterator pos);
	void MakeFeatures(AssDialogue *diag);
	Feature *GetFeatureAt(Vector2D const& mouse_pos);
	bool IsLineSelected(AssDialogue *line) const;
	void SetSelectedFeaturesForClickedFeature(Feature *feature);

	void OnSelectedSetChanged();

	void OnFrameChanged() override;
	void OnFileChanged() override;
	void OnLineChanged() override;
	void OnCoordinateSystemsChanged() override { OnFileChanged(); }
	void OnMouseEvent(wxMouseEvent &event) override;

	bool InitializeDrag(Feature *feature) override;
	void UpdateDrag(Feature *feature) override;
	void Draw() override;
	void OnDoubleClick() override;

	/// Set the pos/move button to the correct icon based on the active line
	void UpdateToggleButtons();
	void OnSubTool(wxCommandEvent &event);
public:
	VisualToolDrag(VideoDisplay *parent, agi::Context *context);
	void SetToolbar(wxToolBar *tb) override;
};
