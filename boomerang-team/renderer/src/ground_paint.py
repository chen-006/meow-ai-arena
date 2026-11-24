# -*- coding: utf-8 -*-
"""地面贴图：按地图数据画一张"手绘感"的草甸地面（venv 的 Python：numpy / scipy / PIL）。
完全由地图数据驱动：格子、格子边长、出生点。画的内容：
  深浅草色的大色斑 → 人常走的地方踩出来的土路（出生点、中心之间的最短路，窄桥自然成路）→
  没人去的角落更深更密的草和三叶草 → 草丛格的深色草底 → 石头脚下的接触暗部 → 岛沿一圈亮边 → 细碎的草笔触。
贴图范围：[-MG, Wm+MG] × [-MG, Hm+MG]（米），每米 PPM 像素。
"""
import heapq
import math

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage

PPM = 80
MG = 0.6
VERSION = 4          # 画法改了就加一，旧缓存的地面贴图会自动重画

C = dict(
    mid=(134, 196, 78), light=(172, 214, 88), dark=(92, 168, 74), lush=(70, 150, 72),
    rim=(196, 226, 112), dirt=(226, 196, 132), dirt_hi=(238, 214, 156), dirt_lo=(196, 160, 104),
    tall=(62, 138, 70), clover=(84, 162, 86),
    dirt_rim=(104, 150, 70), tick_light=(206, 236, 124, 95), tick_dark=(64, 140, 66, 85),
    pebble_a=(176, 146, 104, 200), pebble_b=(246, 228, 180, 210),
)


def _noise(shape, cell_px, seed):
    rng = np.random.default_rng(seed)
    cs = (max(2, int(shape[0] / cell_px) + 3), max(2, int(shape[1] / cell_px) + 3))
    base = rng.uniform(0, 1, cs).astype(np.float32)
    big = ndimage.zoom(base, (shape[0] / (cs[0] - 2), shape[1] / (cs[1] - 2)), order=3)
    return big[:shape[0], :shape[1]]


def _ss(x, a, b):
    t = np.clip((x - a) / (b - a + 1e-9), 0, 1)
    return t * t * (3 - 2 * t)


def _mix(img, col, f):
    f = f[..., None]
    return img * (1 - f) + np.array(col, dtype=np.float32)[None, None, :] * f


def _dijkstra(walk, src, dst, cost):
    H, W = walk.shape
    INF = 1e18
    dist = np.full((H, W), INF)
    prev = {}
    dist[src[1], src[0]] = 0
    pq = [(0.0, src)]
    while pq:
        d, (x, y) = heapq.heappop(pq)
        if (x, y) == dst:
            break
        if d > dist[y, x]:
            continue
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                if dx == 0 and dy == 0:
                    continue
                nx, ny = x + dx, y + dy
                if not (0 <= nx < W and 0 <= ny < H) or not walk[ny, nx]:
                    continue
                if dx and dy and not (walk[y, nx] and walk[ny, x]):
                    continue
                nd = d + math.hypot(dx, dy) * cost[ny, nx]
                if nd < dist[ny, nx]:
                    dist[ny, nx] = nd
                    prev[(nx, ny)] = (x, y)
                    heapq.heappush(pq, (nd, (nx, ny)))
    if dst not in prev and dst != src:
        return None
    path = [dst]
    while path[-1] != src:
        path.append(prev[path[-1]])
    return path[::-1]


def _chaikin(pts, it=2):
    for _ in range(it):
        out = [pts[0]]
        for a, b in zip(pts[:-1], pts[1:]):
            out.append((a[0] * 0.75 + b[0] * 0.25, a[1] * 0.75 + b[1] * 0.25))
            out.append((a[0] * 0.25 + b[0] * 0.75, a[1] * 0.25 + b[1] * 0.75))
        out.append(pts[-1])
        pts = out
    return pts


def _bridges(solid, r_m):
    """窄桥：比 2*r_m 米窄、并且连着两块宽地的条带。返回 (掩码, [(cx, cy, ax, ay, 半长, 半宽)])，坐标是像素。"""
    k = max(1, int(r_m * PPM))
    opened = ndimage.binary_opening(solid, structure=np.ones((2 * k + 1, 2 * k + 1), bool))
    narrow = solid & ~opened
    wide, _ = ndimage.label(opened)
    lab, n = ndimage.label(narrow)
    mask = np.zeros_like(solid)
    out = []
    for i in range(1, n + 1):
        mk = lab == i
        if mk.sum() < (0.5 * PPM) ** 2:
            continue
        ring = ndimage.binary_dilation(mk, iterations=3) & opened
        if len(set(np.unique(wide[ring])) - {0}) < 2:
            continue                                   # 只连着一块地：是个半岛，不算桥
        ys, xs = np.where(mk)
        pts = np.stack([xs - xs.mean(), ys - ys.mean()], 1)
        w, v = np.linalg.eigh(np.cov(pts.T))
        ax = v[:, 1]
        t = pts @ ax
        u = pts @ np.array([-ax[1], ax[0]])
        mask |= mk
        out.append((float(xs.mean()), float(ys.mean()), float(ax[0]), float(ax[1]), float(np.percentile(np.abs(t), 92)),
                    float(np.percentile(np.abs(u), 95))))
    return mask, out


def _stamp(img, layer, solid=None):
    base = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8)).convert("RGBA")
    out = np.asarray(Image.alpha_composite(base, layer).convert("RGB"), dtype=np.float32)
    if solid is not None:
        out = out * solid[..., None] + img * (1 - solid[..., None])
    return out


def _enclosed_pits(g):
    """被陆地围住、不碰地图边的坑格（四连通）。和 liquid.cells(where=enclosed) 是同一个判断。"""
    pit = g == 2
    lab, n = ndimage.label(pit)
    out = np.zeros_like(pit)
    if n == 0:
        return out
    edge = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
    for i in range(1, n + 1):
        if i not in edge:
            out |= lab == i
    return out


def _runs(mask, axis):
    """每个格子所在的、沿 axis 方向连续为真的那一段有多长（格数）。axis=1 横向，0 纵向。"""
    m = mask if axis == 1 else mask.T
    out = np.zeros(m.shape, dtype=np.int32)
    for i in range(m.shape[0]):
        row = m[i]
        j = 0
        while j < len(row):
            if row[j]:
                k = j
                while k < len(row) and row[k]:
                    k += 1
                out[i, j:k] = k - j
                j = k
            else:
                j += 1
    return out if axis == 1 else out.T


def _slab_layer(want, rng, nx, ny, Wm, Hm, rowh=0.5, widths=(0.5, 0.5, 0.75, 1.0), inset=2.5, sq=False):
    """一排排石板（和 pave 同一种画法）：返回 (每块的色差 -1..1, 有石板的程度 0..1)。sq：错缝的方砖（宽 = 行高）。"""
    timg = Image.new("F", (nx, ny), 0.0)
    kimg = Image.new("L", (nx, ny), 0)
    td, kd = ImageDraw.Draw(timg), ImageDraw.Draw(kimg)
    yy0 = -MG
    row = 0
    while yy0 < Hm + MG:
        xx0 = -MG - (rowh * 0.5 * (row % 2) if sq else rng.uniform(0, 0.5))
        while xx0 < Wm + MG:
            wd = rowh if sq else float(rng.choice(widths))
            x0p, y0p = (xx0 + MG) * PPM, (yy0 + MG) * PPM
            x1p, y1p = (xx0 + wd + MG) * PPM, (yy0 + rowh + MG) * PPM
            cxp, cyp = int(min(nx - 1, max(0, (x0p + x1p) / 2))), int(min(ny - 1, max(0, (y0p + y1p) / 2)))
            if want[cyp, cxp] > rng.uniform(0.25, 0.6):
                td.rectangle((x0p, y0p, x1p, y1p), fill=float(rng.uniform(-1, 1)))
                kd.rectangle((x0p + inset, y0p + inset, x1p - inset, y1p - inset), fill=255)
            xx0 += wd
        yy0 += rowh
        row += 1
    tile = np.asarray(timg, dtype=np.float32)
    keep = ndimage.gaussian_filter(np.asarray(kimg, dtype=np.float32) / 255.0, 1.0)
    return tile, np.clip(keep * 1.25, 0, 1)


def _slab_color(C, tile, n_fine, ny, nx, key="slab"):
    scol = np.empty((ny, nx, 3), dtype=np.float32)
    scol[:] = np.array(C.get(key, C["dirt"]), dtype=np.float32)
    scol = _mix(scol, C.get(key + "_hi", C["dirt_hi"]), np.clip(tile, 0, 1) * 0.7)
    scol = _mix(scol, C.get(key + "_lo", C["dirt_lo"]), np.clip(-tile, 0, 1) * 0.6)
    scol *= (1 + (n_fine - 0.5) * 0.06)[..., None]
    return scol


def _ep12_layers(img, st, C, g, cell, inside, solid, fsolid, wall, tall_f, dirt, quiet, n_big, n_mid, n_sm, n_fine,
                 XX, YY, Wm, Hm, nx, ny, area, seed, out_png):
    """第 12 集三个主题的地面层（ash 火山口、deck 栈桥、castle 双堡）。用自己的随机数，不动旧主题的画法。
    返回 (img, 不画草笔触的掩码, 额外的辅助数据)。"""
    rng = np.random.default_rng(seed + 900)
    H, W = g.shape
    enc = _enclosed_pits(g)
    gy = np.clip(np.floor(YY / cell).astype(int), 0, H - 1)
    gx = np.clip(np.floor(XX / cell).astype(int), 0, W - 1)
    liq = enc[gy, gx] & inside
    d_liq = (ndimage.distance_transform_edt(~liq) / PPM) if liq.any() else np.full((ny, nx), 99.0, np.float32)
    notick = np.zeros((ny, nx), bool)
    extra = {}
    if st.get("ash"):
        # 灰烬：大片浅灰的灰烬地，夹着深色的玄武岩斑；裂缝里透出岩浆的光；岩浆湖边一圈烤红的热边
        img = _mix(img, C["light"], _ss(n_big * 0.55 + n_mid * 0.45, 0.52, 0.7) * fsolid * (1 - dirt * 0.5) * 0.6)
        img = _mix(img, C["dark"], _ss(n_mid * 0.7 + n_sm * 0.3, 0.4, 0.26) * fsolid * 0.45)
        crk = Image.new("L", (nx, ny), 0)
        cd2 = ImageDraw.Draw(crk)
        okp = fsolid * (1 - _ss(tall_f, 0.2, 0.5)) * (1 - _ss(dirt, 0.3, 0.6)) * (1 - wall)
        if liq.any():
            ring = ndimage.binary_dilation(liq, iterations=3) & ~liq & solid
            ey, ex = np.where(ring)
        else:
            ey = ex = np.zeros(0, int)
        lcx, lcy = (float(np.mean(np.where(liq)[1])), float(np.mean(np.where(liq)[0]))) if liq.any() else (nx / 2, ny / 2)
        for i in range(int(area * 0.35)):
            if len(ey) and i % 3 == 0:                  # 三分之一从岩浆湖边长出来，往外走
                j = int(rng.integers(0, len(ey)))
                y, x = float(ey[j]), float(ex[j])
                a = math.atan2(y - lcy, x - lcx) + rng.normal(0, 0.5)
            else:
                x, y = float(rng.integers(0, nx)), float(rng.integers(0, ny))
                if okp[int(y), int(x)] < 0.5 or n_mid[int(y), int(x)] < 0.5:
                    continue
                a = rng.uniform(0, 6.28)
            pts = [(x, y)]
            for k in range(int(rng.integers(6, 16))):
                a += rng.normal(0, 0.3) + (rng.choice([-0.9, 0.9]) if rng.random() < 0.2 else 0.0)
                x += math.cos(a) * 0.1 * PPM
                y += math.sin(a) * 0.1 * PPM
                if not (0 <= x < nx and 0 <= y < ny) or okp[int(y), int(x)] < 0.4:
                    break
                pts.append((x, y))
                if rng.random() < 0.25:                         # 分叉
                    b = a + rng.choice([-1, 1]) * rng.uniform(0.6, 1.1)
                    cd2.line([(x, y), (x + math.cos(b) * 0.1 * PPM, y + math.sin(b) * 0.1 * PPM)], fill=150, width=1)
            if len(pts) >= 2:
                cd2.line(pts, fill=255, width=2)
        # 成片的"冷却熔岩板"：几块区域里，地面裂成一块块多边形，缝里透着光（Voronoi 的边）
        from scipy.spatial import cKDTree
        zone = _ss(n_mid * 0.75 + n_sm * 0.25, 0.6, 0.7) * okp
        zone = np.maximum(zone, (1 - _ss(d_liq, 0.15, 0.5)) * okp * 0.4)          # 岩浆湖边也裂开
        zy, zx = np.where(zone > 0.05)
        net = np.zeros((ny, nx), np.float32)
        if len(zy):
            npt = int(len(zy) / PPM / PPM * 3.6)
            sel = rng.integers(0, len(zy), max(4, npt))
            seeds = np.stack([zx[sel] + rng.uniform(-0.5, 0.5, len(sel)), zy[sel] + rng.uniform(-0.5, 0.5, len(sel))], 1)
            tree = cKDTree(seeds)
            dd_, _ = tree.query(np.stack([zx, zy], 1).astype(np.float32), k=2)
            gap = (dd_[:, 1] - dd_[:, 0]) / PPM
            net[zy, zx] = (1 - _ss(gap, 0.004, 0.014)) * zone[zy, zx] * 0.85
        cd = None
        core = np.maximum(np.asarray(crk, dtype=np.float32) / 255.0 * okp, net)
        halo = ndimage.gaussian_filter(core, 0.045 * PPM)
        hv = halo[halo > 0.01]
        halo = np.clip(halo / max(1e-6, float(np.percentile(hv, 90)) if len(hv) else 1.0), 0, 1)
        img = _mix(img, (58, 44, 50), _ss(halo, 0.04, 0.35) * 0.6)
        img = _mix(img, C["heat"], _ss(halo, 0.4, 0.95) * 0.55)
        img = _mix(img, C["glow"], core * 0.95)
        heat = (1 - _ss(d_liq, 0.0, 0.55)) * fsolid
        img = _mix(img, C["heat"], heat * 0.65)
        img = _mix(img, C["glow"], (1 - _ss(d_liq, 0.0, 0.12)) * fsolid * 0.7)
        glow = np.clip(core * 0.9 + _ss(halo, 0.4, 0.95) * 0.25 + (1 - _ss(d_liq, 0.0, 0.14)) * 0.8 + heat * 0.15, 0, 1) * fsolid
        Image.fromarray((glow * 255).astype(np.uint8)).save(out_png[:-4] + "_glow.png")
        # 零星的浮石、火山渣
        dl = Image.new("RGBA", (nx, ny), (0, 0, 0, 0))
        dd = ImageDraw.Draw(dl)
        for _ in range(int(area * 22)):
            x, y = int(rng.integers(0, nx)), int(rng.integers(0, ny))
            if not solid[y, x] or wall[y, x] or d_liq[y, x] < 0.15:
                continue
            r = rng.uniform(0.01, 0.028) * PPM
            c = C["pebble_a"] if rng.random() < 0.65 else C["pebble_b"]
            dd.ellipse((x - r, y - r * 0.8, x + r, y + r * 0.8), fill=c)
        img = _stamp(img, dl)
    if st.get("deck"):
        # 栈桥：比 2*deck 米窄、挨着海水的陆地条 = 木栈桥（铺板跟着桥的走向）；岸上靠桥头、靠水的一圈是石砌码头
        r_m = float(st["deck"])
        up = 4
        q = cell / up
        sc = np.kron((g != 2).astype(np.uint8), np.ones((up, up), np.uint8)).astype(bool)
        core_ = ndimage.distance_transform_edt(sc) * q >= r_m
        opened = (ndimage.distance_transform_edt(~core_) * q <= r_m) if core_.any() else np.zeros_like(sc)
        narrow = (sc & ~opened).reshape(H, up, W, up).mean(axis=(1, 3)) > 0.5
        lab, n = ndimage.label(narrow)
        near_liq = ndimage.binary_dilation(enc, iterations=1)
        deck_c = np.zeros_like(narrow)
        for i in range(1, n + 1):
            mk = lab == i
            if (mk & near_liq).any() and mk.sum() >= 6:
                deck_c |= mk
        hr, vr = _runs(deck_c, 1), _runs(deck_c, 0)
        ew = deck_c & (hr >= vr * 0.8)
        ns = deck_c & ~ew
        extra["deck"] = (ew * 1 + ns * 2).astype(np.int8)
        deck = deck_c[gy, gx] & solid
        ewp = ew[gy, gx]
        if deck.any():
            bw = 0.26
            t = np.where(ewp, XX, YY)
            u = np.where(ewp, YY, XX)
            pid = np.floor(t / bw)
            hsh = np.sin(pid * 12.9898) * 43758.5453
            hsh = (hsh - np.floor(hsh)).astype(np.float32)
            wood = st.get("wood", [(196, 150, 104), (170, 124, 86), (216, 174, 124)])
            wcol = np.empty((ny, nx, 3), dtype=np.float32)
            wcol[:] = np.array(wood[0], dtype=np.float32)
            wcol = _mix(wcol, wood[1], _ss(hsh, 0.55, 0.9) * 0.8)
            wcol = _mix(wcol, wood[2], _ss(hsh, 0.45, 0.1) * 0.7)
            fr = (t / bw) % 1.0
            wcol *= (1 - 0.5 * (1 - _ss(np.minimum(fr, 1 - fr), 0.03, 0.09)))[..., None]          # 板缝
            fu = ((u + hsh * 2.1) / 2.1) % 1.0
            wcol *= (1 - 0.35 * (1 - _ss(np.minimum(fu, 1 - fu), 0.004, 0.012)))[..., None]     # 板头的接缝（错开）
            wcol *= (1 + 0.05 * np.sin(u * 47 + pid * 3.1) + (n_fine - 0.5) * 0.08)[..., None]  # 木纹
            nail = 1 - _ss(np.hypot((fr - 0.5) * bw, (u % 0.7) - 0.35), 0.012, 0.022)
            wcol = _mix(wcol, (110, 84, 70), nail.astype(np.float32) * 0.7)
            dsea = ndimage.distance_transform_edt(deck) / PPM
            wcol = _mix(wcol, (110, 74, 52), (1 - _ss(dsea, 0.05, 0.12)).astype(np.float32) * 0.85)   # 两侧的边梁
            wcol = _mix(wcol, (236, 214, 176), dirt * 0.25)                                         # 常走的地方磨白
            img = np.where(deck[..., None], wcol, img).astype(np.float32)
            notick |= deck
            d_deck = ndimage.distance_transform_edt(~deck) / PPM
            want = (((d_deck < 1.3) | (d_liq < 0.9)) & solid & ~deck).astype(np.float32) * (1 - _ss(tall_f, 0.2, 0.5))
            tile, keep = _slab_layer(want, rng, nx, ny, Wm, Hm, rowh=0.5)
            slab = keep * want
            scol = _slab_color(C, tile, n_fine, ny, nx)
            img = _mix(img, C.get("joint", C["dirt_lo"]), _ss(want, 0.3, 0.8) * 0.9)
            img = img * (1 - slab[..., None]) + scol * slab[..., None]
            notick |= slab > 0.4
            cop = ((1 - _ss(d_liq, 0.05, 0.13)) * solid * ~deck).astype(np.float32)      # 水边一圈石砌的压顶
            img = _mix(img, C.get("slab_hi", C["dirt_hi"]), cop * 0.9)
    if st.get("castle"):
        # 双堡：城堡里铺方砖，中间广场铺石板，池塘边一圈石头压顶；其余是草地和石子路
        # 墙块之间只隔着门洞（≤2 格）的算同一组：南北都开门的城堡被门洞切成两半，要并回来
        lab, n = ndimage.label(ndimage.binary_dilation(g == 1, structure=np.ones((3, 3), bool)))
        lab = lab * (g == 1)
        floor_c = np.zeros((H, W), bool)
        boxes = []
        for i in range(1, n + 1):
            ys_, xs_ = np.where(lab == i)
            bw_, bh_ = xs_.max() - xs_.min() + 1, ys_.max() - ys_.min() + 1
            if bw_ >= 5 and bh_ >= 5 and len(xs_) < 0.7 * bw_ * bh_:
                floor_c[ys_.min():ys_.max() + 1, xs_.min():xs_.max() + 1] = True
                boxes.append((xs_.min(), ys_.min(), xs_.max() + 1, ys_.max() + 1))
        floor = floor_c[gy, gx] & solid & ~wall
        if floor.any():
            tile, keep = _slab_layer(floor.astype(np.float32) * 2, rng, nx, ny, Wm, Hm, rowh=0.25, inset=1.5, sq=True)
            sl = keep * floor
            scol = _slab_color(C, tile, n_fine, ny, nx)
            scol = _mix(scol, (236, 226, 204), dirt * 0.3)
            img = _mix(img, C.get("joint", C["dirt_lo"]), floor.astype(np.float32))
            img = img * (1 - sl[..., None]) + scol * sl[..., None]
            notick |= floor
        lefts = [b for b in boxes if (b[0] + b[2]) / 2 < W / 2]
        rights = [b for b in boxes if (b[0] + b[2]) / 2 >= W / 2]
        xa = max(b[2] for b in lefts) * cell if lefts else Wm * 0.25
        xb = min(b[0] for b in rights) * cell if rights else Wm * 0.75
        rx, ry = max(1.0, (xb - xa) / 2 - 1.0), Hm * 0.32
        ell = np.hypot((XX - (xa + xb) / 2) / rx, (YY - Hm / 2) / ry)
        want = _ss(ell + (n_mid - 0.5) * 0.35, 1.05, 0.85) * fsolid * (1 - _ss(tall_f, 0.2, 0.5)) * ~floor
        tile, keep = _slab_layer(want, rng, nx, ny, Wm, Hm, rowh=0.5)
        slab = keep * want
        scol = _slab_color(C, tile, n_fine, ny, nx)
        scol = _mix(scol, C.get("slab_hi", C["dirt_hi"]), dirt * 0.4)
        img = _mix(img, C.get("joint", C["dirt_lo"]), _ss(want, 0.3, 0.8) * 0.85)
        img = img * (1 - slab[..., None]) + scol * slab[..., None]
        notick |= slab > 0.4
        wv = st.get("_wave")
        if wv is not None:
            # 最后塌的那一片（决战区）：一圈深色石条镶边，里面一圈圈同心的石环，正中一颗八角星
            wa = np.array(wv)
            fz = (wa == wa.max()) & (wa >= 0) & (g != 2)
            if fz.any() and fz.sum() < 0.4 * (g != 2).sum():
                fzp = fz[gy, gx] & solid
                din = ndimage.distance_transform_edt(fzp) / PPM
                fy_, fx_ = np.where(fz)
                ccx, ccy = (fx_.mean() + 0.5) * cell, (fy_.mean() + 0.5) * cell
                rr = np.hypot(XX - ccx, YY - ccy)
                ang = np.arctan2(YY - ccy, XX - ccx)
                inl = np.empty((ny, nx, 3), dtype=np.float32)
                inl[:] = np.array(C.get("slab_hi", C["dirt_hi"]), dtype=np.float32)
                ringi = np.floor(rr / 0.42)
                inl = _mix(inl, C.get("slab", C["dirt"]), (ringi % 2).astype(np.float32) * 0.8)
                fr = (rr / 0.42) % 1.0
                inl = _mix(inl, C.get("joint", C["dirt_lo"]), (1 - _ss(np.minimum(fr, 1 - fr), 0.015, 0.05)) * 0.8)
                nseg = 8 + ringi * 4
                ft = ((ang / (2 * math.pi) + 0.5) * nseg + ringi * 0.5) % 1.0
                inl = _mix(inl, C.get("joint", C["dirt_lo"]), (1 - _ss(np.minimum(ft, 1 - ft) * rr * 6.28 / np.maximum(nseg, 1), 0.006, 0.02)) * 0.7)
                star = np.abs(np.cos(ang * 4)) ** 6 * 0.62 + 0.28
                inl = _mix(inl, C.get("emblem", (196, 120, 96)), _ss(rr, star + 0.03, star - 0.03))
                inl = _mix(inl, C.get("emblem2", (232, 196, 120)), _ss(rr, 0.2, 0.14))
                band = _ss(din, 0.0, 0.02) * (1 - _ss(din, 0.2, 0.24))
                inl = _mix(inl, C.get("slab_lo", C["dirt_lo"]), band * 0.9)
                inl = _mix(inl, C.get("joint", C["dirt_lo"]), (1 - _ss(np.abs(din - 0.22), 0.0, 0.015)) * 0.8)
                k = (_ss(din, 0.0, 0.02) * fsolid).astype(np.float32)
                img = img * (1 - k[..., None]) + inl * k[..., None]
                notick |= k > 0.3
        cop = (1 - _ss(d_liq, 0.04, 0.12)) * fsolid                                     # 池塘边一圈石头压顶
        img = _mix(img, C.get("slab_hi", C["dirt_hi"]), cop * 0.95)
        img = _mix(img, C.get("joint", C["dirt_lo"]), (1 - _ss(np.abs(d_liq - 0.13), 0.0, 0.02)) * fsolid * 0.7)
        notick |= cop > 0.3
    return img, notick, extra


def paint(grid, cell, spawns, out_png, seed=7, colors=None, style=None):
    """grid[y][x]：0 地 1 墙 2 坑 3 草；cell：格子边长（米）；spawns：[(mx, my)]。返回辅助数据 dict。
    style：主题专属的画法（themes.py 的 gstyle）：pave 石板（all / near）、ring 坑边环形铺地、ripples 沙纹、strata 岩层线、
    planks 窄桥铺木板（数值 = 多窄算桥，米）、petals 花瓣、heather 石楠、litter 落叶、roots 树根、ticks 草笔触（none = 不画）。"""
    C = dict(globals()["C"])
    if colors:
        C.update(colors)
    st = style or {}
    g = np.array(grid)
    H, W = g.shape
    Wm, Hm = W * cell, H * cell
    nx, ny = int(round((Wm + 2 * MG) * PPM)), int(round((Hm + 2 * MG) * PPM))
    rng = np.random.default_rng(seed)
    xs = (np.arange(nx) + 0.5) / PPM - MG
    ys = (np.arange(ny) + 0.5) / PPM - MG
    cx = np.clip(np.floor(xs / cell).astype(int), 0, W - 1)
    cy = np.clip(np.floor(ys / cell).astype(int), 0, H - 1)
    inx = (xs >= 0) & (xs < Wm)
    iny = (ys >= 0) & (ys < Hm)
    inside = np.outer(iny, inx)
    cellv = g[cy][:, cx]
    solid = (cellv != 2) & inside
    wall = (cellv == 1) & inside
    tall = (cellv == 3) & inside
    d_edge = ndimage.distance_transform_edt(solid) / PPM
    d_wall = ndimage.distance_transform_edt(~wall) / PPM

    # ---- 人常走的路：出生点 ↔ 中心 / 出生点之间的最短路
    walk = (g == 0) | (g == 3)
    cost = 1.0 + rng.uniform(0, 0.8, (H, W)) + (g == 3) * 1.5

    def near_cell(mx, my):
        best, bd = None, 1e9
        for y in range(H):
            for x in range(W):
                if walk[y, x]:
                    d = (x + 0.5 - mx / cell) ** 2 + (y + 0.5 - my / cell) ** 2
                    if d < bd:
                        best, bd = (x, y), d
        return best

    nodes = [near_cell(mx, my) for (mx, my) in spawns]
    # 连通块（8 邻接）：随机抽若干对同一连通块里的格子求最短路，路过次数多的地方就是"主路"
    comp, ncomp = ndimage.label(walk, structure=np.ones((3, 3)))
    pairs = []
    for i, a in enumerate(nodes):
        others = sorted([b for j, b in enumerate(nodes) if j != i and b != a],
                        key=lambda b: (b[0] - a[0]) ** 2 + (b[1] - a[1]) ** 2)
        for b in others[:3]:
            pairs.append((a, b))
    cells_w = [(x, y) for y in range(H) for x in range(W) if walk[y, x]]
    hubs = []
    for c in range(1, ncomp + 1):
        cc = [q for q in cells_w if comp[q[1], q[0]] == c]
        if len(cc) < 6:
            continue
        k = max(2, int(len(cc) ** 0.5 * 0.9))
        # 尽量分散的几个"去处"（最远点采样）
        hub = [cc[int(rng.integers(0, len(cc)))]]
        while len(hub) < k:
            hub.append(max(cc, key=lambda q: min((q[0] - h[0]) ** 2 + (q[1] - h[1]) ** 2 for h in hub)))
        hubs += hub
        for i in range(len(hub)):
            for j in range(i + 1, len(hub)):
                pairs.append((hub[i], hub[j]))
        for a in nodes:
            if a is not None and comp[a[1], a[0]] == c:
                for h in hub:
                    pairs.append((a, h))
    acc = np.zeros((ny, nx), dtype=np.float32)
    seen = set()
    for a, b in pairs:
        if a is None or b is None or a == b or (b, a) in seen or (a, b) in seen:
            continue
        seen.add((a, b))
        if comp[a[1], a[0]] != comp[b[1], b[0]]:
            continue
        p = _dijkstra(walk, a, b, cost)
        if not p or len(p) < 2:
            continue
        pts = [((x + 0.5) * cell + rng.normal(0, 0.12 * cell), (y + 0.5) * cell + rng.normal(0, 0.12 * cell))
               for (x, y) in p]
        pts = _chaikin(pts, 3)
        px = [((x + MG) * PPM, (y + MG) * PPM) for x, y in pts]
        wd = int(rng.uniform(0.42, 0.6) * PPM)
        one = Image.new("L", (nx, ny), 0)
        ImageDraw.Draw(one).line(px, fill=255, width=wd, joint="curve")
        acc += np.asarray(one, dtype=np.float32) / 255.0
    acc = ndimage.gaussian_filter(acc, 0.2 * PPM)
    hi = np.percentile(acc[solid], 97) if solid.any() else 1.0
    traffic = np.clip(acc / max(hi, 1e-6), 0, 1.2)
    n_big = _noise((ny, nx), 3.2 * PPM, seed + 1)
    n_mid = _noise((ny, nx), 1.1 * PPM, seed + 2)
    n_sm = _noise((ny, nx), 0.32 * PPM, seed + 3)
    n_fine = _noise((ny, nx), 0.09 * PPM, seed + 4)
    dirt_f = traffic * 0.62 + (n_sm - 0.5) * 0.3 + (n_mid - 0.5) * 0.22
    dirt_f = dirt_f * _ss(d_edge, 0.10, 0.34) * _ss(d_wall, 0.02, 0.2)
    dirt = _ss(dirt_f, 0.40, 0.50)                       # 土路（边缘利落一点）
    worn = _ss(dirt_f, 0.16, 0.42) * (1 - dirt)         # 路边被踩秃的浅色草
    far = ndimage.gaussian_filter(np.clip(traffic, 0, 1), 0.9 * PPM)
    quiet = 1.0 - _ss(far, 0.04, 0.3)                   # 离路远 = 没人去的角落

    # ---- 草色
    img = np.empty((ny, nx, 3), dtype=np.float32)
    img[:] = np.array(C["mid"], dtype=np.float32)
    img = _mix(img, C["light"], _ss(n_big, 0.50, 0.66) * 0.8)
    img = _mix(img, C["dark"], _ss(n_big, 0.44, 0.28) * 0.6)
    img = _mix(img, C["light"], _ss(n_mid, 0.60, 0.78) * 0.22)
    img = _mix(img, C["lush"], quiet * _ss(n_big * 0.5 + n_mid * 0.5, 0.36, 0.6) * 0.55)
    img = _mix(img, C["light"], worn * 0.8)
    # 三叶草丛：安静角落里几团更深的圆斑
    clover = np.zeros((ny, nx), dtype=np.float32)
    area = float(solid.sum()) / PPM / PPM
    cl_img = Image.new("L", (nx, ny), 0)
    cd = ImageDraw.Draw(cl_img)
    for _ in range(int(area * 0.5)):
        x, y = rng.integers(0, nx), rng.integers(0, ny)
        if quiet[y, x] < 0.6 or not solid[y, x]:
            continue
        for k in range(int(rng.integers(5, 12))):
            r = rng.uniform(0.05, 0.11) * PPM
            ox, oy = rng.normal(0, 0.16 * PPM, 2)
            cd.ellipse((x + ox - r, y + oy - r, x + ox + r, y + oy + r), fill=255)
    clover = ndimage.gaussian_filter(np.asarray(cl_img, dtype=np.float32) / 255.0, 1.2)
    img = _mix(img, C["clover"], clover * 0.8)
    # ---- 土路
    dcol = np.empty((ny, nx, 3), dtype=np.float32)
    dcol[:] = np.array(C["dirt"], dtype=np.float32)
    dcol = _mix(dcol, C["dirt_hi"], _ss(n_sm, 0.55, 0.7) * 0.8)
    dcol = _mix(dcol, C["dirt_lo"], _ss(n_fine, 0.68, 0.8) * 0.5)
    rim_d = _ss(dirt_f, 0.36, 0.42) * (1 - _ss(dirt_f, 0.46, 0.54))
    img = _mix(img, C["dirt_rim"], rim_d * 0.55)
    img = img * (1 - dirt[..., None]) + dcol * dirt[..., None]
    # ---- 草丛格的深色草底
    tall_f = ndimage.gaussian_filter(tall.astype(np.float32), 0.10 * PPM)
    img = _mix(img, C["tall"], _ss(tall_f, 0.3, 0.7))
    # ---- 石头脚下：柔和的接触暗部（不画方框）+ 零星裸土
    wsh = ndimage.gaussian_filter(wall.astype(np.float32), 0.16 * PPM)
    img = _mix(img, C["dirt_lo"], _ss(wsh, 0.15, 0.6) * _ss(n_sm, 0.5, 0.66) * 0.35)
    img *= (1 - 0.2 * _ss(wsh, 0.05, 0.7))[..., None]
    # ---- 岛沿亮边
    rim = (1 - _ss(d_edge, 0.05, 0.2)) * (1 - dirt)
    img = _mix(img, C["rim"], rim * 0.85)
    # ---- 主题专属的地面层
    XX, YY = np.meshgrid(xs, ys)
    pit = (cellv == 2) & inside
    plank_mask = np.zeros((ny, nx), bool)
    bridges = []
    fsolid = solid.astype(np.float32)
    if st.get("pave"):
        # 石板：一排排宽窄不一的石块，逐块有无、逐块色差；near = 只在墙脚和场地中心留着（残存的铺地），all = 整个广场
        rowh = 0.5
        timg = Image.new("F", (nx, ny), 0.0)
        kimg = Image.new("L", (nx, ny), 0)
        td, kd = ImageDraw.Draw(timg), ImageDraw.Draw(kimg)
        if st["pave"] == "near":
            want = np.maximum(_ss(d_wall, 0.95, 0.4) * 0.8, _ss(np.hypot(XX - Wm / 2, YY - Hm / 2), 1.9, 1.1)) * fsolid
        else:
            want = fsolid * (1.0 - 0.75 * quiet * _ss(n_mid, 0.5, 0.7))
        yy0 = -MG
        while yy0 < Hm + MG:
            xx0 = -MG - rng.uniform(0, 0.5)
            while xx0 < Wm + MG:
                wd = float(rng.choice([0.5, 0.5, 0.75, 1.0]))
                x0p, y0p = (xx0 + MG) * PPM, (yy0 + MG) * PPM
                x1p, y1p = (xx0 + wd + MG) * PPM, (yy0 + rowh + MG) * PPM
                cxp, cyp = int(min(nx - 1, max(0, (x0p + x1p) / 2))), int(min(ny - 1, max(0, (y0p + y1p) / 2)))
                if want[cyp, cxp] > rng.uniform(0.25, 0.6):
                    td.rectangle((x0p, y0p, x1p, y1p), fill=float(rng.uniform(-1, 1)))
                    kd.rectangle((x0p + 2.5, y0p + 2.5, x1p - 2.5, y1p - 2.5), fill=255)
                xx0 += wd
            yy0 += rowh
        tile = np.asarray(timg, dtype=np.float32)
        keep = ndimage.gaussian_filter(np.asarray(kimg, dtype=np.float32) / 255.0, 1.0)
        slab = np.clip(keep * 1.25, 0, 1) * fsolid * (1 - _ss(tall_f, 0.3, 0.7))
        scol = np.empty((ny, nx, 3), dtype=np.float32)
        scol[:] = np.array(C.get("slab", C["dirt"]), dtype=np.float32)
        scol = _mix(scol, C.get("slab_hi", C["dirt_hi"]), np.clip(tile, 0, 1) * 0.7)
        scol = _mix(scol, C.get("slab_lo", C["dirt_lo"]), np.clip(-tile, 0, 1) * 0.6)
        scol = _mix(scol, C.get("slab_hi", C["dirt_hi"]), dirt * 0.45)            # 常走的地方磨亮
        scol *= (1 + (n_fine - 0.5) * 0.06)[..., None]
        if st["pave"] == "all":
            img = _mix(img, C.get("joint", C["dirt_lo"]), fsolid * (1 - _ss(tall_f, 0.3, 0.7)) * (1 - 0.8 * quiet * _ss(n_mid, 0.5, 0.7)))
        img = img * (1 - slab[..., None]) + scol * slab[..., None]
    if st.get("ring") and pit.any():
        # 坑（喷泉）外面一圈放射状的铺地
        plab, pn = ndimage.label(pit)
        for i in range(1, pn + 1):
            mk = plab == i
            ys_, xs_ = np.where(mk)
            if mk.sum() > (4.5 * PPM) ** 2 or ys_.min() < (MG + 0.3) * PPM or xs_.min() < (MG + 0.3) * PPM \
                    or ys_.max() > ny - (MG + 0.3) * PPM or xs_.max() > nx - (MG + 0.3) * PPM:
                continue
            pcx, pcy = xs[int(xs_.mean())], ys[int(ys_.mean())]
            dpit = ndimage.distance_transform_edt(~mk) / PPM
            ang = np.arctan2(YY - pcy, XX - pcx)
            band = np.floor(dpit / 0.42)
            nseg = 14 + band * 6
            tt = (ang / (2 * math.pi) + 0.5) * nseg + band * 0.5
            fr_r = (dpit / 0.42) % 1.0
            jr = np.minimum(fr_r, 1 - fr_r) * 0.42
            fr_t = tt % 1.0
            jt = np.minimum(fr_t, 1 - fr_t) / np.maximum(nseg, 1) * 2 * math.pi * (dpit + 1.5)
            jn = np.minimum(jr, jt)
            zone = _ss(dpit, 1.5, 1.3) * fsolid
            hsh = np.sin(np.floor(tt) * 12.9898 + band * 78.233) * 43758.5453
            hsh = (hsh - np.floor(hsh)).astype(np.float32)
            rcol = np.empty((ny, nx, 3), dtype=np.float32)
            rcol[:] = np.array(C.get("slab_hi", C["dirt_hi"]), dtype=np.float32)
            rcol = _mix(rcol, C.get("slab", C["dirt"]), hsh * 0.8)
            rcol = _mix(rcol, C.get("joint", C["dirt_lo"]), (1 - _ss(jn, 0.012, 0.035)) * 0.9)
            img = img * (1 - zone[..., None]) + rcol * zone[..., None]
    if st.get("ripples"):
        ph = (YY + 0.22 * np.sin(XX * 1.1 + n_big * 5.0) + (n_mid - 0.5) * 1.3) * (2 * math.pi / 0.46)
        sw = np.sin(ph)
        amp = _ss(n_big * 0.6 + n_sm * 0.4, 0.38, 0.6) * (1 - dirt * 0.8) * (1 - _ss(tall_f, 0.2, 0.6)) * fsolid
        img = _mix(img, C["light"], _ss(sw, 0.35, 0.95) * amp * 0.55)
        img = _mix(img, C["dark"], _ss(-sw, 0.45, 0.98) * amp * 0.5)
    if st.get("strata"):
        f = n_big * 3.2 + n_mid * 1.1 + XX * 0.05
        fr = (f * 2.2) % 1.0
        line = (1 - _ss(np.minimum(fr, 1 - fr), 0.02, 0.07)) * (1 - dirt) * fsolid
        img = _mix(img, C.get("strata", C["dark"]), line * 0.6)
        f2 = (f * 2.2) % 2.0
        img = _mix(img, C["light"], _ss(f2, 0.9, 1.1) * (1 - _ss(f2, 1.9, 2.0)) * 0.22 * (1 - dirt) * fsolid)
        crk = Image.new("L", (nx, ny), 0)
        cd2 = ImageDraw.Draw(crk)
        for _ in range(int(area * 0.35)):
            x, y = float(rng.integers(0, nx)), float(rng.integers(0, ny))
            a = rng.uniform(0, 6.28)
            pts = [(x, y)]
            for k in range(int(rng.integers(3, 7))):
                a += rng.normal(0, 0.6)
                x += math.cos(a) * 0.16 * PPM
                y += math.sin(a) * 0.16 * PPM
                pts.append((x, y))
            cd2.line(pts, fill=255, width=2)
        crk = ndimage.gaussian_filter(np.asarray(crk, dtype=np.float32) / 255.0, 0.7)
        img = _mix(img, C.get("strata", C["dark"]), np.clip(crk * 1.3, 0, 1) * 0.6 * (1 - dirt) * fsolid)
    if st.get("heather"):
        hl = Image.new("RGBA", (nx, ny), (0, 0, 0, 0))
        hd = ImageDraw.Draw(hl)
        for _ in range(int(area * 1.1)):
            x, y = int(rng.integers(0, nx)), int(rng.integers(0, ny))
            if not solid[y, x] or quiet[y, x] < 0.35 or dirt[y, x] > 0.2 or n_mid[y, x] < 0.48:
                continue
            for k in range(int(rng.integers(8, 20))):
                ox, oy = rng.normal(0, 0.2 * PPM, 2)
                r = rng.uniform(0.02, 0.045) * PPM
                c = tuple(st["heather"][int(rng.integers(0, 3))])
                hd.ellipse((x + ox - r, y + oy - r * 1.3, x + ox + r, y + oy + r * 1.3), fill=c + (235,))
        img = _stamp(img, hl, fsolid)
    if st.get("planks"):
        plank_mask, bridges = _bridges(solid, float(st["planks"]))
        wood = st.get("wood", [(178, 120, 78), (150, 98, 66), (206, 146, 96)])
        for (bx, by, ax, ay, hl_, hw_) in bridges:
            t = ((np.arange(nx)[None, :] - bx) * ax + (np.arange(ny)[:, None] - by) * ay) / PPM
            u = ((np.arange(nx)[None, :] - bx) * -ay + (np.arange(ny)[:, None] - by) * ax) / PPM
            near = (np.abs(t) < hl_ / PPM + 0.3) & (np.abs(u) < hw_ / PPM + 0.4) & plank_mask
            pid = np.floor(t / 0.3)
            hsh = np.sin(pid * 12.9898) * 43758.5453
            hsh = (hsh - np.floor(hsh)).astype(np.float32)
            wcol = np.empty((ny, nx, 3), dtype=np.float32)
            wcol[:] = np.array(wood[0], dtype=np.float32)
            wcol = _mix(wcol, wood[1], _ss(hsh, 0.55, 0.9) * 0.8)
            wcol = _mix(wcol, wood[2], _ss(hsh, 0.45, 0.1) * 0.7)
            fr = (t / 0.3) % 1.0
            wcol *= (1 - 0.45 * (1 - _ss(np.minimum(fr, 1 - fr), 0.03, 0.09)))[..., None]       # 板缝
            wcol *= (1 + 0.06 * np.sin(u * 60 + pid * 3.1))[..., None]                         # 木纹
            edge = 1 - _ss(d_edge, 0.05, 0.11)
            wcol = _mix(wcol, (96, 62, 44), (edge * 0.85).astype(np.float32))                  # 两侧的边梁
            img = np.where(near[..., None], wcol, img).astype(np.float32)
    if st.get("litter"):
        ll = Image.new("RGBA", (nx, ny), (0, 0, 0, 0))
        ld2 = ImageDraw.Draw(ll)
        for _ in range(int(area * 9)):
            x, y = int(rng.integers(0, nx)), int(rng.integers(0, ny))
            if not solid[y, x] or wall[y, x] or (n_sm[y, x] < 0.5 and rng.random() < 0.7):
                continue
            a = rng.uniform(0, 3.14)
            L, w_ = rng.uniform(0.035, 0.06) * PPM, rng.uniform(0.014, 0.024) * PPM
            c = tuple(st["litter"][int(rng.integers(0, 3))])
            ca, sa = math.cos(a), math.sin(a)
            ld2.polygon([(x - ca * L, y - sa * L), (x - sa * w_, y + ca * w_), (x + ca * L, y + sa * L), (x + sa * w_, y - ca * w_)],
                        fill=c + (int(rng.integers(150, 235)),))
        img = _stamp(img, ll)
    if st.get("roots"):
        rl = Image.new("L", (nx, ny), 0)
        rd = ImageDraw.Draw(rl)
        wl, wn = ndimage.label(wall)
        for i in range(1, wn + 1):
            ys_, xs_ = np.where(wl == i)
            cx_, cy_ = xs_.mean(), ys_.mean()
            rad = math.sqrt(len(xs_) / math.pi)
            for k in range(int(rng.integers(6, 9))):
                a = rng.uniform(0, 6.28)
                x, y = cx_ + math.cos(a) * rad * 0.8, cy_ + math.sin(a) * rad * 0.8
                pts = [(x, y)]
                L = rng.uniform(0.35, 0.75) * PPM
                for j in range(5):
                    a += rng.normal(0, 0.28)
                    x += math.cos(a) * L / 5
                    y += math.sin(a) * L / 5
                    pts.append((x, y))
                for j in range(5):
                    rd.line(pts[j:j + 2], fill=255, width=max(2, int((0.11 - 0.018 * j) * PPM)))
        rt = ndimage.gaussian_filter(np.asarray(rl, dtype=np.float32) / 255.0, 1.2)
        img = _mix(img, C.get("root", (112, 84, 60)), np.clip(rt * 1.4, 0, 1) * fsolid * (1 - wall))
    if st.get("petals"):
        pl = Image.new("RGBA", (nx, ny), (0, 0, 0, 0))
        pd = ImageDraw.Draw(pl)
        dens = (0.25 + 0.75 * quiet) * (0.5 + 0.9 * _ss(n_mid, 0.4, 0.7))
        for _ in range(int(area * 46)):
            x, y = int(rng.integers(0, nx)), int(rng.integers(0, ny))
            if not solid[y, x] or wall[y, x] or rng.random() > dens[y, x]:
                continue
            a = rng.uniform(0, 3.14)
            L, w_ = rng.uniform(0.03, 0.055) * PPM, rng.uniform(0.018, 0.028) * PPM
            c = tuple(st["petals"][int(rng.integers(0, 3))])
            ca, sa = math.cos(a), math.sin(a)
            pd.polygon([(x - ca * L, y - sa * L), (x - sa * w_, y + ca * w_), (x + ca * L, y + sa * L), (x + sa * w_, y - ca * w_)],
                       fill=c + (int(rng.integers(190, 250)),))
        img = _stamp(img, pl)
    ep12 = {}
    notick = None
    if st.get("ash") or st.get("deck") or st.get("castle"):
        img, notick, ep12 = _ep12_layers(img, st, C, g, cell, inside, solid, fsolid, wall, tall_f, dirt, quiet, n_big, n_mid,
                                         n_sm, n_fine, XX, YY, Wm, Hm, nx, ny, area, seed, out_png)
    # ---- 细碎明暗
    img *= (1 + (n_fine - 0.5) * 0.07)[..., None]

    # ---- 草笔触 + 小石子 + 落叶（画在一层透明图上）
    base = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8))
    lay = Image.new("RGBA", (nx, ny), (0, 0, 0, 0))
    ld = ImageDraw.Draw(lay)
    n_tick = int(area * 16)
    px = rng.integers(0, nx, n_tick)
    py = rng.integers(0, ny, n_tick)
    for x, y in zip(px, py):
        if not solid[y, x] or wall[y, x] or plank_mask[y, x] or (notick is not None and notick[y, x]):
            continue
        if st.get("ticks") == "none" and dirt[y, x] <= 0.5:
            if rng.random() < 0.12:      # 不长草的地面：零星的小石子
                r = rng.uniform(0.012, 0.026) * PPM
                ld.ellipse((x - r, y - r * 0.8, x + r, y + r * 0.8), fill=C["pebble_a"] if rng.random() < 0.6 else C["pebble_b"])
            continue
        if dirt[y, x] > 0.5:
            if rng.random() < 0.25:      # 路上的小石子
                r = rng.uniform(0.012, 0.03) * PPM
                c = C["pebble_a"] if rng.random() < 0.6 else C["pebble_b"]
                ld.ellipse((x - r, y - r * 0.8, x + r, y + r * 0.8), fill=c)
            continue
        L = rng.uniform(0.05, 0.11) * PPM
        a = rng.normal(-math.pi / 2, 0.5)
        lightc = rng.random() < 0.5
        c = C["tick_light"] if lightc else C["tick_dark"]
        for s in (-0.3, 0.0, 0.3) if rng.random() < 0.5 else (0.0,):
            ld.line((x, y, x + math.cos(a + s) * L, y + math.sin(a + s) * L), fill=c, width=2)
    base = Image.alpha_composite(base.convert("RGBA"), lay).convert("RGB")
    base.save(out_png)
    # 辅助图（每米 8 像素）：点缀物摆放用
    sub = PPM // 8
    aux = dict(dirt=dirt[::sub, ::sub].astype(np.float32), quiet=quiet[::sub, ::sub].astype(np.float32),
               ppm=8, mg=MG,
               bridges=np.array([((bx / PPM) - MG, (by / PPM) - MG, ax, ay, hl_ / PPM, hw_ / PPM)
                                 for (bx, by, ax, ay, hl_, hw_) in bridges], dtype=np.float32).reshape(-1, 6))
    aux.update(ep12)
    return aux


if __name__ == "__main__":
    import sys
    sys.path.insert(0, __import__("os").path.dirname(__file__))
    import replay
    import themes
    r = replay.load(sys.argv[1])
    th = themes.resolve(r)
    paint(r.grid, r.cell, r.spawns(), sys.argv[2], colors=th["ground"], style=th["gstyle"])
