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

/// @file visual_tool_clip.cpp
/// @brief Rectangular clipping visual typesetting tool
/// @ingroup visual_ts

#include "visual_tool_clip.h"

#include "ass_dialogue.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "selection_controller.h"
#include "video_display.h"

#include <libaegisub/format.h>

#include <wx/colour.h>

VisualToolClip::VisualToolClip(VideoDisplay *parent, agi::Context *context)
: VisualTool<ClipCorner>(parent, context)
{
	RebuildFeatures();
}

void VisualToolClip::RebuildFeatures() {
	features.clear();
	line_states.clear();
	sel_features.clear();
	active_feature = nullptr;

	// 选中集联合活动行，保证活动行不在选中集时工具仍可用
	auto lines = c->selectionController->GetSelectedSet();
	if (active_line) lines.insert(active_line);

	for (auto line : lines) {
		if (!IsDisplayed(line)) continue;

		LineState state;
		ClipCorner *feats[4];
		for (auto& feat : feats) {
			feat = new ClipCorner;
			feat->line = line;
			features.push_back(*feat);
		}

		// Attach each feature to the two features it shares edges with
		// Top-left
		int i = 0;
		feats[i]->horiz = feats[1];
		feats[i]->vert = feats[2];
		i++;

		// Top-right
		feats[i]->horiz = feats[0];
		feats[i]->vert = feats[3];
		i++;

		// Bottom-left
		feats[i]->horiz = feats[3];
		feats[i]->vert = feats[0];
		i++;

		// Bottom-right
		feats[i]->horiz = feats[2];
		feats[i]->vert = feats[1];

		for (int j = 0; j < 4; j++)
			state.corners[j] = feats[j];

		RefreshLineState(state, line);
		line_states[line] = state;
	}
}

void VisualToolClip::RefreshLineState(LineState& state, AssDialogue *line) {
	GetLineClip(line, state.cur_1, state.cur_2, state.inverse);
	state.cur_1 = FromScriptCoords(state.cur_1);
	state.cur_2 = FromScriptCoords(state.cur_2);
	SetFeaturePositions(state);
}

void VisualToolClip::Draw() {
	if (!active_line) return;

	DrawAllFeatures();

	// Load colors from options
	const auto shaded_alpha = static_cast<float>(shaded_area_alpha_opt->GetDouble());

	for (auto& [line, state] : line_states) {
		// 多行同屏时矩形边框按行区分颜色
		const wxColour line_color = GetPerLineColor(line);

		// Draw rectangle
		gl.SetLineColour(line_color, 1.0f, 2);
		gl.SetFillColour(line_color, 0.0f);
		gl.DrawRectangle(state.cur_1, state.cur_2);

		// Draw outside area
		gl.SetLineColour(line_color, 0.0f);
		gl.SetFillColour(*wxBLACK, shaded_alpha);
		if (state.inverse) {
			gl.DrawRectangle(state.cur_1, state.cur_2);
		}
		else {
			Vector2D v_min = video_pos;
			Vector2D v_max = video_pos + video_res;
			Vector2D c_min = state.cur_1.Min(state.cur_2);
			Vector2D c_max = state.cur_1.Max(state.cur_2);
			gl.DrawRectangle(v_min,                  Vector2D(v_max, c_min));
			gl.DrawRectangle(Vector2D(v_min, c_max), v_max);
			gl.DrawRectangle(Vector2D(v_min, c_min), Vector2D(c_min, c_max));
			gl.DrawRectangle(Vector2D(c_max, c_min), Vector2D(v_max, c_max));
		}
	}
}

bool VisualToolClip::InitializeHold() {
	return true;
}

bool VisualToolClip::InitializeDrag(ClipCorner *) {
	return true;
}

void VisualToolClip::UpdateHold() {
	if (!active_line) return;
	auto it = line_states.find(active_line);
	if (it == line_states.end()) return;
	LineState& state = it->second;

	// Limit to video area
	state.cur_1 = video_pos.Max((video_pos + video_res).Min(drag_start));
	state.cur_2 = video_pos.Max((video_pos + video_res).Min(mouse_pos));

	SetFeaturePositions(state);
	CommitHold(state);
}

void VisualToolClip::CommitHold(LineState& state) {
	std::string value = agi::format("(%s,%s)", ToScriptCoords(state.cur_1.Min(state.cur_2)).Str(), ToScriptCoords(state.cur_1.Max(state.cur_2)).Str());

	for (auto line : c->selectionController->GetSelectedSet()) {
		// This check is technically not correct as it could be outside of an
		// override block... but that's rather unlikely
		bool has_iclip = line->Text.get().find("\\iclip") != std::string::npos;
		SetOverride(line, has_iclip ? "\\iclip" : "\\clip", value);
	}

	// 广播编辑后同步其他选中行的显示状态，避免拖动中视觉过期
	for (auto& [line, s] : line_states) {
		if (&s == &state) continue;
		s.cur_1 = state.cur_1;
		s.cur_2 = state.cur_2;
		SetFeaturePositions(s);
	}
}

void VisualToolClip::UpdateDrag(ClipCorner *feature) {
	if (!feature->line) return;
	auto it = line_states.find(feature->line);
	if (it == line_states.end()) return;
	LineState& state = it->second;

	// Update features which share an edge with the dragged one
	feature->horiz->pos = Vector2D(feature->horiz->pos, feature->pos);
	feature->vert->pos = Vector2D(feature->pos, feature->vert->pos);

	state.cur_1 = state.corners[0]->pos;
	state.cur_2 = state.corners[3]->pos;

	CommitHold(state);
}

void VisualToolClip::SetFeaturePositions(LineState& state) {
	state.corners[0]->pos = state.cur_1; // Top-left
	state.corners[1]->pos = Vector2D(state.cur_2, state.cur_1); // Top-right
	state.corners[2]->pos = Vector2D(state.cur_1, state.cur_2); // Bottom-left
	state.corners[3]->pos = state.cur_2; // Bottom-right
}

void VisualToolClip::DoRefresh() {
	RebuildFeatures();
}

void VisualToolClip::OnSelectionChanged() {
	RebuildFeatures();
	parent->Render();
}
