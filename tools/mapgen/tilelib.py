"""Seamless transition tiling from the transition textures (grass / dirt / rock) of the roo_prtf set.

Every texture slice (a quarter, half or the whole bitmap, in all 8 orientations) is a candidate tile. For each one the
material (grass, dirt, rock) is sampled on a 5x5 grid, so tiles can be chosen that match the wanted material at the four
cube corners and continue the outline of their left and top neighbours.
Materials: 0 grass, 1 dirt, 2 rock."""
import os
import numpy as np
from PIL import Image

GRASS, DIRT, ROCK = 0, 1, 2
N = 5      # samples per tile edge


DESERT = False      # desert sets: GRASS = orange sand (base), DIRT = pale sand (paths), ROCK = grey rock


def classify(img):
    a = np.asarray(img.convert("RGB"), dtype=int)
    r, g, b = a[..., 0], a[..., 1], a[..., 2]
    m = np.zeros(r.shape, dtype=np.int8)
    if DESERT:
        mx = a.max(-1); mn = a.min(-1)
        sat = (mx - mn) / np.maximum(mx, 1)
        rock = (sat < 0.43) & (r < 205)
        m[rock] = ROCK
        m[~rock & (r >= 205)] = DIRT
        from scipy import ndimage as _ndi
        return _ndi.median_filter(m, size=7, mode='wrap')
    greenish = (g > r + 8) & (g > b + 40)
    mx = np.maximum(np.maximum(r, g), b); mn = np.minimum(np.minimum(r, g), b)
    sat = (mx - mn) / np.maximum(mx, 1)
    rockish = ~greenish & (sat < 0.235) & (r < 207)
    m[rockish] = ROCK
    m[~greenish & ~rockish] = DIRT
    return m


def _orient(k, a, b):
    """8 orientations of the unit square: returns the texture coordinates (s,t) in 0..1 for cube coordinates (a,b)."""
    if k & 4:
        a, b = b, a
    if k & 1:
        a = 1 - a
    if k & 2:
        b = 1 - b
    return a, b


class TileLib:
    def __init__(self, textures, scales=(0.25, 0.5, 1.0), plain=None):
        self.plain = plain or {}        # material -> texture index used for plain areas
        """textures = {map texture index: path of a bitmap file}"""
        self.cands = []          # (texture index, [4 (s,t)] for h1..h4)
        corners, left, right, top, bottom, full, pure, has, tl, meta = [], [], [], [], [], [], [], [], [], []
        pts = np.linspace(0, 1, N)
        for ti, path in textures.items():
            img = Image.open(path)
            mat = classify(img)
            H, W = mat.shape
            tc = np.bincount(mat.ravel(), minlength=3)
            tpure = bool(tc.max() >= 0.9995 * mat.size)
            seen = set()
            for sc in scales:
                steps = [0.0] if sc >= 1.0 else [i * 0.25 for i in range(int(round((1 - sc) / 0.25)) + 1)]
                for ox in steps:
                    for oy in steps:
                        for k in range(8):
                            grid = np.zeros((N, N), dtype=np.int8)       # [i over a][j over b]
                            for i, a in enumerate(pts):
                                for j, b in enumerate(pts):
                                    s, t = _orient(k, a, b)
                                    px = int(min(W - 1, max(0, (ox + s * sc) * W)))
                                    py = int(min(H - 1, max(0, (oy + t * sc) * H)))
                                    win = mat[max(py - 3, 0):py + 4, max(px - 3, 0):px + 4]
                                    grid[i, j] = np.bincount(win.ravel(), minlength=3).argmax()
                            key = (ti, sc, ox, oy, k)
                            uv = [(ox + s * sc, oy + t * sc) for (s, t) in (_orient(k, a, b) for a, b in ((0, 0), (1, 0), (0, 1), (1, 1)))]
                            if key in seen:
                                continue
                            seen.add(key)
                            self.cands.append((ti, uv))
                            reg = mat[int(oy * H):int(round((oy + sc) * H)), int(ox * W):int(round((ox + sc) * W))]
                            cnt = np.bincount(reg.ravel(), minlength=3)
                            pure.append(tpure); tl.append(tpure); meta.append((ti, sc, ox, oy, k))
                            has.append(sum(1 << m_ for m_ in range(3) if cnt[m_] >= 0.01 * reg.size))
                            full.append(int(cnt.argmax()) if cnt.max() >= 0.997 * reg.size else -1)
                            corners.append([grid[0, 0], grid[N - 1, 0], grid[0, N - 1], grid[N - 1, N - 1]])
                            left.append(grid[0, :]); right.append(grid[N - 1, :])
                            top.append(grid[:, 0]); bottom.append(grid[:, N - 1])
        self.corners = np.array(corners); self.left = np.array(left); self.right = np.array(right)
        self.top = np.array(top); self.bottom = np.array(bottom)
        self.meta = {m_: i for i, m_ in enumerate(meta)}
        self.pure = np.array(pure)
        self.tile_ok = np.array(tl)          # seamless textures may wrap around (0.75..1 then 0..0.25)
        self.uv = np.array([[list(c) for c in uv] for _, uv in self.cands])   # (N,4,2)
        self.tex = np.array([t for t, _ in self.cands])
        self.has = np.array(has)
        self.full = np.array(full)       # material when the whole tile is one material, else -1


    def _cont(self, prev_idx, a_idx, b_idx, pa, pb):
        """True where candidate corners a_idx,b_idx continue the previous tile's corners pa,pb (same texture, same uv)."""
        d1 = self.uv[:, a_idx, :] - self.uv[prev_idx, pa, :]
        d2 = self.uv[:, b_idx, :] - self.uv[prev_idx, pb, :]
        same = self.tex == self.tex[prev_idx]
        exact = (np.abs(d1) < 1e-6).all(1) & (np.abs(d2) < 1e-6).all(1)
        wrap = (np.abs(d1 - np.round(d1)) < 1e-6).all(1) & (np.abs(d2 - np.round(d2)) < 1e-6).all(1) & self.tile_ok
        return same & (exact | wrap)

    def solve(self, corner_field, rng, fixed=(), w_corner=10.0, w_edge=2.0, noise=0.2, w_cont=8.0):
        """corner_field = (W+1, H+1) ints (material at every cube corner). Returns {(x,y): (texture, uv)}."""
        W, H = corner_field.shape[0] - 1, corner_field.shape[1] - 1
        out = {}
        chosen = {}
        fixed = set(fixed)
        for y in range(H):
            for x in range(W):
                if (x, y) in fixed:
                    continue
                want = np.array([corner_field[x, y], corner_field[x + 1, y], corner_field[x, y + 1], corner_field[x + 1, y + 1]])
                cost = w_corner * (self.corners != want).sum(1).astype(float)
                wb = 0
                for m_ in want:
                    wb |= 1 << int(m_)
                cost += 60.0 * ((self.has & ~wb) != 0)       # no materials that are not wanted here
                if (want == want[0]).all():      # plain area: no stray patches inside
                    cost += 25.0 * (self.full != want[0]) + 25.0 * (~self.pure)
                if x > 0 and (x - 1, y) in chosen:
                    cost += w_edge * (self.left != self.right[chosen[(x - 1, y)]]).sum(1)
                    cost -= w_cont * self._cont(chosen[(x - 1, y)], 0, 2, 1, 3)
                if y > 0 and (x, y - 1) in chosen:
                    cost += w_edge * (self.top != self.bottom[chosen[(x, y - 1)]]).sum(1)
                    cost -= w_cont * self._cont(chosen[(x, y - 1)], 0, 1, 2, 3)
                if (want == want[0]).all():     # plain area: one global 4x4 quarter pattern, so grass is one seamless block
                    tex_ = self.plain.get(int(want[0]))
                    j = self.meta.get((tex_, 0.25, (x % 4) * 0.25, (y % 4) * 0.25, 0)) if tex_ is not None else None
                    if j is not None:
                        cost[j] -= 4.0
                cost += rng.random(len(cost)) * noise
                # plain material: favour flat tiles so grass stays calm
                i = int(cost.argmin())
                chosen[(x, y)] = i
                out[(x, y)] = self.cands[i]
        return out


def render(m, tex_paths, x0, y0, w, h, out, S=12, seams=False):
    """Top down preview of a MapGen's tiles (texture only, no heights), same sampling as BrowEdit's tiles."""
    tex = {i: np.asarray(Image.open(p).convert("RGB").resize((128, 128))) for i, p in tex_paths.items()}
    img = np.zeros((h * S, w * S, 3), dtype=np.uint8)
    img[:] = (255, 0, 255)
    a = (np.arange(S) + 0.5) / S
    fa, fb = np.meshgrid(a, a)
    for x in range(x0, min(x0 + w, m.width)):
        for y in range(y0, min(y0 + h, m.height)):
            if (x, y) not in m.cube_uv:
                continue
            ti, uv = m.cube_uv[(x, y)]
            ti = int(m.textures[ti][0][1:]) if m.textures[ti][0].startswith('#') else ti
            u = [c[0] for c in uv]; v = [c[1] for c in uv]
            uu = (u[0] * (1 - fa) + u[1] * fa) * (1 - fb) + (u[2] * (1 - fa) + u[3] * fa) * fb
            vv = (v[0] * (1 - fa) + v[1] * fa) * (1 - fb) + (v[2] * (1 - fa) + v[3] * fa) * fb
            px = np.clip((uu * 128).astype(int), 0, 127); py = np.clip((vv * 128).astype(int), 0, 127)
            img[(y - y0) * S:(y - y0 + 1) * S, (x - x0) * S:(x - x0 + 1) * S] = tex[ti][py, px]
    if seams:
        for x in range(x0, min(x0 + w, m.width)):
            for y in range(y0, min(y0 + h, m.height)):
                a = m.cube_uv.get((x, y))
                a = (m.textures[a[0]][0], a[1]) if a else a
                for dx, dy, ca, cb, na, nb in ((1, 0, 1, 3, 0, 2), (0, 1, 2, 3, 0, 1)):
                    b = m.cube_uv.get((x + dx, y + dy))
                    b = (m.textures[b[0]][0], b[1]) if b else b
                    if a is None or b is None:
                        continue
                    same = a[0] == b[0] and all(abs(a[1][i][k] - b[1][j][k]) < 1e-6 for i, j in ((ca, na), (cb, nb)) for k in (0, 1))
                    if not same:
                        px0 = (x - x0 + dx) * S; py0 = (y - y0 + dy) * S
                        if dx: img[py0:py0 + S, px0 - 1:px0 + 1] = (255, 0, 0)
                        else: img[py0 - 1:py0 + 1, px0:px0 + S] = (255, 0, 0)
    Image.fromarray(img).save(out)
