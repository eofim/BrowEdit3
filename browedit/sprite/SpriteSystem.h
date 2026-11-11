#pragma once
#include "Character.h"
#include <glm/glm.hpp>
#include <set>
#include <map>
#include <memory>
#include <thread>

class Map;
class MapView;
class BrowEdit;

namespace sprite
{
	// Editor-only character placed on a map. Lives in the BrowEdit sidecar file (<map>.browsprites.json), never in the RSW.
	struct Instance
	{
		int id = 0;
		std::string name = "Sprite";
		CharacterConfig cfg;
		int action = 0, dir = 0;
		float speed = 1.0f, scale = 1.0f;
		glm::vec3 pos = glm::vec3(0);	// RSW style coordinates (same as object positions)
		float elevation = 0;
		bool visible = true, locked = false;
		// movement (editor preview): the sprite walks from its position through the waypoints (RSW style coordinates)
		std::vector<glm::vec3> path;
		bool pathEnabled = true;
		int pathMode = 1;			// 0 = once, 1 = loop, 2 = ping-pong
		float moveSpeed = 4.0f;		// cells per second (the client walks about 6.7)
		float waitTime = 1.0f;		// seconds standing at each waypoint
		float startDelay = 0.0f;
		bool faceMove = true, syncAnim = true;
		int walkAction = 1;
		float wanderRadius = 0.0f;	// cells; > 0 and no waypoints: wanders around its position
		// runtime cache of the rendered frames (not saved)
		mutable CharacterConfig rtCfg; mutable std::string rtKey; mutable int rtAction = -1, rtDir = -1; mutable AnimInfo rtInfo; mutable bool rtValid = false;
		mutable std::vector<std::shared_ptr<Frame>> rtFrames;
		nlohmann::json toJson(const Db& db) const;
		static Instance fromJson(const nlohmann::json& j);
	};

	struct MapLayer
	{
		std::vector<Instance> items;
		int nextId = 1;
		std::set<int> selected;
		std::string path;
		bool loaded = false;
		Instance* find(int id) { for (auto& i : items) if (i.id == id) return &i; return nullptr; }
	};

	struct Preset
	{
		std::string name, category = "Custom";
		CharacterConfig cfg;
		int action = 0, dir = 0;
		float scale = 1.0f;
	};

	// What the Sprite Editor panel is editing (not yet on the map).
	struct Draft
	{
		CharacterConfig cfg;
		std::string name = "Sprite";
		int action = 0, dir = 0;
		float speed = 1.0f, scale = 1.0f;
		glm::vec3 pos = glm::vec3(0);
		float elevation = 0;
		bool visible = true, locked = false;
	};

	class System : public AssetSource
	{
	public:
		static System& instance();
		System();
		~System();

		// AssetSource (util::FileIO: GRFs and data folders, in their precedence order)
		bool read(const std::string& path, std::vector<uint8_t>& out) override;
		void listAll(std::vector<std::string>& paths) override;

		Db db;
		Composer composer;
		Draft draft;
		bool placing = false;			// ADD TO MAP pressed: the ghost follows the cursor
		bool showShadow = true;
		bool previewMovement = true, showPaths = true;
		double pathEpoch = 0;
		bool browserDrag = false, browserDrop = false;	// a sprite is being dragged out of the Sprite Browser / was just dropped
		bool showBrowser = true;
		void drawBrowser(BrowEdit* browEdit);
		bool snapToGat = true;			// stand on the GAT height (else on the ground mesh)
		bool showList = true;
		std::vector<Preset> presets;

		void startScan(BrowEdit* browEdit);
		void ensureReady(BrowEdit* browEdit);
		MapLayer& layer(Map* map, BrowEdit* browEdit);
		void save(Map* map, BrowEdit* browEdit);
		void forget(Map* map);

		// per frame hooks
		void showWindows(BrowEdit* browEdit);					// Sprite Editor + Sprite Object List
		void render(MapView* mapView, BrowEdit* browEdit);		// billboards in the 3d view (every edit mode)
		void postRender(MapView* mapView, BrowEdit* browEdit);	// interaction (sprite edit mode)

		struct Impl;
		std::unique_ptr<Impl> impl;
	private:
		std::thread scanThread;
		bool scanStarted = false;
	};
}
