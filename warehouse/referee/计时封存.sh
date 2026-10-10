#!/bin/bash
# 用 bash 计时、到点再调用一次 python 封存。
#   bash 裁判/计时封存.sh <队> <轮次> <开赛 HH:MM:SS> [替补模型]
# 为什么不用 开局.py 开始：第 2 轮选手（Sonnet、Fable、Opus）清理进程时用了 taskkill /IM python.exe、
# Stop-Process python，会把机器上所有 python 进程一起杀掉，常驻的 python 计时器活不下来。
# 选手提前做完时照常运行 python 裁判/开局.py <队> 收卷 --round N（会写 .已提前收卷，本脚本看到就退出）。
cd "$(dirname "$0")/.." || exit 1
export PYTHONIOENCODING=utf-8
team=$1; rnd=$2; start=$3; model=$4
case $rnd in 1) rec="${team}_主程";; *) rec="${team}_攻手_第${rnd}轮";; esac
flag="赛事/运行记录/$rec/.已提前收卷"
deadline=$(( $(date -d "today $start" +%s) + 45*60 ))
echo "$team 第 $rnd 轮：开赛 $start，封存 $(date -d @$deadline +%T)"
while [ "$(date +%s)" -lt "$deadline" ]; do
  [ -f "$flag" ] && { echo "$team 已提前收卷，计时结束 $(date +%T)"; exit 0; }
  sleep 5
done
echo "！！！$team 第 $rnd 轮到 45 分钟 $(date +%T)：封存，请在工具里停止模型"
python 裁判/开局.py "$team" 收卷 --round "$rnd" --forced ${model:+--model "$model"}
