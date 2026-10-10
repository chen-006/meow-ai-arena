#!/bin/bash
# 终版复测（赛后额外项目）：解压即开赛，45 分钟后把 review/cases 封存到 赛事/终版复测/提交/<裁判>/cases。
#   bash 裁判/终版复测计时.sh "GPT-6 Astra"        # 每个裁判各起一个（后台）
cd "$(dirname "$0")/.." || exit 1
j=$1
d="/c/arena5/终版复测 $j"
out="赛事/终版复测/提交/$j"
mkdir -p "$out"
echo "$j：等待解压…（$(date +%T)）"
while [ ! -d "$d/review" ]; do sleep 3; done
start=$(stat -c %W "$d/review" 2>/dev/null); [ -z "$start" ] || [ "$start" = 0 ] && start=$(date +%s)
echo "{\"裁判\": \"$j\", \"开始\": \"$(date -d @$start +%FT%T)\"}" > "$out/元数据.json"
echo "$j 开赛 $(date -d @$start +%T)，封存 $(date -d @$((start+2700)) +%T)"
while [ "$(date +%s)" -lt $((start+2700)) ] && [ ! -f "$out/.提前收卷" ]; do sleep 5; done
rm -rf "$out/cases"; cp -r "$d/review/cases" "$out/cases"
echo "$j 已封存 $(date +%T)：$(find "$out/cases" -name '*.in' | wc -l) 个用例"
