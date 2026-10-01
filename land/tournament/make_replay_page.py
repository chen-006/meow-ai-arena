"""把 replays/ 里的回放 JSON 嵌进查看器，生成双击即可观看的 replays/index.html。

  python tournament/make_replay_page.py
"""
import json
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8")
HERE = Path(__file__).resolve().parent.parent
viewer = (HERE / "workspace" / "viewer" / "viewer.html").read_text(encoding="utf-8")
items = [{"name": p.name, "data": json.loads(p.read_text(encoding="utf-8"))} for p in sorted((HERE / "replays").glob("*.json"))]
blob = json.dumps(items, ensure_ascii=False, separators=(",", ":")).replace("</", r"<\/")
boot = ("<script>\nconst EMBEDDED = " + blob + ";\n"
        "load(EMBEDDED.map(r => new File([JSON.stringify(r.data)], r.name, {type: 'application/json'})));\n</script>\n</body>")
(HERE / "replays" / "index.html").write_text(viewer.replace("</body>", boot, 1), encoding="utf-8")
print(f"已生成 replays/index.html（{len(items)} 局）")
