package sim

import (
	"math"
	"strings"
	"testing"
)

// 攻守引擎测试：构造不变量、布置阶段、矿场收入、胜负链、重放确定性。

func newSiegeGame(t *testing.T, seed int) *SiegeGame {
	t.Helper()
	g, err := NewSiegeGame(seed, SiegeCfg{})
	if err != nil {
		t.Fatalf("NewSiegeGame: %v", err)
	}
	return g
}

func TestSiegeConstructorInvariants(t *testing.T) {
	g := newSiegeGame(t, 20261109)
	if g.Sides[0].Role != "defender" || g.Sides[1].Role != "attacker" {
		t.Fatalf("roles: %s/%s", g.Sides[0].Role, g.Sides[1].Role)
	}
	if g.Sides[0].FortBudget != SiegeFortBudget || g.Sides[1].FortBudget != 0 {
		t.Fatalf("fort budget: %d/%d 应为 16/0", g.Sides[0].FortBudget, g.Sides[1].FortBudget)
	}
	if g.Sides[0].CU != 700 || g.Sides[1].CU != 900 {
		t.Fatalf("初始 CU: %v/%v 应为 700/900", g.Sides[0].CU, g.Sides[1].CU)
	}
	if len(g.Mines) != 4 {
		t.Fatalf("矿场 %d 应为 4（沿用 VP 点位）", len(g.Mines))
	}
	for _, m := range g.Mines {
		if m.Amount != MineAmount || m.Owner != -1 {
			t.Fatalf("矿场初始态异常: %+v", m)
		}
	}
	// 初始兵力：守 1MS+3LAV+SALVAGER；攻 1MS+4LAV+SALVAGER
	count := func(side int, kind string) int {
		n := 0
		for _, u := range g.Units {
			if u.Side == side && u.Kind == kind {
				n++
			}
		}
		return n
	}
	if count(0, "MOTHERSHIP") != 1 || count(1, "MOTHERSHIP") != 1 {
		t.Fatal("双方各 1 母舰")
	}
	if count(0, "LAV") != 3 || count(1, "LAV") != 4 {
		t.Fatalf("LAV %d/%d 应为 3/4", count(0, "LAV"), count(1, "LAV"))
	}
	if count(0, "SALVAGER") != 1 || count(1, "SALVAGER") != 1 {
		t.Fatal("双方各 1 采集车")
	}
	// 母舰 HP 用实验变量（1250），不是 TYPES 的 3000
	ms := g.Mothership(0)
	if ms.MaxHP != 1250 {
		t.Fatalf("守方母舰 HP %v 应为 1250", ms.MaxHP)
	}
	// 已知怪癖（审查附录 C ⑨）：siege hello 约 110KB 超过它声明的 64KB——
	// 该上限只约束 bot→引擎的 actions 行，hello 不在此列。忠实复刻。
	if hello := g.EncodeHello(); len(hello) < 90000 {
		t.Fatalf("siege hello %d 字节异常偏小（地图层丢了？）", len(hello))
	} else if len(hello) <= SiegeMaxLine {
		t.Logf("siege hello %d 字节已回到 64KB 内（上游修了压缩，可更新此断言）", len(hello))
	}
	// 64KB 行上限的真实语义：actions 行超限抛错
	if _, err := g.Decode(0, `{"actions":[],"pad":"`+strings.Repeat("x", SiegeMaxLine+10)+`"}`); err == nil || err.Error() != "line too long" {
		t.Fatalf("超 64KB 的 actions 行应报 line too long: %v", err)
	}
}

func TestSiegeFortPlacement(t *testing.T) {
	g := newSiegeGame(t, 7)
	ms := g.Mothership(0)
	// 合法布置（母舰旁、非山格）——找一块非山平地
	fx, fy := ms.X+Cell*3, ms.Y
	if g.Map.Base[Ci(GxOf(fx), GyOf(fy))] == 3 {
		fx = ms.X - Cell*3
	}
	wall := Action{Type: "fort", Kind: "WALL", X: &fx, Y: &fy}
	g.Step([2][]Action{{wall}, nil})
	if len(g.Rejected) != 0 {
		t.Fatalf("合法布置被拒: %+v", g.Rejected)
	}
	if g.Sides[0].FortBudget != SiegeFortBudget-SiegeFortCostWall {
		t.Fatalf("预算未扣: %d", g.Sides[0].FortBudget)
	}
	// 攻方不能布置
	g.Step([2][]Action{nil, {wall}})
	if len(g.Rejected) == 0 {
		t.Fatal("攻方布置应被拒（only defender）")
	}
	// 超出半径
	far := Action{Type: "fort", Kind: "WALL", X: ptrF(ms.X + SiegeFortRadius*2), Y: ptrF(ms.Y)}
	before := len(g.Rejected)
	g.Step([2][]Action{{far}, nil})
	if len(g.Rejected) == before {
		t.Fatal("超半径布置应被拒")
	}
}

func TestSiegeMineIncome(t *testing.T) {
	g := newSiegeGame(t, 7)
	// 把一台单位直接放到矿场中心 → 下一个 tick 起持续产出
	m := g.Mines[0]
	sv := g.spawn(1, "SALVAGER", m.X, m.Y)
	cu0 := g.Sides[1].CU
	for i := 0; i < 20; i++ {
		g.Step([2][]Action{nil, nil})
	}
	if g.Sides[1].CU <= cu0 {
		t.Fatalf("占矿 20 tick 无收入：CU %v → %v", cu0, g.Sides[1].CU)
	}
	if m.Owner != 1 {
		t.Fatalf("矿场归属 %d 应为攻方 1", m.Owner)
	}
	// SALVAGER 加成：rate = 0.8 + 1.2 = 2.0/tick，另有被动收入 1.5/tick
	expect := cu0 + (1.5+MineRate+MineSalvageBonus)*20
	if math.Abs(g.Sides[1].CU-expect) > 1e-6 {
		t.Fatalf("矿场收入 %v 应为 %v（被动 1.5 + 矿收 2.0/tick）", g.Sides[1].CU, expect)
	}
	_ = sv
}

func TestSiegeWinChain(t *testing.T) {
	/* 守方母舰被摧毁 → 攻方胜（L1）；时间到 → 守方胜（L3） */
	g := newSiegeGame(t, 7)
	g.Mothership(0).HP = -1
	g.Mothership(0).Dead = true // 直击路径上 damageUnit 会置位；这里手动模拟阵亡
	r := g.Result()
	if r.Winner != 1 || r.Level != 1 {
		t.Fatalf("守方母舰被摧毁应攻方胜 L1: winner=%d level=%d", r.Winner, r.Level)
	}
	if !r.Destroyed {
		t.Fatal("destroyed 标记应置位")
	}
	// 时间到守方守住
	g2 := newSiegeGame(t, 7)
	for i := 0; i < g2.Cfg.TickLimit; i++ {
		g2.Step([2][]Action{nil, nil})
		if g2.Done() {
			break
		}
	}
	r2 := g2.Result()
	if r2.Winner != 0 || r2.Level != 3 {
		t.Fatalf("时间到应守方胜 L3: winner=%d level=%d turns=%d", r2.Winner, r2.Level, r2.Turns)
	}
}

func TestSiegeReplayDeterminism(t *testing.T) {
	/* 200 tick 攻守剧本对局：布置工事 + 推进 + 开火 → 重放逐帧一致 */
	g := newSiegeGame(t, 20261109)
	tick := 0
	for tick < 200 && !g.Done() {
		var o0, o1 []Action
		if tick < SiegePlaceTicks && tick%4 == 0 && g.Sides[0].FortBudget >= SiegeFortCostWall {
			ms := g.Mothership(0)
			fx, fy := ms.X+float64(3+tick%6)*Cell, ms.Y+float64(tick%5-2)*Cell
			if g.Map.Base[Ci(GxOf(fx), GyOf(fy))] != 3 {
				o0 = append(o0, Action{Type: "fort", Kind: "WALL", X: &fx, Y: &fy})
			}
		}
		if tick == SiegePlaceTicks {
			al := SiegeAlloc{20, 40, 20, 20}
			o0 = append(o0, Action{Type: "alloc", SiegeAlloc: &al})
			al2 := SiegeAlloc{10, 30, 20, 40}
			o1 = append(o1, Action{Type: "alloc", SiegeAlloc: &al2})
		}
		if tick%8 == 0 {
			o0 = append(o0, Action{Type: "produce", Kind: "LAV"})
			o1 = append(o1, Action{Type: "produce", Kind: "LAV"})
		}
		// 攻方 LAV 朝守方母舰推进（aggressive 命令 = 目标）
		for _, u := range g.Units {
			if u.Dead || u.Side != 1 || u.Kind != "LAV" {
				continue
			}
			ms := g.Mothership(0)
			if ms == nil {
				break
			}
			tx, ty := ms.X, ms.Y
			o1 = append(o1, Action{Type: "move", ID: &u.ID, X: &tx, Y: &ty})
		}
		g.Step([2][]Action{o0, o1})
		tick++
	}
	rep := VerifySiegeDeterminism(&VerifyRec{
		Seed: g.Seed, TickLimit: g.Cfg.TickLimit, Generator: g.Cfg.Generator,
		MapAttempt: &g.Map.Attempt, OrderLog: g.OrderLog, FrameHashes: g.FrameHashes,
	})
	if !rep.OK {
		t.Fatalf("攻守重放确定性失败: %+v", rep)
	}
	// 篡改：删掉布置期结束（tick==PlaceTicks）的 alloc 指令 → 帧的 alloc 字段必漂移
	tampered := &VerifyRec{Seed: g.Seed, TickLimit: g.Cfg.TickLimit, Generator: g.Cfg.Generator,
		MapAttempt: &g.Map.Attempt, FrameHashes: g.FrameHashes}
	for k, e := range g.OrderLog {
		ee := e
		if k == SiegePlaceTicks && len(ee.O[1]) > 0 && ee.O[1][0].Type == "alloc" {
			ee.O[1] = ee.O[1][1:]
		}
		tampered.OrderLog = append(tampered.OrderLog, ee)
	}
	rep2 := VerifySiegeDeterminism(tampered)
	if rep2.OK {
		t.Fatal("篡改 orderLog 未被探测")
	}
}

func TestSiegeVerifyCarriesCfg(t *testing.T) {
	/* 攻守复核必须使用记录里的完整 SiegeCfg：只带 TickLimit 会用默认兵力重放。 */
	g, err := NewSiegeGame(20261109, SiegeCfg{Generator: "minecraft", AttackerStartLav: 5})
	if err != nil {
		t.Fatalf("NewSiegeGame: %v", err)
	}
	for i := 0; i < 10; i++ {
		g.Step([2][]Action{nil, nil})
	}
	c := g.Cfg
	rec := &VerifyRec{
		Seed: g.Seed, TickLimit: g.Cfg.TickLimit, Generator: g.Cfg.Generator,
		MapAttempt: &g.Map.Attempt, OrderLog: g.OrderLog, FrameHashes: g.FrameHashes,
		SiegeCfg: &c,
	}
	if rep := VerifySiegeDeterminism(rec); !rep.OK {
		t.Fatalf("带 SiegeCfg 的复核应通过: %+v", rep)
	}
	noCfg := *rec
	noCfg.SiegeCfg = nil
	if rep := VerifySiegeDeterminism(&noCfg); rep.OK {
		t.Fatal("缺少 SiegeCfg 时起点兵力不同，复核不应通过")
	}
}

func ptrF(v float64) *float64 { return &v }
