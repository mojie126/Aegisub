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

/// @file visual_tool_perspective.h
/// @see visual_tool_perspective.cpp
/// @ingroup visual_ts
///

#include "visual_feature.h"
#include "visual_tool.h"
#include "options.h"

#include <map>

class wxCommandEvent;
class wxToolBar;

/// Button IDs
enum VisualToolPerspectiveSetting {
	PERSP_OUTER = 1 << 0,
	PERSP_LOCK_OUTER = 1 << 1,
	PERSP_GRID = 1 << 2,
	PERSP_LAST = 1 << 3,    // End of simple toggle-able options
	PERSP_ORGMODE_CENTER = 0 << 4,    // Always puts \org at the center of the quad. Default.
	PERSP_ORGMODE_NOFAX = 1 << 4,     // Picks a position for \org where \fax = 0, when possible
	PERSP_ORGMODE_KEEP = 2 << 4,      // Takes the previous \org position as \org
    PERSP_ORGMODE = PERSP_ORGMODE_CENTER | PERSP_ORGMODE_NOFAX | PERSP_ORGMODE_KEEP,
};

class VisualToolPerspective;

class VisualToolPerspectiveDraggableFeature final : public VisualDraggableFeature {
	VisualToolPerspective *tool;

public:
	int group;
	int index;

	VisualToolPerspectiveDraggableFeature(VisualToolPerspective *tool, int group, int index);
	void UpdateDrag(Vector2D d, bool single_axis);
};

class VisualToolPerspective final : public VisualTool<VisualToolPerspectiveDraggableFeature> {
	wxToolBar *toolBar = nullptr; /// The subtoolbar
	int settings = 0;

	agi::OptionValue* optOuter;
	agi::OptionValue* optOuterLocked;
	agi::OptionValue* optGrid;
	agi::OptionValue* optOrgMode;

	/// 单个对话行的透视状态
	struct LineState {
		// All current transform coefficients. Used for drawing the grid.
		float angle_x = 0.f;
		float angle_y = 0.f;
		float angle_z = 0.f;

		float fax = 0.f;
		float fay = 0.f;

		int align = 0;

		// Corners of the bounding box of the event without any formatting.
		// The top left corner is the zero vector for text but might not be for drawings.
		std::pair<Vector2D, Vector2D> bbox;

		Vector2D fsc;

		Vector2D org;
		Vector2D pos;

		// Store these here to reduce rounding errors compounding on updates
		Vector2D bord;
		Vector2D shad;

		// Corner coordinates of the transform quad relative to the ambient quad.
		Vector2D c1 = Vector2D(.25, .25);
		Vector2D c2 = Vector2D(.75, .75);

		Feature *centerf = nullptr;
		Feature *orgf = nullptr;

		std::vector<Feature *> inner_corners;
		std::vector<Feature *> outer_corners;

		std::vector<Vector2D> old_inner;
		std::vector<Vector2D> old_outer;
	};

	/// 选中及活动可见对话行到透视状态的映射
	std::map<AssDialogue*, LineState> line_states;

	/// @brief 预计算的网格基础点坐标（不含偏移）
	std::vector<float> grid_base_points;
	/// @brief 预计算的网格顶点alpha分量（RGB在绘制时填充）
	std::vector<float> grid_base_alphas;
	/// @brief 网格是否已初始化
	bool grid_initialized = false;

	inline float screenZ() const;

	std::vector<Vector2D> FeaturePositions(std::vector<Feature *> features) const;
    void UpdateInner(LineState& state);
    void UpdateOuter(LineState& state);
    void TextToPersp(LineState& state, AssDialogue *line);
    bool InnerToText(LineState& state);

    void WrapSetOverride(AssDialogue* line, std::string const& tag, float value, int precision, float defaultval=0);

	void OnMouseEvent(wxMouseEvent &event) override;
	void DoRefresh() override;
	void OnSelectionChanged() override;
	// 活动行切换不改变特征集（由选中集决定），避免点击特征时重建销毁拖拽目标
	void OnLineChanged() override { }
	void OnFrameChanged() override { RebuildFeatures(); }
	void Draw() override;
	void OnDoubleClick() override;
	void UpdateDrag(Feature *feature) override;
	void EndDrag(Feature *feature) override;
	/// @brief 重建所有选中及活动可见行的特征与状态
	void RebuildFeatures();
	void SetFeaturePositions(LineState& state);
	void SaveFeaturePositions(LineState& state);
	void SaveOuterToLines(LineState& state);

	void AddTool(std::string command_name, VisualToolPerspectiveSetting mode);

public:
	bool ctrl_down = false;
	bool shift_down = false;
	bool alt_down = false;

	/// @brief 获取指定行对应的透视状态，行无效时回退到活动行状态
	/// @param line 特征所属对话行
	LineState& GetLineState(AssDialogue *line);

	VisualToolPerspective(VideoDisplay *parent, agi::Context *context);

	bool HasOuter();
	bool OuterLocked();
	int GetOrgMode();
	bool HasOrgf();

	void SetToolbar(wxToolBar *tb) override;
	void OnSubTool(wxCommandEvent &);
	void SetSubTool(int subtool) override;
	int GetSubTool() override;
};
