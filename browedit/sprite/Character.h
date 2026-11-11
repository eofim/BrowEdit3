#pragma once
#include "Db.h"
#include "Compose.h"
#include <memory>
#include <unordered_map>
#include <json.hpp>

namespace sprite
{
	// Standard player actions (index * 8 + direction in the .act files).
	struct ActionDef { int base; const char* name; };
	extern const ActionDef actionDefs[];
	extern const int actionDefCount;
	extern const char* directionNames[8];	// 0=S 1=SW 2=W 3=NW 4=N 5=NE 6=E 7=SE

	struct CharacterConfig
	{
		int kind = 0;					// 0 = player, 1 = monster, 2 = npc (a single sprite)
		std::string mob;				// sprite name when kind != 0
		int headMode = 0;				// 0 = head animates with the sprite, 1..3 = steady head (pose 0..2 of the head sprite), 4 = none (whole sprite frozen on frame 0)
		std::string job = "\xEC\xB4\x88\xEB\xB3\xB4\xEC\x9E\x90";	// novice
		int gender = Male;
		int hairStyle = 1, hairColor = 0, bodyPalette = -1;			// -1 = no palette
		int head[3] = { 0,0,0 };		// upper, middle, lower headgear (client view ids)
		int costume[3] = { 0,0,0 };		// costume headgear, replaces the normal one in the same slot
		int robe = 0;
		int weapon = 0;
		std::string shield;
		std::string customWing;			// "data\\sprite\\custom\\xxx" (without extension), optional
		int wingOff[8][2] = {};			// per direction alignment of the custom asset
		bool operator==(const CharacterConfig& o) const { return key() == o.key(); }
		std::string key() const;
		nlohmann::json toJson() const;
		static CharacterConfig fromJson(const nlohmann::json& j);
	};

	struct Frame
	{
		int w = 0, h = 0;
		int ox = 0, oy = 0;					// position of the feet (sprite origin) inside the image, from the top left
		std::vector<uint8_t> rgba;
		unsigned int tex = 0;				// GL texture, owned by the editor side cache
	};

	struct AnimInfo { int frames = 1; float ms = 100; bool valid = false; };

	class Composer
	{
	public:
		Composer(Db& db, AssetSource& src) : db(db), src(src) {}
		AnimInfo info(const CharacterConfig& c, int action, int dir);
		std::shared_ptr<Frame> frame(const CharacterConfig& c, int action, int dir, int frame) { return frame_(c, c.key(), action, dir, frame); }
		std::shared_ptr<Frame> frame_(const CharacterConfig& c, const std::string& cfgKey, int action, int dir, int frame);	// cfgKey = c.key(), cached by the caller
		std::vector<std::string> warnings(const CharacterConfig& c);	// resources the config asks for that do not exist
		void clearFrames() { for (auto& kv : frames) dropped.push_back(kv.second); frames.clear(); }
		void clearAll() { clearFrames(); spr.clear(); act.clear(); pal.clear(); imf.clear(); }
		size_t frameCount() const { return frames.size(); }
		std::vector<std::shared_ptr<Frame>> dropped;	// frames removed from the cache, so the GL side can free their textures
	private:
		Db& db; AssetSource& src;
		template<class T> using Cache = std::unordered_map<std::string, std::shared_ptr<T>>;
		Cache<ro::Spr> spr; Cache<ro::Act> act; Cache<ro::Palette> pal; Cache<ro::Imf> imf;
		std::unordered_map<std::string, std::shared_ptr<Frame>> frames;
		std::shared_ptr<ro::Spr> loadSpr(const std::string& path);
		std::shared_ptr<ro::Act> loadAct(const std::string& path);
		std::shared_ptr<ro::Palette> loadPal(const std::string& path);
		std::shared_ptr<ro::Imf> loadImf(const std::string& path);

		struct Layer
		{
			std::shared_ptr<ro::Spr> s; std::shared_ptr<ro::Act> a; std::shared_ptr<ro::Palette> p;
			int attach = 0;			// 0 = origin, 1 = anchored to the body
			float order = 0;
			int extraX = 0, extraY = 0;
			std::string what;
		};
		struct Set { std::vector<Layer> layers; std::shared_ptr<ro::Imf> imf; };
		Set build(const CharacterConfig& c, int dir);
	};
}
