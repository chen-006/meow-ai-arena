// Package schedule 冻结赛程：每对选手在每张地图上换边各打一局。
//
// 版本：v1.2（Desert Arena 正式赛执行链）。
// 功能：赛程的生成/保存/读取；冻结后再执行是正式赛纪律——
// run 只认 赛程.json，不现场决定对阵。
package schedule

import (
	"encoding/json"
	"fmt"
	"os"
)

// Entry 是一名参赛选手。Bin 是已编译程序的路径（相对 war_of_desert 目录）。
type Entry struct {
	Name   string `json:"name"`
	Author string `json:"author,omitempty"`
	Source string `json:"source"`
	Bin    string `json:"bin"`
	// Prebuilt 表示这名选手提交的是预编译好的可执行文件，构建时跳过编译，
	// 只校验文件存在并记录它的 SHA-256。基线 Bot 与「已封存不想再重编」的
	// 选手都走这条路。
	Prebuilt bool `json:"prebuilt,omitempty"`
}

// Match 是一场对阵；Mirror=true 表示同一 seed 换边两局。
type Match struct {
	ID     int    `json:"id"`
	Seed   int    `json:"seed"`
	A      string `json:"a"`
	B      string `json:"b"`
	Mirror bool   `json:"mirror"`
}

// Config 是冻结进赛程的规则与判罚口径。run 只认这份，不再读外部配置。
type Config struct {
	TickLimit  int    `json:"tickLimit"`
	TickMs     int    `json:"tickMs"`
	DecideMs   int    `json:"decideMs"`
	InitMs     int    `json:"initMs"`
	MaxStrikes int    `json:"maxStrikes"`
	StrikeMode string `json:"strikeMode"`
	Generator  string `json:"generator"`
	Vision     bool   `json:"vision"`
	Ruleset    string `json:"ruleset,omitempty"`
}

// Plan 是 赛程.json 的完整内容。
type Plan struct {
	Version   int    `json:"version"`
	CreatedBy string `json:"createdBy"`
	// 赛程必须自带规则身份：v1.1 与 v1.2 的赛程文件曾逐字节相同，
	// 光看赛程分不出这批对局是哪套规则跑的。
	RulesVersion string  `json:"rulesVersion,omitempty"`
	RulesHash    string  `json:"rulesHash,omitempty"`
	Engine       string  `json:"engine,omitempty"`
	Mirror       bool    `json:"mirror"`
	Entries      []Entry `json:"entries"`
	Matches      []Match `json:"matches"`
	Config       Config  `json:"config"`
}

// Build 生成赛程。seed 递增规则与 UP 主原版一致：baseSeed + 已排场次数，
// 保证同一配置重复 plan 得到逐字节相同的赛程。
func Build(entries []Entry, seeds []int, mirror bool, cfg Config) Plan {
	if len(seeds) == 0 {
		seeds = []int{20261109}
	}
	matches := make([]Match, 0, len(entries)*(len(entries)-1)/2*len(seeds))
	for _, base := range seeds {
		for i := 0; i < len(entries); i++ {
			for j := i + 1; j < len(entries); j++ {
				matches = append(matches, Match{
					ID:     len(matches),
					Seed:   base + len(matches),
					A:      entries[i].Name,
					B:      entries[j].Name,
					Mirror: mirror,
				})
			}
		}
	}
	return Plan{
		Version:   1,
		CreatedBy: "cmd/arena plan",
		Mirror:    mirror,
		Entries:   entries,
		Matches:   matches,
		Config:    cfg,
	}
}

// Load 读取已冻结的赛程。
func Load(path string) (*Plan, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	var p Plan
	if err := json.Unmarshal(raw, &p); err != nil {
		return nil, fmt.Errorf("赛程不是合法 JSON: %w", err)
	}
	if len(p.Matches) == 0 {
		return nil, fmt.Errorf("赛程没有对阵")
	}
	return &p, nil
}

// Save 写出赛程（UTF-8，带缩进）。
func Save(path string, p Plan) error {
	raw, err := json.MarshalIndent(p, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(raw, '\n'), 0o644)
}
