"""novground upper-left mine dungeon (Mjolnir Dead Pit style), traced from the user's guide image.
Floor 0, rock mass raised RISE (black ceiling), 4-cube cave slopes, baked atlases from 폐광던전 textures (no stretching).
Writes ul_h.json / ul_g.json / ul_t.json (provisional slots) and prt_novmd01..N.bmp into /mnt/user-data/outputs."""
import json, os, sys
import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage as ndi

SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
OUT = "/mnt/user-data/outputs/"
TEX = "/mnt/user-data/uploads/texture/"
MJ = TEX + "1ÇÊµå¹Ù´Ú/Æó±¤´øÀü-%02d.bmp"
GRP = TEX + "ÇÊµå¹Ù´Ú/prt_ÃÊ¿ø01.bmp"
RISE, SLOPE, SLOPE_B = 30.0, 4.0, 3.0
X0, Y0, NXc, NYc = 0, 104, 90, 96            # grid origin and size in cubes
R = 4; NX, NY = NXc * R, NYc * R
G = 4
SLOT_FLOOR, SLOT_TOP, SLOT_ATL0 = 200, 201, 300   # provisional slots (remapped when applying)

def cc(cx, cy):                                # guide-crop px (2x of image crop at 40,30) -> cube coords
    px = 40 + cx / 2.0; py = 30 + cy / 2.0
    return 100 + (px - 637.5) / 6.0, 96.9 - (py - 635.5) / 6.0

# ---- floor shapes traced from the guide (crop coordinates)
chambers = [((195, 205), 52, 42), ((625, 200), 55, 42), ((425, 385), 118, 50), ((195, 720), 66, 60), ((510, 800), 84, 52), ((690, 628), 108, 40)]
corridors = [
    [(225, 235), (290, 258), (345, 290), (395, 315), (425, 345)],
    [(590, 235), (545, 270), (500, 300), (450, 335)],
    [(330, 395), (280, 420), (235, 480), (210, 545), (200, 610), (195, 665)],
    [(255, 790), (330, 810), (420, 815), (470, 805)],
    [(570, 760), (590, 710), (595, 665), (602, 640)],
    [(520, 415), (565, 445), (590, 490), (600, 550), (605, 600), (615, 625)],
    [(700, 625), (760, 622), (800, 620)],
    [(387, 802), (405, 730), (423, 654), (455, 605), (492, 574), (540, 550), (595, 530)],
]
exit_ext = [(780, 621), (860, 620), (930, 620), (998, 620)]
CW = 2.6                                       # drawn corridor width, cubes (+2 after dilation)
S_ = lambda t: (lambda u: u * u * (3 - 2 * u))(np.clip(t, 0, 1))
def to_px(x, y): return ((x - X0) * R, (NY - 1) - (y - Y0) * R)
img = Image.new("L", (NX, NY), 0); d = ImageDraw.Draw(img)
for (cx, cy), rx, ry in chambers:
    x, y = cc(cx, cy); ax, ay = rx / 12.0 - 0.4, ry / 12.0 - 0.4
    px, py = to_px(x, y); d.ellipse([px - ax * R, py - ay * R, px + ax * R, py + ay * R], fill=255)
for line in corridors:
    pts = [to_px(*cc(*p)) for p in line]
    d.line(pts, fill=255, width=int(CW * R), joint="curve")
    for p in pts: d.ellipse([p[0] - CW * R / 2, p[1] - CW * R / 2, p[0] + CW * R / 2, p[1] + CW * R / 2], fill=255)
floor_core0 = np.array(img)[::-1].T > 127
_d2 = ImageDraw.Draw(img); _pts = [to_px(*cc(*p)) for p in exit_ext]
_d2.line(_pts, fill=255, width=int(CW * R), joint="curve")
for p in _pts: _d2.ellipse([p[0] - CW * R / 2, p[1] - CW * R / 2, p[0] + CW * R / 2, p[1] + CW * R / 2], fill=255)
floor0 = np.array(img)[::-1].T > 127           # [i(x), j(y)]
fx, fy = np.meshgrid(X0 + (np.arange(NX) + 0.5) / R, Y0 + (np.arange(NY) + 0.5) / R, indexing="ij")
rng = np.random.default_rng(23)
def nz(sig):
    a = ndi.gaussian_filter(rng.standard_normal((NX, NY)), sig * R); return a / a.std()
noise, noise2, noiseL = nz(5), nz(2), nz(7)
fm = ndi.gaussian_filter(floor0.astype(float), 1.0 * R) + 0.06 * noise2
floor = fm > 0.5
floor_w = ndi.distance_transform_edt(~floor) / R <= 1.0
dF0 = ndi.distance_transform_edt(~floor_w) / R
core_w = ndi.distance_transform_edt(~(ndi.gaussian_filter(floor_core0.astype(float), 1.0 * R) + 0.06 * noise2 > 0.5)) / R <= 1.0
dCore = ndi.distance_transform_edt(~core_w) / R
rad = (8.5 + 7.0 * S_((fy - 165) / 18) * S_((72 - fx) / 8) + 6.0 * S_((30 - fx) / 16) + 4.0 * S_((138 - fy) / 14) + 6.0 * S_((fx - 42) / 8) * S_((fy - 140) / 8) * S_((197 - fy) / 6))
block = dCore <= rad + 1.7 * noiseL
_cap = json.load(open(SCR + "red2_cube2.json"))["cap"]
_cv = [(x, y) for x, y in _cap] + [(77.6, 143.9), (95, 143.9), (95, 152), (70.4, 152)]
_ci = Image.new("L", (NX, NY), 0); ImageDraw.Draw(_ci).polygon([to_px(x, y) for x, y in _cv], fill=255)
carve = np.array(_ci)[::-1].T > 127
block = ndi.binary_fill_holes(block) & ~carve
block &= (fx > 2.0) & (fy < 197.5) & ~((fx > 70) & (fy > 150))
block = ndi.gaussian_filter(block.astype(float), 1.6 * R) > 0.5
block = ndi.binary_fill_holes(block) & ~carve
# the east exit: let the corridor run out of the rock mass
dB = ndi.distance_transform_edt(block) / R
dF = dF0
rng2 = np.random.default_rng(11)
top_gate = S_((dF - SLOPE) / 3.0) * S_((dB - SLOPE_B) / 3.0)
Hf = -RISE * S_(dF / SLOPE) * S_(dB / SLOPE_B) + top_gate * (2.2 * noise + 0.8 * noise2)
Hf = np.where(block | (dB > 0), Hf, 0.0)
Hf = np.where(floor_w, 0.0, Hf)

Wm = NXc + 1
def corner(arr):
    ii, jj = np.meshgrid(np.arange(NXc + 1), np.arange(NYc + 1), indexing="ij")
    return ndi.map_coordinates(arr, [ii * R - 0.5, jj * R - 0.5], order=1, mode="nearest")
HC = corner(Hf); HC = np.where(np.abs(HC) < 0.05, 0.0, HC)
HC[0, :] = HC[:, 0] = 0.0; HC[-1, :] = HC[:, -1] = 0.0
data = []
for y in range(NYc):
    for x in range(NXc):
        data += [round(float(HC[x, y]), 2), round(float(HC[x + 1, y]), 2), round(float(HC[x, y + 1]), 2), round(float(HC[x + 1, y + 1]), 2)]
_cl = np.array(json.load(open(OUT + "cl11.json"))["commands"][0]["data"]).reshape(50, 90, 4)
_n = 0
for y in range(NYc):
    for x in range(NXc):
        ax, ay = x + X0, y + Y0
        if ax >= 60 and ay >= 150 and ax < 150:
            i = (y + Y0 - 150) * 0 + (ay - 150); j = ax - 60
            if abs(np.array([HC[x, y], HC[x + 1, y], HC[x, y + 1], HC[x + 1, y + 1]])).max() < 0.01 and j < 90:
                base = (y * NXc + x) * 4; data[base:base + 4] = [float(v) for v in _cl[i, j]]; _n += 1
print("cliff cubes restored", _n)
heights = {"cmd": "heights", "x0": X0, "y0": Y0, "w": NXc, "h": NYc, "data": data, "gat": True}

cmin = np.array([[min(HC[x, y], HC[x + 1, y], HC[x, y + 1], HC[x + 1, y + 1]) for y in range(NYc)] for x in range(NXc)])
bmean = block.reshape(NXc, R, NYc, R).mean((1, 3))
inblk = (cmin < -0.05) | (bmean > 0.5)
dFc = corner(dF); dBc = corner(dB)

# ---- textures
def load(p): return np.array(Image.open(p).convert("RGB")).astype(float)
_f6 = load(MJ % 6)
_ft = band_tile0(_f6) if False else None
RK3 = load(MJ % 3); RK7 = load(MJ % 7)         # cliff face pieces
GR = load(GRP)
FU, FV = (0.03, 0.97), (0.03, 0.97)
def flatten(img, U, V):
    r0, r1 = int(V[0] * 256), int(V[1] * 256); c0, c1 = int(U[0] * 256), int(U[1] * 256)
    out = img.copy(); band = img[r0:r1, c0:c1].mean(2); tgt = band.mean()
    pr = ndi.gaussian_filter1d(band.mean(1), 2.5); gr_ = np.ones(256); gr_[r0:r1] = tgt / pr; gr_[:r0] = gr_[r0]; gr_[r1:] = gr_[r1 - 1]
    out *= gr_[:, None, None]
    band2 = out[r0:r1, c0:c1].mean(2); pc = ndi.gaussian_filter1d(band2.mean(0), 2.5)
    gc_ = np.ones(256); gc_[c0:c1] = tgt / pc; gc_[:c0] = gc_[c0]; gc_[c1:] = gc_[c1 - 1]
    out *= gc_[None, :, None]
    return np.clip(out, 0, 255)
def make_tileable(a):
    h, w = a.shape[:2]
    wy_ = np.sin(np.pi * (np.arange(h) + 0.5) / h) ** 2; wx_ = np.sin(np.pi * (np.arange(w) + 0.5) / w) ** 2
    m = (wy_[:, None] * wx_[None, :])[..., None]; r = np.roll(np.roll(a, h // 2, 0), w // 2, 1)
    return a * m + r * (1 - m)
def band_tile(img, U, V):
    f = flatten(img, U, V); return make_tileable(f[int(V[0] * 256):int(V[1] * 256), int(U[0] * 256):int(U[1] * 256)])
T3 = band_tile(RK3, (0.10, 0.90), (0.22, 0.80)); T7 = band_tile(RK7, (0.12, 0.88), (0.28, 0.78))
_ftile = band_tile(_f6, (0.08, 0.92), (0.10, 0.92))
_fim = Image.fromarray(np.clip(_ftile, 0, 255).astype(np.uint8)).resize((256, 256), Image.LANCZOS)
_fim.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(SCR + "prt_novmdf.bmp")
FLT = np.array(Image.open(SCR + "prt_novmdf.bmp").convert("RGB")).astype(float)
def per_tex(T, wx, wy, pu, pv, ox=0.0, oy=0.0):
    h, w = T.shape[:2]
    u = (((wx + ox) / pu) % 1.0) * w; v = (1 - (((wy + oy) / pv) % 1.0)) * h
    return np.stack([ndi.map_coordinates(T[..., k], [v - 0.5, u - 0.5], order=1, mode="wrap") for k in range(3)], -1)
def mir(w, g=G):
    gi = np.floor(w / g); l = (w - gi * g) / g
    return np.where(gi % 2 == 0, l, 1 - l)
def tex_at(img, wx, wy, U, V, gu=G, gv=G):
    tu = mir(wx, gu); tv = mir(wy, gv)
    u = U[0] + (U[1] - U[0]) * tu; v = V[0] + (V[1] - V[0]) * (1 - tv)
    return np.stack([ndi.map_coordinates(img[..., k], [v * 256 - 0.5, u * 256 - 0.5], order=1, mode="nearest") for k in range(3)], -1)
dFin = ndi.distance_transform_edt(floor_w) / R
SD = dF - dFin
dOut = ndi.distance_transform_edt(~block) / R
SE = dB - dOut
SEc = corner(SE); SDc = corner(SD)
Zf = -Hf / 10.0
gzx, gzy = np.gradient(Zf, 1.0 / R)
nrm = np.sqrt(gzx ** 2 + gzy ** 2 + 1)
Lv = np.array([-0.45, 0.55, 0.70]); Lv /= np.linalg.norm(Lv)
shade_f = 1 + 0.6 * ((-gzx * Lv[0] - gzy * Lv[1] + Lv[2]) / nrm - Lv[2])
def fld(a, wx, wy): return ndi.map_coordinates(a, [(wx - X0) * R - 0.5, (wy - Y0) * R - 0.5], order=1, mode="nearest")
nz1 = nz(0.5); nz2 = nz(0.25); nz3a = nz(2.2)
smooth = S_

CELL, ATLW, ATLH = 40, 1024, 512
PERA, PERY = ATLW // CELL, ATLH // CELL; NC = PERA * PERY
cs_ = (np.arange(CELL) + 0.5) / CELL
FXc, FYc = np.meshgrid(cs_, 1 - cs_)
baked, plain = [], []
_cells = {}
for x in range(NXc):
    for y in range(NYc):
        ax, ay = x + X0, y + Y0
        ses = [SEc[x, y], SEc[x + 1, y], SEc[x, y + 1], SEc[x + 1, y + 1]]
        sds = [SDc[x, y], SDc[x + 1, y], SDc[x, y + 1], SDc[x + 1, y + 1]]
        dbs = [dBc[x, y], dBc[x + 1, y], dBc[x, y + 1], dBc[x + 1, y + 1]]
        fl_out = (not inblk[x, y]) and floor_w.reshape(NXc, R, NYc, R)[x, :, y, :].any()
        ext = ((not inblk[x, y]) and min(ses) > -2.6 and max(ses) < 0.3) or fl_out
        if not (inblk[x, y] or ext): continue
        if not ext and min(sds) >= SLOPE + 0.5 and min(dbs) >= SLOPE_B + 0.5: plain.append((ax, ay, "top"))
        else: baked.append((ax, ay))
natl = (len(baked) + NC - 1) // NC
atl = [np.zeros((ATLH, ATLW, 3), np.uint8) for _ in range(natl)]
tiles = []
for n_, (x, y) in enumerate(baked):
    wx = x + FXc; wy = y + FYc
    sd = fld(SD, wx, wy); db = fld(dB, wx, wy); sh = fld(shade_f, wx, wy)
    n1 = fld(nz1, wx, wy); n2 = fld(nz2, wx, wy)
    fl = per_tex(FLT, wx, wy, 4.0, 4.0)
    rkA = per_tex(T3, wx, wy, 3.0, 2.0); rkB = per_tex(T7, wx, wy, 4.4, 3.0, 1.3, 0.7)
    mm = smooth(0.5 + 0.55 * fld(nz3a, wx, wy))[..., None]
    rk = rkA * mm + rkB * (1 - mm)
    gr = tex_at(GR, wx, wy, (0.0, 1.0), (0.0, 1.0))
    wr = smooth((sd + 0.12 + 0.30 * n1 + 0.10 * n2) / 0.85)
    col = fl * (1 - wr[..., None]) + rk * wr[..., None]
    dfo = np.clip(fld(dF, wx, wy), 0, None); se = fld(SE, wx, wy)
    wb = smooth((dfo - 2.7) / 1.3) * smooth((db - 1.5) / 1.5)
    col = col * (1 - wb[..., None])
    ao = 1 - 0.42 * np.exp(-((sd - 0.30) / 0.75) ** 2)
    col = col * ao[..., None] * np.clip(sh, 0.62, 1.25)[..., None]
    wg = (1 - smooth((se + 0.7 + 0.35 * n1 + 0.1 * n2) / 2.3)) * smooth(dfo / 1.2)
    col = col * (1 - wg[..., None]) + gr * wg[..., None]
    wcv = 1 - smooth((-se - 0.2 + 0.3 * n1) / 2.6)                      # the east exit: cave floor fades into the grass outside the rock
    col = col * wcv[..., None] + gr * (1 - wcv[..., None])
    _cells['%d_%d' % (x, y)] = np.clip(col, 0, 255).astype(np.uint8)
    k, c = divmod(n_, NC); cy_, cx_ = divmod(c, PERA)
    atl[k][cy_ * CELL:(cy_ + 1) * CELL, cx_ * CELL:(cx_ + 1) * CELL] = np.clip(col, 0, 255).astype(np.uint8)
    eu, ev = 0.5 / ATLW, 0.5 / ATLH
    u0, u1 = (cx_ * CELL) / ATLW + eu, ((cx_ + 1) * CELL) / ATLW - eu
    v0, v1 = (cy_ * CELL) / ATLH + ev, ((cy_ + 1) * CELL) / ATLH - ev
    tiles.append([x, y, SLOT_ATL0 + k, round(u0, 5), round(v1, 5), round(u1, 5), round(v1, 5), round(u0, 5), round(v0, 5), round(u1, 5), round(v0, 5)])
CORN = [(0, 0), (1, 0), (0, 1), (1, 1)]
def pl(x, y):
    pts = [(((x % G) + dx) / G, 1 - ((y % G) + dy) / G) for dx, dy in CORN]
    return [round(float(q), 5) for p in pts for q in p]
for x, y, kind in plain:
    if kind == "floor": tiles.append([x, y, SLOT_FLOOR] + pl(x, y))
    else: tiles.append([x, y, SLOT_TOP, 0.06, 0.06, 0.14, 0.06, 0.06, 0.14, 0.14, 0.14])
for k in range(natl):
    im = atl[k]
    Image.fromarray(im).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(OUT + "prt_novme%02d.bmp" % (k + 1))
    Image.fromarray(im).save(SCR + "novmd%02d.png" % (k + 1))
np.savez_compressed(SCR + "cells_" + os.environ.get("TAG", "x") + ".npz", **_cells)
json.dump({"plain": [[a, b] for a, b, _ in plain], "baked": [list(p) for p in baked]}, open(SCR + "sets_" + os.environ.get("TAG", "x") + ".json", "w"))
print("baked", len(baked), "plain", len(plain), "atlases", natl)

# ---- gat (2x2 cells per cube): floor walkable, rock / slopes blocked
gx, gy = np.meshgrid(X0 + (np.arange(2 * NXc) + 0.5) / 2, Y0 + (np.arange(2 * NYc) + 0.5) / 2, indexing="ij")
gi = np.clip(((gx - X0) * R).astype(int), 0, NX - 1); gj = np.clip(((gy - Y0) * R).astype(int), 0, NY - 1)
walk = floor_w[gi, gj]; rock = (Hf[gi, gj] < -0.5) & ~walk
rows = []
for yy in range(2 * NYc):
    for typ, m in ((1, rock[:, yy]), (0, walk[:, yy])):
        xs = np.flatnonzero(m)
        if not len(xs): continue
        s = p = xs[0]
        for q in xs[1:]:
            if q != p + 1: rows.append([2 * Y0 + yy, 2 * X0 + int(s), 2 * X0 + int(p) + 1, typ]); s = q
            p = q
        rows.append([2 * Y0 + yy, 2 * X0 + int(s), 2 * X0 + int(p) + 1, typ])
json.dump({"commands": [heights]}, open(OUT + "ul_h.json", "w"), separators=(",", ":"))
json.dump({"commands": [{"cmd": "tiles", "list": tiles}]}, open(OUT + "ul_t.json", "w"), separators=(",", ":"))
json.dump({"commands": [{"cmd": "gat", "rows": rows}]}, open(OUT + "ul_g.json", "w"), separators=(",", ":"))
# preview: schematic
pv = np.zeros((NYc * 6, NXc * 6, 3), np.uint8)
for x in range(NXc):
    for y in range(NYc):
        v = int(np.clip(128 - (HC[x, y] / RISE) * 100, 0, 255)); col = (v, v, v)
        if floor_w.reshape(NXc, R, NYc, R)[x, :, y, :].mean() > 0.5: col = (160, 110, 50)
        pv[(NYc - 1 - y) * 6:(NYc - y) * 6, x * 6:(x + 1) * 6] = col
Image.fromarray(pv).save(SCR + "ul_pv.png")
print("gat rows", len(rows), "hmin", HC.min())
