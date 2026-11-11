"""Upper-right sewer-style dungeon (prt_sewb1 style): rectilinear floor rooms/corridors with a sunken centre channel, hard steps,
always-black rock top. Layout traced from the user's mock-up (crop coords of the red-circled area). Writes ur_h.json / ur_g.json / ur_t.json
(provisional slots: 300 floor, 301 channel, 302 pad, 22 black top) and prt_novsw01..03.bmp copies of the original prt_sewb1 textures."""
import json, os, shutil, numpy as np
from PIL import Image
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
OUT = "/mnt/user-data/outputs/"
X0, Y0, NX, NY = 139, 141, 52, 57                      # global grid x139..190, y141..197
BX0, BX1, BY0, BY1 = 148, 183, 143, 197                # rock block (inclusive)
KX, KY = 36.0 / 900.0, 54.0 / 1310.0
cx2x = lambda c: 148 + (c - 40) * KX * 35.0 / 36.0 * 36.0 / 35.0
cy2y = lambda c: 197 - (c - 40) * KY
def R(x0, y0, x1, y1): return (cx2x(x0), cy2y(y1), cx2x(x1), cy2y(y0))      # crop rect -> cube rect (xmin,ymin,xmax,ymax)
rooms = {                                          # name: (rect, rounded)
    "spine": (R(405, 115, 460, 1350), False),
    "top_left": (R(135, 140, 405, 262), True),
    "top_junction": (R(398, 100, 475, 262), False),
    "top_corr": (R(470, 160, 665, 222), False),
    "top_right": (R(660, 100, 830, 265), True),
    "mid_left": (R(85, 530, 232, 690), True),
    "mid_left_corr": (R(230, 565, 405, 635), False),
    "mid_right_corr": (R(460, 565, 795, 635), False),
    "mid_right": (R(790, 540, 912, 680), True),
    "bottom_hall": (R(75, 958, 908, 1072), True),
}
BL = [(425, 100, 780, 265), (853, 70, 1187, 220), (757, 117, 895, 687), (502, 323, 675, 615), (887, 310, 1138, 503),
      (481, 636, 773, 763), (874, 636, 1131, 752), (500, 698, 598, 868), (1054, 678, 1147, 865)]
def bl(p): x0, y0, x1, y1 = p; return (148 + (x0 - 545) / 16.39, 198 - (y1 - 15) / 16.39, 148 + (x1 - 545) / 16.39, 198 - (y0 - 15) / 16.39)
for i, p in enumerate(BL): rooms["blue%d" % i] = (bl(p), i in (0, 1, 3, 4, 7, 8))
cliffM = np.zeros((200, 200), bool)
for c in json.load(open(OUT + "cl15.json"))["commands"]:
    if c["cmd"] == "heights":
        a = np.array(c["data"]).reshape(c["h"], c["w"], 4)
        for j in range(c["h"]):
            for i in range(c["w"]):
                if np.abs(a[j, i]).max() > 0.01: cliffM[c["x0"] + i, c["y0"] + j] = True
from scipy import ndimage as ndi
forb2 = ndi.binary_dilation(cliffM, iterations=2)[X0:X0 + NX, Y0:Y0 + NY]
forb3 = ndi.binary_dilation(cliffM, iterations=3)[X0:X0 + NX, Y0:Y0 + NY]
gxx = (np.arange(NX)[:, None] + X0) * np.ones((1, NY)); gyy = (np.arange(NY)[None, :] + Y0) * np.ones((NX, 1))
outside_ok = ((gxx >= 145) & (gxx <= 185)) | (gyy >= 180)          # cubes outside the main window only above y180 (cliff-free there)
floor = np.zeros((NX, NY), bool)
def paint(mask, rect, rounded, minw=3):
    x0, y0, x1, y1 = rect
    a, b, c, d = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
    if c - a < minw: m = (a + c) // 2; a, c = m - minw // 2, m - minw // 2 + minw
    if d - b < minw: m = (b + d) // 2; b, d = m - minw // 2, m - minw // 2 + minw
    for x in range(a, c):
        for y in range(b, d):
            if rounded and (c - a) >= 5 and (d - b) >= 5:
                ex = min(x - a, c - 1 - x); ey = min(y - b, d - 1 - y)
                if ex == 0 and ey == 0: continue
            i, j = x - X0, y - Y0
            if 0 <= i < NX and 0 <= j < NY: mask[i, j] = True
for nm, (rect, rd) in rooms.items(): paint(floor, rect, rd)
floor &= ~forb3 & outside_ok
# exit: spine runs on through the rock block's south wall to the grass
sx = int(round(cx2x(432))) - 1
for x in range(sx, sx + 3):
    for y in (141, 142): floor[x - X0, y - Y0] = True
# rock block: rounded rectangle, chamfer 3
block = np.zeros((NX, NY), bool)
for x in range(BX0, BX1 + 1):
    for y in range(BY0, BY1 + 1):
        ex = min(x - BX0, BX1 - x); ey = min(y - BY0, BY1 - y)
        if ex + ey < 3 and not (ex >= 3 or ey >= 3): continue
        block[x - X0, y - Y0] = True
block = block | ndi.binary_dilation(floor, structure=np.ones((3, 3), bool), iterations=3)
block &= ~forb2 & outside_ok
rock = block & ~floor
# sunken centre channel (1 cube) along the spine and the two middle arms, interrupted by plazas at the junctions
chan = np.zeros((NX, NY), bool)
def cube_of_cy(cy): return int(round(cy2y(cy)))
def cube_of_cx(cx): return int(round(cx2x(cx)))
cxs = sx + 1
for y0, y1 in ((245, 575), (640, 990), (1075, 1115)):
    for y in range(cube_of_cy(y1), cube_of_cy(y0) + 1):
        if floor[cxs - X0, y - Y0]: chan[cxs - X0, y - Y0] = True
cym = cube_of_cy(600)
for x0, x1 in ((205, 400), (460, 795)):
    for x in range(cube_of_cx(x0), cube_of_cx(x1) + 1):
        if floor[x - X0, cym - Y0]: chan[x - X0, cym - Y0] = True
chan &= floor
pad = np.zeros((NX, NY), bool)
for x in range(cube_of_cx(130), cube_of_cx(195) + 1):
    for y in range(cube_of_cy(635), cube_of_cy(565) + 1):
        if floor[x - X0, y - Y0] and not chan[x - X0, y - Y0]: pad[x - X0, y - Y0] = True
# heights (positive = down): rock -40, channel +10
Hc = np.zeros((NX, NY)); Hc[rock] = -40.0; Hc[chan] = 10.0
def win(xa, xb, ya, yb):                               # inclusive cube window -> heights command
    d = []
    for y in range(ya, yb + 1):
        for x in range(xa, xb + 1):
            h = float(Hc[x - X0, y - Y0]); d += [h, h, h, h]
    return {"cmd": "heights", "x0": xa, "y0": ya, "w": xb - xa + 1, "h": yb - ya + 1, "gat": True, "wall_texture": "prt\\prt_novsw01.bmp", "data": d}
hcmds = [win(145, 185, 141, 197), win(139, 146, 178, 197), win(184, 190, 178, 197)]
# safety: nothing nonzero outside the three windows, and no cliff cube inside any window
tot = sum(np.count_nonzero(Hc[(c["x0"] - X0):(c["x0"] - X0 + c["w"]), (c["y0"] - Y0):(c["y0"] - Y0 + c["h"])]) for c in hcmds)
assert np.count_nonzero(Hc) <= tot
for c in hcmds:
    assert not cliffM[c["x0"]:c["x0"] + c["w"], c["y0"]:c["y0"] + c["h"]].any(), "window hits a cliff"
# tiles
G = 4
def pl(x, y):
    pts = [(((x % G) + dx) / G, 1 - ((y % G) + dy) / G) for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]]
    return [round(float(q), 5) for p in pts for q in p]
tiles = []
for i in range(NX):
    for j in range(NY):
        x, y = i + X0, j + Y0
        if rock[i, j]: tiles.append([x, y, 22, 0.06, 0.06, 0.14, 0.06, 0.06, 0.14, 0.14, 0.14])
        elif chan[i, j]: tiles.append([x, y, 301] + pl(x, y))
        elif pad[i, j]: tiles.append([x, y, 302] + pl(x, y))
        elif floor[i, j]: tiles.append([x, y, 300] + pl(x, y))
# gat: rock cells blocked, floor cells walkable (2x2 per cube)
rows = []
for yy in range(2 * NY):
    for typ, arr in ((1, rock), (0, floor)):
        xs = np.flatnonzero(np.repeat(arr, 2, axis=0)[:, yy // 2])
        if not len(xs): continue
        s = p = xs[0]
        for q in xs[1:]:
            if q != p + 1: rows.append([2 * Y0 + yy, 2 * X0 + int(s), 2 * X0 + int(p) + 1, typ]); s = q
            p = q
        rows.append([2 * Y0 + yy, 2 * X0 + int(s), 2 * X0 + int(p) + 1, typ])
json.dump({"commands": hcmds}, open(OUT + "ur_h.json", "w"), separators=(",", ":"))
json.dump({"commands": [{"cmd": "tiles", "list": tiles}]}, open(OUT + "ur_t.json", "w"), separators=(",", ":"))
json.dump({"commands": [{"cmd": "gat", "rows": rows}]}, open(OUT + "ur_g.json", "w"), separators=(",", ":"))
# texture copies from the original prt_sewb1 export (floor 03, channel 01, pad 04)
src = "/mnt/user-data/uploads/Claude_BORF _normal/export/prt_sewb1/data/texture/ÇÊµå¹Ù´Ú/"
for k, n in ((1, "prt_ÁöÇÏ03.bmp"), (2, "prt_ÁöÇÏ01.bmp"), (3, "prt_ÁöÇÏ04.bmp")):
    shutil.copy(src + n, OUT + "prt_novsw%02d.bmp" % k)
pv = np.zeros((NY * 12, NX * 12, 3), np.uint8)
for i in range(NX):
    for j in range(NY):
        col = (30, 30, 30) if rock[i, j] else (90, 130, 60)
        if floor[i, j]: col = (110, 115, 140)
        if chan[i, j]: col = (60, 90, 170)
        if pad[i, j]: col = (190, 130, 50)
        pv[(NY - 1 - j) * 12:(NY - j) * 12, i * 12:(i + 1) * 12] = col
Image.fromarray(pv).save(SCR + "ur_pv.png")
print("floor", int(floor.sum()), "chan", int(chan.sum()), "rock", int(rock.sum()), "tiles", len(tiles), "gat rows", len(rows))
