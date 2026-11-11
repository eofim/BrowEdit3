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
 "mine":  [(628,330),(600,290),(560,268),(520,250)],
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
    off = noise1(n, 55, rng) * 11.0 + noise1(n, 18, rng) * 2.0 + 2.6 * np.sin(sarc * 2 * np.pi / rng.uniform(32, 46) + rng.uniform(0, 6)) + 1.2 * np.sin(sarc * 2 * np.pi / rng.uniform(14, 20) + rng.uniform(0, 6))
    ramp = np.clip(np.hypot(q[:, 0] - CAMP[0], q[:, 1] - CAMP[1]) / 14.0 - 0.7, 0, 1)
    q = q + nrm * (off * ramp)[:, None]
    q = smooth(q, 6)
    w0 = 3.0 if name in ("north", "west", "east", "south") else 2.6
    wid = w0 * (1.0 + noise1(n, 50, rng) * 1.1 + 0.18 * np.sin(np.arange(n) * 0.6 * 2 * np.pi / rng.uniform(14, 22) + rng.uniform(0, 6)))
    wid = np.clip(wid, w0 * 0.8, w0 * 1.15)
    centre_lines.append((q, wid))
    for (x, y), w in zip(q, wid):                         # distance field = union of discs along the centreline
        x0, x1 = int(max(x - w - 2, 0)), int(min(x + w + 3, Wc)); y0, y1 = int(max(y - w - 2, 0)), int(min(y + w + 3, Hc))
        d = np.hypot(cx[x0:x1, y0:y1] - x, cy[x0:x1, y0:y1] - y)
        field[x0:x1, y0:y1] = np.maximum(field[x0:x1, y0:y1], w / 2 - d)
# worn patches / small widenings next to the roads (natural trampled spots)
for q, wid in centre_lines:
    for _ in range(max(2, len(q) // 40)):
        i = int(rng.integers(len(q))); a = rng.random() * 6.28
        px_, py_ = q[i] + 2.4 * np.array([np.cos(a), np.sin(a)]); r = 1.1 + rng.random() * 1.2
        d = np.hypot(cx - px_, cy - py_); field = np.maximum(field, r - d + 0.3)
# irregular plaza
ang = np.arctan2(cy - CAMP[1], cx - CAMP[0])
rad = 14.5 + 1.8 * np.sin(3 * ang + 0.7) + 1.1 * np.sin(5 * ang + 2.0) + 0.7 * np.sin(8 * ang)

wb = ndi.zoom(rng.random((Wc // 10 + 4, Hc // 10 + 4)), 10, order=3)[:Wc, :Hc] - 0.5
wb2 = ndi.zoom(rng.random((Wc // 3 + 4, Hc // 3 + 4)), 3, order=3)[:Wc, :Hc] - 0.5
field = np.where(field > -3.5, field + 1.0 * wb + 0.5 * wb2, field)     # irregular, hand-worn edges
field = ndi.gaussian_filter(field, 0.7)
# --- clean mask: fill holes/gaps, drop thin necks and specks, then rebuild a smooth signed-distance field
_m = field > -0.4
_st8 = np.ones((3, 3), bool)
_m = ndi.binary_closing(_m, _st8, iterations=1)
_m = ndi.binary_closing(_m, np.ones((1, 9), bool)) | ndi.binary_closing(_m, np.ones((9, 1), bool)) | _m
_hl, _hn = ndi.label(~_m, ndi.generate_binary_structure(2, 1))
for _i in range(1, _hn + 1):
    if (_hl == _i).sum() < 25: _m[_hl == _i] = True
_lab, _n = ndi.label(_m, _st8)
for _i in range(1, _n + 1):
    if (_lab == _i).sum() < 150: _m[_lab == _i] = False
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
tiles, used = [], {}
cube_uv = {}
for x in range(W):
    for y in range(H):
        fc = field[x:x + 2, y:y + 2]
        thick = DT[max(0, x - 2):x + 4, max(0, y - 2):y + 4].max()      # local half-width of the road
        if fc.min() > 1.0: ti, uv = 5, [(0, 1), (1, 1), (0, 0), (1, 0)]
        elif fc.max() < -1.0: ti, uv = 0, [(0, 1), (1, 1), (0, 0), (1, 0)]
        elif (x, y) in edgeuv and thick >= THK: uv, ti = edgeuv[(x, y)], 1
        else: ti, uv = sel[(x, y)]
        dc = np.hypot(x + 0.5 - CAMP[0], y + 0.5 - CAMP[1])
        if dc <= R_OUT:
            ti = 6 if dc <= R_IN else 7
            n_ = 3 if ti == 6 else 2        # rosette = 3x3 cubes, brick = 2x2 cubes (bigger than one cube each)
            i_, j_ = (x - cxi0) % n_, (y - cyi0) % n_
            u0_, u1_ = i_ / n_, (i_ + 1) / n_
            vh1, vh3 = 1 - j_ / n_, 1 - (j_ + 1) / n_
            uv = [(u0_, vh1), (u1_, vh1), (u0_, vh3), (u1_, vh3)]
        used[ti] = used.get(ti, 0) + 1; cube_uv[(x, y)] = (ti, uv)
        tiles.append([x, y, ti] + [round(c, 4) for (a, b) in uv for c in (a, b)])
print("tiles", len(tiles), used, "dirt cubes", int((mat[:W, :H] == 1).sum()))
json.dump({"commands": [{"cmd": "tiles", "list": tiles}]}, open("/mnt/user-data/outputs/ng16.json", "w"), separators=(",", ":"))
# previews: schematic and textured render
pv = np.zeros((W, H, 3), np.uint8); pv[:] = (110, 170, 50); pv[mat[:W, :H] == 1] = (225, 205, 150)
Image.fromarray(pv.transpose(1, 0, 2)[::-1]).resize((600, 600), Image.NEAREST).save(SCR + "ng_pv.png")
S = 10
tex = {i: np.asarray(Image.open(td + n).convert("RGB").resize((128, 128))) for i, n in names.items()}
tex[5] = np.asarray(Image.open(PLAIN_DIRT).convert("RGB").resize((128, 128)))
tex[6] = np.asarray(Image.open('/mnt/user-data/uploads/texture/prt/prt_y_bot01.png').convert('RGB').resize((128, 128)))
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
