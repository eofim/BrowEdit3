"""Recut the N-S sand road / west branch along the user's red lines (second screenshot), bake soft edges into delta atlases prt_novrg*.bmp."""
import json, numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
OUT = "/mnt/user-data/outputs/"
exec(open("/home/claude/browedit3/tools/mapgen/novground/roadedge.py").read().split("f = np.load")[0].split("SCR =")[0])  # imports only
TD = "/mnt/user-data/uploads/texture/ÇÊµå¹Ù´Ú/"
T_GRASS = np.asarray(Image.open(TD + "prt_ÃÊ¿ø01.bmp").convert("RGB"), np.float32)
T_DIRT = np.asarray(Image.open("/mnt/user-data/uploads/ROO_MAP/data/texture/roo_prtf/prt_Èë03.bmp").convert("RGB"), np.float32)
f = np.load(SCR + "road_field2.npy")
L = json.load(open(SCR + "red2_cube2.json"))
A, B, C = L["A"], L["B"], L["C"]
# polygon: tip -> A (up, extended to meet old west edge) -> across -> C (down, extended) -> B reversed
topA = A + [(101.7, 157.0), (101.8, 161.5)]
topC = [(105.4, 161.5), (105.2, 157.5)] + C
botC = [(99.6, 120.5)]
botB = [(95.7, 120.5)]
poly = topA + topC + [tuple(p) for p in C[:0]] 
poly = topA + [(105.4, 161.5)] + [(105.2, 157.5)] + C + botC + botB[::-1] + B[::-1]
# the old road must stay outside the window: window y range 120.5..161.5, x 76..110
Q = 8
Wc = 201
xs = (np.arange(Wc * Q) + 0.5) / Q
FXq, FYq = np.meshgrid(xs, xs, indexing="ij")
img = Image.new("L", (Wc * Q, Wc * Q), 0)
ImageDraw.Draw(img).polygon([(x * Q, (Wc - y) * Q) for x, y in poly], fill=255)
pm = np.array(img)[::-1].T > 127                      # [ix, iy] fine
# fine old mask from the old corner field
old_fine = ndi.map_coordinates(f, [FXq - 0.0, FYq - 0.0], order=1, mode="nearest") > 0
win = (FXq >= 76) & (FXq <= 110) & (FYq >= 120.5) & (FYq <= 161.5)
m = np.where(win, pm, old_fine)
# soften the tip: round it
sd = (ndi.distance_transform_edt(m) - ndi.distance_transform_edt(~m)) / Q
sd = ndi.gaussian_filter(sd, 0.3 * Q)
rng = np.random.default_rng(31)
wb = ndi.zoom(rng.random((Wc // 10 + 4, Wc // 10 + 4)), 10, order=3)[:Wc, :Wc] - 0.5
wb2 = ndi.zoom(rng.random((Wc // 3 + 4, Wc // 3 + 4)), 3, order=3)[:Wc, :Wc] - 0.5
cornerg = np.arange(Wc) * 1.0
sd_c = ndi.map_coordinates(sd, [cornerg[:, None] * Q * np.ones((1, Wc)) - 0.5, cornerg[None, :] * Q * np.ones((Wc, 1)) - 0.5], order=1, mode="nearest")
sd_c = np.where(sd_c > -3.5, sd_c + 0.35 * wb + 0.18 * wb2, sd_c)
sd_c = ndi.gaussian_filter(sd_c, 0.6)
sd_c = np.clip(sd_c, -4, 4)
# blend with old field outside the window (smooth ramp 3 cubes wide)
wx, wy = np.meshgrid(np.arange(Wc), np.arange(Wc), indexing="ij")
dwin = np.minimum.reduce([wx - 76, 110 - wx, wy - 120.5, 161.5 - wy])
wgt = np.clip(dwin / 3.0, 0, 1); wgt = wgt * wgt * (3 - 2 * wgt)
f2 = np.where(wgt > 0, f * (1 - wgt) + sd_c * wgt, f)
np.save(SCR + "road_field3.npy", f2)
chg = np.abs(f2 - f) > 1e-3
cubechg = chg[:-1, :-1] | chg[1:, :-1] | chg[:-1, 1:] | chg[1:, 1:]
cubechg = ndi.binary_dilation(cubechg, np.ones((3, 3), bool))
mn = np.minimum.reduce([f2[:-1, :-1], f2[1:, :-1], f2[:-1, 1:], f2[1:, 1:]]); mx = np.maximum.reduce([f2[:-1, :-1], f2[1:, :-1], f2[:-1, 1:], f2[1:, 1:]])
zone = (mn < 0.7) & (mx > -0.7)
ul = {(e[0], e[1]) for e in json.load(open(OUT + "ue12_tiles.json"))["commands"][0]["list"]}
def samp(T, tu, tv):
    h, w = T.shape[:2]; fx = (tu % 1.0) * w - 0.5; fy = (tv % 1.0) * h - 0.5
    x0 = np.floor(fx).astype(int); y0 = np.floor(fy).astype(int); ax = (fx - x0)[..., None]; ay = (fy - y0)[..., None]
    g = lambda yy, xx: T[yy % h, xx % w]
    return (g(y0, x0) * (1 - ax) + g(y0, x0 + 1) * ax) * (1 - ay) + (g(y0 + 1, x0) * (1 - ax) + g(y0 + 1, x0 + 1) * ax) * ay
Qn = 4; rng = np.random.default_rng(5)
def nfield(sig):
    a = ndi.gaussian_filter(rng.standard_normal((201 * Qn, 201 * Qn)), sig * Qn); return a / a.std()
N1, N2 = nfield(0.9), nfield(0.3)
sm = lambda t: (lambda u: u * u * (3 - 2 * u))(np.clip(t, 0, 1))
def fld(a, wx_, wy_, q=1): return ndi.map_coordinates(a, [wx_ * q, wy_ * q], order=1, mode="nearest")
CELL, AW, AH = 56, 1024, 768
PA, PY = AW // CELL, AH // CELL; NC = PA * PY
cs = (np.arange(CELL) + 0.5) / CELL; FX, FY = np.meshgrid(cs, 1 - cs)
cl15 = {(e[0], e[1]): e for e in [c for c in json.load(open(OUT + "cl15.json"))["commands"] if c["cmd"] == "tiles"][0]["list"]}
rock = {k for k, e in cl15.items() if e[2] in (15, 18)}
cells = np.load(SCR + "cells_new2.npz")
mixed = [(x, y) for x in range(200) for y in range(200) if cubechg[x, y] and (x, y) in ul and "%d_%d" % (x, y) in cells.files and x >= 82]
cubes = [(x, y) for x in range(200) for y in range(200) if cubechg[x, y] and (x, y) not in ul and (x, y) not in rock]
bake = [(x, y) for x, y in cubes if zone[x, y]] + [p for p in mixed if zone[p]]
plain = [(x, y) for x, y in cubes if not zone[x, y]]
atl = [np.zeros((AH, AW, 3), np.uint8) for _ in range((len(bake) + NC - 1) // NC)]
tiles = []
RSLOT = 53
for n, (x, y) in enumerate(bake):
    wx_ = x + FX; wy_ = y + FY
    sdv = fld(f2, wx_, wy_)
    sdn = sdv + 0.18 * fld(N1, wx_, wy_, Qn) + 0.06 * fld(N2, wx_, wy_, Qn)
    wd = sm(0.5 + (sdn + 0.04) / 0.55)[..., None]
    i4, j4 = x % 4, y % 4
    gr = samp(T_GRASS, (i4 + FX) / 4, 1 - (j4 + FY) / 4); dt = samp(T_DIRT, (i4 + FX) / 4, 1 - (j4 + FY) / 4)
    col = gr * (1 - wd) + dt * wd
    if (x, y) in set(mixed):
        base = np.asarray(Image.fromarray(cells["%d_%d" % (x, y)]).resize((CELL, CELL), Image.BILINEAR), np.float32)
        col = base * (1 - wd) + dt * wd
    k, c = divmod(n, NC); cy_, cx_ = divmod(c, PA)
    atl[k][cy_ * CELL:(cy_ + 1) * CELL, cx_ * CELL:(cx_ + 1) * CELL] = np.clip(col, 0, 255).astype(np.uint8)
    eu, ev = 0.5 / AW, 0.5 / AH
    u0, u1 = cx_ * CELL / AW + eu, (cx_ + 1) * CELL / AW - eu; v0, v1 = cy_ * CELL / AH + ev, (cy_ + 1) * CELL / AH - ev
    tiles.append([x, y, RSLOT + k, round(u0, 5), round(v1, 5), round(u1, 5), round(v1, 5), round(u0, 5), round(v0, 5), round(u1, 5), round(v0, 5)])
for x, y in plain:
    dirt = f2[x:x + 2, y:y + 2].mean() > 0
    pts = [(((x % 4) + dx) / 4, 1 - ((y % 4) + dy) / 4) for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]]
    tiles.append([x, y, 2 if dirt else 0] + [round(float(q), 5) for p in pts for q in p])
RM = {15: 7, 18: 8}
for k_, e in cl15.items():
    if e[2] in RM and 76 <= k_[0] <= 110 and 150 <= k_[1] <= 166: tiles.append([e[0], e[1], RM[e[2]]] + list(e[3:]))
for k, im in enumerate(atl):
    Image.fromarray(im).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(OUT + "prt_novri%02d.bmp" % (k + 1))
json.dump({"commands": [{"cmd": "tiles", "list": tiles}]}, open(OUT + "ue13_tiles.json", "w"), separators=(",", ":"))
print("cubes", len(cubes), "baked", len(bake), "plain", len(plain), "atlases", len(atl))
# preview
x0, x1, y0, y1, S = 60, 115, 118, 164, 12
a = np.zeros(((x1 - x0) * S, (y1 - y0) * S, 3), np.uint8); big = np.kron(f2[x0:x1, y0:y1], np.ones((S, S)))
a[big > 0] = (200, 170, 90); a[big <= 0] = (100, 140, 60)
Image.fromarray(a.transpose(1, 0, 2)[::-1]).save(SCR + "plan3.png")
