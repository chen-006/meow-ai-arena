#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""《纸上谈兵》本地工具。只用 Python 标准库。

  python play.py check                     检查 submission/ 里的 7 份布阵，并编译指挥程序
  python play.py level 1                   跑第 1 关（默认用 submission/level1.json 和 submission/commander.cpp）
  python play.py level 1 别的.json --cmd -   换一份布阵、不用程序（--cmd 可以写 .cpp / .exe / -）
  python play.py levels                    五关各跑一场，打一张总表
  python play.py duel                      1v1：拿 submission/duel.json 和每支陪练各打 4 场（2 张图 × 2 个座位）
  python play.py duel --opp 骑兵快攻        只打这一支陪练；--opp 也可以写 别的布阵.json[:程序.cpp]
  python play.py melee                     四人混战：拿 submission/melee.json 和三支陪练打 4 场（你轮流坐 4 个座位）
  python play.py map 3                     在终端里画第 3 关的地图（我方视角）：地形、布阵区、敌军和道具的初始位置
  python play.py map 樱丘                   对抗图也能画
  python play.py replay replays/level3.wrp           讲一场战斗的经过：时间线、伤亡、关键事件
  python play.py replay replays/level3.wrp --at 20   再画第 20 秒的战场（可以写 --at 10,20,30）

每跑一场都会把回放存进 replays/（同一关、同一组对局只留最新的一份）。
退出码：level 命令 0 = 过关、1 = 没过、3 = 布阵不合法、2 = 命令或文件有错、4 = 引擎超时（engine_timeout，请告诉主办方）。
"""
import collections
import glob
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.dont_write_bytecode = True  # 不在选手目录里留 __pycache__
import build as _build  # noqa: E402

WAR = HERE / "war.exe"
SUB = HERE / "submission"
SPAR = HERE / "sparring"
REPLAYS = HERE / "replays"
TMP = HERE / "build"

KIND_ZH = {"militia": "新兵", "shield": "盾卫", "spear": "长枪兵", "archer": "弓手", "cavalry": "骑兵", "hammer": "大锤",
           "bomber": "炮仗兵", "catapult": "投石车", "giant": "大块头", "boss": "镇山兽", "lord": "主帅"}
KIND_CH = {"militia": "m", "shield": "s", "spear": "p", "archer": "a", "cavalry": "c", "hammer": "h",
           "bomber": "b", "catapult": "t", "giant": "g", "boss": "x", "lord": "l"}
PROP_CH = {0: "O", 1: "K", 2: "F", 3: "=", 4: "=", 5: "I"}
PROP_ZH = {"snowball": "雪球", "keg": "火药桶", "flag": "军旗", "barrier": "木栅", "gate": "木栅门", "pillar": "立石"}
WEAPON_ZH = {"militia": "新兵的刀", "shield": "盾卫的刀", "spear": "长枪", "cavalry": "骑兵贴身砍", "hammer": "大锤", "sweep": "大块头横扫",
             "boss": "镇山兽顶撞", "dagger": "匕首", "arrow": "箭", "stone": "石头", "explosion": "爆炸", "charge": "骑兵冲锋",
             "brace": "架枪迎击", "snowball": "雪球", "boss_charge": "镇山兽冲撞", "fell": "掉下去", "drown": "淹死",
             "explode": "自己炸了", "topple": "被挤倒", "prop": "道具"}
REASON_ZH = {
    "win:eliminate": "敌军全灭", "win:destroy:镇山兽": "镇山兽被打倒",
    "timeout_lose": "时间到，没有达成过关条件（时间到一律算没过）", "eliminated": "我方全灭（场上的石头、雪球、炮仗都停了以后还没赢）",
    "lose:destroy:军旗": "军旗被打倒", "lose:dead:主帅": "主帅阵亡",
    "engine_timeout": "引擎超时（engine_timeout）：这一场超过墙上时钟上限还没跑完，没有胜负。这是引擎的毛病，请把布阵和程序写进 notes.md",
    "annihilation": "全歼", "timeout_zone": "时间到，抢山头：比计分区里的兵的造价",
    "timeout_cost": "时间到，山头上双方都没兵：比全场剩下的兵的造价",
    "timeout_draw": "时间到，山头和全场剩余造价都一样，平局",
    "timeout": "时间到",
}


def out(*a, **k):
    try:
        print(*a, **k, flush=True)
    except (BrokenPipeError, OSError):  # 输出被 head 之类截断
        os._exit(0)


def fail(msg, code=2):
    out(msg)
    sys.exit(code)


def rel(p):
    try:
        return str(Path(p).resolve().relative_to(HERE)).replace("\\", "/")
    except ValueError:
        return str(p)


def load_json(p):
    return json.loads(Path(p).read_text(encoding="utf-8-sig"))


# ====================================================================== 文件
def level_file(n):
    hits = sorted(glob.glob(str(HERE / "levels" / f"第{n}关_*.json")))
    if not hits:
        fail(f"没有第 {n} 关（levels/ 里找不到 第{n}关_*.json）")
    return Path(hits[0])


def map_file(name):
    """'1'～'5' = 关卡；'樱丘' '河心洲' '涨潮沙洲' = 对抗图；也可以直接写路径。"""
    if str(name).isdigit():
        return level_file(int(name))
    p = HERE / "maps" / f"{name}.json"
    if p.is_file():
        return p
    if Path(name).is_file():
        return Path(name)
    fail(f"不认识的地图：{name}（可以写 1～5、樱丘、河心洲、涨潮沙洲）")


def resolve_cmd(arg, default_src=None):
    """返回传给 war.exe 的 --cmd：'-'、或编译好的 exe 路径。"""
    if arg is None:
        if default_src and Path(default_src).is_file():
            arg = str(default_src)
        else:
            return "-", "不用程序"
    if arg == "-":
        return "-", "不用程序（布阵姿态 + 默认行为）"
    p = Path(arg)
    if not p.is_absolute() and not p.exists():
        p = HERE / arg
    if p.suffix.lower() == ".cpp":
        exe, err = _build.build(p)
        if err:
            fail(err + "\n（想先不用程序跑，可以加 --cmd -）")
        return str(exe), rel(p)
    if p.is_file():
        return str(p), rel(p)
    fail(f"找不到指挥程序 {arg}")


def sparring_list(mode):
    man = load_json(SPAR / "陪练.json")
    return man[mode]


def resolve_opp(spec, mode):
    """陪练名，或 布阵.json[:程序]。返回 (显示名, 布阵路径, cmd)。"""
    table = sparring_list(mode)
    if spec in table:
        e = table[spec]
        cmd, _ = resolve_cmd(str(SPAR / e["cmd"]) if e.get("cmd") and e["cmd"] != "-" else "-")
        return spec, str(SPAR / e["deploy"]), cmd
    dep, cmd = spec, None
    if ":" in spec[2:]:
        i = spec.rindex(":")
        if i > 1:
            dep, cmd = spec[:i], spec[i + 1:]
    if not Path(dep).is_file():
        fail(f"不认识的对手 {spec}。陪练有：{'、'.join(table)}；也可以写 布阵.json 或 布阵.json:程序.cpp")
    c, _ = resolve_cmd(cmd or "-")
    return Path(dep).stem, dep, c


# ====================================================================== 跑 war.exe
def war_run(mapf, deploys, cmds, replay=None, secs=None):
    TMP.mkdir(exist_ok=True)
    fd, jpath = tempfile.mkstemp(prefix="r-", suffix=".json", dir=TMP)
    os.close(fd)
    a = [str(WAR), "run", str(mapf)]
    for d in deploys:
        a += ["--deploy", str(d)]
    for c in cmds:
        a += ["--cmd", str(c)]
    a += ["--json", jpath]
    if replay:
        REPLAYS.mkdir(exist_ok=True)
        a += ["--replay", str(replay)]
    if secs:
        a += ["--secs", str(secs)]
    p = subprocess.run(a, capture_output=True)
    try:
        r = json.loads(Path(jpath).read_text(encoding="utf-8"))
    except Exception:
        r = None
    finally:
        try:
            os.unlink(jpath)
        except OSError:
            pass
    if r is None:
        try:  # 出错时（例如引擎超时）结果可能只打在标准输出
            r = json.loads(p.stdout.decode("utf-8", "replace").strip().splitlines()[-1])
        except Exception:
            r = None
    if r is None:
        fail("war.exe 没有给出结果：\n" + p.stderr.decode("utf-8", "replace")[-1500:])
    if "error" in r and r.get("error") != "engine_timeout":
        fail(f"war.exe 出错：{r.get('error')} {r.get('detail', '')}")
    return r


def engine_error(r):
    """引擎超时之类的错误场次：返回一句说明，正常场次返回 None。"""
    if "error" not in r:
        return None
    return REASON_ZH.get(r["error"], f"引擎出错：{r['error']} {r.get('detail', '')}")


def war_batch(tasks, workers=None):
    """tasks：[{map, deploys, cmds, replay}]，返回同顺序的结果。"""
    workers = workers or max(1, min(8, (os.cpu_count() or 4) // 2))
    lines = []
    for i, t in enumerate(tasks):
        d = {"id": i, "map": str(t["map"]), "deploys": [str(x) for x in t["deploys"]], "cmds": [str(x) for x in t["cmds"]]}
        if t.get("replay"):
            REPLAYS.mkdir(exist_ok=True)
            d["replay"] = str(t["replay"])
        lines.append(json.dumps(d, ensure_ascii=False))
    p = subprocess.run([str(WAR), "batch", "--workers", str(workers)], input="\n".join(lines).encode("utf-8"), capture_output=True)
    res = {}
    for l in p.stdout.decode("utf-8", "replace").splitlines():
        try:
            r = json.loads(l)
            res[r["id"]] = r
        except Exception:
            pass
    outl = []
    for i in range(len(tasks)):
        if i not in res or ("error" in res[i] and res[i]["error"] != "engine_timeout"):
            fail(f"第 {i + 1} 场没跑成：{res.get(i, {}).get('error', '')}\n" + p.stderr.decode("utf-8", "replace")[-1500:])
        outl.append(res[i])  # 引擎超时的场次原样带回去（带 error 字段），由调用的地方单独显示
    return outl


def war_check(mapf, deploy, seat=0):
    p = subprocess.run([str(WAR), "check", str(mapf), str(deploy), "--seat", str(seat)], capture_output=True)
    txt = p.stdout.decode("utf-8", "replace").strip()
    try:
        return json.loads(txt.splitlines()[-1])
    except Exception:
        return {"ok": False, "errors": [txt or p.stderr.decode("utf-8", "replace").strip()]}


def commander_note(r, seat=0):
    c = (r.get("commanders") or [{}])[seat] if r.get("commanders") else {}
    s = r["seats"][seat]
    if not c.get("used"):
        return "没用程序（布阵姿态 + 默认行为）"
    after = "之后不再有新指令，每个兵继续执行最后收到的那条"
    st = {"ok": "正常", "crash": f"崩溃了（{after}）", "lost": f"失联（{after}）",
          "timeout_limit": f"超时太多被结束（{after}）", "output_limit": f"输出太多被结束（{after}）",
          "start_fail": "没启动起来（全场按布阵姿态 + 默认行为打）"}.get(c.get("status"), c.get("status"))
    t = f"程序{st}，平均 {c.get('avg_ms', 0):.2f} 毫秒/回合，最慢 {c.get('max_ms', 0):.2f}，超时 {c.get('timeouts', 0)} 次"
    if c.get("invalid"):
        t += f"，输出无效 {c['invalid']} 行（看不懂的应答作废那一回合；往标准输出多打的行被丢掉，只影响那一回合。调试信息请打到标准错误）"
    if s.get("ignored_orders"):
        t += f"；被忽略的指令 {s['ignored_orders']} 条"
    if s.get("faults"):
        fs = [f if isinstance(f, (list, tuple)) else [f.get("step", "?"), f.get("kind", ""), f.get("detail", "")] for f in s["faults"]]
        t += f"；出错 {s.get('faults_total', len(fs))} 次，最早的：" + "；".join(f"第 {f[0] / 60:.1f} 秒 {f[1]} {f[2] if len(f) > 2 else ''}" for f in fs[:3])
    return t


# ====================================================================== 闯关
def level_key(r):
    li = r.get("level_info", {})
    bits = []
    for f in li.get("flags", []):
        bits.append(f"军旗 {f.get('hp', 0):.0f} 血")
    if "lord_hp" in li:
        bits.append(f"主帅 {li['lord_hp']:.0f} 血" + ("" if li.get("lord_alive", True) else "（阵亡）"))
    if "boss_hp" in li:
        bits.append(f"镇山兽 {li['boss_hp']:.0f} 血")
    tags = li.get("tags", {})
    for tg, v in tags.items():
        if tg in ("军旗", "镇山兽"):
            continue
        bits.append(f"{tg} 剩 {v['alive']}/{v['total']}")
    if li.get("terrain_removed"):
        bits.append("桥断了")
    return "，".join(bits)


def print_level(n, r, deploy, cmdname, replay):
    lv = load_json(level_file(n))
    seat = lv["seats"][0]
    name = lv["name"].split("_", 1)[-1]
    if engine_error(r):
        out(f"第{n}关 {name}：{engine_error(r)}")
        out(f"  布阵 {rel(deploy)}，程序 {cmdname}")
        return 4
    s = r["seats"][0]
    if not s.get("deploy_ok", True):
        out(f"第{n}关 {name}：布阵不合法（这一关会判 0 分）  {rel(deploy)}")
        for e in s.get("deploy_errors", [])[:15]:
            out("  - " + e)
        return 3
    t0, t1 = r["teams"][0], r["teams"][1]
    ok = r.get("passed")
    out(f"第{n}关 {name}：{'过关' if ok else '没过'}  用时 {r['secs']:.1f} 秒（时限 {lv['time_limit']:g}）  "
        f"造价 {s['deploy_cost']:g}（预算 {seat['budget']:g}）")
    out(f"  结束原因：{REASON_ZH.get(r['reason'], r['reason'])}")
    lord = 1 if "lord" in seat else 0
    out(f"  我方剩 {t0['alive_units'] - (lord and r.get('level_info', {}).get('lord_alive', False))}/{t0['start_units'] - lord} 个兵"
        f"（造价 {t0['alive_cost']:g}/{t0['start_cost']:g}）  敌方剩 {t1['alive_units']}/{t1['start_units']}")
    k = level_key(r)
    if k:
        out(f"  关键数：{k}")
    out(f"  {commander_note(r)}")
    out(f"  布阵 {rel(deploy)}，程序 {cmdname}，回放 {rel(replay)}（看经过：python play.py replay {rel(replay)}）")
    return 0 if ok else 1


def cmd_level(args):
    if not args or not args[0].isdigit():
        fail("用法：python play.py level <1～5> [布阵.json] [--cmd 程序.cpp|.exe|-]")
    n = int(args[0])
    rest = args[1:]
    cmd = None
    if "--cmd" in rest:
        i = rest.index("--cmd")
        cmd = rest[i + 1]
        rest = rest[:i] + rest[i + 2:]
    deploy = Path(rest[0]) if rest else SUB / f"level{n}.json"
    if not deploy.is_file():
        fail(f"找不到布阵文件 {deploy}")
    c, cname = resolve_cmd(cmd, SUB / "commander.cpp")
    replay = REPLAYS / f"level{n}.wrp"
    r = war_run(level_file(n), [deploy], [c], replay=replay)
    return print_level(n, r, deploy, cname, replay)


def cmd_levels(args):
    cmd = None
    if "--cmd" in args:
        cmd = args[args.index("--cmd") + 1]
    c, cname = resolve_cmd(cmd, SUB / "commander.cpp")
    tasks = []
    for n in range(1, 6):
        tasks.append({"map": level_file(n), "deploys": [SUB / f"level{n}.json"], "cmds": [c], "replay": REPLAYS / f"level{n}.wrp"})
    res = war_batch(tasks)
    out(f"程序：{cname}")
    passed = 0
    for n, r in enumerate(res, 1):
        lv = load_json(level_file(n))
        name = lv["name"].split("_", 1)[-1]
        if engine_error(r):
            out(f"  第{n}关 {name:<4}  {engine_error(r)}")
            continue
        s = r["seats"][0]
        if not s.get("deploy_ok", True):
            out(f"  第{n}关 {name:<4}  布阵不合法：{'；'.join(s.get('deploy_errors', [])[:3])}")
            continue
        passed += bool(r.get("passed"))
        out(f"  第{n}关 {name:<4}  {'过关' if r.get('passed') else '没过'}  造价 {s['deploy_cost']:>5g}（预算 {lv['seats'][0]['budget']:g}）  "
            f"{r['secs']:5.1f} 秒  {REASON_ZH.get(r['reason'], r['reason'])}  {level_key(r)}")
    out(f"过了 {passed}/5 关。每关的回放在 replays/levelN.wrp。")
    out("本地只看得到过没过和造价：正式的关卡分取决于全场最低造价（最省的人 100 分，比他每贵一倍扣 25 分，过关最少 20 分，没过 0 分）。")
    return 0


# ====================================================================== 对抗
def parse_opts(args, names):
    d = {}
    rest = []
    i = 0
    while i < len(args):
        if args[i] in names and i + 1 < len(args):
            d[args[i]] = args[i + 1]
            i += 2
        else:
            rest.append(args[i])
            i += 1
    return d, rest


def safe(s):
    return "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in s)


def cmd_duel(args):
    o, _ = parse_opts(args, ("--opp", "--deploy", "--cmd", "--map"))
    deploy = Path(o.get("--deploy", SUB / "duel.json"))
    if not deploy.is_file():
        fail(f"找不到布阵文件 {deploy}")
    c, cname = resolve_cmd(o.get("--cmd"), SUB / "commander.cpp")
    opps = o["--opp"].split(",") if "--opp" in o else list(sparring_list("duel"))
    maps = o["--map"].split(",") if "--map" in o else ["樱丘", "河心洲"]
    for m in maps:
        chk = war_check(map_file(m), deploy)
        if not chk.get("ok"):
            out(f"布阵不合法（{m}）：{'；'.join(chk.get('errors', [])[:8])}")
            out("正式比赛里不合法的 1v1 布阵等于场上没有兵，每一场都判负。")
            return 3
    tasks, meta = [], []
    for spec in opps:
        name, odep, ocmd = resolve_opp(spec, "duel")
        for m in maps:
            for side in (0, 1):
                deps = [deploy, odep] if side == 0 else [odep, deploy]
                cmds = [c, ocmd] if side == 0 else [ocmd, c]
                rp = REPLAYS / f"duel_{safe(name)}_{m}_{side}号位.wrp"
                tasks.append({"map": map_file(m), "deploys": deps, "cmds": cmds, "replay": rp})
                meta.append((name, m, side, rp))
    res = war_batch(tasks)
    out(f"1v1：我方 {rel(deploy)}（造价 {war_check(map_file(maps[0]), deploy).get('cost', '?')}），程序 {cname}")
    by_opp = collections.OrderedDict()
    bad = 0
    for (name, m, side, rp), r in zip(meta, res):
        if engine_error(r):
            bad += 1
            by_opp.setdefault(name, []).append((m, side, None, engine_error(r) + "（不计分）", rp, r))
            continue
        w = r["winner"]
        pts = 0.5 if w < 0 else (1.0 if w == side else 0.0)
        me, op = r["teams"][side], r["teams"][1 - side]
        txt = {1.0: "胜", 0.5: "平", 0.0: "负"}[pts]
        why = REASON_ZH.get(r["reason"], r["reason"])
        detail = f"{txt} {r['secs']:4.1f}秒 {why}"
        if r["reason"] == "timeout_cost":
            detail += f"（全场剩余造价 我{me['alive_cost']:g}:{op['alive_cost']:g}敌）"
        elif r["reason"].startswith("timeout"):
            detail += f"（山头上 我{me['zone_cost']:g}:{op['zone_cost']:g}敌）"
        detail += f" 剩 我{me['alive_units']}:{op['alive_units']}敌"
        by_opp.setdefault(name, []).append((m, side, pts, detail, rp, r))
    total, cnt = 0.0, 0
    for name, games in by_opp.items():
        ok = [x for x in games if x[2] is not None]
        s = sum(x[2] for x in ok)
        total += s
        cnt += len(ok)
        out(f"  对 {name}：{s:g}/{len(ok)}")
        for m, side, pts, detail, rp, r in games:
            out(f"    {m} {side}号位：{detail}" + ("" if pts is None else f"   回放 {rel(rp)} --seat {side}"))
    out(f"本地 1v1 分（对这些对手的平均得分 × 100）：{100 * total / max(1, cnt):.1f}" + (f"（另有 {bad} 场引擎超时，不计）" if bad else ""))
    first = next(((r, mt[2]) for mt, r in zip(meta, res) if not engine_error(r)), None)
    if first:
        out(f"  {commander_note(first[0], first[1])}")
    return 0


def cmd_melee(args):
    o, _ = parse_opts(args, ("--opp", "--deploy", "--cmd"))
    deploy = Path(o.get("--deploy", SUB / "melee.json"))
    if not deploy.is_file():
        fail(f"找不到布阵文件 {deploy}")
    mf = map_file("涨潮沙洲")
    chk = war_check(mf, deploy)
    if not chk.get("ok"):
        out(f"布阵不合法：{'；'.join(chk.get('errors', [])[:8])}")
        return 3
    c, cname = resolve_cmd(o.get("--cmd"), SUB / "commander.cpp")
    opps = o["--opp"].split(",") if "--opp" in o else list(sparring_list("melee"))[:3]
    if len(opps) != 3:
        fail("混战要正好 3 个对手（--opp 甲,乙,丙）")
    ops = [resolve_opp(s, "melee") for s in opps]
    tasks, seats = [], []
    for me in range(4):
        deps, cmds, names = [], [], []
        k = 0
        for s in range(4):
            if s == me:
                deps.append(deploy); cmds.append(c); names.append("我")
            else:
                n, d, cc = ops[k]; k += 1
                deps.append(d); cmds.append(cc); names.append(n)
        rp = REPLAYS / f"melee_我在{me}号位.wrp"
        tasks.append({"map": mf, "deploys": deps, "cmds": cmds, "replay": rp})
        seats.append((me, names, rp))
    res = war_batch(tasks)
    out(f"四人混战：我方 {rel(deploy)}（造价 {chk.get('cost')}），程序 {cname}；对手 {'、'.join(x[0] for x in ops)}")
    tot, cnt = 0.0, 0
    for (me, names, rp), r in zip(seats, res):
        if engine_error(r):
            out(f"  我在 {me} 号位：{engine_error(r)}（不计分）")
            continue
        cnt += 1
        t = r["teams"][me]
        tot += t["score"]
        others = "，".join(f"{names[i]} 第{r['teams'][i]['rank']}" for i in range(4) if i != me)
        out(f"  我在 {me} 号位：第 {t['rank']} 名  名次分 {t['rank_pts']:g} + 歼敌分 {t['kill_pts']:.2f}（打掉 {t['kill_cost']:g}）= {t['score']:.2f}"
            f"  剩 {t['alive_units']}/{t['start_units']}  [{others}]  回放 {rel(rp)} --seat {me}")
    out(f"本地混战分（平均每场得分 ÷ 6 × 100）：{100 * tot / max(1, cnt) / 6:.1f}" + (f"（另有 {len(res) - cnt} 场引擎超时，不计）" if cnt < len(res) else ""))
    first = next(((r, st[0]) for st, r in zip(seats, res) if not engine_error(r)), None)
    if first:
        out(f"  {commander_note(first[0], first[1])}")
    return 0


# ====================================================================== 检查
def cmd_check(args):
    bad = 0
    out("检查 submission/：")
    for n in range(1, 6):
        f = SUB / f"level{n}.json"
        if not f.is_file():
            out(f"  level{n}.json  没有这个文件（这一关 0 分）"); bad += 1; continue
        r = war_check(level_file(n), f)
        lv = load_json(level_file(n))["seats"][0]
        if r.get("ok"):
            out(f"  level{n}.json  合法  造价 {r['cost']:g}/{lv['budget']:g}  {r['units']} 人/{lv['max_units']}")
        else:
            bad += 1
            out(f"  level{n}.json  不合法：{'；'.join(r.get('errors', [])[:6])}")
    for fn, maps in (("duel.json", ["樱丘", "河心洲"]), ("melee.json", ["涨潮沙洲"])):
        f = SUB / fn
        if not f.is_file():
            out(f"  {fn}  没有这个文件"); bad += 1; continue
        errs, cost = [], None
        for m in maps:
            r = war_check(map_file(m), f)
            cost = r.get("cost", cost)
            if not r.get("ok"):
                errs += [f"{m}：{e}" for e in r.get("errors", [])[:4]]
        lim = load_json(map_file(maps[0]))["seats"][0]
        if errs:
            bad += 1
            out(f"  {fn}  不合法：{'；'.join(errs[:6])}")
        else:
            out(f"  {fn}  合法  造价 {cost:g}/{lim['budget']:g}")
    src = SUB / "commander.cpp"
    if src.is_file():
        exe, err = _build.build(src, quiet=True)
        if err:
            bad += 1
            out("  commander.cpp  " + err)
        else:
            out("  commander.cpp  编译通过 → " + rel(exe))
    else:
        out("  commander.cpp  没有（所有场次按布阵姿态 + 默认行为打）")
    out("全部合法。" if not bad else f"有 {bad} 处要改。")
    return 0 if not bad else 1


# ====================================================================== 回放
REC = struct.Struct("<IBBBBHHfffhhhh")
OT = {1: "unit", 2: "stone", 3: "prop", 4: "arrow"}


def read_replay(path):
    data = Path(path).read_bytes()
    head = b"WARREPLAY1\n"
    if not data.startswith(head):
        fail("不是回放文件（开头不是 WARREPLAY1）")
    pos = len(head)
    meta, frames, events, result = None, [], [], None
    while pos + 8 <= len(data):
        tag = data[pos:pos + 4].decode("ascii", "replace")
        n = struct.unpack_from("<I", data, pos + 4)[0]
        body = data[pos + 8:pos + 8 + n]
        pos += 8 + n
        if tag == "META":
            meta = json.loads(body.decode("utf-8"))
        elif tag == "FRAM":
            step, cnt = struct.unpack_from("<II", body, 0)
            objs = []
            for k in range(cnt):
                (oid, ot, st, fl, team, hp, aux, x, y, z, *_q) = REC.unpack_from(body, 8 + 32 * k)
                objs.append((oid, OT.get(ot, ot), st, fl, -1 if team == 255 else team, hp, aux, x, y, z))
            frames.append((step, objs))
        elif tag == "EVNT":
            events.extend(json.loads(body.decode("utf-8")))
        elif tag == "RSLT":
            result = json.loads(body.decode("utf-8"))
        elif tag == "ENDR":
            break
    return meta, frames, events, result


class Frame:
    """我方视角坐标换算。yaw 用关卡文件里的度数。"""

    def __init__(self, origin, yaw_deg):
        self.ox, self.oz = origin
        a = math.radians(yaw_deg)
        self.c, self.s = math.cos(a), math.sin(a)

    def to_local(self, wx, wz):
        dx, dz = wx - self.ox, wz - self.oz
        return dx * self.c - dz * self.s, dx * self.s + dz * self.c

    def to_world(self, x, z):
        return self.ox + x * self.c + z * self.s, self.oz - x * self.s + z * self.c


def nav_grid(scenario_path):
    p = subprocess.run([str(WAR), "nav", str(scenario_path)], capture_output=True)
    lines = p.stdout.decode("utf-8", "replace").splitlines()
    for i, l in enumerate(lines):
        if l.startswith("GRID "):
            w, h, x0, z0, cell = l.split()[1:6]
            w, h, x0, z0, cell = int(w), int(h), float(x0), float(z0), float(cell)
            rows = []
            for r in lines[i + 1:i + 1 + h]:
                rows.append([None if t == "x" else int(t) / 100 for t in r.split()])
            return {"w": w, "h": h, "x0": x0, "z0": z0, "cell": cell, "rows": rows}
    return None


def grid_h(g, wx, wz):
    cx, cz = int(math.floor((wx - g["x0"]) / g["cell"])), int(math.floor((wz - g["z0"]) / g["cell"]))
    if cx < 0 or cz < 0 or cx >= g["w"] or cz >= g["h"]:
        return None
    row = g["rows"][cz]
    return row[cx] if cx < len(row) else None


def in_zone(z, wx, wz):
    if "circle" in z:
        cx, cz, r = z["circle"]
        return (wx - cx) ** 2 + (wz - cz) ** 2 <= r * r
    x0, z0, x1, z1 = z["rect"]
    return x0 <= wx <= x1 and z0 <= wz <= z1


def draw(sc, seat_idx, objects, kinds, me_team, title, deploy=True, scale=None):
    """画一张我方视角的字符地图。objects：[(id, otype, state, team, wx, wz, aux)]。"""
    seat = sc["seats"][seat_idx]
    fr = Frame(seat["origin"], seat.get("yaw", 0))
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False, encoding="utf-8", dir=TMP) as f:
        json.dump(sc, f, ensure_ascii=False)
        tmpn = f.name
    try:
        g = nav_grid(tmpn)
    finally:
        os.unlink(tmpn)
    b = sc["bounds"]
    cs = [fr.to_local(x, z) for x in (b[0], b[2]) for z in (b[1], b[3])]
    lx0, lx1 = min(c[0] for c in cs), max(c[0] for c in cs)
    lz0, lz1 = min(c[1] for c in cs), max(c[1] for c in cs)
    step = scale or (1.0 if lx1 - lx0 <= 100 else 2.0)
    hmax = max((v for r in g["rows"] for v in r if v is not None), default=0)
    hstep = 1.0 if hmax < 9.5 else 2.0
    cols = int(math.ceil((lx1 - lx0) / step))
    nrows = int(math.ceil((lz1 - lz0) / step))
    grid = []
    for j in range(nrows):
        lz = lz1 - (j + 0.5) * step
        row = []
        for i in range(cols):
            lx = lx0 + (i + 0.5) * step
            wx, wz = fr.to_world(lx, lz)
            h = grid_h(g, wx, wz)
            if h is not None:  # 孤立的高点（树干、立石、柱子）画成 #
                c0 = g["cell"]
                nb = [grid_h(g, wx + dx, wz + dz) for dx, dz in ((c0, 0), (-c0, 0), (0, c0), (0, -c0))]
                if sum(1 for v in nb if v is None or v < h - 1.5) >= 3:
                    h = None
            ch = "#" if h is None else ("." if h < 0.5 * hstep else str(min(9, int(h / hstep + 0.5))))
            for zn in sc.get("zones", []) or []:
                if in_zone(zn, wx, wz) and ch != "#":
                    ch = "~" if zn.get("speed_mul") else ":"
            if deploy and ch != "#":
                for r in seat.get("deploy", []):
                    if r[0] <= lx <= r[2] and r[1] <= lz <= r[3]:
                        ch = "+"
            row.append(ch)
        grid.append(row)
    occ = {}
    for (oid, ot, st, team, wx, wz, aux) in objects:
        lx, lz = fr.to_local(wx, wz)
        i, j = int((lx - lx0) / step), int((lz1 - lz) / step)
        if not (0 <= i < cols and 0 <= j < nrows):
            continue
        if ot == "prop":
            occ.setdefault((i, j), set()).add(PROP_CH.get(aux, "?"))
        elif ot == "unit" and st < 3:
            k = kinds.get(oid, "militia")
            c = KIND_CH.get(k, "?")
            c = c.lower() if team == me_team else c.upper()
            if k == "boss":
                c = "X"
            if k == "lord":
                c = "L"
            occ.setdefault((i, j), set()).add(c)
        elif ot == "stone" and st == 0:
            occ.setdefault((i, j), set()).add("o")
    for (i, j), s in occ.items():
        if len(s) == 1:
            grid[j][i] = next(iter(s))
        else:
            low = [c for c in s if c.islower()]
            up = [c for c in s if c.isupper() and c not in "OKFI=X"]
            grid[j][i] = "*" if (low and up) else sorted(s)[0]
    out(title)
    out(f"（我方视角：上 = 朝敌人的 +z，右 = +x；每格 {step:g} 米；左上角 x={lx0:g} z={lz1:g}，右下角 x={lx1:g} z={lz0:g}）")
    for j, row in enumerate(grid):
        lz = lz1 - (j + 0.5) * step
        out(f"{lz:6.1f} " + "".join(row))
    out("  图例：# 走不了（墙、树干、悬崖、深谷、水）  . 平地  1～9 地面高度（每级 %g 米）  + 我方布阵区  ~ 泥塘  : 计分区/区域" % hstep)
    out("        小写 = 我方，大写 = 敌方：m 新兵 s 盾卫 p 长枪兵 a 弓手 c 骑兵 h 大锤 b 炮仗兵 t 投石车 g 大块头；X 镇山兽 L 主帅 * 双方挤在一格")
    out("        道具：K 火药桶 O 雪球 F 军旗 = 木栅 I 立石 o 飞行中的石头")


def kinds_of(meta):
    return {o["id"]: o["kind"] for o in meta["objects"] if o.get("type") == "unit"}


def label(meta_obj):
    k = KIND_ZH.get(meta_obj.get("kind")) or PROP_ZH.get(meta_obj.get("kind"), meta_obj.get("kind"))
    t = meta_obj.get("tag")
    return f"{k}#{meta_obj['id']}" + (f"[{t}]" if t else "")


def cmd_map(args):
    if not args:
        fail("用法：python play.py map <1～5 | 樱丘 | 河心洲 | 涨潮沙洲> [--seat K]")
    o, rest = parse_opts(args, ("--seat",))
    seat = int(o.get("--seat", 0))
    mf = map_file(rest[0])
    sc = load_json(mf)
    TMP.mkdir(exist_ok=True)
    # 跑 0.1 秒，拿开场的敌军和道具位置（布阵用一个临时的新兵）
    dep = TMP / "_map_probe.json"
    seats = sc["seats"]
    deps = []
    for s in seats:
        r = s["deploy"][0]
        x, z = math.floor(r[0] * 2 + 1) / 2, math.floor(r[1] * 2 + 1) / 2
        deps.append({"units": [{"type": "新兵", "x": x, "z": z}]})
    paths = []
    for i, d in enumerate(deps):
        p = TMP / f"_map_probe{i}.json"
        p.write_text(json.dumps(d, ensure_ascii=False), encoding="utf-8")
        paths.append(p)
    rp = TMP / "_map_probe.wrp"
    war_run(mf, paths, ["-"] * len(paths), replay=rp, secs=0.1)
    meta, frames, events, result = read_replay(rp)
    kinds = kinds_of(meta)
    probe_ids = {o["id"] for o in meta["objects"] if o.get("type") == "unit" and o.get("seat", -1) >= 0}
    objs = [(oid, ot, st, team, x, z, aux) for (oid, ot, st, fl, team, hp, aux, x, y, z) in frames[0][1] if oid not in probe_ids]
    nm = sc["name"]
    draw(sc, seat, objs, kinds, me_team=seat if sc.get("mode") != "level" else 0, title=f"== {nm}（{seat} 号位视角）==")
    if dep.exists():
        dep.unlink()
    fr = Frame(seats[seat]["origin"], seats[seat].get("yaw", 0))
    st = seats[seat]
    out(f"预算 {st['budget']:g}，人数上限 {st['max_units']}，时限 {sc['time_limit']:g} 秒。布阵区（我方视角，[x0, z0, x1, z1]）：{st['deploy']}")
    if "lord" in st:
        lx, lz = fr.to_local(st["lord"]["x"], st["lord"]["z"])
        out(f"主帅起点：x={lx:.1f} z={lz:.1f}")
    for zn in sc.get("zones", []) or []:
        if "rect" in zn:
            r = zn["rect"]
            a, b2 = fr.to_local(r[0], r[1]), fr.to_local(r[2], r[3])
            out(f"区域 {zn['name']}：x {min(a[0], b2[0]):.1f}～{max(a[0], b2[0]):.1f}，z {min(a[1], b2[1]):.1f}～{max(a[1], b2[1]):.1f}" +
                (f"（速度 × {zn['speed_mul']:g}）" if zn.get("speed_mul") else ""))
        else:
            cx, cz, rr = zn["circle"]
            a = fr.to_local(cx, cz)
            out(f"区域 {zn['name']}：圆心 x={a[0]:.1f} z={a[1]:.1f}，半径 {rr:g}")
    # 敌军和道具清单
    pos0 = {oid: (x, z) for (oid, ot, st_, fl, team, hp, aux, x, y, z) in frames[0][1]}
    groups = collections.OrderedDict()
    for ob in meta["objects"]:
        if ob["id"] in probe_ids or ob["id"] not in pos0:
            continue
        lx, lz = fr.to_local(*pos0[ob["id"]])
        if ob.get("type") == "unit":
            key = ("敌军", KIND_ZH.get(ob["kind"], ob["kind"]), ob.get("tag") or "")
        else:
            key = ("道具", PROP_ZH.get(ob["kind"], ob["kind"]), ob.get("tag") or "")
        groups.setdefault(key, []).append((lx, lz, ob))
    out("开场位置（我方视角坐标）：")
    for (cat, k, tg), lst in groups.items():
        xs, zs = [p[0] for p in lst], [p[1] for p in lst]
        where = f"x {min(xs):.1f}～{max(xs):.1f}，z {min(zs):.1f}～{max(zs):.1f}" if len(lst) > 1 else f"x {xs[0]:.1f}，z {zs[0]:.1f}"
        out(f"  {cat} {k}{('[' + tg + ']') if tg else ''} × {len(lst)}：{where}")
    out("（敌军的行为写在关卡文件 units[].behavior 和 RULES.md 里。编号用 python play.py replay 或程序里的 g.units 查。）")
    return 0


def cmd_replay(args):
    if not args:
        fail("用法：python play.py replay 回放.wrp [--seat K] [--at 10,20] [--deaths]")
    o, rest = parse_opts(args, ("--seat", "--at"))
    path = Path(rest[0])
    if not path.is_file() and (HERE / rest[0]).is_file():
        path = HERE / rest[0]
    meta, frames, events, result = read_replay(path)
    sc = json.loads(meta["scenario"])
    mode = meta["mode"]
    seat = int(o.get("--seat", 0))
    me = seat if mode != "level" else 0
    dt = meta["dt"]
    objs = {ob["id"]: ob for ob in meta["objects"]}
    units = {k: v for k, v in objs.items() if v.get("type") == "unit"}
    team_names = {}
    for s in meta.get("seats", []):
        team_names[s["team"]] = s.get("name", "")
    def tname(t):
        if t == me:
            return "我方"
        if mode == "level":
            return "敌方"
        return f"{t}号位({team_names.get(t, '')})"
    out(f"== {meta['map_name']}（{mode}），从 {seat} 号位看 ==")
    if result:
        if mode == "level":
            out(f"结果：{'过关' if result.get('passed') else '没过'}，{REASON_ZH.get(result['reason'], result['reason'])}，第 {result['secs']:.1f} 秒结束")
        else:
            w = result["winner"]
            out(f"结果：{'平局' if w < 0 else tname(w) + '胜'}，{REASON_ZH.get(result['reason'], result['reason'])}，第 {result['secs']:.1f} 秒结束")
    # 每 10 秒点一次名
    nteam = max([u.get("team", 0) for u in units.values()] + [0]) + 1
    alive_at = {}
    dead_step = {}
    for e in events:
        if e["t"] == "death":
            dead_step[e["u"]] = e["s"]
    end_step = result["steps"] if result and "steps" in result else (frames[-1][0] if frames else 0)
    out("兵力（活着的人数）：")
    marks = list(range(0, int(end_step * dt) + 1, 10)) + [round(end_step * dt, 1)]
    for t in marks:
        s = t / dt
        cnt = [0] * nteam
        for uid, u in units.items():
            if u.get("kind") == "lord":
                continue
            if dead_step.get(uid, 10 ** 9) > s:
                cnt[u.get("team", 0)] += 1
        out(f"  第 {t:>5g} 秒：" + "  ".join(f"{tname(k)} {cnt[k]}" for k in range(nteam)))
    # 关键事件
    out("关键事件：")
    shown = 0
    boss_names = {0: "游走", 1: "刨地", 2: "冲撞", 3: "晕"}
    rally_sortie_step = None
    for e in events:
        t = e["s"] * dt
        line = None
        if e["t"] == "terrain_removed":
            line = f"地形「{e['tag']}」塌了"
        elif e["t"] == "release":
            line = f"{label(objs.get(e['u'], {'id': e['u'], 'kind': 'snowball'}))} 被 {label(units[e['by']]) if e.get('by', -1) in units else '?'} 推下去了"
        elif e["t"] == "destroyed":
            line = f"{PROP_ZH.get(e.get('kind'), e.get('kind'))}#{e['u']}{('[' + e['tag'] + ']') if e.get('tag') else ''} 没了（{e.get('why', '')}）"
        elif e["t"] == "explode":
            src = objs.get(e.get("src"))
            line = f"爆炸：{label(src) if src else '#' + str(e.get('src'))}"
        elif e["t"] == "boss_state":
            line = f"镇山兽 → {boss_names.get(e['state'], e['state'])}" + (f"（方向 {e['dx']:.2f},{e['dz']:.2f}，世界坐标）" if e["state"] == 2 else "")
        elif e["t"] == "boss_stun":
            line = "镇山兽撞上东西，晕了"
        elif e["t"] == "pillar_hit":
            line = f"立石#{e['u']} 被撞第 {e.get('hits')} 次"
        elif e["t"] == "sortie" and rally_sortie_step == e["s"]:
            line = None  # 集结的敌军挨打提前出发：上一条 rally_go 已经讲了
        elif e["t"] == "sortie":
            by = units.get(e.get("by"))
            hit = units.get(e.get("u"))
            line = (f"守军「{e.get('squad') or (label(hit) if hit else '#' + str(e.get('u')))}」出击（{e.get('n', '?')} 人）："
                    f"{label(hit) if hit else '#' + str(e.get('u'))} 挨了{(tname(by.get('team')) + '的 ' + label(by)) if by else '?'} 够不着他的远程攻击")
        elif e["t"] == "rally_go":
            if e.get("why") == "sortie":
                rally_sortie_step = e["s"]
            why = {"gathered": "人到齐了", "time": "等够了时间", "sortie": "集结时挨了够不着的远程攻击"}.get(e.get("why"), e.get("why"))
            line = f"桥断后改道、在山口集结的敌军出发（{e.get('n', '?')} 人，{why}）"
        elif e["t"] == "boss_fall":
            line = "镇山兽冲出台地边缘"
        elif e["t"] == "eliminated":
            line = f"{tname(e['team'])} 全灭"
        elif e["t"] == "fault":
            line = f"{e.get('seat')}号位的程序出问题：{e.get('kind')} {e.get('detail', '')}" + ("（程序被结束，之后每个兵继续执行最后收到的指令）" if e.get("fatal") else "")
        elif e["t"] == "death" and units.get(e["u"], {}).get("kind") in ("lord", "boss", "catapult", "giant"):
            u = units[e["u"]]
            k = units.get(e.get("killer"))
            line = f"{tname(u.get('team', 0))}的 {label(u)} 阵亡：{WEAPON_ZH.get(e['cause'], e['cause'])}" + (f"，最后一击 {tname(k.get('team'))}的 {label(k)}" if k else "")
        elif e["t"] == "end":
            line = "结束"
        if line:
            if e["t"] == "explode" and shown > 60:
                continue
            out(f"  {t:6.1f} 秒  {line}")
            shown += 1
    # 我方伤亡
    mine = {uid: u for uid, u in units.items() if u.get("team") == me}
    lost = collections.Counter()
    killers = collections.Counter()
    ff = 0
    deaths = []
    for e in events:
        if e["t"] != "death" or e["u"] not in mine:
            continue
        k = units.get(e.get("killer"))
        lost[WEAPON_ZH.get(e["cause"], e["cause"])] += 1
        if k:
            killers[f"{tname(k.get('team'))}的{KIND_ZH.get(k['kind'], k['kind'])}"] += 1
            if k.get("team") == me:
                ff += 1
        deaths.append((e["s"] * dt, mine[e["u"]], e["cause"], k))
    n_mine = sum(1 for u in mine.values() if u.get("kind") != "lord")
    out(f"我方 {n_mine} 个兵，阵亡 {len(deaths)}：" + ("，".join(f"{k} {v}" for k, v in lost.most_common()) or "无"))
    if killers:
        out("  打死我方的：" + "，".join(f"{k} {v}" for k, v in killers.most_common(8)) + (f"（其中自己人误伤致死 {ff}）" if ff else ""))
    kills = collections.Counter()
    for e in events:
        if e["t"] == "death" and e.get("kteam") == me and units.get(e["u"], {}).get("team") != me:
            k = units.get(e.get("killer"))
            if k:
                kills[KIND_ZH.get(k["kind"], k["kind"])] += 1
    if kills:
        out("  我方谁杀的人：" + "，".join(f"{k} {v}" for k, v in kills.most_common()))
    hits_ff = sum(1 for e in events if e["t"] == "hit" and e.get("ff") and units.get(e.get("a"), {}).get("team") == me)
    if hits_ff:
        out(f"  我方误伤自己人 {hits_ff} 次（箭、石头、爆炸、雪球不认人）")
    tags = collections.OrderedDict()
    for uid, u in sorted(mine.items()):
        tags.setdefault(u.get("tag") or "（没标签）", []).append(uid)
    out("按标签：")
    for tg, ids in tags.items():
        d = [dead_step[i] * dt for i in ids if i in dead_step]
        out(f"  {tg}：{len(ids)} 人，阵亡 {len(d)}" + (f"（第 {min(d):.1f}～{max(d):.1f} 秒）" if d else ""))
    if "--deaths" in rest:
        out("我方逐个阵亡：")
        for t, u, cause, k in deaths:
            out(f"  {t:6.1f} 秒  {label(u)}  {WEAPON_ZH.get(cause, cause)}" + (f"  ← {tname(k.get('team'))}的 {label(k)}" if k else ""))
    if "--at" in o:
        kinds = kinds_of(meta)
        for t in [float(v) for v in o["--at"].split(",")]:
            target = t / dt
            step, fobjs = min(frames, key=lambda f: abs(f[0] - target))
            objs2 = [(oid, ot, st, team, x, z, aux) for (oid, ot, st, fl, team, hp, aux, x, y, z) in fobjs]
            draw(sc, seat, objs2, kinds, me_team=me, title=f"-- 第 {step * dt:.1f} 秒 --", deploy=False)
    out("（--at 10,20 画某几秒的战场；--deaths 列出我方每个人怎么死的）")
    return 0


# ====================================================================== 入口
def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass
    if not WAR.is_file():
        fail("找不到 war.exe")
    a = sys.argv[1:]
    if not a or a[0] in ("-h", "--help", "help"):
        out(__doc__)
        return 0
    cmds = {"check": cmd_check, "level": cmd_level, "levels": cmd_levels, "duel": cmd_duel, "melee": cmd_melee,
            "map": cmd_map, "replay": cmd_replay}
    if a[0] not in cmds:
        fail(f"不认识的命令 {a[0]}。\n" + __doc__)
    return cmds[a[0]](a[1:])


if __name__ == "__main__":
    sys.exit(main() or 0)
