// 沙箱自检工具：用正式赛同一套限制把程序跑起来。
//
// 版本：v1.4
// 功能：两个用法——
//
//	go run ./cmd/sandbox
//	    验证本机沙箱是否真的生效：沙箱内必须无法派生子进程。
//
//	go run ./cmd/sandbox -exe bots/bin/submission.exe -turns 60
//	    用正式赛同一套限制把你的 Bot 跑起来：真实 hello、逐拍 tick、
//	    结束后退出。用来确认你的程序在被裁判掐住之后仍能正常工作。
//
// 选手包里的等价写法是 `cd arena && go run ./cmd/sandbox`：arena/ 是引擎源码
// 副本，这个工具必须跑在 desertarena 模块内，才能拿到 internal/sandbox。
//
// 限制来自 internal/sandbox，和裁判（internal/judge）共用同一份实现，
// 所以这里测到的行为就是赛场上的行为。
package main

import (
	"bufio"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"desertarena/internal/sandbox"
	"desertarena/internal/sim"
)

func main() {
	exe := flag.String("exe", "", "要跑的 Bot 可执行文件；留空则只做沙箱自检")
	turns := flag.Int("turns", 60, "跑多少拍（-exe 模式）")
	seed := flag.Int("seed", 20261109, "地图 seed（-exe 模式）")
	secs := flag.Int("timeout", 5, "每拍等待上限（秒）")
	flag.Parse()

	lim := sandbox.DefaultLimits()
	fmt.Printf("沙箱：%s\n", sandbox.Describe(lim))
	if !sandbox.Available() {
		fmt.Println("✗ 当前平台没有内置沙箱（需要容器或独立低权账户）")
		os.Exit(1)
	}

	if *exe == "" {
		os.Exit(selftest())
	}
	os.Exit(runBot(*exe, *turns, *seed, time.Duration(*secs)*time.Second))
}

// selftest 用内置探针验证隔离真的生效。
func selftest() int {
	dir, err := os.MkdirTemp("", "wod-sbx-")
	if err != nil {
		fmt.Println("✗ 建临时目录失败:", err)
		return 1
	}
	defer os.RemoveAll(dir)

	probe := filepath.Join(dir, "probe"+exeSuffix())
	build := exec.Command("go", "build", "-o", probe, "./internal/sandbox/testdata/childspawn")
	build.Stderr = os.Stderr
	if err := build.Run(); err != nil {
		fmt.Println("✗ 编译探针失败:", err)
		return 1
	}

	spawn := func(withSandbox bool) string {
		cmd := exec.Command(probe)
		out, _ := cmd.StdoutPipe()
		if err := cmd.Start(); err != nil {
			return "START_FAILED"
		}
		var sb *sandbox.Sandbox
		if withSandbox {
			sb = sandbox.New(sandbox.DefaultLimits())
			if err := sb.Apply(cmd.Process); err != nil {
				cmd.Process.Kill()
				return "APPLY_FAILED:" + err.Error()
			}
		}
		buf := make([]byte, 64)
		n, _ := out.Read(buf)
		_ = cmd.Wait()
		if sb != nil {
			sb.Close()
		}
		return strings.TrimSpace(string(buf[:n]))
	}

	ok := true
	if got := spawn(false); got != "SPAWN_OK" {
		fmt.Printf("✗ 对照组异常：期望 SPAWN_OK，实得 %q\n", got)
		ok = false
	} else {
		fmt.Println("✓ 对照组：不加沙箱时能派生子进程")
	}
	if got := spawn(true); got != "SPAWN_BLOCKED" {
		fmt.Printf("✗ 沙箱内仍派生了子进程（%q）——限制没有生效\n", got)
		ok = false
	} else {
		fmt.Println("✓ 沙箱内：派生子进程被拒绝")
	}
	if ok {
		fmt.Println("SANDBOX_OK：本机沙箱可用，限制与正式赛一致")
		return 0
	}
	return 1
}

// runBot 用真实协议把选手程序跑起来，全程在沙箱内。
func runBot(path string, turns, seed int, perTick time.Duration) int {
	abs, err := filepath.Abs(path)
	if err != nil {
		fmt.Println("✗ 路径无效:", err)
		return 1
	}
	if _, err := os.Stat(abs); err != nil {
		fmt.Println("✗ 找不到可执行文件:", abs)
		return 1
	}

	g, err := sim.NewGame(seed, sim.GameCfg{TickLimit: 1200, Generator: "minecraft"})
	if err != nil {
		fmt.Println("✗ 开局失败:", err)
		return 1
	}

	workDir, err := os.MkdirTemp("", "wod-bot-")
	if err != nil {
		fmt.Println("✗ 建工作目录失败:", err)
		return 1
	}
	defer os.RemoveAll(workDir)

	cmd := exec.Command(abs)
	cmd.Dir = workDir
	cmd.Env = minimalEnv()
	stdin, err := cmd.StdinPipe()
	if err != nil {
		fmt.Println("✗ 管道失败:", err)
		return 1
	}
	var errBuf strings.Builder
	cmd.Stderr = &errBuf
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		fmt.Println("✗ 管道失败:", err)
		return 1
	}
	if err := cmd.Start(); err != nil {
		fmt.Println("✗ 启动失败:", err)
		return 1
	}
	sb := sandbox.New(sandbox.DefaultLimits())
	if err := sb.Apply(cmd.Process); err != nil {
		cmd.Process.Kill()
		fmt.Println("✗ 沙箱施加失败:", err)
		return 1
	}
	defer func() { sb.Close() }()

	lines := make(chan string, 4)
	go func() {
		defer close(lines)
		br := bufio.NewReaderSize(stdout, 1<<20)
		for {
			s, err := br.ReadString('\n')
			if len(s) > 0 {
				lines <- strings.TrimRight(s, "\r\n")
			}
			if err != nil {
				return
			}
		}
	}()

	// EncodeHello/EncodeTick 自带行尾换行；再拼一个就会发出空行，
	// 而选手包读到空行会直接报 "unexpected end of JSON input"。
	send := func(s string) bool {
		_, err := fmt.Fprintln(stdin, strings.TrimRight(s, "\r\n"))
		return err == nil
	}
	recv := func() (string, bool) {
		select {
		case l, ok := <-lines:
			return l, ok
		case <-time.After(perTick * 4):
			return "", false
		}
	}

	if !send(g.EncodeHello()) {
		fmt.Println("✗ 写 hello 失败")
		return 1
	}
	line, ok := recv()
	if !ok || !strings.Contains(line, "ready") {
		fmt.Printf("✗ 没收到 ready（%q）\n", line)
		if e := strings.TrimSpace(errBuf.String()); e != "" {
			fmt.Println("  stderr:", truncate(e, 400))
		}
		return 1
	}
	fmt.Println("✓ hello → ready")

	start := time.Now()
	slowest := time.Duration(0)
	for i := 0; i < turns && !g.Done(); i++ {
		t0 := time.Now()
		if !send(g.EncodeTick(0)) {
			fmt.Println("✗ 写 tick 失败")
			return 1
		}
		out, ok := recv()
		dt := time.Since(t0)
		if dt > slowest {
			slowest = dt
		}
		if !ok {
			fmt.Printf("✗ 第 %d 拍超时（>%v）\n", i+1, perTick*4)
			if e := strings.TrimSpace(errBuf.String()); e != "" {
				fmt.Println("  stderr:", truncate(e, 400))
			}
			return 1
		}
		if !strings.Contains(out, "actions") {
			fmt.Printf("✗ 第 %d 拍回包不是动作（%q）\n", i+1, truncate(out, 80))
			return 1
		}
		g.Step([2][]sim.Action{nil, nil})
	}
	send(`{"type":"end","result":{}}`)
	_ = stdin.Close()
	_ = cmd.Wait()
	sb.Close()

	fmt.Printf("BOT_OK：%d 拍全部应答，最慢 %v，总耗时 %v\n", turns, slowest.Round(time.Microsecond), time.Since(start).Round(time.Millisecond))
	return 0
}

func minimalEnv() []string {
	keep := map[string]bool{
		"SystemRoot": true, "windir": true, "TEMP": true, "TMP": true, "PATH": true,
	}
	var out []string
	for _, kv := range os.Environ() {
		if i := strings.IndexByte(kv, '='); i > 0 && keep[kv[:i]] {
			out = append(out, kv)
		}
	}
	return out
}

func truncate(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n] + "…"
}

func exeSuffix() string {
	if runtime.GOOS == "windows" {
		return ".exe"
	}
	return ""
}
