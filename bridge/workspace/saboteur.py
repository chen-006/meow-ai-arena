"""决赛：捣蛋车（同一套材料搭的车）+ 攻防对局。

捣蛋车文件格式（坐标相对于"出发时车头正下方的地面点"：x 向前为正，所以车身 x ≤ 0；y 向上，地面 y = 0）：
{
 "nodes": [[0,0.22],[-1.5,0.22],[-0.75,0.9]],    // 节点；轮子节点的 y 必须 ≥ 0.22（轮半径），其它节点 y ≥ 0.1
 "members": [[0,1,"steel"],[0,2,"wood"],[1,2,"hydraulic","A"]],   // 只能用整数下标；材料不能用 road
 "wheels": [0,1],                                   // 哪些节点是带电机的轮子（2～4 个）
 "motor": {"speed": 8.0, "accel": 3.0},             // 目标车速 ≤ 10 m/s，加速度 ≤ 4 m/s²
 "sensors": [{"id":"s","type":"start"}, {"id":"g","type":"mygate","x":10.0}],  // mygate：自己的车头越过世界坐标 x
 "rules": [{"on":"g","delay":0.0,"do":"hyd","group":"A","ratio":1.5,"speed":3.0}]   // 捣蛋车液压 speed ≤ 3、accel ≤ 5
}
用法：python bridge.py duel <桥设计.json> <捣蛋车.json> [--html 回放.html]
"""
import json, math, os
import bridge as B

SAB = dict(mass=2500, length=3.0, height=1.5, budget=6000, wheels=(2, 4), wheel_r=0.22, wheel_kg=80, body_r=0.1,
           speed=10.0, accel=4.0, brake=6.0, nodes=40, members=120, sensors=10, rules=30, ballast_cost=1.0, hyd_speed=3.0, hyd_accel=5.0)
GROUP_OFFSET = 100


def parse_sab(lv, d):
    """检查捣蛋车文件，返回内部结构；任何问题抛 B.DesignError（结构错误也统一成 DesignError）。"""
    if not isinstance(d, dict):
        raise B.DesignError('捣蛋车文件顶层必须是一个 JSON 对象 {...}')
    for k in ('nodes', 'members', 'wheels', 'sensors', 'rules', 'ballast'):
        if k in d and not isinstance(d[k], list):
            raise B.DesignError(f'{k} 必须是列表')
    if 'motor' in d and not isinstance(d['motor'], dict):
        raise B.DesignError('motor 必须是 JSON 对象')
    try:
        return _parse_sab(lv, d)
    except B.DesignError:
        raise
    except (TypeError, ValueError, KeyError, IndexError, AttributeError, OverflowError) as e:
        raise B.DesignError(f'捣蛋车文件结构错误：{type(e).__name__}: {str(e)[:200]}')


def _idx(v, n, where):
    if isinstance(v, bool) or not isinstance(v, int) or not (0 <= v < n):
        raise B.DesignError(f'{where} = {v!r} 不是有效节点下标（整数 0～{n - 1}）')
    return v


def _parse_sab(lv, d):
    errs = []
    nodes = []
    raw_nodes = d.get('nodes', [])
    if len(raw_nodes) > SAB['nodes']:
        raise B.DesignError(f'节点最多 {SAB["nodes"]} 个（现在 {len(raw_nodes)}）')
    for k, nd in enumerate(raw_nodes):
        if not isinstance(nd, list) or len(nd) != 2:
            raise B.DesignError(f'nodes[{k}] 格式应为 [x, y]')
        nodes.append((B.NUM(nd[0], f'nodes[{k}][0]'), B.NUM(nd[1], f'nodes[{k}][1]')))
    if len(nodes) < 2:
        errs.append('节点至少 2 个')
    for a in range(len(nodes)):
        for b in range(a + 1, len(nodes)):
            if math.dist(nodes[a], nodes[b]) < B.LIMITS['min_gap']:
                errs.append(f'nodes[{a}] 和 nodes[{b}] 距离小于 {B.LIMITS["min_gap"]} 米')
    wheels = [_idx(w, len(nodes), f'wheels[{k}]') for k, w in enumerate(d.get('wheels', []))]
    if not (SAB['wheels'][0] <= len(wheels) <= SAB['wheels'][1]) or len(set(wheels)) != len(wheels):
        errs.append(f'轮子数应在 {SAB["wheels"][0]}～{SAB["wheels"][1]} 个且不重复')
    if errs:
        raise B.DesignError('\n'.join(errs))
    ws = set(wheels)
    for k, (x, y) in enumerate(nodes):
        lo = SAB['wheel_r'] if k in ws else SAB['body_r']
        if y < lo - 1e-9:
            errs.append(f'nodes[{k}] 的 y={y} 太低（{"轮子" if k in ws else "车身节点"}至少 {lo}）')
        if x > 1e-9:
            errs.append(f'nodes[{k}] 的 x={x} > 0（车头在 x=0，车身要在后面）')
    xs = [p[0] for p in nodes]; ys = [p[1] for p in nodes]
    if max(xs) - min(xs) > SAB['length'] + 1e-9:
        errs.append(f'车长 {max(xs) - min(xs):.2f} 超过 {SAB["length"]} 米（按节点中心算）')
    if max(ys) > SAB['height'] + 1e-9:
        errs.append(f'车高 {max(ys):.2f} 超过 {SAB["height"]} 米（按节点中心算）')
    mass = [SAB['wheel_kg'] if k in ws else B.JOINT_KG for k in range(len(nodes))]
    cost = 0.0
    cons = []
    seen = set()
    groups = []
    used = [False] * len(nodes)
    raw_m = d.get('members', [])
    if len(raw_m) > SAB['members']:
        raise B.DesignError(f'杆件最多 {SAB["members"]} 根（现在 {len(raw_m)}）')
    for k, m in enumerate(raw_m):
        where = f'members[{k}]'
        try:
            if not isinstance(m, list) or len(m) < 3:
                raise B.DesignError(f'{where} 格式应为 [i, j, "材料"]，液压杆再加组名')
            i, j, mat = _idx(m[0], len(nodes), f'{where}[0]'), _idx(m[1], len(nodes), f'{where}[1]'), m[2]
            if i == j:
                raise B.DesignError(f'{where}: 两端是同一个节点')
            if not isinstance(mat, str) or mat not in B.MATERIALS or mat == 'road':
                raise B.DesignError(f'{where}: 材料 {mat!r} 不能用（捣蛋车不能用 road）')
            key = (min(i, j), max(i, j))
            if key in seen:
                raise B.DesignError(f'{where}: 重复杆件')
            seen.add(key)
            L = math.dist(nodes[i], nodes[j])
            M = B.MATERIALS[mat]
            if L > M['max_len'] + 1e-9 or L < B.LIMITS['min_len']:
                raise B.DesignError(f'{where}: 长度 {L:.2f} 不在 {B.LIMITS["min_len"]}～{M["max_len"]}')
            g = None
            if mat == 'hydraulic':
                if len(m) < 4 or not isinstance(m[3], str) or not m[3].strip():
                    raise B.DesignError(f'{where}: 液压杆要写组名（字符串），例如 [0, 1, "hydraulic", "A"]')
                g = m[3]
                if g not in groups:
                    groups.append(g)
            mm = M['kg_m'] * L
            mass[i] += mm / 2; mass[j] += mm / 2
            cost += M['cost_m'] * L + M['cost_each']
            used[i] = used[j] = True
            cons.append((i, j, ['road', 'wood', 'steel', 'cable', 'spring', 'hydraulic'].index(mat), mat,
                         GROUP_OFFSET + groups.index(g) if g is not None else -1))
        except B.DesignError as e:
            errs.append(str(e))
    for k in range(len(nodes)):
        if not used[k]:
            errs.append(f'nodes[{k}] 没有连杆件')
    cost += B.JOINT_COST * len(nodes)
    for k, bl in enumerate(d.get('ballast', [])):   # 配重块：[节点下标, 千克]
        try:
            if not isinstance(bl, list) or len(bl) != 2:
                raise B.DesignError(f'ballast[{k}] 格式应为 [节点下标, 千克]')
            ni = _idx(bl[0], len(nodes), f'ballast[{k}][0]')
            kg = B.NUM(bl[1], f'ballast[{k}][1]')
            if kg <= 0:
                raise B.DesignError(f'ballast[{k}] 的千克数必须 > 0')
            mass[ni] += kg
            cost += SAB['ballast_cost'] * kg
        except B.DesignError as e:
            errs.append(str(e))
    total = sum(mass)
    if not math.isfinite(total) or total > SAB['mass'] + 1e-6:
        errs.append(f'总质量 {total:.0f} kg 超过 {SAB["mass"]}')
    if not math.isfinite(cost) or cost > SAB['budget'] + 1e-6:
        errs.append(f'造价 {cost:.0f} 超过 {SAB["budget"]}')
    mot = d.get('motor', {})
    speed = B.NUM(mot.get('speed', 8), 'motor.speed', 0.5, SAB['speed'])
    accel = B.NUM(mot.get('accel', 3), 'motor.accel', 0.1, SAB['accel'])
    sensors, rules, sid = [], [], {}
    raw_s = d.get('sensors', [])
    if len(raw_s) > SAB['sensors']:
        errs.append(f'传感器最多 {SAB["sensors"]} 个')
    for k, s in enumerate(raw_s[:SAB['sensors']]):
        if not isinstance(s, dict):
            raise B.DesignError(f'sensors[{k}] 必须是 JSON 对象')
        name = s.get('id')
        if not isinstance(name, str) or name in sid:
            errs.append(f'sensors[{k}]: id 必须是不重复的字符串'); continue
        t = s.get('type')
        if t == 'start':
            sensors.append(dict(type=2, ci=-1, x=0, dir=0, minmass=0, veh=0))
        elif t == 'mygate':
            sensors.append(dict(type=4, ci=-1, x=B.NUM(s.get('x'), f'sensors[{k}].x'), dir=0, minmass=0, veh=0))
        else:
            errs.append(f'sensors[{k}]: 捣蛋车只能用 start / mygate'); continue
        sid[name] = len(sensors) - 1
    H = B.MATERIALS['hydraulic']
    raw_r = d.get('rules', [])
    if len(raw_r) > SAB['rules']:
        errs.append(f'规则最多 {SAB["rules"]} 条')
    for k, r in enumerate(raw_r[:SAB['rules']]):
        try:
            if not isinstance(r, dict):
                raise B.DesignError(f'rules[{k}] 必须是 JSON 对象')
            if r.get('on') not in sid or r.get('do') != 'hyd' or r.get('group') not in groups:
                raise B.DesignError(f'rules[{k}]: on 要是传感器 id，do 只能是 hyd，group 要是已有液压组')
            ratio = B.NUM(r.get('ratio'), f'rules[{k}].ratio', H['ratio'][0], H['ratio'][1])
            dl = B.NUM(r.get('delay', 0), f'rules[{k}].delay', 0, 120)
            sp = B.NUM(r.get('speed', SAB['hyd_speed']), f'rules[{k}].speed（捣蛋车液压）', 0.05, SAB['hyd_speed'])
            acc = B.NUM(r['accel'], f'rules[{k}].accel（捣蛋车液压）', 0.5, SAB['hyd_accel']) if 'accel' in r else min(8 * sp, SAB['hyd_accel'])
            rules.append(dict(sensor=sid[r['on']], delay=dl, act=0, arg=GROUP_OFFSET + groups.index(r['group']), v1=ratio, v2=sp, v3=acc))
        except B.DesignError as e:
            errs.append(str(e))
    if errs:
        raise B.DesignError('\n'.join(errs))
    return dict(raw=d, group_ids={g: GROUP_OFFSET + i for i, g in enumerate(groups)}, nodes=nodes, wheels=ws, mass_list=mass, mass=total,
                cost=round(cost, 2), cons=cons, speed=speed, accel=accel, sensors=sensors, rules=rules)


def sab_vehicle(lv, S, depart):
    sx = lv['spawn']['left']
    gy = lv.get('ground_y', 0.0)
    parts = []
    for k, (x, y) in enumerate(S['nodes']):
        if k in S['wheels']:
            parts.append((sx + x, gy + y, S['mass_list'][k], SAB['wheel_r'], 1))
        else:
            parts.append((sx + x, gy + y, S['mass_list'][k], SAB['body_r'], 2))
    return dict(type='saboteur', parts=parts, cons=S['cons'], mass=S['mass'], dir=1, speed=S['speed'], accel=S['accel'],
                brake=SAB['brake'], sab=1, depart=depart, sensors=S['sensors'], rules=S['rules'], raw=S['raw'], group_ids=S['group_ids'])


def convoy_scenario(lv):
    return dict(name='决赛车队', vehicles=lv['convoy'])


def duel(lv, P, S, replay=False):
    """返回 (结果 dict, 回放)。结果含 bridge_win, sab_win, sab_arrived, convoy_ok, anomaly, verdict（裁决原因）, text。
    桥方得分：标准车队全部按时完好到达。攻方得分：车队没有全到，且捣蛋车在截止时刻（含）前完好到达对岸。
    捣蛋车没有按时完好到达 → 攻击不算数，按无捣蛋车基线判；出现物理异常 → 桥自身基线也异常则桥不得分，否则判捣蛋车钻漏洞。"""
    sab = sab_vehicle(lv, S, lv['sab_depart'])
    sc = convoy_scenario(lv)
    res, rep = B.simulate(lv, P, sc, replay=replay, sab=sab)
    sv = res['sab']
    sab_ok = sv['status'] == 2 and sv['t'] <= lv['sab_deadline'] + 1e-9
    convoy_ok = all(v['status'] == 2 for v in res['std_vehicles']) and not res.get('anomaly')
    anomaly = res.get('anomaly', 0)
    bridge_win = convoy_ok
    sab_win = (not convoy_ok) and sab_ok and not anomaly
    verdict = '车队全部到达，桥方胜' if convoy_ok else ('车队没全到且捣蛋车按时完好到达，捣蛋车胜' if sab_win else '')
    baseline = None
    if not sab_ok and not anomaly:
        base, _ = B.simulate(lv, P, sc)
        baseline = dict(ok=bool(base.get('ok')), anomaly=base.get('anomaly', 0), text=B.summarize(base, P, sc)['text'])
        bridge_win = baseline['ok']
        verdict = '捣蛋车没有按时完好到达，攻击不算数；按无捣蛋车基线判：' + ('车队全到，桥方胜' if bridge_win else '车队也过不去，双方都不得分')
    if anomaly:
        base, _ = B.simulate(lv, P, sc)
        baseline = dict(ok=bool(base.get('ok')), anomaly=base.get('anomaly', 0), text=B.summarize(base, P, sc)['text'])
        bridge_win = not baseline['anomaly']
        verdict = '对局出现物理异常：' + ('桥自身基线也异常，桥方不得分' if not bridge_win else '判为捣蛋车钻漏洞，桥方胜')
    if not verdict:
        verdict = '车队没全到，捣蛋车也没得分'
    out = dict(convoy_ok=convoy_ok, sab_arrived=sab_ok, bridge_win=bridge_win, sab_win=sab_win, anomaly=anomaly,
               broken=res.get('broken', 0), res=res, verdict=verdict, baseline=baseline)
    t = B.summarize(res, P, sc)['text']
    if sv['status'] == 2 and not sab_ok:
        t += f'\n  捣蛋车到达太晚（{sv["t"]:.2f} 秒 > 截止 {lv["sab_deadline"]} 秒），不算数'
    t += '\n  裁决：' + verdict
    out['text'] = t
    if rep is not None:
        # 回放里写清正式裁决（原始模拟里车队可能没全到，但按规则判桥方胜）
        rep['result'] = dict(ok=bool(bridge_win), text=t, raw_ok=bool(res.get('ok')))
        rep['verdict'] = dict(bridge_win=bridge_win, sab_win=sab_win, sab_arrived=sab_ok, reason=verdict, baseline=baseline)
    _duel_archive(lv, P, S, sc, out)
    return out, rep


def _duel_archive(lv, P, S, sc, out):
    """自动存档：每局一份完整记录（双方原文、场景、原始结果、基线结果、正式裁决）。BRIDGE_NO_ARCHIVE=1 时关闭。"""
    if os.environ.get('BRIDGE_NO_ARCHIVE') == '1':
        return
    try:
        import time
        d = os.path.join(os.getcwd(), 'archive', 'duels')
        os.makedirs(d, exist_ok=True)
        rec = dict(t=time.strftime('%Y-%m-%d %H:%M:%S'), ts=round(time.time(), 3), pid=os.getpid(),
                   bridge=P.get('raw'), bridge_hash=B._hash(P.get('raw')), saboteur=S.get('raw'), saboteur_hash=B._hash(S.get('raw')),
                   scenario=sc, raw_result=dict(ok=bool(out['res'].get('ok')), anomaly=out['anomaly'], vehicles=out['res']['vehicles']),
                   baseline=out['baseline'], bridge_win=out['bridge_win'], sab_win=out['sab_win'], sab_arrived=out['sab_arrived'],
                   verdict=out['verdict'], level_hash=B._hash(B.level_view(lv)), engine_sha256=B.engine_sha256())
        import threading, uuid
        fn = os.path.join(d, time.strftime('%Y%m%d-%H%M%S') + f'_{os.getpid()}_{threading.get_ident()}_{uuid.uuid4().hex[:8]}.json')
        with open(fn, 'x', encoding='utf-8') as f:   # 'x'：不会覆盖别的线程/进程的同名记录
            json.dump(rec, f, ensure_ascii=False)
    except OSError as e:
        B._warn_archive(e)


def cli(pos, opts):
    lv = B.load_level('final')
    try:
        braw = B.load_json(pos[0])
        P = B.parse_design(lv, braw)
    except (OSError, ValueError) as e:
        print(f'桥设计文件读不了：{e}'); return 1
    except B.DesignError as e:
        print('桥设计不合法：\n' + str(e)); return 1
    try:
        sraw = B.load_json(pos[1])
        S = parse_sab(lv, sraw)
    except (OSError, ValueError) as e:
        print(f'捣蛋车文件读不了：{e}'); return 1
    except B.DesignError as e:
        print('捣蛋车不合法：\n' + str(e)); return 1
    arch = None if '--no-archive' in opts else B.archive_dir('duel')
    want = '--html' in opts or '--replay' in opts or bool(arch)
    r, rep = duel(lv, P, S, replay=want)
    arrive = ('是' if r['sab_arrived'] else '否 —— 本场攻击不算数（桥塌了也不算你赢）')
    lines = [f'桥造价 {P["cost"]:.0f} / {lv["budget"]}；捣蛋车 {S["mass"]:.0f} kg、造价 {S["cost"]:.0f} / {SAB["budget"]}',
             f'捣蛋车是否在 {lv["sab_deadline"]:g} 秒（含）前完好到达对岸：{arrive}', r['text'],
             '结果：' + ('桥方胜' if r['bridge_win'] else ('捣蛋车胜' if r['sab_win'] else '双方都不得分'))]
    for l in lines:
        print(l)
    if arch:
        B.save_archive(arch, braw, lines, [('duel', rep)], name='bridge.json')
        with open(os.path.join(arch, 'saboteur.json'), 'w', encoding='utf-8') as f:
            json.dump(sraw, f, ensure_ascii=False, indent=1)
    if rep is not None:
        if '--replay' in opts:
            with open(opts['--replay'], 'w', encoding='utf-8') as f:
                json.dump(rep, f, ensure_ascii=False, separators=(',', ':'))
        if '--html' in opts:
            B.write_html(rep, opts['--html'])
    return 0
