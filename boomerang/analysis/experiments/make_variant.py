"""把一个补丁套到选手原作上，生成改版程序。

用法：python make_variant.py patches/hydrogen/h_nosync.diff 输出.cpp
      python make_variant.py --all 输出目录      （全部补丁各生成一份）

补丁是"改版程序相对选手原作 player.cpp 的逐行差异"（不带上下文行），
第一行 `# eol=crlf|lf` 记录改版文件的换行风格，`--- a/...` 一行写明原作在 boomerang/ 下的路径。
文件按字节（latin-1）读写，选手程序里的 GBK 注释不会被改坏。
"""
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BOOMERANG = HERE.parents[1]


def apply_patch(diff_path):
    raw = Path(diff_path).read_bytes().decode('latin-1').split('\n')
    eol = raw[0].split('=', 1)[1].strip()
    base_rel = next(l for l in raw if l.startswith('--- a/'))[6:].strip()
    base = (BOOMERANG / base_rel).read_bytes().decode('latin-1').replace('\r', '').split('\n')
    # 逐个补丁块：@@ -起,数 +起,数 @@，之后跟若干 -行、+行（没有上下文行）
    hunks, cur = [], None
    for l in raw:
        m = re.match(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@', l)
        if m:
            cur = [int(m.group(1)), 1 if m.group(2) is None else int(m.group(2)), []]
            hunks.append(cur)
        elif cur is not None and l[:1] == '+' and not l.startswith('+++'):
            cur[2].append(l[1:])
    out = list(base)
    for start, count, new in reversed(hunks):
        i = start if count == 0 else start - 1       # 数为 0 时起点表示"在第 start 行之后"
        out[i:i + count] = new
    text = '\n'.join(out)
    if eol == 'crlf':
        text = text.replace('\n', '\r\n')
    return text.encode('latin-1')


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--all':
        dst = Path(sys.argv[2]); dst.mkdir(parents=True, exist_ok=True)
        for d in sorted((HERE / 'patches').rglob('*.diff')):
            (dst / (d.stem + '.cpp')).write_bytes(apply_patch(d))
            print(d.stem)
    elif len(sys.argv) == 3:
        Path(sys.argv[2]).write_bytes(apply_patch(sys.argv[1]))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
