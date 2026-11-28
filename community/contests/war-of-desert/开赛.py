"""Desert Arena 开赛计时与强制收卷。

**已废弃，请勿用于正式赛。** 计时/暂停/收卷已统一由仓库根目录的 赛务.py
接管（裁判台「编码」页签调用的就是它），赛务记录也改名为
`<选手目录>/.赛务.json` 并按选手目录分别保存。本脚本仍按旧的单工作区模型
冻结 `workspace/submission/decide.go`，与现行交付物
`submission/<选手名>_bot.go` 不一致，保留仅为查阅历史流程。

组织方脚本，负责：
  1. 重置选手 clock.py 的计时起点；
  2. 暂停 / 恢复计时，并把审计记录写入赛务记录.json；
  3. 到点后冻结 workspace/submission/decide.go，记录 SHA-256、用时和是否超时；
  4. 选手端用 workspace/clock.py 随时查看剩余时间。

常用命令：

  python -X utf8 开赛.py                 自检计时文件并开始/重置计时
  python -X utf8 开赛.py --status        查看当前计时
  python -X utf8 开赛.py --pause         从现在开始暂停
  python -X utf8 开赛.py --pause=14:33 --note="模型限额"
  python -X utf8 开赛.py --resume        恢复计时
  python -X utf8 开赛.py --freeze        立即冻结 decide.go
  python -X utf8 开赛.py --freeze-due    只冻结已到 120 分钟的目录
  python -X utf8 开赛.py --all --status  查看全部选手目录
  python -X utf8 开赛.py --all --freeze-due

默认时间：90 分钟为目标完成时间，120 分钟为强制收卷。超过 120 分钟仍会冻结
当前文件以便留档，但记录 accepted=false，正式成绩不计。

目标目录默认是根目录 workspace；未来多位选手可放在 bots/<名字>/workspace
或 rts-contest@<模型>/workspace。赛务记录保存在根目录 赛务记录.json。
"""

from __future__ import annotations

import argparse
import ctypes
import hashlib
import json
import os
import shutil
import sys
import time
from datetime import datetime
from pathlib import Path

HERE = Path(__file__).resolve().parent
TARGET_MINUTES = 90
LIMIT_MINUTES = 120
SUBMISSION_REL = Path("workspace") / "submission" / "decide.go"
CLOCK_REL = Path("workspace") / "clock.py"
SERVICE_LOG = HERE / "赛务记录.json"
WIN_EPOCH_OFFSET = 11644473600  # 1601-01-01 到 1970-01-01 的秒数


def load_service_log() -> dict:
    try:
        data = json.loads(SERVICE_LOG.read_text(encoding="utf-8-sig"))
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def save_service_log(data: dict) -> None:
    tmp = SERVICE_LOG.with_name(SERVICE_LOG.name + ".tmp")
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    os.replace(tmp, SERVICE_LOG)


def born(path: Path) -> float:
    st = Path(path).stat()
    return float(getattr(st, "st_birthtime", None) or st.st_ctime)


def find_targets() -> list[Path]:
    """发现包含 workspace/clock.py 的选手目录；单工作区时返回根目录自身。"""
    found: list[Path] = []
    if (HERE / CLOCK_REL).is_file():
        found.append(HERE)

    patterns = ("rts-contest@*", "选手@*", "bots/*", "submissions/*")
    for pattern in patterns:
        for p in HERE.glob(pattern):
            if p.is_dir() and (p / CLOCK_REL).is_file() and p not in found:
                found.append(p)
    return sorted(found, key=lambda p: str(p).lower())


def resolve_target(name: str | None) -> Path:
    targets = find_targets()
    if name:
        p = Path(name)
        if not p.is_absolute():
            p = HERE / p
        p = p.resolve()
        if not (p / CLOCK_REL).is_file():
            print(f"在 {p} 下没有找到 {CLOCK_REL}，不是完整选手目录。")
            raise SystemExit(1)
        return p
    if len(targets) == 1:
        return targets[0]
    if not targets:
        print(f"没有找到任何包含 {CLOCK_REL} 的选手目录。")
        raise SystemExit(1)
    print("发现多个选手目录，请指定一个：")
    for t in targets:
        label = "." if t == HERE else t.relative_to(HERE)
        print(f"  python -X utf8 开赛.py \"{label}\" --status")
    raise SystemExit(1)


class Site:
    """一个选手目录，含 workspace/clock.py 与 workspace/submission/decide.go。"""

    def __init__(self, target: Path):
        self.target = target.resolve()
        self.workspace = self.target / "workspace"
        self.clock = self.workspace / "clock.py"
        self.submission = self.workspace / "submission" / "decide.go"
        self.frozen_dir = self.target / "submissions"
        self.frozen_file = self.frozen_dir / "decide.go"
        self.freeze_record = self.frozen_dir / "交卷记录.json"

    @property
    def key(self) -> str:
        return self.target.name if self.target != HERE else "主工作区"

    def clock_start(self) -> float:
        entry = load_service_log().get(self.key)
        if isinstance(entry, dict):
            try:
                return float(entry["started_at"])
            except (KeyError, TypeError, ValueError):
                pass
        return born(self.clock)

    def has_started(self) -> bool:
        entry = load_service_log().get(self.key)
        return isinstance(entry, dict) and isinstance(entry.get("started_at"), (int, float))

    def record_start(self, ts: float) -> None:
        data = load_service_log()
        old = data.get(self.key)
        entry = {
            "target": self.key,
            "started_at": ts,
            "recorded_at": time.time(),
        }
        if isinstance(old, dict):
            if isinstance(old.get("pauses"), list):
                entry["pauses"] = old["pauses"]
            entry["restarts"] = int(old.get("restarts", 0)) + 1
            if isinstance(old.get("frozen"), dict):
                entry["frozen"] = old["frozen"]
        data[self.key] = entry
        save_service_log(data)

    def set_creation_time_windows(self, ts: float) -> None:
        file_write_attributes = 0x0100
        open_existing = 3
        file_attribute_normal = 0x80
        invalid_handle = ctypes.c_void_p(-1).value

        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        k32.CreateFileW.restype = ctypes.c_void_p
        k32.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32,
                                    ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        k32.SetFileTime.restype = ctypes.c_int
        k32.SetFileTime.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
        k32.CloseHandle.argtypes = [ctypes.c_void_p]

        handle = k32.CreateFileW(str(self.clock), file_write_attributes, 0, None,
                                 open_existing, file_attribute_normal, None)
        if handle in (None, invalid_handle):
            raise OSError(ctypes.get_last_error(), "打开 clock.py 失败")
        try:
            filetime = ctypes.c_ulonglong(int((ts + WIN_EPOCH_OFFSET) * 10_000_000))
            if not k32.SetFileTime(handle, ctypes.byref(filetime), None, None):
                raise OSError(ctypes.get_last_error(), "设置 clock.py 创建时间失败")
        finally:
            k32.CloseHandle(handle)

    def sync_clock_time(self, ts: float) -> None:
        """把 clock.py 显示的逻辑起点同步到 ts。"""
        if os.name == "nt":
            self.set_creation_time_windows(ts)
        else:
            data = self.clock.read_bytes()
            tmp = self.clock.with_name(".clock.py.tmp")
            tmp.write_bytes(data)
            os.replace(tmp, self.clock)

    def reset_clock(self) -> None:
        started = time.time()
        self.sync_clock_time(started)
        self.record_start(started)

    def paused_info(self) -> dict | None:
        entry = load_service_log().get(self.key)
        if isinstance(entry, dict) and isinstance(entry.get("paused"), dict):
            return entry["paused"]
        return None

    def used_minutes(self) -> float:
        info = self.paused_info()
        if info is not None:
            try:
                return max(0.0, float(info["used_minutes"]))
            except (KeyError, TypeError, ValueError):
                pass
        return max(0.0, time.time() - self.clock_start()) / 60

    def pause(self, at: float, note: str = "") -> int:
        if not self.has_started():
            print(f"【{self.key}】尚未开赛，无法暂停。")
            return 1
        info = self.paused_info()
        if info is not None:
            print(f"【{self.key}】已经处于暂停状态。")
            return 1
        start = self.clock_start()
        if at < start:
            print(f"暂停时刻 {local_time(at)} 早于开赛时刻 {local_time(start)}。")
            return 1
        used = (at - start) / 60
        data = load_service_log()
        entry = data.get(self.key)
        if not isinstance(entry, dict):
            entry = {"target": self.key, "started_at": start, "recorded_at": time.time()}
        entry["paused"] = {
            "at": at,
            "started_at": start,
            "used_minutes": used,
            "recorded_at": time.time(),
        }
        note = (note or "").strip()[:120]
        if note:
            entry["paused"]["note"] = note
        data[self.key] = entry
        save_service_log(data)
        try:
            self.sync_clock_time(time.time() - used * 60)
        except OSError:
            pass
        print(f"【{self.key}】计时已暂停于 {local_time(at)}。")
        print(f"  暂停前已用 {used:.1f} 分钟；暂停期间不计时。")
        if note:
            print(f"  原因：{note}")
        print(f"  恢复：python -X utf8 开赛.py \"{self.relative_label()}\" --resume")
        return 0

    def resume(self) -> int:
        if not self.has_started():
            print(f"【{self.key}】尚未开赛，无法恢复。")
            return 1
        info = self.paused_info()
        if info is None:
            print(f"【{self.key}】没有暂停记录，无法恢复。")
            return 1
        try:
            used = float(info["used_minutes"])
            paused_at = float(info["at"])
        except (KeyError, TypeError, ValueError):
            print("暂停记录损坏，无法恢复。")
            return 1
        now = time.time()
        start = now - used * 60
        data = load_service_log()
        entry = data.get(self.key)
        if not isinstance(entry, dict):
            entry = {"target": self.key, "recorded_at": now}
        entry["started_at"] = start
        pauses = entry.get("pauses")
        if not isinstance(pauses, list):
            pauses = []
        pauses.append({
            "from": paused_at,
            "to": now,
            "minutes": max(0.0, (now - paused_at) / 60),
            "note": str(info.get("note") or ""),
        })
        entry["pauses"] = pauses
        entry.pop("paused", None)
        data[self.key] = entry
        save_service_log(data)
        self.sync_clock_time(start)
        print(f"【{self.key}】计时已恢复于 {local_time(now)}。")
        print(f"  本次暂停 {max(0.0, (now - paused_at) / 60):.1f} 分钟不计入用时；接续 {used:.1f} 分钟。")
        print(f"  {TARGET_MINUTES} 分钟目标：{local_time(start + TARGET_MINUTES * 60)}")
        print(f"  {LIMIT_MINUTES} 分钟强制收卷：{local_time(start + LIMIT_MINUTES * 60)}")
        return 0

    def relative_label(self) -> str:
        return "." if self.target == HERE else str(self.target.relative_to(HERE))

    def show_status(self) -> None:
        start = self.clock_start()
        used = self.used_minutes()
        print(f"【{self.key}】")
        entry = load_service_log().get(self.key)
        frozen = entry.get("frozen") if isinstance(entry, dict) else None
        if isinstance(frozen, dict):
            frozen_at = local_time(float(frozen.get("frozen_at", start)))
            frozen_used = float(frozen.get("used_minutes", used))
            print(f"  状态：已冻结收卷（{frozen_at}，用时 {frozen_used:.1f} 分钟）")
            if frozen.get("missing_submission"):
                print("  注意：冻结时未找到 workspace/submission/decide.go。")
            if not frozen.get("accepted", True):
                print(f"  注意：超过 {LIMIT_MINUTES} 分钟，正式成绩不计。")
            return

        if not (isinstance(entry, dict) and isinstance(entry.get("started_at"), (int, float))):
            print("  状态：未开赛（运行 开赛.py 开始计时）")
            return

        info = self.paused_info()
        if info is not None:
            try:
                at = float(info["at"])
            except (KeyError, TypeError, ValueError):
                at = start
            print(f"  状态：已暂停（暂停于 {local_time(at)}）")
            note = str(info.get("note") or "").strip()
            if note:
                print(f"  原因：{note}")
            print(f"  暂停前已用 {used:.1f} 分钟，恢复后从这里接着算。")
            print(f"  恢复：python -X utf8 开赛.py \"{self.relative_label()}\" --resume")
            return

        print(f"  计时起点：{local_time(start)}")
        print(f"  {TARGET_MINUTES} 分钟目标：{local_time(start + TARGET_MINUTES * 60)}")
        print(f"  {LIMIT_MINUTES} 分钟强制收卷：{local_time(start + LIMIT_MINUTES * 60)}")
        print(f"  已用 {used:.1f} 分钟。", end="")
        if used < TARGET_MINUTES:
            print(f"距目标还有 {TARGET_MINUTES - used:.1f} 分钟。")
        elif used < LIMIT_MINUTES:
            print(f"距强制收卷还有 {LIMIT_MINUTES - used:.1f} 分钟。")
        else:
            print("已到强制收卷时间，请立即冻结。")

    def freeze(self, force: bool = False, due_only: bool = False) -> int:
        entry = load_service_log().get(self.key)
        if not (isinstance(entry, dict) and isinstance(entry.get("started_at"), (int, float))):
            print(f"【{self.key}】尚未开赛，无法收卷。")
            return 1
        if self.freeze_record.is_file() and not force:
            print(f"【{self.key}】已有收卷记录：{self.freeze_record}")
            print("如确需重冻，加 --force；正式赛中重冻应视为异常并留痕。")
            return 1

        used = self.used_minutes()
        if due_only and used < LIMIT_MINUTES:
            print(f"【{self.key}】未到时：已用 {used:.1f} 分钟，跳过强制收卷。")
            return 1

        self.frozen_dir.mkdir(exist_ok=True)
        accepted = used <= LIMIT_MINUTES
        missing = not self.submission.is_file()
        digest = None
        size = None
        frozen_path = None

        if missing:
            accepted = False
            print(f"【{self.key}】警告：找不到 {self.submission}，按未提交记录。")
        else:
            shutil.copyfile(self.submission, self.frozen_file)
            digest = hashlib.sha256(self.frozen_file.read_bytes()).hexdigest()
            size = self.frozen_file.stat().st_size
            frozen_path = str(self.frozen_file)

        record = {
            "target": self.key,
            "source": str(self.submission),
            "frozen_file": frozen_path,
            "frozen_at": time.time(),
            "clock_start": self.clock_start(),
            "used_minutes": used,
            "target_minutes": TARGET_MINUTES,
            "limit_minutes": LIMIT_MINUTES,
            "accepted": accepted,
            "missing_submission": missing,
            "sha256": digest,
            "bytes": size,
        }
        self.freeze_record.write_text(json.dumps(record, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")

        data = load_service_log()
        entry = data.get(self.key)
        if not isinstance(entry, dict):
            entry = {"target": self.key}
            data[self.key] = entry
        entry["frozen"] = {
            "frozen_at": record["frozen_at"],
            "used_minutes": used,
            "accepted": accepted,
            "missing_submission": missing,
            "sha256": digest,
            "bytes": size,
        }
        save_service_log(data)

        print(f"【{self.key}】强制收卷记录：{self.freeze_record}")
        print(f"  用时 {used:.1f} 分钟；accepted={accepted}；missing_submission={missing}。")
        if digest:
            print(f"  SHA-256 {digest[:16]}…，大小 {size} 字节。")
        return 0 if accepted and not missing else 2


def local_time(ts: float) -> str:
    return datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")


def parse_when(text: str) -> float:
    """解析 now / 14:33 / 14:33:20 / 2026-11-12 14:33；只有时刻时按今天算。"""
    t = (text or "").strip()
    if not t or t.lower() in ("now", "现在"):
        return time.time()
    today = datetime.now()
    for fmt, has_date in (("%H:%M:%S", False), ("%H:%M", False),
                          ("%Y-%m-%d %H:%M:%S", True), ("%Y-%m-%d %H:%M", True)):
        try:
            dt = datetime.strptime(t, fmt)
        except ValueError:
            continue
        if not has_date:
            dt = dt.replace(year=today.year, month=today.month, day=today.day)
        return dt.timestamp()
    raise SystemExit(f"看不懂的时间 {text!r}，请写成 --pause=14:33 或 --pause=\"2026-11-12 14:33\"。")


def main() -> int:
    ap = argparse.ArgumentParser(description="Desert Arena 开赛计时与强制收卷")
    ap.add_argument("target", nargs="?", help="选手目录；只有一个选手目录时可省略")
    ap.add_argument("--status", action="store_true", help="只查看计时，不重置")
    ap.add_argument("--pause", nargs="?", const="now", default=None, metavar="时间", help="暂停计时")
    ap.add_argument("--note", default="", metavar="说明", help="暂停原因，写入赛务记录")
    ap.add_argument("--resume", action="store_true", help="恢复计时")
    ap.add_argument("--freeze", action="store_true", help="立即冻结 decide.go 并记录哈希")
    ap.add_argument("--freeze-due", action="store_true", help="只冻结已到 120 分钟的目录")
    ap.add_argument("--force", action="store_true", help="允许覆盖已有收卷记录")
    ap.add_argument("--all", action="store_true", help="作用于全部选手目录")
    args = ap.parse_args()

    modes = {
        "--status": args.status,
        "--pause": args.pause is not None,
        "--resume": args.resume,
        "--freeze": args.freeze,
        "--freeze-due": args.freeze_due,
    }
    chosen = [name for name, on in modes.items() if on]
    if len(chosen) > 1:
        ap.error("请且只选择一种模式：" + " / ".join(modes))
    start_mode = not chosen
    if args.note and args.pause is None:
        ap.error("--note 只能与 --pause 一起使用")
    if args.force and start_mode:
        ap.error("--force 只能与 --freeze / --freeze-due 一起使用")

    if args.all:
        if args.pause is not None or args.resume or start_mode:
            ap.error("--all 只能配 --status / --freeze / --freeze-due")
        targets = find_targets()
        if not targets:
            print("没有找到任何选手目录。")
            return 1
        sites = [Site(t) for t in targets]
        if args.status:
            for i, site in enumerate(sites):
                if i:
                    print()
                site.show_status()
            return 0
        codes = [site.freeze(force=args.force, due_only=args.freeze_due) for site in sites]
        return max(codes, default=0)

    site = Site(resolve_target(args.target))
    if args.pause is not None:
        return site.pause(parse_when(args.pause), args.note)
    if args.resume:
        return site.resume()
    if args.status:
        site.show_status()
        return 0
    if args.freeze:
        return site.freeze(force=args.force, due_only=False)
    if args.freeze_due:
        return site.freeze(force=args.force, due_only=True)

    if not site.clock.is_file():
        print(f"找不到 {site.clock}，选手包不完整。")
        return 1
    data = load_service_log().get(site.key)
    if isinstance(data, dict) and isinstance(data.get("frozen"), dict):
        print(f"【{site.key}】已有收卷记录。正式赛不应重新开赛；如确需重开，请先人工移除赛务记录中的 frozen 字段。")
        return 1
    site.reset_clock()
    start = site.clock_start()
    print()
    print(f"【{site.key}】计时已开始（{site.clock} 从现在起算 0 分钟）。")
    print(f"  开赛时间：{local_time(start)}")
    print(f"  {TARGET_MINUTES} 分钟目标：{local_time(start + TARGET_MINUTES * 60)}")
    print(f"  {LIMIT_MINUTES} 分钟强制收卷：{local_time(start + LIMIT_MINUTES * 60)}")
    print("  选手自查：python -X utf8 workspace/clock.py")
    print("  到点收卷：python -X utf8 开赛.py --freeze-due")
    return 0


if __name__ == "__main__":
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass
    sys.exit(main())
