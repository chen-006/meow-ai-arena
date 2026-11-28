// Defender：守方。布置阶段放两圈墙 + 炮塔，之后守在本方母舰周围。
package main

import (
	"fmt"
	"os"

	"desertarena/workspace/siege/siegebot"
)

func main() {
	if err := siegebot.Run("defender", siegebot.Defend); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
