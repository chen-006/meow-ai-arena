// Sapper · 攻坚拆舰：用射程压过母舰的远程兵（RAILGUN）直取敌舰。
//
// 版本：v1.3（官方参考 Bot，新增）。
// 功能：先出 2 台采集车（第二台采 RU），攒够 320 CU + 60 RU 后出 RAILGUN，
// 之后由 Siege 策略把 railgun 压到母舰射程外持续输出。
//
// 存在意义：v1.2 一整季 40 局全部走满 1200 拍、靠 VP 判胜，
// 「母舰被毁」这条最高优先级胜负条件从未触发。缺少攻坚选项时，
// 胜负链退化成单维 VP 赛跑，BT 也只能测到一种能力的强弱。
//
// 注意人口：LAV 只占 2 人口但啃不动母舰，铺满 24 人口就再也塞不进 railgun
// （v1.2 已知缺陷 8）。本阵容刻意不铺轻甲，把人口留给攻坚兵。
package main

import (
	"fmt"
	"os"

	"desertarena/workspace/refbot"
)

func main() {
	err := refbot.Run(refbot.Strategy{
		Alloc:      map[string]int{"armour": 30, "repair": 15, "attack": 25, "range": 30},
		Build:      []string{"RESEARCH", "RAILGUN", "RAILGUN", "SCOUT"},
		Techs:      []string{"I1", "F1", "C1", "I3a", "F2", "C2", "F3", "C3", "I4"},
		Aggression: 1.2,
		HoldVP:     true,
		Siege:      true,
		Mode:       "obey",
		Weapons:    2,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
