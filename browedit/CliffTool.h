#pragma once
#include <string>
#include <vector>
#include <glm/glm.hpp>

class BrowEdit;
class MapView;
class Map;
class Gnd;
class GndRenderer;

// Cliff brush for the Height edit mode (doodle): circular brush that raises / lowers / sets terrain with a natural, noisy edge,
// optionally as a sheer cliff with generated walls and a texture set for the cliff face and the slope.
namespace cliff
{
	struct Settings
	{
		bool active = false;
		bool stackLevel = false;		// build relative to the ground under the brush instead of merging with an earlier cliff
		bool continueCliff = true;		// a stroke that starts on an existing cliff keeps that cliff's height
		// cave brush: carve a walkable floor into a rock mass
		bool cave = false;
		float caveSlope = 3.5f;			// wall slope width (tiles)
		bool caveMass = true;			// raise a rock mass around the floor
		float caveRock = 30.0f;			// rock mass height
		float caveRockW = 5.0f;			// thickness of the rock top (tiles)
		float caveOuter = 4.0f;			// outer slope down to the ground (tiles)
		bool caveFloorAbs = false;
		float caveFloorH = 0.0f;
		bool caveBake = true;
		std::string caveFloorTex, caveEdgeTex, caveRockTex;
		float caveFloorTiling = 3.0f, caveRockTiling = 4.0f;
		float caveBand = 2.5f;			// tiles covered by the edge texture across the wall foot
		float caveEdgeTop = 0.36f, caveEdgeBottom = 0.72f;	// where the edge band sits in the edge texture (v)
		bool caveEdgeFlip = false;
		float caveEdgeShift = 0.1f;		// moves the band towards the wall (+) or the floor (-), in bands		// cliff brush instead of the height doodle brush
		int mode = 0;			// 0 raise, 1 lower, 2 set to height
		float radius = 5.0f;		// tiles
		float height = 20.0f;
		int profile = 0;		// 0 sloped, 1 sheer (walls)
		float edgeWidth = 2.0f;	// tiles, sloped profile
		float roughness = 0.35f;	// how much noise bends the edge
		float noiseScale = 0.35f;	// noise frequency (per tile)
		int terraces = 1;
		int seed = 1;
		bool buildWalls = true;
		bool autoGat = true;			// update the GAT with the cliff: heights, and cliff type (5) on steep ground / along walls
		float gatSlope = 4.0f;			// height difference across one GAT cell (5 units wide) above which it becomes a cliff
		bool randomSlice = true;
		bool texSlope = true;
		std::vector<std::string> wallTextures;
		std::vector<std::string> slopeTextures;
		// ribbon texturing (sloped cliffs): one rock-band texture with continuous UVs along the cliff curve (u along, v across), no seams between tiles
		bool ribbon = true;
		std::string ribbonTex;
		bool ribbonAuto = true;			// texture scale from the real size of the 3D face, so the rock is not squeezed or stretched
		float ribbonBandW = 0.0f;		// tiles across the cliff covered by the texture's rock band (0 = the slope width)
		float ribbonPeriod = 5.0f;		// cubes per texture repeat along the cliff
		float ribbonTop = 0.08f, ribbonBottom = 0.92f;		// where the rock band sits in the texture (v)
		bool ribbonFlip = false;
		bool ribbonBake = true;			// bake the blend per pixel through Advanced Texture (draft preview, files on Build/save); off = plain tile UVs
		std::string ribbonGround;		// optional ground texture the rock blends into (empty = the ground the tiles already have)
		int ribbonDetail = 62;			// baked pixels per tile (30 or 62): the rock texture is detailed, 30 looks soft
		float ribbonSoft = 0.07f;		// softness of the rock edge (texture v units)
		float ribbonNoise = 0.06f;		// how ragged the edge is
		float ribbonFoot = 1.5f;		// ground (tiles) below the cliff foot over which the lower fringe of the texture is spread
		float ribbonMaxStretch = 1.8f;		// tiles that would stretch the texture more than this are left alone
		// smart edge texturing (sheer cliffs): ground pieces that blend rock into grass along the cliff
		bool smartEdges = true;
		std::string pieceTex[3];		// 0 straight edge, 1 rounded corner, 2 diagonal
		int pieceRot[3] = { 0, 0, 0 };	// extra quarter turns
		bool pieceFlip[3] = { false, false, false };
	};
	Settings& settings();
	// called when a texture is clicked in the texture picker while the cliff brush is active: adds it to the active texture list
	void onTexturePicked(const std::string& file);
	void showOptions(BrowEdit* browEdit, MapView* mapView);
	void showCaveOptions(BrowEdit* browEdit, MapView* mapView);
	void saveCave(Map* map, BrowEdit* browEdit);
	void forgetCave(Map* map);
	void update(MapView* mv, BrowEdit* browEdit, Map* map, Gnd* gnd, GndRenderer* gr, const glm::vec3& mouse3D);
}
