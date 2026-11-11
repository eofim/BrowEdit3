#pragma once
#include <vector>
#include <glm/glm.hpp>

class BrowEdit;
class MapView;
class Map;
class Gnd;
class GndRenderer;

// Shape height tool (Height edit mode): click points to draw any shape (closed area) or path (ridge / road with a width), optionally
// smoothed into a curve, then apply. Every ground corner gets a height from its distance to the shape, so diagonals and curves come
// out smooth instead of stepped. The slope width decides between a steep cliff (small) and a soft hill (large).
namespace shapetool
{
	struct Settings
	{
		bool active = false;
		int type = 0;				// 0 closed area, 1 path with width
		bool smooth = true;			// curve through the clicked points (Catmull-Rom)
		int mode = 0;				// 0 raise, 1 lower, 2 set to height
		float height = 20.0f;
		float slope = 1.5f;			// tiles the edge takes to go from the ground to the top (small = steep cliff)
		float pathWidth = 4.0f;		// tiles, path type
		bool autoGat = true;
		float gatSlope = 4.0f;
		std::vector<glm::vec2> pts;	// tile units
		bool applyRequested = false;
	};
	Settings& settings();
	void showOptions(BrowEdit* browEdit, MapView* mapView);
	void update(MapView* mv, BrowEdit* browEdit, Map* map, Gnd* gnd, GndRenderer* gr, const glm::vec3& mouse3D);
}
