import json, subprocess
eng = ["F ENG - MAO", "F BRE S F ENG - MAO", "A LON H", "F LVP - IRI",
       "A MOS H", "F BOT - SWE", "F STP/NC H", "F NWY - NTH"]
cases = {
    "1_法守PIC+舰入MAO": ["A PIC H", "A BUR S A PIC", "F WES - MAO"],
    "2_法A PIC攻BRE切支援": ["A PIC - BRE", "A BUR H", "F WES - MAO"],
}
for name, fra in cases.items():
    json.dump({"ENGLAND": eng, "FRANCE": fra, "RUSSIA": ["A UKR - MOS"]},
              open("假设.json", "w", encoding="utf-8"), ensure_ascii=False)
    print("=== ", name)
    r = subprocess.run(["python", "game.py", "simulate", "假设.json"],
                       capture_output=True, text=True, encoding="utf-8")
    print(r.stdout, r.stderr)
