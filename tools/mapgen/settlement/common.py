"""Shared helpers for the prt_set Tier 1 (Settlement) build. Cell coords (u,v): rsw pos x = 10u - 5W, z = 10v - 5H, y = gnd height."""
import sys, os, json
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import palette

W, H = 156, 196
SRC = "/mnt/user-data/uploads/ROO_MAP/data/claude/"
RECT = (22, 137, 26, 178)            # walled interior (u0,u1,v0,v1)


def uv_of(o):
    return (o["pos"][0] + 5 * W) / 10.0, (o["pos"][2] + 5 * H) / 10.0


def pos_of(u, v, y):
    return [round(10.0 * u - 5 * W, 3), round(float(y), 3), round(10.0 * v - 5 * H, 3)]


def load_models():
    r = palette.read_rsw(SRC + "prt_set.rsw")
    return [o for o in r["objects"] if o["type"] == "model"]


def item(o):
    return {"file": o["file"].replace("/", "\\"), "name": o["name"], "pos": [round(x, 4) for x in o["pos"]],
            "rot": [round(x, 4) for x in o["rot"]], "scale": [round(x, 4) for x in o["scale"]]}


def kept_originals():
    """walls / towers / entrance architecture (exact originals) + the pines, rocks and cliffs outside the walls"""
    keep = []
    for o in load_models():
        f = o["file"].lower().replace("/", "\\")
        u, v = uv_of(o)
        if "gate_new" in f:
            if u > 137.0:
                continue                                         # annex walls beyond the east wall
            if 107.0 < u < 135.0 and 173.5 < v < 176.8:
                continue                                         # inner annex wall of the church yard
            if abs(u - 135.8) < 0.5 and 144 < v < 150:
                continue
            keep.append(o); continue
        inside = 19 < u < 138 and 23 < v < 179
        if inside:
            continue
        b = f.split("\\")[-1]
        if b.startswith(("tanaman12", "tanaman5", "cliff", "rock", "bush")) or "rock" in f:
            keep.append(o)
    return keep


def read_heights():
    g = palette.read_gnd(SRC + "prt_set.gnd")
    c = g["cubes"]
    h = np.zeros((W + 1, H + 1))
    for y in range(H):
        for x in range(W):
            cu = c[y * W + x]
            h[x, y] = cu[0]; h[x + 1, y] = cu[1]; h[x, y + 1] = cu[2]; h[x + 1, y + 1] = cu[3]
    return g, h
