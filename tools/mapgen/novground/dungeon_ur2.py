"""Upper-right sewer dungeon v3: floor = interior of the user's red outline (second drawing), rock block = the user's black outline,
both digitised from the screenshots; clipped to stay clear of every existing cliff (cl11 + cl15 heights)."""
import json, itertools, numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi
from skimage.morphology import skeletonize
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
OUT = "/mnt/user-data/outputs/"
IMG = "/mnt/user-data/attachments/bcc60026-74cc-55ec-960a-59e929b4c57f/ea26a12b-image.png"
SC = 10.84
px2x = lambda p: 200 - (995 - p) / SC
py2y = lambda p: 200 - (p - 29.5) / SC
x2px = lambda x: 995 - (200 - x) * SC
y2py = lambda y: 29.5 + (200 - y) * SC
im = np.array(Image.open(IMG).convert("RGB")).astype(int)
red0 = (im[..., 0] > 180) & (im[..., 1] < 80) & (im[..., 2] < 80)
blk = (im[..., 0] < 40) & (im[..., 1] < 40) & (im[..., 2] < 40); blk[:, 990:] = False; blk[:40] = False
# --- floors from the red strokes: bridge doorway gaps, take enclosed regions
sk = skeletonize(ndi.binary_dilation(red0, iterations=1))
nb = ndi.convolve(sk.astype(int), np.ones((3, 3), int), mode="constant") - 1
ys, xs = np.nonzero(sk & (nb == 1)); E = list(zip(xs.tolist(), ys.tolist()))
pairs, used = [], set()
for d, a, b in sorted(((np.hypot(a[0] - b[0], a[1] - b[1]), a, b) for a, b in itertools.combinations(E, 2))):
    if d > 75 or a in used or b in used: continue
    pairs.append((a, b)); used |= {a, b}
img = Image.new("L", (im.shape[1], im.shape[0]), 0); dr = ImageDraw.Draw(img)
rm = Image.fromarray((red0 * 255).astype(np.uint8)); img.paste(rm, (0, 0))
for a, b in pairs: dr.line([a, b], fill=255, width=5)
m = np.array(img) > 0
lb, k = ndi.label(~ndi.binary_dilation(m, iterations=1))
border = set(np.unique(np.r_[lb[0], lb[-1], lb[:, 0], lb[:, -1]]))
fl = np.zeros(m.shape, bool)
for i in range(1, k + 1):
    if i not in border and (lb == i).sum() > 150: fl |= lb == i
fl = ndi.binary_dilation(fl, iterations=3)
doors = Image.new("L", (im.shape[1], im.shape[0]), 0); dd = ImageDraw.Draw(doors)
for a, b in pairs: dd.line([a, b], fill=255, width=14)
fl |= np.array(doors) > 0
fl = ndi.binary_closing(fl, iterations=3)
# --- cube grids
X0, Y0, NX, NY = 128, 133, 70, 67                    # x128..197, y133..199
cx = np.arange(NX)[:, None] + X0 + 0.5; cy = np.arange(NY)[None, :] + Y0 + 0.5
def sample(mask):                                     # cube -> fraction of its pixel footprint inside mask
    acc = np.zeros((NX, NY)); n = 3
    for a in range(n):
        for b in range(n):
            xx = cx - 0.5 + (a + 0.5) / n + 0 * cy; yy = cy - 0.5 + (b + 0.5) / n + 0 * cx
            ix = np.clip(np.round(x2px(xx)).astype(int), 0, mask.shape[1] - 1); iy = np.clip(np.round(y2py(yy)).astype(int), 0, mask.shape[0] - 1)
            acc += mask[iy, ix]
    return acc / (n * n)
floor = sample(fl) > 0.5
# --- rock block from the black outline
bo = [(235, 28), (235, 150), (280, 153), (285, 255), (408, 260), (400, 555), (345, 560), (330, 745), (400, 755), (537, 770), (570, 765), (700, 768), (935, 758),
      (922, 550), (845, 495), (840, 290), (848, 280), (880, 235), (940, 178), (920, 170), (920, 28)]
pm = Image.new("L", (im.shape[1], im.shape[0]), 0); ImageDraw.Draw(pm).polygon(bo, fill=255)
rockpoly = sample(np.array(pm) > 0) > 0.5
# --- existing cliffs
cliff = np.zeros((200, 200), bool)
for f, xmax in (("cl11", 126), ("cl15", 200)):
    for c in json.load(open(OUT + f + ".json"))["commands"]:
        if c["cmd"] != "heights": continue
        a = np.array(c["data"]).reshape(c["h"], c["w"], 4)
        for j in range(c["h"]):
            for i in range(c["w"]):
                if c["x0"] + i < xmax and np.abs(a[j, i]).max() > 0.01: cliff[c["x0"] + i, c["y0"] + j] = True
sub = lambda a: a[X0:X0 + NX, Y0:Y0 + NY]
c1, c3 = sub(ndi.binary_dilation(cliff, iterations=1)), sub(ndi.binary_dilation(cliff, iterations=3))
floor &= ~c3
block = (rockpoly | ndi.binary_dilation(floor, structure=np.ones((3, 3), bool), iterations=2)) & ~c1
block |= floor
rock = block & ~floor
# the exit: spine mouth stays open at the south end (floor already reaches the polygon's bottom edge)
# --- centre channel along the south spine
spx = int(np.floor(px2x(551)))
chan = np.zeros((NX, NY), bool)
for y in range(138, 152):
    i, j = spx - X0, y - Y0
    if floor[i, j] and floor[i - 1, j] and floor[i + 1, j]: chan[i, j] = True
# --- every cube we may have touched before (old dungeon runs) is reset unless part of the new one
old = set()
for f in ("ue22.json", "ue24.json"):
    for e in json.load(open(OUT + f))["commands"][0]["list"]: old.add((e[0], e[1]))
reset = np.zeros((NX, NY), bool)
for x, y in old:
    if X0 <= x < X0 + NX and Y0 <= y < Y0 + NY: reset[x - X0, y - Y0] = True
reset &= ~(rock | floor) & ~c1
need = ndi.binary_dilation(rock | floor, iterations=1) | reset
need &= ~sub(cliff)
Hc = np.zeros((NX, NY)); Hc[rock] = -40.0; Hc[chan] = 10.0
# --- maximal rectangles covering `need` (greedy row runs merged vertically)
rects = []; left = need.copy()
while left.any():
    j0 = np.flatnonzero(left.any(axis=0))[0]; row = left[:, j0]
    i0 = np.flatnonzero(row)[0]; i1 = i0
    while i1 + 1 < NX and row[i1 + 1]: i1 += 1
    j1 = j0
    while j1 + 1 < NY and left[i0:i1 + 1, j1 + 1].all(): j1 += 1
    rects.append((int(i0 + X0), int(j0 + Y0), int(i1 - i0 + 1), int(j1 - j0 + 1))); left[i0:i1 + 1, j0:j1 + 1] = False
def win(x0, y0, w, h):
    d = []
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w):
            v = float(Hc[x - X0, y - Y0]); d += [v, v, v, v]
    return {"cmd": "heights", "x0": x0, "y0": y0, "w": w, "h": h, "gat": True, "wall_texture": "prt\\prt_novsw01.bmp", "data": d}
hc = [win(*r) for r in rects]
for r in rects: assert not cliff[r[0]:r[0] + r[2], r[1]:r[1] + r[3]].any()
G = 4
def pl(x, y):
    pts = [(((x % G) + dx) / G, 1 - ((y % G) + dy) / G) for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]]
    return [round(float(q), 5) for p in pts for q in p]
tiles = []
for i in range(NX):
    for j in range(NY):
        x, y = i + X0, j + Y0
        if rock[i, j]: tiles.append([x, y, 22, 0.06, 0.06, 0.14, 0.06, 0.06, 0.14, 0.14, 0.14])
        elif chan[i, j]: tiles.append([x, y, 301] + pl(x, y))
        elif floor[i, j]: tiles.append([x, y, 300] + pl(x, y))
        elif reset[i, j]: tiles.append([x, y, 0] + pl(x, y))
walk = floor | reset
rows = []
for yy in range(2 * NY):
    for typ, arr in ((1, rock), (0, walk)):
        xs_ = np.flatnonzero(np.repeat(arr, 2, axis=0)[:, yy // 2])
        if not len(xs_): continue
        s = p = xs_[0]
        for q in xs_[1:]:
            if q != p + 1: rows.append([2 * Y0 + yy, 2 * X0 + int(s), 2 * X0 + int(p) + 1, typ]); s = q
            p = q
        rows.append([2 * Y0 + yy, 2 * X0 + int(s), 2 * X0 + int(p) + 1, typ])
json.dump({"commands": hc}, open(OUT + "ur3_h.json", "w"), separators=(",", ":"))
json.dump({"commands": [{"cmd": "tiles", "list": tiles}]}, open(OUT + "ur3_t.json", "w"), separators=(",", ":"))
json.dump({"commands": [{"cmd": "gat", "rows": rows}]}, open(OUT + "ur3_g.json", "w"), separators=(",", ":"))
S = 12
pv = np.zeros((NY * S, NX * S, 3), np.uint8)
for i in range(NX):
    for j in range(NY):
        col = (90, 130, 60)
        if rock[i, j]: col = (30, 30, 30)
        if floor[i, j]: col = (110, 115, 140)
        if chan[i, j]: col = (60, 90, 170)
        if sub(cliff)[i, j]: col = (200, 170, 90)
        if reset[i, j] and not (rock[i, j] or floor[i, j]): col = (110, 160, 80)
        pv[(NY - 1 - j) * S:(NY - j) * S, i * S:(i + 1) * S] = col
Image.fromarray(pv).save(SCR + "ur3_pv.png")
print("floor", int(floor.sum()), "rock", int(rock.sum()), "chan", int(chan.sum()), "reset", int(reset.sum()), "rects", len(rects), "tiles", len(tiles), "gat rows", len(rows))
