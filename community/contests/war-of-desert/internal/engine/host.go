// Package engine 负责与「规则引擎宿主」通信。
//
// 版本：v1.2（Desert Arena 正式赛执行链）。
// 功能：宿主进程拉起（.js 走 node、二进制直启）、行 JSON 命令流的
// 请求/应答封装；本包只认协议，不认识任何游戏规则。
//
// 正式赛的规则来自冻结 oracle 快照，由独立 Node 进程承载；
// 未来换成 Go 引擎时，替换宿主脚本即可。
package engine

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"os/exec"
	"strings"
	"sync"
	"time"
)

// HostConfig 是一局游戏的规则配置。所有字段都会进规则指纹。
type HostConfig struct {
	Seed       int    `json:"seed"`
	Generator  string `json:"generator"`
	TickLimit  int    `json:"tickLimit"`
	TickMs     int    `json:"tickMs"`
	DecideMs   int    `json:"decideMs"`
	InitMs     int    `json:"initMs"`
	Vision     bool   `json:"vision"`
	FrameEvery int    `json:"frameEvery,omitempty"`
	// Ruleset "v14" / "v15"；空值按 v14 处理。
	Ruleset string `json:"ruleset,omitempty"`

	/* 攻守实验（siege）字段；旧模式留零值即可。 */
	Mode                 string `json:"mode,omitempty"`
	MothershipHP         int    `json:"mothershipHp,omitempty"`
	AttackerMothershipHP int    `json:"attackerMothershipHp,omitempty"`
	FortBudget           int    `json:"fortBudget,omitempty"`
	PlaceTicks           int    `json:"placeTicks,omitempty"`
	AttackerCU           int    `json:"attackerCu,omitempty"`
	DefenderCU           int    `json:"defenderCu,omitempty"`
	AttackerStartLav     int    `json:"attackerStartLav,omitempty"`
	DefenderStartLav     int    `json:"defenderStartLav,omitempty"`
}

// Host 是一个引擎宿主进程。非并发安全，调用方负责串行化。
type Host struct {
	cmd     *exec.Cmd
	in      io.WriteCloser
	out     *bufio.Reader
	outFile io.ReadCloser
	timeout time.Duration
	mu      sync.Mutex

	closed bool
}

// HostCallTimeout 是单条命令的最长等待时间。没有它时，宿主卡死会让裁判永久阻塞。
const HostCallTimeout = 60 * time.Second

type envelope struct {
	OK           bool            `json:"ok"`
	Error        string          `json:"error"`
	Cmd          string          `json:"cmd"`
	Hello        string          `json:"hello"`
	Line         string          `json:"line"`
	Seed         int             `json:"seed"`
	Generator    string          `json:"generator"`
	MapAttempt   int             `json:"mapAttempt"`
	RulesHash    string          `json:"rulesHash"`
	ProtoVersion int             `json:"protoVersion"`
	TickLimit    int             `json:"tickLimit"`
	Done         bool            `json:"done"`
	Turn         int             `json:"turn"`
	Count        int             `json:"count"`
	Frame        json.RawMessage `json:"frame"`
	Result       json.RawMessage `json:"result"`
	Verify       json.RawMessage `json:"verify"`
}

// Start 启动宿主。script 以 .js 结尾时用 node 承载（oracle_host.js），
// 否则视为可执行文件直接拉起（Go 原生 enginehost）。
func Start(script string, rulesPath ...string) (*Host, error) {
	var cmd *exec.Cmd
	if strings.HasSuffix(script, ".js") {
		args := []string{script}
		if len(rulesPath) > 0 && rulesPath[0] != "" {
			args = append(args, rulesPath[0])
		}
		cmd = exec.Command("node", args...)
	} else {
		cmd = exec.Command(script)
	}
	cmd.Stderr = os.Stderr
	in, err := cmd.StdinPipe()
	if err != nil {
		return nil, err
	}
	out, err := cmd.StdoutPipe()
	if err != nil {
		return nil, err
	}
	if err := cmd.Start(); err != nil {
		return nil, fmt.Errorf("启动引擎宿主失败: %w", err)
	}
	return &Host{cmd: cmd, in: in, out: bufio.NewReaderSize(out, 1<<20), outFile: out, timeout: HostCallTimeout}, nil
}

// deadlineSetter 是 os.File 的超时接口（管道在 Windows/Linux 上都支持）。
type deadlineSetter interface {
	SetReadDeadline(time.Time) error
}

type writeDeadlineSetter interface {
	SetWriteDeadline(time.Time) error
}

func (h *Host) call(req any, resp *envelope) error {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.closed {
		return fmt.Errorf("引擎宿主已关闭")
	}
	raw, err := json.Marshal(req)
	if err != nil {
		return err
	}
	// 写也带截止时间：宿主不读 stdin 时，hello 这类大行会把裁判永久卡住。
	if h.timeout > 0 {
		if dl, ok := h.in.(writeDeadlineSetter); ok {
			_ = dl.SetWriteDeadline(time.Now().Add(h.timeout))
			defer func() { _ = dl.SetWriteDeadline(time.Time{}) }()
		}
	}
	if _, err := h.in.Write(append(raw, '\n')); err != nil {
		return fmt.Errorf("写引擎宿主失败: %w", err)
	}
	// 读带截止时间：宿主卡死时返回错误，而不是让整个裁判永久阻塞。
	if h.timeout > 0 {
		if dl, ok := h.outFile.(deadlineSetter); ok {
			_ = dl.SetReadDeadline(time.Now().Add(h.timeout))
			defer func() { _ = dl.SetReadDeadline(time.Time{}) }()
		}
	}
	line, err := h.out.ReadString('\n')
	if err != nil {
		return fmt.Errorf("读引擎宿主失败: %w", err)
	}
	if err := json.Unmarshal([]byte(strings.TrimRight(line, "\r\n")), resp); err != nil {
		return fmt.Errorf("引擎宿主返回非法 JSON: %w", err)
	}
	if !resp.OK {
		return fmt.Errorf("引擎宿主: %s", resp.Error)
	}
	return nil
}

// NewReply 是 new 命令的返回。
type NewReply struct {
	Hello        string
	Seed         int
	Generator    string
	MapAttempt   int
	RulesHash    string
	ProtoVersion int
	TickLimit    int
}

// New 开一局。调用方必须检查 protoVersion，不匹配就拒绝开赛。
func (h *Host) New(cfg HostConfig) (*NewReply, error) {
	var env envelope
	req := map[string]any{
		"cmd":        "new",
		"seed":       cfg.Seed,
		"generator":  cfg.Generator,
		"tickLimit":  cfg.TickLimit,
		"tickMs":     cfg.TickMs,
		"decideMs":   cfg.DecideMs,
		"initMs":     cfg.InitMs,
		"vision":     cfg.Vision,
		"frameEvery": cfg.FrameEvery,
	}
	if cfg.Ruleset != "" {
		req["ruleset"] = cfg.Ruleset
	}
	if cfg.Mode != "" {
		req["mode"] = cfg.Mode
		req["mothershipHp"] = cfg.MothershipHP
		req["attackerMothershipHp"] = cfg.AttackerMothershipHP
		req["fortBudget"] = cfg.FortBudget
		req["placeTicks"] = cfg.PlaceTicks
		req["attackerCu"] = cfg.AttackerCU
		req["defenderCu"] = cfg.DefenderCU
		req["attackerStartLav"] = cfg.AttackerStartLav
		req["defenderStartLav"] = cfg.DefenderStartLav
	}
	if err := h.call(req, &env); err != nil {
		return nil, err
	}
	return &NewReply{
		Hello:        env.Hello,
		Seed:         env.Seed,
		Generator:    env.Generator,
		MapAttempt:   env.MapAttempt,
		RulesHash:    env.RulesHash,
		ProtoVersion: env.ProtoVersion,
		TickLimit:    env.TickLimit,
	}, nil
}

// Encode 取某一方的 tick 行（不含换行）。
func (h *Host) Encode(seat int) (string, error) {
	var env envelope
	if err := h.call(map[string]any{"cmd": "encode", "seat": seat}, &env); err != nil {
		return "", err
	}
	return env.Line, nil
}

// Apply 交回某一方的 actions 行，宿主负责结构校验并缓存到下一拍。
func (h *Host) Apply(seat int, line string) (int, error) {
	var env envelope
	if err := h.call(map[string]any{"cmd": "apply", "seat": seat, "line": line}, &env); err != nil {
		return 0, err
	}
	return env.Count, nil
}

// Step 推进一步，返回是否终局、当前 turn，以及抽帧点的帧。
func (h *Host) Step() (done bool, turn int, frame json.RawMessage, err error) {
	var env envelope
	if err := h.call(map[string]any{"cmd": "step"}, &env); err != nil {
		return false, 0, nil, err
	}
	return env.Done, env.Turn, env.Frame, nil
}

// Note 回填这一拍的决策耗时与故障标记，算力域和故障统计都靠它。
func (h *Host) Note(seat int, ms float64, fault bool) error {
	var env envelope
	return h.call(map[string]any{"cmd": "note", "seat": seat, "ms": ms, "fault": fault}, &env)
}

// Forfeit 判负。
func (h *Host) Forfeit(seat int, reason string) error {
	var env envelope
	return h.call(map[string]any{"cmd": "forfeit", "seat": seat, "reason": reason}, &env)
}

// EndGame 由裁判直接收局（双方同时失去资格时用，forfeit 只能判一方负）。
func (h *Host) EndGame(winner, level int, reason string) error {
	var env envelope
	return h.call(map[string]any{"cmd": "end", "winner": winner, "level": level, "reason": reason}, &env)
}

// Result 取终局结果原始 JSON。
func (h *Host) Result() (json.RawMessage, error) {
	var env envelope
	if err := h.call(map[string]any{"cmd": "result"}, &env); err != nil {
		return nil, err
	}
	return env.Result, nil
}

// Verify 用 orderLog 重跑整局并比对逐帧哈希。
func (h *Host) Verify() (json.RawMessage, error) {
	var env envelope
	if err := h.call(map[string]any{"cmd": "verify"}, &env); err != nil {
		return nil, err
	}
	return env.Verify, nil
}

// Close 关闭宿主。
func (h *Host) Close() {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.closed {
		return
	}
	h.closed = true
	_, _ = h.in.Write([]byte("{\"cmd\":\"bye\"}\n"))
	_ = h.in.Close()
	_ = h.cmd.Process.Kill()
	_ = h.cmd.Wait()
}
