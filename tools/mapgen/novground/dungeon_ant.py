"""Lower-right anthell01-style dungeon: ring corridor + 5 chambers inside a raised rock block (layout from the user's mock-up)."""
import json, random, numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi
OUT = "/mnt/user-data/outputs/"
S_, A_, B_ = 6.13, -564.1, 594.0                      # mock px = S*x + A ; py = B - S*y
x2p = lambda x: S_ * x + A_
y2p = lambda y: B_ - S_ * y
X0, Y0, NX, NY = 132, 6, 56, 62                       # x132..187, y6..67
W, Hh = 646 * 3, 571 * 3; K = 3
def canvas(): return Image.new("L", (646 * K, 571 * K), 0)
def poly(d, pts, w): 
    P = [(x * K, y * K) for x, y in pts]; d.line(P, fill=255, width=int(w * K), joint="curve")
    for p in P: d.ellipse([p[0] - w * K / 2, p[1] - w * K / 2, p[0] + w * K / 2, p[1] + w * K / 2], fill=255)
def ell(d, cx, cy, rx, ry): d.ellipse([(cx - rx) * K, (cy - ry) * K, (cx + rx) * K, (cy + ry) * K], fill=255)
RW, SW = 3 * S_, 2.6 * S_
fl = canvas(); d = ImageDraw.Draw(fl)
for cx, cy, rx, ry in ((410, 252, 25, 23), (318, 343, 31, 34), (497, 338, 29, 31), (408, 376, 23, 21), (405, 465, 48, 36)): ell(d, cx, cy, rx, ry)
poly(d, [(395, 247), (355, 275), (330, 305), (318, 330)], RW)
poly(d, [(322, 360), (332, 392), (347, 422), (375, 447), (395, 458)], RW)
poly(d, [(425, 247), (462, 275), (490, 305), (497, 322)], RW)
poly(d, [(493, 356), (485, 385), (470, 408), (445, 432), (425, 452)], RW)
poly(d, [(410, 268), (409, 300), (407, 340), (408, 360)], SW)
poly(d, [(407, 392), (405, 420), (405, 445)], SW)
poly(d, [(410, 150), (410, 240)], RW)                 # entrance from the road
fl = np.array(fl) > 0
bl = canvas(); d = ImageDraw.Draw(bl)
d.rounded_rectangle([265 * K, 203 * K, 553 * K, 530 * K], radius=int(42 * K), fill=255)
bl = np.array(bl) > 0
def sample(mask, per=3):
    acc = np.zeros((NX, NY))
    for a in range(per):
        for b in range(per):
            xs = (np.arange(NX) + X0 + (a + .5) / per)[:, None] + 0 * np.arange(NY)[None]
            ys = (np.arange(NY) + Y0 + (b + .5) / per)[None] + 0 * np.arange(NX)[:, None]
            ix = np.clip(np.round(x2p(xs) * K).astype(int), 0, mask.shape[1] - 1); iy = np.clip(np.round(y2p(ys) * K).astype(int), 0, mask.shape[0] - 1)
            acc += mask[iy, ix]
    return acc / per ** 2
floor = sample(fl) > 0.45; block = sample(bl) > 0.5
# organic edge: tiny jitter on the block outline
random.seed(7); rng = np.random.RandomState(3)
floor &= block
# keep the 2-cube slope band inside the block: floor must be >=3 cubes from block edge except at the entrance
inner = ndi.binary_erosion(block, iterations=3)
ent = np.zeros((NX, NY), bool)
for y in range(Y0, Y0 + NY):
    if y >= 58:
        ent[:, y - Y0] = True
floor &= inner | ent
# slopes: corner distance (chebyshev) from floor corners
nx, ny = NX + 1, NY + 1
cf = np.zeros((nx, ny), bool)
for dx in (0, 1):
    for dy in (0, 1): cf[dx:dx + NX, dy:dy + NY] |= floor
dist = ndi.distance_transform_cdt(~cf, metric="chessboard")
ch = -np.minimum(40, 20 * dist).astype(float)             # corner heights, raised = negative
bc = np.zeros((nx, ny), bool)                           # corners belonging to the block
for dx in (0, 1):
    for dy in (0, 1): bc[dx:dx + NX, dy:dy + NY] |= block
# outside-block corners stay at 0 (grass)
for i in range(nx):
    for j in range(ny):
        okc = all(0 <= i - dx < NX and 0 <= j - dy < NY and block[i - dx, j - dy] for dx in (0, 1) for dy in (0, 1))
        if not okc and not (0 <= i < NX and 0 <= j < NY and False): ch[i, j] = 0.0 if not cf[i, j] and not okc else ch[i, j]
# rim: corners on the block boundary are pulled to -40 (flat top) except along the entrance cut
Hc = np.zeros((NX, NY, 4))
for i in range(NX):
    for j in range(NY):
        if block[i, j] or floor[i, j]:
            Hc[i, j] = [ch[i, j], ch[i + 1, j], ch[i, j + 1], ch[i + 1, j + 1]]       # SW,SE,NW,NE
need = ndi.binary_dilation(block, iterations=1)
rock = block & ~floor
rects = []; left = need.copy()
while left.any():
    j0 = np.flatnonzero(left.any(axis=0))[0]; row = left[:, j0]; i0 = np.flatnonzero(row)[0]; i1 = i0
    while i1 + 1 < NX and row[i1 + 1]: i1 += 1
    j1 = j0
    while j1 + 1 < NY and left[i0:i1 + 1, j1 + 1].all(): j1 += 1
    rects.append((int(i0 + X0), int(j0 + Y0), int(i1 - i0 + 1), int(j1 - j0 + 1))); left[i0:i1 + 1, j0:j1 + 1] = False
WALL = "prt\\prt_novan02.bmp"
def win(x0, y0, w, h):
    d = []
    for y in range(y0, y0 + h):
        for x in range(x0, x0 + w): d += [float(v) for v in Hc[x - X0, y - Y0]]
    return {"cmd": "heights", "x0": x0, "y0": y0, "w": w, "h": h, "gat": False, "wall_texture": WALL, "data": d}
hc = [win(*r) for r in rects]
json.dump({"commands": hc}, open(OUT + "an1_h.json", "w"), separators=(",", ":"))
# preview
Sx = 12; pv = np.zeros((NY * Sx, NX * Sx, 3), np.uint8)
for i in range(NX):
    for j in range(NY):
        h = Hc[i, j].mean(); col = (90, 130, 60)
        if block[i, j]: col = tuple(int(c) for c in (30 + (-h) * 1.5,) * 3) if not floor[i, j] else (140, 120, 80)
        pv[(NY - 1 - j) * Sx:(NY - j) * Sx, i * Sx:(i + 1) * Sx] = col
Image.fromarray(pv).save(OUT + "an1_pv.png")
np.savez("/tmp/an1.npz", floor=floor, block=block, Hc=Hc, dist=dist)
print(floor.sum(), block.sum(), len(rects))
