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

# ---- cube classification + pay_dun00-style tiles
cf = floor_w.reshape(W, R, W, R).mean((1, 3)) > 0.5               # floor cube (centre majority)
cmin = np.array([[min(HC[x, y], HC[x + 1, y], HC[x, y + 1], HC[x + 1, y + 1]) for y in range(W)] for x in range(W)])
bmean = block.reshape(W, R, W, R).mean((1, 3))
inblk = (cmin < -0.05) | (bmean > 0.5)
dFc = corner(dF); dBc = corner(dB)
# smoothed gradients -> wall tangent
dFs = ndi.gaussian_filter(dF, 1.5 * R); dBs = ndi.gaussian_filter(dB, 1.5 * R)
gFx, gFy = np.gradient(dFs); gBx, gBy = np.gradient(dBs)
tiles = []
cnt = {"floor": 0, "slope": 0, "top": 0}
for x in range(W):
    for y in range(W):
        if not inblk[x, y]:
            continue
        cs = [(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)]
        dfs = [dFc[i, j] for i, j in cs]; dbs = [dBc[i, j] for i, j in cs]
        if max(dfs) < 0.05:                                       # flat floor: plain 00, one quarter of the texture per cube
            uv = [(((x % 4) + dx) / 4, 1 - ((y % 4) + dy) / 4) for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]]
            tiles.append([x, y, T00] + [round(q, 4) for p in uv for q in p]); cnt["floor"] += 1
            continue
        if min(dfs) >= SLOPE - 1e-3 and min(dbs) >= SLOPE_B - 1e-3:   # mass top: black corner of 03
            uv = [(0.04 + 0.16 * ((x + dx) % 3) / 3, 0.04 + 0.16 * ((y + dy) % 3) / 3) for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]]
            tiles.append([x, y, T03] + [round(q, 4) for p in uv for q in p]); cnt["top"] += 1
            continue
        # slope: one column of 01 across the slope: v 0 (black, top) -> 1 (floor); outer slope only goes to rock (0.62)
        ci, cj = int((x + 0.5) * R), int((y + 0.5) * R)
        if min(dfs) < SLOPE - 1e-3 and (min(dfs) / SLOPE) <= (min(dbs) / SLOPE_B + 0.0) or min(dbs) >= SLOPE_B:
            gx, gy = gFx[ci, cj], gFy[ci, cj]
        else:
            gx, gy = gBx[ci, cj], gBy[ci, cj]
        nn = np.hypot(gx, gy) or 1.0
        tx, ty = -gy / nn, gx / nn
        base = 0.15 + 0.7 * (((x + 0.5) * tx + (y + 0.5) * ty) * 0.23 % 1.0)
        uv = []
        for (dx, dy), df, db in zip([(0, 0), (1, 0), (0, 1), (1, 1)], dfs, dbs):
            vF = 1 - min(df / SLOPE, 1.0)
            vB = 0.62 * (1 - min(db / SLOPE_B, 1.0))
            v = max(vF, vB)
            u = base + 0.25 * ((dx - 0.5) * tx + (dy - 0.5) * ty)
            uv.append((u, v))
        tiles.append([x, y, T01] + [round(q, 4) for p in uv for q in p]); cnt["slope"] += 1
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

json.dump({"commands": [heights]}, open(OUT + "dng2_h.json", "w"), separators=(",", ":"))
json.dump({"commands": [tilecmd]}, open(OUT + "dng2_t.json", "w"), separators=(",", ":"))
json.dump({"commands": [gatcmd]}, open(OUT + "dng2_g.json", "w"), separators=(",", ":"))
print(cnt, len(tiles), len(gat_rows), HC.min(), HC.max())

# preview
pv = np.zeros((W * 6, W * 6, 3), np.uint8)
for x in range(W):
    for y in range(W):
        v = int(np.clip(128 - (HC[x, y] / RISE) * 100, 0, 255))
        col = (v, v, v)
        if dFc[x, y] < 0.05: col = (150, 110, 50)
        pv[(W - 1 - y) * 6:(W - y) * 6, x * 6:(x + 1) * 6] = col
Image.fromarray(pv).save(SCR + "dun_cube_pv.png")
