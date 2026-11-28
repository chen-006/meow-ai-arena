package sim

import (
	"math"
	"strings"
	"testing"
)

// 规则引擎测试：移植 rules_test.js 的绝对锚点（§9）、重放确定性（§8）
// 与核心行为断言。字面量期望值与 JS 测试逐条对应，防「常数悄悄漂移」。

func newTestGame(t *testing.T, seed int) *Game {
	t.Helper()
	g, err := NewGame(seed, GameCfg{Generator: "minecraft"})
	if err != nil {
		t.Fatalf("NewGame: %v", err)
	}
	return g
}

func TestAbsoluteAnchors(t *testing.T) {
	/* §9：字面量锚点 —— 任何常数漂移在这里炸 */
	if VPRadius != 4.2*Cell*Scale {
		t.Fatalf("VP 半径 %v 应为 4.2 格 × CELL", VPRadius)
	}
	if PopMax != 24 {
		t.Fatalf("POP_MAX %d", PopMax)
	}
	if BaseIncome != 0 {
		t.Fatalf("v1.1 取消被动收入，BASE_INCOME=%v", BaseIncome)
	}
	// LAV→RAILGUN：8 × 1.8 = 14.4（零距离基准，light 装甲 ×1）
	d1 := DamageOf("AP", Types["LAV"].Dmg, Types["LAV"].V0, Types["LAV"].V0, Types["RAILGUN"].Armor) * RPS["LAV"]["RAILGUN"]
	if math.Abs(d1-14.4) > 1e-9 {
		t.Fatalf("LAV→RAILGUN 伤害 %v 应为 14.4", d1)
	}
	// LAV→AAV：8 × 0.6 × heavy 1.15 = 5.52
	d2 := DamageOf("AP", Types["LAV"].Dmg, Types["LAV"].V0, Types["LAV"].V0, Types["AAV"].Armor) * RPS["LAV"]["AAV"]
	if math.Abs(d2-5.52) > 1e-9 {
		t.Fatalf("LAV→AAV 伤害 %v 应为 5.52", d2)
	}
	// 无武器兵种克制为 0（显式 0 不被兜底成 1）
	if RPS["SCOUT"]["LAV"] != 0 || RPS["SALVAGER"]["AAV"] != 0 {
		t.Fatal("SCOUT/SALVAGER 克制行必须显式全 0")
	}
	// 修理速率 2/tick、命中半径 7.5m、行上限 128KB
	if RepairRate != 2 || HitRM != 7.5 || MaxLine != 131072 {
		t.Fatal("RepairRate/HitRM/MaxLine 锚点漂移")
	}
	if TickLimit != 1200 || TickMS != 50 || DecideMS != 120 || OrderLatency != 1 {
		t.Fatal("时序锚点漂移")
	}
	// 七领域权重和为 1
	sum := 0.0
	for _, w := range Weights {
		sum += w
	}
	if math.Abs(sum-1) > 1e-9 || len(Weights) != 7 {
		t.Fatalf("七领域权重 %v 和=%v", Weights, sum)
	}
}

func TestGameConstructorInvariants(t *testing.T) {
	g := newTestGame(t, 20261109)
	// 初始兵力：母舰 + 2 LAV
	if len(g.Units) != 6 {
		t.Fatalf("初始单位 %d 应为 6", len(g.Units))
	}
	for s := 0; s < 2; s++ {
		ms := 0
		lav := 0
		for _, u := range g.Units {
			if u.Side == s {
				if u.Kind == "MOTHERSHIP" {
					ms++
				}
				if u.Kind == "LAV" {
					lav++
				}
			}
		}
		if ms != 1 || lav != 2 {
			t.Fatalf("side %d 初始兵力 ms=%d lav=%d", s, ms, lav)
		}
		if g.Sides[s].Pop != 4 || g.Sides[s].InitialCost != 200 {
			t.Fatalf("side %d pop=%d initialCost=%d 应为 4/200", s, g.Sides[s].Pop, g.Sides[s].InitialCost)
		}
		if g.Sides[s].CU != 500 || g.Sides[s].RU != 0 {
			t.Fatalf("side %d 初始经济 %v/%v 应为 500/0", s, g.Sides[s].CU, g.Sides[s].RU)
		}
	}
	// 帧哈希链从第 0 帧开始
	if len(g.FrameHashes) != 1 {
		t.Fatalf("构造后应有 1 个帧哈希，got %d", len(g.FrameHashes))
	}
	// hello 协议行：256² 下约 110KB，必须 ≤ 128KB
	hello := g.EncodeHello()
	if len(hello) > MaxLine {
		t.Fatalf("hello %d 字节超过行上限 %d", len(hello), MaxLine)
	}
	if len(hello) < 90000 {
		t.Fatalf("hello %d 字节异常偏小（地图层丢了？）", len(hello))
	}
	if !strings.Contains(hello, `"rle+b64/1"`) || !strings.Contains(hello, `"protoVersion":5`) {
		t.Fatal("hello 缺少地图编码或协议版本字段")
	}
	// tick 行合法
	tick := g.EncodeTick(0)
	if !strings.Contains(tick, `"type":"tick"`) || !strings.Contains(tick, `"applyAt":1`) {
		t.Fatal("tick 缺少 type/applyAt 字段")
	}
}

func TestAllocStepping(t *testing.T) {
	g := newTestGame(t, 7)
	g.Sides[0].Target = Alloc{Armour: 0, Repair: 0, Attack: 100, Range: 0}
	for i := 0; i < 25; i++ {
		g.Step([2][]Action{nil, nil})
	}
	a := g.Sides[0].Alloc
	sum := a.Armour + a.Repair + a.Attack + a.Range
	if sum != 100 {
		t.Fatalf("alloc 过渡后总和 %d 应恒为 100", sum)
	}
	if a.Attack != 100 {
		t.Fatalf("alloc 20 tick 内未收敛到目标: %+v", a)
	}
}

func TestProductionAndTech(t *testing.T) {
	g := newTestGame(t, 7)
	S := g.Sides[0]
	// 生产线：初始 1 条
	if g.productionLines(0) != 1 {
		t.Fatal("初始生产线应为 1")
	}
	// 入队 LAV → 开工扣钱
	S.TechDone["I1"] = true
	if g.productionLines(0) != 2 {
		t.Fatal("I1 后生产线应为 2")
	}
	S.TechDone["I3a"] = true
	if g.productionLines(0) != 3 {
		t.Fatal("I3a 后生产线应为 3")
	}
	// 科技校验：前置 / 互斥 / 槽位 / CU
	if chk := g.techAvailable(0, "F2"); chk.ok {
		t.Fatal("F2 缺前置 F1 应拒绝")
	}
	S.TechDone["F1"] = true
	if chk := g.techAvailable(0, "F2"); !chk.ok {
		t.Fatalf("F2 应可用: %s", chk.reason)
	}
	S.TechDone["M2a"] = true
	if chk := g.techAvailable(0, "M2b"); chk.ok {
		t.Fatal("M2a 已研，M2b 应被互斥拒绝")
	}
	if chk := g.techAvailable(0, "M4"); chk.ok {
		t.Fatal("M4 需 M3a/M3b 之一")
	}
	S.TechDone["M3a"] = true
	if chk := g.techAvailable(0, "M4"); !chk.ok {
		t.Fatalf("M4 reqAny 应满足: %s", chk.reason)
	}
}

func TestSalvagerEconomy(t *testing.T) {
	g := newTestGame(t, 7)
	// 50 tick 无指令：CU 恒 500（无被动收入）
	for i := 0; i < 50; i++ {
		g.Step([2][]Action{nil, nil})
	}
	if g.Sides[0].CU != 500 || g.Sides[1].CU != 500 {
		t.Fatalf("无被动收入：CU %v/%v 应恒 500", g.Sides[0].CU, g.Sides[1].CU)
	}
	// 资源点 4 个 = 2 对镜像格；RU 按对称分配为 2 个（两侧各一）
	if len(g.Resources) != 4 {
		t.Fatalf("资源点 %d 应为 4", len(g.Resources))
	}
	ruCount := 0
	for _, r := range g.Resources {
		if r.Kind == "ru" {
			ruCount++
		}
	}
	if ruCount != 2 {
		t.Fatalf("RU 节点 %d 应为 2（镜像成对，两侧各一）", ruCount)
	}
	// 初始兵力没有采集车（与 JS 一致）——测试里直接造一台放在资源点旁，取 CU 节点
	var node *Resource
	for _, r := range g.Resources {
		if r.Kind == "cu" {
			node = r
			break
		}
	}
	if node == nil {
		t.Fatal("没有 CU 资源点")
	}
	sv := g.spawn(0, "SALVAGER", node.X+40, node.Y)
	// 指派 harvest **只发一次**：每 tick 重发会在指令绑定阶段把 hstate 重置回
	// toResource（JS 同款语义），状态机永远到不了 loading。之后引擎托管运输线。
	id := node.ID
	g.Step([2][]Action{{{Type: "harvest", ID: &sv.ID, ResID: &id}}, nil})
	cu0 := g.Sides[0].CU
	for i := 0; i < 400 && g.Sides[0].CU == cu0; i++ {
		g.Step([2][]Action{nil, nil})
	}
	if g.Sides[0].CU == cu0 {
		t.Fatalf("400 tick 内采集车没有完成一次卸货（transport line 断裂）hstate=%s carry=%v", sv.HState, sv.CarryCU)
	}
}

func TestReplayDeterminism40(t *testing.T) {
	/* §8：40 tick 剧本对局 → orderLog 重放逐帧一致；删一条指令必须探测到漂移 */
	g := newTestGame(t, 20261109)
	for i := 0; i < 40; i++ {
		lavA, lavB := initialLavIDs(g)
		o0 := []Action{
			{Type: "alloc", Alloc: &Alloc{25, 25, 25, 25}},
		}
		if i%9 == 0 {
			o0 = append(o0, Action{Type: "produce", Kind: "LAV"})
		}
		if i == 2 {
			// 替换（不是追加）：两条 alloc 同 tick 会互相覆盖，篡改测试就探不到漂移
			o0[0] = Action{Type: "alloc", Alloc: &Alloc{20, 20, 40, 20}}
		}
		if lavA != 0 {
			tx, ty := g.Vps[0].X, g.Vps[0].Y
			o0 = append(o0, Action{Type: "move", ID: &lavA, X: &tx, Y: &ty})
		}
		if lavB != 0 {
			tx, ty := g.Vps[1].X, g.Vps[1].Y
			o0 = append(o0, Action{Type: "move", ID: &lavB, X: &tx, Y: &ty})
		}
		g.Step([2][]Action{o0, o0})
	}
	rec := &VerifyRec{
		Seed:        g.Seed,
		TickLimit:   g.Cfg.TickLimit,
		Generator:   g.Cfg.Generator,
		MapAttempt:  &g.Map.Attempt,
		OrderLog:    g.OrderLog,
		FrameHashes: g.FrameHashes,
	}
	rep := VerifyDeterminism(rec)
	if !rep.OK {
		t.Fatalf("重放确定性失败: %+v", rep)
	}
	/* 篡改：删掉第 3 条 orderLog 的 alloc 指令 → 帧的 alloc 字段必须漂移 */
	tampered := &VerifyRec{Seed: rec.Seed, TickLimit: rec.TickLimit, Generator: rec.Generator,
		MapAttempt: rec.MapAttempt, FrameHashes: rec.FrameHashes}
	for k, e := range rec.OrderLog {
		ee := e
		if k == 2 && len(ee.O[0]) > 0 && ee.O[0][0].Type == "alloc" {
			ee.O[0] = ee.O[0][1:]
		}
		tampered.OrderLog = append(tampered.OrderLog, ee)
	}
	rep2 := VerifyDeterminism(tampered)
	if rep2.OK || rep2.Mismatch == nil {
		t.Fatalf("篡改 orderLog 未被探测: %+v", rep2)
	}
}

func TestFullGameDeterminism(t *testing.T) {
	/* 1200 tick 完整剧本对局：跑完 → 判定链出结果 → 重放复核 */
	g := newTestGame(t, 20261109)
	for tick := 0; tick < TickLimit && !g.Done(); tick++ {
		o0 := scriptedOrders(g, 0, tick)
		o1 := scriptedOrders(g, 1, tick)
		g.Step([2][]Action{o0, o1})
	}
	r := g.Result()
	if r == nil {
		t.Fatal("1200 tick 后无终局记录")
	}
	if r.Level == 0 && r.Turns < TickLimit {
		t.Fatalf("未到时限不应红牌判负: %+v", r)
	}
	if r.Turns != TickLimit && r.Level == 1 {
		// 母舰被摧毁：剧本对局里允许（互冲），但要 ≤ TickLimit
		if r.Turns > TickLimit {
			t.Fatalf("turns %d 超限", r.Turns)
		}
	}
	rep := VerifyDeterminism(&VerifyRec{
		Seed: g.Seed, TickLimit: g.Cfg.TickLimit, Generator: g.Cfg.Generator,
		MapAttempt: &g.Map.Attempt, OrderLog: g.OrderLog, FrameHashes: g.FrameHashes, Result: r,
	})
	if !rep.OK {
		t.Fatalf("完整对局重放复核失败: mismatch=%+v resultMismatch=%+v", rep.Mismatch, rep.ResultMismatch)
	}
	if rep.Result == nil || rep.Result.Winner != r.Winner || rep.Result.Level != r.Level {
		t.Fatalf("重放终局不一致: %+v vs %+v", rep.Result, r)
	}
}

func TestDecodeStructuralVsSemantic(t *testing.T) {
	g := newTestGame(t, 7)
	lavID := 0
	for _, u := range g.Units {
		if u.Side == 0 && u.Kind == "LAV" {
			lavID = u.ID
			break
		}
	}
	// 结构性错误 → 抛错
	if _, err := g.Decode(0, "not json"); err == nil {
		t.Fatal("bad json 应抛错")
	}
	if _, err := g.Decode(0, `{"actions":{}}`); err == nil {
		t.Fatal("actions 非数组应抛错")
	}
	if _, err := g.Decode(0, `{"actions":[{"type":"move","id":9999,"x":10,"y":10}]}`); err == nil {
		t.Fatal("越权 id 应抛错")
	}
	if _, err := g.Decode(0, `{"actions":[{"type":"move","id":`+itoa(lavID)+`,"x":10,"y":10},{"type":"idle","id":`+itoa(lavID)+`}]}`); err == nil {
		t.Fatal("重复单位动作应抛错")
	}
	if _, err := g.Decode(0, `{"actions":[{"type":"walk","id":`+itoa(lavID)+`}]}`); err == nil {
		t.Fatal("未知动作类型应抛错")
	}
	if _, err := g.Decode(0, `{"actions":[{"type":"move","id":`+itoa(lavID)+`,"targetId":null}]}`); err == nil {
		t.Fatal("targetId:null 应抛错（JS null!==undefined）")
	}
	// 语义非法 → 丢弃 + 记账，不抛错（每条单独发，避免同 tick 重复阵营动作抛错）
	_, err := g.Decode(0, `{"actions":[{"type":"alloc","alloc":{"armour":10,"repair":10,"attack":10}}]}`)
	if err != nil {
		t.Fatalf("alloc 键数不足不应抛错: %v", err)
	}
	acts, err := g.Decode(0, `{"actions":[{"type":"produce","kind":"MOTHERSHIP"}]}`)
	if err != nil || len(acts) != 0 {
		t.Fatalf("不可生产单位应丢弃: err=%v n=%d", err, len(acts))
	}
	acts, err = g.Decode(0, `{"actions":[{"type":"produce","kind":"LAV","by":`+itoa(lavID)+`}]}`)
	if err != nil || len(acts) != 0 {
		t.Fatalf("by 指向非运载舰应丢弃: err=%v n=%d", err, len(acts))
	}
	if len(g.Rejected) < 3 {
		t.Fatalf("rejected 应有记录，got %d", len(g.Rejected))
	}
	// 合法混合
	acts, err = g.Decode(0, `{"actions":[
		{"type":"alloc","alloc":{"armour":25,"repair":25,"attack":25,"range":25}},
		{"type":"produce","kind":"LAV"},
		{"type":"move","id":`+itoa(lavID)+`,"x":100,"y":100}
	]}`)
	if err != nil || len(acts) != 3 {
		t.Fatalf("合法混合应 3 条: err=%v n=%d", err, len(acts))
	}
}

func TestTickInfoFiltration(t *testing.T) {
	/* 视野关闭：tick 发全量单位；开启：敌方单位按 canSee 过滤且模拟不变 */
	g := newTestGame(t, 9)
	tick := g.EncodeTick(0)
	if strings.Count(tick, `"kind":"MOTHERSHIP"`) != 2 {
		t.Fatal("视野关闭时 tick 应含双方母舰")
	}
	g2 := newTestGame(t, 9)
	g2.Vision = VisionAll
	g2.Cfg.Vision = VisionAll
	t2 := g2.EncodeTick(0)
	if strings.Count(t2, `"kind":"MOTHERSHIP"`) != 1 {
		t.Fatal("开雾时本方 tick 只应有己方母舰")
	}
}

/* ---- 测试辅助 ---- */

func initialLavIDs(g *Game) (int, int) {
	a, b := 0, 0
	for _, u := range g.Units {
		if u.Side == 0 && u.Kind == "LAV" && a == 0 {
			a = u.ID
		}
		if u.Side == 1 && u.Kind == "LAV" && b == 0 {
			b = u.ID
		}
	}
	return a, b
}

// scriptedOrders 剧本 bot：只依赖 tick 与公开状态（确定性重放的前提）。
func scriptedOrders(g *Game, side, tick int) []Action {
	S := g.Sides[side]
	alloc := Alloc{25, 25, 25, 25}
	if tick == 2 {
		alloc = Alloc{15, 15, 50, 20} // 非 25 的目标值：给重放篡改测试一个必漂移的锚
	}
	out := []Action{{Type: "alloc", Alloc: &alloc}}
	if tick%6 == 0 && len(S.Queue)+len(S.Active) < 2 && S.Pop+Types["LAV"].Pop <= PopMax {
		out = append(out, Action{Type: "produce", Kind: "LAV"})
	}
	if tick%30 == 3 {
		out = append(out, Action{Type: "weapon"})
	}
	// 每个 LAV 朝对面母舰方向最近 VP 推进；母舰留守
	for _, u := range g.Units {
		if u.Side != side || u.Dead {
			continue
		}
		if u.Kind != "LAV" {
			continue
		}
		vp := g.Vps[tick%len(g.Vps)]
		x, y := vp.X, vp.Y
		out = append(out, Action{Type: "move", ID: &u.ID, X: &x, Y: &y})
	}
	return out
}

func TestVerifyRejectsTruncatedHashes(t *testing.T) {
	/* 删掉帧哈希等于删证据：旧实现只比对现存索引，剩余哈希全对就判 OK。 */
	g := newTestGame(t, 7)
	for i := 0; i < 5; i++ {
		g.Step([2][]Action{nil, nil})
	}
	rec := &VerifyRec{
		Seed: g.Seed, TickLimit: g.Cfg.TickLimit, Generator: g.Cfg.Generator,
		MapAttempt: &g.Map.Attempt, OrderLog: g.OrderLog,
		FrameHashes: g.FrameHashes[:len(g.FrameHashes)-1],
	}
	if rep := VerifyDeterminism(rec); rep.OK {
		t.Fatal("frameHashes 少于 orderLog+1 时必须复核失败")
	}
}

func TestVerifyForfeitNotVacuous(t *testing.T) {
	/* 红牌终止的局：orderLog 里没有收局动作，复核必须靠 rec.Forfeited 复现，
	   否则重放 Result 为 nil，旧实现会静默判 OK。 */
	g := newTestGame(t, 20261109)
	for i := 0; i < 5; i++ {
		g.Step([2][]Action{nil, nil})
	}
	g.Forfeit(1, "")
	r := g.Result()
	if r == nil || r.Winner != 0 {
		t.Fatalf("红牌后终局异常: %+v", r)
	}
	seat := g.Forfeited
	rec := &VerifyRec{
		Seed: g.Seed, TickLimit: g.Cfg.TickLimit, Generator: g.Cfg.Generator,
		MapAttempt: &g.Map.Attempt, OrderLog: g.OrderLog, FrameHashes: g.FrameHashes,
		Forfeited: &seat, Result: r,
	}
	if rep := VerifyDeterminism(rec); !rep.OK {
		t.Fatalf("带 forfeited 的复核应通过: %+v", rep)
	}
	bad := *rec
	bad.Forfeited = nil
	if rep := VerifyDeterminism(&bad); rep.OK || rep.ResultMismatch == nil {
		t.Fatalf("缺少 forfeited 时复核必须失败: %+v", rep)
	}
}
