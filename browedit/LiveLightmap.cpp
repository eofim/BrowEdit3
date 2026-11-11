// Live lightmap baking: when enabled, the GPU lightmapper re-runs shortly after the terrain, models or lights change,
// so shadows and lighting follow the edits without pressing "Calculate lightmaps".  No undo entry is created for these bakes.
#include <Windows.h>
#include "BrowEdit.h"
#include "Map.h"
#include "Node.h"
#include "Image.h"
#include "LightmapperGpu.h"
#include <browedit/components/Gnd.h>
#include <browedit/components/GndRenderer.h>
#include <browedit/components/Rsw.h>
#include <browedit/util/ResourceManager.h>
#include <imgui.h>
#include <iostream>
#include <set>
#include <chrono>

namespace
{
	inline void mix(size_t& h, const void* p, size_t n)
	{
		auto b = (const unsigned char*)p;
		for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ULL; }
	}

	// Cheap fingerprint of everything that influences lightmaps: heights/tiles, models, lights, sun settings.
	size_t mapStamp(Map* map)
	{
		size_t h = 1469598103934665603ULL;
		auto gnd = map->rootNode->getComponent<Gnd>();
		auto rsw = map->rootNode->getComponent<Rsw>();
		if (!gnd || !rsw)
			return h;
		mix(h, &gnd->width, sizeof(int)); mix(h, &gnd->height, sizeof(int));
		for (int x = 0; x < gnd->width; x++)
			for (int y = 0; y < gnd->height; y++)
			{
				auto c = gnd->cubes[x][y];
				mix(h, c->heights, sizeof(c->heights));
				char has[3] = { c->tileIds[0] != -1, c->tileIds[1] != -1, c->tileIds[2] != -1 };	// only whether a surface exists: tile renumbering by the baker must not count as an edit
				mix(h, has, 3);
			}
		mix(h, &rsw->light.longitude, sizeof(int)); mix(h, &rsw->light.latitude, sizeof(int));
		mix(h, &rsw->light.diffuse, sizeof(glm::vec3)); mix(h, &rsw->light.ambient, sizeof(glm::vec3));
		mix(h, &rsw->light.intensity, sizeof(float)); mix(h, &rsw->light.lightmapAmbient, sizeof(float));
		map->rootNode->traverse([&](Node* n) {
			auto o = n->getComponent<RswObject>();
			if (!o)
				return;
			if (auto m = n->getComponent<RswModel>())
			{
				mix(h, &o->position, sizeof(glm::vec3)); mix(h, &o->rotation, sizeof(glm::vec3)); mix(h, &o->scale, sizeof(glm::vec3));
				mix(h, m->fileName.data(), m->fileName.size()); mix(h, &m->shadowStrength, sizeof(float));
			}
			else if (auto l = n->getComponent<RswLight>())
			{
				// every option shown in the light's property panel
				mix(h, &o->position, sizeof(glm::vec3)); mix(h, &o->rotation, sizeof(glm::vec3)); mix(h, &o->scale, sizeof(glm::vec3));
				mix(h, &l->color, sizeof(glm::vec3)); mix(h, &l->range, sizeof(float));
				mix(h, &l->enabled, sizeof(bool)); mix(h, &l->diffuseLighting, sizeof(bool)); mix(h, &l->lightType, sizeof(l->lightType));
				mix(h, &l->givesShadow, sizeof(bool)); mix(h, &l->affectShadowMap, sizeof(bool)); mix(h, &l->affectLightmap, sizeof(bool));
				mix(h, &l->shadowTerrain, sizeof(bool)); mix(h, &l->noTerrainShading, sizeof(bool));
				mix(h, &l->sunMatchRswDirection, sizeof(bool)); mix(h, &l->direction, sizeof(glm::vec3)); mix(h, &l->spotlightWidth, sizeof(float));
				mix(h, &l->falloffStyle, sizeof(l->falloffStyle)); mix(h, &l->cutOff, sizeof(float)); mix(h, &l->intensity, sizeof(float));
				mix(h, &l->minShadowDistance, sizeof(float));
				if (!l->falloff.empty()) mix(h, l->falloff.data(), l->falloff.size() * sizeof(glm::vec2));
			}
		});
		return h;
	}

	// Only tiles that cubes actually use matter (orphan tiles left behind by tile edits are ignored).
	bool lightmapsAreUnique(Gnd* gnd)
	{
		std::set<int> taken;
		for (int x = 0; x < gnd->width; x++)
			for (int y = 0; y < gnd->height; y++)
				for (int i = 0; i < 3; i++)
				{
					int id = gnd->cubes[x][y]->tileIds[i];
					if (id < 0)
						continue;
					int lm = gnd->tiles[id]->lightmapIndex;
					if (lm == -1 || !taken.insert(lm).second)
						return false;
				}
		return true;
	}
}


bool BrowEdit::liveBake(Map* map)
{
	auto gnd = map->rootNode->getComponent<Gnd>();
	auto rsw = map->rootNode->getComponent<Rsw>();
	if (!gnd || !rsw)
		return false;
	if (!LightmapperGpu::canRunGpu())
	{
		std::cerr << "Live lightmap needs the GPU lightmapper (OpenGL 4.3 + ARB_bindless_texture); turning it off." << std::endl;
		return false;
	}

	auto& st = rsw->lightmapSettings;
	auto rangeX = st.rangeX; auto rangeY = st.rangeY; bool sel = st.heightSelectionOnly; int quality = st.quality;
	st.rangeX = glm::ivec2(0, gnd->width); st.rangeY = glm::ivec2(0, gnd->height);
	st.heightSelectionOnly = false;
	st.quality = glm::max(1, glm::min(st.quality, liveLightmapQuality));
	int softSamples = st.softSamples;
	st.softSamples = glm::min(st.softSamples, 12);		// the live bake stays quick; the full lightmap uses all of them

	// Only touch the tile/lightmap structure when a tile has no lightmap of its own (new tiles after a terrain edit).
	if (!lightmapsAreUnique(gnd))
	{
		gnd->cleanTiles(true);
		gnd->makeLightmapsUnique(true);
	}

	{
		LightmapperGpu lGpu(map, this);
		lGpu.run();
	}
	// borders need two passes, same as the normal lightmapper
	gnd->makeLightmapBorders(this, true);
	gnd->makeLightmapBorders(this, true);

	auto gr = map->rootNode->getComponent<GndRenderer>();
	gr->gndShadowDirty = true;
	gr->setChunksDirty();
	for (auto& cc : gr->chunks)
		for (auto c : cc)
			c->dirty = true;
	util::ResourceManager<Image>::clear();

	st.rangeX = rangeX; st.rangeY = rangeY; st.heightSelectionOnly = sel; st.quality = quality; st.softSamples = softSamples;
	return true;
}

void BrowEdit::liveLightmapUpdate()
{
	auto& s = liveLightmapState;
	if (!liveLightmap || !activeMapView || !activeMapView->map || lightmapper || windowData.progressWindowVisible)
	{
		s.wasOn = false; s.map = nullptr;
		return;
	}
	Map* map = activeMapView->map;
	double now = ImGui::GetTime();
	if (!s.wasOn || map != s.map)
	{
		// just switched on (or switched map): bake once so the lightmaps match the current state
		s.wasOn = true; s.map = map;
		s.lastStamp = mapStamp(map); s.lastChange = now - 100; s.pending = true; s.lastCheck = now;
	}
	else if (now - s.lastCheck >= 0.03)
	{
		s.lastCheck = now;
		size_t st = mapStamp(map);
		if (st != s.lastStamp) { s.lastStamp = st; s.lastChange = now; s.pending = true; }
	}
	if (!s.pending || now - s.lastChange < liveLightmapDelay)
		return;
	// adaptive throttle: never spend more than ~1/3 of the time baking, whatever the map size
	if (now - s.lastBakeEnd < s.lastBakeTime)
		return;
	if (s.lastBakeTime > 0.25 && ImGui::IsMouseDown(ImGuiMouseButton_Left) && now - s.lastChange < 100)
		return; // wait for the end of a drag
	s.pending = false;
	s.baking = true;
	auto t0 = std::chrono::steady_clock::now();
	if (!liveBake(map))
		liveLightmap = false;
	s.baking = false;
	s.lastBakeTime = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	s.lastBakeEnd = ImGui::GetTime();
	s.lastStamp = mapStamp(map);	// the bake itself may renumber tiles; never treat that as a new edit (would loop forever)
	std::cout << "Live lightmap: baked in " << s.lastBakeTime << "s" << std::endl;
}

void BrowEdit::liveLightmapFlush(Map* map)
{
	auto& s = liveLightmapState;
	if (!liveLightmap || !map || lightmapper)
		return;
	// compare against the last stamp that was actually baked, so a save right after an edit still gets fresh lightmaps
	size_t st = mapStamp(map);
	if (s.map == map && !s.pending && st == s.lastStamp)
		return;
	if (liveBake(map))
	{
		s.map = map; s.lastStamp = mapStamp(map); s.pending = false;
	}
	else
		liveLightmap = false;
}
