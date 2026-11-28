// 沙箱自检用小程序：尝试拉起一个子进程，把结果打到 stdout。
//
// 被 Job Object（ActiveProcessLimit=1）管住时，exec 会失败并打印 SPAWN_BLOCKED。
package main

import (
	"fmt"
	"os"
	"os/exec"
)

func main() {
	exe := "cmd"
	if len(os.Args) > 1 {
		exe = os.Args[1]
	}
	if err := exec.Command(exe, "/c", "exit 0").Run(); err != nil {
		fmt.Println("SPAWN_BLOCKED")
		return
	}
	fmt.Println("SPAWN_OK")
}
