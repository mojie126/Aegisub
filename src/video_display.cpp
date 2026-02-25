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

/// @file video_display.cpp
/// @brief Control displaying a video frame obtained from the video context
/// @ingroup video main_ui
///

#include "video_display.h"

#include "ass_file.h"
#include "async_video_provider.h"
#include "command/command.h"
#include "compat.h"
#include "format.h"
#include "include/aegisub/context.h"
#include "include/aegisub/hotkey.h"
#include "include/aegisub/menu.h"
#include "options.h"
#include "project.h"
#include "spline_curve.h"
#include "utils.h"
#include "video_out_gl.h"
#include "video_controller.h"
#include "visual_tool.h"


#include <algorithm>
#include <cctype>
#include <limits>
#include <wx/combobox.h>
#include <wx/menu.h>
#include <wx/textctrl.h>
#include <wx/toolbar.h>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

namespace {

/// Attribute list for gl canvases; set the canvases to doublebuffered rgba with an 8 bit stencil buffer
wxGLAttributes buildGLAttributes() {
	wxGLAttributes attrs;
	attrs.PlatformDefaults().RGBA().MinRGBA(8, 8, 8, 0).DoubleBuffer().Stencil(8).EndList();
	return attrs;
}

}

/// An OpenGL error occurred while uploading or displaying a frame
class OpenGlException final : public agi::Exception {
public:
	OpenGlException(const char *func, int err)
	: agi::Exception(agi::format("%s failed with error code %d", func, err))
	{ }
};

int GetFFMSPaddingPixels(AsyncVideoProvider *provider) {
	if (!provider || provider->GetDecoderName() != "FFmpegSource")
		return 0;

	auto padding_opt = OPT_GET("Provider/Video/FFmpegSource/ABB")->GetInt();
	if (padding_opt <= 0)
		return 0;
	if (padding_opt > std::numeric_limits<int>::max())
		return std::numeric_limits<int>::max();
	return static_cast<int>(padding_opt);
}

#define E(cmd) cmd; if (GLenum err = glGetError()) throw OpenGlException(#cmd, err)

VideoDisplay::VideoDisplay(wxToolBar *toolbar, bool freeSize, wxComboBox *zoomBox, wxWindow *parent, agi::Context *c)
: wxGLCanvas(parent, buildGLAttributes())
, autohideTools(OPT_GET("Tool/Visual/Autohide"))
, con(c)
, windowZoomValue(OPT_GET("Video/Default Zoom")->GetInt() * .125 + .125)
, videoZoomValue(1)
, toolBar(toolbar)
, zoomBox(zoomBox)
, freeSize(freeSize)
, scale_factor(GetContentScaleFactor())
{
	zoomBox->SetValue(fmt_wx("%g%%", windowZoomValue * 100.));
	zoomBox->Bind(wxEVT_COMBOBOX, &VideoDisplay::SetZoomFromBox, this);
	zoomBox->Bind(wxEVT_TEXT_ENTER, &VideoDisplay::SetZoomFromBoxText, this);

	con->videoController->Bind(EVT_FRAME_READY, &VideoDisplay::UploadFrameData, this);
	connections = agi::signal::make_vector({
		con->project->AddVideoProviderListener([this] (AsyncVideoProvider *provider) {
			if (!provider) ResetVideoZoom();
			FitClientSizeToVideo();
		}),
		con->videoController->AddARChangeListener(&VideoDisplay::FitClientSizeToVideo, this),
	});

	// 监听图标大小变更，刷新视觉工具子工具栏
	connections.push_back(OPT_SUB("App/Toolbar Icon Size", [this](agi::OptionValue const&) {
		if (tool && toolBar) {
			int subtool = tool->GetSubTool();
			toolBar->Show(false);
			toolBar->ClearTools();
			tool->SetToolbar(toolBar);
			tool->SetSubTool(subtool);
			if (!this->freeSize)
				FitClientSizeToVideo();
			else
				GetGrandParent()->Layout();
		}
	}));

	Bind(wxEVT_PAINT, std::bind(&VideoDisplay::Render, this));
	Bind(wxEVT_SIZE, &VideoDisplay::OnSizeEvent, this);
	Bind(wxEVT_CONTEXT_MENU, &VideoDisplay::OnContextMenu, this);
	Bind(wxEVT_ENTER_WINDOW, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_CHAR_HOOK, &VideoDisplay::OnKeyDown, this);
	Bind(wxEVT_LEAVE_WINDOW, &VideoDisplay::OnMouseLeave, this);
	Bind(wxEVT_LEFT_DCLICK, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_LEFT_DOWN, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_LEFT_UP, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_MIDDLE_DOWN, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_MIDDLE_UP, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_MOTION, &VideoDisplay::OnMouseEvent, this);
	Bind(wxEVT_MOUSEWHEEL, &VideoDisplay::OnMouseWheel, this);

	Bind(wxEVT_DPI_CHANGED, [this] (wxDPIChangedEvent &e) {
		double new_zoom = windowZoomValue * GetContentScaleFactor() / scale_factor;
		scale_factor = GetContentScaleFactor();
		SetWindowZoom(new_zoom);
		e.Skip();
	});

	SetCursor(wxNullCursor);

	c->videoDisplay = this;

	con->videoController->JumpToFrame(con->videoController->GetFrameN());

	SetLayoutDirection(wxLayout_LeftToRight);
}

VideoDisplay::~VideoDisplay () {
	Unload();
	con->videoController->Unbind(EVT_FRAME_READY, &VideoDisplay::UploadFrameData, this);
}

bool VideoDisplay::InitContext() {
	if (!IsShownOnScreen())
		return false;

	// If this display is in a minimized detached dialog IsShownOnScreen will
	// return true, but the client size is guaranteed to be 0
	if (GetClientSize() == wxSize(0, 0))
		return false;

	if (!glContext)
		glContext = std::make_unique<wxGLContext>(this);

	SetCurrent(*glContext);
	return true;
}

void VideoDisplay::UploadFrameData(FrameReadyEvent &evt) {
	pending_frame = evt.frame;
	Render();
}

void VideoDisplay::Render() try {
	if (!con->project->VideoProvider() || !InitContext() || (!videoOut && !pending_frame))
		return;

	if (!videoOut)
		videoOut = std::make_unique<VideoOutGL>();

	if (!tool)
		cmd::call("video/tool/cross", con);

	try {
		if (pending_frame) {
			videoOut->UploadFrameData(*pending_frame);
			pending_frame.reset();
		}
	}
	catch (const VideoOutInitException& err) {
		wxLogError(
			fmt_tl("Failed to initialize video display. Closing other running programs and updating your video card drivers may fix this.\nError message reported: %s",
			err.GetMessage()));
		con->project->CloseVideo();
		return;
	}
	catch (const VideoOutRenderException& err) {
		wxLogError(
			fmt_tl("Could not upload video frame to graphics card.\nError message reported: %s",
			err.GetMessage()));
		return;
	}

	if (videoSize.GetWidth() == 0) videoSize.SetWidth(1);
	if (videoSize.GetHeight() == 0) videoSize.SetHeight(1);

	if (!viewport_height || !viewport_width)
		PositionVideo();

	// 基于视频源的传输特性和元数据检测HDR类型，替代原字符串匹配方式
	const HDRType hdr_type = con->project->VideoProvider()->GetHDRType();
	const bool likely_hdr = (hdr_type != HDRType::SDR);
	videoOut->SetHDRInputHint(likely_hdr, hdr_type);

	int client_w, client_h;
	GetClientSize(&client_w, &client_h);

	videoOut->Render(client_w * scale_factor, client_h * scale_factor, viewport_left, viewport_bottom, viewport_width, viewport_height);

	E(glViewport(0, 0, client_w * scale_factor, client_h * scale_factor));

	E(glMatrixMode(GL_PROJECTION));
	E(glLoadIdentity());
	E(glOrtho(0.0f, std::max(client_w, 1), std::max(client_h, 1), 0.0f, -1000.0f, 1000.0f));

	if (OPT_GET("Video/Overscan Mask")->GetBool()) {
		double ar = con->videoController->GetAspectRatioValue();

		// Based on BBC's guidelines: http://www.bbc.co.uk/guidelines/dq/pdf/tv/tv_standards_london.pdf
		// 16:9 or wider
		if (ar > 1.75) {
			DrawOverscanMask(.1f, .05f);
			DrawOverscanMask(0.035f, 0.035f);
		}
		// Less wide than 16:9 (use 4:3 standard)
		else {
			DrawOverscanMask(.067f, .05f);
			DrawOverscanMask(0.033f, 0.035f);
		}
	}

	if ((mouse_pos || !autohideTools->GetBool()) && tool)
		tool->Draw();

	SwapBuffers();
}
catch (const agi::Exception &err) {
	wxLogError(
		fmt_tl("An error occurred trying to render the video frame on the screen.\nError message reported: %s",
		err.GetMessage()));
	con->project->CloseVideo();
}

void VideoDisplay::DrawOverscanMask(float horizontal_percent, float vertical_percent) const {
	Vector2D v = Vector2D(viewport_width, viewport_height) / scale_factor;
	Vector2D size = Vector2D(horizontal_percent, vertical_percent) * v;

	// Clockwise from top-left
	Vector2D corners[] = {
		size,
		Vector2D(viewport_width / scale_factor - size.X(), size),
		v - size,
		Vector2D(size, viewport_height  / scale_factor - size.Y())
	};

	// Shift to compensate for black bars
	Vector2D pos = Vector2D(viewport_left, viewport_top) / scale_factor;
	for (auto& corner : corners)
		corner = corner + pos;

	int count = 0;
	std::vector<float> points;
	for (size_t i = 0; i < 4; ++i) {
		size_t prev = (i + 3) % 4;
		size_t next = (i + 1) % 4;
		count += SplineCurve(
				(corners[prev] + corners[i] * 4) / 5,
				corners[i], corners[i],
				(corners[next] + corners[i] * 4) / 5)
			.GetPoints(points);
	}

	OpenGLWrapper gl;
	gl.SetFillColour(wxColor(30, 70, 200), .5f);
	gl.SetLineColour(*wxBLACK, 0, 1);

	std::vector<int> vstart(1, 0);
	std::vector<int> vcount(1, count);
	gl.DrawMultiPolygon(points, vstart, vcount, pos, v, true);
}

void VideoDisplay::PositionVideo() {
	auto provider = con->project->VideoProvider();
	if (!provider || !IsShownOnScreen()) return;

	viewport_width = videoSize.GetWidth();
	viewport_height = videoSize.GetHeight();

	// 调整宽高比（仅自由尺寸模式需要）
	if (freeSize) {
		int vidW = provider->GetWidth();
		int vidH = provider->GetHeight();

		AspectRatio arType = con->videoController->GetAspectRatioType();
		double displayAr = double(videoSize.GetWidth()) / videoSize.GetHeight();
		double videoAr = arType == AspectRatio::Default ? double(vidW) / vidH : con->videoController->GetAspectRatioValue();

		// 窗口比视频更宽，左右黑边
		if (displayAr - videoAr > 0.01) {
			viewport_width = viewport_height * videoAr;
		}
		// 视频比窗口更宽，上下黑边
		else if (videoAr - displayAr > 0.01) {
			viewport_height = viewport_width / videoAr;
		}
	}

	// 应用内容缩放
	viewport_width *= videoZoomValue;
	viewport_height *= videoZoomValue;

	// 使用 double 精度居中视频
	double viewport_left_exact = double(videoSize.GetWidth() - viewport_width) / 2;
	double viewport_top_exact = double(videoSize.GetHeight() - viewport_height) / 2;

	// 限制平移范围，防止视频完全离开视口
	double max_pan_x = (0.5 * viewport_width + 0.4 * videoSize.GetWidth()) / videoSize.GetHeight();
	double max_pan_y = (0.5 * viewport_height + 0.4 * videoSize.GetHeight()) / videoSize.GetHeight();
	pan_x = mid(-max_pan_x, pan_x, max_pan_x);
	pan_y = mid(-max_pan_y, pan_y, max_pan_y);

	// 应用平移（平移单位为视口高度的比例）
	viewport_left_exact += pan_x * videoSize.GetHeight();
	viewport_top_exact += pan_y * videoSize.GetHeight();

	viewport_left = std::round(viewport_left_exact);
	viewport_top = std::round(viewport_top_exact);
	viewport_bottom = GetClientSize().GetHeight() * scale_factor - viewport_height - viewport_top;

	if (tool) {
		int client_w = GetClientSize().GetWidth() * scale_factor;
		int client_h = GetClientSize().GetHeight() * scale_factor;
		tool->SetClientSize(client_w, client_h);
		tool->SetDisplayArea(viewport_left / scale_factor, viewport_top / scale_factor,
		                     viewport_width / scale_factor, viewport_height / scale_factor);
	}
	Render();
}

void VideoDisplay::FitClientSizeToVideo() {
	auto provider = con->project->VideoProvider();

	if (!provider || !IsShownOnScreen()) return;

	videoSize.Set(provider->GetWidth(), provider->GetHeight());
	videoSize *= windowZoomValue;
	if (con->videoController->GetAspectRatioType() != AspectRatio::Default)
		videoSize.SetWidth(videoSize.GetHeight() * con->videoController->GetAspectRatioValue());

	wxEventBlocker blocker(this);
	if (freeSize) {
		wxWindow *top = GetParent();
		while (!top->IsTopLevel()) top = top->GetParent();

		wxSize oldClientSize = GetClientSize();
		double csAr = (double)oldClientSize.GetWidth() / (double)oldClientSize.GetHeight();
		wxSize newClientSize = wxSize(std::lround(provider->GetHeight() * csAr), provider->GetHeight()) * windowZoomValue / scale_factor;
		wxSize oldSize = top->GetSize();
		top->SetSize(oldSize + (newClientSize - oldClientSize));
		SetClientSize(oldClientSize + (top->GetSize() - oldSize));
	}
	else {
		SetMinClientSize(videoSize / scale_factor);
		SetMaxClientSize(videoSize / scale_factor);

		GetGrandParent()->Layout();
	}

	PositionVideo();
}

void VideoDisplay::OnSizeEvent(wxSizeEvent &) {
	if (freeSize) {
		/* 无论是否有缩放/平移，视口大小始终等于客户区大小 */
		videoSize = GetClientSize() * scale_factor;
		windowZoomValue = double(GetClientSize().GetHeight() * scale_factor) / con->project->VideoProvider()->GetHeight();
		zoomBox->ChangeValue(fmt_wx("%g%%", windowZoomValue * 100.));
		con->ass->Properties.video_zoom = windowZoomValue;
		FitClientSizeToVideo();
	}
	else {
		PositionVideo();
	}
}

void VideoDisplay::OnMouseEvent(wxMouseEvent& event) {
	if (event.ButtonDown())
		SetFocus();

	last_mouse_pos = mouse_pos = event.GetPosition();

	if (event.GetButton() == wxMOUSE_BTN_MIDDLE) {
		if ((panning = event.ButtonDown()))
			pan_last_pos = event.GetPosition();
	}
	if (panning && event.Dragging()) {
		Pan((Vector2D(event.GetPosition()) - pan_last_pos) * scale_factor);
		pan_last_pos = event.GetPosition();
	}

	if (tool)
		tool->OnMouseEvent(event);
}

void VideoDisplay::OnMouseLeave(wxMouseEvent& event) {
	mouse_pos = Vector2D();
	if (tool)
		tool->OnMouseEvent(event);
}

void VideoDisplay::OnMouseWheel(wxMouseEvent& event) {
	if (int wheel = event.GetWheelRotation()) {
		if (ForwardMouseWheelEvent(this, event) && !OPT_GET("Video/Disable Scroll Zoom")->GetBool()) {
			if (OPT_GET("Video/Reverse Zoom")->GetBool()) {
				wheel = -wheel;
			}
			if (event.ControlDown() == OPT_GET("Video/Default to Video Zoom")->GetBool()) {
				SetWindowZoom(windowZoomValue + .125 * (wheel / event.GetWheelDelta()));
			} else {
				double newZoomValue = videoZoomValue * (1 + .125 * wheel / event.GetWheelDelta());
				wxPoint scaled_position = event.GetPosition() * scale_factor;
				ZoomAndPan(newZoomValue, GetZoomAnchorPoint(scaled_position), scaled_position);
			}
		}
	}
}

void VideoDisplay::OnContextMenu(wxContextMenuEvent&) {
	if (!context_menu) context_menu = menu::GetMenu("video_context", (wxID_HIGHEST + 1) + 9000, con);
	SetCursor(wxNullCursor);
	menu::OpenPopupMenu(context_menu.get(), this);
}

void VideoDisplay::OnKeyDown(wxKeyEvent &event) {
	hotkey::check("Video", con, event);
}

void VideoDisplay::ResetPan() {
	pan_x = pan_y = 0;
	videoZoomValue = 1;
	FitClientSizeToVideo();
}

void VideoDisplay::SetWindowZoom(double value) {
	if (value == 0) return;
	value = std::max(value, .125);
	windowZoomValue = value;
	size_t selIndex = windowZoomValue / .125 - 1;
	if (selIndex < zoomBox->GetCount())
		zoomBox->SetSelection(selIndex);
	zoomBox->ChangeValue(fmt_wx("%g%%", windowZoomValue * 100.));
	con->ass->Properties.video_zoom = windowZoomValue;
	FitClientSizeToVideo();
}

void VideoDisplay::SetVideoZoom(int step) {
	if (step == 0) return;
	double newVideoZoom = videoZoomValue * (1 + .125 * step);
	wxPoint scaled_position = !last_mouse_pos
		? wxPoint(videoSize.GetWidth() / 2, videoSize.GetHeight() / 2)
		: wxPoint(last_mouse_pos.X() * scale_factor, last_mouse_pos.Y() * scale_factor);
	ZoomAndPan(newVideoZoom, GetZoomAnchorPoint(scaled_position), scaled_position);
}

void VideoDisplay::Pan(Vector2D delta) {
	pan_x += delta.X() / videoSize.GetHeight();
	pan_y += delta.Y() / videoSize.GetHeight();
	PositionVideo();
}

Vector2D VideoDisplay::GetZoomAnchorPoint(wxPoint position) {
	// 锚点用视频中心为原点、contentZoomValue=1.0 下的逻辑像素偏移表示。
	//
	// 推导公式：
	//   position = viewportCenter + pan * viewportHeight + anchorPoint * videoZoomValue
	//
	// 反解锚点：
	//   anchorPoint = (position - viewportCenter - pan * viewportHeight) / videoZoomValue
	Vector2D viewportCenter = Vector2D(videoSize.GetWidth(), videoSize.GetHeight()) / 2;
	Vector2D scaledPan = Vector2D(pan_x, pan_y) * videoSize.GetHeight();
	return (Vector2D(position) - viewportCenter - scaledPan) / videoZoomValue;
}

void VideoDisplay::ZoomAndPan(double newZoomValue, Vector2D anchorPoint, wxPoint newPosition) {
	newZoomValue = std::max(0.125, std::min(10.0, newZoomValue));

	// 根据上述公式计算新的平移值，使锚点落在 newPosition 处
	Vector2D viewportCenter = Vector2D(videoSize.GetWidth(), videoSize.GetHeight()) / 2;
	Vector2D newScaledPan = Vector2D(newPosition) - viewportCenter - anchorPoint * newZoomValue;

	pan_x = newScaledPan.X() / videoSize.GetHeight();
	pan_y = newScaledPan.Y() / videoSize.GetHeight();
	videoZoomValue = newZoomValue;

	PositionVideo();
}

void VideoDisplay::SetHDRMapping(bool enable) {
	if (!videoOut)
		videoOut = std::make_unique<VideoOutGL>();
	videoOut->EnableHDRToneMapping(enable);
	Render();
}

void VideoDisplay::ResetVideoZoom() {
	pan_x = 0;
	pan_y = 0;
	videoZoomValue = 1;
	PositionVideo();
}

void VideoDisplay::SetZoomFromBox(wxCommandEvent &) {
	int sel = zoomBox->GetSelection();
	if (sel != wxNOT_FOUND) {
		windowZoomValue = (sel + 1) * .125;
		con->ass->Properties.video_zoom = windowZoomValue;
		FitClientSizeToVideo();
	}
}

void VideoDisplay::SetZoomFromBoxText(wxCommandEvent &) {
	wxString strValue = zoomBox->GetValue();
	if (strValue.EndsWith("%"))
		strValue.RemoveLast();

	double value;
	if (strValue.ToDouble(&value))
		SetWindowZoom(value / 100.);
}

void VideoDisplay::SetTool(std::unique_ptr<VisualToolBase> new_tool) {
	// Set the tool first to prevent repeated initialization from VideoDisplay::Render
	tool = std::move(new_tool);

	// Hide the tool bar first to eliminate unnecessary size changes
	toolBar->Show(false);
	toolBar->ClearTools();
	tool->SetToolbar(toolBar);

	// Update size as the new typesetting tool may have changed the subtoolbar size
	if (!freeSize)
		FitClientSizeToVideo();
	else {
		// FitClientSizeToVideo 会将窗口调整到视频大小，自由尺寸模式下不需要这样做
		GetGrandParent()->Layout();
		PositionVideo();
	}
}

bool VideoDisplay::ToolIsType(std::type_info const& type) const {
	VisualToolBase *toolp = tool.get();		// This shuts up a compiler warning
	return toolp && typeid(*toolp) == type;
}

Vector2D VideoDisplay::GetMousePosition() const {
	return last_mouse_pos ? tool->ToScriptCoords(last_mouse_pos) : last_mouse_pos;
}

void VideoDisplay::Unload() {
	if (glContext) {
		SetCurrent(*glContext);
	}
	videoOut.reset();
	tool.reset();
	glContext.reset();
	pending_frame.reset();
}
