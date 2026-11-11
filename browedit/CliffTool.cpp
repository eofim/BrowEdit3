#include <glad/gl.h>
#include "CliffTool.h"
#include <browedit/BrowEdit.h>
#include <browedit/Map.h>
#include <browedit/MapView.h>
#include <browedit/Node.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/GndRenderer.h>
#include <browedit/components/Gat.h>
#include <browedit/actions/GatTileChangeAction.h>
#include <browedit/actions/GroupAction.h>
#include <browedit/actions/CubeHeightChangeAction.h>
#include <browedit/actions/CubeTileChangeAction.h>
#include <browedit/actions/TileNewAction.h>
#include <browedit/gl/Texture.h>
#include <browedit/advtex/AdvTex.h>
#include <json.hpp>
#include <fstream>
#include <filesystem>
#include <unordered_map>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <set>

namespace cliff
{
	Settings& settings() { static Settings s; return s; }

	void onTexturePicked(const std::string& file)
	{
		auto& s = settings();
		if (!s.active || file.empty()) return;
		auto add = [&](std::vector<std::string>& v) { if (std::find(v.begin(), v.end(), file) == v.end()) v.push_back(file); };
		if (s.profile == 0) { add(s.slopeTextures); if (s.ribbon) s.ribbonTex = file; }
		else add(s.wallTextures);
	}

	namespace
	{
		float hash2(int x, int y, int seed)
		{
			unsigned int h = (unsigned int)(x * 374761393 + y * 668265263 + seed * 2147483647u);
			h = (h ^ (h >> 13)) * 1274126177u;
			h ^= h >> 16;
			return (h & 0xFFFFFF) / (float)0xFFFFFF;
		}
		float vnoise(float x, float y, int seed)
		{
			int xi = (int)std::floor(x), yi = (int)std::floor(y);
			float fx = x - xi, fy = y - yi;
			fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
			float a = hash2(xi, yi, seed), b = hash2(xi + 1, yi, seed), c = hash2(xi, yi + 1, seed), d = hash2(xi + 1, yi + 1, seed);
			return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
		}
		float fbm(float x, float y, int seed)
		{
			return 0.65f * vnoise(x, y, seed) + 0.35f * vnoise(x * 2.3f + 17.0f, y * 2.3f + 5.0f, seed + 7);
		}

		// stroke state
		bool stroking = false;
		std::map<Gnd::Cube*, float[4]> origHeights;
		std::map<Gnd::Cube*, float[4]> weights;
		std::vector<glm::ivec2> touched;
		glm::vec2 lastCentre(-1000.0f);
		int strokeSeed = 1;
		std::vector<glm::vec2> centres;

		// corner i of cube (x,y) in tile units (x to the right, z the same axis as the world z / 10)
		glm::vec2 cornerPos(Gnd* gnd, int x, int y, int i)
		{
			return glm::vec2((float)x + (i % 2), (float)(gnd->height - y) + (i / 2 == 0 ? 1.0f : 0.0f));
		}

		float terrain(Gnd* gnd, float tx, float tz)		// world y at tile coordinates
		{
			int x = std::clamp((int)std::floor(tx), 0, gnd->width - 1);
			int y = std::clamp(gnd->height - 1 - (int)std::floor(tz), 0, gnd->height - 1);
			float fx = std::clamp(tx - x, 0.0f, 1.0f), fz = std::clamp(tz - (gnd->height - 1 - y), 0.0f, 1.0f);
			auto c = gnd->cubes[x][y];
			float top = c->h3 + (c->h4 - c->h3) * fx, bottom = c->h1 + (c->h2 - c->h1) * fx;
			return -(top + (bottom - top) * fz);
		}

		float levelWeight(const Settings& s, float d)		// d = noisy distance, returns 0..1
		{
			float w;
			if (s.profile == 1)
			{
				if (d >= s.radius) return 0.0f;
				int n = std::max(1, s.terraces);
				int level = std::min(n, 1 + (int)std::floor((1.0f - d / s.radius) * n * 0.999f));
				return level / (float)n;
			}
			w = std::clamp((s.radius - d) / std::max(0.01f, s.edgeWidth), 0.0f, 1.0f);
			w = w * w * (3 - 2 * w);
			if (s.terraces > 1 && w > 0.001f)
				w = std::ceil(w * s.terraces - 0.0001f) / s.terraces;
			return w;
		}

		float noisyDistance(const Settings& s, const glm::vec2& p, const glm::vec2& c)
		{
			float d = glm::length(p - c);
			float n = fbm(p.x * s.noiseScale, p.y * s.noiseScale, strokeSeed) - 0.5f;
			return d + n * 2.0f * s.roughness * s.radius * 0.5f;
		}

		void stamp(Gnd* gnd, GndRenderer* gr, const glm::vec2& c)
		{
			auto& s = settings();
			float reach = s.radius * (1.0f + s.roughness * 0.5f) + 2.0f;
			int x0 = std::max(0, (int)std::floor(c.x - reach)), x1 = std::min(gnd->width - 1, (int)std::ceil(c.x + reach));
			int y0 = std::max(0, (int)std::floor(gnd->height - c.y - reach)), y1 = std::min(gnd->height - 1, (int)std::ceil(gnd->height - c.y + reach));
			for (int x = x0; x <= x1; x++)
				for (int y = y0; y <= y1; y++)
				{
					auto cube = gnd->cubes[x][y];
					float w[4];
					bool any = false;
					if (s.profile == 1)
					{
						glm::vec2 centre((float)x + 0.5f, (float)(gnd->height - y) + 0.5f);
						float ww = levelWeight(s, noisyDistance(s, centre, c));
						for (int i = 0; i < 4; i++) w[i] = ww;
						any = ww > 0;
					}
					else
						for (int i = 0; i < 4; i++)
						{
							w[i] = levelWeight(s, noisyDistance(s, cornerPos(gnd, x, y, i), c));
							any |= w[i] > 0;
						}
					if (!any) continue;
					if (origHeights.find(cube) == origHeights.end())
					{
						for (int i = 0; i < 4; i++) { origHeights[cube][i] = cube->heights[i]; weights[cube][i] = 0.0f; }
						touched.push_back(glm::ivec2(x, y));
					}
					for (int i = 0; i < 4; i++)
					{
						weights[cube][i] = std::max(weights[cube][i], w[i]);
						float o = origHeights[cube][i], ww = weights[cube][i];
						if (s.mode == 0) cube->heights[i] = o - s.height * ww;
						else if (s.mode == 1) cube->heights[i] = o + s.height * ww;
						else cube->heights[i] = o + (s.height - o) * ww;
					}
					cube->calcNormal();
					gr->setChunkDirty(x, y);
				}
		}

		int textureIndexFor(Gnd* gnd, const std::string& file)
		{
			for (int i = 0; i < (int)gnd->textures.size(); i++)
				if (gnd->textures[i]->file == file) return i;
			return -1;
		}



		float bandWidth(const Settings& st) { return std::max(0.5f, st.ribbonBandW > 0.0f ? st.ribbonBandW : st.edgeWidth); }
		float effPeriod(const Settings& st)		// tiles per texture repeat along the cliff
		{
			if (!st.ribbonAuto) return std::max(0.5f, st.ribbonPeriod);
			float bw = bandWidth(st), h = std::fabs(st.height);
			float face = std::sqrt((bw * 10.0f) * (bw * 10.0f) + h * h);			// length of the face seen from the side, world units
			float span = std::max(0.2f, st.ribbonBottom - st.ribbonTop);
			return std::max(0.5f, face / span / 10.0f);
		}
		struct Poly { std::vector<glm::vec2> p; std::vector<float> s; bool closed = false; float L = 0, k = 0, period = 0; };


		// what earlier cave strokes left behind, per grid point of the ground: distance outside the floor, the ground level before the cave, and the floor level
		struct CaveEnt { float d = 100.0f, base = 0.0f, level = 0.0f; };
		struct CaveStore { std::unordered_map<int, CaveEnt> pts; std::unordered_map<int, CaveEnt> cor; bool loaded = false; bool dirty = false; int w = 0; std::string path; };
		std::map<Map*, CaveStore>& caveStores() { static std::map<Map*, CaveStore> m; return m; }
		std::map<Map*, CaveStore>& cliffStores() { static std::map<Map*, CaveStore> m; return m; }
		CaveStore* curStore = nullptr;
		std::string cavePath(Map* map, BrowEdit* be, bool cave)
		{
			std::string n = map->name;
			std::string path = n.find(":") != std::string::npos ? n : be->config.ropath + n;
			if (path.size() > 4 && path.substr(path.size() - 4) == ".rsw") path = path.substr(0, path.size() - 4);
			return path + (cave ? ".browcave.json" : ".browcliff.json");
		}
		CaveStore& caveStore(Map* map, BrowEdit* be, Gnd* gnd, bool cave = true)
		{
			auto& st = (cave ? caveStores() : cliffStores())[map];
			st.w = gnd->width;
			if (!st.loaded)
			{
				st.loaded = true;
				st.path = cavePath(map, be, cave);
				std::ifstream i(st.path, std::ios::binary);
				if (i)
				{
					try
					{
						nlohmann::json j; i >> j;
						for (auto& e : j["pts"]) { CaveEnt ce; ce.d = e[1].get<float>(); ce.base = e[2].get<float>(); ce.level = e[3].get<float>(); st.pts[e[0].get<int>()] = ce; }
						if (j.contains("cor")) for (auto& e : j["cor"]) { CaveEnt ce; ce.d = e[1].get<float>(); ce.base = e[2].get<float>(); ce.level = e[3].get<float>(); st.cor[e[0].get<int>()] = ce; }
					}
					catch (std::exception&) {}
				}
			}
			return st;
		}
		int latKey(int gx, int gz, int w) { return gz * (w + 1) + gx; }
		float oldCaveDist(const glm::vec2& p)
		{
			if (!curStore || curStore->pts.empty()) return 100.0f;
			int gx = (int)std::floor(p.x), gz = (int)std::floor(p.y);
			float fx = p.x - gx, fz = p.y - gz;
			auto at = [&](int x, int z) { auto it = curStore->pts.find(latKey(x, z, curStore->w)); return it == curStore->pts.end() ? 100.0f : it->second.d; };
			float a = at(gx, gz) * (1 - fx) + at(gx + 1, gz) * fx, b = at(gx, gz + 1) * (1 - fx) + at(gx + 1, gz + 1) * fx;
			return a * (1 - fz) + b * fz;
		}
		float newField(const Settings& st, const glm::vec2& p)
		{
			if (centres.empty()) return -1e9f;
			// the roughness noise only depends on the point, so the nearest centre decides (and the noise is evaluated once)
			float best = 1e30f;
			for (auto& c : centres) { glm::vec2 q = p - c; best = std::min(best, q.x * q.x + q.y * q.y); }
			float n = fbm(p.x * st.noiseScale, p.y * st.noiseScale, strokeSeed) - 0.5f;
			return st.radius - (std::sqrt(best) + n * 2.0f * st.roughness * st.radius * 0.5f);
		}

		float fieldAt(const Settings& st, const glm::vec2& p)
		{
			float m = newField(st, p);
			if (curStore && !(!st.cave && st.stackLevel)) m = std::max(m, -oldCaveDist(p));		// what earlier strokes made stays part of the shape
			return m;
		}

		// contours of the brush field (marching squares), smoothed and resampled
		std::vector<Poly> contours(const Settings& st, float tx0, float tz0, float tx1, float tz1)
		{
			const int R = 3;
			int nx = (int)std::ceil((tx1 - tx0) * R) + 1, nz = (int)std::ceil((tz1 - tz0) * R) + 1;
			std::vector<float> F((size_t)nx * nz);
			auto at = [&](int i, int j) -> float& { return F[(size_t)i * nz + j]; };
			for (int i = 0; i < nx; i++) for (int j = 0; j < nz; j++) at(i, j) = fieldAt(st, glm::vec2(tx0 + (float)i / R, tz0 + (float)j / R));
			auto key = [&](int i, int j, int dir) -> long long { return ((long long)i * nz + j) * 2 + dir; };
			std::map<long long, glm::vec2> pos;
			std::map<long long, std::vector<long long>> adj;
			auto crossing = [&](int i, int j, int dir) -> long long
			{
				long long k = key(i, j, dir);
				if (!pos.count(k))
				{
					int i2 = dir == 0 ? i + 1 : i, j2 = dir == 0 ? j : j + 1;
					float a = at(i, j), b = at(i2, j2), t = (a == b) ? 0.5f : a / (a - b);
					pos[k] = glm::vec2(tx0 + ((float)i + (i2 - i) * t) / R, tz0 + ((float)j + (j2 - j) * t) / R);
				}
				return k;
			};
			for (int i = 0; i + 1 < nx; i++)
				for (int j = 0; j + 1 < nz; j++)
				{
					int cs = (at(i, j) > 0 ? 1 : 0) | (at(i + 1, j) > 0 ? 2 : 0) | (at(i + 1, j + 1) > 0 ? 4 : 0) | (at(i, j + 1) > 0 ? 8 : 0);
					if (cs == 0 || cs == 15) continue;
					long long e[4] = { crossing(i, j, 0), crossing(i + 1, j, 1), crossing(i, j + 1, 0), crossing(i, j, 1) };	// bottom, right, top, left
					static const int tab[16][4] = { {-1,-1,-1,-1}, {0,3,-1,-1}, {0,1,-1,-1}, {3,1,-1,-1}, {1,2,-1,-1}, {0,3,1,2}, {0,2,-1,-1}, {3,2,-1,-1},
													{2,3,-1,-1}, {0,2,-1,-1}, {0,1,2,3}, {1,2,-1,-1}, {1,3,-1,-1}, {0,1,-1,-1}, {0,3,-1,-1}, {-1,-1,-1,-1} };
					for (int q = 0; q < 4; q += 2)
						if (tab[cs][q] >= 0) { long long a = e[tab[cs][q]], b = e[tab[cs][q + 1]]; adj[a].push_back(b); adj[b].push_back(a); }
				}
			std::vector<Poly> out;
			std::set<long long> seen;
			auto trace = [&](long long start, bool openStart) {
				Poly pl; long long prev = -1, cur = start;
				while (true)
				{
					seen.insert(cur); pl.p.push_back(pos[cur]);
					long long nxt = -1;
					for (auto n : adj[cur]) if (n != prev && !seen.count(n)) { nxt = n; break; }
					if (nxt < 0)
					{
						for (auto n : adj[cur]) if (n == start && n != prev && pl.p.size() > 2) pl.closed = true;
						break;
					}
					prev = cur; cur = nxt;
				}
				return pl;
			};
			for (auto& kv : adj) if (kv.second.size() == 1 && !seen.count(kv.first)) { auto pl = trace(kv.first, true); if (pl.p.size() > 3) out.push_back(pl); }
			for (auto& kv : adj) if (!seen.count(kv.first)) { auto pl = trace(kv.first, false); if (pl.p.size() > 5) out.push_back(pl); }
			for (auto& pl : out)
			{
				int n = (int)pl.p.size();
				for (int pass = 0; pass < 3; pass++)		// smooth
				{
					auto q = pl.p;
					for (int i = 0; i < n; i++)
					{
						if (!pl.closed && (i == 0 || i == n - 1)) continue;
						q[i] = (pl.p[(i + n - 1) % n] + pl.p[i] * 2.0f + pl.p[(i + 1) % n]) * 0.25f;
					}
					pl.p = q;
				}
				// arc length
				pl.s.assign(n, 0.0f);
				for (int i = 1; i < n; i++) pl.s[i] = pl.s[i - 1] + glm::length(pl.p[i] - pl.p[i - 1]);
				pl.L = pl.s[n - 1] + (pl.closed ? glm::length(pl.p[0] - pl.p[n - 1]) : 0.0f);
				float per = effPeriod(st);
				if (pl.closed) { int periods = std::max(1, (int)std::lround(pl.L / per)); pl.k = periods / pl.L; pl.period = (float)periods; }
				else pl.k = 1.0f / per;
			}
			return out;
		}

		// cliff face as a ribbon: continuous UVs from distance across / along the cliff curve
		void ribbonTiles(Gnd* gnd, GroupAction* ga, int& nextId, int tex, const std::vector<glm::ivec2>& tiles)
		{
			auto& st = settings();
			if (centres.empty() || tiles.empty()) return;
			float tx0 = 1e9f, tz0 = 1e9f, tx1 = -1e9f, tz1 = -1e9f;
			for (auto& t : tiles) for (int i = 0; i < 4; i++)
			{
				glm::vec2 c = cornerPos(gnd, t.x, t.y, i);
				tx0 = std::min(tx0, c.x); tx1 = std::max(tx1, c.x); tz0 = std::min(tz0, c.y); tz1 = std::max(tz1, c.y);
			}
			tx0 -= 2; tz0 -= 2; tx1 += 2; tz1 += 2;
			auto polys = contours(st, tx0, tz0, tx1, tz1);
			if (polys.empty()) return;
			// is the inside of the brush the high side?
			float meanH = 0; int cnt = 0;
			for (auto& kv : origHeights) for (int i = 0; i < 4; i++) { meanH += kv.second[i]; cnt++; }
			meanH = cnt ? meanH / cnt : 0.0f;
			bool insideHigh = st.mode == 0 || (st.mode == 2 && st.height < meanH);
			float w = bandWidth(st);
			float span = st.ribbonBottom - st.ribbonTop, vmid = (st.ribbonTop + st.ribbonBottom) * 0.5f;
			struct Hit { int ci; float d, u; int bi; };
			auto eval = [&](const glm::vec2& p, int forceCi) -> Hit
			{
				Hit h{ -1, 0, 0, 0 }; float best = 1e9f; int bi = 0;
				for (int ci = 0; ci < (int)polys.size(); ci++)
				{
					if (forceCi >= 0 && ci != forceCi) continue;
					for (int i = 0; i < (int)polys[ci].p.size(); i++)
					{
						float d2 = glm::dot(polys[ci].p[i] - p, polys[ci].p[i] - p);
						if (d2 < best) { best = d2; h.ci = ci; bi = i; }
					}
				}
				if (h.ci < 0) return h;
				float sgn = fieldAt(st, p) >= 0 ? 1.0f : -1.0f;
				float d = std::sqrt(best) * sgn;
				d -= w * 0.5f;						// the contour is the foot of the slope; the band is centred on the slope
				h.d = insideHigh ? d : -d;
				h.u = polys[h.ci].k * polys[h.ci].s[bi]; h.bi = bi;
				return h;
			};
			for (auto& t : tiles)
			{
				auto c = gnd->cubes[t.x][t.y];
				if (c->tileUp < 0) continue;
				// one planar mapping per tile: u runs along the cliff curve at the tile centre (tangent + arc length), v is the distance across.
				// A single affine map can never fold or smear within a tile, unlike picking the nearest point on the curve for every corner.
				glm::vec2 centre = (cornerPos(gnd, t.x, t.y, 0) + cornerPos(gnd, t.x, t.y, 3)) * 0.5f;
				Hit hc = eval(centre, -1);
				if (hc.ci < 0) continue;
				auto& pl = polys[hc.ci];
				int n = (int)pl.p.size();
				int bi = hc.bi;
				glm::vec2 tg = pl.p[std::min(n - 1, bi + 2)] - pl.p[std::max(0, bi - 2)];
				if (pl.closed) tg = pl.p[(bi + 2) % n] - pl.p[(bi + n - 2) % n];
				if (glm::length(tg) < 1e-5f) continue;
				tg = glm::normalize(tg);
				float u[4], v[4], vmn = 9, vmx = -9;
				for (int i = 0; i < 4; i++)
				{
					glm::vec2 cp = cornerPos(gnd, t.x, t.y, i);
					Hit h = eval(cp, hc.ci);
					u[i] = pl.k * (pl.s[bi] + glm::dot(cp - centre, tg)) * (st.ribbonFlip ? -1.0f : 1.0f);
					// across the slope the band maps linearly; below the foot the texture's lower fringe is spread over a little ground so the rock blends into the grass there
					if (h.d >= -w * 0.5f) v[i] = vmid - span * h.d / w;
					else v[i] = st.ribbonBottom + (1.0f - st.ribbonBottom) * std::min(1.0f, (-w * 0.5f - h.d) / std::max(0.2f, st.ribbonFoot));
					vmn = std::min(vmn, v[i]); vmx = std::max(vmx, v[i]);
				}
				if (vmx < st.ribbonTop + 0.12f || vmn > 0.995f) continue;			// no rock in this tile
				float umn = std::min(std::min(u[0], u[1]), std::min(u[2], u[3]));
				// tiles lying mostly beyond the rock band would only smear the edge pixels: leave them as they were
				{
					int over = 0, under = 0;
					for (int i = 0; i < 4; i++) { over += v[i] > 1.12f; under += v[i] < -0.12f; }
					if (over >= 1 || under >= 1) continue;
					if (vmx < st.ribbonTop + 0.04f) continue;
				}
				float shift = std::floor(umn);
				for (int i = 0; i < 4; i++) { u[i] -= shift; v[i] = std::clamp(v[i], 0.0f, 1.0f); }
				auto nt = new Gnd::Tile(*gnd->tiles[c->tileUp]);
				nt->textureIndex = (short)tex;
				nt->v1 = glm::vec2(u[0], v[0]); nt->v2 = glm::vec2(u[1], v[1]); nt->v3 = glm::vec2(u[2], v[2]); nt->v4 = glm::vec2(u[3], v[3]);
				ga->addAction(new CubeTileChangeAction(t, c, nextId, c->tileFront, c->tileSide));
				c->tileUp = nextId++;
				ga->addAction(new TileNewAction(nt));
			}
		}


		// per-pixel blended ribbon through Advanced Texture: each cell gets an alpha mask (rock where the band is, ground elsewhere) and four corner UVs
		void ribbonBake(Map* map, BrowEdit* browEdit, Gnd* gnd, GroupAction* ga, const std::vector<glm::ivec2>& tiles)
		{
			auto& st = settings();
			if (centres.empty() || tiles.empty()) return;
			float tx0 = 1e9f, tz0 = 1e9f, tx1 = -1e9f, tz1 = -1e9f;
			for (auto& t : tiles) for (int i = 0; i < 4; i++)
			{
				glm::vec2 c = cornerPos(gnd, t.x, t.y, i);
				tx0 = std::min(tx0, c.x); tx1 = std::max(tx1, c.x); tz0 = std::min(tz0, c.y); tz1 = std::max(tz1, c.y);
			}
			tx0 -= 2; tz0 -= 2; tx1 += 2; tz1 += 2;
			auto polys = contours(st, tx0, tz0, tx1, tz1);
			if (polys.empty()) return;
			float meanH = 0; int cnt = 0;
			for (auto& kv : origHeights) for (int i = 0; i < 4; i++) { meanH += kv.second[i]; cnt++; }
			meanH = cnt ? meanH / cnt : 0.0f;
			bool insideHigh = st.mode == 0 || (st.mode == 2 && st.height < meanH);
			float w = bandWidth(st);
			float span = st.ribbonBottom - st.ribbonTop, vmid = (st.ribbonTop + st.ribbonBottom) * 0.5f;
			const float sgnHigh = insideHigh ? 1.0f : -1.0f;
			auto dOf = [&](const glm::vec2& p) { return sgnHigh * (fieldAt(st, p) - w * 0.5f); };
			auto ss = [](float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); };
			std::vector<advtex::RibbonCell> out;
			const int M = advtex::MASK;
			for (auto& t : tiles)
			{
				auto c = gnd->cubes[t.x][t.y];
				if (c->tileUp < 0) continue;
				glm::vec2 centre = (cornerPos(gnd, t.x, t.y, 0) + cornerPos(gnd, t.x, t.y, 3)) * 0.5f;
				// cheap cull: tile far from the band
				float dc = dOf(centre);
				const bool keepOld = !st.cave && st.stackLevel;
				auto clearCell = [&]() { if (keepOld) return; advtex::RibbonCell z; z.x = t.x; z.y = t.y; z.alpha.assign((size_t)M * M, 0); out.push_back(z); };		// drops an older ribbon that no longer belongs here
				if (std::fabs(dc) > w * 0.5f + span * 0 + 2.5f * (1.0f + 0.0f)) { clearCell(); continue; }
				// nearest contour point at the centre: tangent and arc length for u
				int bc = -1, bi = 0; float best = 1e9f;
				for (int ci = 0; ci < (int)polys.size(); ci++)
					for (int i = 0; i < (int)polys[ci].p.size(); i++)
					{
						float d2 = glm::dot(polys[ci].p[i] - centre, polys[ci].p[i] - centre);
						if (d2 < best) { best = d2; bc = ci; bi = i; }
					}
				if (bc < 0) { clearCell(); continue; }
				auto& pl = polys[bc];
				int n = (int)pl.p.size();
				glm::vec2 tg = pl.p[std::min(n - 1, bi + 2)] - pl.p[std::max(0, bi - 2)];
				if (pl.closed) tg = pl.p[(bi + 2) % n] - pl.p[(bi + n - 2) % n];
				if (glm::length(tg) < 1e-5f) continue;
				tg = glm::normalize(tg);
				advtex::RibbonCell rc; rc.x = t.x; rc.y = t.y;
				float u[4], umn = 1e9f;
				for (int i = 0; i < 4; i++)
				{
					glm::vec2 cp = cornerPos(gnd, t.x, t.y, i);
					u[i] = pl.k * (pl.s[bi] + glm::dot(cp - centre, tg)) * (st.ribbonFlip ? -1.0f : 1.0f);
					umn = std::min(umn, u[i]);
				}
				float shift = std::floor(umn);
				for (int i = 0; i < 4; i++)
				{
					glm::vec2 cp = cornerPos(gnd, t.x, t.y, i);
					rc.uv[2 * i] = u[i] - shift;
					rc.uv[2 * i + 1] = vmid - span * dOf(cp) / w;
				}
				rc.alpha.assign((size_t)M * M, 0);
				bool any = false;
				glm::vec2 origin(cornerPos(gnd, t.x, t.y, 2));		// (x, z) corner of the cell
				for (int j = 0; j < M; j++)
					for (int i = 0; i < M; i++)
					{
						glm::vec2 p = origin + glm::vec2((i + 0.5f) / M, (j + 0.5f) / M);
						float v = vmid - span * dOf(p) / w;
						v += (fbm(p.x * 3.3f, p.y * 3.3f, strokeSeed + 31) - 0.5f) * 2.0f * st.ribbonNoise;
						float soft = std::max(0.01f, st.ribbonSoft);
						float a = ss(v / soft) * ss((1.0f - v) / soft);
						uint8_t av = (uint8_t)std::lround(a * 255.0f);
						rc.alpha[(size_t)j * M + i] = av;
						any |= av > 4;
					}
				if (any) out.push_back(rc); else clearCell();
			}
			advtex::project(map, browEdit).cellPx = st.ribbonDetail;
			if (auto act = advtex::makeRibbonAction(map, browEdit, st.ribbonTex, st.ribbonGround, out, "Cliff ribbon"))
				ga->addAction(act);
		}


		float ss3(float x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3 - 2 * x); }
		float caveFloor = 0.0f;

		// distance outside the carved floor (> 0 in the rock), from the noisy brush field
		float outsideDist(const Settings& st, const glm::vec2& p) { return -fieldAt(st, p); }

		float caveLimit(const Settings& st) { return st.caveSlope + (st.caveMass ? st.caveRockW + st.caveOuter : 0.0f); }

		std::map<int, std::pair<bool, CaveEnt>> caveBefore;		// grid points as they were before this stroke (for undo)

		void stampCave(Gnd* gnd, GndRenderer* gr, const glm::vec2& c)
		{
			auto& s = settings();
			float reach = s.radius * (1.0f + s.roughness * 0.5f) + caveLimit(s) + 2.0f;
			int x0 = std::max(0, (int)std::floor(c.x - reach)), x1 = std::min(gnd->width - 1, (int)std::ceil(c.x + reach));
			int y0 = std::max(0, (int)std::floor(gnd->height - c.y - reach)), y1 = std::min(gnd->height - 1, (int)std::ceil(gnd->height - c.y + reach));
			for (int x = x0; x <= x1; x++)
				for (int y = y0; y <= y1; y++)
				{
					auto cube = gnd->cubes[x][y];
					float d[4]; bool any = false;
					CaveEnt ent[4]; bool has[4];
					for (int i = 0; i < 4; i++)
					{
						glm::vec2 cp = cornerPos(gnd, x, y, i);
						int key = latKey((int)std::lround(cp.x), (int)std::lround(cp.y), curStore->w);
						auto it = curStore->pts.find(key);
						has[i] = it != curStore->pts.end();
						float dOld = has[i] ? it->second.d : 100.0f;
						float dNew = -(s.radius - noisyDistance(s, cp, c));		// earlier stamps are already folded into the stored field
						d[i] = std::min(dNew, dOld);
						ent[i] = has[i] ? it->second : CaveEnt();
						if (dNew < dOld) ent[i].level = caveFloor;
						else if (!has[i]) ent[i].level = caveFloor;
						any |= d[i] < caveLimit(s);
					}
					if (!any) continue;
					if (origHeights.find(cube) == origHeights.end())
					{
						for (int i = 0; i < 4; i++) origHeights[cube][i] = cube->heights[i];
						touched.push_back(glm::ivec2(x, y));
					}
					for (int i = 0; i < 4; i++)
					{
						float o = origHeights[cube][i];
						if (!has[i]) ent[i].base = o;
						ent[i].d = d[i];
						float dd = d[i];
						float wf = dd <= 0 ? 1.0f : 1.0f - ss3(dd / std::max(0.2f, s.caveSlope));
						float hRock = ent[i].base;
						if (s.caveMass)
						{
							float outer = std::max(0.2f, s.caveOuter);
							float wm = ss3(dd / std::max(0.2f, s.caveSlope)) * ss3((s.caveSlope + s.caveRockW + outer - dd) / outer);
							hRock = ent[i].base - s.caveRock * wm;
						}
						else if (has[i] == false) hRock = o;
						cube->heights[i] = hRock * (1.0f - wf) + ent[i].level * wf;
						glm::vec2 cp = cornerPos(gnd, x, y, i);
						int key = latKey((int)std::lround(cp.x), (int)std::lround(cp.y), curStore->w);
						if (!caveBefore.count(key)) caveBefore[key] = { has[i], has[i] ? curStore->pts[key] : CaveEnt() };
						if (dd < caveLimit(s) + 1.0f) curStore->pts[key] = ent[i];
					}
					cube->calcNormal();
					gr->setChunkDirty(x, y);
				}
		}

		std::map<int, std::pair<bool, CaveEnt>> corBefore;
		typedef std::map<int, std::pair<bool, CaveEnt>> EntMap;

		class CaveGridAction : public Action
		{
			EntMap before, after, corB, corA;
			bool cave;
			void apply(Map* map, BrowEdit* be, const EntMap& data, const EntMap& cdata)
			{
				auto gnd = map->rootNode->getComponent<Gnd>();
				auto& st = caveStore(map, be, gnd, cave);
				for (auto& kv : data) { if (kv.second.first) st.pts[kv.first] = kv.second.second; else st.pts.erase(kv.first); }
				for (auto& kv : cdata) { if (kv.second.first) st.cor[kv.first] = kv.second.second; else st.cor.erase(kv.first); }
				st.dirty = true;
			}
		public:
			CaveGridAction(bool cave, const EntMap& b, const EntMap& a, const EntMap& cb, const EntMap& ca) : before(b), after(a), corB(cb), corA(ca), cave(cave) {}
			void perform(Map* map, BrowEdit* be) override { apply(map, be, after, corA); }
			void undo(Map* map, BrowEdit* be) override { apply(map, be, before, corB); }
			std::string str() override { return cave ? "Cave shape" : "Cliff shape"; }
		};

		float strokeDelta = 0.0f; bool strokeDeltaSet = false;
		float weightOfF(const Settings& st, float F) { return levelWeight(st, st.radius - F); }

		// cliff stroke on top of what is already there: base (ground before any cliff) and delta per cube corner are remembered,
		// so overlapping or continued strokes merge instead of piling up
		void stampCliff(Gnd* gnd, GndRenderer* gr, const glm::vec2& c)
		{
			auto& s = settings();
			const bool stack = s.stackLevel;
			float reach = s.radius * (1.0f + s.roughness * 0.5f) + 2.0f;
			int x0 = std::max(0, (int)std::floor(c.x - reach)), x1 = std::min(gnd->width - 1, (int)std::ceil(c.x + reach));
			int y0 = std::max(0, (int)std::floor(gnd->height - c.y - reach)), y1 = std::min(gnd->height - 1, (int)std::ceil(gnd->height - c.y + reach));
			for (int x = x0; x <= x1; x++)
				for (int y = y0; y <= y1; y++)
				{
					auto cube = gnd->cubes[x][y];
					glm::vec2 centre((float)x + 0.5f, (float)(gnd->height - y) + 0.5f);
					float Fn[4], Fo[4];
					for (int i = 0; i < 4; i++) { glm::vec2 cp = cornerPos(gnd, x, y, i); Fn[i] = newField(s, cp); Fo[i] = stack ? -100.0f : -oldCaveDist(cp); }
					float Fnc = newField(s, centre), Foc = stack ? -100.0f : -oldCaveDist(centre);
					float wN[4], wO[4], wUn[4]; bool any = false;
					for (int i = 0; i < 4; i++)
					{
						float fn = s.profile == 1 ? Fnc : Fn[i], fo = s.profile == 1 ? Foc : Fo[i];
						wN[i] = weightOfF(s, fn); wO[i] = weightOfF(s, fo); wUn[i] = weightOfF(s, std::max(fn, fo));
						any |= wN[i] > 0;
					}
					if (!any) continue;
					if (origHeights.find(cube) == origHeights.end())
					{
						for (int i = 0; i < 4; i++) { origHeights[cube][i] = cube->heights[i]; weights[cube][i] = 0.0f; }
						touched.push_back(glm::ivec2(x, y));
					}
					for (int i = 0; i < 4; i++)
					{
						int ck = (y * gnd->width + x) * 4 + i;
						auto it = curStore->cor.find(ck);
						bool has = !stack && it != curStore->cor.end();
						float o = origHeights[cube][i];
						float base = has ? it->second.base : o;
						float deltaNew = s.mode == 0 ? -s.height : s.mode == 1 ? s.height : (s.height - base);
						if (s.mode < 2 && strokeDeltaSet) deltaNew = strokeDelta;
						float delta = deltaNew;
						if (has && wO[i] > 0.0f)
							delta = wN[i] > 0.0f ? (it->second.level * wO[i] + deltaNew * wN[i]) / (wO[i] + wN[i]) : it->second.level;		// one smooth height where two strokes meet
						cube->heights[i] = base + delta * wUn[i];
						weights[cube][i] = std::max(weights[cube][i], wUn[i]);
						if (stack) continue;
						if (!corBefore.count(ck)) corBefore[ck] = { has, has ? it->second : CaveEnt() };
						CaveEnt ce; ce.d = 0; ce.base = base; ce.level = delta;
						if (wN[i] > 0 || has) curStore->cor[ck] = ce;
						glm::vec2 cp = cornerPos(gnd, x, y, i);
						int key = latKey((int)std::lround(cp.x), (int)std::lround(cp.y), curStore->w);
						auto lit = curStore->pts.find(key);
						bool lhas = lit != curStore->pts.end();
						if (!caveBefore.count(key)) caveBefore[key] = { lhas, lhas ? lit->second : CaveEnt() };
						float Fl = std::max(Fn[i], Fo[i]);
						if (Fl > -3.0f) { CaveEnt le; le.d = -Fl; le.base = 0; le.level = 0; curStore->pts[key] = le; }
					}
					cube->calcNormal();
					gr->setChunkDirty(x, y);
				}
		}

		// cave textures: floor (world tiled), rock (world tiled) over the wall and the top, and the edge strip laid along the wall foot (continuous UVs)
		void caveBake(Map* map, BrowEdit* browEdit, Gnd* gnd, GroupAction* ga, const std::vector<glm::ivec2>& tiles)
		{
			auto& st = settings();
			if (centres.empty() || tiles.empty()) return;
			bool wantFloor = !st.caveFloorTex.empty(), wantRock = st.caveMass && !st.caveRockTex.empty(), wantEdge = !st.caveEdgeTex.empty();
			if (!wantFloor && !wantRock && !wantEdge) return;
			float tx0 = 1e9f, tz0 = 1e9f, tx1 = -1e9f, tz1 = -1e9f;
			for (auto& t : tiles) for (int i = 0; i < 4; i++)
			{
				glm::vec2 c = cornerPos(gnd, t.x, t.y, i);
				tx0 = std::min(tx0, c.x); tx1 = std::max(tx1, c.x); tz0 = std::min(tz0, c.y); tz1 = std::max(tz1, c.y);
			}
			tx0 -= 2; tz0 -= 2; tx1 += 2; tz1 += 2;
			std::vector<Poly> polys;
			if (wantEdge) polys = contours(st, tx0, tz0, tx1, tz1);
			std::vector<advtex::LayerSpec> specs(3);
			specs[0].tex = wantFloor ? st.caveFloorTex : ""; specs[0].tiling = st.caveFloorTiling;
			specs[1].tex = wantRock ? st.caveRockTex : ""; specs[1].tiling = st.caveRockTiling;
			specs[2].tex = wantEdge ? st.caveEdgeTex : ""; specs[2].ribbon = true;
			float band = std::max(0.5f, st.caveBand);
			float span = st.caveEdgeBottom - st.caveEdgeTop, vmid = (st.caveEdgeTop + st.caveEdgeBottom) * 0.5f;
			float sgn = st.caveEdgeFlip ? -1.0f : 1.0f;
			const int M = advtex::MASK;
			float limit = caveLimit(st);
			// the field is smooth, so it is sampled once on a grid instead of for every pixel
			const int GR = 8;
			const int gnx = (int)std::ceil((tx1 - tx0) * GR) + 2, gnz = (int)std::ceil((tz1 - tz0) * GR) + 2;
			std::vector<float> grid((size_t)gnx * gnz);
			for (int i = 0; i < gnx; i++) for (int j = 0; j < gnz; j++) grid[(size_t)i * gnz + j] = -fieldAt(st, glm::vec2(tx0 + (float)i / GR, tz0 + (float)j / GR));
			auto outsideDist = [&](const Settings&, const glm::vec2& p) -> float
			{
				float fx = (p.x - tx0) * GR, fz = (p.y - tz0) * GR;
				int i = std::clamp((int)std::floor(fx), 0, gnx - 2), j = std::clamp((int)std::floor(fz), 0, gnz - 2);
				float a = std::clamp(fx - i, 0.0f, 1.0f), b = std::clamp(fz - j, 0.0f, 1.0f);
				auto g = [&](int ii, int jj) { return grid[(size_t)ii * gnz + jj]; };
				return (g(i, j) * (1 - a) + g(i + 1, j) * a) * (1 - b) + (g(i, j + 1) * (1 - a) + g(i + 1, j + 1) * a) * b;
			};
			std::vector<advtex::LayeredCell> out;
			for (auto& t : tiles)
			{
				auto c = gnd->cubes[t.x][t.y];
				if (c->tileUp < 0) continue;
				glm::vec2 centre = (cornerPos(gnd, t.x, t.y, 0) + cornerPos(gnd, t.x, t.y, 3)) * 0.5f;
				if (outsideDist(st, centre) > limit + 1.5f) continue;
				advtex::LayeredCell lc; lc.x = t.x; lc.y = t.y;
				lc.alpha.assign(3, std::vector<uint8_t>()); lc.uv.assign(3, std::vector<float>());
				glm::vec2 origin(cornerPos(gnd, t.x, t.y, 2));
				std::vector<uint8_t> aF((size_t)M * M, 0), aR((size_t)M * M, 0), aE((size_t)M * M, 0);
				bool anyF = false, anyR = false, anyE = false;
				// edge ribbon mapping for the tile
				float uv[8] = { 0,0,0,0,0,0,0,0 };
				bool edgeOk = false;
				auto vOf = [&](float d) { return vmid + sgn * span * (d - st.caveEdgeShift * band) / band; };
				if (wantEdge && !polys.empty())
				{
					int bc = -1, bi = 0; float best = 1e9f;
					for (int ci = 0; ci < (int)polys.size(); ci++)
						for (int i = 0; i < (int)polys[ci].p.size(); i++)
						{
							float d2 = glm::dot(polys[ci].p[i] - centre, polys[ci].p[i] - centre);
							if (d2 < best) { best = d2; bc = ci; bi = i; }
						}
					if (bc >= 0)
					{
						auto& pl = polys[bc];
						int n = (int)pl.p.size();
						glm::vec2 tg = pl.p[std::min(n - 1, bi + 2)] - pl.p[std::max(0, bi - 2)];
						if (pl.closed) tg = pl.p[(bi + 2) % n] - pl.p[(bi + n - 2) % n];
						if (glm::length(tg) > 1e-5f)
						{
							tg = glm::normalize(tg);
							float u[4], umn = 1e9f;
							for (int i = 0; i < 4; i++) { glm::vec2 cp = cornerPos(gnd, t.x, t.y, i); u[i] = pl.k * (pl.s[bi] + glm::dot(cp - centre, tg)); umn = std::min(umn, u[i]); }
							float shift = std::floor(umn);
							for (int i = 0; i < 4; i++) { uv[2 * i] = u[i] - shift; uv[2 * i + 1] = vOf(outsideDist(st, cornerPos(gnd, t.x, t.y, i))); }
							edgeOk = true;
						}
					}
				}
				for (int j = 0; j < M; j++)
					for (int i = 0; i < M; i++)
					{
						glm::vec2 p = origin + glm::vec2((i + 0.5f) / M, (j + 0.5f) / M);
						float d = outsideDist(st, p);
						size_t o = (size_t)j * M + i;
						if (wantFloor) { float a = 1.0f - ss3((d - 0.2f) / 0.8f); aF[o] = (uint8_t)std::lround(a * 255.0f); anyF |= aF[o] > 4; }
						if (wantRock)
						{
							float outer = std::max(0.2f, st.caveOuter);
							float a = ss3((d - 0.3f) / 0.8f) * (1.0f - ss3((d - (st.caveSlope + st.caveRockW + outer * 0.4f)) / (outer * 0.6f)));
							aR[o] = (uint8_t)std::lround(a * 255.0f); anyR |= aR[o] > 4;
						}
						if (edgeOk)
						{
							float v = vOf(d) + (fbm(p.x * 3.3f, p.y * 3.3f, strokeSeed + 53) - 0.5f) * 2.0f * st.ribbonNoise;
							float soft = std::max(0.01f, st.ribbonSoft);
							float a = ss3(v / soft) * ss3((1.0f - v) / soft);
							aE[o] = (uint8_t)std::lround(a * 255.0f); anyE |= aE[o] > 4;
						}
					}
				// an all-zero mask clears what an earlier stroke left in that layer of the cell (so continuing a cave never leaves stale rock/edge on the new floor)
				if (wantFloor) lc.alpha[0] = aF;
				if (wantRock) lc.alpha[1] = aR;
				if (wantEdge) { lc.alpha[2] = aE; lc.uv[2].assign(uv, uv + 8); }
				(void)anyF; (void)anyR; (void)anyE;
				out.push_back(lc);
			}
			advtex::project(map, browEdit).cellPx = st.ribbonDetail;
			if (auto act = advtex::makeLayeredAction(map, browEdit, specs, out, "Cave textures")) ga->addAction(act);
		}

		void finish(MapView* mv, BrowEdit* browEdit, Map* map, Gnd* gnd, GndRenderer* gr)
		{
			auto& s = settings();
			const int profNow = s.cave ? 99 : s.profile;
			if (origHeights.empty()) { stroking = false; return; }
			auto ga = new GroupAction("Cliff brush");
			std::map<Gnd::Cube*, float[4]> newHeights;
			for (auto& kv : origHeights)
				for (int i = 0; i < 4; i++)
					newHeights[kv.first][i] = kv.first->heights[i];
			ga->addAction(new CubeHeightChangeAction<Gnd, Gnd::Cube>(origHeights, newHeights, touched));

			if (curStore)
			{
				EntMap after, corAfter;
				for (auto& kv : caveBefore) { auto it = curStore->pts.find(kv.first); after[kv.first] = { it != curStore->pts.end(), it != curStore->pts.end() ? it->second : CaveEnt() }; }
				for (auto& kv : corBefore) { auto it = curStore->cor.find(kv.first); corAfter[kv.first] = { it != curStore->cor.end(), it != curStore->cor.end() ? it->second : CaveEnt() }; }
				ga->addAction(new CaveGridAction(s.cave, caveBefore, after, corBefore, corAfter));
			}
			std::mt19937 rng((unsigned)strokeSeed * 7919u + (unsigned)touched.size());
			auto pick = [&](const std::vector<std::string>& set) -> int {
				std::vector<int> idx;
				for (auto& f : set) { int t = textureIndexFor(gnd, f); if (t >= 0) idx.push_back(t); }
				if (idx.empty()) return -1;
				return idx[rng() % idx.size()];
			};
			int nextId = (int)gnd->tiles.size();

			std::set<std::pair<int, int>> region;
			for (auto& t : touched)
				for (int dx = -1; dx <= 1; dx++)
					for (int dy = -1; dy <= 1; dy++)
					{
						int x = t.x + dx, y = t.y + dy;
						if (x >= 0 && y >= 0 && x < gnd->width && y < gnd->height) region.insert({ x, y });
					}

			// cliff faces
			if (profNow == 1 && s.buildWalls && !s.wallTextures.empty() && !gnd->lightmaps.empty())
			{
				auto makeWall = [&](int x, int y, int slot)
				{
					auto cube = gnd->cubes[x][y];
					int tex = pick(s.wallTextures);
					if (tex < 0) return;
					auto t = new Gnd::Tile();
					t->color = glm::ivec4(255, 255, 255, 255);
					t->textureIndex = (short)tex;
					t->lightmapIndex = 0;
					float u0 = 0.0f, u1 = 1.0f;
					if (s.randomSlice) { u0 = (rng() % 4) * 0.25f; u1 = u0 + 0.25f; }
					if (rng() % 2) std::swap(u0, u1);
					t->v1 = glm::vec2(u0, 1); t->v2 = glm::vec2(u1, 1); t->v3 = glm::vec2(u0, 0); t->v4 = glm::vec2(u1, 0);
					if (slot == 1) ga->addAction(new CubeTileChangeAction(glm::ivec2(x, y), cube, cube->tileUp, nextId, cube->tileSide));
					else ga->addAction(new CubeTileChangeAction(glm::ivec2(x, y), cube, cube->tileUp, cube->tileFront, nextId));
					cube->tileIds[slot] = nextId++;
					ga->addAction(new TileNewAction(t));
				};
				const float eps = 0.01f;
				for (auto& r : region)
				{
					int x = r.first, y = r.second;
					auto c = gnd->cubes[x][y];
					if (c->tileUp < 0) continue;
					if (x < gnd->width - 1)
					{
						auto n = gnd->cubes[x + 1][y];
						bool diff = std::fabs(c->h2 - n->h1) > eps || std::fabs(c->h4 - n->h3) > eps;
						if (diff && c->tileSide == -1 && n->tileUp >= 0) makeWall(x, y, 2);
					}
					if (y < gnd->height - 1)
					{
						auto n = gnd->cubes[x][y + 1];
						bool diff = std::fabs(c->h3 - n->h1) > eps || std::fabs(c->h4 - n->h2) > eps;
						if (diff && c->tileFront == -1 && n->tileUp >= 0) makeWall(x, y, 1);
					}
				}
			}
			// walls that are flat now
			for (auto& r : region)
			{
				int x = r.first, y = r.second;
				auto c = gnd->cubes[x][y];
				const float eps = 0.01f;
				if (x < gnd->width - 1 && c->tileSide != -1)
				{
					auto n = gnd->cubes[x + 1][y];
					if (std::fabs(c->h2 - n->h1) <= eps && std::fabs(c->h4 - n->h3) <= eps && origHeights.count(c) + origHeights.count(n))
					{
						ga->addAction(new CubeTileChangeAction(glm::ivec2(x, y), c, c->tileUp, c->tileFront, -1));
						c->tileSide = -1;
					}
				}
				if (y < gnd->height - 1 && c->tileFront != -1)
				{
					auto n = gnd->cubes[x][y + 1];
					if (std::fabs(c->h3 - n->h1) <= eps && std::fabs(c->h4 - n->h2) <= eps && origHeights.count(c) + origHeights.count(n))
					{
						ga->addAction(new CubeTileChangeAction(glm::ivec2(x, y), c, c->tileUp, -1, c->tileSide));
						c->tileFront = -1;
					}
				}
			}
			// smart edge texturing: pick straight / corner / diagonal piece per ground tile along the cliff and rotate it
			if (profNow == 1 && s.smartEdges)
			{
				int pidx[3];
				for (int k = 0; k < 3; k++) pidx[k] = s.pieceTex[k].empty() ? -1 : textureIndexFor(gnd, s.pieceTex[k]);
				if (pidx[0] >= 0 && pidx[1] >= 0 && pidx[2] >= 0)
				{
					const float eps = 0.01f;
					const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
					auto cubeAt = [&](int x, int y) -> Gnd::Cube* { return (x >= 0 && y >= 0 && x < gnd->width && y < gnd->height) ? gnd->cubes[x][y] : nullptr; };
					auto stepAcross = [&](int x, int y, int d) -> bool
					{
						auto c = gnd->cubes[x][y];
						auto n = cubeAt(x + DX[d], y + DY[d]);
						if (!n || n->tileUp < 0) return false;
						if (d == 0) return std::fabs(c->h2 - n->h1) > eps || std::fabs(c->h4 - n->h3) > eps;
						if (d == 1) return std::fabs(c->h1 - n->h2) > eps || std::fabs(c->h3 - n->h4) > eps;
						if (d == 2) return std::fabs(c->h3 - n->h1) > eps || std::fabs(c->h4 - n->h2) > eps;
						return std::fabs(c->h1 - n->h3) > eps || std::fabs(c->h2 - n->h4) > eps;
					};
					for (auto& r : region)
					{
						int x = r.first, y = r.second;
						auto c = gnd->cubes[x][y];
						if (c->tileUp < 0) continue;
						bool st[4]; int count = 0;
						for (int d = 0; d < 4; d++) { st[d] = stepAcross(x, y, d); count += st[d]; }
						int piece = -1; float targetAngle = 0;
						if (count == 0)
						{
							for (int dx = -1; dx <= 1 && piece < 0; dx += 2)
								for (int dy = -1; dy <= 1 && piece < 0; dy += 2)
								{
									auto n = cubeAt(x + dx, y + dy);
									if (!n || n->tileUp < 0) continue;
									int ci = (dy == 1 ? 2 : 0) + (dx == 1 ? 1 : 0), ni = (dy == 1 ? 0 : 2) + (dx == 1 ? 0 : 1);
									if (std::fabs(c->heights[ci] - n->heights[ni]) > eps) { piece = 1; targetAngle = std::atan2((float)dy, (float)dx); }
								}
						}
						else if (count == 2 && ((st[0] || st[1]) && (st[2] || st[3])))
						{
							int dx = st[0] ? 1 : -1, dy = st[2] ? 1 : -1;
							piece = 2; targetAngle = std::atan2((float)dy, (float)dx);
						}
						else
						{
							int d = 0; while (!st[d]) d++;
							piece = 0; targetAngle = std::atan2((float)DY[d], (float)DX[d]);
						}
						if (piece < 0) continue;
						float baseAngle = piece == 0 ? 1.5707963f : 3.9269908f;		// image: straight piece has rock up, the others rock at the lower left
						int quarter = (int)std::lround((targetAngle - baseAngle) / 1.5707963f) + s.pieceRot[piece];
						quarter = ((quarter % 4) + 4) % 4;
						auto q = [&](float u, float v) -> glm::vec2
						{
							float a = -quarter * 1.5707963f, cs = std::round(std::cos(a)), sn = std::round(std::sin(a));
							float px = u - 0.5f, py = v - 0.5f;
							glm::vec2 res(px * cs - py * sn + 0.5f, px * sn + py * cs + 0.5f);
							if (s.pieceFlip[piece]) res.x = 1.0f - res.x;
							return res;
						};
						auto nt = new Gnd::Tile(*gnd->tiles[c->tileUp]);
						nt->textureIndex = (short)pidx[piece];
						nt->v1 = q(0, 0); nt->v2 = q(1, 0); nt->v3 = q(0, 1); nt->v4 = q(1, 1);
						ga->addAction(new CubeTileChangeAction(glm::ivec2(x, y), c, nextId, c->tileFront, c->tileSide));
						c->tileUp = nextId++;
						ga->addAction(new TileNewAction(nt));
					}
				}
			}
			// ribbon texturing for sloped cliffs (continuous UVs along the cliff curve)
			bool ribbonDone = false;
			if (profNow == 0 && s.ribbon && !s.ribbonTex.empty())
			{
				int rt = textureIndexFor(gnd, s.ribbonTex);
				if (rt >= 0 && s.ribbonBake) { std::vector<glm::ivec2> rtiles; for (auto& r : region) rtiles.push_back(glm::ivec2(r.first, r.second)); ribbonBake(map, browEdit, gnd, ga, rtiles); ribbonDone = true; }
				else if (rt >= 0) { { std::vector<glm::ivec2> rtiles; for (auto& r : region) rtiles.push_back(glm::ivec2(r.first, r.second)); ribbonTiles(gnd, ga, nextId, rt, rtiles); } ribbonDone = true; }
			}
			// slope texture: steep ground gets rock
			if (profNow == 0 && !ribbonDone && s.texSlope && !s.slopeTextures.empty())
			{
				for (auto& t : touched)
				{
					auto c = gnd->cubes[t.x][t.y];
					if (c->tileUp < 0) continue;
					float mn = std::min(std::min(c->h1, c->h2), std::min(c->h3, c->h4)), mx = std::max(std::max(c->h1, c->h2), std::max(c->h3, c->h4));
					if (mx - mn < 2.0f) continue;
					int tex = pick(s.slopeTextures);
					if (tex < 0) continue;
					auto nt = new Gnd::Tile(*gnd->tiles[c->tileUp]);
					nt->textureIndex = (short)tex;
					ga->addAction(new CubeTileChangeAction(t, c, nextId, c->tileFront, c->tileSide));
					c->tileUp = nextId++;
					ga->addAction(new TileNewAction(nt));
				}
			}


			// GAT: heights follow the new ground, steep cells and wall edges become cliff (type 5), flattened cells are released again

			// cave GAT: floor walkable, rock and walls blocked, heights follow
			if (s.cave && s.autoGat)
			{
				auto gat = map->rootNode->getComponent<Gat>();
				if (gat && gat->width == gnd->width * 2 && gat->height == gnd->height * 2)
				{
					std::map<Gat::Cube*, float[4]> oldT, newT;
					std::vector<glm::ivec2> selT;
					std::set<std::pair<int, int>> touchedSet;
					for (auto& t : touched) touchedSet.insert({ t.x, t.y });
					for (auto& r : region)
					{
						auto cb = gnd->cubes[r.first][r.second];
						bool wasTouched = touchedSet.count(r) > 0;
						for (int k = 0; k < 4; k++)
						{
							int gx = r.first * 2 + (k % 2), gy = r.second * 2 + (k >> 1);
							auto gc = gat->cubes[gx][gy];
							if (wasTouched)
							{
								const float* nh = cb->heights;
								float fu0 = (k % 2) / 2.0f, fu1 = fu0 + 0.5f, fv0 = (k >> 1) / 2.0f, fv1 = fv0 + 0.5f;
								auto at = [&](float fu, float fv) { return (nh[0] * (1 - fu) + nh[1] * fu) * (1 - fv) + (nh[2] * (1 - fu) + nh[3] * fu) * fv; };
								float gh[4] = { at(fu0, fv0), at(fu1, fv0), at(fu0, fv1), at(fu1, fv1) };
								for (int i = 0; i < 4; i++) { oldT[gc][i] = gc->heights[i]; newT[gc][i] = gh[i]; }
								selT.push_back(glm::ivec2(gx, gy));
							}
							glm::vec2 gcenter((float)r.first + (k % 2) * 0.5f + 0.25f, (float)(gnd->height - r.second) + 1.0f - (k >> 1) * 0.5f - 0.25f);
							float d = outsideDist(s, gcenter);
							int want = gc->gatType;
							if (d > 0.4f) { if (gc->gatType == 0 || gc->gatType == 5) want = 1; }
							else if (gc->gatType == 5) want = 0;
							else if (gc->gatType == 1 && origHeights.count(cb))
							{
								const float* oh = origHeights[cb];
								float avg = (oh[0] + oh[1] + oh[2] + oh[3]) / 4.0f;
								if (avg < caveFloor - 5.0f) want = 0;		// it was rock before this stroke
							}
							if (want != gc->gatType) ga->addAction(new GatTileChangeAction(glm::ivec2(gx, gy), gc, want));
						}
					}
					if (!newT.empty()) ga->addAction(new CubeHeightChangeAction<Gat, Gat::Cube>(oldT, newT, selT));
				}
			}
			if (s.cave && s.caveBake)
			{
				std::vector<glm::ivec2> ctiles; for (auto& r : region) ctiles.push_back(glm::ivec2(r.first, r.second));
				caveBake(map, browEdit, gnd, ga, ctiles);
			}
			if (s.autoGat && !s.cave)
			{
				auto gat = map->rootNode->getComponent<Gat>();
				if (gat && gat->width == gnd->width * 2 && gat->height == gnd->height * 2)
				{
					std::map<Gat::Cube*, float[4]> oldT, newT;
					std::vector<glm::ivec2> selT;
					std::map<std::pair<int, int>, bool> wantCliff;
					std::set<std::pair<int, int>> touchedSet;
					for (auto& t : touched) touchedSet.insert({ t.x, t.y });
					auto gatHeights = [&](Gnd::Cube* cb, int k, float gh[4])
					{
						const float* nh = cb->heights;
						float fu0 = (k % 2) / 2.0f, fu1 = fu0 + 0.5f, fv0 = (k >> 1) / 2.0f, fv1 = fv0 + 0.5f;
						auto at = [&](float fu, float fv) { return (nh[0] * (1 - fu) + nh[1] * fu) * (1 - fv) + (nh[2] * (1 - fu) + nh[3] * fu) * fv; };
						gh[0] = at(fu0, fv0); gh[1] = at(fu1, fv0); gh[2] = at(fu0, fv1); gh[3] = at(fu1, fv1);
					};
					for (auto& r : region)
					{
						auto cb = gnd->cubes[r.first][r.second];
						for (int k = 0; k < 4; k++)
						{
							int gx = r.first * 2 + (k % 2), gy = r.second * 2 + (k >> 1);
							float gh[4]; gatHeights(cb, k, gh);
							float mn = std::min(std::min(gh[0], gh[1]), std::min(gh[2], gh[3])), mx = std::max(std::max(gh[0], gh[1]), std::max(gh[2], gh[3]));
							wantCliff[{ gx, gy }] = (mx - mn) > s.gatSlope;
							if (touchedSet.count(r))
							{
								auto gc = gat->cubes[gx][gy];
								for (int i = 0; i < 4; i++) { oldT[gc][i] = gc->heights[i]; newT[gc][i] = gh[i]; }
								selT.push_back(glm::ivec2(gx, gy));
							}
						}
					}
					// wall edges: the upper side cells are blocked
					const float eps = 0.01f;
					for (auto& r : region)
					{
						int x = r.first, y = r.second;
						auto c = gnd->cubes[x][y];
						if (x < gnd->width - 1)
						{
							auto n = gnd->cubes[x + 1][y];
							if (std::fabs(c->h2 - n->h1) > eps || std::fabs(c->h4 - n->h3) > eps)
							{
								bool cUpper = (c->h2 + c->h4) < (n->h1 + n->h3);
								int gx = cUpper ? x * 2 + 1 : (x + 1) * 2;
								for (int k = 0; k < 2; k++) wantCliff[{ gx, y * 2 + k }] = true;
							}
						}
						if (y < gnd->height - 1)
						{
							auto n = gnd->cubes[x][y + 1];
							if (std::fabs(c->h3 - n->h1) > eps || std::fabs(c->h4 - n->h2) > eps)
							{
								bool cUpper = (c->h3 + c->h4) < (n->h1 + n->h2);
								int gy = cUpper ? y * 2 + 1 : (y + 1) * 2;
								for (int k = 0; k < 2; k++) wantCliff[{ x * 2 + k, gy }] = true;
							}
						}
					}
					if (!newT.empty())
						ga->addAction(new CubeHeightChangeAction<Gat, Gat::Cube>(oldT, newT, selT));
					for (auto& kv : wantCliff)
					{
						int gx = kv.first.first, gy = kv.first.second;
						if (gx < 0 || gy < 0 || gx >= gat->width || gy >= gat->height) continue;
						auto gc = gat->cubes[gx][gy];
						int want = kv.second ? 5 : (gc->gatType == 5 ? 0 : gc->gatType);
						if (gc->gatType != want)
							ga->addAction(new GatTileChangeAction(glm::ivec2(gx, gy), gc, want));
					}
				}
			}
			for (auto& r : region)
			{
				gnd->cubes[r.first][r.second]->calcNormals(gnd, r.first, r.second);
				gr->setChunkDirty(r.first, r.second);
			}
			map->doAction(ga, browEdit);
			origHeights.clear(); weights.clear(); touched.clear(); caveBefore.clear(); corBefore.clear(); strokeDeltaSet = false;
			stroking = false;
		}
	}

	void saveCave(Map* map, BrowEdit* browEdit)
	{
		for (int kind = 0; kind < 2; kind++)
		{
			auto& all = kind == 0 ? caveStores() : cliffStores();
			auto it = all.find(map);
			if (it == all.end() || !it->second.dirty || it->second.path.empty()) continue;
			nlohmann::json j; j["version"] = 2; j["pts"] = nlohmann::json::array(); j["cor"] = nlohmann::json::array();
			for (auto& kv : it->second.pts) if (kv.second.d < 50.0f) j["pts"].push_back({ kv.first, kv.second.d, kv.second.base, kv.second.level });
			for (auto& kv : it->second.cor) j["cor"].push_back({ kv.first, kv.second.d, kv.second.base, kv.second.level });
			std::string tmp = it->second.path + ".tmp";
			{ std::ofstream o(tmp, std::ios::binary | std::ios::trunc); if (!o) continue; o << j.dump(); }
			std::error_code ec;
			std::filesystem::rename(tmp, it->second.path, ec);
			if (ec) { std::filesystem::remove(it->second.path, ec); std::filesystem::rename(tmp, it->second.path, ec); }
			it->second.dirty = false;
		}
	}
	void forgetCave(Map* map) { caveStores().erase(map); cliffStores().erase(map); }

	void update(MapView* mv, BrowEdit* browEdit, Map* map, Gnd* gnd, GndRenderer* gr, const glm::vec3& mouse3D)
	{
		auto& s = settings();
		bool valid = mouse3D.x >= 0 && mouse3D.z >= 0 && mouse3D.x <= gnd->width * 10.0f && mouse3D.z <= gnd->height * 10.0f;
		glm::vec2 c(mouse3D.x / 10.0f, mouse3D.z / 10.0f);
		auto& io = ImGui::GetIO();
		curStore = &caveStore(map, browEdit, gnd, s.cave);

		if (valid)
		{
			ImGui::Begin("Statusbar");
			ImGui::Text(settings().cave ? "Cave brush: click or drag to carve. Alt + mouse wheel = radius" : "Cliff brush: click or drag. Mouse wheel + Alt = radius, Shift flips raise/lower");
			ImGui::End();
			if (io.KeyAlt && io.MouseWheel != 0)
				s.radius = std::clamp(s.radius + io.MouseWheel * 0.5f, 0.5f, 40.0f);
		}
		if (valid && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt)
		{
			if (!stroking)
			{
				stroking = true;
				strokeSeed = s.seed;
				origHeights.clear(); weights.clear(); touched.clear();
				lastCentre = glm::vec2(-1000.0f);
				centres.clear();
				caveBefore.clear(); corBefore.clear(); strokeDeltaSet = false;
				if (!s.cave && !s.stackLevel && s.continueCliff && curStore && s.mode < 2 && -oldCaveDist(c) > 0.0f)
				{
					// starting on a cliff that already exists: keep its height
					int cx = std::clamp((int)std::floor(c.x), 0, gnd->width - 1), cy = std::clamp(gnd->height - 1 - (int)std::floor(c.y), 0, gnd->height - 1);
					for (int i = 0; i < 4 && !strokeDeltaSet; i++)
					{
						auto it = curStore->cor.find((cy * gnd->width + cx) * 4 + i);
						if (it != curStore->cor.end() && std::fabs(it->second.level) > 0.01f) { strokeDelta = it->second.level; strokeDeltaSet = true; }
					}
				}
				if (s.cave)
				{
					if (s.caveFloorAbs) caveFloor = s.caveFloorH;
					else
					{
						int cx = std::clamp((int)std::floor(c.x), 0, gnd->width - 1), cy = std::clamp(gnd->height - 1 - (int)std::floor(c.y), 0, gnd->height - 1);
						auto cb = gnd->cubes[cx][cy];
						caveFloor = (cb->h1 + cb->h2 + cb->h3 + cb->h4) * 0.25f;
						// starting on a cave floor that already exists: continue at its level
						if (curStore && oldCaveDist(c) <= 0.0f)
						{
							int gx = (int)std::lround(c.x), gz = (int)std::lround(c.y);
							for (int rr = 0; rr < 4; rr++)
							{
								auto it = curStore->pts.find(latKey(gx + (rr % 2) - 0, gz + (rr / 2) - 0, curStore->w));
								if (it != curStore->pts.end() && it->second.d <= 0.0f) { caveFloor = it->second.level; break; }
							}
						}
					}
				}
			}
			if (glm::length(c - lastCentre) >= 0.5f)
			{
				int savedMode = s.mode;
				if (io.KeyShift && s.mode < 2) s.mode = 1 - s.mode;
				if (s.cave) stampCave(gnd, gr, c); else stampCliff(gnd, gr, c);
				centres.push_back(c);
				s.mode = savedMode;
				lastCentre = c;
			}
		}
		if (stroking && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			// the shift flip was applied while stamping; the stored heights are what counts
			finish(mv, browEdit, map, gnd, gr);
		}

		if (valid)		// brush outline (noisy edge as it will be applied)
		{
			glUseProgram(0);
			glLineWidth(2.0f);
			for (int pass = 0; pass < 2; pass++)
			{
				if (pass == 0) glColor4f(1.0f, 0.55f, 0.1f, 1.0f); else glColor4f(1.0f, 1.0f, 1.0f, 0.6f);
				glBegin(GL_LINE_LOOP);
				const int N = 96;
				for (int i = 0; i < N; i++)
				{
					float a = i * 6.2831853f / N;
					glm::vec2 dir(std::cos(a), std::sin(a));
					// find the radius where the noisy distance reaches the brush radius (edge of the brush), inner = full strength
					float target = s.cave ? (pass == 0 ? s.radius : s.radius + s.caveSlope) : (pass == 0 ? s.radius : std::max(0.0f, s.radius - (s.profile == 0 ? s.edgeWidth : 0.0f)));
					float r = target;
					for (int it = 0; it < 4; it++)
					{
						glm::vec2 p = c + dir * r;
						float n = fbm(p.x * s.noiseScale, p.y * s.noiseScale, s.seed) - 0.5f;
						r = std::max(0.0f, target - n * 2.0f * s.roughness * s.radius * 0.5f);
					}
					glm::vec2 p = c + dir * r;
					glVertex3f(p.x * 10.0f, terrain(gnd, p.x, p.y) + 1.0f, p.y * 10.0f);
				}
				glEnd();
			}
			glLineWidth(1.0f);
			glColor4f(1, 1, 1, 1);
		}
	}


	void showCaveOptions(BrowEdit* browEdit, MapView* mv)
	{
		auto& s = settings();
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		ImGui::TextWrapped("Cave brush: paint a walkable floor into a rock mass. Drag for corridors, click for rooms.");
		ImGui::SliderFloat("Floor radius (tiles)", &s.radius, 0.5f, 20.0f);
		ImGui::SliderFloat("Wall slope width (tiles)", &s.caveSlope, 0.5f, 10.0f);
		ImGui::Checkbox("Raise a rock mass around the floor", &s.caveMass);
		if (s.caveMass)
		{
			ImGui::InputFloat("Rock height", &s.caveRock, 1.0f, 5.0f, "%.1f");
			ImGui::SliderFloat("Rock top thickness (tiles)", &s.caveRockW, 0.0f, 20.0f);
			ImGui::SliderFloat("Outer slope (tiles)", &s.caveOuter, 0.5f, 12.0f);
		}
		else ImGui::TextDisabled("Off: only carves into ground that is already raised.");
		ImGui::Checkbox("Fixed floor height", &s.caveFloorAbs);
		if (s.caveFloorAbs) ImGui::InputFloat("Floor height", &s.caveFloorH, 1.0f, 5.0f, "%.1f");
		else ImGui::TextDisabled("Floor level = the ground where the stroke starts.");
		ImGui::SliderFloat("Edge roughness", &s.roughness, 0.0f, 1.5f);
		ImGui::SliderFloat("Curve size", &s.noiseScale, 0.05f, 1.5f);
		ImGui::InputInt("Shape seed", &s.seed);
		ImGui::Separator();
		ImGui::Checkbox("Update the GAT (floor walkable, rock blocked)", &s.autoGat);
		ImGui::Separator();
		ImGui::Checkbox("Bake cave textures (Advanced Texture)", &s.caveBake);
		auto slot = [&](const char* title, std::string& tex)
		{
			ImGui::PushID(title);
			ImGui::Text("%s: %s", title, tex.empty() ? "(none)" : tex.c_str());
			if (ImGui::Button("Use picker texture") && gnd && mv->textureSelected >= 0 && mv->textureSelected < (int)gnd->textures.size())
				tex = gnd->textures[mv->textureSelected]->file;
			ImGui::SameLine(); if (ImGui::Button("Clear")) tex.clear();
			ImGui::PopID();
		};
		if (s.caveBake)
		{
			slot("Floor texture", s.caveFloorTex);
			ImGui::SliderFloat("Floor cells per repeat", &s.caveFloorTiling, 0.5f, 16.0f);
			slot("Wall edge texture (strip along the wall foot)", s.caveEdgeTex);
			ImGui::SliderFloat("Edge band width (tiles)", &s.caveBand, 0.5f, 8.0f);
			ImGui::SliderFloat("Edge band top (v)", &s.caveEdgeTop, 0.0f, 0.5f);
			ImGui::SliderFloat("Edge band bottom (v)", &s.caveEdgeBottom, 0.5f, 1.0f);
			ImGui::SliderFloat("Edge shift (floor <-> wall)", &s.caveEdgeShift, -1.0f, 1.0f);
			ImGui::Checkbox("Flip edge", &s.caveEdgeFlip);
			slot("Rock texture (walls + top)", s.caveRockTex);
			ImGui::SliderFloat("Rock cells per repeat", &s.caveRockTiling, 0.5f, 16.0f);
			ImGui::RadioButton("Detail 30 px/tile", &s.ribbonDetail, 30); ImGui::SameLine();
			ImGui::RadioButton("Detail 62 px/tile (sharper)", &s.ribbonDetail, 62);
			ImGui::SliderFloat("Edge softness", &s.ribbonSoft, 0.01f, 0.3f);
			ImGui::SliderFloat("Edge ragged", &s.ribbonNoise, 0.0f, 0.2f);
			ImGui::TextWrapped("The edge strip follows the wall curve continuously, so no separate corner pieces are needed. Textures are a draft preview until you press Build in Advanced Texture mode (F12) or answer yes when saving.");
		}
	}

	void showOptions(BrowEdit* browEdit, MapView* mv)
	{
		auto& s = settings();
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		ImGui::Text("Brush: raise/lower/set terrain in a circle with a natural edge.");
		ImGui::RadioButton("Raise", &s.mode, 0); ImGui::SameLine();
		ImGui::RadioButton("Lower", &s.mode, 1); ImGui::SameLine();
		ImGui::RadioButton("Set height", &s.mode, 2);
		ImGui::SliderFloat("Radius (tiles)", &s.radius, 0.5f, 40.0f);
		ImGui::InputFloat(s.mode == 2 ? "Target height" : "Cliff height", &s.height, 1.0f, 5.0f, "%.2f");
		ImGui::Checkbox("New level on top of what is there (1st, 2nd, 3rd cliff)", &s.stackLevel);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("On: this cliff is built relative to the ground under it, so a 2nd cliff on the 1st goes above it (Raise) or below it (Lower).\nOff: strokes that touch an existing cliff merge with it and keep its height.");
		if (!s.stackLevel && s.mode < 2) ImGui::Checkbox("Continue the height of a cliff I start on", &s.continueCliff);
		ImGui::Separator();
		ImGui::RadioButton("Sloped cliff", &s.profile, 0); ImGui::SameLine();
		ImGui::RadioButton("Sheer cliff (walls)", &s.profile, 1);
		if (s.profile == 0)
			ImGui::SliderFloat("Slope width (tiles)", &s.edgeWidth, 0.1f, 15.0f);
		ImGui::SliderInt("Terraces", &s.terraces, 1, 8);
		ImGui::SliderFloat("Edge roughness", &s.roughness, 0.0f, 1.5f);
		ImGui::SliderFloat("Curve size", &s.noiseScale, 0.05f, 1.5f);
		ImGui::InputInt("Shape seed", &s.seed);
		ImGui::SameLine();
		if (ImGui::Button("Random")) s.seed = (int)(ImGui::GetTime() * 1000.0) % 100000;
		ImGui::Separator();
		ImGui::Checkbox("Update the GAT (heights + cliff cells)", &s.autoGat);
		if (s.autoGat) ImGui::SliderFloat("GAT cliff above (height per cell)", &s.gatSlope, 1.0f, 15.0f);
		ImGui::Separator();
		auto setEditor = [&](const char* title, std::vector<std::string>& set)
		{
			ImGui::PushID(title);
			ImGui::Text("%s", title);
			for (int i = 0; i < (int)set.size(); i++)
			{
				ImGui::PushID(i);
				if (ImGui::SmallButton("x")) { set.erase(set.begin() + i); ImGui::PopID(); break; }
				ImGui::SameLine();
				ImGui::TextUnformatted(set[i].c_str());
				ImGui::PopID();
			}
			if (ImGui::Button("Add texture selected in the texture picker") && gnd && mv->textureSelected >= 0 && mv->textureSelected < (int)gnd->textures.size())
			{
				auto& f = gnd->textures[mv->textureSelected]->file;
				if (std::find(set.begin(), set.end(), f) == set.end()) set.push_back(f);
			}
			ImGui::PopID();
		};
		if (s.profile == 1)
		{
			ImGui::Checkbox("Build cliff walls", &s.buildWalls);
			ImGui::Checkbox("Random width slice per wall", &s.randomSlice);
			setEditor("Cliff face textures (random pick)", s.wallTextures);
			ImGui::Separator();
			ImGui::Checkbox("Smart edge textures (ground pieces)", &s.smartEdges);
			if (s.smartEdges)
			{
				static const char* names[3] = { "Straight edge", "Rounded corner", "Diagonal" };
				for (int k = 0; k < 3; k++)
				{
					ImGui::PushID(k);
					ImGui::Text("%s: %s", names[k], s.pieceTex[k].empty() ? "(none)" : s.pieceTex[k].c_str());
					if (ImGui::Button("Use picker texture") && gnd && mv->textureSelected >= 0 && mv->textureSelected < (int)gnd->textures.size())
						s.pieceTex[k] = gnd->textures[mv->textureSelected]->file;
					ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::Combo("##rot", &s.pieceRot[k], "Rot 0\0Rot 90\0Rot 180\0Rot 270\0");
					ImGui::SameLine(); ImGui::Checkbox("Flip", &s.pieceFlip[k]);
					ImGui::PopID();
				}
				ImGui::TextWrapped("Pieces assumed: straight = rock on top, corner and diagonal = rock in the lower left. If a piece comes out turned, adjust its Rot/Flip once.");
			}
		}
		else
		{
			ImGui::Checkbox("Ribbon texture (continuous, follows the curve)", &s.ribbon);
			if (s.ribbon)
			{
				ImGui::Text("Ribbon texture: %s", s.ribbonTex.empty() ? "(none)" : s.ribbonTex.c_str());
				if (ImGui::Button("Use picker texture##ribbon") && gnd && mv->textureSelected >= 0 && mv->textureSelected < (int)gnd->textures.size())
					s.ribbonTex = gnd->textures[mv->textureSelected]->file;
				ImGui::Checkbox("Auto texture scale (matches the 3D face)", &s.ribbonAuto);
				if (!s.ribbonAuto)
					ImGui::SliderFloat("Cells per repeat (along)", &s.ribbonPeriod, 0.5f, 24.0f);
				else
					ImGui::TextDisabled("Cells per repeat (along): %.1f", effPeriod(s));
				ImGui::SliderFloat("Band width (tiles, 0 = slope width)", &s.ribbonBandW, 0.0f, 20.0f);
				ImGui::SliderFloat("Rock band top (v)", &s.ribbonTop, 0.0f, 0.5f);
				ImGui::SliderFloat("Rock band bottom (v)", &s.ribbonBottom, 0.5f, 1.0f);
				ImGui::Checkbox("Bake blend with ground (Advanced Texture)", &s.ribbonBake);
				if (s.ribbonBake)
				{
					ImGui::Text("Ground texture: %s", s.ribbonGround.empty() ? "(the tiles' own ground)" : s.ribbonGround.c_str());
					if (ImGui::Button("Use picker texture as ground") && gnd && mv->textureSelected >= 0 && mv->textureSelected < (int)gnd->textures.size())
						s.ribbonGround = gnd->textures[mv->textureSelected]->file;
					ImGui::SameLine(); if (ImGui::Button("Clear##ground")) s.ribbonGround.clear();
					ImGui::RadioButton("Detail 30 px/tile", &s.ribbonDetail, 30); ImGui::SameLine();
					ImGui::RadioButton("Detail 62 px/tile (sharper)", &s.ribbonDetail, 62);
					ImGui::SliderFloat("Edge softness", &s.ribbonSoft, 0.01f, 0.3f);
					ImGui::SliderFloat("Edge ragged", &s.ribbonNoise, 0.0f, 0.2f);
					ImGui::TextWrapped("The blend is a draft preview. Open the Advanced Texture mode (F12) and press Build, or answer the save prompt, to write the texture files.");
				}
				else
					ImGui::SliderFloat("Foot blend (tiles)", &s.ribbonFoot, 0.3f, 4.0f);
				ImGui::Checkbox("Flip direction", &s.ribbonFlip);
				ImGui::TextWrapped("The rock band texture is laid along the cliff curve: u follows the curve, v goes across the slope, so it bends round corners without seams. Used instead of the random slope textures when set.");
			}
			ImGui::Checkbox("Texture steep slopes (random, when no ribbon)", &s.texSlope);
			setEditor("Slope textures (random pick)", s.slopeTextures);
		}
		ImGui::TextWrapped("Clicking a texture in the texture picker adds it to the active list (ribbon / slope textures, or cliff face textures for sheer cliffs); the Add buttons do the same for the picker selection. Shift while painting flips raise/lower. Alt + wheel changes the radius. The whole stroke is one undo step; cliff faces appear when you release the mouse.");
	}
}
