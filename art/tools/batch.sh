#!/bin/bash
# 并行跑多张，等全部完成。在工作目录里运行：bash batch.sh 任务单.txt
# 任务单每行: tag|promptfile|ref1,ref2,...|size（路径相对工作目录；size 不写默认 2160x3840）
HERE="$(cd "$(dirname "$0")" && pwd)"
mkdir -p logs
while IFS='|' read -r tag pf refs size; do
  [ -z "$tag" ] && continue
  args=""; IFS=',' read -ra RS <<< "$refs"; for r in "${RS[@]}"; do args="$args --ref $r"; done
  python "$HERE/gen.py" --tag "$tag" --prompt "$pf" $args --size "${size:-2160x3840}" > "logs/$tag.out" 2>&1 &
  sleep 3
done < "$1"
wait
for f in $(awk -F'|' '{print $1}' "$1"); do cat "logs/$f.out" | python -c "import sys,json;j=json.loads(sys.stdin.read());print(j['tag'],j['status'],j.get('cost'),j.get('secs'),(j.get('error_body') or '')[:300])"; done
