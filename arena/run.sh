#!/bin/sh
# 找一个 Python 3.10 以上，在本目录运行给定的脚本。不带参数时显示开发用时。
cd "$(dirname -- "$0")" || exit 1
for py in python3 python; do
    if command -v "$py" >/dev/null 2>&1 && "$py" -c 'import sys; sys.exit(sys.version_info < (3, 10))' 2>/dev/null; then
        [ "$#" -eq 0 ] && set -- clock.py
        exec "$py" -X utf8 "$@"
    fi
done
echo '没有找到 Python 3.10 以上的版本。请把情况告诉组织方。' >&2
exit 1
