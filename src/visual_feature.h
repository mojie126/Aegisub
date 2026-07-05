// Copyright (c) 2007, Rodrigo Braz Monteiro
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

/// @file visual_feature.h
/// @see visual_feature.cpp
/// @ingroup visual_ts
///

#pragma once

#include "vector2d.h"

#include <boost/intrusive/list_hook.hpp>

class OpenGLWrapper;
class AssDialogue;

/// VisualDraggableFeature display types
enum DraggableFeatureType {
	DRAG_NONE,
	DRAG_BIG_SQUARE,
	DRAG_BIG_CIRCLE,
	DRAG_BIG_TRIANGLE,
	DRAG_SMALL_SQUARE,
	DRAG_SMALL_CIRCLE
};

/// @brief 获取指定类型的锚点从中心到最远端的可视半径（像素坐标）
/// 用于夹持时偏移，确保锚点完整显示在视频可见区域内
inline int GetAnchorMargin(DraggableFeatureType type, int size) {
	switch (type) {
		case DRAG_BIG_SQUARE:
		case DRAG_BIG_CIRCLE:
			return 12; // 十字线延伸到 ±12
		case DRAG_BIG_TRIANGLE:
			return 16; // 线条延伸到 16
		case DRAG_SMALL_SQUARE:
		case DRAG_SMALL_CIRCLE:
			return size;
		default:
			return 0;
	}
}

/// @class VisualDraggableFeature
/// @brief Onscreen control used by many visual tools
///
/// By itself this class doesn't do much. It mostly just draws itself at a
/// specified position and performs hit-testing.
class VisualDraggableFeature : public boost::intrusive::make_list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>::type {
	Vector2D start; ///< position before the last drag operation began

public:
	DraggableFeatureType type = DRAG_NONE; ///< Shape of feature
	int size = 0;
	Vector2D pos;                          ///< Position of this feature
	int layer = 0;                         ///< Layer; Higher = above
	AssDialogue* line = nullptr;           ///< The dialogue line this feature is for; may be nullptr

	VisualDraggableFeature();

	/// @brief Is the given point over this feature?
	/// @param mouse_pos Position of the mouse
	bool IsMouseOver(Vector2D mouse_pos) const;

	/// @brief Draw this feature
	/// @param gl OpenGLWrapper to use
	void Draw(OpenGLWrapper const& gl) const;

	/// Start a drag
	void StartDrag();

	/// Update the position of the feature during a drag
	/// @param d New position of the feature
	/// @param single_axis Only apply the larger of the two changes to the position
	void UpdateDrag(Vector2D d, bool single_axis);

	/// Has this feature actually moved since a drag was last started?
	bool HasMoved() const;
};

/// @brief RAII守卫：临时修改特征位置，析构时自动恢复原始位置
struct ScopedClamp {
	VisualDraggableFeature& f;
	Vector2D orig;
	ScopedClamp(VisualDraggableFeature& f, Vector2D clamped)
		: f(f), orig(f.pos) { f.pos = clamped; }
	~ScopedClamp() { f.pos = orig; }
	ScopedClamp(const ScopedClamp&) = delete;
	ScopedClamp& operator=(const ScopedClamp&) = delete;
};
