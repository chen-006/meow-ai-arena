#!/usr/bin/env python3
"""造桥大赛 模拟器前端（选手和裁判共用同一份代码）。

用法：
  python bridge.py check  <关卡> <设计.json>                       检查设计是否合法、算造价
  python bridge.py run    <关卡> <设计.json> [场景.json ...] [--replay 回放.json] [--html 回放.html] [-q]
        不给场景时跑该关全部公开样例场景。--html 生成可直接双击打开的回放网页。
  python bridge.py duel   <桥设计.json> <捣蛋车.json> [--html 回放.html]           第 7 关攻防（关卡 final）
退出码：0 = 成功且全部场景通过；1 = 设计不合法；2 = 命令参数错误；3 = 有场景没通过。
<关卡> 可以写关卡编号（1～6、final）或关卡文件路径。JSON 用 UTF-8 编码（带不带 BOM 都可以）。
物理计算在 bridge_sim.exe 里完成（同目录），本文件负责读文件、检查、换算、汇总。
"""
import json, math, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SIM_EXE = os.environ.get('BRIDGE_SIM') or os.path.join(HERE, 'bridge_sim.exe')

# ------------------------------------------------------------------ 材料表（全部关卡通用）
# max_len 最大长度(米)  kg_m 每米质量  EA 轴向刚度(牛)  tension 受拉上限(牛)
# comp 受压上限(牛)，实际受压上限 = min(comp, buckle / 长度²)   cost_m 每米价格  cost_each 每根固定价
MATERIALS = {
    'road':      dict(name='路面', max_len=2.5, kg_m=50, EA=3e8, tension=90e3, comp=90e3, buckle=300e3, cost_m=150, cost_each=0),
    'wood':      dict(name='木杆', max_len=3.0, kg_m=12, EA=6e7, tension=50e3, comp=50e3, buckle=120e3, cost_m=40, cost_each=0),
    'steel':     dict(name='钢杆', max_len=6.0, kg_m=35, EA=4e8, tension=140e3, comp=140e3, buckle=900e3, cost_m=160, cost_each=0),
    'cable':     dict(name='钢索', max_len=30.0, kg_m=4, EA=2e8, tension=90e3, comp=0, buckle=0, cost_m=70, cost_each=0),
    'spring':    dict(name='弹簧', max_len=4.0, kg_m=10, k=4e5, tension=60e3, comp=60e3, buckle=1e12, cost_m=30, cost_each=300),
    'hydraulic': dict(name='液压杆', max_len=5.0, kg_m=40, EA=3e8, tension=250e3, comp=250e3, buckle=1e12, cost_m=150, cost_each=800,
                      speed=15.0, ratio=(0.5, 2.0)),
}
JOINT_KG = 5.0          # 每个节点自带质量
JOINT_COST = 20         # 每个新节点价格
SENSOR_COST = {'plate': 100, 'gate': 100, 'start': 0, 'horn': 50}
LIGHT_COST = 200
LIMITS = dict(nodes=300, members=600, sensors=20, rules=40, lights=4, min_len=0.3, min_gap=0.1)

# ------------------------------------------------------------------ 车辆
VEHICLES = {
    'car':   dict(name='小轿车', mass=1000, wb=1.1, r=0.22, h=0.42, br=0.14, speed=8.0, accel=3.0, brake=6.0),
    'van':   dict(name='面包车', mass=1800, wb=1.2, r=0.22, h=0.55, br=0.15, speed=7.0, accel=2.5, brake=5.0),
    'truck': dict(name='卡车', mass=5000, wb=1.9, r=0.28, h=0.65, br=0.18, speed=5.5, accel=1.8, brake=4.0),
}
FAIL_REASONS = {0: '', 1: '落水/坠落', 2: '翻车/车身触地', 3: '摔坏（落地太猛）', 4: '超时未到', 5: '撞船', 6: '散架', 7: '异常'}
ANOMALY = {0: '', 1: '速度超限（能量爆炸）', 2: '穿模（车轮穿过路面）', 3: '数值错误', 4: '杆件异常拉伸（数值失稳）'}

SIM = dict(dt=1 / 60, substeps=20, gravity=9.81, damping=0.15, vcap=60.0, landing=12.0, rec_every=2, settle=2.5)


class DesignError(Exception):
    """选手文件的问题（格式、超限、超预算）：该关/该方判 0 或判负。"""
    pass


class EngineError(RuntimeError):
    """评测系统的问题（引擎启动失败、超时、输出损坏）：不能算作选手成绩，正式评分应中止或标为待评。"""
    pass


SIM_TIMEOUT = 120


# ------------------------------------------------------------------ 读文件
def _no_const(c):
    raise ValueError(f'JSON 里不能有非有限数值 {c}')


def load_json(path):
    """读 JSON：兼容带 BOM 的 UTF-8（Windows 记事本/PowerShell 常见）；拒绝 NaN、Infinity。"""
    with open(path, 'rb') as f:
        return loads_bytes(f.read())


def loads_bytes(data):
    """从已读入的字节解析 JSON（规则同 load_json）。主办方评分用：同一份字节既算 sha256 又解析，二者必然一致。"""
    try:
        return json.loads(data.decode('utf-8-sig'), parse_constant=_no_const)
    except RecursionError:
        raise ValueError('JSON 嵌套层数太深')


def NUM(v, where, lo=None, hi=None):
    """严格数值：只接受 int/float（不接受 bool、字符串），必须有限，可选范围检查。"""
    if isinstance(v, bool) or not isinstance(v, (int, float)):
        raise DesignError(f'{where} 必须是数字（现在是 {type(v).__name__}: {str(v)[:40]!r}）')
    try:
        v = float(v)
    except OverflowError:
        raise DesignError(f'{where} 数值太大（约 {int(v.bit_length() * 0.30103)} 位整数），必须是有限的普通数字')
    if not math.isfinite(v):
        raise DesignError(f'{where} 必须是有限数字')
    if lo is not None and v < lo:
        raise DesignError(f'{where} = {v:g} 小于下限 {lo:g}')
    if hi is not None and v > hi:
        raise DesignError(f'{where} = {v:g} 大于上限 {hi:g}')
    return v


def level_path(arg):
    arg = str(arg)   # 允许传整数关卡号（os.path.exists(整数) 会把它当文件描述符，导致进程静默退出）
    p = os.path.join(HERE, 'levels', f'level{arg}.json')   # 先认关卡编号（当前目录里有同名文件夹也不受影响）
    if os.path.isfile(p):
        return p
    if os.path.isfile(arg):
        return arg
    raise SystemExit(f'找不到关卡 {arg}')


def load_level(arg):
    p = level_path(arg)
    lv = load_json(p)
    lv['_dir'] = os.path.dirname(os.path.abspath(p))
    return lv


def sample_paths(lv):
    return [os.path.join(lv['_dir'], s) for s in lv.get('samples', [])]


# ------------------------------------------------------------------ 几何工具
def point_in_poly(x, y, poly):
    inside = False
    n = len(poly)
    for i in range(n):
        x1, y1 = poly[i]; x2, y2 = poly[(i + 1) % n]
        if (y1 > y) != (y2 > y):
            xi = x1 + (y - y1) * (x2 - x1) / (y2 - y1)
            if x < xi:
                inside = not inside
    return inside


def dist_point_seg(px, py, x1, y1, x2, y2):
    ex, ey = x2 - x1, y2 - y1
    l2 = ex * ex + ey * ey
    t = 0 if l2 == 0 else max(0, min(1, ((px - x1) * ex + (py - y1) * ey) / l2))
    return math.hypot(px - x1 - t * ex, py - y1 - t * ey)


# ------------------------------------------------------------------ 检查设计 + 算造价
def parse_design(lv, d):
    """返回 dict(nodes=[(x,y,fixed)], members=[(i,j,mat,group)], sensors, lights, rules, cost, cost_detail)。出错抛 DesignError。"""
    if not isinstance(d, dict):
        raise DesignError('设计文件顶层必须是一个 JSON 对象 {...}')
    for k, typ in (('nodes', list), ('members', list), ('sensors', list), ('lights', list), ('rules', list)):
        if k in d and not isinstance(d[k], typ):
            raise DesignError(f'{k} 必须是列表')
    for k in ('sensors', 'lights', 'rules'):
        for i, e in enumerate(d.get(k, [])):
            if not isinstance(e, dict):
                raise DesignError(f'{k}[{i}] 必须是一个 JSON 对象 {{...}}')
    try:
        return _parse_design(lv, d)
    except DesignError:
        raise
    except (TypeError, ValueError, KeyError, IndexError, AttributeError, OverflowError) as e:
        raise DesignError(f'设计文件结构错误：{type(e).__name__}: {str(e)[:200]}')


def _parse_design(lv, d):
    errs = []
    anchors = [tuple(map(float, a)) for a in lv['anchors']]
    nodes = [(x, y, True) for x, y in anchors]
    raw_nodes = d.get('nodes', [])
    if not isinstance(raw_nodes, list):
        raise DesignError('nodes 必须是列表')
    if len(raw_nodes) > LIMITS['nodes']:
        errs.append(f'节点太多（{len(raw_nodes)} > {LIMITS["nodes"]}）')
    ba = lv['build_area']
    polys = lv.get('solid', [])
    for k, nd in enumerate(raw_nodes):
        try:
            if not isinstance(nd, list) or len(nd) != 2:
                raise DesignError(f'nodes[{k}] 格式应为 [x, y]')
            x, y = NUM(nd[0], f'nodes[{k}][0]'), NUM(nd[1], f'nodes[{k}][1]')
        except DesignError as e:
            errs.append(str(e)); nodes.append((0.0, 0.0, False)); continue
        if not (ba['x'][0] <= x <= ba['x'][1] and ba['y'][0] <= y <= ba['y'][1]):
            errs.append(f'nodes[{k}] ({x},{y}) 超出可建造范围 x∈{ba["x"]} y∈{ba["y"]}')
        for poly in polys:
            if point_in_poly(x, y, poly):
                errs.append(f'nodes[{k}] ({x},{y}) 在地面/岩石内部')
        nodes.append((x, y, False))
    # 节点间距
    pts = sorted(range(len(nodes)), key=lambda i: nodes[i][0])
    for a in range(len(pts)):
        for b in range(a + 1, len(pts)):
            i, j = pts[a], pts[b]
            if nodes[j][0] - nodes[i][0] > LIMITS['min_gap']:
                break
            if math.hypot(nodes[i][0] - nodes[j][0], nodes[i][1] - nodes[j][1]) < LIMITS['min_gap']:
                errs.append(f'有两个节点距离小于 {LIMITS["min_gap"]} 米（{name_of(i, len(anchors))} 与 {name_of(j, len(anchors))}）')
    na = len(anchors)

    def ref(r, where):
        if isinstance(r, str) and r.upper().startswith('A') and r[1:].isdigit():
            i = int(r[1:])
            if i >= na:
                raise DesignError(f'{where}: 锚点 {r} 不存在（本关锚点 A0～A{na - 1}）')
            return i
        if isinstance(r, int) and not isinstance(r, bool) and 0 <= r < len(raw_nodes):
            return na + r
        raise DesignError(f'{where}: 节点引用 {r!r} 无效（新节点写整数下标 0～{len(raw_nodes) - 1}，锚点写 "A0" 这样的字符串）')

    members = []
    raw_m = d.get('members', [])
    if len(raw_m) > LIMITS['members']:
        errs.append(f'杆件太多（{len(raw_m)} > {LIMITS["members"]}）')
    seen = set()
    used = [False] * len(nodes)
    cost_detail = {}
    total = 0.0
    for k, m in enumerate(raw_m):
        where = f'members[{k}]'
        try:
            if not isinstance(m, list) or len(m) < 3:
                raise DesignError(f'{where} 格式应为 [节点, 节点, "材料"] 或液压杆 [节点, 节点, "hydraulic", "组名"]')
            i, j = ref(m[0], where), ref(m[1], where)
            mat = m[2]
            if not isinstance(mat, str) or mat not in MATERIALS:
                raise DesignError(f'{where}: 材料 {mat!r} 不存在，可选 {list(MATERIALS)}')
            if mat in lv.get('banned', []):
                raise DesignError(f'{where}: 本关不能用 {MATERIALS[mat]["name"]}（{mat}）')
            group = None
            if mat == 'hydraulic':
                if len(m) < 4 or not isinstance(m[3], str) or not m[3].strip():
                    raise DesignError(f'{where}: 液压杆要写组名，例如 [0, 1, "hydraulic", "A"]')
                group = m[3]
            if i == j:
                raise DesignError(f'{where}: 两端是同一个节点')
            key = (min(i, j), max(i, j))
            if key in seen:
                raise DesignError(f'{where}: 这两个节点之间已经有杆件了')
            seen.add(key)
            L = math.hypot(nodes[i][0] - nodes[j][0], nodes[i][1] - nodes[j][1])
            M = MATERIALS[mat]
            if L > M['max_len'] + 1e-9:
                raise DesignError(f'{where}: {M["name"]}长 {L:.3f} 米，超过上限 {M["max_len"]} 米')
            if L < LIMITS['min_len']:
                raise DesignError(f'{where}: 长 {L:.3f} 米，短于 {LIMITS["min_len"]} 米')
            if nodes[i][2] and nodes[j][2]:
                raise DesignError(f'{where}: 两端都是锚点，没有意义')
            c = M['cost_m'] * L + M['cost_each']
            total += c
            cost_detail[M['name']] = cost_detail.get(M['name'], 0) + c
            used[i] = used[j] = True
            members.append((i, j, mat, group, L))
        except DesignError as e:
            errs.append(str(e))
    for k in range(na, len(nodes)):
        if not used[k]:
            errs.append(f'nodes[{k - na}] 没有连任何杆件')
    nnew = len(nodes) - na
    total += JOINT_COST * nnew
    cost_detail['节点'] = JOINT_COST * nnew

    # 机关
    sensors, lights, rules = [], [], []
    sid = {}
    groups = sorted({m[3] for m in members if m[3]})
    gid = {g: i for i, g in enumerate(groups)}
    raw_s = d.get('sensors', [])
    if len(raw_s) > LIMITS['sensors']:
        errs.append(f'传感器太多（> {LIMITS["sensors"]}）')
    for k, s in enumerate(raw_s):
        where = f'sensors[{k}]'
        try:
            t = s.get('type')
            name = s.get('id')
            if not isinstance(name, str) or name in sid:
                raise DesignError(f'{where}: id 必须是不重复的字符串')
            mm = NUM(s.get('min_mass', 0), f'{where}.min_mass', 0)
            if t == 'plate':
                mi = s.get('member')
                if isinstance(mi, bool) or not isinstance(mi, int) or not (0 <= mi < len(members)) or members[mi][2] != 'road':
                    raise DesignError(f'{where}: 压力板的 member 必须是一根路面杆件的下标')
                sensors.append(dict(type=0, ci=mi, x=0, dir=0, minmass=mm))
            elif t == 'gate':
                x = NUM(s['x'], f'{where}.x')
                dr = {'right': 1, 'left': -1, 'both': 0}.get(s.get('dir', 'both'))
                if dr is None:
                    raise DesignError(f'{where}: dir 只能是 right / left / both')
                sensors.append(dict(type=1, ci=-1, x=x, dir=dr, minmass=mm))
            elif t == 'start':
                sensors.append(dict(type=2, ci=-1, x=0, dir=0, minmass=0))
            elif t == 'horn':
                if not lv.get('ship'):
                    raise DesignError(f'{where}: 本关没有船，不能用 horn')
                sensors.append(dict(type=3, ci=-1, x=0, dir=0, minmass=0))
            else:
                raise DesignError(f'{where}: type 只能是 plate / gate / start / horn')
            sid[name] = len(sensors) - 1
            c = SENSOR_COST[t]
            total += c
            cost_detail['传感器'] = cost_detail.get('传感器', 0) + c
        except (DesignError, KeyError, TypeError, ValueError) as e:
            errs.append(str(e) if isinstance(e, DesignError) else f'{where}: 格式错误 {e}')
    lid = {}
    raw_l = d.get('lights', [])
    if raw_l and not lv.get('lights_allowed', True):
        errs.append('本关不能设红绿灯')
    if len(raw_l) > LIMITS['lights']:
        errs.append(f'红绿灯太多（> {LIMITS["lights"]}）')
    for k, l in enumerate(raw_l):
        where = f'lights[{k}]'
        try:
            name = l['id']
            if name in lid:
                raise DesignError(f'{where}: id 重复')
            dr = {'right': 1, 'left': -1, 'both': 0}[l.get('dir', 'both')]
            st = {'green': 0, 'red': 1}[l.get('init', 'green')]
            lights.append(dict(x=NUM(l['x'], f'{where}.x'), dir=dr, state=st))
            lid[name] = len(lights) - 1
            total += LIGHT_COST
            cost_detail['红绿灯'] = cost_detail.get('红绿灯', 0) + LIGHT_COST
        except (DesignError, KeyError, TypeError, ValueError) as e:
            errs.append(str(e) if isinstance(e, DesignError) else f'{where}: 格式错误 {e}')
    raw_r = d.get('rules', [])
    if len(raw_r) > LIMITS['rules']:
        errs.append(f'规则太多（> {LIMITS["rules"]}）')
    H = MATERIALS['hydraulic']
    for k, r in enumerate(raw_r):
        where = f'rules[{k}]'
        try:
            if r.get('on') not in sid:
                raise DesignError(f'{where}: on 必须是某个传感器的 id')
            delay = NUM(r.get('delay', 0), f'{where}.delay')
            if not (0 <= delay <= 120):
                raise DesignError(f'{where}: delay 必须在 0～120 秒')
            act = r.get('do')
            if act == 'hyd':
                g = r.get('group')
                if g not in gid:
                    raise DesignError(f'{where}: 液压组 {g!r} 不存在（现有组 {groups}）')
                ratio = NUM(r['ratio'], f'{where}.ratio')
                if not (H['ratio'][0] <= ratio <= H['ratio'][1]):
                    raise DesignError(f'{where}: ratio 必须在 {H["ratio"][0]}～{H["ratio"][1]}')
                sp = NUM(r.get('speed', H['speed']), f'{where}.speed')
                if not (0.05 <= sp <= H['speed']):
                    raise DesignError(f'{where}: speed 必须在 0.05～{H["speed"]} 米/秒')
                acc = NUM(r['accel'], f'{where}.accel') if 'accel' in r else 0.0   # 不写 = 8×speed；写了就必须在范围内（写 0 不合法）
                if 'accel' in r and not (0.5 <= acc <= 8 * H['speed']):
                    raise DesignError(f'{where}: accel 必须在 0.5～{8 * H["speed"]:g} 米/秒²（不写 = 8 × speed）')
                rules.append(dict(sensor=sid[r['on']], delay=delay, act=0, arg=gid[g], v1=ratio, v2=sp, v3=acc))
            elif act == 'light':
                if r.get('light') not in lid:
                    raise DesignError(f'{where}: 红绿灯 {r.get("light")!r} 不存在')
                st = {'green': 0, 'red': 1}[r['state']]
                rules.append(dict(sensor=sid[r['on']], delay=delay, act=1, arg=lid[r['light']], v1=st, v2=0))
            else:
                raise DesignError(f'{where}: do 只能是 hyd 或 light')
        except (DesignError, KeyError, TypeError, ValueError) as e:
            errs.append(str(e) if isinstance(e, DesignError) else f'{where}: 格式错误 {e}')
    if lv.get('static_only') and (groups or sensors or lights or rules):
        errs.append('本关只能造静态桥：不能用液压杆、传感器、红绿灯、规则')
    total = round(total, 2)
    if not math.isfinite(total):
        errs.append('造价不是有限数')
    budget = lv['budget']
    if total > budget + 1e-6:
        errs.append(f'造价 {total:.0f} 超过预算 {budget}')
    if errs:
        raise DesignError('\n'.join(errs))
    return dict(raw=d, nodes=nodes, members=members, sensors=sensors, lights=lights, rules=rules,
                cost=total, cost_detail=cost_detail, groups=groups, n_anchor=na)


def name_of(i, na):
    return f'A{i}' if i < na else f'nodes[{i - na}]'


# ------------------------------------------------------------------ 车辆 -> 质点
def standard_vehicle(vt, dir_, spawn_x, ground_y, mass=None):
    V = VEHICLES[vt]
    m = mass or V['mass']
    r, wb, h = V['r'], V['wb'], V['h']
    rear_x = spawn_x - dir_ * wb  # 车头在 spawn_x
    wy = ground_y + r
    parts = [  # 后轮、前轮、后车身、前车身
        (rear_x, wy, 0.35 * m, r, 1), (spawn_x, wy, 0.35 * m, r, 1),
        (rear_x + dir_ * 0.1, wy + h, 0.15 * m, V['br'], 2), (spawn_x - dir_ * 0.1, wy + h, 0.15 * m, V['br'], 2)]
    cons = [(0, 1), (2, 3), (0, 2), (1, 3), (0, 3), (1, 2)]
    return dict(type=vt, parts=parts, cons=[(a, b, 6, None) for a, b in cons], mass=m, dir=dir_,
                speed=V['speed'], accel=V['accel'], brake=V['brake'], sab=0)


# ------------------------------------------------------------------ 生成内核输入
def build_sim(lv, P, scenario, sab=None):
    s = SIM
    lines = []
    W = lines.append
    tmax = scenario.get('tmax', lv['tmax'])
    W('BRIDGESIM 1')
    W(f'params {s["dt"]!r} {s["substeps"]} {tmax} {s["gravity"]} {s["damping"]} {s["vcap"]} {s["landing"]} {s["rec_every"]} {s["settle"]}')
    W(f'world {lv.get("water_y", -1e9)} {lv["kill_y"]} {lv["finish"]["left"]} {lv["finish"]["right"]}')
    segs = []
    for pl in lv['terrain']:
        for a, b in zip(pl, pl[1:]):
            segs.append((a[0], a[1], b[0], b[1]))
    W(f'T {len(segs)}')
    for sg in segs:
        W('%r %r %r %r' % sg)
    # 质点
    parts = []
    mass = [JOINT_KG] * len(P['nodes'])
    for (i, j, mat, g, L) in P['members']:
        mm = MATERIALS[mat]['kg_m'] * L
        mass[i] += mm / 2; mass[j] += mm / 2
    for k, (x, y, fixed) in enumerate(P['nodes']):
        parts.append((x, y, 0.0 if fixed else mass[k], 0.0, 0, -1))
    cons = []
    gid = {g: i for i, g in enumerate(P['groups'])}
    for (i, j, mat, g, L) in P['members']:
        M = MATERIALS[mat]
        kind = ['road', 'wood', 'steel', 'cable', 'spring', 'hydraulic'].index(mat)
        k = M['k'] if mat == 'spring' else M['EA'] / L
        cons.append((i, j, kind, L, k, M['tension'], M['comp'] if mat != 'cable' else 0, M['buckle'], -1,
                     gid.get(g, -1), M.get('speed', 0)))
    vehs = []
    vlist = []
    for v in scenario['vehicles']:
        dir_ = 1 if v['from'] == 'left' else -1
        sx = lv['spawn']['left'] if dir_ > 0 else lv['spawn']['right']
        vlist.append(dict(standard_vehicle(v['type'], dir_, sx, lv.get('ground_y', 0.0), v.get('mass')), depart=float(v['t'])))
    if sab is not None:
        vlist.insert(0, sab)
    for vi, v in enumerate(vlist):
        p0 = len(parts); c0 = len(cons)
        for (x, y, m, r, kind) in v['parts']:
            parts.append((x, y, m, r, kind, vi))
        for c in v['cons']:
            a, b, kind = c[0], c[1], c[2]
            xa, ya = v['parts'][a][:2]; xb, yb = v['parts'][b][:2]
            L = math.hypot(xa - xb, ya - yb)
            if kind == 6:
                cons.append((p0 + a, p0 + b, 6, L, 0, 1e12, 1e12, 1e18, vi, -1, 0))
            else:
                mat = c[3]; M = MATERIALS[mat]
                k = M['k'] if mat == 'spring' else M['EA'] / L
                cons.append((p0 + a, p0 + b, kind, L, k, M['tension'], M['comp'] if mat != 'cable' else 0, M['buckle'], vi,
                             c[4] if len(c) > 4 else -1, M.get('speed', 0)))
        vehs.append((v['depart'], v['dir'], v['speed'], v['accel'], v['brake'], v['mass'], p0, len(v['parts']), c0, len(cons) - c0, v['sab']))
    W(f'P {len(parts)}')
    for p in parts:
        W('%r %r %r %r %d %d' % p)
    W(f'C {len(cons)}')
    for c in cons:
        W('%d %d %d %r %r %r %r %r %d %d %r' % c)
    W(f'V {len(vehs)}')
    for v in vehs:
        W('%r %d %r %r %r %r %d %d %d %d %d' % v)
    W(f'L {len(P["lights"])}')
    for l in P['lights']:
        W(f'{l["x"]!r} {l["dir"]} {l["state"]}')
    sens = list(P['sensors']) + (sab.get('sensors', []) if sab else [])
    rules = list(P['rules'])
    if sab:
        off = len(P['sensors'])
        for r in sab.get('rules', []):
            rules.append(dict(r, sensor=r['sensor'] + off))
    W(f'S {len(sens)}')
    for se in sens:
        W(f'{se["type"]} {se["ci"]} {se["x"]!r} {se["dir"]} {se["minmass"]!r} {se.get("veh", -1)}')
    W(f'R {len(rules)}')
    for r in rules:
        W(f'{r["sensor"]} {r["delay"]!r} {r["act"]} {r["arg"]} {r["v1"]!r} {r["v2"]!r} {float(r.get("v3", 0))!r}')
    ship = lv.get('ship')
    if ship and scenario.get('ship'):
        W(f'SHIP 1 {float(scenario["ship"]["t"])!r} {ship["dur"]} {ship["x"][0]} {ship["x"][1]} {ship["y"][0]} {ship["y"][1]} {ship["lead"]}')
    else:
        W('SHIP 0 0 0 0 0 0 0 0')
    return '\n'.join(lines) + '\n', parts, cons, vlist


def run_sim(text, frames=False, n_vehicles=None, n_cons=None):
    with tempfile.TemporaryDirectory() as td:
        ip = os.path.join(td, 'in.txt')
        with open(ip, 'w') as f:
            f.write(text)
        args = [SIM_EXE, ip]
        fp = os.path.join(td, 'frames.txt')
        if frames:
            args.append(fp)
        for _try in range(10):   # 主办方替换 exe、或杀毒软件扫描新复制的 exe 时可能暂时启动不了：重试
            try:
                r = subprocess.run(args, capture_output=True, text=True, timeout=SIM_TIMEOUT)
                break
            except subprocess.TimeoutExpired:
                raise EngineError(f'bridge_sim 超过 {SIM_TIMEOUT} 秒没有结束')
            except OSError:
                import time; time.sleep(1.5)
        else:
            raise EngineError('无法启动 bridge_sim.exe')
        if r.returncode != 0:
            raise EngineError('bridge_sim 出错：' + (r.stderr or '')[:2000])
        out = r.stdout
        fr = None
        if frames:
            try:
                with open(fp) as f:
                    fr = f.read()
            except OSError as e:
                raise EngineError(f'bridge_sim 没有写出回放帧文件：{e}')
    return parse_out(out, n_vehicles, n_cons), fr


# 引擎事件种类（bridge_sim.cpp 里 ev(...) 的全部写法）及每种至少要有的整数参数个数
_EV_KINDS = {'ANOMALY': 1, 'VFAIL': 2, 'SENSOR': 2, 'ACT': 0, 'HORN': 0, 'VSTART': 1, 'BREAK': 1, 'VARRIVE': 1, 'SHIPHIT': 1}
_EV_ANOMALY_MSG = {1: 'speed', 2: 'tunnel', 3: 'nan', 4: 'overstretch'}   # bridge_sim.cpp setAnomaly 的代码与说明


def parse_out(out, n_vehicles=None, n_cons=None):
    """严格解析引擎输出。任何缺记录、多记录、格式错、越界、非有限数、前后矛盾都抛 EngineError（评测故障，不是选手成绩）。
    n_vehicles / n_cons：本场应有的车辆数 / 约束（杆件+车身）数，给了就核对。"""
    try:
        return _parse_out(out, n_vehicles, n_cons)
    except EngineError:
        raise
    except (ValueError, IndexError, TypeError, KeyError, OverflowError, AttributeError) as e:
        raise EngineError(f'bridge_sim 输出无法解析：{type(e).__name__}: {e}')


def _fin(s, what, lo=None):
    v = float(s)
    if not math.isfinite(v) or (lo is not None and v < lo):
        raise EngineError(f'bridge_sim 输出 {what} = {s} 不是有效数值')
    return v


def _flag(s, what):
    v = int(s)
    if v not in (0, 1):
        raise EngineError(f'bridge_sim 输出 {what} = {s} 应为 0/1')
    return v


def _parse_out(out, n_vehicles, n_cons):
    bad = lambda msg: EngineError('bridge_sim 输出损坏：' + msg)
    res = dict(vehicles=[], events=[])
    seen = set()
    for line in (out or '').splitlines():
        t = line.split()
        if not t:
            continue
        k = t[0]
        if k in ('RESULT', 'BROKEN', 'PEAK'):
            if k in seen:
                raise bad(f'{k} 记录出现了不止一次')
            seen.add(k)
        if k == 'RESULT':
            if len(t) != 5:
                raise bad(f'RESULT 应有 4 个字段：{line!r}')
            res.update(ok=bool(_flag(t[1], 'RESULT.ok')), anomaly=int(t[2]), shiphit=bool(_flag(t[3], 'RESULT.shiphit')),
                       t_end=_fin(t[4], 'RESULT.t', 0))
            if res['anomaly'] not in ANOMALY:
                raise bad(f'未知异常代码 {t[2]}')
        elif k == 'VEH':
            if len(t) != 5:
                raise bad(f'VEH 应有 4 个字段：{line!r}')
            idx, status, reason = int(t[1]), int(t[2]), int(t[3])
            if idx != len(res['vehicles']):
                raise bad(f'车辆编号 {idx} 不连续/重复（应为 {len(res["vehicles"])}）')
            if status not in (0, 1, 2, 3) or reason not in FAIL_REASONS:
                raise bad(f'车辆 {idx} 状态 {status} / 原因 {reason} 不在约定范围')
            if (status == 3) != (reason != 0):
                raise bad(f'车辆 {idx} 状态 {status} 与失败原因 {reason} 矛盾')
            res['vehicles'].append(dict(status=status, reason=reason, t=_fin(t[4], f'VEH{idx}.t', 0)))
        elif k == 'BROKEN':
            if len(t) != 2:
                raise bad(f'BROKEN 应有 1 个字段：{line!r}')
            res['broken'] = int(t[1])
        elif k == 'PEAK':
            res['peak'] = [_fin(x, 'PEAK', 0) for x in t[1:]]
        elif k == 'EV':
            if len(t) < 3 or t[2] not in _EV_KINDS:
                raise bad(f'事件格式错误：{line!r}')
            et = _fin(t[1], 'EV 时刻', 0)
            if res.get('_ev_t') is not None and et < res['_ev_t'] - 1e-9:
                raise bad(f'事件时刻倒退：{line!r}')
            res['_ev_t'] = et
            kind, args = t[2], t[3:]
            if kind == 'ACT':   # ACT hyd <规则> <值> / ACT light <灯> <状态>
                if len(args) != 3 or args[0] not in ('hyd', 'light'):
                    raise bad(f'ACT 事件格式错误：{line!r}')
                int(args[1]); _fin(args[2], 'ACT 值')
            elif kind == 'ANOMALY':   # ANOMALY <代码> <说明>
                if len(args) != 2 or _EV_ANOMALY_MSG.get(int(args[0])) != args[1]:
                    raise bad(f'ANOMALY 事件代码/说明不在约定范围：{line!r}')
            elif kind == 'BREAK':     # BREAK <杆件> <力>
                if len(args) != 2:
                    raise bad(f'BREAK 事件格式错误：{line!r}')
                int(args[0]); _fin(args[1], 'BREAK 力')
            else:
                if len(args) != _EV_KINDS[kind]:
                    raise bad(f'{kind} 事件参数个数应为 {_EV_KINDS[kind]}：{line!r}')
                [int(x) for x in args]
            res['events'].append(line[3:])
        else:
            raise bad(f'未知记录 {line[:80]!r}')
    if seen != {'RESULT', 'BROKEN', 'PEAK'}:
        raise bad(f'缺记录 {sorted({"RESULT", "BROKEN", "PEAK"} - seen)}')
    if n_vehicles is not None and len(res['vehicles']) != n_vehicles:
        raise bad(f'车辆数 {len(res["vehicles"])} 与本场 {n_vehicles} 辆不符')
    if n_cons is not None and len(res['peak']) != n_cons:
        raise bad(f'PEAK 个数 {len(res["peak"])} 与约束数 {n_cons} 不符')
    ncon = len(res['peak'])
    nveh = len(res['vehicles'])
    breaks = []
    for e in res['events']:
        f = e.split()
        if f[1] == 'BREAK':
            breaks.append(int(f[2]))
        elif f[1] in ('VSTART', 'VARRIVE', 'VFAIL') and not (0 <= int(f[2]) < nveh):
            raise bad(f'事件里的车辆编号越界：{e!r}')
    if len(set(breaks)) != len(breaks) or any(not (0 <= b < ncon) or res['peak'][b] != 9.0 for b in breaks):
        raise bad('断杆事件与 PEAK 不一致（重复、越界或该杆未标为已断）')
    if res['broken'] != len(breaks):
        raise bad(f'BROKEN {res["broken"]} 与断杆事件数 {len(breaks)} 不符')
    want_ok = (res['anomaly'] == 0 and not res['shiphit'] and all(v['status'] == 2 for v in res['vehicles']))
    if res['ok'] != want_ok:
        raise bad(f'RESULT 过关标志 {int(res["ok"])} 与逐车状态/异常/撞船不一致')
    res.pop('_ev_t', None)
    # 事件与 RESULT / 逐车状态双向核对（引擎：setAnomaly、撞船各最多一次；VSTART/VARRIVE/VFAIL 每车最多一次且与最终状态一致）
    evs = [e.split() for e in res['events']]
    an = [f for f in evs if f[1] == 'ANOMALY']
    if len(an) > 1 or bool(an) != bool(res['anomaly']) or (an and int(an[0][2]) != res['anomaly']):
        raise bad(f'ANOMALY 事件（{len(an)} 条）与 RESULT 异常代码 {res["anomaly"]} 不一致')
    sh = [f for f in evs if f[1] == 'SHIPHIT']
    if len(sh) > 1 or bool(sh) != res['shiphit'] or (sh and not 0 <= int(sh[0][2]) < ncon):
        raise bad(f'SHIPHIT 事件（{len(sh)} 条）与 RESULT 撞船标志 {int(res["shiphit"])} 不一致或杆件编号越界')
    per = {}
    for f in evs:
        if f[1] in ('VSTART', 'VARRIVE', 'VFAIL'):
            vi = int(f[2])
            if f[1] in per.setdefault(vi, {}):
                raise bad(f'车辆 {vi} 的 {f[1]} 事件重复')
            per[vi][f[1]] = f
    for vi, v in enumerate(res['vehicles']):
        e = per.get(vi, {})
        st, ar, fl = 'VSTART' in e, 'VARRIVE' in e, 'VFAIL' in e
        want = {0: (False, False, False), 1: (True, False, False), 2: (True, True, False), 3: (True, False, True)}[v['status']]
        if (st, ar, fl) != want:
            raise bad(f'车辆 {vi} 状态 {v["status"]} 与事件不一致（出发 {st}、到达 {ar}、失败 {fl}）')
        if fl:
            r = int(e['VFAIL'][3])
            if r not in FAIL_REASONS or r == 0 or r != v['reason']:
                raise bad(f'车辆 {vi} 的 VFAIL 原因 {r} 越界或与 VEH 原因 {v["reason"]} 不符')
        done = e.get('VARRIVE') or e.get('VFAIL')
        if done and (abs(float(done[0]) - v['t']) > 1e-3 or float(done[0]) < float(e['VSTART'][0])):
            raise bad(f'车辆 {vi} 的结束事件时刻 {done[0]} 与 VEH 时刻 {v["t"]} 不符或早于出发')
    return res


def parse_frames(fr, nparts, ncons, nveh=None, nlights=None):
    """解析回放帧；结构或长度不对抛 EngineError。"""
    try:
        frames = []
        lines = (fr or '').splitlines()
        i = 0
        while i < len(lines):
            if lines[i].startswith('F '):
                t = _fin(lines[i][2:], '帧时刻', 0)
                pos = [round(_fin(x, '帧坐标'), 3) for x in lines[i + 1].split()]
                st = [int(x) for x in lines[i + 2].split()]
                vs, ls = lines[i + 3].split('|')
                v, l = [int(x) for x in vs.split()], [int(x) for x in ls.split()]
                if len(pos) != 2 * nparts or len(st) != ncons or (nveh is not None and len(v) != nveh) or (nlights is not None and len(l) != nlights):
                    raise EngineError(f'回放第 {len(frames)} 帧数据长度不符（坐标 {len(pos)}/{2 * nparts}，杆件 {len(st)}/{ncons}）')
                frames.append(dict(t=t, p=pos, s=st, v=v, l=l))
                i += 4
            elif lines[i].strip():
                raise EngineError(f'回放帧文件第 {i + 1} 行无法识别')
            else:
                i += 1
        if not frames:
            raise EngineError('回放帧文件是空的')
        return frames
    except EngineError:
        raise
    except (ValueError, IndexError, TypeError, OverflowError) as e:
        raise EngineError(f'回放帧文件损坏：{type(e).__name__}: {e}')


# ------------------------------------------------------------------ 跑一个场景
_LOG = {}
import threading as _threading
_LOG_LOCK = _threading.Lock()   # 同一进程内多线程（线程池调 simulate）写日志/存设计时串行，防止行交错和临时文件撞名


def _log_file(root):
    """每个进程写自己的日志文件（archive/log/<启动时刻>_<进程号>.jsonl），多进程同时试车也不会互相截断。
    调用方须持有 _LOG_LOCK。键里带进程号：fork 出来的子进程不会沿用父进程的文件名。"""
    key = (root, os.getpid())
    if key not in _LOG:
        import time
        d = os.path.join(root, 'log')
        os.makedirs(d, exist_ok=True)
        _LOG[key] = os.path.join(d, time.strftime('%Y%m%d-%H%M%S') + f'_{os.getpid()}.jsonl')
    return _LOG[key]


def _atomic_write(fp, text):
    if os.path.exists(fp):
        return
    import uuid
    tmp = f'{fp}.{os.getpid()}_{_threading.get_ident()}_{uuid.uuid4().hex[:8]}.tmp'
    with open(tmp, 'w', encoding='utf-8') as f:
        f.write(text)
    try:
        os.replace(tmp, fp)
    except OSError:
        if os.path.exists(tmp):
            os.remove(tmp)


_ENGINE_SHA = []


def engine_sha256():
    """当前 bridge_sim.exe 的 sha256（存档里标明是哪个版本的引擎算的）。"""
    if not _ENGINE_SHA:
        import hashlib
        try:
            with open(SIM_EXE, 'rb') as f:
                _ENGINE_SHA.append(hashlib.sha256(f.read()).hexdigest())
        except OSError:
            _ENGINE_SHA.append(None)
    return _ENGINE_SHA[0]


def _warn_archive(e):
    """自动存档写入失败：明确告警（每个进程只提示一次），不影响模拟结果。"""
    if not _WARNED:
        _WARNED.append(1)
        print(f'（警告：自动存档写入失败：{e}；模拟结果不受影响，但这次的存档不完整）', file=sys.stderr)


def _hash(obj):
    import hashlib
    return hashlib.sha1(json.dumps(obj, sort_keys=True, ensure_ascii=False).encode('utf-8')).hexdigest()[:12]


_OFFICIAL = {}


def official_hashes(lv):
    """该关官方场景（关卡文件 samples）的内容哈希集合。"""
    k = lv.get('_dir', '') + str(lv['id'])
    if k not in _OFFICIAL:
        _OFFICIAL[k] = {_hash(load_json(p)): os.path.basename(p) for p in sample_paths(lv)}
    return _OFFICIAL[k]


_WARNED = []


def _auto_log(lv, P, scenario, res, sab):
    """每次模拟（包括选手自己脚本里 import bridge 调 simulate）都记一行日志，不同设计各存一份。
    日志：archive/log/<进程>.jsonl；设计：archive/designs/L<关>_<哈希>.json；捣蛋车：archive/designs/sab_<哈希>.json。
    主办方脚本设环境变量 BRIDGE_NO_ARCHIVE=1 关闭。"""
    if os.environ.get('BRIDGE_NO_ARCHIVE') == '1' or not P.get('raw'):
        return
    with _LOG_LOCK:
        _auto_log_locked(lv, P, scenario, res, sab)


def _auto_log_locked(lv, P, scenario, res, sab):
    try:
        import time
        root = os.path.join(os.getcwd(), 'archive')
        dd = os.path.join(root, 'designs')
        os.makedirs(dd, exist_ok=True)
        h = _hash(P['raw'])
        _atomic_write(os.path.join(dd, f'L{lv["id"]}_{h}.json'), json.dumps(P['raw'], sort_keys=True, ensure_ascii=False))
        sh = None
        if sab is not None and sab.get('raw') is not None:
            sh = _hash(sab['raw'])
            _atomic_write(os.path.join(dd, f'sab_{sh}.json'), json.dumps(sab['raw'], sort_keys=True, ensure_ascii=False))
        sc_h = _hash(scenario)
        off = official_hashes(lv).get(sc_h)
        rec = dict(t=time.strftime('%Y-%m-%d %H:%M:%S'), ts=round(time.time(), 3), pid=os.getpid(), level=lv['id'], design=h,
                   cost=P['cost'], scenario=scenario.get('name', ''), scenario_hash=sc_h, official=off, ok=bool(res.get('ok')),
                   sab=sh, anomaly=res.get('anomaly', 0))
        with open(_log_file(root), 'a', encoding='utf-8') as f:
            f.write(json.dumps(rec, ensure_ascii=False) + '\n')
    except OSError as e:
        _warn_archive(e)


def simulate(lv, P, scenario, replay=False, sab=None):
    text, parts, cons, vlist = build_sim(lv, P, scenario, sab)
    res, fr = run_sim(text, frames=replay, n_vehicles=len(vlist), n_cons=len(cons))
    _auto_log(lv, P, scenario, res, sab)
    nbm = len(P['members'])
    res['bridge_broken'] = sum(1 for e in res['events'] if e.split()[1] == 'BREAK' and int(e.split()[2]) < nbm)
    res['ok_vehicles'] = res.get('ok', False)
    # 11-07 用户定：过关只看车辆是否按时完好通过，车过后桥塌也算过（bridge_broken 只做统计）
    nb = len(P['members'])
    res['peak_members'] = res.get('peak', [])[:nb]
    off = 1 if sab is not None else 0
    res['sab'] = None
    if sab is not None:
        res['sab'] = res['vehicles'][0]
    res['std_vehicles'] = res['vehicles'][off:]
    rep = None
    if replay:
        rep = dict(level=level_view(lv), scenario=scenario,
                   particles=[dict(k=p[4], o=p[5], r=p[3]) for p in parts],
                   members=[[c[0], c[1], c[2], c[8]] for c in cons],
                   n_anchor=P['n_anchor'],
                   vehicles=[dict(type=v.get('type', 'saboteur'), dir=v['dir'], depart=v['depart'], mass=v['mass'], sab=v['sab'],
                                  p0=None) for v in vlist],
                   lights=P['lights'], ship=(dict(lv['ship'], t=scenario['ship']['t']) if lv.get('ship') and scenario.get('ship') else None),
                   frames=parse_frames(fr, len(parts), len(cons), len(vlist), len(P['lights'])),
                   events=res['events'], result=summarize(res, P, scenario), cost=P['cost'],
                   design=P.get('raw'), groups={str(i): g for i, g in enumerate(P['groups'])},
                   sensor_ids=[s.get('id') for s in (P.get('raw') or {}).get('sensors', [])],
                   light_ids=[l.get('id') for l in (P.get('raw') or {}).get('lights', [])])
        if sab is not None:
            rep['sab_design'] = sab.get('raw')
            for g, gi in (sab.get('group_ids') or {}).items():
                rep['groups'][str(gi)] = '捣蛋车' + g
        p0 = 0
        vi = 0
        for k, p in enumerate(parts):
            if p[5] >= 0 and rep['vehicles'][p[5]]['p0'] is None:
                rep['vehicles'][p[5]]['p0'] = k
    return res, rep


def level_view(lv):
    keys = ['id', 'name', 'budget', 'terrain', 'solid', 'water_y', 'kill_y', 'anchors', 'build_area', 'finish', 'spawn', 'ship', 'view', 'static_only', 'sab_deadline']
    return {k: lv[k] for k in keys if k in lv}


def summarize(res, P, scenario):
    lines = []
    vs = res['vehicles']
    names = []
    if res.get('sab') is not None:
        names.append('捣蛋车')
    names += [f'{VEHICLES[v["type"]]["name"]}（{"左→右" if v["from"] == "left" else "右→左"}，{v["t"]}秒出发）' for v in scenario['vehicles']]
    for n, v in zip(names, vs):
        if v['status'] == 2:
            lines.append(f'  {n}：到达（{v["t"]:.1f} 秒）')
        else:
            why = FAIL_REASONS.get(v['reason'], '?') if v['status'] == 3 else '未出发/未完成'
            lines.append(f'  {n}：失败 —— {why}（{v["t"]:.1f} 秒）')
    if res.get('anomaly'):
        lines.append(f'  物理异常：{ANOMALY.get(res["anomaly"])} —— 判失败')
    if res.get('shiphit'):
        lines.append('  船撞上了桥 —— 判失败')
    if res.get('bridge_broken') and res.get('sab') is None:
        lines.append(f'  桥上有 {res["bridge_broken"]} 根杆件断裂（只要车都按时完好到达，仍算通过）')
    lines.append(f'  断裂杆件：{res.get("broken", 0)} 根')
    return dict(ok=res.get('ok', False), text='\n'.join(lines))


def peak_report(res, P, top=5):
    pk = res.get('peak_members', [])
    order = sorted(range(len(pk)), key=lambda i: -pk[i])[:top]
    out = []
    for i in order:
        a, b, mat, g, L = P['members'][i]
        out.append(f'members[{i}] {MATERIALS[mat]["name"]} 长{L:.2f} 峰值受力 {"已断" if pk[i] >= 9 else f"{pk[i] * 100:.0f}%"}')
    return out


# ------------------------------------------------------------------ 回放网页
def write_html(rep, path):
    with open(os.path.join(HERE, 'viewer.html'), encoding='utf-8') as f:
        tpl = f.read()
    data = json.dumps(rep, ensure_ascii=False, separators=(',', ':'))
    data = data.replace('<', '\\u003c').replace('\u2028', '\\u2028').replace('\u2029', '\\u2029')
    html = tpl.replace('/*REPLAY_DATA*/null', data)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(html)


# ------------------------------------------------------------------ 自动存档（每次 run / duel 都存一份，供复盘和"进化过程"）
def archive_dir(tag):
    import datetime
    ts = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')[:-3]
    d = os.path.join(os.getcwd(), 'archive', f'{ts}_{tag}')
    os.makedirs(d, exist_ok=True)
    return d


def save_archive(d, raw, log, reps, name='design.json'):
    import gzip
    try:
        with open(os.path.join(d, name), 'w', encoding='utf-8') as f:
            json.dump(raw, f, ensure_ascii=False, indent=1)
        with open(os.path.join(d, 'result.txt'), 'w', encoding='utf-8') as f:
            f.write('\n'.join(log) + '\n')
        for nm, rep in reps:
            with gzip.open(os.path.join(d, f'replay_{nm}.json.gz'), 'wt', encoding='utf-8') as f:
                json.dump(rep, f, ensure_ascii=False, separators=(',', ':'))
    except OSError as e:
        print(f'（警告：存档失败：{e}；这次运行的存档不完整）', file=sys.stderr)


# ------------------------------------------------------------------ 命令行
def main(argv):
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass
    if len(argv) < 2 or argv[1] in ('-h', '--help'):
        print(__doc__); return 0
    cmd = argv[1]
    args = [a for a in argv[2:]]
    opts = {}
    pos = []
    i = 0
    while i < len(args):
        if args[i] in ('--replay', '--html'):
            if i + 1 >= len(args):
                print(f'{args[i]} 后面要跟输出文件名\n' + __doc__); return 2
            opts[args[i]] = args[i + 1]; i += 2
        elif args[i] in ('-q', '--no-archive'):
            opts[args[i]] = True; i += 1
        else:
            pos.append(args[i]); i += 1
    need = {'check': 2, 'run': 2, 'duel': 2}
    if cmd in need and len(pos) < need[cmd]:
        print(__doc__); return 2
    if cmd == 'check':
        lv = load_level(pos[0])
        try:
            P = parse_design(lv, load_json(pos[1]))
        except (OSError, ValueError) as e:
            print(f'设计文件读不了：{e}'); return 1
        except DesignError as e:
            print('设计不合法：\n' + str(e)); return 1
        print(f'合法。造价 {P["cost"]:.0f} / 预算 {lv["budget"]}  明细：' + '，'.join(f'{k} {v:.0f}' for k, v in P['cost_detail'].items()))
        return 0
    if cmd == 'run':
        lv = load_level(pos[0])
        try:
            raw = load_json(pos[1])
        except (OSError, ValueError) as e:
            print(f'设计文件读不了：{e}'); return 1
        arch = None if '--no-archive' in opts else archive_dir(f'L{lv["id"]}')
        log = []
        def say(t):
            print(t); log.append(t)
        try:
            P = parse_design(lv, raw)
        except DesignError as e:
            say('设计不合法：\n' + str(e))
            if arch: save_archive(arch, raw, log, [])
            return 1
        scen = pos[2:] or sample_paths(lv)
        say(f'关卡 {lv["id"]}「{lv["name"]}」  造价 {P["cost"]:.0f} / 预算 {lv["budget"]}')
        npass = 0
        reps = []
        for k, sp in enumerate(scen):
            sc = load_json(sp)
            want = bool(arch) or (('--replay' in opts or '--html' in opts) and k == 0)
            res, rep = simulate(lv, P, sc, replay=want)
            sm = summarize(res, P, sc)
            npass += sm['ok']
            say(f'[{"通过" if sm["ok"] else "失败"}] {os.path.basename(sp)}  {sc.get("name", "")}')
            if not opts.get('-q'):
                say(sm['text'])
                for l in peak_report(res, P):
                    say('    ' + l)
            if rep is not None:
                reps.append((f'{k + 1}_' + os.path.splitext(os.path.basename(sp))[0], rep))
                if k == 0 and '--replay' in opts:
                    with open(opts['--replay'], 'w', encoding='utf-8') as f:
                        json.dump(rep, f, ensure_ascii=False, separators=(',', ':'))
                if k == 0 and '--html' in opts:
                    write_html(rep, opts['--html'])
                    say(f'  回放网页：{opts["--html"]}（只录第一个场景）')
        say(f'合计 {npass}/{len(scen)} 通过' + ('（全部通过 = 过关）' if npass == len(scen) and not pos[2:] else ''))
        if arch:
            save_archive(arch, raw, log, reps)
        return 0 if npass == len(scen) else 3
    if cmd == 'duel':
        import saboteur
        return saboteur.cli(pos, opts)
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
