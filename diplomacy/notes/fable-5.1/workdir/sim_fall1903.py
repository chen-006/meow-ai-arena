import json, subprocess
eng = ["F STP/NC H", "A SWE - FIN", "F SKA - SWE", "F NWG - BAR", "F NTH H"]
cases = {
    "1_俄两路打STP": ["A LVN - STP", "F BOT S A LVN - STP"],
    "2_俄打SWE和STP": ["A LVN - STP", "F BOT - SWE"],
    "3_俄打FIN和STP": ["A LVN - STP", "F BOT - FIN"],
    "4_德国背叛支援俄进SWE": ["A LVN - STP", "F BOT - SWE"],
}
for name, rus in cases.items():
    ger = ["F DEN S F BOT - SWE"] if name.startswith("4") else ["F DEN H"]
    json.dump({"ENGLAND": eng, "RUSSIA": rus, "GERMANY": ger},
              open("假设.json", "w", encoding="utf-8"), ensure_ascii=False)
    print("=== ", name)
    r = subprocess.run(["python", "game.py", "simulate", "假设.json"],
                       capture_output=True, text=True, encoding="utf-8")
    print(r.stdout)
