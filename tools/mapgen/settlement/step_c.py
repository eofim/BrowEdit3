"""step C: the Tier 1 settlement - buildings, market, fences, props, pines, rocks, shrubs"""
import sys
from common import *
from layout import *
from scipy import ndimage as ndi

F = np.load("/tmp/settle_Fgrid.npy")          # heights of the rewritten block: x 22..150, v 26..179 (corner grid)
field = np.load("/tmp/settle_field.npy")
FX0, FY0 = 22, 26

def ground(u, v):
    i, j = u - FX0, v - FY0
    if i < 0 or j < 0 or i > F.shape[0] - 1.001 or j > F.shape[1] - 1.001:
        return 0.0
    i0, j0 = int(i), int(j); a, b = i - i0, j - j0
    return float((F[i0, j0] * (1 - a) + F[i0 + 1, j0] * a) * (1 - b) + (F[i0, j0 + 1] * (1 - a) + F[i0 + 1, j0 + 1] * a) * b)

rng = np.random.default_rng(42)
items = []
placed = []                      # (u, v, radius) of everything solid, to keep spacing
def add(file, u, v, rot=0.0, scale=1.0, dy=0.0, radius=0.0, name=None, mirror=False):
    sc = scale if isinstance(scale, (list, tuple)) else (scale, scale, scale)
    if mirror:
        sc = (-sc[0], sc[1], sc[2])
    items.append({"file": file, "name": name or file.split("\\")[-1].replace(".rsm", ""), "pos": pos_of(u, v, ground(u, v) + dy),
                  "rot": [0, float(rot), 0], "scale": list(sc)})
    if radius:
        placed.append((u, v, radius))

def free(u, v, r):
    return all((u - a) ** 2 + (v - b) ** 2 >= (r + c) ** 2 for a, b, c in placed)

def on_dirt(u, v):
    return field[int(round(u)), int(round(v))] > -2.0

# ---- buildings (the plots are kept for the next tiers)
BLD = [
    # file, u, v, rot, scale, radius
    ("roo\\refinery.rsm", 55, 97, -90, 1.5, 7),            # blacksmith, west of the square
    ("roo\\house4.rsm", 104, 95, 90, 1.4, 7),              # crafting workshop, east
    ("roo\\house1.rsm", 60, 122, 90, -0.9, 7),             # house NW
    ("roo\\house8.rsm", 97, 124, 270, 1.2, 7),             # house NE
    ("roo\\house1.rsm", 60, 60, 270, -0.9, 7),             # house along the road (W)
    ("roo\\house8.rsm", 95, 75, 90, 1.2, 7),               # house along the road (E)
    ("prontera_re\\bakery_01.rsm", 78, 132, 0, 1.0, 8),    # small shop, north end of the square
]
for f, u, v, r, s, rad in BLD:
    add(f, u, v, r, (abs(s), abs(s), abs(s)), radius=rad, mirror=(s < 0))
# blacksmith yard
add("prontera_re\\anvil_01.rsm", 62.5, 99.5, 30, 1.5, radius=1.5)
add("roo\\barrel.rsm", 51, 92, 0, 1.5, radius=1)
add("roo\\barrel1.rsm", 52.5, 91.5, 0, 1.5, radius=1)
add("roo\\wood.rsm", 50, 104, 20, 1.3, radius=2)
add("roo\\wood2.rsm", 53, 105.5, 70, 1.3, radius=2)
add("roo\\box2.rsm", 61, 94, 15, 1.5, radius=1.2)
# workshop yard
add("roo\\box3.rsm", 109, 101, 20, 2.0, radius=2)
add("roo\\box2.rsm", 111, 99, 80, 1.5, radius=1.2)
add("roo\\barrel2.rsm", 99, 101, 0, 1.5, radius=1)
add("roo\\sack.rsm", 100.5, 89.5, 0, 1.5, radius=1)
add("ilusion\\cart_j_01.rsm", 108, 87, 100, 1.2, radius=3)
# fountain and the market square
add("roo\\fountain.rsm", C[0], C[1], 0, (1.17, 1.1, 1.17), radius=7, name="fountain")
add("roo\\fountain3.rsm", C[0], C[1], -45, 1.0, dy=-17.1, radius=0, name="fountain_top")
for dx in (-6.5, 6.5):
    add("roo\\prt_flag.rsm", C[0] + dx, C[1] - 5.5, 0, 2.0, radius=1)
STALLS = [("roo\\vendor2.rsm", 66, 109, 135, 1.5), ("roo\\vendor3.rsm", 91, 108, 225, 2.0),
          ("roo\\vendor.rsm", 67, 93, 45, 1.5), ("roo\\vendor4.rsm", 90, 93, 315, 2.0)]
for f, u, v, r, s in STALLS:
    add(f, u, v, r, s, radius=3.5)
add("roo\\umbrella2.rsm", 78, 114, 0, 1.8, radius=2)
add("roo\\bench.rsm", 70, 114, 0, 1.1, radius=1)
add("roo\\bench.rsm", 86, 114, 180, 1.1, radius=1)
# gate: torches and banners, notice board, mailbox on the road
for dx in (-9, 9):
    add("roo\\prt_flag.rsm", G[0] + dx, G[1] + 7, 0, 2.0, radius=1)
    add("roo\\lamp_shell2.rsm", G[0] + dx * 0.55, G[1] + 11, 0, (2.0, 2.3, 2.0), radius=1)
add("roo\\notice.rsm", 84, 38, 0, 1.5, radius=1)
add("roo\\sign.rsm", 72, 36, 20, 2.0, radius=1)
add("roo\\mailbox.rsm", 82, 66, 0, 2.0, radius=1)
# house yards: barrels, crates, planters
for (u, v, k) in ((60, 122, 0), (97, 124, 1), (60, 60, 2), (95, 75, 3)):
    s = 1 if k % 2 == 0 else -1
    add("roo\\barrel3.rsm", u + 6 * s, v + 4, 0, 1.5, radius=1)
    add("roo\\box2.rsm", u + 7 * s, v + 2.5, 30 * k, 1.5, radius=1)
    add("roo\\plantbox3.rsm", u - 5.5 * s, v + 6, 0, 2.0, radius=1.5)

# ---- wooden fences (segments are 1.5 cells long): arcs around the yards, with gaps
def fence_run(pts, fence="roo\\fence2.rsm", step=1.5, skip=()):
    pts = np.array(pts, float)
    seg = 0
    for a, b in zip(pts[:-1], pts[1:]):
        L = np.hypot(*(b - a)); n = max(1, int(L / step))
        ang = np.degrees(np.arctan2(b[1] - a[1], b[0] - a[0]))
        for i in range(n):
            if seg in skip:
                seg += 1; continue
            p = a + (b - a) * ((i + 0.5) / n)
            add(fence, p[0], p[1], -ang, 1.0, name="fence")
            seg += 1
def arc(cu, cv, r, a0, a1, n=14):
    t = np.radians(np.linspace(a0, a1, n))
    return [(cu + r * np.cos(x), cv + r * np.sin(x)) for x in t]
fence_run(arc(60, 122, 11.5, -40, 140), skip=range(10, 14))                 # NW yard
fence_run(arc(97, 124, 12, 40, 230), skip=range(18, 22))                    # NE yard (a crop plot to the north)
fence_run([(86, 134), (112, 134), (112, 150), (86, 150), (86, 134)], skip=range(14, 18))   # reserved plot, fenced
fence_run(arc(55, 97, 11, 110, 250), skip=range(8, 12))                     # smithy yard
fence_run(arc(104, 95, 11, -80, 70), skip=range(8, 11))                     # workshop yard
fence_run([(72, 48), (72, 58)], skip=())                                    # a few free-standing pieces along the road
fence_run([(86, 80), (86, 90)], skip=())
fence_run([(50, 74), (40, 74), (40, 64)], skip=())

# ---- scattered nature
def scatter(file, n, scale, rmin, rr, rot_rand=False, dirt_ok=False, region=(30, 129, 32, 172), zone=None, dy=0.0, tries=4000, name=None):
    made = 0
    for _ in range(tries):
        if made >= n:
            break
        if zone is not None:
            cu, cv, cr = zone[rng.integers(len(zone))]
            u, v = rng.normal(cu, cr), rng.normal(cv, cr)
        else:
            u, v = rng.uniform(region[0], region[1]), rng.uniform(region[2], region[3])
        if not (region[0] <= u <= region[1] and region[2] <= v <= region[3]):
            continue
        if not dirt_ok and on_dirt(u, v):
            continue
        if not free(u, v, rmin):
            continue
        add(file, u, v, float(rng.uniform(0, 360)) if rot_rand else 0.0, scale, dy=dy, radius=rr, name=name)
        made += 1
    return made
# pines: along the walls and in small groves (leave the central lanes open)
GROVES = [(34, 40, 9), (122, 40, 9), (30, 78, 8), (126, 62, 9), (32, 140, 10), (124, 150, 10), (46, 160, 9), (108, 164, 9), (118, 112, 8), (38, 118, 7), (84, 168, 8), (70, 160, 8), (90, 52, 5), (66, 42, 4)]
print("pines", scatter("roo\\tanaman12.rsm", 70, 3.0, 5.0, 4.0, zone=GROVES))
print("pines singles", scatter("roo\\tanaman12.rsm", 18, 3.0, 9.0, 4.0))
print("shrubs", scatter("roo\\tanaman5.rsm", 14, 1.2, 8.0, 2.0, rot_rand=True))
print("bushes", scatter("roo\\tanaman7.rsm", 26, 1.0, 3.0, 1.0, rot_rand=True))
print("bushes9", scatter("roo\\tanaman9.rsm", 10, 1.5, 4.0, 1.5, rot_rand=True))
print("rocks", scatter("roo_fild\\rock5.rsm", 14, 1.83, 4.0, 1.5, rot_rand=True, dirt_ok=False, name="rock5") + scatter("roo_fild\\rock8.rsm", 7, 5.0, 6.0, 3.0, rot_rand=True, name="rock8"))
print("crates in the open", scatter("roo\\box2.rsm", 4, 1.5, 3.0, 1.0, rot_rand=True, region=(40, 118, 40, 90)))

cmds = [{"cmd": "info"}]
chunks = [items[i:i + 60] for i in range(0, len(items), 60)]
for ch in chunks:
    cmds.append({"cmd": "models", "items": ch, "smart_gat": True})
cmds.append({"cmd": "camera", "u": 78, "v": 90, "distance": 1100, "rot": [50, 0]})
json.dump({"commands": cmds}, open("/mnt/user-data/outputs/t1c.json", "w"), separators=(",", ":"))
json.dump(items, open("/tmp/t1c_items.json", "w"))
print(len(items), "models")
