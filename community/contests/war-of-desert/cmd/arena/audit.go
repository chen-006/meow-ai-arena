package main

import (
	"fmt"
	"path/filepath"
	"runtime"
	"sort"
	"strings"

	"desertarena/internal/contest"
	"desertarena/internal/engine"
	"desertarena/internal/schedule"
	"desertarena/internal/sim"
)

func entryBinRel(name, bin string) string {
	if bin != "" {
		return bin
	}
	return filepath.ToSlash(filepath.Join("bots", "bin", name+exeSuffix()))
}

func sameBinPath(a, b string) bool {
	ca, cb := filepath.Clean(a), filepath.Clean(b)
	if runtime.GOOS == "windows" {
		return strings.EqualFold(ca, cb)
	}
	return ca == cb
}

func normRuleset(r string) string {
	s := strings.ToLower(strings.TrimSpace(r))
	if s == "" {
		return sim.RulesetV14
	}
	return s
}

// resolveRunBins 把开赛实际执行的选手程序路径与封存记录强绑定。
// 正式赛（seal != nil）必须保证配置、赛程与封存记录里的名单和可执行文件路径完全一致，
// 且最终执行路径直接取自已通过哈希校验的封存记录。
func resolveRunBins(root string, cfg *contest.Config, plan *schedule.Plan, seal *contest.Seal) (map[string]string, error) {
	planBins := make(map[string]string, len(plan.Entries))
	for _, e := range plan.Entries {
		if _, dup := planBins[e.Name]; dup {
			return nil, fmt.Errorf("赛程 entries 中选手 %s 重复", e.Name)
		}
		planBins[e.Name] = binPath(root, entryBinRel(e.Name, e.Bin))
	}
	if seal == nil {
		return planBins, nil
	}

	sealBins := make(map[string]string, len(seal.Entries))
	for _, e := range seal.Entries {
		if _, dup := sealBins[e.Name]; dup {
			return nil, fmt.Errorf("封存记录中选手 %s 重复", e.Name)
		}
		sealBins[e.Name] = binPath(root, entryBinRel(e.Name, e.Bin))
	}

	var bad []string
	for name, pBin := range planBins {
		sBin, ok := sealBins[name]
		if !ok {
			bad = append(bad, name+"：在赛程中但不在封存记录里")
			continue
		}
		if !sameBinPath(pBin, sBin) {
			bad = append(bad, fmt.Sprintf("%s：赛程 bin=%q 与封存 bin=%q 不一致", name, pBin, sBin))
		}
	}
	for name := range sealBins {
		if _, ok := planBins[name]; !ok {
			bad = append(bad, name+"：在封存记录中但不在赛程里")
		}
	}
	for _, e := range cfg.Entries {
		cBin := binPath(root, entryBinRel(e.Name, e.Bin))
		if sBin, ok := sealBins[e.Name]; ok && !sameBinPath(cBin, sBin) {
			bad = append(bad, fmt.Sprintf("%s：配置 bin=%q 与封存 bin=%q 不一致", e.Name, cBin, sBin))
		}
	}
	if len(bad) > 0 {
		sort.Strings(bad)
		return nil, fmt.Errorf("赛程/配置与封存记录不一致（先重新 arena build / arena plan）:\n  %s",
			strings.Join(bad, "\n  "))
	}
	return sealBins, nil
}

// resolveRunRules 确保开赛使用的规则与时限参数来自冻结赛程，且与当前配置一致。
func resolveRunRules(plan *schedule.Plan, cfg *contest.Config) (engine.HostConfig, error) {
	sc := cfg.ScheduleConfig()
	if plan.Config.TickLimit > 0 {
		pc := plan.Config
		if pc.TickLimit != sc.TickLimit ||
			pc.TickMs != sc.TickMs ||
			pc.DecideMs != sc.DecideMs ||
			pc.InitMs != sc.InitMs ||
			pc.MaxStrikes != sc.MaxStrikes ||
			pc.StrikeMode != sc.StrikeMode ||
			pc.Generator != sc.Generator ||
			pc.Vision != sc.Vision ||
			normRuleset(pc.Ruleset) != normRuleset(sc.Ruleset) {
			return engine.HostConfig{}, fmt.Errorf("冻结赛程中的规则或时限与当前配置不一致（先重新 arena plan）")
		}
		return engine.HostConfig{
			Generator: pc.Generator,
			TickLimit: pc.TickLimit,
			TickMs:    pc.TickMs,
			DecideMs:  pc.DecideMs,
			InitMs:    pc.InitMs,
			Vision:    pc.Vision,
			Ruleset:   pc.Ruleset,
		}, nil
	}
	return engine.HostConfig{
		Generator: cfg.Rules.Generator,
		TickLimit: cfg.Rules.TickLimit,
		TickMs:    cfg.Rules.TickMs,
		DecideMs:  cfg.Rules.DecideMs,
		InitMs:    cfg.Rules.InitMs,
		Vision:    cfg.Rules.Vision,
		Ruleset:   cfg.Rules.Ruleset,
	}, nil
}

type gameKey struct {
	match int
	game  int
}

type expectedGame struct {
	key   gameKey
	swap  bool
	seats [2]string
}

// validateResultsAgainstPlan 逐局核对 结果.jsonl 与 赛程.json。
// 无论是否允许赛中临时榜，都拒绝赛程外对局、重复局及座位/换边错位；
// 正式评分（allowPartial == false）额外要求每一局恰好出现一次。
func validateResultsAgainstPlan(plan *schedule.Plan, lines []gameLine, allowPartial bool) (int, error) {
	perMatch := gamesPerMatch(plan.Mirror)
	ordered := make([]expectedGame, 0, len(plan.Matches)*perMatch)
	expMap := make(map[gameKey]expectedGame, len(plan.Matches)*perMatch)
	seenMatch := make(map[int]bool, len(plan.Matches))

	for _, m := range plan.Matches {
		if seenMatch[m.ID] {
			return 0, fmt.Errorf("赛程.json 存在重复场次 match=%d", m.ID)
		}
		seenMatch[m.ID] = true
		for gi := 0; gi < perMatch; gi++ {
			swap := gi == 1
			seats := [2]string{m.A, m.B}
			if swap {
				seats = [2]string{m.B, m.A}
			}
			eg := expectedGame{key: gameKey{match: m.ID, game: gi}, swap: swap, seats: seats}
			ordered = append(ordered, eg)
			expMap[eg.key] = eg
		}
	}

	seen := make(map[gameKey]bool, len(lines))
	for _, l := range lines {
		k := gameKey{match: l.Match, game: l.Game}
		exp, ok := expMap[k]
		if !ok {
			return 0, fmt.Errorf("结果.jsonl 包含赛程外对局 match=%d game=%d，拒绝评分", l.Match, l.Game)
		}
		if seen[k] {
			return 0, fmt.Errorf("结果.jsonl 中 match=%d game=%d 重复出现，拒绝评分", l.Match, l.Game)
		}
		seen[k] = true
		if len(l.Seats) != 2 || l.Seats[0] != exp.seats[0] || l.Seats[1] != exp.seats[1] || l.Swap != exp.swap {
			return 0, fmt.Errorf("结果.jsonl 中 match=%d game=%d 的座位或换边标记与赛程不符（期望 %v swap=%v，实际 %v swap=%v），拒绝评分",
				l.Match, l.Game, exp.seats, exp.swap, l.Seats, l.Swap)
		}
	}

	if !allowPartial {
		var missing []string
		for _, eg := range ordered {
			if !seen[eg.key] {
				missing = append(missing, fmt.Sprintf("match=%d/game=%d", eg.key.match, eg.key.game))
			}
		}
		if len(missing) > 0 {
			sample := strings.Join(missing, ", ")
			if len(missing) > 5 {
				sample = strings.Join(missing[:5], ", ") + " 等"
			}
			return 0, fmt.Errorf("结果.jsonl 不完整：赛程应有 %d 局，实际 %d 局，缺少 %d 局（%s）；如需查看赛中临时榜请加 -partial",
				len(ordered), len(lines), len(missing), sample)
		}
	}
	return len(ordered), nil
}
