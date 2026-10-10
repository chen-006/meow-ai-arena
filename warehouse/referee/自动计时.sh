#!/bin/bash
# 解压即开赛：盯着 6 个比赛文件夹，哪一家解压了比赛包，就把解压时刻记为它的开赛时间，并启动它的 45 分钟封存计时。
#   bash 裁判/自动计时.sh <轮次 2|3> [队A 队B ...]      # 不写队名就是 6 队全部
# 开赛前先 python 裁判/开局.py <队> 准备 --round N，再运行本脚本（后台），然后就可以让用户随意打开会话粘贴提示词。
# 选手提前做完：python 裁判/开局.py <队> 收卷 --round N（各队的封存计时看到 .已提前收卷 会自己退出）。
# 6 家都开赛后本脚本退出；各队的封存计时（裁判/计时封存.sh）继续跑到 45 分钟。
# 为什么用 bash：选手清理进程时可能把机器上的 python 全杀掉（第一次比赛第 2 轮发生过），常驻的 python 计时器活不下来。
cd "$(dirname "$0")/.." || exit 1
export PYTHONIOENCODING=utf-8
rnd=$1; shift
teams=("$@"); [ ${#teams[@]} -eq 0 ] && teams=(队A 队B 队C 队D 队E 队F)
mkdir -p 赛事/运行记录
declare -A started
echo "自动计时：第 $rnd 轮，等待 ${teams[*]} 解压比赛包…（$(date +%T)）"
while :; do
  left=0
  for t in "${teams[@]}"; do
    [ -n "${started[$t]}" ] && continue
    out=$(python 裁判/开局.py "$t" 自动开赛 --round "$rnd" 2>/dev/null)
    if [[ "$out" == START* ]]; then
      hms=${out#START }
      started[$t]=$hms
      echo "$t 开赛 $hms（解压时刻），封存计时已启动"
      nohup bash 裁判/计时封存.sh "$t" "$rnd" "$hms" > "赛事/运行记录/计时_${t}_第${rnd}轮.log" 2>&1 &
    else
      left=$((left+1))
    fi
  done
  [ $left -eq 0 ] && break
  sleep 3
done
echo "全部开赛（$(date +%T)）：$(for t in "${teams[@]}"; do printf '%s %s  ' "$t" "${started[$t]}"; done)"
