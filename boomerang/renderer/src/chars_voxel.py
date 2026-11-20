# -*- coding: utf-8 -*-
"""像素（体素）画风的小人在 Blender 里的装配和逐帧摆放。角色定义在 voxchars\\（一人一个文件），积木在 voxkit.py。
接口和 chars.CharRig 一样：VoxRig(key, coll, name, team_color, ring_color)，apply(pose)。pose 的字段见 pose.py。

和旧的圆润小人不同的地方：
  * 每个部件是一堆方块，平直着色、几乎不反光；明暗靠受光面 / 背光面的色块；
  * 手臂、腿是连在肩和胯上的方柱，按动作里"手 / 脚要到的位置"换算成绕肩、绕胯的刚性转动；
  * 跑动时整个人一蹦一蹦（pose 里的 hop）；
  * 描边仍是反向外壳，但更细；4v4 时外面再包一圈队伍色。
"""
import math

import bpy
import numpy as np
from mathutils import Matrix, Vector

import cast
import chars as CH
import pose as POSE
import voxchars
import voxkit as VK
from geom import MeshBuilder
from scene_lib import hexc, NB, _MATS, _objcol

OUTLINE_W = float(__import__("os").environ.get("DART_VOX_OUTLINE", "0.0125"))          # 深色描边的宽度（角色局部单位；一个方块 0.07）
TEAM_W = 0.024              # 4v4：深色描边外那圈队伍色
TEAM_PARTS = ("body", "head", "hairback", "tailL", "tailR", "tail")
VOX_EMIT = 0.16             # 自发光一点点：背光面不发灰，颜色保持干净
OUTLINE_COL = np.array(hexc("#2E2238"))


def mat_vox():
    if 'vox' in _MATS:
        return _MATS['vox']
    m = bpy.data.materials.new("M_vox")
    nb = NB(m)
    col = _objcol(nb, nb.out(nb.attr('col'), 'Color'))
    p = nb.principled(col, rough=0.92, spec=0.04, emission=col, emis_strength=VOX_EMIT)
    nb.output(p)
    _MATS['vox'] = m
    return m


def build_part(part, coll, name, team=None, width=OUTLINE_W):
    d = part.mesh(outline_w=width if part.outline else 0.0)
    mb = MeshBuilder()
    mb.add(d["V"], d["F"], 0, vcolors=d["C"], smooth=False)
    if "OV" in d:
        oc = d["OC"] * 0.2 + OUTLINE_COL * 0.8
        oc[:, 3] = 1.0
        mb.add(d["OV"], d["OF"], 1, vcolors=oc, smooth=False)
        if team is not None:
            d2 = part.mesh(outline_w=width + TEAM_W)
            mb.add(d2["OV"], d2["OF"], 1, vcolors=np.tile(team, (len(d2["OV"]), 1)), smooth=False)
    ob = mb.build(name, [mat_vox(), CH.mat_outline()], coll)
    ob.location = tuple(np.array(part.pivot) * VK.U)
    return ob


DART_CELL = 0.058           # 像素镖一格的边长（飞行时放大 1.42 倍，和角色的方块差不多大）


def dart_part(color_hex, blades=3):
    """三叶镖的像素版：把三片弯刃和中间的圆毂描到一张小格子上，再挤出一格厚。刃尖浅色、圆毂白色。"""
    def mixh(h, w, to=255):
        h = h.lstrip("#")
        return "#" + "".join("%02X" % int(int(h[i:i + 2], 16) * (1 - w) + to * w) for i in (0, 2, 4))
    main, light, deep = color_hex, mixh(color_hex, 0.5), mixh(color_hex, 0.28, to=20)
    P = VK.VPart("dart", (0.5, 0.5, 0.5))
    n = 6
    for i in range(-n, n + 1):
        for j in range(-n, n + 1):
            x, y = i * DART_CELL, j * DART_CELL
            r = math.hypot(x, y)
            if r < 0.085:
                P.put(i, j, 0, "#FFFFFF" if r > 0.03 else main)
                continue
            best = None
            for b in range(blades):
                a0 = 2 * math.pi * b / blades
                for k in range(41):
                    t = k / 40.0
                    rr = 0.05 + 0.26 * t
                    ang = a0 + 0.8 * t ** 1.25
                    d = math.hypot(x - rr * math.cos(ang), y - rr * math.sin(ang))
                    w = 0.074 * (1.0 - 0.32 * t)
                    if d < w and (best is None or d / w < best[0]):
                        # 在弯刃的外侧（迎风的一边）还是内侧
                        side = (x * math.cos(ang) + y * math.sin(ang)) - rr
                        best = (d / w, t, side)
            if best is not None:
                P.put(i, j, 0, light if best[1] > 0.8 else (deep if best[2] < -0.012 else main))
    return P


def make_dart(color_hex, coll, name, scale=1.0, width=0.012):
    P = dart_part(color_hex)
    d = P.mesh(outline_w=width / max(scale, 1e-6), unit=DART_CELL)
    mb = MeshBuilder()
    mb.add(d["V"] * scale, d["F"], 0, vcolors=d["C"], smooth=False)
    oc = d["OC"] * 0.2 + OUTLINE_COL * 0.8
    oc[:, 3] = 1.0
    mb.add(d["OV"] * scale, d["OF"], 1, vcolors=oc, smooth=False)
    from scene_lib import mat_vcol
    return mb.build(name, [mat_vcol("M_dart_vox", rough=0.8, spec=0.05, emis=0.35, objcol=True), CH.mat_outline()], coll)


class VoxRig:
    def __init__(self, key, coll, name, team_color=None, ring_color=None):
        self.key = key
        vk = cast.vox_key(key)
        self.model = M = voxchars.build(vk, team_color)
        self.color = voxchars.color(vk)
        self.scale = CH.CHAR_SCALE * M.scale
        root = self.root = bpy.data.objects.new(name, None)
        root.rotation_mode = 'QUATERNION'
        coll.objects.link(root)
        tcol = np.array(hexc(team_color)) if team_color is not None else None
        self.parts, self.rest = {}, {}
        for nm, part in M.parts.items():
            ob = build_part(part, coll, name + "_" + nm, team=tcol if nm in TEAM_PARTS else None)
            self.parts[nm] = ob
            self.rest[nm] = np.array(part.pivot) * VK.U
        P = self.parts
        for nm, ob in P.items():
            par, ref = root, None
            if nm in ("hairback", "tailL", "tailR", "face_open", "face_tight", "face_happy"):
                par, ref = P["head"], "head"
            elif nm in ("armA", "armB", "tail"):
                par, ref = P["body"], "body"
            ob.parent = par
            if ref:
                ob.location = tuple(self.rest[nm] - self.rest[ref])
            ob.rotation_mode = 'QUATERNION' if nm in ("armA", "armB") else 'XYZ'
        self.arm_len = M.arm_len * VK.U
        self.dart = make_dart(self.color, coll, name + "_held", scale=0.74, width=0.011)
        self.dart.parent = root
        self.blob = CH._blob(coll, name + "_blob")
        self.ring = CH._ring(coll, name + "_ring", ring_color or self.color, team=team_color is not None)
        self.objs = list(P.values()) + [self.dart]
        self.coll = coll
        self.name = name
        self.halves = None          # 被镖切开时用的两半（build_slice 建）

    def set_visible(self, vis):
        for ob in self.objs + [self.blob, self.ring]:
            if ob.hide_render != (not vis):
                ob.hide_render = not vis
        if self.halves and not vis:
            for h in self.halves["obs"] + [getattr(self, "blob2", None)]:
                if h is not None:
                    h.hide_render = True

    # ------------------------------------------------------------ 被镖切成两半
    def build_slice(self, info):
        """按"被切前一瞬的姿势"和镖的方向，把整个人沿一个斜面分成两堆方块，各烘成一个物体（世界坐标）。
        分界处新露出来的面刷成一个干净的浅色（主色调得很淡），没有任何内部结构。平时两半都藏着。"""
        self.apply(info["base"])
        root = self.root
        M0 = root.matrix_basis.copy()
        dx, dy = info["d"]
        d = Vector((dx, -dy, 0.0)).normalized()
        s = Vector((info["s"][0], -info["s"][1], 0.0)).normalized()
        up = Vector((0.0, 0.0, 1.0))
        n = up * math.cos(info["tilt"]) + s * math.sin(info["tilt"])
        p0 = M0.translation + up * 0.5
        if abs(info["tilt"]) < 1e-3:
            # 横切：切在身体（躯干 + 裙子）从下往上 65% 的那条方块缝上，也就是腰；站直时这条缝是水平的，切面平整
            bp = self.model.parts["body"]
            zs = [kk[2] for kk in bp.vox]
            zb = min(zs) + int((max(zs) + 1 - min(zs)) * 0.65 + 0.5)
            mb_ = self.parts["body"].matrix_basis.copy()
            Ab = np.array(M0 @ mb_)
            wz = Ab[:3, :3] @ ((np.array([0.0, 0.0, zb]) - np.array(bp.pivot)) * VK.U) + Ab[:3, 3]
            p0 = Vector((M0.translation.x, M0.translation.y, float(wz[2])))
        cut = self._cut_color()
        builders = [MeshBuilder(), MeshBuilder()]
        cents = [[], []]
        for nm, ob in self.parts.items():
            if nm.startswith("face_") and nm != "face_tight":
                continue
            m = ob.matrix_basis.copy()
            par = ob.parent
            while par is not None and par != root:
                m = par.matrix_basis @ m
                par = par.parent
            Mw = M0 @ m
            A = np.array(Mw)
            part = self.model.parts[nm]
            piv = np.array(part.pivot)
            subs = [VK.VPart(nm, part.pivot, part.outline), VK.VPart(nm, part.pivot, part.outline)]
            side = {}
            for key in part.vox:
                c = (np.array(key) + 0.5 - piv) * VK.U
                w = A[:3, :3] @ c + A[:3, 3]
                side[key] = 0 if (Vector(w) - p0).dot(n) >= 0 else 1
            for key, col in part.vox.items():
                sp = subs[side[key]]
                sp.vox[key] = col
                for dd, (ax, ay, az) in enumerate(VK.DIRS):
                    nb = (key[0] + ax, key[1] + ay, key[2] + az)
                    if nb in side and side[nb] != side[key]:
                        sp.paint[key + (dd,)] = cut
                    elif key + (dd,) in part.paint:
                        sp.paint[key + (dd,)] = part.paint[key + (dd,)]
            for sl in part.slabs:
                c = (np.array([(sl[0] + sl[1]) / 2, (sl[2] + sl[3]) / 2, (sl[4] + sl[5]) / 2]) - piv) * VK.U
                w = A[:3, :3] @ c + A[:3, 3]
                dist = (Vector(w) - p0).dot(n)
                if abs(dist) < 0.3 * VK.U:          # 正好骑在切面上的小贴片（嘴）不要了
                    continue
                subs[0 if dist >= 0 else 1].slabs.append(sl)
            for i, sp in enumerate(subs):
                if not sp.vox and not sp.slabs:
                    continue
                dm = sp.mesh(outline_w=OUTLINE_W if sp.outline else 0.0)
                V = dm["V"] @ A[:3, :3].T + A[:3, 3]
                builders[i].add(V, dm["F"], 0, vcolors=dm["C"], smooth=False)
                cents[i].append(V)
                if "OV" in dm:
                    oc = dm["OC"] * 0.2 + OUTLINE_COL * 0.8
                    oc[:, 3] = 1.0
                    builders[i].add(dm["OV"] @ A[:3, :3].T + A[:3, 3], dm["OF"], 1, vcolors=oc, smooth=False)
        obs, cs = [], []
        for i in range(2):
            ob = builders[i].build("%s_half%d" % (self.name, i), [mat_vox(), CH.mat_outline()], self.coll)
            ob.hide_render = True
            obs.append(ob)
            allv = np.concatenate(cents[i]) if cents[i] else np.zeros((1, 3))
            cs.append(Vector(((allv[:, 0].min() + allv[:, 0].max()) / 2, (allv[:, 1].min() + allv[:, 1].max()) / 2,
                              (allv[:, 2].min() + allv[:, 2].max()) / 2)))
        g0 = float(M0.translation.z)
        # 每截取一小把顶点，摆放时用来算"最低点在哪"（落地、搁在下半截的切面上）
        samp = []
        for i in range(2):
            allv = np.concatenate(cents[i]) if cents[i] else np.zeros((1, 3))
            step = max(1, len(allv) // 400)
            samp.append(allv[::step].copy())
        hw = float(max(0.05, ((samp[1][:, :2] - np.array([p0.x, p0.y])) @ np.array([s.x, s.y])).max()))
        self.halves = dict(obs=obs, c=cs, d=d, s=s, n=n, up=up, ground=g0, base=Vector((p0.x, p0.y, g0)),
                           land=info.get("land", (True, True)), samp=samp, cut_z=float(p0.z), hw=hw,
                           horiz=abs(info["tilt"]) < 1e-3)
        # 往外倒的方向：绕 d 正转时顶部往 +s 走就记 +1
        self.halves["sg"] = 1.0 if (Matrix.Rotation(0.3, 3, d) @ up).dot(s) > 0 else -1.0
        print("SLICE_CUT_Z %s %.3f" % (self.key, float(p0.z) - g0), flush=True)
        self.blob2 = CH._blob(self.coll, self.name + "_blob2")
        self.blob2.hide_render = True

    def _cut_color(self):
        """切面的颜色：一个干净的纯色。主色调得很淡，或者主色压深一号，取和头发反差大的那个。"""
        def rgb(hx):
            hx = hx.lstrip("#")
            return np.array([int(hx[i:i + 2], 16) for i in (0, 2, 4)], dtype=float)
        cnt = {}
        for c in self.model.parts["head"].vox.values():
            cnt[c] = cnt.get(c, 0) + 1
        hair = rgb(max(cnt, key=lambda k: cnt[k] if k != self.model.skin else 0))
        cnt = {}
        for c in self.model.parts["body"].vox.values():
            cnt[c] = cnt.get(c, 0) + 1
        body = rgb(max(cnt, key=lambda k: cnt[k]))
        main = rgb(self.color)
        # 横切时切面四周是衣服（下半截朝上）和头发（长发也被切断），所以要和两者都拉开
        cands = [main * 0.2 + 255 * 0.8, main * 0.4 + 255 * 0.6, np.array([255.0, 246.0, 222.0])]
        best = max(cands, key=lambda c: min(float(np.abs(c - hair).sum()), float(np.abs(c - body).sum())))
        return "#" + "".join("%02X" % int(v) for v in best)

    @staticmethod
    def _xf(M, P):
        return P @ np.array(M)[:3, :3].T + np.array(M)[:3, 3]

    def _apply_slice(self, p):
        """横切：上半截沿镖的方向滑出去、从下半截上掉下来并侧翻（切面翻向镜头）；下半截顿一下再朝镜头倒下。
        两截都按顶点样本算最低点，保证搁在下半截的切面上 / 落在地上，不穿地。"""
        H = self.halves
        for ob in self.objs:
            if not ob.hide_render:
                ob.hide_render = True
        self.ring.hide_render = True
        u = p["slice"]["u"]
        c = POSE.slice_curve(u)
        d, s, up = H["d"], H["s"], H["up"]
        g0 = H["ground"]
        col = (1.0, 1.0, 1.0, 1.0 - c["flash"])
        G = 0.03                                  # 重力（米/步²，比真实的略大一点，落得干脆）
        # ---- 上半截
        ob, ci = H["obs"][0], H["c"][0]
        S = Matrix.Diagonal((c["scale"],) * 3).to_4x4()
        R = Matrix.Rotation(c["yaw_up"] * H["sg"], 4, up) @ Matrix.Rotation(-H["sg"] * c["th_up"], 4, d)
        off = d * c["carry"]
        W = Matrix.Translation(off) @ Matrix.Translation(ci) @ R @ S @ Matrix.Translation(-ci)
        m0 = float(self._xf(W, H["samp"][0])[:, 2].min())
        tau = u - c["leave"]
        dz = c["lift"] - (0.5 * G * tau * tau if tau > 0 else 0.0)
        if tau <= 0 or c["carry"] < 0.24:
            floor = H["cut_z"]
        elif H["land"][0]:
            floor = g0
        else:
            floor = None                          # 底下是坑 / 云海：一直掉
        if floor is not None:
            dz = max(dz, floor - m0)
        top_dz = dz
        ob.hide_render = (floor is None and dz < -9.0)
        ob.matrix_world = Matrix.Translation(up * dz) @ W
        ob.color = col
        # ---- 下半截：绕靠镜头那一侧（+s）的底边倒下
        ob, ci = H["obs"][1], H["c"][1]
        piv = H["base"] + s * H["hw"]
        R = Matrix.Rotation(H["sg"] * c["th_low"], 4, d)
        W = Matrix.Translation(piv) @ R @ Matrix.Translation(-piv) @ Matrix.Translation(ci) @ S @ Matrix.Translation(-ci)
        dz = 0.0
        if not H["land"][1]:
            v = max(0.0, u - 4.5)
            dz = -0.5 * G * v * v
        else:
            m1 = float(self._xf(W, H["samp"][1])[:, 2].min())
            dz = max(0.0, g0 - m1)
        ob.hide_render = dz < -9.0
        ob.matrix_world = Matrix.Translation(up * dz) @ W
        ob.color = col
        # ---- 接触影：下半截一个；上半截落地后跟着它一个
        ga = p.get("ground", 0.0)
        if ga <= 0.01:
            self.blob.hide_render = True
        else:
            self.blob.hide_render = False
            self.blob.location = (H["base"].x + s.x * H["hw"] * 0.8 * min(1.0, c["th_low"] / 1.5),
                                  H["base"].y + s.y * H["hw"] * 0.8 * min(1.0, c["th_low"] / 1.5), 0.012)
            self.blob.scale = (1.0, 1.0, 1)
            self.blob.color = (1, 1, 1, ga)
        b2 = getattr(self, "blob2", None)
        if b2 is not None:
            hgt = top_dz + H["cut_z"] - g0
            if H["land"][0] and ga > 0.01 and c["carry"] >= 0.24:
                b2.hide_render = False
                q = H["base"] + d * c["carry"]
                b2.location = (q.x, q.y, 0.012)
                b2.scale = (0.9, 0.9, 1)
                b2.color = (1, 1, 1, ga * max(0.3, 1.0 - hgt * 1.5))
            else:
                b2.hide_render = True

    def apply(self, p):
        if not p.get("visible", True):
            self.set_visible(False)
            return
        self.set_visible(True)
        if self.halves:
            if p.get("slice") is not None:
                self._apply_slice(p)
                return
            for h in self.halves["obs"] + [self.blob2]:
                if not h.hide_render:
                    h.hide_render = True
        s = self.scale
        M = self.model
        hz = math.radians(90.0 - p["heading"])
        zz = p.get("z", 0.0) + 0.055 * p.get("hop", 0.0)
        Mx = (Matrix.Translation((p["x"], -p["y"], zz)) @ Matrix.Rotation(hz, 4, 'Z') @
              Matrix.Rotation(p.get("lean", 0.0) * 0.85, 4, 'X') @ Matrix.Rotation(p.get("roll", 0.0), 4, 'Y') @
              Matrix.Rotation(p.get("spin", 0.0), 4, 'Z'))
        if p.get("flip"):
            c = Matrix.Translation((0, 0, 0.45 * s))
            Mx = Mx @ c @ Matrix.Rotation(p["flip"], 4, 'X') @ c.inverted()
        k = p.get("scale", 1.0)
        sx, sy, sz = p.get("sx", 1.0), p.get("sy", p.get("sx", 1.0)), p.get("sz", 1.0)
        loc, rot, _ = Mx.decompose()
        self.root.location = loc
        self.root.rotation_quaternion = rot
        self.root.scale = (s * k * sx, s * k * sy, s * k * sz)
        P, R = self.parts, self.rest
        bdz = p.get("body_dz", 0.0)
        tw = p.get("twist", 0.0)
        P["body"].location = (0, 0, bdz)
        P["body"].rotation_euler = (0, 0, tw)
        hp, hy, hr = p.get("head", (0, 0, 0))
        P["head"].location = (R["head"][0], R["head"][1], R["head"][2] + bdz + p.get("head_dz", 0.0))
        P["head"].rotation_euler = (hp, hr, hy + tw * 0.5)
        hb, hs = p.get("hair", (0, 0))
        if "hairback" in P:
            P["hairback"].rotation_euler = (-hb * M.hair_swing, 0, hs * 0.6)
        for nm, sg in (("tailL", 1), ("tailR", -1)):
            if nm in P:
                P[nm].rotation_euler = (-hb * 1.1, sg * (0.04 + hb * 0.3) + hs * 0.5, hs * 0.5)
        if "tail" in P:
            tb, ts = p.get("tail", (hb * 0.5, hs))
            P["tail"].rotation_euler = (M.tail_rest - tb * 0.6 * M.tail_swing, 0, ts * 1.5 * M.tail_swing)
        # 手臂：绕肩刚性转到"手要去的方向"，伸得远时略微拉长
        ct, st_ = math.cos(-tw), math.sin(-tw)
        tip_a = None
        for nm, key_, sg in (("armA", "hand_a", 1), ("armB", "hand_b", -1)):
            f, sd, u = p[key_]
            dx, dy, dz = (sd * sg - 0.22) * sg, -f, u - 0.40
            d = Vector((dx * ct - dy * st_, dx * st_ + dy * ct, dz))
            L = d.length
            if L < 1e-5:
                d, L = Vector((0, 0, -1)), 1.0
            q = Vector((0.0, 0.0, -1.0)).rotation_difference(d / L)
            ob = P[nm]
            ob.rotation_quaternion = q
            stretch = 1.0 + 0.3 * min(1.0, max(0.0, (L - 0.14) / 0.3))
            ob.scale = (1.0, 1.0, stretch)
            if sg > 0:
                sh = Vector(R[nm]) + Vector((0, 0, bdz))
                tip = (d / L) * (self.arm_len * stretch)
                tip_a = Vector((sh.x * math.cos(tw) - sh.y * math.sin(tw), sh.x * math.sin(tw) + sh.y * math.cos(tw), sh.z)) + \
                    Vector((tip.x * math.cos(tw) - tip.y * math.sin(tw), tip.x * math.sin(tw) + tip.y * math.cos(tw), tip.z))
        # 腿：绕胯前后摆，抬脚时往上收一点
        for nm, key_, sg in (("footA", "foot_a", 1), ("footB", "foot_b", -1)):
            f, sd, u = p[key_]
            th = -max(-1.3, min(1.3, f / 0.16)) * 0.7 + p.get(key_ + "_pitch", 0.0) * 0.45
            ob = P[nm]
            ob.rotation_euler = (th, 0, 0)
            ob.location = (R[nm][0] + (sd - sg * 0.085) * 0.6, R[nm][1], R[nm][2] + max(0.0, u - 0.04) * 0.55)
        face = p.get("face", "open")
        for nm in ("open", "tight", "happy"):
            ob = P["face_" + nm]
            hide = (nm != face)
            if ob.hide_render != hide:
                ob.hide_render = hide
        d = p.get("dart")
        if d is None or tip_a is None:
            self.dart.hide_render = True
        else:
            self.dart.hide_render = False
            f, sd, u = d["pos"]
            ha = p["hand_a"]
            off = Vector((sd - ha[1], -(f - ha[0]), u - ha[2]))
            self.dart.location = tip_a + off * 0.7
            self.dart.rotation_euler = d["rot"]
            self.dart.scale = (d.get("scale", 1.0),) * 3
        tint = p.get("tint", (1, 1, 1))
        col = (tint[0], tint[1], tint[2], 1.0 - p.get("flash", 0.0))
        for ob in self.objs:
            ob.color = col
        gx, gy = p.get("gx", p["x"]), p.get("gy", p["y"])
        ga = p.get("ground", 1.0)
        if ga <= 0.01:
            self.blob.hide_render = True
            self.ring.hide_render = True
        else:
            hgt = max(0.0, zz)
            bs = max(0.3, 1.0 - hgt * 0.35)
            self.blob.location = (gx, -gy, 0.012)
            self.blob.scale = (bs, bs, 1)
            self.blob.color = (1, 1, 1, ga * max(0.25, 1.0 - hgt * 0.4))
            self.ring.location = (gx, -gy, 0.016)
            self.ring.rotation_euler = (0, 0, hz)
            self.ring.color = (1, 1, 1, ga * p.get("ring", 1.0))
