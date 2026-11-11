"""novground: raised plateau with a rock-ribbon cliff in the oval north of the north road (the red circle)."""
import sys, os, json
import numpy as np
from scipy import ndimage as ndi
sys.path.insert(0, ".."); sys.path.insert(0, "../..")
import cliffs; cliffs.HO = float(os.environ.get('HO', 3.2)); cliffs.TP = float(os.environ.get('TP', 9.0))
from cliffs import CliffField
from PIL import Image
W = H = 200
import cliffs as _c
_orig = _c.extract_contours
_c.extract_contours = lambda Ds, **k: _orig(Ds, sigma=1.1)
STEP = float(os.environ.get("STEP", 30.0))
CX, CY = float(os.environ.get("CX", 104)), float(os.environ.get("CY", 200))
AX, AY = float(os.environ.get("AX", 23)), float(os.environ.get("AY", 38))
gx, gy = np.meshgrid(np.arange(W + 1), np.arange(H + 1), indexing="ij")
from skimage.draw import polygon as _poly
from scipy.interpolate import splprep, splev
OVAL = np.array([[98.06, 161.22], [94.77, 161.53], [91.78, 162.02], [88.78, 163.37], [86.99, 164.41], [84.59, 164.9], [82.2, 164.9], [79.2, 163.8], [77.11, 163.68], [75.01, 164.48], [73.81, 166.44], [73.33, 168.59], [73.51, 170.73], [74.41, 173.8], [75.31, 176.56], [75.73, 179.32], [74.71, 181.78], [72.91, 183.49], [72.14, 185.58], [72.91, 187.6], [74.41, 188.83], [77.11, 189.87], [80.4, 190.67], [82.2, 191.9], [84.59, 193.74], [87.59, 195.15], [91.78, 196.01], [97.77, 196.19], [103.15, 196.62], [108.54, 196.38], [113.33, 195.58], [117.83, 194.78], [119.62, 193.56], [121.12, 190.98], [122.91, 188.22], [124.71, 185.15], [126.21, 183.74], [128.3, 183.31], [130.4, 181.47], [131.3, 178.71], [131.18, 175.95], [129.8, 173.19], [128.78, 170.73], [128.0, 168.28], [128.6, 165.52], [129.5, 163.37], [129.02, 161.53], [127.11, 159.69], [123.51, 158.77], [119.32, 158.34], [115.13, 158.77], [110.94, 159.69], [108.84, 160.67], [106.15, 160.92]])
sm = OVAL
inside = np.zeros((W + 1, H + 1), bool)
rr, cc = _poly(sm[:, 0], sm[:, 1], inside.shape); inside[rr, cc] = True
TD = "/mnt/user-data/uploads/texture/ÇÊµå¹Ù´Ú/"
CF = CliffField(inside, W + 1, H + 1, step=STEP, w=float(os.environ.get('CW', 3.0)), wt=float(os.environ.get('CW', 3.0)), end_tex=TD + "prt_¾ð´ö10.bmp", gaps=False, sigma=1.6, rng=np.random.default_rng(12))
GAPXY = (float(os.environ.get("GX", 101.8)), float(os.environ.get("GY", 161.5)))
for ct in CF.contours:           # one ramp where the road climbs onto the plateau
    i = int(np.argmin(np.hypot(ct.pts[:, 0] - GAPXY[0], ct.pts[:, 1] - GAPXY[1])))
    ct.gaps = [float(ct.s[i])]; ct.half_open, ct.taper = cliffs.HO, cliffs.TP
    print("gap at", ct.pts[i])
gxx, gyy = np.meshgrid(np.arange(W + 1), np.arange(H + 1), indexing="ij")
CF.eval_grid(np.stack([gxx.ravel(), gyy.ravel()], 1).astype(float))
print("contours", [(round(c.L), c.closed) for c in CF.contours])
CF.max_stretch = float(os.environ.get("MS", 1.8))
rock = CF.rock_cubes((0, W), (0, H))
print("rock cubes", len(rock), "dropped", CF.dropped)
# region to send
X0, X1, Y0, Y1 = 60, 150, 150, 200
data = []
for y in range(Y0, Y1):
    for x in range(X0, X1):
        c = [CF.height[x, y], CF.height[x + 1, y], CF.height[x, y + 1], CF.height[x + 1, y + 1]]
        data.extend(round(-v, 3) for v in c)
cmds = [{"cmd": "heights", "x0": X0, "y0": Y0, "w": X1 - X0, "h": Y1 - Y0, "data": data, "gat": True}]
CL, CLEND = 15, 18          # prt_언덕01 (face) / prt_언덕10 (tapering end piece), indices in the open map
lst = []
for (x, y), (kind, uv) in sorted(rock.items()):
    lst.append([x, y, CLEND if kind == "end" else CL] + [round(c, 4) for (a, b) in uv for c in (a, b)])
base = {(t[0], t[1]): t for t in json.load(open("/mnt/user-data/outputs/ng24.json"))["commands"][0]["list"]}     # restore the flat base tiles first
for t in lst: base[(t[0], t[1])] = t
cmds.append({"cmd": "tiles", "list": [base[k] for k in sorted(base)]})
rows = []
for (x, y) in rock:
    hh = [CF.height[x, y], CF.height[x + 1, y], CF.height[x, y + 1], CF.height[x + 1, y + 1]]
    if max(hh) - min(hh) > 6:
        for j in range(2): rows.append([2 * y + j, 2 * x, 2 * x + 2, 5])
cmds.append({"cmd": "gat", "rows": rows})
json.dump({"commands": cmds}, open("/mnt/user-data/outputs/cl5.json", "w"), separators=(",", ":"))
# preview
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
pv = np.zeros((Y1 - Y0, X1 - X0, 3), np.uint8)
for y in range(Y0, Y1):
    for x in range(X0, X1):
        h = CF.height[x, y] / STEP
        pv[y - Y0, x - X0] = (90 + 110 * h, 150 + 70 * h, 60 + 50 * h)
        if (x, y) in rock: pv[y - Y0, x - X0] = (150, 120, 90) if rock[(x, y)][0] == "body" else (230, 80, 80)
Image.fromarray(pv[::-1]).resize(((X1 - X0) * 8, (Y1 - Y0) * 8), Image.NEAREST).save(SCR + "cl_pv.png")

from collections import Counter
print(Counter(v.split()[0] for v in CF.why.values()))
print("---- near gap")
for y in range(157, 168):
    row = ""
    for x in range(84, 120):
        k = rock.get((x, y))
        w = CF.why.get((x, y), "")
        row += ("B" if k and k[0] == "body" else "E" if k else ("t" if w == "tip" else "s" if w.startswith("str") else "."))
    print(y, row)
print("T along contour near gap:", [round(float(CF.T[x, 162]), 2) for x in range(90, 114, 2)])
