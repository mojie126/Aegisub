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

/// @file visual_tool_rotatez.cpp
/// @brief 2D rotation in Z axis visual typesetting tool
/// @ingroup visual_ts

#include "visual_tool_rotatez.h"

#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "selection_controller.h"
#include "video_display.h"

#include <libaegisub/format.h>

#include <cmath>
#include <wx/colour.h>

static const float deg2rad = 3.1415926536f / 180.f;
static const float rad2deg = 180.f / 3.1415926536f;

VisualToolRotateZ::VisualToolRotateZ(VideoDisplay *parent, agi::Context *context)
: VisualTool<VisualDraggableFeature>(parent, context)
{
	RebuildFeatures();
}

void VisualToolRotateZ::RebuildFeatures() {
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
		state.org = new Feature;
		state.org->type = DRAG_BIG_TRIANGLE;
		state.org->line = line;
		features.push_back(*state.org);

		state.pos = FromScriptCoords(GetLinePosition(line));
		if (!(state.org->pos = GetLineOrigin(line)))
			state.org->pos = state.pos;
		else
			state.org->pos = FromScriptCoords(state.org->pos);

		GetLineRotation(line, state.rotation_x, state.rotation_y, state.angle);
		GetLineScale(line, state.scale);

		line_states[line] = state;
	}
}

void VisualToolRotateZ::Draw() {
	if (!active_line) return;

	DrawAllFeatures();

	for (auto& [line, state] : line_states) {
		// 辅助视觉元素使用夹持后的原点，使圆环/标记在预览区边缘完整可见
		Vector2D clamped_org = ClampToVideo(state.org->pos, GetAnchorMargin(state.org->type, state.org->size));

		// 多行同屏时环/基线/标记按行区分颜色
		wxColour line_color = GetPerLineColor(line);
		wxColour line_color_secondary = GetPerLineOutlineColor(line);
		wxColour highlight_color = GetPerLineBaseColor(line);

		float radius = (state.pos - state.org->pos).Len();
		float oRadius = radius;
		if (radius < 50)
			radius = 50;

		// Set up the projection
		gl.SetOrigin(clamped_org);
		gl.SetRotation(state.rotation_x, state.rotation_y, 0);
		gl.SetScale(state.scale);

		// Draw the circle
		gl.SetLineColour(line_color_secondary);
		gl.SetFillColour(highlight_color, 0.3f);
		gl.DrawRing(Vector2D(0, 0), radius + 4, radius - 4);

		// Draw markers around circle
		int markers = 6;
		float markStart = -90.f / markers;
		float markEnd = markStart + (180.f / markers);
		for (int i = 0; i < markers; ++i) {
			float angle = i * (360.f / markers);
			gl.DrawRing(Vector2D(0, 0), radius+30, radius+12, 1.0, angle+markStart, angle+markEnd);
		}

		// Draw the baseline through the origin showing current rotation
		Vector2D angle_vec(Vector2D::FromAngle(state.angle * deg2rad));
		gl.SetLineColour(line_color, 1, 2);
		gl.DrawLine(angle_vec * -radius, angle_vec * radius);

		if (state.org->pos != state.pos) {
			Vector2D rotated_pos = Vector2D::FromAngle(state.angle * deg2rad - (state.pos - state.org->pos).Angle()) * oRadius;

			// Draw the line from origin to rotated position
			gl.DrawLine(Vector2D(), rotated_pos);

			// Draw the line under the text
			gl.DrawLine(rotated_pos - angle_vec * 20, rotated_pos + angle_vec * 20);
		}

		// Draw the fake features on the ring
		gl.SetLineColour(line_color_secondary, 1.f, 1);
		gl.SetFillColour(highlight_color, 0.3f);
		gl.DrawCircle(angle_vec * radius, 4);
		gl.DrawCircle(angle_vec * -radius, 4);

		// Clear the projection
		gl.ResetTransform();

		// 仅活动行绘制原点到鼠标的连线（若鼠标不在原点特征上）
		if (line == active_line && mouse_pos && (mouse_pos - state.org->pos).SquareLen() > 100) {
			gl.SetLineColour(line_color_secondary);
			gl.DrawLine(clamped_org, mouse_pos);
		}
	}
}

bool VisualToolRotateZ::InitializeHold() {
	auto it = line_states.find(active_line);
	if (it == line_states.end()) return false;
	LineState& state = it->second;

	state.orig_angle = state.angle + (state.org->pos - mouse_pos).Angle() * rad2deg;
	return true;
}

void VisualToolRotateZ::UpdateHold() {
	auto it = line_states.find(active_line);
	if (it == line_states.end()) return;
	LineState& state = it->second;

	state.angle = state.orig_angle - (state.org->pos - mouse_pos).Angle() * rad2deg;

	if (ctrl_down)
		state.angle = floorf(state.angle / 30.f + .5f) * 30.f;

	state.angle = fmodf(state.angle + 360.f, 360.f);

	SetSelectedOverride("\\frz", agi::format("%.4g", state.angle));
}

void VisualToolRotateZ::UpdateDrag(Feature *feature) {
	if (!feature->line) return;
	auto org = GetLineOrigin(feature->line);
	if (!org) org = GetLinePosition(feature->line);
	auto d = ToScriptCoords(feature->pos) - org;

	for (auto line : c->selectionController->GetSelectedSet()) {
		org = GetLineOrigin(line);
		if (!org) org = GetLinePosition(line);
		Vector2D new_org = d + org;
		SetOverride(line, "\\org", new_org.PStr());
		// 同步更新该行的原点特征位置，避免拖动中视觉过期
		auto it = line_states.find(line);
		if (it != line_states.end())
			it->second.org->pos = FromScriptCoords(new_org);
	}
}

void VisualToolRotateZ::DoRefresh() {
	RebuildFeatures();
}

void VisualToolRotateZ::OnSelectionChanged() {
	RebuildFeatures();
	parent->Render();
}
