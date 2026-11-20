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
        if not solid[y, x] or wall[y, x] or plank_mask[y, x]:
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
    return aux


if __name__ == "__main__":
    import sys
    sys.path.insert(0, __import__("os").path.dirname(__file__))
    import replay
    import themes
    r = replay.load(sys.argv[1])
    th = themes.resolve(r)
    paint(r.grid, r.cell, r.spawns(), sys.argv[2], colors=th["ground"], style=th["gstyle"])
