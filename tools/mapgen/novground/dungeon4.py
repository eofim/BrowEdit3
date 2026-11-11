"""novground dungeon (lower-left), traced from the user's mock-up (mock_zoom.png).
Technique follows pay_dun00: floor at 0, rock mass raised 30 (positive height = down), 2.5-cube slopes,
bumpy top, cave textures (00 floor / 01 straight edge shadow / 02 outer-corner notch / 03 inner corner).
Writes dng1.json (heights+gat+tiles as separate commands) in /mnt/user-data/outputs."""
import json, sys
import numpy as np
from PIL import Image
from scipy import ndimage as ndi

SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
OUT = "/mnt/user-data/outputs/"
T00, T01, T02, T03 = 23, 22, 20, 21          # texture indices in the map (info job)
RISE = 30.0                                   # rock mass height (positive = down => mass is -RISE)
SLOPE = 4.0                                   # slope width in cubes
PER = 4.0                                     # cubes per texture repeat

# ---- mock-up (zoom image) <-> cube space
def z2c(zx, zy): return (zx - 30) / 12.62, 2.5 + (960 - zy) / 12.07
def c2z(cx, cy): return 30 + 12.62 * cx, 960 - 12.07 * (cy - 2.5)

im = np.array(Image.open(SCR + "mock_zoom.png").convert("RGB")).astype(float)
floor_z = np.load(SCR + "dun_floor_zoom.npy")
r, g, b = im[..., 0], im[..., 1], im[..., 2]
gray = im.mean(2)
loc = np.sqrt(np.maximum(ndi.uniform_filter(gray ** 2, 11) - ndi.uniform_filter(gray, 11) ** 2, 0))
grass = (g > r + 2) & (loc < 9) & (gray > 90)
blk_z = ~grass
blk_z[:, :110] = False; blk_z[:, 960:] = False; blk_z[:150] = False; blk_z[965:] = False
blk_z = ndi.binary_opening(blk_z, iterations=6)
blk_z = ndi.binary_closing(blk_z, iterations=8)
lab, n = ndi.label(blk_z); sz = ndi.sum(blk_z, lab, range(1, n + 1))
blk_z = lab == (1 + int(np.argmax(sz)))
blk_z = ndi.binary_fill_holes(blk_z)
Image.fromarray((blk_z * 255).astype(np.uint8)).save(SCR + "dun_block_zoom.png")

# ---- fine grid (1/4 cube) over cubes 0..90
R = 4
N = 90 * R
fx, fy = np.meshgrid((np.arange(N) + 0.5) / R, (np.arange(N) + 0.5) / R, indexing="ij")   # cube coords
zx, zy = c2z(fx, fy)
def samp(mask, sig):
    m = ndi.gaussian_filter(mask.astype(float), sig)
    return ndi.map_coordinates(m, [zy, zx], order=1, mode="constant", cval=0.0) > 0.5
floor = samp(floor_z, 3.0)
block = samp(blk_z, 5.0)
# entrance corridor joins the west road end (cube ~ (41,71)) -- 4 wide
ent = (np.abs(fx - 42.4) < 2.0) & (fy > 62) & (fy < 75)
floor |= ent & (fy > 62)
floor &= (fx > 4) & (fx < 85)
# one cube of extra width so every passage is >= 3 cubes
dist_to_floor = ndi.distance_transform_edt(~floor) / R
floor_w = dist_to_floor <= 1.0
# block (rock) outline: keep the mouth open
block = block & ~(ent & (fy > 60))
# the corridor mouth runs out of the block, so block must contain it only up to the edge
dB = ndi.distance_transform_edt(block) / R                       # inside block distance to the outer edge
dF = ndi.distance_transform_edt(~floor_w) / R                     # distance from floor (0 inside)
S = lambda t: (lambda u: u * u * (3 - 2 * u))(np.clip(t, 0, 1))
rng = np.random.default_rng(11)
noise = ndi.gaussian_filter(rng.standard_normal((N, N)), 5 * R)
noise = noise / noise.std()
noise2 = ndi.gaussian_filter(rng.standard_normal((N, N)), 2 * R)
noise2 = noise2 / noise2.std()
SLOPE_B = 3.0
top_gate = S((dF - SLOPE) / 3.0) * S((dB - SLOPE_B) / 3.0)
Hf = -RISE * S(dF / SLOPE) * S(dB / SLOPE_B) + top_gate * (2.2 * noise + 0.8 * noise2)
Hf = np.where(block | (dB > 0), Hf, 0.0)
Hf = np.where(floor_w, 0.0, Hf)

# ---- corner heights at integer points, sampled from the fine field
Wm = 91
def corner(arr):
    ii, jj = np.meshgrid(np.arange(Wm), np.arange(Wm), indexing="ij")
    return ndi.map_coordinates(arr, [ii * R - 0.5, jj * R - 0.5], order=1, mode="nearest")
HC = corner(Hf)
HC = np.where(np.abs(HC) < 0.05, 0.0, HC)
HC[0, :] = HC[:, 0] = 0.0
HC[-1, :] = HC[:, -1] = 0.0

# ---- heights command (x0=0,y0=0, w=h=90)
W = 90
data = []
for y in range(W):
    for x in range(W):
        data += [round(float(HC[x, y]), 2), round(float(HC[x + 1, y]), 2), round(float(HC[x, y + 1]), 2), round(float(HC[x + 1, y + 1]), 2)]
heights = {"cmd": "heights", "x0": 0, "y0": 0, "w": W, "h": W, "data": data, "gat": True}

# ---- cube classification + baked atlas transitions (smooth, no stretching)
cmin = np.array([[min(HC[x, y], HC[x + 1, y], HC[x, y + 1], HC[x + 1, y + 1]) for y in range(W)] for x in range(W)])
bmean = block.reshape(W, R, W, R).mean((1, 3))
inblk = (cmin < -0.05) | (bmean > 0.5)
dFc = corner(dF); dBc = corner(dB)
TEX = "/mnt/user-data/uploads/texture/"
def load(p): return np.array(Image.open(p).convert("RGB")).astype(float)
FL = load(TEX + "1\u00c7\u00ca\u00b5\u00e5\u00b9\u00d9\u00b4\u00da/\u00b5\u00bf\u00b1\u00bc\u00b4\u00f8\u00c0\u00fc00.bmp")
RKT = load(TEX + "1\u00c7\u00ca\u00b5\u00e5\u00b9\u00d9\u00b4\u00da/\u00b5\u00bf\u00b1\u00bc\u00b4\u00f8\u00c0\u00fc01.bmp")
GR = load(TEX + "\u00c7\u00ca\u00b5\u00e5\u00b9\u00d9\u00b4\u00da/prt_\u00c3\u00ca\u00bf\u00f801.bmp")
G = 4
FU, FV = (0.03, 0.97), (0.03, 0.97)          # floor 00 span per G cubes
KU, KV = (0.20, 0.80), (0.30, 0.70)          # rock: band of 01
def mir(w, g=G):                              # mirrored triangle coordinate in [0,1]
    gi = np.floor(w / g); l = (w - gi * g) / g
    return np.where(gi % 2 == 0, l, 1 - l)
def tex_at(img, wx, wy, U, V):
    tu = mir(wx); tv = mir(wy)
    u = U[0] + (U[1] - U[0]) * tu; v = V[0] + (V[1] - V[0]) * (1 - tv)
    c = [ndi.map_coordinates(img[..., k], [v * 256 - 0.5, u * 256 - 0.5], order=1, mode="nearest") for k in range(3)]
    return np.stack(c, -1)
# signed distance to the floor edge (negative inside), cubes
dFin = ndi.distance_transform_edt(floor_w) / R
SD = dF - dFin
Zf = -Hf / 10.0
gzx, gzy = np.gradient(Zf, 1.0 / R)
nrm = np.sqrt(gzx ** 2 + gzy ** 2 + 1)
Lv = np.array([-0.45, 0.55, 0.70]); Lv /= np.linalg.norm(Lv)
dotl = (-gzx * Lv[0] - gzy * Lv[1] + Lv[2]) / nrm
shade_f = 1 + 0.85 * (dotl - Lv[2])
def fld(a, wx, wy): return ndi.map_coordinates(a, [wx * R - 0.5, wy * R - 0.5], order=1, mode="nearest")
nz1 = ndi.gaussian_filter(rng.standard_normal((N, N)), 0.5 * R); nz1 /= nz1.std()
nz2 = ndi.gaussian_filter(rng.standard_normal((N, N)), 0.25 * R); nz2 /= nz2.std()
smooth = lambda t: (lambda u: u * u * (3 - 2 * u))(np.clip(t, 0, 1))

CELL, ATLW, ATLH = 40, 1024, 512                     # 1024x512 8-bit BMP = ~0.5 MB each
ATL = ATLW
PERA = ATLW // CELL                                  # cells per row
PERY = ATLH // CELL
NC = PERA * PERY
cs_ = (np.arange(CELL) + 0.5) / CELL
FXc, FYc = np.meshgrid(cs_, 1 - cs_)                 # row 0 = north
SDc = corner(SD)
baked = []        # list of (x, y)
plain = []        # (x, y, kind)
for x in range(W):
    for y in range(W):
        if not inblk[x, y]:
            continue
        sds = [SDc[x, y], SDc[x + 1, y], SDc[x, y + 1], SDc[x + 1, y + 1]]
        dbs = [dBc[x, y], dBc[x + 1, y], dBc[x, y + 1], dBc[x + 1, y + 1]]
        if max(sds) <= -1.2 and min(dbs) > 1.5:
            plain.append((x, y, "floor"))
        elif min(sds) >= SLOPE + 0.5 and min(dbs) >= SLOPE_B + 0.5:
            plain.append((x, y, "top"))
        else:
            baked.append((x, y))
natl = (len(baked) + NC - 1) // NC
atl = [np.zeros((ATLH, ATLW, 3), np.uint8) for _ in range(natl)]
T0 = 24                                              # first atlas texture index (after the 24 existing ones)
tiles = []
for n_, (x, y) in enumerate(baked):
    wx = x + FXc; wy = y + FYc
    sd = fld(SD, wx, wy); db = fld(dB, wx, wy); sh = fld(shade_f, wx, wy)
    n1 = fld(nz1, wx, wy); n2 = fld(nz2, wx, wy)
    fl = tex_at(FL, wx, wy, FU, FV); rk = tex_at(RKT, wx, wy, KU, KV)
    gr = tex_at(GR, wx, wy, (0.0, 1.0), (0.0, 1.0))
    wr = smooth((sd + 0.12 + 0.30 * n1 + 0.10 * n2) / 0.85)             # floor -> rock, ragged natural foot of the wall
    col = fl * (1 - wr[..., None]) + rk * wr[..., None]
    ao = 1 - 0.42 * np.exp(-((sd - 0.30) / 0.75) ** 2)                  # soft contact shadow around the wall base
    col = col * ao[..., None] * np.clip(sh, 0.45, 1.3)[..., None]
    dfo = np.clip(fld(dF, wx, wy), 0, None)
    wg = smooth((0.85 - db + 0.30 * n1) / 1.0) * smooth(dfo / 1.2)      # outer foot of the rock blends into the grass
    col = col * (1 - wg[..., None]) + gr * wg[..., None]
    k, c = divmod(n_, NC); cy_, cx_ = divmod(c, PERA)
    atl[k][cy_ * CELL:(cy_ + 1) * CELL, cx_ * CELL:(cx_ + 1) * CELL] = np.clip(col, 0, 255).astype(np.uint8)
    eu, ev = 0.5 / ATLW, 0.5 / ATLH
    u0, u1 = (cx_ * CELL) / ATLW + eu, ((cx_ + 1) * CELL) / ATLW - eu
    v0, v1 = (cy_ * CELL) / ATLH + ev, ((cy_ + 1) * CELL) / ATLH - ev
    tiles.append([x, y, T0 + k, round(u0, 5), round(v1, 5), round(u1, 5), round(v1, 5), round(u0, 5), round(v0, 5), round(u1, 5), round(v0, 5)])
CORN = [(0, 0), (1, 0), (0, 1), (1, 1)]
def pl(x, y, U, V):
    pts = []
    for dx, dy in CORN:
        tu = mir(np.array(x + dx, float)); tv = mir(np.array(y + dy, float))
        gx = x // G; gy = y // G
        lx = (x + dx) - gx * G; ly = (y + dy) - gy * G
        tu = lx / G if gx % 2 == 0 else 1 - lx / G
        tv = ly / G if gy % 2 == 0 else 1 - ly / G
        pts.append((U[0] + (U[1] - U[0]) * tu, V[0] + (V[1] - V[0]) * (1 - tv)))
    return [round(float(q), 5) for p in pts for q in p]
for x, y, kind in plain:
    tiles.append([x, y, T00 if kind == "floor" else T01] + (pl(x, y, FU, FV) if kind == "floor" else pl(x, y, KU, KV)))
for k in range(natl):
    im = atl[k]
    Image.fromarray(im).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(OUT + "prt_novdun%02d.bmp" % (k + 1))
    Image.fromarray(im).save(SCR + "novdun%02d.png" % (k + 1))
tilecmd = {"cmd": "tiles", "list": tiles}
print("baked", len(baked), "plain", len(plain), "atlases", natl)

# ---- gat: 2x2 cells per cube. floor walkable, rock mass / slopes blocked, only inside the block outline
gx, gy = np.meshgrid((np.arange(2 * W) + 0.5) / 2, (np.arange(2 * W) + 0.5) / 2, indexing="ij")
gi = np.clip((gx * R).astype(int), 0, N - 1); gj = np.clip((gy * R).astype(int), 0, N - 1)
walk = floor_w[gi, gj]
rock = (Hf[gi, gj] < -0.5) & ~walk
gat_rows = []
for y in range(2 * W):
    for typ, m in ((1, rock[:, y]), (0, walk[:, y])):
        xs = np.flatnonzero(m)
        if not len(xs): continue
        s = xs[0]; p = xs[0]
        for q in xs[1:]:
            if q != p + 1:
                gat_rows.append([y, int(s), int(p) + 1, typ]); s = q
            p = q
        gat_rows.append([y, int(s), int(p) + 1, typ])
gatcmd = {"cmd": "gat", "rows": gat_rows}

json.dump({"commands": [heights]}, open(OUT + "dng4_h.json", "w"), separators=(",", ":"))
json.dump({"commands": [tilecmd]}, open(OUT + "dng4_t.json", "w"), separators=(",", ":"))
json.dump({"commands": [gatcmd]}, open(OUT + "dng4_g.json", "w"), separators=(",", ":"))
print(len(tiles), len(gat_rows), HC.min(), HC.max())

# preview
pv = np.zeros((W * 6, W * 6, 3), np.uint8)
for x in range(W):
    for y in range(W):
        v = int(np.clip(128 - (HC[x, y] / RISE) * 100, 0, 255))
        col = (v, v, v)
        if dFc[x, y] < 0.05: col = (150, 110, 50)
        pv[(W - 1 - y) * 6:(W - y) * 6, x * 6:(x + 1) * 6] = col
Image.fromarray(pv).save(SCR + "dun_cube_pv.png")
