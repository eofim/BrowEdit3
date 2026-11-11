#include <glad/gl.h>
#include "AdvTex.h"
#include <browedit/BrowEdit.h>
#include <browedit/Map.h>
#include <browedit/MapView.h>
#include <browedit/Node.h>
#include <browedit/actions/Action.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/GndRenderer.h>
#include <browedit/shaders/GndShader.h>
#include <browedit/gl/Vertex.h>
#include <browedit/gl/FBO.h>
#include <browedit/gl/Texture.h>
#include <browedit/math/Ray.h>
#include <browedit/shaders/SimpleShader.h>
#include <browedit/util/FileIO.h>
#include <browedit/util/ResourceManager.h>
#include <browedit/util/Util.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <imgui_internal.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <queue>

namespace advtex
{
	static const glm::vec3 NO_HIT(std::numeric_limits<float>().max());

	enum Tool { ToolBrush, ToolRect, ToolFill, ToolFillAll, ToolPick };

	// ------------------------------------------------------------------ state
	static bool stroking = false, strokeErase = false, haveLast = false;
	static glm::vec2 lastPos;
	static std::map<int, Masks> strokeBefore;
	static int tool = ToolBrush;
	static float brushW = 3.0f, brushH = 3.0f;		// cells
	static int brushShape = 0;						// 0 ellipse, 1 rectangle
	static bool snapBrush = true;
	// the permanent grid overlay
	static bool gridShow = true, gridSnap = false;
	static float gridStep = 10.0f, gridOffX = 0.0f, gridOffZ = 0.0f, gridWidth = 2.0f;
	static glm::vec4 gridColor(1.0f, 0.0f, 0.0f, 1.0f);
	static float brushOpacity = 1.0f, brushHardness = 0.5f;
	static bool eraseMode = false;
	static std::string statusLine;
	static bool rectActive = false;
	static glm::ivec2 rectStart(0), rectEnd(0);

	static void markEdited(Project& P) { P.pending = true; P.lastEdit = glfwGetTime(); }

	static bool captureOrig(Gnd* gnd, int x, int y, Cell& c)
	{
		if (x < 0 || y < 0 || x >= gnd->width || y >= gnd->height) return false;
		auto cube = gnd->cubes[x][y];
		if (cube->tileUp < 0) return false;
		auto t = gnd->tiles[cube->tileUp];
		if (t->textureIndex < 0 || t->textureIndex >= (int)gnd->textures.size()) return false;
		c.orig.valid = true; c.orig.file = gnd->textures[t->textureIndex]->file;
		c.orig.uv[0] = t->v1.x; c.orig.uv[1] = t->v1.y; c.orig.uv[2] = t->v2.x; c.orig.uv[3] = t->v2.y;
		c.orig.uv[4] = t->v3.x; c.orig.uv[5] = t->v3.y; c.orig.uv[6] = t->v4.x; c.orig.uv[7] = t->v4.y;
		return true;
	}

	// the texture the cell had before any paint
	static std::string baseFile(Gnd* gnd, Project& P, int x, int y)
	{
		auto it = P.cells.find(key(x, y));
		if (it != P.cells.end() && it->second.orig.valid) return it->second.orig.file;
		auto cube = gnd->cubes[x][y];
		if (cube->tileUp < 0) return "";
		auto t = gnd->tiles[cube->tileUp];
		if (t->textureIndex < 0 || t->textureIndex >= (int)gnd->textures.size()) return "";
		return gnd->textures[t->textureIndex]->file;
	}

	static int layerForFile(Project& P, const std::string& file)
	{
		for (int i = 0; i < (int)P.layers.size(); i++) if (P.layers[i].tex == file && !P.layers[i].ribbon) return i;
		PaintLayer l; l.tex = file;
		P.layers.push_back(l);
		getImg(file);		// load it now so the first stroke does not stall
		return (int)P.layers.size() - 1;
	}

	// ------------------------------------------------------------------ undo / redo (paint data only; files are written by the next bake)
	class PaintAction : public Action
	{
		std::map<int, Masks> before, after;
		std::string title;
		void apply(Map* map, BrowEdit* be, const std::map<int, Masks>& data)
		{
			auto& P = project(map, be);
			auto gnd = map->rootNode->getComponent<Gnd>();
			for (auto& kv : data)
			{
				auto it = P.cells.find(kv.first);
				if (it == P.cells.end())
				{
					Cell c;
					if (!captureOrig(gnd, keyX(kv.first), keyY(kv.first), c)) continue;
					it = P.cells.emplace(kv.first, c).first;
				}
				it->second.masks = kv.second;
				it->second.dirty = true;
				P.touched.insert(kv.first);
			}
			markEdited(P);
		}
	public:
		PaintAction(const std::map<int, Masks>& b, const std::map<int, Masks>& a, const std::string& t) : before(b), after(a), title(t) {}
		void perform(Map* map, BrowEdit* be) override { apply(map, be, after); }
		void undo(Map* map, BrowEdit* be) override { apply(map, be, before); }
		std::string str() override { return title; }
	};

	Action* makeRibbonAction(Map* map, BrowEdit* be, const std::string& ribbonTex, const std::string& groundTex, const std::vector<RibbonCell>& cells, const std::string& title)
	{
		auto& P = project(map, be);
		auto gnd = map->rootNode->getComponent<Gnd>();
		if (cells.empty() || ribbonTex.empty()) return nullptr;
		int ground = groundTex.empty() ? -1 : layerForFile(P, groundTex);
		int rib = -1;
		for (int i = 0; i < (int)P.layers.size(); i++) if (P.layers[i].ribbon && P.layers[i].tex == ribbonTex && i > ground) rib = i;
		if (rib < 0) { PaintLayer l; l.tex = ribbonTex; l.ribbon = true; P.layers.push_back(l); rib = (int)P.layers.size() - 1; getImg(ribbonTex); }
		std::map<int, Masks> before, after;
		for (auto& rc : cells)
		{
			int k = key(rc.x, rc.y);
			if (rc.x < 0 || rc.y < 0 || rc.x >= gnd->width || rc.y >= gnd->height || gnd->cubes[rc.x][rc.y]->tileUp < 0) continue;
			auto it = P.cells.find(k);
			Masks b = it != P.cells.end() ? it->second.masks : Masks();
			Masks a = b;
			bool blank = true;
			for (uint8_t v : rc.alpha) if (v > 4) { blank = false; break; }
			if (blank)
			{
				if (!a.erase(rib)) continue;		// nothing to clear
				before[k] = b; after[k] = a;
				continue;
			}
			if (ground >= 0) a[ground].assign(MASK * MASK, 255);
			std::vector<uint8_t> mk = rc.alpha;
			mk.resize(MASK * MASK, 0);
			mk.resize(MASK * MASK + 32);
			std::memcpy(mk.data() + MASK * MASK, rc.uv, 32);
			a[rib] = mk;
			before[k] = b; after[k] = a;
		}
		if (after.empty()) return nullptr;
		return new PaintAction(before, after, title);
	}

	Action* makeLayeredAction(Map* map, BrowEdit* be, const std::vector<LayerSpec>& specs, const std::vector<LayeredCell>& cells, const std::string& title)
	{
		auto& P = project(map, be);
		auto gnd = map->rootNode->getComponent<Gnd>();
		if (cells.empty() || specs.empty()) return nullptr;
		std::vector<int> idx(specs.size(), -1);
		int prev = -1;
		for (size_t j = 0; j < specs.size(); j++)
		{
			if (specs[j].tex.empty()) continue;
			int found = -1;
			for (int i = prev + 1; i < (int)P.layers.size(); i++)
				if (P.layers[i].tex == specs[j].tex && P.layers[i].ribbon == specs[j].ribbon && (specs[j].ribbon || std::fabs(P.layers[i].tiling - specs[j].tiling) < 1e-4f)) { found = i; break; }
			if (found < 0) { PaintLayer l; l.tex = specs[j].tex; l.ribbon = specs[j].ribbon; l.tiling = specs[j].tiling; P.layers.push_back(l); found = (int)P.layers.size() - 1; getImg(l.tex); }
			idx[j] = found; prev = found;
		}
		std::map<int, Masks> before, after;
		for (auto& lc : cells)
		{
			int k = key(lc.x, lc.y);
			if (lc.x < 0 || lc.y < 0 || lc.x >= gnd->width || lc.y >= gnd->height || gnd->cubes[lc.x][lc.y]->tileUp < 0) continue;
			auto it = P.cells.find(k);
			Masks b = it != P.cells.end() ? it->second.masks : Masks();
			Masks a = b;
			bool any = false;
			for (size_t j = 0; j < specs.size() && j < lc.alpha.size(); j++)
			{
				if (idx[j] < 0) continue;
				if (lc.alpha[j].empty()) continue;
				{
					bool blank = true;
					for (uint8_t v : lc.alpha[j]) if (v > 4) { blank = false; break; }
					if (blank)
					{
						// nothing of this layer belongs in the cell any more: drop what was painted there before
						if (a.erase(idx[j])) any = true;
						continue;
					}
				}
				std::vector<uint8_t> mk = lc.alpha[j];
				mk.resize(MASK * MASK, 0);
				if (specs[j].ribbon)
				{
					mk.resize(MASK * MASK + 32);
					float f[8] = { 0,0,0,0,0,0,0,0 };
					for (int q = 0; q < 8 && q < (int)lc.uv[j].size(); q++) f[q] = lc.uv[j][q];
					std::memcpy(mk.data() + MASK * MASK, f, 32);
				}
				a[idx[j]] = mk; any = true;
			}
			if (!any) continue;
			before[k] = b; after[k] = a;
		}
		if (after.empty()) return nullptr;
		return new PaintAction(before, after, title);
	}

	// ------------------------------------------------------------------ painting
	static Cell* touchCell(Project& P, Gnd* gnd, int cx, int cy, bool create)
	{
		if (cx < 0 || cy < 0 || cx >= gnd->width || cy >= gnd->height) return nullptr;
		if (gnd->cubes[cx][cy]->tileUp < 0) return nullptr;
		int k = key(cx, cy);
		auto it = P.cells.find(k);
		if (it == P.cells.end())
		{
			if (!create) return nullptr;
			Cell c;
			if (!captureOrig(gnd, cx, cy, c)) return nullptr;
			it = P.cells.emplace(k, c).first;
		}
		Cell& c = it->second;
		if (!c.orig.valid && !captureOrig(gnd, cx, cy, c)) return nullptr;
		if (!strokeBefore.count(k)) strokeBefore[k] = c.masks;
		return &c;
	}

	static void stamp(Project& P, Gnd* gnd, glm::vec2 p, bool erase, int layer)
	{
		float hw = brushW * 5.0f, hh = brushH * 5.0f;
		int mMin = (int)floorf((p.y - hh) / 10.0f), mMax = (int)floorf((p.y + hh) / 10.0f);
		int x0 = (int)floorf((p.x - hw) / 10.0f), x1 = (int)floorf((p.x + hw) / 10.0f);
		for (int cy = gnd->height - mMax; cy <= gnd->height - mMin; cy++)
			for (int cx = x0; cx <= x1; cx++)
			{
				if (cx < 0 || cy < 0 || cx >= gnd->width || cy >= gnd->height) continue;
				int k = key(cx, cy);
				if (erase) { auto it = P.cells.find(k); if (it == P.cells.end() || !it->second.masks.count(layer)) continue; }
				Cell* cp = touchCell(P, gnd, cx, cy, true);
				if (!cp) continue;
				auto& m = cp->masks[layer];
				if (m.empty()) m.assign(MASK * MASK, 0);
				bool changed = false;
				for (int j = 0; j < MASK; j++)
					for (int i = 0; i < MASK; i++)
					{
						float wx = 10.0f * cx + 10.0f * (i + 0.5f) / MASK, wz = 10.0f * (gnd->height - cy) + 10.0f * (j + 0.5f) / MASK;
						float nx = (wx - p.x) / hw, nz = (wz - p.y) / hh;
						float d = brushShape == 0 ? sqrtf(nx * nx + nz * nz) : std::max(fabsf(nx), fabsf(nz));
						if (d >= 1.0f) continue;
						float w = d <= brushHardness ? 1.0f : 1.0f - (d - brushHardness) / (1.0f - brushHardness + 1e-4f);
						w = std::clamp(w, 0.0f, 1.0f) * brushOpacity;
						uint8_t& v = m[j * MASK + i];
						uint8_t nv = erase ? (uint8_t)lroundf(v * (1.0f - w)) : (uint8_t)std::max<int>(v, (int)lroundf(255.0f * w));
						if (nv != v) { v = nv; changed = true; }
					}
				if (changed) { cp->dirty = true; P.touched.insert(k); markEdited(P); }
			}
	}

	// paints whole cells (rectangle / fill tools)
	static void fillCells(Project& P, Gnd* gnd, const std::vector<glm::ivec2>& cells, bool erase, int layer)
	{
		uint8_t val = erase ? 0 : (uint8_t)lroundf(255.0f * brushOpacity);
		for (auto& c : cells)
		{
			if (erase) { auto it = P.cells.find(key(c.x, c.y)); if (it == P.cells.end() || !it->second.masks.count(layer)) continue; }
			Cell* cp = touchCell(P, gnd, c.x, c.y, true);
			if (!cp) continue;
			auto& m = cp->masks[layer];
			if (m.empty()) m.assign(MASK * MASK, 0);
			for (auto& v : m) v = erase ? 0 : std::max(v, val);
			cp->dirty = true; P.touched.insert(key(c.x, c.y));
		}
		markEdited(P);
	}

	static void endStroke(MapView* mv, BrowEdit* be, Project& P, const char* title)
	{
		std::map<int, Masks> after;
		for (auto& kv : strokeBefore) if (P.cells.count(kv.first)) after[kv.first] = P.cells[kv.first].masks;
		if (!after.empty()) mv->map->doAction(new PaintAction(strokeBefore, after, title), be);
		strokeBefore.clear();
	}

	// ------------------------------------------------------------------ viewport drawing
	static void drawLoop(MapView* mv, Gnd* gnd, const std::vector<glm::vec2>& pts, glm::vec4 color)
	{
		std::vector<float> v;
		for (auto& p : pts)
		{
			auto r = gnd->rayCast(math::Ray(glm::vec3(p.x, 3000, p.y), glm::vec3(0, -1, 0)));
			float y = r != NO_HIT ? r.y : 0;
			v.insert(v.end(), { p.x, y + 0.5f, p.y, 0, 0 });
		}
		if (v.empty()) return;
		mv->simpleShader->use();
		mv->simpleShader->setUniform(SimpleShader::Uniforms::projectionMatrix, mv->nodeRenderContext.projectionMatrix);
		mv->simpleShader->setUniform(SimpleShader::Uniforms::viewMatrix, mv->nodeRenderContext.viewMatrix);
		mv->simpleShader->setUniform(SimpleShader::Uniforms::modelMatrix, glm::mat4(1.0f));
		mv->simpleShader->setUniform(SimpleShader::Uniforms::textureFac, 0.0f);
		mv->simpleShader->setUniform(SimpleShader::Uniforms::color, color);
		mv->fbo->bind();
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDisable(GL_TEXTURE_2D);
		glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
		glDisableVertexAttribArray(2); glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
		glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &v[0]);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &v[3]);
		glLineWidth(2);
		glDrawArrays(GL_LINE_LOOP, 0, (int)(v.size() / 5));
		glLineWidth(1);
		mv->fbo->unbind();
	}

	static std::vector<glm::vec2> brushOutline(glm::vec2 c)
	{
		std::vector<glm::vec2> pts;
		float hw = brushW * 5.0f, hh = brushH * 5.0f;
		if (brushShape == 0)
			for (int i = 0; i < 64; i++) { float a = i / 64.0f * 6.2831853f; pts.emplace_back(c.x + cosf(a) * hw, c.y + sinf(a) * hh); }
		else
		{
			const int n = 16;
			glm::vec2 corner[4] = { {c.x - hw, c.y - hh}, {c.x + hw, c.y - hh}, {c.x + hw, c.y + hh}, {c.x - hw, c.y + hh} };
			for (int e = 0; e < 4; e++)
				for (int i = 0; i < n; i++) pts.push_back(corner[e] + (corner[(e + 1) % 4] - corner[e]) * (i / (float)n));
		}
		return pts;
	}

	static std::vector<glm::vec2> cellRectOutline(Gnd* gnd, glm::ivec2 a, glm::ivec2 b)
	{
		int x0 = std::min(a.x, b.x), x1 = std::max(a.x, b.x) + 1, y0 = std::min(a.y, b.y), y1 = std::max(a.y, b.y) + 1;
		float xa = 10.0f * x0, xb = 10.0f * x1, za = 10.0f * (gnd->height - y1 + 1), zb = 10.0f * (gnd->height - y0 + 1);
		std::vector<glm::vec2> pts;
		const int n = 16;
		glm::vec2 corner[4] = { {xa, za}, {xb, za}, {xb, zb}, {xa, zb} };
		for (int e = 0; e < 4; e++)
			for (int i = 0; i < n; i++) pts.push_back(corner[e] + (corner[(e + 1) % 4] - corner[e]) * (i / (float)n));
		return pts;
	}

	// terrain height of a point inside cube (cx, cy): fx/fz 0..1 across the cube
	static float cubeY(Gnd* gnd, int cx, int cy, float fx, float fz)
	{
		auto c = gnd->cubes[cx][cy];
		float a = c->h3 * (1 - fx) + c->h4 * fx, b = c->h1 * (1 - fx) + c->h2 * fx;
		return -(a * (1 - fz) + b * fz) + 0.5f;
	}

	static void drawGrid(MapView* mv, Gnd* gnd)
	{
		float step = std::max(0.5f, gridStep);
		float W = 10.0f * gnd->width, zmin = 10.0f, zmax = 10.0f * gnd->height + 10.0f;
		std::vector<float> v;
		auto push = [&](float x, float y, float z) { v.insert(v.end(), { x, y, z, 0, 0 }); };
		// lines that run along x (fixed z)
		for (float z = zmin + std::fmod(std::fmod(gridOffZ - zmin, step) + step, step); z <= zmax + 0.001f; z += step)
		{
			int cy = std::clamp(gnd->height - (int)std::floor(std::min(z, zmax - 0.001f) / 10.0f), 0, gnd->height - 1);
			float fz = (z - 10.0f * (gnd->height - cy)) / 10.0f;
			for (int cx = 0; cx < gnd->width; cx++)
			{
				push(10.0f * cx, cubeY(gnd, cx, cy, 0, fz), z);
				push(10.0f * cx + 10.0f, cubeY(gnd, cx, cy, 1, fz), z);
			}
		}
		// lines that run along z (fixed x)
		for (float x = std::fmod(std::fmod(gridOffX, step) + step, step); x <= W + 0.001f; x += step)
		{
			int cx = std::clamp((int)std::floor(std::min(x, W - 0.001f) / 10.0f), 0, gnd->width - 1);
			float fx = (x - 10.0f * cx) / 10.0f;
			for (int cy = 0; cy < gnd->height; cy++)
			{
				float Z = 10.0f * (gnd->height - cy);
				push(x, cubeY(gnd, cx, cy, fx, 0), Z);
				push(x, cubeY(gnd, cx, cy, fx, 1), Z + 10.0f);
			}
		}
		if (v.empty()) return;
		mv->simpleShader->use();
		mv->simpleShader->setUniform(SimpleShader::Uniforms::projectionMatrix, mv->nodeRenderContext.projectionMatrix);
		mv->simpleShader->setUniform(SimpleShader::Uniforms::viewMatrix, mv->nodeRenderContext.viewMatrix);
		mv->simpleShader->setUniform(SimpleShader::Uniforms::modelMatrix, glm::mat4(1.0f));
		mv->simpleShader->setUniform(SimpleShader::Uniforms::textureFac, 0.0f);
		mv->simpleShader->setUniform(SimpleShader::Uniforms::color, gridColor);
		mv->fbo->bind();
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glDisable(GL_TEXTURE_2D);
		glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
		glDisableVertexAttribArray(2); glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
		glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &v[0]);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &v[3]);
		glLineWidth(gridWidth);
		glDrawArrays(GL_LINES, 0, (int)(v.size() / 5));
		glLineWidth(1);
		mv->fbo->unbind();
	}

	static float snapAxis(float v, float cells)
	{
		int c = std::max(1, (int)lroundf(cells));
		if (c % 2 == 1) return (floorf(v / 10.0f) + 0.5f) * 10.0f;
		return roundf(v / 10.0f) * 10.0f;
	}

	// ------------------------------------------------------------------ interaction (only in the Advanced Texture mode)
	void postRender(MapView* mv, BrowEdit* be)
	{
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		if (!gnd) return;
		Project& P = project(mv->map, be);
		auto& io = ImGui::GetIO();
		if (gridShow) drawGrid(mv, gnd);
		bool layerOk = P.selLayer >= 0 && P.selLayer < (int)P.layers.size();
		glm::vec3 hit = mv->hovered || stroking || rectActive ? gnd->rayCast(mv->mouseRay) : NO_HIT;
		bool ok = hit != NO_HIT;
		glm::vec2 cur(ok ? hit.x : 0.0f, ok ? hit.z : 0.0f);
		glm::ivec2 curCell(ok ? std::clamp((int)floorf(hit.x / 10.0f), 0, gnd->width - 1) : 0, ok ? std::clamp(gnd->height - (int)floorf(hit.z / 10.0f), 0, gnd->height - 1) : 0);
		if (ok && tool == ToolBrush && snapBrush && !io.KeyShift)
		{
			if (gridSnap)
			{
				// the brush centre sits on the middle of a grid cell (odd size) or on a grid line (even size)
				auto sn = [&](float v, float off, float cells) {
					float st = std::max(0.5f, gridStep);
					return ((int)lroundf(cells)) % 2 == 1 ? (floorf((v - off) / st) + 0.5f) * st + off : roundf((v - off) / st) * st + off; };
				cur.x = sn(cur.x, gridOffX, brushW); cur.y = sn(cur.y, gridOffZ, brushH);
			}
			else { cur.x = snapAxis(cur.x, brushW); cur.y = snapAxis(cur.y, brushH); }
		}
		bool wantInput = io.WantTextInput;
		bool pickNow = ok && mv->hovered && ImGui::IsMouseClicked(0) && !wantInput && (tool == ToolPick || io.KeyAlt);

		if (pickNow)
		{
			bool picked = false;
			auto cit = P.cells.find(key(curCell.x, curCell.y));
			if (cit != P.cells.end())
			{
				int i = std::clamp((int)floorf((hit.x - 10.0f * curCell.x) / 10.0f * MASK), 0, MASK - 1);
				int j = std::clamp((int)floorf((hit.z - 10.0f * (gnd->height - curCell.y)) / 10.0f * MASK), 0, MASK - 1);
				for (auto it = cit->second.masks.rbegin(); it != cit->second.masks.rend() && !picked; ++it)
					if (it->first >= 0 && it->first < (int)P.layers.size() && it->second.size() == (size_t)MASK * MASK && it->second[j * MASK + i] >= 64)
					{ P.selLayer = it->first; picked = true; statusLine = "Picked layer " + P.layers[it->first].tex; }
			}
			if (!picked)
			{
				std::string f = baseFile(gnd, P, curCell.x, curCell.y);
				if (!f.empty()) { P.selLayer = layerForFile(P, f); statusLine = "Picked " + f; }
			}
		}
		else if (!stroking && !rectActive && mv->hovered && ok && ImGui::IsMouseClicked(0) && !wantInput && !io.KeyAlt && layerOk)
		{
			strokeErase = eraseMode || io.KeyCtrl;
			strokeBefore.clear();
			if (tool == ToolBrush) { stroking = true; haveLast = false; }
			else if (tool == ToolRect) { rectActive = true; rectStart = rectEnd = curCell; }
			else if (tool == ToolFill || tool == ToolFillAll)
			{
				std::string target = baseFile(gnd, P, curCell.x, curCell.y);
				std::vector<glm::ivec2> cells;
				if (!target.empty())
				{
					if (tool == ToolFillAll)
					{
						for (int y = 0; y < gnd->height; y++) for (int x = 0; x < gnd->width; x++)
							if (gnd->cubes[x][y]->tileUp >= 0 && baseFile(gnd, P, x, y) == target) cells.emplace_back(x, y);
					}
					else
					{
						std::vector<char> seen((size_t)gnd->width * gnd->height, 0);
						std::queue<glm::ivec2> q; q.push(curCell); seen[(size_t)curCell.y * gnd->width + curCell.x] = 1;
						const glm::ivec2 d4[4] = { {1,0},{-1,0},{0,1},{0,-1} };
						while (!q.empty() && cells.size() < 40000)
						{
							auto c = q.front(); q.pop(); cells.push_back(c);
							for (auto& d : d4)
							{
								glm::ivec2 n = c + d;
								if (n.x < 0 || n.y < 0 || n.x >= gnd->width || n.y >= gnd->height || seen[(size_t)n.y * gnd->width + n.x]) continue;
								seen[(size_t)n.y * gnd->width + n.x] = 1;
								if (gnd->cubes[n.x][n.y]->tileUp >= 0 && baseFile(gnd, P, n.x, n.y) == target) q.push(n);
							}
						}
					}
				}
				fillCells(P, gnd, cells, strokeErase, P.selLayer);
				endStroke(mv, be, P, strokeErase ? "Advanced texture erase fill" : "Advanced texture fill");
				strokeErase = false;
			}
		}

		if (stroking)
		{
			if (ImGui::IsMouseDown(0))
			{
				if (ok && layerOk)
				{
					if (!haveLast) { stamp(P, gnd, cur, strokeErase, P.selLayer); lastPos = cur; haveLast = true; }
					else
					{
						float step = std::max(2.0f, std::min(brushW, brushH) * 10.0f * 0.2f), dist = glm::length(cur - lastPos);
						if (dist >= step)
						{
							int n = (int)(dist / step);
							glm::vec2 dir = (cur - lastPos) / dist;
							for (int i = 1; i <= n; i++) stamp(P, gnd, lastPos + dir * (step * i), strokeErase, P.selLayer);
							lastPos += dir * (step * n);
						}
					}
				}
			}
			else
			{
				stroking = false;
				endStroke(mv, be, P, strokeErase ? "Advanced texture erase" : "Advanced texture paint");
			}
		}
		if (rectActive)
		{
			if (ImGui::IsMouseDown(0)) { if (ok) rectEnd = curCell; }
			else
			{
				rectActive = false;
				std::vector<glm::ivec2> cells;
				for (int y = std::min(rectStart.y, rectEnd.y); y <= std::max(rectStart.y, rectEnd.y); y++)
					for (int x = std::min(rectStart.x, rectEnd.x); x <= std::max(rectStart.x, rectEnd.x); x++) cells.emplace_back(x, y);
				fillCells(P, gnd, cells, strokeErase, P.selLayer);
				endStroke(mv, be, P, strokeErase ? "Advanced texture erase area" : "Advanced texture fill area");
			}
		}

		glm::vec4 col = (strokeErase && (stroking || rectActive)) || eraseMode || io.KeyCtrl ? glm::vec4(1, 0.3f, 0.2f, 1) : glm::vec4(0.2f, 0.9f, 1, 1);
		if (rectActive) drawLoop(mv, gnd, cellRectOutline(gnd, rectStart, rectEnd), col);
		else if (mv->hovered && ok)
		{
			if (tool == ToolBrush) drawLoop(mv, gnd, brushOutline(cur), col);
			else drawLoop(mv, gnd, cellRectOutline(gnd, curCell, curCell), tool == ToolPick || io.KeyAlt ? glm::vec4(1, 1, 0.2f, 1) : col);
		}
	}

	// ------------------------------------------------------------------ live preview + delayed bake (runs in every edit mode)
	static void uploadBlock(Atlas& a, int slot)
	{
		int block = a.cellPx + 2 * PAD, perRow = a.size / block;
		int bx = (slot % perRow) * block, by = (slot / perRow) * block;
		if (!a.prev) a.prev = new gl::Texture(a.size, a.size);
		glBindTexture(GL_TEXTURE_2D, a.prev->id());
		glPixelStorei(GL_UNPACK_ROW_LENGTH, a.size);
		glTexSubImage2D(GL_TEXTURE_2D, 0, bx, by, block, block, GL_RGBA, GL_UNSIGNED_BYTE, &a.rgba[((size_t)by * a.size + bx) * 4]);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}

	static void flushPreview(Map* map, Project& P, Gnd* gnd, int budget)
	{
		int n = 0;
		while (!P.touched.empty() && n < budget)
		{
			int k = *P.touched.begin(); P.touched.erase(P.touched.begin()); n++;
			auto it = P.cells.find(k);
			if (it == P.cells.end() || !it->second.orig.valid) continue;
			Cell& c = it->second;
			if (c.slot < 0 || !atlasById(P, c.atlas)) assignSlot(P, map, k, c);
			Atlas* a = atlasById(P, c.atlas);
			if (!a) continue;
			if (a->rgba.size() != (size_t)a->size * a->size * 4) { a->rgba.assign((size_t)a->size * a->size * 4, 0); a->complete = false; }
			renderCellToAtlas(gnd, P, *a, k, c);
			a->dirty = true;
			uploadBlock(*a, c.slot);
			c.prevOk = true;
		}
	}

	// draws the unbuilt paint with the map's own ground shader, so lightmap shadows, light colours, lights, fog and tile colours apply like the final result
	static void drawPreview(MapView* mv, Project& P, Gnd* gnd)
	{
		bool any = false;
		for (auto& kv : P.cells) if (kv.second.dirty && kv.second.prevOk) { any = true; break; }
		if (!any) return;
		auto gr = gnd->node ? gnd->node->getComponent<GndRenderer>() : nullptr;
		if (!gr || !gr->gndShadow) return;
		auto ctx = GndRenderer::GndRenderContext::getInstance();
		auto shader = ctx->shader;
		shader->use();
		shader->setUniform(GndShader::Uniforms::ProjectionMatrix, mv->nodeRenderContext.projectionMatrix);
		shader->setUniform(GndShader::Uniforms::ModelViewMatrix, ctx->viewMatrix);
		shader->setUniform(GndShader::Uniforms::s_texture, 0);
		shader->setUniform(GndShader::Uniforms::s_lighting, 1);
		shader->setUniform(GndShader::Uniforms::shadowMapToggle, gr->viewLightmapShadow ? 0.0f : 1.0f);
		shader->setUniform(GndShader::Uniforms::lightColorToggle, gr->viewLightmapColor ? 1.0f : 0.0f);
		shader->setUniform(GndShader::Uniforms::colorToggle, gr->viewColors ? 0.0f : 1.0f);
		shader->setUniform(GndShader::Uniforms::viewTextures, 1.0f);
		glActiveTexture(GL_TEXTURE1);
		gr->gndShadow->bind();
		glActiveTexture(GL_TEXTURE0);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		for (int i = 0; i < 5; i++) glEnableVertexAttribArray(i);
		glEnable(GL_POLYGON_OFFSET_FILL);
		glPolygonOffset(-2.0f, -2.0f);
		const float lmsx = (float)gnd->lightmapWidth, lmsy = (float)gnd->lightmapHeight;
		const int rowCount = std::max(1, gr->shadowmapSize / gnd->lightmapWidth);
		auto tileColor = [&](int x, int y) -> glm::vec4 {
			if (x < 0 || y < 0 || x >= gnd->width || y >= gnd->height) return glm::vec4(1.0f);
			int t = gnd->cubes[x][y]->tileUp;
			return t == -1 ? glm::vec4(0.0f) : glm::vec4(gnd->tiles[t]->color) / 255.0f;
		};
		for (auto& a : P.atlases)
		{
			if (a.retired || !a.prev) continue;
			std::vector<VertexP3T2T2C4N3> v;
			int block = a.cellPx + 2 * PAD, perRow = a.size / block;
			for (int sl = 0; sl < (int)a.slots.size(); sl++)
			{
				if (a.slots[sl] < 0) continue;
				auto it = P.cells.find(a.slots[sl]);
				if (it == P.cells.end() || !it->second.dirty || !it->second.prevOk) continue;
				int x = keyX(it->first), y = keyY(it->first);
				if (x >= gnd->width || y >= gnd->height) continue;
				auto cube = gnd->cubes[x][y];
				if (cube->tileUp < 0 || cube->tileUp >= (int)gnd->tiles.size()) continue;
				auto tile = gnd->tiles[cube->tileUp];
				float bx = (float)((sl % perRow) * block + PAD), by = (float)((sl / perRow) * block + PAD);
				float u0 = bx / a.size, v0 = by / a.size, u1 = (bx + a.cellPx) / a.size, v1 = (by + a.cellPx) / a.size;
				glm::vec2 lm1(0), lm2(0);
				if (tile->lightmapIndex >= 0)
				{
					lm1 = glm::vec2((tile->lightmapIndex % rowCount) * (lmsx / gr->shadowmapSize) + 1.0f / gr->shadowmapSize, (tile->lightmapIndex / rowCount) * (lmsy / gr->shadowmapSize) + 1.0f / gr->shadowmapSize);
					lm2 = lm1 + glm::vec2((lmsx - 2.0f) / gr->shadowmapSize, (lmsy - 2.0f) / gr->shadowmapSize);
				}
				float X = 10.0f * x, Z = 10.0f * gnd->height - 10.0f * y;
				VertexP3T2T2C4N3 q1(glm::vec3(X, -cube->h3, Z), glm::vec2(u0, v0), glm::vec2(lm1.x, lm2.y), tileColor(x, y + 1), cube->normals[2]);
				VertexP3T2T2C4N3 q2(glm::vec3(X + 10, -cube->h4, Z), glm::vec2(u1, v0), glm::vec2(lm2.x, lm2.y), tileColor(x + 1, y + 1), cube->normals[3]);
				VertexP3T2T2C4N3 q3(glm::vec3(X, -cube->h1, Z + 10), glm::vec2(u0, v1), glm::vec2(lm1.x, lm1.y), glm::vec4(tile->color) / 255.0f, cube->normals[0]);
				VertexP3T2T2C4N3 q4(glm::vec3(X + 10, -cube->h2, Z + 10), glm::vec2(u1, v1), glm::vec2(lm2.x, lm1.y), tileColor(x + 1, y), cube->normals[1]);
				v.push_back(q4); v.push_back(q2); v.push_back(q1);
				v.push_back(q4); v.push_back(q1); v.push_back(q3);
			}
			if (v.empty()) continue;
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, a.prev->id());
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			const char* base = (const char*)v.data();
			glVertexAttribPointer(0, 3, GL_FLOAT, false, sizeof(VertexP3T2T2C4N3), base + 0 * sizeof(float));
			glVertexAttribPointer(1, 2, GL_FLOAT, false, sizeof(VertexP3T2T2C4N3), base + 3 * sizeof(float));
			glVertexAttribPointer(2, 2, GL_FLOAT, false, sizeof(VertexP3T2T2C4N3), base + 5 * sizeof(float));
			glVertexAttribPointer(3, 4, GL_FLOAT, false, sizeof(VertexP3T2T2C4N3), base + 7 * sizeof(float));
			glVertexAttribPointer(4, 3, GL_FLOAT, false, sizeof(VertexP3T2T2C4N3), base + 11 * sizeof(float));
			glDrawArrays(GL_TRIANGLES, 0, (int)v.size());
		}
		glDisable(GL_POLYGON_OFFSET_FILL);
	}

	void frame(MapView* mv, BrowEdit* be)
	{
		Project* P = findProject(mv->map);
		if (!P) return;
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		if (!gnd) return;
		flushPreview(mv->map, *P, gnd, 250);		// a few hundred cells per frame at most, so big brushes never stall
		drawPreview(mv, *P, gnd);
		// the files are written once painting has stopped for a moment
		if (P->autoBake && P->pending && P->touched.empty() && !stroking && !rectActive && !ImGui::IsMouseDown(0) && glfwGetTime() - P->lastEdit > 0.8)
		{
			std::string msg;
			bake(mv->map, be, *P, false, msg);
			statusLine = msg;
		}
	}

	// ------------------------------------------------------------------ Layer texture picker (same layout as the Texture Picker)
	static util::FileIO::Node* pickerNode = nullptr;

	void showLayerPicker(BrowEdit* be)
	{
		if (!be->windowData.layerPickerVisible) return;
		if (!be->activeMapView || !be->activeMapView->map) return;
		Project& P = project(be->activeMapView->map, be);
		if (!ImGui::Begin("Layer texture picker", &be->windowData.layerPickerVisible)) { ImGui::End(); return; }
		static std::string tagFilter;
		ImGui::InputText("Filter", &tagFilter);

		std::function<void(util::FileIO::Node*)> buildTreeNodes;
		buildTreeNodes = [&](util::FileIO::Node* node)
		{
			for (auto f : node->directories)
			{
				int flags = ImGuiTreeNodeFlags_OpenOnDoubleClick;
				if (f.second->directories.size() == 0) flags |= ImGuiTreeNodeFlags_Bullet;
				if (f.second == pickerNode) flags |= ImGuiTreeNodeFlags_Selected;
				if (ImGui::TreeNodeEx(f.second->name.c_str(), flags)) { buildTreeNodes(f.second); ImGui::TreePop(); }
				if (ImGui::IsItemClicked()) pickerNode = f.second;
			}
		};
		ImGui::BeginChild("left pane", ImVec2(250, 0), true);
		if (auto root = util::FileIO::directoryNode("data\\texture\\"))
		{
			if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnDoubleClick))
			{
				if (ImGui::IsItemClicked()) pickerNode = root;
				buildTreeNodes(root);
				ImGui::TreePop();
			}
			else if (ImGui::IsItemClicked()) pickerNode = root;
		}
		ImGui::EndChild();
		ImGui::SameLine();

		ImGuiStyle& style = ImGui::GetStyle();
		float window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
		auto buildBox = [&](const std::string& file, bool fullPath)
		{
			if (ImGui::BeginChild(file.c_str(), ImVec2(be->config.thumbnailSize.x, be->config.thumbnailSize.y + 50), true, ImGuiWindowFlags_NoScrollbar))
			{
				std::string path = util::utf8_to_iso_8859_1(file);
				if (!fullPath)
				{
					auto n = pickerNode;
					while (n) { if (n->name != "") path = util::utf8_to_iso_8859_1(n->name) + "\\" + path; n = n->parent; }
				}
				auto& cache = be->windowData.textureManageWindowCache;
				if (cache.find(path) == cache.end()) cache[path] = util::ResourceManager<gl::Texture>::load(path);
				ImTextureID texture = 0;
				if (cache[path]->loaded) texture = (ImTextureID)(long long)cache[path]->id();
				std::string rel = path.size() > 13 ? path.substr(13) : path;		// remove "data\texture\"
				bool used = false;
				for (auto& l : P.layers) if (l.tex == rel) used = true;
				if (ImGui::ImageButtonEx(ImGui::GetID(path.c_str()), texture, be->config.thumbnailSize, ImVec2(0, 0), ImVec2(1, 1), ImVec2(0, 0), ImVec4(0, 0, 0, 0), used ? ImVec4(0.3f, 1, 0.4f, 1) : ImVec4(1, 1, 1, 1)))
					P.selLayer = layerForFile(P, rel);
				if (ImGui::IsItemHovered())
				{
					std::string tip = used ? "Already a layer: click to select it" : "Click to add as a layer";
					for (auto& t : be->tagListReverse[path]) tip += "\n" + t;
					ImGui::SetTooltip("%s", tip.c_str());
				}
				ImGui::Text("%s", file.c_str());
			}
			ImGui::EndChild();
			float last_button_x2 = ImGui::GetItemRectMax().x;
			float next_button_x2 = last_button_x2 + style.ItemSpacing.x + be->config.thumbnailSize.x;
			if (next_button_x2 < window_visible_x2) ImGui::SameLine();
		};
		auto isImage = [](const std::string& f) { return f.find(".bmp") != std::string::npos || f.find(".tga") != std::string::npos || f.find(".png") != std::string::npos || f.find(".gif") != std::string::npos; };

		if (ImGui::BeginChild("right pane", ImVec2(ImGui::GetContentRegionAvail().x, 0), true))
		{
			if (pickerNode != nullptr && tagFilter == "")
			{
				window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
				for (auto file : pickerNode->files)
					if (isImage(file)) buildBox(file, false);
			}
			else if (tagFilter != "")
			{
				static std::string currentFilterText = "";
				static std::vector<std::string> filteredFiles;
				if (currentFilterText != tagFilter)
				{
					currentFilterText = tagFilter;
					std::vector<std::string> filterTags = util::split(tagFilter, " ");
					filteredFiles.clear();
					for (auto t : be->tagListReverse)
					{
						bool match = true;
						for (auto tag : filterTags)
						{
							bool tagOk = t.first.find(tag) != std::string::npos;
							for (auto fileTag : t.second) if (fileTag.find(tag) != std::string::npos) { tagOk = true; break; }
							if (!tagOk) { match = false; break; }
						}
						if (match && isImage(t.first)) filteredFiles.push_back(util::iso_8859_1_to_utf8(t.first));
					}
				}
				window_visible_x2 = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
				for (const auto& file : filteredFiles) buildBox(file, true);
			}
		}
		ImGui::EndChild();
		ImGui::End();
	}

	// ------------------------------------------------------------------ window
	static void swapLayers(Project& P, int a, int b)
	{
		std::swap(P.layers[a], P.layers[b]);
		for (auto& kv : P.cells)
		{
			auto& m = kv.second.masks;
			std::vector<uint8_t> va, vb; bool ha = m.count(a), hb = m.count(b);
			if (ha) va = m[a];
			if (hb) vb = m[b];
			m.erase(a); m.erase(b);
			if (hb) m[a] = vb;
			if (ha) m[b] = va;
			if (ha || hb) { kv.second.dirty = true; P.touched.insert(kv.first); }
		}
		markEdited(P);
	}

	static void removeLayer(Project& P, int idx)
	{
		for (auto& kv : P.cells)
		{
			Masks nm; bool had = false;
			for (auto& m : kv.second.masks)
			{
				if (m.first == idx) { had = true; continue; }
				nm[m.first > idx ? m.first - 1 : m.first] = m.second;
			}
			kv.second.masks = nm;
			if (had) { kv.second.dirty = true; P.touched.insert(kv.first); }
		}
		P.layers.erase(P.layers.begin() + idx);
		P.selLayer = std::clamp(P.selLayer, 0, std::max(0, (int)P.layers.size() - 1));
		markEdited(P);
	}

	static void layerChanged(Project& P, int layer)
	{
		for (auto& kv : P.cells) if (kv.second.masks.count(layer)) { kv.second.dirty = true; P.touched.insert(kv.first); }
		markEdited(P);
	}

	static void thumb(const std::string& file, float size)
	{
		auto t = util::ResourceManager<gl::Texture>::load("data\\texture\\" + file);
		if (t && t->id()) ImGui::Image((ImTextureID)(intptr_t)t->id(), ImVec2(size, size));
		else ImGui::Dummy(ImVec2(size, size));
	}

	void showWindow(BrowEdit* be)
	{
		if (!be->activeMapView || !be->activeMapView->map) return;
		Map* map = be->activeMapView->map;
		Project& P = project(map, be);

		ImGui::SetNextWindowSize(ImVec2(380, 640), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Advanced Texture"))
		{
			ImGui::TextWrapped("Paint is non-destructive and is built into %s_at_NNNN.bmp files that the GND uses like any other texture. Painting is a live draft preview; the texture files are only created when you press Build (or answer yes when saving). Left mouse paints, Ctrl + left mouse erases, Alt + left mouse is the dropper, Shift turns grid snapping off.", mapBase(map).c_str());
			ImGui::Separator();
			ImGui::Text("Tool");
			const char* toolNames[] = { "Brush", "Rectangle", "Fill area", "Fill all", "Dropper" };
			for (int i = 0; i < 5; i++)
			{
				if (i) ImGui::SameLine();
				if (ImGui::RadioButton(toolNames[i], tool == i)) tool = i;
			}
			ImGui::TextDisabled(tool == ToolBrush ? "Paint with the brush below." : tool == ToolRect ? "Drag a rectangle of cells." : tool == ToolFill ? "Click: paints all connected cells that share the clicked cell's texture." : tool == ToolFillAll ? "Click: paints every cell on the map with the clicked cell's texture." : "Click: picks the layer painted at that spot, or the cell's ground texture as a new layer.");

			ImGui::Separator();
			ImGui::Text("Brush");
			ImGui::SliderFloat("Width (cells)", &brushW, 0.5f, 24.0f);
			ImGui::SliderFloat("Height (cells)", &brushH, 0.5f, 24.0f);
			if (ImGui::Button("Square")) brushH = brushW;
			ImGui::SameLine();
			if (ImGui::Button("Swap W/H")) std::swap(brushW, brushH);
			ImGui::SameLine();
			ImGui::RadioButton("Ellipse", &brushShape, 0); ImGui::SameLine(); ImGui::RadioButton("Rectangle", &brushShape, 1);
			float op = brushOpacity * 100, hd = brushHardness * 100;
			if (ImGui::SliderFloat("Opacity %", &op, 1, 100)) brushOpacity = op / 100;
			if (ImGui::SliderFloat("Hardness %", &hd, 0, 95)) brushHardness = hd / 100;
			ImGui::Checkbox("Snap brush to cells", &snapBrush);
			ImGui::Checkbox("Show grid", &gridShow);
			if (gridShow || gridSnap)
			{
				ImGui::SetNextItemWidth(90);
				ImGui::InputFloat("Grid size", &gridStep, 1.0f, 5.0f, "%.2f");
				gridStep = std::clamp(gridStep, 0.5f, 200.0f);
				ImGui::SameLine();
				if (ImGui::SmallButton("2.5")) gridStep = 2.5f;
				ImGui::SameLine(); if (ImGui::SmallButton("5")) gridStep = 5.0f;
				ImGui::SameLine(); if (ImGui::SmallButton("10 = 1 cell")) gridStep = 10.0f;
				ImGui::SameLine(); if (ImGui::SmallButton("20")) gridStep = 20.0f;
				ImGui::SetNextItemWidth(90); ImGui::InputFloat("Offset X", &gridOffX, 0.5f, 2.5f, "%.2f");
				ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::InputFloat("Offset Z", &gridOffZ, 0.5f, 2.5f, "%.2f");
				ImGui::ColorEdit4("Grid colour", &gridColor.x, ImGuiColorEditFlags_NoInputs);
				ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::SliderFloat("Line", &gridWidth, 1.0f, 4.0f, "%.1f");
				ImGui::Checkbox("Brush snaps to this grid", &gridSnap);
			}
			ImGui::SameLine(); ImGui::Checkbox("Erase mode", &eraseMode);

			ImGui::Separator();
			ImGui::Text("Layers (top of the list is painted first)");
			for (int i = 0; i < (int)P.layers.size(); i++)
			{
				ImGui::PushID(i);
				thumb(P.layers[i].tex, 32);
				ImGui::SameLine();
				if (ImGui::Selectable(P.layers[i].tex.c_str(), P.selLayer == i, 0, ImVec2(0, 32))) P.selLayer = i;
				ImGui::PopID();
			}
			if (ImGui::Button("Add layer...")) be->windowData.layerPickerVisible = true;
			bool sel = P.selLayer >= 0 && P.selLayer < (int)P.layers.size();
			if (sel)
			{
				ImGui::SameLine(); if (ImGui::Button("Up") && P.selLayer > 0) { swapLayers(P, P.selLayer, P.selLayer - 1); P.selLayer--; }
				ImGui::SameLine(); if (ImGui::Button("Down") && P.selLayer + 1 < (int)P.layers.size()) { swapLayers(P, P.selLayer, P.selLayer + 1); P.selLayer++; }
				ImGui::SameLine(); if (ImGui::Button("Remove")) { removeLayer(P, P.selLayer); sel = false; }
			}
			if (sel)
			{
				auto& L = P.layers[P.selLayer];
				bool ch = false;
				ch |= ImGui::InputFloat("Cells per repeat", &L.tiling, 0.5f, 1.0f, "%.3f");
				if (L.tiling < 0.1f) L.tiling = 0.1f;
				const char* rots[] = { "0", "90", "180", "270" };
				ch |= ImGui::Combo("Rotation", &L.rot, rots, 4);
				ch |= ImGui::Checkbox("Flip H", &L.flipH); ImGui::SameLine();
				ch |= ImGui::Checkbox("Flip V", &L.flipV);
				ch |= ImGui::InputFloat("Offset X", &L.offX, 0.05f, 0.25f, "%.3f");
				ch |= ImGui::InputFloat("Offset Y", &L.offY, 0.05f, 0.25f, "%.3f");
				if (ch) layerChanged(P, P.selLayer);
			}

			ImGui::Separator();
			ImGui::Text("Baked textures");
			ImGui::InputText("Folder (data\\texture\\...)", &P.folder);
			const char* cp[] = { "Low (14 px per cell)", "Normal (30 px per cell)", "High (62 px per cell)" };
			int ci = P.cellPx == 14 ? 0 : P.cellPx == 62 ? 2 : 1;
			if (ImGui::Combo("Resolution", &ci, cp, 3)) P.cellPx = ci == 0 ? 14 : ci == 2 ? 62 : 30;
			const char* as[] = { "256 x 256", "512 x 512" };
			int ai = P.atlasSize == 256 ? 0 : 1;
			if (ImGui::Combo("Atlas size", &ai, as, 2)) P.atlasSize = ai == 0 ? 256 : 512;
			ImGui::Checkbox("Build automatically when painting stops (writes the files)", &P.autoBake);
			ImGui::Checkbox("Square atlas layout (atlas image = a block of the map)", &P.blockLayout);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("New atlases hold one square block of map cells at their map positions, so the image looks like a piece of the map.\nA thin cliff across many blocks makes more, emptier files. Off = cells are packed in order (fewest files).\nRebuild all re-lays out existing atlases.");
			ImGui::TextDisabled("Changing folder or resolution takes effect with 'Rebuild all'.");

			int dirtyCells = 0, painted = 0;
			for (auto& kv : P.cells) { if (kv.second.dirty) dirtyCells++; if (!isEmptyCell(kv.second)) painted++; }
			int liveAtlases = 0; for (auto& a : P.atlases) if (!a.retired) liveAtlases++;
			ImGui::Text("%d painted cells, %d draft (not built), %d texture file(s)", painted, dirtyCells, liveAtlases);

			if (ImGui::Button("Build textures")) { std::string msg; bake(map, be, P, false, msg); statusLine = msg; }
			ImGui::SameLine();
			if (ImGui::Button("Rebuild all")) ImGui::OpenPopup("Rebuild?");
			if (ImGui::BeginPopup("Rebuild?"))
			{
				ImGui::Text("Re-create every texture with the current folder and resolution?");
				ImGui::Text("The old files are kept until you clean them up.");
				if (ImGui::Button("Rebuild")) { std::string msg; bake(map, be, P, true, msg); statusLine = msg; ImGui::CloseCurrentPopup(); }
				ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			auto unused = unusedAtlases(map, P);
			if (!unused.empty())
				if (ImGui::Button(("Clean up unused files (" + std::to_string(unused.size()) + ")").c_str())) ImGui::OpenPopup("Clean?");
			if (ImGui::BeginPopup("Clean?"))
			{
				ImGui::Text("These generated textures are not used by any tile and will be deleted:");
				for (auto& f : unused) ImGui::BulletText("%s", f.c_str());
				if (ImGui::Button("Delete")) { int n = deleteUnused(map, be, P); statusLine = "Deleted " + std::to_string(n) + " unused file(s)"; ImGui::CloseCurrentPopup(); }
				ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
				ImGui::EndPopup();
			}
			if (!statusLine.empty()) ImGui::TextWrapped("%s", statusLine.c_str());
		}
		ImGui::End();
		showLayerPicker(be);
	}
}
