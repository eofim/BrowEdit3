#include "Compose.h"
#include <algorithm>
#include <cmath>

namespace ro
{
	namespace
	{
		void blend(Canvas& cv, int x, int y, const uint8_t* s, float alphaMul, const uint8_t* tint)
		{
			if (x < 0 || y < 0 || x >= cv.w || y >= cv.h) return;
			float sa = s[3] / 255.0f * alphaMul;
			if (sa <= 0) return;
			uint8_t* d = &cv.rgba[((size_t)y * cv.w + x) * 4];
			float sr = s[0] * tint[0] / 255.0f, sg = s[1] * tint[1] / 255.0f, sb = s[2] * tint[2] / 255.0f;
			float da = d[3] / 255.0f;
			float oa = sa + da * (1 - sa);
			if (oa <= 0) return;
			d[0] = (uint8_t)((sr * sa + d[0] * da * (1 - sa)) / oa);
			d[1] = (uint8_t)((sg * sa + d[1] * da * (1 - sa)) / oa);
			d[2] = (uint8_t)((sb * sa + d[2] * da * (1 - sa)) / oa);
			d[3] = (uint8_t)(oa * 255);
		}
	}

	void drawFrame(Canvas& cv, const Part& p, int action, int frame, int dx, int dy)
	{
		if (!p.spr || !p.act || action < 0 || action >= (int)p.act->actions.size()) return;
		auto& A = p.act->actions[action];
		if (A.frames.empty()) return;
		auto& F = A.frames[frame % A.frames.size()];
		std::vector<uint8_t> px;
		for (auto& L : F.layers)
		{
			if (L.spr < 0) continue;
			int w, h;
			if (!p.spr->pixels(L.type, L.spr, p.pal, px, w, h) || w <= 0 || h <= 0) continue;
			float sx = L.sx * (L.mirror ? -1 : 1), sy = L.sy;
			float ang = -L.rot * 3.14159265f / 180.0f;
			float ca = cosf(ang), sa = sinf(ang);
			// inverse-map every canvas pixel in the bounding box of the transformed image
			float hw = w * 0.5f * fabsf(L.sx), hh = h * 0.5f * fabsf(L.sy);
			float ext = fabsf(ca) * hw + fabsf(sa) * hh, exth = fabsf(sa) * hw + fabsf(ca) * hh;
			float cx = cv.ox + dx + L.x, cy = cv.oy + dy + L.y;
			int x0 = (int)floorf(cx - ext), x1 = (int)ceilf(cx + ext), y0 = (int)floorf(cy - exth), y1 = (int)ceilf(cy + exth);
			for (int y = y0; y <= y1; y++)
				for (int x = x0; x <= x1; x++)
				{
					float px_ = x + 0.5f - cx, py_ = y + 0.5f - cy;
					float rx = (ca * px_ + sa * py_) / sx, ry = (-sa * px_ + ca * py_) / sy;
					int ix = (int)floorf(rx + w * 0.5f), iy = (int)floorf(ry + h * 0.5f);
					if (ix < 0 || iy < 0 || ix >= w || iy >= h) continue;
					blend(cv, x, y, &px[((size_t)iy * w + ix) * 4], L.col[3] / 255.0f, L.col);
				}
		}
	}
}
