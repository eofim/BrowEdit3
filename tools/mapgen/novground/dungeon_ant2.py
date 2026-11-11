"""Lower-right ant-nest dungeon, UL-style: smooth cave slopes, black rock ceiling, baked atlases (antflat floor + moss/mine rock).
Layout traced from the user's 'Ant Nest' guide.  Writes an_h/an_g/an_t json + prt_novar%02d.bmp, preview an_pv.png."""
import json, os, numpy as np, cv2
from PIL import Image, ImageDraw
from scipy import ndimage as ndi
SCR = "/tmp/claude-0/-home-claude/bcc60026-74cc-55ec-960a-59e929b4c57f/scratchpad/"
OUT = "/mnt/user-data/outputs/"
TEX = "/mnt/user-data/uploads/texture/"
MJ = TEX + "1ÇÊµå¹Ù´Ú/Æó±¤´øÀü-%02d.bmp"
ANT = "/mnt/user-data/uploads/Claude_BORF _normal/export/anthell01/data/texture/ÇÊµå¹Ù´Ú/antflat-%d.bmp"
RISE, SLOPE, SLOPE_B = 28.0, 2.2, 3.5
X0, Y0, NXc, NYc = 124, 4, 72, 72
R = 4; NX, NY = NXc * R, NYc * R
G = 4
SLOT_TOP, SLOT_ATL0 = 201, 300
SC_, CX_, YT_ = 18.5, 158.9, 66.0
def cc(px, py): return CX_ + (px - 428) / SC_, YT_ - py / SC_
chambers = []
corridors = [
    [(350, 150), (290, 180), (255, 205), (205, 250), (165, 300), (148, 340)],
    [(508, 152), (565, 178), (610, 205), (668, 245), (698, 288), (702, 320)],
    [(135, 520), (150, 585), (195, 645), (250, 700), (300, 738)],
    [(715, 495), (705, 560), (672, 630), (620, 690), (570, 722)],
    [(428, 345), (422, 365), (416, 390), (428, 450)],
    [(428, 570), (428, 640), (430, 700)],
]
CW = 3.2; SPW = 2.3
S_ = lambda t: (lambda u: u * u * (3 - 2 * u))(np.clip(t, 0, 1))
def to_px(x, y): return ((x - X0) * R, (NY - 1) - (y - Y0) * R)
img = Image.new("L", (NX, NY), 0); d = ImageDraw.Draw(img)
def line(pts, w):
    P = [to_px(*cc(*p)) for p in pts]; d.line(P, fill=255, width=int(w * R), joint="curve")
    for p in P: d.ellipse([p[0] - w * R / 2, p[1] - w * R / 2, p[0] + w * R / 2, p[1] + w * R / 2], fill=255)
for (cx, cy), rx, ry in chambers:
    x, y = cc(cx, cy); px, py = to_px(x, y); ax, ay = rx / SC_, ry / SC_
    d.ellipse([px - ax * R, py - ay * R, px + ax * R, py + ay * R], fill=255)
for i, c in enumerate(corridors): line(c, SPW if i >= 4 else CW)
floor_core0 = np.array(img)[::-1].T > 127
_lm = np.load(os.path.join(os.path.dirname(os.path.abspath(__file__)), "ant_layout_mask.npy")).astype(float)
_lm = ndi.binary_dilation(_lm, iterations=3)
_lm[172:285, 330:530] = False
_gx, _gy = np.meshgrid(X0 + (np.arange(NX) + 0.5) / R, Y0 + (np.arange(NY) + 0.5) / R, indexing="ij")
_ipx = np.round(428 + (_gx - CX_) * SC_).astype(int); _ipy = np.round((YT_ - _gy) * SC_).astype(int)
_ok = (_ipx >= 0) & (_ipx < _lm.shape[1]) & (_ipy >= 0) & (_ipy < _lm.shape[0])
_cm = np.zeros((NX, NY), bool); _cm[_ok] = _lm[_ipy[_ok], _ipx[_ok]] > 0
floor_core0 = floor_core0 | _cm
_ci = Image.fromarray((floor_core0[:, ::-1].T * 255).astype(np.uint8)); d = ImageDraw.Draw(_ci); img = _ci
_P = [to_px(*p) for p in [(158.8, 70.0), (158.5, 64.0)]]; d.line(_P, fill=255, width=int(2.8 * R), joint="curve")
_F = [(157.4, 68.0), (160.0, 68.0), (160.1, 66.0), (161.4, 63.4), (164.2, 60.8), (153.8, 60.8), (156.6, 63.4), (157.0, 66.0)]
d.polygon([to_px(*p) for p in _F], fill=255)
floor0 = np.array(img)[::-1].T > 127
fx, fy = np.meshgrid(X0 + (np.arange(NX) + 0.5) / R, Y0 + (np.arange(NY) + 0.5) / R, indexing="ij")
rng = np.random.default_rng(23)
def nz(sig):
    a = ndi.gaussian_filter(rng.standard_normal((NX, NY)), sig * R); return a / a.std()
noise, noise2, noiseL = nz(5), nz(2), nz(7)
fm = ndi.gaussian_filter(floor0.astype(float), 0.8 * R) + 0.05 * noise2
floor = fm > 0.5
floor_w = ndi.distance_transform_edt(~floor) / R <= 0.6
dF = ndi.distance_transform_edt(~floor_w) / R
# rock block = interior of the user's red outline (screenshot A), mapped through two homographies
import cv2
Hb = np.load("/tmp/Hb.npy"); Hab = np.load("/tmp/Hab.npy")
Aim = cv2.imread("/mnt/user-data/attachments/bcc60026-74cc-55ec-960a-59e929b4c57f/c72d0343-image.png")
redm = ((Aim[..., 2] > 200) & (Aim[..., 1] < 60) & (Aim[..., 0] < 60)).astype(np.uint8) * 255
redm = cv2.dilate(redm, np.ones((5, 5), np.uint8)); cv2.line(redm, (704, 118), (760, 118), 255, 5)
ff = redm.copy(); h_, w_ = ff.shape; msk = np.zeros((h_ + 2, w_ + 2), np.uint8); cv2.floodFill(ff, msk, (5, 5), 128)
inA = (ff != 128).astype(np.uint8)                                  # red line + interior (outside flood-filled)
Hm2A = np.linalg.inv(Hab) @ np.linalg.inv(Hb)
P = np.stack([fx.ravel(), fy.ravel(), np.ones(fx.size)]); Q = Hm2A @ P; qa = (Q[:2] / Q[2]).T
ixa = np.clip(np.round(qa[:, 0]).astype(int), 0, w_ - 1); iya = np.clip(np.round(qa[:, 1]).astype(int), 0, h_ - 1)
block = inA[iya, ixa].reshape(fx.shape) > 0
block &= (fy < 74)
cliff = np.zeros((200, 200), bool)
for f, xmax in (("cl11", 126), ("cl15", 200)):
    for c in json.load(open(OUT + f + ".json"))["commands"]:
        if c["cmd"] != "heights": continue
        a = np.array(c["data"]).reshape(c["h"], c["w"], 4)
        for j in range(c["h"]):
            for i in range(c["w"]):
                if c["x0"] + i < xmax and np.abs(a[j, i]).max() > 0.01: cliff[c["x0"] + i, c["y0"] + j] = True
subc = lambda a: a[X0:X0 + NXc, Y0:Y0 + NYc]
cl_f = np.kron(subc(ndi.binary_dilation(cliff, iterations=2)), np.ones((R, R), bool))
block &= ~cl_f
block = ndi.gaussian_filter(block.astype(float), 1.2 * R) > 0.5
block = ndi.binary_fill_holes(block) & ~cl_f
dB = ndi.distance_transform_edt(block) / R
top_gate = S_((dF - SLOPE) / 3.0) * S_((dB - SLOPE_B) / 3.0)
Hf = -RISE * S_(dF / SLOPE) * S_(dB / SLOPE_B) + top_gate * (1.8 * noise + 0.7 * noise2)
Hf = np.where(block | (dB > 0), Hf, 0.0); Hf = np.where(floor_w, 0.0, Hf)
def corner(arr):
    ii, jj = np.meshgrid(np.arange(NXc + 1), np.arange(NYc + 1), indexing="ij")
    return ndi.map_coordinates(arr, [ii * R - 0.5, jj * R - 0.5], order=1, mode="nearest")
HC = corner(Hf); HC = np.where(np.abs(HC) < 0.05, 0.0, HC)
HC[0, :] = HC[:, 0] = 0.0; HC[-1, :] = HC[:, -1] = 0.0
data = []
for y in range(NYc):
    for x in range(NXc):
        data += [round(float(HC[x, y]), 2), round(float(HC[x + 1, y]), 2), round(float(HC[x, y + 1]), 2), round(float(HC[x + 1, y + 1]), 2)]
need = ndi.binary_dilation(block.reshape(NXc, R, NYc, R).mean((1, 3)) > 0.3, iterations=1)
for c in json.load(open(OUT + "an1_h.json"))["commands"]:
    need[c["x0"] - X0:c["x0"] - X0 + c["w"], c["y0"] - Y0:c["y0"] - Y0 + c["h"]] = True
need &= ~subc(cliff)
rects = []; left_ = need.copy()
while left_.any():
    jj0 = np.flatnonzero(left_.any(axis=0))[0]; row = left_[:, jj0]; ii0 = np.flatnonzero(row)[0]; ii1 = ii0
    while ii1 + 1 < NXc and row[ii1 + 1]: ii1 += 1
    jj1 = jj0
    while jj1 + 1 < NYc and left_[ii0:ii1 + 1, jj1 + 1].all(): jj1 += 1
    rects.append((int(ii0), int(jj0), int(ii1 - ii0 + 1), int(jj1 - jj0 + 1))); left_[ii0:ii1 + 1, jj0:jj1 + 1] = False
heights = []
for ii, jj, w_, h_2 in rects:
    dd = []
    for y in range(jj, jj + h_2):
        for x in range(ii, ii + w_): dd += [round(float(HC[x, y]), 2), round(float(HC[x + 1, y]), 2), round(float(HC[x, y + 1]), 2), round(float(HC[x + 1, y + 1]), 2)]
    heights.append({"cmd": "heights", "x0": ii + X0, "y0": jj + Y0, "w": w_, "h": h_2, "data": dd, "gat": True, "wall_texture": "prt\\prt_novan02.bmp"})
cmin = np.array([[min(HC[x, y], HC[x + 1, y], HC[x, y + 1], HC[x + 1, y + 1]) for y in range(NYc)] for x in range(NXc)])
bmean = block.reshape(NXc, R, NYc, R).mean((1, 3))
inblk = (cmin < -0.05) | (bmean > 0.5)
dFc = corner(dF); dBc = corner(dB)
# ---- textures
def load(p): return np.array(Image.open(p).convert("RGB")).astype(float)
def make_tileable(a):
    h, w = a.shape[:2]
    wy_ = np.sin(np.pi * (np.arange(h) + 0.5) / h) ** 2; wx_ = np.sin(np.pi * (np.arange(w) + 0.5) / w) ** 2
    m = (wy_[:, None] * wx_[None, :])[..., None]; r = np.roll(np.roll(a, h // 2, 0), w // 2, 1)
    return a * m + r * (1 - m)
def flat(a):                                   # remove low-frequency lighting
    lo = ndi.gaussian_filter(a, (18, 18, 0), mode="grid-wrap"); return np.clip(a - lo + lo.mean((0, 1)), 0, 255)
A4, A1, A3, A2 = [load(ANT % n) for n in (4, 1, 3, 2)]
GR = load(TEX + "ÇÊµå¹Ù´Ú/prt_ÃÊ¿ø01.bmp"); SDT = load(TEX + "ÇÊµå¹Ù´Ú/prt_Èë02.bmp")
def tile_at(img, wx, wy):
    u = (wx % 4) / 4 * img.shape[1]; v = (1 - (wy % 4) / 4) * img.shape[0]
    return np.stack([ndi.map_coordinates(img[..., k], [v - 0.5, u - 0.5], order=1, mode="grid-wrap") for k in range(3)], -1)
cr = lambda a: a[14:242, 14:242]
F1 = make_tileable(flat(cr(A4))); F2 = make_tileable(flat(cr(A1))); F3 = F2
RK3, RK7 = load(MJ % 3), load(MJ % 7)
def band(img, U, V): return make_tileable(img[int(V[0] * 256):int(V[1] * 256), int(U[0] * 256):int(U[1] * 256)])
T3 = band(RK3, (0.10, 0.90), (0.22, 0.80)); T7 = band(RK7, (0.12, 0.88), (0.28, 0.78))
def per_tex(T, wx, wy, pu, pv, ox=0.0, oy=0.0):
    h, w = T.shape[:2]
    u = (((wx + ox) / pu) % 1.0) * w; v = (1 - (((wy + oy) / pv) % 1.0)) * h
    return np.stack([ndi.map_coordinates(T[..., k], [v - 0.5, u - 0.5], order=1, mode="grid-wrap") for k in range(3)], -1)
dFin = ndi.distance_transform_edt(floor_w) / R; SD = dF - dFin
dOut = ndi.distance_transform_edt(~block) / R; SE = dB - dOut
SEc = corner(SE); SDc = corner(SD)
Zf = -Hf / 10.0; gzx, gzy = np.gradient(Zf, 1.0 / R); nrm = np.sqrt(gzx ** 2 + gzy ** 2 + 1)
Lv = np.array([-0.45, 0.55, 0.70]); Lv /= np.linalg.norm(Lv)
shade_f = 1 + 0.6 * ((-gzx * Lv[0] - gzy * Lv[1] + Lv[2]) / nrm - Lv[2])
def fld(a, wx, wy): return ndi.map_coordinates(a, [(wx - X0) * R - 0.5, (wy - Y0) * R - 0.5], order=1, mode="nearest")
_zi = Image.new("L", (NX, NY), 0); _zd = ImageDraw.Draw(_zi)
for _poly in ([(150.5, 76), (155.8, 76), (155.8, 61.1), (143, 54.5), (138, 56), (140.2, 59.5), (147, 64), (150.5, 67.5)],
              [(161.2, 76), (166.5, 76), (166.5, 70), (172, 64.5), (180, 62), (181, 57), (176.6, 54.5), (161.2, 61.1)]):
    _zd.polygon([to_px(*p) for p in _poly], fill=255)
zoneE = ndi.gaussian_filter((np.array(_zi)[::-1].T > 127).astype(float), 0.35 * R)
nzc = nz(0.8); nz1 = nz(0.5); nz2 = nz(0.25); nz3a = nz(2.2); nzf = nz(1.6)
smooth = S_
glows = []
CELL, GUT, ATLW, ATLH, SS = 56, 4, 1024, 960, 2      # GUT: gutter pixels baked around every cell (world-continuous) so mip levels never bleed into the neighbouring atlas cell
CELLP = CELL + 2 * GUT
PERA, PERY = ATLW // CELLP, ATLH // CELLP; NC = PERA * PERY
cs_ = ((np.arange(-GUT, CELL + GUT)[:, None] + (np.arange(SS) + 0.5) / SS - 0.5) / (CELL - 1)).ravel()   # edge pixel centres sit exactly on the cube edges -> neighbouring cells share identical border colours (no seams without mipmaps)
FXc, FYc = np.meshgrid(cs_, 1 - cs_)
baked, plain = [], []
for x in range(NXc):
    for y in range(NYc):
        ax, ay = x + X0, y + Y0
        ses = [SEc[x, y], SEc[x + 1, y], SEc[x, y + 1], SEc[x + 1, y + 1]]
        sds = [SDc[x, y], SDc[x + 1, y], SDc[x, y + 1], SDc[x + 1, y + 1]]
        dbs = [dBc[x, y], dBc[x + 1, y], dBc[x, y + 1], dBc[x + 1, y + 1]]
        fl_out = (not inblk[x, y]) and floor_w.reshape(NXc, R, NYc, R)[x, :, y, :].any() and ay < 74
        ext = ((not inblk[x, y]) and min(ses) > -2.6 and max(ses) < 0.3) or fl_out
        if not (inblk[x, y] or ext): continue
        if not ext and min(sds) >= SLOPE + 0.5 and min(dbs) >= SLOPE_B + 0.5: plain.append((ax, ay))
        else: baked.append((ax, ay))
# ---- new road along the user's red lines (screenshot road_red_shot2.png), calibrated through marker cubes
HERE = os.path.dirname(os.path.abspath(__file__))
T_DIRT = load("/mnt/user-data/uploads/ROO_MAP/data/texture/roo_prtf/prt_Èë03.bmp")
f3 = np.load(SCR + "road_field3.npy")
Sm = cv2.imread(os.path.join(HERE, "road_red_shot2.png")); Hs = np.load(os.path.join(HERE, "road_Hsmap.npy"))
_red = ((Sm[..., 2] > 200) & (Sm[..., 1] < 60) & (Sm[..., 0] < 60)).astype(np.uint8) * 255
_red = cv2.dilate(_red, np.ones((5, 5), np.uint8)); _lab = ndi.label(_red > 0)[0]
_big = _lab == np.bincount(_lab[_lab > 0]).argmax()
_loop = _big.astype(np.uint8) * 255
cv2.line(_loop, (35, 250), (25, 300), 255, 5); _ff = _loop.copy(); _m = np.zeros((_ff.shape[0] + 2, _ff.shape[1] + 2), np.uint8); cv2.floodFill(_ff, _m, (3, 3), 128)
loopS = (_ff != 128)
RQ, RX0, RX1, RY0, RY1 = 8, 116, 168, 68, 122
rxs = RX0 + (np.arange((RX1 - RX0) * RQ) + 0.5) / RQ; rys = RY0 + (np.arange((RY1 - RY0) * RQ) + 0.5) / RQ
RX, RY = np.meshgrid(rxs, rys, indexing="ij")
_P = np.stack([RX.ravel(), RY.ravel(), np.ones(RX.size)]); _Q = np.linalg.inv(Hs) @ _P; _q = (_Q[:2] / _Q[2]).T
_ix = np.clip(np.round(_q[:, 0]).astype(int), 0, loopS.shape[1] - 1); _iy = np.clip(np.round(_q[:, 1]).astype(int), 0, loopS.shape[0] - 1)
redm = (_big[_iy, _ix]).reshape(RX.shape)
_ii = lambda x, y: (int((x - RX0) * RQ), int((y - RY0) * RQ))
_pm = redm.astype(np.uint8)
cv2.line(_pm, _ii(115.4, 100.4)[::-1], _ii(115.8, 96.8)[::-1], 1, 3) if False else None
_t = np.ascontiguousarray(_pm.T.copy()); cv2.line(_t, _ii(115.4, 100.4), _ii(115.8, 96.8), 1, 3)
_t = cv2.dilate(_t, np.ones((3, 3), np.uint8))
_tt = _t.T > 0
for _k in (9, 15, 21, 29, 41):
    inloop = ndi.binary_fill_holes(ndi.binary_closing(np.pad(_tt, _k), structure=np.ones((_k, _k)))[_k:-_k, _k:-_k])
    if inloop.sum() > 2.2 * _tt.sum(): break
print('road fill', _k, inloop.sum(), _tt.sum())
old_f = ndi.map_coordinates(f3, [RX, RY], order=1, mode="nearest") > 0
M = np.where((RX > 119.0) & (RY < 116.5), inloop, old_f)
sdR = (ndi.distance_transform_edt(M) - ndi.distance_transform_edt(~M)) / RQ
sdR = ndi.gaussian_filter(sdR, 0.45 * RQ)
f4 = f3.copy()
gx_, gy_ = np.meshgrid(np.arange(RX0, RX1 + 1), np.arange(RY0, RY1 + 1), indexing="ij")
sdc = ndi.map_coordinates(sdR, [(gx_ - RX0) * RQ - 0.5, (gy_ - RY0) * RQ - 0.5], order=1, mode="nearest")
_rw = np.random.default_rng(31)
sdc = np.where(sdc > -3.5, sdc + 0.25 * (ndi.zoom(_rw.random((24, 22)), 3, order=3)[:sdc.shape[0], :sdc.shape[1]] - 0.5), sdc)
sdc = np.clip(ndi.gaussian_filter(sdc, 0.5), -4, 4)
_dw = np.minimum.reduce([gx_ - RX0, RX1 - gx_, gy_ - RY0, RY1 - gy_]); _wg = np.clip(_dw / 3.0, 0, 1); _wg = _wg * _wg * (3 - 2 * _wg)
f4[RX0:RX1 + 1, RY0:RY1 + 1] = f3[RX0:RX1 + 1, RY0:RY1 + 1] * (1 - _wg) + sdc * _wg
np.save(SCR + "road_field4.npy", f4)
def road_sd(wx, wy): return ndi.map_coordinates(f4, [wx, wy], order=1, mode="nearest")
dsetx = {(a, b) for a, b in baked} | {(a, b) for a, b in plain}
road_cubes = []; road_plain = []
for x in range(RX0, RX1):
    for y in range(RY0, RY1):
        if (x, y) in dsetx: continue
        v = f4[x:x + 2, y:y + 2]
        if v.min() < 0.7 and v.max() > -0.7: road_cubes.append((x, y))
        else: road_plain.append((x, y, v.mean() > 0))
road_blend = set()
baked = baked + road_cubes
set_road_only = set(road_cubes)
natl = (len(baked) + NC - 1) // NC
atl = [np.zeros((ATLH, ATLW, 3), np.uint8) for _ in range(natl)]
tiles = []; cells = {}
GRASS = np.array([112, 128, 48.0]); SAND = np.array([196, 160, 96.0])
for n_, (x, y) in enumerate(baked):
    wx = x + FXc; wy = y + FYc
    sd = fld(SD, wx, wy); db = fld(dB, wx, wy); sh = fld(shade_f, wx, wy)
    n1 = fld(nz1, wx, wy); n2 = fld(nz2, wx, wy)
    mf = smooth(0.5 + 0.7 * fld(nzf, wx, wy))[..., None]
    fl = per_tex(F1, wx, wy, 3.0, 3.0)
    fl = fl * np.array([0.60, 0.56, 0.52])
    for gx_, gy_ in glows:
        gd = np.hypot(wx - gx_, wy - gy_); fl = fl + np.array([150, 80, 25.0]) * np.exp(-(gd / 3.4) ** 2)[..., None]
    rkA = per_tex(T3, wx, wy, 1.2, 0.8); rkB = per_tex(T7, wx, wy, 1.7, 1.15, 1.3, 0.7)
    mm = smooth(0.5 + 0.55 * fld(nz3a, wx, wy))[..., None]
    moss = per_tex(F2, wx, wy, 0.9, 0.9, 0.4, 0.9)
    rk = (rkA * mm + rkB * (1 - mm)) * 0.62 + moss * np.array([0.62, 0.72, 0.45]) * 0.55
    det = per_tex(T3, wx, wy, 0.5, 0.35, 0.2, 0.1).mean(-1, keepdims=True); det = det / det.mean()
    rk = rk * np.clip(0.55 + 0.45 * det, 0.5, 1.5)
    cz = fld(nzc, wx, wy); rk = rk * (1 - 0.5 * np.exp(-(cz / 0.16) ** 2))[..., None]          # dark crevices / strata
    rk = rk * (0.8 + 0.35 * smooth(0.5 + 0.5 * fld(nz3a, wx, wy)))[..., None]
    wr = smooth((sd + 0.12 + 0.30 * n1 + 0.10 * n2) / 0.85)
    col = fl * (1 - wr[..., None]) + rk * wr[..., None]
    dfo = np.clip(fld(dF, wx, wy), 0, None); se = fld(SE, wx, wy)
    wb = smooth((dfo - 2.0) / 1.0) * smooth((db - 1.3) / 1.4)
    col = col * (1 - wb[..., None])
    ao = 1 - 0.45 * np.exp(-((sd - 0.30) / 0.75) ** 2)
    col = col * ao[..., None] * np.clip(sh, 0.5, 1.35)[..., None]
    wg = (1 - smooth((se + 0.7 + 0.35 * n1 + 0.1 * n2) / 2.3)) * smooth(dfo / 1.2)
    out = tile_at(GR, wx, wy)
    is_road_only = (x, y) in set_road_only
    outc = out
    col = col * (1 - wg[..., None]) + outc * wg[..., None]
    wcv = 1 - smooth((-se - 0.2 + 0.3 * n1) / 2.6)
    col = col * wcv[..., None] + outc * (1 - wcv[..., None])
    if is_road_only: col = out.copy()
    wf = smooth((wy - 67.8) / 3.2)[..., None] * smooth((fld(dF, wx, wy) * -1 + 1.5) / 1.5)[..., None] * (1.0 if y >= 66 else 0.0)
    col = col * (1 - wf) + out * wf
    if is_road_only or (x, y) in road_blend:
        sdn = road_sd(wx, wy) + 0.12 * fld(nz1, wx, wy) + 0.05 * fld(nz2, wx, wy)
        wdr = smooth(0.5 + (sdn + 0.04) / 0.55)[..., None]
        if (x, y) in road_blend: wdr = wdr * smooth((wy - 71.8) / 2.8)[..., None]
        col = col * (1 - wdr) + tile_at(T_DIRT, wx, wy) * wdr
    col = col.reshape(CELLP, SS, CELLP, SS, 3).mean((1, 3))
    c8 = np.clip(col, 0, 255).astype(np.uint8); cells["%d_%d" % (x, y)] = c8[GUT:-GUT, GUT:-GUT]
    k, c = divmod(n_, NC); cy_, cx_ = divmod(c, PERA)
    atl[k][cy_ * CELLP:(cy_ + 1) * CELLP, cx_ * CELLP:(cx_ + 1) * CELLP] = c8
    u0, u1 = (cx_ * CELLP + GUT + 0.5) / ATLW, (cx_ * CELLP + GUT + CELL - 0.5) / ATLW
    v0, v1 = (cy_ * CELLP + GUT + 0.5) / ATLH, (cy_ * CELLP + GUT + CELL - 0.5) / ATLH
    tiles.append([x, y, SLOT_ATL0 + k, round(u0, 5), round(v1, 5), round(u1, 5), round(v1, 5), round(u0, 5), round(v0, 5), round(u1, 5), round(v0, 5)])
for x, y in plain: tiles.append([x, y, SLOT_TOP, 0.09, 0.09, 0.11, 0.09, 0.09, 0.11, 0.11, 0.11])
def _pl(x, y):
    pts = [(((x % 4) + dx) / 4, 1 - ((y % 4) + dy) / 4) for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]]
    return [round(float(q), 5) for p in pts for q in p]
for x, y, dirt in road_plain: tiles.append([x, y, 2 if dirt else 0] + _pl(x, y))
for k in range(natl):
    Image.fromarray(atl[k]).quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.FLOYDSTEINBERG).save(OUT + "prt_novar%02d.bmp" % (k + 1))
# gat
gx, gy = np.meshgrid(X0 + (np.arange(2 * NXc) + 0.5) / 2, Y0 + (np.arange(2 * NYc) + 0.5) / 2, indexing="ij")
gi = np.clip(((gx - X0) * R).astype(int), 0, NX - 1); gj = np.clip(((gy - Y0) * R).astype(int), 0, NY - 1)
walk = floor_w[gi, gj] & (gy < 74); rock = (Hf[gi, gj] < -0.5) & ~walk
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
json.dump({"commands": heights}, open(OUT + "an_h.json", "w"), separators=(",", ":"))
json.dump({"commands": [{"cmd": "tiles", "list": tiles}]}, open(OUT + "an_t.json", "w"), separators=(",", ":"))
json.dump({"commands": [{"cmd": "gat", "rows": rows}]}, open(OUT + "an_g.json", "w"), separators=(",", ":"))
# preview: composite of cells + black tops on grass
PX0, PX1, PY0, PY1 = 108, 196, Y0, 120
pv = np.zeros(((PY1 - PY0) * CELL, (PX1 - PX0) * CELL, 3), np.uint8); pv[:] = (112, 128, 48)
def _put(x, y, c):
    if PX0 <= x < PX1 and PY0 <= y < PY1:
        pv[(PY1 - 1 - y) * CELL:(PY1 - y) * CELL, (x - PX0) * CELL:(x - PX0 + 1) * CELL] = c
for x, y in plain: _put(x, y, 0)
for x, y, d in road_plain:
    if d: _put(x, y, (196, 160, 96))
for k, c in cells.items():
    x, y = map(int, k.split("_")); _put(x, y, c)
Image.fromarray(pv).resize(((PX1 - PX0) * 16, (PY1 - PY0) * 16), Image.LANCZOS).save(OUT + "an_pv.png")
hv = (np.clip(128 - HC / RISE * 100, 0, 255)).astype(np.uint8).T[::-1]
Image.fromarray(hv).resize((NXc * 8, NYc * 8), Image.NEAREST).save(SCR + "an_hpv.png")
print("baked", len(baked), "plain", len(plain), "atlases", natl, "hmin", HC.min(), "rows", len(rows))
