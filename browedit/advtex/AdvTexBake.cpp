#include <glad/gl.h>
#include "AdvTex.h"
#include <browedit/BrowEdit.h>
#include <browedit/Map.h>
#include <browedit/Node.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/GndRenderer.h>
#include <browedit/gl/Texture.h>
#include <browedit/util/FileIO.h>
#include <browedit/util/ResourceManager.h>
#include <browedit/util/Util.h>
#include <stb/stb_image.h>
#include <json.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace advtex
{
	// ------------------------------------------------------------------ helpers
	static std::map<Map*, Project> projects;
	static std::map<std::string, Img> imgCache;
	static Map* askMap = nullptr;			// map waiting for the answer of the save prompt

	bool isEmptyCell(const Cell& c)
	{
		for (auto& m : c.masks)
			for (size_t i = 0; i < m.second.size() && i < (size_t)MASK * MASK; i++)
				if (m.second[i]) return false;
		return true;
	}

	std::string mapBase(Map* map)
	{
		std::string n = map->name;
		auto p = n.find_last_of("\\/");
		if (p != std::string::npos) n = n.substr(p + 1);
		if (n.size() > 4 && n.substr(n.size() - 4) == ".rsw") n = n.substr(0, n.size() - 4);
		for (auto& c : n) if (!isalnum((unsigned char)c) && c != '_') c = '_';
		return n;
	}

	std::string atlasFile(Project& P, Map* map, int id)
	{
		std::string num = std::to_string(id);
		while (num.size() < 4) num = "0" + num;
		std::string name = mapBase(map) + "_at_" + num + ".bmp";
		return P.folder.empty() ? name : P.folder + "\\" + name;
	}

	static std::string diskPath(BrowEdit* be, const std::string& gndFile)
	{
		return be->config.ropath + "data\\texture\\" + gndFile;
	}

	void clearImageCache() { imgCache.clear(); }

	const Img* getImg(const std::string& file)
	{
		auto it = imgCache.find(file);
		if (it != imgCache.end()) return it->second.w ? &it->second : nullptr;
		Img& img = imgCache[file];
		std::istream* is = util::FileIO::open("data\\texture\\" + file);
		if (!is) return nullptr;
		is->seekg(0, std::ios::end);
		auto len = (size_t)is->tellg();
		is->seekg(0, std::ios::beg);
		std::vector<char> buf(len);
		is->read(buf.data(), len);
		delete is;
		int w, h, comp;
		stbi_set_flip_vertically_on_load(false);
		unsigned char* d = stbi_load_from_memory((const stbi_uc*)buf.data(), (int)len, &w, &h, &comp, 4);
		if (!d) return nullptr;
		img.w = w; img.h = h; img.px.assign(d, d + (size_t)w * h * 4);
		stbi_image_free(d);
		return &img;
	}

	static void sampleImg(const Img& im, float u, float v, bool repeat, float out[3])
	{
		float x = u * im.w - 0.5f, y = v * im.h - 0.5f;
		int x0 = (int)floorf(x), y0 = (int)floorf(y);
		float fx = x - x0, fy = y - y0;
		auto at = [&](int xx, int yy, int c) {
			if (repeat) { xx = ((xx % im.w) + im.w) % im.w; yy = ((yy % im.h) + im.h) % im.h; }
			else { xx = std::clamp(xx, 0, im.w - 1); yy = std::clamp(yy, 0, im.h - 1); }
			return (float)im.px[((size_t)yy * im.w + xx) * 4 + c];
		};
		for (int c = 0; c < 3; c++)
		{
			float a = at(x0, y0, c) * (1 - fx) + at(x0 + 1, y0, c) * fx;
			float b = at(x0, y0 + 1, c) * (1 - fx) + at(x0 + 1, y0 + 1, c) * fx;
			out[c] = a * (1 - fy) + b * fy;
		}
	}

	static float sampleMask(const std::vector<uint8_t>& m, float s, float t)
	{
		float x = std::clamp(s * MASK - 0.5f, 0.0f, (float)MASK - 1), y = std::clamp(t * MASK - 0.5f, 0.0f, (float)MASK - 1);
		int x0 = (int)x, y0 = (int)y, x1 = std::min(x0 + 1, MASK - 1), y1 = std::min(y0 + 1, MASK - 1);
		float fx = x - x0, fy = y - y0;
		float a = m[y0 * MASK + x0] * (1 - fx) + m[y0 * MASK + x1] * fx;
		float b = m[y1 * MASK + x0] * (1 - fx) + m[y1 * MASK + x1] * fx;
		return (a * (1 - fy) + b * fy) / 255.0f;
	}

	// ------------------------------------------------------------------ rendering of one cell into the atlas cache
	static void renderCell(Gnd* gnd, Project& P, Atlas& A, int k, Cell& c)
	{
		int cx = keyX(k), cy = keyY(k);
		int block = A.cellPx + 2 * PAD, perRow = A.size / block;
		int bx = (c.slot % perRow) * block, by = (c.slot / perRow) * block;
		const Img* base = c.orig.valid ? getImg(c.orig.file) : nullptr;
		struct L { const Img* img; const std::vector<uint8_t>* mask; const PaintLayer* pl; };
		std::vector<L> layers;
		for (auto& m : c.masks)
			if (m.first >= 0 && m.first < (int)P.layers.size())
				layers.push_back({ getImg(P.layers[m.first].tex), &m.second, &P.layers[m.first] });
		// original uv corners: v1 (s0,t1) v2 (s1,t1) v3 (s0,t0) v4 (s1,t0)
		const float* uv = c.orig.uv;
		glm::vec2 v1(uv[0], uv[1]), v2(uv[2], uv[3]), v3(uv[4], uv[5]), v4(uv[6], uv[7]);
		for (int j = -PAD; j < A.cellPx + PAD; j++)
			for (int i = -PAD; i < A.cellPx + PAD; i++)
			{
				float s = std::clamp((i + 0.5f) / A.cellPx, 0.0f, 1.0f), t = std::clamp((j + 0.5f) / A.cellPx, 0.0f, 1.0f);
				float col[3] = { 128, 128, 128 };
				if (base)
				{
					glm::vec2 a = v3 + (v4 - v3) * s, b = v1 + (v2 - v1) * s, p = a + (b - a) * t;
					sampleImg(*base, p.x, p.y, false, col);
				}
				float wx = 10.0f * cx + 10.0f * s, wz = 10.0f * (gnd->height - cy) + 10.0f * t;
				for (auto& l : layers)
				{
					if (!l.img) continue;
					float a = sampleMask(*l.mask, s, t);
					if (a <= 0.0f) continue;
					float oc[3];
					if (l.pl->ribbon && l.mask->size() >= (size_t)MASK * MASK + 32)
					{
						float f[8]; std::memcpy(f, l.mask->data() + MASK * MASK, 32);
						auto mixc = [&](int k0, int k1, int k2, int k3, int comp) { return (1 - t) * ((1 - s) * f[2 * k2 + comp] + s * f[2 * k3 + comp]) + t * ((1 - s) * f[2 * k0 + comp] + s * f[2 * k1 + comp]); };
						float ru = mixc(0, 1, 2, 3, 0), rv = mixc(0, 1, 2, 3, 1);
						rv = std::clamp(rv, 0.5f / l.img->h, 1.0f - 0.5f / l.img->h);
						sampleImg(*l.img, ru, rv, true, oc);
						for (int q = 0; q < 3; q++) col[q] = col[q] * (1 - a) + oc[q] * a;
						continue;
					}
					float tl = std::max(0.1f, l.pl->tiling), u = wx / (10.0f * tl), v = wz / (10.0f * tl);
					if (l.pl->flipH) u = -u;
					if (l.pl->flipV) v = -v;
					if (l.pl->rot == 1) { float t2 = u; u = -v; v = t2; }
					else if (l.pl->rot == 2) { u = -u; v = -v; }
					else if (l.pl->rot == 3) { float t2 = u; u = v; v = -t2; }
					sampleImg(*l.img, u + l.pl->offX, v + l.pl->offY, true, oc);
					for (int q = 0; q < 3; q++) col[q] = col[q] * (1 - a) + oc[q] * a;
				}
				int px = bx + PAD + i, py = by + PAD + j;
				uint8_t* d = &A.rgba[((size_t)py * A.size + px) * 4];
				for (int q = 0; q < 3; q++) d[q] = (uint8_t)std::clamp((int)lroundf(col[q]), 0, 255);
				d[3] = 255;
			}
	}

	void renderCellToAtlas(Gnd* gnd, Project& P, Atlas& A, int k, Cell& c) { renderCell(gnd, P, A, k, c); }
	Atlas* atlasById(Project& P, int id) { for (auto& a : P.atlases) if (a.id == id) return &a; return nullptr; }
	Project* findProject(Map* map) { auto it = projects.find(map); return it == projects.end() ? nullptr : &it->second; }

	// gives a painted cell a stable slot in an atlas (new atlas ids are never reused)
	bool assignSlot(Project& P, Map* map, int k, Cell& c)
	{
		int block = P.cellPx + 2 * PAD, perRow = P.atlasSize / block, slotCount = perRow * perRow;
		int bx = P.blockLayout ? keyX(k) / perRow : -1, by = P.blockLayout ? keyY(k) / perRow : -1;
		int want = P.blockLayout ? (keyY(k) % perRow) * perRow + (keyX(k) % perRow) : -1;
		for (auto& a : P.atlases)
		{
			if (a.retired || a.size != P.atlasSize || a.cellPx != P.cellPx) continue;
			if (P.blockLayout)
			{
				if (a.blockX != bx || a.blockY != by) continue;
				if (want < (int)a.slots.size() && a.slots[want] < 0) { a.slots[want] = k; c.atlas = a.id; c.slot = want; a.dirty = true; return true; }
				continue;
			}
			if (a.blockX >= 0) continue;
			for (int s = 0; s < (int)a.slots.size(); s++)
				if (a.slots[s] < 0) { a.slots[s] = k; c.atlas = a.id; c.slot = s; a.dirty = true; return true; }
		}
		Atlas a; a.id = P.nextAtlasId++; a.file = atlasFile(P, map, a.id); a.size = P.atlasSize; a.cellPx = P.cellPx; a.slots.assign(slotCount, -1); a.blockX = bx; a.blockY = by;
		int first = want >= 0 ? want : 0;
		a.slots[first] = k; c.atlas = a.id; c.slot = first; a.dirty = true;
		P.atlases.push_back(a);
		return true;
	}

	// ------------------------------------------------------------------ quantizer (median cut on a 5-5-5 histogram) + BMP writer
	struct Quant { std::vector<uint8_t> idx; uint8_t pal[256][3]; int count = 0; };

	static void quantize(const std::vector<uint8_t>& rgba, int w, int h, Quant& q)
	{
		std::vector<uint32_t> hist(32768, 0);
		for (size_t i = 0; i < (size_t)w * h; i++)
		{
			const uint8_t* p = &rgba[i * 4];
			hist[((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3)]++;
		}
		struct Box { std::vector<int> bins; uint64_t pop = 0; };
		Box first;
		for (int b = 0; b < 32768; b++) if (hist[b]) { first.bins.push_back(b); first.pop += hist[b]; }
		std::vector<Box> boxes; boxes.push_back(std::move(first));
		auto chan = [](int b, int c) { return c == 0 ? (b >> 10) : c == 1 ? ((b >> 5) & 31) : (b & 31); };
		auto range = [&](const Box& bx, int& bestC) {
			int best = 0; bestC = 0;
			for (int c = 0; c < 3; c++)
			{
				int lo = 31, hi = 0;
				for (int b : bx.bins) { int v = chan(b, c); lo = std::min(lo, v); hi = std::max(hi, v); }
				if (hi - lo >= best) { best = hi - lo; bestC = c; }
			}
			return best;
		};
		while ((int)boxes.size() < 256)
		{
			int pick = -1; double bestScore = 0; int pickC = 0;
			for (int i = 0; i < (int)boxes.size(); i++)
			{
				if (boxes[i].bins.size() < 2) continue;
				int c; int r = range(boxes[i], c);
				double sc = (r + 0.5) * std::sqrt((double)boxes[i].pop);
				if (sc > bestScore) { bestScore = sc; pick = i; pickC = c; }
			}
			if (pick < 0) break;
			Box& bx = boxes[pick];
			std::sort(bx.bins.begin(), bx.bins.end(), [&](int a, int b) { return chan(a, pickC) < chan(b, pickC); });
			uint64_t half = bx.pop / 2, acc = 0; size_t cut = 1;
			for (size_t i = 0; i < bx.bins.size(); i++) { acc += hist[bx.bins[i]]; if (acc >= half) { cut = std::clamp<size_t>(i + 1, 1, bx.bins.size() - 1); break; } }
			Box nb; nb.bins.assign(bx.bins.begin() + cut, bx.bins.end());
			bx.bins.resize(cut);
			bx.pop = 0; for (int b : bx.bins) bx.pop += hist[b];
			nb.pop = 0; for (int b : nb.bins) nb.pop += hist[b];
			boxes.push_back(std::move(nb));
		}
		std::vector<uint8_t> binToIdx(32768, 0);
		memset(q.pal, 0, sizeof(q.pal));
		q.count = (int)boxes.size();
		for (int i = 0; i < q.count; i++)
		{
			double r = 0, g = 0, b = 0, n = 0;
			for (int bin : boxes[i].bins) { double c = hist[bin]; r += c * chan(bin, 0); g += c * chan(bin, 1); b += c * chan(bin, 2); n += c; binToIdx[bin] = (uint8_t)i; }
			if (n <= 0) continue;
			auto ex = [](double v) { int x = (int)lround(v); return (uint8_t)std::clamp((x << 3) | (x >> 2), 0, 255); };
			q.pal[i][0] = ex(r / n); q.pal[i][1] = ex(g / n); q.pal[i][2] = ex(b / n);
		}
		q.idx.resize((size_t)w * h);
		for (size_t i = 0; i < (size_t)w * h; i++)
		{
			const uint8_t* p = &rgba[i * 4];
			q.idx[i] = binToIdx[((p[0] >> 3) << 10) | ((p[1] >> 3) << 5) | (p[2] >> 3)];
		}
	}

	static uint64_t fnv(const void* data, size_t n, uint64_t h = 1469598103934665603ull)
	{
		const uint8_t* p = (const uint8_t*)data;
		for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
		return h;
	}

	static bool writeBmp8(const std::string& path, const Quant& q, int w, int h)
	{
		int rowSize = (w + 3) & ~3;
		uint32_t dataSize = rowSize * h, offset = 14 + 40 + 1024, fileSize = offset + dataSize;
		std::ofstream f(path, std::ios::binary | std::ios::trunc);
		if (!f) return false;
		auto w16 = [&](uint16_t v) { f.write((char*)&v, 2); };
		auto w32 = [&](uint32_t v) { f.write((char*)&v, 4); };
		f.write("BM", 2); w32(fileSize); w32(0); w32(offset);
		w32(40); w32(w); w32(h); w16(1); w16(8); w32(0); w32(dataSize); w32(2835); w32(2835); w32(256); w32(0);
		for (int i = 0; i < 256; i++) { uint8_t e[4] = { q.pal[i][2], q.pal[i][1], q.pal[i][0], 0 }; f.write((char*)e, 4); }
		std::vector<uint8_t> row(rowSize, 0);
		for (int y = h - 1; y >= 0; y--)
		{
			memcpy(row.data(), &q.idx[(size_t)y * w], w);
			f.write((char*)row.data(), rowSize);
		}
		f.flush();
		return (bool)f;
	}

	// reads the file back and checks it really is what we meant to write
	static bool validateBmp(const std::string& path, int w, int h, uint64_t& fileSizeOut)
	{
		std::error_code ec;
		fileSizeOut = std::filesystem::file_size(path, ec);
		if (ec || fileSizeOut >= 1024 * 1024) return false;
		std::ifstream f(path, std::ios::binary);
		char hdr[54];
		if (!f.read(hdr, 54)) return false;
		if (hdr[0] != 'B' || hdr[1] != 'M') return false;
		int32_t bw, bh; uint16_t bpp;
		memcpy(&bw, hdr + 18, 4); memcpy(&bh, hdr + 22, 4); memcpy(&bpp, hdr + 28, 2);
		if (bw != w || bh != h || bpp != 8) return false;
		int cw, ch, comp;
		return stbi_info(path.c_str(), &cw, &ch, &comp) && cw == w && ch == h;
	}

	// ------------------------------------------------------------------ gnd
	static int ensureTexture(Gnd* gnd, const std::string& file)
	{
		for (int i = 0; i < (int)gnd->textures.size(); i++)
			if (gnd->textures[i]->file == file) return i;
		std::string name = file.substr(file.find_last_of('\\') == std::string::npos ? 0 : file.find_last_of('\\') + 1);
		gnd->textures.push_back(new Gnd::Texture(name.substr(0, 39), file));
		return (int)gnd->textures.size() - 1;
	}

	static void setTileUV(Gnd::Tile* t, float u0, float v0, float u1, float v1)
	{
		t->v3 = glm::vec2(u0, v0); t->v4 = glm::vec2(u1, v0); t->v1 = glm::vec2(u0, v1); t->v2 = glm::vec2(u1, v1);
	}

	// ------------------------------------------------------------------ bake
	bool bake(Map* map, BrowEdit* be, Project& P, bool rebuildAll, std::string& msg)
	{
		auto gnd = map->rootNode->getComponent<Gnd>();
		auto gndRenderer = map->rootNode->getComponent<GndRenderer>();
		if (!gnd) { msg = "No ground"; return false; }
		for (char c : P.folder) if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '\\') { msg = "Folder name: use letters, digits, _ and - only"; return false; }
		std::string sample = atlasFile(P, map, 9999);
		if (sample.size() > 39) { msg = "Texture path '" + sample + "' is " + std::to_string(sample.size()) + " characters; the GND allows 39. Use a shorter folder name."; return false; }
		if (P.cellPx != 14 && P.cellPx != 30 && P.cellPx != 62) P.cellPx = 30;
		if (P.atlasSize != 256 && P.atlasSize != 512) P.atlasSize = 512;
		int block = P.cellPx + 2 * PAD, perRow = P.atlasSize / block, slotCount = perRow * perRow;

		if (rebuildAll)
		{
			clearImageCache();
			for (auto& a : P.atlases) { a.retired = true; a.rgba.clear(); a.complete = false; std::fill(a.slots.begin(), a.slots.end(), -1); }
			for (auto& kv : P.cells) { kv.second.atlas = kv.second.slot = -1; kv.second.dirty = true; }
		}

		// cells without paint go back to the original tile
		std::vector<int> restore;
		for (auto& kv : P.cells)
			if (isEmptyCell(kv.second)) restore.push_back(kv.first);

		// give every painted cell a stable slot
		for (auto& kv : P.cells)
		{
			Cell& c = kv.second;
			if (isEmptyCell(c) || !c.orig.valid) continue;
			if (c.slot < 0 || !atlasById(P, c.atlas)) assignSlot(P, map, kv.first, c);
			if (c.dirty) if (auto a = atlasById(P, c.atlas)) a->dirty = true;
		}

		// render + quantize + write to temp files
		struct Out { Atlas* a; std::string tmp, finalPath; uint64_t hash; bool write; };
		std::vector<Out> outs;
		bool failed = false;
		for (auto& a : P.atlases)
		{
			if (a.retired || !a.dirty) continue;
			bool full = !a.complete || a.rgba.size() != (size_t)a.size * a.size * 4;
			if (a.rgba.size() != (size_t)a.size * a.size * 4) a.rgba.assign((size_t)a.size * a.size * 4, 0);
			for (int s = 0; s < (int)a.slots.size(); s++)
			{
				if (a.slots[s] < 0) continue;
				auto it = P.cells.find(a.slots[s]);
				if (it == P.cells.end() || isEmptyCell(it->second)) continue;
				if (full || it->second.dirty) renderCell(gnd, P, a, a.slots[s], it->second);
			}
			a.complete = true;
			Quant q; quantize(a.rgba, a.size, a.size, q);
			uint64_t h = fnv(q.idx.data(), q.idx.size());
			h = fnv(q.pal, sizeof(q.pal), h);
			Out o{ &a, "", diskPath(be, a.file), h, false };
			o.tmp = o.finalPath + ".tmp";
			std::error_code ec;
			bool exists = std::filesystem::exists(o.finalPath, ec);
			if (h != a.hash || !exists)
			{
				std::filesystem::create_directories(std::filesystem::path(o.finalPath).parent_path(), ec);
				uint64_t sz;
				if (!writeBmp8(o.tmp, q, a.size, a.size) || !validateBmp(o.tmp, a.size, a.size, sz))
				{
					msg = "Could not write " + a.file + " (kept the old files, nothing was changed)";
					failed = true; break;
				}
				o.write = true;
			}
			outs.push_back(o);
		}
		if (failed)
		{
			for (auto& o : outs) { std::error_code ec; std::filesystem::remove(o.tmp, ec); }
			return false;
		}
		// commit: replace the files
		int written = 0;
		for (auto& o : outs)
		{
			if (o.write)
			{
				std::error_code ec;
				std::filesystem::rename(o.tmp, o.finalPath, ec);
				if (ec) { std::filesystem::remove(o.finalPath, ec); std::filesystem::rename(o.tmp, o.finalPath, ec); }
				if (ec) { msg = "Could not replace " + o.a->file + ": " + ec.message(); for (auto& o2 : outs) { std::error_code e2; std::filesystem::remove(o2.tmp, e2); } return false; }
				written++;
			}
			o.a->hash = o.hash;
			o.a->dirty = false;
		}

		// files are in place: now the gnd
		int touched = 0;
		auto markChunk = [&](int x, int y) {
			if (!gndRenderer) return;
			int cy = y / CHUNKSIZE, cx = x / CHUNKSIZE;
			if (cy >= 0 && cy < (int)gndRenderer->chunks.size() && cx >= 0 && cx < (int)gndRenderer->chunks[cy].size() && gndRenderer->chunks[cy][cx])
				gndRenderer->chunks[cy][cx]->dirty = true;
		};
		for (auto& a : P.atlases)
		{
			if (a.retired) continue;
			bool used = false;
			for (int s : a.slots) if (s >= 0) { used = true; break; }
			if (!used) continue;
			int texIdx = ensureTexture(gnd, a.file);
			int perRowA = a.size / (a.cellPx + 2 * PAD), blockA = a.cellPx + 2 * PAD;
			for (int s = 0; s < (int)a.slots.size(); s++)
			{
				if (a.slots[s] < 0) continue;
				auto it = P.cells.find(a.slots[s]);
				if (it == P.cells.end() || isEmptyCell(it->second)) continue;
				Cell& c = it->second;
				int x = keyX(it->first), y = keyY(it->first);
				if (x >= gnd->width || y >= gnd->height) continue;
				Gnd::Cube* cube = gnd->cubes[x][y];
				if (cube->tileUp < 0) continue;
				bool newTile = !c.hasTile;
				if (!c.hasTile)
				{
					gnd->tiles.push_back(new Gnd::Tile(*gnd->tiles[cube->tileUp]));	// copy on write: the old tile may be shared with other cubes
					cube->tileUp = (int)gnd->tiles.size() - 1;
					c.hasTile = true;
				}
				Gnd::Tile* t = gnd->tiles[cube->tileUp];
				float bx = (float)((c.slot % perRowA) * blockA + PAD), by = (float)((c.slot / perRowA) * blockA + PAD);
				glm::vec2 o1 = t->v1, o2 = t->v2, o3 = t->v3, o4 = t->v4; short oldTex = t->textureIndex;
				t->textureIndex = (short)texIdx;
				setTileUV(t, bx / a.size, by / a.size, (bx + a.cellPx) / a.size, (by + a.cellPx) / a.size);
				if (oldTex != t->textureIndex || o1 != t->v1 || o2 != t->v2 || o3 != t->v3 || o4 != t->v4 || newTile) { markChunk(x, y); touched++; }
				c.dirty = false;
				c.prevOk = false;
			}
		}
		for (int k : restore)
		{
			Cell& c = P.cells[k];
			int x = keyX(k), y = keyY(k);
			if (c.hasTile && c.orig.valid && x < gnd->width && y < gnd->height)
			{
				Gnd::Cube* cube = gnd->cubes[x][y];
				if (cube->tileUp >= 0)
				{
					Gnd::Tile* t = gnd->tiles[cube->tileUp];
					t->textureIndex = (short)ensureTexture(gnd, c.orig.file);
					t->v1 = glm::vec2(c.orig.uv[0], c.orig.uv[1]); t->v2 = glm::vec2(c.orig.uv[2], c.orig.uv[3]);
					t->v3 = glm::vec2(c.orig.uv[4], c.orig.uv[5]); t->v4 = glm::vec2(c.orig.uv[6], c.orig.uv[7]);
					markChunk(x, y);
					touched++;
				}
			}
			if (auto a = atlasById(P, c.atlas)) if (c.slot >= 0 && c.slot < (int)a->slots.size()) a->slots[c.slot] = -1;
			P.cells.erase(k);
		}

		// show the new pixels now
		for (auto& o : outs)
			if (o.write)
				util::ResourceManager<gl::Texture>::load("data\\texture\\" + o.a->file)->reload();
		P.touched.clear();
		if (touched || written) map->changed = true;
		P.pending = false;
		msg = "Baked: " + std::to_string(written) + " texture(s) written, " + std::to_string(touched) + " cell(s) updated";
		return true;
	}

	// ------------------------------------------------------------------ unused files
	std::vector<std::string> unusedAtlases(Map* map, Project& P)
	{
		std::vector<std::string> ret;
		auto gnd = map->rootNode->getComponent<Gnd>();
		if (!gnd) return ret;
		std::set<int> usedTex;
		for (auto t : gnd->tiles) usedTex.insert(t->textureIndex);
		for (auto& a : P.atlases)
		{
			bool slotUsed = false;
			for (int s : a.slots) if (s >= 0) slotUsed = true;
			if (slotUsed) continue;
			bool referenced = false;
			for (int i = 0; i < (int)gnd->textures.size(); i++)
				if (gnd->textures[i]->file == a.file && usedTex.count(i)) referenced = true;
			if (!referenced) ret.push_back(a.file);
		}
		return ret;
	}

	int deleteUnused(Map* map, BrowEdit* be, Project& P)
	{
		int n = 0;
		for (auto& f : unusedAtlases(map, P))
		{
			std::error_code ec;
			if (std::filesystem::remove(diskPath(be, f), ec)) n++;
			for (auto& a : P.atlases) if (a.file == f) { a.retired = true; a.hash = 0; a.rgba.clear(); }
		}
		return n;
	}

	// ------------------------------------------------------------------ sidecar
	static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	static std::string encodeMask(const std::vector<uint8_t>& m)
	{
		std::string raw;
		for (size_t i = 0; i < m.size();)
		{
			size_t j = i; while (j < m.size() && m[j] == m[i] && j - i < 255) j++;
			raw.push_back((char)(j - i)); raw.push_back((char)m[i]); i = j;
		}
		std::string out;
		for (size_t i = 0; i < raw.size(); i += 3)
		{
			uint32_t v = (uint8_t)raw[i] << 16 | (i + 1 < raw.size() ? (uint8_t)raw[i + 1] << 8 : 0) | (i + 2 < raw.size() ? (uint8_t)raw[i + 2] : 0);
			out.push_back(B64[(v >> 18) & 63]); out.push_back(B64[(v >> 12) & 63]);
			out.push_back(i + 1 < raw.size() ? B64[(v >> 6) & 63] : '='); out.push_back(i + 2 < raw.size() ? B64[v & 63] : '=');
		}
		return out;
	}
	static std::vector<uint8_t> decodeMask(const std::string& s)
	{
		int rev[256]; for (int i = 0; i < 256; i++) rev[i] = -1;
		for (int i = 0; i < 64; i++) rev[(uint8_t)B64[i]] = i;
		std::string raw;
		for (size_t i = 0; i + 4 <= s.size(); i += 4)
		{
			int a = rev[(uint8_t)s[i]], b = rev[(uint8_t)s[i + 1]], c = s[i + 2] == '=' ? -2 : rev[(uint8_t)s[i + 2]], d = s[i + 3] == '=' ? -2 : rev[(uint8_t)s[i + 3]];
			if (a < 0 || b < 0) break;
			raw.push_back((char)((a << 2) | (b >> 4)));
			if (c >= 0) raw.push_back((char)(((b & 15) << 4) | (c >> 2)));
			if (c >= 0 && d >= 0) raw.push_back((char)(((c & 3) << 6) | d));
		}
		std::vector<uint8_t> m;
		for (size_t i = 0; i + 1 < raw.size(); i += 2) m.insert(m.end(), (uint8_t)raw[i], (uint8_t)raw[i + 1]);
		m.resize(m.size() > (size_t)MASK * MASK ? (size_t)MASK * MASK + 32 : (size_t)MASK * MASK, 0);
		return m;
	}

	static std::string sidecarPath(Map* map, BrowEdit* be)
	{
		std::string n = map->name;
		std::string path = n.find(":") != std::string::npos ? n : be->config.ropath + n;
		if (path.size() > 4 && path.substr(path.size() - 4) == ".rsw") path = path.substr(0, path.size() - 4);
		return path + ".browtex.json";
	}

	void loadProject(Map* map, BrowEdit* be, Project& P)
	{
		P.loaded = true;
		P.path = sidecarPath(map, be);
		std::ifstream i(P.path, std::ios::binary);
		if (!i) return;
		try
		{
			auto j = nlohmann::json::parse(i);
			P.folder = j.value("folder", P.folder); P.cellPx = j.value("cellPx", 30); P.atlasSize = j.value("atlasSize", 512);
			P.nextAtlasId = j.value("nextAtlasId", 1); P.autoBake = j.value("autoBake", false); P.blockLayout = j.value("blockLayout", false);
			for (auto& l : j["layers"])
			{
				PaintLayer pl; pl.tex = util::utf8_to_iso_8859_1(l.value("tex", std::string())); pl.tiling = l.value("tiling", 2.0f);
				pl.rot = l.value("rot", 0); pl.flipH = l.value("flipH", false); pl.flipV = l.value("flipV", false); pl.offX = l.value("offX", 0.0f); pl.offY = l.value("offY", 0.0f); pl.ribbon = l.value("ribbon", false);
				P.layers.push_back(pl);
			}
			for (auto& a : j["atlases"])
			{
				Atlas A; A.id = a.value("id", 0); A.file = util::utf8_to_iso_8859_1(a.value("file", std::string())); A.size = a.value("size", 512); A.cellPx = a.value("cellPx", 30);
				A.hash = std::stoull(a.value("hash", std::string("0")), nullptr, 16); A.retired = a.value("retired", false); A.dirty = false;
				A.slots = a["slots"].get<std::vector<int>>(); A.blockX = a.value("bx", -1); A.blockY = a.value("by", -1);
				P.atlases.push_back(A);
				P.nextAtlasId = std::max(P.nextAtlasId, A.id + 1);
			}
			for (auto& c : j["cells"])
			{
				Cell C; int k = c.value("k", 0);
				C.atlas = c.value("atlas", -1); C.slot = c.value("slot", -1); C.hasTile = c.value("hasTile", false); C.dirty = false;
				if (c.contains("orig"))
				{
					C.orig.valid = true; C.orig.file = util::utf8_to_iso_8859_1(c["orig"].value("file", std::string()));
					auto uv = c["orig"]["uv"].get<std::vector<float>>();
					for (int q = 0; q < 8 && q < (int)uv.size(); q++) C.orig.uv[q] = uv[q];
				}
				for (auto it = c["masks"].begin(); it != c["masks"].end(); ++it) C.masks[std::stoi(it.key())] = decodeMask(it.value().get<std::string>());
				P.cells[k] = C;
			}
			// files that vanished or changed on disk are re-baked on the next bake
			for (auto& a : P.atlases)
			{
				std::error_code ec;
				if (a.retired) continue;
				if (!std::filesystem::exists(diskPath(be, a.file), ec)) { a.dirty = true; a.hash = 0; P.pending = true; }
			}
			if (P.pending) for (auto& kv : P.cells) kv.second.dirty = true;
		}
		catch (std::exception& e) { std::cerr << "AdvTex: could not read " << P.path << ": " << e.what() << std::endl; }
	}

	void saveProject(Map* map, BrowEdit* be, Project& P)
	{
		if (!P.loaded) return;
		if (P.layers.empty() && P.cells.empty() && P.atlases.empty()) { std::error_code ec; if (std::filesystem::exists(P.path, ec)) std::filesystem::remove(P.path, ec); return; }
		nlohmann::json j;
		j["version"] = 1; j["folder"] = P.folder; j["cellPx"] = P.cellPx; j["atlasSize"] = P.atlasSize; j["nextAtlasId"] = P.nextAtlasId; j["autoBake"] = P.autoBake; j["blockLayout"] = P.blockLayout;
		j["layers"] = nlohmann::json::array(); j["atlases"] = nlohmann::json::array(); j["cells"] = nlohmann::json::array();
		for (auto& l : P.layers) j["layers"].push_back({ {"tex", util::iso_8859_1_to_utf8(l.tex)}, {"tiling", l.tiling}, {"rot", l.rot}, {"flipH", l.flipH}, {"flipV", l.flipV}, {"offX", l.offX}, {"offY", l.offY}, {"ribbon", l.ribbon} });
		for (auto& a : P.atlases)
		{
			std::ostringstream hs; hs << std::hex << (unsigned long long)a.hash;
			std::string h = hs.str();
			j["atlases"].push_back({ {"id", a.id}, {"file", util::iso_8859_1_to_utf8(a.file)}, {"size", a.size}, {"cellPx", a.cellPx}, {"hash", h}, {"retired", a.retired}, {"slots", a.slots}, {"bx", a.blockX}, {"by", a.blockY} });
		}
		for (auto& kv : P.cells)
		{
			const Cell& c = kv.second;
			nlohmann::json cj; cj["k"] = kv.first; cj["atlas"] = c.atlas; cj["slot"] = c.slot; cj["hasTile"] = c.hasTile;
			if (c.orig.valid) cj["orig"] = { {"file", util::iso_8859_1_to_utf8(c.orig.file)}, {"uv", std::vector<float>(c.orig.uv, c.orig.uv + 8)} };
			cj["masks"] = nlohmann::json::object();
			for (auto& m : c.masks)
			{
				bool any = false; for (auto v : m.second) if (v) { any = true; break; }
				if (any) cj["masks"][std::to_string(m.first)] = encodeMask(m.second);
			}
			j["cells"].push_back(cj);
		}
		std::string tmp = P.path + ".tmp";
		{
			std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
			if (!o) { std::cerr << "AdvTex: cannot write " << tmp << std::endl; return; }
			o << j.dump();
		}
		std::error_code ec;
		std::filesystem::rename(tmp, P.path, ec);
		if (ec) { std::filesystem::remove(P.path, ec); std::filesystem::rename(tmp, P.path, ec); }
	}

	// ------------------------------------------------------------------ project registry
	Project& project(Map* map, BrowEdit* be)
	{
		auto& P = projects[map];
		if (!P.loaded) loadProject(map, be, P);
		return P;
	}

	void forget(Map* map)
	{
		auto it = projects.find(map);
		if (it == projects.end()) return;
		for (auto& a : it->second.atlases) delete a.prev;
		projects.erase(it);
	}

	bool hasUnbuilt(Map* map)
	{
		auto it = projects.find(map);
		if (it == projects.end()) return false;
		if (it->second.pending) return true;
		for (auto& kv : it->second.cells) if (kv.second.dirty) return true;
		return false;
	}

	bool onSave(Map* map, BrowEdit* be)
	{
		auto it = projects.find(map);
		if (it == projects.end()) return true;
		Project& P = it->second;
		if (hasUnbuilt(map))
		{
			if (P.saveChoice == 0) { askMap = map; return false; }
			if (P.saveChoice == 1)
			{
				std::string msg;
				bool ok = bake(map, be, P, false, msg);
				P.status = msg;
				std::cout << "AdvTex: " << msg << std::endl;
				if (!ok) { P.saveChoice = 0; askMap = nullptr; return false; }	// nothing was changed; the user can fix the problem and save again
			}
			else
				std::cout << "AdvTex: saved as draft, the baked textures were not rebuilt" << std::endl;
		}
		P.saveChoice = 0;
		saveProject(map, be, P);
		return true;
	}

	void showSavePrompt(BrowEdit* be)
	{
		if (askMap && !projects.count(askMap)) askMap = nullptr;
		if (askMap) ImGui::OpenPopup("Advanced Texture: build before saving?");
		if (!ImGui::BeginPopupModal("Advanced Texture: build before saving?", 0, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking)) return;
		Map* map = askMap;
		if (!map) { ImGui::CloseCurrentPopup(); ImGui::EndPopup(); return; }
		Project& P = projects[map];
		int cells = 0; for (auto& kv : P.cells) if (kv.second.dirty) cells++;
		ImGui::Text("%d painted cell(s) have not been built into texture files yet.", cells);
		ImGui::Text("Build the baked textures now? Finished textures are written to data\\texture\\%s.", P.folder.c_str());
		ImGui::Spacing();
		ImGui::TextWrapped("Save as draft keeps the paint in the map's .browtex.json so you can keep editing; the GND and the texture files stay as they were.");
		ImGui::Spacing();
		auto go = [&](int choice) { P.saveChoice = choice; askMap = nullptr; ImGui::CloseCurrentPopup(); be->saveMap(map); };
		if (ImGui::Button("Build and save")) go(1);
		ImGui::SameLine();
		if (ImGui::Button("Save as draft")) go(2);
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) { askMap = nullptr; ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
}
