// Command arena 是 Desert Arena 正式赛的唯一入口。
//
// 版本：v1.2（Desert Arena 正式赛执行链）。
// 功能：build / plan / run / rate / check / selftest / season 全流程；
// 引擎双轨（oracle 快照 | Go 原生 enginehost）。
//
//	arena build  编译选手包并封存源码哈希
//	arena plan   冻结赛程
//	arena run    按冻结赛程跑完所有对局
//	arena rate   出 BT 实力值、95% 区间、计分卡加权总分
//	arena check  单场自检（选手包默认用它）
package main

import (
	"bufio"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strings"
	"time"

	"desertarena/internal/contest"
	"desertarena/internal/engine"
	"desertarena/internal/judge"
	"desertarena/internal/protocol"
	"desertarena/internal/rating"
	"desertarena/internal/schedule"
	"desertarena/internal/sim"
)

func main() {
	if len(os.Args) < 2 {
		usage()
	}
	cmd := os.Args[1]
	args := os.Args[2:]
	var err error
	switch cmd {
	case "build":
		err = cmdBuild(args)
	case "plan":
		err = cmdPlan(args)
	case "run":
		err = cmdRun(args)
	case "rate":
		err = cmdRate(args)
	case "check":
		err = cmdCheck(args)
	case "local":
		err = cmdLocal(args)
	case "version":
		err = cmdVersion(args)
	case "selftest":
		err = cmdSelftest(args)
	case "seal":
		err = cmdSeal(args)
	default:
		usage()
	}
	if err != nil {
		fmt.Fprintln(os.Stderr, "错误："+err.Error())
		os.Exit(1)
	}
}

func usage() {
	fmt.Fprint(os.Stderr, `Desert Arena 正式赛

  arena build  [-config tournament/formal_v19.json]
  arena plan   [-config tournament/formal_v19.json]
	arena run    [-config tournament/formal_v19.json] [-resume] [-limit N]
	arena rate   [-config tournament/formal_v19.json] [-partial]
  arena check  [-a <bot.exe>] [-b <bot.exe>] [-seed N] [-config ...]
  arena local  [-bot bot.exe] [-vs a.exe,b.exe] [-seeds N] [-out 目录]
               选手本地调试：与基线对打，出逐局 VP + 回放（不校验封存）
  arena version        打印规则版本与指纹
  arena selftest [-config ...]   验证超时 kill / 崩溃重启 / 非法结构红牌
  arena seal   [-config ...]   只校验封存记录（源码哈希 / 名单覆盖）

所有相对路径都以本目录（community/contests/war-of-desert/）为基准，请在该目录下执行。
`)
	os.Exit(2)
}

func exeSuffix() string {
	if runtime.GOOS == "windows" {
		return ".exe"
	}
	return ""
}

// cmdSeal 只做封存校验，不跑对局。裁判台用它点亮「封存」状态灯。
func cmdSeal(args []string) error {
	fs := flag.NewFlagSet("seal", flag.ExitOnError)
	cfgPath := fs.String("config", "tournament/formal_v19.json", "赛事配置")
	_ = fs.Parse(args)

	cfg, root, err := contest.Load(*cfgPath)
	if err != nil {
		return err
	}
	seal, err := contest.LoadSeal(root, cfg.Key)
	if err != nil {
		return err
	}
	names := make([]string, 0, len(cfg.Entries))
	for _, e := range cfg.Entries {
		names = append(names, e.Name)
	}
	bad := append(seal.Verify(root), seal.CheckEntryCoverage(names)...)
	if len(bad) > 0 {
		return fmt.Errorf("封存校验未通过（先跑 arena build -config tournament/%s.json）:\n  %s",
			cfg.Key, strings.Join(bad, "\n  "))
	}
	fmt.Printf("SEAL_OK  %s 赛季封存于 %s ｜ %d 名选手 ｜ 源码哈希与工作区一致\n",
		cfg.Key, seal.GeneratedAt, len(seal.Entries))
	return nil
}

/* ------------------------------------------------------------------ */
/* build                                                               */
/* ------------------------------------------------------------------ */

func cmdBuild(args []string) error {
	fs := flag.NewFlagSet("build", flag.ExitOnError)
	cfgPath := fs.String("config", "tournament/formal_v19.json", "赛事配置")
	_ = fs.Parse(args)

	cfg, root, err := contest.Load(*cfgPath)
	if err != nil {
		return err
	}
	rec := &contest.Seal{
		Season:      cfg.Key,
		GeneratedAt: time.Now().Format(time.RFC3339),
		Note:        "封存源码哈希；bin 为编译产物，可随时由 source 重建；shared 为基线共用的策略库（botkit/refbot/acebot）",
		Shared:      map[string]string{},
	}
	for _, shared := range []string{"workspace/botkit", "workspace/refbot", "workspace/acebot"} {
		files, err := hashTree(filepath.Join(root, filepath.FromSlash(shared)))
		if err != nil {
			return err
		}
		for f, h := range files {
			rec.Shared[shared+"/"+f] = h
		}
	}
	for _, e := range cfg.Entries {
		src := filepath.Join(root, filepath.FromSlash(e.Source))
		binRel := e.Bin
		if binRel == "" {
			binRel = filepath.ToSlash(filepath.Join("bots", "bin", e.Name+exeSuffix()))
		}
		bin := binPath(root, binRel)
		if err := os.MkdirAll(filepath.Dir(bin), 0o755); err != nil {
			return err
		}
		if e.Prebuilt {
			sum, err := hashFile(bin)
			if err != nil {
				return fmt.Errorf("预编译选手 %s 的可执行文件不可用（%s）: %w", e.Name, binRel, err)
			}
			fmt.Printf("seal  %-12s 预编译 → %s\n", e.Name, binRel)
			rec.Entries = append(rec.Entries, contest.SealEntry{
				Name: e.Name, Source: e.Source, Bin: binRel,
				Prebuilt: true, BinSHA256: sum,
			})
			continue
		}
		cmd := exec.Command("go", "build", "-o", bin, "./"+filepath.ToSlash(e.Source))
		cmd.Dir = root
		cmd.Stdout = os.Stdout
		cmd.Stderr = os.Stderr
		fmt.Printf("build %-12s %s → %s\n", e.Name, e.Source, binRel)
		if err := cmd.Run(); err != nil {
			return fmt.Errorf("编译 %s 失败: %w", e.Name, err)
		}
		files, err := hashTree(src)
		if err != nil {
			return err
		}
		// 源码型选手同样封存可执行文件哈希：只锁源码而不锁 bin，
		// 赛后无法证明实际跑的就是这份源码编译出的程序。
		binSum, err := hashFile(bin)
		if err != nil {
			return fmt.Errorf("读取 %s 编译产物失败: %w", e.Name, err)
		}
		rec.Entries = append(rec.Entries, contest.SealEntry{
			Name: e.Name, Source: e.Source, Bin: binRel, Files: files, BinSHA256: binSum,
		})
	}
	if err := rec.Save(root, cfg.Key); err != nil {
		return err
	}
	fmt.Printf("封存记录 → bots/%s/%s.json（%d 名选手）\n", contest.SealDir, cfg.Key, len(rec.Entries))
	// 旧版只有一本 bots/封存记录.json，v1.5 实验赛季一 build 就把正式赛季的名册盖掉。
	// 现在按赛季分册，旧文件不再被读写，留着只会误导。
	if legacy := filepath.Join(root, "bots", "封存记录.json"); fileExists(legacy) {
		fmt.Println("提示：旧版单本封存 bots/封存记录.json 已停用（内容可能属于别的赛季），可删除")
	}
	return nil
}

// fileExists 只用来判断旧版封存文件还在不在。
func fileExists(p string) bool {
	st, err := os.Stat(p)
	return err == nil && !st.IsDir()
}

// binPath 把配置里的 bin 解析成绝对路径。
//
// 相对路径按仓库根解析；绝对路径原样使用——基线 Bot 以预编译 exe 的形式
// 放在 algorithm-contest/ 下，不在 Go 模块里，不能被 Join 拼接。
func binPath(root, rel string) string {
	if filepath.IsAbs(rel) || (len(rel) > 1 && rel[1] == ':') {
		return filepath.FromSlash(rel)
	}
	return filepath.Join(root, filepath.FromSlash(rel))
}

func hashFile(p string) (string, error) {
	raw, err := os.ReadFile(p)
	if err != nil {
		return "", err
	}
	sum := sha256.Sum256(raw)
	return hex.EncodeToString(sum[:]), nil
}

func hashTree(dir string) (map[string]string, error) {
	out := map[string]string{}
	err := filepath.Walk(dir, func(p string, info os.FileInfo, err error) error {
		if err != nil || info.IsDir() || !strings.HasSuffix(p, ".go") {
			return err
		}
		raw, err := os.ReadFile(p)
		if err != nil {
			return err
		}
		sum := sha256.Sum256(raw)
		rel, _ := filepath.Rel(dir, p)
		out[filepath.ToSlash(rel)] = hex.EncodeToString(sum[:])
		return nil
	})
	return out, err
}

/* ------------------------------------------------------------------ */
/* plan                                                                */
/* ------------------------------------------------------------------ */

func cmdPlan(args []string) error {
	fs := flag.NewFlagSet("plan", flag.ExitOnError)
	cfgPath := fs.String("config", "tournament/formal_v19.json", "赛事配置")
	_ = fs.Parse(args)

	cfg, root, err := contest.Load(*cfgPath)
	if err != nil {
		return err
	}
	outDir := cfg.OutDir(root)
	plan := schedule.Build(cfg.Entries, cfg.Schedule.Seeds, cfg.Schedule.Mirror, cfg.ScheduleConfig())
	plan.RulesVersion = sim.RulesVersion
	if cfg.Rules.Ruleset == sim.RulesetV15 {
		plan.RulesVersion = sim.RulesVersionV15
	}
	plan.RulesHash = sim.RulesHashFor(cfg.Rules.Ruleset, cfg.Rules.TickLimit, sim.NormalizeVision(sim.Vision{Enabled: cfg.Rules.Vision, Fog: cfg.Rules.Vision, LOS: cfg.Rules.Vision}))
	plan.Engine = cfg.Engine.Kind
	if err := os.MkdirAll(outDir, 0o755); err != nil {
		return err
	}
	if err := schedule.Save(filepath.Join(outDir, "赛程.json"), plan); err != nil {
		return err
	}
	fmt.Printf("plan → %s（%d 场对阵 × %d 局，%d 名选手，seed %v）\n",
		filepath.Join(outDir, "赛程.json"), len(plan.Matches), gamesPerMatch(cfg.Schedule.Mirror), len(plan.Entries), cfg.Schedule.Seeds)
	return nil
}

func gamesPerMatch(mirror bool) int {
	if mirror {
		return 2
	}
	return 1
}

/* ------------------------------------------------------------------ */
/* run                                                                 */
/* ------------------------------------------------------------------ */

type gameLine struct {
	Match      int         `json:"match"`
	Seed       int         `json:"seed"`
	Game       int         `json:"game"`
	Swap       bool        `json:"swap"`
	Seats      []string    `json:"seats"`
	Winner     string      `json:"winner"`
	WinnerSeat int         `json:"winnerSeat"`
	Level      int         `json:"level"`
	Reason     string      `json:"reason"`
	Turn       int         `json:"turn"`
	VP         []int       `json:"vp"`
	Totals     []float64   `json:"totals"`
	Fields     [][]float64 `json:"fields"`
	RulesHash  string      `json:"rulesHash"`
	Faults     []int       `json:"faults"`
	VerifyOK   *bool       `json:"verifyOk,omitempty"`
	Invalid    bool        `json:"invalid,omitempty"`
	DurationMS int64       `json:"durationMs"`
}

// resolveOutDir 解析结果目录：默认取配置 output，-dir 可覆盖；强制留在仓库根内，
// 防止 `-dir ../…` 把赛果写到仓库之外。
func resolveOutDir(root, dirFlag, def string) (string, error) {
	outDir := filepath.Join(root, filepath.FromSlash(def))
	if dirFlag != "" {
		outDir = filepath.Join(root, filepath.FromSlash(dirFlag))
	}
	absDir, err := filepath.Abs(outDir)
	if err != nil {
		return "", err
	}
	absRoot, err := filepath.Abs(root)
	if err != nil {
		return "", err
	}
	if absDir != absRoot && !strings.HasPrefix(absDir, absRoot+string(os.PathSeparator)) {
		return "", fmt.Errorf("结果目录必须在仓库目录内: %s", dirFlag)
	}
	return outDir, nil
}

func cmdRun(args []string) error {
	fs := flag.NewFlagSet("run", flag.ExitOnError)
	cfgPath := fs.String("config", "tournament/formal_v19.json", "赛事配置")
	resume := fs.Bool("resume", false, "跳过 结果.jsonl 中已完成的对阵")
	limit := fs.Int("limit", 0, "最多再跑 N 场（0 = 不限）")
	skipSeal := fs.Bool("skip-seal", false, "跳过封存校验（仅限演练，正式赛禁用）")
	_ = fs.Parse(args)

	cfg, root, err := contest.Load(*cfgPath)
	if err != nil {
		return err
	}
	// 开赛硬门槛：源码哈希必须与封存记录一致，否则赛后无法自证跑的是收卷那份代码。
	var seal *contest.Seal
	if !*skipSeal {
		seal, err = contest.LoadSeal(root, cfg.Key)
		if err != nil {
			return err
		}
		names := make([]string, 0, len(cfg.Entries))
		for _, e := range cfg.Entries {
			names = append(names, e.Name)
		}
		bad := append(seal.Verify(root), seal.CheckEntryCoverage(names)...)
		if len(bad) > 0 {
			return fmt.Errorf("封存校验未通过（先跑 arena build -config tournament/%s.json）:\n  %s",
				cfg.Key, strings.Join(bad, "\n  "))
		}
	}
	outDir, err := resolveOutDir(root, "", cfg.Output)
	if err != nil {
		return err
	}
	plan, err := schedule.Load(filepath.Join(outDir, "赛程.json"))
	if err != nil {
		return fmt.Errorf("读不到冻结赛程（先跑 arena plan）: %w", err)
	}
	runRules, err := resolveRunRules(plan, cfg)
	if err != nil {
		return err
	}
	// 冻结赛程必须与当前引擎同一套规则；否则 seed 与规则指纹都会对不上。
	wantHash := sim.RulesHashFor(runRules.Ruleset, runRules.TickLimit, sim.NormalizeVision(sim.Vision{
		Enabled: runRules.Vision, Fog: runRules.Vision, LOS: runRules.Vision,
	}))
	if plan.RulesHash != "" && plan.RulesHash != wantHash {
		return fmt.Errorf("冻结赛程 rulesHash=%s 与当前引擎 rulesHash=%s 不一致（规则已变，先重新 arena plan）",
			plan.RulesHash, wantHash)
	}
	binByName, err := resolveRunBins(root, cfg, plan, seal)
	if err != nil {
		return err
	}
	resultsPath := filepath.Join(outDir, "结果.jsonl")
	done := map[int]bool{}
	if *resume {
		if f, err := os.Open(resultsPath); err == nil {
			sc := bufio.NewScanner(f)
			sc.Buffer(make([]byte, 1<<20), 1<<20)
			type resultLine struct {
				raw   string
				match int
				game  int
			}
			var all []resultLine
			seen := map[int]map[int]bool{}
			bad := map[int]bool{}
			for sc.Scan() {
				var l gameLine
				if json.Unmarshal(sc.Bytes(), &l) != nil {
					continue
				}
				rl := resultLine{raw: sc.Text(), match: l.Match, game: l.Game}
				if cfg.Verify && (l.VerifyOK == nil || !*l.VerifyOK) {
					bad[l.Match] = true
				}
				all = append(all, rl)
				if seen[l.Match] == nil {
					seen[l.Match] = map[int]bool{}
				}
				seen[l.Match][l.Game] = true
			}
			scanErr := sc.Err()
			f.Close()
			if scanErr != nil {
				return fmt.Errorf("读取结果.jsonl 失败: %w", scanErr)
			}
			// 一场必须换边两局（Mirror=true）且复核通过才算完成；只写了一局就崩溃、
			// 或复核失败的场次，旧逻辑会整场跳过，这里改为丢弃残余行后重跑。
			want := 1
			if plan.Mirror {
				want = 2
			}
			for id, games := range seen {
				if bad[id] {
					continue
				}
				complete := true
				for gi := 0; gi < want; gi++ {
					if !games[gi] {
						complete = false
						break
					}
				}
				if complete {
					done[id] = true
				}
			}
			var kept []string
			for _, rl := range all {
				if done[rl.match] {
					kept = append(kept, rl.raw)
				}
			}
			if len(kept) != len(all) {
				data := []byte{}
				if len(kept) > 0 {
					data = []byte(strings.Join(kept, "\n") + "\n")
				}
				tmp := resultsPath + ".tmp"
				if err := os.WriteFile(tmp, data, 0o644); err != nil {
					return fmt.Errorf("整理结果.jsonl 失败: %w", err)
				}
				if err := os.Rename(tmp, resultsPath); err != nil {
					return fmt.Errorf("替换结果.jsonl 失败: %w", err)
				}
			}
		}
	}
	var out *os.File
	if *resume {
		out, err = os.OpenFile(resultsPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0o644)
	} else {
		out, err = os.Create(resultsPath)
	}
	if err != nil {
		return err
	}
	defer out.Close()
	w := bufio.NewWriter(out)
	defer w.Flush()

	replayDir := filepath.Join(outDir, "replays")
	if err := os.MkdirAll(replayDir, 0o755); err != nil {
		return err
	}

	hostScript, err := cfg.EngineHostPath(root)
	if err != nil {
		return err
	}
	judgeCfg := judge.Config{
		HostScript: hostScript,
		Rules:      runRules,
		Mirror:     plan.Mirror,
		MaxStrikes: plan.Config.MaxStrikes,
		StrikeMode: plan.Config.StrikeMode,
		Verify:     cfg.Verify,
	}

	ran := 0
	written := 0
	started := time.Now()
	for _, m := range plan.Matches {
		if *resume && done[m.ID] {
			continue
		}
		if *limit > 0 && ran >= *limit {
			break
		}
		binA, ok := binByName[m.A]
		if !ok {
			return fmt.Errorf("赛程里的选手 %s 不在 entries 中", m.A)
		}
		binB, ok := binByName[m.B]
		if !ok {
			return fmt.Errorf("赛程里的选手 %s 不在 entries 中", m.B)
		}
		specA := judge.BotSpec{Name: m.A, Path: binA}
		specB := judge.BotSpec{Name: m.B, Path: binB}
		rec, err := judge.RunMatch(m.ID, m.Seed, specA, specB, judgeCfg)
		if err != nil {
			return fmt.Errorf("第 %d 场（%s vs %s）失败: %w", m.ID, m.A, m.B, err)
		}
		writeReplay(replayDir, rec)
		for _, g := range rec.Games {
			line := gameLine{
				Match: m.ID, Seed: g.Seed, Game: g.Index, Swap: g.Swap, Seats: g.Seats,
				Winner: protocol.WinnerName(g.Result.Winner, g.Seats), WinnerSeat: g.Result.Winner,
				Level: g.Result.Level, Reason: g.Result.Reason, Turn: g.Result.Turn,
				VP: g.Result.VP, RulesHash: rec.RulesHash, Faults: []int{len(g.Faults[0]), len(g.Faults[1])},
				Invalid: g.Result.Invalid, DurationMS: g.DurationMS,
			}
			for _, c := range g.Result.Cards {
				line.Totals = append(line.Totals, round2(c.Total))
				line.Fields = append(line.Fields, c.Fields)
			}
			if len(g.Verify) > 0 {
				var v struct {
					OK bool `json:"ok"`
				}
				if json.Unmarshal(g.Verify, &v) == nil {
					line.VerifyOK = &v.OK
				}
			}
			// 正式赛把确定性复核当门禁：复核失败/缺失都中止，不写进结果。
			if cfg.Verify && (line.VerifyOK == nil || !*line.VerifyOK) {
				return fmt.Errorf("第 %d 场（%s vs %s）第 %d 局确定性复核未通过，已中止",
					m.ID, m.A, m.B, g.Index+1)
			}
			raw, _ := json.Marshal(line)
			w.Write(append(raw, '\n'))
			written++
		}
		ran++
		fmt.Printf("[%d/%d] %-4d %-12s vs %-12s  seed=%d  %s  %s\n",
			len(done)+ran, len(plan.Matches), m.ID, m.A, m.B, rec.Seed,
			scoreText(rec), durationText(rec.DurationMS))
	}
	fmt.Printf("run 完成：%d 场 / %d 局，用时 %s，结果 → %s\n",
		ran, written, time.Since(started).Round(time.Second), resultsPath)
	return nil
}

func writeReplay(dir string, rec *judge.MatchRecord) {
	payload := map[string]any{
		"version": 1,
		"match":   rec.Index,
		"seed":    rec.Seed,
		"header":  rec.Header,
		"games":   rec.Games,
	}
	raw, err := json.Marshal(payload)
	if err != nil {
		return
	}
	_ = os.WriteFile(filepath.Join(dir, fmt.Sprintf("match-%03d.json", rec.Index)), raw, 0o644)
}

func scoreText(rec *judge.MatchRecord) string {
	parts := make([]string, 0, len(rec.Games))
	for _, g := range rec.Games {
		w := g.Result.Winner
		mark := "平"
		if w == 0 {
			mark = "左胜"
		} else if w == 1 {
			mark = "右胜"
		}
		parts = append(parts, fmt.Sprintf("%s(L%d)", mark, g.Result.Level))
	}
	return strings.Join(parts, " ")
}

func durationText(ms int64) string {
	return (time.Duration(ms) * time.Millisecond).Round(100 * time.Millisecond).String()
}

func round2(v float64) float64 { return float64(int64(v*100+0.5)) / 100 }

/* ------------------------------------------------------------------ */
/* rate                                                                */
/* ------------------------------------------------------------------ */

type standing struct {
	Name   string
	Played int
	Wins   int
	Draws  int
	Losses int
	Points float64
	Total  float64
	Faults int
}

func cmdRate(args []string) error {
	fs := flag.NewFlagSet("rate", flag.ExitOnError)
	cfgPath := fs.String("config", "tournament/formal_v19.json", "赛事配置")
	partial := fs.Bool("partial", false, "允许赛中不完整结果（输出 临时成绩.md，不覆盖正式成绩）")
	_ = fs.Parse(args)

	cfg, root, err := contest.Load(*cfgPath)
	if err != nil {
		return err
	}
	outDir, err := resolveOutDir(root, "", cfg.Output)
	if err != nil {
		return err
	}
	plan, err := schedule.Load(filepath.Join(outDir, "赛程.json"))
	if err != nil {
		return err
	}
	wantHash := sim.RulesHashFor(cfg.Rules.Ruleset, cfg.Rules.TickLimit, sim.NormalizeVision(sim.Vision{
		Enabled: cfg.Rules.Vision, Fog: cfg.Rules.Vision, LOS: cfg.Rules.Vision,
	}))
	if plan.RulesHash != "" && plan.RulesHash != wantHash {
		return fmt.Errorf("冻结赛程 rulesHash=%s 与当前引擎 rulesHash=%s 不一致，拒绝评分", plan.RulesHash, wantHash)
	}
	f, err := os.Open(filepath.Join(outDir, "结果.jsonl"))
	if err != nil {
		return err
	}
	defer f.Close()
	var lines []gameLine
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	for sc.Scan() {
		var l gameLine
		if err := json.Unmarshal(sc.Bytes(), &l); err != nil {
			return fmt.Errorf("结果.jsonl 某行非法: %w", err)
		}
		// 不同规则版本的局不能混在一起拟合，否则实力值没有可比性。
		if l.RulesHash != wantHash {
			return fmt.Errorf("结果.jsonl 中 match=%d game=%d 的 rulesHash=%q 与当前引擎 %q 不一致，拒绝评分",
				l.Match, l.Game, l.RulesHash, wantHash)
		}
		if l.VerifyOK != nil && !*l.VerifyOK {
			return fmt.Errorf("结果.jsonl 中 match=%d game=%d 的确定性复核未通过，拒绝评分", l.Match, l.Game)
		}
		if cfg.Verify && l.VerifyOK == nil {
			return fmt.Errorf("结果.jsonl 中 match=%d game=%d 缺少确定性复核结果，拒绝评分", l.Match, l.Game)
		}
		lines = append(lines, l)
	}
	if err := sc.Err(); err != nil {
		return fmt.Errorf("读取结果.jsonl 失败: %w", err)
	}
	if len(lines) == 0 {
		return fmt.Errorf("结果.jsonl 为空，先跑 arena run")
	}
	expectedGames, err := validateResultsAgainstPlan(plan, lines, *partial)
	if err != nil {
		return err
	}

	// 计分域数从结果行推导（v1.0 八领域 / v1.1 七领域），标签不再硬编码。
	domains := 0
	for _, l := range lines {
		if len(l.Fields) > 0 {
			domains = len(l.Fields[0])
			break
		}
	}
	if domains == 0 {
		domains = 7
	}
	domainLabel := fmt.Sprintf("%d领域", domains)

	table := map[string]*standing{}
	get := func(n string) *standing {
		if _, ok := table[n]; !ok {
			table[n] = &standing{Name: n}
		}
		return table[n]
	}
	for _, e := range plan.Entries {
		get(e.Name)
	}
	var btGames []rating.Game
	invalidGames := 0
	for _, l := range lines {
		// v1.6：无进展/卡死局不进 BT 与加权总分，避免无效平局污染排名。
		if l.Invalid {
			invalidGames++
			continue
		}
		a, b := l.Seats[0], l.Seats[1]
		score := 0.5
		if l.WinnerSeat == 0 {
			score = 1
		} else if l.WinnerSeat == 1 {
			score = 0
		}
		// Group = 场次 + 1：同一场换边两局作为整块抽样，避免把同图两局当独立样本。
		btGames = append(btGames, rating.Game{A: a, B: b, Score: score, Group: l.Match + 1})
		sa, sb := get(a), get(b)
		sa.Played++
		sb.Played++
		sa.Points += score
		sb.Points += 1 - score
		switch l.WinnerSeat {
		case 0:
			sa.Wins++
			sb.Losses++
		case 1:
			sb.Wins++
			sa.Losses++
		default:
			sa.Draws++
			sb.Draws++
		}
		sa.Faults += l.Faults[0]
		sb.Faults += l.Faults[1]
		if len(l.Totals) == 2 {
			sa.Total += l.Totals[0]
			sb.Total += l.Totals[1]
		}
	}

	jsonName, mdName, totalName := "成绩.json", "成绩.md", "成绩_加权总分.md"
	titleKind := "正式赛成绩"
	if *partial && len(lines) < expectedGames {
		jsonName, mdName, totalName = "临时成绩.json", "临时成绩.md", "临时成绩_加权总分.md"
		titleKind = fmt.Sprintf("临时成绩（未完赛 %d/%d 局）", len(lines), expectedGames)
	}

	anchor := plan.Entries[0].Name
	bt := rating.Fit(btGames, anchor, cfg.Bootstrap(), 20261109)
	rawBT, _ := json.MarshalIndent(bt, "", "  ")
	if err := os.WriteFile(filepath.Join(outDir, jsonName), append(rawBT, '\n'), 0o644); err != nil {
		return err
	}

	type row struct {
		Name      string
		R         float64
		Lo        float64
		Hi        float64
		Intervals bool
		St        *standing
	}
	var rows []row
	for name := range table {
		r := row{Name: name, R: bt.Ratings[name], St: table[name]}
		if iv, ok := bt.Intervals[name]; ok {
			r.Lo, r.Hi, r.Intervals = iv[0], iv[1], true
		}
		rows = append(rows, r)
	}
	sort.Slice(rows, func(i, j int) bool {
		if rows[i].R != rows[j].R {
			return rows[i].R > rows[j].R
		}
		return rows[i].St.Points > rows[j].St.Points
	})

	var b strings.Builder
	fmt.Fprintf(&b, "# %s · %s\n\n", cfg.Name, titleKind)
	fmt.Fprintf(&b, "> 赛季：%s ｜ 规则 %s ｜ 引擎 %s ｜ 规则指纹：%s ｜ 局数：%d（无效 %d） ｜ 基准 seed：%v（逐场按序递增）\n",
		cfg.Season, versionOr(plan.RulesVersion), valueOr(plan.Engine, cfg.Engine.Kind), lines[0].RulesHash, len(lines), invalidGames, cfg.Schedule.Seeds)
	fmt.Fprintf(&b, "> 评分口径：%s；%s。\n\n", bt.Method, bt.Scale)
	fmt.Fprintf(&b, "| 名次 | 选手 | 实力值 | 95%% 区间 | 胜/平/负 | 局数 | %s总分 |\n|---:|---|---:|---|---:|---:|---:|\n", domainLabel)
	for i, r := range rows {
		iv := "—"
		if r.Intervals {
			iv = fmt.Sprintf("%.1f ~ %.1f", r.Lo, r.Hi)
		}
		fmt.Fprintf(&b, "| %d | %s | %.1f | %s | %d/%d/%d | %d | %.1f |\n",
			i+1, r.Name, r.R, iv, r.St.Wins, r.St.Draws, r.St.Losses, r.St.Played, r.St.Total)
	}
	fmt.Fprintf(&b, "\n基准锚：%s = 0；有效 %d 局（排除无效 %d）；bootstrap %d 次。\n", bt.Anchor, bt.Games, invalidGames, bt.Bootstrap)
	fmt.Fprintf(&b, "\n> 胜负标签与%s加权总分是两条独立信号：本表按胜负拟合实力值，加权总分单列。\n", domainLabel)
	fmt.Fprintf(&b, "> 区间按对局整块重抽样；全胜/全败时区间会退化，属统计口径而非成绩异常。\n")
	if err := os.WriteFile(filepath.Join(outDir, mdName), []byte(b.String()), 0o644); err != nil {
		return err
	}

	var t strings.Builder
	fmt.Fprintf(&t, "# %s加权总分\n\n| 名次 | 选手 | 合计 | 场均 | 故障 |\n|---:|---|---:|---:|---:|\n", domainLabel)
	byTotal := make([]*standing, 0, len(table))
	for _, s := range table {
		byTotal = append(byTotal, s)
	}
	sort.Slice(byTotal, func(i, j int) bool { return byTotal[i].Total > byTotal[j].Total })
	for i, s := range byTotal {
		avg := 0.0
		if s.Played > 0 {
			avg = s.Total / float64(s.Played)
		}
		fmt.Fprintf(&t, "| %d | %s | %.1f | %.2f | %d |\n", i+1, s.Name, s.Total, avg, s.Faults)
	}
	if err := os.WriteFile(filepath.Join(outDir, totalName), []byte(t.String()), 0o644); err != nil {
		return err
	}

	fmt.Printf("rate → %s\n", filepath.Join(outDir, mdName))
	fmt.Printf("       %s\n", filepath.Join(outDir, totalName))
	fmt.Printf("       %s\n", filepath.Join(outDir, jsonName))
	return nil
}

/* ------------------------------------------------------------------ */
/* check                                                               */
/* ------------------------------------------------------------------ */

// loadConfigOrDefault 读配置；默认路径不存在时用内置的正式赛默认值。
//
// 选手包里的 arena/ 只有引擎源码，没有 tournament/ 目录，但 check 得能直接跑；
// 显式指定的配置文件找不到时仍然报错，不静默降级。
func loadConfigOrDefault(p string) (*contest.Config, string, error) {
	if st, err := os.Stat(p); err == nil && !st.IsDir() {
		return contest.Load(p)
	}
	if p == "tournament/formal_v19.json" {
		return contest.Default(), ".", nil
	}
	return nil, "", fmt.Errorf("找不到配置 %s", p)
}

func cmdCheck(args []string) error {
	fs := flag.NewFlagSet("check", flag.ExitOnError)
	cfgPath := fs.String("config", "tournament/formal_v19.json", "赛事配置")
	aPath := fs.String("a", "", "左方程序（默认 workspace/submission 编译产物）")
	bPath := fs.String("b", "", "右方程序（默认第一个参考 Bot）")
	seed := fs.Int("seed", 20261109, "seed")
	_ = fs.Parse(args)

	cfg, root, err := loadConfigOrDefault(*cfgPath)
	if err != nil {
		return err
	}
	if *aPath == "" {
		*aPath = filepath.Join(root, "bots", "bin", "submission"+exeSuffix())
	}
	if *bPath == "" {
		if len(cfg.Entries) > 0 {
			*bPath = binPath(root, cfg.Entries[0].Bin)
		} else {
			// 选手包布局：<workspace>/{bot.exe, bots/baseline@1.exe}
			*bPath = filepath.Join(filepath.Dir(*aPath), "bots", "baseline@1.exe")
		}
	}
	for _, p := range []string{*aPath, *bPath} {
		if _, err := os.Stat(p); err != nil {
			return fmt.Errorf("找不到程序 %s（先跑 arena build 或 go build）", p)
		}
	}
	hostScript, err := cfg.EngineHostPath(root)
	if err != nil {
		return err
	}
	jcfg := judge.Config{
		HostScript: hostScript,
		Rules: engine.HostConfig{
			Generator: cfg.Rules.Generator,
			TickLimit: cfg.Rules.TickLimit,
			TickMs:    cfg.Rules.TickMs,
			DecideMs:  cfg.Rules.DecideMs,
			InitMs:    cfg.Rules.InitMs,
			Vision:    cfg.Rules.Vision,
			Ruleset:   cfg.Rules.Ruleset,
		},
		Mirror:     true,
		MaxStrikes: cfg.Rules.MaxStrikes,
		StrikeMode: cfg.Rules.StrikeMode,
		Verify:     true,
	}
	rec, err := judge.RunMatch(0, *seed, judge.BotSpec{Name: "A", Path: *aPath}, judge.BotSpec{Name: "B", Path: *bPath}, jcfg)
	if err != nil {
		return err
	}
	fmt.Printf("  引擎：%s ｜ 地图：%d×%d ｜ 规则指纹：%s ｜ seed=%d ｜ 生成器：%s\n",
		protocol.ProtoName, protocol.Grid, protocol.Grid, rec.RulesHash, rec.Seed, rec.Generator)
	verifyFailed := false
	for _, g := range rec.Games {
		fmt.Printf("  第 %d 局  座位=%s / %s  tick=%d  winner=%s  层级=%d  VP=%v  总分=%.2f : %.2f  故障=%d/%d\n",
			g.Index+1, g.Seats[0], g.Seats[1], g.Result.Turn,
			protocol.WinnerName(g.Result.Winner, g.Seats), g.Result.Level, g.Result.VP,
			g.Result.Cards[0].Total, g.Result.Cards[1].Total, len(g.Faults[0]), len(g.Faults[1]))
		fmt.Printf("    判定：%s ｜ 帧=%d ｜ 平均决策=%.3fms / %.3fms\n",
			g.Result.Reason, len(g.Frames), g.Result.Decide[0].AvgMS, g.Result.Decide[1].AvgMS)
		for seat, list := range g.Faults {
			for i, f := range list {
				if i >= 3 {
					fmt.Printf("      座位%d 故障（仅列前 3 条）…\n", seat)
					break
				}
				fmt.Printf("      座位%d 故障 t=%d %s: %s\n", seat, f.Turn, f.Kind, f.Err)
			}
		}
		if len(g.Verify) > 0 {
			var v struct {
				OK bool `json:"ok"`
			}
			_ = json.Unmarshal(g.Verify, &v)
			fmt.Printf("    确定性复核：%v\n", v.OK)
			if !v.OK {
				verifyFailed = true
			}
		} else {
			fmt.Println("    确定性复核：缺失")
			verifyFailed = true
		}
	}
	if verifyFailed {
		return fmt.Errorf("确定性复核未通过，check 失败")
	}
	fmt.Println("CHECK_OK")
	return nil
}

// cmdVersion 打印规则身份，供打包脚本与选手核对「裁判/宿主/规则」三者是否配套。
func cmdVersion(args []string) error {
	fs := flag.NewFlagSet("version", flag.ExitOnError)
	tickLimit := fs.Int("ticklimit", 1500, "tick 上限")
	ruleset := fs.String("ruleset", sim.RulesetV15, "规则集：v14（历史基线）/ v15（当前正式）")
	_ = fs.Parse(args)
	rs := strings.ToLower(strings.TrimSpace(*ruleset))
	version := sim.RulesVersion
	if rs == sim.RulesetV15 {
		version = sim.RulesVersionV15
	}
	fmt.Printf("rulesVersion=%s\n", version)
	fmt.Printf("rulesHash=%s\n", sim.RulesHashFor(rs, *tickLimit, sim.NormalizeVision(sim.Vision{})))
	fmt.Printf("proto=%s.%d\n", protocol.ProtoName, protocol.ProtoVersion)
	return nil
}

/* ------------------------------------------------------------------ */
/* local                                                               */
/* ------------------------------------------------------------------ */

// cmdLocal 是发给选手的本地对局器：拿自己编的 Bot 跟基线对打。
//
// 它与正式赛共用同一套裁判（internal/judge）与规则（internal/sim），差别只有
// 三点：不校验封存记录、不读正式赛赛程、结果写到当前目录。这样选手在自己
// 机器上看到的结果，就是赛场上会拿到的结果。
func cmdLocal(args []string) error {
	fs := flag.NewFlagSet("local", flag.ExitOnError)
	botPath := fs.String("bot", "bot.exe", "要调试的 Bot 可执行文件")
	vsFlag := fs.String("vs", "", "对手 exe，逗号分隔（默认取 bots/baseline@*.exe）")
	nSeeds := fs.Int("seeds", 2, "跑几个 seed（每个 seed 换边两局）")
	baseSeed := fs.Int("seed0", 20261109, "起始 seed")
	outDir := fs.String("out", "matches", "回放与结果目录")
	limit := fs.Int("limit", 0, "覆盖 tick 上限（0 = 规则集默认）")
	rulesetFlag := fs.String("ruleset", sim.RulesetV14, "规则集：v14（历史基线）/ v15（当前正式）")
	_ = fs.Parse(args)

	ruleset := strings.ToLower(strings.TrimSpace(*rulesetFlag))
	if ruleset != sim.RulesetV14 && ruleset != sim.RulesetV15 {
		return fmt.Errorf("未知规则集 %q（支持 %s / %s）", *rulesetFlag, sim.RulesetV14, sim.RulesetV15)
	}

	botAbs, err := filepath.Abs(*botPath)
	if err != nil {
		return err
	}
	if st, err := os.Stat(botAbs); err != nil || st.IsDir() {
		return fmt.Errorf("找不到你的 Bot：%s（先在 workspace 下 go build -o bot.exe ./submission）", *botPath)
	}

	opponents, err := localOpponents(*vsFlag, botAbs)
	if err != nil {
		return err
	}

	host, err := localHost()
	if err != nil {
		return err
	}

	tickLimit := 1200
	if ruleset == sim.RulesetV15 {
		tickLimit = 1500
	}
	if *limit > 0 {
		tickLimit = *limit
	}
	vision := sim.NormalizeVision(sim.Vision{})
	wantHash := sim.RulesHashFor(ruleset, tickLimit, vision)

	jcfg := judge.Config{
		HostScript: host,
		Rules: engine.HostConfig{
			Generator: "minecraft",
			TickLimit: tickLimit,
			TickMs:    50,
			DecideMs:  120,
			InitMs:    2000,
			Vision:    false,
			Ruleset:   ruleset,
		},
		Mirror:     true,
		MaxStrikes: 3,
		StrikeMode: "consecutive",
		Verify:     true,
	}

	you := "你的 Bot"
	version := sim.RulesVersion
	label := "v14 正式"
	if ruleset == sim.RulesetV15 {
		version = sim.RulesVersionV15
		label = "v15 实验"
	}
	fmt.Println("Desert Arena 本地对局")
	fmt.Printf("  规则 %s（%s）｜ 指纹 %s ｜ %d tick ｜ 换边两局 ｜ 确定性复核 开\n",
		version, label, wantHash, tickLimit)
	if *limit > 0 && *limit != 1200 && ruleset == sim.RulesetV14 {
		fmt.Printf("  ⚠ tick 上限被改成 %d，与正式赛（1200）不同，结果不可与正式成绩比较\n", *limit)
	}
	fmt.Printf("  Bot ：%s\n", botAbs)
	fmt.Printf("  对手：%s\n", strings.Join(opponents, "、"))
	fmt.Printf("  宿主：%s\n\n", host)

	replayDir := filepath.Join(*outDir, "replays")
	if err := os.MkdirAll(replayDir, 0o755); err != nil {
		return err
	}
	resultsPath := filepath.Join(*outDir, "结果.jsonl")
	rf, err := os.Create(resultsPath)
	if err != nil {
		return err
	}
	defer rf.Close()
	w := bufio.NewWriter(rf)
	defer w.Flush()

	if *nSeeds <= 0 {
		*nSeeds = 1
	}
	wins, draws, losses := 0, 0, 0
	sumYou, sumOpp, games, faultTotal := 0, 0, 0, 0
	maxDecide := 0.0
	matchIdx := 0
	started := time.Now()

	for _, opp := range opponents {
		oppName := strings.TrimSuffix(filepath.Base(opp), filepath.Ext(opp))
		fmt.Printf("── 对手 %s ──\n", oppName)
		for k := 0; k < *nSeeds; k++ {
			seed := *baseSeed + k*7
			rec, err := judge.RunMatch(matchIdx, seed,
				judge.BotSpec{Name: you, Path: botAbs},
				judge.BotSpec{Name: oppName, Path: opp}, jcfg)
			if err != nil {
				return fmt.Errorf("对局失败（%s / seed=%d）: %w", oppName, seed, err)
			}
			if rec.RulesHash != wantHash {
				return fmt.Errorf("裁判程序与引擎宿主版本不一致：\n  裁判期望 %s\n  宿主实际 %s\n"+
					"tools/arena.exe 与 tools/enginehost.exe 必须成对重编（让主办方重新发卷）",
					wantHash, rec.RulesHash)
			}
			writeReplay(replayDir, rec)
			matchIdx++

			for _, g := range rec.Games {
				seatYou := 0
				if g.Seats[0] != you {
					seatYou = 1
				}
				seatOpp := 1 - seatYou
				mark := "平"
				switch {
				case g.Result.Winner == seatYou:
					mark = "胜"
					wins++
				case g.Result.Winner == seatOpp:
					mark = "负"
					losses++
				default:
					draws++
				}
				myVP, opVP := g.Result.VP[seatYou], g.Result.VP[seatOpp]
				sumYou += myVP
				sumOpp += opVP
				games++

				faults := len(g.Faults[seatYou])
				faultTotal += faults
				ds := g.Result.Decide[seatYou].AvgMS
				if ds > maxDecide {
					maxDecide = ds
				}

				line := gameLine{
					Match: rec.Index, Seed: g.Seed, Game: g.Index, Swap: g.Swap, Seats: g.Seats,
					Winner: protocol.WinnerName(g.Result.Winner, g.Seats), WinnerSeat: g.Result.Winner,
					Level: g.Result.Level, Reason: g.Result.Reason, Turn: g.Result.Turn,
					VP: g.Result.VP, RulesHash: rec.RulesHash,
					Faults:  []int{len(g.Faults[0]), len(g.Faults[1])},
					Invalid: g.Result.Invalid, DurationMS: g.DurationMS,
				}
				for _, c := range g.Result.Cards {
					line.Totals = append(line.Totals, round2(c.Total))
					line.Fields = append(line.Fields, c.Fields)
				}
				if len(g.Verify) > 0 {
					var v struct {
						OK bool `json:"ok"`
					}
					if json.Unmarshal(g.Verify, &v) == nil {
						line.VerifyOK = &v.OK
					}
				}
				raw, _ := json.Marshal(line)
				w.Write(append(raw, '\n'))

				fmt.Printf("  seed %-8d 第%d局 座位%d  VP %4d:%-4d  %s  层级%d  故障%d  决策%.1fms\n",
					g.Seed, g.Index+1, seatYou, myVP, opVP, mark, g.Result.Level, faults, ds)
				for i, f := range g.Faults[seatYou] {
					if i >= 2 {
						fmt.Printf("      其余 %d 条故障见回放\n", len(g.Faults[seatYou])-2)
						break
					}
					fmt.Printf("      t=%d %s: %s\n", f.Turn, f.Kind, f.Err)
				}
			}
		}
	}

	avgYou, avgOpp := 0.0, 0.0
	if games > 0 {
		avgYou = float64(sumYou) / float64(games)
		avgOpp = float64(sumOpp) / float64(games)
	}
	fmt.Printf("\n合计：%d 胜 %d 平 %d 负（%d 局）｜平均 VP %.0f : %.0f｜最大决策 %.1fms｜故障 %d 条\n",
		wins, draws, losses, games, avgYou, avgOpp, maxDecide, faultTotal)
	fmt.Printf("用时 %s ｜ 回放 → %s ｜ 结果 → %s\n",
		time.Since(started).Round(100*time.Millisecond), replayDir, resultsPath)
	if faultTotal > 0 {
		fmt.Println("⚠ 有故障：机器人超时 / 崩溃 / 返回非法结构都会记在这里，正式赛按红牌处理。")
	}
	return nil
}

// localOpponents 解析对手列表；没给就在 bot 旁边和当前目录找基线 exe。
//
// 选手包布局是 <workspace>/{bot.exe, bots/baseline@*.exe}，而 arena 源码在
// <workspace>/arena/，所以只按 cwd 找会落空——先看 bot 所在目录。
func localOpponents(vs, botPath string) ([]string, error) {
	if strings.TrimSpace(vs) != "" {
		var out []string
		for _, raw := range strings.Split(vs, ",") {
			raw = strings.TrimSpace(raw)
			if raw == "" {
				continue
			}
			abs, err := filepath.Abs(raw)
			if err != nil {
				return nil, err
			}
			if st, err := os.Stat(abs); err != nil || st.IsDir() {
				return nil, fmt.Errorf("找不到对手程序：%s", raw)
			}
			out = append(out, abs)
		}
		if len(out) > 0 {
			return out, nil
		}
	}
	dirs := []string{filepath.Join(filepath.Dir(botPath), "bots"), "bots"}
	for _, dir := range dirs {
		cands, _ := filepath.Glob(filepath.Join(dir, "baseline@*.exe"))
		if len(cands) == 0 {
			continue
		}
		sort.Strings(cands)
		out := make([]string, 0, len(cands))
		for _, c := range cands {
			abs, err := filepath.Abs(c)
			if err != nil {
				return nil, err
			}
			out = append(out, abs)
		}
		return out, nil
	}
	return nil, fmt.Errorf("没指定对手，也没在 %s 下找到 baseline@*.exe", dirs[0])
}

// localHost 找引擎宿主：优先与裁判程序放在一起的那份，其次当前目录的源码。
func localHost() (string, error) {
	if p := contest.PackagedEngineHost(); p != "" {
		return p, nil
	}
	if st, err := os.Stat(filepath.Join("cmd", "enginehost")); err == nil && st.IsDir() {
		// 编到 .bin/ 而不是 bots/bin/：选手包里的 arena/ 是只读源码副本，
		// 不该因为跑一次本地对局就多出一棵构建产物目录。
		bin := filepath.Join(".bin", "enginehost"+exeSuffix())
		_ = os.MkdirAll(filepath.Dir(bin), 0o755)
		build := exec.Command("go", "build", "-o", bin, "./cmd/enginehost")
		build.Stdout, build.Stderr = os.Stdout, os.Stderr
		if err := build.Run(); err != nil {
			return "", fmt.Errorf("编译 enginehost 失败: %w", err)
		}
		return filepath.Abs(bin)
	}
	if st, err := os.Stat("enginehost" + exeSuffix()); err == nil && !st.IsDir() {
		return filepath.Abs("enginehost" + exeSuffix())
	}
	return "", fmt.Errorf("找不到引擎宿主 enginehost%s：它必须和 arena%s 放在同一目录（tools/）",
		exeSuffix(), exeSuffix())
}

/* ------------------------------------------------------------------ */
/* selftest                                                            */
/* ------------------------------------------------------------------ */

func cmdSelftest(args []string) error {
	fs := flag.NewFlagSet("selftest", flag.ExitOnError)
	cfgPath := fs.String("config", "tournament/formal_v19.json", "赛事配置")
	_ = fs.Parse(args)

	cfg, root, err := contest.Load(*cfgPath)
	if err != nil {
		return err
	}
	normal := filepath.Join(root, "bots", "bin", "submission"+exeSuffix())
	if _, err := os.Stat(normal); err != nil {
		return fmt.Errorf("找不到基准选手 %s（先跑 arena build）", normal)
	}
	tmp, err := os.MkdirTemp("", "arena-selftest")
	if err != nil {
		return err
	}
	defer os.RemoveAll(tmp)
	faulty := filepath.Join(tmp, "faultbot"+exeSuffix())
	build := exec.Command("go", "build", "-o", faulty, "./cmd/faultbot")
	build.Dir = root
	build.Stderr = os.Stderr
	fmt.Println("自检：编译故障注入 Bot …")
	if err := build.Run(); err != nil {
		return fmt.Errorf("编译 faultbot 失败: %w", err)
	}

	hostScript, err := cfg.EngineHostPath(root)
	if err != nil {
		return err
	}
	base := judge.Config{
		HostScript: hostScript,
		Rules: engine.HostConfig{
			Generator: cfg.Rules.Generator,
			TickLimit: cfg.Rules.TickLimit,
			TickMs:    cfg.Rules.TickMs,
			DecideMs:  cfg.Rules.DecideMs,
			InitMs:    cfg.Rules.InitMs,
			Vision:    cfg.Rules.Vision,
			Ruleset:   cfg.Rules.Ruleset,
		},
		Mirror:     false,
		MaxStrikes: cfg.Rules.MaxStrikes,
		StrikeMode: cfg.Rules.StrikeMode,
		Verify:     false,
	}
	fail := 0
	for _, mode := range []string{"timeout", "crash", "badjson"} {
		jc := base
		jc.Env = []string{"FAULT_MODE=" + mode}
		rec, err := judge.RunMatch(0, 20261109, judge.BotSpec{Name: "Faulty", Path: faulty}, judge.BotSpec{Name: "Normal", Path: normal}, jc)
		if err != nil {
			fmt.Printf("  %-8s ERROR %v\n", mode, err)
			fail++
			continue
		}
		g := rec.Games[0]
		ok := g.Result.Winner == 1 && g.Result.Level == 0 && len(g.Faults[0]) >= cfg.Rules.MaxStrikes
		status := "PASS"
		if !ok {
			status = "FAIL"
			fail++
		}
		fmt.Printf("  %-8s %s  胜者=座位%d 层级=%d 故障=%d 判定=%s\n",
			mode, status, g.Result.Winner, g.Result.Level, len(g.Faults[0]), g.Result.Reason)
	}
	if fail > 0 {
		return fmt.Errorf("自检失败 %d 项", fail)
	}
	fmt.Println("SELFTEST_OK：超时 kill、崩溃重启、非法结构三种路径都按连续 3 次红牌收局。")
	return nil
}

func versionOr(v string) string {
	if v == "" {
		return "（未记录）"
	}
	return v
}

func valueOr(v, fallback string) string {
	if v == "" {
		return fallback
	}
	return v
}
