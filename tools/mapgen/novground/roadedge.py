"""Bake the road/grass edge cubes into small atlases (no stretched strip tiles): per pixel blend of the same grass/dirt texture
slices the plain tiles use, driven by the road signed-distance field. Writes prt_novre01..N.bmp and re_tiles.json (provisional slot = 100+k)."""
import json, numpy as np
from PIL import Image
from scipy import ndimage as ndi
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
OUT = "/mnt/user-data/outputs/"
TD = "/mnt/user-data/uploads/texture/ÇÊµå¹Ù´Ú/"
T_GRASS = np.asarray(Image.open(TD + "prt_ÃÊ¿ø01.bmp").convert("RGB"), np.float32)
T_DIRT = np.asarray(Image.open("/mnt/user-data/uploads/ROO_MAP/data/texture/roo_prtf/prt_Èë03.bmp").convert("RGB"), np.float32)
f = np.load(SCR + "road_field2.npy")
CAMP = (625 / 1250 * 200, (1 - 598 / 1160) * 200)
def samp(T, tu, tv):
    h, w = T.shape[:2]; fx = (tu % 1.0) * w - 0.5; fy = (tv % 1.0) * h - 0.5
    x0 = np.floor(fx).astype(int); y0 = np.floor(fy).astype(int); ax = (fx - x0)[..., None]; ay = (fy - y0)[..., None]
    g = lambda yy, xx: T[yy % h, xx % w]
    return (g(y0, x0) * (1 - ax) + g(y0, x0 + 1) * ax) * (1 - ay) + (g(y0 + 1, x0) * (1 - ax) + g(y0 + 1, x0 + 1) * ax) * ay
rng = np.random.default_rng(5); Q = 4
def nfield(sig):
    a = ndi.gaussian_filter(rng.standard_normal((201 * Q, 201 * Q)), sig * Q); return a / a.std()
N1, N2 = nfield(0.9), nfield(0.3)
sm = lambda t: (lambda u: u * u * (3 - 2 * u))(np.clip(t, 0, 1))
def fld(a, wx, wy, q=1): return ndi.map_coordinates(a, [wx * q, wy * q], order=1, mode="nearest")
mn = np.minimum.reduce([f[:-1, :-1], f[1:, :-1], f[:-1, 1:], f[1:, 1:]]); mx = np.maximum.reduce([f[:-1, :-1], f[1:, :-1], f[:-1, 1:], f[1:, 1:]])
zone = (mn < 0.7) & (mx > -0.7)
old = json.load(open("/tmp/ng29_orig.json"))["commands"][0]["list"]
keep = {(t[0], t[1]) for t in old if t[2] in (6, 11, 12, 13)}          # plaza rosette / brick / rim atlases
cubes = []
for x in range(200):
    for y in range(200):
        if not zone[x, y] or (x, y) in keep: continue
        if np.hypot(x + 0.5 - CAMP[0], y + 0.5 - CAMP[1]) < 16.5: continue
        if 36 <= x <= 49 and 58 <= y <= 77: continue                    # cave mouth is baked by the dungeon script
        cubes.append((x, y))
print("edge cubes", len(cubes))
CELL, AW, AH = 56, 1024, 768
PA, PY = AW // CELL, AH // CELL; NC = PA * PY
cs = (np.arange(CELL) + 0.5) / CELL
FX, FY = np.meshgrid(cs, 1 - cs)
natl = (len(cubes) + NC - 1) // NC
atl = [np.zeros((AH, AW, 3), np.uint8) for _ in range(natl)]
tiles = []
for n, (x, y) in enumerate(cubes):
    wx = x + FX; wy = y + FY
    sd = fld(f, wx, wy)
    sdn = sd + 0.18 * fld(N1, wx, wy, Q) + 0.06 * fld(N2, wx, wy, Q)
    wd = sm(0.5 + (sdn + 0.04) / 0.55)[..., None]
    i4, j4 = x % 4, y % 4
    gr = samp(T_GRASS, (i4 + FX) / 4, 1 - (j4 + FY) / 4); dt = samp(T_DIRT, (i4 + FX) / 4, 1 - (j4 + FY) / 4)
    col = gr * (1 - wd) + dt * wd
    k, c = divmod(n, NC); cy_, cx_ = divmod(c, PA)
    atl[k][cy_ * CELL:(cy_ + 1) * CELL, cx_ * CELL:(cx_ + 1) * CELL] = np.clip(col, 0, 255).astype(np.uint8)
    eu, ev = 0.5 / AW, 0.5 / AH
    u0, u1 = cx_ * CELL / AW + eu, (cx_ + 1) * CELL / AW - eu; v0, v1 = cy_ * CELL / AH + ev, (cy_ + 1) * CELL / AH - ev
    tiles.append([x, y, 100 + k, round(u0, 5), round(v1, 5), round(u1, 5), round(v1, 5), round(u0, 5), round(v0, 5), round(u1, 5), round(v0, 5)])
for k, im in enumerate(atl):
    Image.fromarray(im).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(OUT + "prt_novre%02d.bmp" % (k + 1))
    Image.fromarray(im).save(SCR + "novre%02d.png" % (k + 1))
json.dump(tiles, open(SCR + "re_tiles.json", "w"), separators=(",", ":"))
print("atlases", natl)
