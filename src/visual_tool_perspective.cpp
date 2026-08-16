// Copyright (c) 2022, arch1t3cht <arch1t3cht@gmail.com>
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

/// @file visual_tool_perspective.cpp
/// @brief 3D perspective visual typesetting tool
/// @ingroup visual_ts

#include "visual_tool_perspective.h"

#include "command/command.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "selection_controller.h"
#include "vector3d.h"
#include "ass_file.h"
#include "ass_dialogue.h"
#include "ass_style.h"
#include "video_display.h"

#include <libaegisub/format.h>
#include <libaegisub/split.h>
#include <libaegisub/util.h>

#include <libaegisub/log.h>

#include <cmath>
#include <wx/colour.h>
#include <wx/toolbar.h>

static const float pi = 3.1415926536f;
static const float deg2rad = pi / 180.f;
static const float rad2deg = 180.f / pi;
static const float default_screen_z = 312.5;
static const char *ambient_plane_key = "_aegi_perspective_ambient_plane";

static const int BUTTON_ID_BASE = 1400;

enum VisualToolPerspectiveFeatureType {
	FEATURE_INNER = 0,
	FEATURE_OUTER = 1,
	FEATURE_CENTER = 2,
	FEATURE_ORG = 3,
};

void Solve2x2(float a11, float a12, float a21, float a22, float b1, float b2, float &x1, float &x2) {
	// Simple pivoting
	if (abs(a11) < abs(a21)) {
		std::swap(b1, b2);
		std::swap(a11, a21);
		std::swap(a12, a22);
	}
	// LU decomposition
	// i = 1
	a21 = a21 / a11;
	// i = 2
	a22 = a22 - a21 * a12;
	// forward substitution
	float z1 = b1;
	float z2 = b2 - a21 * z1;
	// backward substitution
	x2 = z2 / a22;
	x1 = (z1 - a12 * x2) / a11;
}

Vector2D QuadMidpoint(std::vector<Vector2D> quad) {
	Vector2D diag1 = quad[2] - quad[0];
	Vector2D diag2 = quad[1] - quad[3];
	Vector2D b = quad[3] - quad[0];
	float center_la1, center_la2;
	Solve2x2(diag1.X(), diag2.X(), diag1.Y(), diag2.Y(), b.X(), b.Y(), center_la1, center_la2);
	return quad[0] + center_la1 * diag1;
}

void UnwrapQuadRel(std::vector<Vector2D> quad, float &x1, float &x2, float &x3, float &x4, float &y1, float &y2, float &y3, float &y4) {
	x1 = quad[0].X();
	x2 = quad[1].X() - x1;
	x3 = quad[2].X() - x1;
	x4 = quad[3].X() - x1;
	y1 = quad[0].Y();
	y2 = quad[1].Y() - y1;
	y3 = quad[2].Y() - y1;
	y4 = quad[3].Y() - y1;
}

Vector2D XYToUV(std::vector<Vector2D> quad, Vector2D xy) {
	float x1, x2, x3, x4, y1, y2, y3, y4;
	UnwrapQuadRel(quad, x1, x2, x3, x4, y1, y2, y3, y4);
	float x = xy.X() - x1;
	float y = xy.Y() - y1;
	// Dumped from Mathematica
	float u = -(((x3*y2 - x2*y3)*(x4*y - x*y4)*(x4*(-y2 + y3) + x3*(y2 - y4) + x2*(-y3 + y4)))/(x3*x3*(x4*y2*y2*(-y + y4) + y4*(x*y2*(y2 - y4) + x2*(y - y2)*y4)) + x3*(x4*x4*y2*y2*(y - y3) + 2*x4*(x2*y*y3*(y2 - y4) + x*y2*(-y2 + y3)*y4) + x2*y4*(x2*(-y + y3)*y4 + 2*x*y2*(-y3 + y4))) + y3*(x*x4*x4*y2*(y2 - y3) + x2*x4*x4*(y2*y3 + y*(-2*y2 + y3)) - x2*x2*(x4*y*(y3 - 2*y4) + x4*y3*y4 + x*y4*(-y3 + y4)))));
	float v = ((x2*y - x*y2)*(x4*y3 - x3*y4)*(x4*(y2 - y3) + x2*(y3 - y4) + x3*(-y2 + y4)))/(x3*(x4*x4*y2*y2*(-y + y3) + x2*y4*(2*x*y2*(y3 - y4) + x2*(y - y3)*y4) - 2*x4*(x2*y*y3*(y2 - y4) + x*y2*(-y2 + y3)*y4)) + x3*x3*(x4*y2*y2*(y - y4) + y4*(x2*(-y + y2)*y4 + x*y2*(-y2 + y4))) + y3*(x*x4*x4*y2*(-y2 + y3) + x2*x4*x4*(2*y*y2 - y*y3 - y2*y3) + x2*x2*(x4*y*(y3 - 2*y4) + x4*y3*y4 + x*y4*(-y3 + y4))));
	return Vector2D(u, v);
}

Vector2D UVToXY(std::vector<Vector2D> quad, Vector2D uv) {
	float x1, x2, x3, x4, y1, y2, y3, y4;
	UnwrapQuadRel(quad, x1, x2, x3, x4, y1, y2, y3, y4);
	float u = uv.X();
	float v = uv.Y();
	// Also dumped from Mathematica
	float d = (x4*((-1 + u + v)*y2 + y3 - v*y3) + x3*(y2 - u*y2 + (-1 + v)*y4) + x2*((-1 + u)*y3 - (-1 + u + v)*y4));
	float x = (v*x4*(x3*y2 - x2*y3) + u*x2*(x4*y3 - x3*y4)) / d;
	float y = (v*y4*(x3*y2 - x2*y3) + u*y2*(x4*y3 - x3*y4)) / d;
	return Vector2D(x + x1, y + y1);
}

std::vector<Vector2D> MakeRect(Vector2D a, Vector2D b) {
	return std::vector<Vector2D>({
		Vector2D(a.X(), a.Y()),
		Vector2D(b.X(), a.Y()),
		Vector2D(b.X(), b.Y()),
		Vector2D(a.X(), b.Y()),
	});
}

inline float VisualToolPerspective::screenZ() const {
	return default_screen_z * script_res.Y() / layout_res.Y();
}

void VisualToolPerspective::AddTool(std::string command_name, VisualToolPerspectiveSetting setting) {
	cmd::Command *command = cmd::get(command_name);
	int icon_size = OPT_GET("App/Toolbar Icon Size")->GetInt();
	toolBar->AddTool(BUTTON_ID_BASE + setting, command->StrDisplay(c), command->Icon(icon_size), command->GetTooltip("Video"), wxITEM_CHECK);
}

VisualToolPerspective::VisualToolPerspective(VideoDisplay *parent, agi::Context *context)
: VisualTool<VisualToolPerspectiveDraggableFeature>(parent, context)
, optOuter(OPT_SET("Tool/Visual/Perspective/Outer"))
, optOuterLocked(OPT_SET("Tool/Visual/Perspective/Outer Locked"))
, optGrid(OPT_SET("Tool/Visual/Perspective/Grid"))
, optOrgMode(OPT_SET("Tool/Visual/Perspective/Org Mode"))
{
	settings = 0;
	if (optOuter->GetBool()) settings |= PERSP_OUTER;
	if (optOuterLocked->GetBool()) settings |= PERSP_LOCK_OUTER;
	if (optGrid->GetBool()) settings |= PERSP_GRID;
	settings |= optOrgMode->GetInt();

	RebuildFeatures();
}

void VisualToolPerspective::SetToolbar(wxToolBar *toolBar) {
	this->toolBar = toolBar;

	toolBar->AddSeparator();

	AddTool("video/tool/perspective/plane", PERSP_OUTER);
	AddTool("video/tool/perspective/lock_outer", PERSP_LOCK_OUTER);
	AddTool("video/tool/perspective/grid", PERSP_GRID);
	AddTool("video/tool/perspective/orgmode/center", PERSP_ORGMODE);

	SetSubTool(settings);

	toolBar->Realize();
	toolBar->Show(true);
	toolBar->Bind(wxEVT_TOOL, &VisualToolPerspective::OnSubTool, this);
}

void VisualToolPerspective::OnSubTool(wxCommandEvent &e) {
	int id = e.GetId() - BUTTON_ID_BASE;
	if (id == PERSP_ORGMODE) {
		cmd::call("video/tool/perspective/orgmode/cycle", c);
	} else {
		SetSubTool(GetSubTool() ^ id);
	}
}

void VisualToolPerspective::SetSubTool(int subtool) {
	if (toolBar == nullptr) {
		throw agi::InternalError("Vector clip toolbar hasn't been set yet!");
	}
	for (int i = 1; i < PERSP_LAST; i <<= 1)
		toolBar->ToggleTool(BUTTON_ID_BASE + i, i & subtool);

	toolBar->EnableTool(BUTTON_ID_BASE + PERSP_LOCK_OUTER, subtool & PERSP_OUTER);

	cmd::Command *orgmode;
	switch (subtool & PERSP_ORGMODE) {
		case PERSP_ORGMODE_CENTER:
			orgmode = cmd::get("video/tool/perspective/orgmode/center");
			break;
		case PERSP_ORGMODE_NOFAX:
			orgmode = cmd::get("video/tool/perspective/orgmode/nofax");
			break;
		case PERSP_ORGMODE_KEEP:
			orgmode = cmd::get("video/tool/perspective/orgmode/keep");
			break;
		default:
			throw agi::InternalError("Invalid perspective subtool");
	}
	wxString orgmodehelp = orgmode->StrDisplay(c) + wxString(_(". Click to cycle.\n")) + orgmode->GetTooltip("Video");
	toolBar->SetToolShortHelp(BUTTON_ID_BASE + PERSP_ORGMODE, orgmodehelp);
	toolBar->SetToolLongHelp(BUTTON_ID_BASE + PERSP_ORGMODE, orgmodehelp);
	toolBar->SetToolNormalBitmap(BUTTON_ID_BASE + PERSP_ORGMODE, orgmode->Icon(OPT_GET("App/Toolbar Icon Size")->GetInt()));
	toolBar->ToggleTool(BUTTON_ID_BASE + PERSP_ORGMODE, false);

	settings = subtool;

	optOuter->SetBool(HasOuter());
	optOuterLocked->SetBool(OuterLocked());
	optGrid->SetBool(settings & PERSP_GRID);
	optOrgMode->SetInt(GetOrgMode());

	RebuildFeatures();
	parent->Render();
}

int VisualToolPerspective::GetSubTool() {
	return settings;
}

bool VisualToolPerspective::HasOuter() {
	return GetSubTool() & PERSP_OUTER;
}

bool VisualToolPerspective::OuterLocked() {
	return HasOuter() && (GetSubTool() & PERSP_LOCK_OUTER);
}

int VisualToolPerspective::GetOrgMode() {
	return GetSubTool() & PERSP_ORGMODE;
}

bool VisualToolPerspective::HasOrgf() {
	return GetOrgMode() == PERSP_ORGMODE_KEEP;
}

VisualToolPerspective::LineState& VisualToolPerspective::GetLineState(AssDialogue *line) {
	auto it = line_states.find(line);
	if (it != line_states.end()) return it->second;
	// 特征行未命中时回退到活动行状态，两者均无时取映射中的第一个状态
	// 正常流程下特征与状态同建同销，此回退仅作防御
	if (active_line) {
		it = line_states.find(active_line);
		if (it != line_states.end()) return it->second;
	}
	return line_states.begin()->second;
}

std::vector<Vector2D> VisualToolPerspective::FeaturePositions(std::vector<Feature *> features) const {
	std::vector<Vector2D> result;
	for (size_t i = 0; i < 4; i++) {
		result.push_back(features[i]->pos);
	}
	return result;
}

void VisualToolPerspective::UpdateInner(LineState& state) {
	std::vector<Vector2D> uv = MakeRect(state.c1, state.c2);
	std::vector<Vector2D> quad = FeaturePositions(state.outer_corners);
	for (int i = 0; i < 4; i++)
		state.inner_corners[i]->pos = UVToXY(quad, uv[i]);
}

void VisualToolPerspective::UpdateOuter(LineState& state) {
	if (!HasOuter())
		return;
	std::vector<Vector2D> uv = MakeRect(-state.c1 / (state.c2 - state.c1), (1 - state.c1) / (state.c2 - state.c1));
	std::vector<Vector2D> quad = FeaturePositions(state.inner_corners);
	for (int i = 0; i < 4; i++)
		state.outer_corners[i]->pos = UVToXY(quad, uv[i]);
}

void VisualToolPerspective::RebuildFeatures() {
	sel_features.clear();
	features.clear();
	line_states.clear();
	active_feature = nullptr;

	// 选中集联合活动行，保证活动行不在选中集时工具仍可用
	auto lines = c->selectionController->GetSelectedSet();
	if (active_line) lines.insert(active_line);

	for (auto line : lines) {
		if (!IsDisplayed(line)) continue;

		LineState state;
		state.old_outer.resize(4);
		state.old_inner.resize(4);

		state.centerf = new Feature(this, FEATURE_CENTER, 0);
		state.centerf->type = DRAG_BIG_TRIANGLE;
		state.centerf->line = line;
		features.push_back(*state.centerf);

		if (HasOrgf()) {
			state.orgf = new Feature(this, FEATURE_ORG, 0);
			state.orgf->type = DRAG_BIG_TRIANGLE;
			state.orgf->line = line;
			features.push_back(*state.orgf);
		}

		for (int i = 0; i < 4; i++) {
			state.inner_corners.push_back(new Feature(this, FEATURE_INNER, i));
			state.inner_corners.back()->type = DRAG_SMALL_CIRCLE;
			state.inner_corners.back()->line = line;
			features.push_back(*state.inner_corners.back());

			if (HasOuter()) {
				state.outer_corners.push_back(new Feature(this, FEATURE_OUTER, i));
				state.outer_corners.back()->type = DRAG_SMALL_CIRCLE;
				state.outer_corners.back()->line = line;
				features.push_back(*state.outer_corners.back());
			}
		}

		TextToPersp(state, line);
		SetFeaturePositions(state);
		SaveFeaturePositions(state);

		line_states[line] = std::move(state);
	}
}

void VisualToolPerspective::Draw() {
	if (!active_line) return;

	// 四边形先于 DrawAllFeatures 取色，需先填充按行色相映射保证首帧颜色正确
	UpdateLineHueMap();

	// 为每个选中行绘制四边形
	for (auto& [line, state] : line_states) {
		// 多行同屏时四边形按行区分颜色
		wxColour line_color = GetPerLineColor(line);

		gl.SetLineColour(line_color);
		for (int i = 0; i < 4; i++) {
			if (HasOuter()) {
				int m1 = GetAnchorMargin(state.outer_corners[i]->type, state.outer_corners[i]->size);
				int m2 = GetAnchorMargin(state.outer_corners[(i + 1) % 4]->type, state.outer_corners[(i + 1) % 4]->size);
				Vector2D oc1 = ClampToVideo(state.outer_corners[i]->pos, m1);
				Vector2D oc2 = ClampToVideo(state.outer_corners[(i + 1) % 4]->pos, m2);
				gl.DrawDashedLine(oc1, oc2, 6);
				int m3 = GetAnchorMargin(state.inner_corners[i]->type, state.inner_corners[i]->size);
				int m4 = GetAnchorMargin(state.inner_corners[(i + 1) % 4]->type, state.inner_corners[(i + 1) % 4]->size);
				Vector2D ic1 = ClampToVideo(state.inner_corners[i]->pos, m3);
				Vector2D ic2 = ClampToVideo(state.inner_corners[(i + 1) % 4]->pos, m4);
				gl.DrawLine(ic1, ic2);
			} else {
				int m1 = GetAnchorMargin(state.inner_corners[i]->type, state.inner_corners[i]->size);
				int m2 = GetAnchorMargin(state.inner_corners[(i + 1) % 4]->type, state.inner_corners[(i + 1) % 4]->size);
				Vector2D ic1 = ClampToVideo(state.inner_corners[i]->pos, m1);
				Vector2D ic2 = ClampToVideo(state.inner_corners[(i + 1) % 4]->pos, m2);
				gl.DrawDashedLine(ic1, ic2, 6);
			}
		}
	}

	DrawAllFeatures();

	// 网格仅对活动行绘制，避免多行网格叠加干扰
	if (GetSubTool() & PERSP_GRID) {
		auto it = line_states.find(active_line);
		if (it == line_states.end()) return;
		LineState& state = it->second;

		// Draw Grid - Copied and modified from visual_tool_rotatexy.cpp

		// Number of lines on each side of each axis
		static const int radius = 15;
		// Total number of lines, including center axis line
		static const int line_count = radius * 2 + 1;
		// Distance between each line in pixels
		static const int spacing = 20;
		// Length of each grid line in pixels from axis to one end
		static const int half_line_length = spacing * (radius + 1);
		static const float fade_factor = 0.9f / radius;

		// 多行同屏时网格颜色跟随活动行颜色
		wxColour line_color_secondary = GetPerLineOutlineColor(active_line);

		// Transform grid
		gl.SetOrigin(ClampToVideo(FromScriptCoords(state.org), GetAnchorMargin(state.centerf->type, state.centerf->size)));
		gl.SetScale(100 * video_res / script_res);
		gl.SetRotation(state.angle_x, state.angle_y, state.angle_z, script_res.Y() / layout_res.Y());
		gl.SetScale(state.fsc);
		gl.SetShear(state.fax, state.fay);
		Vector2D glScale = (state.bbox.second.Y() - state.bbox.first.Y()) * Vector2D(1, 1) / spacing / 4;
		gl.SetScale(100 * glScale);

		// 延迟初始化网格基础数据（仅首次分配）
		if (!grid_initialized) {
			grid_base_points.resize(line_count * 8 * 2);
			grid_base_alphas.resize(line_count * 8);
			for (int i = 0; i < line_count; ++i) {
				int pos = spacing * (i - radius);
				grid_base_points[i * 16 + 0] = static_cast<float>(pos);
				grid_base_points[i * 16 + 1] = static_cast<float>(half_line_length);
				grid_base_points[i * 16 + 2] = static_cast<float>(pos);
				grid_base_points[i * 16 + 3] = 0;
				grid_base_points[i * 16 + 4] = static_cast<float>(pos);
				grid_base_points[i * 16 + 5] = 0;
				grid_base_points[i * 16 + 6] = static_cast<float>(pos);
				grid_base_points[i * 16 + 7] = static_cast<float>(-half_line_length);
				grid_base_points[i * 16 + 8] = static_cast<float>(half_line_length);
				grid_base_points[i * 16 + 9] = static_cast<float>(pos);
				grid_base_points[i * 16 + 10] = 0;
				grid_base_points[i * 16 + 11] = static_cast<float>(pos);
				grid_base_points[i * 16 + 12] = 0;
				grid_base_points[i * 16 + 13] = static_cast<float>(pos);
				grid_base_points[i * 16 + 14] = static_cast<float>(-half_line_length);
				grid_base_points[i * 16 + 15] = static_cast<float>(pos);
			}
			for (int i = 0; i < line_count * 8; ++i) {
				grid_base_alphas[i] = (i + 3) % 4 > 1 ? 0 : (1.f - abs(i / 8 - radius) * fade_factor);
			}
			grid_initialized = true;
		}

		// Draw grid - 使用预计算的基础数据，仅更新偏移和颜色
		gl.SetLineColour(line_color_secondary, 0.5f, 2);
		gl.SetModeLine();
		float r = line_color_secondary.Red() / 255.f;
		float g = line_color_secondary.Green() / 255.f;
		float b = line_color_secondary.Blue() / 255.f;

		std::vector<float> colors(line_count * 8 * 4);
		for (int i = 0; i < line_count * 8; ++i) {
			colors[i * 4 + 0] = r;
			colors[i * 4 + 1] = g;
			colors[i * 4 + 2] = b;
			colors[i * 4 + 3] = grid_base_alphas[i];
		}

		// 复制基础点并应用偏移
		std::vector<float> points(grid_base_points);
		Vector2D offset = (ToScriptCoords(QuadMidpoint(FeaturePositions(state.inner_corners))) - state.org) / glScale;
		for (int i = 0; i < line_count * 8; ++i) {
			points[i * 2 + 0] += offset.X();
			points[i * 2 + 1] += offset.Y();
		}

		gl.DrawLines(2, points, 4, colors);

		gl.ResetTransform();
	}
}

void VisualToolPerspective::OnDoubleClick() {
	// 在所有选中行的角点中寻找最近者，作用于其所属行
	Feature *closest = nullptr;
	float mind = -1;
	for (auto& [line, state] : line_states) {
		std::vector<Feature *> active_features = (HasOuter() && !OuterLocked()) ? state.outer_corners : state.inner_corners;
		for (auto feature : active_features) {
			float d = (feature->pos - mouse_pos).Len();
			if (!closest || d < mind) {
				closest = feature;
				mind = d;
			}
		}
	}
	if (!closest) return;
	closest->pos = mouse_pos;
	UpdateDrag(closest);
	Commit();
}

void VisualToolPerspective::OnMouseEvent(wxMouseEvent &event) {
	// Override this so we can find out which modifier keys were held
	shift_down = event.ShiftDown();
	ctrl_down = event.CmdDown();
	alt_down = event.AltDown();
	VisualTool<Feature>::OnMouseEvent(event);
	shift_down = false;
	ctrl_down = false;
	alt_down = false;
};

void VisualToolPerspective::UpdateDrag(Feature *feature) {
	LineState& state = GetLineState(feature->line);

	if (feature == state.centerf) {
		Vector2D oldCenter = QuadMidpoint(FeaturePositions(state.inner_corners));
		if (HasOuter() && !OuterLocked()) {
			std::vector<Vector2D> quad = FeaturePositions(state.outer_corners);
			Vector2D olduv = XYToUV(quad, oldCenter);
			Vector2D newuv = XYToUV(quad, state.centerf->pos);
			state.c1 = state.c1 + newuv - olduv;
			state.c2 = state.c2 + newuv - olduv;
			UpdateInner(state);
		} else {
			Vector2D diff = state.centerf->pos - oldCenter;
			for (int i = 0; i < 4; i++) {
				state.inner_corners[i]->pos = state.inner_corners[i]->pos + diff;
			}
			UpdateOuter(state);
		}
	} else if (HasOrgf() && feature == state.orgf) {
		state.org = ToScriptCoords(feature->pos);
	}

	std::vector<Feature *> changed_quad;
	std::vector<Vector2D> changed_quad_old;
	if (feature->group == FEATURE_INNER) {
		changed_quad = state.inner_corners;
		changed_quad_old = state.old_inner;
	} else if (HasOuter() && feature->group == FEATURE_OUTER) {
		changed_quad = state.outer_corners;
		changed_quad_old = state.old_outer;
	}

	if (!changed_quad.empty() && !ctrl_down) {
		// Validate: If the quad isn't convex, the intersection of the diagonals will not lie inside it.
		Vector2D diag1 = changed_quad[2]->pos - changed_quad[0]->pos;
		Vector2D diag2 = changed_quad[1]->pos - changed_quad[3]->pos;
		Vector2D b = changed_quad[3]->pos - changed_quad[0]->pos;
		float center_la1, center_la2;
		Solve2x2(diag1.X(), diag2.X(), diag1.Y(), diag2.Y(), b.X(), b.Y(), center_la1, center_la2);
		if (center_la1 < 0 || center_la1 > 1 || -center_la2 < 0 || -center_la2 > 1) {
			TextToPersp(state, feature->line);
			return;
		}
	}

	int i = feature->index;

	if (ctrl_down && !changed_quad.empty()) {
		if (alt_down) {
			if (shift_down) {
				int bestsnap = -1;
				float mindist = -1;
				for (int j = 0; j < 4; j++) {
					float dist = (feature->pos - changed_quad_old[j]).SquareLen();
					if (bestsnap == -1 || dist < mindist) {
						bestsnap = j;
						mindist = dist;
					}
				}
				feature->pos = changed_quad_old[bestsnap];
			} else {
				Vector2D center = QuadMidpoint(changed_quad_old);
				Vector2D diff = feature->pos - center;
				Vector2D snapDirection1 = (changed_quad_old[0] - center).Unit();
				Vector2D snapDirection2 = (changed_quad_old[1] - center).Unit();
				Vector2D snap1 = diff.Dot(snapDirection1) * snapDirection1;
				Vector2D snap2 = diff.Dot(snapDirection2) * snapDirection2;
				diff = (snap1 - diff).SquareLen() <= (snap2 - diff).SquareLen() ? snap1 : snap2;
				feature->pos = center + diff;
			}
		}

		Vector2D relUV = XYToUV(changed_quad_old, feature->pos) - Vector2D(0.5, 0.5);

		for (int j = 0; j < 4; j++) {
			Vector2D flipi(i == 1 || i == 2 ? -1 : 1, i >= 2 ? -1 : 1);
			Vector2D flipj(j == 1 || j == 2 ? -1 : 1, j >= 2 ? -1 : 1);
			changed_quad[j]->pos = UVToXY(changed_quad_old, Vector2D(0.5, 0.5) + relUV * flipi * flipj);
		}

		if (HasOuter()) {
			if (feature->group == FEATURE_INNER) {
				if (!OuterLocked()) {
					state.c1 = XYToUV(FeaturePositions(state.outer_corners), state.inner_corners[0]->pos);
					state.c2 = XYToUV(FeaturePositions(state.outer_corners), state.inner_corners[2]->pos);
					UpdateInner(state);
				} else {
					UpdateOuter(state);
				}
			} else if (feature->group == FEATURE_OUTER) {
				if (OuterLocked()) {
					state.c1 = XYToUV(FeaturePositions(state.outer_corners), state.inner_corners[0]->pos);
					state.c2 = XYToUV(FeaturePositions(state.outer_corners), state.inner_corners[2]->pos);
					UpdateOuter(state);
				} else {
					UpdateInner(state);
				}
			}
		}
	} else if (!changed_quad.empty() && HasOuter()) {
		// Normally dragging one corner
		if (feature->group == FEATURE_INNER) {
			if (!OuterLocked()) {
				Vector2D newuv = XYToUV(FeaturePositions(state.outer_corners), feature->pos);
				state.c1 = Vector2D(i == 0 || i == 3 ? newuv.X() : state.c1.X(), i < 2 ? newuv.Y() : state.c1.Y());
				state.c2 = Vector2D(i == 0 || i == 3 ? state.c2.X() : newuv.X(), i < 2 ? state.c2.Y() : newuv.Y());
				UpdateInner(state);
			} else {
				UpdateOuter(state);
			}
		} else if (feature->group == FEATURE_OUTER) {
			if (OuterLocked()) {
				Vector2D d1 = -state.c1 / (state.c2 - state.c1);
				Vector2D d2 = (1 - state.c1) / (state.c2 - state.c1);
				Vector2D newuv = XYToUV(FeaturePositions(state.inner_corners), feature->pos);
				d1 = Vector2D(i == 0 || i == 3 ? newuv.X() : d1.X(), i < 2 ? newuv.Y() : d1.Y());
				d2 = Vector2D(i == 0 || i == 3 ? d2.X() : newuv.X(), i < 2 ? d2.Y() : newuv.Y());
				state.c1 = -d1 / (d2 - d1);
				state.c2 = (1 - d1) / (d2 - d1);
				UpdateOuter(state);
			} else {
				UpdateInner(state);
			}
		}
	}

	if (!InnerToText(state))
		TextToPersp(state, feature->line);
	SetFeaturePositions(state);
}

void VisualToolPerspective::EndDrag(Feature *feature) {
	// 拖拽的特征行即活动行（点击特征时已切换），
	// 用活动行定位状态，避免 EndDrag 时特征已被重建销毁而悬垂
	if (!active_line) return;
	auto it = line_states.find(active_line);
	if (it == line_states.end()) return;
	SaveFeaturePositions(it->second);
	SaveOuterToLines(it->second);

	// 广播编辑后刷新所有行的状态，避免其他选中行显示过期值
	DoRefresh();
}

void VisualToolPerspective::WrapSetOverride(AssDialogue* line, std::string const& tag, float value, int precision, float defaultval) {
	std::string format = agi::format("%%.%df", precision);
	std::string formatted = agi::format(format.c_str(), value);
	std::string default_formatted = agi::format(format.c_str(), defaultval);
	if (formatted == default_formatted || (defaultval == 0 && agi::format(format.c_str(), -value) == default_formatted))
		RemoveOverride(line, tag);
	else
		SetOverride(line, tag, formatted);
}

bool VisualToolPerspective::InnerToText(LineState& state) {
	// 防御：状态未初始化（角点缺失）时直接失败
	if (state.inner_corners.size() < 4)
		return false;

	Vector2D q0 = ToScriptCoords(state.inner_corners[0]->pos);
	Vector2D q1 = ToScriptCoords(state.inner_corners[1]->pos);
	Vector2D q2 = ToScriptCoords(state.inner_corners[2]->pos);
	Vector2D q3 = ToScriptCoords(state.inner_corners[3]->pos);

	// Find a parallelogram projecting to the quad. This is independent of translation.
	float z1, z3;
	Vector2D diag = q2 - q0;
	Vector2D side2 = q1 - q2;
	Vector2D side3 = q3 - q2;
	Solve2x2(side2.X(), side3.X(), side2.Y(), side3.Y(), -diag.X(), -diag.Y(), z1, z3);

	Vector2D midpoint = QuadMidpoint(std::vector<Vector2D>({q0, q1, q2, q3}));

	if (GetOrgMode() == PERSP_ORGMODE_CENTER) {
		state.org = midpoint;
	} else if (GetOrgMode() == PERSP_ORGMODE_NOFAX) {
		Vector2D v1 = q1 - q0;
		Vector2D v3 = q3 - q0;
		// Look for a translation after which the quad will unproject to a rectangle.
		// Specifically, look for a vector t such that this happens after moving q0 to t.
		// The set of such vectors is cut out by the equation a (x^2 + y^2) - b1 x - b2 y + c
		// with the following coefficients.
		float a = (1 - z1) * (1 - z3);
		Vector2D b = z1 * v1 + z3 * v3 - z1 * z3 * (v1 + v3);
		float c = z1 * z3 * v1.Dot(v3) + (z1 - 1) * (z3 - 1) * screenZ() * screenZ();

		// Our default value for t, which would put \org at the center of the quad.
		// We'll try to find a value for \org that's as close as possible to it.
		Vector2D t = q0 - midpoint;

		// Handle all the edge cases. These can actually come up in practice, like when
		// starting from text without any perspective.
		if (a == 0) {
			// If b = 0 we get a trivial or impossible equation, so just keep the previous \org.
			if (b.SquareLen() != 0) {
				// The equation cuts out a line. Find the point closest to the previous t.
				t = t + b * ((c - t.Dot(b)) / b.SquareLen());
			}
		} else {
			// The equation cuts out a circle.
			// Complete the square to find center and radius.
			Vector2D circleCenter = b / (2 * a);
			float sqradius = (b.SquareLen() / (4 * a) - c) / a;

			if (sqradius <= 0) {
				// This is actually very rare.
				state.org = circleCenter;
			} else {
				// Find the point on the circle closest to the current \org.
				float radius = sqrt(sqradius);
				Vector2D center2t = t - circleCenter;
				if (center2t.Len() == 0) {
					t = circleCenter + Vector2D(radius, 0);
				} else {
					t = circleCenter + center2t / center2t.Len() * radius;
				}
			}
		}

		state.org = q0 - t;
	}

	// Normalize to org
	q0 = q0 - state.org;
	q1 = q1 - state.org;
	q2 = q2 - state.org;
	q3 = q3 - state.org;

	Vector3D r0 = Vector3D(q0, screenZ());
	Vector3D r1 = z1 * Vector3D(q1, screenZ());
	Vector3D r2 = (z1 + z3 - 1) * Vector3D(q2, screenZ());
	Vector3D r3 = z3 * Vector3D(q3, screenZ());
	std::vector<Vector3D> r({r0, r1, r2, r3});

	// Find the z coordinate of the point projecting to the origin
	float orgla0, orgla1;
	Vector3D side0 = r1 - r0;
	Vector3D side1 = r3 - r0;
	Solve2x2(side0.X(), side1.X(), side0.Y(), side1.Y(), -r0.X(), -r0.Y(), orgla0, orgla1);
	float orgz = (r0 + orgla0 * side0 + orgla1 * side1).Z();

	// Normalize so the origin has z=screenZ, and move the screen plane to z=0
	for (int i = 0; i < 4; i++)
		r[i] = r[i] * screenZ() / orgz - Vector3D(0, 0, screenZ());

	// Find the rotations
	Vector3D n = (r[1] - r[0]).Cross(r[3] - r[0]);
	float roty = atan(n.X() / n.Z());
	if (n.Z() < 0)
		roty += pi;
	n = n.RotateY(roty);
	float rotx = atan(n.Y() / n.Z());

	// Rotate into the z=0 plane
	for (int i = 0; i < 4; i++)
		r[i] = r[i].RotateY(roty).RotateX(rotx);

	Vector3D ab = r[1] - r[0];
	float rotz = atan(ab.Y() / ab.X());
	if (ab.X() < 0)
		rotz += pi;

	// Rotate to make the top side be horizontal
	for (int i = 0; i < 4; i++)
		r[i] = r[i].RotateZ(-rotz);

	// We now have a horizontal parallelogram in the plane, so find the shear and the dimensions
	ab = r[1] - r[0];
	Vector3D ad = r[3] - r[0];
	float rawfax = ad.X() / ad.Y();

	float quadwidth = ab.Len();
	float quadheight = abs(ad.Y());
	float scalex = quadwidth / std::max(state.bbox.second.X() - state.bbox.first.X(), 1.0f);
	float scaley = quadheight / std::max(state.bbox.second.Y() - state.bbox.first.Y(), 1.0f);
	Vector2D scale = Vector2D(scalex, scaley);

	float shiftv = state.align <= 3 ? 1 : (state.align <= 6 ? 0.5 : 0);
	float shifth = state.align % 3 == 0 ? 1 : (state.align % 3 == 2 ? 0.5 : 0);
	state.pos = state.org + r[0].XY() - state.bbox.first * scale + Vector2D(quadwidth * shifth, quadheight * shiftv);
	state.angle_x = rotx * rad2deg;
	state.angle_y = -roty * rad2deg;
	state.angle_z = -rotz * rad2deg;
	Vector2D oldfsc = state.fsc;
	state.fsc = 100 * scale;
	state.fax = rawfax * scaley / scalex;
	state.fay = 0;

	state.bord = state.bord * state.fsc / oldfsc;
	state.shad = state.shad * state.fsc / oldfsc;

	// Give up if any of these numbers were invalid
	std::vector<float> allvalues({state.fax, state.fsc.X(), state.fsc.Y(), state.angle_z, state.angle_x, state.angle_y, state.bord.X(), state.bord.Y(), state.shad.X(), state.shad.Y(), state.org.X(), state.org.Y(), state.pos.X(), state.pos.Y()});
	for (float f : allvalues) {
		if (!isfinite(f)) return false;
	}

	for (auto line : c->selectionController->GetSelectedSet()) {
		auto style = c->ass->GetStyle(line->Style);
		// Maybe just set the tags manually so the line doesn't need to be parsed again for every tag?
		WrapSetOverride(line, "\\fax", state.fax, 6);
		WrapSetOverride(line, "\\fay", 0, 6);
		WrapSetOverride(line, "\\fscx", state.fsc.X(), 2, style->scalex);
		WrapSetOverride(line, "\\fscy", state.fsc.Y(), 2, style->scaley);
		WrapSetOverride(line, "\\frz", state.angle_z, 4, style->angle);
		WrapSetOverride(line, "\\frx", state.angle_x, 4);
		WrapSetOverride(line, "\\fry", state.angle_y, 4);
		RemoveOverride(line, "\\bord");
		RemoveOverride(line, "\\shad");
		WrapSetOverride(line, "\\xbord", state.bord.X(), 2, style->outline_w);
		WrapSetOverride(line, "\\ybord", state.bord.Y(), 2, style->outline_w);
		WrapSetOverride(line, "\\xshad", state.shad.X(), 2, style->shadow_w);
		WrapSetOverride(line, "\\yshad", state.shad.Y(), 2, style->shadow_w);
		SetOverride(line, "\\org", state.org.PStr());
		SetOverride(line, "\\pos", state.pos.PStr());
	}
	return true;
}

void VisualToolPerspective::SaveFeaturePositions(LineState& state) {
	for (int i = 0; i < 4; i++) {
		state.old_inner[i] = state.inner_corners[i]->pos;
		if (HasOuter())
			state.old_outer[i] = state.outer_corners[i]->pos;
	}
}

void VisualToolPerspective::SaveOuterToLines(LineState& state) {
	if (HasOuter()) {
		std::string plane_descriptor;
		for (int i = 0; i < 4; i++) {
			Vector2D saved_corner = ToScriptCoords(state.outer_corners[i]->pos);
			if (!isfinite(saved_corner.X()) || !isfinite(saved_corner.Y()))
				return;
			plane_descriptor += agi::format("%.2f;%.2f", saved_corner.X(), saved_corner.Y());
			if (i < 3) plane_descriptor += "|";
		}
		uint32_t plane_extra = c->ass->AddExtradata(ambient_plane_key, plane_descriptor);

		for (auto line : c->selectionController->GetSelectedSet()) {
			// Let's reinvent the wheel a bit since extradata tooling is nonexistent
			std::vector<uint32_t> extra = line->ExtradataIds.get();
			std::vector<ExtradataEntry> entries = c->ass->GetExtradata(extra);
			for (int i = entries.size() - 1; i >= 0; i--) {
				if (entries[i].key == ambient_plane_key)
					extra.erase(extra.begin() + i, extra.begin() + i + 1);
			}
			extra.push_back(plane_extra);
			line->ExtradataIds = extra;
		}
	}
}

void VisualToolPerspective::SetFeaturePositions(LineState& state) {
	state.centerf->pos = QuadMidpoint(FeaturePositions(state.inner_corners));
	if (state.orgf != nullptr)
		state.orgf->pos = FromScriptCoords(state.org);
}

void VisualToolPerspective::TextToPersp(LineState& state, AssDialogue *line) {
	if (!line) return;

	state.org = GetLineOrigin(line);
	state.pos = GetLinePosition(line);
	if (!state.org)
		state.org = state.pos;

	GetLineRotation(line, state.angle_x, state.angle_y, state.angle_z);
	GetLineShear(line, state.fax, state.fay);
	GetLineScale(line, state.fsc);
	GetLineOutline(line, state.bord);
	GetLineShadow(line, state.shad);

	state.align = GetLineAlignment(line);

	state.bbox = GetLineBaseExtents(line);
	float textwidth = std::max(state.bbox.second.X() - state.bbox.first.X(), 1.f);
	float textheight = std::max(state.bbox.second.Y() - state.bbox.first.Y(), 1.f);
	double shiftx = 0., shifty = 0.;

	switch ((state.align - 1) % 3) {
		case 1:
			shiftx = -textwidth / 2;
			break;
		case 2:
			shiftx = -textwidth;
			break;
		default:
			break;
	}
	switch ((state.align - 1) / 3) {
		case 0:
			shifty = -textheight;
			break;
		case 1:
			shifty = -textheight / 2;
			break;
		default:
			break;
	}

	std::vector<Vector2D> textrect = MakeRect(state.bbox.first, state.bbox.second);
	for (int i = 0; i < 4; i++) {
		Vector2D p = textrect[i];
		// Apply \fax and \fay
		p = Vector2D(p.X() + p.Y() * state.fax, p.X() * state.fay + p.Y());
		// Translate to alignment point
		p = p + Vector2D(shiftx, shifty);
		// Apply scaling
		p = Vector2D(p.X() * state.fsc.X() / 100., p.Y() * state.fsc.Y() / 100.);
		// Translate relative to origin
		p = p + state.pos - state.org;
		// Rotate ZXY
		Vector3D q(p);
		q = q.RotateZ(-state.angle_z * deg2rad);
		q = q.RotateX(-state.angle_x * deg2rad);
		q = q.RotateY(state.angle_y * deg2rad);
		// Project
		q = (screenZ() / (q.Z() + screenZ())) * q;
		// Move to origin
		Vector2D r = q.XY() + state.org;
		state.inner_corners[i]->pos = FromScriptCoords(r);
	}

	for (auto const& extra : c->ass->GetExtradata(line->ExtradataIds)) {
		if (extra.key == ambient_plane_key) {
			std::vector<std::string> fields;
			agi::Split(fields, extra.value, '|');
			if (fields.size() != 4)
				break;

			std::vector<Vector2D> saved_outer;
			for (int i = 0; i < 4; i++) {
				std::vector<std::string> ordinates;
				agi::Split(ordinates, fields[i], ';');
				if (ordinates.size() != 2)
					break;

				double x, y;
				if (!agi::util::try_parse(ordinates[0], &x)) break;
				if (!agi::util::try_parse(ordinates[1], &y)) break;

				saved_outer.emplace_back(x, y);
			}
			if (saved_outer.size() != 4) break;

			Vector2D d1 = XYToUV(saved_outer, ToScriptCoords(state.inner_corners[0]->pos));
			Vector2D d2 = XYToUV(saved_outer, ToScriptCoords(state.inner_corners[2]->pos));
			if (isfinite(d1.X()) && isfinite(d1.Y()) && isfinite(d2.X()) && isfinite(d2.Y())) {
				state.c1 = d1;
				state.c2 = d2;
			}
		}
	}

	UpdateOuter(state);
}

void VisualToolPerspective::DoRefresh() {
	RebuildFeatures();
}

void VisualToolPerspective::OnSelectionChanged() {
	RebuildFeatures();
	parent->Render();
}

VisualToolPerspectiveDraggableFeature::VisualToolPerspectiveDraggableFeature(VisualToolPerspective *tool, int group, int index) : tool(tool), group(group), index(index) {}

void VisualToolPerspectiveDraggableFeature::UpdateDrag(Vector2D d, bool single_axis) {
	auto& state = tool->GetLineState(line);

	if (tool->ctrl_down && tool->alt_down)
		single_axis = false;   // This is handled manually later on

	if (single_axis && !(group == FEATURE_CENTER && !(tool->HasOuter() && !tool->OuterLocked()))) {
		// Snap to the axes *inside* of the quad's perspective plane.
		std::vector<Vector2D> quad = state.old_inner;
		Vector2D posUV = XYToUV(quad, pos);
		Vector2D axis1 = UVToXY(quad, posUV + Vector2D(1, 0)) - pos;
		Vector2D axis2 = UVToXY(quad, posUV + Vector2D(0, 1)) - pos;

		// Normalize and project
		axis1 = axis1.Unit();
		axis2 = axis2.Unit();
		Vector2D snap1 = d.Dot(axis1) * axis1;
		Vector2D snap2 = d.Dot(axis2) * axis2;
		d = (snap1 - d).SquareLen() <= (snap2 - d).SquareLen() ? snap1 : snap2;
		single_axis = false;
	}
	VisualDraggableFeature::UpdateDrag(d, single_axis);
}
