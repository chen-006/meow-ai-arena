# -*- coding: utf-8 -*-
"""浮岛地形：由"实地"掩码生成整块岛体网格（草面 + 崖壁 + 倒山形底部）。venv 的 Python 跑（numpy / scipy / scikit-image）。
坐标：Blender X = 地图 x（米），Y = -地图 y（米），Z 向上，草面 Z = 0。

相对第四轮的改动：崖壁不再是平滑噪声捏出来的"黏土"，而是一根根棱柱——平面上用 Voronoi 分柱，
每根柱子的立面是一个平面（各自内收、偏转一点），柱与柱之间是利落的竖棱；往下有一两道横向的台阶。
"""
import numpy as np
from scipy import ndimage
from scipy.spatial import cKDTree
from skimage import measure

RES = 0.05  # 体素边长（米）
STRATA_P = 1.15


def coarse_noise(shape, cell, seed, order=3):
    """把粗网格随机数插值到 shape，得到平滑噪声（约 -1..1）。cell = 粗格对应多少个细格。"""
    rng = np.random.default_rng(seed)
    cell = max(1, int(cell))
    cs = tuple(max(2, int(np.ceil(s / cell)) + 3) for s in shape)
    base = rng.uniform(-1.0, 1.0, cs).astype(np.float32)
    zoom = tuple((s + 3 * cell) / c for s, c in zip(shape, cs))
    big = ndimage.zoom(base, zoom, order=order, mode="nearest")
    sl = tuple(slice(cell, cell + s) for s in shape)
    out = big[sl]
    return out / max(1e-6, np.abs(out).max())


def signed_dist_2d(mask, res):
    inside = ndimage.distance_transform_edt(mask) * res
    outside = ndimage.distance_transform_edt(~mask) * res
    d = inside - outside
    d = np.where(mask, d - res * 0.5, d + res * 0.5)
    return ndimage.gaussian_filter(d.astype(np.float32), 1.2)


def smax(a, b, k):
    h = np.clip(k - np.abs(a - b), 0.0, None) / k
    return np.maximum(a, b) + h * h * k * 0.25


def voronoi_columns(shape, res, spacing, seed):
    """平面 Voronoi 分柱。返回 (每像素的柱号, 每柱随机量 [n,4], 像素到柱心的偏移 dx, dy)。"""
    rng = np.random.default_rng(seed)
    ny, nx = shape
    n = max(4, int(nx * ny * res * res / (spacing * spacing)))
    pts = np.stack([rng.uniform(0, nx * res, n), rng.uniform(0, ny * res, n)], axis=1)
    yy, xx = np.mgrid[0:ny, 0:nx]
    q = np.stack([(xx.ravel() + 0.5) * res, (yy.ravel() + 0.5) * res], axis=1)
    _, idx = cKDTree(pts).query(q)
    idx = idx.reshape(shape)
    rnd = rng.uniform(-1, 1, (n, 4)).astype(np.float32)
    dx = ((xx + 0.5) * res - pts[idx, 0]).astype(np.float32)
    dy = ((yy + 0.5) * res - pts[idx, 1]).astype(np.float32)
    return idx, rnd, dx, dy


def rasterize_cells(cell_mask, cell, margin, res, origin=(0.0, 0.0)):
    """格子掩码 -> 高分辨率掩码。返回 (mask, (x0, y0))。cell = 格子边长（米）。"""
    cm = np.asarray(cell_mask, dtype=bool)
    H, W = cm.shape
    x0 = origin[0] - margin
    y0 = origin[1] - margin
    nx = int(round((W * cell + 2 * margin) / res))
    ny = int(round((H * cell + 2 * margin) / res))
    xs = x0 + (np.arange(nx) + 0.5) * res
    ys = y0 + (np.arange(ny) + 0.5) * res
    cx = np.floor((xs - origin[0]) / cell).astype(int)
    cy = np.floor((ys - origin[1]) / cell).astype(int)
    okx = (cx >= 0) & (cx < W)
    oky = (cy >= 0) & (cy < H)
    m = np.zeros((ny, nx), dtype=bool)
    sub = cm[np.clip(cy, 0, H - 1)][:, np.clip(cx, 0, W - 1)]
    m[np.ix_(oky, okx)] = sub[np.ix_(oky, okx)]
    return m, (x0, y0)


def round_mask(m, corner_r, res=RES, seed=1, edge_wobble=0.03):
    """圆角：模糊后取 0.5 等值线（直边位置不变，凸角削圆、凹角填圆）。"""
    m = np.asarray(m, dtype=bool)
    if corner_r <= 0:
        return m
    mb = ndimage.gaussian_filter(m.astype(np.float32), corner_r / res)
    if edge_wobble > 0:
        mb = mb + coarse_noise(mb.shape, int(1.6 / res), seed + 11) * edge_wobble * 2.0
    return mb > 0.5


def build_island(hr_mask, hr_origin, corner_r=0.36, seed=1, depth_max=9.0, taper_k=0.26, taper_p=1.35,
                 edge_wobble=0.03, lip_round=0.07, rough_amp=0.22, res=RES, cliff_h=1.9,
                 col_spacing=0.62, col_amp=0.11, ledge=0.06, voro_seed=None, mc_step=1):
    """返回 V (N,3) Blender 坐标、F (M,3)、S (N,4) = [到草面边缘的水平距离, 层理, 深度, 柱面随机量]。
    voro_seed：同一张图的各个塌陷状态用同一个值，没塌的部分崖壁长得一样。"""
    m = np.asarray(hr_mask, dtype=bool)
    ny, nx = m.shape
    x0, y0 = hr_origin
    mask2 = round_mask(m, corner_r, res, seed, edge_wobble)
    mb = mask2
    if not mask2.any():
        return np.zeros((0, 3), np.float32), np.zeros((0, 3), np.int32), np.zeros((0, 4), np.float32)
    s2 = signed_dist_2d(mask2, res)

    vs = seed if voro_seed is None else voro_seed
    idx, rnd, dx, dy = voronoi_columns(mb.shape, res, col_spacing, vs + 29)
    # 每根柱子：一个平的立面，各自内收一点，再偏转一点（平面上的线性项）
    col = rnd[idx, 0] * 0.75 + (rnd[idx, 1] * dx + rnd[idx, 2] * dy) * 1.1
    col = col.astype(np.float32)
    facet = rnd[idx, 3]
    ledge1 = 0.55 + (rnd[idx, 1] * 0.5 + 0.5) * 0.9     # 第一道横台阶的深度（每柱不同）
    ledge2 = ledge1 + 0.7 + (rnd[idx, 2] * 0.5 + 0.5) * 0.9

    smax_s = float(s2.max())
    zbot = -min(depth_max, cliff_h + (max(smax_s, 0.3) / taper_k) ** (1.0 / taper_p) + 0.6)
    nz = int(np.ceil((0.3 - zbot) / res))
    zs = 0.3 - (np.arange(nz) + 0.5) * res
    u = (-zs).astype(np.float32)
    g = np.where(u > 0.15, 0.035, 0.0).astype(np.float32)
    g = g + taper_k * np.clip(u - cliff_h, 0.0, None) ** taper_p

    D = np.empty((nz, ny, nx), dtype=np.float32)
    rough = coarse_noise((max(8, nz // 2), max(2, ny // 2), max(2, nx // 2)), 8, seed + 37)
    rough = ndimage.zoom(rough, (nz / rough.shape[0], ny / rough.shape[1], nx / rough.shape[2]), order=1)
    rough = rough[:nz, :ny, :nx]
    for k in range(nz):
        uk = float(u[k])
        layer = float(g[k]) - s2
        if uk > 0.15:
            w = min(1.0, (uk - 0.15) / 0.22)
            layer = layer + col * (col_amp * w)
            layer = layer + ledge * (uk > ledge1) + ledge * 0.8 * (uk > ledge2)
        if uk > cliff_h:
            layer = layer + rough[k] * rough_amp * min(1.0, (uk - cliff_h) / 2.0)
        layer = smax(layer, np.full_like(layer, zs[k]), lip_round)
        D[k] = np.maximum(layer, zbot - zs[k])
    D[0] = 1.0; D[-1] = 1.0; D[:, 0] = 1.0; D[:, -1] = 1.0; D[:, :, 0] = 1.0; D[:, :, -1] = 1.0
    if D.min() >= 0:
        return np.zeros((0, 3), np.float32), np.zeros((0, 3), np.int32), np.zeros((0, 4), np.float32)
    verts, faces, _, _ = measure.marching_cubes(D, level=0.0, spacing=(res, res, res), allow_degenerate=False,
                                                  step_size=mc_step)
    vz = 0.3 - (verts[:, 0] + 0.5 * res)
    vy_map = y0 + verts[:, 1] + 0.5 * res
    vx = x0 + verts[:, 2] + 0.5 * res
    iy = np.clip((vy_map - y0) / res - 0.5, 0, ny - 1)
    ix = np.clip((vx - x0) / res - 0.5, 0, nx - 1)
    es = ndimage.map_coordinates(s2, [iy, ix], order=1, mode="nearest")
    fc = ndimage.map_coordinates(facet, [iy, ix], order=0, mode="nearest")
    strata = -vz / STRATA_P
    V = np.stack([vx, -vy_map, vz], axis=1).astype(np.float32)
    F = faces[:, ::-1].astype(np.int32)
    S = np.stack([es, strata, -vz, fc], axis=1).astype(np.float32)
    return V, F, S


def fracture(region, res, seed, piece_area=1.7, warp=0.14):
    """把一片要塌的区域（高分辨率 bool 掩码）裂成不规则的碎块。返回 int32 标号图（-1 = 不在区域里）和块数。"""
    rng = np.random.default_rng(seed)
    lab = np.full(region.shape, -1, dtype=np.int32)
    comp, nc = ndimage.label(region)
    k = 0
    ny, nx = region.shape
    n1 = coarse_noise(region.shape, int(0.45 / res), seed + 1) * warp
    n2 = coarse_noise(region.shape, int(0.45 / res), seed + 2) * warp
    for c in range(1, nc + 1):
        ys, xs = np.where(comp == c)
        area = len(ys) * res * res
        if area < 0.02:
            continue
        n = max(1, int(round(area / piece_area)))
        pick = rng.choice(len(ys), size=n, replace=False)
        seeds = np.stack([xs[pick] * res, ys[pick] * res], axis=1)
        q = np.stack([xs * res + n1[ys, xs], ys * res + n2[ys, xs]], axis=1)
        _, idx = cKDTree(seeds).query(q)
        tmp = np.zeros(region.shape, dtype=np.int32)
        tmp[ys, xs] = idx + 1
        for j in range(1, n + 1):
            sub, ns = ndimage.label(tmp == j)
            for s in range(1, ns + 1):
                mk = sub == s
                if mk.sum() * res * res < 0.04:
                    continue
                lab[mk] = k
                k += 1
    # 没分到块的零碎像素并到最近的块
    if k > 0:
        miss = region & (lab < 0)
        if miss.any():
            _, (iy, ix) = ndimage.distance_transform_edt(lab < 0, return_indices=True)
            lab[miss] = lab[iy[miss], ix[miss]]
    return lab, k
