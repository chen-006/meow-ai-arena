# -*- coding: utf-8 -*-
"""地图预处理（venv 的 Python 跑），按地图指纹缓存，同一张图只算一次：
    python prep.py <回放文件> [--force]
输出到 renderer/work/cache/<地图名_指纹>/（环境变量 DART_CACHE 可改）：
    island.npz   各塌陷状态的岛体（state0 = 开局，stateK = 前 K 波塌掉以后）
    pieces.npz   每一波塌掉的那片地裂成的碎块（不规则形状，正好拼满"塌前 − 塌后"的差）
    pieces.json  每块的波号、中心、离剩余陆地的距离（决定先后）
    labels.npz   高分辨率标号图：每个像素属于哪一块（-1 = 永久陆地 / 虚空），给石头、草、点缀物归属用
    ground.png   地面贴图；aux.npz 土路 / 安静角落的低分辨率图
另有共用的场外台地 cache\\decor.npz。
"""
import hashlib
import json
import os
import sys
import time
from multiprocessing import Pool

import numpy as np
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ground_paint  # noqa: E402
import replay  # noqa: E402
import terrain  # noqa: E402
import themes  # noqa: E402

CACHE = os.environ.get("DART_CACHE") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "cache")
MARGIN = 1.2


def corner_r(cell):
    """岛沿圆角的半径：1 米格子 0.36 米；格子细了就跟着小（0.5 米格子 0.22 米），免得把一格宽的缺口抹平。"""
    return min(0.36, 0.44 * cell)


def cache_dir(rp):
    return os.path.join(CACHE, rp.map_key())


def _state_job(a):
    k, mask2, origin, seed = a
    V, F, S = terrain.build_island(mask2, origin, corner_r=0, edge_wobble=0, seed=seed, voro_seed=seed)
    return "state%d" % k, V, F, S


def _piece_job(a):
    pid, crop, origin, seed = a
    V, F, S = terrain.build_island(crop, origin, corner_r=0.035, edge_wobble=0, seed=seed, cliff_h=0.55,
                                   taper_k=0.95, taper_p=1.1, depth_max=1.9, rough_amp=0.3, col_spacing=0.4,
                                   col_amp=0.05, ledge=0.03)
    return "p%d" % pid, V, F, S


def blob_mask(rx, ry, seed, res=terrain.RES, lobes=5):
    rng = np.random.default_rng(seed)
    W, H = rx * 2 + 2.0, ry * 2 + 2.0
    nx, ny = int(W / res), int(H / res)
    xs = (np.arange(nx) + 0.5) * res - W / 2
    ys = (np.arange(ny) + 0.5) * res - H / 2
    X, Y = np.meshgrid(xs, ys)
    f = np.zeros_like(X)
    for k in range(lobes):
        cx = rng.uniform(-0.45, 0.45) * rx
        cy = rng.uniform(-0.45, 0.45) * ry
        ax = rng.uniform(0.45, 0.7) * rx
        ay = rng.uniform(0.45, 0.7) * ry
        f = np.maximum(f, 1.0 - ((X - cx) / ax) ** 2 - ((Y - cy) / ay) ** 2)
    n = terrain.coarse_noise(f.shape, int(1.2 / res), seed + 5)
    m = (f + n * 0.12) > 0.0
    m = ndimage.binary_opening(m, iterations=3)
    return m, (-W / 2, -H / 2)


# 场外的岛：m* 是框住画面的台地，s* 是小浮石
DECOR = ([dict(name="m%02d" % i, rx=rx, ry=ry, seed=200 + i, cliff_h=3.4, depth=12.0, lobes=6)
          for i, (rx, ry) in enumerate([(2.6, 4.6), (2.4, 4.2), (8.5, 2.4), (3.0, 2.0), (2.6, 1.8),
                                        (10.5, 2.6), (3.0, 5.6), (2.8, 5.2)])] +
         [dict(name="s%02d" % i, rx=rx, ry=ry, seed=300 + i, cliff_h=0.6, depth=3.0, taper_k=0.6, lobes=3)
          for i, (rx, ry) in enumerate([(0.9, 0.7), (0.6, 0.5), (1.2, 0.8), (0.5, 0.45)])])


def _decor_job(sp):
    msk, org = blob_mask(sp["rx"], sp["ry"], sp["seed"], lobes=sp.get("lobes", 5))
    V, F, S = terrain.build_island(msk, org, seed=sp["seed"], depth_max=sp.get("depth", 9.0),
                                   taper_k=sp.get("taper_k", 0.35), taper_p=1.25, corner_r=0.3, edge_wobble=0.0,
                                   rough_amp=0.3, cliff_h=sp.get("cliff_h", 2.6), col_spacing=0.8, col_amp=0.14, mc_step=2)
    return sp["name"], V, F, S


def prep_decor(pool, force=False):
    out = os.path.join(CACHE, "decor.npz")
    if os.path.exists(out) and not force:
        return out
    res = {}
    for name, V, F, S in pool.imap_unordered(_decor_job, DECOR):
        res[name + "__V"], res[name + "__F"], res[name + "__S"] = V, F, S
    np.savez_compressed(out, **res)
    return out


def prep_map(rp, pool, force=False, seed=3):
    th = themes.resolve(rp)
    d = cache_dir(rp)
    ok = os.path.join(d, "ok.json")
    gkey = hashlib.md5((repr(sorted(th["ground"].items())) + repr(sorted(th["gstyle"].items())) + th["name"] +
                        str(ground_paint.VERSION)).encode()).hexdigest()[:10]
    gfile = os.path.join(d, "ground.key")
    stale = not os.path.exists(gfile) or open(gfile).read().strip() != gkey
    if os.path.exists(ok) and not force and ("--ground" in sys.argv or stale):       # 只重画地面贴图（主题或画法变了）
        aux = ground_paint.paint(rp.grid, rp.cell, rp.spawns(), os.path.join(d, "ground.png"), seed=seed + 4,
                                 colors=th["ground"], style=th["gstyle"])
        np.savez_compressed(os.path.join(d, "aux.npz"), **aux)
        open(gfile, "w").write(gkey)
        print("ground repaint", rp.map_key(), th["name"], flush=True)
        return d
    if os.path.exists(ok) and not force:
        return d
    os.makedirs(d, exist_ok=True)
    t0 = time.time()
    res = terrain.RES
    R = len(rp.wave_steps)
    # 各状态的圆角掩码（同一套噪声，保证后一状态是前一状态的子集）
    masks = []
    origin = None
    for k in range(R + 1):
        m, origin = terrain.rasterize_cells(rp.solid_mask(k), rp.cell, MARGIN, res)
        m2 = terrain.round_mask(m, corner_r(rp.cell), res, seed, 0.03)
        if masks:
            m2 &= masks[-1]
        masks.append(m2)
    jobs = [(k, masks[k], origin, seed) for k in range(R + 1)]
    # 碎块
    labels = np.full(masks[0].shape, -1, dtype=np.int32)
    meta = []
    pjobs = []
    pad = int(0.5 / res)
    for w in range(R):
        region = masks[w] & ~masks[w + 1]
        if not region.any():
            continue
        lab, n = terrain.fracture(region, res, seed + 100 + w)
        d_in = ndimage.distance_transform_edt(~masks[w + 1]) * res if masks[w + 1].any() else None
        for j in range(n):
            mk = lab == j
            ys, xs = np.where(mk)
            if len(ys) == 0:
                continue
            pid = len(meta)
            labels[mk] = pid
            y0, y1 = max(0, ys.min() - pad), min(mk.shape[0], ys.max() + pad + 1)
            x0, x1 = max(0, xs.min() - pad), min(mk.shape[1], xs.max() + pad + 1)
            cx = origin[0] + (xs.mean() + 0.5) * res
            cy = origin[1] + (ys.mean() + 0.5) * res
            din = float(d_in[int(ys.mean()), int(xs.mean())]) if d_in is not None else 3.0
            meta.append(dict(id=pid, wave=w, cx=float(cx), cy=float(cy), area=float(len(ys) * res * res), d_in=din))
            pjobs.append((pid, mk[y0:y1, x0:x1].copy(), (origin[0] + x0 * res, origin[1] + y0 * res), seed + 500 + pid))
    isl = {}
    for name, V, F, S in pool.imap_unordered(_state_job, jobs):
        isl[name + "__V"], isl[name + "__F"], isl[name + "__S"] = V, F, S
        print("  ", name, len(V), "verts", flush=True)
    np.savez_compressed(os.path.join(d, "island.npz"), **isl)
    pcs = {}
    for name, V, F, S in pool.imap_unordered(_piece_job, pjobs, chunksize=4):
        pcs[name + "__V"], pcs[name + "__F"], pcs[name + "__S"] = V, F, S
    np.savez_compressed(os.path.join(d, "pieces.npz"), **pcs)
    json.dump(meta, open(os.path.join(d, "pieces.json"), "w"))
    # near：没分到块的像素（圆角削掉的角、岛沿外侧）就近归到 0.6 米内的块，给岛沿垂草之类用
    near = labels.copy()
    if len(meta):
        dist, (iy, ix) = ndimage.distance_transform_edt(labels < 0, return_indices=True)
        mk = (labels < 0) & (dist * res < 0.6)
        near[mk] = labels[iy[mk], ix[mk]]
    np.savez_compressed(os.path.join(d, "labels.npz"), labels=labels.astype(np.int16), near=near.astype(np.int16),
                        origin=np.array(origin, dtype=np.float32), res=np.float32(res))
    aux = ground_paint.paint(rp.grid, rp.cell, rp.spawns(), os.path.join(d, "ground.png"), seed=seed + 4,
                             colors=th["ground"], style=th["gstyle"])
    np.savez_compressed(os.path.join(d, "aux.npz"), **aux)
    open(gfile, "w").write(gkey)
    json.dump(dict(map=rp.mapname, W=rp.W, H=rp.H, cell=rp.cell, states=R + 1, pieces=len(meta),
                   sec=round(time.time() - t0, 1)), open(ok, "w"))
    print("prep", rp.map_key(), "states", R + 1, "pieces", len(meta), "%.1fs" % (time.time() - t0), flush=True)
    return d


def main():
    force = "--force" in sys.argv
    files = [a for a in sys.argv[1:] if not a.startswith("--")]
    os.makedirs(CACHE, exist_ok=True)
    with Pool(5) as pool:
        prep_decor(pool, force and "--decor" in sys.argv)
        for f in files:
            prep_map(replay.load(f), pool, force)


if __name__ == "__main__":
    main()
