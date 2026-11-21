"""社区投稿自测：检查一份投稿的结构，调用对应那一集的评测，输出 result.json 和 Markdown 摘要。

  python community/check.py community/ctf/<GitHub用户名>__<作品名>
  python community/check.py community/warehouse/<GitHub用户名>__<作品名>
  python community/check.py <投稿目录> --full        局数更多（bot 类）/ 跑全部 8 组隐藏负载（仓储调度）
  python community/check.py <投稿目录> --skip-eval   只查结构和能否编译，不跑评测

bot 类各集（land、bomberman、ctf、tank、tron、soccer、boomerang）直接调用那一集的 challenge.py，不改动游戏引擎、预算或评分算法。
仓储调度（warehouse）在本机把基线和投稿各跑一遍：先比输出，再算加速比。
默认的快速档跑 5 组公开负载和 3 组较快的隐藏负载；--full 跑第 1 轮的全部 8 组隐藏负载。

结果写到 community/.runs/<集>/<投稿目录名>/（result.json、summary.md），这个目录不会进 git。
只用 Python 标准库和 g++。规则说明见 community/README.md。
"""
import argparse
import hashlib
import json
import math
import os
import platform
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
SCHEMA = 1

BOT_EPISODES = ("land", "bomberman", "ctf", "tank", "tron", "soccer", "boomerang")
EPISODES = ("land", "bomberman", "ctf", "warehouse", "tank", "tron", "soccer", "boomerang")
EPISODE_TITLES = {"land": "第 1 集 · 圈地", "bomberman": "第 2 集 · 炸弹人", "ctf": "第 3 集 · 夺旗",
                  "warehouse": "第 5 集 · 仓储调度", "tank": "第 6 集 · 坦克大战", "tron": "第 7 集 · 光轮对战",
                  "soccer": "第 8 集 · 2v2 足球", "boomerang": "第 11 集 · 回旋镖大作战"}
# 裁判只支持 Windows 的几集：本地评测和 GitHub Actions 都要在 Windows 上跑
WINDOWS_EPISODES = ("tron", "soccer", "boomerang")
# 评测脚本自己写 result.json、按固定参照只拟合投稿的几集
FIXED_REF_EPISODES = ("tank", "tron", "soccer", "boomerang")
# 选手包自带 build.py 的几集：试编译沿用它的源码检查和正式编译命令，只配这一个官方头文件
KIT_HEADER = {"soccer": "soccer.hpp", "boomerang": "dart.hpp"}

# 目录名：<GitHub 用户名小写>__<作品名>。两段都只允许小写字母、数字、连字符，连字符不能在开头结尾，也不能连用。
_PART = r"[a-z0-9]+(?:-[a-z0-9]+)*"
ENTRY_RE = re.compile(r"^(?P<user>%s)__(?P<slug>%s)\Z" % (_PART, _PART))
MAX_USER, MAX_SLUG = 39, 40

WRITTEN_BY = ("human", "ai", "human+ai")
# meta.json 的字段：名字 -> (是否必填, 最大字符数)
META_FIELDS = {"author": (True, 40), "github": (True, MAX_USER), "name": (True, 40), "description": (True, 120),
               "written_by": (True, 10), "model": (False, 80), "tool": (False, 80), "based_on": (False, 120),
               "license": (True, 10)}

KB = 1024
MAX_META = 4 * KB
MAX_NOTES = 64 * KB
MAX_BOT = 256 * KB
MAX_SRC_FILE = 256 * KB
MAX_SRC_TOTAL = 768 * KB
MAX_SRC_FILES = 40
SRC_SUFFIXES = (".cpp", ".h", ".hpp", ".md")

# 只是提醒维护者人工看一眼，不会因此判不通过。
SUSPICIOUS = [
    (r"#\s*include\s*<(fstream|filesystem|thread|future|unistd\.h|windows\.h|winsock2?\.h|"
     r"sys/[a-z_]+\.h|netinet/[a-z_]+\.h|arpa/[a-z_]+\.h|dlfcn\.h|signal\.h|csignal)>", "包含了 <%s>"),
    (r"\b(system|popen|_popen|fork|execl|execlp|execv|execvp|execve|CreateProcessA|CreateProcessW|WinExec|ShellExecuteA|"
     r"fopen|freopen|socket|dlopen|LoadLibraryA)\s*\(", "调用了 %s("),
    (r"\b(asm|__asm__)\b", "出现了 %s"),
]

# 仓储调度的两档负载。隐藏负载后面的数字是比赛电脑上基线的 CPU 秒数（referee/隐藏负载/manifest.json）。
WH_PUBLIC = ["pub_crowded", "pub_dynamic", "pub_flood", "pub_small", "pub_wide"]
WH_QUICK_HIDDEN = ["hid_lowbattery", "hid_mixed", "hid_dynamic"]          # 35、86、99 秒
WH_FULL_HIDDEN = ["hid_medium", "hid_huge", "hid_crowded", "hid_dynamic", "hid_flood", "hid_lowbattery", "hid_long",
                  "hid_mixed"]                                              # 合计约 1100 秒
WH_MEM_LIMIT_MB = 1024
WH_MIN_TOTAL = 1.0     # 单次很快的程序连跑到累计 CPU 1 秒
WH_MAX_RUNS = 30


def out(*a):
    print(*a, flush=True)


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def md_escape(s, limit=200):
    """把任意字符串变成可以安全放进 Markdown 表格的一段文字。"""
    s = "".join(ch if ch.isprintable() else " " for ch in str(s))[:limit]
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    return re.sub(r"([\\`*_\[\]|~])", r"\\\1", s)


def find_compiler():
    cxx = os.environ.get("CXX") or shutil.which("g++") or shutil.which("clang++")
    if not cxx and os.name == "nt":
        for guess in (ROOT / "tools" / "w64devkit" / "bin" / "g++.exe", Path("C:/w64devkit/bin/g++.exe")):
            if guess.is_file():
                return str(guess)
    return cxx


def compiler_version(cxx):
    try:
        r = subprocess.run([cxx, "--version"], capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=30)
        return (r.stdout.strip().splitlines() or ["?"])[0]
    except (OSError, subprocess.SubprocessError):
        return "?"


def cxx_flags():
    return ["-O2", "-std=c++20"] + (["-static"] if os.name == "nt" else [])


def load_module(name, path):
    """按路径加载可信 checkout 里的脚本（各集的 challenge.py、选手包的 build.py），不从投稿目录找。"""
    import importlib.util
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def compile_in(build_dir, sources, exe_name, cxx, timeout=None, flags=None):
    """在 build_dir 里编译。源文件事先复制进去，命令行里只出现不带目录的文件名，
    所以仓库放在中文路径下、Windows 代码页是 UTF-8 时也不会碰到链接器打不开文件的问题。"""
    cmd = [cxx] + (flags or cxx_flags()) + ["-o", exe_name] + sorted(sources)
    try:
        r = subprocess.run(cmd, cwd=build_dir, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout)
    except subprocess.TimeoutExpired:
        return None, "编译超过 5 分钟，已中止"
    exe = Path(build_dir) / exe_name
    if r.returncode != 0 or not exe.is_file():
        return None, "编译命令：%s\n%s" % (" ".join(["g++"] + cmd[1:]), (r.stderr or r.stdout)[-3000:])
    return exe, None


# ---------------------------------------------------------------- 结构检查

def read_text_file(path, errors, label):
    data = path.read_bytes()
    if b"\0" in data:
        errors.append("%s 里有 NUL 字节，看起来不是文本文件" % label)
        return None
    try:
        return data.decode("utf-8-sig")
    except UnicodeDecodeError:
        errors.append("%s 不是 UTF-8 编码" % label)
        return None


def validate_meta(path, user, errors):
    if path.stat().st_size > MAX_META:
        errors.append("meta.json 超过 %d KB" % (MAX_META // KB))
        return None
    text = read_text_file(path, errors, "meta.json")
    if text is None:
        return None
    try:
        meta = json.loads(text)
    except ValueError as e:
        errors.append("meta.json 不是合法的 JSON：%s" % e)
        return None
    if not isinstance(meta, dict):
        errors.append("meta.json 最外层必须是一个对象")
        return None
    for key in meta:
        if key not in META_FIELDS:
            errors.append("meta.json 里有不认识的字段 %r（允许的字段：%s）" % (key, "、".join(META_FIELDS)))
    clean = {}
    for key, (required, limit) in META_FIELDS.items():
        v = meta.get(key, "")
        if v is None:
            v = ""
        if not isinstance(v, str):
            errors.append("meta.json 的 %s 必须是字符串" % key)
            continue
        v = v.strip()
        if required and not v:
            errors.append("meta.json 缺少必填字段 %s" % key)
        if len(v) > limit:
            errors.append("meta.json 的 %s 超过 %d 个字符" % (key, limit))
        if any(not ch.isprintable() for ch in v):
            errors.append("meta.json 的 %s 里有换行或控制字符" % key)
        clean[key] = v
    if clean.get("written_by") and clean["written_by"] not in WRITTEN_BY:
        errors.append("meta.json 的 written_by 只能是 %s" % " / ".join(WRITTEN_BY))
    if clean.get("written_by") in ("ai", "human+ai") and not clean.get("model"):
        errors.append("written_by 是 %s 时，model 要写上用的模型名" % clean["written_by"])
    if clean.get("license") and clean["license"] != "MIT":
        errors.append('meta.json 的 license 必须写 "MIT"，表示同意按本仓库的 MIT 许可发布')
    if clean.get("github") and user and clean["github"].lower() != user:
        errors.append("meta.json 的 github（%s）和目录名里的用户名（%s）对不上" % (clean["github"], user))
    return clean


def has_symlink(path):
    """检查 community/集/投稿 或 artifacts/结果/result.json 这三层。

    更高层属于调用者选择的工作目录，可能包含 macOS 的 /tmp 等系统链接。
    """
    path = Path(path).absolute()
    return any(p.is_symlink() for p in (path, *path.parents[:2]))


def validate(entry, episode):
    """返回 (meta, errors, warnings, files)。files 是 {相对路径: sha256}。"""
    errors, warnings, files = [], [], {}
    if episode not in EPISODES or has_symlink(entry) or not entry.is_dir():
        return None, ["投稿目录不存在、集数不合法或路径包含符号链接"], warnings, files
    m = ENTRY_RE.match(entry.name)
    user = None
    if not m or len(m["user"]) > MAX_USER or len(m["slug"]) > MAX_SLUG:
        errors.append("目录名 %r 不符合 <GitHub用户名>__<作品名>：两段都只能用小写字母、数字和连字符，"
                      "用户名最多 %d 个字符，作品名最多 %d 个字符" % (entry.name, MAX_USER, MAX_SLUG))
    else:
        user = m["user"]

    allowed_top = {"meta.json", "NOTES.md"} | ({"src"} if episode == "warehouse" else {"bot.cpp"})
    for p in sorted(entry.iterdir()):
        if p.is_symlink():
            errors.append("%s 是符号链接，不收" % p.name)
        elif p.name not in allowed_top:
            errors.append("多出来的文件或文件夹：%s（这一集的投稿目录里只能有 %s）" % (p.name, "、".join(sorted(allowed_top))))
        elif p.name == "src" and not p.is_dir():
            errors.append("src 必须是文件夹")
        elif p.name != "src" and not p.is_file():
            errors.append("%s 必须是普通文件" % p.name)

    meta = None
    if (entry / "meta.json").is_file() and not (entry / "meta.json").is_symlink():
        meta = validate_meta(entry / "meta.json", user, errors)
    else:
        errors.append("缺少 meta.json")

    notes = entry / "NOTES.md"
    if notes.is_file() and not notes.is_symlink():
        if notes.stat().st_size > MAX_NOTES:
            errors.append("NOTES.md 超过 %d KB" % (MAX_NOTES // KB))
        read_text_file(notes, errors, "NOTES.md")

    code = []  # (相对路径, 文本)
    if episode == "warehouse":
        src = entry / "src"
        if not src.is_dir() or src.is_symlink():
            errors.append("缺少 src/ 文件夹")
        else:
            total, cpps, n = 0, 0, 0
            for p in sorted(src.iterdir()):
                rel = "src/" + p.name
                if p.is_symlink() or not p.is_file():
                    errors.append("%s 不是普通文件（src/ 下不能有子文件夹或符号链接，评测只编译 src/ 这一层的 .cpp）" % rel)
                    continue
                n += 1
                if p.suffix not in SRC_SUFFIXES:
                    errors.append("%s 的扩展名不在允许范围内（%s）" % (rel, "、".join(SRC_SUFFIXES)))
                    continue
                size = p.stat().st_size
                total += size
                if size > MAX_SRC_FILE:
                    errors.append("%s 超过 %d KB" % (rel, MAX_SRC_FILE // KB))
                text = read_text_file(p, errors, rel)
                files[rel] = sha256_file(p)
                if p.suffix != ".md":
                    cpps += p.suffix == ".cpp"
                    if text is not None:
                        code.append((rel, text))
            if not cpps:
                errors.append("src/ 下没有 .cpp 文件")
            if n > MAX_SRC_FILES:
                errors.append("src/ 下的文件超过 %d 个" % MAX_SRC_FILES)
            if total > MAX_SRC_TOTAL:
                errors.append("src/ 合计超过 %d KB" % (MAX_SRC_TOTAL // KB))
    else:
        bot = entry / "bot.cpp"
        if not bot.is_file() or bot.is_symlink():
            errors.append("缺少 bot.cpp")
        else:
            if bot.stat().st_size > MAX_BOT:
                errors.append("bot.cpp 超过 %d KB" % (MAX_BOT // KB))
            text = read_text_file(bot, errors, "bot.cpp")
            files["bot.cpp"] = sha256_file(bot)
            if text is not None:
                code.append(("bot.cpp", text))

    for rel, text in code:
        for pattern, message in SUSPICIOUS:
            found = sorted({x.group(1) for x in re.finditer(pattern, text)})
            for name in found[:5]:
                rule = ("请维护者按 %s/workspace/README.md 人工确认；允许线程，但按所有线程的 CPU 总时间计费" % episode
                        if episode in ("tank", "tron", "soccer", "boomerang") else "规则不允许读写文件、联网、启动别的程序或多线程，请维护者人工确认")
                warnings.append("%s：%s。%s" % (rel, message % name, rule))
    return meta, errors, warnings, files


# ---------------------------------------------------------------- bot 类四集

def parse_land(run_dir):
    doc = json.loads((run_dir / "ratings.json").read_text(encoding="utf-8"))
    elo = 400 / math.log(10)
    field = []
    for name, m in doc["models"].items():
        thetas = [m["modes"].get(k, {}).get("theta") for k in ("duel", "melee")]
        row = {"name": name, "is_entry": name == "your-bot@you", "total": m.get("score"),
               "duel": None if thetas[0] is None else round(thetas[0] * elo, 1),
               "melee": None if thetas[1] is None else round(thetas[1] * elo, 1)}
        row["score"] = None if None in thetas else round((thetas[0] + thetas[1]) / 2 * elo, 1)
        field.append(row)
    games = sum(doc["modes"][k]["games"] for k in doc["modes"])
    return field, {"games_note": "单挑 %d 局；混战拆成 %d 组两两比较" % (
        doc["modes"].get("duel", {}).get("games", 0), doc["modes"].get("melee", {}).get("games", 0)), "pairs": games}


def parse_bomberman(run_dir):
    doc = json.loads((run_dir / "bt.json").read_text(encoding="utf-8"))
    ratings = doc.get("ratings", doc)
    field = []
    for name, v in ratings.items():
        score = v.get("rating", v.get("score")) if isinstance(v, dict) else v
        if isinstance(score, (list, tuple)):
            score = score[0]
        field.append({"name": name, "is_entry": name == "你的 bot", "score": round(float(score), 1)})
    rows = json.loads((run_dir / "results.json").read_text(encoding="utf-8"))
    return field, {"games_note": "%d 局" % len(rows)}


def parse_ctf(run_dir):
    doc = json.loads((run_dir / "games" / "成绩.json").read_text(encoding="utf-8"))
    field = [{"name": name, "is_entry": name == "你的 bot", "score": round(v[0], 1), "ci95": [round(v[1], 1), round(v[2], 1)]}
             for name, v in doc["ratings"].items()]
    n = sum(1 for line in (run_dir / "games" / "结果.jsonl").read_text(encoding="utf-8").splitlines() if line.strip())
    return field, {"games_note": "%d 局" % n}


def parse_tank(run_dir):
    """Tank 挑战沿用固定终场赛评分，不把参照分数误当作本次重测。"""
    doc = json.loads((run_dir / "result.json").read_text(encoding="utf-8"))
    final = json.loads((ROOT / "tank" / "results" / "final" / "成绩.json").read_text(encoding="utf-8"))["players"]
    number = lambda v: isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v)
    if not isinstance(doc, dict) or not isinstance(doc.get("budget"), dict):
        raise ValueError("坦克成绩格式不合法")
    budget = doc["budget"]
    if (doc.get("schema") != 1 or doc.get("complete") is not True or doc.get("mode") != "formal" or
            doc.get("rating_method") != "fixed-final" or budget.get("budget_kind") != "cpu" or
            (budget.get("first_ms"), budget.get("turn_ms")) != (2000, 50)):
        raise ValueError("坦克成绩不是完整的官方 CPU 预算正式对局")
    games = doc["games_per_opponent"]
    opponents = doc["opponents"]
    if (type(games) is not int or games not in (20, 80) or not isinstance(opponents, list) or
            len(opponents) != len(final) or any(not isinstance(r, dict) for r in opponents) or
            {r["name"] for r in opponents} != set(final) or
            doc["games"] != games * len(final) or doc["seed"] != 1001 or not number(doc["rating"])):
        raise ValueError("坦克成绩缺少对手、对局或有效实力值")
    for row in opponents:
        ref = final[row["name"]]
        counts = [row[k] for k in ("wins", "draws", "losses")]
        if (any(type(x) is not int or x < 0 for x in counts) or sum(counts) != games or
                row["rating"] != ref["rating"] or row["ranked"] is not ref["ranked"]):
            raise ValueError("坦克对局不完整或固定参照成绩不一致")
    field = [{"name": r["display"] or r["label"] or name, "is_entry": False,
              "score": round(r["rating"], 1)} for name, r in final.items() if r["ranked"]]
    field.append({"name": "你的 bot", "is_entry": True, "score": round(doc["rating"], 1)})
    return field, {"games_note": "%d 局（16 个对手，每个 %d 局）" % (doc["games"], games),
                   "games": doc["games"], "rating_method": "fixed-final", "opponents": opponents,
                   "source_sha256": doc["source_sha256"], "budget": budget,
                   "rank": 1 + sum(r["ranked"] and r["rating"] > doc["rating"] for r in final.values())}


def parse_tron(run_dir):
    from tron_contract import validate_result
    doc = json.loads((run_dir / "result.json").read_text(encoding="utf-8"))
    return validate_result(doc, ROOT)


def parse_fixed(episode):
    """足球、回旋镖：用可信 checkout 里那一集的 challenge.py 按固定赛程和固定参照重算一遍，不信任结果文件里的分数。"""
    def parse(run_dir):
        doc = json.loads((run_dir / "result.json").read_text(encoding="utf-8"))
        return load_module("trusted_%s_challenge" % episode, ROOT / episode / "challenge.py").validate_result(doc, ROOT)
    return parse


def run_tron_challenge(cmd, cwd, env, timeout):
    """Keep the parent alive until Windows can terminate its complete process tree."""
    process = subprocess.Popen(cmd, cwd=cwd, env=env)
    try:
        return subprocess.CompletedProcess(cmd, process.wait(timeout=timeout))
    except subprocess.TimeoutExpired:
        try:
            subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"],
                           capture_output=True, timeout=30)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait(timeout=30)
        raise


BOT_SPEC = {
    "soccer": {"parse": parse_fixed("soccer"), "report": "report.md",
               "score_label": "实力值（固定个人榜参照，只拟合投稿的 Bradley–Terry 实力值；你的程序踢两个座位，基准队 = 0，强 10 倍记 +400）"},
    "boomerang": {"parse": parse_fixed("boomerang"), "report": "report.md",
                  "score_label": "个人实力值（固定海选参照，只拟合投稿；一对一、四人局两项平均，13 位选手平均 = 0，强 10 倍记 +400）"},
    "tron": {"parse": parse_tron, "report": "report.md",
             "score_label": "加权实力值（固定终场强对手榜，只拟合投稿；豆包 = 0；1v1 / 4人 / 12人 = 50 / 30 / 20%）"},
    "tank": {"parse": parse_tank, "report": "report.md",
             "score_label": "实力值（固定终场赛对手评分，只拟合投稿的 Bradley–Terry 实力值，基准 = 0，强 10 倍记 +400）"},
    "land": {"parse": parse_land, "report": "ratings.md",
             "score_label": "实力值（单挑、混战两种赛制的 Bradley–Terry 实力值取平均，greedy 基准 = 0，强 10 倍记 +400）"},
    "bomberman": {"parse": parse_bomberman, "report": "汇总.md",
                  "score_label": "实力值（Bradley–Terry，公开基准 = 0，强 10 倍记 +400）"},
    "ctf": {"parse": parse_ctf, "report": "games/成绩.md",
            "score_label": "实力值（Bradley–Terry，基准 = 0，强 10 倍记 +400）"},
}


def eval_bot(entry, episode, work, full, timeout, result):
    if episode in WINDOWS_EPISODES and sys.platform != "win32":
        result["errors"].append("%s沿用官方 Windows 正式裁判；请在 Windows 自测，社区 CI 使用 Windows。" % EPISODE_TITLES[episode])
        return
    cxx = find_compiler()
    if not cxx:
        result["errors"].append("没有找到 C++ 编译器（g++ 或 clang++）。安装办法见仓库根目录 README。")
        return
    result["env"]["compiler"] = compiler_version(cxx)
    build = work / "build"
    shutil.rmtree(build, ignore_errors=True)
    build.mkdir(parents=True)
    shutil.copy(entry / "bot.cpp", build / "bot.cpp")
    sources = ["bot.cpp"]
    if episode in ("tank", "tron"):
        # 投稿只有 bot.cpp。头文件、主循环只取运行本脚本的可信 checkout，
        # 不从 PR 的 workspace 或投稿旁边查找；正式挑战也只读取这份源码快照。
        for name in (episode + ".hpp", episode + "_main.cpp"):
            shutil.copy(ROOT / episode / "workspace" / name, build / name)
        sources.append(episode + "_main.cpp")
    flags = None
    if episode in KIT_HEADER:
        # 和比赛时相同：选手包 build.py 的源码检查和正式编译命令，只配官方头文件
        kit = load_module("trusted_%s_build" % episode, ROOT / episode / "workspace" / "build.py")
        problems = kit.check_source((entry / "bot.cpp").read_text(encoding="utf-8", errors="replace"))
        if problems:
            result["errors"].append("bot.cpp 没通过选手包的源码检查：" + "；".join(problems[:5]))
            return
        shutil.copy(ROOT / episode / "workspace" / KIT_HEADER[episode], build / KIT_HEADER[episode])
        flags = list(kit.FLAGS)
    out("[1/2] 试编译 bot.cpp（%s）" % " ".join(["g++"] + (flags or cxx_flags()) + sources))
    exe, err = compile_in(build, sources, "bot" + (".exe" if os.name == "nt" else ""), cxx,
                          **({"timeout": 300} if episode in WINDOWS_EPISODES else {}), flags=flags)
    if err:
        result["errors"].append("bot.cpp 编译失败")
        result["detail"]["compile_error"] = err
        return
    result["detail"]["compiled"] = True
    if result["mode"] == "skip-eval":
        return

    ep_dir = ROOT / episode
    runs = ep_dir / "challenge-runs"
    before = {p.name for p in runs.iterdir()} if runs.is_dir() else set()
    source = build / "bot.cpp" if episode in FIXED_REF_EPISODES else entry / "bot.cpp"
    cmd = [sys.executable, "-X", "utf8", str(ep_dir / "challenge.py"), str(source.resolve())] + (["--full"] if full else [])
    if episode in FIXED_REF_EPISODES:
        run_dir = work / "challenge"
        shutil.rmtree(run_dir, ignore_errors=True)
        cmd += ["--out", str(run_dir.resolve())] + (["--require-cpu"] if episode != "boomerang" else [])
    out("[2/2] 运行 %s/challenge.py%s（官方脚本，原样调用）\n" % (episode, " --full" if full else ""))
    env = dict(os.environ, PYTHONUTF8="1", CXX=cxx)
    try:
        r = (run_tron_challenge(cmd, ep_dir, env, timeout) if episode in WINDOWS_EPISODES else
             subprocess.run(cmd, cwd=ep_dir, env=env, timeout=timeout))
    except subprocess.TimeoutExpired:
        result["errors"].append("%s/challenge.py 运行超过 %d 分钟，已中止" % (episode, timeout // 60))
        return
    if r.returncode != 0:
        result["errors"].append("%s/challenge.py 以退出码 %d 结束，比赛没有打完（上面有它的报错）" % (episode, r.returncode))
        return
    if episode not in FIXED_REF_EPISODES:
        new = sorted(p for p in runs.iterdir() if p.name not in before) if runs.is_dir() else []
        if not new:
            result["errors"].append("没有在 %s/challenge-runs/ 里找到这次运行的结果目录" % episode)
            return
        run_dir = new[-1]
    spec = BOT_SPEC[episode]
    try:
        field, extra = spec["parse"](run_dir)
        if episode == "tank" and (extra["source_sha256"] != result["files"]["bot.cpp"] or
                                   extra["games"] != (1280 if full else 320)):
            raise ValueError("坦克成绩的源码哈希或局数与本次投稿评测不符")
        if episode in ("tron", "soccer", "boomerang") and (extra["source_sha256"] != result["files"]["bot.cpp"] or
                                                           extra["full"] is not full):
            raise ValueError("%s成绩的源码哈希或档位与本次投稿评测不符" % EPISODE_TITLES[episode])
    except (OSError, ValueError, KeyError, TypeError, IndexError, AttributeError, OverflowError) as e:
        result["errors"].append("读不懂 %s 里的成绩文件：%s: %s" % (run_dir.name, type(e).__name__, e))
        return
    field.sort(key=lambda x: (x["score"] is None, -(x["score"] or 0), x["name"]))
    for i, row in enumerate(field, 1):
        row["rank"] = i
    mine = next((x for x in field if x["is_entry"]), None)
    result["detail"].update(extra, field=field, field_size=len(field), run_dir=os.path.relpath(run_dir, ROOT))
    result["score_label"] = spec["score_label"]
    report = run_dir / spec["report"]
    if report.is_file():
        shutil.copy(report, work / "official-report.md")
    if mine is None or mine["score"] is None:
        result["errors"].append("成绩表里没有这份投稿的实力值（多半是它在评测脚本里编译失败或一局都没打成）")
        return
    result["score"] = mine["score"]
    result["detail"]["rank"] = extra["rank"] if episode in FIXED_REF_EPISODES else mine["rank"]
    if "ci95" in mine:
        result["detail"]["ci95"] = mine["ci95"]


# ---------------------------------------------------------------- 仓储调度

def first_diff(expected, actual):
    a, b = expected.replace(b"\r\n", b"\n").split(b"\n"), actual.replace(b"\r\n", b"\n").split(b"\n")
    for i in range(max(len(a), len(b))):
        x = a[i] if i < len(a) else b"<EOF>"
        y = b[i] if i < len(b) else b"<EOF>"
        if x != y:
            return {"line": i + 1, "expected": x.decode("utf-8", "replace")[:160], "actual": y.decode("utf-8", "replace")[:160]}
    return None


def eval_warehouse(entry, work, full, fresh, result):
    wh = ROOT / "warehouse"
    sys.path.insert(0, str(wh / "workspace" / "tools"))
    import measure  # 官方的计时工具，原样使用：CPU 时间、峰值内存、输出摘要

    cxx = find_compiler()
    if not cxx:
        result["errors"].append("没有找到 C++ 编译器（g++ 或 clang++）。安装办法见仓库根目录 README。")
        return
    version = compiler_version(cxx)
    result["env"]["compiler"] = version
    exe_name = "sim" + measure.EXE

    build = work / "build"
    shutil.rmtree(build, ignore_errors=True)
    build.mkdir(parents=True)
    for p in (entry / "src").iterdir():
        if p.is_file() and p.suffix in (".cpp", ".h", ".hpp"):
            shutil.copy(p, build / p.name)
    sources = [p.name for p in build.iterdir() if p.suffix == ".cpp"]
    out("[1/3] 编译投稿（%s）" % " ".join(["g++"] + cxx_flags() + ["src/*.cpp"]))
    exe, err = compile_in(build, sources, exe_name, cxx)
    if err:
        result["errors"].append("src/ 编译失败")
        result["detail"]["compile_error"] = err
        return
    result["detail"]["compiled"] = True
    if result["mode"] == "skip-eval":
        return

    # 基线：编译一次，用时和输出按（基线源码、编译器、参数、平台、这台机器）缓存在 community/.cache/ 里。
    base_src = wh / "workspace" / "baseline"
    h = hashlib.sha256()
    for p in sorted(base_src.iterdir()):
        h.update(p.name.encode() + b"\0" + p.read_bytes() + b"\0")
    h.update(json.dumps([version, cxx_flags(), sys.platform, platform.node()]).encode())
    cache_dir = HERE / ".cache" / "warehouse" / h.hexdigest()[:16]
    cache_dir.mkdir(parents=True, exist_ok=True)
    cache_file = cache_dir / "times.json"
    cache = {}
    if cache_file.is_file() and not fresh:
        try:
            cache = json.loads(cache_file.read_text(encoding="utf-8"))
        except ValueError:
            cache = {}
    base_exe = cache_dir / exe_name
    if not base_exe.is_file():
        out("[2/3] 编译基线（warehouse/workspace/baseline/）")
        for p in base_src.iterdir():
            if p.suffix in (".cpp", ".h", ".hpp"):
                shutil.copy(p, cache_dir / p.name)
        base_exe, err = compile_in(cache_dir, [p.name for p in base_src.iterdir() if p.suffix == ".cpp"], exe_name, cxx)
        if err:
            result["errors"].append("基线编译失败，这是环境问题，不是投稿的问题")
            result["detail"]["compile_error"] = err
            return

    manifest = json.loads((wh / "referee" / "隐藏负载" / "manifest.json").read_text(encoding="utf-8"))
    public = wh / "workspace" / "workloads" / "public"
    hidden = wh / "referee" / "隐藏负载"
    plan = [(n, "隐藏", hidden / (n + ".in")) for n in WH_FULL_HIDDEN] if full else \
        [(n, "公开", public / (n + ".in")) for n in WH_PUBLIC] + [(n, "隐藏", hidden / (n + ".in")) for n in WH_QUICK_HIDDEN]
    est = sum(manifest[n]["baseline_cpu"] for n, kind, _ in plan if kind == "隐藏") + (40 if not full else 0)
    out("[3/3] 在 %d 组负载上先跑基线、再跑投稿。基线很慢：比赛电脑上这几组合计约 %d 秒，没有缓存时要等这么久。" % (len(plan), est))

    rows = []
    for name, kind, inp in plan:
        key = "%s|%s" % (name, sha256_file(inp))
        base_out = cache_dir / (name + ".out")
        if key not in cache or not base_out.is_file():
            out("  %-15s 基线运行中…" % name)
            b = measure.run(str(base_exe), str(inp))
            if b["code"] != 0:
                result["errors"].append("基线在 %s 上以退出码 %d 结束，这是环境问题，不是投稿的问题" % (name, b["code"]))
                return
            base_out.write_bytes(b["output"])
            cache[key] = {"cpu": b["cpu"], "digest": b["digest"], "mem_mb": b["mem_mb"]}
            cache_file.write_text(json.dumps(cache, indent=1), encoding="utf-8")
        base = cache[key]
        # 官方的标准答案：公开负载是 .out 文件，隐藏负载是清单里的摘要。基线在这台机器上的输出应当和它一致。
        official = manifest[name]["digest"] if kind == "隐藏" else measure.digest((public / (name + ".out")).read_bytes())
        row = {"name": name, "set": kind, "baseline_cpu": round(base["cpu"], 3),
               "baseline_matches_official": base["digest"] == official, "runs": 0}
        limit = base["cpu"] * 1.2 + 5      # 和第 1 轮正式评测相同：比基线还慢就不必等它跑完
        cpus, status, peak = [], "ok", 0.0
        while True:
            r = measure.run(str(build / exe_name), str(inp), timeout=limit)
            peak = max(peak, r["mem_mb"])
            if r["timeout"]:
                status = "timeout"
            elif r["code"] != 0:
                status = "crash"
                row["exit_code"] = r["code"]
            elif r["digest"] != base["digest"]:
                status = "wrong_output" if not cpus else "unstable"
                d = first_diff(base_out.read_bytes(), r["output"])
                if d:
                    row["diff"] = d
            elif r["mem_mb"] > WH_MEM_LIMIT_MB:
                status = "memory"
            if status != "ok":
                break
            cpus.append(r["cpu"])
            # 单次超过 10 秒的只跑 1 遍；其余至少 3 遍，并且连跑到累计 CPU 1 秒（最多 30 遍），取平均
            if r["cpu"] > 10 or len(cpus) >= WH_MAX_RUNS or (len(cpus) >= 3 and sum(cpus) >= WH_MIN_TOTAL):
                break
        row.update(status=status, runs=len(cpus), mem_mb=round(peak, 1))
        if status == "ok":
            cpu = sum(cpus) / len(cpus)
            row["cpu"] = round(cpu, 4)
            row["speedup"] = round(base["cpu"] / max(cpu, 1e-4), 2)
            out("  %-15s 正确   基线 %8.2f 秒   投稿 %8.4f 秒   加速 %.1f 倍" % (name, base["cpu"], cpu, row["speedup"]))
        else:
            out("  %-15s %s" % (name, status))
        rows.append(row)

    ok = [r for r in rows if r["status"] == "ok"]
    result["detail"].update(workloads=rows, correct=len(ok), total=len(rows))
    result["score_label"] = "几何平均加速比（基线 CPU 时间 ÷ 投稿 CPU 时间，同一台机器上测）"
    if any(not r["baseline_matches_official"] for r in rows):
        result["warnings"].append("基线在这台机器上的输出和官方标准答案不一致（%s）。判对错用的是这台机器上基线的输出；"
                                  "请把操作系统和编译器版本告诉维护者" % "、".join(
                                      r["name"] for r in rows if not r["baseline_matches_official"]))
    names = {"timeout": "比基线还慢，超时", "crash": "异常退出", "wrong_output": "输出和基线不同",
             "unstable": "同一个输入多次运行的输出不一样", "memory": "内存超过 1 GB"}
    for r in rows:
        if r["status"] != "ok":
            result["errors"].append("%s：%s" % (r["name"], names[r["status"]]))
    if len(ok) == len(rows):
        result["score"] = round(math.exp(sum(math.log(r["speedup"]) for r in ok) / len(ok)), 2)


# ---------------------------------------------------------------- 摘要

def fmt(x, digits=1):
    return "—" if x is None else ("%." + str(digits) + "f") % x


def render_summary(result):
    meta = result.get("meta") or {}
    ep = result["episode"]
    L = ["## 社区投稿自测：%s / %s" % (ep, md_escape(result["entry"])), ""]
    verdict = {"pass": "通过", "fail": "未通过"}[result["status"]]
    mode = {"quick": "快速档", "full": "完整档（--full）", "skip-eval": "只查结构和编译（--skip-eval）"}[result["mode"]]
    L += ["**结论：%s**（%s，%s）" % (verdict, EPISODE_TITLES[ep], mode), ""]
    who = {"human": "人写", "ai": "AI 写", "human+ai": "人和 AI 合写"}.get(meta.get("written_by"), "—")
    L += ["| 项目 | 内容 |", "|---|---|",
          "| 作品 | %s |" % md_escape(meta.get("name", "—")),
          "| 作者 | %s（@%s） |" % (md_escape(meta.get("author", "—")), md_escape(meta.get("github", "—"))),
          "| 谁写的 | %s |" % who,
          "| 模型 / 工具 | %s / %s |" % (md_escape(meta.get("model") or "—"), md_escape(meta.get("tool") or "—")),
          "| 基于 | %s |" % md_escape(meta.get("based_on") or "—"),
          "| 一句话说明 | %s |" % md_escape(meta.get("description", "—")), ""]
    if result["errors"]:
        L += ["### 没通过的地方", ""] + ["- %s" % md_escape(e, 400) for e in result["errors"]] + [""]
    if result["warnings"]:
        L += ["### 提醒", ""] + ["- %s" % md_escape(w, 400) for w in result["warnings"]] + [""]
    d = result["detail"]
    if d.get("compile_error"):
        L += ["### 编译器输出", "", "```text", d["compile_error"].replace("```", "'''"), "```", ""]

    if ep == "warehouse" and d.get("workloads"):
        L += ["### 成绩", "", "| 负载 | 来源 | 结果 | 基线 CPU 秒 | 投稿 CPU 秒 | 加速比 | 跑了几遍 | 峰值内存 MB |",
              "|---|---|---|---:|---:|---:|---:|---:|"]
        names = {"ok": "正确", "timeout": "超时", "crash": "异常退出", "wrong_output": "输出不同", "unstable": "输出不稳定",
                 "memory": "超内存"}
        for r in d["workloads"]:
            L.append("| %s | %s | %s | %s | %s | %s | %d | %s |" % (
                md_escape(r["name"]), r["set"], names[r["status"]], fmt(r["baseline_cpu"], 2), fmt(r.get("cpu"), 4),
                "—" if r.get("speedup") is None else "%.1f 倍" % r["speedup"], r["runs"], fmt(r.get("mem_mb"))))
        L += ["", "输出正确 %d / %d 组。%s" % (d["correct"], d["total"],
              "几何平均加速比 **%.1f 倍**。" % result["score"] if result["score"] is not None else "有负载没通过，不计算几何平均。"), ""]
        for r in [r for r in d["workloads"] if r.get("diff")][:3]:
            if r.get("diff"):
                L += ["%s 第 %d 行开始不同：" % (md_escape(r["name"]), r["diff"]["line"]), "", "```text",
                      "基线：" + r["diff"]["expected"].replace("```", "'''"), "投稿：" + r["diff"]["actual"].replace("```", "'''"), "```", ""]
        L += ["加速比是同一台机器上基线和投稿的 CPU 时间之比。%s和节目里的正式成绩（比赛电脑、8 组隐藏负载、另一套计时遍数）不是同一次测量，不能直接比。" % (
            "快速档只跑 5 组公开负载和 3 组隐藏负载，" if result["mode"] == "quick" else ""), ""]
    elif ep == "tank" and d.get("field"):
        L += ["### 成绩", "", "正式模式挑战 14 位 AI 的下半场版本、出题方参照和基准（%s）。" % d.get("games_note", ""), "",
              "投稿实力值 **%s**；放进 14 位 AI 里相当于第 **%s / 15** 名。" % (fmt(result["score"]), d["rank"]), "",
              result["score_label"] + "。官方参照分数固定，只有投稿分数由本次胜平负拟合；不是官方选手之间重新打的一轮联赛。", "",
              "| 对手 | 终场赛实力值（固定） | 你的胜-平-负 |", "|---|---:|---|"]
        for r in d["opponents"]:
            L.append("| %s | %s | %d-%d-%d |" % (md_escape(r["display"]), fmt(r["rating"]), r["wins"], r["draws"], r["losses"]))
        L += ["", "官方预算：首回合 2000 CPU 毫秒，之后每回合 50 CPU 毫秒（所有线程合计）。",
              "局数少，且机器和地图与终场赛不同；这个参照名次不是正式赛成绩。", ""]
    elif ep == "tron" and d.get("field"):
        L += ["### 成绩", "", "Windows 正式模式；%s。" % d["games_note"], "",
              "加权实力值 **%s**；强对手榜参照名次 **%s / 13**（12 位选手 + 这份投稿）。" % (fmt(result["score"]), d["rank"]), "",
              result["score_label"] + "。sparring_hard 参加对战和拟合，只作参考线、不占名次。", "",
              "| 局型 | 投稿实力值 | 权重 |", "|---|---:|---:|"]
        for mode, weight in d["weights"].items():
            L.append("| %s | %s | %.0f%% |" % (mode, fmt(d["ratings"][mode]), weight * 100))
        L += ["", "首回合 2000 CPU 毫秒、之后 50 CPU 毫秒，所有线程合计。固定参照不是本次重测成绩；机器、地图和局数不同，参照名次不是正式赛名次。", ""]
    elif ep == "soccer" and d.get("field"):
        L += ["### 成绩", "", "Windows 正式计时，你的程序同时踢本队两个座位，逐一对踢个人赛的 12 支个人队和 3 支参考队；%s。" % d["games_note"], "",
              "实力值 **%s**；放进个人榜的 12 支个人队里相当于第 **%s / 13** 名。" % (fmt(result["score"]), d["rank"]), "",
              result["score_label"] + "。对手分数固定取比赛时个人榜的 BT 点估计，参考队参加拟合、不占名次。", "",
              "| 对手 | 个人榜实力值（固定） | 你的胜-平-负 |", "|---|---:|---|"]
        for r in d["opponents"]:
            L.append("| %s | %s | %d-%d-%d |" % (md_escape(r["display"]), fmt(r["rating"]), r["wins"], r["draws"], r["losses"]))
        L += ["", "每步 10 CPU 毫秒，和比赛时同一个裁判和编译命令。种子和正式赛不同、局数少，参照名次不是正式名次。", ""]
    elif ep == "boomerang" and d.get("field"):
        L += ["### 成绩", "", "Windows 正式计时，和海选的 13 位选手对打；%s。" % d["games_note"], "",
              "个人实力值 **%s**（一对一 %s、四人局 %s）；放进海选个人榜相当于第 **%s / 14** 名。" % (
                  fmt(result["score"]), fmt(d["ratings"]["duel"]), fmt(d["ratings"]["ffa"]), d["rank"]), "",
              result["score_label"] + "。官方选手的分数固定取比赛时的海选成绩，不是这次重新测出的。", "",
              "| 对手 | 海选个人实力值（固定） | 一对一 胜-平-负 | 四人局两两比 胜-平-负 |", "|---|---:|---|---|"]
        for r in d["opponents"]:
            L.append("| %s | %s | %s | %s |" % (md_escape(r["display"]), fmt(r["rating"]),
                                               "-".join(map(str, r["tally"]["duel"])), "-".join(map(str, r["tally"]["ffa"]))))
        L += ["", "每步 10 CPU 毫秒、第 0 步 1000 毫秒，累计超时 20 次程序被结束。种子和海选不同、局数少，参照名次不是正式名次。", ""]
    elif ep in BOT_EPISODES and d.get("field"):
        land = ep == "land"
        L += ["### 成绩", "", "这份投稿和全部官方选手、基准打了一轮小型比赛（%s）。" % d.get("games_note", ""), ""]
        if land:
            L += ["| 名次 | 选手 | 实力值 | 单挑 | 混战 | 官方脚本的 0–100 总分 |", "|---:|---|---:|---:|---:|---:|"]
        else:
            L += ["| 名次 | 选手 | 实力值 |" + (" 95% 区间 |" if ep == "ctf" else ""), "|---:|---|---:|" + ("---|" if ep == "ctf" else "")]
        for r in d["field"]:
            name = "**%s（这份投稿）**" % md_escape(meta.get("name", result["entry"])) if r["is_entry"] else md_escape(r["name"])
            if land:
                L.append("| %d | %s | %s | %s | %s | %s |" % (r["rank"], name, fmt(r["score"]), fmt(r["duel"]), fmt(r["melee"]), fmt(r["total"])))
            elif ep == "ctf":
                L.append("| %d | %s | %s | %s ~ %s |" % (r["rank"], name, fmt(r["score"], 0), fmt(r["ci95"][0], 0), fmt(r["ci95"][1], 0)))
            else:
                L.append("| %d | %s | %s |" % (r["rank"], name, fmt(r["score"])))
        L += ["", "%s。" % result["score_label"], "",
              "这是一次局数很少的小比赛，名次会有起伏；和节目里的正式成绩（几千到几万局）不能直接比。", ""]
    env = result["env"]
    L += ["### 环境", "", "%s；Python %s；%s；%d 个逻辑核；用时 %d 秒。" % (
        md_escape(env.get("platform", "?")), env.get("python", "?"), md_escape(env.get("compiler", "未找到编译器")),
        env.get("cpus", 0), result.get("seconds", 0)), ""]
    return "\n".join(L)


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("entry", help="投稿目录，例如 community/ctf/yourname__my-bot")
    ap.add_argument("--full", action="store_true", help="bot 类：给 challenge.py 加 --full；仓储调度：跑全部 8 组隐藏负载")
    ap.add_argument("--skip-eval", action="store_true", help="只查结构和能否编译")
    ap.add_argument("--episode", choices=EPISODES, help="哪一集；默认取投稿目录的上一级目录名")
    ap.add_argument("--out", help="结果目录，默认 community/.runs/<集>/<投稿目录名>/")
    ap.add_argument("--fresh", action="store_true", help="仓储调度：不用缓存，重新测基线用时")
    ap.add_argument("--timeout", type=int, default=120, help="bot 类：challenge.py 最多跑多少分钟（默认 120）")
    args = ap.parse_args()

    entry_path = Path(args.entry)
    if has_symlink(entry_path):
        raise SystemExit("投稿路径不能包含符号链接：%s" % entry_path)
    entry = entry_path.resolve()
    if not entry.is_dir():
        raise SystemExit("找不到投稿目录：%s" % entry)
    episode = args.episode or entry.parent.name
    if episode not in EPISODES:
        raise SystemExit("认不出是哪一集：投稿目录应当放在 community/<集>/ 下，<集> 是 %s 之一；或者用 --episode 指定。" % " / ".join(EPISODES))
    safe = entry.name if ENTRY_RE.match(entry.name) else "invalid-name"
    work = Path(args.out).resolve() if args.out else HERE / ".runs" / episode / safe
    work.mkdir(parents=True, exist_ok=True)

    started = time.time()
    result = {"schema": SCHEMA, "episode": episode, "entry": entry.name,
              "mode": "skip-eval" if args.skip_eval else "full" if args.full else "quick",
              "status": "fail", "score": None, "score_label": "", "meta": None, "errors": [], "warnings": [],
              "files": {}, "detail": {},
              "env": {"platform": platform.platform(), "python": platform.python_version(), "cpus": os.cpu_count() or 0,
                      "github_actions": os.environ.get("GITHUB_ACTIONS") == "true"},
              "time": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())}
    out("检查 %s（%s）" % (entry, EPISODE_TITLES[episode]))
    meta, errors, warnings, files = validate(entry, episode)
    result.update(meta=meta, errors=errors, warnings=warnings, files=files)
    if errors:
        out("结构检查没有通过，不运行评测。")
    elif episode == "warehouse":
        eval_warehouse(entry, work, args.full, args.fresh, result)
    else:
        eval_bot(entry, episode, work, args.full, args.timeout * 60, result)
    result["seconds"] = int(time.time() - started)
    if not result["errors"]:
        result["status"] = "pass"

    summary = render_summary(result)
    (work / "result.json").write_text(json.dumps(result, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    (work / "summary.md").write_text(summary + "\n", encoding="utf-8", newline="\n")
    out("\n" + summary)
    out("结果文件：%s\n摘要：%s" % (work / "result.json", work / "summary.md"))
    out("把上面从“## 社区投稿自测”开始的摘要贴进 PR 描述。" if result["status"] == "pass" else "没有通过，先按上面列出的问题改。")
    sys.exit(0 if result["status"] == "pass" else 1)


if __name__ == "__main__":
    main()
