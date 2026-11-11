"""
rogen - generate Ragnarok Online maps (.rsw / .gnd / .gat) that open in BrowEdit3.

Units (everything is in "cubes" unless said otherwise):
    1 cube = 1 terrain square = 10 world units = 2x2 GAT cells.
    Positions are (u, v): u runs west->east (0..width), v runs north->south (0..height),
    exactly like the cube columns/rows in the .gnd file.
    Heights are world units, positive = up (a tree is ~ 20-30 high, a hill 20-40).

Typical use:

    from rogen import MapGen
    m = MapGen("my_field", 80, 80, seed=1)
    grass = m.add_texture("backside\\\\grass.bmp")        # first texture is the default
    m.noise(scale=18, amplitude=12)
    m.river([(0, 40), (40, 30), (80, 45)], width=6, depth=14)
    m.water(level=-3)
    m.path([(10, 70), (40, 40), (70, 10)], width=3, texture=m.add_texture("..."))
    m.scatter(["tree1.rsm", "tree2.rsm"], count=120, region=(5, 5, 75, 75), min_dist=3,
              footprint=(0.8, 0.8), avoid=["water", "path"])
    m.save("out")

The generated files are valid BrowEdit3 files.  Open the .rsw in BrowEdit3, then
  * Object Edit -> "Smart GAT: recalculate all models" for the exact GAT of every model,
  * run the lightmapper for shadows, and save (that also recalculates the quadtree).
"""
import math
import os
import json
import random
import struct

import numpy as np

# GAT cell types
GAT_WALKABLE = 0
GAT_BLOCKED = 1
GAT_WATER = 3
GAT_CLIFF = 5


def _smoothstep(t):
    t = np.clip(t, 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _cstr(s, n):
    b = s.encode("latin-1", "replace")[: n - 1] if n > 0 else b""
    return b + b"\0" * (n - len(b))


class Model:
    def __init__(self, file, u, v, rot, scale, name, footprint, radius, height_offset, gat_type, ground):
        self.file = file
        self.u = u
        self.v = v
        self.rot = rot
        self.scale = scale
        self.name = name
        self.footprint = footprint      # (width, depth) in cubes, or None
        self.radius = radius            # circle footprint radius in cubes, or None
        self.height_offset = height_offset
        self.gat_type = gat_type        # -1 = default, otherwise a forced gat type for this model
        self.ground = ground            # True: sits on terrain


class MapGen:
    def __init__(self, name, width, height, seed=0, base_height=0.0):
        self.name = name
        self.width = int(width)
        self.height = int(height)
        self.rng = np.random.default_rng(seed)
        self.pyrng = random.Random(seed)
        self.seed = seed
        W, H = self.width, self.height
        # heights of the cube corners, positive = up
        self.base = np.full((W + 1, H + 1), float(base_height))
        # per cube offset, a difference between neighbours becomes a cliff wall
        self.terrace = np.zeros((W, H))
        self.textures = []
        self.cube_tex = np.zeros((W, H), dtype=int)
        self.wall_tex = None
        self.masks = {"path": np.zeros((W, H)), "water": np.zeros((W, H))}
        self.water_level = None         # world units, positive = up
        self.water_params = dict(type=0, amplitude=1.0, wave_speed=2.0, wave_pitch=50.0, anim_speed=3)
        self.sun = dict(longitude=45, latitude=45, diffuse=(1.0, 1.0, 1.0), ambient=(0.8, 0.8, 0.8), intensity=0.5)
        self.corner_path = np.zeros((W + 1, H + 1), dtype=bool)   # cube corners on a path
        self.cube_uv = {}               # (x,y) -> (texture index, 4 (s,t) corner uvs) overrides, live link only
        self.models = []
        self.lights = []
        self.sounds = []
        self.effects = []
        self.cliff_gat_diff = 6.0       # height difference between neighbouring cubes that blocks the GAT
        self.slope_gat = 1.1            # slope (height per world unit) above which a GAT cell is blocked
        self.deep_water = 25.0          # water deeper than this is not walkable
        self._order = []                # objects in file order

    # ------------------------------------------------------------------ terrain
    def _grid(self):
        W, H = self.width, self.height
        gx, gy = np.meshgrid(np.arange(W + 1, dtype=float), np.arange(H + 1, dtype=float), indexing="ij")
        return gx, gy

    def _value_noise(self, period, rng):
        W, H = self.width, self.height
        gw = int(W / period) + 3
        gh = int(H / period) + 3
        g = rng.random((gw, gh))
        xs = np.arange(W + 1) / period
        ys = np.arange(H + 1) / period
        x0 = np.floor(xs).astype(int)
        y0 = np.floor(ys).astype(int)
        fx = _smoothstep(xs - x0)[:, None]
        fy = _smoothstep(ys - y0)[None, :]
        a = g[x0][:, y0]
        b = g[x0 + 1][:, y0]
        c = g[x0][:, y0 + 1]
        d = g[x0 + 1][:, y0 + 1]
        return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy

    def noise(self, scale=16.0, amplitude=10.0, octaves=4, persistence=0.5, seed=None, region=None):
        """Adds fractal noise (hills and dents). scale = size of the largest bumps in cubes."""
        rng = np.random.default_rng(self.seed + 17 if seed is None else seed)
        total = np.zeros_like(self.base)
        amp, period, norm = 1.0, float(scale), 0.0
        for _ in range(octaves):
            total += amp * (self._value_noise(max(period, 1.0), rng) - 0.5) * 2.0
            norm += amp
            amp *= persistence
            period /= 2.0
        total = total / norm * amplitude
        if region:
            total = total * self._region_weight(region)
        self.base += total
        return self

    def _region_weight(self, region, blend=4.0):
        u0, v0, u1, v1 = region
        gx, gy = self._grid()
        dx = np.minimum(gx - u0, u1 - gx)
        dy = np.minimum(gy - v0, v1 - gy)
        return _smoothstep(np.minimum(dx, dy) / max(blend, 1e-6))

    def hill(self, u, v, radius, height, sharpness=1.0):
        """A round hill (negative height = a pit)."""
        gx, gy = self._grid()
        d = np.hypot(gx - u, gy - v) / radius
        self.base += height * _smoothstep(1.0 - d) ** sharpness
        return self

    def plateau(self, u, v, radius, height, edge=3.0):
        """Flat raised (or lowered) circle with smooth edges; height is absolute."""
        gx, gy = self._grid()
        d = np.hypot(gx - u, gy - v)
        w = _smoothstep((radius + edge - d) / max(edge, 1e-6))
        self.base = self.base * (1 - w) + height * w
        return self

    def flatten(self, u0, v0, u1, v1, height=None, blend=2.0):
        """Flattens a rectangle (e.g. under a house or town square). height=None uses the average."""
        gx, gy = self._grid()
        inside = (gx >= u0) & (gx <= u1) & (gy >= v0) & (gy <= v1)
        if height is None:
            height = float(self.base[inside].mean()) if inside.any() else 0.0
        dx = np.maximum(np.maximum(u0 - gx, gx - u1), 0)
        dy = np.maximum(np.maximum(v0 - gy, gy - v1), 0)
        w = 1.0 - _smoothstep(np.hypot(dx, dy) / max(blend, 1e-6))
        self.base = self.base * (1 - w) + height * w
        return self

    def lake(self, u, v, radius, depth, irregular=0.25):
        """A round, slightly irregular basin. Combine with water(level=...)."""
        gx, gy = self._grid()
        n = (self._value_noise(max(radius / 2.0, 2.0), np.random.default_rng(self.seed + int(u * 7 + v * 13))) - 0.5) * 2
        d = np.hypot(gx - u, gy - v) / (radius * (1.0 + irregular * n))
        self.base -= depth * _smoothstep(1.0 - d)
        return self

    def _dist_to_polyline(self, points, gx, gy):
        best = np.full(gx.shape, 1e9)
        for (ax, ay), (bx, by) in zip(points[:-1], points[1:]):
            abx, aby = bx - ax, by - ay
            l2 = abx * abx + aby * aby
            t = np.clip(((gx - ax) * abx + (gy - ay) * aby) / (l2 if l2 else 1.0), 0, 1)
            best = np.minimum(best, np.hypot(gx - (ax + t * abx), gy - (ay + t * aby)))
        return best

    def river(self, points, width, depth, bank=3.0):
        """Carves a river bed along a polyline [(u,v),...]. Set water(level=...) to fill it."""
        gx, gy = self._grid()
        d = self._dist_to_polyline(points, gx, gy)
        w = _smoothstep((width / 2.0 + bank - d) / max(bank, 1e-6))
        core = _smoothstep((width / 2.0 - d) / max(width / 2.0, 1e-6) + 0.5)
        self.base -= depth * (0.35 * w + 0.65 * core)
        return self

    def path(self, points, width=3.0, texture=None, flatten=0.6):
        """Marks a walkable road along a polyline, smooths the ground under it and (optionally) paints it."""
        gx, gy = self._grid()
        d = self._dist_to_polyline(points, gx, gy)
        w = (1.0 - _smoothstep((d - width / 2.0) / 2.0)) * flatten
        blurred = self._blur(self.base, 3)
        self.base = self.base * (1 - w) + blurred * w
        cx, cy = np.meshgrid(np.arange(self.width) + 0.5, np.arange(self.height) + 0.5, indexing="ij")
        dc = self._dist_to_polyline(points, cx, cy)
        self.corner_path |= d <= width / 2.0
        mask = dc <= width / 2.0
        self.masks["path"] = np.maximum(self.masks["path"], mask.astype(float))
        if texture is not None:
            self.cube_tex[mask] = texture
        return self

    def terrace_rect(self, u0, v0, u1, v1, delta):
        """Raises (delta>0) or lowers a block of whole cubes; the height step becomes a cliff wall."""
        u0, v0, u1, v1 = int(round(u0)), int(round(v0)), int(round(u1)), int(round(v1))
        self.terrace[max(u0, 0):min(u1, self.width), max(v0, 0):min(v1, self.height)] += delta
        return self

    def _blur(self, a, n=1):
        for _ in range(n):
            p = np.pad(a, 1, mode="edge")
            a = (p[:-2, 1:-1] + p[2:, 1:-1] + p[1:-1, :-2] + p[1:-1, 2:] + 4 * a) / 8.0
        return a

    def smooth(self, iterations=1):
        self.base = self._blur(self.base, iterations)
        return self

    def border(self, margin=6, drop=-40.0):
        """Slopes the edge of the map down so the world ends in a natural cliff or sea."""
        gx, gy = self._grid()
        d = np.minimum(np.minimum(gx, self.width - gx), np.minimum(gy, self.height - gy))
        w = 1.0 - _smoothstep(d / max(margin, 1e-6))
        self.base += drop * w
        return self

    # ------------------------------------------------------------------ queries
    def _cube_corners(self, x, y):
        t = self.terrace[x, y]
        b = self.base
        # order: (x,y), (x+1,y), (x,y+1), (x+1,y+1)
        return (b[x, y] + t, b[x + 1, y] + t, b[x, y + 1] + t, b[x + 1, y + 1] + t)

    def height_at(self, u, v):
        """Terrain height (world units, up) under a position."""
        x = int(min(max(math.floor(u), 0), self.width - 1))
        y = int(min(max(math.floor(v), 0), self.height - 1))
        fu, fv = u - x, v - y
        h1, h2, h3, h4 = self._cube_corners(x, y)
        return (h1 * (1 - fu) + h2 * fu) * (1 - fv) + (h3 * (1 - fu) + h4 * fu) * fv

    def slope_map(self):
        """Steepness per cube (height difference per world unit)."""
        W, H = self.width, self.height
        s = np.zeros((W, H))
        for x in range(W):
            for y in range(H):
                c = self._cube_corners(x, y)
                dx = max(abs(c[1] - c[0]), abs(c[3] - c[2]))
                dy = max(abs(c[2] - c[0]), abs(c[3] - c[1]))
                s[x, y] = max(dx, dy) / 10.0
        return s

    def cube_height_map(self):
        W, H = self.width, self.height
        h = np.zeros((W, H))
        for x in range(W):
            for y in range(H):
                h[x, y] = sum(self._cube_corners(x, y)) / 4.0
        return h

    # ------------------------------------------------------------------ tile level texturing (live link)
    def set_tile(self, x, y, texture, corner_uv):
        """Own tile for one cube. corner_uv = [(s,t)] for corners h1 (x,y), h2 (x+1,y), h3 (x,y+1), h4 (x+1,y+1).
        A normal ground tile is [(0,1),(1,1),(0,0),(1,0)]."""
        self.cube_uv[(int(x), int(y))] = (int(texture), [tuple(map(float, c)) for c in corner_uv])

    def autotile_dirt(self, mask, dirt, edge, blob, fringe=0.5):
        """Seamless dirt paths with transition tiles: dirt = a full dirt texture, edge = a tile with a dirt strip on
        its left side (grass elsewhere), blob = an isolated dirt patch. mask = bool array (W,H) of dirt cubes."""
        W, H = self.width, self.height
        d = np.array(mask, dtype=bool)
        for _ in range(3):      # remove concave corners and 1 wide gaps so every grass cube touches dirt on one side at most
            n = np.zeros((W, H), dtype=int)
            n[1:, :] += d[:-1, :]; n[:-1, :] += d[1:, :]; n[:, 1:] += d[:, :-1]; n[:, :-1] += d[:, 1:]
            d = d | (n >= 2)
        self.masks["dirt"] = d.astype(float)
        full = [(0, 1), (1, 1), (0, 0), (1, 0)]
        corners = [(0, 0), (1, 0), (0, 1), (1, 1)]          # (px, py) of h1..h4
        for x in range(W):
            for y in range(H):
                nb = {k: (0 <= x + dx < W and 0 <= y + dy < H and d[x + dx, y + dy])
                      for k, (dx, dy) in dict(W=(-1, 0), E=(1, 0), N=(0, -1), S=(0, 1)).items()}
                cnt = sum(nb.values())
                if d[x, y]:
                    if cnt == 0:
                        self.set_tile(x, y, blob, full)
                    else:
                        self.set_tile(x, y, dirt, full)
                elif cnt == 1:
                    k = [k for k, v in nb.items() if v][0]
                    uv = []
                    for px, py in corners:
                        if k == "W": s_, t_ = px * fringe, 1 - py
                        elif k == "E": s_, t_ = (1 - px) * fringe, 1 - py
                        elif k == "N": s_, t_ = py * fringe, px
                        else: s_, t_ = (1 - py) * fringe, px
                        uv.append((s_, t_))
                    self.set_tile(x, y, edge, uv)
        return d

    def autotile_cliffs(self, cliff, min_slope=0.55, skip_mask=None):
        """Steep ground gets a cliff face texture (grass on top, rock below) turned to face downhill."""
        W, H = self.width, self.height
        hm = self.cube_height_map()
        slope = self.slope_map()
        corners = [(0, 0), (1, 0), (0, 1), (1, 1)]
        count = 0
        for x in range(W):
            for y in range(H):
                if slope[x, y] < min_slope or (skip_mask is not None and skip_mask[x, y]):
                    continue
                c = self._cube_corners(x, y)       # h1 (x,y) h2 (x+1,y) h3 (x,y+1) h4 (x+1,y+1), up positive
                gx_ = (c[1] + c[3]) - (c[0] + c[2])     # > 0: east is higher
                gy_ = (c[2] + c[3]) - (c[0] + c[1])     # > 0: south is higher
                uv = []
                for px, py in corners:
                    if abs(gx_) >= abs(gy_):
                        hi = 0 if gx_ < 0 else 1                  # px of the high side
                        t_ = 0.0 if px == hi else 1.0
                        s_ = float(py)
                    else:
                        hi = 0 if gy_ < 0 else 1
                        t_ = 0.0 if py == hi else 1.0
                        s_ = float(px)
                    uv.append((s_, t_))
                self.set_tile(x, y, cliff, uv)
                count += 1
        return count

    # ------------------------------------------------------------------ water
    def water(self, level, type=0, amplitude=1.0, wave_speed=2.0, wave_pitch=50.0, anim_speed=3):
        """Water surface height (world units, up). Cubes whose ground is below it count as 'water'."""
        self.water_level = float(level)
        self.water_params = dict(type=type, amplitude=amplitude, wave_speed=wave_speed,
                                 wave_pitch=wave_pitch, anim_speed=anim_speed)
        return self

    def _update_water_mask(self):
        if self.water_level is None:
            self.masks["water"][:] = 0
            return
        h = self.cube_height_map()
        self.masks["water"] = (h < self.water_level).astype(float)

    # ------------------------------------------------------------------ textures
    def add_texture(self, file, name=None):
        """Registers a texture (path below data\\texture, e.g. "backside\\\\grass.bmp"); returns its index."""
        file = file.replace("/", "\\")
        for i, (f, _) in enumerate(self.textures):
            if f == file:
                return i
        self.textures.append((file, name or os.path.splitext(os.path.basename(file.replace("\\", "/")))[0]))
        return len(self.textures) - 1

    def paint_all(self, texture):
        self.cube_tex[:] = texture
        return self

    def paint_rect(self, u0, v0, u1, v1, texture):
        self.cube_tex[max(int(u0), 0):min(int(math.ceil(u1)), self.width),
                      max(int(v0), 0):min(int(math.ceil(v1)), self.height)] = texture
        return self

    def paint_circle(self, u, v, radius, texture):
        cx, cy = np.meshgrid(np.arange(self.width) + 0.5, np.arange(self.height) + 0.5, indexing="ij")
        self.cube_tex[np.hypot(cx - u, cy - v) <= radius] = texture
        return self

    def paint_by_slope(self, texture, min_slope):
        self.cube_tex[self.slope_map() >= min_slope] = texture
        return self

    def paint_by_height(self, texture, min_height=-1e9, max_height=1e9):
        h = self.cube_height_map()
        self.cube_tex[(h >= min_height) & (h <= max_height)] = texture
        return self

    def paint_mask(self, mask_name, texture, threshold=0.5):
        self.cube_tex[self.masks[mask_name] >= threshold] = texture
        return self

    def paint_noise(self, texture, scale=8.0, coverage=0.3, seed=None, region=None):
        """Random patches of a texture (dirt patches in grass, flower meadows...)."""
        rng = np.random.default_rng(self.seed + 99 if seed is None else seed)
        n = self._value_noise(max(scale, 1.0), rng)[: self.width, : self.height]
        sel = n >= np.quantile(n, 1.0 - coverage)
        if region:
            u0, v0, u1, v1 = region
            cx, cy = np.meshgrid(np.arange(self.width) + 0.5, np.arange(self.height) + 0.5, indexing="ij")
            sel &= (cx >= u0) & (cx <= u1) & (cy >= v0) & (cy <= v1)
        self.cube_tex[sel] = texture
        return self

    def paint_shore(self, texture, width=2.0):
        """Textures the cubes just next to the water line (sand)."""
        if self.water_level is None:
            return self
        h = self.cube_height_map()
        band = (h >= self.water_level) & (h < self.water_level + width * 2.0)
        self.cube_tex[band] = texture
        return self

    def set_wall_texture(self, texture):
        self.wall_tex = texture
        return self

    # ------------------------------------------------------------------ objects
    def place(self, file, u, v, rot=(0, 0, 0), scale=(1, 1, 1), name=None, footprint=None, radius=None,
              height_offset=0.0, gat_type=-1, ground=True):
        """Places a model (file relative to data\\model, e.g. "frontera\\\\house.rsm").

        footprint = (width, depth) in cubes of the area the model blocks (GAT), radius = round footprint.
        rot = (x, y, z) degrees, y turns the model around the vertical axis."""
        if isinstance(rot, (int, float)):
            rot = (0, rot, 0)
        if isinstance(scale, (int, float)):
            scale = (scale, scale, scale)
        file = file.replace("/", "\\")
        idx = sum(1 for o in self._order if o[0] == "model") + 1
        m = Model(file, float(u), float(v), tuple(rot), tuple(scale), name or "%s_%03d" % (
            os.path.splitext(os.path.basename(file.replace("\\", "/")))[0][:30], idx),
                  footprint, radius, height_offset, gat_type, ground)
        self.models.append(m)
        self._order.append(("model", m))
        return m

    def _region_ok(self, u, v, avoid, max_slope, min_height, max_height):
        x = int(min(max(math.floor(u), 0), self.width - 1))
        y = int(min(max(math.floor(v), 0), self.height - 1))
        for a in avoid or []:
            if self.masks[a][x, y] >= 0.5:
                return False
        if max_slope is not None and self._slope[x, y] > max_slope:
            return False
        h = self.height_at(u, v)
        return min_height <= h <= max_height

    def scatter(self, files, count, region=None, min_dist=2.0, avoid=("water",), max_slope=0.9,
                min_height=-1e9, max_height=1e9, scale_range=(0.9, 1.2), rotate=True, footprint=None,
                radius=None, weights=None, gat_type=-1, mask=None, mask_min=0.5, attempts=30):
        """Random models over an area (trees, rocks, grass...).
        region=(u0,v0,u1,v1) or None for the whole map, avoid = mask names ("water", "path", ...),
        mask = only where this mask is >= mask_min."""
        self._update_water_mask()
        self._slope = self.slope_map()
        if isinstance(files, str):
            files = [files]
        u0, v0, u1, v1 = region or (0, 0, self.width, self.height)
        placed = [(m.u, m.v) for m in self.models]
        made = []
        for _ in range(count):
            for _try in range(attempts):
                u = self.pyrng.uniform(u0, u1)
                v = self.pyrng.uniform(v0, v1)
                if not self._region_ok(u, v, avoid, max_slope, min_height, max_height):
                    continue
                if mask is not None and self.masks[mask][int(min(u, self.width - 1)), int(min(v, self.height - 1))] < mask_min:
                    continue
                if any((u - pu) ** 2 + (v - pv) ** 2 < min_dist ** 2 for pu, pv in placed):
                    continue
                f = self.pyrng.choices(files, weights=weights)[0] if weights else self.pyrng.choice(files)
                s = self.pyrng.uniform(*scale_range)
                made.append(self.place(f, u, v, rot=(0, self.pyrng.uniform(0, 360) if rotate else 0, 0),
                                       scale=(s, s, s), footprint=footprint, radius=radius, gat_type=gat_type))
                placed.append((u, v))
                break
        return made

    def line(self, files, points, spacing=1.0, rotate_along=True, footprint=None, radius=None, scale=1.0,
             rot_offset=0.0, jitter=0.0):
        """Models in a row along a polyline (fences, lamp posts, walls). Turns them to follow the line."""
        if isinstance(files, str):
            files = [files]
        made = []
        carry = 0.0
        for (ax, ay), (bx, by) in zip(points[:-1], points[1:]):
            length = math.hypot(bx - ax, by - ay)
            if length == 0:
                continue
            ang = math.degrees(math.atan2(bx - ax, by - ay))
            t = carry
            while t <= length + 1e-9:
                f = t / length
                u = ax + (bx - ax) * f + self.pyrng.uniform(-jitter, jitter)
                v = ay + (by - ay) * f + self.pyrng.uniform(-jitter, jitter)
                made.append(self.place(self.pyrng.choice(files), u, v,
                                       rot=(0, (ang + rot_offset) if rotate_along else 0, 0), scale=scale,
                                       footprint=footprint, radius=radius))
                t += spacing
            carry = t - length
        return made

    def add_light(self, u, v, color=(1, 1, 1), range=60.0, above=12.0, name=None):
        self.lights.append(dict(u=u, v=v, color=color, range=range, above=above,
                                name=name or "light_%03d" % (len(self.lights) + 1)))
        self._order.append(("light", self.lights[-1]))

    def add_sound(self, file, u, v, volume=1.0, width=10, height=10, range=40.0, cycle=4.0, name=None):
        self.sounds.append(dict(u=u, v=v, file=file, volume=volume, width=width, height=height,
                                range=range, cycle=cycle, name=name or "sound_%03d" % (len(self.sounds) + 1)))
        self._order.append(("sound", self.sounds[-1]))

    def add_effect(self, effect_id, u, v, above=0.0, loop=1.0, params=(0, 0, 0, 0), name=None):
        self.effects.append(dict(u=u, v=v, id=effect_id, above=above, loop=loop, params=params,
                                 name=name or "effect_%03d" % (len(self.effects) + 1)))
        self._order.append(("effect", self.effects[-1]))

    def set_sun(self, longitude=45, latitude=45, diffuse=(1, 1, 1), ambient=(0.8, 0.8, 0.8), intensity=0.5):
        self.sun = dict(longitude=longitude, latitude=latitude, diffuse=diffuse, ambient=ambient, intensity=intensity)
        return self

    # ------------------------------------------------------------------ GAT
    def _build_gat(self):
        W, H = self.width, self.height
        gw, gh = W * 2, H * 2
        types = np.zeros((gw, gh), dtype=int)
        heights = np.zeros((gw, gh, 4))   # gnd convention: positive = down
        for gx in range(gw):
            for gy in range(gh):
                x, y = gx // 2, gy // 2
                h1, h2, h3, h4 = self._cube_corners(x, y)
                fu0, fu1 = (gx % 2) / 2.0, (gx % 2) / 2.0 + 0.5
                fv0, fv1 = (gy % 2) / 2.0, (gy % 2) / 2.0 + 0.5

                def at(fu, fv):
                    return (h1 * (1 - fu) + h2 * fu) * (1 - fv) + (h3 * (1 - fu) + h4 * fu) * fv
                heights[gx, gy] = (-at(fu0, fv0), -at(fu1, fv0), -at(fu0, fv1), -at(fu1, fv1))
        # slope: height change over one GAT cell (5 units)
        up = -heights
        slope = np.maximum(np.abs(up[:, :, 1] - up[:, :, 0]), np.abs(up[:, :, 2] - up[:, :, 0])) / 5.0
        slope = np.maximum(slope, np.abs(up[:, :, 3] - up[:, :, 1]) / 5.0)
        types[slope > self.slope_gat] = GAT_CLIFF
        # cliffs: block the upper cells along wall edges
        for x in range(W):
            for y in range(H):
                for dx, dy in ((1, 0), (0, 1)):
                    nx, ny = x + dx, y + dy
                    if nx >= W or ny >= H:
                        continue
                    a = self._cube_corners(x, y)
                    b = self._cube_corners(nx, ny)
                    if dx:
                        pairs = ((a[1], b[0]), (a[3], b[2]))
                    else:
                        pairs = ((a[2], b[0]), (a[3], b[1]))
                    diff = max(abs(p - q) for p, q in pairs)
                    if diff < self.cliff_gat_diff:
                        continue
                    upper_first = sum(p for p, _ in pairs) > sum(q for _, q in pairs)
                    ux, uy = (x, y) if upper_first else (nx, ny)
                    # the two GAT cells of the upper cube that touch the shared edge
                    sx, sy = (-dx if not upper_first else dx), (-dy if not upper_first else dy)
                    for k in range(2):
                        if dx:
                            gxx = ux * 2 + (1 if sx > 0 else 0)
                            gyy = uy * 2 + k
                        else:
                            gxx = ux * 2 + k
                            gyy = uy * 2 + (1 if sy > 0 else 0)
                        types[gxx, gyy] = GAT_CLIFF
        # water
        if self.water_level is not None:
            cell_up = -heights.mean(axis=2)
            wet = cell_up < self.water_level
            deep = cell_up < self.water_level - self.deep_water
            types[wet & (types == GAT_WALKABLE)] = GAT_WATER
            types[deep] = GAT_BLOCKED
        return types, heights

    def _footprint_polygon(self, m):
        if m.footprint is None and m.radius is None:
            return None
        if m.radius is not None:
            r = m.radius * max(m.scale[0], m.scale[2])
            pts = [(r * math.cos(a), r * math.sin(a)) for a in [i * math.pi / 8 for i in range(16)]]
        else:
            hw = m.footprint[0] * m.scale[0] / 2.0
            hd = m.footprint[1] * m.scale[2] / 2.0
            pts = [(-hw, -hd), (hw, -hd), (hw, hd), (-hw, hd)]
        a = math.radians(m.rot[1])
        c, s = math.cos(a), math.sin(a)
        return [(m.u + (lx * c + lz * s), m.v + (-lx * s + lz * c)) for lx, lz in pts]

    @staticmethod
    def _poly_overlaps_rect(poly, x0, y0, x1, y1):
        xs = [p[0] for p in poly]
        ys = [p[1] for p in poly]
        if max(xs) <= x0 or min(xs) >= x1 or max(ys) <= y0 or min(ys) >= y1:
            return False
        corners = [(x0, y0), (x1, y0), (x0, y1), (x1, y1)]
        n = len(poly)
        for i in range(n):
            ex, ey = poly[(i + 1) % n][0] - poly[i][0], poly[(i + 1) % n][1] - poly[i][1]
            nx, ny = -ey, ex
            ln = math.hypot(nx, ny)
            if ln < 1e-9:
                continue
            nx, ny = nx / ln, ny / ln
            pd = [nx * p[0] + ny * p[1] for p in poly]
            cd = [nx * c[0] + ny * c[1] for c in corners]
            if max(pd) <= min(cd) or min(pd) >= max(cd):
                return False
        return True

    def _apply_model_gat(self, types):
        """Blocks the GAT cells under every model footprint. Returns {model: [(x,y,orig,set),...]}"""
        owners = {}
        result = {}
        gw, gh = types.shape
        margin = 0.005
        for m in self.models:
            poly = self._footprint_polygon(m)
            if poly is None:
                continue
            gtype = m.gat_type if m.gat_type > -1 else GAT_BLOCKED
            xs = [p[0] for p in poly]
            ys = [p[1] for p in poly]
            tiles = []
            for tx in range(max(int(math.floor(min(xs) * 2)), 0), min(int(math.floor(max(xs) * 2)), gw - 1) + 1):
                for ty in range(max(int(math.floor(min(ys) * 2)), 0), min(int(math.floor(max(ys) * 2)), gh - 1) + 1):
                    if self._poly_overlaps_rect(poly, tx / 2.0 + margin, ty / 2.0 + margin,
                                                (tx + 1) / 2.0 - margin, (ty + 1) / 2.0 - margin):
                        tiles.append((tx, ty))
            entries = []
            for tx, ty in tiles:
                prev = owners.get((tx, ty))
                orig = prev if prev is not None else int(types[tx, ty])
                owners.setdefault((tx, ty), orig)
                types[tx, ty] = gtype
                entries.append((tx, ty, orig, gtype))
            result[id(m)] = entries
        return result

    # ------------------------------------------------------------------ writing
    def _build_gnd(self):
        W, H = self.width, self.height
        if not self.textures:
            self.add_texture("backside\\grass.bmp")
        wall_tex = self.wall_tex if self.wall_tex is not None else 0
        tiles = []
        tile_index = {}

        def tile(tex, wall=False):
            key = (tex, wall)
            if key not in tile_index:
                tile_index[key] = len(tiles)
                tiles.append(key)
            return tile_index[key]

        cubes = []
        for y in range(H):
            for x in range(W):
                h1, h2, h3, h4 = self._cube_corners(x, y)
                up = tile(int(self.cube_tex[x, y]))
                side = front = -1
                if x < W - 1:
                    a = self._cube_corners(x + 1, y)
                    if abs(h2 - a[0]) > 1e-3 or abs(h4 - a[2]) > 1e-3:
                        side = tile(wall_tex, True)
                if y < H - 1:
                    a = self._cube_corners(x, y + 1)
                    if abs(h3 - a[0]) > 1e-3 or abs(h4 - a[1]) > 1e-3:
                        front = tile(wall_tex, True)
                # gnd stores heights positive = down
                cubes.append((-h1, -h2, -h3, -h4, up, front, side))
        return tiles, cubes

    def _quadtree(self, gat_heights):
        W, H = self.width, self.height
        # gnd convention heights per cube (positive = down), min/max over corners and the gat cells
        cube_min = np.zeros((W, H))
        cube_max = np.zeros((W, H))
        for x in range(W):
            for y in range(H):
                c = [-v for v in self._cube_corners(x, y)]
                lo, hi = min(c), max(c)
                for k in range(4):
                    g = gat_heights[x * 2 + k % 2, y * 2 + (k >> 1)]
                    lo, hi = min(lo, g.min()), max(hi, g.max())
                if self.water_level is not None:
                    wave = -self.water_level - self.water_params["amplitude"]
                    if all(-v > wave for v in c):
                        lo, hi = min(lo, wave), max(hi, wave)
                cube_min[x, y], cube_max[x, y] = lo, hi
        # pre-order output: build tree first then flatten
        def build(x, z, w, d, level):
            n = {"x": x, "z": z, "w": w, "d": d, "level": level, "kids": []}
            if level < 5:
                n["kids"] = [build(x, z, w / 2, d / 2, level + 1), build(x + w / 2, z, w / 2, d / 2, level + 1),
                             build(x, z + d / 2, w / 2, d / 2, level + 1), build(x + w / 2, z + d / 2, w / 2, d / 2, level + 1)]
                n["min"] = min(k["min"] for k in n["kids"])
                n["max"] = max(k["max"] for k in n["kids"])
            else:
                mn, mx = 99999.0, -99999.0
                for cx in range(W // 2 + int(math.floor(x / 10)), W // 2 + int(math.ceil((x + w) / 10))):
                    for cy in range(H // 2 + int(math.floor(z / 10)), H // 2 + int(math.ceil((z + d) / 10))):
                        if 0 <= cx < W and 0 <= cy < H:
                            mn = min(mn, cube_min[cx, cy])
                            mx = max(mx, cube_max[cx, cy])
                n["min"], n["max"] = mn, mx
            return n

        root = build(-W * 5.0, -H * 5.0, W * 10 - 1.0, H * 10 - 1.0, 0)
        floats = []

        def emit(n):
            bmin = (n["x"], n["min"], n["z"])
            bmax = (n["x"] + n["w"], n["max"], n["z"] + n["d"])
            r0 = tuple((bmax[i] - bmin[i]) / 2.0 for i in range(3))
            r1 = tuple(bmax[i] - r0[i] for i in range(3))
            for vec in (bmax, bmin, r0, r1):
                floats.extend(vec)
            for k in n["kids"]:
                emit(k)
        emit(root)
        return floats

    def save(self, out_dir):
        os.makedirs(out_dir, exist_ok=True)
        W, H = self.width, self.height
        self._update_water_mask()
        tiles, cubes = self._build_gnd()
        types, gat_h = self._build_gat()
        model_tiles = self._apply_model_gat(types)

        base = os.path.join(out_dir, self.name)
        # ---- GND (version 1.7: water lives in the rsw)
        g = bytearray(b"GRGN" + bytes([1, 7]))
        g += struct.pack("<iifii", W, H, 10.0, len(self.textures), 80)
        for f, n in self.textures:
            g += _cstr(f, 40) + _cstr(n, 40)
        g += struct.pack("<iiii", 1, 8, 8, 1)
        g += bytes([255]) * 64 + bytes(192)         # one lightmap: no shadow, no color
        g += struct.pack("<i", len(tiles))
        wall_uv = ((0, 1, 0, 1), (0, 0, 1, 1))     # top of a wall at v=0
        ground_uv = ((0, 1, 0, 1), (1, 1, 0, 0))   # same orientation as BrowEdit's texture fill tool
        for tex, is_wall in tiles:
            uvs = wall_uv if is_wall else ground_uv
            g += struct.pack("<4f4f", *uvs[0], *uvs[1])
            g += struct.pack("<hH", tex, 0)
            g += bytes([255, 255, 255, 255])
        for (h1, h2, h3, h4, up, front, side) in cubes:
            g += struct.pack("<4f3i", h1, h2, h3, h4, up, front, side)
        open(base + ".gnd", "wb").write(bytes(g))

        # ---- GAT
        t = bytearray(b"GRAT" + struct.pack("<hii", 0x0201, W * 2, H * 2))
        for gy in range(H * 2):
            for gx in range(W * 2):
                t += struct.pack("<4fi", *gat_h[gx, gy], int(types[gx, gy]))
        open(base + ".gat", "wb").write(bytes(t))

        # ---- RSW (version 2.1)
        s = self.sun
        wp = self.water_params
        water_gnd = -(self.water_level if self.water_level is not None else -1000.0)
        r = bytearray(b"GRSW" + bytes([2, 1]))
        r += _cstr("", 40) + _cstr(self.name + ".gnd", 40) + _cstr(self.name + ".gat", 40) + _cstr("", 40)
        r += struct.pack("<fifff", water_gnd, wp["type"], wp["amplitude"], wp["wave_speed"], wp["wave_pitch"])
        r += struct.pack("<i", wp["anim_speed"])
        r += struct.pack("<ii", int(s["longitude"]), int(s["latitude"]))
        r += struct.pack("<3f3f", *s["diffuse"], *s["ambient"])
        r += struct.pack("<f", s["intensity"])
        half = int(max(W, H) * 5)
        r += struct.pack("<4i", -half, half, -half, half)

        def rsw_pos(u, v, up):
            return (10.0 * u - 5.0 * W, -up, 10.0 * v - 5.0 * H)

        r += struct.pack("<i", len(self._order))
        extra_models = []
        extra_lights = []
        for i, (kind, o) in enumerate(self._order):
            if kind == "model":
                up = self.height_at(o.u, o.v) + o.height_offset if o.ground else o.height_offset
                px, py, pz = rsw_pos(o.u, o.v, up)
                r += struct.pack("<i", 1) + _cstr(o.name, 40) + struct.pack("<ifi", 0, 1.0, 0)
                r += _cstr(o.file, 80) + _cstr("", 80)
                r += struct.pack("<3f3f3f", px, py, pz, *o.rot, *o.scale)
                entries = model_tiles.get(id(o), [])
                ex = dict(id=i, shadowStrength=1.0, gatCollision=True, gatStraightType=0, gatType=o.gat_type)
                if entries:
                    ex["autoGatTiles"] = [list(e) for e in entries]
                extra_models.append(ex)
            elif kind == "light":
                up = self.height_at(o["u"], o["v"]) + o["above"]
                px, py, pz = rsw_pos(o["u"], o["v"], up)
                r += struct.pack("<i", 2) + _cstr(o["name"], 80)
                r += struct.pack("<3f3ff", px, py, pz, *o["color"], o["range"])
            elif kind == "sound":
                up = self.height_at(o["u"], o["v"])
                px, py, pz = rsw_pos(o["u"], o["v"], up)
                r += struct.pack("<i", 3) + _cstr(o["name"], 80) + _cstr(o["file"], 80)
                r += struct.pack("<3ffiiff", px, py, pz, o["volume"], o["width"], o["height"], o["range"], o["cycle"])
            elif kind == "effect":
                up = self.height_at(o["u"], o["v"]) + o["above"]
                px, py, pz = rsw_pos(o["u"], o["v"], up)
                r += struct.pack("<i", 4) + _cstr(o["name"], 80)
                r += struct.pack("<3fi5f", px, py, pz, o["id"], o["loop"], *o["params"])
        floats = self._quadtree(gat_h)
        r += struct.pack("<%df" % len(floats), *floats)
        open(base + ".rsw", "wb").write(bytes(r))

        extra = {"model": extra_models, "light": []}
        open(base + ".rsw.extra.json", "w").write(json.dumps(extra, indent=1))
        return {k: base + k for k in (".rsw", ".gnd", ".gat", ".rsw.extra.json")}

    # ------------------------------------------------------------------ live mode
    def _rsw_pos(self, u, v, up):
        return (10.0 * u - 5.0 * self.width, -up, 10.0 * v - 5.0 * self.height)

    @staticmethod
    def _runs(values, y):
        """[[y, x0, x1, value], ...] runs of equal values in one row"""
        out = []
        start = 0
        for x in range(1, len(values) + 1):
            if x == len(values) or values[x] != values[start]:
                out.append([y, start, x, int(values[start])])
                start = x
        return out

    def live_commands(self, smart_gat=True, models_per_command=60, look_at=True):
        """The whole map as commands for BrowEdit3's live link (open a blank map of the same size first).
        Write them with save_live(); BrowEdit applies them step by step so the map builds up on screen."""
        if not self.textures:
            self.add_texture("backside\\grass.bmp")
        W, H = self.width, self.height
        self._update_water_mask()
        types, _ = self._build_gat()
        cmds = [{"cmd": "info"}]
        if look_at:
            cmds.append({"cmd": "camera", "u": W / 2, "v": H / 2, "distance": max(W, H) * 6})
        cmds.append({"cmd": "textures", "add": [f for f, _ in self.textures]})
        # terrain heights (gnd convention: positive = down)
        data = []
        for y in range(H):
            for x in range(W):
                data.extend(round(-c, 3) for c in self._cube_corners(x, y))
        hc = {"cmd": "heights", "x0": 0, "y0": 0, "w": W, "h": H, "data": data, "gat": True}
        if self.wall_tex is not None or (self.terrace != 0).any():
            hc["wall_texture"] = self.textures[self.wall_tex if self.wall_tex is not None else 0][0]
        cmds.append(hc)
        if self.water_level is not None:
            wp = self.water_params
            cmds.append({"cmd": "water", "height": -self.water_level, "type": wp["type"], "amplitude": wp["amplitude"],
                         "wave_speed": wp["wave_speed"], "wave_pitch": wp["wave_pitch"], "anim_speed": wp["anim_speed"]})
        # textures, one command per texture with row runs
        for ti, (f, _) in enumerate(self.textures):
            rows = []
            for y in range(H):
                x = 0
                while x < W:
                    if self.cube_tex[x, y] == ti:
                        x1 = x
                        while x1 < W and self.cube_tex[x1, y] == ti:
                            x1 += 1
                        rows.append([y, x, x1])
                        x = x1
                    else:
                        x += 1
            if rows:
                cmds.append({"cmd": "paint", "texture": f, "rows": rows})
        if self.cube_uv:
            lst = []
            for (x, y), (ti, uv) in sorted(self.cube_uv.items()):
                ref = self.textures[ti][0]
                if not ref.startswith("#"):
                    raise ValueError("tile textures must be given as '#index' of the open map's texture list")
                lst.append([x, y, int(ref[1:])] + [round(c, 4) for (a, b) in uv for c in (a, b)])
            cmds.append({"cmd": "tiles", "list": lst})
        # terrain gat (cliffs, water), models follow with their exact footprint
        rows = []
        for gy in range(H * 2):
            rows.extend(r for r in self._runs([types[gx, gy] for gx in range(W * 2)], gy) if r[3] != 0)
        if rows:
            cmds.append({"cmd": "gat", "rows": rows})
        groups = [(True, [m for m in self.models if not getattr(m, "nogat", False)]),
                  (False, [m for m in self.models if getattr(m, "nogat", False)])]
        chunks = [(flag, lst[i:i + models_per_command]) for flag, lst in groups for i in range(0, len(lst), models_per_command)]
        for flag, chunk in chunks:
            items = []
            for m in chunk:
                up = self.height_at(m.u, m.v) + m.height_offset if m.ground else m.height_offset
                items.append({"file": m.file, "name": m.name, "pos": [round(c, 3) for c in self._rsw_pos(m.u, m.v, up)],
                              "rot": list(m.rot), "scale": list(m.scale)})
            cmds.append({"cmd": "models", "items": items, "smart_gat": bool(smart_gat and flag)})
        return cmds

    def save_live(self, path, **kw):
        """Writes the live commands to a json file. Put it in BrowEdit's claude_link\\inbox folder."""
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
        with open(path, "w") as f:
            json.dump({"commands": self.live_commands(**kw)}, f, separators=(",", ":"))
        return path
