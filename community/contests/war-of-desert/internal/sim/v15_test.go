package sim

import (
	"fmt"
	"math"
	"strings"
	"testing"
)

func newV15Game(t *testing.T, seed int) *Game {
	t.Helper()
	g, err := NewGame(seed, GameCfg{Generator: "minecraft", TickLimit: 1200, Ruleset: RulesetV15})
	if err != nil {
		t.Fatalf("NewGame v15: %v", err)
	}
	return g
}

// 遗产运输 + 插旗 + 拔旗掉落的主链路。
func TestV15RelicFlagChain(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	if g.V15 == nil || len(g.V15.Relics) < 1 {
		t.Fatal("v15 未初始化遗产")
	}
	vp := g.Vps[0]
	var sv *Unit
	for _, u := range g.Units {
		if u.Side == 0 && u.Kind == "SALVAGER" {
			sv = u
			break
		}
	}
	if sv == nil {
		t.Fatal("v15 开局没有采矿机")
	}
	// 直接把采矿机放到 VP 中心，省掉寻路；obey 防止中性行为把它带走。
	sv.Mode = "obey"
	sv.X, sv.Y = vp.X, vp.Y
	sv.PX, sv.PY = vp.X, vp.Y
	for i := 0; i < FlagPlaceTicks+5; i++ {
		g.Step([2][]Action{nil, nil})
	}
	f := g.flagAtVP(vp.ID)
	if f == nil || f.Side != 0 {
		t.Fatalf("插旗失败：flag=%v sv=(%.1f,%.1f) vp=(%.1f,%.1f) timer=%d dead=%v harvest=%v flags=%d",
			f, sv.X, sv.Y, vp.X, vp.Y, sv.FlagTimer, sv.Dead, sv.Harvest, len(g.V15.Flags))
	}
	if g.Sides[0].VS.Flags != FlagPlaceVP {
		t.Fatalf("插旗分 %d 应为 %d", g.Sides[0].VS.Flags, FlagPlaceVP)
	}

	// 把一件遗产挪到采矿机脚下并指派运输。
	r := g.V15.Relics[0]
	r.X, r.Y, r.State = sv.X, sv.Y, "ground"
	rid := r.ID
	sv.Relic = &rid
	sv.RState = "toRelic"
	sv.RelicTimer = 0
	for i := 0; i < RelicLoadTicks+RelicDropTicks+20; i++ {
		g.Step([2][]Action{nil, nil})
	}
	if r.State != "stored" {
		t.Fatalf("遗产未入库：state=%s rstate=%s", r.State, sv.RState)
	}
	if g.Sides[0].VS.Relics != RelicValue {
		t.Fatalf("遗产分 %d 应为 %d", g.Sides[0].VS.Relics, RelicValue)
	}

	// 拔旗：守方扣旗惩罚 + 该点库存遗产分，遗产掉回地面，攻方得赏金。
	g.destroyFlag(f, 1)
	if g.Sides[0].VS.Flags != -FlagPenalty || g.Sides[0].VS.Relics != 0 {
		t.Fatalf("拔旗后守方账本 %+v 应为 flags=%d relics=0", g.Sides[0].VS, -FlagPenalty)
	}
	if g.Sides[1].VS.Flags != FlagBounty {
		t.Fatalf("拔旗方得分 %d 应为 %d", g.Sides[1].VS.Flags, FlagBounty)
	}
	if r.State != "ground" {
		t.Fatalf("拔旗后遗产应为无主，实际 %s", r.State)
	}
}

// 等级：只靠击杀经验，5 级封顶，精度加成不超过科技增益的 60%。
func TestV15Veterancy(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	var lav *Unit
	for _, u := range g.Units {
		if u.Side == 0 && u.Kind == "LAV" {
			lav = u
			break
		}
	}
	if lav == nil {
		t.Fatal("没有 LAV")
	}
	g.Sides[0].Mods.CEP = 0.37
	// 杀 9 台 LAV 的量（每台 1 XP）→ light 档 9 XP = 5 级。
	for i := 0; i < 9; i++ {
		g.addKillXP(lav, "LAV")
	}
	if lav.Level != 5 {
		t.Fatalf("等级 %d 应为 5", lav.Level)
	}
	if m := g.vetCEPMul(lav); math.Abs(m-(1-0.6*0.37)) > 1e-9 {
		t.Fatalf("5 级精度系数 %v 应为 %v", m, 1-0.6*0.37)
	}
	if g.vetVPMul(lav) < 1.5 {
		t.Fatalf("5 级击杀分倍率 %v 过低", g.vetVPMul(lav))
	}
}

// v15 射界对准后必须能开火（防 CEP/炮塔改造把战斗彻底锁死）。
func TestV15Fire(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	var a, b *Unit
	for _, u := range g.Units {
		if u.Kind == "LAV" && u.Side == 0 && a == nil {
			a = u
		}
		if u.Kind == "LAV" && u.Side == 1 && b == nil {
			b = u
		}
	}
	if a == nil || b == nil {
		t.Fatal("缺少 LAV")
	}
	// 清出一块平地，避免弹道被山挡。
	for y := 90; y < 110; y++ {
		for x := 90; x < 110; x++ {
			g.Map.Base[Ci(x, y)] = 0
			g.Map.Elev[Ci(x, y)] = 0
		}
	}
	a.X, a.Y, a.PX, a.PY = 600, 600, 600, 600
	b.X, b.Y, b.PX, b.PY = 660, 600, 660, 600
	a.Facing, a.Turret = 0, 0
	b.Facing, b.Turret = 180, 180
	aid, bid := a.ID, b.ID
	for i := 0; i < 40; i++ {
		g.Step([2][]Action{
			{{Type: "attack", ID: &aid, TargetID: &bid}},
			{{Type: "attack", ID: &bid, TargetID: &aid}},
		})
	}
	if g.Sides[0].Shots+g.Sides[1].Shots == 0 {
		t.Fatalf("对准后仍未开火：a(facing=%.1f turret=%.1f) b(facing=%.1f turret=%.1f)",
			a.Facing, a.Turret, b.Facing, b.Turret)
	}
}

// 攻坚兵必须能对母舰造成伤害（证明斩舰路径在规则层是通的）。
func TestV15RailgunVsMothership(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	ms := g.Mothership(1)
	if ms == nil {
		t.Fatal("没有敌方母舰")
	}
	// 在母舰射程外放一台 railgun（38 格 vs 母舰 7 格）。
	x, y := ms.X-200, ms.Y
	if x < 10 {
		x = ms.X + 200
	}
	rg := g.spawn(0, "RAILGUN", x, y)
	rg.Level = 1
	rg.Facing, rg.Turret = 0, 0
	rg.Mode = "obey"
	id, mid := rg.ID, ms.ID
	before := ms.HP
	for i := 0; i < 400; i++ {
		g.Step([2][]Action{
			{{Type: "attack", ID: &id, TargetID: &mid}},
			nil,
		})
		if g.Done() {
			break
		}
	}
	if ms.HP >= before {
		t.Fatalf("railgun 未对母舰造成伤害：%.0f → %.0f (shots=%d)", before, ms.HP, g.Sides[0].Shots)
	}
	t.Logf("母舰 HP %.0f → %.0f，railgun shots=%d", before, ms.HP, g.Sides[0].Shots)
}

// 拔旗必须走「打旗 HP」而不是站圈读条。
func TestV15FlagShot(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	vp := g.Vps[0]
	f := g.placeFlag(1, vp) // 敌方旗
	var lav *Unit
	for _, u := range g.Units {
		if u.Side == 0 && u.Kind == "LAV" {
			lav = u
			break
		}
	}
	if lav == nil {
		t.Fatal("没有 LAV")
	}
	lav.X, lav.Y, lav.PX, lav.PY = vp.X-40, vp.Y, vp.X-40, vp.Y
	lav.Facing, lav.Turret = 0, 0
	lav.Mode = "obey"
	id, fid := lav.ID, f.ID
	for i := 0; i < 400 && g.flagByID(fid) != nil; i++ {
		g.Step([2][]Action{
			{{Type: "attackFlag", ID: &id, FlagID: &fid}},
			nil,
		})
	}
	if g.flagByID(fid) != nil {
		t.Fatalf("旗未被击毁：hp=%.0f shots=%d", f.HP, g.Sides[0].Shots)
	}
	if g.Sides[1].VS.Flags != -FlagPenalty {
		t.Fatalf("守方旗分 %d 应为 %d", g.Sides[1].VS.Flags, -FlagPenalty)
	}
	if g.Sides[0].VS.Flags != FlagBounty {
		t.Fatalf("攻方旗分 %d 应为 %d", g.Sides[0].VS.Flags, FlagBounty)
	}
}

func TestV16RelicVSCapAndRetract(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	vp := g.Vps[0]
	f := g.placeFlag(0, vp)
	r1 := &Relic{ID: 1, State: "stored", VPID: vp.ID}
	r2 := &Relic{ID: 2, State: "stored", VPID: vp.ID}
	r3 := &Relic{ID: 3, State: "stored", VPID: vp.ID}
	g.V15.Relics = append(g.V15.Relics, r1, r2, r3)
	g.awardRelicVS(0, r1)
	g.awardRelicVS(0, r2)
	g.awardRelicVS(0, r3)
	if g.Sides[0].VS.Relics != RelicVSCap {
		t.Fatalf("遗产分 %d 应为上限 %d", g.Sides[0].VS.Relics, RelicVSCap)
	}
	if g.Sides[0].DeliveredRelics != 3 || r3.AwardedVS[0] {
		t.Fatalf("第 3 件应计科技但不计分：delivered=%d awarded=%v", g.Sides[0].DeliveredRelics, r3.AwardedVS[0])
	}
	g.destroyFlag(f, 1)
	if g.Sides[0].VS.Relics != 0 {
		t.Fatalf("拔旗后库存遗产分应撤回为 0，实际 %d", g.Sides[0].VS.Relics)
	}
	if g.Sides[0].VS.Flags != -FlagPenalty || g.Sides[1].VS.Flags != FlagBounty {
		t.Fatalf("拔旗账本错误：守方 %d 攻方 %d", g.Sides[0].VS.Flags, g.Sides[1].VS.Flags)
	}
	for _, r := range []*Relic{r1, r2, r3} {
		if r.State != "ground" {
			t.Fatalf("遗产 %d 应掉回地面，实际 %s", r.ID, r.State)
		}
	}
	if g.Sides[0].VS.Total != g.Sides[0].VS.Kills+g.Sides[0].VS.Relics+g.Sides[0].VS.Flags {
		t.Fatalf("VS.Total 不守恒：%+v", g.Sides[0].VS)
	}
}

func TestV16ResearchShip(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	g.Sides[0].CU = 1000
	vp := g.Vps[0]
	g.placeFlag(0, vp)
	for i := 1; i <= 2; i++ {
		g.V15.Relics = append(g.V15.Relics, &Relic{ID: 100 + i, State: "stored", VPID: vp.ID})
	}
	ship := g.spawn(0, "RESEARCH", vp.X, vp.Y)
	ship.Mode = "obey"
	for i := 0; i < 5; i++ {
		g.Step([2][]Action{nil, nil})
	}
	if g.V15.Research[0] == nil || g.V15.Research[0].Node != "R1" {
		t.Fatalf("研究舰未开始 R1：%+v", g.V15.Research[0])
	}
	// 研究状态必须随 tick 下发给本方（曾因结构体整体赋值被清掉）。
	if line := g.EncodeTick(0); !strings.Contains(line, `"research":{"node":"R1"`) {
		t.Fatalf("tick.me.research 未下发：%s", line)
	}
	for i := 0; i < 100; i++ {
		g.Step([2][]Action{nil, nil})
	}
	if !g.Sides[0].TechDone["R1"] {
		t.Fatalf("R1 未完成：research=%+v techs=%v", g.V15.Research[0], g.Sides[0].TechDone)
	}
	if math.Abs(g.Sides[0].Mods.Damage-0.10) > 1e-9 {
		t.Fatalf("R1 伤害加成 %v 应为 0.10", g.Sides[0].Mods.Damage)
	}
}

func TestV16ResearchCancelOnFlag(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	g.Sides[0].CU = 1000
	vp := g.Vps[0]
	f := g.placeFlag(0, vp)
	for i := 1; i <= 2; i++ {
		g.V15.Relics = append(g.V15.Relics, &Relic{ID: 200 + i, State: "stored", VPID: vp.ID})
	}
	ship := g.spawn(0, "RESEARCH", vp.X, vp.Y)
	ship.Mode = "obey"
	for i := 0; i < 5; i++ {
		g.Step([2][]Action{nil, nil})
	}
	if g.V15.Research[0] == nil {
		t.Fatal("研究未开始")
	}
	g.destroyFlag(f, 1)
	if g.V15.Research[0] != nil {
		t.Fatalf("拔旗后研究应取消：%+v", g.V15.Research[0])
	}
	if g.Sides[0].TechDone["R1"] {
		t.Fatal("拔旗后 R1 不应完成")
	}
}

func TestV16KillVP(t *testing.T) {
	cases := map[string]int{"LAV": 66, "AAV": 160, "RAILGUN": 160, "ARTILLERY": 200, "RESEARCH": 66}
	for kind, want := range cases {
		if got := KillVP(kind); got != want {
			t.Fatalf("%s KillVP=%d 应为 %d", kind, got, want)
		}
	}
}

func TestV16RelicSitesReachable(t *testing.T) {
	// 20261277 的遗迹候选点原始位置不可达，是回归「修补结果被 if 作用域遮蔽」的用例。
	for _, seed := range []int{20261121, 20261122, 20261142, 20261150, 20261277} {
		g := newV15Game(t, seed)
		r0, r1 := g.reachFromSpawn(0), g.reachFromSpawn(1)
		for i, s := range g.V15.Sites {
			if !g.pointReachable(r0, r1, s.X, s.Y) {
				t.Fatalf("seed=%d site=%d (%.0f,%.0f) 不可达", seed, i, s.X, s.Y)
			}
		}
	}
}

func TestV16SeatOrderAlternates(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	g.Turn = 0
	if o := g.sideOrder(); o != [2]int{0, 1} {
		t.Fatalf("turn0 顺序 %v", o)
	}
	g.Turn = 1
	if o := g.sideOrder(); o != [2]int{1, 0} {
		t.Fatalf("turn1 顺序 %v", o)
	}
}

func TestV16ResearchAction(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	vp := g.Vps[0]
	ship := g.spawn(0, "RESEARCH", vp.X+20, vp.Y+20)
	id := ship.ID
	acts, err := g.Decode(0, fmt.Sprintf(`{"actions":[{"type":"relicresearch","id":%d}]}`, id))
	if err != nil || len(acts) != 1 || acts[0].Type != "relicresearch" {
		t.Fatalf("relicresearch 解码失败：acts=%v err=%v rejected=%v", acts, err, g.Rejected)
	}
	g.Step([2][]Action{acts, nil})
	if !ship.RDuty {
		t.Fatal("relicresearch 动作未设置研究舰职责")
	}
}

func TestV172NoRelicTechTieBreak(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	g.Turn = g.Cfg.TickLimit
	g.Sides[0].TechDone["R1"] = true
	g.Sides[1].TechDone["R1"] = true
	g.Sides[1].TechDone["R2"] = true
	if !g.checkEndV15(g.Mothership(0), g.Mothership(1)) {
		t.Fatal("应触发时间到判定")
	}
	// v1.7.2：科技等级不再参与 tie-break，VS/KD/母舰 HP 全同应直接判平。
	if g.ResultData == nil || g.ResultData.Winner != -1 || g.ResultData.Level != 5 {
		t.Fatalf("科技等级不应再决定胜负：%+v", g.ResultData)
	}
}

func TestV16NoProgressInvalid(t *testing.T) {
	g, err := NewGame(fairnessSeeds[0], GameCfg{Generator: "minecraft", TickLimit: 5, Ruleset: RulesetV15})
	if err != nil {
		t.Fatal(err)
	}
	for !g.Done() {
		g.Step([2][]Action{nil, nil})
	}
	if g.ResultData == nil || !g.ResultData.Invalid {
		t.Fatalf("5 tick 无进展应标记 invalid：%+v", g.ResultData)
	}
}

func TestV19RuleValues(t *testing.T) {
	if TechsV15["R1"].Tick != 60 || TechsV15["R2"].Tick != 85 ||
		TechsV15["R3"].Tick != 110 || TechsV15["R4"].Tick != 135 {
		t.Fatalf("遗产科技研究时间被改动：%v/%v/%v/%v",
			TechsV15["R1"].Tick, TechsV15["R2"].Tick, TechsV15["R3"].Tick, TechsV15["R4"].Tick)
	}
	if TypesV15["RESEARCH"].Cost != 100 || TypesV15["RESEARCH"].Build != 25 ||
		TypesV15["RESEARCH"].Speed != 5.0 {
		t.Fatalf("研究舰数值不符：%+v", TypesV15["RESEARCH"])
	}
	if TypesV15["RAILGUN"].Cost != 240 || TypesV15["RAILGUN"].RU != 40 ||
		TypesV15["RAILGUN"].Speed != 2.0*Scale {
		t.Fatalf("RAILGUN v15 数值不符：%+v", TypesV15["RAILGUN"])
	}
	if TypesV15["ARTILLERY"].Cost != 550 || TypesV15["ARTILLERY"].RU != 180 ||
		TypesV15["ARTILLERY"].Build != 75 || TypesV15["ARTILLERY"].Speed != 1.8*Scale {
		t.Fatalf("ARTILLERY v15 数值不符：%+v", TypesV15["ARTILLERY"])
	}
	if RelicSpawnTicks != 70 || RelicMaxGround != 3 {
		t.Fatalf("遗产补给节奏不符：%d tick / %d 件", RelicSpawnTicks, RelicMaxGround)
	}
}

func TestV15Smoke(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[1])
	for i := 0; i < 200; i++ {
		g.Step([2][]Action{nil, nil})
	}
	f := g.BuildFrame()
	if len(f.Relics) == 0 || len(f.VS) != 2 {
		t.Fatalf("v15 帧缺遗产/胜利分：relics=%d vs=%d", len(f.Relics), len(f.VS))
	}
}

// v1.9.2 运货死锁修复：携带遗产且场上无己方旗时，采矿机必须能接受 Bot move
// （修复前被引擎托管锁死在原地，永远走不到 VP 去插旗）；转采集时先弃货。
func TestV15CarryNoFlagMobility(t *testing.T) {
	g := newV15Game(t, fairnessSeeds[0])
	var sv *Unit
	for _, u := range g.Units {
		if u.Side == 0 && u.Kind == "SALVAGER" {
			sv = u
			break
		}
	}
	if sv == nil {
		t.Fatal("v15 开局没有采矿机")
	}
	if len(g.V15.Flags) != 0 {
		t.Fatal("开局不应有旗")
	}
	sv.Mode = "obey"
	r := g.V15.Relics[0]
	r.State, r.CarrierID = "carried", sv.ID
	r.X, r.Y = sv.X, sv.Y
	rid := r.ID
	sv.Relic, sv.RState = &rid, "toDeposit"

	x0, y0 := sv.X, sv.Y
	tx, ty := sv.X+60, sv.Y+60
	for i := 0; i < 10; i++ {
		g.Step([2][]Action{{Action{Type: "move", ID: &sv.ID, X: &tx, Y: &ty}}, nil})
	}
	if distXY(sv.X, sv.Y, x0, y0) < 1 {
		t.Fatalf("无旗时携货采矿机应能移动：(%.1f,%.1f)→(%.1f,%.1f) rstate=%s",
			x0, y0, sv.X, sv.Y, sv.RState)
	}
	if sv.Relic == nil || r.State != "carried" {
		t.Fatalf("移动不应丢货：sv.Relic=%v r.State=%s", sv.Relic, r.State)
	}

	resID := g.Resources[0].ID
	g.Step([2][]Action{{Action{Type: "harvest", ID: &sv.ID, ResID: &resID}}, nil})
	if sv.Relic != nil || sv.RState != "" {
		t.Fatalf("转采集应清运输状态：sv.Relic=%v rstate=%s", sv.Relic, sv.RState)
	}
	if r.State != "ground" {
		t.Fatalf("转采集应把遗产丢回地面，实际 %s", r.State)
	}
	if sv.Harvest == nil || *sv.Harvest != resID {
		t.Fatalf("转采集未生效：harvest=%v", sv.Harvest)
	}
}
