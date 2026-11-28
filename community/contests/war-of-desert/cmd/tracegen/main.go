// tracegen —— 生成 golden trace（Go 引擎的回归基准）。
//
// 版本：v1.2（Desert Arena · Go 原生引擎回归门禁工具）。
// 功能：用固定剧本跑若干对局，把 {seed, generator, mode, mapAttempt, orderLog,
// frameHashes, result} 冻结到 engine/golden/*.json。此后任何 sim 包改动
// 都必须让 golden_test.go 重放全对（自确定性回归门禁，Plan_Beta §5.2）。
//
//	运行：go run ./cmd/tracegen        （在 war_of_desert/ 下）
package main

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"

	"desertarena/internal/sim"
)

type trace struct {
	Name        string              `json:"name"`
	Mode        string              `json:"mode"`
	Seed        int                 `json:"seed"`
	Generator   string              `json:"generator"`
	TickLimit   int                 `json:"tickLimit"`
	TicksRun    int                 `json:"ticksRun"`
	MapAttempt  int                 `json:"mapAttempt"`
	OrderLog    []sim.OrderLogEntry `json:"orderLog"`
	FrameHashes []string            `json:"frameHashes"`
	Result      json.RawMessage     `json:"result"`
}

func main() {
	root, _ := os.Getwd()
	dir := filepath.Join(root, "engine", "golden")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		panic(err)
	}

	// 剧本：只依赖 tick 的确定性指令序列（重放时 orderLog 就是唯一输入）
	script := func(tick int) []sim.Action {
		var out []sim.Action
		al := sim.Alloc{Armour: 25, Repair: 25, Attack: 25, Range: 25}
		if tick == 2 {
			al = sim.Alloc{Armour: 20, Repair: 20, Attack: 40, Range: 20}
		}
		out = append(out, sim.Action{Type: "alloc", Alloc: &al})
		if tick%7 == 0 {
			out = append(out, sim.Action{Type: "produce", Kind: "LAV"})
		}
		if tick%23 == 3 {
			out = append(out, sim.Action{Type: "produce", Kind: "SALVAGER"})
		}
		if tick%31 == 5 {
			out = append(out, sim.Action{Type: "weapon"})
		}
		if tick == 11 {
			out = append(out, sim.Action{Type: "mode", Mode: "aggressive"})
		}
		return out
	}

	runs := []struct {
		name      string
		mode      string
		seed      int
		generator string
		ticks     int
	}{}
	for _, gen := range []string{"minecraft", "geomorphic"} {
		for _, seed := range []int{20261109, 7} {
			runs = append(runs, struct {
				name      string
				mode      string
				seed      int
				generator string
				ticks     int
			}{"duel-" + gen + "-" + itoa(seed), "duel", seed, gen, 400})
		}
	}
	runs = append(runs,
		struct {
			name      string
			mode      string
			seed      int
			generator string
			ticks     int
		}{"siege-minecraft-20261109", "siege", 20261109, "minecraft", 300},
		struct {
			name      string
			mode      string
			seed      int
			generator string
			ticks     int
		}{"siege-minecraft-7", "siege", 7, "minecraft", 300},
	)

	for _, r := range runs {
		tr := trace{Name: r.name, Mode: r.mode, Seed: r.seed, Generator: r.generator}
		if r.mode == "siege" {
			g, err := sim.NewSiegeGame(r.seed, sim.SiegeCfg{Generator: r.generator})
			if err != nil {
				panic(err)
			}
			tr.TickLimit = g.Cfg.TickLimit
			for t := 0; t < r.ticks && !g.Done(); t++ {
				var acts []sim.Action
				for _, a := range script(t) {
					if a.Type == "alloc" {
						sa := sim.SiegeAlloc{Produce: 30, Attack: 35, Research: 15, Move: 20}
						acts = append(acts, sim.Action{Type: "alloc", SiegeAlloc: &sa})
					} else {
						acts = append(acts, a)
					}
				}
				// 攻方（side 1）把 LAV 派往矿场方向（固定坐标，剧本确定性）
				for _, u := range g.Units {
					if u.Side == 1 && u.Kind == "LAV" {
						x := g.Mines[t%len(g.Mines)].X
						y := g.Mines[t%len(g.Mines)].Y
						acts = append(acts, sim.Action{Type: "move", ID: &u.ID, X: &x, Y: &y})
					}
				}
				g.Step([2][]sim.Action{acts, nil})
			}
			tr.TicksRun = g.Turn
			tr.MapAttempt = g.Map.Attempt
			tr.OrderLog = g.OrderLog
			tr.FrameHashes = g.FrameHashes
			b, _ := json.Marshal(g.Result())
			tr.Result = b
		} else {
			g, err := sim.NewGame(r.seed, sim.GameCfg{Generator: r.generator})
			if err != nil {
				panic(err)
			}
			tr.TickLimit = g.Cfg.TickLimit
			for t := 0; t < r.ticks && !g.Done(); t++ {
				o0 := script(t)
				o1 := script(t)
				// 双方 LAV 各自朝地图中心推进（固定坐标）
				for _, u := range g.Units {
					if u.Kind != "LAV" || u.Dead {
						continue
					}
					x := float64(sim.World / 2)
					y := float64(sim.World / 2)
					o0 = append(o0, sim.Action{Type: "move", ID: &u.ID, X: &x, Y: &y})
				}
				g.Step([2][]sim.Action{o0, o1})
			}
			tr.TicksRun = g.Turn
			tr.MapAttempt = g.Map.Attempt
			tr.OrderLog = g.OrderLog
			tr.FrameHashes = g.FrameHashes
			b, _ := json.Marshal(g.Result())
			tr.Result = b
		}
		blob, _ := json.MarshalIndent(tr, "", " ")
		dest := filepath.Join(dir, tr.Name+".json")
		if err := os.WriteFile(dest, blob, 0o644); err != nil {
			panic(err)
		}
		sum := sha256.Sum256(blob)
		fmt.Printf("GOLDEN %-28s turns=%-4d frames=%-5d sha=%s\n", tr.Name, tr.TicksRun, len(tr.FrameHashes), hex.EncodeToString(sum[:])[:12])
	}

	// MANIFEST：逐文件 sha256
	entries := map[string]string{}
	files, _ := os.ReadDir(dir)
	for _, f := range files {
		if filepath.Ext(f.Name()) != ".json" {
			continue
		}
		blob, err := os.ReadFile(filepath.Join(dir, f.Name()))
		if err != nil {
			panic(err)
		}
		sum := sha256.Sum256(blob)
		entries[f.Name()] = hex.EncodeToString(sum[:])
	}
	man, _ := json.MarshalIndent(map[string]any{
		"note":      "golden trace：Go 引擎自确定性回归基准（Plan_Beta §5.2）。sim 包任何改动必须重放全对；重生成用 go run ./cmd/tracegen",
		"generated": "go run ./cmd/tracegen",
		"files":     entries,
	}, "", " ")
	if err := os.WriteFile(filepath.Join(dir, "MANIFEST.json"), man, 0o644); err != nil {
		panic(err)
	}
	fmt.Println("MANIFEST_WRITTEN " + filepath.Join(dir, "MANIFEST.json"))
}

func itoa(n int) string { return fmt.Sprintf("%d", n) }
