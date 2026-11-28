// Hunter · 猎杀：轻甲集群直插敌方母舰，强制制造交战与等级。
//
// v1.5 实验专用：验证「击杀胜利分 / 单位等级 / 斩舰」这条路径，
// 不追求经济最优。
package main

import (
	"fmt"
	"os"

	"desertarena/workspace/refbot"
)

func main() {
	err := refbot.Run(refbot.Strategy{
		Alloc:      map[string]int{"armour": 20, "repair": 15, "attack": 40, "range": 25},
		Build:      []string{"RESEARCH", "LAV", "LAV", "AAV", "LAV", "LAV"},
		Techs:      []string{"I1", "F1", "C1", "I3a", "F2", "C2", "F3", "C3"},
		Aggression: 2.2,
		Hunt:       true,
		Weapons:    2,
	})
	if err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
