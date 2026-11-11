"""novground: raised plateau with a rock-ribbon cliff in the oval north of the north road (the red circle)."""
import sys, os, json
import numpy as np
from scipy import ndimage as ndi
sys.path.insert(0, ".."); sys.path.insert(0, "../..")
import cliffs; cliffs.HO = float(os.environ.get('HO', 3.4))
from cliffs import CliffField
from PIL import Image
W = H = 200
STEP = float(os.environ.get("STEP", 30.0))
CX, CY = float(os.environ.get("CX", 104)), float(os.environ.get("CY", 200))
AX, AY = float(os.environ.get("AX", 23)), float(os.environ.get("AY", 38))
gx, gy = np.meshgrid(np.arange(W + 1), np.arange(H + 1), indexing="ij")
inside = ((gx - CX) / AX) ** 2 + ((gy - CY) / AY) ** 2 < 1.0
# organic outline: slow wobble on the boundary
rng = np.random.default_rng(5)
wob = ndi.zoom(rng.random((W // 8 + 4, H // 8 + 4)), 8, order=3)[:W + 1, :H + 1] - 0.5
inside = (((gx - CX) / AX) ** 2 + ((gy - CY) / AY) ** 2 + 0.28 * wob) < 1.0
TD = "/mnt/user-data/uploads/texture/ÇÊµå¹Ù´Ú/"
CF = CliffField(inside, W + 1, H + 1, step=STEP, w=3.0, wt=3.0, end_tex=TD + "prt_¾ð´ö10.bmp", gaps=False, rng=np.random.default_rng(12))
GAPXY = (float(os.environ.get("GX", 101.0)), float(os.environ.get("GY", 162.0)))
for ct in CF.contours:           # one ramp where the road climbs onto the plateau
    i = int(np.argmin(np.hypot(ct.pts[:, 0] - GAPXY[0], ct.pts[:, 1] - GAPXY[1])))
    ct.gaps = [float(ct.s[i])]; ct.half_open, ct.taper = cliffs.HO, 4.5
    print("gap at", ct.pts[i])
gxx, gyy = np.meshgrid(np.arange(W + 1), np.arange(H + 1), indexing="ij")
CF.eval_grid(np.stack([gxx.ravel(), gyy.ravel()], 1).astype(float))
print("contours", [(round(c.L), c.closed) for c in CF.contours])
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
json.dump({"commands": cmds}, open("/mnt/user-data/outputs/cl2.json", "w"), separators=(",", ":"))
# preview
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
pv = np.zeros((Y1 - Y0, X1 - X0, 3), np.uint8)
for y in range(Y0, Y1):
    for x in range(X0, X1):
        h = CF.height[x, y] / STEP
        pv[y - Y0, x - X0] = (90 + 110 * h, 150 + 70 * h, 60 + 50 * h)
        if (x, y) in rock: pv[y - Y0, x - X0] = (150, 120, 90) if rock[(x, y)][0] == "body" else (230, 80, 80)
Image.fromarray(pv[::-1]).resize(((X1 - X0) * 8, (Y1 - Y0) * 8), Image.NEAREST).save(SCR + "cl_pv.png")
