#define IMGUI_DEFINE_MATH_OPERATORS
#include <browedit/BrowEdit.h>
#include <browedit/Icons.h>
#include <browedit/Map.h>
#include <browedit/MapView.h>
#include <browedit/Node.h>
#include <browedit/gl/Texture.h>
#include <browedit/gl/FBO.h>
#include <glad/gl.h>
#include <browedit/components/Rsw.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <stb/stb_image_write.h>

#include <set>
#include <memory>
#include <map>
#include <queue>
#include <functional>
#include <algorithm>
#include <GLFW/glfw3.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/Gat.h>
#include <browedit/math/HermiteCurve.h>
#include <browedit/math/Ray.h>
#include <browedit/CinematicDraw.h>
#include <browedit/util/Util.h>
#include <json.hpp>

namespace
{
	using PosFrame = Rsw::KeyFrameData<std::pair<glm::vec3, glm::vec3>>;
	using TgtFrame = Rsw::KeyFrameData<Rsw::CameraTarget>;

	struct Snap { std::vector<std::vector<Rsw::KeyFrame*>> tracks; float length = 0; };
	std::vector<Snap> undoStack, redoStack;
	struct ClipItem { int track; Rsw::KeyFrame* frame; float rel; };
	std::vector<ClipItem> clipboard;
	std::set<Rsw::KeyFrame*> multiSel;
	Rsw* lastRsw = nullptr;
	float snapStep = 0.0f;
	float playSpeed = 1.0f;

	struct GenParams
	{
		int preset = 0;
		float duration = 40;
		float height = 90;
		float margin = 18;		// percent of the map size that is kept clear at every edge
		float radius = 70;		// orbit / reveal size, percent of the usable area
		bool clockwise = true;
		float keysPerSec = 0.6f;
		bool useViewCenter = false;
		float pacing = 70;		// slow-motion strength (percent) for field / city / dungeon
		int startSide = 0;		// 0 auto, 1 from west, 2 from east, 3 from north, 4 from south
		bool loop = false;		// seamless loop: the end of the path returns to the start
		int look = 0;			// 0 ahead, 1 sideways left, 2 sideways right, 3 looking back, 4 focus on spots, 5 pan sweep
	};

	Rsw::KeyFrame* cloneFrame(Rsw::KeyFrame* f)
	{
		if (auto p = dynamic_cast<PosFrame*>(f)) return new PosFrame(*p);
		if (auto t = dynamic_cast<TgtFrame*>(f)) return new TgtFrame(*t);
		return nullptr;
	}

	Snap takeSnap(Rsw* rsw)
	{
		Snap s;
		s.length = rsw->cinematicLength;
		for (auto& t : rsw->cinematicTracks)
		{
			s.tracks.emplace_back();
			for (auto f : t.frames)
				s.tracks.back().push_back(cloneFrame(f));
		}
		return s;
	}

	void restoreSnap(Rsw* rsw, const Snap& s)
	{
		rsw->cinematicLength = s.length;
		for (size_t i = 0; i < s.tracks.size() && i < rsw->cinematicTracks.size(); i++)
		{
			rsw->cinematicTracks[i].frames.clear();
			for (auto f : s.tracks[i])
				rsw->cinematicTracks[i].frames.push_back(cloneFrame(f));
		}
		multiSel.clear();
	}

	void pushUndo(Rsw* rsw)
	{
		undoStack.push_back(takeSnap(rsw));
		if (undoStack.size() > 100)
			undoStack.erase(undoStack.begin());
		redoStack.clear();
	}

	void sortTrack(Rsw::Track& t)
	{
		std::stable_sort(t.frames.begin(), t.frames.end(), [](Rsw::KeyFrame* a, Rsw::KeyFrame* b) { return a->time < b->time; });
	}

	Rsw::KeyFrame* frameBefore(Rsw::Track& tr, float t)
	{
		Rsw::KeyFrame* r = nullptr;
		for (auto f : tr.frames)
			if (f->time <= t && (!r || f->time >= r->time))
				r = f;
		if (!r)
			for (auto f : tr.frames)
				if (!r || f->time < r->time)
					r = f;
		return r;
	}

	// position and tangent of the camera path at time t (clamped before the first / after the last keyframe)
	bool evalPos(Rsw::Track& tr, float t, glm::vec3& pos, glm::vec3& dir)
	{
		std::vector<PosFrame*> fr;
		for (auto f : tr.frames)
			if (auto p = dynamic_cast<PosFrame*>(f))
				fr.push_back(p);
		if (fr.empty())
			return false;
		std::sort(fr.begin(), fr.end(), [](PosFrame* a, PosFrame* b) { return a->time < b->time; });
		if (fr.size() == 1 || t <= fr.front()->time) { pos = fr.front()->data.first; dir = fr.front()->data.second; return true; }
		if (t >= fr.back()->time) { pos = fr.back()->data.first; dir = fr.back()->data.second; return true; }
		for (size_t i = 0; i + 1 < fr.size(); i++)
		{
			if (fr[i]->time <= t && t < fr[i + 1]->time)
			{
				auto a = fr[i], b = fr[i + 1];
				float ft = b->time - a->time;
				if (ft <= 0.00001f) { pos = a->data.first; dir = a->data.second; return true; }
				float mixF = (t - a->time) / ft;
				float len = math::HermiteCurve::getLength(a->data.first, a->data.second, b->data.first, b->data.second);
				pos = math::HermiteCurve::getPointAtDistance(a->data.first, a->data.second, b->data.first, b->data.second, mixF * len);
				glm::vec3 p2 = math::HermiteCurve::getPointAtDistance(a->data.first, a->data.second, b->data.first, b->data.second, glm::min(1.0f, mixF + 0.02f) * len);
				glm::vec3 p1 = math::HermiteCurve::getPointAtDistance(a->data.first, a->data.second, b->data.first, b->data.second, glm::max(0.0f, mixF - 0.02f) * len);
				glm::vec3 d = p2 - p1;
				float l = glm::length(d);
				float speed = glm::max(1.0f, glm::mix(glm::length(a->data.second), glm::length(b->data.second), mixF));
				dir = l > 0.0001f ? d / l * speed : a->data.second;
				return true;
			}
		}
		return false;
	}

	// a new keyframe at time t that starts out with the value the animation has there (instead of zeros)
	Rsw::KeyFrame* makeFrameAt(Rsw* rsw, int trackIndex, float t)
	{
		auto& tr = rsw->cinematicTracks[trackIndex];
		Rsw::KeyFrame* base = frameBefore(tr, t);
		Rsw::KeyFrame* nf = nullptr;
		if (base)
			nf = cloneFrame(base);
		else if (trackIndex == 0)
			nf = new PosFrame(t, std::pair<glm::vec3, glm::vec3>(glm::vec3(0, 50, 0), glm::vec3(0, 0, 100)));
		else
			nf = new TgtFrame(t, Rsw::CameraTarget());
		if (!nf)
			return nullptr;
		nf->time = t;
		if (auto pf = dynamic_cast<PosFrame*>(nf))
		{
			glm::vec3 p, d;
			if (evalPos(rsw->cinematicTracks[0], t, p, d))
			{
				pf->data.first = p;
				pf->data.second = d;
			}
		}
		return nf;
	}

	void editorCamera(MapView* mv, glm::vec3& pos, glm::vec3& center)
	{
		float d = mv->cameraDistance;
		float a = glm::radians(mv->cameraRot.x);
		float b = glm::radians(mv->cameraRot.y);
		center = mv->cameraCenter;
		pos = center + glm::vec3(-d * std::cos(a) * std::sin(b), d * std::sin(a), d * std::cos(a) * std::cos(b));
	}

	void goToShot(Rsw* rsw, MapView* mv, float t)
	{
		glm::vec3 pos, dir;
		if (!evalPos(rsw->cinematicTracks[0], t, pos, dir))
			return;
		glm::vec3 center = pos + glm::normalize(dir + glm::vec3(0, 0.0001f, 0)) * 100.0f;
		if (rsw->cinematicTracks.size() > 1)
			if (auto tf = dynamic_cast<TgtFrame*>(frameBefore(rsw->cinematicTracks[1], t)))
				if (tf->data.lookAt == Rsw::CameraTarget::LookAt::Point)
					center = tf->data.point;
		glm::vec3 o = pos - center;
		float d = glm::clamp(glm::length(o), 5.0f, 4000.0f);
		float a = std::asin(glm::clamp(o.y / glm::max(0.001f, glm::length(o)), 0.0f, 1.0f));
		float b = std::atan2(-o.x, o.z);
		mv->cameraCenter = center;
		mv->cameraDistance = d;
		mv->cameraRot = glm::vec2(glm::degrees(a), glm::degrees(b));
		mv->cinematicPlay = false;
	}

	float groundAt(Gnd* gnd, float x, float z, float fallback)
	{
		glm::vec3 h = gnd->rayCast(math::Ray(glm::vec3(x, 3000.0f, z), glm::vec3(0, -1, 0)), true);
		if (h.x > 1e30f || h.y > 1e30f)
			return fallback;
		return h.y;
	}

	float groundMax(Gnd* gnd, float x, float z, float r, float fallback)
	{
		float m = groundAt(gnd, x, z, fallback);
		for (int i = 0; i < 8; i++)
		{
			float a = i * 3.14159265f / 4.0f;
			m = glm::max(m, groundAt(gnd, x + std::cos(a) * r, z + std::sin(a) * r, fallback));
		}
		return m;
	}

	struct Poly
	{
		std::vector<glm::vec2> pts;
		std::vector<float> cum;
		void build()
		{
			cum.assign(pts.size(), 0.0f);
			for (size_t i = 1; i < pts.size(); i++)
				cum[i] = cum[i - 1] + glm::length(pts[i] - pts[i - 1]);
		}
		glm::vec2 at(float u) const
		{
			if (pts.size() == 1 || cum.back() <= 0.0001f)
				return pts[0];
			float d = glm::clamp(u, 0.0f, 1.0f) * cum.back();
			size_t i = std::upper_bound(cum.begin(), cum.end(), d) - cum.begin();
			if (i == 0) return pts[0];
			if (i >= pts.size()) return pts.back();
			float f = (d - cum[i - 1]) / glm::max(0.0001f, cum[i] - cum[i - 1]);
			return glm::mix(pts[i - 1], pts[i], f);
		}
		void smooth(int window, int passes)
		{
			for (int p = 0; p < passes; p++)
			{
				auto src = pts;
				for (int i = 1; i + 1 < (int)pts.size(); i++)
				{
					glm::vec2 s(0);
					int n = 0;
					for (int k = -window; k <= window; k++)
					{
						int j = i + k;
						if (j < 0 || j >= (int)src.size()) continue;
						s += src[j];
						n++;
					}
					pts[i] = s / (float)n;
				}
			}
		}
	};

	// nearest-neighbour ordering of waypoints, starting at the first one
	std::vector<glm::vec2> orderWaypoints(std::vector<glm::vec2> w)
	{
		std::vector<glm::vec2> out;
		if (w.empty()) return out;
		out.push_back(w[0]);
		w.erase(w.begin());
		while (!w.empty())
		{
			size_t best = 0;
			for (size_t i = 1; i < w.size(); i++)
				if (glm::distance(w[i], out.back()) < glm::distance(w[best], out.back()))
					best = i;
			out.push_back(w[best]);
			w.erase(w.begin() + best);
		}
		return out;
	}

	// slow-motion / fast mix along a polyline: returns the remap from time (0..1) to path position (0..1)
	std::function<float(float)> buildRemap(const Poly& poly, const std::vector<float>& poiS, float strength, float slowSpeed, float fastSpeed, bool corners = true)
	{
		const int ST = 600;
		std::vector<float> mask(ST + 1, 0.0f), tc(ST + 1, 0.0f);
		for (int k = 0; k <= ST; k++)
		{
			float sp = k / (float)ST, v = 0.0f;
			for (float ps : poiS)
			{
				float d = (sp - ps) / 0.045f;
				v = glm::max(v, std::exp(-d * d));
			}
			glm::vec2 a = poly.at(glm::max(0.0f, sp - 0.012f)), b = poly.at(sp), c2 = poly.at(glm::min(1.0f, sp + 0.012f));
			glm::vec2 v1 = b - a, v2 = c2 - b;
			if (corners && glm::length(v1) > 0.01f && glm::length(v2) > 0.01f)
			{
				float ang = std::acos(glm::clamp(glm::dot(glm::normalize(v1), glm::normalize(v2)), -1.0f, 1.0f));
				v = glm::max(v, glm::clamp(ang / 0.9f, 0.0f, 1.0f) * 0.7f);
			}
			mask[k] = v;
		}
		for (int pass = 0; pass < 3; pass++)
		{
			auto m2 = mask;
			for (int k = 1; k < ST; k++) m2[k] = 0.25f * mask[k - 1] + 0.5f * mask[k] + 0.25f * mask[k + 1];
			mask = m2;
		}
		for (int k = 0; k < ST; k++)
			tc[k + 1] = tc[k] + 1.0f / glm::mix(1.0f, glm::mix(fastSpeed, slowSpeed, mask[k]), strength);
		for (auto& t : tc) t /= tc.back();
		return [tc, ST](float tau)
		{
			tau = glm::clamp(tau, 0.0f, 1.0f);
			size_t k = std::upper_bound(tc.begin(), tc.end(), tau) - tc.begin();
			if (k == 0) return 0.0f;
			if (k >= tc.size()) return 1.0f;
			float f = (tau - tc[k - 1]) / glm::max(0.000001f, tc[k] - tc[k - 1]);
			return ((k - 1) + f) / (float)ST;
		};
	}

	// "City road": street-level tour that stays on the roads (walkable cells away from large models), then spirals at the
	// centre of the map, zooms out and zooms back in. Returns false (with a message) when the map has no usable roads.
	bool generateCityRoad(Rsw* rsw, MapView* mv, const GenParams& gp, std::string& msg)
	{
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		auto gat = mv->map->rootNode->getComponent<Gat>();
		const int gw = gat->width, gh = gat->height;
		const float W = gnd->width * 10.0f, D = gnd->height * 10.0f;
		const float mg = glm::clamp(gp.margin, 0.0f, 40.0f) / 100.0f;
		const float x0 = W * mg, x1 = W * (1 - mg), z0 = D * mg, z1 = D * (1 - mg);
		const float sgn = gp.clockwise ? 1.0f : -1.0f;
		glm::vec2 dirV = (x1 - x0) >= (z1 - z0) ? glm::vec2(1, 0) : glm::vec2(0, 1);
		if (gp.startSide == 1) dirV = glm::vec2(1, 0);
		else if (gp.startSide == 2) dirV = glm::vec2(-1, 0);
		else if (gp.startSide == 3) dirV = glm::vec2(0, 1);
		else if (gp.startSide == 4) dirV = glm::vec2(0, -1);
		glm::vec2 mapC(W / 2, D / 2);
		if (gp.useViewCenter) mapC = glm::vec2(mv->cameraCenter.x, mv->cameraCenter.z);
		const float cg = groundAt(gnd, mapC.x, mapC.y, 0.0f);

		auto walkable = [&](int x, int y) {
			if (x < 0 || y < 0 || x >= gw || y >= gh) return false;
			int t = gat->cubes[x][y]->gatType;
			return t == 0 || t == 3 || t == 4 || t == 6;
		};
		auto cellWorld = [&](int x, int y) { return glm::vec2(5.0f * x + 2.5f, 5.0f * gh - 5.0f * y - 2.5f); };
		auto worldCell = [&](glm::vec2 w) { return glm::ivec2((int)std::floor(w.x / 5.0f), (int)std::floor((5.0f * gh - w.y) / 5.0f)); };

		// cells covered by large models (houses, walls, ...) plus a safety margin
		std::vector<char> blocked((size_t)gw * gh, 0);
		mv->map->rootNode->traverse([&](Node* n)
		{
			auto m = n->getComponent<RswModel>();
			auto o = n->getComponent<RswObject>();
			if (!m || !o) return;
			float ex = (m->aabb.max.x - m->aabb.min.x) * std::abs(o->scale.x), ez = (m->aabb.max.z - m->aabb.min.z) * std::abs(o->scale.z);
			float diameter = std::sqrt(ex * ex + ez * ez);
			if (diameter < 30.0f || diameter > 5000.0f) return;
			glm::vec2 wp(gnd->width * 5.0f + o->position.x, gnd->height * 5.0f - o->position.z + 10.0f);
			float r = diameter * 0.5f * 0.85f + 8.0f;
			glm::ivec2 cc = worldCell(wp);
			int rc = (int)std::ceil(r / 5.0f);
			for (int dy = -rc; dy <= rc; dy++)
				for (int dx = -rc; dx <= rc; dx++)
					if (dx * dx + dy * dy <= rc * rc && cc.x + dx >= 0 && cc.y + dy >= 0 && cc.x + dx < gw && cc.y + dy < gh)
						blocked[(size_t)(cc.y + dy) * gw + cc.x + dx] = 1;
		});
		// distance (in cells) to the nearest blocked / non-walkable cell: the middle of a road has the highest value
		std::vector<int> clr((size_t)gw * gh, -1);
		{
			std::queue<glm::ivec2> q;
			for (int y = 0; y < gh; y++)
				for (int x = 0; x < gw; x++)
					if (!walkable(x, y) || blocked[(size_t)y * gw + x]) { clr[(size_t)y * gw + x] = 0; q.push({ x, y }); }
			const glm::ivec2 nb[4] = { {1,0},{-1,0},{0,1},{0,-1} };
			while (!q.empty())
			{
				auto cur = q.front(); q.pop();
				int d = clr[(size_t)cur.y * gw + cur.x];
				if (d >= 30) continue;
				for (auto& dd : nb)
				{
					glm::ivec2 nn = cur + dd;
					if (nn.x < 0 || nn.y < 0 || nn.x >= gw || nn.y >= gh || clr[(size_t)nn.y * gw + nn.x] != -1) continue;
					clr[(size_t)nn.y * gw + nn.x] = d + 1;
					q.push(nn);
				}
			}
		}
		auto clearance = [&](int x, int y) { int v = clr[(size_t)y * gw + x]; return v < 0 ? 30 : v; };

		std::vector<glm::ivec2> cands;
		for (int y = 3; y < gh; y += 6)
			for (int x = 3; x < gw; x += 6)
			{
				auto w = cellWorld(x, y);
				if (clearance(x, y) >= 3 && w.x >= x0 && w.x <= x1 && w.y >= z0 && w.y <= z1)
					cands.push_back({ x, y });
			}
		if (cands.size() < 8)
		{
			msg = "No roads found (walkable ground away from large models), used the city preset instead.";
			return false;
		}
		// centre of the spiral: the road cell closest to the map centre
		glm::ivec2 c0 = cands[0];
		for (auto& cc : cands)
			if (glm::distance(cellWorld(cc.x, cc.y), mapC) < glm::distance(cellWorld(c0.x, c0.y), mapC)) c0 = cc;
		const glm::vec2 centreW = cellWorld(c0.x, c0.y);
		// tour waypoints spread over the map, away from the centre
		std::vector<glm::ivec2> far2;
		for (auto& cc : cands) if (glm::distance(cellWorld(cc.x, cc.y), centreW) > 120.0f) far2.push_back(cc);
		if (far2.size() < 3)
		{
			msg = "The roads are too small for the city road preset, used the city preset instead.";
			return false;
		}
		std::vector<glm::ivec2> chosen;
		{
			size_t startI = 0;
			for (size_t k = 1; k < far2.size(); k++)
			{
				auto a = cellWorld(far2[k].x, far2[k].y), b = cellWorld(far2[startI].x, far2[startI].y);
				if (gp.startSide != 0 ? glm::dot(a, dirV) < glm::dot(b, dirV) : glm::distance(a, centreW) > glm::distance(b, centreW))
					startI = k;
			}
			chosen.push_back(far2[startI]);
			while (chosen.size() < 6 && chosen.size() < far2.size())
			{
				float bestD = -1; glm::ivec2 best = far2[0];
				for (auto& cc : far2)
				{
					float md = 1e30f;
					for (auto& ch : chosen) md = glm::min(md, glm::distance(glm::vec2(cc), glm::vec2(ch)));
					if (md > bestD) { bestD = md; best = cc; }
				}
				chosen.push_back(best);
			}
		}
		std::vector<glm::ivec2> order;		// nearest-neighbour tour, ending at the centre
		order.push_back(chosen[0]);
		chosen.erase(chosen.begin());
		while (!chosen.empty())
		{
			size_t best = 0;
			for (size_t k = 1; k < chosen.size(); k++)
				if (glm::distance(glm::vec2(chosen[k]), glm::vec2(order.back())) < glm::distance(glm::vec2(chosen[best]), glm::vec2(order.back()))) best = k;
			order.push_back(chosen[best]);
			chosen.erase(chosen.begin() + best);
		}
		order.push_back(c0);

		// shortest path that prefers the middle of the road (Dijkstra)
		auto shortest = [&](glm::ivec2 a, glm::ivec2 b, std::vector<glm::vec2>& out)
		{
			std::vector<float> dist((size_t)gw * gh, 1e30f);
			std::vector<int> prev((size_t)gw * gh, -1);
			using Item = std::pair<float, int>;
			std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
			dist[(size_t)a.y * gw + a.x] = 0;
			pq.push({ 0.0f, a.y * gw + a.x });
			const glm::ivec2 nb[4] = { {1,0},{-1,0},{0,1},{0,-1} };
			bool found = false;
			while (!pq.empty())
			{
				auto [d, idx] = pq.top(); pq.pop();
				if (d > dist[idx]) continue;
				if (idx == b.y * gw + b.x) { found = true; break; }
				glm::ivec2 cur(idx % gw, idx / gw);
				for (auto& dd : nb)
				{
					glm::ivec2 nn = cur + dd;
					if (!walkable(nn.x, nn.y) || clearance(nn.x, nn.y) < 2) continue;
					float cost = d + 1.0f + 8.0f / (1.0f + clearance(nn.x, nn.y));
					if (cost < dist[(size_t)nn.y * gw + nn.x])
					{
						dist[(size_t)nn.y * gw + nn.x] = cost;
						prev[(size_t)nn.y * gw + nn.x] = idx;
						pq.push({ cost, nn.y * gw + nn.x });
					}
				}
			}
			if (!found) return false;
			std::vector<glm::vec2> leg;
			for (int cur = b.y * gw + b.x; cur != -1; cur = prev[cur])
				leg.push_back(cellWorld(cur % gw, cur / gw));
			std::reverse(leg.begin(), leg.end());
			for (size_t i = 0; i < leg.size(); i += 3)
				out.push_back(leg[i]);
			out.push_back(leg.back());
			return true;
		};
		auto buildRoute = [&](const std::vector<glm::ivec2>& wps, Poly& poly, std::vector<float>& poiS)
		{
			std::vector<size_t> wpIdx;
			for (size_t k = 0; k + 1 < wps.size(); k++)
			{
				if (shortest(wps[k], wps[k + 1], poly.pts))
					wpIdx.push_back(poly.pts.size() - 1);
			}
			poly.build();
			for (size_t i : wpIdx)
				if (i + 1 < poly.pts.size())
					poiS.push_back(poly.cum[i] / glm::max(0.0001f, poly.cum.back()));
			poly.smooth(2, 2);
			poly.build();
		};
		Poly tourA, tourB;
		std::vector<float> poiA, poiB;
		buildRoute(order, tourA, poiA);
		if (tourA.pts.size() < 4)
		{
			msg = "Could not connect the roads, used the city preset instead.";
			return false;
		}
		bool loop = gp.loop;
		if (loop)
		{
			std::vector<glm::ivec2> back = { c0, order[0] };
			buildRoute(back, tourB, poiB);
			if (tourB.pts.size() < 3) loop = false;
		}

		// phases (fractions of the total duration)
		struct Phase { int kind; float t0, t1; };		// 0 tour A, 1 spiral, 2 zoom out, 3 zoom in, 4 tour B
		std::vector<Phase> phases;
		float fr[5] = { 0.55f, 0.20f, 0.12f, 0.13f, 0.0f };
		if (loop) { fr[0] = 0.40f; fr[1] = 0.17f; fr[2] = 0.10f; fr[3] = 0.11f; fr[4] = 0.22f; }
		float acc = 0.0f;
		for (int k = 0; k < 5; k++)
			if (fr[k] > 0)
			{
				phases.push_back({ k, acc * gp.duration, (acc + fr[k]) * gp.duration });
				acc += fr[k];
			}

		const float rE = glm::clamp(0.12f * glm::min(x1 - x0, z1 - z0), 80.0f, 400.0f);
		const float rFar = rE * 3.0f;
		const float roadH = gp.height;
		auto smooth01 = [](float u) { u = glm::clamp(u, 0.0f, 1.0f); return u * u * (3 - 2 * u); };
		auto clampW = [&](glm::vec2 p) { return glm::clamp(p, glm::vec2(W * 0.02f, D * 0.02f), glm::vec2(W * 0.98f, D * 0.98f)); };

		// altitude along a road: low, following the ground gently
		auto roadAltitude = [&](const Poly& pl)
		{
			const int K = 300;
			std::vector<float> y(K + 1), sm(K + 1);
			for (int k = 0; k <= K; k++)
			{
				glm::vec2 xz = pl.at(k / (float)K);
				y[k] = groundMax(gnd, xz.x, xz.y, 8.0f, cg) + roadH;
			}
			for (int pass = 0; pass < 2; pass++)
			{
				for (int k = 0; k <= K; k++)
				{
					float sum = 0; int n = 0;
					for (int j = glm::max(0, k - 8); j <= glm::min(K, k + 8); j++) { sum += y[j]; n++; }
					sm[k] = sum / n;
				}
				y = sm;
			}
			return std::function<float(float)>([y, K](float s) {
				float f = glm::clamp(s, 0.0f, 1.0f) * K; int k0 = glm::min(K - 1, (int)f);
				return glm::mix(y[k0], y[k0 + 1], f - k0); });
		};
		auto altA = roadAltitude(tourA);
		std::function<float(float)> altB;
		if (loop) altB = roadAltitude(tourB);
		const float strength = glm::clamp(gp.pacing / 100.0f, 0.0f, 1.0f);
		auto remapA = buildRemap(tourA, poiA, strength, 0.4f, 1.5f);
		std::function<float(float)> remapB;
		if (loop) remapB = buildRemap(tourB, poiB, strength, 0.4f, 1.5f);

		const float groundC = groundAt(gnd, centreW.x, centreW.y, cg);
		const glm::vec3 centreP(centreW.x, groundC + roadH, centreW.y);
		auto headingAt = [&](const Poly& pl, float s) {
			glm::vec2 h = pl.at(glm::min(1.0f, s + 0.02f)) - pl.at(glm::max(0.0f, s - 0.02f));
			return glm::length(h) > 0.001f ? glm::normalize(h) : dirV;
		};
		const glm::vec2 endHeadA = headingAt(tourA, 1.0f);
		const glm::vec2 startHeadB = loop ? headingAt(tourB, 0.0f) : endHeadA;
		auto rot2 = [](glm::vec2 v, float a) { return glm::vec2(v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)); };
		const float th0 = std::atan2(endHeadA.y, endHeadA.x);

		// camera position and look-at target for a phase at local progress u (0..1)
		auto sample = [&](const Phase& ph, float u, glm::vec3& pos, glm::vec3& tgt)
		{
			const float phDur = ph.t1 - ph.t0;
			if (ph.kind == 0 || ph.kind == 4)
			{
				const Poly& pl = ph.kind == 0 ? tourA : tourB;
				auto& remap = ph.kind == 0 ? remapA : remapB;
				auto& alt = ph.kind == 0 ? altA : altB;
				float s = remap(u);
				glm::vec2 here = pl.at(s);
				pos = glm::vec3(here.x, alt(s), here.y);
				float la = glm::clamp(3.0f / glm::max(1.0f, phDur), 0.02f, 0.25f);
				glm::vec2 q = pl.at(glm::min(1.0f, s + la));
				glm::vec2 hd = headingAt(pl, s);
				glm::vec2 dir = q - here;
				if (glm::length(dir) < 1.0f) dir = hd * 40.0f;
				float dist = glm::max(40.0f, glm::length(dir));
				float ang = 0.0f;
				if (gp.look == 0) ang = 0.45f * std::sin(6.2831853f * u * 3.0f) * glm::sin(3.14159265f * glm::clamp(u, 0.0f, 1.0f));		// gentle look left / right at the shop fronts
				else if (gp.look == 1) ang = 1.5707963f;
				else if (gp.look == 2) ang = -1.5707963f;
				else if (gp.look == 3) ang = 3.14159265f;
				else if (gp.look == 5) ang = (u - 0.5f) * 2.0f * 1.15f * sgn;
				glm::vec2 d2 = rot2(glm::normalize(gp.look == 0 ? dir : hd), ang) * (gp.look == 0 ? dist : glm::max(60.0f, roadH * 3.0f));
				float pitch = glm::radians(8.0f + 3.0f * std::sin(6.2831853f * u));
				tgt = glm::vec3(here.x + d2.x, pos.y - glm::length(d2) * std::tan(pitch), here.y + d2.y);
			}
			else
			{
				float r, y, th;
				glm::vec3 look(centreW.x, groundC + roadH * 1.5f, centreW.y);
				glm::vec3 aheadTgt(centreW.x + (ph.kind == 3 ? startHeadB.x : endHeadA.x) * 90.0f, centreP.y - 6.0f, centreW.y + (ph.kind == 3 ? startHeadB.y : endHeadA.y) * 90.0f);
				if (ph.kind == 1)			// spiral outwards from the road, rising
				{
					float e = smooth01(u);
					r = rE * e;
					y = glm::mix(centreP.y, groundC + roadH * 4.0f, e);
					th = th0 + sgn * 6.2831853f * 1.25f * u;
				}
				else if (ph.kind == 2)		// zoom out
				{
					float e = smooth01(u);
					r = glm::mix(rE, rFar, e);
					y = glm::mix(groundC + roadH * 4.0f, groundC + glm::max(roadH * 8.0f, 0.55f * rFar), e);
					th = th0 + sgn * (6.2831853f * 1.25f + 0.6f * e);
				}
				else						// zoom in, back down to the road at the centre
				{
					float e = smooth01(u);
					r = glm::mix(rFar, 0.0f, e);
					y = glm::mix(groundC + glm::max(roadH * 8.0f, 0.55f * rFar), centreP.y, e);
					th = th0 + sgn * (6.2831853f * 1.25f + 0.6f + 1.2f * e);
				}
				glm::vec2 p2 = clampW(glm::vec2(centreW.x + r * std::cos(th), centreW.y + r * std::sin(th)));
				pos = glm::vec3(p2.x, y, p2.y);
				float blend = smooth01(r / (0.35f * rE));		// near the road look along it, further out look at the centre
				tgt = glm::mix(aheadTgt, look, blend);
			}
		};

		// emit keyframes
		const float kps = glm::max(0.3f, gp.keysPerSec);
		std::vector<float> times;
		std::vector<glm::vec3> pos, tgt;
		for (size_t pi = 0; pi < phases.size(); pi++)
		{
			auto& ph = phases[pi];
			float ks = (ph.kind == 0 || ph.kind == 4) ? kps : glm::max(kps, 1.2f);
			int n = glm::max(3, (int)std::round((ph.t1 - ph.t0) * ks));
			for (int k = 0; k < n; k++)
			{
				float u = k / (float)n;
				glm::vec3 p, t;
				sample(ph, u, p, t);
				times.push_back(ph.t0 + u * (ph.t1 - ph.t0));
				pos.push_back(p);
			}
		}
		{
			glm::vec3 p, t;
			sample(phases.back(), 1.0f, p, t);
			times.push_back(gp.duration);
			pos.push_back(p);
		}
		auto& posTrack = rsw->cinematicTracks[0];
		auto& tgtTrack = rsw->cinematicTracks[1];
		posTrack.frames.clear();
		tgtTrack.frames.clear();
		const size_t NP = pos.size();
		for (size_t i = 0; i < NP; i++)
		{
			size_t a = i == 0 ? 0 : i - 1, b = i + 1 < NP ? i + 1 : i;
			glm::vec3 v = (pos[b] - pos[a]) / glm::max(0.0001f, times[b] - times[a]);
			float dtAvg = 0.5f * ((i > 0 ? times[i] - times[i - 1] : times[1] - times[0]) + (i + 1 < NP ? times[i + 1] - times[i] : times[i] - times[i - 1]));
			posTrack.frames.push_back(new PosFrame(times[i], std::pair<glm::vec3, glm::vec3>(pos[i], v * dtAvg)));
		}
		const int M = glm::max(2, (int)std::ceil(gp.duration / 0.5f));
		for (int j = 0; j <= M; j++)
		{
			float t = gp.duration * j / (float)M;
			const Phase* ph = &phases.back();
			for (auto& p : phases) if (t < p.t1) { ph = &p; break; }
			glm::vec3 p, tg;
			sample(*ph, glm::clamp((t - ph->t0) / glm::max(0.0001f, ph->t1 - ph->t0), 0.0f, 1.0f), p, tg);
			tgtTrack.frames.push_back(new TgtFrame(t, Rsw::CameraTarget(tg, 1.0f)));
		}
		rsw->cinematicLength = gp.duration;
		return true;
	}


	// Builds the cinematic from the path drawn on the map: the camera follows the line and turns with it
	std::string generateFromPath(Rsw* rsw, MapView* mv)
	{
		using namespace cinedraw;
		if (points.size() < 2)
			return "Draw at least two points first.";
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		const bool loop = closedLoop && points.size() >= 3;
		std::vector<int> ctrl;
		auto C = curve(18, loop, &ctrl);
		const int n = (int)C.size();
		Poly poly;
		for (auto& c : C) poly.pts.push_back(glm::vec2(c.x, c.z));
		poly.build();
		const float total = glm::max(1.0f, poly.cum.back());
		const float cg = points[0].pos.y;

		// altitude: the drawn heights, kept above the ground and averaged so it stays calm
		std::vector<float> y(n), sm(n);
		for (int i = 0; i < n; i++)
			y[i] = glm::max(C[i].y, groundMax(gnd, C[i].x, C[i].z, 10.0f, cg) + 6.0f);
		for (int pass = 0; pass < 2; pass++)
		{
			for (int i = 0; i < n; i++)
			{
				float sum = 0; int cnt = 0;
				for (int j = i - 5; j <= i + 5; j++)
				{
					int jj = loop ? ((j % (n - 1)) + (n - 1)) % (n - 1) : j;
					if (jj < 0 || jj >= n) continue;
					sum += y[jj]; cnt++;
				}
				sm[i] = sum / cnt;
			}
			y = sm;
			if (loop) y[n - 1] = y[0];
		}

		std::vector<float> ctrlS;
		std::vector<float> poiS;
		for (size_t j = 0; j < ctrl.size() && j < points.size(); j++)
		{
			float sj = poly.cum[glm::min(ctrl[j], n - 1)] / total;
			ctrlS.push_back(sj);
			if (points[j].hasFocus || points[j].look >= 0 || points[j].height >= 0)
				poiS.push_back(sj);
		}
		auto remap = buildRemap(poly, poiS, glm::clamp(pacing / 100.0f, 0.0f, 1.0f), 0.4f, 1.5f, slowCorners);

		// look angle at each control point (0 ahead, +90 left, -90 right, 180 back), unwrapped so turns take the short way
		auto modeAngle = [](int m) { return m == 1 ? 1.5707963f : m == 2 ? -1.5707963f : m == 3 ? 3.14159265f : 0.0f; };
		std::vector<float> A;
		for (size_t j = 0; j < points.size(); j++)
		{
			int m = points[j].look >= 0 ? points[j].look : (defLook == 5 ? 0 : defLook);
			float a = modeAngle(m);
			if (!A.empty())
				while (a - A.back() > 3.14159265f) a -= 6.2831853f;
			if (!A.empty())
				while (a - A.back() < -3.14159265f) a += 6.2831853f;
			A.push_back(a);
		}
		std::vector<float> sA = ctrlS;
		if (loop)
		{
			float a0 = A[0];
			while (a0 - A.back() > 3.14159265f) a0 -= 6.2831853f;
			while (a0 - A.back() < -3.14159265f) a0 += 6.2831853f;
			A.push_back(a0);
			sA.push_back(1.0f);
		}
		auto smooth01 = [](float u) { u = glm::clamp(u, 0.0f, 1.0f); return u * u * (3 - 2 * u); };
		auto angleAt = [&](float s)
		{
			if (s <= sA.front()) return A.front();
			for (size_t j = 0; j + 1 < sA.size() && j + 1 < A.size(); j++)
				if (s < sA[j + 1])
					return glm::mix(A[j], A[j + 1], smooth01((s - sA[j]) / glm::max(0.0001f, sA[j + 1] - sA[j])));
			return A.back();
		};
		auto rot2 = [](glm::vec2 v, float a) { return glm::vec2(v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a)); };
		auto altitude = [&](float s) { float f = glm::clamp(s, 0.0f, 1.0f) * (n - 1); int k0 = glm::min(n - 2, (int)f); return glm::mix(y[k0], y[k0 + 1], f - k0); };
		const float lookAheadS = glm::clamp(glm::max(60.0f, 3.0f * defaultHeight) / total, 0.01f, 0.3f);
		const float sigmaS = 150.0f / total;

		auto sample = [&](float tau, glm::vec3& pos, glm::vec3& tgt)
		{
			float s = remap(tau);
			glm::vec2 here = poly.at(s);
			pos = glm::vec3(here.x, altitude(s), here.y);
			float sa = loop ? s + lookAheadS : glm::min(1.0f, s + lookAheadS);
			if (loop) sa -= std::floor(sa);
			glm::vec2 ahead = poly.at(sa);
			glm::vec2 hd = poly.at(glm::min(1.0f, s + 0.01f)) - poly.at(glm::max(0.0f, s - 0.01f));
			hd = glm::length(hd) > 0.0001f ? glm::normalize(hd) : glm::vec2(1, 0);
			glm::vec2 dir = ahead - here;
			float dl = glm::length(dir);
			if (dl < 1.0f) { dir = hd * 40.0f; dl = 40.0f; }
			float ang = angleAt(s);
			if (defLook == 5)
				ang += loop ? std::sin(6.2831853f * tau) : (tau - 0.5f) * 2.3f;
			glm::vec2 d2 = rot2(dir / dl, ang) * glm::max(40.0f, dl);
			float pitch = glm::radians(pitchDeg + 3.0f * std::sin(6.2831853f * tau));
			tgt = glm::vec3(here.x + d2.x, pos.y - glm::length(d2) * std::tan(pitch), here.y + d2.y);
			// look at the focus point of a nearby control point
			float bestW = 0; int bestJ = -1;
			for (size_t j = 0; j < ctrlS.size() && j < points.size(); j++)
			{
				if (!points[j].hasFocus) continue;
				float d = std::abs(s - ctrlS[j]);
				if (loop) d = glm::min(d, 1.0f - d);
				float w = std::exp(-(d / sigmaS) * (d / sigmaS));
				if (w > bestW) { bestW = w; bestJ = (int)j; }
			}
			if (bestJ >= 0)
				tgt = glm::mix(tgt, points[bestJ].focus, smooth01(bestW));
		};

		const int N = glm::max(4, (int)std::round(duration * keysPerSec));
		std::vector<glm::vec3> P(N + 1);
		std::vector<float> T(N + 1);
		for (int i = 0; i <= N; i++)
		{
			glm::vec3 t;
			T[i] = duration * i / (float)N;
			sample(i / (float)N, P[i], t);
		}
		if (loop) P[N] = P[0];
		auto& posTrack = rsw->cinematicTracks[0];
		auto& tgtTrack = rsw->cinematicTracks[1];
		posTrack.frames.clear();
		tgtTrack.frames.clear();
		for (int i = 0; i <= N; i++)
		{
			glm::vec3 v;
			float dtAvg;
			if (loop && (i == 0 || i == N))
			{
				v = (P[1] - P[N - 1]) / glm::max(0.0001f, (T[1] - T[0]) + (T[N] - T[N - 1]));
				dtAvg = 0.5f * ((T[1] - T[0]) + (T[N] - T[N - 1]));
			}
			else
			{
				int a = glm::max(0, i - 1), b = glm::min(N, i + 1);
				v = (P[b] - P[a]) / glm::max(0.0001f, T[b] - T[a]);
				dtAvg = 0.5f * ((i > 0 ? T[i] - T[i - 1] : T[1] - T[0]) + (i < N ? T[i + 1] - T[i] : T[i] - T[i - 1]));
			}
			posTrack.frames.push_back(new PosFrame(T[i], std::pair<glm::vec3, glm::vec3>(P[i], v * dtAvg)));
		}
		const int M = glm::max(2, (int)std::ceil(duration / 0.5f));
		for (int j = 0; j <= M; j++)
		{
			glm::vec3 p, t;
			sample(j / (float)M, p, t);
			tgtTrack.frames.push_back(new TgtFrame(duration * j / (float)M, Rsw::CameraTarget(t, 1.0f)));
		}
		rsw->cinematicLength = duration;
		return "";
	}

	// Builds the keyframes of an automatic shot. Returns a message when it had to fall back to another preset.
	std::string generateCinematic(Rsw* rsw, MapView* mv, GenParams gp)
	{
		std::string msg;
		if (gp.preset == 11)
		{
			if (generateCityRoad(rsw, mv, gp, msg))
				return msg;
			gp.preset = 1;		// fall back to the plain city tour
		}
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		auto gat = mv->map->rootNode->getComponent<Gat>();
		const float W = gnd->width * 10.0f, D = gnd->height * 10.0f;
		const float mg = glm::clamp(gp.margin, 0.0f, 40.0f) / 100.0f;
		const float x0 = W * mg, x1 = W * (1 - mg), z0 = D * mg, z1 = D * (1 - mg);
		glm::vec2 c(W / 2, D / 2);
		if (gp.useViewCenter)
			c = glm::vec2(mv->cameraCenter.x, mv->cameraCenter.z);
		c = glm::clamp(c, glm::vec2(x0, z0), glm::vec2(x1, z1));
		const float cg = groundAt(gnd, c.x, c.y, 0.0f);
		const float spanX = x1 - x0, spanZ = z1 - z0;
		const float half = 0.5f * glm::min(spanX, spanZ);
		auto clampXZ = [&](glm::vec2 p) { return glm::clamp(p, glm::vec2(x0, z0), glm::vec2(x1, z1)); };

		std::function<glm::vec2(float)> xzAt;
		std::function<float(float)> hAt = [](float) { return 1.0f; };
		bool constTarget = false, loop = false;
		float groundRadius = 35.0f;
		Poly poly;
		const float sgn = gp.clockwise ? 1.0f : -1.0f;
		int preset = gp.preset;
		glm::vec2 dirV = spanX >= spanZ ? glm::vec2(1, 0) : glm::vec2(0, 1);
		if (gp.startSide == 1) dirV = glm::vec2(1, 0);
		else if (gp.startSide == 2) dirV = glm::vec2(-1, 0);
		else if (gp.startSide == 3) dirV = glm::vec2(0, 1);
		else if (gp.startSide == 4) dirV = glm::vec2(0, -1);
		if (!gp.clockwise) dirV = -dirV;
		const glm::vec2 latV(-dirV.y, dirV.x);
		const float Lh = (std::abs(dirV.x) > 0.5f ? spanX : spanZ) * 0.5f;
		const float Ls = (std::abs(dirV.x) > 0.5f ? spanZ : spanX) * 0.5f;
		bool topDown = false;

		// closed "racetrack" for straight presets: out along a -> b, back along a parallel line, with rounded corners
		auto makeRace = [&](glm::vec2 a, glm::vec2 b, glm::vec2 lateral)
		{
			auto pl = std::make_shared<Poly>();
			std::vector<glm::vec2> corners = { a - lateral, b - lateral, b + lateral, a + lateral };
			for (size_t k = 0; k < corners.size(); k++)
			{
				glm::vec2 pv = corners[(k + 3) % 4], cu = corners[k], nx = corners[(k + 1) % 4];
				pl->pts.push_back(cu + (pv - cu) * 0.15f);
				pl->pts.push_back(cu);
				pl->pts.push_back(cu + (nx - cu) * 0.15f);
			}
			pl->pts.push_back(pl->pts.front());
			pl->build();
			pl->smooth(1, 2);
			pl->pts.back() = pl->pts.front();
			pl->build();
			return std::function<glm::vec2(float)>([pl, x0, x1, z0, z1](float u) { return glm::clamp(pl->at(u), glm::vec2(x0, z0), glm::vec2(x1, z1)); });
		};
		struct Poi { float s; float w; glm::vec2 pos; };
		std::vector<Poi> pois;
		bool pace = false, cornerSlow = false, aimAtPoi = false;
		float slowSpeed = 0.4f, fastSpeed = 1.5f;

		// busiest / most interesting spots: object clusters and rough terrain, spaced apart from each other
		auto hotspots = [&](int K, float wo, float wt, float bin)
		{
			struct B { glm::vec2 sum = glm::vec2(0); int n = 0; };
			std::map<std::pair<int, int>, B> bins;
			mv->map->rootNode->traverse([&](Node* n)
			{
				if (!n->getComponent<RswModel>()) return;
				auto o = n->getComponent<RswObject>();
				if (!o) return;
				glm::vec2 p(gnd->width * 5.0f + o->position.x, gnd->height * 5.0f - o->position.z + 10.0f);
				if (p.x < x0 || p.x > x1 || p.y < z0 || p.y > z1) return;
				auto& b = bins[{(int)(p.x / bin), (int)(p.y / bin)}];
				b.sum += p;
				b.n++;
			});
			int maxN = 1;
			for (auto& b : bins) maxN = glm::max(maxN, b.second.n);
			struct Cell { float obj, rough; glm::vec2 pos; };
			std::vector<Cell> cells;
			float maxRough = 1.0f;
			for (int iz = (int)(z0 / bin); iz <= (int)(z1 / bin); iz++)
				for (int ix = (int)(x0 / bin); ix <= (int)(x1 / bin); ix++)
				{
					glm::vec2 cc((ix + 0.5f) * bin, (iz + 0.5f) * bin);
					if (cc.x < x0 || cc.x > x1 || cc.y < z0 || cc.y > z1) continue;
					Cell cell{ 0, 0, cc };
					auto it = bins.find({ ix, iz });
					if (it != bins.end() && it->second.n > 0)
					{
						cell.obj = it->second.n / (float)maxN;
						cell.pos = it->second.sum / (float)it->second.n;
					}
					if (wt > 0)
					{
						float lo = 1e30f, hi = -1e30f;
						const glm::vec2 offs[5] = { {0,0},{1,1},{-1,1},{1,-1},{-1,-1} };
						for (auto& o : offs)
						{
							float g = groundAt(gnd, cc.x + o.x * bin / 3, cc.y + o.y * bin / 3, cg);
							lo = glm::min(lo, g); hi = glm::max(hi, g);
						}
						cell.rough = hi - lo;
						maxRough = glm::max(maxRough, cell.rough);
					}
					cells.push_back(cell);
				}
			std::vector<std::pair<float, glm::vec2>> scored;
			for (auto& cell : cells)
				scored.push_back({ wo * cell.obj + wt * cell.rough / maxRough, cell.pos });
			std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
			std::vector<std::pair<float, glm::vec2>> out;
			for (auto& sc : scored)
			{
				if (sc.first <= 0.0001f || (int)out.size() >= K) break;
				bool tooClose = false;
				for (auto& o : out) if (glm::distance(o.second, sc.second) < bin * 1.6f) tooClose = true;
				if (!tooClose) out.push_back(sc);
			}
			return out;
		};
		// registers the waypoints (already in path order) as slow-motion spots
		auto addPois = [&](const std::vector<std::pair<float, glm::vec2>>& ordered)
		{
			float mx = 0.0001f;
			for (auto& o : ordered) mx = glm::max(mx, o.first);
			for (size_t k = 0; k < ordered.size(); k++)
				pois.push_back({ poly.cum.back() > 0 ? poly.cum[k] / poly.cum.back() : 0.0f, 0.5f + 0.5f * ordered[k].first / mx, ordered[k].second });
		};
		// the spot that is first in the travel direction (or the one farthest from the centre when the start side is automatic)
		auto startIndex = [&](const std::vector<std::pair<float, glm::vec2>>& v)
		{
			size_t best = 0;
			for (size_t k = 1; k < v.size(); k++)
			{
				if (gp.startSide != 0 ? glm::dot(v[k].second, dirV) < glm::dot(v[best].second, dirV)
					: glm::distance(v[k].second, c) > glm::distance(v[best].second, c))
					best = k;
			}
			return best;
		};
		if (preset == 1)	// city: tour the busiest clusters of objects, slowing down in each of them
		{
			auto spots = hotspots(8, 1.0f, 0.15f, 150.0f);
			if (spots.size() < 3)
			{
				msg = "Not enough objects for the city preset, used the field preset instead.";
				preset = 0;
			}
			else
			{
				size_t startI = startIndex(spots);
				std::vector<std::pair<float, glm::vec2>> ordered;
				ordered.push_back(spots[startI]);
				spots.erase(spots.begin() + startI);
				while (!spots.empty())
				{
					size_t best = 0;
					for (size_t k = 1; k < spots.size(); k++)
						if (glm::distance(spots[k].second, ordered.back().second) < glm::distance(spots[best].second, ordered.back().second))
							best = k;
					ordered.push_back(spots[best]);
					spots.erase(spots.begin() + best);
				}
				for (auto& o : ordered) poly.pts.push_back(o.second);
				if (gp.loop) { poly.pts.push_back(ordered[0].second); loop = true; }
				poly.build();
				addPois(ordered);
				poly.smooth(1, 1);
				poly.build();
				groundRadius = 30.0f;
				pace = true; aimAtPoi = true; slowSpeed = 0.45f; fastSpeed = 1.5f;
			}
		}
		if (preset == 2)	// dungeon: walk the walkable cells (breadth first search between waypoints)
		{
			const int gw = gat->width, gh = gat->height;
			auto walkable = [&](int x, int y) {
				if (x < 0 || y < 0 || x >= gw || y >= gh) return false;
				int t = gat->cubes[x][y]->gatType;
				return t == 0 || t == 3 || t == 4 || t == 6;
			};
			auto cellWorld = [&](int x, int y) { return glm::vec2(5.0f * x + 2.5f, 5.0f * gh - 5.0f * y - 2.5f); };
			const float dx0 = W * mg * 0.5f, dx1 = W - dx0, dz0 = D * mg * 0.5f, dz1 = D - dz0;
			std::vector<glm::ivec2> cands;
			for (int y = 4; y < gh; y += 8)
				for (int x = 4; x < gw; x += 8)
				{
					auto w = cellWorld(x, y);
					if (walkable(x, y) && w.x >= dx0 && w.x <= dx1 && w.y >= dz0 && w.y <= dz1)
						cands.push_back({ x, y });
				}
			std::vector<glm::ivec2> chosen;
			if (cands.size() >= 3)
			{
				auto first = std::min_element(cands.begin(), cands.end(), [](auto& a, auto& b) { return a.x < b.x; });
				chosen.push_back(*first);
				while (chosen.size() < 8 && chosen.size() < cands.size())
				{
					float bestD = -1; glm::ivec2 best = cands[0];
					for (auto& cc : cands)
					{
						float md = 1e30f;
						for (auto& ch : chosen) md = glm::min(md, glm::distance(glm::vec2(cc), glm::vec2(ch)));
						if (md > bestD) { bestD = md; best = cc; }
					}
					chosen.push_back(best);
				}
			}
			std::vector<glm::vec2> wpf;
			for (auto& ch : chosen) wpf.push_back(glm::vec2(ch));
			wpf = orderWaypoints(wpf);
			if (gp.loop && wpf.size() > 1) wpf.push_back(wpf[0]);		// walk back to where we started
			std::vector<glm::vec2> route;
			std::vector<size_t> routeWp;
			std::vector<int> prev((size_t)gw * gh);
			for (size_t k = 0; k + 1 < wpf.size(); k++)
			{
				glm::ivec2 a(wpf[k]), b(wpf[k + 1]);
				std::fill(prev.begin(), prev.end(), -2);
				std::queue<glm::ivec2> q;
				q.push(a);
				prev[a.y * gw + a.x] = -1;
				bool found = false;
				while (!q.empty() && !found)
				{
					auto cur = q.front(); q.pop();
					const glm::ivec2 nb[4] = { {1,0},{-1,0},{0,1},{0,-1} };
					for (auto& d : nb)
					{
						glm::ivec2 n = cur + d;
						if (!walkable(n.x, n.y) || prev[n.y * gw + n.x] != -2) continue;
						prev[n.y * gw + n.x] = cur.y * gw + cur.x;
						if (n == b) { found = true; break; }
						q.push(n);
					}
				}
				if (!found) continue;
				std::vector<glm::vec2> leg;
				for (int cur = b.y * gw + b.x; cur != -1; cur = prev[cur])
					leg.push_back(cellWorld(cur % gw, cur / gw));
				std::reverse(leg.begin(), leg.end());
				for (size_t i = 0; i < leg.size(); i += 3)
					route.push_back(leg[i]);
				route.push_back(leg.back());
				routeWp.push_back(route.size() - 1);
			}
			if (route.size() < 4)
			{
				msg = "No connected walkable area found for the dungeon preset, used the field preset instead.";
				preset = 0;
			}
			else
			{
				poly.pts = route;
				poly.build();
				if (gp.loop && !routeWp.empty()) routeWp.pop_back();
				if (gp.loop) loop = true;
				for (auto idx : routeWp)
					pois.push_back({ poly.cum[idx] / glm::max(0.0001f, poly.cum.back()), 1.0f, route[idx] });
				poly.smooth(3, 2);
				poly.build();
				groundRadius = 12.0f;
				pace = true; cornerSlow = true; slowSpeed = 0.35f; fastSpeed = 1.6f;
			}
		}

		if (preset == 1 || preset == 2)
		{
			xzAt = [&](float u) { return poly.at(u); };
		}
		else if (preset == 3)	// orbit
		{
			loop = true; constTarget = true;
			float r = glm::max(20.0f, gp.radius / 100.0f * half);
			xzAt = [=](float u) { float th = -1.5707963f + sgn * 6.2831853f * u; return clampXZ(c + r * glm::vec2(std::cos(th), std::sin(th))); };
		}
		else if (preset == 4)	// diagonal sweep
		{
			glm::vec2 a(x0, z0), b(x1, z1);
			if (!gp.clockwise) std::swap(a.x, b.x);
			xzAt = [=](float u) { return glm::mix(a, b, u); };
			hAt = [](float u) { return 1.0f - 0.3f * u; };
			if (gp.loop) { glm::vec2 lt = glm::normalize(glm::vec2(-(b - a).y, (b - a).x)) * 0.12f * glm::length(b - a); xzAt = makeRace(a, b, lt); hAt = [](float) { return 1.0f; }; loop = true; }
		}
		else if (preset == 5)	// reveal: pull in on the focus point
		{
			constTarget = true;
			float farD = glm::max(40.0f, gp.radius / 100.0f * glm::max(spanX, spanZ) * 0.6f);
			float nearD = farD * 0.15f;
			float th = 0.7853982f * sgn;
			xzAt = [=](float u) { float e = u * u * (3 - 2 * u); float d = glm::mix(farD, nearD, e); return clampXZ(c + d * glm::vec2(std::cos(th + sgn * 0.5f * u), std::sin(th + sgn * 0.5f * u))); };
			hAt = [](float u) { float e = u * u * (3 - 2 * u); return glm::mix(3.0f, 1.0f, e); };
		}
		else if (preset == 6)	// spiral descent
		{
			constTarget = true;
			float rBig = glm::max(40.0f, gp.radius / 100.0f * half), rSmall = rBig * 0.25f;
			xzAt = [=](float u) { float th = -1.5707963f + sgn * 6.2831853f * 1.5f * u; float r = glm::mix(rBig, rSmall, u); return clampXZ(c + r * glm::vec2(std::cos(th), std::sin(th))); };
			hAt = [](float u) { return glm::mix(3.0f, 1.0f, u * u * (3 - 2 * u)); };
		}
		else if (preset == 7)	// crane: rise up and pull back while turning around the focus
		{
			constTarget = true;
			float rBig = glm::max(40.0f, gp.radius / 100.0f * half);
			xzAt = [=](float u) { float e = u * u * (3 - 2 * u); float th = 0.7853982f + sgn * 2.4f * e; float r = glm::mix(rBig * 0.2f, rBig, e); return clampXZ(c + r * glm::vec2(std::cos(th), std::sin(th))); };
			hAt = [](float u) { float e = u * u * (3 - 2 * u); return glm::mix(0.5f, 3.5f, e); };
		}
		else if (preset == 8 || preset == 9 || preset == 10)	// straight passes: side tracking, top-down pan, low fly-by
		{
			float off = preset == 10 ? 0.2f * Ls : 0.0f;
			xzAt = [=](float u) { return clampXZ(c + dirV * ((u - 0.5f) * 2.0f * Lh) + latV * off); };
			if (gp.loop) { xzAt = makeRace(c - dirV * Lh * 0.9f + latV * off, c + dirV * Lh * 0.9f + latV * off, latV * (0.3f * Ls)); loop = true; }
			if (preset == 9) { topDown = true; groundRadius = 60.0f; }
			if (preset == 10) groundRadius = 20.0f;
			if (preset == 8) hAt = [](float) { return 0.9f; };
		}
		else if (preset == 0)	// field: tour the most interesting spots (objects and hills) in a wide arc
		{
			auto spots = hotspots(4, 0.6f, 0.4f, 200.0f);
			if (spots.size() >= 2)
			{
				std::sort(spots.begin(), spots.end(), [&](auto& a, auto& b) {
					return std::atan2(a.second.y - c.y, a.second.x - c.x) < std::atan2(b.second.y - c.y, b.second.x - c.x); });
				if (!gp.clockwise) std::reverse(spots.begin(), spots.end());
				std::rotate(spots.begin(), spots.begin() + startIndex(spots), spots.end());
				poly.pts.clear();
				for (auto& o : spots) poly.pts.push_back(o.second);
				if (gp.loop) { poly.pts.push_back(spots[0].second); loop = true; }
				poly.build();
				addPois(spots);
				poly.smooth(1, 1);
				poly.build();
				xzAt = [&](float u) { return poly.at(u); };
				hAt = [](float u) { return 1.4f - 0.4f * u; };
				groundRadius = 45.0f;
				pace = true; aimAtPoi = true; slowSpeed = 0.5f; fastSpeed = 1.3f;
			}
		}
		if (preset == 0 && !xzAt)	// field: long gently swaying pass over the middle of the map
		{
			glm::vec2 axis = dirV;
			glm::vec2 lat = latV;
			float L = Lh * 2.0f, LsS = Ls * 2.0f;
			xzAt = [=](float u) { return clampXZ(c + axis * ((u - 0.5f) * L) + lat * (std::sin(u * 6.2831853f) * 0.18f * LsS)); };
			if (gp.loop) { xzAt = makeRace(c - axis * L * 0.45f, c + axis * L * 0.45f, lat * (0.3f * LsS * 0.5f)); loop = true; }
			hAt = [](float u) { return 1.4f - 0.5f * u; };
			constTarget = false; loop = false;
		}

		if (gp.loop && constTarget && !loop)		// in and back out
		{
			auto b0 = xzAt; auto h0 = hAt;
			xzAt = [b0](float u) { return b0((1.0f - std::cos(6.2831853f * u)) * 0.5f); };
			hAt = [h0](float u) { return h0((1.0f - std::cos(6.2831853f * u)) * 0.5f); };
			loop = true;
		}
		// slow-motion / fast mix: remap time so the camera crawls near points of interest and in tight turns, and speeds up in between
		std::function<float(float)> slowAt = [](float) { return 0.0f; };
		std::function<float(float)> camY;		// smooth camera altitude (not used for the dungeon, which hugs the floor)
		if (pace && gp.pacing > 0.5f && (!pois.empty() || cornerSlow))
		{
			const int ST = 600;
			const float strength = glm::clamp(gp.pacing / 100.0f, 0.0f, 1.0f);
			std::vector<float> mask(ST + 1, 0.0f), tc(ST + 1, 0.0f);
			for (int k = 0; k <= ST; k++)
			{
				float sp = k / (float)ST, v = 0.0f;
				for (auto& po : pois)
				{
					float d = (sp - po.s) / 0.045f;
					v = glm::max(v, po.w * std::exp(-d * d));
				}
				if (cornerSlow)
				{
					glm::vec2 a = poly.at(glm::max(0.0f, sp - 0.012f)), b = poly.at(sp), cc2 = poly.at(glm::min(1.0f, sp + 0.012f));
					glm::vec2 v1 = b - a, v2 = cc2 - b;
					if (glm::length(v1) > 0.01f && glm::length(v2) > 0.01f)
					{
						float ang = std::acos(glm::clamp(glm::dot(glm::normalize(v1), glm::normalize(v2)), -1.0f, 1.0f));
						v = glm::max(v, glm::clamp(ang / 0.9f, 0.0f, 1.0f) * 0.7f);
					}
				}
				mask[k] = v;
			}
			for (int pass = 0; pass < 3; pass++)		// soften the speed changes
			{
				auto m2 = mask;
				for (int k = 1; k < ST; k++) m2[k] = 0.25f * mask[k - 1] + 0.5f * mask[k] + 0.25f * mask[k + 1];
				mask = m2;
			}
			for (int k = 0; k < ST; k++)
			{
				float spd = glm::mix(1.0f, glm::mix(fastSpeed, slowSpeed, mask[k]), strength);
				tc[k + 1] = tc[k] + 1.0f / spd;
			}
			for (auto& t : tc) t /= tc.back();
			auto sOf = [tc, ST](float tau)
			{
				tau = glm::clamp(tau, 0.0f, 1.0f);
				size_t k = std::upper_bound(tc.begin(), tc.end(), tau) - tc.begin();
				if (k == 0) return 0.0f;
				if (k >= tc.size()) return 1.0f;
				float f = (tau - tc[k - 1]) / glm::max(0.000001f, tc[k] - tc[k - 1]);
				return ((k - 1) + f) / (float)ST;
			};
			auto baseXz = xzAt;
			auto baseH = hAt;
			xzAt = [baseXz, sOf](float tau) { return baseXz(sOf(tau)); };
			slowAt = [mask, sOf, strength, ST](float tau) { return mask[(int)glm::clamp(sOf(tau) * ST, 0.0f, (float)ST)] * strength; };
			if (aimAtPoi)
				hAt = [baseH, sOf, slowAt](float tau) { return baseH(sOf(tau)) * (1.1f - 0.45f * slowAt(tau)); };
			else
				hAt = [baseH, sOf](float tau) { return baseH(sOf(tau)); };
			// (pois are in path space; remember the mapping for the focus blend below)
			auto poisCopy = pois;
			pois.clear();
			for (auto& po : poisCopy)
			{
				// convert path position back to time so poi positions line up with tau
				float lo = 0, hi = 1;
				for (int it = 0; it < 30; it++) { float mid = 0.5f * (lo + hi); if (sOf(mid) < po.s) lo = mid; else hi = mid; }
				pois.push_back({ 0.5f * (lo + hi), po.w, po.pos });
			}
		}

		const int N = glm::max(4, (int)std::round(gp.duration * gp.keysPerSec));
		std::vector<glm::vec3> P(N + 1);
		for (int i = 0; i <= N; i++)
		{
			float u = i / (float)N;
			glm::vec2 xz = xzAt(u);
			float g = groundMax(gnd, xz.x, xz.y, groundRadius, cg);
			P[i] = glm::vec3(xz.x, g + gp.height * hAt(u), xz.y);
		}
		if (preset == 2)
		{
			for (int i = 1; i < N; i++)
				P[i].y = glm::max(P[i].y * 0.5f + 0.25f * (P[i - 1].y + P[i + 1].y), groundAt(gnd, P[i].x, P[i].z, cg) + gp.height * 0.3f);
		}
		else if (!constTarget || preset == 3 || preset == 6 || preset == 7 || preset == 5)
		{
			// natural, steady altitude: take the highest ground in a window, then average over the same window.
			// The average of the envelope never drops below the ground, and hills turn into long gentle swells instead of bobbing.
			const int K = glm::max(150, N * 6);
			std::vector<float> yr(K + 1), env(K + 1), sm(K + 1);
			for (int k = 0; k <= K; k++)
			{
				float tau = k / (float)K;
				glm::vec2 xz = xzAt(tau);
				yr[k] = groundMax(gnd, xz.x, xz.y, glm::max(groundRadius, 25.0f), cg) + gp.height * hAt(tau);
			}
			int hw = glm::max(2, (int)std::round(K * glm::clamp(3.0f / gp.duration, 0.03f, 0.2f)));
			if (loop) yr[K] = yr[0];
			auto wrapI = [&](int j) { return loop ? ((j % K) + K) % K : glm::clamp(j, 0, K); };
			for (int k = 0; k <= K; k++)
			{
				float m = -1e30f;
				for (int j = k - 2 * hw; j <= k + 2 * hw; j++)
				{
					if (!loop && (j < 0 || j > K)) continue;
					m = glm::max(m, yr[wrapI(j)]);
				}
				env[k] = m;
			}
			for (int pass = 0; pass < 2; pass++)
			{
				for (int k = 0; k <= K; k++)
				{
					float sum = 0; int n = 0;
					for (int j = k - hw; j <= k + hw; j++)
					{
						if (!loop && (j < 0 || j > K)) continue;
						sum += env[wrapI(j)]; n++;
					}
					sm[k] = sum / n;
				}
				if (loop) sm[K] = sm[0];
				env = sm;
			}
			camY = [sm, K](float tau)
			{
				float f = glm::clamp(tau, 0.0f, 1.0f) * K;
				int k0 = glm::min(K - 1, (int)f);
				return glm::mix(sm[k0], sm[k0 + 1], f - k0);
			};
			for (int i = 0; i <= N; i++)
				P[i].y = camY(i / (float)N);
		}
		if (loop) P[N] = P[0];

		auto& posTrack = rsw->cinematicTracks[0];
		auto& tgtTrack = rsw->cinematicTracks[1];
		posTrack.frames.clear();
		tgtTrack.frames.clear();
		for (int i = 0; i <= N; i++)
		{
			glm::vec3 m;
			if (loop && i < N) m = (P[(i + 1) % N] - P[(i - 1 + N) % N]) * 0.5f;
			else if (loop) m = (P[1] - P[N - 1]) * 0.5f;
			else if (i == 0) m = P[1] - P[0];
			else if (i == N) m = P[N] - P[N - 1];
			else m = (P[i + 1] - P[i - 1]) * 0.5f;
			posTrack.frames.push_back(new PosFrame(gp.duration * i / (float)N, std::pair<glm::vec3, glm::vec3>(P[i], m)));
		}
		if (constTarget)
		{
			glm::vec3 tp(c.x, cg + 8.0f, c.y);
			tgtTrack.frames.push_back(new TgtFrame(0.0f, Rsw::CameraTarget(tp, 1.0f)));
			tgtTrack.frames.push_back(new TgtFrame(gp.duration, Rsw::CameraTarget(tp, 1.0f)));
		}
		else
		{
			const int M = glm::max(2, (int)std::ceil(gp.duration / 0.5f));
			const float la = glm::clamp(3.0f / gp.duration, 0.02f, 0.2f);
			const float lookDist = glm::max(60.0f, gp.height * 1.6f);
			for (int j = 0; j <= M; j++)
			{
				float u = j / (float)M;
				glm::vec2 here = xzAt(u);
				auto wrapU = [&](float v) { return loop ? v - std::floor(v) : glm::clamp(v, 0.0f, 1.0f); };
				glm::vec2 hd = xzAt(wrapU(u + 0.03f)) - xzAt(wrapU(u - 0.03f));
				hd = glm::length(hd) > 0.001f ? glm::normalize(hd) : dirV;
				glm::vec2 q = xzAt(wrapU(u + la));
				glm::vec3 tp(q.x, groundAt(gnd, q.x, q.y, cg) + gp.height * 0.2f, q.y);
				// gentle, slowly varying downward pitch (a few degrees of drift) instead of chasing the terrain
				const float pitch = glm::radians(20.0f + 4.0f * std::sin(6.2831853f * u));
				if (camY)
					tp.y = camY(u) - glm::max(40.0f, glm::distance(q, here)) * std::tan(pitch);
				if (topDown)
				{
					glm::vec2 q2 = xzAt(wrapU(u + 0.01f));
					tp = glm::vec3(q2.x, groundAt(gnd, q2.x, q2.y, cg), q2.y);
				}
				else if (gp.look == 0)
				{
					if (!loop && u + la > 1.0f)		// near the end keep looking along the heading
						tp = glm::vec3(here.x + hd.x * 60.0f, tp.y, here.y + hd.y * 60.0f);
					if (aimAtPoi && !pois.empty())		// drift the view towards the spot we are passing
					{
						size_t bi = 0;
						for (size_t k = 1; k < pois.size(); k++) if (std::abs(pois[k].s - u) < std::abs(pois[bi].s - u)) bi = k;
						float k = glm::clamp(slowAt(u) * 0.6f, 0.0f, 0.6f);
						glm::vec3 focus(pois[bi].pos.x, groundAt(gnd, pois[bi].pos.x, pois[bi].pos.y, cg) + gp.height * 0.1f, pois[bi].pos.y);
						tp = glm::mix(tp, focus, k);
					}
				}
				else
				{
					float ang = 0.0f;
					if (gp.look == 1) ang = 1.5707963f;
					else if (gp.look == 2) ang = -1.5707963f;
					else if (gp.look == 3) ang = 3.14159265f;
					else if (gp.look == 5) ang = (u - 0.5f) * 2.0f * 1.15f * sgn;		// slow pan left to right
					glm::vec2 dir2(hd.x * std::cos(ang) - hd.y * std::sin(ang), hd.x * std::sin(ang) + hd.y * std::cos(ang));
					glm::vec2 tq = here + dir2 * lookDist;
					tp = glm::vec3(tq.x, groundAt(gnd, tq.x, tq.y, cg) + gp.height * 0.2f, tq.y);
					if (camY)
						tp.y = camY(u) - lookDist * std::tan(pitch);
					if (gp.look == 4 && !pois.empty())		// circle around the nearest spot
					{
						size_t bi = 0;
						for (size_t k = 1; k < pois.size(); k++) if (glm::distance(pois[k].pos, here) < glm::distance(pois[bi].pos, here)) bi = k;
						tp = glm::vec3(pois[bi].pos.x, groundAt(gnd, pois[bi].pos.x, pois[bi].pos.y, cg) + gp.height * 0.1f, pois[bi].pos.y);
					}
				}
				tgtTrack.frames.push_back(new TgtFrame(gp.duration * u, Rsw::CameraTarget(tp, 1.0f)));
			}
		}
		rsw->cinematicLength = gp.duration;
		return msg;
	}
}

float scale = 100;
float rowHeight = 25;
float leftAreaSize = 200;
float timeSelected = 1;
int selectedKeyFrame = -1;
int selectedTrack = -1;
bool playing = true;

// ======================================================================================================
// Video export (cinematic mode): non-blocking recorder, ffmpeg + H.264 / hardware encoders, MP4 by default
// ======================================================================================================
#include <thread>
#include <mutex>
#include <atomic>
#include <deque>
#include <cmath>
#include <chrono>
#include <fstream>

namespace videoexport
{
	enum class Enc { X264, X265, Nvenc, Qsv, Amf };

	struct Settings
	{
		int encoder = 0;			// 0 auto, 1 x264, 2 x265, 3 nvenc, 4 qsv, 5 amf
		int quality = 1;			// 0 draft, 1 good, 2 high
		int resPreset = 1;			// 0 720p, 1 1080p, 2 1440p, 3 4K, 4 custom
		int resolution[2] = { 1920, 1080 };
		int fpsPreset = 1;			// 0 24, 1 30, 2 60, 3 custom
		float fps = 30;
		std::string file = "video.mp4";
		bool openWhenDone = true;
		int loops = 1;
		float rangeStart = 0;
		float rangeEnd = 0;			// 0 = to the end of the cinematic
	};
	Settings settings;
	bool settingsLoaded = false;

	void saveSettings()
	{
		nlohmann::json j;
		j["encoder"] = settings.encoder; j["quality"] = settings.quality; j["resPreset"] = settings.resPreset;
		j["resolution"] = { settings.resolution[0], settings.resolution[1] };
		j["fpsPreset"] = settings.fpsPreset; j["fps"] = settings.fps; j["file"] = settings.file;
		j["openWhenDone"] = settings.openWhenDone; j["loops"] = settings.loops;
		std::ofstream out("video_export.json");
		if (out.is_open()) out << j.dump(1);
	}
	void loadSettings()
	{
		settingsLoaded = true;
		try
		{
			std::ifstream in("video_export.json");
			if (!in.is_open()) return;
			nlohmann::json j; in >> j;
			settings.encoder = j.value("encoder", settings.encoder); settings.quality = j.value("quality", settings.quality);
			settings.resPreset = j.value("resPreset", settings.resPreset);
			if (j.contains("resolution")) { settings.resolution[0] = j["resolution"][0]; settings.resolution[1] = j["resolution"][1]; }
			settings.fpsPreset = j.value("fpsPreset", settings.fpsPreset); settings.fps = j.value("fps", settings.fps);
			settings.file = j.value("file", settings.file); settings.openWhenDone = j.value("openWhenDone", settings.openWhenDone);
			settings.loops = j.value("loops", settings.loops);
		}
		catch (...) {}
	}

	std::string quoted(const std::string& s) { return "\"" + s + "\""; }

	// runs a command line, collects stdout+stderr, returns the exit code (-1 when it could not be started)
	int runProcess(const std::string& cmd, std::string& out, int timeoutMs)
	{
		SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES) };
		sa.bInheritHandle = TRUE;
		HANDLE r = nullptr, w = nullptr;
		if (!CreatePipe(&r, &w, &sa, 0)) return -1;
		SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOA si = { sizeof(STARTUPINFOA) };
		si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;
		si.hStdOutput = w; si.hStdError = w;
		PROCESS_INFORMATION pi = { 0 };
		std::string c = cmd;
		if (!CreateProcessA(nullptr, c.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			CloseHandle(r); CloseHandle(w);
			return -1;
		}
		CloseHandle(w);
		char buf[2048]; DWORD n;
		while (ReadFile(r, buf, sizeof(buf), &n, nullptr) && n > 0)
			out.append(buf, n);
		WaitForSingleObject(pi.hProcess, timeoutMs);
		DWORD code = 1;
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(r); CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
		return (int)code;
	}

	std::string ffmpegExe(BrowEdit* be) { return quoted(be->config.ffmpegPath.empty() ? "ffmpeg" : be->config.ffmpegPath); }

	std::string encoderName(Enc e) { return e == Enc::X264 ? "libx264" : e == Enc::X265 ? "libx265" : e == Enc::Nvenc ? "h264_nvenc" : e == Enc::Qsv ? "h264_qsv" : "h264_amf"; }

	// codec arguments and the pixel format conversion for each encoder and quality (0 draft, 1 good, 2 high)
	std::string encoderArgs(Enc e, int q, bool mp4like)
	{
		q = glm::clamp(q, 0, 2);
		static const char* x264Preset[] = { "ultrafast", "veryfast", "faster" };
		static const int x264Crf[] = { 30, 23, 18 };
		static const char* x265Preset[] = { "ultrafast", "veryfast", "fast" };
		static const int x265Crf[] = { 32, 26, 21 };
		static const char* nvPreset[] = { "p3", "p4", "p5" };
		static const int nvCq[] = { 32, 26, 20 };
		static const int qsvQ[] = { 30, 24, 19 };
		static const char* amfQuality[] = { "speed", "balanced", "quality" };
		static const int amfQp[] = { 30, 24, 19 };
		std::string a;
		switch (e)
		{
		case Enc::X264: a = "-vf vflip,format=yuv420p -c:v libx264 -preset " + std::string(x264Preset[q]) + " -crf " + std::to_string(x264Crf[q]); break;
		case Enc::X265: a = "-vf vflip,format=yuv420p -c:v libx265 -preset " + std::string(x265Preset[q]) + " -crf " + std::to_string(x265Crf[q]) + (mp4like ? " -tag:v hvc1" : ""); break;
		case Enc::Nvenc: a = "-vf vflip,format=yuv420p -c:v h264_nvenc -preset " + std::string(nvPreset[q]) + " -rc vbr -cq " + std::to_string(nvCq[q]) + " -b:v 0"; break;
		case Enc::Qsv: a = "-vf vflip,format=nv12 -c:v h264_qsv -preset veryfast -global_quality " + std::to_string(qsvQ[q]); break;
		case Enc::Amf: a = "-vf vflip,format=yuv420p -c:v h264_amf -quality " + std::string(amfQuality[q]) + " -rc cqp -qp_i " + std::to_string(amfQp[q]) + " -qp_p " + std::to_string(amfQp[q] + 2) + " -qp_b " + std::to_string(amfQp[q] + 2); break;
		}
		return a;
	}

	// what this ffmpeg build can actually encode (hardware encoders are tested with a tiny encode)
	struct Detected
	{
		std::atomic<int> state{ 0 };		// 0 not started, 1 checking, 2 done
		bool ffmpegFound = false;
		bool has[5] = { false, false, false, false, false };	// X264, X265, Nvenc, Qsv, Amf
		std::string note;
	};
	Detected detected;

	void startDetection(BrowEdit* be)
	{
		if (detected.state.load() != 0) return;
		detected.state = 1;
		std::string exe = ffmpegExe(be);
		std::thread([exe]()
		{
			std::string list;
			int code = runProcess(exe + " -hide_banner -encoders", list, 15000);
			detected.ffmpegFound = code >= 0 && list.find("Encoders") != std::string::npos;
			if (detected.ffmpegFound)
			{
				detected.has[0] = list.find(" libx264 ") != std::string::npos;
				detected.has[1] = list.find(" libx265 ") != std::string::npos;
				const Enc hw[3] = { Enc::Nvenc, Enc::Qsv, Enc::Amf };
				for (int i = 0; i < 3; i++)
				{
					if (list.find(" " + encoderName(hw[i]) + " ") == std::string::npos) continue;
					std::string out;
					std::string cmd = exe + " -hide_banner -loglevel error -nostdin -f lavfi -i color=c=black:s=320x240:r=30:d=0.3 " + encoderArgs(hw[i], 1, true) + " -f null -";
					// the test source is not rgb, but the vf chain converts anyway
					detected.has[2 + i] = runProcess(cmd, out, 15000) == 0;
				}
			}
			detected.state = 2;
		}).detach();
	}

	// ---------------------------------------------------------------------------------------------------
	struct Recorder
	{
		enum class State { Idle, Recording, Flushing, Done, Failed, Cancelled } state = State::Idle;
		HANDLE procHandle = nullptr, procThread = nullptr, stdinWrite = nullptr, logRead = nullptr;
		std::thread writer, logger;
		std::mutex qMutex;
		std::deque<std::vector<char>> queue;
		std::atomic<bool> writerFinish{ false }, writerFailed{ false }, writerDone{ false }, cancel{ false };
		std::mutex logMutex;
		std::string log;
		GLuint pbo[2] = { 0, 0 };
		int W = 0, H = 0, frame = 0, totalFrames = 0, pending = 0;
		float fps = 30, rangeStart = 0, span = 1;
		float savedTime = 0; bool savedPlaying = false, savedPreview = false;
		std::chrono::steady_clock::time_point startedAt;
		std::string outFile, encoderUsed, message;
		bool openWhenDone = true;
		size_t frameBytes = 0;
	};
	Recorder rec;

	void cleanupHandles()
	{
		if (rec.stdinWrite) { CloseHandle(rec.stdinWrite); rec.stdinWrite = nullptr; }
		if (rec.writer.joinable()) rec.writer.join();
		if (rec.logger.joinable()) rec.logger.join();
		if (rec.logRead) { CloseHandle(rec.logRead); rec.logRead = nullptr; }
		if (rec.procHandle) { CloseHandle(rec.procHandle); rec.procHandle = nullptr; }
		if (rec.procThread) { CloseHandle(rec.procThread); rec.procThread = nullptr; }
		if (rec.pbo[0]) { glDeleteBuffers(2, rec.pbo); rec.pbo[0] = rec.pbo[1] = 0; }
	}

	void restoreView(MapView& mv)
	{
		timeSelected = rec.savedTime;
		playing = rec.savedPlaying;
		mv.cinematicPlay = rec.savedPreview;
	}

	std::string logTail()
	{
		std::lock_guard<std::mutex> l(rec.logMutex);
		return rec.log.size() > 700 ? rec.log.substr(rec.log.size() - 700) : rec.log;
	}

	bool startRecording(BrowEdit* be, Rsw* rsw, std::string& error)
	{
		auto& s = settings;
		auto& mv = *be->activeMapView;
		if (detected.state.load() != 2 || !detected.ffmpegFound) { error = "ffmpeg was not found. Set its path in the config window (ffmpeg path)."; return false; }
		// pick the encoder
		Enc enc = Enc::X264;
		bool ok = false;
		auto pick = [&](Enc e, int idx) { if (!ok && detected.has[idx]) { enc = e; ok = true; } };
		switch (s.encoder)
		{
		case 1: pick(Enc::X264, 0); break;
		case 2: pick(Enc::X265, 1); break;
		case 3: pick(Enc::Nvenc, 2); break;
		case 4: pick(Enc::Qsv, 3); break;
		case 5: pick(Enc::Amf, 4); break;
		default: pick(Enc::Nvenc, 2); pick(Enc::Qsv, 3); pick(Enc::Amf, 4); pick(Enc::X264, 0); pick(Enc::X265, 1); break;
		}
		if (!ok) { error = "The selected encoder is not available in this ffmpeg build."; return false; }

		rec.W = glm::max(16, s.resolution[0] & ~1);
		rec.H = glm::max(16, s.resolution[1] & ~1);
		rec.fps = glm::max(1.0f, s.fps);
		rec.rangeStart = glm::clamp(s.rangeStart, 0.0f, rsw->cinematicLength);
		float end = s.rangeEnd > rec.rangeStart ? glm::min(s.rangeEnd, rsw->cinematicLength) : rsw->cinematicLength;
		rec.span = glm::max(0.05f, end - rec.rangeStart);
		rec.totalFrames = glm::max(1, (int)std::ceil(rec.fps * rec.span * glm::max(1, s.loops)));
		rec.frameBytes = (size_t)rec.W * rec.H * 3;
		rec.outFile = s.file.empty() ? "video.mp4" : s.file;
		rec.openWhenDone = s.openWhenDone;
		std::string ext = rec.outFile.find_last_of('.') == std::string::npos ? "" : rec.outFile.substr(rec.outFile.find_last_of('.'));
		for (auto& c : ext) c = (char)tolower(c);
		if (ext.empty()) { rec.outFile += ".mp4"; ext = ".mp4"; }
		bool mp4like = ext == ".mp4" || ext == ".mov" || ext == ".m4v";

		std::string cmd = ffmpegExe(be) + " -y -hide_banner -loglevel warning -nostats -f rawvideo -pix_fmt rgb24 -s " + std::to_string(rec.W) + "x" + std::to_string(rec.H)
			+ " -framerate " + std::to_string(rec.fps) + " -i - " + encoderArgs(enc, s.quality, mp4like) + (mp4like ? " -movflags +faststart" : "") + " " + quoted(rec.outFile);

		SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES) };
		sa.bInheritHandle = TRUE;
		HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
		if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) { error = "Could not create pipes for ffmpeg."; return false; }
		SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
		STARTUPINFOA si = { sizeof(STARTUPINFOA) };
		si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
		si.wShowWindow = SW_HIDE;
		si.hStdInput = inR; si.hStdOutput = outW; si.hStdError = outW;
		PROCESS_INFORMATION pi = { 0 };
		if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
		{
			CloseHandle(inR); CloseHandle(inW); CloseHandle(outR); CloseHandle(outW);
			error = "Could not start ffmpeg.";
			return false;
		}
		CloseHandle(inR); CloseHandle(outW);
		rec.procHandle = pi.hProcess; rec.procThread = pi.hThread; rec.stdinWrite = inW; rec.logRead = outR;
		rec.log.clear();
		rec.writerFinish = false; rec.writerFailed = false; rec.writerDone = false; rec.cancel = false;
		rec.queue.clear();
		rec.logger = std::thread([]()
		{
			char buf[1024]; DWORD n;
			while (ReadFile(rec.logRead, buf, sizeof(buf), &n, nullptr) && n > 0)
			{
				std::lock_guard<std::mutex> l(rec.logMutex);
				rec.log.append(buf, n);
				if (rec.log.size() > 8192) rec.log.erase(0, rec.log.size() - 4096);
			}
		});
		rec.writer = std::thread([]()
		{
			while (true)
			{
				std::vector<char> f;
				{
					std::lock_guard<std::mutex> l(rec.qMutex);
					if (!rec.queue.empty()) { f = std::move(rec.queue.front()); rec.queue.pop_front(); }
				}
				if (f.empty())
				{
					if (rec.writerFinish.load()) break;
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				size_t offset = 0;
				while (offset < f.size())
				{
					DWORD written = 0;
					if (!WriteFile(rec.stdinWrite, f.data() + offset, (DWORD)(f.size() - offset), &written, nullptr) || written == 0)
					{
						rec.writerFailed = true;
						rec.writerDone = true;
						return;
					}
					offset += written;
				}
			}
			rec.writerDone = true;
		});

		glGenBuffers(2, rec.pbo);
		for (int i = 0; i < 2; i++)
		{
			glBindBuffer(GL_PIXEL_PACK_BUFFER, rec.pbo[i]);
			glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)rec.frameBytes, nullptr, GL_STREAM_READ);
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

		rec.savedTime = timeSelected; rec.savedPlaying = playing; rec.savedPreview = mv.cinematicPlay;
		playing = false;
		rec.frame = 0; rec.pending = 0;
		rec.encoderUsed = encoderName(enc);
		rec.startedAt = std::chrono::steady_clock::now();
		rec.state = Recorder::State::Recording;
		return true;
	}

	void pushPbo(int idx)
	{
		glBindBuffer(GL_PIXEL_PACK_BUFFER, rec.pbo[idx]);
		void* p = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)rec.frameBytes, GL_MAP_READ_BIT);
		if (p)
		{
			std::vector<char> f(rec.frameBytes);
			memcpy(f.data(), p, rec.frameBytes);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
			std::lock_guard<std::mutex> l(rec.qMutex);
			rec.queue.push_back(std::move(f));
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
	}

	void abort(MapView& mv, Recorder::State st, const std::string& msg, bool deleteFile)
	{
		rec.cancel = true;
		if (rec.stdinWrite) { CloseHandle(rec.stdinWrite); rec.stdinWrite = nullptr; }
		if (rec.procHandle) TerminateProcess(rec.procHandle, 1);
		rec.writerFinish = true;
		cleanupHandles();
		if (deleteFile) DeleteFileA(rec.outFile.c_str());
		restoreView(mv);
		rec.state = st;
		rec.message = msg;
	}

	// renders as many frames as fit in a few milliseconds, so the editor stays responsive while recording
	void tick(BrowEdit* be)
	{
		if (rec.state != Recorder::State::Recording && rec.state != Recorder::State::Flushing) return;
		auto& mv = *be->activeMapView;
		if (rec.writerFailed.load())
		{
			abort(mv, Recorder::State::Failed, "ffmpeg stopped while recording.\n" + logTail(), true);
			return;
		}
		if (rec.cancel.load() && rec.state == Recorder::State::Recording)
		{
			abort(mv, Recorder::State::Cancelled, "Recording cancelled.", true);
			return;
		}
		if (rec.state == Recorder::State::Recording)
		{
			auto t0 = std::chrono::steady_clock::now();
			while (rec.frame < rec.totalFrames)
			{
				{
					std::lock_guard<std::mutex> l(rec.qMutex);
					if (rec.queue.size() >= 6) break;		// the encoder is behind, wait a moment
				}
				if (mv.fbo->getWidth() != rec.W || mv.fbo->getHeight() != rec.H)
					mv.fbo->resize(rec.W, rec.H);
				float t = rec.rangeStart + (float)std::fmod(rec.frame / rec.fps, rec.span);
				timeSelected = t;
				mv.cinematicPlay = true;
				mv.render(be);
				// asynchronous readback: this frame goes into one buffer while the previous one is copied out of the other
				int cur = rec.frame % 2;
				mv.fbo->use();
				glPixelStorei(GL_PACK_ALIGNMENT, 1);
				glBindBuffer(GL_PIXEL_PACK_BUFFER, rec.pbo[cur]);
				glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
				glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
				glPixelStorei(GL_PACK_ALIGNMENT, 4);
				if (rec.frame > 0)
					pushPbo((rec.frame - 1) % 2);
				rec.frame++;
				if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > 25.0)
					break;
			}
			if (rec.frame >= rec.totalFrames)
			{
				pushPbo((rec.frame - 1) % 2);		// the last frame
				rec.writerFinish = true;
				rec.state = Recorder::State::Flushing;
			}
		}
		else if (rec.state == Recorder::State::Flushing)
		{
			if (rec.writerDone.load())
			{
				if (rec.stdinWrite) { CloseHandle(rec.stdinWrite); rec.stdinWrite = nullptr; }
				if (WaitForSingleObject(rec.procHandle, 0) == WAIT_OBJECT_0)
				{
					DWORD code = 1;
					GetExitCodeProcess(rec.procHandle, &code);
					std::string tail = logTail();
					cleanupHandles();
					restoreView(mv);
					if (code == 0)
					{
						rec.state = Recorder::State::Done;
						rec.message = "Saved " + rec.outFile;
						if (rec.openWhenDone)
						{
							std::string c = "explorer.exe /select," + quoted(rec.outFile);
							STARTUPINFOA si = { sizeof(STARTUPINFOA) };
							PROCESS_INFORMATION pi = { 0 };
							if (CreateProcessA(nullptr, c.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
						}
					}
					else
					{
						rec.state = Recorder::State::Failed;
						rec.message = "ffmpeg failed (code " + std::to_string(code) + ").\n" + tail;
					}
				}
			}
		}
	}

	// settings window + progress window
	void popups(BrowEdit* be, Rsw* rsw)
	{
		auto& s = settings;
		if (ImGui::BeginPopupModal("RecordVideo", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			if (!settingsLoaded) loadSettings();
			startDetection(be);
			int st = detected.state.load();
			if (st != 2)
				ImGui::TextDisabled("Checking ffmpeg and the available encoders...");
			else if (!detected.ffmpegFound)
				ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "ffmpeg was not found. Set its path in the config window (ffmpeg path).");
			else
				ImGui::TextDisabled("Found: %s%s%s%s%s", detected.has[0] ? "H.264 " : "", detected.has[1] ? "H.265 " : "", detected.has[2] ? "NVIDIA " : "", detected.has[3] ? "Intel " : "", detected.has[4] ? "AMD" : "");
			ImGui::SameLine();
			if (ImGui::SmallButton("Re-check")) detected.state = 0;

			ImGui::SetNextItemWidth(300);
			static const char* encNames[] = { "Automatic (hardware if available, otherwise H.264)", "H.264 (CPU, libx264)", "H.265 (CPU, libx265)", "H.264 NVIDIA (NVENC)", "H.264 Intel (QuickSync)", "H.264 AMD (AMF)" };
			ImGui::Combo("Encoder", &s.encoder, encNames, 6);
			ImGui::SetNextItemWidth(300);
			ImGui::Combo("Quality", &s.quality, "Draft (fastest)\0Good\0High (bigger file)\0");
			static const int resW[] = { 1280, 1920, 2560, 3840 }, resH[] = { 720, 1080, 1440, 2160 };
			ImGui::SetNextItemWidth(300);
			if (ImGui::Combo("Resolution", &s.resPreset, "1280 x 720 (720p)\0" "1920 x 1080 (1080p)\0" "2560 x 1440 (1440p)\0" "3840 x 2160 (4K)\0" "Custom\0"))
				if (s.resPreset < 4) { s.resolution[0] = resW[s.resPreset]; s.resolution[1] = resH[s.resPreset]; }
			if (s.resPreset == 4)
			{
				ImGui::SetNextItemWidth(300);
				ImGui::InputInt2("Size", s.resolution);
			}
			static const float fpsV[] = { 24, 30, 60 };
			ImGui::SetNextItemWidth(300);
			if (ImGui::Combo("Frame rate", &s.fpsPreset, "24 fps\0" "30 fps\0" "60 fps\0" "Custom\0"))
				if (s.fpsPreset < 3) s.fps = fpsV[s.fpsPreset];
			if (s.fpsPreset == 3)
			{
				ImGui::SetNextItemWidth(300);
				ImGui::DragFloat("Fps", &s.fps, 1, 1, 240);
			}
			ImGui::SetNextItemWidth(140);
			ImGui::DragFloat("From (s)", &s.rangeStart, 0.1f, 0, rsw->cinematicLength);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(140);
			ImGui::DragFloat("To (s, 0 = end)", &s.rangeEnd, 0.1f, 0, rsw->cinematicLength);
			ImGui::SetNextItemWidth(140);
			ImGui::InputInt("Loops", &s.loops);
			s.loops = glm::clamp(s.loops, 1, 50);
			ImGui::SetNextItemWidth(300);
			ImGui::InputText("File", &s.file);
			ImGui::SameLine();
			if (ImGui::Button("Browse##videofile"))
			{
				auto path = util::SaveAsDialog(s.file, "MP4 video\0*.mp4\0Matroska video\0*.mkv\0QuickTime\0*.mov\0");
				if (!path.empty()) s.file = path;
			}
			ImGui::Checkbox("Show the file in the explorer when done", &s.openWhenDone);
			float seconds = (s.rangeEnd > s.rangeStart ? glm::min(s.rangeEnd, rsw->cinematicLength) - s.rangeStart : rsw->cinematicLength - s.rangeStart) * s.loops;
			ImGui::TextDisabled("%d frames, %.1f s of video", (int)std::ceil(glm::max(0.0f, seconds) * s.fps), glm::max(0.0f, seconds));
			ImGui::Separator();
			static std::string startError;
			bool canRecord = st == 2 && detected.ffmpegFound;
			if (!canRecord) ImGui::BeginDisabled();
			if (ImGui::Button("Record", ImVec2(120, 0)))
			{
				saveSettings();
				if (startRecording(be, rsw, startError))
				{
					startError.clear();
					ImGui::CloseCurrentPopup();
					ImGui::OpenPopup("RecordingProgress");
				}
			}
			if (!canRecord) ImGui::EndDisabled();
			ImGui::SameLine();
			if (ImGui::Button("Close", ImVec2(120, 0))) { saveSettings(); ImGui::CloseCurrentPopup(); }
			if (!startError.empty())
				ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", startError.c_str());
			ImGui::EndPopup();
		}

		if (rec.state == Recorder::State::Recording || rec.state == Recorder::State::Flushing)
			if (!ImGui::IsPopupOpen("RecordingProgress"))
				ImGui::OpenPopup("RecordingProgress");
		if (ImGui::BeginPopupModal("RecordingProgress", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			bool running = rec.state == Recorder::State::Recording || rec.state == Recorder::State::Flushing;
			if (running)
			{
				float progress = rec.totalFrames > 0 ? rec.frame / (float)rec.totalFrames : 0.0f;
				double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - rec.startedAt).count();
				double rate = elapsed > 0.1 ? rec.frame / elapsed : 0.0;
				double eta = rate > 0.01 ? (rec.totalFrames - rec.frame) / rate : 0.0;
				ImGui::Text("%s  -  %dx%d at %.0f fps  -  %s", rec.state == Recorder::State::Flushing ? "Finishing the file..." : "Recording", rec.W, rec.H, rec.fps, rec.encoderUsed.c_str());
				ImGui::ProgressBar(progress, ImVec2(420, 0));
				ImGui::Text("Frame %d / %d   %.1f frames/s   about %d s left", rec.frame, rec.totalFrames, rate, (int)eta);
				if (rec.state == Recorder::State::Recording && ImGui::Button("Cancel", ImVec2(120, 0)))
					rec.cancel = true;
			}
			else
			{
				ImGui::TextWrapped("%s", rec.message.c_str());
				if (ImGui::Button("Close", ImVec2(120, 0)))
				{
					rec.state = Recorder::State::Idle;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::EndPopup();
		}
	}
}



void BrowEdit::showCinematicModeWindow()
{
	if (!activeMapView)
		return;
	auto rsw = activeMapView->map->rootNode->getComponent<Rsw>();

	if (lastRsw != rsw)
	{
		lastRsw = rsw;
		undoStack.clear(); redoStack.clear(); clipboard.clear(); multiSel.clear();
		selectedKeyFrame = -1; selectedTrack = -1;
	}
	if (playing && rsw->cinematicLength > 0)
		timeSelected = (float)std::fmod(timeSelected + ImGui::GetIO().DeltaTime * playSpeed, rsw->cinematicLength);
	while (rsw->cinematicTracks.size() > 0 && rsw->cinematicTracks.size() < 2)
		rsw->cinematicTracks.push_back(Rsw::Track{ rsw->cinematicTracks.size() == 1 ? "Camera Rotation" : "Camera Position" });
	if (rsw->cinematicTracks.size() == 0) // stub data
	{
		Rsw::Track t1{ "Camera Position" };
		Rsw::Track t2{ "Camera Rotation" };
		t1.frames.push_back(new Rsw::KeyFrameData<std::pair<glm::vec3, glm::vec3>>(0, std::pair<glm::vec3, glm::vec3>(glm::vec3(50, 50, 50), glm::vec3(0,0,100))));
		
		t2.frames.push_back(new Rsw::KeyFrameData<Rsw::CameraTarget>(0, Rsw::CameraTarget(glm::vec3(100,10,100), 0.01f)));

		rsw->cinematicTracks.push_back(t1);
		rsw->cinematicTracks.push_back(t2);
	}

	ImGui::Begin("Cinematic Mode");
	if (!activeMapView)
	{
		ImGui::End();
		return;
	}

	ImGui::SetNextItemWidth(150);
	ImGui::DragFloat("##CurrentTime", &timeSelected, 0.1f, 0, 10000.0f);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Animation time");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(150);
	ImGui::DragFloat("##Length", &rsw->cinematicLength, 0.1f, 0, 10000.0f);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Animation length");
	ImGui::SameLine();
	if (toolBarButton("Prev", ICON_PREV, "Previous Keyframe", ImVec4(1,1,1,1)))
	{
		if (selectedTrack >= 0 && selectedTrack < rsw->cinematicTracks.size() && selectedKeyFrame > 0)
		{
			selectedKeyFrame--;
			multiSel.clear();
			timeSelected = rsw->cinematicTracks[selectedTrack].frames[selectedKeyFrame]->time;
		}
	}
	ImGui::SameLine();
	if (toolBarButton("PlayPause", playing ? ICON_PAUSE : ICON_PLAY, "Play / Pause", ImVec4(1, 1, 1, 1)))
		playing = !playing;
	ImGui::SameLine();
	if (toolBarButton("Next", ICON_NEXT, "Nexts Keyframe", ImVec4(1, 1, 1, 1)))
	{
		if (selectedTrack >= 0 && selectedTrack < rsw->cinematicTracks.size() && selectedKeyFrame < (int)rsw->cinematicTracks[selectedTrack].frames.size()-1)
		{
			selectedKeyFrame++;
			multiSel.clear();
			timeSelected = rsw->cinematicTracks[selectedTrack].frames[selectedKeyFrame]->time;
		}
	}
	ImGui::SameLine();
	ImGui::Checkbox("##preview", &activeMapView->cinematicPlay);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Preview animation in main viewport");
	ImGui::SameLine();
	if (toolBarButton("Record", ICON_REC, "Record a video", ImVec4(1, 1, 1, 1)))
	{
		ImGui::OpenPopup("RecordVideo");
	}
	videoexport::tick(this);
	videoexport::popups(this, rsw);


	// ---- extra controls -------------------------------------------------------------------------------
	bool actCopy = false, actPaste = false, actDelete = false, actKeyView = false, actUndo = false, actRedo = false, actGoto = false, actAdd = false;
	float gotoTime = timeSelected;
	{
		auto& io = ImGui::GetIO();
		if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput)
		{
			if (io.KeyCtrl && ImGui::IsKeyPressed(GLFW_KEY_C)) actCopy = true;
			if (io.KeyCtrl && (ImGui::IsKeyPressed(GLFW_KEY_V) || ImGui::IsKeyPressed(GLFW_KEY_D))) actPaste = true;
			if (ImGui::IsKeyPressed(GLFW_KEY_DELETE)) actDelete = true;
		}
		if (ImGui::Button("Key from view")) actKeyView = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fly the editor camera where you want the shot, then press this:\nit adds a camera position + target keyframe at the playhead");
		ImGui::SameLine();
		if (ImGui::Button("Add key")) actAdd = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add keyframes at the playhead that start with the current animated values\n(or double-click a track; new keys copy the value at that time, not zeros)");
		ImGui::SameLine();
		if (ImGui::Button("View shot")) { actGoto = true; gotoTime = timeSelected; }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move the editor camera to the shot at the playhead\n(double-click a keyframe does the same)");
		ImGui::SameLine();
		if (ImGui::Button("Copy")) actCopy = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy selected keyframes (Ctrl+C). Ctrl+click selects several");
		ImGui::SameLine();
		if (ImGui::Button("Paste")) actPaste = true;
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paste at the playhead (Ctrl+V / Ctrl+D)");
		ImGui::SameLine();
		if (ImGui::Button("Delete")) actDelete = true;
		ImGui::SameLine();
		if (ImGui::Button("Undo")) actUndo = true;
		ImGui::SameLine();
		if (ImGui::Button("Redo")) actRedo = true;
		ImGui::SameLine();
		if (ImGui::Button("Draw path...")) { cinedraw::showPanel = true; cinedraw::active = true; }
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Draw the camera path on the map: click to add points, the camera follows the line and turns with it");
		ImGui::SameLine();
		if (ImGui::Button("Generate...")) ImGui::OpenPopup("GenerateCinematic");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Automatic cinematic based on the map (field, city, dungeon, orbit, ...)");

		ImGui::SetNextItemWidth(80);
		ImGui::DragFloat("Play speed", &playSpeed, 0.01f, 0.1f, 4.0f, "%.2fx");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(70);
		static int snapIdx = 0;
		static const float snapValues[] = { 0.0f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f };
		if (ImGui::Combo("Snap", &snapIdx, "Off\0" "0.05 s\0" "0.1 s\0" "0.25 s\0" "0.5 s\0" "1 s\0"))
			snapStep = snapValues[snapIdx];
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120);
		ImGui::SliderFloat("Zoom", &scale, 20.0f, 400.0f, "%.0f");
	}

	if (ImGui::BeginPopupModal("GenerateCinematic", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		static GenParams gp;
		static int lastPreset = 0;
		static std::string resultMsg;
		if (ImGui::Combo("Preset", &gp.preset, "Field (tour of the best spots)\0City (tour of the busiest areas)\0Dungeon (walk the corridors)\0Orbit around the centre\0Diagonal sweep\0Reveal (pull in on a point)\0Spiral descent\0Crane (rise and pull back)\0Side tracking shot\0Top-down pan\0Low fly-by\0City road (street level, spiral at the centre)\0"))
		{
			static const float defH[] = { 90, 60, 22, 80, 50, 60, 70, 40, 45, 250, 25, 20 };
			static const float defD[] = { 40, 45, 45, 30, 25, 20, 30, 20, 30, 30, 18, 70 };
			static const int defLook[] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
			gp.height = defH[gp.preset];
			gp.duration = defD[gp.preset];
			gp.look = defLook[gp.preset];
		}
		ImGui::DragFloat("Duration (s)", &gp.duration, 0.5f, 4.0f, 600.0f);
		ImGui::DragFloat("Camera height", &gp.height, 1.0f, 5.0f, 500.0f);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Height above the ground below the camera.\nRaise it if the camera clips through tall buildings.");
		ImGui::SliderFloat("Edge margin (%)", &gp.margin, 0.0f, 40.0f, "%.0f");
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stays this far away from the map edges and corners, they are rarely worth showing");
		if (gp.preset == 3 || gp.preset == 5 || gp.preset == 6 || gp.preset == 7)
			ImGui::SliderFloat("Size (%)", &gp.radius, 20.0f, 100.0f, "%.0f");
		if (gp.preset == 0 || gp.preset == 1 || gp.preset == 2 || gp.preset == 11)
		{
			ImGui::SliderFloat("Pacing (slow-mo)", &gp.pacing, 0.0f, 100.0f, "%.0f%%");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow motion near the interesting spots / tight turns, faster in between. 0 = constant speed");
		}
		if (gp.preset != 3 && gp.preset != 5 && gp.preset != 6 && gp.preset != 7)
		{
			ImGui::Combo("Start from", &gp.startSide, "Automatic\0West\0East\0North\0South\0");
			ImGui::Combo("Camera looks", &gp.look, "Ahead\0Sideways (left)\0Sideways (right)\0Looking back\0Circle the spots\0Slow pan left to right\0");
		}
		ImGui::SliderFloat("Keys per second", &gp.keysPerSec, 0.2f, 2.0f, "%.2f");
		ImGui::Checkbox("Loop (seamless, ends where it starts)", &gp.loop);
		ImGui::Checkbox("Clockwise / forward", &gp.clockwise);
		ImGui::Checkbox("Use the editor view centre as focus", &gp.useViewCenter);
		if (!resultMsg.empty())
			ImGui::TextWrapped("%s", resultMsg.c_str());
		if (ImGui::Button("Generate"))
		{
			pushUndo(rsw);
			resultMsg = generateCinematic(rsw, activeMapView, gp);
			timeSelected = 0;
			selectedKeyFrame = -1; selectedTrack = -1;
			multiSel.clear();
			if (resultMsg.empty())
				ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Close"))
		{
			resultMsg.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}


	if (cinedraw::showPanel)
	{
		using namespace cinedraw;
		bool open = true;
		ImGui::SetNextWindowSize(ImVec2(360, 520), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Cinematic Path", &open))
		{
			if (ImGui::Checkbox("Drawing (click on the map)", &active)) pickFocus = false;
			ImGui::TextWrapped("Left click adds a point at the end (Alt+click inserts after the selected point), drag a point to move it, right click on a point deletes it, Enter stops drawing.");
			if (ImGui::Button("Clear")) { points.clear(); selected = -1; dirty = true; }
			ImGui::SameLine();
			if (ImGui::Button("Remove last") && !points.empty()) { points.pop_back(); selected = -1; dirty = true; }
			ImGui::SameLine();
			if (ImGui::Button("Reverse")) { std::reverse(points.begin(), points.end()); selected = -1; dirty = true; }
			ImGui::Separator();
			bool changed = false;
			changed |= ImGui::DragFloat("Camera height", &defaultHeight, 0.5f, 3.0f, 500.0f);
			changed |= ImGui::DragFloat("Duration (s)", &duration, 0.5f, 3.0f, 600.0f);
			changed |= ImGui::SliderFloat("Pitch (degrees down)", &pitchDeg, -10.0f, 60.0f, "%.0f");
			changed |= ImGui::Combo("Camera looks", &defLook, "Ahead\0Sideways (left)\0Sideways (right)\0Looking back\0\0Slow pan left to right\0");
			changed |= ImGui::SliderFloat("Pacing (slow-mo)", &pacing, 0.0f, 100.0f, "%.0f%%");
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Slow motion at corners and at points with their own height / look / focus, faster in between");
			changed |= ImGui::Checkbox("Slow down in corners", &slowCorners);
			changed |= ImGui::SliderFloat("Keys per second", &keysPerSec, 0.3f, 2.0f, "%.2f");
			changed |= ImGui::Checkbox("Loop (close the path)", &closedLoop);
			ImGui::Checkbox("Update the cinematic while editing", &autoUpdate);
			if (changed) dirty = true;
			ImGui::Separator();
			ImGui::Text("Points (%d)", (int)points.size());
			if (ImGui::BeginChild("pathpoints", ImVec2(-1, 110), true))
			{
				for (int i = 0; i < (int)points.size(); i++)
				{
					std::string label = "#" + std::to_string(i + 1) + "  " + std::to_string((int)points[i].pos.x) + ", " + std::to_string((int)points[i].pos.z)
						+ (points[i].hasFocus ? "  [focus]" : "") + (points[i].look >= 0 ? "  [look]" : "") + (points[i].height >= 0 ? "  [height]" : "");
					if (ImGui::Selectable(label.c_str(), selected == i)) selected = i;
				}
			}
			ImGui::EndChild();
			if (selected >= 0 && selected < (int)points.size())
			{
				auto& pt = points[selected];
				ImGui::Text("Point #%d", selected + 1);
				bool ho = pt.height >= 0;
				if (ImGui::Checkbox("Own height", &ho)) { pt.height = ho ? defaultHeight : -1.0f; dirty = true; }
				if (ho) dirty |= ImGui::DragFloat("Height##pt", &pt.height, 0.5f, 3.0f, 500.0f);
				int lk = pt.look + 1;
				if (ImGui::Combo("Look here", &lk, "Default\0Ahead\0Sideways (left)\0Sideways (right)\0Looking back\0")) { pt.look = lk - 1; dirty = true; }
				if (ImGui::Checkbox("Look at a focus point here", &pt.hasFocus))
				{
					if (pt.hasFocus && glm::length(pt.focus) < 0.001f) pt.focus = pt.pos + glm::vec3(0, 8, 0);
					dirty = true;
				}
				if (pt.hasFocus)
				{
					ImGui::SameLine();
					if (ImGui::Button(pickFocus ? "Click on the map..." : "Pick on map")) { pickFocus = !pickFocus; active = true; }
					dirty |= ImGui::DragFloat3("Focus", &pt.focus.x, 0.5f);
				}
				if (ImGui::Button("Delete point")) { points.erase(points.begin() + selected); selected = -1; dirty = true; }
			}
			ImGui::Separator();
			bool createClicked = ImGui::Button("Create cinematic from path", ImVec2(-1, 0));
			bool createAuto = autoUpdate && dirty && !ImGui::IsMouseDown(0) && points.size() >= 2;
			if (createClicked || createAuto)
			{
				if (createClicked)
					pushUndo(rsw);
				std::string pathMsg = generateFromPath(rsw, activeMapView);
				dirty = false;
				timeSelected = 0;
				selectedKeyFrame = -1; selectedTrack = -1;
				multiSel.clear();
				if (!pathMsg.empty())
					ImGui::OpenPopup("PathMsg");
				else
					activeMapView->cinematicPlay = false;
			}
			if (ImGui::BeginPopup("PathMsg"))
			{
				ImGui::Text("Draw at least two points first.");
				ImGui::EndPopup();
			}
		}
		ImGui::End();
		if (!open)
		{
			showPanel = false;
			active = false;
			pickFocus = false;
		}
	}
	// ---- timeline -------------------------------------------------------------------------------------
	if (rsw->cinematicLength > 0 && ImGui::BeginChild("KeyframeEditor", ImVec2(-1, 200), true, ImGuiWindowFlags_AlwaysHorizontalScrollbar))
	{
		const ImGuiStyle& style = ImGui::GetStyle();
		const ImGuiIO& IO = ImGui::GetIO();
		ImDrawList* DrawList = ImGui::GetWindowDrawList();
		auto window = ImGui::GetCurrentWindow();

		ImRect bb(window->DC.CursorPos, window->DC.CursorPos + ImVec2(leftAreaSize + scale * rsw->cinematicLength, 100));
		ImGui::ItemSize(bb);
		if (!ImGui::ItemAdd(bb, NULL))
		{
			ImGui::EndChild();
			ImGui::End();
			return;
		}
		ImVec2 cursorPos = ImGui::GetCursorScreenPos();
		ImGui::RenderFrame(bb.Min, bb.Max, ImGui::GetColorU32(ImGuiCol_PopupBg, 1), true, style.FrameRounding);

		Rsw::KeyFrame* primaryPtr = nullptr;
		if (selectedTrack >= 0 && selectedTrack < (int)rsw->cinematicTracks.size() && selectedKeyFrame >= 0 && selectedKeyFrame < (int)rsw->cinematicTracks[selectedTrack].frames.size())
			primaryPtr = rsw->cinematicTracks[selectedTrack].frames[selectedKeyFrame];

		int createTrack = -1;
		float createT = 0;
		bool overKeyframe = false;
		bool needSort = false;
		float gotoFrameTime = -1;

		int trackIndex = 0;
		for (auto& track : rsw->cinematicTracks)
		{
			ImGui::RenderText(bb.Min + ImVec2(10, rowHeight + rowHeight * trackIndex + 2), track.name.c_str()); //todo: center
			ImGui::RenderFrame(bb.Min + ImVec2(leftAreaSize, rowHeight + rowHeight * trackIndex  +1), bb.Min + ImVec2(leftAreaSize + scale * rsw->cinematicLength, rowHeight + rowHeight * trackIndex + rowHeight-2), ImGui::GetColorU32(ImGuiCol_FrameBg, 1), true, style.FrameRounding);
			if (trackIndex < 2 && ImGui::IsMouseHoveringRect(bb.Min + ImVec2(leftAreaSize, rowHeight + rowHeight * trackIndex + 1), bb.Min + ImVec2(leftAreaSize + scale * rsw->cinematicLength, rowHeight + rowHeight * trackIndex + rowHeight - 2)) && ImGui::IsMouseDoubleClicked(0))
			{
				createTrack = trackIndex;
				createT = glm::clamp((ImGui::GetMousePos().x - (bb.Min.x + leftAreaSize)) / scale, 0.0f, rsw->cinematicLength);
			}
			trackIndex++;
		}

		for (int i = 0; i < rsw->cinematicLength; i++)
		{
			ImGui::RenderText(bb.Min + ImVec2(leftAreaSize + scale * i + 4, 0), std::to_string(i).c_str());
			for (int ii = 0; ii < 10; ii++)
			{
				window->DrawList->AddLine(bb.Min + ImVec2(leftAreaSize + scale * i + scale / 10.0f * ii, rowHeight - 5), bb.Min + ImVec2(leftAreaSize + scale * i + scale / 10.0f * ii, rowHeight), ImGui::GetColorU32(ImGuiCol_TextDisabled, 1.0f));
				window->DrawList->AddLine(bb.Min + ImVec2(leftAreaSize + scale * i + scale / 10.0f * ii, rowHeight), bb.Min + ImVec2(leftAreaSize + scale * i + scale / 10.0f * ii, rowHeight * (rsw->cinematicTracks.size() + 1)), ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.5f));
			}
			window->DrawList->AddLine(bb.Min + ImVec2(leftAreaSize + scale * i, 0), bb.Min + ImVec2(leftAreaSize + scale * i, rowHeight * (rsw->cinematicTracks.size() + 1)), ImGui::GetColorU32(ImGuiCol_Text, 1));

		}

		static std::map<Rsw::KeyFrame*, float> dragStart;
		static float dragAcc = 0;
		static bool dragUndoDone = false;
		trackIndex = 0;
		for (auto& track : rsw->cinematicTracks)
		{
			ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_Button, 10));
			for (int keyframeIndex = 0; keyframeIndex < (int)track.frames.size(); keyframeIndex++)
			{
				auto keyframe = track.frames[keyframeIndex];
				ImGui::PushID(keyframe);
				bool selected = keyframe == primaryPtr || multiSel.count(keyframe) > 0;
				if (selected)
				{
					ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_PlotHistogram, 10));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_PlotHistogram, 10));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_PlotHistogram, 10));
				}
				ImGui::SetCursorScreenPos(bb.Min + ImVec2(leftAreaSize + scale * keyframe->time - 5, rowHeight + rowHeight * trackIndex));
				ImGui::Button("##", ImVec2(10, rowHeight));
				if (ImGui::IsItemHovered())
					overKeyframe = true;
				if (ImGui::IsItemActivated())
				{
					if (IO.KeyCtrl)
					{
						if (multiSel.count(keyframe)) multiSel.erase(keyframe);
						else
						{
							if (primaryPtr) multiSel.insert(primaryPtr);
							multiSel.insert(keyframe);
						}
					}
					else if (!multiSel.count(keyframe) && keyframe != primaryPtr)
					{
						multiSel.clear();
						multiSel.insert(keyframe);
					}
					selectedTrack = trackIndex;
					selectedKeyFrame = keyframeIndex;
					primaryPtr = keyframe;
					dragStart.clear();
					for (auto& t2 : rsw->cinematicTracks)
						for (auto f : t2.frames)
							if (multiSel.count(f) || f == keyframe)
								dragStart[f] = f->time;
					dragAcc = 0;
					dragUndoDone = false;
				}
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
					gotoFrameTime = keyframe->time;
				if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && !multiSel.count(keyframe) && keyframe != primaryPtr)
				{
					multiSel.clear();
					multiSel.insert(keyframe);
					selectedTrack = trackIndex;
					selectedKeyFrame = keyframeIndex;
					primaryPtr = keyframe;
				}
				if (ImGui::BeginPopupContextItem("KeyFramePopup"))
				{
					if (ImGui::MenuItem("Go to this shot")) gotoFrameTime = keyframe->time;
					if (ImGui::MenuItem("Copy", "Ctrl+C")) actCopy = true;
					if (ImGui::MenuItem("Duplicate at playhead", "Ctrl+D")) { actCopy = true; actPaste = true; }
					if (ImGui::MenuItem("Delete", "Del")) actDelete = true;
					ImGui::EndPopup();
				}
				if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !dragStart.empty())
				{
					dragAcc += ImGui::GetIO().MouseDelta.x / scale;
					if (ImGui::GetIO().MouseDelta.x != 0 && !dragUndoDone)
					{
						// the snapshot is taken from the positions before this drag started
						std::map<Rsw::KeyFrame*, float> cur;
						for (auto& kv : dragStart) { cur[kv.first] = kv.first->time; kv.first->time = kv.second; }
						pushUndo(rsw);
						for (auto& kv : cur) kv.first->time = kv.second;
						dragUndoDone = true;
					}
					for (auto& kv : dragStart)
					{
						float nt = kv.second + dragAcc;
						if (snapStep > 0)
							nt = std::round(nt / snapStep) * snapStep;
						kv.first->time = glm::clamp(nt, 0.0f, rsw->cinematicLength);
					}
					needSort = true;
				}
				if (selected)
					ImGui::PopStyleColor(3);
				ImGui::PopID();
			}
			ImGui::PopStyleColor();
			trackIndex++;
		}

		if (needSort)
		{
			for (auto& t : rsw->cinematicTracks)
				sortTrack(t);
			if (primaryPtr)
				for (int ti = 0; ti < (int)rsw->cinematicTracks.size(); ti++)
					for (int fi = 0; fi < (int)rsw->cinematicTracks[ti].frames.size(); fi++)
						if (rsw->cinematicTracks[ti].frames[fi] == primaryPtr)
						{
							selectedTrack = ti;
							selectedKeyFrame = fi;
						}
		}

		if (gotoFrameTime >= 0)
		{
			actGoto = true;
			gotoTime = gotoFrameTime;
			timeSelected = gotoFrameTime;
		}

		if (createTrack >= 0 && !overKeyframe)
		{
			pushUndo(rsw);
			auto keyframe = makeFrameAt(rsw, createTrack, createT);
			if (keyframe)
			{
				rsw->cinematicTracks[createTrack].frames.push_back(keyframe);
				sortTrack(rsw->cinematicTracks[createTrack]);
				multiSel.clear();
				multiSel.insert(keyframe);
				selectedTrack = createTrack;
				for (int i = 0; i < (int)rsw->cinematicTracks[createTrack].frames.size(); i++)
					if (rsw->cinematicTracks[createTrack].frames[i] == keyframe)
						selectedKeyFrame = i;
			}
		}

		window->DrawList->AddLine(bb.Min + ImVec2(leftAreaSize + scale * timeSelected, 0), bb.Min + ImVec2(leftAreaSize + scale * timeSelected, rowHeight * (rsw->cinematicTracks.size() + 1)), ImGui::GetColorU32(ImGuiCol_Text, 1));
		ImGui::SetCursorScreenPos(bb.Min + ImVec2(leftAreaSize, 0));
		ImGui::InvisibleButton("##", ImVec2(scale * rsw->cinematicLength, rowHeight));
		if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			timeSelected = (ImGui::GetMousePos().x - (bb.Min.x+leftAreaSize)) / scale;
			if (timeSelected > rsw->cinematicLength)
				timeSelected = rsw->cinematicLength;
			if (timeSelected < 0)
				timeSelected = 0;
		}


		ImGui::SetCursorScreenPos(cursorPos);


		ImGui::EndChild();
	}

	// ---- actions (buttons, shortcuts and context menus all end up here) -----------------------------
	auto selection = [&]()
	{
		std::vector<std::pair<int, Rsw::KeyFrame*>> out;
		for (int ti = 0; ti < (int)rsw->cinematicTracks.size(); ti++)
			for (int fi = 0; fi < (int)rsw->cinematicTracks[ti].frames.size(); fi++)
			{
				auto f = rsw->cinematicTracks[ti].frames[fi];
				if (multiSel.count(f) || (ti == selectedTrack && fi == selectedKeyFrame))
					out.push_back({ ti, f });
			}
		return out;
	};
	if (actUndo && !undoStack.empty())
	{
		redoStack.push_back(takeSnap(rsw));
		restoreSnap(rsw, undoStack.back());
		undoStack.pop_back();
		selectedKeyFrame = -1; selectedTrack = -1;
	}
	if (actRedo && !redoStack.empty())
	{
		undoStack.push_back(takeSnap(rsw));
		restoreSnap(rsw, redoStack.back());
		redoStack.pop_back();
		selectedKeyFrame = -1; selectedTrack = -1;
	}
	if (actCopy)
	{
		auto sel = selection();
		if (!sel.empty())
		{
			clipboard.clear();
			float base = sel[0].second->time;
			for (auto& s2 : sel) base = glm::min(base, s2.second->time);
			for (auto& s2 : sel)
				clipboard.push_back({ s2.first, cloneFrame(s2.second), s2.second->time - base });
		}
	}
	if (actPaste && !clipboard.empty())
	{
		pushUndo(rsw);
		multiSel.clear();
		float maxT = rsw->cinematicLength;
		for (auto& item : clipboard)
		{
			auto nf = cloneFrame(item.frame);
			if (!nf || item.track >= (int)rsw->cinematicTracks.size()) continue;
			nf->time = timeSelected + item.rel;
			maxT = glm::max(maxT, nf->time);
			rsw->cinematicTracks[item.track].frames.push_back(nf);
			multiSel.insert(nf);
		}
		for (auto& t : rsw->cinematicTracks) sortTrack(t);
		rsw->cinematicLength = std::ceil(maxT);
		selectedKeyFrame = -1; selectedTrack = -1;
	}
	if (actDelete)
	{
		auto sel = selection();
		if (!sel.empty())
		{
			pushUndo(rsw);
			for (int ti = 0; ti < (int)rsw->cinematicTracks.size(); ti++)
			{
				auto& fr = rsw->cinematicTracks[ti].frames;
				std::vector<Rsw::KeyFrame*> keep;
				for (auto f : fr)
				{
					bool del = false;
					for (auto& s2 : sel) if (s2.second == f) del = true;
					if (!del) keep.push_back(f);
				}
				if (!keep.empty())		// a track keeps at least one keyframe
					fr = keep;
			}
			multiSel.clear();
			selectedKeyFrame = -1; selectedTrack = -1;
		}
	}
	if (actAdd)
	{
		pushUndo(rsw);
		for (int ti = 0; ti < 2 && ti < (int)rsw->cinematicTracks.size(); ti++)
			if (auto nf = makeFrameAt(rsw, ti, timeSelected))
			{
				rsw->cinematicTracks[ti].frames.push_back(nf);
				sortTrack(rsw->cinematicTracks[ti]);
			}
	}
	if (actKeyView)
	{
		pushUndo(rsw);
		glm::vec3 camPos, center;
		editorCamera(activeMapView, camPos, center);
		auto findNear = [&](int ti) -> Rsw::KeyFrame* {
			for (auto f : rsw->cinematicTracks[ti].frames)
				if (std::abs(f->time - timeSelected) < 0.05f) return f;
			return nullptr;
		};
		// position
		auto pf = dynamic_cast<PosFrame*>(findNear(0));
		if (!pf)
		{
			pf = dynamic_cast<PosFrame*>(makeFrameAt(rsw, 0, timeSelected));
			if (pf) { rsw->cinematicTracks[0].frames.push_back(pf); sortTrack(rsw->cinematicTracks[0]); }
		}
		if (pf)
		{
			pf->data.first = camPos;
			if (glm::length(pf->data.second) < 1.0f)
				pf->data.second = glm::normalize(center - camPos + glm::vec3(0, 0.001f, 0)) * 100.0f;
		}
		// target
		auto tf = dynamic_cast<TgtFrame*>(findNear(1));
		if (!tf)
		{
			tf = dynamic_cast<TgtFrame*>(makeFrameAt(rsw, 1, timeSelected));
			if (tf) { rsw->cinematicTracks[1].frames.push_back(tf); sortTrack(rsw->cinematicTracks[1]); }
		}
		if (tf)
		{
			tf->data.lookAt = Rsw::CameraTarget::LookAt::Point;
			tf->data.point = center;
		}
	}
	if (actGoto)
		goToShot(rsw, activeMapView, gotoTime);

	ImGui::End();


	if (ImGui::Begin("Cinematic Mode Properties"))
	{
		bool selected = selectedTrack >= 0 && selectedTrack < rsw->cinematicTracks.size() && selectedKeyFrame >= 0 && selectedKeyFrame < rsw->cinematicTracks[selectedTrack].frames.size();
		if (selected)
		{
			auto keyFrame = rsw->cinematicTracks[selectedTrack].frames[selectedKeyFrame];

			bool wasActive = ImGui::IsAnyItemActive();
			Snap before = takeSnap(rsw);
			keyFrame->buildEditor();
			if (!wasActive && ImGui::IsAnyItemActive())
			{
				undoStack.push_back(before);
				redoStack.clear();
			}
		}
	}
	ImGui::End();
}
