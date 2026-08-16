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

#pragma once

#include "gl_wrap.h"
#include "vector2d.h"
#include "options.h"

#include <libaegisub/owning_intrusive_list.h>
#include <libaegisub/signal.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include <wx/event.h>

class AssDialogue;
class VideoDisplay;
class wxMouseCaptureLostEvent;
class wxMouseEvent;
class wxToolBar;
namespace agi {
	struct Context;
	class OptionValue;
}

/// @class VisualToolBase
/// @brief Base class for visual tools containing all functionality that doesn't interact with features
///
/// This is required so that visual tools can be used polymorphically, as
/// different VisualTool<T>s are unrelated types otherwise. In addition, as much
/// functionality as possible is implemented here to avoid having four copies
/// of each method for no good reason (and four times as many error messages)
class VisualToolBase {
	void SetResolutions();
	void OnCommit(int type);
	void OnSeek(int new_frame);

	void OnMouseCaptureLost(wxMouseCaptureLostEvent &);

	/// @brief Get the dialogue line currently in the edit box
	/// @return nullptr if the line is not active on the current frame
	AssDialogue *GetActiveDialogueLine();

	// SubtitleSelectionListener implementation
	void OnActiveLineChanged(AssDialogue *new_line);

	// Below here are the virtuals that must be implemented

	/// Called when the script, video or screen resolutions change
	virtual void OnCoordinateSystemsChanged() { DoRefresh(); }

	/// Called when the file is changed by something other than a visual tool
	virtual void OnFileChanged() { DoRefresh(); }

	/// Called when the frame number changes
	virtual void OnFrameChanged() { }

	/// Called when the active line changes
	virtual void OnLineChanged() { DoRefresh(); }

	/// Generic refresh to simplify tools which have no interesting state and
	/// can simply do the same thing for any external change (i.e. most of
	/// them). Called only by the above virtual methods.
	virtual void DoRefresh() { }

protected:
	std::vector<agi::signal::Connection> connections;

	OpenGLWrapper gl;

	/// Called when the user double-clicks
	virtual void OnDoubleClick() { }

	/// @brief 鼠标捕获丢失时清理拖拽状态（由 OnMouseCaptureLost 调用）
	/// 派生类应在此清理 active_feature、sel_features 等拖拽相关状态
	virtual void OnDragCleanup() { }

	/// @brief 获取指定对话行特征使用的填充基色，多行同时显示时用于区分不同行
	/// @param line 特征所属对话行，可能为 nullptr
	virtual wxColour GetPerLineBaseColor(AssDialogue *line) const;

	/// @brief 获取指定对话行特征使用的线条色（轮廓/十字线），多行同时显示时用于区分不同行
	/// @param line 特征所属对话行，可能为 nullptr
	virtual wxColour GetPerLineOutlineColor(AssDialogue *line) const;

	/// @brief 获取指定对话行特征使用的连接线颜色，多行同时显示时用于区分不同行
	/// @param line 特征所属对话行，可能为 nullptr
	virtual wxColour GetPerLineColor(AssDialogue *line) const;

	/// @brief 将 point 限制在视频可见区域内（基于 Draw 坐标系）
	/// @param margin 从边缘内缩的像素数，用于确保锚点完整可见
	Vector2D ClampToVideo(Vector2D point, int margin = 0) const {
		return point.Max(video_pos + margin).Min(video_pos + video_res - margin);
	}

	agi::Context *c;
	VideoDisplay *parent;

	bool holding = false; ///< Is a hold currently in progress?
	AssDialogue *active_line = nullptr; ///< Active dialogue line; nullptr if it is not visible on the current frame
	bool dragging = false; ///< Is a drag currently in progress?

	int frame_number; ///< Current frame number

	bool shift_down = false; ///< Is shift down?
	bool ctrl_down = false; ///< Is ctrl down?
	bool alt_down = false; ///< Is alt down?

	Vector2D mouse_pos; ///< Last seen mouse position
	Vector2D drag_start; ///< Mouse position at the beginning of the last drag
	Vector2D script_res; ///< Script resolution
	Vector2D layout_res; ///< Layout resolution
	Vector2D video_pos; ///< Top-left corner of the video in the display area
	Vector2D video_res; ///< Video resolution
	Vector2D client_size; ///< The size of the display area

	const agi::OptionValue *highlight_color_primary_opt;
	const agi::OptionValue *highlight_color_secondary_opt;
	const agi::OptionValue *line_color_primary_opt;
	const agi::OptionValue *line_color_secondary_opt;
	const agi::OptionValue *shaded_area_alpha_opt;

	agi::signal::Connection file_changed_connection;
	int commit_id = -1; ///< Last used commit id for coalescing

	/// 当前提交周期内被 SetOverride/RemoveOverride 修改过的对话行集合
	std::set<AssDialogue*> modified_lines;

	/// @brief Commit the current file state
	/// @param message Description of changes for undo
	virtual void Commit(wxString message = wxString());
	bool IsDisplayed(AssDialogue *line) const;

	/// Get the line's position if it's set, or it's default based on style if not
	Vector2D GetLinePosition(AssDialogue *diag);
	/// Get the line's origin if it's set, or Vector2D::Bad() if not
	Vector2D GetLineOrigin(AssDialogue *diag);
	bool GetLineMove(AssDialogue *diag, Vector2D &p1, Vector2D &p2, int &t1, int &t2);
	void GetLineRotation(AssDialogue *diag, float &rx, float &ry, float &rz);
	void GetLineShear(AssDialogue *diag, float& fax, float& fay);
	void GetLineScale(AssDialogue *diag, Vector2D &scale);
	void GetLineOutline(AssDialogue *diag, Vector2D &outline);
	void GetLineShadow(AssDialogue *diag, Vector2D &shadow);
	float GetLineFontSize(AssDialogue *diag);
	int GetLineAlignment(AssDialogue *diag);
	/// @brief Compute text extents of the given line without any formatting
	/// @param diag The dialogue line
	/// @return The top left and bottom right corners of the line's bounding box respectively.
	///
	/// Formatting tags are stripped and \fs tags are respected, but \fscx and \fscy are kept as 100 even if
	/// they are different in the style.
	/// For text the top left corner of the bounding box will always be at the origin, but this needn't be
	/// the case for drawings. The width and height of the bounding box are the shifts used for text alignment.
	///
	///	This function works for most common line formats, but can be inaccurate for more complex cases such as lines
	///	containing both text and drawings.
	/// Returns a rough estimate when getting the precise extents fails
	std::pair<Vector2D, Vector2D> GetLineBaseExtents(AssDialogue *diag);
	void GetLineClip(AssDialogue *diag, Vector2D &p1, Vector2D &p2, bool &inverse);
	std::string GetLineVectorClip(AssDialogue *diag, int &scale, bool &inverse);

	void RemoveOverride(AssDialogue *line, std::string const& tag);
	void SetOverride(AssDialogue* line, std::string const& tag, std::string const& value);
	void SetSelectedOverride(std::string const& tag, std::string const& value);

	VisualToolBase(VideoDisplay *parent, agi::Context *context);

public:
	/// Convert a point from video to script coordinates
	Vector2D ToScriptCoords(Vector2D point) const;
	/// Convert a point from script to video coordinates
	Vector2D FromScriptCoords(Vector2D point) const;

	// Stuff called by VideoDisplay
	virtual void OnMouseEvent(wxMouseEvent &event)=0;
	virtual void Draw()=0;
	/// @brief 由VideoDisplay调用，设置GL坐标系中的画布大小（即逻辑wx坐标）
	virtual void SetClientSize(int w, int h);
	/// @brief 由VideoDisplay调用，设置视频在画布中的位置和大小（GL坐标系）
	virtual void SetDisplayArea(int x, int y, int w, int h);
	virtual void SetToolbar(wxToolBar *) { }
	virtual void SetSubTool(int subtool) { }
	virtual int GetSubTool() { return 0; }
	/// @brief 由 VideoDisplay 转发键盘事件，返回 true 表示事件已被工具消费
	virtual bool OnKeyDown(wxKeyEvent &event) { return false; }

	/// @brief 选中集变化时由基类调用，默认不处理，派生类可重写以重建多行特征
	virtual void OnSelectionChanged() { }

	virtual ~VisualToolBase() = default;
};

/// @brief 计算与已分配色相的最小环上色相差（0..255 域，0 与 255 相邻）
inline int HueRingDistance(int a, int b) {
	int d = std::abs(a - b);
	return std::min(d, 256 - d);
}

/// @brief 为指定行选择色相，近邻行在色环上错开、反差尽量大
/// @param line_pos 当前行锚点位置（视频像素坐标）
/// @param assigned_pos 已分配行的锚点位置列表
/// @param assigned_hues 已分配行的色相列表（0..255，与 assigned_pos 一一对应）
/// @return 色相值 0..255
inline int SelectPerLineHue(Vector2D line_pos, std::vector<Vector2D> const& assigned_pos, std::vector<int> const& assigned_hues) {
	// 近邻判定距离（像素），超过该距离的行不参与色差约束
	static const float near_threshold = 250.f;
	// 孤立行使用的最小色相差，保证错开且不过度消耗色相
	static const int min_spread = 32;

	// 近邻行参与最大最小化：最大化与所有近邻的最小色差
	int best_hue = 0;
	int best_score = -1;
	bool has_near = false;
	for (int h = 0; h < 256; h += 4) {
		int score = 256;
		bool constrained = false;
		for (size_t j = 0; j < assigned_hues.size(); ++j) {
			float d = (line_pos - assigned_pos[j]).Len();
			if (d > near_threshold) continue;
			constrained = true;
			score = std::min(score, HueRingDistance(h, assigned_hues[j]));
		}
		if (!constrained) continue;
		has_near = true;
		if (score > best_score) {
			best_score = score;
			best_hue = h;
		}
	}
	if (has_near) return best_hue;

	// 无近邻（孤立行）：取与所有已分配行色差足够大的最小未用色相
	for (int h = 0; h < 256; h += 4) {
		bool ok = true;
		for (int hue : assigned_hues) {
			if (HueRingDistance(h, hue) < min_spread) {
				ok = false;
				break;
			}
		}
		if (ok) return h;
	}
	return 0;
}

/// Visual tool base class containing all common feature-related functionality
template<class FeatureType>
class VisualTool : public VisualToolBase {
protected:
	typedef FeatureType Feature;
	typedef agi::owning_intrusive_list<FeatureType> feature_list;

private:
	bool sel_changed = false; /// Has the selection already been changed in the current click?

	/// @brief Called when a hold is begun
	/// @return Should the hold actually happen?
	virtual bool InitializeHold() { return false; }
	/// @brief Called on every mouse event during a hold
	virtual void UpdateHold() { }
	/// @brief Called when the hold ended
	virtual void EndHold() { }

	/// @brief Called at the beginning of a drag
	/// @param feature The visual feature clicked on
	/// @return Should the drag happen?
	virtual bool InitializeDrag(FeatureType *feature) { return true; }
	/// @brief Called on every mouse event during a drag
	/// @param feature The current feature to process; not necessarily the one clicked on
	virtual void UpdateDrag(FeatureType *feature) { }
	/// @brief Called at the end of a drag
	/// @param feature The current feature to process; not necessarily the one clicked on
	virtual void EndDrag(FeatureType *feature) { }

protected:
	std::set<FeatureType *> sel_features; ///< Currently selected visual features

	/// Topmost feature under the mouse; generally only valid during a drag
	FeatureType *active_feature = nullptr;
	/// List of features which are drawn and can be clicked on
	/// List is used here for the iterator invalidation properties
	feature_list features;

	/// 特征所属对话行到色相的映射，多行同屏时按空间位置贪心分配
	std::map<AssDialogue*, int> line_hue_map;

	/// 上次计算色相时的特征行集合，用于跳过无变化的重复计算
	std::set<AssDialogue*> line_hue_lines;

	/// @brief 重建按行色相映射，每行取第一个特征的位置为锚点，近邻行色差大
	/// 仅当特征行集合变化时重算，拖动中位置变化不改变颜色分配
	void UpdateLineHueMap();

	/// @brief 获取指定行的特征填充色，多行时返回按行分配的互补纯色
	wxColour GetPerLineBaseColor(AssDialogue *line) const override;

	/// @brief 获取指定行的特征轮廓色，多行时返回按行分配的互补纯色
	wxColour GetPerLineOutlineColor(AssDialogue *line) const override;

	/// @brief 获取指定行的连接线颜色，多行时返回按行分配的互补纯色
	wxColour GetPerLineColor(AssDialogue *line) const override;

	/// Draw all of the features in the list
	void DrawAllFeatures();

	/// @brief Remove a feature from the selection
	/// @param i Index in the feature list
	/// Also deselects lines if all features for that line have been deselected
	void RemoveSelection(FeatureType *feat);

	/// @brief Set the selection to a single feature, deselecting everything else
	/// @param i Index in the feature list
	void SetSelection(FeatureType *feat, bool clear);

public:
	/// @brief Handler for all mouse events
	/// @param event Shockingly enough, the mouse event
	void OnMouseEvent(wxMouseEvent &event) override;

	/// @brief 鼠标捕获丢失时清理拖拽选择集和活动特征
	void OnDragCleanup() override;

	/// @brief Constructor
	/// @param parent The VideoDisplay to use for coordinate conversion
	/// @param video Video and mouse information passing blob
	VisualTool(VideoDisplay *parent, agi::Context *context);
};
