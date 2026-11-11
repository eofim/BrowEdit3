#include "RoFormats.h"

namespace ro
{
	namespace
	{
		struct Rd
		{
			const std::vector<uint8_t>& d; size_t p = 0; bool bad = false;
			Rd(const std::vector<uint8_t>& d) : d(d) {}
			bool has(size_t n) { if (p + n > d.size()) { bad = true; return false; } return true; }
			int u8() { if (!has(1)) return 0; return d[p++]; }
			int u16() { if (!has(2)) return 0; int v = d[p] | (d[p + 1] << 8); p += 2; return v; }
			int i32() { if (!has(4)) return 0; int32_t v; memcpy(&v, &d[p], 4); p += 4; return v; }
			float f32() { if (!has(4)) return 0; float v; memcpy(&v, &d[p], 4); p += 4; return v; }
			void skip(size_t n) { if (has(n)) p += n; }
		};
	}

	bool Palette::load(const std::vector<uint8_t>& d)
	{
		if (d.size() < 1024)
			return false;
		memcpy(c, d.data(), 1024);
		valid = true;
		return true;
	}

	bool Spr::load(const std::vector<uint8_t>& d)
	{
		Rd r(d);
		if (d.size() < 8 || d[0] != 'S' || d[1] != 'P')
			return false;
		r.p = 2;
		int minor = r.u8(), major = r.u8();
		int ver = major * 10 + minor;
		int nIdx = r.u16();
		int nRgba = ver >= 20 ? r.u16() : 0;
		indexed.assign(nIdx, Image());
		for (int i = 0; i < nIdx && !r.bad; i++)
		{
			int w = r.u16(), h = r.u16();
			if (w > 4096 || h > 4096) return false;
			auto& im = indexed[i];
			im.w = w; im.h = h;
			im.idx.assign((size_t)w * h, 0);
			if (ver >= 21)
			{
				int csize = r.u16();
				size_t end = r.p + csize, o = 0;
				while (r.p < end && o < im.idx.size() && !r.bad)
				{
					int c = r.u8();
					if (c == 0)
					{
						int n = r.u8();
						if (n == 0) n = 1;
						o += n;
					}
					else
						im.idx[o++] = (uint8_t)c;
				}
				r.p = end;
			}
			else
			{
				size_t n = (size_t)w * h;
				if (r.has(n)) { memcpy(im.idx.data(), &d[r.p], n); r.p += n; }
			}
		}
		truecolor.assign(nRgba, Image());
		for (int i = 0; i < nRgba && !r.bad; i++)
		{
			int w = r.u16(), h = r.u16();
			if (w > 4096 || h > 4096) return false;
			auto& im = truecolor[i];
			im.w = w; im.h = h;
			size_t n = (size_t)w * h * 4;
			im.rgba.assign(n, 0);
			if (!r.has(n)) return false;
			for (int y = 0; y < h; y++)			// ABGR, bottom row first
				for (int x = 0; x < w; x++)
				{
					const uint8_t* s = &d[r.p + ((size_t)(h - 1 - y) * w + x) * 4];
					uint8_t* t = &im.rgba[((size_t)y * w + x) * 4];
					t[0] = s[3]; t[1] = s[2]; t[2] = s[1]; t[3] = s[0];
				}
			r.p += n;
		}
		if (r.bad)
			return false;
		if (ver >= 11 && d.size() >= r.p + 1024)
		{
			memcpy(palette.c, &d[d.size() - 1024], 1024);
			palette.valid = true;
		}
		return true;
	}

	bool Spr::pixels(int type, int index, const Palette* custom, std::vector<uint8_t>& out, int& w, int& h) const
	{
		if (type == 1)
		{
			if (index < 0 || index >= (int)truecolor.size()) return false;
			auto& im = truecolor[index];
			w = im.w; h = im.h; out = im.rgba;
			return true;
		}
		if (index < 0 || index >= (int)indexed.size()) return false;
		auto& im = indexed[index];
		w = im.w; h = im.h;
		const Palette& pal = (custom && custom->valid) ? *custom : palette;
		out.assign((size_t)w * h * 4, 0);
		for (size_t k = 0; k < im.idx.size(); k++)
		{
			int c = im.idx[k];
			if (c == 0) continue;
			out[k * 4 + 0] = pal.c[c][0]; out[k * 4 + 1] = pal.c[c][1]; out[k * 4 + 2] = pal.c[c][2]; out[k * 4 + 3] = 255;
		}
		return true;
	}

	bool Act::load(const std::vector<uint8_t>& d)
	{
		Rd r(d);
		if (d.size() < 16 || d[0] != 'A' || d[1] != 'C')
			return false;
		r.p = 2;
		int minor = r.u8(), major = r.u8();
		version = major * 10 + minor;
		int nActions = r.u16();
		r.skip(10);
		actions.assign(nActions, ActAction());
		for (int a = 0; a < nActions && !r.bad; a++)
		{
			int nFrames = r.i32();
			if (nFrames < 0 || nFrames > 100000) return false;
			actions[a].frames.assign(nFrames, ActFrame());
			for (int m = 0; m < nFrames && !r.bad; m++)
			{
				auto& F = actions[a].frames[m];
				r.skip(32);
				int nLayers = r.i32();
				if (nLayers < 0 || nLayers > 10000) return false;
				F.layers.assign(nLayers, ActLayer());
				for (int l = 0; l < nLayers && !r.bad; l++)
				{
					auto& L = F.layers[l];
					L.x = r.i32(); L.y = r.i32(); L.spr = r.i32(); L.mirror = r.i32();
					if (version >= 20)
					{
						for (int k = 0; k < 4; k++) L.col[k] = (uint8_t)r.u8();
						L.sx = r.f32();
						L.sy = version >= 24 ? r.f32() : L.sx;
						L.rot = r.i32();
						L.type = r.i32();
						if (version >= 25) { L.w = r.i32(); L.h = r.i32(); }
					}
				}
				if (version >= 20) F.sound = r.i32();
				if (version >= 23)
				{
					int n = r.i32();
					if (n < 0 || n > 1000) return false;
					for (int k = 0; k < n && !r.bad; k++)
					{
						r.i32();
						ActAnchor an; an.x = r.i32(); an.y = r.i32();
						r.i32();
						F.anchors.push_back(an);
					}
				}
			}
		}
		if (r.bad) return false;
		if (version >= 21)
		{
			int sounds = r.i32();
			if (sounds >= 0 && sounds < 100000) r.skip((size_t)sounds * 40);
			if (version >= 22)
				for (int a = 0; a < nActions; a++)
					actions[a].interval = r.f32();
		}
		return !r.bad;
	}

	bool Imf::load(const std::vector<uint8_t>& d)
	{
		Rd r(d);
		if (d.size() < 12) return false;
		r.f32();		// version
		r.i32();		// checksum
		int maxLayer = r.i32();
		if (maxLayer < 0 || maxLayer > 32) return false;
		layers.assign(maxLayer + 1, std::vector<std::vector<Entry>>());
		for (int l = 0; l <= maxLayer && !r.bad; l++)
		{
			int nActions = r.i32();
			if (nActions < 0 || nActions > 100000) return false;
			layers[l].assign(nActions, std::vector<Entry>());
			for (int a = 0; a < nActions && !r.bad; a++)
			{
				int nFrames = r.i32();
				if (nFrames < 0 || nFrames > 100000) return false;
				layers[l][a].assign(nFrames, Entry());
				for (int f = 0; f < nFrames && !r.bad; f++)
				{
					auto& e = layers[l][a][f];
					e.priority = r.i32(); e.x = r.i32(); e.y = r.i32();
				}
			}
		}
		return !r.bad;
	}

	const Imf::Entry* Imf::get(int layer, int action, int frame) const
	{
		if (layer < 0 || layer >= (int)layers.size()) return nullptr;
		auto& la = layers[layer];
		if (action < 0 || action >= (int)la.size() || la[action].empty()) return nullptr;
		auto& fr = la[action];
		return &fr[frame % fr.size()];
	}
}
