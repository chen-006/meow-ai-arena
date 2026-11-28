// Vanguard · 正面推进：重甲开路，火力优先。
//
// 版本：v1.2（官方参考 Bot）。
// 功能：交战半径 1.4×、先经济后轻甲、AAV 垫后等 RU 到账；
// 造 3 门母舰主炮。策略参数见 refbot.Strategy。
package main

import (
	"fmt"
	"os"

	"desertarena/workspace/refbot"
)

func main() {
	err := refbot.Run(refbot.Strategy{
		Alloc: map[string]int{"armour": 20, "repair": 20, "attack": 40, "range": 20},
		// v1.1 队列是死板 FIFO：AAV 需 30 RU 且 RU 只能靠 SALVAGER 采 ru 节点。
		// 先 SALVAGER（CU+RU 两个经济源），再轻单位，AAV 垫在后面等 RU 到账。
		Build:      []string{"RESEARCH", "AAV", "RAILGUN", "LAV", "LAV"},
		Techs:      []string{"I1", "A1", "F1", "C1", "I3a", "A2", "F2", "C2", "A3", "C3"},
		Aggression: 1.4,
		// v1.8 实验：默认压向敌方 VP，制造真实交战（原先只在射程内才前压）。
		Push: true,
		// 加入 RAILGUN 后开启攻坚：射程 38 格压过母舰 28 格，站桩拆舰。
		Siege:   true,
		Weapons: 3,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
