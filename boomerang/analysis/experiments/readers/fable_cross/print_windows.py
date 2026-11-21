import sys
from pathlib import Path
sys.dont_write_bytecode = True

OUT = Path("<本机路径，未收录>")

def show_window(rep_path, t_start, t_end):
    print(f"\n================ {rep_path.name} (t={t_start}..{t_end}) ================")
    for ln in rep_path.read_text(encoding="utf-8").splitlines():
        if ln.startswith(("MAPNAME ", "SEED ", "PLAYER ", "END ", "RESULT ")):
            print(ln)
        elif ln.startswith(("S ", "A ", "E ")):
            t = int(ln.split()[1])
            if t_start <= t <= t_end:
                print(ln)

af = OUT / "replays_astra62_fable"
show_window(af / "g21.txt", 248, 266)
show_window(af / "g22.txt", 190, 204)
show_window(af / "g23.txt", 205, 222)
show_window(af / "g24.txt", 295, 314)
show_window(af / "g14.txt", 175, 206)

fo = OUT / "replays_fable_opus"
show_window(fo / "g153.txt", 1, 120)
show_window(fo / "g153.txt", 340, 363)
