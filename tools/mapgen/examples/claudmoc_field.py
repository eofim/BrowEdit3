"""Terraced field in the prt_fild00 style: two levels joined by ramps, smooth rock-ribbon cliffs, winding dirt paths,
a lake, forest clumps. Textures are the roo_prtf transition set, addressed by index in the open map:
#0 grass, #1 cliff face, #2 dirt edge strip, #3 dirt, #6 dirt blob."""
import sys, os, json
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from rogen import MapGen, _smoothstep
from cliffs import CliffField
import tilelib; tilelib.DESERT = True
TEXDIR = "/mnt/user-data/uploads/ROO_MAP/data/texture/roo_mocf/"

W = H = 150
m = MapGen("claudmoc", W, H, seed=int(os.environ.get("SEED", 5)))
GRASS = m.add_texture("#3"); CLIFF = m.add_texture("#4"); EDGE = m.add_texture("#2")
DIRT = m.add_texture("#0"); BLOB = m.add_texture("#1")
rng = np.random.default_rng(m.seed)

# ---- terrain: two LARGE level regions, joined by ramps; the border between them is a smooth curve
from scipy import ndimage as ndi
STEP = 30.0
SEEDS = np.random.default_rng(m.seed + 100)
def smooth_noise(scale, rg):
    g = rg.random((int((W + 1) / scale) + 3, int((H + 1) / scale) + 3))
    return ndi.zoom(g, scale, order=3)[:W + 1, :H + 1]
z = smooth_noise(26, SEEDS) + 0.5 * smooth_noise(12, SEEDS)
NL = 3
lvl = np.digitize(z, np.quantile(z, [1 / 3, 2 / 3]))
for _ in range(3):                                     # clean up: majority filter
    cnt = np.stack([ndi.uniform_filter((lvl == k).astype(float), 13) for k in range(NL)])
    lvl = cnt.argmax(0)
def drop_small(lvl, min_size):
    lvl = lvl.copy()
    for k in range(NL):
        lab, n = ndi.label(lvl == k)
        for i in range(1, n + 1):
            reg = lab == i
            if reg.sum() < min_size:
                lvl[reg] = -1
    bad = lvl < 0
    if bad.any():
        idx = ndi.distance_transform_edt(bad, return_distances=False, return_indices=True)
        lvl = lvl[idx[0], idx[1]]
    return lvl
for _ in range(2):
    lvl = drop_small(lvl, 350)
# nested level masks; each upper terrace keeps a margin from the cliff below so two cliffs never crowd each other
mk0 = lvl >= 1
mk1 = (lvl >= 2) & (ndi.distance_transform_edt(mk0) > 8)
lab, n = ndi.label(mk1)
for i in range(1, n + 1):
    if (lab == i).sum() < 120:
        mk1[lab == i] = False
lvl = mk0.astype(int) + mk1.astype(int)
CFS = []
for k, mk in enumerate((mk0, mk1)):
    if mk.any() and (~mk).any():
        CFS.append(CliffField(mk, W + 1, H + 1, step=STEP, w=float(os.environ.get("CLIFFW", 3.0)), wt=float(os.environ.get("CLIFFWT", 3.0)), end_tex=TEXDIR + "moc_¸Ç¶¥14.bmp", desert=True, band=(0.16, 0.91), rng=np.random.default_rng(m.seed + 7 + k)))
m.base = sum(c.height for c in CFS)
m.base += (smooth_noise(9, np.random.default_rng(3)) - 0.5) * 0.8        # a little roughness
for c in CFS:
    print("contours", [(round(ct.L), ct.closed, [round(g) for g in ct.gaps]) for ct in c.contours])
LEVEL = sum((c.d > 0).astype(int) for c in CFS)

def region_dist(mask):
    return ndi.distance_transform_edt(np.pad(mask, 1))[1:-1, 1:-1]       # the map edge counts as a border
regions = []
for k in range(NL):
    lab, n = ndi.label(lvl == k)
    for i in range(1, n + 1):
        regions.append(((lab == i).sum(), k, lab == i))
regions.sort(key=lambda r: -r[0])
# ---- lake in the biggest lowest region
low = [r for r in regions if r[1] == 0] or regions[-1:]
_, lk, lmask = max(low, key=lambda r: r[0])
dl = region_dist(lmask)
lu, lv = np.unravel_index(np.argmax(dl - 0.02 * np.abs(np.arange(W + 1)[:, None] - W // 2)), dl.shape)
lrad = float(min(7, dl.max() * 0.5))
L0 = float(lk * STEP)
m.lake(float(lu), float(lv), lrad, 10)
m.water(level=L0 - 3.5, amplitude=0.4)
gxx, gyy = np.meshgrid(np.arange(W + 1), np.arange(H + 1), indexing="ij")
lake_zone = np.hypot(gxx - lu, gyy - lv) < lrad + 7

# ---- paths: winding routes that cross between the levels only through the ramps
from skimage.graph import route_through_array
def route(allow, a, b, extra):
    cost = np.where(allow, 1.0 + extra, 1e6)
    path, c = route_through_array(cost, a, b, fully_connected=True, geometric=True)
    return [tuple(p) for p in path], c
def smooth_path(cells, k=4.0, every=6):
    pts = np.array(cells, dtype=float)
    pts[:, 0] = ndi.gaussian_filter1d(pts[:, 0], k, mode="nearest")
    pts[:, 1] = ndi.gaussian_filter1d(pts[:, 1], k, mode="nearest")
    sel = list(range(0, len(pts), every)) + [len(pts) - 1]
    return [(float(pts[i, 0]), float(pts[i, 1])) for i in sel]
allow = ~lake_zone
for c in CFS:
    allow &= (np.abs(c.d) > c.w / 2 + 4.5) | (c.T < 0.02)    # clear of every rock band, except in the ramps
rockc = np.zeros((W + 1, H + 1), dtype=bool)          # keep paths well away from every bit of rock
for c in CFS:
    for (x_, y_) in c.rock_cubes((0, W), (0, H)):
        rockc[x_:x_ + 2, y_:y_ + 2] = True
allow &= ndi.distance_transform_edt(~rockc) > 4.5
allow[:4, :] = allow[-4:, :] = False; allow[:, :4] = allow[:, -4:] = False
margin = np.minimum.reduce([np.abs(c.d) for c in CFS])
cliffcost = np.clip(7.0 - margin, 0, 7) * 0.5
paths = []
wob = ndi.zoom(rng.random((9, 9)), 17, order=3)[:W + 1, :H + 1] * 14.0           # makes the route meander
top = int(LEVEL.max())
cells_at = {k: np.argwhere(allow & (LEVEL == k) & (margin > 6)) for k in range(top + 1)}
lab_, _n = ndi.label(allow)                            # pick the walkable area that links the most levels
def _score(L):
    lv = [k for k in cells_at if len(cells_at[k]) and (lab_[tuple(cells_at[k].T)] == L).any()]
    return (len(lv), int((lab_ == L).sum()))
Lbest = max(range(1, _n + 1), key=_score)
cells_at = {k: (c[lab_[tuple(c.T)] == Lbest] if len(c) else c) for k, c in cells_at.items()}
A = cells_at[0][int(rng.integers(len(cells_at[0])))]
B = None
for k in range(top, 0, -1):
    hc = cells_at[k]
    if len(hc):
        dB = np.hypot(*(hc - A).T)
        Bc = hc[dB > 40] if (dB > 40).any() else hc
        B = Bc[int(rng.integers(len(Bc)))]
        break
main, mc = route(allow, tuple(A), tuple(B), wob + cliffcost)
assert mc < 1e5, 'no route between the levels'
paths.append(smooth_path(main))
taken = np.zeros((W + 1, H + 1), dtype=bool)
for (cx, cy) in main:
    taken[max(cx - 7, 0):cx + 8, max(cy - 7, 0):cy + 8] = True
# a branch on every level the main path visits, from a point of it to a far cell of the same level
for k in range(top + 1):
    on = [c for c in main if LEVEL[c] == k and margin[c] > 4]
    cells = cells_at[k]
    if len(on) < 12 or not len(cells):
        continue
    st = on[len(on) // 2]
    cand = cells[~taken[tuple(cells.T)]]
    if not len(cand):
        continue
    far = cand[np.argmax(((cand - np.array(st)) ** 2).sum(1))]
    cb, cc_ = route(allow & (LEVEL == k), tuple(st), tuple(far), wob + cliffcost)
    if len(cb) >= 14 and cc_ < 1e5:
        paths.append(smooth_path(cb))
PATHW = 3.2
for pts in paths:
    m.path(pts, width=PATHW, flatten=0.4)

# ---- texturing
m.paint_all(GRASS)
wm = m.masks["water"] > 0.5
ring = wm.copy()
ring[1:, :] |= wm[:-1, :]; ring[:-1, :] |= wm[1:, :]; ring[:, 1:] |= wm[:, :-1]; ring[:, :-1] |= wm[:, 1:]
dirt_cubes = (m.masks["path"] > 0.5) | ring
m.masks["dirt"] = dirt_cubes.astype(float)
# material wanted at every cube corner: 0 grass, 1 dirt
mat = np.zeros((W + 1, H + 1), dtype=int)
cw = np.pad(ring, ((1, 1), (1, 1)))          # corner (x,y) touches cubes (x-1..x, y-1..y)
shore = cw[:-1, :-1] | cw[1:, :-1] | cw[:-1, 1:] | cw[1:, 1:]
gxc, gyc = m._grid()
field = np.full((W + 1, H + 1), -9.0)
for pts in paths:                                   # smooth: distance to the path's centre line, not a cube staircase
    field = np.maximum(field, PATHW / 2.0 - m._dist_to_polyline(pts, gxc, gyc))
wsm = ndi.gaussian_filter(wm[:W, :H].astype(float), 1.5)           # lake shore: a soft, round outline
wsm = np.pad(wsm, ((0, 1), (0, 1)), mode="edge")
field = np.maximum(field, (wsm - 0.18) * 6.0)
dirt_corner = field > 0
from edges import EdgeField
EF = EdgeField(field, W + 1, H + 1, np.random.default_rng(11), u0=0.2, sgn=1.0, lo=0.0, hi=0.62)
mat[EF.dd > 0] = 1
from tilelib import TileLib, render
TEX = {0: "¸Ç¶¥05", 1: "¸Ç¶¥06", 2: "¸Ç¶¥07", 3: "¸Ç¶¥08", 4: "¸Ç¶¥09", 5: "¸Ç¶¥03", 6: "¸Ç¶¥02", 7: "¸Ç¶¥14", 8: "¸Ç¶¥13"}
tex_paths = {i: TEXDIR + "moc_%s.bmp" % n for i, n in TEX.items()}
BANNED = {4, 5, 6, 7, 8}                                  # 초원06/08/09 are not used on paths
lib = TileLib({i: p for i, p in tex_paths.items() if i not in BANNED}, plain={0: 3, 1: 0})
for t in TEX:
    m.add_texture("#%d" % t)
# ---- cliff faces: one continuous UV field along the rock ribbon (no seams, no stretching)
nearp = m.corner_path.copy()
for _ in range(2):
    nn = nearp.copy()
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1), (1, -1), (-1, 1)):
        nn |= np.roll(np.roll(nearp, dx, 0), dy, 1)
    nearp = nn
rockuv = {}
clash = set()
for c in CFS:
    for key, ku in c.rock_cubes((0, W), (0, H), exclude=(m.corner_path & (EF.dd > 1.0)) | shore).items():
        if key in rockuv:
            clash.add(key)
        rockuv[key] = ku
for key in clash:
    del rockuv[key]
print('rock cubes', len(rockuv), 'dropped', sum(c.dropped for c in CFS))
CL = m.add_texture("#4"); CLEND = m.add_texture("#7")
for (x, y), (kind, uv) in rockuv.items():
    m.cube_uv[(x, y)] = (CLEND if kind == "end" else CL, uv)
EDGE = m.add_texture("#2")
edgeuv = EF.edge_cubes((0, W), (0, H), skip=set(rockuv))
from cliffs import cube_stretch
for key in [k for k, uv in edgeuv.items() if cube_stretch(m.base, k[0], k[1], uv) > 2.2]:
    del edgeuv[key]                                   # would smear: leave to the plain dirt / grass tile
for key, uv in edgeuv.items():
    m.cube_uv[key] = (EDGE, uv)
sel = lib.solve(mat, np.random.default_rng(4), fixed=set(rockuv) | set(edgeuv))
for (x, y), (ti, uv) in sel.items():
    m.cube_uv[(x, y)] = (m.add_texture("#%d" % ti), uv)
dirt = dirt_cubes
if os.environ.get("PREVIEW"):
    render(m, tex_paths, 0, 0, W, H, os.environ["PREVIEW"], S=8, seams=bool(os.environ.get("SEAMS")))

# ---- model placement the way prt_fild00 does it: fixed scales, small clusters, a few dense stands
PALM, CAC, CAC1, C4, C5, BONE = "roo_morroc\\tree-2.rsm", "roo_fild\\cactus.rsm", "roo_fild\\cactus1.rsm", "roo_mocf\\c4.rsm", "roo_mocf\\c5.rsm", "roo_izdun\\sc_longgu7.rsm"
rockm = np.zeros((W, H), dtype=bool)
for (x_, y_) in rockuv:
    rockm[max(x_ - 1, 0):x_ + 2, max(y_ - 1, 0):y_ + 2] = True
sl = m.slope_map()
m._update_water_mask(); m._slope = sl
placed_xy = []

def ok(u, v, slope=0.4, gap=2.2):
    if not (3 < u < W - 3 and 3 < v < H - 3):
        return False
    x, y = int(u), int(v)
    if any(m.masks[k][x, y] > 0.5 for k in ("water", "path", "dirt")) or sl[x, y] > slope or rockm[x, y]:
        return False
    return all((u - pu) ** 2 + (v - pv) ** 2 >= gap * gap for pu, pv in placed_xy)

def put(file, u, v, scale):
    m.place(file, u, v, rot=(0, float(rng.uniform(0, 360)), 0), scale=scale, footprint=(0.8, 0.8))
    placed_xy.append((u, v))

def cluster(files, weights, n, radius, scale_of, cu=None, cv=None, gap=2.2):
    for _ in range(40):
        c_u = cu if cu is not None else rng.uniform(6, W - 6)
        c_v = cv if cv is not None else rng.uniform(6, H - 6)
        if ok(c_u, c_v):
            break
    else:
        return 0
    made = 0
    for _ in range(n * 6):
        if made >= n:
            break
        u, v = rng.normal(c_u, radius), rng.normal(c_v, radius)
        if ok(u, v, gap=gap):
            f = files[int(rng.choice(len(files), p=np.array(weights) / sum(weights)))]
            put(f, u, v, scale_of[f])
            made += 1
    return made

SC = {PALM: (0.59, 0.59, -0.59), CAC: (1.59,) * 3, CAC1: (2.03,) * 3, C4: (0.08,) * 3, C5: (0.09,) * 3, BONE: (1.0,) * 3}
# palms ring the oasis
for _ in range(14):
    a_ = rng.uniform(0, 2 * np.pi); r_ = lrad + rng.uniform(2.5, 6)
    u, v = lu + r_ * np.cos(a_), lv + r_ * np.sin(a_)
    if ok(u, v, gap=3.0):
        put(PALM, u, v, SC[PALM])
for _ in range(10):
    cluster([PALM], [1], int(rng.integers(1, 3)), 1.8, SC, gap=3.5)
# cacti in small groups, scattered dry plants and rocks
for _ in range(22):
    cluster([CAC, CAC1], [3, 2], int(rng.integers(1, 4)), 2.2, SC, gap=2.8)
for _ in range(26):
    cluster([C4, C5], [1, 2], int(rng.integers(1, 3)), 1.8, SC, gap=2.5)
for _ in range(6):
    cluster([BONE], [1], 1, 0.1, SC, gap=10)
rim = np.argwhere(rockm)
for _ in range(18):
    if len(rim):
        cx, cy = rim[int(rng.integers(len(rim)))]
        for _ in range(8):
            u, v = cx + rng.uniform(-3, 3), cy + rng.uniform(-3, 3)
            if ok(u, v, slope=1.5, gap=3):
                put(C5 if rng.random() < 0.5 else C4, u, v, SC[C5]); break
m.set_sun(longitude=40, latitude=50, ambient=(0.8, 0.74, 0.64))

cmds = m.live_commands()
cmds.insert(1, {"cmd": "delete_models", "filter": ""})
cmds.append({"cmd": "save"})
out = sys.argv[1] if len(sys.argv) > 1 else "/mnt/user-data/outputs/c005.json"
json.dump({"commands": cmds}, open(out, "w"), separators=(",", ":"))
print(len(m.models), "models,", int(dirt.sum()), "dirt cubes,", len(m.cube_uv), "tile overrides, lake", round(lu), round(lv))
