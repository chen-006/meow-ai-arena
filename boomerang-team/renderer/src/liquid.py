# -*- coding: utf-8 -*-
"""坑里的液面（第 12 集加）：岩浆湖、港湾的水。纯 Python，Blender 和普通 Python 都能用。

主题字典里可以写一个 liquid 键（不写 = 没有液面，坑照旧是通到云海的洞）：
    liquid=dict(kind="lava" | "water", z=-0.55, where="enclosed" | "all", min_cells=1)
  kind       液面种类：env 那边决定画法；pose / fx 用它决定掉进去时的水花颜色
  z          液面高度（米，地面是 0）
  where      enclosed = 只有被陆地围住、不碰地图边的坑；all = 所有坑（整张图像浮在海面上）
  min_cells  连通块至少几格才算（太小的坑仍然是洞）
画液面（几何、材质）由 env.py 负责；这里只回答"哪些格子有液面、液面多高"。
"""


def pit_components(rp):
    """所有坑格的四连通块：[(格子列表, 是否碰到地图边)]。格子是 (x, y)。"""
    H, W = rp.H, rp.W
    seen = set()
    out = []
    for y in range(H):
        for x in range(W):
            if rp.grid[y][x] != 2 or (x, y) in seen:
                continue
            st, comp, border = [(x, y)], [], False
            seen.add((x, y))
            while st:
                q = st.pop()
                comp.append(q)
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    r = (q[0] + dx, q[1] + dy)
                    if not (0 <= r[0] < W and 0 <= r[1] < H):
                        border = True
                    elif rp.grid[r[1]][r[0]] == 2 and r not in seen:
                        seen.add(r)
                        st.append(r)
            out.append((comp, border))
    return out


def spec(rp):
    """这份回放的主题有没有液面：dict 或 None。"""
    import themes
    th = themes.get(getattr(rp, "theme", None) or themes.resolve(rp)["name"])
    lq = th.get("liquid")
    return dict(lq) if lq else None


def cells(rp):
    """有液面的格子集合（坑格）。"""
    if hasattr(rp, "_liquid_cells"):
        return rp._liquid_cells
    sp = spec(rp)
    out = set()
    if sp:
        for comp, border in pit_components(rp):
            if sp.get("where", "enclosed") == "enclosed" and border:
                continue
            if len(comp) < sp.get("min_cells", 1):
                continue
            out.update(comp)
    rp._liquid_cells = out
    return out


def z_at(rp, mx, my):
    """地图坐标 (米) 这一点下面有没有液面：液面高度（米）或 None。地图外面：where=all 时也算有。"""
    sp = spec(rp)
    if not sp:
        return None
    cx, cy = int(mx // rp.cell), int(my // rp.cell)
    if not (0 <= cx < rp.W and 0 <= cy < rp.H):
        return sp.get("z", -0.55) if sp.get("where") == "all" else None
    return sp.get("z", -0.55) if (cx, cy) in cells(rp) else None


def kind(rp):
    sp = spec(rp)
    return sp.get("kind") if sp else None
