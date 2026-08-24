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

#include "visual_tool_vector_clip.h"

#include "video_display.h"
#include "ass_dialogue.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "libresrc/libresrc.h"
#include "options.h"
#include "selection_controller.h"

#include <algorithm>
#include <boost/range/algorithm/copy.hpp>
#include <boost/range/algorithm/set_algorithm.hpp>
#include <wx/toolbar.h>

int BUTTON_ID_BASE = 1300;

VisualToolVectorClip::VisualToolVectorClip(VideoDisplay *parent, agi::Context *context)
: VisualTool<VisualToolVectorClipDraggableFeature>(parent, context)
{
	// 从选项恢复上次使用的辅助工具模式
	int saved_mode = OPT_GET("Tool/Visual/Vector Clip/Mode")->GetInt();
	if (saved_mode >= 0 && saved_mode < VCLIP_LAST)
		mode = saved_mode;

	RebuildFeatures();
}

// Having the mode as an extra argument here isn't the cleanest, but using a counter instead
// as is done in toolbar.cpp feels like too big of a hack. At least this way the button's actions
// are not purely controlled by the order they're added in.
void VisualToolVectorClip::AddTool(std::string command_name, VisualToolVectorClipMode mode) {
	cmd::Command *command = cmd::get(command_name);
	int icon_size = OPT_GET("App/Toolbar Icon Size")->GetInt();
	toolBar->AddTool(BUTTON_ID_BASE + mode, command->StrDisplay(c), command->Icon(icon_size), command->GetTooltip("Video"), wxITEM_CHECK);
}


void VisualToolVectorClip::SetToolbar(wxToolBar *toolBar) {
	this->toolBar = toolBar;

	toolBar->AddSeparator();

	AddTool("video/tool/vclip/drag", VCLIP_DRAG);
	AddTool("video/tool/vclip/line", VCLIP_LINE);
	AddTool("video/tool/vclip/bicubic", VCLIP_BICUBIC);
	toolBar->AddSeparator();
	AddTool("video/tool/vclip/convert", VCLIP_CONVERT);
	AddTool("video/tool/vclip/insert", VCLIP_INSERT);
	AddTool("video/tool/vclip/remove", VCLIP_REMOVE);
	toolBar->AddSeparator();
	AddTool("video/tool/vclip/freehand", VCLIP_FREEHAND);
	AddTool("video/tool/vclip/freehand_smooth", VCLIP_FREEHAND_SMOOTH);

	toolBar->ToggleTool(BUTTON_ID_BASE + VCLIP_DRAG, true);
	toolBar->Realize();
	toolBar->Show(true);
	toolBar->Bind(wxEVT_TOOL, [=, this](wxCommandEvent& e) { SetSubTool(e.GetId() - BUTTON_ID_BASE); });
	SetSubTool(mode);
}

void VisualToolVectorClip::SetSubTool(int subtool) {
	if (toolBar == nullptr) {
		throw agi::InternalError("Vector clip toolbar hasn't been set yet!");
	}
	// Manually enforce radio behavior as we want one selection in the bar
	// rather than one per group
	for (int i = 0; i < VCLIP_LAST; i++)
		toolBar->ToggleTool(BUTTON_ID_BASE + i, i == subtool);

	mode = subtool;
	// 保存辅助工具模式，跨工具切换保留
	OPT_SET("Tool/Visual/Vector Clip/Mode")->SetInt(subtool);
	// 点击辅助工具栏后焦点会落在按钮上，交还视频显示区使方向键继续生效
	parent->SetFocus();
}

int VisualToolVectorClip::GetSubTool() {
	return mode;
}

void VisualToolVectorClip::Draw() {
	if (!active_line) return;

	// 本工具自绘特征不经过 DrawAllFeatures，需手动填充按行色相映射
	UpdateLineHueMap();

	bool any_spline = false;
	for (auto& [line, state] : line_states) {
		if (!state.spline.empty()) {
			any_spline = true;
			break;
		}
	}
	if (!any_spline) return;

	float shaded_alpha = static_cast<float>(shaded_area_alpha_opt->GetDouble());

	// 每行绘制裁剪阴影、路径线和 bicubic 控制柄线
	for (auto& [line, state] : line_states) {
		if (state.spline.empty()) continue;

		// 多行同屏时路径线按行区分颜色
		wxColour line_color = GetPerLineColor(line);

		std::vector<int> start;
		std::vector<int> count;
		auto points = state.spline.GetPointList(start, count);
		assert(!start.empty());
		assert(!count.empty());

		gl.SetLineColour(line_color, .5f, 2);
		gl.SetFillColour(*wxBLACK, shaded_alpha);

		// draw the shade over clipped out areas and line showing the clip
		gl.DrawMultiPolygon(points, start, count, video_pos, video_res, !state.inverse);

		// Draw lines connecting the bicubic features
		gl.SetLineColour(line_color, 0.9f, 1);
		for (auto const& curve : state.spline) {
			if (curve.type == SplineCurve::BICUBIC) {
				gl.DrawDashedLine(curve.p1, curve.p2, 6);
				gl.DrawDashedLine(curve.p3, curve.p4, 6);
			}
		}
	}

	if (mode == VCLIP_DRAG && holding && drag_start && mouse_pos) {
		// Draw drag-select box
		Vector2D top_left = drag_start.Min(mouse_pos);
		Vector2D bottom_right = drag_start.Max(mouse_pos);
		gl.DrawDashedLine(top_left, Vector2D(top_left.X(), bottom_right.Y()), 6);
		gl.DrawDashedLine(Vector2D(top_left.X(), bottom_right.Y()), bottom_right, 6);
		gl.DrawDashedLine(bottom_right, Vector2D(bottom_right.X(), top_left.Y()), 6);
		gl.DrawDashedLine(Vector2D(bottom_right.X(), top_left.Y()), top_left, 6);
	}

	// 活动行的编辑预览（高亮曲线/插入预览）
	auto it = line_states.find(active_line);
	if (it != line_states.end() && !it->second.spline.empty()) {
		LineState& state = it->second;
		wxColour line_color = GetPerLineColor(active_line);

		Vector2D pt;
		float t;
		Spline::iterator highlighted_curve;
		state.spline.GetClosestParametricPoint(mouse_pos, highlighted_curve, t, pt);

		// Draw highlighted line
		if ((mode == VCLIP_CONVERT || mode == VCLIP_INSERT) && !active_feature && state.spline.size() > 2) {
			auto highlighted_points = state.spline.GetPointList(highlighted_curve);
			if (!highlighted_points.empty()) {
				gl.SetLineColour(line_color, 1.f, 2);
				gl.DrawLineStrip(2, highlighted_points);
			}
		}

		// Draw preview of inserted line
		if (mode == VCLIP_LINE || mode == VCLIP_BICUBIC) {
			if (state.spline.size() && mouse_pos) {
				auto c0 = std::find_if(state.spline.rbegin(), state.spline.rend(),
					[](SplineCurve const& s) { return s.type == SplineCurve::POINT; });
				SplineCurve *c1 = &state.spline.back();
				gl.DrawDashedLine(mouse_pos, c0->p1, 6);
				gl.DrawDashedLine(mouse_pos, c1->EndPoint(), 6);
			}
		}

		// Draw preview of insert point
		if (mode == VCLIP_INSERT)
			gl.DrawCircle(pt, 4);
	}

	// 按行着色绘制控制点特征
	const int featureSize = OPT_GET("Tool/Visual/Shape Handle Size")->GetInt();
	for (auto& feature : features) {
		wxColour feature_color = GetPerLineColor(feature.line);
		if (&feature == active_feature)
			feature_color = to_wx(highlight_color_primary_opt->GetColor());
		else if (sel_features.count(&feature))
			feature_color = to_wx(highlight_color_secondary_opt->GetColor());
		gl.SetFillColour(feature_color, .6f);

		ScopedClamp clamp(feature, ClampToVideo(feature.pos, GetAnchorMargin(feature.type, featureSize)));
		if (feature.type == DRAG_SMALL_SQUARE) {
			gl.SetLineColour(GetPerLineColor(feature.line), .5f, 1);
			gl.DrawRectangle(feature.pos - featureSize, feature.pos + featureSize);
		}
		else {
			gl.SetLineColour(feature_color, .5f, 1);
			gl.DrawCircle(feature.pos, featureSize * 2.f / 3.f);
		}
	}
}

void VisualToolVectorClip::MakeFeature(Spline& spline, AssDialogue *line, size_t idx) {
	auto feat = std::make_unique<Feature>();
	feat->idx = idx;
	feat->line = line;

	auto const& curve = spline[idx];
	if (curve.type == SplineCurve::POINT) {
		feat->pos = curve.p1;
		feat->type = DRAG_SMALL_CIRCLE;
		feat->point = 0;
	}
	else if (curve.type == SplineCurve::LINE) {
		feat->pos = curve.p2;
		feat->type = DRAG_SMALL_CIRCLE;
		feat->point = 1;
	}
	else if (curve.type == SplineCurve::BICUBIC) {
		// Control points
		feat->pos = curve.p2;
		feat->point = 1;
		feat->type = DRAG_SMALL_SQUARE;
		features.push_back(*feat.release());

		feat = std::make_unique<Feature>();
		feat->idx = idx;
		feat->line = line;
		feat->pos = curve.p3;
		feat->point = 2;
		feat->type = DRAG_SMALL_SQUARE;
		features.push_back(*feat.release());

		// End point
		feat = std::make_unique<Feature>();
		feat->idx = idx;
		feat->line = line;
		feat->pos = curve.p4;
		feat->point = 3;
		feat->type = DRAG_SMALL_CIRCLE;
	}
	features.push_back(*feat.release());
}

void VisualToolVectorClip::RebuildLineFeatures(AssDialogue *line) {
	auto it = features.begin();
	while (it != features.end()) {
		if (it->line == line)
			it = features.erase(it);
		else
			++it;
	}
	auto state_it = line_states.find(line);
	if (state_it == line_states.end()) return;
	for (size_t i = 0; i < state_it->second.spline.size(); ++i)
		MakeFeature(state_it->second.spline, line, i);
}

void VisualToolVectorClip::RebuildFeatures() {
	features.clear();
	line_states.clear();
	sel_features.clear();
	active_feature = nullptr;

	// 选中集联合活动行，保证活动行不在选中集时工具仍可用
	auto lines = c->selectionController->GetSelectedSet();
	if (active_line) lines.insert(active_line);

	for (auto line : lines) {
		if (!IsDisplayed(line)) continue;

		auto state_it = line_states.emplace(line, this);
		LineState& state = state_it.first->second;

		int scale;
		std::string vect = GetLineVectorClip(line, scale, state.inverse);
		state.spline.SetScale(scale);
		state.spline.DecodeFromAss(vect);

		for (size_t i = 0; i < state.spline.size(); ++i)
			MakeFeature(state.spline, line, i);
	}
}

void VisualToolVectorClip::Save() {
	auto it = line_states.find(active_line);
	if (it == line_states.end()) return;
	LineState& state = it->second;

	std::string value = "(";
	if (state.spline.GetScale() != 1)
		value += std::to_string(state.spline.GetScale()) + ",";
	value += state.spline.EncodeToAss() + ")";

	for (auto line : c->selectionController->GetSelectedSet()) {
		// This check is technically not correct as it could be outside of an
		// override block... but that's rather unlikely
		bool has_iclip = line->Text.get().find("\\iclip") != std::string::npos;
		SetOverride(line, has_iclip ? "\\iclip" : "\\clip", value);
	}
}

void VisualToolVectorClip::Commit(wxString message) {
	// 框选（VCLIP_DRAG 的 hold）只是选择特征，不修改数据，跳过 Save
	// 避免把活动行值意外写入所有选中行
	if (!(holding && mode == VCLIP_DRAG))
		Save();
	VisualToolBase::Commit(message);
}

void VisualToolVectorClip::UpdateDrag(Feature *feature) {
	if (!feature->line) return;
	auto it = line_states.find(feature->line);
	if (it == line_states.end()) return;
	it->second.spline.MovePoint(it->second.spline.begin() + feature->idx, feature->point, feature->pos);
}

bool VisualToolVectorClip::InitializeDrag(Feature *feature) {
	if (mode != 5) return true;

	// 特征可能在拖拽起点被重建销毁，防御空指针
	if (!feature || !feature->line) return false;
	auto it = line_states.find(feature->line);
	if (it == line_states.end()) return false;
	LineState& state = it->second;

	auto curve = state.spline.begin() + feature->idx;
	if (curve->type == SplineCurve::BICUBIC && (feature->point == 1 || feature->point == 2)) {
		// Deleting bicubic curve handles, so convert to line
		curve->type = SplineCurve::LINE;
		curve->p2 = curve->p4;
	}
	else {
		auto next = std::next(curve);
		if (next != state.spline.end()) {
			if (curve->type == SplineCurve::POINT) {
				next->p1 = next->EndPoint();
				next->type = SplineCurve::POINT;
			}
			else {
				next->p1 = curve->p1;
			}
		}

		state.spline.erase(curve);
	}
	active_feature = nullptr;

	RebuildLineFeatures(feature->line);
	Commit(_("delete control point"));

	return false;
}

bool VisualToolVectorClip::InitializeHold() {
	auto state_it = line_states.find(active_line);
	if (state_it == line_states.end()) return false;
	LineState& state = state_it->second;

	// Box selection
	if (mode == VCLIP_DRAG) {
		box_added.clear();
		return true;
	}

	// Insert line/bicubic
	if (mode == VCLIP_LINE || mode == VCLIP_BICUBIC) {
		SplineCurve curve;

		// New spline beginning at the clicked point
		if (state.spline.empty()) {
			curve.p1 = mouse_pos;
			curve.type = SplineCurve::POINT;
		}
		else {
			// Continue from the spline in progress
			// Don't bother setting p2 as UpdateHold will handle that
			curve.p1 = state.spline.back().EndPoint();
			curve.type = mode == VCLIP_LINE ? SplineCurve::LINE : SplineCurve::BICUBIC;
		}

		state.spline.push_back(curve);
		sel_features.clear();
		MakeFeature(state.spline, active_line, state.spline.size() - 1);
		UpdateHold();
		return true;
	}

	// Convert and insert
	if (mode == VCLIP_CONVERT || mode == VCLIP_INSERT) {
		// Get closest point
		Vector2D pt;
		Spline::iterator curve;
		float t;
		state.spline.GetClosestParametricPoint(mouse_pos, curve, t, pt);

		// Convert line <-> bicubic
		if (mode == VCLIP_CONVERT) {
			if (curve != state.spline.end()) {
				if (curve->type == SplineCurve::LINE) {
					curve->type = SplineCurve::BICUBIC;
					curve->p4 = curve->p2;
					curve->p2 = curve->p1 * 0.75 + curve->p4 * 0.25;
					curve->p3 = curve->p1 * 0.25 + curve->p4 * 0.75;
				}

				else if (curve->type == SplineCurve::BICUBIC) {
					curve->type = SplineCurve::LINE;
					curve->p2 = curve->p4;
				}
			}
		}
		// Insert
		else {
			if (state.spline.empty()) return false;

			// Split the curve
			if (curve == state.spline.end()) {
				SplineCurve ct(state.spline.back().EndPoint(), state.spline.front().p1);
				ct.p2 = ct.p1 * (1 - t) + ct.p2 * t;
				state.spline.push_back(ct);
			}
			else {
				std::pair<SplineCurve, SplineCurve> split = curve->Split(t);
				*curve = split.first;
				state.spline.insert(++curve, split.second);
			}
		}

		RebuildLineFeatures(active_line);
		Commit();
		return false;
	}

	// Freehand spline draw
	if (mode == VCLIP_FREEHAND || mode == VCLIP_FREEHAND_SMOOTH) {
		sel_features.clear();
		auto fit = features.begin();
		while (fit != features.end()) {
			if (fit->line == active_line)
				fit = features.erase(fit);
			else
				++fit;
		}
		active_feature = nullptr;
		state.spline.clear();
		state.spline.emplace_back(mouse_pos);
		return true;
	}

	// Nothing to do for mode 5 (remove)
	return false;
}

static bool in_box(Vector2D top_left, Vector2D bottom_right, Vector2D p) {
	return p.X() >= top_left.X()
		&& p.X() <= bottom_right.X()
		&& p.Y() >= top_left.Y()
		&& p.Y() <= bottom_right.Y();
}

void VisualToolVectorClip::UpdateHold() {
	auto state_it = line_states.find(active_line);
	if (state_it == line_states.end()) return;
	LineState& state = state_it->second;

	// Box selection
	if (mode == VCLIP_DRAG) {
		std::set<Feature *> boxed_features;
		Vector2D p1 = drag_start.Min(mouse_pos);
		Vector2D p2 = drag_start.Max(mouse_pos);
		for (auto& feature : features) {
			if (in_box(p1, p2, feature.pos))
				boxed_features.insert(&feature);
		}

		// Keep track of which features were selected by the box selection so
		// that only those are deselected if the user is holding ctrl
		boost::set_difference(boxed_features, sel_features,
			std::inserter(box_added, end(box_added)));

		boost::copy(boxed_features, std::inserter(sel_features, end(sel_features)));

		std::vector<Feature *> to_deselect;
		boost::set_difference(box_added, boxed_features, std::back_inserter(to_deselect));
		for (auto feature : to_deselect)
			sel_features.erase(feature);

		return;
	}

	if (mode == VCLIP_LINE) {
		state.spline.back().EndPoint() = mouse_pos;
		RebuildLineFeatures(active_line);
	}

	// Insert bicubic
	else if (mode == VCLIP_BICUBIC) {
		SplineCurve &curve = state.spline.back();
		curve.EndPoint() = mouse_pos;

		// Control points
		if (state.spline.size() > 1) {
			SplineCurve &c0 = state.spline.back();
			float len = (curve.p4 - curve.p1).Len();
			curve.p2 = (c0.type == SplineCurve::LINE ? c0.p2 - c0.p1 : c0.p4 - c0.p3).Unit() * (0.25f * len) + curve.p1;
		}
		else
			curve.p2 = curve.p1 * 0.75 + curve.p4 * 0.25;
		curve.p3 = curve.p1 * 0.25 + curve.p4 * 0.75;
		RebuildLineFeatures(active_line);
	}

	// Freehand
	else if (mode == VCLIP_FREEHAND || mode == VCLIP_FREEHAND_SMOOTH) {
		// See if distance is enough
		Vector2D const& last = state.spline.back().EndPoint();
		float len = (last - mouse_pos).SquareLen();
		if ((mode == VCLIP_FREEHAND && len >= 900) || (mode == VCLIP_FREEHAND_SMOOTH && len >= 3600)) {
			state.spline.emplace_back(last, mouse_pos);
			MakeFeature(state.spline, active_line, state.spline.size() - 1);
		}
	}

	if (mode == VCLIP_CONVERT || mode == VCLIP_INSERT) return;

	// Smooth spline
	if (!holding && mode == VCLIP_FREEHAND_SMOOTH)
		state.spline.Smooth();

	// End freedraw
	if (!holding && (mode == VCLIP_FREEHAND || mode == VCLIP_FREEHAND_SMOOTH)) {
		SetSubTool(VCLIP_DRAG);
		RebuildFeatures();
	}
}

void VisualToolVectorClip::DoRefresh() {
	RebuildFeatures();
}

void VisualToolVectorClip::OnSelectionChanged() {
	RebuildFeatures();
	parent->Render();
}
