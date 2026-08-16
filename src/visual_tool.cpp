// Copyright (c) 2013, Thomas Goyne <plorkyeran@aegisub.org>
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

#include "visual_tool.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "auto4_base.h"
#include "colorspace.h"
#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "selection_controller.h"
#include "video_controller.h"
#include "video_display.h"
#include "visual_tool_clip.h"
#include "visual_tool_drag.h"
#include "visual_tool_perspective.h"
#include "visual_tool_vector_clip.h"

#include <libaegisub/ass/time.h>
#include <libaegisub/format.h>
#include <libaegisub/of_type_adaptor.h>
#include <libaegisub/split.h>
#include <libaegisub/string.h>

#include <algorithm>
#include <limits>

VisualToolBase::VisualToolBase(VideoDisplay *parent, agi::Context *context)
: c(context)
, parent(parent)
, frame_number(c->videoController->GetFrameN())
, highlight_color_primary_opt(OPT_GET("Colour/Visual Tools/Highlight Primary"))
, highlight_color_secondary_opt(OPT_GET("Colour/Visual Tools/Highlight Secondary"))
, line_color_primary_opt(OPT_GET("Colour/Visual Tools/Lines Primary"))
, line_color_secondary_opt(OPT_GET("Colour/Visual Tools/Lines Secondary"))
, shaded_area_alpha_opt(OPT_GET("Colour/Visual Tools/Shaded Area Alpha"))
, file_changed_connection(c->ass->AddCommitListener(&VisualToolBase::OnCommit, this))
{
	SetResolutions();
	active_line = GetActiveDialogueLine();
	connections.emplace_back(c->selectionController->AddActiveLineListener(&VisualToolBase::OnActiveLineChanged, this));
	// 选中集变化前先清理拖拽状态，避免重建特征时销毁正在拖拽的特征对象
	connections.emplace_back(c->selectionController->AddSelectionListener([this] {
		dragging = false;
		holding = false;
		OnSelectionChanged();
	}));
	connections.emplace_back(c->videoController->AddSeekListener(&VisualToolBase::OnSeek, this));
	// 控制点大小配置变更时立即重绘，使所有可视化工具的控制点即时生效
	connections.emplace_back(OPT_SUB("Tool/Visual/Shape Handle Size", [this](agi::OptionValue const&) { this->parent->Render(); }));
	parent->Bind(wxEVT_MOUSE_CAPTURE_LOST, &VisualToolBase::OnMouseCaptureLost, this);
}

void VisualToolBase::SetResolutions() {
	int script_w, script_h, layout_w, layout_h;
	c->ass->GetResolution(script_w, script_h);
	c->ass->GetEffectiveLayoutResolution(c, layout_w, layout_h);
	script_res = Vector2D(script_w, script_h);
	layout_res = Vector2D(layout_w, layout_h);
}

void VisualToolBase::OnCommit(int type) {
	holding = false;
	dragging = false;
	// 外部提交可能增删对话行，清除追踪集合以防止野指针
	modified_lines.clear();

	if (type == AssFile::COMMIT_NEW || type & AssFile::COMMIT_SCRIPTINFO) {
		SetResolutions();
		OnCoordinateSystemsChanged();
	}

	if (type & AssFile::COMMIT_DIAG_FULL || type & AssFile::COMMIT_DIAG_ADDREM) {
		active_line = GetActiveDialogueLine();
		OnFileChanged();
	}
}

void VisualToolBase::OnSeek(int new_frame) {
	if (frame_number == new_frame) return;

	frame_number = new_frame;
	OnFrameChanged();

	AssDialogue *new_line = GetActiveDialogueLine();
	if (new_line != active_line) {
		dragging = false;
		active_line = new_line;
		OnLineChanged();
	}
}

void VisualToolBase::OnMouseCaptureLost(wxMouseCaptureLostEvent &) {
	holding = false;
	dragging = false;
	OnDragCleanup();
}

void VisualToolBase::OnActiveLineChanged(AssDialogue *new_line) {
	if (!IsDisplayed(new_line))
		new_line = nullptr;

	holding = false;
	dragging = false;
	if (new_line != active_line) {
		active_line = new_line;
		OnLineChanged();
		parent->Render();
	}
}

bool VisualToolBase::IsDisplayed(AssDialogue *line) const {
	int frame = c->videoController->GetFrameN();
	return line
		&& !line->Comment
		&& c->videoController->FrameAtTime(line->Start, agi::vfr::START) <= frame
		&& c->videoController->FrameAtTime(line->End, agi::vfr::END) >= frame;
}

void VisualToolBase::Commit(wxString message) {
	file_changed_connection.Block();
	if (message.empty())
		message = _("visual typesetting");

	// 逐行提交，使撤销快照与监听器（网格/字幕渲染）均走单行快速路径，
	// 避免多行提交触发的全量复制、全量刷新与字幕轨道重载
	for (auto line : modified_lines)
		commit_id = c->ass->Commit(message, AssFile::COMMIT_DIAG_TEXT, commit_id, line);
	modified_lines.clear();

	file_changed_connection.Unblock();
}

AssDialogue* VisualToolBase::GetActiveDialogueLine() {
	AssDialogue *diag = c->selectionController->GetActiveLine();
	if (IsDisplayed(diag))
		return diag;
	return nullptr;
}

void VisualToolBase::SetClientSize(int w, int h) {
	client_size = Vector2D(w, h);
}

void VisualToolBase::SetDisplayArea(int x, int y, int w, int h) {
	if (x == video_pos.X() && y == video_pos.Y() && w == video_res.X() && h == video_res.Y()) return;

	video_pos = Vector2D(x, y);
	video_res = Vector2D(w, h);

	holding = false;
	dragging = false;
	if (parent->HasCapture())
		parent->ReleaseMouse();
	OnCoordinateSystemsChanged();
}

Vector2D VisualToolBase::ToScriptCoords(Vector2D point) const {
	return (point - video_pos) * script_res / video_res;
}

Vector2D VisualToolBase::FromScriptCoords(Vector2D point) const {
	return (point * video_res / script_res) + video_pos;
}

template<class FeatureType>
VisualTool<FeatureType>::VisualTool(VideoDisplay *parent, agi::Context *context)
: VisualToolBase(parent, context)
{
}

template<class FeatureType>
void VisualTool<FeatureType>::OnDragCleanup() {
	if (dragging) {
		// 最后帧可能未提交，捕获丢失时确保提交一次
		if (!modified_lines.empty())
			Commit();
		for (auto sel : sel_features)
			EndDrag(sel);
		sel_features.clear();
		active_feature = nullptr;
	}
}

template<class FeatureType>
void VisualTool<FeatureType>::OnMouseEvent(wxMouseEvent &event) {
	bool left_click = event.LeftDown();
	bool left_double = event.LeftDClick();
	shift_down = event.ShiftDown();
	ctrl_down = event.CmdDown();
	alt_down = event.AltDown();

	mouse_pos = event.GetPosition();

	if (event.Leaving()) {
		mouse_pos = Vector2D();
		parent->Render();
		return;
	}

	if (!dragging) {
		int max_layer = INT_MIN;
		active_feature = nullptr;
		for (auto& feature : features) {
			ScopedClamp clamp(feature, ClampToVideo(feature.pos, GetAnchorMargin(feature.type, feature.size)));
			bool over = feature.IsMouseOver(mouse_pos);
			if (over && feature.layer >= max_layer) {
				active_feature = &feature;
				max_layer = feature.layer;
			}
		}
	}

	if (dragging) {
		// 鼠标捕获丢失时系统可能未投递 LEFT_UP，
		// 此时收到 LEFT_DOWN 应视为前一次拖拽的隐式结束，
		// 并重新进行特征检测让本次点击也能正常开始新拖拽
		if (left_click) {
			dragging = false;
			// 使用副本遍历，EndDrag 内可能重建特征并清空 sel_features
			auto sels = std::vector<FeatureType*>(sel_features.begin(), sel_features.end());
			for (auto sel : sels)
				EndDrag(sel);
			sel_features.clear();
			active_feature = nullptr;
			if (parent->HasCapture())
				parent->ReleaseMouse();
			// 重新检测特征（dragging 已为 false）
			int max_layer = INT_MIN;
			for (auto& feature : features) {
				ScopedClamp clamp(feature, ClampToVideo(feature.pos, GetAnchorMargin(feature.type, feature.size)));
				bool over = feature.IsMouseOver(mouse_pos);
				if (over && feature.layer >= max_layer) {
					active_feature = &feature;
					max_layer = feature.layer;
				}
			}
		}
		// continue drag
		else if (event.LeftIsDown()) {
			// 使用副本遍历，防止拖拽中特征重建导致迭代器失效
			auto sels = std::vector<FeatureType*>(sel_features.begin(), sel_features.end());
			for (auto sel : sels)
				sel->UpdateDrag(mouse_pos - drag_start, shift_down);
			for (auto sel : sels)
				UpdateDrag(sel);
			// 每帧提交使字幕画面实时跟随，逐行提交走单行快速路径避免全量刷新
			Commit();
		}
		// end drag
		else {
			dragging = false;

			// 最后帧可能未提交，结束时确保提交一次
			if (!modified_lines.empty())
				Commit();

			// mouse didn't move, fiddle with selection
			if (active_feature && !active_feature->HasMoved()) {
				// Don't deselect stuff that was selected in this click's mousedown event
				if (!sel_changed) {
					if (ctrl_down)
						RemoveSelection(active_feature);
					else
						SetSelection(active_feature, true);
				}
			} else {
				// 使用副本遍历，EndDrag 内可能重建特征并清空 sel_features
				auto sels = std::vector<FeatureType*>(sel_features.begin(), sel_features.end());
				for (auto sel : sels)
					EndDrag(sel);
			}

			active_feature = nullptr;
			parent->ReleaseMouse();
			parent->SetFocus();
		}
	}
	else if (holding) {
		if (!event.LeftIsDown()) {
			holding = false;
			EndHold();

			// 最后帧可能未提交，结束时确保提交一次
			if (!modified_lines.empty())
				Commit();

			parent->ReleaseMouse();
			parent->SetFocus();
		}

		UpdateHold();
		// 每帧提交使字幕画面实时跟随，逐行提交走单行快速路径避免全量刷新
		Commit();

	}
	else if (left_click) {
		drag_start = mouse_pos;

		// start drag
		if (active_feature) {
			if (!sel_features.count(active_feature)) {
				sel_changed = true;
				SetSelection(active_feature, !ctrl_down);
			}
			else
				sel_changed = false;

			// 选中集变化可能触发特征重建（OnSelectionChanged），
			// 重建后 active_feature 已被置空，重新命中点击位置的特征并恢复选择
			if (!active_feature) {
				int max_layer = INT_MIN;
				for (auto& feature : features) {
					ScopedClamp clamp(feature, ClampToVideo(feature.pos, GetAnchorMargin(feature.type, feature.size)));
					bool over = feature.IsMouseOver(mouse_pos);
					if (over && feature.layer >= max_layer) {
						active_feature = &feature;
						max_layer = feature.layer;
					}
				}
				if (active_feature)
					sel_features.insert(active_feature);
			}

			if (active_feature->line)
				c->selectionController->SetActiveLine(active_feature->line);

			if (InitializeDrag(active_feature)) {
				for (auto sel : sel_features) sel->StartDrag();
				dragging = true;
				parent->CaptureMouse();
			}
		}
		// start hold
		else {
			if (!alt_down && features.size() > 1) {
				sel_features.clear();
			}
			if (active_line && InitializeHold()) {
				holding = true;
				parent->CaptureMouse();
			}
		}
	}

	if (active_line && left_double)
		OnDoubleClick();

	parent->Render();

	// Only coalesce the changes made in a single drag
	if (!event.LeftIsDown())
		commit_id = -1;
}

wxColour VisualToolBase::GetPerLineBaseColor(AssDialogue *) const {
	return to_wx(highlight_color_primary_opt->GetColor());
}

wxColour VisualToolBase::GetPerLineOutlineColor(AssDialogue *) const {
	return to_wx(line_color_secondary_opt->GetColor());
}

wxColour VisualToolBase::GetPerLineColor(AssDialogue *) const {
	return to_wx(line_color_primary_opt->GetColor());
}

template<class FeatureType>
void VisualTool<FeatureType>::UpdateLineHueMap() {
	// 特征行集合未变化时跳过重算，避免拖动中位置变化导致颜色闪烁
	std::set<AssDialogue*> current;
	for (auto& feature : features) {
		if (feature.line) current.insert(feature.line);
	}
	if (current == line_hue_lines) return;
	line_hue_lines = std::move(current);

	line_hue_map.clear();
	std::vector<Vector2D> assigned_pos;
	std::vector<int> assigned_hues;
	for (auto& feature : features) {
		// 每行取第一个特征的位置为锚点，按空间距离贪心分配色相
		if (!feature.line || line_hue_map.count(feature.line)) continue;
		int hue = SelectPerLineHue(feature.pos, assigned_pos, assigned_hues);
		line_hue_map[feature.line] = hue;
		assigned_pos.push_back(feature.pos);
		assigned_hues.push_back(hue);
	}
}

template<class FeatureType>
wxColour VisualTool<FeatureType>::GetPerLineColor(AssDialogue *line) const {
	auto it = line_hue_map.find(line);
	if (it == line_hue_map.end() || line_hue_map.size() < 2)
		return VisualToolBase::GetPerLineColor(line);

	// 高饱和高亮度的纯色，近邻行色相在色环上错开、反差最大
	unsigned char r, g, b;
	hsv_to_rgb(it->second, 230, 255, &r, &g, &b);
	return wxColour(r, g, b);
}

template<class FeatureType>
wxColour VisualTool<FeatureType>::GetPerLineBaseColor(AssDialogue *line) const {
	return GetPerLineColor(line);
}

template<class FeatureType>
wxColour VisualTool<FeatureType>::GetPerLineOutlineColor(AssDialogue *line) const {
	return GetPerLineColor(line);
}

template<class FeatureType>
void VisualTool<FeatureType>::DrawAllFeatures() {
	UpdateLineHueMap();
	wxColour active_fill = to_wx(highlight_color_secondary_opt->GetColor());
	for (auto& feature : features) {
		wxColour fill = GetPerLineBaseColor(feature.line);
		if (&feature == active_feature)
			fill = active_fill;
		else if (sel_features.count(&feature))
			fill = to_wx(line_color_primary_opt->GetColor());
		// 轮廓/十字线按行着色，普通控制点填充同色，激活/选中控制点使用统一高亮色
		gl.SetLineColour(GetPerLineOutlineColor(feature.line), 1.0f, 1);
		gl.SetFillColour(fill, 0.3f);
		ScopedClamp clamp(feature, ClampToVideo(feature.pos, GetAnchorMargin(feature.type, feature.size)));
		feature.Draw(gl);
	}
}

template<class FeatureType>
void VisualTool<FeatureType>::SetSelection(FeatureType *feat, bool clear) {
	if (clear)
		sel_features.clear();

	if (sel_features.insert(feat).second && feat->line) {
		Selection sel;
		if (!clear)
			sel = c->selectionController->GetSelectedSet();
		if (sel.insert(feat->line).second)
			c->selectionController->SetSelectedSet(std::move(sel));
	}
}

template<class FeatureType>
void VisualTool<FeatureType>::RemoveSelection(FeatureType *feat) {
	if (!sel_features.erase(feat) || !feat->line) return;
	for (auto sel : sel_features)
		if (sel->line == feat->line) return;

	auto sel = c->selectionController->GetSelectedSet();

	// Don't deselect the only selected line
	if (sel.size() <= 1) return;

	sel.erase(feat->line);

	// Set the active line to an arbitrary selected line if we just
	// deselected the active line
	AssDialogue *new_active = c->selectionController->GetActiveLine();
	if (feat->line == new_active)
		new_active = *sel.begin();

	c->selectionController->SetSelectionAndActive(std::move(sel), new_active);
}

//////// PARSERS

typedef const std::vector<AssOverrideParameter> * param_vec;

// Find a tag's parameters in a line or return nullptr if it's not found
static param_vec find_tag(std::vector<std::unique_ptr<AssDialogueBlock>>& blocks, std::string const& tag_name) {
	for (auto ovr : blocks | agi::of_type<AssDialogueBlockOverride>()) {
		for (auto const& tag : ovr->Tags) {
			if (tag.Name == tag_name)
				return &tag.Params;
		}
	}

	return nullptr;
}

// Get a Vector2D from the given tag parameters, or Vector2D::Bad() if they are not valid
static Vector2D vec_or_bad(param_vec tag, size_t x_idx, size_t y_idx) {
	if (!tag ||
		tag->size() <= x_idx || tag->size() <= y_idx ||
		(*tag)[x_idx].omitted || (*tag)[y_idx].omitted)
	{
		return Vector2D();
	}
	return Vector2D((*tag)[x_idx].Get<float>(), (*tag)[y_idx].Get<float>());
}

Vector2D VisualToolBase::GetLinePosition(AssDialogue *diag) {
	auto blocks = diag->ParseTags();

	if (Vector2D ret = vec_or_bad(find_tag(blocks, "\\pos"), 0, 1)) return ret;
	if (Vector2D ret = vec_or_bad(find_tag(blocks, "\\move"), 0, 1)) return ret;

	// Get default position
	auto margin = diag->Margin;
	int align = 2;

	if (AssStyle *style = c->ass->GetStyle(diag->Style)) {
		align = style->alignment;
		for (int i = 0; i < 3; i++) {
			if (margin[i] == 0)
				margin[i] = style->Margin[i];
		}
	}

	param_vec align_tag;
	int ovr_align = 0;
	if ((align_tag = find_tag(blocks, "\\an")))
		ovr_align = (*align_tag)[0].Get<int>(ovr_align);
	else if ((align_tag = find_tag(blocks, "\\a")))
		ovr_align = AssStyle::SsaToAss((*align_tag)[0].Get<int>(2));

	if (ovr_align > 0 && ovr_align <= 9)
		align = ovr_align;

	// Alignment type
	int hor = (align - 1) % 3;
	int vert = (align - 1) / 3;

	// Calculate positions
	int x, y;
	if (hor == 0)
		x = margin[0];
	else if (hor == 1)
		x = (script_res.X() + margin[0] - margin[1]) / 2;
	else
		x = script_res.X() - margin[1];

	if (vert == 0)
		y = script_res.Y() - margin[2];
	else if (vert == 1)
		y = script_res.Y() / 2;
	else
		y = margin[2];

	return Vector2D(x, y);
}

Vector2D VisualToolBase::GetLineOrigin(AssDialogue *diag) {
	auto blocks = diag->ParseTags();
	return vec_or_bad(find_tag(blocks, "\\org"), 0, 1);
}

bool VisualToolBase::GetLineMove(AssDialogue *diag, Vector2D &p1, Vector2D &p2, int &t1, int &t2) {
	auto blocks = diag->ParseTags();

	param_vec tag = find_tag(blocks, "\\move");
	if (!tag)
		return false;

	p1 = vec_or_bad(tag, 0, 1);
	p2 = vec_or_bad(tag, 2, 3);
	// VSFilter actually defaults to -1, but it uses <= 0 to check for default and 0 seems less bug-prone
	t1 = (*tag)[4].Get<int>(0);
	t2 = (*tag)[5].Get<int>(0);

	return p1 && p2;
}

void VisualToolBase::GetLineRotation(AssDialogue *diag, float &rx, float &ry, float &rz) {
	rx = ry = rz = 0.f;

	if (AssStyle *style = c->ass->GetStyle(diag->Style))
		rz = style->angle;

	auto blocks = diag->ParseTags();

	if (param_vec tag = find_tag(blocks, "\\frx"))
		rx = tag->front().Get(rx);
	if (param_vec tag = find_tag(blocks, "\\fry"))
		ry = tag->front().Get(ry);
	if (param_vec tag = find_tag(blocks, "\\frz"))
		rz = tag->front().Get(rz);
	else if ((tag = find_tag(blocks, "\\fr")))
		rz = tag->front().Get(rz);
}

void VisualToolBase::GetLineShear(AssDialogue *diag, float& fax, float& fay) {
	fax = fay = 0.f;

	auto blocks = diag->ParseTags();

	if (param_vec tag = find_tag(blocks, "\\fax"))
		fax = tag->front().Get(fax);
	if (param_vec tag = find_tag(blocks, "\\fay"))
		fay = tag->front().Get(fay);
}

void VisualToolBase::GetLineScale(AssDialogue *diag, Vector2D &scale) {
	float x = 100.f, y = 100.f;

	if (AssStyle *style = c->ass->GetStyle(diag->Style)) {
		x = style->scalex;
		y = style->scaley;
	}

	auto blocks = diag->ParseTags();

	if (param_vec tag = find_tag(blocks, "\\fscx"))
		x = tag->front().Get(x);
	if (param_vec tag = find_tag(blocks, "\\fscy"))
		y = tag->front().Get(y);

	scale = Vector2D(x, y);
}

void VisualToolBase::GetLineOutline(AssDialogue *diag, Vector2D &outline) {
	float x = 0.f, y = 0.f;

	if (AssStyle *style = c->ass->GetStyle(diag->Style)) {
		x = style->outline_w;
		y = style->outline_w;
	}

	auto blocks = diag->ParseTags();

	if (param_vec tag = find_tag(blocks, "\\bord")) {
		x = tag->front().Get(x);
		y = tag->front().Get(y);
	}
	if (param_vec tag = find_tag(blocks, "\\xbord"))
		x = tag->front().Get(x);
	if (param_vec tag = find_tag(blocks, "\\ybord"))
		y = tag->front().Get(y);

	outline = Vector2D(x, y);
}

void VisualToolBase::GetLineShadow(AssDialogue *diag, Vector2D &shadow) {
	float x = 0.f, y = 0.f;

	if (AssStyle *style = c->ass->GetStyle(diag->Style)) {
		x = style->shadow_w;
		y = style->shadow_w;
	}

	auto blocks = diag->ParseTags();

	if (param_vec tag = find_tag(blocks, "\\shad")) {
		x = tag->front().Get(x);
		y = tag->front().Get(y);
	}
	if (param_vec tag = find_tag(blocks, "\\xshad"))
		x = tag->front().Get(x);
	if (param_vec tag = find_tag(blocks, "\\yshad"))
		y = tag->front().Get(y);

	shadow = Vector2D(x, y);
}

int VisualToolBase::GetLineAlignment(AssDialogue *diag) {
	int an = 0;

	if (AssStyle *style = c->ass->GetStyle(diag->Style))
		an = style->alignment;
	auto blocks = diag->ParseTags();
	if (param_vec tag = find_tag(blocks, "\\an"))
		an = tag->front().Get(an);

	return an;
}

std::pair<Vector2D, Vector2D> VisualToolBase::GetLineBaseExtents(AssDialogue *diag) {
	double width = 0.;
	double height = 0.;

	AssStyle style;
	if (AssStyle *basestyle = c->ass->GetStyle(diag->Style)) {
		style = AssStyle(basestyle->GetEntryData());
		style.scalex = 100.;
		style.scaley = 100.;
	}

	auto blocks = diag->ParseTags();
	param_vec ptag = find_tag(blocks, "\\p");

	if (ptag && ptag->front().Get(0)) {		// A drawing
		Spline spline;
		spline.SetScale(ptag->front().Get(1));
		std::string drawing_text;
		for (auto *block : blocks | agi::of_type<AssDialogueBlockDrawing>())
			drawing_text += block->GetText();
		spline.DecodeFromAss(drawing_text);

		if (!spline.size())
			return std::make_pair(Vector2D(0, 0), Vector2D(0, 0));

		float left = std::numeric_limits<float>::max();
		float top = std::numeric_limits<float>::max();
		float right = -std::numeric_limits<float>::max();
		float bot = -std::numeric_limits<float>::max();

		for (SplineCurve curve : spline) {
			for (Vector2D pt : curve.AnchorPoints()) {
				left = std::min(left, pt.X());
				top = std::min(top, pt.Y());
				right = std::max(right, pt.X());
				bot = std::max(bot, pt.Y());
			}
		}

		return std::make_pair(Vector2D(left, top), Vector2D(right, bot));
	} else {
		if (param_vec tag = find_tag(blocks, "\\fs"))
			style.fontsize = tag->front().Get(style.fontsize);
		if (param_vec tag = find_tag(blocks, "\\fn"))
			style.font = tag->front().Get(style.font);

		std::string text = diag->GetStrippedText();
		std::vector<std::string> textlines;
		for (size_t pos = 0; (pos = text.find("\\N", pos)) != std::string::npos; )
			text.replace(pos, 2, "\n");
		agi::Split(textlines, text, '\n');
		for (std::string line : textlines) {
			double linewidth = 0;
			double lineheight = 0;

			double descent;
			double extlead;
			if (!Automation4::CalculateTextExtents(&style, line, linewidth, lineheight, descent, extlead)) {
				// meh... let's make some ballpark estimates
				linewidth = style.fontsize * line.length();
				lineheight = style.fontsize;
			}
			width = std::max(width, linewidth);
			height += lineheight;
		}
		return std::make_pair(Vector2D(0, 0), Vector2D(width, height));
	}
}

void VisualToolBase::GetLineClip(AssDialogue *diag, Vector2D &p1, Vector2D &p2, bool &inverse) {
	inverse = false;

	auto blocks = diag->ParseTags();
	param_vec tag = find_tag(blocks, "\\iclip");
	if (tag)
		inverse = true;
	else
		tag = find_tag(blocks, "\\clip");

	if (tag && tag->size() == 4) {
		p1 = vec_or_bad(tag, 0, 1);
		p2 = vec_or_bad(tag, 2, 3);
	}
	else {
		p1 = Vector2D(0, 0);
		p2 = script_res - 1;
	}
}

std::string VisualToolBase::GetLineVectorClip(AssDialogue *diag, int &scale, bool &inverse) {
	auto blocks = diag->ParseTags();

	scale = 1;
	inverse = false;

	param_vec tag = find_tag(blocks, "\\iclip");
	if (tag)
		inverse = true;
	else
		tag = find_tag(blocks, "\\clip");

	if (tag && tag->size() == 4) {
		return agi::format("m %.2f %.2f l %.2f %.2f %.2f %.2f %.2f %.2f"
			, (*tag)[0].Get<double>(), (*tag)[1].Get<double>()
			, (*tag)[2].Get<double>(), (*tag)[1].Get<double>()
			, (*tag)[2].Get<double>(), (*tag)[3].Get<double>()
			, (*tag)[0].Get<double>(), (*tag)[3].Get<double>());
	}
	if (tag) {
		scale = std::max((*tag)[0].Get(scale), 1);
		return (*tag)[1].Get<std::string>("");
	}

	return "";
}

void VisualToolBase::SetSelectedOverride(std::string const& tag, std::string const& value) {
	for (auto line : c->selectionController->GetSelectedSet())
		SetOverride(line, tag, value);
}

void VisualToolBase::RemoveOverride(AssDialogue *line, std::string const& tag) {
	if (!line) return;
	modified_lines.insert(line);
	auto blocks = line->ParseTags();
	for (auto ovr : blocks | agi::of_type<AssDialogueBlockOverride>()) {
		for (size_t i = 0; i < ovr->Tags.size(); i++) {
			if (tag == ovr->Tags[i].Name) {
				ovr->Tags.erase(ovr->Tags.begin() + i);
				i--;
			}
		}
	}
	line->UpdateText(blocks);
}

void VisualToolBase::SetOverride(AssDialogue* line, std::string const& tag, std::string const& value) {
	if (!line) return;
	modified_lines.insert(line);

	std::string removeTag;
	std::string removeTag2;
	if (tag == "\\1c") removeTag = "\\c";
	else if (tag == "\\frz") removeTag = "\\fr";
	else if (tag == "\\pos") removeTag = "\\move";
	else if (tag == "\\move") removeTag = "\\pos";
	else if (tag == "\\clip") removeTag = "\\iclip";
	else if (tag == "\\iclip") removeTag = "\\clip";
	else if (tag == "\\xbord" || tag == "\\ybord") removeTag = "\\bord";
	else if (tag == "\\xshad" || tag == "\\yshad") removeTag = "\\shad";
	else if (tag == "\\bord") { removeTag = "\\xbord"; removeTag2 = "\\ybord"; }
	else if (tag == "\\shad") { removeTag = "\\xshad"; removeTag2 = "\\yshad"; }

	// Get block at start
	auto blocks = line->ParseTags();
	AssDialogueBlock *block = blocks.front().get();

	if (block->GetType() == AssBlockType::OVERRIDE) {
		auto ovr = static_cast<AssDialogueBlockOverride*>(block);
		// Remove old of same
		for (size_t i = 0; i < ovr->Tags.size(); i++) {
			std::string const& name = ovr->Tags[i].Name;
			if (tag == name || removeTag == name || removeTag2 == name) {
				ovr->Tags.erase(ovr->Tags.begin() + i);
				i--;
			}
		}
		ovr->AddTag(tag + value);

		line->UpdateText(blocks);
	}
	else
		line->Text = agi::Str("{", tag, value, "}", line->Text.get());
}

// If only export worked
template class VisualTool<VisualDraggableFeature>;
template class VisualTool<ClipCorner>;
template class VisualTool<VisualToolDragDraggableFeature>;
template class VisualTool<VisualToolPerspectiveDraggableFeature>;
template class VisualTool<VisualToolVectorClipDraggableFeature>;
