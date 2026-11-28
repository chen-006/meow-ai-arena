// Command siege 跑攻守实验。
//
// 版本：v1.2（Desert Arena 攻守变体标定工具）。
// 功能：固定守方/攻方 Bot 与地图 seed，逐级改变守方母舰 HP，
// 统计“攻方摧毁守方母舰”的比例。
//
//	go run ./cmd/siege -hps 3000,1500,800,400 -seeds 1-8 -workers 4
//	go run ./cmd/siege -smoke -hp 1500 -seed 20261201
package main

import (
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"desertarena/internal/engine"
	"desertarena/internal/judge"
)

type gameResult struct {
	HP          int
	Seed        int
	Winner      int
	AttackerWin bool
	Destroyed   bool
	DestroyedAt int
	Turn        int
	Reason      string
	DurationMS  int64
	Faults      int
	VerifyOK    bool
	Err         string
	Walls       int
	Turrets     int
	DefUnits    int
	AttUnits    int
	DefMHP      float64
	AttMHP      float64
	DefKills    int
	AttKills    int
	DefLosses   int
	AttLosses   int
}

type summary struct {
	HP       int
	Games    int
	Wins     int
	Rate     float64
	Lo, Hi   float64
	AvgTurn  float64
	Faults   int
	Verified int
}

func main() {
	var (
		attacker   = flag.String("attacker", "bots/bin/attacker.exe", "攻方程序")
		defender   = flag.String("defender", "bots/bin/defender.exe", "守方程序")
		seeds      = flag.String("seeds", "20261201-20261208", "seed 列表/范围")
		hps        = flag.String("hps", "1250", "守方母舰 HP 梯度")
		workers    = flag.Int("workers", 4, "并行局数")
		out        = flag.String("out", "results/siege/pilot.csv", "CSV 输出")
		smoke      = flag.Bool("smoke", false, "只跑一局并打印细节")
		smokeHP    = flag.Int("hp", 1250, "smoke 用的 HP")
		smokeSeed  = flag.Int("seed", 20261201, "smoke 用的 seed")
		tickLim    = flag.Int("tickLimit", 1260, "tick 上限")
		fortBud    = flag.Int("fortBudget", 16, "守方工事预算")
		attCU      = flag.Int("attackerCu", 900, "攻方起始 CU")
		defCU      = flag.Int("defenderCu", 700, "守方起始 CU")
		attLav     = flag.Int("attackerLav", 4, "攻方初始 LAV")
		defLav     = flag.Int("defenderLav", 3, "守方初始 LAV")
		place      = flag.Int("placeTicks", 80, "布置阶段 tick")
		dump       = flag.String("dump", "", "把 smoke 的完整对局 JSON 写到该路径")
		engineKind = flag.String("engine", "oracle", "规则引擎：oracle（Node 快照）| native（Go enginehost）")
	)
	flag.Parse()

	root, err := os.Getwd()
	if err != nil {
		fail(err)
	}
	host := filepath.Join(root, "engine", "oracle_host.js")
	rules := filepath.Join(root, "engine", "oracle", "rules_siege.js")
	if *engineKind == "native" {
		// Go 原生引擎：构建 enginehost，规则语义在二进制内，不再传规则变体
		bin := filepath.Join(root, "bots", "bin", "enginehost.exe")
		build := exec.Command("go", "build", "-o", bin, "./cmd/enginehost")
		build.Dir = root
		if out, err := build.CombinedOutput(); err != nil {
			fail(fmt.Errorf("构建 enginehost 失败: %s", strings.TrimSpace(string(out))))
		}
		host = bin
		rules = ""
	} else {
		if _, err := os.Stat(host); err != nil {
			fail(fmt.Errorf("找不到宿主 %s", host))
		}
		if _, err := os.Stat(rules); err != nil {
			fail(fmt.Errorf("找不到规则变体 %s", rules))
		}
	}

	mkCfg := func(seed, hp int) judge.Config {
		return judge.Config{
			HostScript: host,
			RulesPath:  rules,
			Rules: engine.HostConfig{
				Seed: seed, Generator: "minecraft",
				TickLimit: *tickLim, TickMs: 50, DecideMs: 120, InitMs: 2000, Vision: false,
				Mode: "siege", MothershipHP: hp, AttackerMothershipHP: hp,
				FortBudget: *fortBud, PlaceTicks: *place,
				AttackerCU: *attCU, DefenderCU: *defCU,
				AttackerStartLav: *attLav, DefenderStartLav: *defLav,
			},
			Mirror: false, MaxStrikes: 3, StrikeMode: "consecutive", Verify: true,
		}
	}

	if *smoke {
		if *dump != "" {
			rec, err := judge.RunMatch(0, *smokeSeed,
				judge.BotSpec{Name: "Defender", Path: *defender},
				judge.BotSpec{Name: "Attacker", Path: *attacker}, mkCfg(*smokeSeed, *smokeHP))
			if err != nil {
				fail(err)
			}
			raw, _ := json.Marshal(rec)
			_ = os.MkdirAll(filepath.Dir(*dump), 0o755)
			if err := os.WriteFile(*dump, raw, 0o644); err != nil {
				fail(err)
			}
		}
		res := runOne(mkCfg(*smokeSeed, *smokeHP), *defender, *attacker, *smokeHP, *smokeSeed)
		pretty, _ := json.MarshalIndent(res, "", "  ")
		fmt.Println(string(pretty))
		return
	}

	seedList := parseSeeds(*seeds)
	hpList := parseInts(*hps)
	if len(seedList) == 0 || len(hpList) == 0 {
		fail(fmt.Errorf("seed 或 HP 列表为空"))
	}

	type job struct{ hp, seed int }
	jobs := make(chan job)
	results := make(chan gameResult, len(seedList)*len(hpList))
	var wg sync.WaitGroup
	for i := 0; i < *workers; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			for j := range jobs {
				results <- runOne(mkCfg(j.seed, j.hp), *defender, *attacker, j.hp, j.seed)
			}
		}()
	}
	go func() {
		for _, hp := range hpList {
			for _, sd := range seedList {
				jobs <- job{hp: hp, seed: sd}
			}
		}
		close(jobs)
		wg.Wait()
		close(results)
	}()

	all := make([]gameResult, 0, len(seedList)*len(hpList))
	for r := range results {
		all = append(all, r)
		fmt.Printf("[hp=%4d seed=%d] winner=%d attackerWin=%v turn=%d faults=%d %s\n",
			r.HP, r.Seed, r.Winner, r.AttackerWin, r.Turn, r.Faults, r.Reason)
	}
	sort.Slice(all, func(i, j int) bool {
		if all[i].HP != all[j].HP {
			return all[i].HP > all[j].HP
		}
		return all[i].Seed < all[j].Seed
	})

	if err := os.MkdirAll(filepath.Dir(*out), 0o755); err != nil {
		fail(err)
	}
	if err := writeCSV(*out, all); err != nil {
		fail(err)
	}
	sums := summarize(all, hpList)
	// 汇总跟着 CSV 命名，别固定写 sweep.md —— 否则逐 HP 逐预算的试跑会把正式汇总冲掉。
	md := strings.TrimSuffix(*out, filepath.Ext(*out)) + ".md"
	if err := writeSummary(md, sums, *tickLim, *fortBud, *attCU, *defCU); err != nil {
		fail(err)
	}
	fmt.Printf("\nCSV → %s\n汇总 → %s\n", *out, md)
	for _, s := range sums {
		fmt.Printf("HP=%4d  n=%2d  摧毁率=%.1f%%  95%%CI=[%.1f%%, %.1f%%]  平均turn=%.0f\n",
			s.HP, s.Games, s.Rate*100, s.Lo*100, s.Hi*100, s.AvgTurn)
	}
}

func runOne(cfg judge.Config, defender, attacker string, hp, seed int) gameResult {
	rec, err := judge.RunMatch(0, seed,
		judge.BotSpec{Name: "Defender", Path: defender},
		judge.BotSpec{Name: "Attacker", Path: attacker}, cfg)
	if err != nil {
		return gameResult{HP: hp, Seed: seed, Winner: -2, Err: err.Error()}
	}
	g := rec.Games[0]
	res := gameResult{
		HP: hp, Seed: seed, Winner: g.Result.Winner,
		AttackerWin: g.Result.Winner == 1,
		Destroyed:   g.Result.Destroyed,
		DestroyedAt: g.Result.DestroyedAt,
		Turn:        g.Result.Turn, Reason: g.Result.Reason, DurationMS: g.DurationMS,
		Faults: len(g.Faults[0]) + len(g.Faults[1]),
	}
	if len(g.Frames) > 0 {
		var f struct {
			Units []struct {
				Kind string  `json:"kind"`
				Side int     `json:"side"`
				HP   float64 `json:"hp"`
			} `json:"units"`
		}
		if json.Unmarshal(g.Frames[len(g.Frames)-1], &f) == nil {
			for _, u := range f.Units {
				if u.Kind == "WALL" {
					res.Walls++
				}
				if u.Kind == "TURRET" {
					res.Turrets++
				}
				if u.Kind == "MOTHERSHIP" {
					if u.Side == 0 {
						res.DefMHP = u.HP
					} else {
						res.AttMHP = u.HP
					}
				}
				if u.Side == 0 {
					res.DefUnits++
				} else {
					res.AttUnits++
				}
			}
		}
	}
	if len(g.Result.Cards) == 2 {
		res.DefKills = g.Result.Cards[0].Kills
		res.DefLosses = g.Result.Cards[0].Losses
		res.AttKills = g.Result.Cards[1].Kills
		res.AttLosses = g.Result.Cards[1].Losses
	}
	if len(g.Verify) > 0 {
		var v struct {
			OK bool `json:"ok"`
		}
		if json.Unmarshal(g.Verify, &v) == nil {
			res.VerifyOK = v.OK
		}
	}
	return res
}

func writeCSV(path string, rows []gameResult) error {
	f, err := os.Create(path)
	if err != nil {
		return err
	}
	defer f.Close()
	w := csv.NewWriter(f)
	defer w.Flush()
	_ = w.Write([]string{"hp", "seed", "winner", "attacker_win", "destroyed", "destroyed_at", "turn", "reason", "duration_ms", "faults", "verify_ok", "error"})
	for _, r := range rows {
		_ = w.Write([]string{
			strconv.Itoa(r.HP), strconv.Itoa(r.Seed), strconv.Itoa(r.Winner),
			strconv.FormatBool(r.AttackerWin), strconv.FormatBool(r.Destroyed),
			strconv.Itoa(r.DestroyedAt), strconv.Itoa(r.Turn), r.Reason,
			strconv.FormatInt(r.DurationMS, 10), strconv.Itoa(r.Faults),
			strconv.FormatBool(r.VerifyOK), r.Err,
		})
	}
	return w.Error()
}

func summarize(all []gameResult, hps []int) []summary {
	out := make([]summary, 0, len(hps))
	for _, hp := range hps {
		var games, wins, faults, verified int
		var turns float64
		for _, r := range all {
			if r.HP != hp || r.Err != "" {
				continue
			}
			games++
			if r.AttackerWin {
				wins++
			}
			faults += r.Faults
			if r.VerifyOK {
				verified++
			}
			turns += float64(r.Turn)
		}
		s := summary{HP: hp, Games: games, Wins: wins, Faults: faults, Verified: verified}
		if games > 0 {
			s.Rate = float64(wins) / float64(games)
			s.Lo, s.Hi = wilson(wins, games)
			s.AvgTurn = turns / float64(games)
		}
		out = append(out, s)
	}
	sort.Slice(out, func(i, j int) bool { return out[i].HP > out[j].HP })
	return out
}

func wilson(k, n int) (float64, float64) {
	if n == 0 {
		return 0, 0
	}
	z := 1.959963984540054
	p := float64(k) / float64(n)
	den := 1 + z*z/float64(n)
	center := (p + z*z/(2*float64(n))) / den
	half := z * math.Sqrt(p*(1-p)/float64(n)+z*z/(4*float64(n*n))) / den
	lo, hi := center-half, center+half
	if lo < 0 {
		lo = 0
	}
	if hi > 1 {
		hi = 1
	}
	return lo, hi
}

func writeSummary(path string, sums []summary, tickLimit, fortBudget, attCU, defCU int) error {
	var b strings.Builder
	fmt.Fprintf(&b, "# 攻守实验 · 母舰 HP 扫描\n\n")
	fmt.Fprintf(&b, "> tickLimit=%d ｜ 工事预算=%d ｜ 攻方 CU=%d ｜ 守方 CU=%d ｜ 摧毁率=攻方胜率\n\n", tickLimit, fortBudget, attCU, defCU)
	fmt.Fprintf(&b, "| 守方母舰 HP | 局数 | 攻方胜 | 摧毁率 | 95%% CI | 平均结束 tick | 故障 | 复核 |\n")
	fmt.Fprintf(&b, "|---:|---:|---:|---:|---|---:|---:|---:|\n")
	for _, s := range sums {
		fmt.Fprintf(&b, "| %d | %d | %d | %.1f%% | %.1f%% ~ %.1f%% | %.0f | %d | %d/%d |\n",
			s.HP, s.Games, s.Wins, s.Rate*100, s.Lo*100, s.Hi*100, s.AvgTurn, s.Faults, s.Verified, s.Games)
	}
	fmt.Fprintf(&b, "\n目标区间：20%%～45%%。\n")
	return os.WriteFile(path, []byte(b.String()), 0o644)
}

func parseSeeds(s string) []int {
	s = strings.TrimSpace(s)
	if s == "" {
		return nil
	}
	if strings.Contains(s, "-") && !strings.Contains(s, ",") {
		parts := strings.SplitN(s, "-", 2)
		a, err1 := strconv.Atoi(strings.TrimSpace(parts[0]))
		b, err2 := strconv.Atoi(strings.TrimSpace(parts[1]))
		if err1 == nil && err2 == nil && b >= a {
			out := make([]int, 0, b-a+1)
			for i := a; i <= b; i++ {
				out = append(out, i)
			}
			return out
		}
	}
	return parseInts(s)
}

func parseInts(s string) []int {
	var out []int
	for _, p := range strings.Split(s, ",") {
		p = strings.TrimSpace(p)
		if p == "" {
			continue
		}
		v, err := strconv.Atoi(p)
		if err == nil {
			out = append(out, v)
		}
	}
	return out
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "错误：", err)
	os.Exit(1)
}

var _ = time.Now
