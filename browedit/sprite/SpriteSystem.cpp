#include <glad/gl.h>
#include "SpriteInternal.h"
#include "Text.h"
#include <browedit/BrowEdit.h>
#include <browedit/Map.h>
#include <browedit/MapView.h>
#include <browedit/Node.h>
#include <browedit/actions/Action.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/Gat.h>
#include <browedit/components/SprRenderer.h>
#include <browedit/util/FileIO.h>
#include <browedit/util/ResourceManager.h>
#include <browedit/math/Ray.h>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

namespace sprite
{
	class CharShader : public gl::Shader
	{
	public:
		CharShader() : gl::Shader("data/shaders/sprchar", Uniforms::End) { bindUniforms(); }
		struct Uniforms { enum { projectionMatrix, cameraMatrix, modelMatrix, s_texture, color, depthBias, End }; };
		void bindUniforms() override
		{
			bindUniform(Uniforms::projectionMatrix, "projectionMatrix");
			bindUniform(Uniforms::cameraMatrix, "cameraMatrix");
			bindUniform(Uniforms::modelMatrix, "modelMatrix");
			bindUniform(Uniforms::s_texture, "s_texture");
			bindUniform(Uniforms::color, "color");
			bindUniform(Uniforms::depthBias, "depthBias");
		}
	};

	const char* categories[] = { "Town Citizens", "Merchants", "Guards", "Adventurers", "Event NPCs", "Custom" };
	const int categoryCount = 6;

	// ------------------------------------------------------------------ instance json
	nlohmann::json Instance::toJson(const Db& db) const
	{
		nlohmann::json j;
		j["id"] = id; j["name"] = name;
		j["headMode"] = cfg.headMode;
		j["kind"] = cfg.kind == 0 ? "player" : cfg.kind == 1 ? "monster" : "npc"; j["mob"] = cfg.mob;
		j["job"] = cfg.job;
		auto je = db.job(cfg.job);
		j["jobId"] = je ? je->id : -1;
		j["gender"] = cfg.gender == Female ? "female" : "male";
		j["hairStyle"] = cfg.hairStyle; j["hairColor"] = cfg.hairColor; j["bodyPalette"] = cfg.bodyPalette;
		j["headgear"] = { cfg.head[0], cfg.head[1], cfg.head[2] };
		j["costumeHeadgear"] = { cfg.costume[0], cfg.costume[1], cfg.costume[2] };
		j["garment"] = cfg.robe; j["weapon"] = cfg.weapon; j["shield"] = cfg.shield;
		j["customAssets"] = { {"wing", cfg.customWing}, {"alignment", cfg.toJson()["wingOff"]} };
		j["action"] = action; j["direction"] = dir; j["speed"] = speed; j["scale"] = scale;
		j["position"] = { pos.x, pos.y, pos.z }; j["elevation"] = elevation;
		j["visible"] = visible; j["locked"] = locked;
		{
			nlohmann::json m;
			m["enabled"] = pathEnabled; m["mode"] = pathMode; m["speed"] = moveSpeed; m["wait"] = waitTime; m["delay"] = startDelay;
			m["faceMovement"] = faceMove; m["syncAnimation"] = syncAnim; m["walkAction"] = walkAction; m["wanderRadius"] = wanderRadius;
			m["waypoints"] = nlohmann::json::array();
			for (auto& p : path) m["waypoints"].push_back({ p.x, p.y, p.z });
			j["movement"] = m;
		}
		j["editorOnly"] = true;	// never an NPC in the game
		return j;
	}
	Instance Instance::fromJson(const nlohmann::json& j)
	{
		Instance i;
		i.id = j.value("id", 0); i.name = j.value("name", std::string("Sprite"));
		auto& c = i.cfg;
		{ std::string k = j.value("kind", std::string("player")); c.kind = k == "monster" ? 1 : k == "npc" ? 2 : 0; }
		c.mob = j.value("mob", std::string()); c.headMode = j.value("headMode", 0);
		c.job = j.value("job", c.job);
		c.gender = j.value("gender", std::string("male")) == "female" ? Female : Male;
		c.hairStyle = j.value("hairStyle", 1); c.hairColor = j.value("hairColor", 0); c.bodyPalette = j.value("bodyPalette", -1);
		if (j.contains("headgear")) for (int k = 0; k < 3; k++) c.head[k] = j["headgear"][k];
		if (j.contains("costumeHeadgear")) for (int k = 0; k < 3; k++) c.costume[k] = j["costumeHeadgear"][k];
		c.robe = j.value("garment", 0); c.weapon = j.value("weapon", 0); c.shield = j.value("shield", std::string());
		if (j.contains("customAssets"))
		{
			c.customWing = j["customAssets"].value("wing", std::string());
			if (j["customAssets"].contains("alignment"))
				for (int k = 0; k < 8 && k < (int)j["customAssets"]["alignment"].size(); k++) { c.wingOff[k][0] = j["customAssets"]["alignment"][k][0]; c.wingOff[k][1] = j["customAssets"]["alignment"][k][1]; }
		}
		i.action = j.value("action", 0); i.dir = j.value("direction", 0); i.speed = j.value("speed", 1.0f); i.scale = j.value("scale", 1.0f);
		if (j.contains("position")) i.pos = glm::vec3(j["position"][0], j["position"][1], j["position"][2]);
		i.elevation = j.value("elevation", 0.0f);
		i.visible = j.value("visible", true); i.locked = j.value("locked", false);
		if (j.contains("movement"))
		{
			auto& m = j["movement"];
			i.pathEnabled = m.value("enabled", true); i.pathMode = m.value("mode", 1); i.moveSpeed = m.value("speed", 4.0f); i.waitTime = m.value("wait", 1.0f);
			i.startDelay = m.value("delay", 0.0f); i.faceMove = m.value("faceMovement", true); i.syncAnim = m.value("syncAnimation", true);
			i.walkAction = m.value("walkAction", 1); i.wanderRadius = m.value("wanderRadius", 0.0f);
			if (m.contains("waypoints")) for (auto& p : m["waypoints"]) i.path.push_back(glm::vec3(p[0], p[1], p[2]));
		}
		return i;
	}

	// ------------------------------------------------------------------ undo
	namespace
	{
		class LayerAction : public Action
		{
			MapLayer before, after;
			std::string title;
			void apply(Map* map, BrowEdit* be, const MapLayer& from)
			{
				auto& L = System::instance().layer(map, be);
				L.items = from.items; L.nextId = std::max(L.nextId, from.nextId);
				for (auto it = L.selected.begin(); it != L.selected.end();)
					it = L.find(*it) ? std::next(it) : L.selected.erase(it);
			}
		public:
			LayerAction(const MapLayer& b, const MapLayer& a, const std::string& t) : before(b), after(a), title(t) {}
			void perform(Map* map, BrowEdit* be) override { apply(map, be, after); }
			void undo(Map* map, BrowEdit* be) override { apply(map, be, before); }
			std::string str() override { return title; }
		};
	}
	void pushLayerAction(Map* map, BrowEdit* be, MapLayer& layer, const MapLayer& before, const std::string& title)
	{
		MapLayer after = layer;
		map->doAction(new LayerAction(before, after, title), be);
	}

	// ------------------------------------------------------------------ system / assets
	System& System::instance() { static System s; return s; }
	System::System() : composer(db, *this), impl(new Impl()) {}
	System::~System() { if (scanThread.joinable()) scanThread.join(); }

	bool System::read(const std::string& path, std::vector<uint8_t>& out)
	{
		try
		{
			std::istream* is = util::FileIO::open(utf8ToRaw(path));
			if (!is) return false;
			is->seekg(0, std::ios::end);
			auto size = is->tellg();
			is->seekg(0, std::ios::beg);
			if (size <= 0) { delete is; return false; }
			out.resize((size_t)size);
			is->read((char*)out.data(), size);
			delete is;
			return true;
		}
		catch (...) { return false; }
	}

	void System::listAll(std::vector<std::string>& paths)
	{
		auto all = util::FileIO::listAllFiles();
		const char* roots[] = { "data\\sprite\\", "data\\palette\\", "data\\imf\\" };
		for (auto& f : all)
		{
			bool keep = false;
			for (auto r : roots)
			{
				size_t n = strlen(r);
				if (f.size() >= n && lower(f.substr(0, n)) == r) { keep = true; break; }
			}
			if (keep) paths.push_back(rawToUtf8(f));
		}
	}

	void System::startScan(BrowEdit* browEdit)
	{
		if (db.scanning) return;
		if (scanThread.joinable()) scanThread.join();
		std::string meta;
		{
			std::ifstream i("data/spritedb.json", std::ios::binary);
			std::stringstream ss; ss << i.rdbuf(); meta = ss.str();
		}
		std::string err;
		if (meta.empty() || !db.loadMeta(meta, err))
			std::cerr << "Sprite: could not load data/spritedb.json: " << err << std::endl;
		composer.clearAll();
		db.ready = false; db.scanning = true;
		scanStarted = true;
		scanThread = std::thread([this]() { try { db.scan(*this); } catch (...) { db.scanning = false; } impl->scanVersion++; });
	}

	void System::ensureReady(BrowEdit* browEdit)
	{
		if (!scanStarted) startScan(browEdit);
		if (!impl->presetsLoaded) impl->loadPresets(*this);
	}

	// ------------------------------------------------------------------ persistence
	MapLayer& System::layer(Map* map, BrowEdit* be)
	{
		auto& L = impl->layers[map];
		if (!L.loaded)
		{
			L.loaded = true;
			std::string n = map->name;
			std::string path = n.find(":") != std::string::npos ? n : be->config.ropath + n;
			if (path.size() > 4 && path.substr(path.size() - 4) == ".rsw") path = path.substr(0, path.size() - 4);
			L.path = path + ".browsprites.json";
			std::ifstream i(L.path, std::ios::binary);
			if (i)
			{
				try
				{
					auto j = nlohmann::json::parse(i);
					for (auto& e : j["sprites"]) { auto in = Instance::fromJson(e); if (in.id <= 0) in.id = L.nextId; L.nextId = std::max(L.nextId, in.id + 1); L.items.push_back(in); }
					L.nextId = std::max(L.nextId, j.value("nextId", 1));
				}
				catch (std::exception& e) { std::cerr << "Sprite: could not read " << L.path << ": " << e.what() << std::endl; }
			}
		}
		return L;
	}

	void System::save(Map* map, BrowEdit* be)
	{
		auto it = impl->layers.find(map);
		if (it == impl->layers.end()) return;
		auto& L = it->second;
		if (L.items.empty() && !std::filesystem::exists(L.path)) return;
		nlohmann::json j;
		j["version"] = 1;
		j["note"] = "BrowEdit editor-only sprite characters. They are not part of the RSW and are not NPCs.";
		j["nextId"] = L.nextId;
		j["sprites"] = nlohmann::json::array();
		for (auto& i : L.items) j["sprites"].push_back(i.toJson(db));
		try
		{
			std::ofstream o(L.path, std::ios::binary);
			o << j.dump(1);
		}
		catch (...) { std::cerr << "Sprite: could not write " << L.path << std::endl; }
	}

	void System::forget(Map* map) { impl->layers.erase(map); }

	// ------------------------------------------------------------------ gl helpers
	unsigned int System::Impl::tex(Frame& f)
	{
		if (f.tex) return f.tex;
		if (f.w <= 0 || f.h <= 0) return 0;
		glGenTextures(1, &f.tex);
		glBindTexture(GL_TEXTURE_2D, f.tex);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, f.w, f.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, f.rgba.data());
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);	// pixel art: nearest
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		return f.tex;
	}

	void System::Impl::freeDropped(System& s)
	{
		for (auto& f : s.composer.dropped)
			if (f && f->tex) { glDeleteTextures(1, &f->tex); f->tex = 0; }
		s.composer.dropped.clear();
	}

	static bool sameCfg(const CharacterConfig& a, const CharacterConfig& b)
	{
		return a.headMode == b.headMode && a.kind == b.kind && a.mob == b.mob && a.job == b.job && a.gender == b.gender && a.hairStyle == b.hairStyle && a.hairColor == b.hairColor && a.bodyPalette == b.bodyPalette &&
			a.head[0] == b.head[0] && a.head[1] == b.head[1] && a.head[2] == b.head[2] && a.costume[0] == b.costume[0] && a.costume[1] == b.costume[1] && a.costume[2] == b.costume[2] &&
			a.robe == b.robe && a.weapon == b.weapon && a.shield == b.shield && a.customWing == b.customWing && memcmp(a.wingOff, b.wingOff, sizeof(a.wingOff)) == 0;
	}

	std::shared_ptr<Frame> System::Impl::frameFor(System& s, const Instance& in, int effDir, double time, AnimInfo* infoOut, int actionOverride, float speedMul)
	{
		int act = actionOverride >= 0 ? actionOverride : in.action;
		if (!in.rtValid || !sameCfg(in.cfg, in.rtCfg) || in.rtAction != act || in.rtDir != effDir)
		{
			in.rtCfg = in.cfg; in.rtKey = in.cfg.key(); in.rtDir = effDir; in.rtValid = true;
			in.rtInfo = s.composer.info(in.cfg, act, effDir);
			if (!in.rtInfo.valid && act != in.action) { act = in.action; in.rtInfo = s.composer.info(in.cfg, act, effDir); }	// no such action for this sprite
			in.rtAction = act;
			in.rtFrames.assign(in.rtInfo.valid ? in.rtInfo.frames : 0, nullptr);
		}
		if (infoOut) *infoOut = in.rtInfo;
		if (!in.rtInfo.valid) return nullptr;
		int fi = (int)(time * 1000.0 * std::max(0.01f, in.speed * speedMul) / in.rtInfo.ms) % in.rtInfo.frames;
		if (fi < 0) fi = 0;
		auto& f = in.rtFrames[fi];
		if (!f) f = s.composer.frame_(in.cfg, in.rtKey, in.rtAction, effDir, fi);
		return f;
	}

	void drawFrameQuad(gl::Shader* shader, unsigned int tex, const Frame& f, float ps, const glm::vec4& color)
	{
		float x0 = -f.ox * ps, x1 = (f.w - f.ox) * ps, y1 = f.oy * ps, y0 = -(f.h - f.oy) * ps;
		const float v[4][5] = { {x0, y0, 0, 0, 1}, {x0, y1, 0, 0, 0}, {x1, y1, 0, 1, 0}, {x1, y0, 0, 1, 1} };
		shader->setUniform(CharShader::Uniforms::color, color);
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, tex);
		glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &v[0][0]);
		glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &v[0][3]);
		glDrawArrays(GL_QUADS, 0, 4);
	}

	// ------------------------------------------------------------------ world helpers
	static glm::vec3 toWorld(const glm::vec3& p, Gnd* g) { return glm::vec3(5 * g->width + p.x, -p.y, 10 + 5 * g->height - p.z); }
	static glm::vec3 toRsw(const glm::vec3& w, Gnd* g) { return glm::vec3(w.x - 5 * g->width, -w.y, 10 + 5 * g->height - w.z); }

	static const glm::vec3 NO_HIT(std::numeric_limits<float>().max());

	// height of the walkable surface (GAT) or the ground (GND) below a world x/z position
	static bool surfaceHeight(Map* map, Gnd* gnd, bool useGat, float x, float z, float& y)
	{
		if (useGat)
			if (auto gat = map->rootNode->getComponent<Gat>())
			{
				int cx = (int)floorf(x / 5.0f), cy = (int)floorf((5.0f * gat->height + 10.0f - z) / 5.0f);
				auto r = gat->rayCast(math::Ray(glm::vec3(x, 3000, z), glm::vec3(0, -1, 0)), cx - 1, cy - 2, cx + 2, cy + 3);
				if (r != NO_HIT) { y = r.y; return true; }
			}
		auto r = gnd->rayCast(math::Ray(glm::vec3(x, 3000, z), glm::vec3(0, -1, 0)));
		if (r != NO_HIT) { y = r.y; return true; }
		return false;
	}

	// where the cursor is on the map (GAT surface when 'useGat', else the ground), with grid snapping like objects
	static bool cursorGround(MapView* mv, Gnd* gnd, bool useGat, glm::vec3& out)
	{
		glm::vec3 hit = NO_HIT;
		if (useGat)
			if (auto gat = mv->map->rootNode->getComponent<Gat>()) hit = gat->rayCast(mv->mouseRay);
		if (hit == NO_HIT) hit = gnd->rayCast(mv->mouseRay);
		if (hit == NO_HIT) return false;
		bool snap = mv->snapToGrid;
		if (ImGui::GetIO().KeyShift) snap = !snap;
		if (snap)
		{
			hit.x = glm::round((hit.x - mv->gridOffsetTranslate) / mv->gridSizeTranslate) * mv->gridSizeTranslate + mv->gridOffsetTranslate;
			hit.z = glm::round((hit.z - mv->gridOffsetTranslate) / mv->gridSizeTranslate) * mv->gridSizeTranslate + mv->gridOffsetTranslate;
			float y; if (surfaceHeight(mv->map, gnd, useGat, hit.x, hit.z, y)) hit.y = y;
		}
		out = hit;
		return true;
	}

	// direction of the frame to show: the character's own direction turned by where the camera looks from
	static int viewDir(const glm::mat4& view, int dir)
	{
		glm::vec3 right(view[0][0], view[1][0], view[2][0]);
		float fx = right.z, fz = -right.x;	// camera looking direction on the ground (x east, z south)
		float a = glm::degrees(atan2f(-fx, fz));
		int steps = (int)lroundf(a / 45.0f);
		return (((dir + 4 - steps) % 8) + 8) % 8;
	}

	// ------------------------------------------------------------------ movement
	struct Pose { glm::vec3 world; int action; int dir; float mul; };

	static float hash01(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return (x & 0xFFFFFF) / 16777216.0f; }
	static int dirOf(float dx, float dz) { int s = (int)lroundf(glm::degrees(atan2f(-dx, dz)) / 45.0f); return ((s % 8) + 8) % 8; }

	static bool walkableAt(Map* map, float x, float z)
	{
		auto gat = map->rootNode->getComponent<Gat>();
		if (!gat) return true;
		int cx = (int)floorf(x / 5.0f), cy = (int)floorf((5.0f * gat->height + 10.0f - z) / 5.0f);
		if (cx < 0 || cy < 0 || cx >= gat->width || cy >= gat->height) return false;
		int t = gat->cubes[cx][cy]->gatType;
		return t == 0 || t == 3 || t == 6;
	}

	struct Leg { glm::vec3 a, b; double t, wait; };

	static Pose poseOf(System& sys, Map* map, Gnd* gnd, const Instance& in, double now, bool frozen)
	{
		glm::vec3 home = toWorld(in.pos, gnd);
		Pose p{ home, in.action, in.dir, 1.0f };
		double tt = now - sys.pathEpoch - in.startDelay;
		if (!sys.previewMovement || frozen || !in.pathEnabled || tt < 0) return p;
		const float speed = std::max(0.5f, in.moveSpeed) * 5.0f;	// world units per second
		std::vector<Leg> legs;
		bool repeat = true;
		if (!in.path.empty())
		{
			std::vector<glm::vec3> pts{ home };
			for (auto& w : in.path) pts.push_back(toWorld(w, gnd));
			if (in.pathMode == 1) pts.push_back(home);
			else if (in.pathMode == 2) for (int i = (int)pts.size() - 2; i >= 0; i--) pts.push_back(pts[i]);
			repeat = in.pathMode != 0;
			for (size_t i = 0; i + 1 < pts.size(); i++) legs.push_back({ pts[i], pts[i + 1], glm::length(glm::vec2(pts[i + 1].x - pts[i].x, pts[i + 1].z - pts[i].z)) / speed, in.waitTime });
		}
		else if (in.wanderRadius > 0.01f)
		{
			glm::vec3 cur = home; double acc = 0;
			uint32_t seed = (uint32_t)in.id * 7919u + 13u;
			for (int k = 0; k < 400 && acc < 600; k++)
			{
				glm::vec3 target = cur; bool found = false;
				for (int tryN = 0; tryN < 5 && !found; tryN++)
				{
					float ang = hash01(seed++) * 6.2831853f, r = in.wanderRadius * 5.0f * sqrtf(hash01(seed++));
					glm::vec3 c(home.x + cosf(ang) * r, home.y, home.z + sinf(ang) * r);
					if (walkableAt(map, c.x, c.z)) { target = c; found = true; }
				}
				double w = in.waitTime * (0.3 + 1.4 * hash01(seed++)) + 0.3;
				double lt = glm::length(glm::vec2(target.x - cur.x, target.z - cur.z)) / speed;
				legs.push_back({ cur, target, lt, w });
				acc += lt + w; cur = target;
			}
			legs.push_back({ cur, home, glm::length(glm::vec2(home.x - cur.x, home.z - cur.z)) / speed, 0.0 });
		}
		if (legs.empty()) return p;
		double cyc = 0; for (auto& l : legs) cyc += l.t + l.wait;
		auto place = [&](glm::vec3 w, int dir, bool moving)
		{
			float y; if (surfaceHeight(map, gnd, sys.snapToGat, w.x, w.z, y)) w.y = y;
			p.world = w;
			if (in.faceMove) p.dir = dir;
			if (moving) { p.action = in.walkAction; if (in.syncAnim) p.mul = glm::clamp(in.moveSpeed / 6.67f, 0.3f, 3.0f); }
			return p;
		};
		if (repeat) { if (cyc > 0.001) tt = fmod(tt, cyc); }
		else if (tt >= cyc - legs.back().wait) { auto& l = legs.back(); return place(l.b, dirOf(l.b.x - l.a.x, l.b.z - l.a.z), false); }
		for (auto& l : legs)
		{
			int d = dirOf(l.b.x - l.a.x, l.b.z - l.a.z);
			if (l.t > 0.0001 && l.a != l.b) d = dirOf(l.b.x - l.a.x, l.b.z - l.a.z); else d = p.dir;
			if (tt < l.t) { float f = l.t > 0 ? (float)(tt / l.t) : 1.0f; return place(l.a + (l.b - l.a) * f, d, true); }
			tt -= l.t;
			if (tt < l.wait) return place(l.b, d, false);
			tt -= l.wait;
		}
		return p;
	}

	// ------------------------------------------------------------------ rendering
	void System::render(MapView* mv, BrowEdit* be)
	{
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		if (!gnd) return;
		auto& L = layer(mv->map, be);
		if (!L.items.empty() && !scanStarted) ensureReady(be);	// sprites from the sidecar file are shown in every mode
		if (!db.ready) return;
		impl->freeDropped(*this);
		impl->ghostValid = false;
		bool ghost = (placing || browserDrag || browserDrop) && be->editMode == BrowEdit::EditMode::Sprite && mv->hovered;
		if (ghost)
		{
			glm::vec3 hit;
			if (cursorGround(mv, gnd, snapToGat, hit))
			{
				impl->ghostValid = true; impl->ghostWorld = hit;
				draft.pos = toRsw(hit, gnd);
			}
		}
		if (L.items.empty() && !(ghost && impl->ghostValid)) return;

		if (!impl->shader)
		{
			impl->shader = util::ResourceManager<gl::Shader>::load<CharShader>();
			glGenTextures(1, &impl->whiteTex);
			unsigned char white[4] = { 255,255,255,255 };
			glBindTexture(GL_TEXTURE_2D, impl->whiteTex);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		}
		auto shader = impl->shader;
		const glm::mat4& view = mv->nodeRenderContext.viewMatrix;
		shader->use();
		shader->setUniform(CharShader::Uniforms::projectionMatrix, mv->nodeRenderContext.projectionMatrix);
		shader->setUniform(CharShader::Uniforms::cameraMatrix, view);
		shader->setUniform(CharShader::Uniforms::s_texture, 0);
		glEnableVertexAttribArray(0); glEnableVertexAttribArray(1);
		glDisableVertexAttribArray(2); glDisableVertexAttribArray(3); glDisableVertexAttribArray(4);
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glEnable(GL_BLEND);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		glEnable(GL_DEPTH_TEST);
		glDepthMask(1);
		glDisable(GL_CULL_FACE);

		double time = ImGui::GetTime();
		glm::vec3 camPos = glm::vec3(glm::inverse(view)[3]);
		glm::vec3 fwd = -glm::vec3(view[0][2], view[1][2], view[2][2]);
		const float basePs = 5.0f / 35.0f;

		struct Item { const Instance* in; glm::vec3 world; float depth; bool ghost; int dir; int action; float mul; };
		std::vector<Item> order;
		for (auto& in : L.items)
			if (in.visible)
			{
				Pose po = poseOf(*this, mv->map, gnd, in, time, impl->dragging && impl->dragOrig.count(in.id));
				glm::vec3 w = po.world + glm::vec3(0, in.elevation, 0);
				order.push_back({ &in, w, glm::dot(w - camPos, fwd), false, po.dir, po.action, po.mul });
			}
		Instance ghostInst;
		if (ghost && impl->ghostValid)
		{
			ghostInst.cfg = draft.cfg; ghostInst.action = draft.action; ghostInst.dir = draft.dir; ghostInst.speed = draft.speed; ghostInst.scale = draft.scale;
			glm::vec3 w = impl->ghostWorld + glm::vec3(0, draft.elevation, 0);
			order.push_back({ &ghostInst, w, glm::dot(w - camPos, fwd), true, draft.dir, draft.action, 1.0f });
		}
		std::sort(order.begin(), order.end(), [](const Item& a, const Item& b) { return a.depth > b.depth; });

		// shadows first (flat ellipse on the ground)
		glDepthMask(0);	// the shadow must not hide the sprites standing in the same plane
		if (showShadow)
			for (auto& it : order)
			{
				float r = 6.0f * basePs * it.in->scale * 1.8f;
				glm::vec3 gw = it.world - glm::vec3(0, it.in->elevation, 0);
				shader->setUniform(CharShader::Uniforms::modelMatrix, glm::translate(glm::mat4(1.0f), gw));
				shader->setUniform(CharShader::Uniforms::color, glm::vec4(0, 0, 0, it.ghost ? 0.15f : 0.3f));
				shader->setUniform(CharShader::Uniforms::depthBias, r * 1.2f);
				glBindTexture(GL_TEXTURE_2D, impl->whiteTex);
				std::vector<float> fan; fan.reserve(24 * 5);
				for (int k = 0; k < 24; k++)
				{
					float a = k / 24.0f * 6.2831853f;
					glm::vec3 off = glm::vec3(view * glm::vec4(cosf(a) * r, 0, sinf(a) * r, 0));
					fan.insert(fan.end(), { off.x, off.y, 0, 0, 0 });
				}
				glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &fan[0]);
				glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &fan[3]);
				glDrawArrays(GL_POLYGON, 0, 24);
			}

		glDepthMask(1);
		for (auto& it : order)
		{
			const Instance& in = *it.in;
			int eff = viewDir(view, it.dir);
			auto f = impl->frameFor(*this, in, eff, time, nullptr, it.action, it.mul);
			if (!f || f->w <= 0) continue;
			unsigned int t = impl->tex(*f);
			if (!t) continue;
			shader->setUniform(CharShader::Uniforms::modelMatrix, glm::translate(glm::mat4(1.0f), it.world));
			float ps = basePs * in.scale;
			shader->setUniform(CharShader::Uniforms::depthBias, f->h * ps * 0.8f);
			bool sel = !it.ghost && L.selected.count(in.id);
			drawFrameQuad(shader, t, *f, ps, it.ghost ? glm::vec4(1, 1, 1, 0.6f) : glm::vec4(1, 1, 1, 1));
			if (sel)
			{
				float x0 = -f->ox * ps, x1 = (f->w - f->ox) * ps, y1 = f->oy * ps, y0 = -(f->h - f->oy) * ps;
				const float v[4][5] = { {x0, y0, 0, 0, 0}, {x0, y1, 0, 0, 0}, {x1, y1, 0, 0, 0}, {x1, y0, 0, 0, 0} };
				shader->setUniform(CharShader::Uniforms::color, glm::vec4(1.0f, 0.85f, 0.2f, 1));
				glBindTexture(GL_TEXTURE_2D, impl->whiteTex);
				glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &v[0][0]);
				glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &v[0][3]);
				glLineWidth(2.0f);
				glDrawArrays(GL_LINE_LOOP, 0, 4);
				glLineWidth(1.0f);
			}
		}
		// paths of the selected sprites (and of all when 'Show paths' is on)
		glDisable(GL_DEPTH_TEST);
		shader->setUniform(CharShader::Uniforms::depthBias, 0.0f);
		glBindTexture(GL_TEXTURE_2D, impl->whiteTex);
		for (auto& in : L.items)
		{
			bool sel = L.selected.count(in.id) > 0;
			if (!in.visible || !in.pathEnabled || !(sel || showPaths)) continue;
			glm::vec3 home = toWorld(in.pos, gnd);
			if (sel && in.path.empty() && in.wanderRadius > 0.01f)
			{	// wander area
				std::vector<float> ring;
				shader->setUniform(CharShader::Uniforms::modelMatrix, glm::translate(glm::mat4(1.0f), home));
				shader->setUniform(CharShader::Uniforms::color, glm::vec4(0.4f, 0.8f, 1.0f, 0.9f));
				for (int k = 0; k < 48; k++)
				{
					float a = k / 48.0f * 6.2831853f;
					glm::vec3 off = glm::vec3(view * glm::vec4(cosf(a) * in.wanderRadius * 5.0f, 0, sinf(a) * in.wanderRadius * 5.0f, 0));
					ring.insert(ring.end(), { off.x, off.y, 0, 0, 0 });
				}
				glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &ring[0]);
				glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &ring[3]);
				glLineWidth(2.0f); glDrawArrays(GL_LINE_LOOP, 0, 48); glLineWidth(1.0f);
			}
			if (in.path.empty()) continue;
			std::vector<glm::vec3> pts{ home };
			for (auto& w : in.path) pts.push_back(toWorld(w, gnd) + glm::vec3(0, 0.3f, 0));
			pts[0].y += 0.3f;
			if (in.pathMode == 1) pts.push_back(pts[0]);
			std::vector<float> line;
			for (auto& p : pts) { glm::vec3 off = glm::vec3(view * glm::vec4(p - pts[0], 0)); line.insert(line.end(), { off.x, off.y, 0, 0, 0 }); }
			shader->setUniform(CharShader::Uniforms::modelMatrix, glm::translate(glm::mat4(1.0f), pts[0]));
			shader->setUniform(CharShader::Uniforms::color, sel ? glm::vec4(0.5f, 1.0f, 0.4f, 1) : glm::vec4(0.5f, 1.0f, 0.4f, 0.5f));
			glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &line[0]);
			glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &line[3]);
			glLineWidth(sel ? 2.5f : 1.5f); glDrawArrays(GL_LINE_STRIP, 0, (int)pts.size()); glLineWidth(1.0f);
			if (!sel) continue;
			for (size_t k = 0; k < in.path.size() + 1; k++)
			{
				glm::vec3 c = k == 0 ? pts[0] : pts[k];
				const float h = 0.9f;
				const float q[4][5] = { {-h,-h,0,0,0}, {-h,h,0,0,0}, {h,h,0,0,0}, {h,-h,0,0,0} };
				shader->setUniform(CharShader::Uniforms::modelMatrix, glm::translate(glm::mat4(1.0f), c));
				shader->setUniform(CharShader::Uniforms::color, k == 0 ? glm::vec4(1, 0.6f, 0.1f, 1) : k == in.path.size() ? glm::vec4(1, 0.3f, 0.3f, 1) : glm::vec4(0.4f, 1, 0.4f, 1));
				glVertexAttribPointer(0, 3, GL_FLOAT, false, 5 * sizeof(float), &q[0][0]);
				glVertexAttribPointer(1, 2, GL_FLOAT, false, 5 * sizeof(float), &q[0][3]);
				glDrawArrays(GL_QUADS, 0, 4);
			}
		}
		glEnable(GL_DEPTH_TEST);
		glUseProgram(0);
	}

	// ------------------------------------------------------------------ interaction
	static Instance instanceFromDraft(const Draft& d)
	{
		Instance i;
		i.name = d.name; i.cfg = d.cfg; i.action = d.action; i.dir = d.dir; i.speed = d.speed; i.scale = d.scale;
		i.pos = d.pos; i.elevation = d.elevation; i.visible = d.visible; i.locked = d.locked;
		return i;
	}

	static int pick(System& sys, System::Impl& im, MapLayer& L, MapView* mv, double time)
	{
		const glm::mat4& view = mv->nodeRenderContext.viewMatrix;
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		glm::vec3 right(view[0][0], view[1][0], view[2][0]), up(view[0][1], view[1][1], view[2][1]);
		glm::vec3 fwd = -glm::vec3(view[0][2], view[1][2], view[2][2]);
		const float basePs = 5.0f / 35.0f;
		int best = 0; float bestT = std::numeric_limits<float>::max();
		for (auto& in : L.items)
		{
			if (!in.visible || in.locked) continue;
			Pose po = poseOf(sys, mv->map, gnd, in, time, im.dragging && im.dragOrig.count(in.id));
			auto f = im.frameFor(sys, in, viewDir(view, po.dir), time, nullptr, po.action, po.mul);
			if (!f || f->w <= 0) continue;
			glm::vec3 w = po.world + glm::vec3(0, in.elevation, 0);
			float denom = glm::dot(mv->mouseRay.dir, fwd);
			if (fabsf(denom) < 1e-5f) continue;
			float t = glm::dot(w - mv->mouseRay.origin, fwd) / denom;
			if (t < 0 || t > bestT) continue;
			glm::vec3 p = mv->mouseRay.origin + mv->mouseRay.dir * t - w;
			float ps = basePs * in.scale;
			int px = (int)floorf(glm::dot(p, right) / ps) + f->ox, py = f->oy - (int)ceilf(glm::dot(p, up) / ps);
			bool hit = false;
			for (int dy = -2; dy <= 2 && !hit; dy++)		// a little tolerance so thin parts can be grabbed
				for (int dx = -2; dx <= 2 && !hit; dx++)
				{
					int x = px + dx, y = py + dy;
					if (x >= 0 && y >= 0 && x < f->w && y < f->h && f->rgba[((size_t)y * f->w + x) * 4 + 3] > 20) hit = true;
				}
			if (hit) { best = in.id; bestT = t; }
		}
		return best;
	}

	void System::postRender(MapView* mv, BrowEdit* be)
	{
		if (!db.ready) return;
		auto gnd = mv->map->rootNode->getComponent<Gnd>();
		if (!gnd) return;
		auto& L = layer(mv->map, be);
		auto& io = ImGui::GetIO();
		double time = ImGui::GetTime();
		bool typing = io.WantTextInput;

		if (browserDrop && mv->hovered && impl->ghostValid)
		{	// a sprite dragged out of the Sprite Browser was dropped on the map
			browserDrop = false;
			MapLayer before = L;
			Instance in = instanceFromDraft(draft);
			in.id = L.nextId++; in.pos = toRsw(impl->ghostWorld, gnd);
			L.items.push_back(in); L.selected = { in.id };
			pushLayerAction(mv->map, be, L, before, "Add sprite " + in.name);
			return;
		}
		if (placing)
		{
			if (!typing && ImGui::IsKeyPressed(GLFW_KEY_ESCAPE)) { placing = false; return; }
			if (mv->hovered && ImGui::IsMouseReleased(1) && io.MouseDragMaxDistanceSqr[1] < 5) { placing = false; return; }
			if (mv->hovered && impl->ghostValid && ImGui::IsMouseClicked(0))
			{
				MapLayer before = L;
				Instance in = instanceFromDraft(draft);
				in.id = L.nextId++;
				in.pos = toRsw(impl->ghostWorld, gnd);
				L.items.push_back(in);
				L.selected = { in.id };
				pushLayerAction(mv->map, be, L, before, "Add sprite " + in.name);
				if (!io.KeyShift) placing = false;
			}
			return;
		}

		if (impl->dragging)
		{
			if (ImGui::IsMouseDown(0))
			{
				glm::vec3 hit;
				if (cursorGround(mv, gnd, snapToGat, hit))
				{
					glm::vec3 d = hit - impl->dragStartWorld;
					if (glm::length(d) > 0.01f) impl->dragMoved = true;
					for (auto& kv : impl->dragOrig)
					{
						auto* in = L.find(kv.first);
						if (!in) continue;
						glm::vec3 w = toWorld(kv.second, gnd) + glm::vec3(d.x, 0, d.z);
						float hy;
						if (surfaceHeight(mv->map, gnd, snapToGat, w.x, w.z, hy)) w.y = hy; else w.y = -kv.second.y;
						in->pos = toRsw(w, gnd);
					}
				}
			}
			else
			{
				impl->dragging = false;
				if (impl->dragMoved)
					pushLayerAction(mv->map, be, L, impl->dragBefore, "Move sprite(s)");
			}
			return;
		}

		if (mv->hovered && ImGui::IsMouseClicked(0))
		{
			int hit = pick(*this, *impl, L, mv, time);
			if (hit)
			{
				if (ImGui::IsMouseDoubleClicked(0))
				{
					L.selected = { hit };
					auto* in = L.find(hit);
					draft.cfg = in->cfg; draft.name = in->name; draft.action = in->action; draft.dir = in->dir; draft.speed = in->speed; draft.scale = in->scale;
					draft.pos = in->pos; draft.elevation = in->elevation; draft.visible = in->visible; draft.locked = in->locked;
					impl->editingId = hit;
				}
				else
				{
					if (io.KeyCtrl) { if (L.selected.count(hit)) L.selected.erase(hit); else L.selected.insert(hit); }
					else if (io.KeyShift) L.selected.insert(hit);
					else if (!L.selected.count(hit)) L.selected = { hit };
					if (L.selected.count(hit) && !io.KeyCtrl)
					{
						glm::vec3 h;
						if (cursorGround(mv, gnd, snapToGat, h))
						{
							impl->dragging = true; impl->dragMoved = false; impl->dragStartWorld = h; impl->dragBefore = L;
							impl->dragOrig.clear();
							for (int id : L.selected) if (auto* in = L.find(id)) if (!in->locked) impl->dragOrig[id] = in->pos;
							if (impl->dragOrig.empty()) impl->dragging = false;
						}
					}
				}
			}
			else if (io.KeyShift && L.selected.size() == 1 && L.find(*L.selected.begin()) && !L.find(*L.selected.begin())->locked)
			{	// Shift + left click on the ground: add a waypoint to the selected sprite
				glm::vec3 w;
				if (cursorGround(mv, gnd, snapToGat, w))
				{
					MapLayer before = L;
					L.find(*L.selected.begin())->path.push_back(toRsw(w, gnd));
					L.find(*L.selected.begin())->pathEnabled = true;
					pushLayerAction(mv->map, be, L, before, "Add waypoint");
				}
			}
			else if (io.KeyCtrl)
			{	// Ctrl + left click on the ground: add the sprite from the panel right here
				glm::vec3 w;
				if (cursorGround(mv, gnd, snapToGat, w))
				{
					MapLayer before = L;
					Instance in = instanceFromDraft(draft);
					in.id = L.nextId++; in.pos = toRsw(w, gnd);
					L.items.push_back(in); L.selected = { in.id };
					pushLayerAction(mv->map, be, L, before, "Add sprite " + in.name);
				}
			}
			else if (!io.KeyShift)
				L.selected.clear();
		}

		if (!typing && mv->hovered)
		{
			if (ImGui::IsKeyPressed(GLFW_KEY_DELETE) && !L.selected.empty())
			{
				MapLayer before = L;
				L.items.erase(std::remove_if(L.items.begin(), L.items.end(), [&](const Instance& i) { return L.selected.count(i.id) && !i.locked; }), L.items.end());
				L.selected.clear();
				pushLayerAction(mv->map, be, L, before, "Delete sprite(s)");
			}
			if (ImGui::IsKeyPressed(GLFW_KEY_ESCAPE)) L.selected.clear();
		}
	}
}
