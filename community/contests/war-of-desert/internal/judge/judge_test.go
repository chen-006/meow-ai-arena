package judge

import (
	"bufio"
	"errors"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"testing"

	"desertarena/internal/engine"
	"desertarena/internal/protocol"
	"desertarena/internal/sandbox"
)

type countingReader struct {
	r io.Reader
	n int
}

func (c *countingReader) Read(p []byte) (int, error) {
	n, err := c.r.Read(p)
	c.n += n
	return n, err
}

func TestReadBoundedLineBoundaries(t *testing.T) {
	max := protocol.MaxLine

	// 恰好等于 MaxLine（分别测试 LF 与 CRLF）必须通过
	exact := strings.Repeat("a", max)
	for _, tc := range []struct {
		name string
		raw  string
	}{
		{"lf", exact + "\n"},
		{"crlf", exact + "\r\n"},
		{"eof", exact},
	} {
		t.Run(tc.name, func(t *testing.T) {
			br := bufio.NewReaderSize(strings.NewReader(tc.raw), max+2)
			got, err := readBoundedLine(br, max)
			if err != nil {
				t.Fatalf("合法边界行被误拒: %v", err)
			}
			if got != exact {
				t.Fatalf("读回长度=%d，期望=%d", len(got), len(exact))
			}
		})
	}

	// 超过 1 字节必须返回 ErrLineTooLong
	for _, tc := range []struct {
		name string
		raw  string
	}{
		{"over1_lf", strings.Repeat("b", max+1) + "\n"},
		{"over1_crlf", strings.Repeat("b", max+1) + "\r\n"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			br := bufio.NewReaderSize(strings.NewReader(tc.raw), max+2)
			if _, err := readBoundedLine(br, max); !errors.Is(err, ErrLineTooLong) {
				t.Fatalf("超限行期望 ErrLineTooLong，实得 %v", err)
			}
		})
	}

	// 5 MiB 无换行流必须在上限附近立即停读，不得吃光内存
	huge := &countingReader{r: strings.NewReader(strings.Repeat("x", 5<<20))}
	br := bufio.NewReaderSize(huge, max+2)
	if _, err := readBoundedLine(br, max); !errors.Is(err, ErrLineTooLong) {
		t.Fatalf("超长无换行流期望 ErrLineTooLong，实得 %v", err)
	}
	if huge.n > max+2 {
		t.Fatalf("读取字节数=%d 超过缓冲区上限 %d，存在无限缓存风险", huge.n, max+2)
	}
}

const testBotSrc = `package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"strings"
)

func main() {
	mode := os.Getenv("TEST_BOT_MODE")
	in := bufio.NewReaderSize(os.Stdin, 1<<20)
	if _, err := in.ReadString('\n'); err != nil {
		return
	}
	if mode == "init_oversize" {
		// 5 MiB 合法 JSON 握手行：旧实现会整行读进裁判内存并放行，新实现必须在读行阶段拒绝
		fmt.Println(` + "`" + `{"ready":true,"pad":"` + "`" + ` + strings.Repeat("Z", 5<<20) + ` + "`" + `"}` + "`" + `)
		return
	}
	fmt.Println(` + "`" + `{"ready":true}` + "`" + `)
	for {
		line, err := in.ReadString('\n')
		if err != nil {
			return
		}
		var probe struct {
			Type string ` + "`" + `json:"type"` + "`" + `
		}
		_ = json.Unmarshal([]byte(line), &probe)
		if probe.Type == "end" {
			return
		}
		switch mode {
		case "turn_oversize_5mb":
			// 5 MiB 单行：旧实现会撑爆 enginehost 的 4 MiB Scanner 导致整季中止
			fmt.Println(` + "`" + `{"actions":[],"pad":"` + "`" + ` + strings.Repeat("A", 5<<20) + ` + "`" + `"}` + "`" + `)
		case "exact_max":
			base := ` + "`" + `{"actions":[]}` + "`" + `
			fmt.Println(base + strings.Repeat(" ", 131072-len(base)))
		default:
			fmt.Println(` + "`" + `{"actions":[]}` + "`" + `)
		}
	}
}
`

func buildTestBinaries(t *testing.T) (hostExe, botExe string) {
	t.Helper()
	tmp := t.TempDir()
	suffix := ""
	if runtime.GOOS == "windows" {
		suffix = ".exe"
	}
	hostExe = filepath.Join(tmp, "enginehost"+suffix)
	cmd := exec.Command("go", "build", "-o", hostExe, "../../cmd/enginehost")
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("编译 enginehost 失败: %v\n%s", err, out)
	}

	botDir := filepath.Join(tmp, "testbot")
	if err := os.MkdirAll(botDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(botDir, "go.mod"), []byte("module testbot\n\ngo 1.22\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(botDir, "main.go"), []byte(testBotSrc), 0o644); err != nil {
		t.Fatal(err)
	}
	botExe = filepath.Join(tmp, "testbot"+suffix)
	cmd = exec.Command("go", "build", "-o", botExe, ".")
	cmd.Dir = botDir
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("编译测试 Bot 失败: %v\n%s", err, out)
	}
	return hostExe, botExe
}

func TestRunMatchIsolatesOversizedBotOutput(t *testing.T) {
	if !sandbox.Available() {
		t.Skip("当前平台无内置沙箱，跳过实跑裁判测试")
	}
	hostExe, botExe := buildTestBinaries(t)

	// 另编一份恒正常 Bot，避免环境变量同时影响双方
	tmp := t.TempDir()
	suffix := ""
	if runtime.GOOS == "windows" {
		suffix = ".exe"
	}
	normalDir := filepath.Join(tmp, "normalbot")
	if err := os.MkdirAll(normalDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(normalDir, "go.mod"), []byte("module normalbot\n\ngo 1.22\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	normalSrc := strings.ReplaceAll(testBotSrc, `os.Getenv("TEST_BOT_MODE")`, `""`)
	if err := os.WriteFile(filepath.Join(normalDir, "main.go"), []byte(normalSrc), 0o644); err != nil {
		t.Fatal(err)
	}
	normalExe := filepath.Join(tmp, "normalbot"+suffix)
	cmd := exec.Command("go", "build", "-o", normalExe, ".")
	cmd.Dir = normalDir
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("编译正常 Bot 失败: %v\n%s", err, out)
	}

	baseCfg := Config{
		HostScript: hostExe,
		Rules: engine.HostConfig{
			Generator: "minecraft",
			TickLimit: 20,
			TickMs:    50,
			DecideMs:  200,
			InitMs:    2000,
			Vision:    false,
		},
		Mirror:     false,
		MaxStrikes: 3,
		StrikeMode: "consecutive",
		Verify:     true,
	}

	t.Run("turn_oversize_5mb", func(t *testing.T) {
		cfg := baseCfg
		cfg.Env = []string{"TEST_BOT_MODE=turn_oversize_5mb"}
		rec, err := RunMatch(0, 20261109,
			BotSpec{Name: "Rogue", Path: botExe},
			BotSpec{Name: "Normal", Path: normalExe}, cfg)
		if err != nil {
			t.Fatalf("超长输出不应拖垮裁判或引擎宿主，实得错误: %v", err)
		}
		g := rec.Games[0]
		if g.Result.Winner != 1 || g.Result.Level != 0 {
			t.Fatalf("超长输出 Bot 应红牌判负，实得 winner=%d level=%d reason=%q",
				g.Result.Winner, g.Result.Level, g.Result.Reason)
		}
		if len(g.Faults[0]) < 3 {
			t.Fatalf("期望至少 3 次故障，实得 %d", len(g.Faults[0]))
		}
		sawLineTooLong := false
		for _, f := range g.Faults[0] {
			if f.Kind == "line_too_long" {
				sawLineTooLong = true
				break
			}
		}
		if !sawLineTooLong {
			t.Fatalf("期望记录 line_too_long 故障，实得 %+v", g.Faults[0])
		}
	})

	t.Run("handshake_oversize", func(t *testing.T) {
		cfg := baseCfg
		cfg.Env = []string{"TEST_BOT_MODE=init_oversize"}
		rec, err := RunMatch(1, 20261109,
			BotSpec{Name: "RogueInit", Path: botExe},
			BotSpec{Name: "Normal", Path: normalExe}, cfg)
		if err != nil {
			t.Fatalf("握手超长输出不应拖垮裁判，实得错误: %v", err)
		}
		g := rec.Games[0]
		if g.Result.Winner != 1 || g.Result.Level != 0 {
			t.Fatalf("握手超长输出应直接判负，实得 winner=%d level=%d", g.Result.Winner, g.Result.Level)
		}
		if len(g.Faults[0]) == 0 || g.Faults[0][0].Kind != "init" || !strings.Contains(g.Faults[0][0].Err, "line too long") {
			t.Fatalf("期望记录含 line too long 的 init 故障，实得 %+v", g.Faults[0])
		}
	})

	t.Run("exact_max_line_and_next_match_ok", func(t *testing.T) {
		cfg := baseCfg
		cfg.Env = []string{"TEST_BOT_MODE=exact_max"}
		rec, err := RunMatch(2, 20261109,
			BotSpec{Name: "Boundary", Path: botExe},
			BotSpec{Name: "Normal", Path: normalExe}, cfg)
		if err != nil {
			t.Fatalf("合法边界（恰好 MaxLine）及后续对局应正常完成，实得错误: %v", err)
		}
		g := rec.Games[0]
		if len(g.Faults[0]) != 0 || len(g.Faults[1]) != 0 {
			t.Fatalf("合法边界不应记故障，实得 faults=%+v", g.Faults)
		}
	})
}
