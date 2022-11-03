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

#include "compat.h"
#include "include/aegisub/context.h"
#include "options.h"
#include "selection_controller.h"
#include "vector3d.h"

#include <libaegisub/format.h>

#include <cmath>
#include <wx/colour.h>

static const float deg2rad = 3.1415926536f / 180.f;
static const float rad2deg = 180.f / 3.1415926536f;
static const float screen_z = 312.5;

void VisualToolPerspective::Solve2x2Proper(float a11, float a12, float a21, float a22, float b1, float b2, float &x1, float &x2) {
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


void VisualToolPerspective::Solve2x2(float a11, float a12, float a21, float a22, float b1, float b2, float &x1, float &x2) {
	// Simple pivoting
	if (abs(a11) >= abs(a21))
		Solve2x2Proper(a11, a12, a21, a22, b1, b2, x1, x2);
	else
		Solve2x2Proper(a21, a22, a11, a12, b2, b1, x1, x2);
}


VisualToolPerspective::VisualToolPerspective(VideoDisplay *parent, agi::Context *context)
: VisualTool<VisualDraggableFeature>(parent, context)
{
	orgf = new Feature;
	orgf->type = DRAG_BIG_TRIANGLE;
	features.push_back(*orgf);

	for (int i = 0; i < 4; i++) {
		quad_corners.push_back(new Feature);
		old_positions.push_back(Vector2D());
		quad_corners.back()->type = DRAG_SMALL_CIRCLE;
		features.push_back(*quad_corners.back());
	}
}

void VisualToolPerspective::Draw() {
	if (!active_line) return;

	wxColour line_color = to_wx(line_color_primary_opt->GetColor());
	gl.SetLineColour(line_color);

	for (int i = 0; i < 4; i++) {
		gl.DrawDashedLine(quad_corners[i]->pos, quad_corners[(i + 1) % 4]->pos, 6);
	}

	DrawAllFeatures();
}

void VisualToolPerspective::UpdateDrag(Feature *feature) {
	if (feature == orgf) {
		Vector2D diff = orgf->pos - old_orgf;
		for (int i = 0; i < 4; i++) {
			quad_corners[i]->pos = quad_corners[i]->pos + diff;
		}
	}
	Vector2D q1 = ToScriptCoords(quad_corners[0]->pos);
	Vector2D q2 = ToScriptCoords(quad_corners[1]->pos);
	Vector2D q3 = ToScriptCoords(quad_corners[2]->pos);
	Vector2D q4 = ToScriptCoords(quad_corners[3]->pos);

	Vector2D diag1 = q3 - q1;
	Vector2D diag2 = q2 - q4;
	Vector2D b = q4 - q1;
	float center_la1, center_la2;
	Solve2x2(diag1.X(), diag2.X(), diag1.Y(), diag2.Y(), b.X(), b.Y(), center_la1, center_la2);
	if (center_la1 < 0 || center_la1 > 1 || -center_la2 < 0 || -center_la2 > 1) {
		ResetFeaturePositions();
		return;
	}
	Vector2D center = q1 + center_la1 * diag1;

	// Normalize to center
	q1 = q1 - center;
	q2 = q2 - center;
	q3 = q3 - center;
	q4 = q4 - center;

	// Find a parallelogram projecting to the quad
	float z2, z4;
	Vector2D side1 = q2 - q3;
	Vector2D side2 = q4 - q3;
	Solve2x2(side1.X(), side2.X(), side1.Y(), side2.Y(), -diag1.X(), -diag1.Y(), z2, z4);

	float scalefactor = (z2 + z4) / 2;
	Vector3D r1 = Vector3D(q1, screen_z) / scalefactor;
	Vector3D r2 = z2 * Vector3D(q2, screen_z) / scalefactor;
	Vector3D r4 = z4 * Vector3D(q4, screen_z) / scalefactor;

	// Find the rotations
	Vector3D n = (r2 - r1).Cross(r4 - r1);
	float roty = atan(n.X() / n.Z());
	n = n.RotateY(roty);
	float rotx = atan(n.Y() / n.Z());

	Vector3D ab = (r2 - r1).RotateY(roty).RotateX(rotx);
	float rotz = atan(ab.Y() / ab.X());

	Vector3D ad = (r4 - r1).RotateY(roty).RotateX(rotx).RotateZ(-rotz);
	float fax = ad.X() / ad.Y();

	float scalex = ab.Len() / textwidth;
	float scaley = ad.Y() / textheight;

	for (auto line : c->selectionController->GetSelectedSet()) {
		angle_x = rotx * rad2deg;
		angle_y = -roty * rad2deg;
		angle_z = -rotz * rad2deg;
		orgf->pos = FromScriptCoords(center);
		SetOverride(line, "\\fax", agi::format("%.6f", fax * scaley / scalex));
		SetOverride(line, "\\fay", agi::format("%.4f", 0)); 	// TODO just kill the tag
		SetOverride(line, "\\fscx", agi::format("%.2f", 100 * scalex));
		SetOverride(line, "\\fscy", agi::format("%.2f", 100 * scaley));
		SetOverride(line, "\\frz", agi::format("%.4f", angle_z));
		SetOverride(line, "\\frx", agi::format("%.4f", angle_x));
		SetOverride(line, "\\fry", agi::format("%.4f", angle_y));
		SetOverride(line, "\\pos", (center - Vector2D(fax_shift_factor * fax * scaley, 0)).PStr());
		SetOverride(line, "\\org", center.PStr());
	}
	SaveFeaturePositions();
}

void VisualToolPerspective::ResetFeaturePositions() {
	for (int i = 0; i < 4; i++) {
		quad_corners[i]->pos = old_positions[i];
	}
	orgf->pos = old_orgf;
}

void VisualToolPerspective::SaveFeaturePositions() {
	for (int i = 0; i < 4; i++) {
		old_positions[i] = quad_corners[i]->pos;
	}
	old_orgf = orgf->pos;
}

void VisualToolPerspective::SetFeaturePositions() {
	double textleft, texttop = 0.;

	switch ((align - 1) % 3) {
		case 1:
			textleft = -textwidth / 2;
			break;
		case 2:
			textleft = -textwidth;
			break;
		default:
			break;
	}
	switch ((align - 1) / 3) {
		case 0:
			texttop = -textheight;
			break;
		case 1:
			texttop = -textheight / 2;
			break;
		default:
			break;
	}

	Vector2D textrect[] = {
		Vector2D(0, 0),
		Vector2D(textwidth, 0),
		Vector2D(textwidth, textheight),
		Vector2D(0, textheight),
	};

	for (int i = 0; i < 4; i++) {
		Vector2D p = textrect[i];
		// Apply \fax and \fay
		p = Vector2D(p.X() + p.Y() * fax, p.X() * fay + p.Y());
		// Translate to alignment point
		p = p + Vector2D(textleft, texttop);
		// Apply scaling
		p = Vector2D(p.X() * fsc.X() / 100., p.Y() * fsc.Y() / 100.);
		// Translate relative to origin
		p = p + pos - org;
		// Rotate ZXY
		Vector3D q(p);
		q = q.RotateZ(-angle_z * deg2rad);
		q = q.RotateX(-angle_x * deg2rad);
		q = q.RotateY(angle_y * deg2rad);
		// Project
		q = (screen_z / (q.Z() + screen_z)) * q;
		// Move to origin
		Vector2D r = q.XY() + org;

		quad_corners[i]->pos = FromScriptCoords(r);
	}

	if (!(orgf->pos = org))
		orgf->pos = pos;
	orgf->pos = FromScriptCoords(orgf->pos);
	SaveFeaturePositions();
}

void VisualToolPerspective::DoRefresh() {
	if (!active_line) return;

	org = GetLineOrigin(active_line);
	pos = GetLinePosition(active_line);
	if (!org)
		org = pos;

	GetLineRotation(active_line, angle_x, angle_y, angle_z);
	GetLineShear(active_line, fax, fay);
	GetLineScale(active_line, fsc);

	float fs = GetLineFontSize(active_line);
	align = GetLineAlignment(active_line);

	switch (align) {
		case 1:
		case 2:
		case 3:
			fax_shift_factor = fs;
			break;
		case 4:
		case 5:
		case 6:
			fax_shift_factor = fs / 2;
			break;
		default:
			fax_shift_factor = 0.;
			break;
	}

	double descend, extlead;
	GetLineBaseExtents(active_line, textwidth, textheight, descend, extlead);
	SetFeaturePositions();
}
