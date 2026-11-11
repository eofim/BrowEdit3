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
SLOPE = 2.5                                   # slope width in cubes
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
top_gate = S((dF - SLOPE) / 3.0) * S((dB - SLOPE) / 3.0)
Hf = -RISE * S(dF / SLOPE) * S(dB / SLOPE) + top_gate * (2.2 * noise + 0.8 * noise2)
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

# ---- cube classification
cf = floor_w.reshape(W, R, W, R).mean((1, 3)) > 0.5               # floor cube (centre majority)
cmin = np.array([[min(HC[x, y], HC[x + 1, y], HC[x, y + 1], HC[x + 1, y + 1]) for y in range(W)] for x in range(W)])
inblk = (cmin < -0.05) | (block.reshape(W, R, W, R).mean((1, 3)) > 0.5)
cf &= inblk
rows = []
def uvc(x, y, pts):  # world continuous uv for tex 00 / rock
    return [(x + dx) / PER for dx, dy in pts] , [(y + dy) / PER for dx, dy in pts]

tiles = []
def isw(x, y):   # wall = in-block, not floor
    return 0 <= x < W and 0 <= y < W and inblk[x, y] and not cf[x, y]
# uv corner order SW,SE,NW,NE
UV01 = {"N": [(0, 1), (1, 1), (0, 0), (1, 0)], "S": [(1, 0), (0, 0), (1, 1), (0, 1)],
        "E": [(1, 1), (1, 0), (0, 1), (0, 0)], "W": [(0, 0), (0, 1), (1, 0), (1, 1)]}
# 03 / 02: dark at top-left (NW) by default; mirror for other corners
UVNW = [(0, 1), (1, 1), (0, 0), (1, 0)]
UVNE = [(1, 1), (0, 1), (1, 0), (0, 0)]
UVSW = [(0, 0), (1, 0), (0, 1), (1, 1)]
UVSE = [(1, 0), (0, 0), (1, 1), (0, 1)]
cnt = {"00": 0, "01": 0, "02": 0, "03": 0, "rock": 0}
for x in range(W):
    for y in range(W):
        if not inblk[x, y]:
            continue
        if not cf[x, y]:
            pts = [(0, 0), (1, 0), (0, 1), (1, 1)]
            us = [(x + dx) / PER for dx, dy in pts]; vs = [1 - (y + dy) / PER for dx, dy in pts]
            tiles.append([x, y, T00] + [round(q, 4) for pair in zip(us, vs) for q in pair]); cnt["rock"] += 1
            continue
        N_, S_, E_, W_ = isw(x, y + 1), isw(x, y - 1), isw(x + 1, y), isw(x - 1, y)
        nw, ne, sw, se = isw(x - 1, y + 1), isw(x + 1, y + 1), isw(x - 1, y - 1), isw(x + 1, y - 1)
        tex, uv = T00, None
        if (N_ and W_): tex, uv, k = T03, UVNW, "03"
        elif (N_ and E_): tex, uv, k = T03, UVNE, "03"
        elif (S_ and W_): tex, uv, k = T03, UVSW, "03"
        elif (S_ and E_): tex, uv, k = T03, UVSE, "03"
        elif N_: tex, uv, k = T01, UV01["N"], "01"
        elif S_: tex, uv, k = T01, UV01["S"], "01"
        elif E_: tex, uv, k = T01, UV01["E"], "01"
        elif W_: tex, uv, k = T01, UV01["W"], "01"
        elif nw: tex, uv, k = T02, UVNW, "02"
        elif ne: tex, uv, k = T02, UVNE, "02"
        elif sw: tex, uv, k = T02, UVSW, "02"
        elif se: tex, uv, k = T02, UVSE, "02"
        else:
            k = "00"
            pts = [(0, 0), (1, 0), (0, 1), (1, 1)]
            uv = [(((x + dx) / PER) % 1.0 if False else (x + dx) / PER, 1 - (y + dy) / PER) for dx, dy in pts]
        cnt[k] += 1
        tiles.append([x, y, tex] + [round(q, 4) for p in uv for q in p])
tilecmd = {"cmd": "tiles", "list": tiles}

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

json.dump({"commands": [heights]}, open(OUT + "dng_h.json", "w"), separators=(",", ":"))
json.dump({"commands": [tilecmd]}, open(OUT + "dng_t.json", "w"), separators=(",", ":"))
json.dump({"commands": [gatcmd]}, open(OUT + "dng_g.json", "w"), separators=(",", ":"))
print(cnt, len(tiles), len(gat_rows), HC.min(), HC.max())

# preview
pv = np.zeros((W * 6, W * 6, 3), np.uint8)
for x in range(W):
    for y in range(W):
        v = int(np.clip(128 - (HC[x, y] / RISE) * 100, 0, 255))
        col = (v, v, v)
        if cf[x, y]: col = (150, 110, 50)
        pv[(W - 1 - y) * 6:(W - y) * 6, x * 6:(x + 1) * 6] = col
Image.fromarray(pv).save(SCR + "dun_cube_pv.png")
