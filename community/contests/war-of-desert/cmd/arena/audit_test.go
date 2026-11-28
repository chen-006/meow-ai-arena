package main

import (
	"bufio"
	"encoding/json"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"

	"desertarena/internal/contest"
	"desertarena/internal/sandbox"
	"desertarena/internal/schedule"
	"desertarena/internal/sim"
)

const stubBotSrc = `package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
)

func main() {
	in := bufio.NewReaderSize(os.Stdin, 1<<20)
	if _, err := in.ReadString('\n'); err != nil {
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
		fmt.Println(` + "`" + `{"actions":[]}` + "`" + `)
	}
}
`

func setupMiniSeason(t *testing.T, players []string, tickLimit int) (root, cfgPath, outDir string) {
	t.Helper()
	root = t.TempDir()
	for _, dir := range []string{
		"tournament",
		"bots/bin",
		"workspace/botkit",
		"workspace/refbot",
		"workspace/acebot",
	} {
		if err := os.MkdirAll(filepath.Join(root, filepath.FromSlash(dir)), 0o755); err != nil {
			t.Fatal(err)
		}
	}

	// 放一份可运行的真实 enginehost 到 bots/bin/enginehost.exe
	hostBin := filepath.Join(root, "bots", "bin", "enginehost"+exeSuffix())
	cmd := exec.Command("go", "build", "-o", hostBin, "../enginehost")
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("编译 enginehost 失败: %v\n%s", err, out)
	}

	// 编一份真实预编译 Bot 并复制给所有选手与一个未封存的 rogue.exe
	botSrcDir := filepath.Join(t.TempDir(), "stubbot")
	if err := os.MkdirAll(botSrcDir, 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(botSrcDir, "go.mod"), []byte("module stubbot\n\ngo 1.22\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(botSrcDir, "main.go"), []byte(stubBotSrc), 0o644); err != nil {
		t.Fatal(err)
	}
	baseExe := filepath.Join(botSrcDir, "stub"+exeSuffix())
	cmd = exec.Command("go", "build", "-o", baseExe, ".")
	cmd.Dir = botSrcDir
	if out, err := cmd.CombinedOutput(); err != nil {
		t.Fatalf("编译测试 Bot 失败: %v\n%s", err, out)
	}
	botBytes, err := os.ReadFile(baseExe)
	if err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(filepath.Join(root, "bots", "bin", "rogue"+exeSuffix()), botBytes, 0o755); err != nil {
		t.Fatal(err)
	}

	entries := make([]schedule.Entry, 0, len(players))
	for _, name := range players {
		rel := "bots/bin/" + strings.ToLower(name) + exeSuffix()
		if err := os.WriteFile(filepath.Join(root, filepath.FromSlash(rel)), botBytes, 0o755); err != nil {
			t.Fatal(err)
		}
		entries = append(entries, schedule.Entry{
			Name:     name,
			Bin:      rel,
			Prebuilt: true,
		})
	}

	cfgObj := map[string]any{
		"name":    "测试赛季",
		"season":  "test",
		"version": "1.4",
		"engine": map[string]any{
			"kind": "native",
			"host": "bots/bin/enginehost" + exeSuffix(),
		},
		"rules": map[string]any{
			"tickLimit":  tickLimit,
			"tickMs":     50,
			"decideMs":   120,
			"initMs":     2000,
			"maxStrikes": 3,
			"strikeMode": "consecutive",
			"generator":  "minecraft",
			"vision":     false,
			"ruleset":    "v14",
		},
		"schedule": map[string]any{
			"seeds":     []int{20261109},
			"mirror":    true,
			"bootstrap": 20,
		},
		"entries": entries,
		"output":  "results/test",
		"verify":  true,
	}
	raw, err := json.MarshalIndent(cfgObj, "", "  ")
	if err != nil {
		t.Fatal(err)
	}
	cfgPath = filepath.Join(root, "tournament", "test.json")
	if err := os.WriteFile(cfgPath, raw, 0o644); err != nil {
		t.Fatal(err)
	}
	return root, cfgPath, filepath.Join(root, "results", "test")
}

func TestCmdRunRejectsTamperedPlanBinAndRules(t *testing.T) {
	_, cfgPath, outDir := setupMiniSeason(t, []string{"Alpha", "Beta"}, 20)

	if err := cmdBuild([]string{"-config", cfgPath}); err != nil {
		t.Fatalf("cmdBuild 失败: %v", err)
	}
	if err := cmdPlan([]string{"-config", cfgPath}); err != nil {
		t.Fatalf("cmdPlan 失败: %v", err)
	}

	planPath := filepath.Join(outDir, "赛程.json")
	origPlan, err := os.ReadFile(planPath)
	if err != nil {
		t.Fatal(err)
	}

	// 场景 1：封存记录未动，但篡改 赛程.json 里 Alpha 的可执行文件路径指向 rogue.exe
	var p schedule.Plan
	if err := json.Unmarshal(origPlan, &p); err != nil {
		t.Fatal(err)
	}
	p.Entries[0].Bin = "bots/bin/rogue" + exeSuffix()
	if err := schedule.Save(planPath, p); err != nil {
		t.Fatal(err)
	}
	if err := cmdRun([]string{"-config", cfgPath, "-limit", "1"}); err == nil {
		t.Fatal("赛程 bin 与封存记录不一致时，cmdRun 必须拒绝开赛")
	} else if !strings.Contains(err.Error(), "不一致") {
		t.Fatalf("报错应指出 bin 不一致，实得: %v", err)
	}

	// 场景 2：还原赛程，但篡改赛程冻结的 decideMs（与当前配置不一致）
	if err := json.Unmarshal(origPlan, &p); err != nil {
		t.Fatal(err)
	}
	p.Config.DecideMs = 999
	if err := schedule.Save(planPath, p); err != nil {
		t.Fatal(err)
	}
	if err := cmdRun([]string{"-config", cfgPath, "-limit", "1"}); err == nil {
		t.Fatal("赛程冻结时限与配置不一致时，cmdRun 必须拒绝开赛")
	}

	// 场景 3：还原合法赛程，真实开赛应顺利完成
	if err := os.WriteFile(planPath, origPlan, 0o644); err != nil {
		t.Fatal(err)
	}
	if !sandbox.Available() {
		t.Skip("当前平台无内置沙箱，跳过真实 run 验证")
	}
	if err := cmdRun([]string{"-config", cfgPath}); err != nil {
		t.Fatalf("合法封存与赛程应正常完赛，实得错误: %v", err)
	}
	if err := cmdRate([]string{"-config", cfgPath}); err != nil {
		t.Fatalf("完赛后正式评分应通过，实得错误: %v", err)
	}
	if _, err := os.Stat(filepath.Join(outDir, "成绩.md")); err != nil {
		t.Fatalf("完赛后应生成 成绩.md: %v", err)
	}
}

func TestCmdRateCompletenessAndPartialGate(t *testing.T) {
	if !sandbox.Available() {
		t.Skip("当前平台无内置沙箱，跳过实跑验证")
	}
	_, cfgPath, outDir := setupMiniSeason(t, []string{"Alpha", "Beta", "Gamma"}, 20)

	if err := cmdBuild([]string{"-config", cfgPath}); err != nil {
		t.Fatalf("cmdBuild 失败: %v", err)
	}
	if err := cmdPlan([]string{"-config", cfgPath}); err != nil {
		t.Fatalf("cmdPlan 失败: %v", err)
	}

	// 3 人 × 1 seed = 3 场 6 局；只跑 1 场（2 局）
	if err := cmdRun([]string{"-config", cfgPath, "-limit", "1"}); err != nil {
		t.Fatalf("cmdRun -limit 1 失败: %v", err)
	}

	// 默认正式评分必须拒绝残缺结果，且不得生成 成绩.md
	if err := cmdRate([]string{"-config", cfgPath}); err == nil {
		t.Fatal("仅打 2/6 局时，cmdRate 必须拒绝生成正式赛成绩")
	} else if !strings.Contains(err.Error(), "不完整") {
		t.Fatalf("报错应说明结果不完整，实得: %v", err)
	}
	if _, err := os.Stat(filepath.Join(outDir, "成绩.md")); err == nil {
		t.Fatal("缺局拒绝评分后不应残留 成绩.md")
	}

	// 显式传 -partial 允许查看赛中进度，但必须输出到 临时成绩.md，不覆盖正式榜
	if err := cmdRate([]string{"-config", cfgPath, "-partial"}); err != nil {
		t.Fatalf("cmdRate -partial 应允许生成临时榜，实得错误: %v", err)
	}
	tempMD, err := os.ReadFile(filepath.Join(outDir, "临时成绩.md"))
	if err != nil {
		t.Fatalf("应生成 临时成绩.md: %v", err)
	}
	if !strings.Contains(string(tempMD), "临时成绩") {
		t.Fatalf("临时成绩.md 标题应标明临时成绩，实得:\n%s", tempMD)
	}
	if _, err := os.Stat(filepath.Join(outDir, "成绩.md")); err == nil {
		t.Fatal("-partial 不得生成正式 成绩.md")
	}

	// 注入重复局：即使加 -partial 也必须拒绝
	resPath := filepath.Join(outDir, "结果.jsonl")
	raw2, err := os.ReadFile(resPath)
	if err != nil {
		t.Fatal(err)
	}
	firstLine := strings.Split(strings.TrimSpace(string(raw2)), "\n")[0]
	if err := os.WriteFile(resPath, append(raw2, []byte(firstLine+"\n")...), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := cmdRate([]string{"-config", cfgPath, "-partial"}); err == nil {
		t.Fatal("存在重复局时，即使 -partial 也必须拒绝评分")
	} else if !strings.Contains(err.Error(), "重复") {
		t.Fatalf("报错应指出重复出现，实得: %v", err)
	}

	// 恢复两局，用 -resume 补打完剩余 2 场（总计 6/6 局），此时正式评分应通过
	if err := os.WriteFile(resPath, raw2, 0o644); err != nil {
		t.Fatal(err)
	}
	if err := cmdRun([]string{"-config", cfgPath, "-resume"}); err != nil {
		t.Fatalf("cmdRun -resume 失败: %v", err)
	}
	if err := cmdRate([]string{"-config", cfgPath}); err != nil {
		t.Fatalf("补完 6/6 局后正式评分应通过，实得错误: %v", err)
	}
	formalMD, err := os.ReadFile(filepath.Join(outDir, "成绩.md"))
	if err != nil {
		t.Fatalf("完赛后应生成 成绩.md: %v", err)
	}
	if !strings.Contains(string(formalMD), "正式赛成绩") {
		t.Fatalf("正式 成绩.md 标题异常:\n%s", formalMD)
	}
}

func TestValidateResultsAcceptsArchivedFormalV14(t *testing.T) {
	// 回归兼容性检查：真实归档的第 5 季 90 局结果必须原样通过新校验
	if _, err := os.Stat(filepath.Join("..", "..", "results", "formal-v14", "赛程.json")); err != nil {
		t.Skip("缺少归档 formal-v14 目录")
	}
	cfg, root, err := contest.Load(filepath.Join("..", "..", "tournament", "formal_v14.json"))
	if err != nil {
		t.Fatal(err)
	}
	outDir := cfg.OutDir(root)
	plan, err := schedule.Load(filepath.Join(outDir, "赛程.json"))
	if err != nil {
		t.Fatal(err)
	}
	f, err := os.Open(filepath.Join(outDir, "结果.jsonl"))
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	var lines []gameLine
	sc := bufio.NewScanner(f)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	for sc.Scan() {
		var l gameLine
		if err := json.Unmarshal(sc.Bytes(), &l); err != nil {
			t.Fatal(err)
		}
		lines = append(lines, l)
	}
	if err := sc.Err(); err != nil {
		t.Fatal(err)
	}
	wantHash := sim.RulesHashFor(cfg.Rules.Ruleset, cfg.Rules.TickLimit, sim.NormalizeVision(sim.Vision{
		Enabled: cfg.Rules.Vision, Fog: cfg.Rules.Vision, LOS: cfg.Rules.Vision,
	}))
	if plan.RulesHash != wantHash {
		t.Fatalf("formal-v14 rulesHash 漂移: plan=%s want=%s", plan.RulesHash, wantHash)
	}
	total, err := validateResultsAgainstPlan(plan, lines, false)
	if err != nil {
		t.Fatalf("归档 formal-v14 应通过完整性校验: %v", err)
	}
	if total != 90 || len(lines) != 90 {
		t.Fatalf("formal-v14 期望 90 局，实得 expected=%d actual=%d", total, len(lines))
	}
}
