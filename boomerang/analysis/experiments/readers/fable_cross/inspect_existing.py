import json, sys
from collections import Counter, defaultdict
from pathlib import Path
sys.dont_write_bytecode = True

af = json.load(open("<本机路径，未收录>", encoding="utf-8"))
print("astra62_fable.json keys:", list(af.keys()) if isinstance(af, dict) else f"list len={len(af)}")
rows = af["results"] if isinstance(af, dict) and "results" in af else af
print("sample row:", json.dumps(rows[0], ensure_ascii=False)[:600])

# Aggregate detailed stats for Astra62 vs Fable across all 228 games!
tot_stats = {0: Counter(), 1: Counter()} # 0=astra62, 1=fable? Let's check names or seats!
# Wait: in astra62_fable.json, how are seats mapped? Let's check!
for i in range(4):
    print(f"row {i}: id={rows[i]['id']} map={rows[i].get('map')} steps={rows[i]['steps']} players={[(p['seat'], p.get('bot'), p['rank'], p['cause_name'], p.get('throws'), p.get('slashes'), p.get('blocks'), p.get('recalls')) for p in rows[i]['players']]}")

# Also check 第1组_核查 paired results!
p1_dir = Path("<本机路径，未收录>")
for jf in sorted(p1_dir.glob("*_paired.json")):
    d = json.load(open(jf, encoding="utf-8"))
    print(f"{jf.name}: opp={d['opponent']} tally={d['tally']}")
