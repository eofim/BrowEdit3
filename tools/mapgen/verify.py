"""Reads a generated map back the way BrowEdit3 reads it, to check the files are consistent.
usage: python verify.py out/my_field [preview.png]"""
import struct, sys, io

def cstr(b): return b.split(b"\0", 1)[0].decode("latin-1")

def read_gnd(path):
    d = open(path, "rb").read(); p = 0
    assert d[:4] == b"GRGN"; ver = (d[4] << 8) | d[5]; p = 6
    W, H, scale, ntex, maxtex = struct.unpack_from("<iifii", d, p); p += 20
    tex = []
    for _ in range(ntex):
        tex.append((cstr(d[p:p+40]), cstr(d[p+40:p+80]))); p += 80
    nlm, lw, lh, grid = struct.unpack_from("<iiii", d, p); p += 16
    p += nlm * lw * lh * 4
    (ntile,) = struct.unpack_from("<i", d, p); p += 4
    tiles = []
    for _ in range(ntile):
        f = struct.unpack_from("<8f", d, p); p += 32
        ti, li = struct.unpack_from("<hH", d, p); p += 4
        p += 4
        assert 0 <= ti < ntex and li < nlm
        tiles.append((f, ti, li))
    cubes = []
    for _ in range(W * H):
        c = struct.unpack_from("<4f3i", d, p); p += 28
        for t in c[4:]: assert -1 <= t < ntile
        cubes.append(c)
    assert p == len(d), "gnd: %d unread bytes" % (len(d) - p)
    return dict(ver=ver, W=W, H=H, tex=tex, tiles=tiles, cubes=cubes)

def read_gat(path):
    d = open(path, "rb").read()
    assert d[:4] == b"GRAT"
    ver, W, H = struct.unpack_from("<hii", d, 4); p = 14
    cells = []
    for _ in range(W * H):
        cells.append(struct.unpack_from("<4fi", d, p)); p += 20
    assert p == len(d), "gat: %d unread bytes" % (len(d) - p)
    return dict(ver=ver, W=W, H=H, cells=cells)

def read_rsw(path):
    d = open(path, "rb").read()
    assert d[:4] == b"GRSW"; ver = (d[4] << 8) | d[5]; p = 6
    assert ver == 0x201
    ini = cstr(d[p:p+40]); gnd = cstr(d[p+40:p+80]); gat = cstr(d[p+80:p+120]); p += 160
    water = struct.unpack_from("<fifffi", d, p); p += 24
    sun = struct.unpack_from("<ii3f3ff", d, p); p += 8 + 24 + 4
    bounds = struct.unpack_from("<4i", d, p); p += 16
    (n,) = struct.unpack_from("<i", d, p); p += 4
    objs = []
    for _ in range(n):
        (t,) = struct.unpack_from("<i", d, p); p += 4
        if t == 1:
            name = cstr(d[p:p+40]); p += 40 + 12
            f = cstr(d[p:p+80]); p += 160
            pos = struct.unpack_from("<3f3f3f", d, p); p += 36
            objs.append(("model", name, f, pos))
        elif t == 2:
            name = cstr(d[p:p+80]); p += 80
            pos = struct.unpack_from("<3f3ff", d, p); p += 28; objs.append(("light", name, pos))
        elif t == 3:
            name = cstr(d[p:p+80]); p += 160
            pos = struct.unpack_from("<3ffiiff", d, p); p += 36; objs.append(("sound", name, pos))
        elif t == 4:
            name = cstr(d[p:p+80]); p += 80
            pos = struct.unpack_from("<3fi5f", d, p); p += 36; objs.append(("effect", name, pos))
        else:
            raise AssertionError("bad object type %d" % t)
    rest = len(d) - p
    assert rest % 48 == 0 and rest // 48 == 1365, "quadtree should be 1365 nodes, got %s" % (rest / 48)
    return dict(gnd=gnd, gat=gat, water=water, sun=sun, objs=objs, bounds=bounds)

if __name__ == "__main__":
    base = sys.argv[1]
    g = read_gnd(base + ".gnd"); a = read_gat(base + ".gat"); r = read_rsw(base + ".rsw")
    assert a["W"] == g["W"] * 2 and a["H"] == g["H"] * 2
    from collections import Counter
    print("gnd %dx%d v0x%x, %d textures, %d tiles" % (g["W"], g["H"], g["ver"], len(g["tex"]), len(g["tiles"])))
    print("gat types:", dict(Counter(c[4] for c in a["cells"])))
    print("rsw: %d objects, water %s, gnd=%s gat=%s" % (len(r["objs"]), r["water"], r["gnd"], r["gat"]))
    if len(sys.argv) > 2:
        import numpy as np, matplotlib; matplotlib.use("Agg"); import matplotlib.pyplot as plt
        W, H = g["W"], g["H"]
        up = np.array([[-(sum(g["cubes"][y*W+x][:4]) / 4) for x in range(W)] for y in range(H)])
        gt = np.array([[a["cells"][y*a["W"]+x][4] for x in range(a["W"])] for y in range(a["H"])])
        fig, ax = plt.subplots(1, 2, figsize=(14, 7))
        ax[0].imshow(up, cmap="terrain", extent=(0, W, H, 0)); ax[0].set_title("height")
        col = {0: (0.3, 0.8, 0.3), 1: (0.9, 0.2, 0.2), 3: (0.3, 0.5, 0.95), 5: (0.9, 0.6, 0.1)}
        img = np.zeros((a["H"], a["W"], 3))
        for k, c in col.items(): img[gt == k] = c
        ax[1].imshow(img, extent=(0, W, H, 0)); ax[1].set_title("GAT (green walk, red blocked, blue water, orange cliff)")
        for o in r["objs"]:
            if o[0] == "model":
                x = (o[3][0] + 5 * W) / 10; z = (o[3][2] + 5 * H) / 10
                ax[1].plot(x, z, "k.", ms=3)
        plt.tight_layout(); plt.savefig(sys.argv[2], dpi=80)
