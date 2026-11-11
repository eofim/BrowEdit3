"""Beach strip along the shore inside the user's red lines (screenshot beach_red_shot.png, calibrated -> beach_loop.npy,
8 px/cube, window x 120..200, y 40..125).  Bakes grass -> dirt -> pale sand cells; never touches cliff cubes, the dungeon, road or plaza.
Writes beach_t.json (provisional slots 300+k) and prt_novbg%02d.bmp."""
import json, os, numpy as np
from PIL import Image
from scipy import ndimage as ndi
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = "/mnt/user-data/outputs/"; TEX = "/mnt/user-data/uploads/texture/"
ROO = "/mnt/user-data/uploads/ROO_MAP/data/texture/roo_prtf/"
Q, LX0, LY0, LY1 = 8, 120, 40, 125
loop = np.load(os.path.join(HERE, "beach_loop.npy"))                     # [row (north first), col]
ins = ndi.distance_transform_edt(loop) / Q; outs = ndi.distance_transform_edt(~loop) / Q
sdl = ins - outs
rng = np.random.default_rng(5)
def vn(sc, seed):
    r = np.random.default_rng(seed); g = r.random((int(80 / sc) + 4, int(90 / sc) + 4))
    z = ndi.zoom(g, sc, order=3, mode="grid-wrap"); return z - z.mean()
NZ1, NZ2, NZ3 = vn(3.0, 11), vn(1.2, 12), vn(0.5, 13)
def fld(a, wx, wy, org=(0, 0)): return ndi.map_coordinates(a, [(wx - org[0]) - 0.5 + 0.0, (wy - org[1]) - 0.5], order=1, mode="wrap")
def sd_at(wx, wy):                       # signed distance (cubes, + inside red loop)
    r = (LY1 - wy) * Q - 0.5; c = (wx - LX0) * Q - 0.5
    return ndi.map_coordinates(sdl, [r, c], order=1, mode="nearest")
def noise(a, wx, wy, s=1.0): return ndi.map_coordinates(a, [(wy * 2.0 + 7) % a.shape[0] / s, (wx * 2.0 + 3) % a.shape[1] / s], order=1, mode="wrap")
cliff = np.zeros((200, 200), bool)
for f, xmax in (("cl11", 126), ("cl15", 200)):
    for c in json.load(open(OUT + f + ".json"))["commands"]:
        if c["cmd"] != "heights": continue
        a = np.array(c["data"]).reshape(c["h"], c["w"], 4)
        for j in range(c["h"]):
            for i in range(c["w"]):
                if c["x0"] + i < xmax and np.abs(a[j, i]).max() > 0.01: cliff[c["x0"] + i, c["y0"] + j] = True
busy = set()
for e in json.load(open(OUT + "an_t.json"))["commands"][0]["list"]:
    if e[2] >= 200: busy.add((e[0], e[1]))                              # dungeon / road cells and black tiles
def load(p): return np.array(Image.open(p).convert("RGB")).astype(float)
GR = load(TEX + "ÇÊµå¹Ù´Ú/prt_ÃÊ¿ø01.bmp"); DIRT = load(TEX + "ÇÊµå¹Ù´Ú/prt_Èë02.bmp"); SAND = DIRT      # original field dirt (not the roo copies)
def tile_at(img, wx, wy):
    u = (wx % 4) / 4 * img.shape[1]; v = (1 - (wy % 4) / 4) * img.shape[0]
    return np.stack([ndi.map_coordinates(img[..., k], [v - 0.5, u - 0.5], order=1, mode="grid-wrap") for k in range(3)], -1)
S_ = lambda t: (lambda u: u * u * (3 - 2 * u))(np.clip(t, 0, 1))
CELL, GUT, ATLW, ATLH, SS = 56, 4, 1024, 960, 2
CELLP = CELL + 2 * GUT
PERA, PERY = ATLW // CELLP, ATLH // CELLP; NC = PERA * PERY
px = ((np.arange(-GUT, CELL + GUT)[:, None] + (np.arange(SS) + 0.5) / SS - 0.5) / (CELL - 1)).ravel(); FX, FY = np.meshgrid(px, 1 - px)   # edge pixel centres on the cube edges
cand = []
for x in range(LX0, 200):
    for y in range(LY0 + 1, LY1 - 1):
        if cliff[x, y] or (x, y) in busy: continue
        r0 = (LY1 - (y + 1)) * Q; c0 = (x - LX0) * Q
        if r0 < 0 or c0 + Q > loop.shape[1]: continue
        if loop[r0:r0 + Q, c0:c0 + Q].any(): cand.append((x, y))
print("beach cubes", len(cand))
def colour(wx, wy):
    sd = sd_at(wx, wy) + 0.35 * noise(NZ1, wx, wy) + 0.16 * noise(NZ2, wx, wy) + 0.06 * noise(NZ3, wx, wy)
    a = S_((sd - 0.1) / 1.5)[..., None]               # grass -> dirt
    b = S_((sd - 0.9) / 1.7)[..., None]               # dirt -> sand
    gr = tile_at(GR, wx, wy); dt = tile_at(DIRT, wx, wy)
    lum = dt.mean(-1, keepdims=True) / DIRT.mean()
    sn = np.clip(np.array([233.0, 214.0, 160.0]) * (1.0 + 0.55 * (lum - 1.0)), 0, 255)      # pale beach sand baked from the dirt's grain
    sn = sn * (0.97 + 0.06 * noise(NZ3, wx * 1.3, wy * 1.3)[..., None] / 0.3)
    mid = gr * 0.35 + dt * 0.65
    col = gr * (1 - a) + mid * a
    col = col * (1 - b) + sn * b
    return col
natl = (len(cand) + NC - 1) // NC
atl = [np.zeros((ATLH, ATLW, 3), np.uint8) for _ in range(natl)]
tiles = []; cells = {}
for n_, (x, y) in enumerate(cand):
    col = colour(x + FX, y + FY).reshape(CELLP, SS, CELLP, SS, 3).mean((1, 3))
    c8 = np.clip(col, 0, 255).astype(np.uint8); cells[(x, y)] = c8[GUT:-GUT, GUT:-GUT]
    k, c = divmod(n_, NC); cy_, cx_ = divmod(c, PERA)
    atl[k][cy_ * CELLP:(cy_ + 1) * CELLP, cx_ * CELLP:(cx_ + 1) * CELLP] = c8
    u0, u1 = (cx_ * CELLP + GUT + 0.5) / ATLW, (cx_ * CELLP + GUT + CELL - 0.5) / ATLW
    v0, v1 = (cy_ * CELLP + GUT + 0.5) / ATLH, (cy_ * CELLP + GUT + CELL - 0.5) / ATLH
    tiles.append([x, y, 300 + k, round(u0, 5), round(v1, 5), round(u1, 5), round(v1, 5), round(u0, 5), round(v0, 5), round(u1, 5), round(v0, 5)])
json.dump({"commands": [{"cmd": "tiles", "list": tiles}]}, open(OUT + "beach_t.json", "w"), separators=(",", ":"))
for k in range(natl):
    Image.fromarray(atl[k]).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(OUT + "prt_novbg%02d.bmp" % (k + 1))
# preview
xs = [c[0] for c in cand]; ys = [c[1] for c in cand]; px0, px1, py0, py1 = min(xs) - 3, max(xs) + 4, min(ys) - 3, max(ys) + 4
pv = np.zeros(((py1 - py0) * CELL, (px1 - px0) * CELL, 3), np.uint8)
for x in range(px0, px1):
    for y in range(py0, py1):
        r0 = (py1 - 1 - y) * CELL; c0 = (x - px0) * CELL
        if (x, y) in cells: pv[r0:r0 + CELL, c0:c0 + CELL] = cells[(x, y)]
        else:
            ff = np.array([112, 128, 48], np.uint8) if not cliff[x, y] else np.array([40, 90, 110], np.uint8)
            pv[r0:r0 + CELL, c0:c0 + CELL] = tile_at(GR, x + FX[::SS, ::SS][:CELL, :CELL] if False else x + (np.arange(CELL) + .5)[None, :].repeat(CELL, 0) / CELL, y + (1 - (np.arange(CELL) + .5) / CELL)[:, None].repeat(CELL, 1)).astype(np.uint8) if not cliff[x, y] else ff
Image.fromarray(pv).resize((pv.shape[1] // 2, pv.shape[0] // 2), Image.LANCZOS).save(OUT + "beach_pv.png")
print("atlases", natl)
