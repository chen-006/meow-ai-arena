// Command siegepool 跑攻守模式的循环赛，并用双参数 Bradley–Terry 给选手池评分。
//
// 版本：v1.2（Desert Arena 攻守变体评分工具）。
// 功能：所有有序对（攻方 i, 守方 j）× 每个地图 seed 各打一局，
// 然后拟合「攻方实力 A_i / 守方实力 D_j」，
// 避免把攻强守弱和攻弱守强平均成同一个数。
//
//	go run ./cmd/siegepool \
//	  -pool "v1=bots/bin/pool_v1.exe,v2=bots/bin/pool_v2.exe,..." \
//	  -seeds 20261301-20261310 -workers 4 -out results/siege/bt
package main

import (
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
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
	"desertarena/internal/rating"
)

type botSpec struct {
	Name string
	Path string
}

type oneGame struct {
	Attacker string
	Defender string
	Seed     int
	Winner   int
	Turn     int
	Faults   int
	VerifyOK bool
	Err      string
}

func fail(err error) {
	fmt.Fprintln(os.Stderr, "错误:", err)
	os.Exit(1)
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
	var out []int
	for _, p := range strings.Split(s, ",") {
		if v, err := strconv.Atoi(strings.TrimSpace(p)); err == nil {
			out = append(out, v)
		}
	}
	return out
}

func parsePool(s string) []botSpec {
	var out []botSpec
	for _, item := range strings.Split(s, ",") {
		item = strings.TrimSpace(item)
		if item == "" {
			continue
		}
		kv := strings.SplitN(item, "=", 2)
		if len(kv) != 2 {
			fail(fmt.Errorf("选手写法应为 名字=路径：%s", item))
		}
		out = append(out, botSpec{Name: strings.TrimSpace(kv[0]), Path: strings.TrimSpace(kv[1])})
	}
	return out
}

// loadGames 读回 siegepool 自己写出的逐局 CSV，用来在不重跑对局的情况下重算评分。
func loadGames(path string) ([]oneGame, error) {
	fh, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer fh.Close()
	rows, err := csv.NewReader(fh).ReadAll()
	if err != nil {
		return nil, err
	}
	out := make([]oneGame, 0, len(rows))
	for i, row := range rows {
		if i == 0 || len(row) < 8 {
			continue
		}
		seed, _ := strconv.Atoi(row[2])
		winner, _ := strconv.Atoi(row[3])
		turn, _ := strconv.Atoi(row[5])
		faults, _ := strconv.Atoi(row[6])
		errMsg := ""
		if len(row) > 8 {
			errMsg = row[8]
		}
		out = append(out, oneGame{
			Attacker: row[0], Defender: row[1], Seed: seed, Winner: winner,
			Turn: turn, Faults: faults, VerifyOK: row[7] == "true", Err: errMsg,
		})
	}
	return out, nil
}

func main() {
	var (
		pool       = flag.String("pool", "", "选手池，形如 v1=bots/bin/pool_v1.exe,v2=...")
		seeds      = flag.String("seeds", "20261301-20261310", "地图 seed 列表/范围")
		workers    = flag.Int("workers", 4, "并行局数")
		out        = flag.String("out", "results/siege/bt", "输出前缀：写 <out>.csv / .json / .md")
		bootstrap  = flag.Int("bootstrap", 1000, "bootstrap 次数（按 seed 整块重抽样）")
		hp         = flag.Int("hp", 1250, "守方与攻方母舰 HP")
		tickLim    = flag.Int("tickLimit", 1260, "tick 上限")
		fortBud    = flag.Int("fortBudget", 16, "守方工事预算")
		attCU      = flag.Int("attackerCu", 900, "攻方起始 CU")
		defCU      = flag.Int("defenderCu", 700, "守方起始 CU")
		attLav     = flag.Int("attackerLav", 4, "攻方初始 LAV")
		defLav     = flag.Int("defenderLav", 3, "守方初始 LAV")
		place      = flag.Int("placeTicks", 80, "布置阶段 tick")
		from       = flag.String("from", "", "不从零跑：直接读已有的 games.csv 重算评分")
		engineKind = flag.String("engine", "oracle", "规则引擎：oracle | native")
	)
	flag.Parse()

	bots := parsePool(*pool)
	if *from == "" && len(bots) < 2 {
		fail(fmt.Errorf("-pool 至少要有 2 个选手"))
	}
	seedList := parseSeeds(*seeds)
	if *from == "" && len(seedList) == 0 {
		fail(fmt.Errorf("seed 列表为空"))
	}

	root, err := os.Getwd()
	if err != nil {
		fail(err)
	}
	host := filepath.Join(root, "engine", "oracle_host.js")
	rules := filepath.Join(root, "engine", "oracle", "rules_siege.js")
	if *engineKind == "native" {
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
	for _, b := range bots {
		if _, err := os.Stat(b.Path); err != nil {
			fail(fmt.Errorf("找不到选手程序 %s（先编译 pool 变体）", b.Path))
		}
	}

	cfg := judge.Config{
		HostScript: host,
		RulesPath:  rules,
		Rules: engine.HostConfig{
			Generator: "minecraft",
			TickLimit: *tickLim, TickMs: 50, DecideMs: 120, InitMs: 2000, Vision: false,
			Mode: "siege", MothershipHP: *hp, AttackerMothershipHP: *hp,
			FortBudget: *fortBud, PlaceTicks: *place,
			AttackerCU: *attCU, DefenderCU: *defCU,
			AttackerStartLav: *attLav, DefenderStartLav: *defLav,
		},
		Mirror: false, MaxStrikes: 3, StrikeMode: "consecutive", Verify: true,
	}

	type job struct {
		a, d botSpec
		seed int
	}
	jobs := make(chan job)
	results := make([]oneGame, 0, len(bots)*(len(bots)-1)*len(seedList))
	var mu sync.Mutex
	var wg sync.WaitGroup

	start := time.Now()
	total := len(bots) * (len(bots) - 1) * len(seedList)
	done := 0

	if *from != "" {
		loaded, err := loadGames(*from)
		if err != nil {
			fail(err)
		}
		results = loaded
		fmt.Printf("从 %s 读入 %d 局（跳过对局）\n", *from, len(results))
	} else {
		for w := 0; w < *workers; w++ {
			wg.Add(1)
			go func() {
				defer wg.Done()
				for j := range jobs {
					rec, err := judge.RunMatch(0, j.seed,
						judge.BotSpec{Name: j.d.Name, Path: j.d.Path},
						judge.BotSpec{Name: j.a.Name, Path: j.a.Path}, cfg)
					g := oneGame{Attacker: j.a.Name, Defender: j.d.Name, Seed: j.seed}
					if err != nil {
						g.Winner = -2
						g.Err = err.Error()
					} else {
						gg := rec.Games[0]
						g.Winner = gg.Result.Winner
						g.Turn = gg.Result.Turn
						g.Faults = len(gg.Faults[0]) + len(gg.Faults[1])
						if len(gg.Verify) > 0 {
							var v struct {
								OK bool `json:"ok"`
							}
							if json.Unmarshal(gg.Verify, &v) == nil {
								g.VerifyOK = v.OK
							}
						}
					}
					mu.Lock()
					results = append(results, g)
					done++
					if done%10 == 0 || done == total {
						fmt.Printf("\r进度 %d/%d  %.0fs", done, total, time.Since(start).Seconds())
					}
					mu.Unlock()
				}
			}()
		}
		for _, a := range bots {
			for _, d := range bots {
				if a.Name == d.Name {
					continue
				}
				for _, s := range seedList {
					jobs <- job{a: a, d: d, seed: s}
				}
			}
		}
		close(jobs)
		wg.Wait()
	}
	fmt.Println()

	sort.Slice(results, func(i, j int) bool {
		if results[i].Attacker != results[j].Attacker {
			return results[i].Attacker < results[j].Attacker
		}
		if results[i].Defender != results[j].Defender {
			return results[i].Defender < results[j].Defender
		}
		return results[i].Seed < results[j].Seed
	})

	if dir := filepath.Dir(*out); dir != "" && dir != "." {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			fail(err)
		}
	}

	/* 逐局明细 */
	csvPath := *out + ".csv"
	fh, err := os.Create(csvPath)
	if err != nil {
		fail(err)
	}
	cw := csv.NewWriter(fh)
	_ = cw.Write([]string{"attacker", "defender", "seed", "winner", "attacker_win", "turn", "faults", "verify_ok", "error"})
	var games []rating.SiegeGame
	faults, verified, errs := 0, 0, 0
	for _, g := range results {
		score := 0.0
		switch g.Winner {
		case 1:
			score = 1
		case -1:
			score = 0.5
		}
		games = append(games, rating.SiegeGame{
			Attacker: g.Attacker, Defender: g.Defender, Score: score,
			Block: strconv.Itoa(g.Seed),
		})
		if g.Faults > 0 {
			faults += g.Faults
		}
		if g.VerifyOK {
			verified++
		}
		if g.Err != "" {
			errs++
		}
		_ = cw.Write([]string{
			g.Attacker, g.Defender, strconv.Itoa(g.Seed), strconv.Itoa(g.Winner),
			strconv.FormatBool(g.Winner == 1), strconv.Itoa(g.Turn),
			strconv.Itoa(g.Faults), strconv.FormatBool(g.VerifyOK), g.Err,
		})
	}
	cw.Flush()
	_ = fh.Close()

	bt := rating.FitSiege(games, *bootstrap, 20261113)
	raw, _ := json.MarshalIndent(bt, "", "  ")
	_ = os.WriteFile(*out+".json", append(raw, '\n'), 0o644)

	/* 攻方胜率（不分对手）作为对照，说明 BT 与裸胜率的差别 */
	type agg struct{ w, n int }
	att := map[string]*agg{}
	def := map[string]*agg{}
	for _, g := range games {
		if att[g.Attacker] == nil {
			att[g.Attacker] = &agg{}
		}
		if def[g.Defender] == nil {
			def[g.Defender] = &agg{}
		}
		att[g.Attacker].n++
		def[g.Defender].n++
		att[g.Attacker].w += int(g.Score * 2)
		def[g.Defender].w += int((1 - g.Score) * 2)
	}

	names := make([]string, 0, len(bt.Overall))
	for n := range bt.Overall {
		names = append(names, n)
	}
	sort.SliceStable(names, func(i, j int) bool { return bt.Overall[names[i]] > bt.Overall[names[j]] })

	rank := map[string]int{}
	idx := 1
	for _, group := range bt.Ties {
		for _, n := range group {
			rank[n] = idx
		}
		idx += len(group)
	}

	var b strings.Builder
	b.WriteString("# 攻守模式 · Bradley–Terry 评分\n\n")
	b.WriteString("> 双参数模型：攻方实力 A 与守方实力 D 分开拟合，")
	b.WriteString("`P(攻方 i 破防) = 1/(1+10^((D_j−A_i)/400))`。\n")
	b.WriteString("> 整体均值锚定 0；区间按地图 seed 整块 bootstrap ")
	fmt.Fprintf(&b, "%d 次。名次相同的选手区间互相重叠，判并列。\n\n", *bootstrap)
	fmt.Fprintf(&b, "参数：HP %d ｜ 工事预算 %d ｜ tickLimit %d ｜ 每对 (攻,守) × %d 个 seed\n\n",
		*hp, *fortBud, *tickLim, len(seedList))

	b.WriteString("| 名次 | 选手 | 综合 | 95% 区间 | 攻方实力 | 守方实力 | 攻方胜率 | 守方失守率 |\n")
	b.WriteString("|---:|---|---:|---|---:|---:|---:|---:|\n")
	for _, n := range names {
		ci := bt.OverallCI[n]
		ar, dr := 0.0, 0.0
		if a := att[n]; a != nil && a.n > 0 {
			ar = float64(a.w) / float64(2*a.n)
		}
		if d := def[n]; d != nil && d.n > 0 {
			dr = float64(d.w) / float64(2*d.n)
		}
		fmt.Fprintf(&b, "| %d | %s | %.1f | %.1f ~ %.1f | %.1f | %.1f | %.1f%% | %.1f%% |\n",
			rank[n], n, bt.Overall[n], ci[0], ci[1], bt.Attack[n], bt.Defense[n], ar*100, dr*100)
	}
	b.WriteString("\n## 并列组\n\n")
	for _, group := range bt.Ties {
		if len(group) > 1 {
			fmt.Fprintf(&b, "- %s\n", strings.Join(group, " / "))
		}
	}
	fmt.Fprintf(&b, "\n共 %d 局；故障 %d 次；`verify.ok=true` %d/%d；运行错误 %d 次；耗时 %.0f 秒。\n",
		len(results), faults, verified, len(results), errs, time.Since(start).Seconds())

	if err := os.WriteFile(*out+".md", []byte(b.String()), 0o644); err != nil {
		fail(err)
	}
	fmt.Println(b.String())
	fmt.Printf("CSV → %s\nJSON → %s\nMarkdown → %s\n", csvPath, *out+".json", *out+".md")
}
