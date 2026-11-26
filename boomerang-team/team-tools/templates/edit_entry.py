"""队长目录里的 亲手改.cmd 入口模板。

部署到队长工作目录时，将 TOOLS_DIR、MATCH_JSON、TEAM_NAME 改为实际值（或通过环境变量覆盖）。
"""
import os
import sys
from pathlib import Path

TOOLS_DIR = Path(os.environ.get("TEAM_TOOLS_DIR", Path(__file__).resolve().parent.parent))
MATCH_JSON = os.environ.get("TEAM_MATCH_JSON", str((Path(__file__).resolve().parent / "match.json").resolve()))
TEAM_NAME = os.environ.get("TEAM_NAME", "Demo队")

sys.path.insert(0, str(TOOLS_DIR))
import team12  # noqa: E402

raise SystemExit(team12.hand_edit_cli(MATCH_JSON, TEAM_NAME, sys.argv[1:]))
