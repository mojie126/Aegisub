// Copyright (c) 2005-2010, Rodrigo Braz Monteiro
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file video_display.h
/// @see video_display.cpp
/// @ingroup video main_ui
///

#include <libaegisub/signal.h>

#include "vector2d.h"
#include "visual_tool_vector_clip.h"

#include <memory>
#include <typeinfo>
#include <vector>
#include <wx/glcanvas.h>

// Prototypes
class VideoController;
class VideoOutGL;
class VisualToolBase;
class wxComboBox;
class wxTextCtrl;
class wxToolBar;
struct FrameReadyEvent;
struct VideoFrame;

namespace agi {
	struct Context;
	class OptionValue;
}

class VideoDisplay final : public wxGLCanvas {
	/// Signals the display is connected to
	std::vector<agi::signal::Connection> connections;

	const agi::OptionValue* autohideTools;

	agi::Context *con;

	std::unique_ptr<wxMenu> context_menu;

	/// 当前窗口缩放级别下理想视口的物理像素大小。
	/// 包含黑边区域（letter/pillarboxing），不受内容缩放和平移影响。
	///
	/// 通常等于客户区大小（乘以 @ref scale_factor），但实际客户区
	/// 大小由窗口布局控制，可能大于或小于理想视口大小。
	///
	/// 在自由尺寸模式下，窗口缩放级别随客户区大小变化而调整，
	/// 因此视口大小应始终等于客户区大小。
	wxSize videoSize;

	Vector2D last_mouse_pos, mouse_pos;

	/// 从视口左边缘到视频左边缘的物理像素距离（向右为正）
	int viewport_left = 0;
	/// 视频缩放后的物理像素宽度（忽略视口裁剪）
	int viewport_width = 0;
	/// 从视口底边缘到视频底边缘的物理像素距离（向上为正）；传递给 @ref VideoOutGL::Render
	int viewport_bottom = 0;
	/// 从视口顶边缘到视频顶边缘的物理像素距离（向下为正）
	int viewport_top = 0;
	/// 视频缩放后的物理像素高度（忽略视口裁剪）
	int viewport_height = 0;

	/// 当前窗口缩放级别，即视口大小与原始视频分辨率的比值
	double windowZoomValue;

	/// The last position of the mouse, when dragging
	Vector2D pan_last_pos;
	/// True if middle mouse button is down, and we should update pan_{x,y}
	bool panning = false;

	/// 视口内视频的缩放级别
	double videoZoomValue = 1;

	double videoZoomAtGestureStart = 1;

	/// 缩放手势开始时的锚点（视频相对坐标系，与缩放/平移无关）
	Vector2D zoomGestureAnchorPoint = {0, 0};

	/// 视频平移量，以视口高度为单位
	/// @see videoSize
	double pan_x = 0;
	double pan_y = 0;

	/// The video renderer
	std::unique_ptr<VideoOutGL> videoOut;

	/// The active visual typesetting tool
	std::unique_ptr<VisualToolBase> tool;
	/// The toolbar used by individual typesetting tools
	wxToolBar* toolBar;

	/// The OpenGL context for this display
	std::unique_ptr<wxGLContext> glContext;

	/// The dropdown box for selecting zoom levels
	wxComboBox *zoomBox;

	/// Whether the display can be freely resized by the user
	bool freeSize;

	/// Frame which will replace the currently visible frame on the next render
	std::shared_ptr<VideoFrame> pending_frame;

	int scale_factor;

	/// @brief Draw an overscan mask
	/// @param horizontal_percent The percent of the video reserved horizontally
	/// @param vertical_percent The percent of the video reserved vertically
	void DrawOverscanMask(float horizontal_percent, float vertical_percent) const;

	/// Upload the image for the current frame to the video card
	void UploadFrameData(FrameReadyEvent&);

	/// @brief Initialize the gl context and set the active context to this one
	/// @return Could the context be set?
	bool InitContext();

	/// @brief 根据当前窗口缩放级别和视频分辨率重新计算视口大小，然后调整客户区以匹配视口
	void FitClientSizeToVideo();
	/// @brief 根据当前视口大小、内容缩放和平移更新视频位置和尺寸
	///
	/// 更新 @ref viewport_left, @ref viewport_width, @ref viewport_bottom, @ref viewport_top 和 @ref viewport_height
	void PositionVideo();
	/// Set the zoom level to that indicated by the dropdown
	void SetZoomFromBox(wxCommandEvent&);
	/// Set the zoom level to that indicated by the text
	void SetZoomFromBoxText(wxCommandEvent&);

	/// @brief Key event handler
	void OnKeyDown(wxKeyEvent &event);
	/// @brief Mouse event handler
	void OnMouseEvent(wxMouseEvent& event);
	void OnMouseWheel(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnGestureZoom(wxZoomGestureEvent& event);
	/// @brief Recalculate video positioning and scaling when the available area or zoom changes
	void OnSizeEvent(wxSizeEvent &event);
	void OnContextMenu(wxContextMenuEvent&);

	/// @brief 平移视频
	/// @param delta 物理像素单位的偏移量
	void Pan(Vector2D delta);

	/// @brief 将客户区位置转换为缩放锚点
	///
	/// 锚点是视频帧中的一个位置，在缩放过程中应始终保持不动。
	/// 本函数返回视频相对坐标系中的锚点，确保在视频缩放和平移时能准确追踪该位置。
	///
	/// @param position 物理像素单位的客户区位置
	/// @return 可用于 @ref ZoomAndPan() 的锚点
	Vector2D GetZoomAnchorPoint(wxPoint position);

	/// @brief 使用锚点进行缩放和平移
	///
	/// 使用前需先通过 @ref GetZoomAnchorPoint() 获取锚点。
	///
	/// 若 @p newPosition 等于锚点当前位置，则以锚点为不动点进行缩放。
	/// 若 @p newPosition 不同于锚点当前位置，则额外平移视频使锚点移至新位置。
	///
	/// @param newZoomValue 新的缩放值
	/// @param anchorPoint 通过 @ref GetZoomAnchorPoint() 获得的锚点
	/// @param newPosition 锚点的新客户区位置（物理像素单位）
	void ZoomAndPan(double newZoomValue, Vector2D anchorPoint, wxPoint newPosition);

public:
	/// @brief Constructor
	VideoDisplay(
		wxToolBar *visualSubToolBar,
		bool isDetached,
		wxComboBox *zoomBox,
		wxWindow* parent,
		agi::Context *context);
	~VideoDisplay();

	/// @brief Render the currently visible frame
	void Render();

	/// @brief Set the zoom level
	/// @param value The new zoom level
	void SetWindowZoom(double value);
	/// @brief 通过步进值调整视频缩放
	/// @param step 缩放步数（正值放大，负值缩小）
	void SetVideoZoom(int step);
	/// @brief Get the current zoom level
	double GetZoom() const { return windowZoomValue; }

	/// @brief Enable/Disable HDR to SDR tone mapping
	/// @param enable Whether to enable HDR tone mapping
	void SetHDRMapping(bool enable);

	/// @brief Reset the video pan
	void ResetPan();

	/// @brief 重置内容缩放和平移
	void ResetVideoZoom();

	/// Get the last seen position of the mouse in script coordinates
	Vector2D GetMousePosition() const;

	void SetTool(std::unique_ptr<VisualToolBase> new_tool);

	void SetSubTool(int subtool) const { tool->SetSubTool(subtool); };

	bool ToolIsType(std::type_info const& type) const;

	int GetSubTool() const { return tool->GetSubTool(); };

	/// Discard all OpenGL state
	void Unload();
};
