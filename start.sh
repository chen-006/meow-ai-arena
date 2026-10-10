#!/bin/sh
# 找一个 Python 3.10 以上，打开入口菜单。
cd "$(dirname -- "$0")" || exit 1
for py in python3 python; do
    if command -v "$py" >/dev/null 2>&1 && "$py" -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null; then
        exec "$py" -X utf8 start.py
    fi
done
echo '没有找到 Python 3.10 以上的版本。只想看回放的话，直接用浏览器打开各集 replays/ 里的网页即可。' >&2
echo '第 5 集没有回放，成绩在 warehouse/results/最终成绩.md，用文本编辑器打开即可。' >&2
exit 1
