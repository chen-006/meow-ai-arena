// Desert Arena 正式赛 · 选手提交模板。
//
// 版本：v1.2（选手包 Starter）。
// 功能：「能跑通、不犯错」的基线——占点、造兵、研究、开火各做一点；
// 生产带 RU 自查与跳过轮转，harvest 按节点类型分流。
//
// 你只需要改这个文件（以及你愿意加的自己的包），把它编译成独立程序交上来：
//
//	go build -o bots/bin/submission.exe ./workspace/submission
//
// 程序必须遵守 hello → ready → tick/actions → end 的行协议：
//
//	· 读完 hello 后回一行 {"ready":true}
//	· 每收到一行 tick，回一行 {"actions":[...]}
//	· 收到 {"type":"end",...} 后自行退出
//
// 下面这份实现不追求强度，追求你读得懂、改得动。
// botkit 里的注释写清了每个字段的含义。
package main

import (
	"fmt"
	"math"
	"os"

	"desertarena/workspace/botkit"
)

// buildOrder 是生产循环；按顺序尝试，钱不够就跳到下一个。
var buildOrder = []string{"LAV", "SALVAGER", "LAV", "AAV"}

// techOrder 是研究优先级；前置不满足或钱不够会自动跳过。
// v1.1：生产线由工业科技驱动（I1→2 条、I3a/I3b→3 条），所以 I 系要早研。
var techOrder = []string{"I1", "A1", "F1", "I3a", "A2", "F2"}

// alloc 是母舰四子系统分配（v1.1：armour 减伤 / repair 自愈 / attack 增伤 /
// range 增程），和必须为 100。生产/研究不再吃分配——生产线看科技，研究槽固定 2。
var alloc = map[string]int{"armour": 25, "repair": 25, "attack": 30, "range": 20}

var buildIdx int

// 采集车指派过就不再重发（重发会重置运输线状态机）
var harvested = map[int]bool{}

// 已指派采集车数：第 1 台 cu、第 2 台（需要 RU 时）ru
var harvestN int

func main() {
	if err := botkit.Run(decide); err != nil {
		fmt.Fprintln(os.Stderr, "bot 退出：", err)
		os.Exit(1)
	}
}

func decide(b *botkit.Bot, st *botkit.State) []botkit.Action {
	side := st.Side
	acts := make([]botkit.Action, 0, len(st.Units)+4)

	/* ---------- 阵营动作 ---------- */
	if !sameAlloc(st.Me.AllocTarget, alloc) {
		acts = append(acts, botkit.Action{Type: "alloc", Alloc: alloc})
	}
	if st.Me.Mode != "neutral" {
		/* neutral：途中遇敌会自己停步开火，被高差压制会后退 —— 对新手最友好 */
		acts = append(acts, botkit.Action{Type: "mode", Mode: "neutral"})
	}
	if kind, ok := nextBuild(b, st); ok {
		acts = append(acts, botkit.Action{Type: "produce", Kind: kind})
		buildIdx++
	}
	if tech := nextTech(b, st); tech != "" {
		acts = append(acts, botkit.Action{Type: "research", Tech: tech})
	}

	/* ---------- 单位动作 ---------- */
	enemies := botkit.Enemies(st, side)
	for _, u := range botkit.Own(st, side) {
		t := b.Rules.Types[u.Kind]

		switch u.Kind {
		case "MOTHERSHIP":
			/* 母舰守家附近的点；有敌人进射程就开火 */
			if e := botkit.Nearest(enemies, u.X, u.Y); e != nil && dist(u, *e) <= t.Range*0.95*float64(b.Hello.Cell) {
				acts = append(acts, botkit.Action{ID: u.ID, Type: "attack", TargetID: e.ID})
				continue
			}
			if v := b.HomeVP(st, side); v != nil {
				acts = append(acts, moveAlong(b, u, v.X, v.Y))
				continue
			}
			acts = append(acts, botkit.Action{ID: u.ID, Type: "idle"})

		case "SALVAGER":
			/* 采集车（v1.1 运输线）：指派一次资源点，引擎托管「装载→回程→卸货」；
			 * 每 tick 重发 harvest 会把状态机打回 toResource（语义见引擎）。
			 * 第 1 台采 cu（基础收入 0，CU 全靠采集），生产表里有吃 RU 的单位时
			 * 第 2 台采 ru 解锁高阶单位。 */
			if !harvested[u.ID] {
				kind := "cu"
				if harvestN == 1 && needsRU(b) {
					kind = "ru"
				}
				if r := botkit.NearestResKind(st, u.X, u.Y, kind); r != nil {
					rid := r.ID
					acts = append(acts, botkit.Action{ID: u.ID, Type: "harvest", ResID: &rid})
					harvested[u.ID] = true
					harvestN++
				}
			}

		case "SCOUT":
			if v := botkit.NearestVP(st, u.X, u.Y, side, true); v != nil {
				acts = append(acts, moveAlong(b, u, v.X, v.Y))
			}

		default:
			/* 战斗单位：射程内打最近的敌人，否则压上去；没有敌人就去占点 */
			if e := botkit.Nearest(enemies, u.X, u.Y); e != nil {
				d := dist(u, *e)
				if d <= t.Range*0.95*float64(b.Hello.Cell) {
					acts = append(acts, botkit.Action{ID: u.ID, Type: "attack", TargetID: e.ID})
					continue
				}
				if d <= t.Range*3*float64(b.Hello.Cell) {
					x, y := b.StepToward(u, e.X, e.Y, b.Stride(u))
					acts = append(acts, botkit.Action{ID: u.ID, Type: "move", X: x, Y: y})
					continue
				}
			}
			if v := botkit.NearestVP(st, u.X, u.Y, side, true); v != nil {
				acts = append(acts, moveAlong(b, u, v.X, v.Y))
			}
		}
	}
	return acts
}

/* ------------------------------------------------------------------ */
/* 下面是几个小工具，按需改。                                          */
/* ------------------------------------------------------------------ */

func dist(a, b botkit.Unit) float64 {
	return math.Hypot(a.X-b.X, a.Y-b.Y)
}

func sameAlloc(have, want map[string]int) bool {
	for k, v := range want {
		if have[k] != v {
			return false
		}
	}
	return true
}

// needsRU 报告生产表里有没有吃 RU 的高阶单位（决定要不要派采集车去 ru 节点）。
func needsRU(b *botkit.Bot) bool {
	for _, kind := range buildOrder {
		if t, ok := b.Rules.Types[kind]; ok && t.RU > 0 {
			return true
		}
	}
	return false
}

func nextBuild(b *botkit.Bot, st *botkit.State) (string, bool) {
	for i := 0; i < len(buildOrder); i++ {
		kind := buildOrder[(buildIdx+i)%len(buildOrder)]
		t, ok := b.Rules.Types[kind]
		if !ok {
			continue
		}
		if st.Me.Pop+t.Pop > st.Me.PopMax || len(st.Me.Queue)+len(st.Me.Active) >= 4 {
			return "", false
		}
		// v1.1：RU 不够绝不下单——引擎队列付不起只会整队冻结（不丢单也不可取消）。
		if float64(t.RU) > st.Me.RU {
			continue
		}
		if st.Me.CU >= t.Cost {
			return kind, true
		}
	}
	return "", false
}

func nextTech(b *botkit.Bot, st *botkit.State) string {
	// 真正研究完成（前置只认它）
	done := map[string]bool{}
	doing := map[string]bool{}
	for _, id := range st.Me.TechDone {
		done[id] = true
	}
	for _, d := range st.Me.TechDoing {
		doing[d.ID] = true
	}
	// 槽满（固定 2）本拍就不发——发了也会被引擎拒，白空转。
	if len(doing) >= st.Me.Slots.Research {
		return ""
	}
	for _, id := range techOrder {
		if done[id] || doing[id] {
			continue
		}
		t, ok := b.Rules.Techs[id]
		if !ok || st.Me.CU < t.CU {
			continue
		}
		// 前置只认已完成：在研不算，否则会被 missing prereq 拒到天荒地老。
		okReq := true
		for _, r := range t.Req {
			if !done[r] {
				okReq = false
				break
			}
		}
		if okReq {
			return id
		}
	}
	return ""
}

// moveAlong 用流场走到固定目标（VP / 资源点）；到达或不可达时待命。
func moveAlong(b *botkit.Bot, u botkit.Unit, x, y float64) botkit.Action {
	if nx, ny, ok := b.StepAlong(u, b.FieldTo(x, y), b.Stride(u)); ok {
		return botkit.Action{ID: u.ID, Type: "move", X: nx, Y: ny}
	}
	return botkit.Action{ID: u.ID, Type: "idle"}
}
