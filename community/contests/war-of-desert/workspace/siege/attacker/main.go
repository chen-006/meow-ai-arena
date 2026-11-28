// Attacker：攻方。生产 LAV/AAV，沿 BFS 场推进到守方母舰，先破墙再集火。
package main

import (
	"fmt"
	"os"

	"desertarena/workspace/siege/siegebot"
)

func main() {
	if err := siegebot.Run("attacker", siegebot.Attack); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
