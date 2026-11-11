#include "Db.h"
#include "Text.h"
#include <json.hpp>
#include <algorithm>
#include <cctype>

namespace sprite
{
	static const std::string B = "\\";
	static std::string g2(int g) { return genderName(g); }

	std::string Db::bodyPath(const std::string& job, int g) { return "data\\sprite\\\xEC\x9D\xB8\xEA\xB0\x84\xEC\xA1\xB1\\\xEB\xAA\xB8\xED\x86\xB5\\" + g2(g) + B + job + "_" + g2(g); }
	std::string Db::headPath(int style, int g) { return "data\\sprite\\\xEC\x9D\xB8\xEA\xB0\x84\xEC\xA1\xB1\\\xEB\xA8\xB8\xEB\xA6\xAC\xED\x86\xB5\\" + g2(g) + B + std::to_string(style) + "_" + g2(g); }
	std::string Db::hairPalPath(int style, int g, int color) { return "data\\palette\\\xEB\xA8\xB8\xEB\xA6\xAC\\\xEB\xA8\xB8\xEB\xA6\xAC" + std::to_string(style) + "_" + g2(g) + "_" + std::to_string(color) + ".pal"; }
	std::string Db::bodyPalPath(const std::string& job, int g, int idx) { return "data\\palette\\\xEB\xAA\xB8\\" + job + "_" + g2(g) + "_" + std::to_string(idx) + ".pal"; }
	std::string Db::headgearPath(const GearEntry& e, int g) { return "data\\sprite\\\xEC\x95\x85\xEC\x84\xB8\xEC\x82\xAC\xEB\xA6\xAC\\" + g2(g) + B + g2(g) + e.sprite; }
	std::string Db::robePath(const GearEntry& e, const std::string& job, int g) { return "data\\sprite\\\xEB\xA1\x9C\xEB\xB8\x8C\\" + e.sprite + B + g2(g) + B + job + "_" + g2(g); }
	std::string Db::weaponPath(const GearEntry& e, const std::string& job, int g) { return "data\\sprite\\\xEC\x9D\xB8\xEA\xB0\x84\xEC\xA1\xB1\\" + job + B + job + "_" + g2(g) + e.sprite; }
	std::string Db::shieldPath(const std::string& s, const std::string& job, int g) { return "data\\sprite\\\xEB\xB0\xA9\xED\x8C\xA8\\" + job + B + job + "_" + g2(g) + s; }
	std::string Db::mobPath(int kind, const std::string& n) { return (kind == 1 ? "data\\sprite\\\xEB\xAA\xAC\xEC\x8A\xA4\xED\x84\xB0\\" : "data\\sprite\\npc\\") + n; }
	std::string Db::imfPath(const std::string& job, int g) { return "data\\imf\\" + job + "_" + g2(g) + ".imf"; }

	int Db::jobGroup(int id)
	{
		if (id >= 0 && id <= 6) return 0;
		if ((id >= 7 && id <= 12) || (id >= 14 && id <= 20)) return 1;
		if (id >= 4046 && id <= 4049) return 2;
		if (id == 23 || id == 24 || id == 25 || id == 4190 || id == 4191 || (id >= 4211 && id <= 4221)) return 3;
		if (id >= 4001 && id <= 4022 && id != 4014 && id != 4022) return 4;
		if (id >= 4054 && id <= 4095) return 5;
		if ((id >= 4252 && id <= 4281) || (id >= 4302 && id <= 4316)) return 6;
		return 7;
	}

	bool Db::exists(const std::string& path) const { return files.find(lower(path)) != files.end(); }
	std::string Db::actual(const std::string& path) const { auto it = files.find(lower(path)); return it == files.end() ? path : it->second; }
	bool Db::hasPrefix(const std::string& d) const { return dirs.find(lower(d)) != dirs.end(); }
	const JobEntry* Db::job(const std::string& s) const { for (auto& j : jobs) if (j.sprite == s) return &j; return nullptr; }
	const GearEntry* Db::gear(const std::vector<GearEntry>& v, int id) const { for (auto& g : v) if (g.id == id) return &g; return nullptr; }

	static std::string titleCase(std::string s)
	{
		bool up = true;
		for (auto& c : s) { if (c == '_') { c = ' '; up = true; } else if (up && c >= 'a' && c <= 'z') { c = (char)(c - 32); up = false; } else up = false; }
		return s;
	}

	bool Db::loadMeta(const std::string& text, std::string& error)
	{
		try
		{
			auto j = nlohmann::json::parse(text);
			metaAcc.clear(); metaRobes.clear(); metaWeapons.clear(); metaJobs.clear(); jobConsts.clear(); layerItems.clear();
			for (auto& a : j["accessories"]) { GearEntry e; e.id = a["id"]; e.label = a["name"]; e.sprite = a["sprite"]; metaAcc.push_back(e); }
			for (auto& a : j["robes"]) { GearEntry e; e.id = a["id"]; e.label = a["name"]; e.sprite = a["sprite"]; metaRobes.push_back(e); }
			for (auto& a : j["weapons"]) { GearEntry e; e.id = a["id"]; e.label = a["name"]; e.sprite = a["sprite"]; metaWeapons.push_back(e); }
			for (auto& a : j["jobs"]) { JobEntry e; e.sprite = a["sprite"]; e.label = a["const"]; e.id = a["id"]; e.label = titleCase(std::string(a["const"])); std::transform(e.label.begin() + 1, e.label.end(), e.label.begin() + 1, ::tolower); metaJobs.push_back(e); }
			for (auto it = j["jobConsts"].begin(); it != j["jobConsts"].end(); ++it) jobConsts[it.key()] = it.value();
			if (j.contains("layerPriority"))
			{
				auto& p = j["layerPriority"];
				prioTop = p.value("Default_Top", 200); prioMid = p.value("Default_Mid", 100); prioBottom = p.value("Default_Bottom", 300); prioRobe = p.value("Default_Robe", 400);
			}
			if (j.contains("layerItems"))
				for (auto it = j["layerItems"].begin(); it != j["layerItems"].end(); ++it)
				{
					int id = std::stoi(it.key());
					if (it.value().contains("d")) layerItems[id][-1] = it.value()["d"];
					if (it.value().contains("r"))
						for (auto d = it.value()["r"].begin(); d != it.value()["r"].end(); ++d) layerItems[id][std::stoi(d.key())] = d.value();
				}
			return true;
		}
		catch (std::exception& e) { error = e.what(); return false; }
	}

	void Db::scan(AssetSource& src)
	{
		scanning = true; ready = false;
		std::vector<std::string> all;
		src.listAll(all);
		files.clear(); dirs.clear();
		const std::string roots[] = { "data\\sprite\\", "data\\palette\\", "data\\imf\\" };
		for (auto& f : all)
		{
			std::string l = lower(f);
			bool keep = false;
			for (auto& r : roots) if (l.compare(0, r.size(), r) == 0) { keep = true; break; }
			if (!keep) continue;
			files[l] = f;
			for (size_t p = l.find('\\'); p != std::string::npos; p = l.find('\\', p + 1)) dirs.insert(l.substr(0, p));
		}
		fileCount = files.size();

		for (int g = 0; g < 2; g++) { hairStyles[g].clear(); hairColors[g].clear(); bodyPalettes[g].clear(); }
		jobs = metaJobs;
		for (auto& j : jobs) { j.has[0] = j.has[1] = j.imf[0] = j.imf[1] = false; }
		auto endsWith = [](const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; };

		for (int g = 0; g < 2; g++)
		{
			std::string gs = g2(g);
			// bodies: <job>_<g>.spr directly in the body folder
			std::string bodyDir = lower(bodyPath("", g)); bodyDir = bodyDir.substr(0, bodyDir.size() - gs.size() - 1);
			std::string headDir = lower(headPath(0, g)); headDir = headDir.substr(0, headDir.size() - (std::string("0_") + gs).size());
			std::string palDir = "data\\palette\\\xEB\xA8\xB8\xEB\xA6\xAC\\\xEB\xA8\xB8\xEB\xA6\xAC";
			std::string bpalDir = "data\\palette\\\xEB\xAA\xB8\\";
			std::string shieldDir = "data\\sprite\\\xEB\xB0\xA9\xED\x8C\xA8\\";
			for (auto& kv : files)
			{
				const std::string& l = kv.first;
				if (l.compare(0, bodyDir.size(), bodyDir) == 0 && endsWith(l, ".spr"))
				{
					std::string stem = l.substr(bodyDir.size(), l.size() - bodyDir.size() - 4);
					if (stem.find('\\') == std::string::npos && endsWith(stem, "_" + gs))
					{
						std::string name = stem.substr(0, stem.size() - gs.size() - 1);
						if (!files.count(lower(bodyPath(name, g)) + ".act")) continue;
						JobEntry* je = nullptr;
						for (auto& j : jobs) if (lower(j.sprite) == name) { je = &j; break; }
						if (!je)
						{
							JobEntry n; n.sprite = name; n.label = titleCase(name);
							auto it = jobConsts.find(name);
							n.id = it == jobConsts.end() ? -1 : it->second;
							jobs.push_back(n); je = &jobs.back();
						}
						je->has[g] = true;
						je->imf[g] = files.count(lower(imfPath(je->sprite, g))) > 0;
					}
				}
				else if (l.compare(0, headDir.size(), headDir) == 0 && endsWith(l, ".spr"))
				{
					std::string stem = l.substr(headDir.size(), l.size() - headDir.size() - 4);
					if (stem.find('\\') == std::string::npos && endsWith(stem, "_" + gs))
					{
						std::string n = stem.substr(0, stem.size() - gs.size() - 1);
						if (!n.empty() && std::all_of(n.begin(), n.end(), ::isdigit) && files.count(lower(headPath(std::stoi(n), g)) + ".act"))
							hairStyles[g].push_back(std::stoi(n));
					}
				}
				else if (l.compare(0, palDir.size(), palDir) == 0 && endsWith(l, ".pal"))
				{
					// 머리<style>_<g>_<color>.pal
					std::string rest = l.substr(palDir.size(), l.size() - palDir.size() - 4);
					size_t u = rest.find('_');
					if (u != std::string::npos && rest.compare(u + 1, gs.size(), gs) == 0 && rest.size() > u + 2 + gs.size())
					{
						std::string st = rest.substr(0, u), co = rest.substr(u + 2 + gs.size());
						if (!st.empty() && !co.empty() && std::all_of(st.begin(), st.end(), ::isdigit) && std::all_of(co.begin(), co.end(), ::isdigit))
							hairColors[g][std::stoi(st)].push_back(std::stoi(co));
					}
				}
				else if (l.compare(0, bpalDir.size(), bpalDir) == 0 && endsWith(l, ".pal"))
				{
					std::string rest = l.substr(bpalDir.size(), l.size() - bpalDir.size() - 4);
					std::string suffix = "_" + gs + "_";
					size_t p = rest.rfind(suffix);
					if (p != std::string::npos && rest.find('\\') == std::string::npos)
					{
						std::string n = rest.substr(p + suffix.size());
						if (!n.empty() && std::all_of(n.begin(), n.end(), ::isdigit)) bodyPalettes[g][rest.substr(0, p)].push_back(std::stoi(n));
					}
				}
			}
			std::sort(hairStyles[g].begin(), hairStyles[g].end());
			for (auto& kv : hairColors[g]) std::sort(kv.second.begin(), kv.second.end());
			for (auto& kv : bodyPalettes[g]) std::sort(kv.second.begin(), kv.second.end());
		}
		for (auto& j : jobs) j.group = jobGroup(j.id);
		std::sort(jobs.begin(), jobs.end(), [](const JobEntry& a, const JobEntry& b) { return a.group != b.group ? a.group < b.group : a.label < b.label; });
		jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [](const JobEntry& j) { return !j.has[0] && !j.has[1]; }), jobs.end());

		auto fill = [&](std::vector<GearEntry>& out, const std::vector<GearEntry>& meta, auto exists2)
		{
			out = meta;
			for (auto& e : out) for (int g = 0; g < 2; g++) e.has[g] = exists2(e, g);
		};
		fill(headgear, metaAcc, [&](const GearEntry& e, int g) { return files.count(lower(headgearPath(e, g)) + ".spr") > 0; });
		fill(robes, metaRobes, [&](const GearEntry& e, int g) { return dirs.count(lower("data\\sprite\\\xEB\xA1\x9C\xEB\xB8\x8C\\" + e.sprite + B + g2(g))) > 0; });
		// weapons exist when any job has the file; checked against the jobs found
		fill(weapons, metaWeapons, [&](const GearEntry& e, int g) { for (auto& j : jobs) if (j.has[g] && files.count(lower(weaponPath(e, j.sprite, g)) + ".spr")) return true; return false; });
		// the client keeps the shield names in a table that is not part of the lua files, so they are taken from the folder names
		shields.clear();
		{
			std::set<std::string> found;
			std::string sd = "data\\sprite\\\xEB\xB0\xA9\xED\x8C\xA8\\";
			for (auto& kv : files)
			{
				if (kv.first.compare(0, sd.size(), sd) != 0 || !endsWith(kv.first, ".spr")) continue;
				std::string rest = kv.first.substr(sd.size(), kv.first.size() - sd.size() - 4);
				size_t sl = rest.find('\\');
				if (sl == std::string::npos) continue;
				std::string job = rest.substr(0, sl), file = rest.substr(sl + 1);
				for (int g = 0; g < 2; g++)
				{
					std::string pre = job + "_" + g2(g);
					if (file.compare(0, pre.size(), pre) == 0 && file.size() > pre.size() && file[pre.size()] == '_') found.insert(file.substr(pre.size()));
				}
			}
			shields.assign(found.begin(), found.end());
		}
		for (int kind = 1; kind <= 2; kind++)
		{
			auto& out = kind == 1 ? monsters : npcs;
			out.clear();
			std::string md = lower(mobPath(kind, ""));
			for (auto& kv : files)
			{
				if (kv.first.compare(0, md.size(), md) != 0 || !endsWith(kv.first, ".spr")) continue;
				std::string stem = kv.first.substr(md.size(), kv.first.size() - md.size() - 4);
				if (stem.find('\\') != std::string::npos || !files.count(md + stem + ".act")) continue;
				MobEntry e; e.sprite = kv.second.substr(md.size(), kv.second.size() - md.size() - 4); e.label = titleCase(e.sprite);
				auto it = jobConsts.find(stem);
				e.id = it == jobConsts.end() ? -1 : it->second;
				out.push_back(e);
			}
			std::sort(out.begin(), out.end(), [](const MobEntry& a, const MobEntry& b) { return lower(a.label) < lower(b.label); });
		}
		customWings.clear();
		{
			std::string cd = "data\\sprite\\custom\\";
			for (auto& kv : files)
				if (kv.first.compare(0, cd.size(), cd) == 0 && endsWith(kv.first, ".spr") && files.count(kv.first.substr(0, kv.first.size() - 4) + ".act"))
					customWings.push_back(kv.second.substr(0, kv.second.size() - 4));
			std::sort(customWings.begin(), customWings.end());
		}
		ready = true; scanning = false;
	}
}
