// Offline check of Db discovery + Composer against a real data folder.
// usage: sim <dataRoot> <spritedb.json> <out.raw> [job gender style color headgearId robeId action]
#include "../../browedit/sprite/Character.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
namespace fs = std::filesystem;
struct DirSrc : sprite::AssetSource
{
	std::string root;
	bool read(const std::string& p, std::vector<uint8_t>& out) override
	{
		std::string q = p; for (auto& c : q) if (c == '\\') c = '/';
		std::ifstream i(root + "/" + q.substr(5), std::ios::binary);		// strip "data\"
		if (!i) return false;
		out.assign(std::istreambuf_iterator<char>(i), std::istreambuf_iterator<char>());
		return true;
	}
	void listAll(std::vector<std::string>& v) override
	{
		for (auto& e : fs::recursive_directory_iterator(root))
			if (e.is_regular_file())
			{
				std::string r = "data\\" + fs::relative(e.path(), root).string();
				for (auto& c : r) if (c == '/') c = '\\';
				v.push_back(r);
			}
	}
};
int main(int argc, char** argv)
{
	if (argc < 4) return 1;
	DirSrc src; src.root = argv[1];
	sprite::Db db; std::string err;
	std::ifstream mj(argv[2]); std::stringstream ss; ss << mj.rdbuf();
	if (!db.loadMeta(ss.str(), err)) { std::cerr << "meta: " << err << "\n"; return 2; }
	db.scan(src);
	printf("files %zu jobs %zu hair m/f %zu/%zu headgear total %zu (present m %d f %d) robes present %d weapons present %d shields %zu\n", db.fileCount, db.jobs.size(),
		db.hairStyles[0].size(), db.hairStyles[1].size(), db.headgear.size(),
		(int)std::count_if(db.headgear.begin(), db.headgear.end(), [](auto& e) { return e.has[0]; }), (int)std::count_if(db.headgear.begin(), db.headgear.end(), [](auto& e) { return e.has[1]; }),
		(int)std::count_if(db.robes.begin(), db.robes.end(), [](auto& e) { return e.has[0] || e.has[1]; }), (int)std::count_if(db.weapons.begin(), db.weapons.end(), [](auto& e) { return e.has[0] || e.has[1]; }), db.shields.size());
	for (size_t i = 0; i < db.jobs.size() && i < 8; i++) printf("  job %s label=%s id=%d m=%d f=%d imf=%d\n", db.jobs[i].sprite.c_str(), db.jobs[i].label.c_str(), db.jobs[i].id, db.jobs[i].has[0], db.jobs[i].has[1], db.jobs[i].imf[0]);
	sprite::Composer comp(db, src);
	sprite::CharacterConfig c;
	if (argc > 4) c.job = argv[4];
	if (argc > 5) c.gender = atoi(argv[5]);
	if (argc > 6) c.hairStyle = atoi(argv[6]);
	if (argc > 7) c.hairColor = atoi(argv[7]);
	if (argc > 8) c.head[0] = atoi(argv[8]);
	if (argc > 9) c.robe = atoi(argv[9]);
	int action = argc > 10 ? atoi(argv[10]) : 0;
	for (auto& w : comp.warnings(c)) printf("warning: %s\n", w.c_str());
	// 8 directions side by side
	const int cw = 160, ch = 200;
	std::vector<uint8_t> sheet((size_t)cw * 8 * ch * 4, 90); for (size_t i = 3; i < sheet.size(); i += 4) sheet[i] = 255;
	for (int d = 0; d < 8; d++)
	{
		auto inf = comp.info(c, action, d);
		auto f = comp.frame(c, action, d, 0);
		printf("dir %d valid=%d frames=%d ms=%.0f img %dx%d\n", d, inf.valid, inf.frames, inf.ms, f->w, f->h);
		for (int y = 0; y < f->h; y++) for (int x = 0; x < f->w; x++)
		{
			int sx = d * cw + cw / 2 - f->ox + x, sy = ch - 30 - f->oy + y;
			if (sx < d * cw || sx >= (d + 1) * cw || sy < 0 || sy >= ch) continue;
			const uint8_t* s = &f->rgba[((size_t)y * f->w + x) * 4]; uint8_t* o = &sheet[((size_t)sy * cw * 8 + sx) * 4];
			float a = s[3] / 255.0f;
			for (int k = 0; k < 3; k++) o[k] = (uint8_t)(s[k] * a + o[k] * (1 - a));
		}
	}
	FILE* f = fopen(argv[3], "wb"); int w = cw * 8, h = ch; fwrite(&w, 4, 1, f); fwrite(&h, 4, 1, f); fwrite(sheet.data(), 1, sheet.size(), f); fclose(f);
}
