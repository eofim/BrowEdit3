"""novground (200x200): retexture the whole ground as green grass + pale dirt paths from the reference layout.
Texture indices of the open map: 0..4 = prt_초원01..05, 5 = prt_흙03 (unused). Only tiles are written; heights are untouched."""
import sys, os, json
import numpy as np
from scipy import ndimage as ndi
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tilelib import TileLib
from edges import EdgeField

W = H = 200
Wc = Hc = 201
IMG_W, IMG_H = 1250.0, 1160.0
def P(px, py):                      # reference image pixel -> cube coordinates (image up = north = higher v)
    return (px / IMG_W * W, (1 - py / IMG_H) * H)

ROADS = {
 "north":  [(625,598),(625,520),(625,470),(635,400),(625,340),(635,290),(650,230),(642,185),(630,150)],
 "mine":   [(525,248),(585,275),(620,320),(630,360)],
 "west":   [(625,598),(560,585),(485,575),(420,575),(340,585),(290,620),(262,690),(255,750)],
 "east":   [(625,598),(705,590),(770,580),(850,565),(930,555),(958,520),(955,470),(946,430),(945,405)],
 "eastsouth": [(958,540),(962,620),(982,700),(996,770)],
 "south":  [(625,598),(625,660),(625,705),(622,740),(630,800),(618,860),(590,920),(562,990),(568,1058),(615,1092),(690,1090),(738,1050),(750,985),(735,925),(700,880),(655,850),(632,820)],
}
CAMP = (625, 598, 11.5)             # centre pixel, plaza radius in cells

rng = np.random.default_rng(21)
def smooth_noise(scale, rg, shape):
    a = rg.random((int(shape[0] / scale) + 4, int(shape[1] / scale) + 4))
    return ndi.zoom(a, scale, order=3)[:shape[0], :shape[1]]
cx, cy = np.meshgrid(np.arange(Wc), np.arange(Hc), indexing="ij")
def dist_poly(pts):
    d = np.full((Wc, Hc), 1e9)
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        dx, dy = x1 - x0, y1 - y0
        t = np.clip(((cx - x0) * dx + (cy - y0) * dy) / (dx * dx + dy * dy), 0, 1)
        d = np.minimum(d, np.hypot(cx - (x0 + t * dx), cy - (y0 + t * dy)))
    return d
def smooth_pts(pts, k=6):
    p = np.array(pts, float); out = []
    for a, b in zip(p[:-1], p[1:]):
        for t in np.linspace(0, 1, k, endpoint=False):
            out.append(a * (1 - t) + b * t)
    out.append(p[-1]); out = np.array(out)
    for _ in range(4):
        out[1:-1] = (out[:-2] + 2 * out[1:-1] + out[2:]) / 4
    return out
wob = smooth_noise(9, np.random.default_rng(5), (Wc, Hc)) - 0.5
field = np.full((Wc, Hc), -9.0)
for name, pts in ROADS.items():
    w = 3.8 if name in ("north", "west", "east", "south") else 3.2
    field = np.maximum(field, w / 2 - dist_poly(smooth_pts([P(*q) for q in pts])) + 0.9 * wob)
ccx, ccy = P(CAMP[0], CAMP[1])
field = np.maximum(field, CAMP[2] + 3.0 * wob - np.hypot(cx - ccx, cy - ccy))
field = ndi.gaussian_filter(field, 0.8)
EF = EdgeField(field, Wc, Hc, np.random.default_rng(11))
mat = np.zeros((Wc, Hc), int)
mat[EF.dd > 0] = 1

td = "/mnt/user-data/uploads/ROO_MAP/data/texture/roo_prtf/"
names = {0: "prt_ÃÊ¿ø01.bmp", 1: "prt_ÃÊ¿ø02.bmp", 2: "prt_ÃÊ¿ø03.bmp", 3: "prt_ÃÊ¿ø04.bmp", 4: "prt_ÃÊ¿ø05.bmp"}
lib = TileLib({i: td + n for i, n in names.items()}, plain={0: 0})
sel = lib.solve(mat, np.random.default_rng(4), fixed=set())
edgeuv = EF.edge_cubes((0, W), (0, H), skip=set())
tiles, used = [], {}
for x in range(W):
    for y in range(H):
        if (x, y) in edgeuv:
            uv, ti = edgeuv[(x, y)], 1
        else:
            ti, uv = sel[(x, y)]
        used[ti] = used.get(ti, 0) + 1
        tiles.append([x, y, ti] + [round(c, 4) for (a, b) in uv for c in (a, b)])
print("tiles", len(tiles), used, "dirt cubes", int((mat[:W, :H] == 1).sum()))
cmds = [{"cmd": "tiles", "list": tiles}, {"cmd": "camera", "u": 100, "v": 100, "distance": 900, "rot": [90, 0]}]
json.dump({"commands": cmds}, open("/mnt/user-data/outputs/ng1.json", "w"), separators=(",", ":"))
# preview
from PIL import Image
pv = np.zeros((W, H, 3), np.uint8); pv[:] = (110, 170, 50); pv[mat[:W, :H] == 1] = (225, 205, 150)
Image.fromarray(pv.transpose(1, 0, 2)[::-1]).resize((600, 600), Image.NEAREST).save("/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/ng_pv.png")
