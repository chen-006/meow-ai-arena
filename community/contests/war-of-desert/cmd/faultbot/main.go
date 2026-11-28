// Command faultbot 是正式赛自检专用的故障注入 Bot。
//
// 版本：v1.2（Desert Arena 正式赛执行链自检工具）。
// 功能：故意违反协议，验证裁判的硬保证——
// 超时 kill、崩溃重启、非法结构红牌。
// 由 arena selftest 编译并驱动，不参加正式赛。
package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"time"
)

func main() {
	mode := os.Getenv("FAULT_MODE")
	in := bufio.NewReaderSize(os.Stdin, 1<<20)
	if _, err := in.ReadString('\n'); err != nil {
		os.Exit(1)
	}
	fmt.Println(`{"ready":true}`)
	for {
		line, err := in.ReadString('\n')
		if err != nil {
			return
		}
		var probe struct {
			Type string `json:"type"`
		}
		_ = json.Unmarshal([]byte(line), &probe)
		if probe.Type == "end" {
			return
		}
		switch mode {
		case "timeout":
			// 远超 120ms 预算
			time.Sleep(600 * time.Millisecond)
		case "crash":
			// 直接消失，模拟段错误/被外部杀掉
			os.Exit(3)
		case "badjson":
			fmt.Println(`{"actions":"nope"}`)
			continue
		case "toomany":
			fmt.Println(`{"actions":[{"id":1,"type":"move","x":1,"y":1},{"id":1,"type":"move","x":2,"y":2}]}`)
			continue
		}
		fmt.Println(`{"actions":[]}`)
	}
}
