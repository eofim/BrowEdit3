"""Smooth cliff ribbons the way prt_fild00 builds them.

The terrain is two levels. The border between them is a smooth curve (a contour of a blurred signed distance field).
Along that curve we run a rock band of constant width: the ground height steps over `w` cubes, and the cliff
texture gets UVs from the same field (u = distance along the curve, v = distance across it). So the face has the
same scale everywhere, bends round corners without stair-steps, and has no seam between cubes (the editor's red
grid shows no lines inside a cliff). Gaps in the curve are ramps: there the step fades out and the rock band
narrows to a point, which is what the diagonal end pieces do in the real maps."""
import numpy as np
from scipy import ndimage as ndi
from scipy.spatial import cKDTree
from skimage.measure import find_contours


def _ss(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3 - 2 * t)


def lens_profile(path=None, desert=False, band=(0.08, 0.92)):
    """(rf, u): how thick the rock of the end piece is (1 = full wall .. 0) along its length u, read from the texture"""
    u = np.linspace(0, 0.95, 96)
    if path is None:
        return 1.0 - u / 0.95 * 0.8, u
    from PIL import Image
    a = np.array(Image.open(path).convert("RGB")).astype(float)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    grass = (g > r + 8) & (g > b + 40)
    if desert:
        sat = (a.max(-1) - a.min(-1)) / np.maximum(a.max(-1), 1)
        grass = ~((sat < 0.43) & (r < 205))          # "not rock"
    H, W = grass.shape
    top = np.full(W, 0.9)
    for c in range(W):
        ys = np.flatnonzero(~grass[:, c])
        if len(ys) > 12:
            top[c] = ys.min() / H
    top = ndi.gaussian_filter1d(top, 6, mode="nearest")
    top = np.maximum.accumulate(top)
    if desert:
        rf = np.clip((band[1] - top) / (band[1] - top[0]), 0.0, 1.0)
    else:
        vt = np.clip(top - 0.12, 0.08, 0.92)
        rf = np.clip((0.92 - vt) / 0.84, 0.0, 1.0)
    rf_u = np.interp(u, np.arange(W) / (W - 1), rf)
    rf_u = np.minimum.accumulate(rf_u)
    return rf_u, u


def cube_stretch(h, x, y, uv):
    """how unevenly the texture is stretched over cube (x,y) of height grid h (1 = not at all)"""
    P = [np.array([x * 10, h[x, y], y * 10]), np.array([(x + 1) * 10, h[x + 1, y], y * 10]),
         np.array([x * 10, h[x, y + 1], (y + 1) * 10]), np.array([(x + 1) * 10, h[x + 1, y + 1], (y + 1) * 10])]
    U = [np.array(c, dtype=float) for c in uv]
    e1 = ((P[1] - P[0]) + (P[3] - P[2])) / 2; e2 = ((P[2] - P[0]) + (P[3] - P[1])) / 2
    a1 = ((U[1] - U[0]) + (U[3] - U[2])) / 2; a2 = ((U[2] - U[0]) + (U[3] - U[1])) / 2
    J = np.stack([a1, a2], 1)
    if abs(np.linalg.det(J)) < 1e-5:
        return 99.0
    sv = np.linalg.svd(np.stack([e1, e2], 1) @ np.linalg.inv(J), compute_uv=False)
    return float(sv[0] / max(sv[1], 1e-9))


class Contour:
    pass


KK = 0.2
TP = 4.5                                            # length over which an end piece tapers to its point
HO = 8.5                                            # half the clear width of a ramp gap, in cubes


def signed_field(inside, sigma=3.0):
    sd = ndi.distance_transform_edt(inside) - ndi.distance_transform_edt(~inside)
    return ndi.gaussian_filter(sd, sigma, mode="nearest")


def extract_contours(Ds, min_len=22.0, sigma=2.2, spacing=0.25):
    out = []
    for c in find_contours(Ds, 0.0):
        closed = bool(np.allclose(c[0], c[-1]))
        P = c[:-1] if closed else c
        if len(P) < 6:
            continue
        P = ndi.gaussian_filter1d(P, sigma, axis=0, mode="wrap" if closed else "nearest")
        Q = np.vstack([P, P[:1]]) if closed else P
        seg = np.hypot(*np.diff(Q, axis=0).T)
        s = np.concatenate([[0], np.cumsum(seg)])
        L = float(s[-1])
        if L < min_len:
            continue
        sn = np.arange(0, L, spacing)
        pts = np.stack([np.interp(sn, s, Q[:, 0]), np.interp(sn, s, Q[:, 1])], 1)
        # orient so that the high side (Ds > 0) is on the left of the direction of travel
        tg = np.gradient(pts, axis=0)
        tg /= np.maximum(np.hypot(tg[:, 0], tg[:, 1])[:, None], 1e-9)
        left = np.stack([-tg[:, 1], tg[:, 0]], 1)
        probe = pts + 1.5 * left
        val = ndi.map_coordinates(Ds, [probe[:, 0], probe[:, 1]], order=1, mode="nearest")
        if val.mean() < 0:
            pts = pts[::-1].copy()
        ct = Contour()
        ct.pts = pts
        ct.s = np.arange(len(pts)) * spacing
        ct.closed = closed
        ct.L = len(pts) * spacing if closed else L
        if closed:
            n = max(1, int(round(ct.L * KK)))
            ct.k = n / ct.L                    # whole texture periods round the loop: no seam where it closes
            ct.period = float(n)
        else:
            ct.k = KK
            ct.period = None
        ct.gaps = []
        out.append(ct)
    return out


def place_gaps(contours, Ds, W1, H1, rng, spacing=70.0, half_open=HO, taper=4.5):
    """ramps: choose where each contour opens. Needs room on both sides."""
    for ct in contours:
        n = max(1, int(round(ct.L / spacing)))
        tg = np.gradient(ct.pts, axis=0)
        tg /= np.maximum(np.hypot(tg[:, 0], tg[:, 1])[:, None], 1e-9)
        left = np.stack([-tg[:, 1], tg[:, 0]], 1)
        hi, lo = ct.pts + 8 * left, ct.pts - 8 * left
        ok = ((hi[:, 0] > 4) & (hi[:, 0] < W1 - 5) & (hi[:, 1] > 4) & (hi[:, 1] < H1 - 5) &
              (lo[:, 0] > 4) & (lo[:, 0] < W1 - 5) & (lo[:, 1] > 4) & (lo[:, 1] < H1 - 5))
        if not ct.closed:
            ok &= (ct.s > 14) & (ct.s < ct.L - 14)
        cand = np.flatnonzero(ok)
        for _ in range(n):
            cand = np.array([i for i in cand if all(_cdist(ct, ct.s[i], g) > spacing * 0.45 for g in ct.gaps)], dtype=int)
            if not len(cand):
                break
            i = int(cand[int(rng.integers(len(cand)))])
            ct.gaps.append(float(ct.s[i]))
        ct.half_open, ct.taper = half_open, taper


def _cdist(ct, a, b):
    d = abs(a - b)
    return min(d, ct.L - d) if ct.closed else d


class CliffField:
    """Evaluates height / texture coordinates at any set of points (corner grid)."""

    def __init__(self, inside, W1, H1, step=30.0, w=2.6, wr=14.0, rng=None, sigma=3.0, gaps=True, wt=None, kk=0.2, end_tex=None, desert=False, band=(0.08, 0.92), gap_spacing=70.0):
        self.W1, self.H1, self.step, self.w, self.wr = W1, H1, step, w, wr
        self.wt = wt or w
        self.kk = kk
        self.band = band
        self.desert = desert
        self.rf, self.rf_u = lens_profile(end_tex, desert, band)
        self.rf_ext_u = np.concatenate([self.rf_u, [0.99]])
        self.rf_ext = np.concatenate([self.rf, [0.0]])
        self.rng = rng or np.random.default_rng(0)
        self.Ds = signed_field(inside, sigma)
        self.contours = extract_contours(self.Ds)
        if gaps:
            place_gaps(self.contours, self.Ds, W1, H1, self.rng, spacing=gap_spacing)
        pts = np.vstack([c.pts for c in self.contours])
        self.cid = np.concatenate([np.full(len(c.pts), i) for i, c in enumerate(self.contours)])
        self.sall = np.concatenate([c.s for c in self.contours])
        self.tree = cKDTree(pts)
        self.phase = [float(self.rng.random()) for _ in self.contours]
        gx, gy = np.meshgrid(np.arange(W1), np.arange(H1), indexing="ij")
        self.eval_grid(np.stack([gx.ravel(), gy.ravel()], 1).astype(float))

    def taper(self, ci, s):
        """1 on the full cliff, 0 in the middle of a ramp."""
        T = np.ones(len(s))
        for i, ct in enumerate(self.contours):
            sel = ci == i
            if not sel.any() or not ct.gaps:
                continue
            Ti = np.ones(sel.sum())
            for g in ct.gaps:
                d = np.abs(s[sel] - g)
                if ct.closed:
                    d = np.minimum(d, ct.L - d)
                uu = np.clip(0.95 * (1.0 - (d - ct.half_open) / ct.taper), 0.0, 0.99)       # position along the end piece
                Ti = np.minimum(Ti, np.interp(uu, self.rf_ext_u, self.rf_ext))
            T[sel] = Ti
        return T

    def eval_grid(self, P):
        dist, idx = self.tree.query(P)
        sgn = np.sign(ndi.map_coordinates(self.Ds, [P[:, 0], P[:, 1]], order=1, mode="nearest"))
        sgn[sgn == 0] = 1
        d = dist * sgn
        ci = self.cid[idx]
        s = self.sall[idx]
        T = self.taper(ci, s)
        gd = np.full(len(s), 1e9)
        for i, ct in enumerate(self.contours):
            sel = ci == i
            for g in ct.gaps:
                dd = np.abs(s[sel] - g)
                if ct.closed:
                    dd = np.minimum(dd, ct.L - dd)
                gd[sel] = np.minimum(gd[sel], dd)
        Tc = np.maximum(T, 0.35)                           # the face narrows with the rock of the end piece (its foot stays put)
        lin = np.clip((d + self.w / 2) / (self.w * Tc), 0, 1)
        Sc = 0.65 * lin + 0.35 * _ss(lin)
        Sr = _ss(np.clip(0.5 + d / self.wr, 0, 1))
        S = T * Sc + (1 - T) * Sr
        shape = (self.W1, self.H1)
        self.d, self.ci, self.s, self.T = d.reshape(shape), ci.reshape(shape), s.reshape(shape), T.reshape(shape)
        self.S = S.reshape(shape)
        self.height = self.S * self.step
        self.gd = gd.reshape(shape)
        ue = np.clip(0.95 * (1.0 - (gd - HO) / TP), 0.0, 0.99)
        self.ue = ue.reshape(shape)
        self.v = ((self.band[0] + self.band[1]) / 2 - (self.band[1] - self.band[0]) * d / self.wt).reshape(shape)             # raw: <0.08 top lip ... >0.92 bottom lip
        u = np.zeros(len(P))
        for i, ct in enumerate(self.contours):
            sel = ci == i
            u[sel] = ct.k * s[sel] + self.phase[i]
        self.u = u.reshape(shape)

    # ------------------------------------------------------------------
    max_stretch = 1.8

    def stretch(self, x, y, uv):
        return cube_stretch(self.height, x, y, uv)

    def _u_on(self, ci, P):
        """u of points P projected onto contour ci only"""
        ct = self.contours[ci]
        if not hasattr(ct, "tree"):
            ct.tree = cKDTree(ct.pts)
        _, idx = ct.tree.query(P)
        return ct.k * ct.s[idx] + self.phase[ci]

    def rock_cubes(self, x_range, y_range, exclude=None):
        """{(x,y): (kind, [(u,v) x4])}; kind 'body' = straight face texture, 'end' = the tapering end piece
        (rock narrows to a point where the cliff meets a ramp). Corner order (x,y),(x+1,y),(x,y+1),(x+1,y+1)."""
        out = {}
        self.dropped = 0
        self.why = {}
        ho, tp = HO, TP
        tip = ho + 1.5                                  # where the point of the end piece sits
        for x in range(*x_range):
            for y in range(*y_range):
                cc = [(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)]
                if exclude is not None and any(exclude[a, b] for a, b in cc):
                    self.why[(x, y)] = 'excl'
                    continue
                vr = np.array([self.v[a, b] for a, b in cc])
                if vr.max() < (self.band[0] - 0.01 if self.desert else self.band[0] + 0.12) or vr.min() > self.band[1] + 0.01:
                    continue                      # no rock in this cube
                clamped = vr.min() < -0.03 or vr.max() > 1.03
                Ts = np.array([self.T[a, b] for a, b in cc])
                end = Ts.max() < 0.999
                if end and (Ts < 0.03).sum() >= 2:
                    self.why[(x, y)] = 'tip'
                    continue                      # beyond the point of the end piece: plain grass
                cids = [int(self.ci[a, b]) for a, b in cc]
                ds = np.abs([self.d[a, b] for a, b in cc])
                c0 = cids[int(np.argmin(ds))]
                ct = self.contours[c0]
                if end:
                    us = np.clip([self.ue[a, b] for a, b in cc], 0.0, 1.0)
                else:
                    if len(set(cids)) > 1:
                        P = np.array(cc, dtype=float)
                        us = self._u_on(c0, P)
                    else:
                        us = np.array([self.u[a, b] for a, b in cc])
                    if ct.closed:
                        per = ct.period
                        us = us[0] + ((us - us[0] + per / 2) % per) - per / 2
                    if us.max() - us.min() > 0.9:
                        continue
                    us = us - np.floor(us.min())
                vr = np.clip(vr, 0.0, 1.0)
                if self.stretch(x, y, [(us[i], vr[i]) for i in range(4)]) > (3.0 if end else self.max_stretch):
                    self.why[(x, y)] = 'stretch %.1f' % self.stretch(x, y, [(us[i], vr[i]) for i in range(4)])
                    self.dropped += 1             # would smear the grass lip over the cube: leave it to plain grass
                    continue
                out[(x, y)] = ("end" if end else "body", [(float(us[i]), float(vr[i])) for i in range(4)])
        return out
