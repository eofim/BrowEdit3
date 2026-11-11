#include <windows.h>
#include <browedit/BrowEdit.h>
#include <misc/cpp/imgui_stdlib.h>
#include <browedit/util/FileIO.h>
#include <browedit/Map.h>
#include <browedit/Node.h>
#include <browedit/components/Rsm.h>
#include <browedit/components/Rsw.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/Str.h>
#include <browedit/components/SprRenderer.h>
#include <browedit/util/ResourceManager.h>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <set>
#include <map>

void collectEffectAssets(int id, std::vector<std::pair<std::string, std::string>>& out);	// Rsw.Effect.cpp

void BrowEdit::showExportWindow()
{
	if (windowData.exportVisible)
		ImGui::OpenPopup("Export Map");
	ImVec2 center = ImGui::GetMainViewport()->GetCenter();
	ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

	if (ImGui::BeginPopupModal("Export Map", 0, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::InputText("Export folder", &windowData.exportFolder);
		ImGui::SameLine();
		if (ImGui::Button("Browse"))
		{
			windowData.exportFolder = util::iso_8859_1_to_utf8(util::SelectPathDialog(util::utf8_to_iso_8859_1(windowData.exportFolder)));
		}
		static bool exportMinimap = true;
		static int exportMinimapSize = 512;
		ImGui::Checkbox("Also generate the rendered minimap", &exportMinimap);
		if (exportMinimap)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(90);
			ImGui::InputInt("px", &exportMinimapSize, 0, 0);
			exportMinimapSize = std::clamp(exportMinimapSize, 64, 4096);
		}
		if (ImGui::Button("Export!"))
		{
			char buf[1024];
			for (const auto& e : windowData.exportToExport)
			{
				if (!e.enabled)
					continue;
				std::string outFileName = util::utf8_to_iso_8859_1(windowData.exportFolder) + e.filename;
				std::filesystem::path path(outFileName);
				auto p = path.parent_path().string();
				std::filesystem::create_directories(p);

				auto is = util::FileIO::open(e.filename);
				if (!is)
				{
					std::cout << "Error opening " << e.filename << std::endl;
					continue;
				}
				auto os = std::ofstream(outFileName, std::ios_base::out | std::ios_base::binary);
				while (!is->eof())
				{
					is->read(buf, 1024);
					auto count = is->gcount();
					os.write(buf, count);
				}
				delete is;
			}
			if (exportMinimap && activeMapView && activeMapView->map == windowData.exportMap)
			{
				std::string root = util::utf8_to_iso_8859_1(windowData.exportFolder);
				if (!root.empty() && root.back() != '\\' && root.back() != '/') root += "\\";
				std::cout << activeMapView->renderMinimap(this, exportMinimapSize, true, root) << std::endl;
			}
			windowData.exportVisible = false;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			windowData.exportVisible = false;
			ImGui::CloseCurrentPopup();
		}

		ImGui::SameLine();
		if (ImGui::Button("Select all")) for (auto& e : windowData.exportToExport) e.enabled = true;
		ImGui::SameLine();
		if (ImGui::Button("Select none")) for (auto& e : windowData.exportToExport) e.enabled = false;
		{
			std::map<std::string, std::pair<int, int>> counts;
			for (auto& e : windowData.exportToExport) { counts[e.type].second++; if (e.enabled) counts[e.type].first++; }
			std::string summary;
			for (auto& c : counts) summary += c.first + " " + std::to_string(c.second.first) + "/" + std::to_string(c.second.second) + "   ";
			ImGui::TextWrapped("%s", summary.c_str());
		}
		if (ImGui::BeginTable("Data", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg, ImVec2(780, 420)))
		{
			for (auto& e : windowData.exportToExport)
			{
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				if (ImGui::Checkbox(util::iso_8859_1_to_utf8(e.filename).c_str(), &e.enabled))
				{
					for (auto c : e.linkedForward)
					{
						bool canDisabled = true;
						for (auto cc : c->linkedBackward)
							if (cc->enabled)
								canDisabled = false;
						c->enabled = !canDisabled;
					}
				}
				ImGui::TableNextColumn();
				ImGui::Text(e.type.c_str());
				ImGui::TableNextColumn();
				ImGui::Text(e.source.c_str());
				ImGui::TableNextColumn();
				for(auto i : e.linkedBackward)
					if(i->enabled)
						ImGui::Text(util::iso_8859_1_to_utf8(i->filename).c_str());
			}

			ImGui::EndTable();
		}

		ImGui::EndPopup();
	}
}


void BrowEdit::exportMap(Map* map)
{
	windowData.exportToExport.clear();
	windowData.exportToExport.push_back(WindowData::ExportInfo{ map->name,"rsw", util::FileIO::getSrc(map->name), true });
	windowData.exportToExport.push_back(WindowData::ExportInfo{ map->name.substr(0, map->name.size() - 4) + ".gnd","gnd", util::FileIO::getSrc(map->name.substr(0, map->name.size() - 4) + ".gnd"), true });
	windowData.exportToExport.push_back(WindowData::ExportInfo{ map->name.substr(0, map->name.size() - 4) + ".gat","gat", util::FileIO::getSrc(map->name.substr(0, map->name.size() - 4) + ".gat"), true });
	map->rootNode->traverse([&](Node* n)
	{
		auto gnd = n->getComponent<Gnd>();
		if (gnd)
		{
			for (const auto& t : gnd->textures)
				windowData.exportToExport.push_back(WindowData::ExportInfo{ "data\\texture\\" + t->file,"gndTexture", util::FileIO::getSrc("data\\texture\\" + t->file), true});
		}
		auto rsm = n->getComponent<Rsm>();
		if (rsm)
		{
			if (std::find_if(windowData.exportToExport.begin(), windowData.exportToExport.end(), [rsm](const WindowData::ExportInfo& ei) { return ei.filename == rsm->fileName; }) != windowData.exportToExport.end())
				return;
			windowData.exportToExport.push_back(WindowData::ExportInfo{ rsm->fileName ,"rsm", util::FileIO::getSrc(rsm->fileName), true});
			auto modelExport = &windowData.exportToExport.back();
			//find all textures used by this model
			std::vector<std::string> textures;
			textures.insert(textures.end(), rsm->textures.begin(), rsm->textures.end());
			//std::function<void(Rsm::Mesh*)> addTex;
			//addTex = [&textures, &addTex](Rsm::Mesh* m)
			//{
			//	textures.insert(textures.end(), m->textureFiles.begin(), m->textureFiles.end());
			//	for (auto n : m->children)
			//		addTex(n);
			//};
			//addTex(rsm->rootMesh);
			std::sort(textures.begin(), textures.end());
			auto last = std::unique(textures.begin(), textures.end());
			textures.erase(last, textures.end());
			//add textures
			for (const auto& t : textures)
			{
				auto textureFound = std::find_if(windowData.exportToExport.begin(), windowData.exportToExport.end(), [t](const WindowData::ExportInfo& ei) { return ei.filename == "data\\model\\" + t; });
				if (textureFound == windowData.exportToExport.end())
				{
					windowData.exportToExport.push_back(WindowData::ExportInfo{ "data\\texture\\" + t, "rsmTexture", util::FileIO::getSrc("data\\texture\\" + t), true });
					windowData.exportToExport.back().linkedBackward.push_back(modelExport);
					modelExport->linkedForward.push_back(&windowData.exportToExport.back());
				}
				else
				{
					textureFound->linkedBackward.push_back(modelExport);
					modelExport->linkedForward.push_back(&(*textureFound));
				}
			}

		}
	});



	// ---- effects, sounds, lub files and water textures used by the map ----
	{
		auto lowerOf = [](std::string f) { for (auto& c : f) { if (c == '/') c = '\\'; c = (char)tolower((unsigned char)c); } return f; };
		auto addAsset = [&](const std::string& file, const std::string& type, WindowData::ExportInfo* owner, bool enabled = true) -> WindowData::ExportInfo*
		{
			std::string norm = lowerOf(file);
			WindowData::ExportInfo* found = nullptr;
			for (auto& e : windowData.exportToExport)
				if (lowerOf(e.filename) == norm) { found = &e; break; }
			if (!found)
			{
				if (!util::FileIO::exists(file))
					return nullptr;
				windowData.exportToExport.push_back(WindowData::ExportInfo{ file, type, util::FileIO::getSrc(file), enabled });
				found = &windowData.exportToExport.back();
			}
			if (owner && std::find(found->linkedBackward.begin(), found->linkedBackward.end(), owner) == found->linkedBackward.end())
			{
				found->linkedBackward.push_back(owner);
				owner->linkedForward.push_back(found);
			}
			return found;
		};
		// a .str and every texture its layers use
		auto addStr = [&](const std::string& strPath)
		{
			auto strInfo = addAsset(strPath, "str", nullptr);
			if (!strInfo) return;
			Str* str = nullptr;
			try { str = util::ResourceManager<Str>::load(strPath); } catch (...) {}
			if (!str) return;
			std::string folder = strPath.substr(0, strPath.find_last_of("\\/") + 1);
			for (auto& layer : str->layers)
				for (auto& t : layer->textures)
					addAsset(folder + t, "strTexture", strInfo);
		};
		std::set<int> effectIds;
		map->rootNode->traverse([&](Node* n)
		{
			if (auto sound = n->getComponent<RswSound>())
				addAsset("data\\wav\\" + sound->fileName, "sound", nullptr);
			auto effect = n->getComponent<RswEffect>();
			if (!effect)
				return;
			if (auto se = n->getComponent<StrEffect>())
				addStr("data\\texture\\effect\\" + util::utf8_to_iso_8859_1(se->str));
			if (auto sp = n->getComponent<SprEffect>())
			{
				std::string base = "data\\sprite\\\xC0\xCC\xC6\xD1\xC6\xAE\\" + sp->file;
				auto spr = addAsset(base + ".spr", "sprite", nullptr);
				addAsset(base + ".act", "sprite", spr);
			}
			if (auto le = n->getComponent<LubEffect>())
				addAsset("data\\texture\\" + util::replace(le->texture, "/", "\\"), "effectTexture", nullptr);
			if (auto lw = n->getComponent<LubWindEffect>())
				addAsset("data\\texture\\" + util::replace(lw->texture, "/", "\\"), "effectTexture", nullptr);
			effectIds.insert(effect->id);
		});
		for (int id : effectIds)
		{
			std::vector<std::pair<std::string, std::string>> assets;
			collectEffectAssets(id, assets);
			for (auto& a : assets)
			{
				if (a.second == "str") addStr(a.first);
				else addAsset(a.first, a.second, nullptr);
			}
		}
		// lua files of the map (effect tool; the sky map table is shared by all maps so it starts unticked)
		std::string mapName = map->name;
		if (mapName.find_last_of("\\/") != std::string::npos) mapName = mapName.substr(mapName.find_last_of("\\/") + 1);
		if (mapName.size() > 4) mapName = mapName.substr(0, mapName.size() - 4);
		addAsset("data\\luafiles514\\lua files\\effecttool\\" + mapName + ".lub", "lub", nullptr);
		addAsset("data\\luafiles514\\lua files\\mapskydata\\mapskydata.lub", "lub (shared)", nullptr, false);
		// water
		if (auto rsw = map->rootNode->getComponent<Rsw>())
		{
			std::set<int> waterTypes;
			for (auto& col : rsw->water.zones) for (auto& z : col) waterTypes.insert(z.type);
			for (int t : waterTypes)
				for (int i = 0; i < 32; i++)
				{
					char buf[160];
					sprintf_s(buf, sizeof(buf), "data\\texture\\\xBF\xF6\xC5\xCD\\water%i%02i.jpg", t, i);
					addAsset(buf, "water", nullptr);
				}
		}
	}

	windowData.exportVisible = true;
	windowData.exportMap = map;
	ImGui::OpenPopup("Export Map");
}