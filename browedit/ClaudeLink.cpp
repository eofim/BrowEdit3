// Live link: watches claude_link\inbox for command files (json) and carries them out on the open map, step by step,
// so the changes can be watched in the editor. Everything goes through the normal undo system.
#include <windows.h>
#include <browedit/BrowEdit.h>
#include <browedit/MapView.h>
#include <browedit/Map.h>
#include <browedit/Node.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/Gat.h>
#include <browedit/components/Rsm.h>
#include <browedit/components/Rsw.h>
#include <browedit/components/RsmRenderer.h>
#include <browedit/components/WaterRenderer.h>
#include <browedit/actions/GroupAction.h>
#include <browedit/actions/TileNewAction.h>
#include <browedit/actions/CubeTileChangeAction.h>
#include <browedit/actions/CubeHeightChangeAction.h>
#include <browedit/actions/GndTextureActions.h>
#include <browedit/actions/GatTileChangeAction.h>
#include <browedit/actions/NewObjectAction.h>
#include <browedit/actions/DeleteObjectAction.h>
#include <browedit/util/ResourceManager.h>
#include <browedit/util/Util.h>
#include <imgui.h>
#include <json.hpp>
#include <filesystem>
#include <fstream>
#include <deque>
#include <chrono>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace
{
	namespace fs = std::filesystem;
	using json = nlohmann::json;

	struct Job
	{
		std::string name;
		json commands;
		json results = json::array();
		size_t index = 0;		// current command
		int phase = 0;			// state inside the current command
		size_t cursor = 0;		// progress inside the current command
		int waitFrames = 0;
		std::vector<Node*> created;
		std::string label;
		float progress = 0.0f;
	};

	std::deque<Job> jobs;
	std::map<std::string, std::pair<uintmax_t, int>> seen;	// file -> (size at last poll, polls it stayed the same)
	double lastPoll = 0;
	double lastStatus = 0;
	std::string lastMessage;

	const char* inboxDir = "claude_link\\inbox";
	const char* outboxDir = "claude_link\\outbox";
	const char* doneDir = "claude_link\\done";

	std::string lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), ::tolower);
		return s;
	}

	int findTexture(Gnd* gnd, const std::string& file)
	{
		if (file.size() > 1 && file[0] == '#' && isdigit((unsigned char)file[1]))
		{
			int n = atoi(file.c_str() + 1);
			return (n >= 0 && n < (int)gnd->textures.size()) ? n : -1;
		}
		std::string f = lower(file);
		std::replace(f.begin(), f.end(), '/', '\\');
		for (size_t i = 0; i < gnd->textures.size(); i++)
		{
			std::string t = lower(gnd->textures[i]->file);
			std::replace(t.begin(), t.end(), '/', '\\');
			if (t == f)
				return (int)i;
			if (lower(util::iso_8859_1_to_utf8(gnd->textures[i]->file)) == f)
				return (int)i;
		}
		return -1;
	}

	int ensureTexture(Map* map, BrowEdit* browEdit, Gnd* gnd, const std::string& file)
	{
		int idx = findTexture(gnd, file);
		if (idx >= 0)
			return idx;
		// names in the JSON are UTF-8; the map stores raw (cp949 as latin-1) bytes, so "ÇÊµå¹Ù´Ú\\x" style names must be converted back
		std::string raw = util::utf8_to_iso_8859_1(file);
		map->doAction(new GndTextureAddAction(raw), browEdit);
		idx = findTexture(gnd, file);
		if (idx < 0)
			throw std::runtime_error("could not add texture " + file);
		return idx;
	}

	Gnd::Tile* newTile(int texture, bool wall)
	{
		auto t = new Gnd::Tile();
		if (wall)
		{	// same layout as the rest of the app: top of the wall at v=0
			t->v1 = glm::vec2(0, 0); t->v2 = glm::vec2(1, 0); t->v3 = glm::vec2(0, 1); t->v4 = glm::vec2(1, 1);
		}
		else
		{	// ground tiles use the same orientation as the texture fill tool
			t->v1 = glm::vec2(0, 1); t->v2 = glm::vec2(1, 1); t->v3 = glm::vec2(0, 0); t->v4 = glm::vec2(1, 0);
		}
		t->color = glm::ivec4(255, 255, 255, 255);
		t->lightmapIndex = 0;
		t->textureIndex = (short)texture;
		return t;
	}

	// returns true when the command is finished
	bool stepCommand(Job& job, BrowEdit* browEdit, Map* map, json& result)
	{
		const json& cmd = job.commands[job.index];
		std::string name = cmd.value("cmd", "");
		auto gnd = map->rootNode->getComponent<Gnd>();
		auto gat = map->rootNode->getComponent<Gat>();
		auto rsw = map->rootNode->getComponent<Rsw>();
		job.label = name;
		result["cmd"] = name;
		result["ok"] = true;

		if (name == "info")
		{
			result["map"] = util::iso_8859_1_to_utf8(map->name);
			result["width"] = gnd->width;
			result["height"] = gnd->height;
			result["gatWidth"] = gat->width;
			result["gatHeight"] = gat->height;
			result["textures"] = json::array();
			for (auto t : gnd->textures)
				result["textures"].push_back(util::iso_8859_1_to_utf8(t->file));
			int models = 0, objects = 0;
			map->rootNode->traverse([&](Node* n) {
				if (n->getComponent<RswObject>()) objects++;
				if (n->getComponent<RswModel>()) models++;
			});
			result["models"] = models;
			result["objects"] = objects;
			return true;
		}
		else if (name == "textures")
		{
			for (const auto& t : cmd["add"])
				ensureTexture(map, browEdit, gnd, t.get<std::string>());
			return true;
		}
		else if (name == "paint")
		{
			int tex = ensureTexture(map, browEdit, gnd, cmd["texture"].get<std::string>());
			const auto& rows = cmd["rows"];
			auto ga = new GroupAction("Claude: paint " + cmd["texture"].get<std::string>());
			int id = (int)gnd->tiles.size();
			size_t end = std::min(rows.size(), job.cursor + 30);
			for (size_t r = job.cursor; r < end; r++)
			{
				int y = rows[r][0], x0 = rows[r][1], x1 = rows[r][2];
				if (y < 0 || y >= gnd->height)
					continue;
				for (int x = std::max(x0, 0); x < std::min(x1, gnd->width); x++)
				{
					auto cube = gnd->cubes[x][y];
					ga->addAction(new TileNewAction(newTile(tex, false)));
					ga->addAction(new CubeTileChangeAction(glm::ivec2(x, y), cube, id, cube->tileFront, cube->tileSide));
					id++;
				}
			}
			map->doAction(ga, browEdit);
			job.cursor = end;
			job.progress = rows.size() ? (float)job.cursor / rows.size() : 1.0f;
			return job.cursor >= rows.size();
		}
		else if (name == "tiles")
		{	// per cube tiles with their own UVs: [x, y, texture index, u1,v1, u2,v2, u3,v3, u4,v4] (corners h1..h4)
			const auto& list = cmd["list"];
			auto ga = new GroupAction("Claude: tiles");
			int id = (int)gnd->tiles.size();
			size_t end = std::min(list.size(), job.cursor + 150);
			for (size_t r = job.cursor; r < end; r++)
			{
				const auto& e = list[r];
				int x = e[0], y = e[1];
				if (!gnd->inMap(glm::ivec2(x, y)))
					continue;
				int tex = e[2];
				if (tex < 0 || tex >= (int)gnd->textures.size())
					continue;
				auto cube = gnd->cubes[x][y];
				auto t = newTile(tex, false);
				t->v1 = glm::vec2(e[3].get<float>(), e[4].get<float>());
				t->v2 = glm::vec2(e[5].get<float>(), e[6].get<float>());
				t->v3 = glm::vec2(e[7].get<float>(), e[8].get<float>());
				t->v4 = glm::vec2(e[9].get<float>(), e[10].get<float>());
				ga->addAction(new TileNewAction(t));
				ga->addAction(new CubeTileChangeAction(glm::ivec2(x, y), cube, id, cube->tileFront, cube->tileSide));
				id++;
			}
			map->doAction(ga, browEdit);
			job.cursor = end;
			job.progress = list.size() ? (float)job.cursor / list.size() : 1.0f;
			return job.cursor >= list.size();
		}
		else if (name == "heights")
		{
			int x0 = cmd["x0"], y0 = cmd["y0"], w = cmd["w"], h = cmd["h"];
			const auto& data = cmd["data"];
			if (job.phase == 0)
			{
				size_t endRow = std::min((size_t)h, job.cursor + 12);
				std::map<Gnd::Cube*, float[4]> oldG, newG;
				std::map<Gat::Cube*, float[4]> oldT, newT;
				std::vector<glm::ivec2> selG, selT;
				bool withGat = cmd.value("gat", true) && gat && gat->width == gnd->width * 2;
				for (size_t ry = job.cursor; ry < endRow; ry++)
					for (int rx = 0; rx < w; rx++)
					{
						int x = x0 + rx, y = y0 + (int)ry;
						if (!gnd->inMap(glm::ivec2(x, y)))
							continue;
						auto cube = gnd->cubes[x][y];
						size_t o = (ry * w + rx) * 4;
						float nh[4] = { data[o].get<float>(), data[o + 1].get<float>(), data[o + 2].get<float>(), data[o + 3].get<float>() };
						for (int i = 0; i < 4; i++)
						{
							oldG[cube][i] = cube->heights[i];
							newG[cube][i] = nh[i];
						}
						selG.push_back(glm::ivec2(x, y));
						if (withGat)
							for (int k = 0; k < 4; k++)
							{
								int gx = x * 2 + (k % 2), gy = y * 2 + (k >> 1);
								auto gc = gat->cubes[gx][gy];
								float fu0 = (k % 2) / 2.0f, fu1 = fu0 + 0.5f, fv0 = (k >> 1) / 2.0f, fv1 = fv0 + 0.5f;
								auto at = [&](float fu, float fv) { return (nh[0] * (1 - fu) + nh[1] * fu) * (1 - fv) + (nh[2] * (1 - fu) + nh[3] * fu) * fv; };
								float gh[4] = { at(fu0, fv0), at(fu1, fv0), at(fu0, fv1), at(fu1, fv1) };
								for (int i = 0; i < 4; i++)
								{
									oldT[gc][i] = gc->heights[i];
									newT[gc][i] = gh[i];
								}
								selT.push_back(glm::ivec2(gx, gy));
							}
					}
				auto ga = new GroupAction("Claude: terrain heights");
				ga->addAction(new CubeHeightChangeAction<Gnd, Gnd::Cube>(oldG, newG, selG));
				if (!newT.empty())
					ga->addAction(new CubeHeightChangeAction<Gat, Gat::Cube>(oldT, newT, selT));
				map->doAction(ga, browEdit);
				job.cursor = endRow;
				job.progress = h ? (float)job.cursor / h * (cmd.contains("wall_texture") ? 0.7f : 1.0f) : 1.0f;
				if (job.cursor < (size_t)h)
					return false;
				if (!cmd.contains("wall_texture"))
					return true;
				job.phase = 1;
				job.cursor = 0;
				return false;
			}
			// phase 1: cliff walls where neighbouring cubes have different heights
			int wallTex = ensureTexture(map, browEdit, gnd, cmd["wall_texture"].get<std::string>());
			auto ga = new GroupAction("Claude: cliff walls");
			int id = (int)gnd->tiles.size();
			size_t endRow = std::min((size_t)h, job.cursor + 20);
			for (size_t ry = job.cursor; ry < endRow; ry++)
				for (int rx = 0; rx < w; rx++)
				{
					int x = x0 + rx, y = y0 + (int)ry;
					if (!gnd->inMap(glm::ivec2(x, y)))
						continue;
					auto c = gnd->cubes[x][y];
					bool needSide = false, needFront = false;
					if (x < gnd->width - 1)
					{
						auto a = gnd->cubes[x + 1][y];
						needSide = std::abs(c->h2 - a->h1) > 1e-3f || std::abs(c->h4 - a->h3) > 1e-3f;
					}
					if (y < gnd->height - 1)
					{
						auto a = gnd->cubes[x][y + 1];
						needFront = std::abs(c->h3 - a->h1) > 1e-3f || std::abs(c->h4 - a->h2) > 1e-3f;
					}
					int side = needSide ? id : -1;
					if (needSide) { ga->addAction(new TileNewAction(newTile(wallTex, true))); id++; }
					int front = needFront ? id : -1;
					if (needFront) { ga->addAction(new TileNewAction(newTile(wallTex, true))); id++; }
					if (side != c->tileSide || front != c->tileFront)
						ga->addAction(new CubeTileChangeAction(glm::ivec2(x, y), c, c->tileUp, front, side));
				}
			map->doAction(ga, browEdit);
			job.cursor = endRow;
			job.progress = 0.7f + 0.3f * (h ? (float)job.cursor / h : 1.0f);
			return job.cursor >= (size_t)h;
		}
		else if (name == "gat")
		{
			const auto& rows = cmd["rows"];
			auto ga = new GroupAction("Claude: gat");
			size_t end = std::min(rows.size(), job.cursor + 60);
			for (size_t r = job.cursor; r < end; r++)
			{
				int y = rows[r][0], x0 = rows[r][1], x1 = rows[r][2], type = rows[r][3];
				if (y < 0 || y >= gat->height)
					continue;
				for (int x = std::max(x0, 0); x < std::min(x1, gat->width); x++)
					if (gat->cubes[x][y]->gatType != type)
						ga->addAction(new GatTileChangeAction(glm::ivec2(x, y), gat->cubes[x][y], type));
			}
			map->doAction(ga, browEdit);
			job.cursor = end;
			job.progress = rows.size() ? (float)job.cursor / rows.size() : 1.0f;
			return job.cursor >= rows.size();
		}
		else if (name == "models")
		{
			const auto& items = cmd["items"];
			if (job.phase == 0)
			{
				auto ga = new GroupAction("Claude: place models");
				size_t end = std::min(items.size(), job.cursor + 6);
				for (size_t i = job.cursor; i < end; i++)
				{
					const auto& it = items[i];
					std::string file = it["file"].get<std::string>();
					std::replace(file.begin(), file.end(), '/', '\\');
					std::string path = "data\\model\\" + file;
					std::string nodeName = it.value("name", file.substr(0, file.rfind(".")));
					Node* node = new Node(util::iso_8859_1_to_utf8(nodeName));
					auto rsm = util::ResourceManager<Rsm>::load(path);
					node->addComponent(rsm);
					node->addComponent(new RsmRenderer());
					auto o = new RswObject();
					node->addComponent(o);
					auto pos = it["pos"];
					o->position = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
					if (it.contains("rot"))
						o->rotation = glm::vec3(it["rot"][0].get<float>(), it["rot"][1].get<float>(), it["rot"][2].get<float>());
					if (it.contains("scale"))
						o->scale = glm::vec3(it["scale"][0].get<float>(), it["scale"][1].get<float>(), it["scale"][2].get<float>());
					node->addComponent(new RswModel(util::iso_8859_1_to_utf8(file)));
					node->addComponent(new RswModelCollider());
					ga->addAction(new NewObjectAction(node));
					job.created.push_back(node);
				}
				map->doAction(ga, browEdit);
				job.cursor = end;
				job.progress = items.size() ? (float)job.cursor / items.size() * 0.9f : 1.0f;
				if (job.cursor < items.size())
					return false;
				if (!cmd.value("smart_gat", false))
					return true;
				job.phase = 1;
				job.waitFrames = 3;	// the models have to be drawn once before their shape is known
				return false;
			}
			if (job.waitFrames-- > 0)
				return false;
			std::vector<Node*> alive;
			for (auto n : job.created)
				if (n->parent)
					alive.push_back(n);
			auto ga = new GroupAction("Claude: smart GAT for placed models");
			map->addAutoGatActions(alive, browEdit->autoGatBandHeight, browEdit->autoGatType, ga, false, false, browEdit->autoGatFillInterior, browEdit->autoGatCloseGap);
			map->doAction(ga, browEdit);
			job.progress = 1.0f;
			return true;
		}
		else if (name == "smart_gat")
		{
			std::vector<Node*> models;
			map->rootNode->traverse([&](Node* n) {
				if (n->getComponent<RswModel>())
					models.push_back(n);
			});
			auto ga = new GroupAction("Claude: smart GAT for all models");
			map->addAutoGatActions(models, browEdit->autoGatBandHeight, browEdit->autoGatType, ga, false, false, browEdit->autoGatFillInterior, browEdit->autoGatCloseGap);
			map->doAction(ga, browEdit);
			result["models"] = (int)models.size();
			return true;
		}
		else if (name == "delete_models")
		{
			std::string filter = lower(cmd.value("filter", ""));
			std::vector<Node*> nodes;
			map->rootNode->traverse([&](Node* n) {
				auto m = n->getComponent<RswModel>();
				if (m && (filter.empty() || lower(m->fileName).find(filter) != std::string::npos))
					nodes.push_back(n);
			});
			auto ga = new GroupAction("Claude: delete models");
			for (auto n : nodes)
				ga->addAction(new DeleteObjectAction(n));
			map->autoGatFollow(nodes, browEdit, ga, true);
			map->doAction(ga, browEdit);
			result["deleted"] = (int)nodes.size();
			return true;
		}
		else if (name == "water")
		{
			if (rsw->water.zones.empty())
				throw std::runtime_error("map has no water zones");
			for (auto& col : rsw->water.zones)
				for (auto& z : col)
				{
					z.height = cmd.value("height", z.height);
					z.type = cmd.value("type", z.type);
					z.amplitude = cmd.value("amplitude", z.amplitude);
					z.waveSpeed = cmd.value("wave_speed", z.waveSpeed);
					z.wavePitch = cmd.value("wave_pitch", z.wavePitch);
					z.textureAnimSpeed = cmd.value("anim_speed", z.textureAnimSpeed);
				}
			auto waterRenderer = map->rootNode->getComponent<WaterRenderer>();
			if (waterRenderer)
			{
				waterRenderer->renderFullWater = false;
				waterRenderer->setDirty();
			}
			for (auto& mv : browEdit->mapViews)
				if (mv.map == map)
					mv.waterGridDirty = true;
			return true;
		}
		else if (name == "camera")
		{
			if (browEdit->activeMapView)
			{
				auto& mv = *browEdit->activeMapView;
				float u = cmd.value("u", gnd->width / 2.0f), v = cmd.value("v", gnd->height / 2.0f);
				mv.cameraCenter = glm::vec3(10.0f * u, cmd.value("y", 0.0f), 10.0f * (gnd->height - v) + 10.0f);
				mv.cameraDistance = cmd.value("distance", mv.cameraDistance);
				if (cmd.contains("rot"))
					mv.cameraRot = glm::vec2(cmd["rot"][0].get<float>(), cmd["rot"][1].get<float>());
			}
			return true;
		}
		else if (name == "save")
		{
			browEdit->saveMap(map);
			return true;
		}
		else if (name == "undo")
		{
			for (int i = 0; i < cmd.value("count", 1); i++)
				map->undo(browEdit);
			return true;
		}
		else if (name == "redo")
		{
			for (int i = 0; i < cmd.value("count", 1); i++)
				map->redo(browEdit);
			return true;
		}
		else if (name == "wait")
		{
			if (job.waitFrames == 0 && job.phase == 0)
			{
				job.waitFrames = cmd.value("frames", 30);
				job.phase = 1;
			}
			return job.waitFrames-- <= 0;
		}
		throw std::runtime_error("unknown command '" + name + "'");
	}

	void finishJob(Job& job, bool ok)
	{
		try {
		json out;
		out["file"] = job.name;
		out["ok"] = ok;
		out["results"] = job.results;
		std::ofstream f(std::string(outboxDir) + "\\" + job.name + ".result.json");
		f << out.dump(1, ' ', false, json::error_handler_t::replace);
		} catch (...) {}
		try { fs::rename(std::string(inboxDir) + "\\" + job.name + ".json", std::string(doneDir) + "\\" + job.name + ".json"); }
		catch (...) {}
	}
}

void BrowEdit::claudeLink()
{
	static bool initialised = false;
	if (!initialised)
	{
		initialised = true;
		std::error_code ec;
		fs::create_directories(inboxDir, ec);
		fs::create_directories(outboxDir, ec);
		fs::create_directories(doneDir, ec);
	}
	if (!claudeLinkEnabled)
		return;

	double now = ImGui::GetTime();
	Map* map = activeMapView ? activeMapView->map : nullptr;

	// look for new command files, a file is only taken once it stopped growing
	if (now - lastPoll > 0.5)
	{
		lastPoll = now;
		std::vector<std::string> files;
		std::error_code ec;
		for (auto& e : fs::directory_iterator(inboxDir, ec))
			if (e.is_regular_file() && e.path().extension() == ".json")
				files.push_back(e.path().filename().string());
		std::sort(files.begin(), files.end());
		for (const auto& f : files)
		{
			std::string stem = f.substr(0, f.size() - 5);
			bool queued = std::any_of(jobs.begin(), jobs.end(), [&](const Job& j) { return j.name == stem; });
			if (queued)
				continue;
			auto size = fs::file_size(std::string(inboxDir) + "\\" + f, ec);
			auto& s = seen[f];
			if (s.first == size && size > 0)
				s.second++;
			else
				s = { size, 0 };
			if (s.second < 1)
				continue;
			try
			{
				std::ifstream in(std::string(inboxDir) + "\\" + f, std::ios::binary);
				json doc = json::parse(in);
				Job job;
				job.name = stem;
				job.commands = doc.is_array() ? doc : doc["commands"];
				jobs.push_back(job);
				seen.erase(f);
			}
			catch (const std::exception& e)
			{
				if (s.second > 6)
				{	// really broken, report it and move on
					Job bad;
					bad.name = stem;
					bad.results.push_back({ {"ok", false}, {"error", std::string("cannot read file: ") + e.what()} });
					finishJob(bad, false);
					seen.erase(f);
				}
			}
		}
	}

	// do some work, a little each frame so the result can be watched
	auto start = std::chrono::steady_clock::now();
	while (!jobs.empty())
	{
		Job& job = jobs.front();
		if (!map)
		{
			job.results.push_back({ {"ok", false}, {"error", "no map is open in BrowEdit"} });
			finishJob(job, false);
			jobs.pop_front();
			continue;
		}
		if (job.index >= job.commands.size())
		{
			finishJob(job, true);
			jobs.pop_front();
			continue;
		}
		json result = json::object();
		bool done = false;
		try
		{
			done = stepCommand(job, this, map, result);
		}
		catch (const std::exception& e)
		{
			result["ok"] = false;
			result["error"] = e.what();
			result["cmd"] = job.commands[job.index].value("cmd", "");
			done = true;
		}
		if (done)
		{
			job.results.push_back(result);
			job.index++;
			job.phase = 0;
			job.cursor = 0;
			job.waitFrames = 0;
			job.created.clear();
			job.progress = 0;
		}
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
		if (ms > 25 || job.waitFrames > 0)
			break;
	}

	// small progress box while Claude is working
	if (!jobs.empty())
	{
		auto& job = jobs.front();
		ImGui::SetNextWindowPos(ImVec2(10, 60), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.8f);
		if (ImGui::Begin("##claudelink", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs))
		{
			ImGui::Text("Claude: %s (%d/%d)", job.label.c_str(), (int)job.index + 1, (int)job.commands.size());
			ImGui::ProgressBar(job.progress, ImVec2(220, 6), "");
		}
		ImGui::End();
	}

	// heartbeat, so the other side can see that the link is alive and what map is open
	if (now - lastStatus > 1.0)
	{
		lastStatus = now;
		json st;
		st["alive"] = true;
		st["time"] = now;
		st["queue"] = (int)jobs.size();
		st["map"] = map ? util::iso_8859_1_to_utf8(map->name) : "";
		if (map)
		{
			auto gnd = map->rootNode->getComponent<Gnd>();
			if (gnd)
			{
				st["width"] = gnd->width;
				st["height"] = gnd->height;
			}
		}
		std::ofstream f(std::string("claude_link\\status.json"));
		f << st.dump(1, ' ', false, json::error_handler_t::replace);
	}
}
