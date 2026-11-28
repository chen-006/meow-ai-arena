// Warden · 攻城压制：远程兵 + 航母，靠射程与维修打消耗。
//
// 版本：v1.9（实验参考 Bot）。
// 功能：先出 RAILGUN，再补 SCOUT 视野、ARTILLERY 远程火力与 CARRIER 续航；
// Siege + obey：不理会路过敌人，直接压到母舰射程外持续输出。
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
