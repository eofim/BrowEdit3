// Offline check of the sprite parsers/compositor against real client files.
// usage: spritetest out.raw action frame mode body.spr body.act [head.spr head.act [headpal] [acc.spr acc.act]]
#include "../../browedit/sprite/Compose.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

static bool slurp(const char* f, std::vector<uint8_t>& d)
{
	std::ifstream i(f, std::ios::binary);
	if (!i) return false;
	d.assign(std::istreambuf_iterator<char>(i), std::istreambuf_iterator<char>());
	return true;
}

int main(int argc, char** argv)
{
	if (argc < 7) { fprintf(stderr, "args\n"); return 1; }
	int action = atoi(argv[2]), frame = atoi(argv[3]), mode = atoi(argv[4]);
	std::vector<uint8_t> d;
	ro::Spr bs, hs, as; ro::Act ba, ha, aa; ro::Palette hp;
	if (!slurp(argv[5], d) || !bs.load(d)) { fprintf(stderr, "body spr fail\n"); return 2; }
	if (!slurp(argv[6], d) || !ba.load(d)) { fprintf(stderr, "body act fail\n"); return 2; }
	printf("body act v%d actions=%zu spr idx=%zu rgba=%zu\n", ba.version, ba.actions.size(), bs.indexed.size(), bs.truecolor.size());
	bool haveHead = argc >= 9, havePal = argc >= 10, haveAcc = argc >= 12;
	if (haveHead) { if (!slurp(argv[7], d) || !hs.load(d)) { fprintf(stderr, "head spr fail\n"); return 2; } if (!slurp(argv[8], d) || !ha.load(d)) { fprintf(stderr, "head act fail\n"); return 2; } }
	if (havePal && (!slurp(argv[9], d) || !hp.load(d))) { fprintf(stderr, "pal fail\n"); return 2; }
	if (haveAcc) { if (!slurp(argv[10], d) || !as.load(d)) { fprintf(stderr, "acc spr fail\n"); return 2; } if (!slurp(argv[11], d) || !aa.load(d)) { fprintf(stderr, "acc act fail\n"); return 2; } }

	ro::Canvas cv(260, 300, 130, 220);
	auto anchor = [&](const ro::Act& a, int act, int fr, int& x, int& y) {
		x = y = 0;
		if (act < (int)a.actions.size() && !a.actions[act].frames.empty())
		{
			auto& F = a.actions[act].frames[fr % a.actions[act].frames.size()];
			if (!F.anchors.empty()) { x = F.anchors[0].x; y = F.anchors[0].y; }
			printf("  anchors=%zu first=(%d,%d)\n", F.anchors.size(), x, y);
		}
	};
	ro::Part body; body.spr = &bs; body.act = &ba;
	ro::drawFrame(cv, body, action, frame, 0, 0);
	int bx, by, hx, hy;
	printf("body:"); anchor(ba, action, frame, bx, by);
	int hdx = 0, hdy = 0;
	if (haveHead)
	{
		printf("head:"); anchor(ha, action, frame, hx, hy);
		hdx = bx - hx; hdy = by - hy;
		ro::Part head; head.spr = &hs; head.act = &ha; head.pal = havePal ? &hp : nullptr;
		ro::drawFrame(cv, head, action, frame, hdx, hdy);
	}
	if (haveAcc)
	{
		int ax, ay;
		printf("acc:"); anchor(aa, action, frame, ax, ay);
		ro::Part acc; acc.spr = &as; acc.act = &aa;
		int ox = mode == 0 ? hdx : bx - ax, oy = mode == 0 ? hdy : by - ay;
		ro::drawFrame(cv, acc, action, frame, ox, oy);
	}
	FILE* f = fopen(argv[1], "wb");
	fwrite(&cv.w, 4, 1, f); fwrite(&cv.h, 4, 1, f); fwrite(cv.rgba.data(), 1, cv.rgba.size(), f);
	fclose(f);
	return 0;
}
