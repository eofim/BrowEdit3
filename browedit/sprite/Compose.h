#pragma once
#include "RoFormats.h"
#include <vector>

namespace ro
{
	// One sprite of a character (body, head, headgear, garment, weapon...).
	struct Part
	{
		const Spr* spr = nullptr;
		const Act* act = nullptr;
		const Palette* pal = nullptr;	// optional palette override
		int attach = 0;					// 0 = draw at the origin, 1 = attach to the parent (body) through the anchors, 2 = like 1 but relative to the head
		int order = 0;					// draw order, low first
		bool parentIsHead = false;
	};

	struct Canvas
	{
		int w = 0, h = 0, ox = 0, oy = 0;	// origin (feet) inside the canvas
		std::vector<uint8_t> rgba;
		Canvas(int w, int h, int ox, int oy) : w(w), h(h), ox(ox), oy(oy), rgba((size_t)w * h * 4, 0) {}
	};

	// Draw one action frame of a sprite (all layers) onto the canvas, offset by (dx, dy) from the origin.
	void drawFrame(Canvas& cv, const Part& p, int action, int frame, int dx, int dy);
}
