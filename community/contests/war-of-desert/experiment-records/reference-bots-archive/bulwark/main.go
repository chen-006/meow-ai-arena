// Bulwark · 经济防守：先攒工业与装甲，再靠母舰火力守点。
//
// 版本：v1.2（官方参考 Bot）。
// 功能：交战半径 0.55×、母舰前出占点、造 4 门主炮；
// 生产先 SALVAGER 再 AAV/LAV（策略见 refbot.Strategy）。
package main

import (
	"fmt"
	"os"

	"desertarena/workspace/refbot"
)

func main() {
	err := refbot.Run(refbot.Strategy{
		Alloc:      map[string]int{"armour": 30, "repair": 30, "attack": 20, "range": 20},
		Build:      []string{"RESEARCH", "AAV", "LAV", "ARTILLERY"},
		Techs:      []string{"I1", "A1", "A2", "I3a", "I4", "A3"},
		Aggression: 0.55,
		HoldVP:     true,
		// v1.8 实验：压向敌方 VP，避免双方各守半场零交战。
		Push: true,
		// 经济型阵容攒得起 ARTILLERY（14 格射程），开启攻坚拆舰。
		Siege:   true,
		Weapons: 4,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
