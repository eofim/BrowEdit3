#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <atomic>
#include <cstdint>

namespace sprite
{
	// Where the sprites come from. The editor implements this on top of util::FileIO (GRFs + data folders, in their precedence order).
	// All paths are UTF-8, backslash separated, relative to the client root (e.g. "data\\sprite\\...").
	struct AssetSource
	{
		virtual ~AssetSource() {}
		virtual bool read(const std::string& path, std::vector<uint8_t>& out) = 0;
		virtual void listAll(std::vector<std::string>& paths) = 0;
	};

	enum Gender { Male = 0, Female = 1 };
	inline const char* genderName(int g) { return g == Female ? "\xEC\x97\xAC" : "\xEB\x82\xA8"; }	// 여 / 남

	struct JobEntry
	{
		std::string sprite;		// sprite name used in the file names (utf-8)
		std::string label;		// readable name
		int id = -1;			// client job id (-1 = not known)
		bool has[2] = { false,false };
		bool imf[2] = { false,false };
		int group = 7;			// see Db::groupNames
	};
	struct GearEntry
	{
		int id = 0;				// client view id (-1 when the client has no table for it)
		std::string label;
		std::string sprite;		// "_name" part of the file name
		bool has[2] = { false,false };	// file exists for male / female (robes: for at least one job)
	};

	struct MobEntry { std::string sprite; std::string label; int id = -1; };

	class Db
	{
	public:
		static inline const char* groupNames[8] = { "1st class", "2nd class", "Alternate class", "Expanded class", "Transcendent / Rebirth", "3rd class", "4th class", "Others" };
		static int jobGroup(int id);
		// metadata exported from the client lua tables by tools/spritedb/gen_spritedb.py
		bool loadMeta(const std::string& jsonText, std::string& error);
		// scans what exists (can run on a worker thread; set ready when done)
		void scan(AssetSource& src);
		std::atomic<bool> ready{ false };
		std::atomic<bool> scanning{ false };

		std::vector<JobEntry> jobs;
		std::vector<int> hairStyles[2];
		std::map<int, std::vector<int>> hairColors[2];		// style -> colour indices (palette files)
		std::map<std::string, std::vector<int>> bodyPalettes[2];	// job sprite -> palette indices
		std::vector<GearEntry> headgear;						// accessories
		std::vector<GearEntry> robes;
		std::vector<GearEntry> weapons;
		std::vector<MobEntry> monsters, npcs;					// single sprites found in data\sprite\monster and data\sprite\npc
		std::vector<std::string> shields;						// found by folder scan
		std::vector<std::string> customWings;					// sprite files found in the optional custom folder
		std::map<int, std::map<int, int>> layerItems;			// view id -> (dir or -1 for default) -> priority
		int prioTop = 200, prioMid = 100, prioBottom = 300, prioRobe = 400;
		size_t fileCount = 0;

		bool exists(const std::string& path) const;				// case insensitive
		std::string actual(const std::string& path) const;		// path as it is stored by the source
		const JobEntry* job(const std::string& sprite) const;
		const GearEntry* gear(const std::vector<GearEntry>& v, int id) const;
		bool hasPrefix(const std::string& dirLower) const;

		// path builders
		static std::string bodyPath(const std::string& job, int g);
		static std::string headPath(int style, int g);
		static std::string hairPalPath(int style, int g, int color);
		static std::string bodyPalPath(const std::string& job, int g, int idx);
		static std::string headgearPath(const GearEntry& e, int g);
		static std::string robePath(const GearEntry& e, const std::string& job, int g);
		static std::string weaponPath(const GearEntry& e, const std::string& job, int g);
		static std::string shieldPath(const std::string& shield, const std::string& job, int g);
		static std::string mobPath(int kind, const std::string& name);	// kind 1 = monster, 2 = npc
		static std::string imfPath(const std::string& job, int g);

	private:
		std::map<std::string, std::string> files;	// lower -> actual
		std::set<std::string> dirs;					// lower directories
		std::vector<JobEntry> metaJobs;
		std::map<std::string, int> jobConsts;
		std::vector<GearEntry> metaAcc, metaRobes, metaWeapons;
	};
}
