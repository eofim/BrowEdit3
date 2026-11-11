#include "Character.h"
#include "Text.h"
#include <algorithm>
#include <cstring>

namespace sprite
{
	const ActionDef actionDefs[] = {
		{0,"Idle"},{1,"Walk"},{2,"Sit"},{3,"Pick up"},{4,"Ready (stand by)"},{5,"Attack 1"},{6,"Hit"},{7,"Frozen 1"},
		{8,"Dead"},{9,"Frozen 2"},{10,"Attack 2"},{11,"Attack 3"},{12,"Cast"} };
	const int actionDefCount = 13;
	const char* directionNames[8] = { "South","South-West","West","North-West","North","North-East","East","South-East" };

	std::string CharacterConfig::key() const { return toJson().dump(); }

	nlohmann::json CharacterConfig::toJson() const
	{
		nlohmann::json j;
		j["headMode"] = headMode; j["kind"] = kind; j["mob"] = mob; j["job"] = job; j["gender"] = gender; j["hairStyle"] = hairStyle; j["hairColor"] = hairColor; j["bodyPalette"] = bodyPalette;
		j["head"] = { head[0], head[1], head[2] }; j["costume"] = { costume[0], costume[1], costume[2] };
		j["robe"] = robe; j["weapon"] = weapon; j["shield"] = shield; j["customWing"] = customWing;
		nlohmann::json o = nlohmann::json::array();
		for (int i = 0; i < 8; i++) o.push_back({ wingOff[i][0], wingOff[i][1] });
		j["wingOff"] = o;
		return j;
	}
	CharacterConfig CharacterConfig::fromJson(const nlohmann::json& j)
	{
		CharacterConfig c;
		c.headMode = j.value("headMode", 0); c.kind = j.value("kind", 0); c.mob = j.value("mob", std::string()); c.job = j.value("job", c.job); c.gender = j.value("gender", 0); c.hairStyle = j.value("hairStyle", 1); c.hairColor = j.value("hairColor", 0);
		c.bodyPalette = j.value("bodyPalette", -1);
		if (j.contains("head")) for (int i = 0; i < 3; i++) c.head[i] = j["head"][i];
		if (j.contains("costume")) for (int i = 0; i < 3; i++) c.costume[i] = j["costume"][i];
		c.robe = j.value("robe", 0); c.weapon = j.value("weapon", 0); c.shield = j.value("shield", ""); c.customWing = j.value("customWing", "");
		if (j.contains("wingOff")) for (int i = 0; i < 8 && i < (int)j["wingOff"].size(); i++) { c.wingOff[i][0] = j["wingOff"][i][0]; c.wingOff[i][1] = j["wingOff"][i][1]; }
		return c;
	}

	std::shared_ptr<ro::Spr> Composer::loadSpr(const std::string& path)
	{
		auto it = spr.find(path); if (it != spr.end()) return it->second;
		std::shared_ptr<ro::Spr> r; std::vector<uint8_t> d;
		if (db.exists(path) && src.read(db.actual(path), d)) { auto s = std::make_shared<ro::Spr>(); if (s->load(d)) r = s; }
		return spr[path] = r;
	}
	std::shared_ptr<ro::Act> Composer::loadAct(const std::string& path)
	{
		auto it = act.find(path); if (it != act.end()) return it->second;
		std::shared_ptr<ro::Act> r; std::vector<uint8_t> d;
		if (db.exists(path) && src.read(db.actual(path), d)) { auto s = std::make_shared<ro::Act>(); if (s->load(d)) r = s; }
		return act[path] = r;
	}
	std::shared_ptr<ro::Palette> Composer::loadPal(const std::string& path)
	{
		auto it = pal.find(path); if (it != pal.end()) return it->second;
		std::shared_ptr<ro::Palette> r; std::vector<uint8_t> d;
		if (db.exists(path) && src.read(db.actual(path), d)) { auto s = std::make_shared<ro::Palette>(); if (s->load(d)) r = s; }
		return pal[path] = r;
	}
	std::shared_ptr<ro::Imf> Composer::loadImf(const std::string& path)
	{
		auto it = imf.find(path); if (it != imf.end()) return it->second;
		std::shared_ptr<ro::Imf> r; std::vector<uint8_t> d;
		if (db.exists(path) && src.read(db.actual(path), d)) { auto s = std::make_shared<ro::Imf>(); if (s->load(d)) r = s; }
		return imf[path] = r;
	}

	std::vector<std::string> Composer::warnings(const CharacterConfig& c)
	{
		std::vector<std::string> w;
		int g = c.gender;
		auto need = [&](const std::string& what, const std::string& base)
		{
			if (!db.exists(base + ".spr") || !db.exists(base + ".act")) w.push_back("Missing " + what + ": " + base);
		};
		if (c.kind != 0) { need(c.kind == 1 ? "monster" : "npc", Db::mobPath(c.kind, c.mob)); return w; }
		need("body", Db::bodyPath(c.job, g));
		if (c.hairStyle > 0) need("head", Db::headPath(c.hairStyle, g));
		for (int i = 0; i < 3; i++)
		{
			int id = c.costume[i] ? c.costume[i] : c.head[i];
			if (!id) continue;
			if (auto e = db.gear(db.headgear, id)) need("headgear", Db::headgearPath(*e, g)); else w.push_back("Headgear id " + std::to_string(id) + " has no sprite name in the client tables");
		}
		if (c.robe) { if (auto e = db.gear(db.robes, c.robe)) need("garment", Db::robePath(*e, c.job, g)); }
		if (c.weapon) { if (auto e = db.gear(db.weapons, c.weapon)) need("weapon", Db::weaponPath(*e, c.job, g)); }
		if (!c.shield.empty()) need("shield", Db::shieldPath(c.shield, c.job, g));
		if (!c.customWing.empty()) need("custom wing", c.customWing);
		return w;
	}

	Composer::Set Composer::build(const CharacterConfig& c, int dir)
	{
		Set s;
		int g = c.gender;
		auto add = [&](const std::string& base, int attach, float order, std::shared_ptr<ro::Palette> p, const char* what) -> Layer*
		{
			auto sp = loadSpr(base + ".spr"); auto ac = loadAct(base + ".act");
			if (!sp || !ac) return nullptr;
			Layer l; l.s = sp; l.a = ac; l.p = p; l.attach = attach; l.order = order; l.what = what;
			s.layers.push_back(l);
			return &s.layers.back();
		};
		if (c.kind != 0) { add(Db::mobPath(c.kind, c.mob), 0, 0, nullptr, "body"); return s; }
		std::shared_ptr<ro::Palette> bp;
		if (c.bodyPalette >= 0) bp = loadPal(Db::bodyPalPath(c.job, g, c.bodyPalette));
		add(Db::bodyPath(c.job, g), 0, 0, bp, "body");
		s.imf = loadImf(Db::imfPath(c.job, g));
		if (c.hairStyle > 0)
		{
			std::shared_ptr<ro::Palette> hp = loadPal(Db::hairPalPath(c.hairStyle, g, c.hairColor));
			add(Db::headPath(c.hairStyle, g), 1, 50, hp, "head");
		}
		const int slotDefault[3] = { db.prioTop, db.prioMid, db.prioBottom };
		for (int i = 0; i < 3; i++)
		{
			int id = c.costume[i] ? c.costume[i] : c.head[i];
			if (!id) continue;
			auto e = db.gear(db.headgear, id); if (!e) continue;
			float pr = (float)slotDefault[i];
			auto it = db.layerItems.find(id);
			if (it != db.layerItems.end())
			{
				auto d = it->second.find(dir);
				if (d != it->second.end()) pr = (float)d->second;
				else { auto df = it->second.find(-1); if (df != it->second.end()) pr = (float)df->second; }
			}
			if (add(Db::headgearPath(*e, g), 1, pr, nullptr, "gear")) {}
		}
		bool facingAway = (dir == 3 || dir == 4 || dir == 5);
		if (c.robe)
			if (auto e = db.gear(db.robes, c.robe))
				add(Db::robePath(*e, c.job, g), 1, facingAway ? (float)db.prioRobe : -200.0f, nullptr, "robe");
		if (!c.customWing.empty())
			if (auto* l = add(c.customWing, 1, facingAway ? (float)db.prioRobe : -190.0f, nullptr, "custom"))
			{ l->extraX = c.wingOff[dir][0]; l->extraY = c.wingOff[dir][1]; }
		if (c.weapon)
			if (auto e = db.gear(db.weapons, c.weapon))
				add(Db::weaponPath(*e, c.job, g), 0, facingAway ? -10.0f : 20.0f, nullptr, "weapon");
		if (!c.shield.empty())
			add(Db::shieldPath(c.shield, c.job, g), 0, facingAway ? -11.0f : 21.0f, nullptr, "shield");
		return s;
	}

	AnimInfo Composer::info(const CharacterConfig& c, int action, int dir)
	{
		AnimInfo i;
		auto body = loadAct((c.kind != 0 ? Db::mobPath(c.kind, c.mob) : Db::bodyPath(c.job, c.gender)) + ".act");
		int a = action * 8 + dir;
		if (body && body->actions.size() < 8 && !body->actions.empty()) a = std::min(action, (int)body->actions.size() - 1);	// sprites without direction sets
		if (!body || a >= (int)body->actions.size() || body->actions[a].frames.empty()) return i;
		i.valid = true; i.frames = (int)body->actions[a].frames.size();
		i.ms = std::max(20.0f, body->actions[a].interval * 25.0f);
		return i;
	}

	std::shared_ptr<Frame> Composer::frame_(const CharacterConfig& c, const std::string& cfgKey, int action, int dir, int fr)
	{
		if (c.headMode == 4) fr = 0;	// None: no animation at all, the first frame stays
		std::string key = cfgKey + "|" + std::to_string(action) + "|" + std::to_string(dir) + "|" + std::to_string(fr);
		auto it = frames.find(key);
		if (it != frames.end()) return it->second;
		if (frames.size() > 3000) clearFrames();

		Set s = build(c, dir);
		auto f = std::make_shared<Frame>();
		if (s.layers.empty() || s.layers[0].what != "body") { frames[key] = f; return f; }
		int a = action * 8 + dir;
		if (s.layers[0].a->actions.size() < 8 && !s.layers[0].a->actions.empty()) a = std::min(action, (int)s.layers[0].a->actions.size() - 1);

		const int W = 480, H = 480, OX = 240, OY = 340;
		ro::Canvas cv(W, H, OX, OY);
		auto anchorOf = [&](const Layer& l, int& x, int& y, int fr)
		{
			x = y = 0;
			if (a < (int)l.a->actions.size() && !l.a->actions[a].frames.empty())
			{
				auto& F = l.a->actions[a].frames[fr % l.a->actions[a].frames.size()];
				if (!F.anchors.empty()) { x = F.anchors[0].x; y = F.anchors[0].y; }
			}
		};
		int bx = 0, by = 0; anchorOf(s.layers[0], bx, by, fr);
		// body / head order through the IMF (lower value = in front); without one the head is in front
		bool headBehind = false;
		if (s.imf)
		{
			auto* b = s.imf->get(0, a, fr); auto* h = s.imf->get(1, a, fr);
			if (b && h && h->priority > b->priority) headBehind = true;
		}
		for (auto& l : s.layers)
		{
			if (l.what == "head" && headBehind) l.order = -50;
			if (l.what == "gear" && headBehind) l.order = -100 + l.order * 0.001f;
		}
		std::vector<Layer*> sorted;
		for (auto& l : s.layers) sorted.push_back(&l);
		std::stable_sort(sorted.begin(), sorted.end(), [](Layer* x, Layer* y) { return x->order < y->order; });
		for (auto* l : sorted)
		{
			int dx = 0, dy = 0;
			// steady head: the head and what sits on it keep one pose while the body goes on
			int lf = (c.headMode > 0 && (l->what == "head" || l->what == "gear")) ? (c.headMode == 4 ? 0 : c.headMode - 1) : fr;
			if (l->attach == 1)
			{
				int ax, ay; anchorOf(*l, ax, ay, lf);
				dx = bx - ax; dy = by - ay;
			}
			ro::Part p; p.spr = l->s.get(); p.act = l->a.get(); p.pal = l->p.get();
			ro::drawFrame(cv, p, a, lf, dx + l->extraX, dy + l->extraY);
		}
		int x0 = W, y0 = H, x1 = -1, y1 = -1;
		for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) if (cv.rgba[((size_t)y * W + x) * 4 + 3] > 0) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
		if (x1 < 0) { frames[key] = f; return f; }
		f->w = x1 - x0 + 1; f->h = y1 - y0 + 1; f->ox = OX - x0; f->oy = OY - y0;
		f->rgba.resize((size_t)f->w * f->h * 4);
		for (int y = 0; y < f->h; y++) memcpy(&f->rgba[(size_t)y * f->w * 4], &cv.rgba[((size_t)(y + y0) * W + x0) * 4], (size_t)f->w * 4);
		frames[key] = f;
		return f;
	}
}
