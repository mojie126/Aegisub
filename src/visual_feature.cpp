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

/// @file visual_feature.cpp
/// @brief Feature on video the user can interact with using mouse
/// @ingroup visual_ts
///

#include "gl_wrap.h"
#include "options.h"
#include "visual_feature.h"

VisualDraggableFeature::VisualDraggableFeature()
: size(OPT_GET("Tool/Visual/Shape Handle Size")->GetInt())
{}

bool VisualDraggableFeature::IsMouseOver(Vector2D mouse_pos) const {
	if (!pos) return false;

	size = OPT_GET("Tool/Visual/Shape Handle Size")->GetInt();
	Vector2D delta = mouse_pos - pos;

	switch (type) {
		case DRAG_BIG_SQUARE:
			return fabs(delta.X()) < size && fabs(delta.Y()) < size;

		case DRAG_BIG_CIRCLE:
			return delta.SquareLen() < size * size;

		case DRAG_BIG_TRIANGLE: {
			if (delta.Y() < -size * 5.f / 3.f || delta.Y() > size) return false;
			float dy = delta.Y() - size;
			return 8.f / 3.f * delta.X() + 1.5f * dy < 0 && 8.f / 3.f * delta.X() - 1.5f * dy > 0;
		}

		case DRAG_SMALL_SQUARE:
			return fabs(delta.X()) < size && fabs(delta.Y()) < size;

		case DRAG_SMALL_CIRCLE:
			return delta.SquareLen() < size * size;

		default:
			return false;
	}
}

void VisualDraggableFeature::Draw(OpenGLWrapper const& gl) const {
	if (!pos) return;

	size = OPT_GET("Tool/Visual/Shape Handle Size")->GetInt();

	switch (type) {
		case DRAG_BIG_SQUARE:
			gl.DrawRectangle(pos - Vector2D(size, size), pos + Vector2D(size, size));
			gl.DrawLine(pos - Vector2D(0, 2 * size), pos + Vector2D(0, 2 * size));
			gl.DrawLine(pos - Vector2D(2 * size, 0), pos + Vector2D(2 * size, 0));
			break;

		case DRAG_BIG_CIRCLE:
			gl.DrawCircle(pos, size);
			gl.DrawLine(pos - Vector2D(0, 2 * size), pos + Vector2D(0, 2 * size));
			gl.DrawLine(pos - Vector2D(2 * size, 0), pos + Vector2D(2 * size, 0));
			break;

		case DRAG_BIG_TRIANGLE:
			gl.DrawTriangle(pos - Vector2D(1.5f * size, size), pos + Vector2D(1.5f * size, -size), pos + Vector2D(0, size * 5.f / 3.f));
			gl.DrawLine(pos, pos + Vector2D(0, -size * 8.f / 3.f));
			gl.DrawLine(pos, pos + Vector2D(-size * 7.f / 3.f, size * 4.f / 3.f));
			gl.DrawLine(pos, pos + Vector2D(size * 7.f / 3.f, size * 4.f / 3.f));
			break;

		case DRAG_SMALL_SQUARE:
			gl.DrawRectangle(pos - size, pos + size);
			break;

		case DRAG_SMALL_CIRCLE:
			gl.DrawCircle(pos, size);
			break;
		default:
			break;
	}
}

void VisualDraggableFeature::StartDrag() {
	start = pos;
}

void VisualDraggableFeature::UpdateDrag(Vector2D d, bool single_axis) {
	if (single_axis)
		d = d.SingleAxis();

	pos = start + d;
}

bool VisualDraggableFeature::HasMoved() const {
	return pos != start;
}
