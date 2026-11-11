"""Delta atlas of dungeon cubes changed vs the first applied build (cells_old) -> prt_novmh01.bmp (slot 52) + ue12_tiles.json."""
import json, numpy as np
from PIL import Image
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
OUT = "/mnt/user-data/outputs/"
TAGN = "new2"; DSLOT = 52
co = np.load(SCR + "cells_old.npz"); cn = np.load(SCR + "cells_%s.npz" % TAGN)
so = json.load(open(SCR + "sets_old.json")); sn = json.load(open(SCR + "sets_%s.json" % TAGN))
told = {(e[0], e[1]): e for e in json.load(open('/tmp/ul_t_old.json'))['commands'][0]['list']}
tnew = {(e[0], e[1]): e for e in json.load(open(OUT + 'ul_t.json'))['commands'][0]['list']}
changed = [k for k in cn.files if k not in co.files or np.abs(co[k].astype(int) - cn[k].astype(int)).max() > 0]
removed = [k for k in co.files if k not in cn.files]
oldplain = {tuple(p) for p in so['plain']}; newplain = {tuple(p) for p in sn['plain']}
CELL, AW, AH = 40, 1024, 512; PA = AW // CELL; NC = PA * (AH // CELL)
atl = [np.zeros((AH, AW, 3), np.uint8) for _ in range((len(changed) + NC - 1) // NC)]
tiles = []
for n, k in enumerate(changed):
    x, y = map(int, k.split('_')); kk, c = divmod(n, NC); cy, cx = divmod(c, PA)
    atl[kk][cy * CELL:(cy + 1) * CELL, cx * CELL:(cx + 1) * CELL] = cn[k]
    eu, ev = 0.5 / AW, 0.5 / AH
    u0, u1 = cx * CELL / AW + eu, (cx + 1) * CELL / AW - eu; v0, v1 = cy * CELL / AH + ev, (cy + 1) * CELL / AH - ev
    tiles.append([x, y, DSLOT + kk, round(u0, 5), round(v1, 5), round(u1, 5), round(v1, 5), round(u0, 5), round(v0, 5), round(u1, 5), round(v0, 5)])
chs = {tuple(map(int, k.split('_'))) for k in changed}
rm = lambda i: 22 if i == 201 else 32 + (i - 300)
for (x, y), e in tnew.items():
    if (x, y) in chs: continue
    if e[2] == 201: tiles.append([x, y, 22] + e[3:]); continue
    o = told[(x, y)]; tiles.append([o[0], o[1], rm(o[2])] + list(o[3:]))
G = 4
def pl(x, y):
    pts = [(((x % G) + dx) / G, 1 - ((y % G) + dy) / G) for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]]
    return [round(float(q), 5) for p in pts for q in p]
for x, y in [tuple(map(int, k.split('_'))) for k in removed] + list(oldplain - newplain): tiles.append([x, y, 0] + pl(x, y))
for i, a in enumerate(atl):
    Image.fromarray(a).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(OUT + 'prt_novmh%02d.bmp' % (i + 1))
json.dump({'commands': [{'cmd': 'tiles', 'list': tiles}]}, open(OUT + 'ue12_tiles.json', 'w'), separators=(',', ':'))
print('changed', len(changed), 'tiles', len(tiles), 'atlases', len(atl))
