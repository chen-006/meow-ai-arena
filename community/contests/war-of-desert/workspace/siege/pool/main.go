// Pool：同一条 Bot 按引擎给的角色自动切换攻/守逻辑，用来做 BT 评测的选手池。
// 风格差异由编译期注入：go build -ldflags "-X desertarena/workspace/siege/siegebot.Variant=v2"
package main

import (
	"fmt"
	"os"

	"desertarena/workspace/siege/siegebot"
)

func main() {
	if err := siegebot.RunDual(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
