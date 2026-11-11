#include <windows.h>
#include <browedit/BrowEdit.h>
#include <browedit/MapView.h>
#include <browedit/ModelVariants.h>
#include <browedit/Map.h>
#include <browedit/Node.h>
#include <browedit/gl/FBO.h>
#include <browedit/gl/Texture.h>
#include <browedit/util/FileIO.h>
#include <browedit/util/Util.h>
#include <browedit/Lightmapper.h>
#include <browedit/components/Gnd.h>
#include <browedit/components/GndRenderer.h>
#include <browedit/components/RsmRenderer.h>
#include <browedit/components/LubRenderer.h>
#include <browedit/components/LubWindRenderer.h>
#include <browedit/components/StrRenderer.h>
#include <browedit/components/Rsw.h>
#include <browedit/components/BillboardRenderer.h>
#include <browedit/util/ResourceManager.h>
#include <browedit/HotkeyRegistry.h>
#include <browedit/actions/SelectAction.h>
#include <browedit/actions/GroupAction.h>
#include <GLFW/glfw3.h>
#include <imgui_internal.h>
#include <fstream>
#include <set>
#include <algorithm>
#include <json.hpp>

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#include <filesystem>
#include <iterator>

namespace
{
	bool openTextureSheetPopup = false;
	bool openMinimapPopup = false;
	int minimapSize = 512;
	bool minimapShading = true, minimapRelief = false, minimapPad = true;
	std::string minimapResult;
	char textureSheetPrefix[128] = "prontera_re";
	std::string textureSheetResult;

	// contact sheet of the textures in a folder of the client, with the index of every thumbnail, so a tool that can
	// only look at pictures knows what the textures look like. The names are written next to it in a .json file.
	void exportTextureSheets(const std::string& prefixIn)
	{
		static const char* digits[10] = {
			"111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
			"111100111001111", "111100111101111", "111001001010010", "111101111101111", "111101111001111" };
		std::string prefix = prefixIn;
		std::transform(prefix.begin(), prefix.end(), prefix.begin(), ::tolower);
		std::replace(prefix.begin(), prefix.end(), '/', '\\');
		std::string base = "data\\texture\\" + prefix;
		std::vector<std::string> names;
		for (auto f : util::FileIO::listAllFiles())
		{
			std::transform(f.begin(), f.end(), f.begin(), ::tolower);
			std::replace(f.begin(), f.end(), '/', '\\');
			if (f.rfind(base, 0) != 0 || f.size() < 5)
				continue;
			bool ascii = true;
			for (unsigned char c : f)
				if (c > 127) ascii = false;
			auto ext = f.substr(f.size() - 4);
			if (ascii && (ext == ".bmp" || ext == ".jpg" || ext == ".tga" || ext == ".png"))
				names.push_back(f.substr(13));
		}
		std::sort(names.begin(), names.end());
		names.erase(std::unique(names.begin(), names.end()), names.end());
		if (names.empty())
		{
			textureSheetResult = "No textures found starting with '" + prefixIn + "'";
			return;
		}

		const int cell = 96, cols = 16, rows = 16, perSheet = cols * rows;
		std::filesystem::create_directories("sheets");
		std::string safe = prefix;
		std::replace(safe.begin(), safe.end(), '\\', '_');
		int sheetCount = ((int)names.size() + perSheet - 1) / perSheet;
		for (int sheet = 0; sheet < sheetCount; sheet++)
		{
			const int W = cols * cell, H = rows * cell;
			std::vector<unsigned char> img((size_t)W * H * 3, 60);
			nlohmann::json list = nlohmann::json::array();
			for (int i = 0; i < perSheet && sheet * perSheet + i < (int)names.size(); i++)
			{
				const std::string& name = names[sheet * perSheet + i];
				list.push_back(name);
				int ox = (i % cols) * cell, oy = (i / cols) * cell;
				auto is = util::FileIO::open("data\\texture\\" + name);
				if (is)
				{
					std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(*is)), std::istreambuf_iterator<char>());
					delete is;
					int w, h, c;
					unsigned char* data = bytes.empty() ? nullptr : stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &c, 3);
					if (data)
					{
						for (int y = 0; y < cell - 2; y++)
							for (int x = 0; x < cell - 2; x++)
							{
								const unsigned char* src = data + ((size_t)(y * h / (cell - 2)) * w + (x * w / (cell - 2))) * 3;
								unsigned char* dst = &img[((size_t)(oy + y + 1) * W + ox + x + 1) * 3];
								dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
							}
						stbi_image_free(data);
					}
				}
				// index number, white digits on black
				std::string num = std::to_string(i);
				int nx = ox + 3, ny = oy + 3;
				for (int px = -1; px < (int)num.size() * 8; px++)
					for (int py = -1; py < 11; py++)
					{
						unsigned char* d = &img[((size_t)(ny + py) * W + nx + px) * 3];
						d[0] = d[1] = d[2] = 0;
					}
				for (size_t k = 0; k < num.size(); k++)
					for (int py = 0; py < 5; py++)
						for (int px = 0; px < 3; px++)
							if (digits[num[k] - '0'][py * 3 + px] == '1')
								for (int sy = 0; sy < 2; sy++)
									for (int sx = 0; sx < 2; sx++)
									{
										unsigned char* d = &img[((size_t)(ny + py * 2 + sy) * W + nx + (int)k * 8 + px * 2 + sx) * 3];
										d[0] = d[1] = d[2] = 255;
									}
			}
			std::string stem = "sheets\\" + safe + "_" + std::to_string(sheet);
			stbi_write_png((stem + ".png").c_str(), W, H, 3, img.data(), W * 3);
			std::ofstream out(stem + ".json");
			out << list;
		}
		textureSheetResult = "Wrote " + std::to_string(sheetCount) + " sheet(s) for " + std::to_string(names.size()) + " textures to the 'sheets' folder next to BrowEdit3.exe";
	}
}

void BrowEdit::menuBar()
{
	ImGui::BeginMainMenuBar();
	if (ImGui::BeginMenu("File"))
	{
		hotkeyMenuItem("New", HotkeyAction::Global_New);
		hotkeyMenuItem("Open", HotkeyAction::Global_Load);
		if(activeMapView)
			hotkeyMenuItem("Save " + activeMapView->map->name, HotkeyAction::Global_Save);
		if (activeMapView && ImGui::MenuItem("Save as "))
			saveAsMap(activeMapView->map);
		if (activeMapView && ImGui::MenuItem("Export to folder"))
			exportMap(activeMapView->map);
		if (activeMapView && ImGui::MenuItem("Model variants (recolour copies)..."))
			modelvariants::visible = true;
		if (ImGui::BeginMenu("Recent Maps", config.recentFiles.size() > 0))
		{
			for (const auto& m : config.recentFiles)
				if (ImGui::MenuItem(m.c_str()))
				{
					loadMap(m);
					break;
				}
			ImGui::EndMenu();
		}
		if (activeMapView && ImGui::MenuItem("Export mapdata for lightmapping"))
		{
			nlohmann::json mapData;
			auto rsw = activeMapView->map->rootNode->getComponent<Rsw>();
			auto gnd = activeMapView->map->rootNode->getComponent<Gnd>();

			mapData["lights"] = nlohmann::json::array();
			activeMapView->map->rootNode->traverse([&](Node* n) {
				auto light = n->getComponent<RswLight>();
				auto object = n->getComponent<RswObject>();
				if (light && object)
				{
					nlohmann::json l;
					to_json(l, *light);
					to_json(l, *object);
					mapData["lights"].push_back(l);
				}
			});

			mapData["floorTiles"] = gnd->getMapQuads();

			mapData["modelPolygons"] = nlohmann::json::array();
			activeMapView->map->rootNode->traverse([&](Node* n) {
				auto rsm = n->getComponent<Rsm>();
				auto rswModel = n->getComponent<RswModel>();
				auto collider = n->getComponent<RswModelCollider>();
				if (rsm && rswModel)
				{
					auto verts = collider->getVerticesWorldSpace();
					for (const auto& v : verts)
						mapData["modelPolygons"].push_back(v);
				}
				});


			std::ofstream mapDataFile("mapdata.json");
			mapDataFile << std::setw(2) << mapData;
		}
		ImGui::MenuItem("Claude live link (claude_link folder)", nullptr, &claudeLinkEnabled);
		if (ImGui::MenuItem("Texture sheets for Claude..."))
			openTextureSheetPopup = true;
		if (ImGui::MenuItem("Export asset catalog (for Claude)"))
		{
			// lists the models and textures available in the client, so a map generator knows what it can use
			nlohmann::json catalog;
			catalog["models"] = nlohmann::json::array();
			catalog["textures"] = nlohmann::json::array();
			std::set<std::string> models, textures;
			auto isAscii = [](const std::string& f) { for (unsigned char c : f) if (c > 127) return false; return true; };
			for (auto f : util::FileIO::listAllFiles())
			{
				std::transform(f.begin(), f.end(), f.begin(), ::tolower);
				std::replace(f.begin(), f.end(), '/', '\\');
				if (!isAscii(f))
					continue;
				if (f.rfind("data\\model\\", 0) == 0 && f.size() > 4 && f.substr(f.size() - 4) == ".rsm")
					models.insert(f.substr(11));
				else if (f.rfind("data\\texture\\", 0) == 0 && f.size() > 4)
				{
					auto ext = f.substr(f.size() - 4);
					if (ext == ".bmp" || ext == ".jpg" || ext == ".tga" || ext == ".png")
						textures.insert(f.substr(13));
				}
			}
			for (const auto& m : models) catalog["models"].push_back(m);
			for (const auto& t : textures) catalog["textures"].push_back(t);
			std::ofstream catalogFile("catalog.json");
			catalogFile << catalog;
		}
		hotkeyMenuItem("Open Model Editor", HotkeyAction::Global_ModelEditor_Open);
		hotkeyMenuItem("Quit", HotkeyAction::Global_Exit);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Edit"))
	{
		hotkeyMenuItem("Undo", HotkeyAction::Global_Undo);
		hotkeyMenuItem("Redo", HotkeyAction::Global_Redo);

		if (editMode == EditMode::Object)
		{
			hotkeyMenuItem("Copy", HotkeyAction::Global_Copy);
			hotkeyMenuItem("Paste", HotkeyAction::Global_Paste);
		}

		if (ImGui::MenuItem("Toggle Undo Window", nullptr, windowData.undoVisible))
			windowData.undoVisible = !windowData.undoVisible;
		hotkeyMenuItem("Global Settings", HotkeyAction::Global_Settings);

		if (ImGui::MenuItem("Hotkey Settings"))
		{
			windowData.hotkeyEditWindowVisible = true;
			windowData.hotkeys = config.hotkeys;
		}			
		if (ImGui::MenuItem("Demo Window", nullptr, windowData.demoWindowVisible))
			windowData.demoWindowVisible = !windowData.demoWindowVisible;
		hotkeyMenuItem("Reload Textures", HotkeyAction::Global_ReloadTextures);
		hotkeyMenuItem("Reload Models", HotkeyAction::Global_ReloadModels);
		ImGui::EndMenu();
	}
	if (editMode == EditMode::Object && activeMapView && ImGui::BeginMenu("Object Selection"))
	{
		if (ImGui::BeginMenu("Grow/shrink Selection"))
		{
			if (ImGui::MenuItem("Select same models"))
				activeMapView->map->selectSameModels(this);
			static float nearDistance = 50;
			ImGui::SetNextItemWidth(100.0f);
			ImGui::DragFloat("Near Distance", &nearDistance, 1.0f, 0.0f, 1000.0f);
			if (ImGui::MenuItem("Select objects near"))
				activeMapView->map->selectNear(nearDistance, this);
			if (ImGui::BeginMenu("Select all"))
			{
				hotkeyMenuItem("Select all", HotkeyAction::ObjectEdit_SelectAll);
				hotkeyMenuItem("Select all models", HotkeyAction::ObjectEdit_SelectAllModels);
				hotkeyMenuItem("Select all effects", HotkeyAction::ObjectEdit_SelectAllEffects);
				hotkeyMenuItem("Select all sounds", HotkeyAction::ObjectEdit_SelectAllSounds);
				hotkeyMenuItem("Select all lights", HotkeyAction::ObjectEdit_SelectAllLights);
				ImGui::EndMenu();
			}
			hotkeyMenuItem("Invert selection", HotkeyAction::ObjectEdit_InvertSelection);
			ImGui::EndMenu();
		}
		hotkeyMenuItem("Flip horizontally", HotkeyAction::ObjectEdit_FlipHorizontal);
		hotkeyMenuItem("Flip vertically", HotkeyAction::ObjectEdit_FlipVertical);

		hotkeyMenuItem("Invert Scale X", HotkeyAction::ObjectEdit_InvertScaleX);
		hotkeyMenuItem("Invert Scale Y", HotkeyAction::ObjectEdit_InvertScaleY);
		hotkeyMenuItem("Invert Scale Z", HotkeyAction::ObjectEdit_InvertScaleZ);

		hotkeyMenuItem("Create Prefab", HotkeyAction::ObjectEdit_CreatePrefab);
		hotkeyMenuItem("Delete", HotkeyAction::ObjectEdit_Delete);
		hotkeyMenuItem("Focus on selection", HotkeyAction::ObjectEdit_FocusOnSelection);
		if (ImGui::MenuItem("Set to floor height") && activeMapView)
			activeMapView->map->setSelectedItemsToFloorHeight(this);

		ImGui::EndMenu();
	}
	if (editMode == EditMode::Object && activeMapView && ImGui::BeginMenu("Object Edit"))
	{
		if (ImGui::MenuItem("Add new lub effect")) {
			auto e = new RswEffect();
			e->id = RswEffect::EffectType::Emitter;
			e->loop = 1.0f;
			Node* newNode = new Node("effect");
			newNode->addComponent(new RswObject());
			newNode->addComponent(e);
			newNode->addComponent(new BillboardRenderer("data\\effect.png", "data\\effect_selected.png"));
			newNode->addComponent(new CubeCollider(5));

			e->setEffectNode(nullptr, newNode);
			newNodes.push_back(std::pair<Node*, glm::vec3>(newNode, glm::vec3(0, 0, 0)));
			newNodesCenter = glm::vec3(0, 0, 0);
			newNodePlacement = BrowEdit::Ground;
		}

		if (ImGui::MenuItem("Add new wind effect")) {
			auto e = new RswEffect();
			e->id = RswEffect::EffectType::WindEffect;
			e->loop = 1.0f;
			Node* newNode = new Node("effect");
			newNode->addComponent(new RswObject());
			newNode->addComponent(e);
			newNode->addComponent(new BillboardRenderer("data\\effect.png", "data\\effect_selected.png"));
			newNode->addComponent(new CubeCollider(5));
			e->setEffectNode(nullptr, newNode);

			newNodes.push_back(std::pair<Node*, glm::vec3>(newNode, glm::vec3(0, 0, 0)));
			newNodesCenter = glm::vec3(0, 0, 0);
			newNodePlacement = BrowEdit::Ground;
		}

		if (ImGui::MenuItem("Add new str effect")) {
			auto e = new RswEffect();
			e->id = RswEffect::EffectType::Ez2Str;
			e->loop = 1.0f;
			Node* newNode = new Node("effect");
			newNode->addComponent(new RswObject());
			newNode->addComponent(e);
			newNode->addComponent(new BillboardRenderer("data\\effect.png", "data\\effect_selected.png"));
			newNode->addComponent(new CubeCollider(5));
			e->setEffectNode(nullptr, newNode);

			newNodes.push_back(std::pair<Node*, glm::vec3>(newNode, glm::vec3(0, 0, 0)));
			newNodesCenter = glm::vec3(0, 0, 0);
			newNodePlacement = BrowEdit::Ground;
		}

		if (ImGui::MenuItem("Add new animated effect")) {
			auto e = new RswEffect();
			e->id = RswEffect::EffectType::AnimatedEmitter;
			e->loop = 1.0f;
			Node* newNode = new Node("effect");
			newNode->addComponent(new RswObject());
			newNode->addComponent(e);
			newNode->addComponent(new BillboardRenderer("data\\effect.png", "data\\effect_selected.png"));
			newNode->addComponent(new CubeCollider(5));

			e->setEffectNode(nullptr, newNode);
			auto lubEffect = newNode->getComponent<LubEffect>();
			lubEffect->dir1 = glm::vec3(-3, -1, -3);
			lubEffect->dir2 = glm::vec3(3, 1, 3);
			lubEffect->gravity = glm::vec3(0);
			lubEffect->rate = glm::vec2(1, 3);
			lubEffect->speed = 1.0f;
			lubEffect->srcmode = 5;
			lubEffect->destmode = 2;
			lubEffect->maxcount = 4;

			newNodes.push_back(std::pair<Node*, glm::vec3>(newNode, glm::vec3(0, 0, 0)));
			newNodesCenter = glm::vec3(0, 0, 0);
			newNodePlacement = BrowEdit::Ground;
		}

		if (ImGui::MenuItem("Add evils paw effect")) {
			auto e = new RswEffect();
			e->id = RswEffect::EffectType::EvilsPaw;
			e->loop = 1.0f;
			Node* newNode = new Node("effect");
			newNode->addComponent(new RswObject());
			newNode->addComponent(e);
			newNode->addComponent(new BillboardRenderer("data\\effect.png", "data\\effect_selected.png"));
			newNode->addComponent(new CubeCollider(5));

			e->setEffectNode(nullptr, newNode);

			newNodes.push_back(std::pair<Node*, glm::vec3>(newNode, glm::vec3(0, 0, 0)));
			newNodesCenter = glm::vec3(0, 0, 0);
			newNodePlacement = BrowEdit::Ground;
		}

		if (ImGui::MenuItem("Add new light")) {
			auto l = new RswLight();
			l->range = 60.0f;
			l->color = glm::vec3(0.8f, 0.8f, 0.8f);
			Node* newNode = new Node("light");
			newNode->addComponent(new RswObject());
			newNode->addComponent(l);
			newNode->addComponent(new BillboardRenderer("data\\light.png", "data\\light_selected.png"));
			newNode->addComponent(new CubeCollider(5));
			newNodes.push_back(std::pair<Node*, glm::vec3>(newNode, glm::vec3(0, 0, 0)));
			newNodesCenter = glm::vec3(0, -35, 0);
			newNodePlacement = BrowEdit::Relative;
		}

		if (ImGui::MenuItem("Add new sun")) {
			auto l = new RswLight();
			l->range = 0.0f;
			l->intensity = 0.5f;
			l->givesShadow = true;
			l->affectShadowMap = true;
			l->sunMatchRswDirection = true;
			l->diffuseLighting = false;
			l->lightType = RswLight::Type::Sun;
			Node* newNode = new Node("sun");
			newNode->addComponent(new RswObject());
			newNode->addComponent(l);
			newNode->addComponent(new BillboardRenderer("data\\light.png", "data\\light_selected.png"));
			newNode->addComponent(new CubeCollider(5));
			newNodes.push_back(std::pair<Node*, glm::vec3>(newNode, glm::vec3(0, 0, 0)));
			newNodesCenter = glm::vec3(0, 0, 0);
			newNodePlacement = BrowEdit::Ground;
		}

		if (ImGui::MenuItem("Select all lights")) {
			if (activeMapView)
				activeMapView->map->selectAll<RswLight>(this);
		}

		if (ImGui::MenuItem("Smart GAT: recalculate all models"))
		{
			// blocks the exact ground footprint of every model in the map, using the Smart model GAT settings
			auto ga = new GroupAction("Smart GAT for all models");
			std::vector<Node*> models;
			activeMapView->map->rootNode->traverse([&](Node* n) {
				if (n->getComponent<RswModel>())
					models.push_back(n);
			});
			activeMapView->map->addAutoGatActions(models, autoGatBandHeight, autoGatType, ga, false, false, autoGatFillInterior, autoGatCloseGap);
			activeMapView->map->doAction(ga, this);
		}


		ImGui::EndMenu();
	}

	if(activeMapView)
	if (ImGui::BeginMenu(activeMapView->map->name.c_str()))
	{
		if (ImGui::MenuItem("Open new view"))
			loadMap(activeMapView->map->name);
		if (ImGui::BeginMenu("Import/Export maps"))
		{
			static bool exportWalls = true;
			static bool exportBorders = true;
			ImGui::PushItemFlag(ImGuiItemFlags_SelectableDontClosePopup, true);
			ImGui::MenuItem("Import/Export Walls", nullptr, &exportWalls);
			ImGui::MenuItem("Import/Export Borders", nullptr, &exportBorders);
			ImGui::PopItemFlag();
			ImGui::Separator();
			if (ImGui::MenuItem("Export shadowmap"))
				activeMapView->map->exportShadowMap(this, exportWalls, exportBorders);
			if (ImGui::MenuItem("Export colormap"))
				activeMapView->map->exportLightMap(this, exportWalls, exportBorders);
			if (ImGui::MenuItem("Export tile colors"))
				activeMapView->map->exportTileColors(this, exportWalls);
			if (ImGui::MenuItem("Export minimap"))
				activeMapView->map->exportMinimapWalkable(this);
			if (ImGui::MenuItem("Generate minimap (rendered)..."))
				openMinimapPopup = true;
			ImGui::Separator();
			if (ImGui::MenuItem("Import shadowmap"))
				activeMapView->map->importShadowMap(this, exportWalls, exportBorders);
			if (ImGui::MenuItem("Import colormap"))
				activeMapView->map->importLightMap(this, exportWalls, exportBorders);
			if (ImGui::MenuItem("Import tile colors"))
				activeMapView->map->importTileColors(this, exportWalls);


			ImGui::EndMenu();
		}
		hotkeyMenuItem("Calculate Quadtree", HotkeyAction::Global_CalculateQuadtree);
		hotkeyMenuItem("Calculate lightmaps", HotkeyAction::Global_CalculateLightmaps);
		ImGui::MenuItem("Live lightmap (re-bake on edits)", nullptr, &liveLightmap);
			if (liveLightmap) { ImGui::SetNextItemWidth(120); ImGui::SliderFloat("Live delay (s)", &liveLightmapDelay, 0.0f, 1.0f, "%.2f"); }
		if (ImGui::MenuItem("Make tiles unique"))
			activeMapView->map->rootNode->getComponent<Gnd>()->makeTilesUnique();
		if (ImGui::MenuItem("Clean unused tiles"))
			activeMapView->map->rootNode->getComponent<Gnd>()->cleanTiles(false);
		hotkeyMenuItem("Remove zero height walls", HotkeyAction::Global_ClearZeroHeightWalls);
		if (ImGui::MenuItem("Remove unused ground textures"))
			activeMapView->map->rootNode->getComponent<Gnd>()->removeUnusedTextures(this);
		if (ImGui::MenuItem("Clean Tiles"))
			activeMapView->map->rootNode->getComponent<Gnd>()->cleanTiles();
		if (ImGui::MenuItem("Edit lighting/water/fog")) {
			editMode = EditMode::Object;
			activeMapView->map->doAction(new SelectAction(activeMapView->map, activeMapView->map->rootNode, false, false), this);
		}
		if (ImGui::MenuItem("Fix lightmap borders"))
			activeMapView->map->rootNode->getComponent<Gnd>()->makeLightmapBorders(this);
		if (ImGui::MenuItem("Clear lightmaps"))
			activeMapView->map->rootNode->getComponent<Gnd>()->makeLightmapsClear();
		if (ImGui::MenuItem("Smoothen lightmaps"))
			activeMapView->map->rootNode->getComponent<Gnd>()->makeLightmapsSmooth(this);
		if (ImGui::MenuItem("Make lightmaps unique"))
			activeMapView->map->rootNode->getComponent<Gnd>()->makeLightmapsUnique();
		if (ImGui::MenuItem("Clean up lightmaps"))
			activeMapView->map->rootNode->getComponent<Gnd>()->cleanLightmaps();
		static int s[2] = { activeMapView->map->rootNode->getComponent<Gnd>()->lightmapWidth, activeMapView->map->rootNode->getComponent<Gnd>()->lightmapHeight };
		ImGui::InputInt2("##lightmapRes", s);
		ImGui::SameLine();
		if (ImGui::MenuItem("Change Lightmap Resolution"))
			activeMapView->map->rootNode->getComponent<Gnd>()->makeLightmapsDiffRes(s[0], s[1]);

		if (activeMapView->map->name == "data\\effects_ro.rsw" && ImGui::MenuItem("Generate effects")) //speedrun map
		{
			auto effectsFile = util::FileIO::open("data\\EffectTable.json");
			int braces = 0;
			bool singleQuote = false;
			bool doubleQuote = false;
			int comment = 0;
			int mlComment = 0;
			std::string buffer = "";
			std::set<int> ids;
			while (!effectsFile->eof())
			{
				char c = effectsFile->get();
				if (c == '{' && !singleQuote && !doubleQuote)
					braces++;
				else if (c == '}' && !singleQuote && !doubleQuote)
					braces--;
				else if (c == '[' && !singleQuote && !doubleQuote)
					braces++;
				else if (c == ']' && !singleQuote && !doubleQuote)
					braces--;
				else if (c == '\'')
					singleQuote = !singleQuote;
				else if (c == '\"')
					doubleQuote = !doubleQuote;
				else if (c == ',')
					buffer = "";
				else if (c == '/' && mlComment == 2)
					mlComment = 0;
				else if (c == '/')
					comment++;
				else if (c == '*' && mlComment == 1)
					mlComment = 2;
				else if (c == '*' && comment == 1)
					mlComment = 1;
				else if (c == '\t')
					;
				else if (c == ' ')
					;
				else if (c == '\r')
					;
				else if (c == '\n')
				{
					comment = 0;
					doubleQuote = false;
					singleQuote = false;
				}
				else if (c == ':')
				{
					if (buffer != "")
					{
						try
						{
							int id = std::stoi(buffer);
							if (id != 0)
								ids.insert(id);
						}
						catch (...) {}
						buffer = "";
					}
				}
				else if (braces == 1 && comment == 0 && mlComment == 0)
					buffer += c;
			}
			delete effectsFile;

			activeMapView->map->rootNode->children.clear(); //oops
			auto gnd = activeMapView->map->rootNode->getComponent<Gnd>();

			int tU[6][6];
			int tF[6][6];
			int tS[6][6];
			int digits[10];

			int xx = 5 + 6 * (0 % 40) - 1;
			int yy = 17 + 6 * (0 / 40) - 1;
			for (int x = xx; x < xx + 6; x++)
			{
				for (int y = yy; y < yy + 6; y++)
				{
					tU[x - xx][y - yy] = gnd->cubes[x][gnd->height - 1 - y]->tileUp;
					tF[x - xx][y - yy] = gnd->cubes[x][gnd->height - 1 - y]->tileFront;
					tS[x - xx][y - yy] = gnd->cubes[x][gnd->height - 1 - y]->tileSide;
				}
			}
			for (int x = 0; x < 10; x++)
			{
				xx = 5 + 6 * (x % 40) - 1;
				yy = 17 + 6 * (0 / 40) - 1;
				digits[x] = gnd->cubes[xx + 1][gnd->height - 1 - (yy + 5)]->tileUp;
			}

			for (int x = 0; x < gnd->width; x++)
			{
				for (int y = 0; y < gnd->height-15; y++)
				{
					gnd->cubes[x][y]->h1 = -30;
					gnd->cubes[x][y]->h2 = -30;
					gnd->cubes[x][y]->h3 = -30;
					gnd->cubes[x][y]->h4 = -30;
					activeMapView->map->rootNode->getComponent<GndRenderer>()->setChunkDirty(x, y);
				}
			}
			for (int i = 0; i < 1000; i++)
			{
				bool enabled = std::find(ids.begin(), ids.end(), i) != ids.end();

				int xx = 5+  6 * (i % 40)-1;
				int yy = 17+ 6 * (i / 40)-1;
				for (int x = xx; x < xx + 6; x++)
				{
					for (int y = yy; y < yy + 6; y++)
					{
						float h = -40;
						if (x == xx || x == xx + 5 || y == yy || y == yy + 5)
							h = -30;
						gnd->cubes[x][gnd->height - 1 - y]->h1 = h;
						gnd->cubes[x][gnd->height - 1 - y]->h2 = h;
						gnd->cubes[x][gnd->height - 1 - y]->h3 = h;
						gnd->cubes[x][gnd->height - 1 - y]->h4 = h;
						gnd->cubes[x][gnd->height - 1 - y]->tileFront = tF[x-xx][y-yy];
						gnd->cubes[x][gnd->height - 1 - y]->tileSide = tS[x - xx][y - yy];
						gnd->cubes[x][gnd->height - 1 - y]->tileUp = tU[x - xx][y - yy];
					}
				}
				if (enabled)
				{
					int nr = i;
					int len = (int)floor(log10(nr));
					while (nr > 0)
					{
						gnd->cubes[xx + 1 + len][gnd->height - 1 - (yy + 5)]->tileUp = digits[nr % 10];
						nr /= 10;
						len--;
					}

					auto e = new RswEffect();
					auto o = new RswObject();
					o->position.x = -1180.0f + 60 * (i % 40);
					o->position.y = -48.0f;
					o->position.z = 1060.0f - 60 * (i / 40);
					e->id = i;
					Node* newNode = new Node("Effect" + std::to_string(i));
					newNode->addComponent(o);
					newNode->addComponent(e);
					newNode->addComponent(new BillboardRenderer("data\\effect.png", "data\\effect_selected.png"));
					newNode->addComponent(new CubeCollider(5));
					newNode->setParent(activeMapView->map->rootNode);
				}
			}

		}


		for (auto mv : mapViews)
		{
			if (mv.map == activeMapView->map)
			{
				if (ImGui::BeginMenu(mv.viewName.c_str()))
				{
					if (ImGui::BeginMenu("High Res Screenshot"))
					{
						static int size[2] = { 2048, 2048 };
						ImGui::DragInt2("Size", size);
						if (ImGui::MenuItem("Save"))
						{
							std::string filename = util::SaveAsDialog("screenshot.png", "All\0*.*\0Png\0*.png");
							if (filename != "")
							{
								mv.fbo->resize(size[0], size[1]);
								mv.render(this);
								mv.fbo->saveAsFile(filename);
							}
						}
						ImGui::EndMenu();
					}
					ImGui::EndMenu();
				}
			}
		}
		ImGui::EndMenu();
	}

	if (ImGui::MenuItem("Help"))
		windowData.helpWindowVisible = true;

	if (openMinimapPopup)
	{
		ImGui::OpenPopup("Generate minimap");
		openMinimapPopup = false;
		minimapResult.clear();
	}
	if (ImGui::BeginPopupModal("Generate minimap", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Top-down picture of the map, saved as <map>.bmp in data\\texture\\(user interface)\\map");
		ImGui::SetNextItemWidth(100);
		ImGui::InputInt("Size (pixels, square)", &minimapSize, 64, 128);
		minimapSize = std::clamp(minimapSize, 64, 4096);
		ImGui::SameLine(); if (ImGui::SmallButton("128")) minimapSize = 128;
		ImGui::SameLine(); if (ImGui::SmallButton("256")) minimapSize = 256;
		ImGui::SameLine(); if (ImGui::SmallButton("512")) minimapSize = 512;
		ImGui::Checkbox("Shadows and light colours", &minimapShading);
		ImGui::TextWrapped("Rendered from straight above with the ground, models, water, shadows and colours; the area outside the map is magenta (the client's transparent colour). This replaces the walkable-area minimap if you exported that earlier.");
		if (ImGui::Button("Generate"))
			minimapResult = activeMapView->renderMinimap(this, minimapSize, minimapShading, "");
		ImGui::SameLine();
		if (ImGui::Button("Close"))
			ImGui::CloseCurrentPopup();
		if (!minimapResult.empty())
			ImGui::TextWrapped("%s", minimapResult.c_str());
		ImGui::EndPopup();
	}
	if (openTextureSheetPopup)
	{
		ImGui::OpenPopup("Texture sheets");
		openTextureSheetPopup = false;
		textureSheetResult.clear();
	}
	if (ImGui::BeginPopupModal("Texture sheets", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Folder below data\\texture (for example prontera_re, roo, rockridge):");
		ImGui::InputText("##prefix", textureSheetPrefix, sizeof(textureSheetPrefix));
		if (ImGui::Button("Export"))
			exportTextureSheets(textureSheetPrefix);
		ImGui::SameLine();
		if (ImGui::Button("Close"))
			ImGui::CloseCurrentPopup();
		if (!textureSheetResult.empty())
			ImGui::TextWrapped("%s", textureSheetResult.c_str());
		ImGui::EndPopup();
	}
	ImGui::EndMainMenuBar();


}

