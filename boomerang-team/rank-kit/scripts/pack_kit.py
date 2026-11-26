#!/usr/bin/env python3
"""将 rank-kit/ 中的 1v1 排位赛选手包源文件打包为可分发的 kit.zip。"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

KIT_ROOT = Path(__file__).resolve().parent.parent
TOP_FILES = ("player.cpp", "config.json", "arena.cpp", "dart.hpp", "README.md", "play.py", "viewer.html", "LICENSE")


def build_kit_zip(output: Path) -> Path:
    cfg = json.loads((KIT_ROOT / "config.json").read_text(encoding="utf-8"))
    entries: list[str] = list(TOP_FILES)
    entries += [f"sparring/{level}.cpp" for level in cfg["levels"]]
    entries += [f"maps/{map_id}.txt" for map_id in cfg["maps"]]

    output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(output, "w", compression=ZIP_DEFLATED) as archive:
        for rel in entries:
            src = KIT_ROOT / rel
            if not src.is_file():
                raise FileNotFoundError(f"缺少选手包文件：{src}")
            archive.write(src, arcname=rel)
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, nargs="?", default=KIT_ROOT / "dist" / "kit.zip",
                        help="输出 zip 路径（默认 rank-kit/dist/kit.zip）")
    out = build_kit_zip(parser.parse_args().output)
    print(f"[pack_kit] wrote {out.resolve()} ({out.stat().st_size:,} bytes)")


if __name__ == "__main__":
    main()
