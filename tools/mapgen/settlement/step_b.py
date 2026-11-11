"""step B: flatten the old plaza, retexture the ground (grass, dirt roads and yards), reset the GAT of the interior"""
import sys
from common import *
from layout import *
from scipy import ndimage as ndi
from PIL import Image
sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from tilelib import TileLib
from edges import EdgeField

g, h0 = read_heights()
tx = g["textures"]
rng = np.random.default_rng(7)

def smooth_noise(scale, rg, shape):
    a = rg.random((int(shape[0] / scale) + 4, int(shape[1] / scale) + 4))
    return ndi.zoom(a, scale, order=3)[:shape[0], :shape[1]]

# ---- 1. heights: the raised plazas become gently rolling meadow
X0, X1, Y0, Y1 = 22, 150, 26, 179
gx, gy = np.meshgrid(np.arange(X0, X1 + 1), np.arange(Y0, Y1 + 1), indexing="ij")
n = smooth_noise(14, np.random.default_rng(3), gx.shape) - 0.5
taper = np.clip(np.minimum.reduce([gx - X0, X1 - gx, gy - Y0, Y1 - gy]) / 6.0, 0, 1)
F = n * 5.0 * taper                                  # +-2.5 units (a cell is 10) of gentle undulation
# the gate apron keeps flat ground
ap = np.exp(-(((gx - 78) / 9.0) ** 2 + ((gy - 27) / 8.0) ** 2))
F *= (1 - ap)
data = []
for y in range(Y0, Y1):
    for x in range(X0, X1):
        i, j = x - X0, y - Y0
        data += [round(float(F[i, j]), 3), round(float(F[i + 1, j]), 3), round(float(F[i, j + 1]), 3), round(float(F[i + 1, j + 1]), 3)]
heights = {"cmd": "heights", "x0": X0, "y0": Y0, "w": X1 - X0, "h": Y1 - Y0, "data": data, "gat": True}
np.save("/tmp/settle_F.npy", np.array([X0, Y0]))
np.save("/tmp/settle_Fgrid.npy", F)

# ---- 2. what to retexture: every cobble cube (interior, annex, castle platform), plus the dirt layout
tex_of = np.full((W, H), -1)
for y in range(H):
    for x in range(W):
        t = g["cubes"][y * W + x][4]
        if t >= 0:
            tex_of[x, y] = g["tiles"][t]
cobble_ids = [i for i, t in enumerate(tx) if t.lower().startswith("roo\\pront") and i not in (8, 10)] + [0, 1]
cobble = np.isin(tex_of, cobble_ids)
keep_apron = np.zeros((W, H), bool)
keep_apron[72:85, 19:31] = True                        # cobbles under/inside the arch stay
cobble &= ~keep_apron
print("cobble cubes", int(cobble.sum()))

# ---- 3. dirt field (smooth scalar, > 0 where dirt): roads, central plaza, yards
Wc, Hc = W + 1, H + 1
cx, cy = np.meshgrid(np.arange(Wc), np.arange(Hc), indexing="ij")
def dist_poly(pts):
    d = np.full((Wc, Hc), 1e9)
    for (x0, y0), (x1, y1) in zip(pts[:-1], pts[1:]):
        dx, dy = x1 - x0, y1 - y0
        t = np.clip(((cx - x0) * dx + (cy - y0) * dy) / (dx * dx + dy * dy), 0, 1)
        d = np.minimum(d, np.hypot(cx - (x0 + t * dx), cy - (y0 + t * dy)))
    return d
def smooth_pts(pts, k=6):
    p = np.array(pts, float)
    out = []
    for a, b in zip(p[:-1], p[1:]):
        for t in np.linspace(0, 1, k, endpoint=False):
            out.append(a * (1 - t) + b * t)
    out.append(p[-1])
    out = np.array(out)
    for _ in range(3):
        out[1:-1] = (out[:-2] + 2 * out[1:-1] + out[2:]) / 4
    return out
wob = smooth_noise(9, np.random.default_rng(5), (Wc, Hc)) - 0.5
field = np.full((Wc, Hc), -9.0)
widths = {"main": 3.8, "west_track": 2.8, "east_track": 2.8, "north_street": 3.2}
paths = {"main": MAIN, **BRANCH}
for name, pts in paths.items():
    w = widths.get(name, 2.8)
    field = np.maximum(field, w / 2 - dist_poly(smooth_pts(pts)) + 0.5 * wob)
field = np.maximum(field, PLAZA_R + 2.2 * wob * 2 - np.hypot(cx - C[0], cy - C[1]))
for (yu, yv, r) in [(a, b, c * 0.85) for a, b, c in YARDS]:
    field = np.maximum(field, r + 2.2 * wob * 2 - np.hypot(cx - yu, cy - yv))
dirt_mask = field > -1.5
np.save("/tmp/settle_field.npy", field)
field = ndi.gaussian_filter(field, 0.8)
EF = EdgeField(field, Wc, Hc, np.random.default_rng(11))

# ---- 4. tiles for every cube we touch
region = np.zeros((W, H), bool)
region[20:138, 4:179] = True                              # interior + south road
region[138:150, 146:179] = True                           # former annex
region |= cobble
region |= ndi.binary_dilation(field[:W, :H] > -3, iterations=1) & (np.hypot(cx[:W, :H] - 78, cy[:W, :H] - 100) < 200)
region &= ~keep_apron
mat = np.zeros((Wc, Hc), int)
mat[EF.dd > 0] = 1
texdir_prt = "/mnt/user-data/uploads/ROO_MAP/data/texture/roo_prtf/"
TEXP = {8: "/mnt/user-data/uploads/ROO_MAP/data/texture/ROO/pront4.bmp",
        23: texdir_prt + "prt_ÃÊ¿ø02.bmp", 24: texdir_prt + "prt_ÃÊ¿ø03.bmp", 25: texdir_prt + "prt_ÃÊ¿ø04.bmp", 27: texdir_prt + "prt_Èë02.bmp"}
lib = TileLib(TEXP, plain={0: 8, 1: 27})
sel = lib.solve(mat, np.random.default_rng(4), fixed=set())
edgeuv = EF.edge_cubes((0, W), (0, H), skip=set())
from cliffs import cube_stretch
base = np.zeros((Wc, Hc))
tiles = []
EDGE_TEX = 23
used = {}
for x in range(W):
    for y in range(H):
        if not region[x, y]:
            continue
        if (x, y) in edgeuv:
            uv = edgeuv[(x, y)]
            ti = EDGE_TEX
        else:
            ti, uv = sel[(x, y)]
        used[ti] = used.get(ti, 0) + 1
        tiles.append([x, y, ti] + [round(c, 4) for (a, b) in uv for c in (a, b)])
print("tiles", len(tiles), used)

# ---- 5. gat: the interior is walkable again (models re-add their own blocking)
rows = [[gy_, 2 * 25, 2 * 134, 0] for gy_ in range(2 * 29, 2 * 175)]
cmds = [{"cmd": "info"}, heights, {"cmd": "gat", "rows": rows}, {"cmd": "tiles", "list": tiles}, {"cmd": "camera", "u": 78, "v": 100, "distance": 1300}]
json.dump({"commands": cmds}, open("/mnt/user-data/outputs/t1b.json", "w"), separators=(",", ":"))
print("written", len(json.dumps(cmds)) // 1000, "kB")
