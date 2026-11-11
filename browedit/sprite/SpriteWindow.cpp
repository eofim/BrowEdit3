#include <glad/gl.h>
#include "SpriteInternal.h"
#include "Text.h"
#include <browedit/BrowEdit.h>
#include <browedit/Map.h>
#include <browedit/MapView.h>
#include <browedit/Node.h>
#include <browedit/components/Gnd.h>
#include <imgui.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>

namespace sprite
{
	namespace
	{
		std::string pretty(std::string s)
		{
			bool up = true;
			for (auto& c : s)
			{
				if (c == '_') { c = ' '; up = true; }
				else if (c >= 'A' && c <= 'Z') { if (!up) c = (char)(c + 32); up = false; }
				else if (c >= 'a' && c <= 'z') { if (up) c = (char)(c - 32); up = false; }
				else up = false;
			}
			return s;
		}

		bool inputStr(const char* label, std::string& s, int max = 96)
		{
			std::vector<char> buf(max, 0);
			memcpy(buf.data(), s.c_str(), std::min((size_t)max - 1, s.size()));
			if (ImGui::InputText(label, buf.data(), max)) { s = buf.data(); return true; }
			return false;
		}

		bool containsNoCase(const std::string& hay, const std::string& needle)
		{
			if (needle.empty()) return true;
			return lower(hay).find(lower(needle)) != std::string::npos;
		}

		// searchable dropdown; 'sel' is an Opt id. allowNone adds a "None" entry that sets sel to noneValue.
		bool searchCombo(const char* label, const std::string& preview, const std::vector<Opt>& opts, int& sel, bool showUnavailable, bool allowNone, int noneValue = 0)
		{
			bool changed = false;
			if (ImGui::BeginCombo(label, preview.c_str()))
			{
				static std::map<ImGuiID, std::string> filters;
				auto& f = filters[ImGui::GetID(label)];
				if (ImGui::IsWindowAppearing()) { ImGui::SetKeyboardFocusHere(); f.clear(); }
				ImGui::SetNextItemWidth(-1);
				inputStr("##search", f, 64);
				if (allowNone && ImGui::Selectable("None", sel == noneValue)) { sel = noneValue; changed = true; }
				std::vector<const Opt*> shown;
				for (auto& o : opts)
					if ((o.ok || showUnavailable) && containsNoCase(o.label, f)) shown.push_back(&o);
				ImGuiListClipper clip;
				clip.Begin((int)shown.size());
				while (clip.Step())
					for (int i = clip.DisplayStart; i < clip.DisplayEnd; i++)
					{
						auto* o = shown[i];
						if (!o->ok) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.4f, 0.4f, 1));
						if (ImGui::Selectable((o->label + (o->ok ? "" : " (missing)")).c_str(), sel == o->id)) { sel = o->id; changed = true; }
						if (!o->ok) ImGui::PopStyleColor();
					}
				if (shown.empty()) ImGui::TextDisabled("Nothing found");
				ImGui::EndCombo();
			}
			return changed;
		}
	}

	// job dropdown sorted into groups (1st class, 2nd class, ...), with search
	static bool jobCombo(const char* label, const std::string& preview, const Db& db, const std::vector<Opt>& labels, int gender, int& sel, bool showUnavailable)
	{
		bool changed = false;
		if (ImGui::BeginCombo(label, preview.c_str()))
		{
			static std::string f;
			bool appearing = ImGui::IsWindowAppearing();
			if (appearing) { ImGui::SetKeyboardFocusHere(); f.clear(); }
			ImGui::SetNextItemWidth(-1);
			inputStr("##jobsearch", f, 64);
			int curGroup = sel >= 0 && sel < (int)db.jobs.size() ? db.jobs[sel].group : -1;
			for (int grp = 0; grp < 8; grp++)
			{
				std::vector<int> idx;
				for (int i = 0; i < (int)db.jobs.size(); i++)
					if (db.jobs[i].group == grp && (db.jobs[i].has[gender] || showUnavailable) && containsNoCase(labels[i].label, f)) idx.push_back(i);
				if (idx.empty()) continue;
				if (!f.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				else if (appearing) ImGui::SetNextItemOpen(grp == curGroup, ImGuiCond_Always);
				if (ImGui::CollapsingHeader((std::string(Db::groupNames[grp]) + " (" + std::to_string(idx.size()) + ")").c_str()))
					for (int i : idx)
						if (ImGui::Selectable((labels[i].label + "##j" + std::to_string(i)).c_str(), i == sel)) { sel = i; changed = true; }
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	static std::string actionLabel(const CharacterConfig& c, int a)
	{
		if (c.kind == 0) return actionDefs[std::min(std::max(a, 0), actionDefCount - 1)].name;
		static const char* m[] = { "Idle", "Walk", "Attack", "Hit", "Die" };
		if (c.kind == 1 && a >= 0 && a < 5) return m[a];
		return "Action " + std::to_string(a);
	}

	// ------------------------------------------------------------------ option lists (discovered from the data)
	void System::Impl::ensureOpts(System& s)
	{
		if (optsVersion == scanVersion || !s.db.ready) return;
		optsVersion = scanVersion;
		jobOpts.clear();
		for (size_t i = 0; i < s.db.jobs.size(); i++)
		{
			auto& j = s.db.jobs[i];
			std::string l = j.label;
			if (std::all_of(j.sprite.begin(), j.sprite.end(), [](unsigned char ch) { return ch < 0x80; }))
				l += "  [" + j.sprite + "]";	// Korean sprite names are not shown (the UI font has no Hangul)
			if (j.has[0] != j.has[1]) l += j.has[0] ? " (male only)" : " (female only)";
			jobOpts.push_back({ (int)i, l, true });
		}
		for (int g = 0; g < 2; g++)
		{
			headgearOpts[g].clear(); robeOpts[g].clear(); weaponOpts[g].clear();
			for (auto& e : s.db.headgear) headgearOpts[g].push_back({ e.id, pretty(e.label) + " [" + std::to_string(e.id) + "]", e.has[g] });
			for (auto& e : s.db.robes) robeOpts[g].push_back({ e.id, pretty(e.label) + " [" + std::to_string(e.id) + "]", e.has[g] });
			for (auto& e : s.db.weapons) weaponOpts[g].push_back({ e.id, pretty(e.label.rfind("WEAPONTYPE_", 0) == 0 ? e.label.substr(11) : e.label) + " [" + std::to_string(e.id) + "]", e.has[g] });
		}
		for (int k = 1; k <= 2; k++)
		{
			mobOpts[k].clear();
			auto& list = k == 1 ? s.db.monsters : s.db.npcs;
			for (size_t i = 0; i < list.size(); i++) mobOpts[k].push_back({ (int)i + 1, list[i].label + (list[i].id >= 0 ? " [" + std::to_string(list[i].id) + "]" : ""), true });
		}
		thumbs.clear(); mobThumbs.clear();
	}

	void System::Impl::applyJobChange(System& s, CharacterConfig& c, const std::string& newJob, int newGender)
	{
		auto& db = s.db;
		std::string removed;
		c.job = newJob; c.gender = newGender;
		int g = newGender;
		if (!db.hairStyles[g].empty() && std::find(db.hairStyles[g].begin(), db.hairStyles[g].end(), c.hairStyle) == db.hairStyles[g].end()) { c.hairStyle = db.hairStyles[g][0]; removed += "hairstyle "; }
		auto colors = db.hairColors[g].find(c.hairStyle);
		if (colors != db.hairColors[g].end() && !colors->second.empty() && std::find(colors->second.begin(), colors->second.end(), c.hairColor) == colors->second.end()) { c.hairColor = colors->second[0]; removed += "hair colour "; }
		auto bp = db.bodyPalettes[g].find(c.job);
		if (c.bodyPalette >= 0 && (bp == db.bodyPalettes[g].end() || std::find(bp->second.begin(), bp->second.end(), c.bodyPalette) == bp->second.end())) { c.bodyPalette = -1; removed += "body palette "; }
		for (int i = 0; i < 3; i++)
		{
			auto chk = [&](int& id, const char* what)
			{
				if (!id) return;
				auto e = db.gear(db.headgear, id);
				if (!e || !db.exists(Db::headgearPath(*e, g) + ".spr")) { id = 0; removed += what; }
			};
			chk(c.head[i], "headgear "); chk(c.costume[i], "costume headgear ");
		}
		if (c.robe) { auto e = db.gear(db.robes, c.robe); if (!e || !db.exists(Db::robePath(*e, c.job, g) + ".spr")) { c.robe = 0; removed += "garment "; } }
		if (c.weapon) { auto e = db.gear(db.weapons, c.weapon); if (!e || !db.exists(Db::weaponPath(*e, c.job, g) + ".spr")) { c.weapon = 0; removed += "weapon "; } }
		if (!c.shield.empty() && !db.exists(Db::shieldPath(c.shield, c.job, g) + ".spr")) { c.shield.clear(); removed += "shield "; }
		notice = removed.empty() ? "" : "Not available for this job/gender, removed: " + removed;
	}

	// ------------------------------------------------------------------ presets
	void System::Impl::loadPresets(System& s)
	{
		presetsLoaded = true;
		s.presets.clear();
		std::ifstream i(presetsPath, std::ios::binary);
		if (!i) return;
		try
		{
			auto j = nlohmann::json::parse(i);
			for (auto& e : j["presets"])
			{
				Preset p; p.name = e.value("name", std::string("Preset")); p.category = e.value("category", std::string("Custom"));
				p.cfg = CharacterConfig::fromJson(e["config"]); p.action = e.value("action", 0); p.dir = e.value("dir", 0); p.scale = e.value("scale", 1.0f);
				s.presets.push_back(p);
			}
		}
		catch (std::exception& e) { std::cerr << "Sprite: could not read presets: " << e.what() << std::endl; }
	}
	void System::Impl::savePresets(System& s)
	{
		nlohmann::json j; j["version"] = 1; j["presets"] = nlohmann::json::array();
		for (auto& p : s.presets) j["presets"].push_back({ {"name", p.name}, {"category", p.category}, {"config", p.cfg.toJson()}, {"action", p.action}, {"dir", p.dir}, {"scale", p.scale} });
		std::ofstream o(presetsPath, std::ios::binary);
		o << j.dump(1);
		thumbs.clear();
	}

	// ------------------------------------------------------------------ windows
	static void previewWidget(System& s, System::Impl& im, float height)
	{
		auto& d = s.draft;
		ImVec2 p0 = ImGui::GetCursorScreenPos();
		ImVec2 size(ImGui::GetContentRegionAvail().x, height);
		ImGui::InvisibleButton("##preview", size);
		bool hover = ImGui::IsItemHovered();
		ImDrawList* dl = ImGui::GetWindowDrawList();
		ImVec2 p1(p0.x + size.x, p0.y + size.y);
		dl->PushClipRect(p0, p1, true);
		if (im.background == 2) dl->AddRectFilled(p0, p1, IM_COL32(70, 90, 110, 255));
		else if (im.background == 1)
		{
			dl->AddRectFilled(p0, p1, IM_COL32(60, 60, 60, 255));
			for (float y = 0; y < size.y; y += 16) for (float x = (((int)(y / 16)) & 1) * 16.0f; x < size.x; x += 32)
				dl->AddRectFilled(ImVec2(p0.x + x, p0.y + y), ImVec2(std::min(p0.x + x + 16, p1.x), std::min(p0.y + y + 16, p1.y)), IM_COL32(85, 85, 85, 255));
		}
		else dl->AddRectFilled(p0, p1, IM_COL32(25, 25, 28, 255));

		if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0))
		{	// drag to rotate
			im.dragAccum += (int)ImGui::GetIO().MouseDelta.x;
			while (im.dragAccum >= 24) { im.dragAccum -= 24; d.dir = (d.dir + 7) % 8; }
			while (im.dragAccum <= -24) { im.dragAccum += 24; d.dir = (d.dir + 1) % 8; }
		}
		if (hover && ImGui::GetIO().MouseWheel != 0) im.zoom = glm::clamp(im.zoom + ImGui::GetIO().MouseWheel * 0.5f, 1.0f, 8.0f);

		im.previewInst.cfg = d.cfg; im.previewInst.action = d.action; im.previewInst.speed = d.speed;
		if (!im.eightDirs)
		{
			ImVec2 feet(p0.x + size.x * 0.5f, p0.y + size.y * 0.82f);
			float z = im.zoom;
			if (im.grid)
			{
				float step = 16 * z;
				for (float x = fmodf(feet.x, step); x < size.x + p0.x; x += step) dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p1.y), IM_COL32(255, 255, 255, 25));
				for (float y = fmodf(feet.y, step); y < size.y + p0.y; y += step) dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y), IM_COL32(255, 255, 255, 25));
				dl->AddLine(ImVec2(p0.x, feet.y), ImVec2(p1.x, feet.y), IM_COL32(255, 200, 0, 90));
				dl->AddLine(ImVec2(feet.x, p0.y), ImVec2(feet.x, p1.y), IM_COL32(255, 200, 0, 90));
			}
			if (im.previewShadow)
			{
				ImVec2 pts[20];
				for (int k = 0; k < 20; k++) { float a = k / 20.0f * 6.2831853f; pts[k] = ImVec2(feet.x + cosf(a) * 14 * z, feet.y + sinf(a) * 6 * z); }
				dl->AddConvexPolyFilled(pts, 20, IM_COL32(0, 0, 0, 90));
			}
			auto f = im.frameFor(s, im.previewInst, d.dir, im.previewTime);
			if (f && f->w > 0)
			{
				unsigned int t = im.tex(*f);
				dl->AddImage((ImTextureID)(intptr_t)t, ImVec2(feet.x - f->ox * z, feet.y - f->oy * z), ImVec2(feet.x + (f->w - f->ox) * z, feet.y + (f->h - f->oy) * z));
			}
			else
				dl->AddText(ImVec2(p0.x + 8, p0.y + 8), IM_COL32(255, 120, 120, 255), "No sprite for this selection");
		}
		else
		{
			float cw = size.x / 8.0f;
			float z = std::max(1.0f, floorf(std::min(im.zoom, cw / 60.0f)));
			if (cw / 60.0f < 1.0f) z = cw / 60.0f;
			for (int k = 0; k < 8; k++)
			{
				ImVec2 feet(p0.x + cw * (k + 0.5f), p0.y + size.y * 0.8f);
				if (k == d.dir) dl->AddRectFilled(ImVec2(p0.x + cw * k, p0.y), ImVec2(p0.x + cw * (k + 1), p1.y), IM_COL32(255, 255, 255, 25));
				auto f = im.frameFor(s, im.previewInst, k, im.previewTime);
				if (f && f->w > 0)
				{
					unsigned int t = im.tex(*f);
					dl->AddImage((ImTextureID)(intptr_t)t, ImVec2(feet.x - f->ox * z, feet.y - f->oy * z), ImVec2(feet.x + (f->w - f->ox) * z, feet.y + (f->h - f->oy) * z));
				}
				dl->AddText(ImVec2(p0.x + cw * k + 3, p0.y + 3), IM_COL32(220, 220, 220, 255), directionNames[k]);
				if (hover && ImGui::IsMouseClicked(0) && ImGui::GetMousePos().x >= p0.x + cw * k && ImGui::GetMousePos().x < p0.x + cw * (k + 1)) d.dir = k;
			}
		}
		dl->PopClipRect();
	}

	void System::showWindows(BrowEdit* be)
	{
		ensureReady(be);
		auto& im = *impl;
		browserDrop = false;
		if (browserDrag && !ImGui::IsMouseDown(0)) { browserDrag = false; browserDrop = true; }
		if (browserDrag && ImGui::IsKeyPressed(GLFW_KEY_ESCAPE)) browserDrag = false;
		im.ensureOpts(*this);
		auto* mv = be->activeMapView;
		Map* map = mv ? mv->map : nullptr;

		if (map && db.ready && !im.pathPending) im.pathSnap = layer(map, be);	// state before any movement edit, for undo
		double now = ImGui::GetTime();
		if (im.playing) im.previewTime += now - im.lastTime;
		im.lastTime = now;
		if (db.ready) im.freeDropped(*this);

		ImGui::SetNextWindowSize(ImVec2(430, 820), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Sprite Editor"))
		{
			if (!db.ready)
			{
				ImGui::Text("Scanning the client data for sprites...");
				ImGui::End();
				return;
			}
			ImGui::TextDisabled("%zu sprite/palette/imf files, %zu jobs, %zu/%zu hair styles (m/f), %zu headgear, %zu garments, %zu weapons",
				db.fileCount, db.jobs.size(), db.hairStyles[0].size(), db.hairStyles[1].size(),
				(size_t)std::count_if(db.headgear.begin(), db.headgear.end(), [](auto& e) { return e.has[0] || e.has[1]; }),
				(size_t)std::count_if(db.robes.begin(), db.robes.end(), [](auto& e) { return e.has[0] || e.has[1]; }),
				(size_t)std::count_if(db.weapons.begin(), db.weapons.end(), [](auto& e) { return e.has[0] || e.has[1]; }));
			if (ImGui::Button("Rescan data")) { startScan(be); ImGui::End(); return; }
			ImGui::SameLine(); ImGui::Checkbox("Show unavailable", &im.showUnavailable);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also list entries from the client tables whose files are not in your data");
			ImGui::SameLine(); ImGui::Checkbox("Shadow in map", &showShadow);
			ImGui::SameLine(); ImGui::Checkbox("Stand on GAT", &snapToGat);
			if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sprites stand on the GAT cell height (bridges, raised walkable areas). Off: they stand on the ground mesh.");
			ImGui::SameLine(); ImGui::Checkbox("Object list", &showList);

			auto& d = draft;
			auto& c = d.cfg;
			int g = c.gender;

			// ---------- preview
			if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen))
			{
				previewWidget(*this, im, 250);
				if (ImGui::Button(im.playing ? "Pause" : "Play")) im.playing = !im.playing;
				ImGui::SameLine(); if (ImGui::Button("Stop")) { im.playing = false; im.previewTime = 0; }
				ImGui::SameLine(); ImGui::SetNextItemWidth(90); ImGui::SliderFloat("Speed", &d.speed, 0.1f, 3.0f, "%.2f");
				ImGui::SameLine(); ImGui::SetNextItemWidth(70); ImGui::SliderFloat("Zoom", &im.zoom, 1, 8, "%.1f");
				const char* bgs[] = { "Transparent", "Checker", "Solid" };
				ImGui::SetNextItemWidth(100); ImGui::Combo("Background", &im.background, bgs, 3);
				ImGui::SameLine(); ImGui::Checkbox("8 dirs", &im.eightDirs);
				ImGui::SameLine(); ImGui::Checkbox("Shadow", &im.previewShadow);
				ImGui::SameLine(); ImGui::Checkbox("Grid", &im.grid);
				ImGui::TextDisabled("Drag in the preview to rotate, mouse wheel to zoom.");
			}

			// warnings (recomputed only when the selection changes)
			std::string key = c.key();
			if (key != im.warnKey)
			{
				im.warnKey = key;
				im.warnings = composer.warnings(c);
				im.actionsAvail.clear();
				for (int a = 0; a < (c.kind == 0 ? actionDefCount : 40); a++)
					if (composer.info(c, a, d.dir).valid) im.actionsAvail.push_back(a);
			}

			// ---------- customisation
			if (ImGui::CollapsingHeader("Customization", ImGuiTreeNodeFlags_DefaultOpen))
			{
				{
					int k = c.kind; const char* kinds[] = { "Player", "Monster", "NPC" };
					ImGui::SetNextItemWidth(140);
					if (ImGui::Combo("Type", &k, kinds, 3) && k != c.kind)
					{
						c.kind = k; d.action = 0; im.notice.clear(); im.previewTime = 0;
						if (k && c.mob.empty()) { auto& l = k == 1 ? db.monsters : db.npcs; if (!l.empty()) c.mob = l[0].sprite; }
					}
					if (c.kind != 0)
					{
						auto& list = c.kind == 1 ? db.monsters : db.npcs;
						if (list.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "No %s sprites found in data\\sprite\\%s", c.kind == 1 ? "monster" : "npc", c.kind == 1 ? "(monster folder)" : "npc");
						int sel = 0;
						for (size_t i = 0; i < list.size(); i++) if (list[i].sprite == c.mob) sel = (int)i + 1;
						if (searchCombo(c.kind == 1 ? "Monster sprite" : "NPC sprite", c.mob.empty() ? "None" : c.mob, im.mobOpts[c.kind], sel, true, false) && sel > 0)
						{
							c.mob = list[sel - 1].sprite; d.action = 0;
							if (d.name == "Sprite" || d.name.empty()) d.name = list[sel - 1].label;
						}
					}
				}
				if (c.kind == 0) {
				auto jobIdx = -1;
				for (size_t i = 0; i < db.jobs.size(); i++) if (db.jobs[i].sprite == c.job) jobIdx = (int)i;
				std::string jobPreview = jobIdx >= 0 ? db.jobs[jobIdx].label + "  [" + c.job + "]" : c.job;
				int sel = jobIdx;
				if (jobCombo("Job class", jobPreview, db, im.jobOpts, g, sel, im.showUnavailable) && sel >= 0)
					im.applyJobChange(*this, c, db.jobs[sel].sprite, g);
				int gsel = g;
				if (ImGui::RadioButton("Male", g == Male)) gsel = Male;
				ImGui::SameLine(); if (ImGui::RadioButton("Female", g == Female)) gsel = Female;
				if (gsel != g) { im.applyJobChange(*this, c, c.job, gsel); g = gsel; }
				{
					const char* hm[] = { "Moving head (animated)", "Steady head - straight", "Steady head - pose 2", "Steady head - pose 3", "None (no animation)" };
					ImGui::SetNextItemWidth(220);
					ImGui::Combo("Head movement", &c.headMode, hm, 5);
				}

				// hairstyle / colour
				{
					std::vector<Opt> o;
					for (int st : db.hairStyles[g]) o.push_back({ st, "Style " + std::to_string(st) });
					int st = c.hairStyle;
					if (searchCombo("Hairstyle", "Style " + std::to_string(c.hairStyle), o, st, false, true, 0)) { c.hairStyle = st; }
					std::vector<Opt> col;
					auto it = db.hairColors[g].find(c.hairStyle);
					if (it != db.hairColors[g].end()) for (int k : it->second) col.push_back({ k, "Colour " + std::to_string(k) });
					int cv = c.hairColor;
					if (searchCombo("Hair colour", "Colour " + std::to_string(c.hairColor), col, cv, false, false)) c.hairColor = cv;
					std::vector<Opt> bp;
					auto bi = db.bodyPalettes[g].find(c.job);
					if (bi != db.bodyPalettes[g].end()) for (int k : bi->second) bp.push_back({ k, "Palette " + std::to_string(k) });
					int bv = c.bodyPalette;
					if (searchCombo("Body / clothes palette", c.bodyPalette < 0 ? "None" : "Palette " + std::to_string(c.bodyPalette), bp, bv, false, true, -1)) c.bodyPalette = bv;
				}
				auto gearName = [&](const std::vector<GearEntry>& v, int id) { if (!id) return std::string("None"); auto e = db.gear(v, id); return e ? pretty(e->label) + " [" + std::to_string(id) + "]" : "Unknown [" + std::to_string(id) + "]"; };
				const char* slots[3] = { "Upper headgear", "Middle headgear", "Lower headgear" };
				for (int i = 0; i < 3; i++)
				{
					int v = c.head[i];
					if (searchCombo(slots[i], gearName(db.headgear, v), im.headgearOpts[g], v, im.showUnavailable, true)) c.head[i] = v;
				}
				const char* cslots[3] = { "Costume upper", "Costume middle", "Costume lower" };
				for (int i = 0; i < 3; i++)
				{
					int v = c.costume[i];
					if (searchCombo(cslots[i], gearName(db.headgear, v), im.headgearOpts[g], v, im.showUnavailable, true)) c.costume[i] = v;
				}
				{
					std::vector<Opt> ro = im.robeOpts[g];
					for (auto& o : ro) { auto e = db.gear(db.robes, o.id); o.ok = e && db.exists(Db::robePath(*e, c.job, g) + ".spr"); }
					int v = c.robe;
					if (searchCombo("Garment / wings", gearName(db.robes, v), ro, v, im.showUnavailable, true)) c.robe = v;
					std::vector<Opt> wo = im.weaponOpts[g];
					for (auto& o : wo) { auto e = db.gear(db.weapons, o.id); o.ok = e && db.exists(Db::weaponPath(*e, c.job, g) + ".spr"); }
					int w = c.weapon;
					if (searchCombo("Weapon", gearName(db.weapons, w), wo, w, im.showUnavailable, true)) c.weapon = w;
					std::vector<Opt> so;
					for (size_t i = 0; i < db.shields.size(); i++) so.push_back({ (int)i + 1, pretty(rawToUtf8(db.shields[i])).substr(0), db.exists(Db::shieldPath(db.shields[i], c.job, g) + ".spr") });
					int sv = 0;
					for (size_t i = 0; i < db.shields.size(); i++) if (db.shields[i] == c.shield) sv = (int)i + 1;
					if (searchCombo("Shield", c.shield.empty() ? "None" : c.shield, so, sv, im.showUnavailable, true)) c.shield = sv ? db.shields[sv - 1] : "";
				}
				// custom asset (e.g. new wings) with per direction alignment
				{
					std::vector<Opt> co;
					for (size_t i = 0; i < db.customWings.size(); i++) co.push_back({ (int)i + 1, db.customWings[i].substr(db.customWings[i].rfind('\\') + 1) });
					int cv = 0;
					for (size_t i = 0; i < db.customWings.size(); i++) if (lower(db.customWings[i]) == lower(c.customWing)) cv = (int)i + 1;
					if (searchCombo("Custom wings (data\\sprite\\custom)", c.customWing.empty() ? "None" : c.customWing, co, cv, true, true)) c.customWing = cv ? db.customWings[cv - 1] : "";
					if (!c.customWing.empty())
					{
						ImGui::Text("Alignment for %s (originals are never modified)", directionNames[d.dir]);
						ImGui::SetNextItemWidth(80); ImGui::DragInt("X##wing", &c.wingOff[d.dir][0]);
						ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::DragInt("Y##wing", &c.wingOff[d.dir][1]);
						if (ImGui::Button("Copy to all directions")) for (int k = 0; k < 8; k++) { c.wingOff[k][0] = c.wingOff[d.dir][0]; c.wingOff[k][1] = c.wingOff[d.dir][1]; }
						ImGui::SameLine(); if (ImGui::Button("Reset")) memset(c.wingOff, 0, sizeof(c.wingOff));
					}
				}
				}
				if (!im.notice.empty()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "%s", im.notice.c_str());
			}
			if (!im.warnings.empty())
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 0.45f, 0.4f, 1));
				ImGui::TextWrapped("%zu missing resource(s):", im.warnings.size());
				for (auto& w : im.warnings) ImGui::TextWrapped("%s", rawToUtf8(w).c_str());
				ImGui::PopStyleColor();
			}

			// ---------- placement
			if (ImGui::CollapsingHeader("Placement", ImGuiTreeNodeFlags_DefaultOpen))
			{
				inputStr("Name", d.name);
				int dirv = d.dir;
				ImGui::SetNextItemWidth(150);
				if (ImGui::BeginCombo("Direction", directionNames[d.dir])) { for (int k = 0; k < 8; k++) if (ImGui::Selectable(directionNames[k], k == d.dir)) dirv = k; ImGui::EndCombo(); }
				d.dir = dirv;
				ImGui::SameLine(); if (ImGui::ArrowButton("##rl", ImGuiDir_Left)) d.dir = (d.dir + 1) % 8;
				ImGui::SameLine(); if (ImGui::ArrowButton("##rr", ImGuiDir_Right)) d.dir = (d.dir + 7) % 8;
				ImGui::SetNextItemWidth(200);
				if (ImGui::BeginCombo("Animation", actionLabel(c, d.action).c_str()))
				{
					for (int a : im.actionsAvail) if (ImGui::Selectable(actionLabel(c, a).c_str(), a == d.action)) { d.action = a; im.previewTime = 0; }
					ImGui::EndCombo();
				}
				ImGui::SetNextItemWidth(200); ImGui::DragFloat("Scale", &d.scale, 0.01f, 0.1f, 10.0f, "%.2f");
				ImGui::SetNextItemWidth(260); ImGui::DragFloat3("Position X/Y/Z", &d.pos.x, 0.1f);
				ImGui::SetNextItemWidth(200); ImGui::DragFloat("Elevation offset", &d.elevation, 0.05f);
				ImGui::Checkbox("Visible", &d.visible); ImGui::SameLine(); ImGui::Checkbox("Locked", &d.locked);

				bool haveMap = map != nullptr && map->rootNode->getComponent<Gnd>();
				bool bad = !composer.info(c, d.action, d.dir).valid;
				ImGui::BeginDisabled(!haveMap || bad);
				if (ImGui::Button(placing ? "ADD TO MAP (placing...)" : "ADD TO MAP  (Ctrl+Click)")) { placing = true; im.editingId = 0; }
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(!haveMap ? "Open a map first" : bad ? "This character has no sprite to place" : "The ghost follows the cursor. Left click places it (Shift: keep placing), right click / Esc cancels.\nShortcut: Ctrl + left click on the map adds the sprite straight away, no need to press this button.");
				ImGui::SameLine();
				if (ImGui::Button("Add at X/Y/Z"))
				{
					auto& L = layer(map, be); MapLayer before = L;
					Instance in; in.name = d.name; in.cfg = c; in.action = d.action; in.dir = d.dir; in.speed = d.speed; in.scale = d.scale; in.pos = d.pos; in.elevation = d.elevation; in.visible = d.visible; in.locked = d.locked;
					in.id = L.nextId++; L.items.push_back(in); L.selected = { in.id };
					pushLayerAction(map, be, L, before, "Add sprite " + in.name);
				}
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("CANCEL")) { placing = false; im.editingId = 0; d = Draft(); d.cfg = c; im.notice.clear(); }
				if (ImGui::IsItemHovered()) ImGui::SetTooltip("Stops placing and resets the placement fields (the look stays)");

				auto* L = map ? &layer(map, be) : nullptr;
				size_t nsel = L ? L->selected.size() : 0;
				ImGui::BeginDisabled(nsel == 0);
				if (ImGui::Button("UPDATE SELECTED"))
				{
					MapLayer before = *L;
					for (int id : L->selected)
						if (auto* in = L->find(id))
						{
							in->cfg = c; in->action = d.action; in->dir = d.dir; in->speed = d.speed; in->scale = d.scale; in->elevation = d.elevation; in->visible = d.visible; in->locked = d.locked;
							if (nsel == 1) { in->name = d.name; in->pos = d.pos; }
						}
					pushLayerAction(map, be, *L, before, "Update sprite(s)");
				}
				if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Applies everything in this panel to the selected sprite(s). Name and position only when one is selected.");
				ImGui::SameLine();
				if (ImGui::Button("Duplicate"))
				{
					MapLayer before = *L; std::set<int> ns;
					for (int id : L->selected) if (auto* in = L->find(id)) { Instance cp = *in; cp.id = L->nextId++; cp.pos.x += 5; cp.name += " copy"; L->items.push_back(cp); ns.insert(cp.id); }
					L->selected = ns;
					pushLayerAction(map, be, *L, before, "Duplicate sprite(s)");
				}
				ImGui::SameLine();
				if (ImGui::Button("Copy")) { im.clipboard.clear(); for (int id : L->selected) if (auto* in = L->find(id)) im.clipboard.push_back(*in); }
				ImGui::SameLine();
				if (ImGui::Button("Delete"))
				{
					MapLayer before = *L;
					L->items.erase(std::remove_if(L->items.begin(), L->items.end(), [&](const Instance& i) { return L->selected.count(i.id) && !i.locked; }), L->items.end());
					L->selected.clear();
					pushLayerAction(map, be, *L, before, "Delete sprite(s)");
				}
				ImGui::EndDisabled();
				ImGui::BeginDisabled(!map || im.clipboard.empty());
				ImGui::SameLine();
				if (ImGui::Button("Paste") && L)
				{
					MapLayer before = *L; std::set<int> ns;
					for (auto cp : im.clipboard) { cp.id = L->nextId++; cp.pos.x += 5; L->items.push_back(cp); ns.insert(cp.id); }
					L->selected = ns;
					pushLayerAction(map, be, *L, before, "Paste sprite(s)");
				}
				ImGui::EndDisabled();
				ImGui::TextDisabled("%zu selected. Click: select (Ctrl+click a sprite toggles, Shift adds), Ctrl+click on the ground: add, Shift+click on the ground: waypoint for the selected sprite, drag: move, double click: edit here, Delete key: remove.", nsel);
				ImGui::TextDisabled("Editor-only: saved next to the map in <map>.browsprites.json. They are not NPCs and are not written to the RSW.");
			}

			// ---------- movement
			if (ImGui::CollapsingHeader("Movement / waypoints"))
			{
				ImGui::Checkbox("Preview movement in map", &previewMovement);
				ImGui::SameLine(); if (ImGui::Button("Restart")) pathEpoch = ImGui::GetTime();
				ImGui::SameLine(); ImGui::Checkbox("Show all paths", &showPaths);
				auto* LL = map ? &layer(map, be) : nullptr;
				std::vector<Instance*> sel;
				if (LL) for (int id : LL->selected) if (auto* in = LL->find(id)) sel.push_back(in);
				if (sel.empty()) ImGui::TextDisabled("Select a sprite on the map to edit how it moves.");
				else
				{
					Instance& f = *sel[0];
					auto apply = [&](auto fn) { for (auto* i : sel) fn(*i); im.pathPending = true; };
					bool en = f.pathEnabled;
					if (ImGui::Checkbox("Move along the path / wander", &en)) apply([&](Instance& i) { i.pathEnabled = en; });
					float v = f.moveSpeed;
					ImGui::SetNextItemWidth(180);
					if (ImGui::DragFloat("Move speed (cells/s)", &v, 0.05f, 0.5f, 30.0f, "%.2f")) apply([&](Instance& i) { i.moveSpeed = v; });
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("The client walks about 6.7 cells per second (one cell = 5 map units)");
					v = f.waitTime; ImGui::SetNextItemWidth(180);
					if (ImGui::DragFloat("Wait at each point (s)", &v, 0.05f, 0.0f, 120.0f, "%.2f")) apply([&](Instance& i) { i.waitTime = v; });
					v = f.startDelay; ImGui::SetNextItemWidth(180);
					if (ImGui::DragFloat("Start delay (s)", &v, 0.05f, 0.0f, 120.0f, "%.2f")) apply([&](Instance& i) { i.startDelay = v; });
					int mode = f.pathMode; const char* modes[] = { "Once (stop at the last point)", "Loop (walk back to the start)", "Ping-pong (there and back)" };
					ImGui::SetNextItemWidth(220);
					if (ImGui::Combo("Path mode", &mode, modes, 3)) apply([&](Instance& i) { i.pathMode = mode; });
					bool b = f.faceMove;
					if (ImGui::Checkbox("Face the walking direction", &b)) apply([&](Instance& i) { i.faceMove = b; });
					b = f.syncAnim; ImGui::SameLine();
					if (ImGui::Checkbox("Walk animation follows speed", &b)) apply([&](Instance& i) { i.syncAnim = b; });
					int wa = f.walkAction;
					ImGui::SetNextItemWidth(180);
					if (ImGui::BeginCombo("Walking animation", actionLabel(f.cfg, wa).c_str()))
					{
						for (int a = 0; a < (f.cfg.kind == 0 ? actionDefCount : 12); a++)
							if (composer.info(f.cfg, a, 0).valid && ImGui::Selectable(actionLabel(f.cfg, a).c_str(), a == wa)) apply([&](Instance& i) { i.walkAction = a; });
						ImGui::EndCombo();
					}
					v = f.wanderRadius; ImGui::SetNextItemWidth(180);
					if (ImGui::DragFloat("Wander radius (cells)", &v, 0.1f, 0.0f, 40.0f, "%.1f")) apply([&](Instance& i) { i.wanderRadius = v; });
					if (ImGui::IsItemHovered()) ImGui::SetTooltip("Without waypoints, the sprite strolls to random walkable GAT cells around where it stands. 0 = off.");
					if (sel.size() == 1)
					{
						ImGui::Separator();
						ImGui::Text("%zu waypoint(s) - Shift+click on the map adds one", f.path.size());
						int del = -1;
						ImGui::BeginChild("##wps", ImVec2(0, std::min(140.0f, 24.0f * (f.path.size() + 1))), true);
						for (size_t k = 0; k < f.path.size(); k++)
						{
							ImGui::PushID((int)k);
							ImGui::Text("%zu: %.0f, %.0f, %.0f", k + 1, f.path[k].x, f.path[k].y, f.path[k].z);
							ImGui::SameLine(); if (ImGui::SmallButton("Delete")) del = (int)k;
							ImGui::PopID();
						}
						ImGui::EndChild();
						if (del >= 0) { f.path.erase(f.path.begin() + del); im.pathPending = true; }
						if (ImGui::Button("Remove last") && !f.path.empty()) { f.path.pop_back(); im.pathPending = true; }
						ImGui::SameLine(); if (ImGui::Button("Clear path")) { f.path.clear(); im.pathPending = true; }
					}
					else ImGui::TextDisabled("Waypoints can be edited when exactly one sprite is selected.");
				}
				if (im.pathPending && LL && !ImGui::IsAnyItemActive())
				{
					pushLayerAction(map, be, *LL, im.pathSnap, "Edit movement");
					im.pathPending = false;
				}
			}

			// ---------- presets
			if (ImGui::CollapsingHeader("Presets"))
			{
				inputStr("Preset name", im.presetName, 48);
				static int saveCat = 5;
				ImGui::SetNextItemWidth(160);
				if (ImGui::BeginCombo("Save in category", categories[saveCat])) { for (int k = 0; k < categoryCount; k++) if (ImGui::Selectable(categories[k], k == saveCat)) saveCat = k; ImGui::EndCombo(); }
				if (ImGui::Button("SAVE PRESET"))
				{
					Preset p; p.name = im.presetName; p.category = categories[saveCat]; p.cfg = c; p.action = d.action; p.dir = d.dir; p.scale = d.scale;
					bool replaced = false;
					for (auto& e : presets) if (e.name == p.name && e.category == p.category) { e = p; replaced = true; }
					if (!replaced) presets.push_back(p);
					im.savePresets(*this);
				}
				ImGui::Separator();
				inputStr("Search##preset", im.presetFilter, 48);
				ImGui::SetNextItemWidth(160);
				if (ImGui::BeginCombo("Category", im.presetCategory.c_str()))
				{
					if (ImGui::Selectable("All", im.presetCategory == "All")) im.presetCategory = "All";
					for (int k = 0; k < categoryCount; k++) if (ImGui::Selectable(categories[k], im.presetCategory == categories[k])) im.presetCategory = categories[k];
					ImGui::EndCombo();
				}
				ImGui::BeginChild("##presets", ImVec2(0, 220), true);
				int del = -1, dup = -1;
				for (int i = 0; i < (int)presets.size(); i++)
				{
					auto& p = presets[i];
					if ((im.presetCategory != "All" && p.category != im.presetCategory) || !containsNoCase(p.name, im.presetFilter)) continue;
					ImGui::PushID(i);
					auto& th = im.thumbs[i];
					if (!th) th = composer.frame(p.cfg, 0, 0, 0);
					ImVec2 box(48, 64);
					ImVec2 cp = ImGui::GetCursorScreenPos();
					ImGui::Dummy(box);
					if (th && th->w > 0)
					{
						float z = std::min(box.x / th->w, box.y / th->h); z = std::min(z, 1.0f);
						ImVec2 a(cp.x + (box.x - th->w * z) * 0.5f, cp.y + (box.y - th->h * z));
						ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)im.tex(*th), a, ImVec2(a.x + th->w * z, a.y + th->h * z));
					}
					ImGui::SameLine();
					ImGui::BeginGroup();
					ImGui::Text("%s", p.name.c_str()); ImGui::TextDisabled("%s", p.category.c_str());
					if (ImGui::SmallButton("Load")) { d.cfg = p.cfg; d.action = p.action; d.dir = p.dir; d.scale = p.scale; d.name = p.name; }
					ImGui::SameLine(); if (ImGui::SmallButton("Rename")) { im.renameBuf = p.name; im.presetSelected = i; ImGui::OpenPopup("RenamePreset"); }
					ImGui::SameLine(); if (ImGui::SmallButton("Duplicate")) dup = i;
					ImGui::SameLine(); if (ImGui::SmallButton("Delete")) del = i;
					ImGui::EndGroup();
					ImGui::Separator();
					ImGui::PopID();
				}
				if (ImGui::BeginPopup("RenamePreset"))
				{
					inputStr("Name", im.renameBuf, 48);
					if (ImGui::Button("OK") && im.presetSelected >= 0 && im.presetSelected < (int)presets.size()) { presets[im.presetSelected].name = im.renameBuf; im.savePresets(*this); ImGui::CloseCurrentPopup(); }
					ImGui::SameLine(); if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
					ImGui::EndPopup();
				}
				if (dup >= 0) { Preset p = presets[dup]; p.name += " copy"; presets.insert(presets.begin() + dup + 1, p); im.savePresets(*this); }
				if (del >= 0) { presets.erase(presets.begin() + del); im.savePresets(*this); }
				ImGui::EndChild();
			}
		}
		ImGui::End();

		if (showBrowser) drawBrowser(be);

		// ---------- object list
		if (showList && map)
		{
			auto& L = layer(map, be);
			ImGui::SetNextWindowSize(ImVec2(520, 260), ImGuiCond_FirstUseEver);
			if (ImGui::Begin("Sprite Object List", &showList))
			{
				ImGui::Text("%zu sprite(s) on this map (editor only)", L.items.size());
				if (ImGui::BeginTable("sprites", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable))
				{
					ImGui::TableSetupColumn("Name"); ImGui::TableSetupColumn("Job"); ImGui::TableSetupColumn("Position");
					ImGui::TableSetupColumn("Show", ImGuiTableColumnFlags_WidthFixed, 40); ImGui::TableSetupColumn("Lock", ImGuiTableColumnFlags_WidthFixed, 40); ImGui::TableSetupColumn("##x", ImGuiTableColumnFlags_WidthFixed, 30);
					ImGui::TableHeadersRow();
					int toDelete = 0;
					for (auto& in : L.items)
					{
						ImGui::TableNextRow();
						ImGui::PushID(in.id);
						ImGui::TableNextColumn();
						if (ImGui::Selectable((in.name + "##n").c_str(), L.selected.count(in.id) > 0, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowItemOverlap))
						{
							if (ImGui::GetIO().KeyCtrl) { if (L.selected.count(in.id)) L.selected.erase(in.id); else L.selected.insert(in.id); }
							else L.selected = { in.id };
						}
						if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
						{
							draft.cfg = in.cfg; draft.name = in.name; draft.action = in.action; draft.dir = in.dir; draft.speed = in.speed; draft.scale = in.scale; draft.pos = in.pos; draft.elevation = in.elevation; draft.visible = in.visible; draft.locked = in.locked;
							if (mv) if (auto gnd = map->rootNode->getComponent<Gnd>()) mv->cameraCenter = glm::vec3(5 * gnd->width + in.pos.x, -in.pos.y, 10 + 5 * gnd->height - in.pos.z);
						}
						ImGui::TableNextColumn(); ImGui::Text("%s", in.cfg.kind != 0 ? in.cfg.mob.c_str() : pretty(db.job(in.cfg.job) ? db.job(in.cfg.job)->label : in.cfg.job).c_str());
						ImGui::TableNextColumn(); ImGui::Text("%.0f, %.0f, %.0f", in.pos.x, in.pos.y, in.pos.z);
						ImGui::TableNextColumn();
						bool vis = in.visible; if (ImGui::Checkbox("##v", &vis)) { MapLayer before = L; in.visible = vis; pushLayerAction(map, be, L, before, "Toggle sprite visibility"); }
						ImGui::TableNextColumn();
						bool lk = in.locked; if (ImGui::Checkbox("##l", &lk)) { MapLayer before = L; in.locked = lk; pushLayerAction(map, be, L, before, "Toggle sprite lock"); }
						ImGui::TableNextColumn();
						if (ImGui::SmallButton("X")) toDelete = in.id;
						ImGui::PopID();
					}
					if (toDelete)
					{
						MapLayer before = L;
						L.items.erase(std::remove_if(L.items.begin(), L.items.end(), [&](const Instance& i) { return i.id == toDelete; }), L.items.end());
						L.selected.erase(toDelete);
						pushLayerAction(map, be, L, before, "Delete sprite");
					}
					ImGui::EndTable();
				}
			}
			ImGui::End();
		}
	}
}

namespace sprite
{
	// grid of all monster / npc sprites found in the data: click to choose, drag onto the map to place
	void System::drawBrowser(BrowEdit* be)
	{
		auto& im = *impl;
		if (!db.ready) return;
		ImGui::SetNextWindowSize(ImVec2(460, 520), ImGuiCond_FirstUseEver);
		if (ImGui::Begin("Sprite Browser", &showBrowser))
		{
			const char* tabs[] = { "Monsters", "NPCs" };
			for (int t = 0; t < 2; t++)
			{
				if (t) ImGui::SameLine();
				if (ImGui::RadioButton((std::string(tabs[t]) + " (" + std::to_string(t == 0 ? db.monsters.size() : db.npcs.size()) + ")").c_str(), im.browserTab == t)) im.browserTab = t;
			}
			ImGui::SetNextItemWidth(160); inputStr("Search##browser", im.browserFilter, 48);
			ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::SliderFloat("Size", &im.thumbSize, 48, 160, "%.0f");
			int kind = im.browserTab + 1;
			auto& list = kind == 1 ? db.monsters : db.npcs;
			if (list.empty())
				ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "No %s sprites found in your data (data\\sprite\\%s).", kind == 1 ? "monster" : "npc", kind == 1 ? "monster folder" : "npc");
			ImGui::TextDisabled("Click to choose it for the Sprite Editor, drag it onto the map to place it. Ctrl+click on the map also adds the chosen one.");
			std::vector<const MobEntry*> shown;
			for (auto& e : list) if (containsNoCase(e.label + " " + e.sprite, im.browserFilter)) shown.push_back(&e);

			ImGui::BeginChild("##grid", ImVec2(0, 0), true);
			float cell = im.thumbSize, pad = 6, lh = ImGui::GetTextLineHeight() + 4;
			int cols = std::max(1, (int)((ImGui::GetContentRegionAvail().x + pad) / (cell + pad)));
			int rows = ((int)shown.size() + cols - 1) / cols;
			ImGuiListClipper clip;
			clip.Begin(rows, cell + lh + pad);
			const MobEntry* dragged = nullptr;
			while (clip.Step())
				for (int r = clip.DisplayStart; r < clip.DisplayEnd; r++)
					for (int cI = 0; cI < cols; cI++)
					{
						int i = r * cols + cI;
						if (i >= (int)shown.size()) break;
						auto& e = *shown[i];
						if (cI) ImGui::SameLine(0, pad);
						ImGui::PushID(i + kind * 100000);
						ImVec2 p = ImGui::GetCursorScreenPos();
						ImGui::InvisibleButton("##cell", ImVec2(cell, cell + lh));
						bool hov = ImGui::IsItemHovered();
						bool isCur = draft.cfg.kind == kind && draft.cfg.mob == e.sprite;
						if (ImGui::IsItemClicked(0) || (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 5) && !(isCur)))
						{
							draft.cfg.kind = kind; draft.cfg.mob = e.sprite; draft.action = 0; draft.name = e.label; im.previewTime = 0; im.notice.clear();
						}
						if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 5)) { dragged = &e; browserDrag = true; }
						std::string key = std::to_string(kind) + ":" + e.sprite;
						auto& th = im.mobThumbs[key];
						if (!th) { CharacterConfig cc; cc.kind = kind; cc.mob = e.sprite; th = composer.frame(cc, 0, 0, 0); }
						auto* dl = ImGui::GetWindowDrawList();
						dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell + lh), isCur ? IM_COL32(90, 120, 40, 200) : hov ? IM_COL32(80, 80, 95, 255) : IM_COL32(45, 45, 52, 255), 4);
						if (th && th->w > 0)
						{
							float z = std::min((cell - 6) / th->w, (cell - 6) / th->h); z = std::min(z, 2.0f);
							ImVec2 a(p.x + (cell - th->w * z) * 0.5f, p.y + cell - 3 - th->h * z);
							dl->AddImage((ImTextureID)(intptr_t)im.tex(*th), a, ImVec2(a.x + th->w * z, a.y + th->h * z));
						}
						else dl->AddText(ImVec2(p.x + 6, p.y + cell * 0.4f), IM_COL32(200, 120, 120, 255), "no image");
						std::string name = e.label;
						int maxChars = std::max(4, (int)(cell / 7));
						if ((int)name.size() > maxChars) name = name.substr(0, maxChars - 1) + "~";
						dl->AddText(ImVec2(p.x + 3, p.y + cell + 1), IM_COL32(230, 230, 230, 255), name.c_str());
						if (hov && !browserDrag) ImGui::SetTooltip("%s%s", e.sprite.c_str(), e.id >= 0 ? (" (id " + std::to_string(e.id) + ")").c_str() : "");
						ImGui::PopID();
					}
			ImGui::EndChild();
		}
		ImGui::End();
		if (browserDrag)
		{	// the sprite follows the cursor until it is dropped (on the map it becomes a ghost)
			std::string key = std::to_string(draft.cfg.kind) + ":" + draft.cfg.mob;
			auto it = im.mobThumbs.find(key);
			if (it != im.mobThumbs.end() && it->second && it->second->w > 0)
			{
				auto& f = *it->second; ImVec2 m = ImGui::GetMousePos(); float z = std::min(1.0f, 80.0f / std::max(f.w, f.h));
				ImGui::GetForegroundDrawList()->AddImage((ImTextureID)(intptr_t)im.tex(f), ImVec2(m.x - f.ox * z, m.y - f.oy * z), ImVec2(m.x + (f.w - f.ox) * z, m.y + (f.h - f.oy) * z), ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 170));
			}
		}
	}
}
