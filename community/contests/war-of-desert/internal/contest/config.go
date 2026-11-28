// Package contest 读取正式赛配置并解析相对路径。
//
// 版本：v1.2（Desert Arena 正式赛执行链）。
// 功能：tournament/*.json 的结构定义与校验、引擎宿主路径解析
// （kind=native 时构建 enginehost）、输出目录约束在仓库根内。
package contest

import (
	"encoding/json"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"

	"desertarena/internal/schedule"
)

// Config 是 tournament/formal.json 的结构。
type Config struct {
	Name    string `json:"name"`
	Season  string `json:"season"`
	Version string `json:"version"`
	Engine  struct {
		Kind         string `json:"kind"`
		Host         string `json:"host"`
		Snapshot     string `json:"snapshot"`
		OracleCommit string `json:"oracleCommit"`
	} `json:"engine"`
	Rules struct {
		TickLimit  int    `json:"tickLimit"`
		TickMs     int    `json:"tickMs"`
		DecideMs   int    `json:"decideMs"`
		InitMs     int    `json:"initMs"`
		MaxStrikes int    `json:"maxStrikes"`
		StrikeMode string `json:"strikeMode"`
		Generator  string `json:"generator"`
		Vision     bool   `json:"vision"`
		Ruleset    string `json:"ruleset,omitempty"`
	} `json:"rules"`
	Schedule struct {
		Seeds     []int `json:"seeds"`
		Mirror    bool  `json:"mirror"`
		Bootstrap int   `json:"bootstrap"`
	} `json:"schedule"`
	Entries []schedule.Entry `json:"entries"`
	Output  string           `json:"output"`
	Verify  bool             `json:"verify"`

	// Key 是配置文件名（去掉目录与 .json），也是这个赛季封存记录的文件名。
	// 两个赛季并存靠它区分：bots/seal-records/<Key>.json。
	Key string `json:"-"`
}

// Load 读取配置。root 是 war_of_desert 目录（配置文件的上一级）。
func Load(path string) (*Config, string, error) {
	abs, err := filepath.Abs(path)
	if err != nil {
		return nil, "", err
	}
	raw, err := os.ReadFile(abs)
	if err != nil {
		return nil, "", err
	}
	var c Config
	if err := json.Unmarshal(raw, &c); err != nil {
		return nil, "", fmt.Errorf("配置不是合法 JSON: %w", err)
	}
	if c.Rules.TickLimit <= 0 || c.Rules.DecideMs <= 0 {
		return nil, "", fmt.Errorf("配置缺 rules.tickLimit / rules.decideMs")
	}
	if len(c.Entries) < 2 {
		return nil, "", fmt.Errorf("正式赛至少需要 2 名选手，当前 %d", len(c.Entries))
	}
	c.Key = strings.TrimSuffix(filepath.Base(abs), filepath.Ext(abs))
	root := filepath.Dir(filepath.Dir(abs))
	return &c, root, nil
}

// Default 返回正式赛的默认规则。
//
// 选手包里的 arena/ 不带 tournament/ 目录，check/local 需要一个不需要配置文件的
// 兜底：数值必须与正式赛配置一致，否则本地跑出来的判定和赛场不是一回事。
func Default() *Config {
	c := &Config{Name: "本地默认", Season: "local", Version: "1.9.2"}
	c.Engine.Kind = "native"
	c.Rules.TickLimit = 1500
	c.Rules.TickMs = 50
	c.Rules.DecideMs = 120
	c.Rules.InitMs = 2000
	c.Rules.MaxStrikes = 3
	c.Rules.StrikeMode = "consecutive"
	c.Rules.Generator = "minecraft"
	c.Rules.Vision = false
	c.Rules.Ruleset = "v15"
	c.Schedule.Seeds = []int{20261109}
	c.Schedule.Mirror = true
	c.Schedule.Bootstrap = 200
	c.Verify = true
	return c
}

// ScheduleConfig 转成赛程包的口径。
func (c *Config) ScheduleConfig() schedule.Config {
	return schedule.Config{
		TickLimit:  c.Rules.TickLimit,
		TickMs:     c.Rules.TickMs,
		DecideMs:   c.Rules.DecideMs,
		InitMs:     c.Rules.InitMs,
		MaxStrikes: c.Rules.MaxStrikes,
		StrikeMode: c.Rules.StrikeMode,
		Generator:  c.Rules.Generator,
		Vision:     c.Rules.Vision,
		Ruleset:    c.Rules.Ruleset,
	}
}

// HostPath 返回引擎宿主脚本的绝对路径。
func (c *Config) HostPath(root string) string {
	return filepath.Join(root, filepath.FromSlash(c.Engine.Host))
}

// EngineHostPath 返回引擎宿主：kind=native 时优先从源码构建 Go 原生
// enginehost（go build 有缓存，重复调用开销小）；没有源码时退回预编译宿主。
//
// 两种源码布局都要认：赛事仓库是 <root>/cmd/enginehost；发给选手的包里
// 引擎整体放在 <root>/arena/，配置在 <root>/tournament/ 下。
func (c *Config) EngineHostPath(root string) (string, error) {
	if c.Engine.Kind != "native" {
		return c.HostPath(root), nil
	}
	for _, base := range []string{root, filepath.Join(root, "arena")} {
		if st, err := os.Stat(filepath.Join(base, "cmd", "enginehost")); err != nil || !st.IsDir() {
			continue
		}
		bin := filepath.Join(base, ".bin", "enginehost"+exeSuffix())
		build := exec.Command("go", "build", "-o", bin, "./cmd/enginehost")
		build.Dir = base
		if out, err := build.CombinedOutput(); err != nil {
			return "", fmt.Errorf("构建 enginehost 失败: %s", strings.TrimSpace(string(out)))
		}
		return bin, nil
	}
	if p := PackagedEngineHost(); p != "" {
		return p, nil
	}
	bin := filepath.Join(root, "bots", "bin", "enginehost"+exeSuffix())
	if st, err := os.Stat(bin); err == nil && !st.IsDir() {
		return bin, nil
	}
	return "", fmt.Errorf("找不到引擎宿主 enginehost%s：要么在比赛目录下运行，"+
		"要么把 arena/（引擎源码）放在配置目录旁边", exeSuffix())
}

// PackagedEngineHost 找与当前可执行文件同目录的引擎宿主。
//
// 发给选手的裁判程序是「arena.exe + enginehost.exe」一对：选手机器上没有
// 引擎源码，只能靠这份预编译宿主。找不到返回空串。
func PackagedEngineHost() string {
	exe, err := os.Executable()
	if err != nil {
		return ""
	}
	dir := filepath.Dir(exe)
	for _, cand := range []string{
		filepath.Join(dir, "enginehost"+exeSuffix()),
		filepath.Join(dir, "tools", "enginehost"+exeSuffix()),
	} {
		if st, err := os.Stat(cand); err == nil && !st.IsDir() {
			abs, err := filepath.Abs(cand)
			if err != nil {
				return cand
			}
			return abs
		}
	}
	return ""
}

func exeSuffix() string {
	if runtime.GOOS == "windows" {
		return ".exe"
	}
	return ""
}

// OutDir 返回赛事目录的绝对路径。
func (c *Config) OutDir(root string) string {
	if c.Output == "" {
		return filepath.Join(root, "results", "formal")
	}
	return filepath.Join(root, filepath.FromSlash(c.Output))
}

// Bootstrap 缺省 200 次，与 UP 主原版一致。
func (c *Config) Bootstrap() int {
	if c.Schedule.Bootstrap <= 0 {
		return 200
	}
	return c.Schedule.Bootstrap
}
