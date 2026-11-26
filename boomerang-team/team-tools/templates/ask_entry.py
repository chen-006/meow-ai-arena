"""队长目录里的 ask.cmd 入口模板。

部署到队长工作目录时，将 TOOLS_DIR 和 TEAM_JSON 改为本机实际路径（或通过环境变量覆盖）。
本入口会阻止队长传入 --config 覆盖队伍配置，并把 `ask.cmd 队员名 ...` 规范化为 `team_cli.py ask --config ...`。
"""
import os
import runpy
import sys
from pathlib import Path

TOOLS_DIR = Path(os.environ.get("TEAM_TOOLS_DIR", Path(__file__).resolve().parent.parent))
TEAM_JSON = Path(os.environ.get("TEAM_CONFIG_JSON", Path(__file__).resolve().parent / "team.json"))

sys.path.insert(0, str(TOOLS_DIR))
if any(x == "--config" or x.startswith("--config=") for x in sys.argv[1:]):
    raise SystemExit("配置由主办方固定")
action = sys.argv.pop(1) if len(sys.argv) > 1 and sys.argv[1] in ("ask", "wait", "status") else "ask"
sys.argv[1:1] = [action, "--config", str(TEAM_JSON.resolve())]
runpy.run_path(str((TOOLS_DIR / "team_cli.py").resolve()), run_name="__main__")
