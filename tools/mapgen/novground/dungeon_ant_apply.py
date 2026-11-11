import json, random, numpy as np
OUT = "/mnt/user-data/outputs/"; X0, Y0 = 132, 6
z = np.load("/tmp/an1.npz"); floor, block, Hc = z["floor"], z["block"], z["Hc"]; NX, NY = floor.shape
FL, SL1, FL2, SL2, BLK = 52, 53, 54, 55, 22
random.seed(11)
def pl(x, y, rot=0):
    pts = [(((x % 4) + dx) / 4, 1 - ((y % 4) + dy) / 4) for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]]
    return [round(float(q), 5) for p in pts for q in p]
tiles = []
for i in range(NX):
    for j in range(NY):
        if not block[i, j]: continue
        x, y = i + X0, j + Y0; h = Hc[i, j]
        if floor[i, j]: tiles.append([x, y, FL if random.random() > .18 else FL2] + pl(x, y))
        elif h.max() <= -39.9 and h.min() <= -39.9: tiles.append([x, y, BLK, 0.06, 0.06, 0.14, 0.06, 0.06, 0.14, 0.14, 0.14])
        else: tiles.append([x, y, SL1 if h.mean() > -20 else SL2] + pl(x, y))
rows = []
for yy in range(2 * NY):
    for typ, arr in ((1, block & ~floor), (0, floor)):
        xs_ = np.flatnonzero(np.repeat(arr, 2, axis=0)[:, yy // 2])
        if not len(xs_): continue
        s = p = xs_[0]
        for q in xs_[1:]:
            if q != p + 1: rows.append([2 * Y0 + yy, 2 * X0 + int(s), 2 * X0 + int(p) + 1, typ]); s = q
            p = q
        rows.append([2 * Y0 + yy, 2 * X0 + int(s), 2 * X0 + int(p) + 1, typ])
h = json.load(open(OUT + "an1_h.json"))["commands"]
for c in h: c["wall_texture"] = "prt\\prt_novan02.bmp"
json.dump({"commands": [{"cmd": "info"}] + h + h + [{"cmd": "gat", "rows": rows}]}, open(OUT + "an2.json", "w"), separators=(",", ":"))
json.dump({"commands": [{"cmd": "info"}, {"cmd": "tiles", "list": tiles}]}, open(OUT + "an3.json", "w"), separators=(",", ":"))
print(len(h), len(tiles), len(rows))
