#pragma once
// Plain parsers for the Ragnarok sprite formats (.spr, .act, .pal, .imf). No OpenGL / editor dependencies,
// so they can be unit tested on their own (tools/spritetest).
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ro
{
	struct Palette
	{
		uint8_t c[256][4];	// r,g,b,a
		bool valid = false;
		Palette() { memset(c, 0, sizeof(c)); }
		bool load(const std::vector<uint8_t>& d);	// 1024 bytes, rgba
	};

	struct Image
	{
		int w = 0, h = 0;
		std::vector<uint8_t> idx;	// indexed pixels (w*h), when indexed
		std::vector<uint8_t> rgba;	// truecolor pixels, top row first
	};

	struct Spr
	{
		std::vector<Image> indexed, truecolor;
		Palette palette;			// the sprite's embedded palette
		bool load(const std::vector<uint8_t>& d);
		// RGBA pixels of an image; custom palette overrides the embedded one (indexed images only)
		bool pixels(int type, int index, const Palette* custom, std::vector<uint8_t>& out, int& w, int& h) const;
	};

	struct ActLayer { int x = 0, y = 0, spr = -1, mirror = 0, type = 0; uint8_t col[4] = { 255,255,255,255 }; float sx = 1, sy = 1; int rot = 0; int w = 0, h = 0; };
	struct ActAnchor { int x = 0, y = 0; };
	struct ActFrame { std::vector<ActLayer> layers; int sound = -1; std::vector<ActAnchor> anchors; };
	struct ActAction { std::vector<ActFrame> frames; float interval = 4.0f; };
	struct Act
	{
		int version = 0;			// major*10+minor
		std::vector<ActAction> actions;
		bool load(const std::vector<uint8_t>& d);
	};

	// Layer priorities per action/frame (which of body/head/... is in front)
	struct Imf
	{
		struct Entry { int priority = 0, x = 0, y = 0; };
		// layers[layer][action][frame]
		std::vector<std::vector<std::vector<Entry>>> layers;
		bool load(const std::vector<uint8_t>& d);
		const Entry* get(int layer, int action, int frame) const;
	};
}
