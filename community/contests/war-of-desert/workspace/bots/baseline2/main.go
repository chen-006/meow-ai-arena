// baseline@2 · 极强基线：acebot 增强策略（集火/微操/护旗/劫旗/遗产竞速）。
//
// 定位：精心调试的上限对手——选手必须超越它才能证明实力。
// 参数经多轮实验标定，见 experiment-records/bot-experiments/baseline-tuning/。
package main

import (
	"fmt"
	"os"

	"desertarena/workspace/acebot"
)

func main() {
	err := acebot.Run(acebot.Profile{
		Alloc:        map[string]int{"armour": 35, "repair": 15, "attack": 30, "range": 20},
		Build:        []string{"RESEARCH", "LAV", "LAV", "LAV", "CARRIER", "RAILGUN", "LAV"},
		Techs:        []string{"I1", "F1", "C1", "I3a", "F2", "C2", "F3", "C3"},
		Aggression:   2.6,
		Weapons:      2,
		RelicRunners: 2,
		RetreatHP:    0.25,
		VetRetreatHP: 0.4,
		Garrison:     true,
		Raid:         true,
		FaceMicro:    true,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}