#pragma once
// Advanced Texture edit mode: non-destructive overlay painting that is baked into stable 8-bit BMP atlases.
// The GND only ever sees normal texture paths and UVs; the paint layers live in <map>.browtex.json.
#include <string>
#include <vector>
#include <map>
#include <set>
#include <cstdint>

class Map;
class Gnd;
class MapView;
class BrowEdit;
class Action;

namespace gl { class Texture; }

namespace advtex
{
	constexpr int MASK = 32;			// paint mask resolution per ground cell
	constexpr int PAD = 1;				// gutter (pixels) around every baked cell, avoids seams with linear filtering

	struct Img { int w = 0, h = 0; std::vector<uint8_t> px; };	// RGBA

	inline int key(int x, int y) { return (y << 16) | x; }
	inline int keyX(int k) { return k & 0xFFFF; }
	inline int keyY(int k) { return k >> 16; }

	struct PaintLayer
	{
		std::string tex;				// relative to data\texture\ (raw cp949)
		float tiling = 2.0f;			// cells per repeat
		int rot = 0;					// 0..3 = 0, 90, 180, 270 degrees
		bool flipH = false, flipV = false;
		float offX = 0.0f, offY = 0.0f;	// in repeats
		bool ribbon = false;			// cliff ribbon: the mask carries 4 corner UVs after the alpha values, instead of world tiling
	};
	struct Orig { bool valid = false; std::string file; float uv[8] = { 0,0,0,0,0,0,0,0 }; };	// the tile as it was before the first bake
	typedef std::map<int, std::vector<uint8_t>> Masks;					// layer index -> MASK*MASK alpha

	struct Cell
	{
		Masks masks;
		Orig orig;
		int atlas = -1, slot = -1;
		bool hasTile = false;			// the cube owns a private tile that points to the atlas
		bool dirty = true;
		bool prevOk = false;			// the live preview texture holds this cell (not saved)
	};
	struct Atlas
	{
		int id = 0;
		std::string file;				// as stored in the GND (relative to data\texture\)
		int size = 512, cellPx = 30;
		std::vector<int> slots;			// cell key per slot, -1 = free
		uint64_t hash = 0;				// fingerprint of the last written file
		bool dirty = true;
		bool retired = false;
		std::vector<uint8_t> rgba;		// render cache (not saved)
		int blockX = -1, blockY = -1;	// map block this atlas covers when laid out by map position (-1 = filled in order)
		bool complete = false;			// the cache holds every cell of the atlas
		gl::Texture* prev = nullptr;	// live preview texture (not saved)
	};
	struct Project
	{
		std::vector<PaintLayer> layers;
		std::map<int, Cell> cells;
		std::vector<Atlas> atlases;
		int nextAtlasId = 1;
		std::string folder = "advtex";	// below data\texture\, ASCII
		int cellPx = 30;				// 14 / 30 / 62
		int atlasSize = 512;			// 256 / 512 (8 bit BMP, must stay below 1 MB)
		bool blockLayout = true;		// atlas images mirror the map: each atlas holds one square block of cells at their map positions
		bool autoBake = false;			// off: paint stays a draft preview until 'Build' / save
		bool loaded = false, pending = false;
		std::string path, status;
		int selLayer = 0;
		int saveChoice = 0;				// answer to the save prompt: 0 ask, 1 build, 2 keep as draft
		std::set<int> touched;			// cells whose preview has to be re-rendered
		double lastEdit = 0;
	};

	bool isEmptyCell(const Cell& c);
	std::string mapBase(Map* map);
	std::string atlasFile(Project& P, Map* map, int id);
	// every file operation of a bake goes through here; false = nothing was changed
	bool bake(Map* map, BrowEdit* be, Project& P, bool rebuildAll, std::string& msg);
	void loadProject(Map* map, BrowEdit* be, Project& P);
	void saveProject(Map* map, BrowEdit* be, Project& P);
	void clearImageCache();
	const Img* getImg(const std::string& file);
	std::vector<std::string> unusedAtlases(Map* map, Project& P);
	int deleteUnused(Map* map, BrowEdit* be, Project& P);
	// bake pending changes and write the sidecar (called by the map save)
	// returns false when it has to ask the user first (the save is repeated after the answer)
	bool onSave(Map* map, BrowEdit* be);
	bool hasUnbuilt(Map* map);
	void showSavePrompt(BrowEdit* be);
	void forget(Map* map);
	Project& project(Map* map, BrowEdit* be);

	Project* findProject(Map* map);
	bool assignSlot(Project& P, Map* map, int k, Cell& c);
	Atlas* atlasById(Project& P, int id);
	void renderCellToAtlas(Gnd* gnd, Project& P, Atlas& A, int k, Cell& c);

	// cliff brush: one cell of a baked cliff ribbon (rock band laid along the cliff curve, blended into the ground per pixel)
	struct RibbonCell
	{
		int x = 0, y = 0;
		float uv[8];					// (u,v) of the four tile corners, in tile order v1..v4 (= height index 0..3)
		std::vector<uint8_t> alpha;		// MASK*MASK, 255 = rock texture, 0 = ground
	};
	// returns an undoable action that puts the ribbon into the draft (preview now, files on Build / save); groundTex may be empty (the cell's own texture is the ground)
	Action* makeRibbonAction(Map* map, BrowEdit* be, const std::string& ribbonTex, const std::string& groundTex, const std::vector<RibbonCell>& cells, const std::string& title);

	// several layers at once (cave brush: floor, rock, edge ribbon); layers are created in the order given so later ones draw on top
	struct LayerSpec { std::string tex; bool ribbon = false; float tiling = 3.0f; };
	struct LayeredCell
	{
		int x = 0, y = 0;
		std::vector<std::vector<uint8_t>> alpha;	// per spec, MASK*MASK, empty = leave the layer out
		std::vector<std::vector<float>> uv;		// per spec, 8 floats for ribbon layers
	};
	Action* makeLayeredAction(Map* map, BrowEdit* be, const std::vector<LayerSpec>& specs, const std::vector<LayeredCell>& cells, const std::string& title);

	// UI (AdvTexMode.cpp)
	void frame(MapView* mv, BrowEdit* be);		// every frame, every mode: live preview + delayed bake
	void showLayerPicker(BrowEdit* be);
	void showWindow(BrowEdit* be);
	void postRender(MapView* mv, BrowEdit* be);
}
