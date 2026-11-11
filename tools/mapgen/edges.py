"""Seamless dirt/grass edges: the boundary of the dirt area is a smooth curve; the edge texture (초원02, a dirt strip with a
clean organic edge) gets UVs from the distance to that curve (across) and the distance along it (v), so the edge line is
continuous over every cube border instead of being re-picked tile by tile."""
import numpy as np
from scipy import ndimage as ndi
from scipy.spatial import cKDTree
from cliffs import signed_field, extract_contours

EDGE_U = 0.13          # where the dirt/grass line sits in the texture (dirt for u < 0.13, and for u > 0.88 by wrapping)


class EdgeField:
    def __init__(self, field, W1, H1, rng, scale=0.25, u0=EDGE_U, sgn=-1.0, lo=-0.06, hi=0.85, fold=None, straddle=False):
        """field: smooth scalar, > 0 in the dirt, 0 on the edge (a distance, so the outline has no cube staircase)"""
        self.scale = scale
        self.straddle = straddle
        self.fold = fold                 # (units of u per cell, width of the dirt band): dirt side folds back and forth inside the band instead of clipping
        self.u0, self.sgn, self.lo, self.hi = u0, sgn, lo, hi
        self.Ds = field
        self.contours = extract_contours(self.Ds, min_len=8.0, sigma=1.5)
        self.W1, self.H1 = W1, H1
        phase = [float(rng.random()) for _ in self.contours]
        self.phase = phase
        if not self.contours:
            self.dd = np.where(field > 0, 5.0, -5.0); self.u = self.v = np.zeros((W1, H1)); self.ci = np.zeros((W1, H1), int)
            return
        pts = np.vstack([c.pts for c in self.contours])
        self.cid = np.concatenate([np.full(len(c.pts), i) for i, c in enumerate(self.contours)])
        self.sall = np.concatenate([c.s for c in self.contours])
        tree = cKDTree(pts)
        gx, gy = np.meshgrid(np.arange(W1), np.arange(H1), indexing="ij")
        P = np.stack([gx.ravel(), gy.ravel()], 1).astype(float)
        dist, idx = tree.query(P)
        sgn = np.where(self.Ds.ravel() >= 0, 1.0, -1.0)
        shape = (W1, H1)
        self.dd = (dist * sgn).reshape(shape)                # > 0 inside the dirt
        self.ci = self.cid[idx].reshape(shape)
        s = self.sall[idx]
        v = np.zeros(len(P))
        for i, ct in enumerate(self.contours):
            sel = self.cid[idx] == i
            v[sel] = ct.k * s[sel] + phase[i]
        self.v = v.reshape(shape)

    def _v_on(self, ci, P):
        ct = self.contours[ci]
        if not hasattr(ct, "tree"):
            ct.tree = cKDTree(ct.pts)
        _, idx = ct.tree.query(P)
        return ct.k * ct.s[idx] + self.phase[ci]

    def edge_cubes(self, x_range, y_range, skip=None):
        """{(x,y): [(u,v) x4]} for every cube the dirt/grass line passes through or touches."""
        out = {}
        if not self.contours:
            return out
        for x in range(*x_range):
            for y in range(*y_range):
                if skip is not None and (x, y) in skip:
                    continue
                cc = [(x, y), (x + 1, y), (x, y + 1), (x + 1, y + 1)]
                dd = np.array([self.dd[a, b] for a, b in cc])
                if self.straddle and (dd.min() >= 0 or dd.max() <= 0):
                    continue
                if dd.min() > 1.0 or dd.max() < -1.0:
                    continue
                cids = [int(self.ci[a, b]) for a, b in cc]
                c0 = cids[int(np.argmin(np.abs(dd)))]
                if len(set(cids)) > 1:
                    vs = self._v_on(c0, np.array(cc, dtype=float))
                else:
                    vs = np.array([self.v[a, b] for a, b in cc])
                ct = self.contours[c0]
                if ct.closed:
                    per = ct.period
                    vs = vs[0] + ((vs - vs[0] + per / 2) % per) - per / 2
                if vs.max() - vs.min() > 0.6:
                    continue
                vs = vs - np.floor(vs.min())
                us = self.u0 + self.sgn * self.scale * dd
                if self.fold is not None:
                    fs, fw = self.fold
                    t = np.where(dd > 0, fs * dd, 0.0)
                    f = fw - np.abs(np.mod(t, 2 * fw) - fw)
                    us = np.where(dd > 0, self.u0 - f, self.u0 + self.scale * (-dd))
                us = np.clip(us, self.lo, self.hi)         # stay inside the dirt strip: the far border of the texture is at 0.88
                out[(x, y)] = [(float(us[i]), float(vs[i])) for i in range(4)]
        return out
