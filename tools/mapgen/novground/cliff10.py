"""novground: lower ground on the east side (traced from the user's red line), rock-ribbon cliff along the edge."""
import sys, os, json
import numpy as np
from scipy import ndimage as ndi
sys.path.insert(0, ".."); sys.path.insert(0, "../..")
import cliffs
from PIL import Image
from skimage.draw import polygon as _poly
cliffs.HO = 3.0; cliffs.TP = 5.0
_orig = cliffs.extract_contours
cliffs.extract_contours = lambda Ds, **k: _orig(Ds, sigma=1.1)
def _lens_hd(path=None, desert=False, band=(0.08, 0.92)):
    u = np.linspace(0, 0.95, 96)
    a = np.array(Image.open(path).convert("RGB")).astype(float)
    rock = ndi.median_filter((a[..., 2] > 0.57 * a[..., 1]).astype(np.uint8), 5) > 0
    H_, W_ = rock.shape; top = np.full(W_, 0.9)
    for c in range(W_):
        ys = np.flatnonzero(rock[:, c])
        if len(ys) > 12: top[c] = ys.min() / H_
    top = np.maximum.accumulate(ndi.gaussian_filter1d(top, 6, mode="nearest"))
    vt = np.clip(top - 0.12, 0.08, 0.92); rf = np.clip((0.92 - vt) / 0.84, 0.0, 1.0)
    rf_u = np.minimum.accumulate(np.interp(u, np.arange(W_) / (W_ - 1), rf)); rf_u = np.clip(rf_u / rf_u[0], 0, 1)
    return rf_u, u
cliffs.lens_profile = _lens_hd
from cliffs import CliffField
W = H = 200
STEP = 30.0
C = np.load('/tmp/east_line.npy')
poly = np.vstack([C, [[215, C[-1, 1]], [215, C[0, 1]]]])
east = np.zeros((W + 1, H + 1), bool)
rr, cc = _poly(poly[:, 0], poly[:, 1], east.shape); east[rr, cc] = True
inside = ~east                                        # high side = the main field
TD = "/mnt/user-data/uploads/texture/ÇÊµå¹Ù´Ú/"
CF = CliffField(inside, W + 1, H + 1, step=STEP, w=2.6, wt=2.6, end_tex=TD + "prt_¾ð´ö10.bmp", gaps=False, sigma=1.6, rng=np.random.default_rng(12))
print("contours", [(round(c.L), c.closed) for c in CF.contours])
CF.max_stretch = 2.6
rock = CF.rock_cubes((0, W), (0, H))
print("rock cubes", len(rock), "dropped", CF.dropped)
X0, X1, Y0, Y1 = 134, 200, 0, 200
data = []
for y in range(Y0, Y1):
    for x in range(X0, X1):
        c = [CF.height[x, y], CF.height[x + 1, y], CF.height[x, y + 1], CF.height[x + 1, y + 1]]
        data.extend(round(-(v - STEP), 3) for v in c)       # high side stays at 0, the low side goes 3 cubes down
cmds = [{"cmd": "heights", "x0": X0, "y0": Y0, "w": X1 - X0, "h": Y1 - Y0, "data": data, "gat": True}]
CL, CLEND = 15, 18
base = {(t[0], t[1]): t for t in json.load(open("/mnt/user-data/outputs/cl11.json"))["commands"][1]["list"]}
for (x, y), (kind, uv) in sorted(rock.items()):
    base[(x, y)] = [x, y, CLEND if kind == "end" else CL] + [round(c, 4) for (a, b) in uv for c in (a, b)]
cmds.append({"cmd": "tiles", "list": [base[k] for k in sorted(base)]})
rows = []
for (x, y) in rock:
    hh = [CF.height[x, y], CF.height[x + 1, y], CF.height[x, y + 1], CF.height[x + 1, y + 1]]
    if max(hh) - min(hh) > 6:
        for j in range(2): rows.append([2 * y + j, 2 * x, 2 * x + 2, 5])
cmds.append({"cmd": "gat", "rows": rows})
json.dump({"commands": cmds}, open("/mnt/user-data/outputs/cl12.json", "w"), separators=(",", ":"))
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
pv = np.zeros((H, W - 100, 3), np.uint8)
for y in range(H):
    for x in range(100, W):
        h = CF.height[x, y] / STEP
        pv[y, x - 100] = (90 + 110 * h, 150 + 70 * h, 60 + 50 * h)
        if (x, y) in rock: pv[y, x - 100] = (150, 120, 90) if rock[(x, y)][0] == "body" else (230, 80, 80)
Image.fromarray(pv[::-1]).resize(((W - 100) * 5, H * 5), Image.NEAREST).save(SCR + "east_pv.png")
