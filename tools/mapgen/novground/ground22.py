"""novground v2: natural meandering dirt paths (varying width, forks, wear patches) on grass; only tiles are written."""
import sys, os, json
import numpy as np
from scipy import ndimage as ndi
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import tilelib
from tilelib import TileLib
from edges import EdgeField
from PIL import Image

W = H = 200; Wc = Hc = 201
IMG_W, IMG_H = 1250.0, 1160.0
def P(px, py): return (px / IMG_W * W, (1 - py / IMG_H) * H)
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
rng = np.random.default_rng(int(os.environ.get("SEED", 21)))

CAMP = P(625, 598)
ROADS = {   # control points (reference pixels); every road starts inside the plaza
 "north": [(625,598),(630,500),(640,420),(622,350),(640,290),(655,230),(640,180),(628,150)],
 "mine":  [(631,388),(592,382),(548,372),(504,368)],
 "west":  [(625,598),(560,590),(490,572),(420,578),(345,590),(292,625),(265,690),(255,750)],
 "east":  [(625,598),(710,595),(780,578),(860,566),(930,556),(960,530),(956,470),(946,430),(945,405)],
 "eastsouth": [(957,500),(958,545),(962,620),(984,700),(996,770)],
 "south": [(625,598),(622,680),(632,760),(626,830),(596,900),(566,985),(570,1058),(618,1090),(692,1088),(738,1048),(750,985),(736,925),(702,880),(658,850),(630,820)],
}
cx, cy = np.meshgrid(np.arange(Wc), np.arange(Hc), indexing="ij")
def noise1(n, scale, rg):
    a = rg.random(n // scale + 4)
    return ndi.zoom(a, scale, order=3)[:n] - 0.5
def resample(pts, step=0.7):
    p = np.array(pts, float); out = [p[0]]
    for a, b in zip(p[:-1], p[1:]):
        L = np.hypot(*(b - a)); n = max(int(L / step), 1)
        for t in np.arange(1, n + 1) / n: out.append(a * (1 - t) + b * t)
    return np.array(out)
def smooth(p, passes=10):
    p = p.copy()
    for _ in range(passes): p[1:-1] = (p[:-2] + 2 * p[1:-1] + p[2:]) / 4
    return p
def draws(rg, n):
    return dict(a=noise1(n, 55, rg), b=noise1(n, 18, rg), p1=rg.uniform(32, 46), p2=rg.uniform(0, 6), p3=rg.uniform(14, 20), p4=rg.uniform(0, 6),
                c=noise1(n, 50, rg), p5=rg.uniform(14, 22), p6=rg.uniform(0, 6))
rng_mine = np.random.default_rng(7)
OLD_MINE = [(628, 330), (600, 290), (560, 268), (520, 250)]
field = np.full((Wc, Hc), -9.0)
centre_lines = []
for name, pts in ROADS.items():
    q = smooth(resample([P(*a) for a in pts], 1.0), 6)
    q = resample(q, 0.6)
    n = len(q)
    t = np.gradient(q, axis=0); t /= np.linalg.norm(t, axis=1, keepdims=True) + 1e-9
    nrm = np.stack([-t[:, 1], t[:, 0]], 1)
    # meander: slow wander plus a finer wobble, tapered to 0 where the road joins the plaza
    sarc = np.arange(n) * 0.6
    if name == "mine":                                  # keep the other roads exactly as they were: burn the draws the old branch used
        qo = resample(smooth(resample([P(*a) for a in OLD_MINE], 1.0), 6), 0.6); draws(rng, len(qo)); D = draws(rng_mine, n)
    else: D = draws(rng, n)
    off = D["a"] * 11.0 + D["b"] * 2.0 + 2.6 * np.sin(sarc * 2 * np.pi / D["p1"] + D["p2"]) + 1.2 * np.sin(sarc * 2 * np.pi / D["p3"] + D["p4"])
    ramp = np.clip(np.hypot(q[:, 0] - CAMP[0], q[:, 1] - CAMP[1]) / 14.0 - 0.7, 0, 1)
    q = q + nrm * (off * ramp)[:, None]
    q = smooth(q, 6)
    w0 = 3.8 if name in ("north", "west", "east", "south") else 3.2
    wid = w0 * (1.0 + D["c"] * 1.1 + 0.18 * np.sin(np.arange(n) * 0.6 * 2 * np.pi / D["p5"] + D["p6"]))
    wid = np.clip(wid, w0 * 0.8, w0 * 1.15)
    if name == "north":                                   # narrow the last stretch (where it meets the cliff ramp) so it never touches the cliff
        yy = q[:, 1]
        f = 1.0 - (1.0 - float(os.environ.get("NARROW", 0.45))) * np.clip((yy - 140.0) / 12.0, 0, 1)
        wid = wid * f
        q = q.copy(); q[:, 0] -= float(os.environ.get('SHIFT', 0.9)) * np.clip((yy - 140.0) / 12.0, 0, 1)
        sel = (yy > 150) & (yy < 176); print("north centre x at y>150:", np.round(q[sel][::6], 1).tolist())
    centre_lines.append((q, wid))
    for (x, y), w in zip(q, wid):                         # distance field = union of discs along the centreline
        x0, x1 = int(max(x - w - 2, 0)), int(min(x + w + 3, Wc)); y0, y1 = int(max(y - w - 2, 0)), int(min(y + w + 3, Hc))
        d = np.hypot(cx[x0:x1, y0:y1] - x, cy[x0:x1, y0:y1] - y)
        field[x0:x1, y0:y1] = np.maximum(field[x0:x1, y0:y1], w / 2 - d)
# round, generous end caps so dead ends never pinch into a distorted point
for q, wid in centre_lines:
    for e in (q[0], q[-1]):
        if np.hypot(e[0] - CAMP[0], e[1] - CAMP[1]) < 6: continue
        d = np.hypot(cx - e[0], cy - e[1]); field = np.maximum(field, (1.7 if e[1] > 150 else 2.4) - d)
# worn patches / small widenings next to the roads (natural trampled spots)
for q, wid in centre_lines:
    for _ in range(max(2, len(q) // 40)):
        i = int(rng.integers(len(q))); a = rng.random() * 6.28
        px_, py_ = q[i] + 2.4 * np.array([np.cos(a), np.sin(a)]); r = 1.1 + rng.random() * 1.2
        d = np.hypot(cx - px_, cy - py_); field = np.maximum(field, r - d + 0.3)
# dirt clearing at the end of the north road (traced from the user's red guide)
from skimage.draw import polygon as _poly
CLEAR = np.array([[98.33, 170.01], [96.74, 171.0], [94.96, 173.11], [92.97, 174.1], [90.99, 175.45], [90.27, 177.55], [90.59, 179.34], [91.07, 180.13], [90.67, 181.12], [90.47, 182.51], [91.39, 183.9], [92.97, 184.58], [94.96, 184.81], [97.34, 184.81], [98.85, 185.17], [100.12, 186.48], [101.11, 187.35], [101.9, 187.55], [103.29, 186.8], [104.88, 185.89], [106.47, 185.17], [108.05, 184.89], [110.04, 184.62], [111.62, 183.9], [112.74, 182.71], [112.93, 181.12], [112.42, 179.54], [111.23, 177.75], [109.64, 176.36], [107.85, 175.05], [106.07, 174.3], [103.69, 174.26], [101.9, 174.18], [100.91, 173.39], [100.71, 172.0], [101.11, 170.81], [101.43, 170.09]])
_cm = np.zeros((Wc, Hc), bool); _rr, _cc = _poly(CLEAR[:, 0], CLEAR[:, 1], _cm.shape); _cm[_rr, _cc] = True
_cf = ndi.gaussian_filter(ndi.distance_transform_edt(_cm) - ndi.distance_transform_edt(~_cm), 0.8)
field = np.maximum(field, _cf)
# irregular plaza
ang = np.arctan2(cy - CAMP[1], cx - CAMP[0])
rad = 14.5 + 1.8 * np.sin(3 * ang + 0.7) + 1.1 * np.sin(5 * ang + 2.0) + 0.7 * np.sin(8 * ang)

wb = ndi.zoom(rng.random((Wc // 10 + 4, Hc // 10 + 4)), 10, order=3)[:Wc, :Hc] - 0.5
wb2 = ndi.zoom(rng.random((Wc // 3 + 4, Hc // 3 + 4)), 3, order=3)[:Wc, :Hc] - 0.5
field = np.where(field > -3.5, field + 1.0 * wb + 0.5 * wb2, field)     # irregular, hand-worn edges
field = ndi.gaussian_filter(field, 0.7)
# --- clean mask: fill holes/gaps, drop thin necks and specks, then rebuild a smooth signed-distance field
_m = field > -0.7
_st8 = np.ones((3, 3), bool)
_m = ndi.binary_closing(_m, _st8, iterations=1)
_m = ndi.binary_closing(_m, np.ones((1, 9), bool)) | ndi.binary_closing(_m, np.ones((9, 1), bool)) | _m
_hl, _hn = ndi.label(~_m, ndi.generate_binary_structure(2, 1))
for _i in range(1, _hn + 1):
    if (_hl == _i).sum() < 25: _m[_hl == _i] = True
_lab, _n = ndi.label(_m, _st8)
for _i in range(1, _n + 1):
    if (_lab == _i).sum() < 150: _m[_lab == _i] = False
# --- east road redrawn from the user's red outline (traced on a top-down screenshot, cube coords)
RR = np.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "road_red_cube.npy"))
_pm = np.zeros((Wc, Hc), bool); _r2, _c2 = _poly(RR[:, 0], RR[:, 1], _pm.shape); _pm[_r2, _c2] = True
XCUT = 126.5
_reg = np.zeros((Wc, Hc), bool); _reg[int(XCUT):, 40:150] = True
_old = _m.copy()
_m = np.where(_reg, _pm, _m | _pm)
print("east road override: cells", int(_pm.sum()), "old cells removed", int((_old & _reg & ~_pm).sum()))
for _x in (124, 125, 126, 127, 128):
    _ys = np.flatnonzero(_m[_x, 95:115]) + 95; print("seam x", _x, (_ys.min(), _ys.max()) if len(_ys) else None)
field = ndi.gaussian_filter(ndi.distance_transform_edt(_m) - ndi.distance_transform_edt(~_m), 1.1)
field = np.clip(field, -4, 4)
EF = EdgeField(field, Wc, Hc, np.random.default_rng(11), lo=-0.04, straddle=True)
mat = np.zeros((Wc, Hc), int); mat[EF.dd > 0] = 1
st = ndi.generate_binary_structure(2, 1)
m_ = ndi.binary_closing(mat == 1, st, iterations=1); m_ = ndi.binary_opening(m_, st, iterations=1)
mat = m_.astype(int)

def classify_hd(img):        # the HD client textures: olive grass (r<g) vs golden dirt (r>>g)
    a = np.asarray(img.convert("RGB"), dtype=int)
    d = ndi.gaussian_filter((a[..., 0] - a[..., 1]).astype(float), 2.5, mode="wrap")
    return (d > 13).astype(np.int8)
tilelib.classify = classify_hd
td = "/mnt/user-data/uploads/texture/ÇÊµå¹Ù´Ú/"
PLAIN_DIRT = "/mnt/user-data/uploads/ROO_MAP/data/texture/roo_prtf/prt_Èë03.bmp"
names = {0: "prt_ÃÊ¿ø01.bmp", 1: "prt_ÃÊ¿ø02.bmp", 2: "prt_ÃÊ¿ø03.bmp", 3: "prt_ÃÊ¿ø04.bmp", 4: "prt_ÃÊ¿ø05.bmp"}
lib = TileLib({**{i: td + n for i, n in names.items()}, 5: PLAIN_DIRT}, plain={0: 0, 1: 5})
sel = lib.solve(mat, np.random.default_rng(4), fixed=set(), noise=float(os.environ.get("NOISE", 0.2)))
edgeuv = {} if os.environ.get("TILES_ONLY","1")=="1" else EF.edge_cubes((0, W), (0, H), skip=set())
cxi0, cyi0 = int(CAMP[0]) - 15, int(CAMP[1]) - 15
R_IN, R_OUT = 11.8, 14.6          # stone plaza: rosette tiles inside, brick ring around
DT = ndi.distance_transform_edt(field > 0); THK = float(os.environ.get('THK', 2.2))
_gx, _gy = np.meshgrid(np.arange(W) + 0.5, np.arange(H) + 0.5, indexing="ij")
_dcm = np.hypot(_gx - CAMP[0], _gy - CAMP[1])
OUTER = ndi.gaussian_filter((_dcm <= R_OUT).astype(float), 1.0) > 0.5      # slightly eased circle: no one-cube nubs
INNER = ndi.gaussian_filter((_dcm <= R_IN).astype(float), 1.0) > 0.5
# ---------- smooth plaza: custom-painted rim tiles (atlas PNGs) ----------
R1, R2 = 12.2, 15.0               # nominal radii (cube units): rosette | brick ring | grass
CELL, ATL = 64, 1024              # 64 px per cube, 16x16 cells per 1024 px atlas
from scipy.ndimage import map_coordinates as _mc
BX0, BY0 = CAMP[0] - 24.0, CAMP[1] - 24.0
def vnoise(cell, seed):             # smooth world-continuous noise, unit-ish variance, wavelength ~ cell (cubes)
    g = np.random.default_rng(seed).normal(size=(int(48 / cell) + 6, int(48 / cell) + 6))
    g = ndi.gaussian_filter(g, 0.55); g /= g.std()
    return lambda wx, wy: _mc(g, [(wx - BX0) / cell + 2, (wy - BY0) / cell + 2], order=3, mode="nearest")
NL, NM, NF, NI1, NI2 = vnoise(9.0, 101), vnoise(3.0, 102), vnoise(0.7, 103), vnoise(5.0, 104), vnoise(0.9, 105)
NG = vnoise(0.35, 106)
def _ld(p, n=None):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float32)
T_GRASS = _ld(td + names[0]); T_DIRT = _ld(PLAIN_DIRT)
T_ROS = _ld("/mnt/user-data/uploads/texture/prt/prt_y_bot01.png"); T_BRK = _ld("/mnt/user-data/uploads/texture/prt/prt_y_bot02.bmp")
def samp(T, tu, tv):                      # bilinear, wrap
    h, w = T.shape[:2]; fx = (tu % 1.0) * w - 0.5; fy = (tv % 1.0) * h - 0.5
    x0 = np.floor(fx).astype(int); y0 = np.floor(fy).astype(int); ax = (fx - x0)[..., None]; ay = (fy - y0)[..., None]
    g = lambda yy, xx: T[yy % h, xx % w]
    return (g(y0, x0) * (1 - ax) + g(y0, x0 + 1) * ax) * (1 - ay) + (g(y0 + 1, x0) * (1 - ax) + g(y0 + 1, x0 + 1) * ax) * ay
def sdist(x, y):                          # nearest / farthest distance from CAMP to cube square
    nx = min(max(CAMP[0], x), x + 1); ny = min(max(CAMP[1], y), y + 1)
    fx = x if abs(CAMP[0] - x) > abs(CAMP[0] - x - 1) else x + 1; fy = y if abs(CAMP[1] - y) > abs(CAMP[1] - y - 1) else y + 1
    return np.hypot(nx - CAMP[0], ny - CAMP[1]), np.hypot(fx - CAMP[0], fy - CAMP[1])
SR, SB = 3.6, 2.4                         # cubes per texture repeat (rosette, brick): ~20% bigger
ZI, ZO = 1.5, 1.6                          # transition half-widths: inner (rosette|brick), outer (brick|grass)
RIM = []
REG = {}
for x in range(W):
    for y in range(H):
        dmin, dmax = sdist(x, y)
        if dmax <= R1 - ZI: REG[(x, y)] = 6
        elif dmin >= R2 + ZO: pass
        elif dmin >= R1 + ZI and dmax <= R2 - ZO: REG[(x, y)] = 7
        else: RIM.append((x, y))
NCELL = (ATL // CELL) ** 2
natl = (len(RIM) + NCELL - 1) // NCELL
atlas = [np.zeros((ATL, ATL, 3), np.uint8) for _ in range(max(natl, 4))]
px = (np.arange(CELL) + 0.5) / CELL; FX, FY = np.meshgrid(px, 1 - px)     # FY: image row 0 = north
RIMUV = {}
SM = lambda t: np.clip(t, 0, 1) ** 2 * (3 - 2 * np.clip(t, 0, 1))
for n_, (x, y) in enumerate(RIM):
    wx, wy = x + FX, y + FY
    r = np.hypot(wx - CAMP[0], wy - CAMP[1])
    # irregular, broken-up edges (world-continuous noise, so neighbouring cells match)
    ro = R2 + 0.22 * NL(wx, wy) + 0.08 * NM(wx, wy)       # outer edge, wanders +-1.5 cubes
    ri = R1                                                                    # rosette circle stays a perfect circle
    a2 = SM((ro - r) / 0.9 + 0.5 + 0.07 * NG(wx, wy))                          # wide soft stone -> ground fade, grainy
    a1 = SM((ri - r) / 0.9 + 0.5 + 0.04 * NG(wx, wy))
    i3, j3 = (x - cxi0) % 3, (y - cyi0) % 3; i2, j2 = (x - cxi0) % 2, (y - cyi0) % 2; i4, j4 = x % 4, y % 4
    ros = samp(T_ROS, (wx - cxi0) / SR, 1 - (wy - cyi0) / SR)
    brk = samp(T_BRK, (wx - cxi0) / SB, 1 - (wy - cyi0) / SB)
    gr = samp(T_GRASS, (i4 + FX) / 4, 1 - (j4 + FY) / 4)
    dt = samp(T_DIRT, (i4 + FX) / 4, 1 - (j4 + FY) / 4)
    fcorn = field[x:x + 2, y:y + 2]
    fi = (fcorn[0, 0] * (1 - FX) + fcorn[1, 0] * FX) * (1 - FY) + (fcorn[0, 1] * (1 - FX) + fcorn[1, 1] * FX) * FY
    ad = SM(fi / 0.6 + 0.5)
    # worn, dusty earth just outside the stones, thinning out into grass
    beyond = r - ro
    wd = SM(1.0 - beyond / 1.3) * SM(beyond / 0.5 + 0.8) * np.clip(0.30 + 0.10 * NM(wx + 40, wy), 0, 0.5)
    ground = gr * (1 - np.maximum(ad, wd)[..., None]) + dt * np.maximum(ad, wd)[..., None]
    stone = ros * a1[..., None] + brk * (1 - a1[..., None])
    # stone fades into the ground; the brick ring is also dusted with soil near its outer edge
    dust = SM((r - (ro - 0.9)) / 0.9) * 0.15 * (0.6 + 0.4 * NM(wx, wy + 40))
    stone = stone * (1 - dust[..., None]) + dt * dust[..., None]
    img = stone * a2[..., None] + ground * (1 - a2[..., None])
    k, c = divmod(n_, NCELL); cy_, cx_ = divmod(c, ATL // CELL)
    atlas[k][cy_ * CELL:(cy_ + 1) * CELL, cx_ * CELL:(cx_ + 1) * CELL] = np.clip(img, 0, 255).astype(np.uint8)
    e = 0.5 / ATL
    u0, u1 = (cx_ * CELL) / ATL + e, ((cx_ + 1) * CELL) / ATL - e
    v0, v1 = (cy_ * CELL) / ATL + e, ((cy_ + 1) * CELL) / ATL - e          # v0 = north edge of the cell
    RIMUV[(x, y)] = (11 + k, [(u0, v1), (u1, v1), (u0, v0), (u1, v0)])      # SW SE NW NE
for k, im in enumerate(atlas):
    Image.fromarray(im).save("/mnt/user-data/outputs/prt_novplaza%02d.png" % (k + 1))
    Image.fromarray(im).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save("/mnt/user-data/outputs/prt_novplaza%02d.bmp" % (k + 1))
print("rim cubes", len(RIM), "atlases", len(atlas))
tiles, used = [], {}
cube_uv = {}
for x in range(W):
    for y in range(H):
        ddc = EF.dd[x:x + 2, y:y + 2]
        FULL = [(0, 1), (1, 1), (0, 0), (1, 0)]
        if ddc.min() >= 0: ti, uv = 5, FULL
        elif ddc.max() <= 0: ti, uv = 0, FULL
        elif (x, y) in edgeuv: uv, ti = edgeuv[(x, y)], 1
        else: ti, uv = (5, FULL) if ddc.mean() > 0 else (0, FULL)      # rare: contour too curved for the strip -> plain majority
        if ti in (0, 5) and len(set((round(a, 3), round(b, 3)) for a, b in uv)) == 4 and (max(a for a, b in uv) - min(a for a, b in uv)) > 0.9:
            i4, j4 = x % 4, y % 4          # plain fill: 1/4 slice of the texture per cube -> grain 4x bigger, seamless over 4 cubes
            uv = [(i4 / 4, 1 - j4 / 4), ((i4 + 1) / 4, 1 - j4 / 4), (i4 / 4, 1 - (j4 + 1) / 4), ((i4 + 1) / 4, 1 - (j4 + 1) / 4)]
        if (x, y) in RIMUV: ti, uv = RIMUV[(x, y)]
        elif (x, y) in REG:
            ti = REG[(x, y)]
            S_ = SR if ti == 6 else SB
            fx0, fy0 = (x - cxi0) / S_, (y - cyi0) / S_
            u0_ = fx0 - np.floor(fx0); u1_ = u0_ + 1.0 / S_
            vs_ = fy0 - np.floor(fy0)
            vh1, vh3 = 1 - vs_, 1 - vs_ - 1.0 / S_
            if vh3 < 0: vh1, vh3 = vh1 + 1, vh3 + 1
            uv = [(u0_, vh1), (u1_, vh1), (u0_, vh3), (u1_, vh3)]
        used[ti] = used.get(ti, 0) + 1; cube_uv[(x, y)] = (ti, uv)
        tiles.append([x, y, ti] + [round(c, 4) for (a, b) in uv for c in (a, b)])
print("tiles", len(tiles), used, "dirt cubes", int((mat[:W, :H] == 1).sum()))
json.dump({"commands": [{"cmd": "tiles", "list": tiles}]}, open("/mnt/user-data/outputs/ng29.json", "w"), separators=(",", ":"))
# previews: schematic and textured render
pv = np.zeros((W, H, 3), np.uint8); pv[:] = (110, 170, 50); pv[mat[:W, :H] == 1] = (225, 205, 150)
Image.fromarray(pv.transpose(1, 0, 2)[::-1]).resize((600, 600), Image.NEAREST).save(SCR + "ng_pv.png")
S = 10
tex = {i: np.asarray(Image.open(td + n).convert("RGB").resize((128, 128))) for i, n in names.items()}
tex[5] = np.asarray(Image.open(PLAIN_DIRT).convert("RGB").resize((128, 128)))
tex[6] = np.asarray(Image.open('/mnt/user-data/uploads/texture/prt/prt_y_bot01.png').convert('RGB').resize((128, 128)))
for _k in range(len(atlas)): tex[11 + _k] = np.asarray(Image.open('/mnt/user-data/outputs/prt_novplaza%02d.png' % (_k + 1)).convert('RGB').resize((128, 128)))
tex[7] = np.asarray(Image.open('/mnt/user-data/uploads/texture/prt/prt_y_bot02.bmp').convert('RGB').resize((128, 128)))
def render(x0, y0, w, h, out):
    img = np.zeros((h * S, w * S, 3), np.uint8)
    a = (np.arange(S) + 0.5) / S; fa, fb = np.meshgrid(a, a)
    for x in range(x0, x0 + w):
        for y in range(y0, y0 + h):
            ti, uv = cube_uv[(x, y)]
            u = [c[0] for c in uv]; v = [c[1] for c in uv]
            uu = (u[0] * (1 - fa) + u[1] * fa) * (1 - fb) + (u[2] * (1 - fa) + u[3] * fa) * fb
            vv = (v[0] * (1 - fa) + v[1] * fa) * (1 - fb) + (v[2] * (1 - fa) + v[3] * fa) * fb
            px = np.clip(((uu % 1.0) * 128).astype(int), 0, 127); py = np.clip(((vv % 1.0) * 128).astype(int), 0, 127)
            r = (h - 1 - (y - y0)) * S
            img[r:r + S, (x - x0) * S:(x - x0 + 1) * S] = tex[ti][py[::-1], px]
    Image.fromarray(img).save(out)
cxi, cyi = int(CAMP[0]), int(CAMP[1])
render(cxi - 40, cyi - 25, 80, 50, SCR + "ng_r1.png")
S = 24
render(int(CAMP[0]) - 36, int(CAMP[1]) - 4, 26, 10, SCR + "ng_r2.png")
print("edge cubes", len(edgeuv))
S = 40
render(int(CAMP[0]) - 34, int(CAMP[1]) - 4, 18, 10, SCR + "ng_r3.png")
if os.environ.get("DEBUG"):
    ks = sorted(k for k in edgeuv if abs(k[1] - (int(CAMP[1]) + 0)) < 25 and k[0] < int(CAMP[0]) - 20)
    for k in ks[:16]:
        print(k, [(round(a, 2), round(b, 2)) for a, b in edgeuv[k]], [round(float(EF.dd[k[0] + i, k[1] + j]), 2) for i, j in ((0, 0), (1, 0), (0, 1), (1, 1))])

# accurate plaza preview straight from the atlases
Sx = 24; x0, y0, w_, h_ = int(CAMP[0]) - 19, int(CAMP[1]) - 19, 38, 38
big = np.zeros((h_ * Sx, w_ * Sx, 3), np.uint8)
at = [np.asarray(Image.open('/mnt/user-data/outputs/prt_novplaza%02d.png' % (k + 1)).convert('RGB')) for k in range(len(atlas))]
for x in range(x0, x0 + w_):
    for y in range(y0, y0 + h_):
        ti, uv = cube_uv[(x, y)]
        if ti >= 11:
            A = at[ti - 11]; u0 = int(round(uv[2][0] * ATL)); v0 = int(round(uv[2][1] * ATL))
            cell = np.asarray(Image.fromarray(A[v0:v0 + CELL, u0:u0 + CELL]).resize((Sx, Sx)))
        else:
            cell = np.asarray(Image.fromarray(tex[ti] if ti in tex else np.zeros((8, 8, 3), np.uint8)).resize((Sx, Sx)))
        r_ = (h_ - 1 - (y - y0)) * Sx
        big[r_:r_ + Sx, (x - x0) * Sx:(x - x0 + 1) * Sx] = cell
Image.fromarray(big).save(SCR + "plaza_pv.png")
for yy_ in (140, 148, 152, 156, 160, 164, 168, 172, 175):
    row = np.flatnonzero(EF.dd[:, yy_] > 0); row = row[(row > 90) & (row < 115)]
    print("y", yy_, "road x", (int(row.min()), int(row.max()), len(row)) if len(row) else None)
