// baseline@1 · 中等战术基线：攻城压制剖面（原 Warden 口径）。
//
// 定位：能打但不神——有经济、有科技、有攻坚，但无集火/微操/护旗，
// 给选手一个「认真写就能赢」的中等对手。强度锚定旧 7 Bot 场中游。
package main

import (
	"fmt"
	"os"

	"desertarena/workspace/refbot"
)

func main() {
	err := refbot.Run(refbot.Strategy{
		Alloc:      map[string]int{"armour": 25, "repair": 25, "attack": 30, "range": 20},
		Build:      []string{"RESEARCH", "RAILGUN", "SCOUT", "ARTILLERY", "LAV"},
		Techs:      []string{"I1", "F1", "C1", "I3a", "F2", "C2", "I4", "F3", "C3"},
		Aggression: 1.0,
		HoldVP:     true,
		Push:       true,
		Siege:      true,
		Mode:       "obey",
		Weapons:    2,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}