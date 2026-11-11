#pragma once
#include "SpriteSystem.h"
#include <browedit/gl/Shader.h>
#include <array>

namespace sprite
{
	struct Opt { int id; std::string label; bool ok = true; };

	struct System::Impl
	{
		// ---- gl
		unsigned int whiteTex = 0;
		gl::Shader* shader = nullptr;

		// ---- asset state
		int scanVersion = 0;
		int optsVersion = -1;
		std::vector<Opt> mobOpts[3];
		std::vector<Opt> jobOpts, headgearOpts[2], robeOpts[2], weaponOpts[2];
		std::map<Map*, MapLayer> layers;

		// ---- preview
		Instance previewInst;
		double previewTime = 0, lastTime = 0;
		bool playing = true;
		float zoom = 3.0f;
		int background = 1;			// 0 transparent, 1 checker, 2 solid
		bool grid = false, previewShadow = true, eightDirs = false, showUnavailable = false;
		std::string warnKey;
		std::vector<std::string> warnings;
		std::string notice;
		int dragAccum = 0;
		std::vector<int> actionsAvail;
		std::map<int, std::shared_ptr<Frame>> thumbs;
		std::string renameBuf;
		MapLayer pathSnap;
		bool pathPending = false;
		std::map<std::string, std::shared_ptr<Frame>> mobThumbs;
		std::string browserFilter;
		int browserTab = 0;
		float thumbSize = 72;

		// ---- placing / editing
		std::vector<Instance> clipboard;
		bool ghostValid = false;
		glm::vec3 ghostWorld = glm::vec3(0);
		bool dragging = false, dragMoved = false;
		glm::vec3 dragStartWorld = glm::vec3(0);
		MapLayer dragBefore;
		std::map<int, glm::vec3> dragOrig;
		int editingId = 0;			// instance the panel was loaded from (0 = new)
		Map* lastMap = nullptr;

		// ---- presets
		bool presetsLoaded = false;
		std::string presetFilter, presetCategory = "All", presetName = "New preset";
		char presetCategoryNew[2] = {};
		int presetSelected = -1;
		std::string presetsPath = "data\\browedit_sprite_presets.json";

		// helpers
		unsigned int tex(Frame& f);
		void freeDropped(System& s);
		void ensureOpts(System& s);
		std::shared_ptr<Frame> frameFor(System& s, const Instance& in, int effDir, double time, AnimInfo* info = nullptr, int actionOverride = -1, float speedMul = 1.0f);
		void loadPresets(System& s);
		void savePresets(System& s);
		void applyJobChange(System& s, CharacterConfig& c, const std::string& newJob, int newGender);
	};

	// shared by SpriteSystem.cpp and SpriteWindow.cpp
	void pushLayerAction(Map* map, BrowEdit* be, MapLayer& layer, const MapLayer& before, const std::string& title);
	extern const char* categories[];
	extern const int categoryCount;
	void drawFrameQuad(gl::Shader* shader, unsigned int tex, const Frame& f, float ps, const glm::vec4& color);
}
