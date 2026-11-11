"""Reads a 'palette map' made in BrowEdit3: which textures are painted where, which models are placed where.
usage: python palette.py path/to/map.rsw
Handles every rsw/gnd version BrowEdit3 can load (follows the loader code)."""
import struct, sys, os, json
from collections import defaultdict


def cstr(b):
    return b.split(b"\0", 1)[0].decode("cp949", "replace")


def read_gnd(path):
    d = open(path, "rb").read()
    assert d[:4] == b"GRGN", "not a gnd file"
    ver = (d[4] << 8) | d[5]
    p = 6
    W, H, scale, ntex, maxtex = struct.unpack_from("<iifii", d, p); p += 20
    tex = []
    for _ in range(ntex):
        tex.append(cstr(d[p:p + 40])); p += 80          # file name, then the 40 byte display name
    nlm, lw, lh, grid = struct.unpack_from("<iiii", d, p); p += 16
    p += nlm * lw * lh * 4
    (ntile,) = struct.unpack_from("<i", d, p); p += 4
    tiles = []
    for _ in range(ntile):
        p += 32
        ti, li = struct.unpack_from("<hH", d, p); p += 4
        p += 4
        tiles.append(ti)
    cubes = []
    for _ in range(W * H):
        if ver >= 0x106:
            c = struct.unpack_from("<4f3i", d, p); p += 28
        else:
            c = struct.unpack_from("<4f3hh", d, p)[:7]; p += 24
        cubes.append(c)
    return dict(ver=ver, W=W, H=H, textures=tex, tiles=tiles, cubes=cubes)


def read_rsw(path):
    d = open(path, "rb").read()
    assert d[:4] == b"GRSW", "not an rsw file"
    ver = (d[4] << 8) | d[5]
    p = 6
    build = 0
    if ver >= 0x205:
        (build,) = struct.unpack_from("<i", d, p); p += 4
    if ver >= 0x202:
        p += 1
    ini = cstr(d[p:p + 40]); p += 40
    gnd = cstr(d[p:p + 40]); p += 40
    gat = cstr(d[p:p + 40]) if ver > 0x104 else gnd
    if ver > 0x104:
        p += 40
    p += 40
    if ver < 0x206:
        if ver >= 0x103: p += 4
        if ver >= 0x108: p += 16
        if ver >= 0x109: p += 4
    if ver >= 0x105: p += 8 + 24
    if ver >= 0x107: p += 4
    if ver >= 0x106: p += 16
    if ver >= 0x207:
        (n,) = struct.unpack_from("<i", d, p); p += 4 + 4 * n
    (count,) = struct.unpack_from("<i", d, p); p += 4
    objs = []
    for _ in range(count):
        (t,) = struct.unpack_from("<i", d, p); p += 4
        if t == 1:
            name = ""
            if ver >= 0x103:
                name = cstr(d[p:p + 40]); p += 40 + 12
            if ver >= 0x206 and build >= 186: p += 1
            if ver >= 0x207: p += 4
            f = cstr(d[p:p + 80]); p += 160
            pos = struct.unpack_from("<3f", d, p)
            rot = struct.unpack_from("<3f", d, p + 12)
            scale = struct.unpack_from("<3f", d, p + 24)
            p += 36
            objs.append(dict(type="model", name=name, file=f, pos=pos, rot=rot, scale=scale))
        elif t == 2:
            name = cstr(d[p:p + 80]); p += 80
            pos = struct.unpack_from("<3f", d, p); p += 28
            objs.append(dict(type="light", name=name, pos=pos))
        elif t == 3:
            name = cstr(d[p:p + 80]); p += 160
            pos = struct.unpack_from("<3f", d, p); p += 28 + (4 if ver >= 0x200 else 0) + 4
            objs.append(dict(type="sound", name=name, pos=pos))
        elif t == 4:
            name = cstr(d[p:p + 80]); p += 80
            pos = struct.unpack_from("<3f", d, p); p += 36
            objs.append(dict(type="effect", name=name, pos=pos))
        else:
            raise ValueError("unknown object type %d at object %d" % (t, len(objs)))
    return dict(ver=ver, build=build, gnd=gnd, gat=gat, objects=objs)


def summarize(rsw_path):
    rsw = read_rsw(rsw_path)
    gnd = read_gnd(os.path.join(os.path.dirname(rsw_path), rsw["gnd"]))
    W, H = gnd["W"], gnd["H"]
    use = defaultdict(list)
    for i, c in enumerate(gnd["cubes"]):
        up = c[4]
        if up >= 0:
            use[gnd["textures"][gnd["tiles"][up]]].append((i % W, i // W))
    print("map %dx%d cubes, rsw v0x%x, gnd v0x%x" % (W, H, rsw["ver"], gnd["ver"]))
    print("\nTEXTURES (cubes painted, area covered as u0-u1 x v0-v1):")
    for t in gnd["textures"]:
        cells = use.get(t, [])
        if cells:
            xs = [c[0] for c in cells]; ys = [c[1] for c in cells]
            print("  %-45s %5d cubes   u %d-%d  v %d-%d" % (t, len(cells), min(xs), max(xs), min(ys), max(ys)))
        else:
            print("  %-45s (in the texture list, not painted)" % t)
    models = defaultdict(list)
    for o in rsw["objects"]:
        if o["type"] == "model":
            models[o["file"]].append(o)
    print("\nMODELS (count, first position as u,v in cubes):")
    for f, lst in sorted(models.items(), key=lambda kv: (kv[1][0]["pos"][2], kv[1][0]["pos"][0])):
        o = lst[0]
        u = (o["pos"][0] + 5 * W) / 10; v = (o["pos"][2] + 5 * H) / 10
        print("  %-50s x%-3d at u=%.1f v=%.1f  scale=%s" % (f, len(lst), u, v, tuple(round(s, 2) for s in o["scale"])))
    other = [o for o in rsw["objects"] if o["type"] != "model"]
    if other:
        print("\nOTHER OBJECTS:", len(other), {t: sum(1 for o in other if o["type"] == t) for t in ("light", "sound", "effect")})


if __name__ == "__main__":
    summarize(sys.argv[1])
