#pragma once

class BrowEdit;
class MapView;

// makes recoloured copies of a model: new textures + a copy of the .rsm/.rsm2 (same version) that uses them
namespace modelvariants
{
	extern bool visible;
	void showWindow(BrowEdit* browEdit);
	bool postRender(MapView* mapView, BrowEdit* browEdit);		// 3D painting; true when it took over the mouse
}
