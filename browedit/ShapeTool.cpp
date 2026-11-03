#include <glad/gl.h>
#include "ShapeTool.h"
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
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace shapetool
{
	Settings& settings() { static Settings s; return s; }

	namespace
	{
		glm::vec2 cornerPos(Gnd* gnd, int x, int y, int i)
		{
			return glm::vec2((float)x + (i % 2), (float)(gnd->height - y) + (i / 2 == 0 ? 1.0f : 0.0f));
		}

		float terrain(Gnd* gnd, float tx, float tz)
		{
			int x = std::clamp((int)std::floor(tx), 0, gnd->width - 1);
			int y = std::clamp(gnd->height - 1 - (int)std::floor(tz), 0, gnd->height - 1);
			float fx = std::clamp(tx - x, 0.0f, 1.0f), fz = std::clamp(tz - (gnd->height - 1 - y), 0.0f, 1.0f);
			auto c = gnd->cubes[x][y];
			float top = c->h3 + (c->h4 - c->h3) * fx, bottom = c->h1 + (c->h2 - c->h1) * fx;
			return -(top + (bottom - top) * fz);
		}

		// the polyline that is really used: the clicked points, or a Catmull-Rom curve through them
		std::vector<glm::vec2> buildLine(const Settings& s, bool closed)
		{
			const auto& p = s.pts;
			int n = (int)p.size();
			if (!s.smooth || n < 3)
			{
				std::vector<glm::vec2> r = p;
				return r;
			}
			std::vector<glm::vec2> out;
			int segs = closed ? n : n - 1;
			auto P = [&](int i) -> glm::vec2
			{
				if (closed) return p[((i % n) + n) % n];
				return p[std::clamp(i, 0, n - 1)];
			};
			for (int i = 0; i < segs; i++)
			{
				glm::vec2 p0 = P(i - 1), p1 = P(i), p2 = P(i + 1), p3 = P(i + 2);
				float len = glm::length(p2 - p1);
				int steps = std::max(4, (int)std::ceil(len * 3.0f));
				for (int k = 0; k < steps; k++)
				{
					float t = k / (float)steps, t2 = t * t, t3 = t2 * t;
					out.push_back(0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3));
				}
			}
			if (!closed) out.push_back(p.back());
			return out;
		}

		float segDist(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
		{
			glm::vec2 ab = b - a;
			float l2 = glm::dot(ab, ab);
			float t = l2 > 1e-8f ? std::clamp(glm::dot(p - a, ab) / l2, 0.0f, 1.0f) : 0.0f;
			return glm::length(p - (a + ab * t));
		}

		// signed distance to the shape in tiles: negative inside
		float signedDist(const std::vector<glm::vec2>& line, bool closed, float halfWidth, const glm::vec2& p)
		{
			float d = 1e9f;
			int n = (int)line.size();
			int segs = closed ? n : n - 1;
			bool inside = false;
			for (int i = 0; i < segs; i++)
			{
				const glm::vec2& a = line[i];
				const glm::vec2& b = line[(i + 1) % n];
				d = std::min(d, segDist(p, a, b));
				if (closed && ((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x))
					inside = !inside;
			}
			if (closed) return inside ? -d : d;
			return d - halfWidth;
		}

		float weightOf(float d, float slope)
		{
			float w = std::clamp(0.5f - d / std::max(0.25f, slope), 0.0f, 1.0f);
			return w * w * (3.0f - 2.0f * w);
		}

		void apply(BrowEdit* browEdit, Map* map, Gnd* gnd, GndRenderer* gr)
		{
			auto& s = settings();
			bool closed = s.type == 0;
			if (s.pts.size() < (closed ? 3u : 2u)) return;
			auto line = buildLine(s, closed);
			float halfW = closed ? 0.0f : s.pathWidth * 0.5f;
			glm::vec2 mn(1e9f), mx(-1e9f);
			for (auto& p : line) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
			float reach = s.slope + halfW + 2.0f;
			int x0 = std::max(0, (int)std::floor(mn.x - reach)), x1 = std::min(gnd->width - 1, (int)std::ceil(mx.x + reach));
			int y0 = std::max(0, (int)std::floor(gnd->height - mx.y - reach)), y1 = std::min(gnd->height - 1, (int)std::ceil(gnd->height - mn.y + reach));

			std::map<Gnd::Cube*, float[4]> oldH, newH;
			std::vector<glm::ivec2> touched;
			for (int x = x0; x <= x1; x++)
				for (int y = y0; y <= y1; y++)
				{
					auto cube = gnd->cubes[x][y];
					float w[4]; bool any = false;
					for (int i = 0; i < 4; i++)
					{
						w[i] = weightOf(signedDist(line, closed, halfW, cornerPos(gnd, x, y, i)), s.slope);
						any |= w[i] > 0.0f;
					}
					if (!any) continue;
					for (int i = 0; i < 4; i++)
					{
						float o = cube->heights[i];
						oldH[cube][i] = o;
						if (s.mode == 0) newH[cube][i] = o - s.height * w[i];
						else if (s.mode == 1) newH[cube][i] = o + s.height * w[i];
						else newH[cube][i] = o + (s.height - o) * w[i];
					}
					touched.push_back(glm::ivec2(x, y));
				}
			if (touched.empty()) return;

			auto ga = new GroupAction("Shape height");
			ga->addAction(new CubeHeightChangeAction<Gnd, Gnd::Cube>(oldH, newH, touched));
			// the action's perform() sets the new heights; apply them now as well so the GAT below sees them
			for (auto& kv : newH) for (int i = 0; i < 4; i++) kv.first->heights[i] = kv.second[i];

			std::set<std::pair<int, int>> region;
			for (auto& t : touched)
				for (int dx = -1; dx <= 1; dx++)
					for (int dy = -1; dy <= 1; dy++)
					{
						int x = t.x + dx, y = t.y + dy;
						if (x >= 0 && y >= 0 && x < gnd->width && y < gnd->height) region.insert({ x, y });
					}

			if (s.autoGat)
			{
				auto gat = map->rootNode->getComponent<Gat>();
				if (gat && gat->width == gnd->width * 2 && gat->height == gnd->height * 2)
				{
					std::map<Gat::Cube*, float[4]> oldT, newT;
					std::vector<glm::ivec2> selT;
					std::map<std::pair<int, int>, bool> wantCliff;
					std::set<std::pair<int, int>> touchedSet;
					for (auto& t : touched) touchedSet.insert({ t.x, t.y });
					for (auto& r : region)
					{
						auto cb = gnd->cubes[r.first][r.second];
						const float* nh = cb->heights;
						for (int k = 0; k < 4; k++)
						{
							int gx = r.first * 2 + (k % 2), gy = r.second * 2 + (k >> 1);
							float fu0 = (k % 2) / 2.0f, fu1 = fu0 + 0.5f, fv0 = (k >> 1) / 2.0f, fv1 = fv0 + 0.5f;
							auto at = [&](float fu, float fv) { return (nh[0] * (1 - fu) + nh[1] * fu) * (1 - fv) + (nh[2] * (1 - fu) + nh[3] * fu) * fv; };
							float gh[4] = { at(fu0, fv0), at(fu1, fv0), at(fu0, fv1), at(fu1, fv1) };
							float lo = std::min(std::min(gh[0], gh[1]), std::min(gh[2], gh[3])), hi = std::max(std::max(gh[0], gh[1]), std::max(gh[2], gh[3]));
							wantCliff[{ gx, gy }] = (hi - lo) > s.gatSlope;
							if (touchedSet.count(r))
							{
								auto gc = gat->cubes[gx][gy];
								for (int i = 0; i < 4; i++) { oldT[gc][i] = gc->heights[i]; newT[gc][i] = gh[i]; }
								selT.push_back(glm::ivec2(gx, gy));
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
			s.pts.clear();
		}
	}

	void update(MapView* mv, BrowEdit* browEdit, Map* map, Gnd* gnd, GndRenderer* gr, const glm::vec3& mouse3D)
	{
		auto& s = settings();
		auto& io = ImGui::GetIO();
		bool valid = mouse3D.x >= 0 && mouse3D.z >= 0 && mouse3D.x <= gnd->width * 10.0f && mouse3D.z <= gnd->height * 10.0f;
		glm::vec2 c(mouse3D.x / 10.0f, mouse3D.z / 10.0f);
		bool closed = s.type == 0;

		if (valid)
		{
			ImGui::Begin("Statusbar");
			ImGui::Text("Shape tool: click = add point, Enter = apply, Backspace = remove last point, Esc = clear");
			ImGui::End();
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
				s.pts.push_back(c);
		}
		if (!io.WantTextInput)
		{
			if (ImGui::IsKeyPressed(GLFW_KEY_BACKSPACE) && !s.pts.empty()) s.pts.pop_back();
			if (ImGui::IsKeyPressed(GLFW_KEY_ESCAPE)) s.pts.clear();
			if ((ImGui::IsKeyPressed(GLFW_KEY_ENTER) || ImGui::IsKeyPressed(GLFW_KEY_KP_ENTER))) s.applyRequested = true;
		}
		if (s.applyRequested)
		{
			s.applyRequested = false;
			apply(browEdit, map, gnd, gr);
		}

		// outline of the shape, the clicked points, and the segment to the mouse
		if (!s.pts.empty())
		{
			auto preview = s;
			if (valid) preview.pts.push_back(c);
			auto line = buildLine(preview, closed);
			glUseProgram(0);
			glDisable(GL_DEPTH_TEST);
			glLineWidth(2.0f);
			for (int pass = 0; pass < 2; pass++)
			{
				if (pass == 0) glColor4f(1.0f, 0.55f, 0.1f, 1.0f); else glColor4f(1.0f, 1.0f, 1.0f, 0.5f);
				float off = pass == 0 ? 0.0f : (closed ? 0.0f : s.pathWidth * 0.5f);
				if (pass == 1 && closed) continue;
				glBegin(closed ? GL_LINE_LOOP : GL_LINE_STRIP);
				for (size_t i = 0; i < line.size(); i++)
				{
					glm::vec2 d = line[std::min(i + 1, line.size() - 1)] - line[i > 0 ? i - 1 : 0];
					glm::vec2 nrm = glm::length(d) > 1e-5f ? glm::normalize(glm::vec2(-d.y, d.x)) : glm::vec2(0);
					glm::vec2 p = line[i] + nrm * off;
					glVertex3f(p.x * 10.0f, terrain(gnd, line[i].x, line[i].y) + 2.0f, p.y * 10.0f);
				}
				glEnd();
			}
			glPointSize(8.0f);
			glColor4f(1, 1, 0, 1);
			glBegin(GL_POINTS);
			for (auto& p : s.pts)
				glVertex3f(p.x * 10.0f, terrain(gnd, p.x, p.y) + 2.0f, p.y * 10.0f);
			glEnd();
			glPointSize(1.0f);
			glLineWidth(1.0f);
			glColor4f(1, 1, 1, 1);
			glEnable(GL_DEPTH_TEST);
		}
	}

	void showOptions(BrowEdit* browEdit, MapView* mv)
	{
		auto& s = settings();
		ImGui::TextWrapped("Click points on the ground to draw a shape, then apply. Works for circles, diagonals, any outline.");
		ImGui::RadioButton("Area (closed shape)", &s.type, 0); ImGui::SameLine();
		ImGui::RadioButton("Path (ridge / road)", &s.type, 1);
		ImGui::Checkbox("Smooth curve through the points", &s.smooth);
		ImGui::RadioButton("Raise", &s.mode, 0); ImGui::SameLine();
		ImGui::RadioButton("Lower", &s.mode, 1); ImGui::SameLine();
		ImGui::RadioButton("Set height", &s.mode, 2);
		ImGui::DragFloat("Height", &s.height, 0.5f, -200.0f, 200.0f);
		ImGui::SliderFloat("Slope width (tiles)", &s.slope, 0.5f, 12.0f);
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("Small = steep cliff, large = soft hill.\nBelow about 1 tile a diagonal edge starts to look stepped again.");
		if (s.type == 1) ImGui::SliderFloat("Path width (tiles)", &s.pathWidth, 1.0f, 30.0f);
		ImGui::Checkbox("Update the GAT (heights + cliff cells)", &s.autoGat);
		if (s.autoGat) ImGui::SliderFloat("GAT cliff above (height per cell)", &s.gatSlope, 1.0f, 15.0f);
		ImGui::Text("%d points", (int)s.pts.size());
		if (ImGui::Button("Apply (Enter)")) s.applyRequested = true;
		ImGui::SameLine();
		if (ImGui::Button("Remove last") && !s.pts.empty()) s.pts.pop_back();
		ImGui::SameLine();
		if (ImGui::Button("Clear")) s.pts.clear();
	}
}
