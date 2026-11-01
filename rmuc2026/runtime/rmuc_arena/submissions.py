"""Host-side authoring checks and immutable, content-addressed submissions."""
import hashlib
import json
from pathlib import Path
import shutil
import datetime
import secrets
import time
from contextlib import contextmanager
from .platform_support import file_lock
from .paths import data_path
import os
from .models import model_ids, normalize_team_lineup
from . import seasons as season_rules
from .worker import SOURCE_LIMIT
from .showcase import PROFILE_FILE, SHOWCASE_FILE, SHOWCASE_LIMIT, parse_showcase, validate_profile

from .paths import ROOT
SDK = Path(__file__).with_name("sdk.py")
SLOT_IDS = (1, 2, 3, 4, 6, 7)
RULES_PROFILE = "rmuc-2026-v1.0.0-arena-v3"
RULES_PROFILE_2027 = "rmuc-2027-v1.0.0-arena-v1"
STARTER_STRATEGY = ('from sdk import action\n\n\ndef act(observation, state):\n'
    '    """Implement your strategy here; publish it to Arena-Strategies."""\n'
    '    return {slot: action() for slot in observation["controlled_ids"]}, state\n')


def manifest_season(manifest):
    """The rules season a manifest or workspace record was written for."""
    return season_rules.season_of_profile(manifest.get("rules_profile")) or season_rules.LEGACY_SEASON


def editable_files(mode, season=season_rules.LEGACY_SEASON):
    return [f"robot_{slot}.py" for slot in season_rules.slots(season)] if mode == "distributed" else ["strategy.py"]


# The agent writes showcase.json once its strategy is finished; it may be absent while practising.
OPTIONAL_FILES = (SHOWCASE_FILE,)


def workspace_showcase(path):
    """The final submission's showcase.json, required and validated."""
    file = Path(path) / SHOWCASE_FILE
    if file.is_symlink() or not file.is_file():
        raise ValueError("Write showcase.json before submitting; see SHOWCASE.md")
    return parse_showcase(file.read_bytes())


def workspace_entries(path, record):
    if record["mode"] == "distributed":
        return {str(slot): path / f"robot_{slot}.py" for slot in season_rules.slots(manifest_season(record))}
    key = "team" if record["mode"] == "team" else str(record["robot"])
    return {key: path / "strategy.py"}


def submission_keys(mode, robot=None, slots=SLOT_IDS, season=None):
    if season is not None:
        slots = season_rules.slots(season)
    if mode == "team":
        return {"team"}
    if mode in ("squad", "distributed"):
        return {str(slot) for slot in slots}
    if (mode == "robot" and robot in slots) or (mode == "adaptive" and robot in model_ids()
                                                and (season is None or season_rules.legal(robot, season))):
        return {str(robot)}
    raise ValueError("Invalid submission mode or robot slot")


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path = Path(path)
    temporary = path.with_name(path.name + "." + secrets.token_hex(6) + ".tmp")
    try:
        temporary.write_bytes((json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


@contextmanager
def locked_registry(path):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with file_lock(path.with_suffix(".lock")):
        records = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
        yield records
        write_json(path, records)


def training_metadata(minutes=90, url="http://127.0.0.1:8765"):
    if not 1 <= minutes <= 1440:
        raise ValueError("Training time must be between 1 and 1440 minutes")
    return {"session_id":secrets.token_hex(8),"token":secrets.token_hex(32),"url":url,
            "time_limit_seconds":int(minutes*60),"started_at":None,"deadline_at":None,"last_submission":None}


def check_training_window(record):
    training = record.get("training")
    if training is None:  # Existing organizer-owned submissions remain readable.
        return
    if training["started_at"] is None:
        raise ValueError("Development window has not started; ask the organizer to start it")
    if time.time() >= training["deadline_at"]:
        raise ValueError("Development deadline expired; tests and submissions are closed")


def guide_path(mode, robot=None, lang="zh", season=season_rules.LEGACY_SEASON):
    """The guide for team, distributed or one robot slot, built from shared fragments."""
    if lang not in ("zh", "en"):
        raise ValueError("Guide language must be zh or en")
    slots = season_rules.slots(season)
    if mode in ("team", "distributed"):
        name = mode
    elif (mode == "robot" and robot in slots) or (mode == "adaptive" and robot in model_ids() and season_rules.legal(robot, season)):
        name = f"{'adaptive-' if mode == 'adaptive' else ''}robot-{robot}"
    else:
        raise ValueError(f"Guides exist for team, distributed, robot slots {','.join(map(str, slots))} and this season's adaptive models")
    folder = ROOT / "docs/agent_guides" / lang
    return (folder / f"{name}.md") if season == season_rules.LEGACY_SEASON else (folder / season / f"{name}.md")


def write_references(path, metadata):
    from .engine import arena_map
    season = manifest_season(metadata)
    shutil.copyfile(SDK, path / "sdk.py")
    shutil.copyfile(Path(__file__).with_name("author_client.py"), path / "arena.py")
    rules = json.loads((ROOT / "config/rules.json").read_text(encoding="utf-8"))
    write_json(path / "arena.json", {"season":season,"map":arena_map(season_rules.map_of(season)),"specs":season_rules.specs(season),
        "rulebook_rules":season_rules.rulebook(season),"simulation_assumptions":season_rules.simulation(season),
        "robot_designs":rules.get("robot_designs",{})})
    write_json(path / "agent.json", metadata)
    if metadata['mode']=='team':
        lineup=normalize_team_lineup(metadata.get('team_lineup'),season)
        write_json(path/'lineup.json',lineup)
    # Each competition receives its own mode/slot guide.
    shutil.copyfile(ROOT / "docs/STRATEGY_SHOWCASE.md", path / "SHOWCASE.md")
    shutil.copyfile(guide_path(metadata["mode"], metadata.get("robot"), "zh", season), path / "AGENT_GUIDE.md")
    shutil.copyfile(guide_path(metadata["mode"], metadata.get("robot"), "en", season), path / "AGENT_GUIDE.en.md")
    if metadata['mode']=='team':
        from scripts.build_agent_guides import render
        for lang,name in [('zh','AGENT_GUIDE.md'),('en','AGENT_GUIDE.en.md')]:
            (path/name).write_text(render(lang,team_lineup=lineup,season=season),encoding='utf-8')
    minutes = metadata["training"]["time_limit_seconds"] // 60
    slots = season_rules.slots(season)
    count_zh = {5: "五", 6: "六"}.get(len(slots), str(len(slots)))
    count_en = {5: "five", 6: "six"}.get(len(slots), str(len(slots)))
    listed = "、".join(map(str, slots))
    listed_en = ", ".join(map(str, slots[:-1])) + f" and {slots[-1]}"
    if metadata["mode"] == "team":
        zh, en = f"全队赛（{season} 赛季）：你控制己方 {listed} 号全部{count_zh}台机器人", f"Team Competition ({season} season): you control all {count_en} of your robots, slots {listed_en}"
    elif metadata["mode"] == "distributed":
        zh, en = f"{count_zh}机协同（{season} 赛季）：由你独立开发{count_zh}份策略，比赛时每份只控制对应机器人，各自维护 state，可通过 messages 与队友通信", \
            f"Distributed Squad ({season} season): you develop {count_en} strategies; each controls its own robot in a separate process with private state and may communicate via messages"
    elif metadata["mode"] == "adaptive":
        zh, en = f"自由适配：你只开发一台 {metadata['robot']} 型机器人的策略。开发时不预告最终阵容；比赛时可读取完整己方阵容，可能有重复型号，只控制 controlled_ids 中的自身实例", \
            f"Adaptive Robot: develop one model {metadata['robot']} robot policy. The final lineup is not disclosed during development; the match observation includes the full current own lineup, possibly with duplicates. Control only your own instance from controlled_ids"
    else:
        zh, en = f"单机器人赛：你只控制己方 {metadata['robot']} 号，其余{len(slots)-1}台由其他参赛者的程序控制", \
            f"Single-Robot Competition: you control only your slot {metadata['robot']}; other participants' programs control the other {len(slots)-1}"
    files = "、".join(editable_files(metadata["mode"], season))
    files_en = ", ".join(editable_files(metadata["mode"], season))
    lineup_context={'zh':'','en':''}
    if metadata['mode']=='team':
        from scripts.build_agent_guides import team_task_context
        lineup_context={lang:team_task_context(lang,rules,lineup,season) for lang in ('zh','en')}
    task = f"""# 机器人策略对战任务

{zh}。以 observation["controlled_ids"] 为准。
{lineup_context['zh']}
完整规则与接口见 AGENT_GUIDE.md；AGENT_GUIDE.en.md 是内容相同的英文版，读其中一份即可。
在 {files} 中实现 act(observation, state)，返回 (actions, new_state)。只修改 {files} 和 showcase.json；practice/ 可保存练习产物，其他文件受保护。
策略完成后、提交前，按 SHOWCASE.md 写 showcase.json：策略库用它直观展示你的打法（风格、阶段、路线、关键决策），不展示代码。没有合格的 showcase.json 不能提交。
组织者开始计时后，总开发时间为 {minutes} 分钟，阅读、编码、练习和提交都计入；用 python3 -B arena.py status 查看剩余时间。
用 arena.py 的 test、results、replay、submit 练习与提交。尽早提交一次；截止前最后一次成功提交即参赛版本。
不要查看其他参赛者的代码与私有状态。比赛中每次决策限时 200 ms。

# Robot Strategy Battle Task

{en}. Always trust observation["controlled_ids"].
{lineup_context['en']}
The full rules and interface are in AGENT_GUIDE.en.md; AGENT_GUIDE.md is the same guide in Chinese. Read either one.
Implement act(observation, state) in {files_en}, returning (actions, new_state). Edit only {files_en} and showcase.json; practice/ may hold practice output, and every other file is protected.
When the strategy is finished, before submitting, write showcase.json as described in SHOWCASE.md. The strategy gallery uses it to present how you play (style, phases, routes, key decisions) instead of your code. Submissions without a valid showcase.json are refused.
Once the organizer starts the clock you have {minutes} minutes in total for reading, coding, practice and submitting; check the remaining time with python3 -B arena.py status.
Use the test, results, replay and submit commands of arena.py. Submit early; your last successful submission before the deadline is your entry.
Do not look at other participants' code or private state. Each decision in a match is limited to 200 ms.
"""
    (path / "AGENT_TASK.md").write_text(task,encoding="utf-8")
    (path / "practice").mkdir(exist_ok=True)


def protected_hashes(path, editable=("strategy.py",)):
    return {p.name:sha(p) for p in path.iterdir() if p.is_file() and p.name not in editable and p.name not in OPTIONAL_FILES}


def scaffold(path, mode="team", robot=None, registry=None, minutes=90, training_url="http://127.0.0.1:8765", team_lineup=None, season=None):
    path = Path(path).resolve()
    if path.exists():
        raise ValueError("Workspace already exists")
    if season is None:
        season = season_rules.infer_season(team_lineup.keys()) if mode == "team" and team_lineup else season_rules.LEGACY_SEASON
    slots = season_rules.slots(season)
    if mode not in ("team", "robot", "distributed", "adaptive") or (mode == "robot" and robot not in slots) or (mode == "adaptive" and (robot not in model_ids() or not season_rules.legal(robot, season))) or (mode not in ("robot", "adaptive") and robot is not None):
        raise ValueError(f"Use team/distributed without --robot; robot/adaptive requires --robot model legal in {season} (slots {','.join(map(str, slots))})")
    lineup=normalize_team_lineup(team_lineup,season) if mode=='team' else None
    if mode!='team' and team_lineup is not None:raise ValueError('A shared team lineup is only for team mode')
    path.mkdir(parents=True)
    editable = editable_files(mode, season)
    for name in editable:
        (path / name).write_text(STARTER_STRATEGY, encoding='utf-8')
    metadata = {"api_version":3,"rules_profile":season_rules.rules_profile(season),"mode":mode,"robot":robot,
        "editable":editable,"output_directories":["practice"],"training":training_metadata(minutes,training_url)}
    if mode=='team':metadata['team_lineup']=lineup
    write_references(path, metadata)
    registry = Path(registry or data_path(".arena_host") / "workspaces.json")
    registry.parent.mkdir(parents=True, exist_ok=True)
    with locked_registry(registry) as records:
        records[str(path)] = {**metadata,"protected":protected_hashes(path, editable)}
    return path


def start_training(workspace, registry=None):
    path = Path(workspace).resolve()
    registry = Path(registry or data_path(".arena_host/workspaces.json"))
    validate_workspace(path,registry,require_window=False)
    with locked_registry(registry) as records:
        record = records[str(path)]
        training = record.get("training")
        if training is None:
            raise ValueError("Refresh the workspace references before starting training")
        if training["started_at"] is not None:
            raise ValueError("Development window has already started and cannot be reset")
        training["started_at"] = time.time()
        training["deadline_at"] = training["started_at"] + training["time_limit_seconds"]
        metadata = {key:value for key,value in record.items() if key != "protected"}
        write_json(path / "agent.json",metadata)
        record["protected"]["agent.json"] = sha(path / "agent.json")
        return {"session_id":training["session_id"],"time_limit_minutes":training["time_limit_seconds"]//60,
                "deadline_utc":datetime.datetime.fromtimestamp(training["deadline_at"],datetime.timezone.utc).isoformat()}


def refresh_workspace(workspace, registry=None):
    path = Path(workspace).resolve()
    registry = Path(registry or data_path(".arena_host/workspaces.json"))
    with locked_registry(registry) as records:
        record = records[str(path)]
        metadata = {key:value for key,value in record.items() if key != "protected"}
        metadata.setdefault("training",training_metadata())
        metadata["output_directories"] = ["practice"]
        write_references(path,metadata)
        records[str(path)] = {**metadata,"protected":protected_hashes(path, editable_files(metadata["mode"], manifest_season(metadata)))}
    return path


def save_submission(output, mode, entries, robot=None, team_lineup=None, showcase=None, profile=None, season=None):
    output = Path(output).resolve()
    if output.exists():
        raise ValueError("Submission exists; choose a new version name")
    if season is None:
        season = season_rules.infer_season(team_lineup.keys()) if mode == "team" and team_lineup else \
            season_rules.infer_season(entries.keys()) if mode in ("squad", "distributed") and len(entries) in (5, 6) else season_rules.LEGACY_SEASON
    lineup=normalize_team_lineup(team_lineup,season) if mode=='team' else None
    if mode!='team' and team_lineup is not None:raise ValueError('A shared team lineup is only for team mode')
    if set(entries) != submission_keys(mode, robot, season=season):
        raise ValueError("Entries do not match the selected control mode")
    if any(p.is_symlink() or not p.is_file() or p.stat().st_size > SOURCE_LIMIT for p in entries.values()):
        raise ValueError(f"Strategy must be a regular file of at most {SOURCE_LIMIT // 1024} KiB")
    sources = {key: p.read_bytes() for key, p in entries.items()}
    for source in sources.values():
        compile(source, "strategy.py", "exec")
    sidecars = {}
    if showcase is not None:
        sidecars[SHOWCASE_FILE] = parse_showcase(showcase) if isinstance(showcase, (str, bytes)) else parse_showcase(json.dumps(showcase))
    if profile is not None:
        sidecars[PROFILE_FILE] = validate_profile(profile)
    output.mkdir(parents=True)
    manifest = {"api_version": 3, "rules_profile":season_rules.rules_profile(season), "mode": mode, "robot": robot, "entries": {}, "hashes": {}}
    if mode=='team':manifest['team_lineup']=lineup
    for slot, source in sources.items():
        name = "strategy.py" if mode in ("team", "robot", "adaptive") else f"robot_{slot}.py"
        (output / name).write_bytes(source)
        manifest["entries"][slot] = name
        manifest["hashes"][name] = hashlib.sha256(source).hexdigest()
    manifest["version"] = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()[:16]
    write_json(output / "manifest.json", manifest)
    # Display sidecars are not hashed: the code alone is the version.
    for name, value in sidecars.items():
        write_json(output / name, value)
    return manifest


def validate_workspace(workspace, registry=None, require_window=True):
    path = Path(workspace).resolve()
    registry = Path(registry or data_path(".arena_host") / "workspaces.json")
    records = json.loads(registry.read_text(encoding="utf-8"))
    if str(path) not in records:
        raise ValueError("Unknown workspace; use new-agent first")
    record = records[str(path)]
    editable = editable_files(record["mode"], manifest_season(record))
    expected = set(record["protected"]) | set(editable)
    actual = {p.name for p in path.iterdir()}
    for name in OPTIONAL_FILES:
        p = path / name
        if name in actual:
            if p.is_symlink() or not p.is_file() or p.stat().st_size > SHOWCASE_LIMIT:
                raise ValueError(f"{name} must be a regular file of at most {SHOWCASE_LIMIT // 1024} KiB")
            actual.discard(name)
    for name in record.get("output_directories",[]):
        p = path / name
        if p.exists():
            if p.is_symlink() or not p.is_dir():
                raise ValueError("Practice output must be an ordinary directory")
            actual.discard(name)
    if actual != expected:
        raise ValueError(f"Only {', '.join(editable)} is editable; unexpected/missing files: {sorted(actual ^ expected)}")
    for name, digest in record["protected"].items():
        p = path / name
        if p.is_symlink() or not p.is_file() or sha(p) != digest:
            raise ValueError(f"Protected file changed: {name}")
    for name in editable:
        strategy = path / name
        if strategy.is_symlink() or not strategy.is_file() or strategy.stat().st_size>SOURCE_LIMIT:
            raise ValueError(f"Strategy must be a regular file of at most {SOURCE_LIMIT // 1024} KiB")
    if require_window:
        check_training_window(record)
    return record


def submit(workspace, output, registry=None, profile=None):
    path = Path(workspace).resolve()
    record = validate_workspace(path,registry)
    showcase = workspace_showcase(path)
    profile = profile or {"agent": showcase.get("generated_by") or {"provider": "unknown"}, "created_at": time.time()}
    return save_submission(output, record["mode"], workspace_entries(path, record), record["robot"],team_lineup=record.get('team_lineup'),
                           showcase=showcase, profile=profile, season=manifest_season(record))


def load_submission(path):
    path = Path(path).resolve()
    manifest = json.loads((path / "manifest.json").read_text(encoding="utf-8"))
    mode = manifest.get("mode")
    season = manifest_season(manifest)
    slots = season_rules.slots(season) if manifest.get("api_version")==3 else tuple(range(1,8))
    expected = submission_keys(mode, manifest.get("robot"), slots)
    if manifest.get("api_version") not in (1, 2, 3) or (mode in ("distributed", "adaptive") and manifest.get("api_version") != 3) or set(manifest["entries"]) != expected:
        raise ValueError("Invalid submission manifest")
    if mode == "robot" and manifest.get("robot") not in slots:
        raise ValueError("Invalid robot slot")
    if mode=='team':normalize_team_lineup(manifest.get('team_lineup'),season)
    # Refuse nested paths and symlinks before resolving or mounting anything.
    for name in manifest["entries"].values():
        p = path / name
        if Path(name).name != name or p.is_symlink() or not p.is_file() or p.stat().st_size > SOURCE_LIMIT:
            raise ValueError("Invalid strategy path")
        if sha(p) != manifest["hashes"].get(name):
            raise ValueError(f"Submitted strategy changed: {name}")
    version_body = {k: v for k, v in manifest.items() if k != "version"}
    version = hashlib.sha256(json.dumps(version_body, sort_keys=True).encode()).hexdigest()[:16]
    if version != manifest.get("version"):
        raise ValueError("Submission manifest changed")
    return manifest, {key: path / name for key, name in manifest["entries"].items()}


def assemble(inputs, output, season=season_rules.LEGACY_SEASON):
    slots = season_rules.slots(season)
    if len(inputs) != len(slots):
        raise ValueError(f"Exactly {len(slots)} robot submissions are required, ordered {','.join(map(str, slots))}")
    entries = {}
    for i, source in zip(slots, inputs):
        manifest, files = load_submission(source)
        if manifest["mode"] != "robot" or manifest["robot"] != i or manifest_season(manifest) != season:
            raise ValueError(f"Input {i} must be a {season} robot {i} submission")
        entries[str(i)] = files[str(i)]
    return save_submission(output, "squad", entries, season=season)
