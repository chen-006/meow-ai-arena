"""GitHub Actions 用的几个小工具。这里的代码都不编译、不运行投稿。

  python community/ci_tools.py scope --repo pr --base HEAD^1 --head HEAD     PR 改了哪些文件，是不是一份合规的投稿
  python community/ci_tools.py plan --before <sha> --after <sha>             这次推送要重测哪些投稿
  python community/ci_tools.py collect --artifacts <目录> --plan <json>      把评测结果核对后存进 community/results/
  python community/ci_tools.py render                                        重新生成 community/LEADERBOARD.md

本地也可以运行 render 看榜单长什么样。
"""
import argparse
import json
import math
import os
import statistics
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import check  # noqa: E402  只用里面的规则和结构检查，不调用评测

RESULTS = HERE / "results"
BOARD = HERE / "LEADERBOARD.md"
MAINTAINERS = ("OWNER", "MEMBER", "COLLABORATOR")
GENERATED = ("community/LEADERBOARD.md", "community/results/")


def git(repo, *args):
    r = subprocess.run(["git", "-C", str(repo), "-c", "core.quotepath=false", *args], capture_output=True)
    if r.returncode != 0:
        raise SystemExit("git %s 失败：%s" % (" ".join(args), r.stderr.decode("utf-8", "replace")))
    return r.stdout


def set_output(**kv):
    path = os.environ.get("GITHUB_OUTPUT")
    for k, v in kv.items():
        print("%s=%s" % (k, v))
        if path:
            with open(path, "a", encoding="utf-8") as f:
                f.write("%s=%s\n" % (k, v))


def say(text):
    print(text)
    path = os.environ.get("GITHUB_STEP_SUMMARY")
    if path:
        with open(path, "a", encoding="utf-8") as f:
            f.write(text + "\n\n")


def entry_of(path):
    """community/<集>/<目录>/... -> (集, 目录)；别的路径返回 None。"""
    parts = path.split("/")
    if len(parts) >= 4 and parts[0] == "community" and parts[1] in check.EPISODES:
        return parts[1], parts[2]
    return None


def changed_paths(repo, base, head):
    # -z：路径之间用 NUL 分隔，中文和带空格的路径不会被加引号转义；--no-renames：改名按“删一个加一个”算
    raw = git(repo, "diff", "--name-status", "-z", "--no-renames", base, head).decode("utf-8", "replace")
    tokens = [t for t in raw.split("\0") if t]
    return [tokens[i + 1] for i in range(0, len(tokens) - 1, 2)]


def all_entries(root=ROOT):
    found = []
    for ep in check.EPISODES:
        d = root / "community" / ep
        if d.is_dir() and not check.has_symlink(d):
            for p in sorted(d.iterdir()):
                if not check.has_symlink(p) and p.is_dir() and check.ENTRY_RE.match(p.name) and (p / "meta.json").is_file():
                    found.append((ep, p.name))
    return found


# ---------------------------------------------------------------- scope：PR 的改动范围

def cmd_scope(args):
    paths = changed_paths(args.repo, args.base, args.head)
    author = (args.author or "").lower()
    maintainer = args.association in MAINTAINERS
    entries = sorted({e for e in map(entry_of, paths) if e})
    others = [p for p in paths if not entry_of(p)]
    generated = [p for p in others if p.startswith(GENERATED)]

    def fail(message):
        say("### 改动范围检查没有通过\n\n" + message)
        set_output(kind="invalid", entry="")
        sys.exit(1)

    def listing(items, limit=20):
        return "\n".join("- `%s`" % p.replace("`", "'") for p in items[:limit]) + ("\n- ……" if len(items) > limit else "")

    if generated and not maintainer:
        fail("`community/LEADERBOARD.md` 和 `community/results/` 是合并后自动生成的，投稿不要改它们：\n\n" + listing(generated))
    if not entries:
        say("这个 PR 没有改任何投稿目录，不运行投稿评测，按普通 PR 由维护者审。")
        set_output(kind="other", entry="")
        return
    if len(entries) > 1 or others:
        if maintainer:
            say("这个 PR 来自维护者，改动不限于一份投稿，不运行投稿评测。")
            set_output(kind="other", entry="")
            return
        message = "一个投稿 PR 只能新增或修改 `community/<集>/<你的目录>/` 这一个文件夹里的文件。"
        if len(entries) > 1:
            message += "\n\n这个 PR 动了 %d 个投稿目录：\n\n" % len(entries) + listing(["community/%s/%s/" % e for e in entries])
        if others:
            message += "\n\n这个 PR 还改了投稿目录之外的文件：\n\n" + listing(others)
        message += "\n\n请把别的改动拆到另一个 PR 里。想改文档或脚本的 PR 不要和投稿放在一起。"
        fail(message)
    ep, name = entries[0]
    m = check.ENTRY_RE.match(name)
    if not m:
        fail("目录名 `%s` 不符合 `<GitHub用户名>__<作品名>`（两段都只能用小写字母、数字、连字符）。" % name.replace("`", "'"))
    if m["user"] != author and not maintainer:
        fail("目录名里的用户名是 `%s`，开 PR 的账号是 `%s`。只能新增或修改自己名下的投稿目录。" % (m["user"], author))
    rel = "community/%s/%s" % (ep, name)
    if not (Path(args.repo) / rel).is_dir():
        say("这个 PR 删除了投稿 `%s`，不运行评测。" % rel)
        set_output(kind="removal", entry=rel)
        return
    say("投稿目录：`%s`（%s）" % (rel, check.EPISODE_TITLES[ep]))
    set_output(kind="submission", entry=rel, episode=ep)


# ---------------------------------------------------------------- plan：推送后要重测哪些投稿

def cmd_plan(args):
    wanted = (args.entries or "").strip()
    before = (args.before or "").strip()
    known = all_entries()
    if wanted == "all":
        todo, why = known, "手动触发：全部重测"
    elif wanted:
        asked = {tuple(x.strip("/").split("/")[-2:]) for x in wanted.split()}
        todo, why = [e for e in known if e in asked], "手动触发：指定的投稿"
    else:
        ok = bool(before) and set(before) != {"0"} and subprocess.run(
            ["git", "-C", str(ROOT), "cat-file", "-e", before + "^{commit}"], capture_output=True).returncode == 0
        if ok:
            touched = {e for e in map(entry_of, changed_paths(ROOT, before, args.after)) if e}
            todo, why = [e for e in known if e in touched], "只重测这次推送改动过的投稿"
        else:
            todo, why = known, "找不到上一次推送的提交，全部重测"
    include = [{"episode": ep, "entry": name} for ep, name in todo]
    say("%s，共 %d 份：%s" % (why, len(include), "、".join("`%s/%s`" % (x["episode"], x["entry"]) for x in include) or "无"))
    set_output(count=len(include), matrix=json.dumps({"include": include}, ensure_ascii=True))


# ---------------------------------------------------------------- collect：核对评测结果

def clean(x, depth=0):
    """评测任务运行过投稿代码，它交回来的 JSON 一律当作不可信的数据：只留下类型和大小都在限度内的内容。"""
    if depth > 6:
        return None
    if x is None or isinstance(x, bool):
        return x
    if isinstance(x, int):
        return x if abs(x) < 10 ** 12 else None
    if isinstance(x, float):
        return x if math.isfinite(x) and abs(x) < 1e12 else None
    if isinstance(x, str):
        return "".join(ch if ch.isprintable() else " " for ch in x)[:300]
    if isinstance(x, list):
        return [clean(v, depth + 1) for v in x[:64]]
    if isinstance(x, dict):
        return {clean(str(k)): clean(v, depth + 1) for k, v in list(x.items())[:64]}
    return None


def is_num(x):
    return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x)


def as_list(value):
    return value if isinstance(value, list) else []


def collect_one(ep, name, artifacts, commit):
    entry_dir = HERE / ep / name
    if ep not in check.EPISODES or not check.ENTRY_RE.match(name) or check.has_symlink(entry_dir) or not entry_dir.is_dir():
        return None, "仓库里已经没有这个目录"
    meta, errors, _, files = check.validate(entry_dir, ep)     # 只读文件，不运行任何东西
    rec = {"schema": check.SCHEMA, "episode": ep, "entry": name, "status": "missing", "score": None, "files": files,
           "commit": commit, "mode": "", "errors": [], "detail": {}, "env": {}, "time": ""}
    path = Path(artifacts) / ("result-%s-%s" % (ep, name)) / "result.json"
    if check.has_symlink(path) or not path.is_file():
        rec["errors"] = ["评测任务没有交回结果（可能是超时或中途失败），见这次工作流的日志"]
        return rec, "没有结果文件"
    if path.stat().st_size > 512 * 1024:
        rec["errors"] = ["评测结果文件过大，没有采用"]
        return rec, "结果文件过大"
    try:
        raw = clean(json.loads(path.read_text(encoding="utf-8")))
    except (ValueError, UnicodeDecodeError):
        raw = None
    if not isinstance(raw, dict) or raw.get("episode") != ep or raw.get("entry") != name:
        rec["errors"] = ["评测结果文件读不懂，或者不是这份投稿的结果"]
        return rec, "结果文件不合格"
    if raw.get("files") != files:
        return None, "结果对应的代码和仓库里现在的不一样（评测之后又有新的提交），等下一次评测"
    status, score, detail = raw.get("status"), raw.get("score"), raw.get("detail")
    detail = detail if isinstance(detail, dict) else {}
    rec["status"] = "pass" if status == "pass" and is_num(score) and not errors else "fail"
    rec["score"] = float(score) if rec["status"] == "pass" else None
    rec["mode"] = raw.get("mode") if raw.get("mode") in ("quick", "full") else ""
    rec["errors"] = [e for e in as_list(raw.get("errors")) if isinstance(e, str)][:20] + errors[:20]
    rec["time"] = raw.get("time") if isinstance(raw.get("time"), str) else ""
    env = raw.get("env") if isinstance(raw.get("env"), dict) else {}
    rec["env"] = {k: env.get(k) for k in ("platform", "python", "compiler", "cpus") if isinstance(env.get(k), (str, int))}
    if ep == "warehouse":
        rows = [r for r in as_list(detail.get("workloads")) if isinstance(r, dict)]
        rec["detail"] = {"correct": detail.get("correct") if isinstance(detail.get("correct"), int) else 0,
                         "total": detail.get("total") if isinstance(detail.get("total"), int) else 0,
                         "workloads": [{k: r.get(k) for k in ("name", "set", "status", "baseline_cpu", "cpu", "speedup", "runs", "mem_mb")}
                                       for r in rows]}
    else:
        field = [r for r in as_list(detail.get("field")) if isinstance(r, dict) and isinstance(r.get("name"), str)]
        rec["detail"] = {"rank": detail.get("rank") if isinstance(detail.get("rank"), int) else None,
                         "field_size": len(field), "games_note": detail.get("games_note") if isinstance(detail.get("games_note"), str) else "",
                         "ci95": detail.get("ci95") if isinstance(detail.get("ci95"), list) and all(map(is_num, detail.get("ci95"))) else None,
                         "field": [{"name": r["name"], "score": r.get("score") if is_num(r.get("score")) else None}
                                   for r in field if r.get("is_entry") is not True]}
        if ep == "tank":
            budget = detail.get("budget") if isinstance(detail.get("budget"), dict) else {}
            opponents = detail.get("opponents") if isinstance(detail.get("opponents"), list) else []
            expected_games = {"quick": 320, "full": 1280}.get(rec["mode"])
            if rec["status"] == "pass" and (
                    detail.get("rating_method") != "fixed-final" or budget.get("budget_kind") != "cpu" or
                    type(budget.get("first_ms")) is not int or budget.get("first_ms") != 2000 or
                    type(budget.get("turn_ms")) is not int or budget.get("turn_ms") != 50 or
                    type(detail.get("games")) is not int or detail.get("games") != expected_games):
                rec["status"], rec["score"] = "fail", None
                rec["errors"].append("坦克结果缺少完整的正式 CPU 预算、固定参照评分或对应档位的局数")
            rec["detail"].update(
                rating_method=detail.get("rating_method") if detail.get("rating_method") == "fixed-final" else None,
                games=detail.get("games") if type(detail.get("games")) is int else None,
                budget={"budget_kind": budget.get("budget_kind") if budget.get("budget_kind") in ("cpu", "wall") else None,
                        **{k: budget.get(k) if type(budget.get(k)) is int else None for k in ("first_ms", "turn_ms")}},
                opponents=[{"name": r["name"], "display": r.get("display") if isinstance(r.get("display"), str) else "",
                            "rating": r.get("rating") if is_num(r.get("rating")) else None,
                            **{k: r.get(k) if type(r.get(k)) is int and 0 <= r[k] <= 80 else None
                               for k in ("wins", "draws", "losses")}}
                           for r in opponents if isinstance(r, dict) and isinstance(r.get("name"), str)])
    if ep == "tron" and rec["status"] == "pass":
        try:
            from tron_contract import validate_result
            field, verified = validate_result(detail, ROOT)
            if (verified["source_sha256"] != files.get("bot.cpp") or
                    verified["full"] is not (rec["mode"] == "full") or rec["mode"] not in ("quick", "full") or
                    not math.isclose(rec["score"], round(verified["rating"], 1), abs_tol=1e-7)):
                raise ValueError("源码、档位或加权分与投稿不符")
            rec["detail"] = dict(verified, field=field, field_size=len(field))
            rec["score"] = round(verified["rating"], 1)
        except (OSError, ValueError, KeyError, TypeError, IndexError, AttributeError, OverflowError) as error:
            rec["status"], rec["score"] = "fail", None
            rec["errors"].append("光轮结果未通过固定参照与赛程复核：%s" % error)
    if ep in ("soccer", "boomerang") and rec["status"] == "pass":
        try:
            field, verified = check.load_module("trusted_%s_challenge" % ep, ROOT / ep / "challenge.py") \
                .validate_result(detail, ROOT)
            if (verified["source_sha256"] != files.get("bot.cpp") or
                    verified["full"] is not (rec["mode"] == "full") or rec["mode"] not in ("quick", "full") or
                    not math.isclose(rec["score"], round(verified["rating"], 1), abs_tol=1e-7)):
                raise ValueError("源码、档位或实力值与投稿不符")
            rec["detail"] = dict(verified, field=field, field_size=len(field))
            rec["score"] = round(verified["rating"], 1)
        except (OSError, ValueError, KeyError, TypeError, IndexError, AttributeError, OverflowError) as error:
            rec["status"], rec["score"] = "fail", None
            rec["errors"].append("%s结果未通过固定参照与赛程复核：%s" % (check.EPISODE_TITLES[ep], error))
    return rec, "已采用（%s）" % rec["status"]


def cmd_collect(args):
    plan = json.loads(args.plan).get("include", [])
    for item in plan:
        ep, name = str(item.get("episode")), str(item.get("entry"))
        if ep not in check.EPISODES:
            continue
        rec, note = collect_one(ep, name, args.artifacts, args.commit)
        print("%s/%s：%s" % (ep, name, note))
        if rec:
            (RESULTS / ep).mkdir(parents=True, exist_ok=True)
            (RESULTS / ep / (name + ".json")).write_text(
                json.dumps(rec, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


# ---------------------------------------------------------------- render：生成榜单

WHO = {"human": "人写", "ai": "AI 写", "human+ai": "人和 AI 合写"}
E = check.md_escape


def load_board(ep):
    """返回 (有成绩的, 没成绩的)。每项是 (目录名, meta, 结果或 None, 原因)。"""
    ranked, unranked = [], []
    for e, name in all_entries():
        if e != ep:
            continue
        meta, errors, _, files = check.validate(HERE / ep / name, ep)
        if meta is None:
            continue
        rec, path = None, RESULTS / ep / (name + ".json")
        if path.is_file():
            try:
                rec = json.loads(path.read_text(encoding="utf-8"))
            except ValueError:
                rec = None
        if not isinstance(rec, dict):
            unranked.append((name, meta, None, "还没有评测"))
        elif rec.get("files") != files:
            unranked.append((name, meta, None, "代码有更新，等待重新评测"))
        elif rec.get("status") != "pass" or not is_num(rec.get("score")):
            reason = "；".join(str(x) for x in (rec.get("errors") or [])[:3]) or "评测没有通过"
            unranked.append((name, meta, rec, reason))
        else:
            ranked.append((name, meta, rec, ""))
    ranked.sort(key=lambda x: (-x[2]["score"], x[0]))
    return ranked, unranked


def who_cells(ep, name, meta):
    user = name.split("__")[0]
    tools = " / ".join(x for x in (meta.get("model"), meta.get("tool")) if x) or "—"
    return "[%s](%s/%s/) | %s（[@%s](https://github.com/%s)） | %s | %s" % (
        E(meta["name"]), ep, name, E(meta["author"]), user, user, WHO.get(meta["written_by"], "—"), E(tools))


def cmd_render(args):
    L = ["# 社区榜", "",
         "这个文件由 `community/ci_tools.py render` 自动生成，投稿合并后由 GitHub Actions 更新，请不要手改。怎么投稿见 [README.md](README.md)。", "",
         "成绩是在 GitHub 提供的虚拟机上测的：bot 类每份投稿只和官方选手打一轮几百局的小比赛，仓储调度的加速比是同一台虚拟机上基线和投稿的 CPU 时间之比。"
         "不同投稿不一定在同一台虚拟机上测，局数也少，名次相近的不必当真；**这些数字和节目里的正式成绩不能直接比。**", ""]
    for ep in check.EPISODES:
        ranked, unranked = load_board(ep)
        L += ["## %s（`%s/`）" % (check.EPISODE_TITLES[ep], ep), ""]
        if not ranked and not unranked:
            L += ["还没有投稿。", ""]
            continue
        if ranked and ep == "warehouse":
            L += ["按几何平均加速比排序，只列全部负载输出都正确的投稿。", "",
                  "| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 几何平均加速比 | 输出正确 | 负载 | 评测日期 |", "|---:|---|---|---|---|---:|---:|---|---|"]
            for i, (name, meta, rec, _) in enumerate(ranked, 1):
                d = rec.get("detail") or {}
                L.append("| %d | %s | %.1f 倍 | %s / %s | %s | %s |" % (
                    i, who_cells(ep, name, meta), rec["score"], d.get("correct", "—"), d.get("total", "—"),
                    "8 组隐藏负载" if rec.get("mode") == "full" else "快速档", E(str(rec.get("time", ""))[:10])))
            L += ["", "官方选手的第 1 轮成绩见 [`warehouse/results/第1轮_速度成绩.md`](../warehouse/results/第1轮_速度成绩.md)，"
                  "那是在比赛电脑上测的，评测脚本不会在虚拟机上重测它们，所以这里不列参照。", ""]
        elif ranked and ep == "tank":
            L += ["按投稿实力值排序：正式模式挑战 14 位 AI、出题方参照和基准，固定终场赛对手评分，只拟合投稿的 Bradley–Terry 实力值（基准 = 0，强 10 倍 = +400）。", "",
                  "“终场赛参照名次”只把投稿分数放进 14 位 AI 的固定评分中比较，不是重跑正式赛。", "",
                  "| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 实力值 | 终场赛参照名次 | 评测日期 |", "|---:|---|---|---|---|---:|---|---|"]
            for i, (name, meta, rec, _) in enumerate(ranked, 1):
                d = rec.get("detail") or {}
                L.append("| %d | %s | %.0f | %s / 15 | %s |" % (
                    i, who_cells(ep, name, meta), rec["score"], d.get("rank") or "—", E(str(rec.get("time", ""))[:10])))
            L += ["", "固定参照来自 [`tank/results/final/成绩.md`](../tank/results/final/成绩.md)。"
                  "社区对局使用新地图，在不同机器上运行，局数较少；参照分数不是本次在虚拟机上重新测出的官方成绩。", ""]
        elif ranked and ep == "tron":
            L += ["按加权实力值排序：Windows 正式模式，固定终场强对手榜各局型评分，只拟合投稿（豆包 = 0，1v1 / 4人 / 12人权重 50 / 30 / 20%）。", "",
                  "13 个对手包含 10 位正式选手、2 位参考选手和 sparring_hard；陪练参与拟合但不占参照名次。", "",
                  "| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 加权实力值 | 强对手榜参照名次 | 评测日期 |", "|---:|---|---|---|---|---:|---|---|"]
            for i, (name, meta, rec, _) in enumerate(ranked, 1):
                d = rec.get("detail") or {}
                L.append("| %d | %s | %.0f | %s / 13 | %s |" % (
                    i, who_cells(ep, name, meta), rec["score"], d.get("rank") or "—", E(str(rec.get("time", ""))[:10])))
            L += ["", "固定参照来自 [`tron/results/RESULTS_强对手榜.md`](../tron/results/RESULTS_强对手榜.md)。"
                  "默认 82 局（52 / 24 / 6），参照名次只与 12 位选手比较；机器、地图、局数不同，不能当作正式赛名次。", ""]
        elif ranked and ep == "soccer":
            L += ["按实力值排序：Windows 正式计时，投稿同时踢本队两个座位，逐一对踢个人赛的 12 支个人队和 3 支参考队；"
                  "固定个人榜的 BT 参照分，只拟合投稿（基准队 = 0，强 10 倍 = +400）。参考队参加拟合、不占名次。", "",
                  "| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 实力值 | 个人榜参照名次 | 评测日期 |", "|---:|---|---|---|---|---:|---|---|"]
            for i, (name, meta, rec, _) in enumerate(ranked, 1):
                d = rec.get("detail") or {}
                L.append("| %d | %s | %.0f | %s / 13 | %s |" % (
                    i, who_cells(ep, name, meta), rec["score"], d.get("rank") or "—", E(str(rec.get("time", ""))[:10])))
            L += ["", "固定参照来自 [`soccer/results/个人榜.md`](../soccer/results/个人榜.md)。"
                  "默认 120 局（15 个对手 × 4 个种子 × 换边），种子和正式赛不同；参照名次只和 12 支个人队比较，不能当作正式名次。", ""]
        elif ranked and ep == "boomerang":
            L += ["按个人实力值排序：Windows 正式计时，和海选的 13 位选手打一对一和四人局；固定海选成绩里两项的参照分，"
                  "只拟合投稿，两项平均（13 位选手平均 = 0，强 10 倍 = +400），和海选个人榜同一口径。", "",
                  "| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 个人实力值 | 一对一 | 四人局 | 海选参照名次 | 评测日期 |",
                  "|---:|---|---|---|---|---:|---:|---:|---|---|"]
            for i, (name, meta, rec, _) in enumerate(ranked, 1):
                d = rec.get("detail") or {}
                r = d.get("ratings") if isinstance(d.get("ratings"), dict) else {}
                cell = lambda v: "%.0f" % v if is_num(v) else "—"
                L.append("| %d | %s | %.0f | %s | %s | %s / 14 | %s |" % (
                    i, who_cells(ep, name, meta), rec["score"], cell(r.get("duel")), cell(r.get("ffa")), d.get("rank") or "—",
                    E(str(rec.get("time", ""))[:10])))
            L += ["", "固定参照来自 [`boomerang/results/海选榜.md`](../boomerang/results/海选榜.md)。"
                  "默认 416 局（一对一 312、四人局 104），种子和海选不同；参照名次只和 13 位选手比较，不能当作正式名次。", ""]
        elif ranked:
            label = "单挑、混战两种赛制的 Bradley–Terry 实力值取平均，greedy 基准 = 0" if ep == "land" else "Bradley–Terry，基准 = 0"
            L += ["按实力值排序（%s，强 10 倍记 +400）。“小比赛名次”是这份投稿在它自己那一轮小比赛里排第几。" % label, "",
                  "| 名次 | 作品 | 作者 | 谁写的 | 模型 / 工具 | 实力值 | 小比赛名次 | 评测日期 |", "|---:|---|---|---|---|---:|---|---|"]
            for i, (name, meta, rec, _) in enumerate(ranked, 1):
                d = rec.get("detail") or {}
                L.append("| %d | %s | %.0f | %s / %s | %s |" % (
                    i, who_cells(ep, name, meta), rec["score"], d.get("rank") or "—", d.get("field_size") or "—",
                    E(str(rec.get("time", ""))[:10])))
            seen = {}
            for _, _, rec, _ in ranked:
                for r in (rec.get("detail") or {}).get("field") or []:
                    if isinstance(r, dict) and is_num(r.get("score")):
                        seen.setdefault(str(r.get("name")), []).append(r["score"])
            if seen:
                L += ["", "官方选手参照：每份投稿评测时，官方选手和基准也在同一台虚拟机上打了同一轮小比赛。下表是它们在各次评测里实力值的中位数。", "",
                      "| 官方选手 | 实力值（中位数） | 出现在几次评测里 |", "|---|---:|---:|"]
                for n, v in sorted(seen.items(), key=lambda kv: -statistics.median(kv[1])):
                    L.append("| %s | %.0f | %d |" % (E(n), statistics.median(v), len(v)))
            L.append("")
        if unranked:
            L += ["没有成绩的投稿：", ""]
            for name, meta, _, reason in unranked:
                L.append("- [%s](%s/%s/)（@%s）：%s" % (E(meta["name"]), ep, name, name.split("__")[0], E(reason, 300)))
            L.append("")
    # 目录已经删掉的投稿，把它的结果文件也删掉
    alive = set(all_entries())
    for ep in check.EPISODES:
        if (RESULTS / ep).is_dir():
            for p in (RESULTS / ep).glob("*.json"):
                if (ep, p.stem) not in alive:
                    p.unlink()
                    print("删除过期结果：%s/%s" % (ep, p.name))
    BOARD.write_text("\n".join(L).rstrip("\n") + "\n", encoding="utf-8", newline="\n")
    print("已生成 %s" % BOARD)


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("scope")
    p.add_argument("--repo", required=True)
    p.add_argument("--base", required=True)
    p.add_argument("--head", required=True)
    p.add_argument("--author", default=os.environ.get("PR_AUTHOR", ""))
    p.add_argument("--association", default=os.environ.get("PR_ASSOCIATION", ""))
    p.set_defaults(fn=cmd_scope)
    p = sub.add_parser("plan")
    p.add_argument("--before", default=os.environ.get("PUSH_BEFORE", ""))
    p.add_argument("--after", default="HEAD")
    p.add_argument("--entries", default=os.environ.get("PLAN_ENTRIES", ""))
    p.set_defaults(fn=cmd_plan)
    p = sub.add_parser("collect")
    p.add_argument("--artifacts", required=True)
    p.add_argument("--plan", default=os.environ.get("PLAN_MATRIX", '{"include": []}'))
    p.add_argument("--commit", default=os.environ.get("EVAL_SHA", ""))
    p.set_defaults(fn=cmd_collect)
    p = sub.add_parser("render")
    p.set_defaults(fn=cmd_render)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
