// v1.6 实验规则：胜利分账本、遗产上限/撤回、研究舰、遗产科技、旗分转移、CEP/射界、单位等级。
//
// 设计目标（2026-11-20 讨论稿）：
//   - 删掉「站在 VP 圈里每 tick 加分」，VP 只作为插旗与遗产入库的据点；
//   - 击杀/遗产/插旗三本账合成 VictoryScore，时间到按胜利分判；
//   - 遗产物由 SALVAGER 运输，插旗后入库；旗被击毁则旗分扣除、库存遗产掉回地面；
//   - 战斗单位有射界与炮塔转速，射击带圆概率误差（CEP），科技树新增火控分支；
//   - 单位等级参考《家园：卡拉克沙漠》：只靠击杀获得经验，5 级，逐级加精度/伤害/减伤，
//     高等级单位击杀获得更多胜利分。
//
// 本文件只在 GameCfg.Ruleset == "v15" 时生效；v14 走原路径，指纹与语义不变。
package sim

import (
	"fmt"
	"math"
)

const (
	RulesetV14 = "v14"
	RulesetV15 = "v15"

	// v1.9.2：运货死锁修复——携带遗产的采矿机在己方无旗时恢复 Bot 移动；
	// 对携带遗产的采矿机下达 harvest 先弃货再转采集。语义修订，指纹随之变化。
	RulesVersionV15 = "1.9.2"

	// MothershipSiegeMul v1.8.2：RAILGUN/ARTILLERY 对母舰伤害倍率。
	// 远程兵每方每局只有 0.4 / 0.08 台，单靠造价调整不足以在 1500 tick 内拆舰。
	MothershipSiegeMul = 1.5

	// PopMaxV15 v1.8 人口上限：v1.4 的 24 在实测中经常顶满（峰值 23–30），
	// 双方各堆到 11 台却几乎没有交战。提高到 36 让编成有扩张空间。
	PopMaxV15 = 36

	// VSWinScore 胜利分即时胜利阈值。时间到仍未达到则比胜利分。
	VSWinScore = 300

	// RelicValue 一件遗产入库的胜利分（v1.6：50）。
	RelicValue = 50
	// RelicVSCap 每方遗产胜利分上限（v1.6：100，即 2 件）。
	// 超过上限的遗产仍会入库并推进遗产科技，但不再给胜利分。
	RelicVSCap = 100
	// FlagPlaceVP 插旗不再给胜利分。
	FlagPlaceVP = 0
	// FlagPenalty 旗被击毁时守方固定扣分；每次失去 VP 都扣。
	FlagPenalty = 25
	// FlagBounty 击毁敌方旗时攻方得分；同一 VP 每方只结算一次。
	FlagBounty = 25
	// FlagHP 旗的耐久。LAV 单台约 100 tick 拆掉，给守军回防留窗口。
	FlagHP = 160
	// KillVPCap v1.6 单次击杀胜利分上限，避免一发 ARTILLERY 击杀直接接近阈值。
	KillVPCap = 200

	// RelicLoadTicks / RelicDropTicks 采矿机装载/入库的读条。
	RelicLoadTicks = 30
	RelicDropTicks = 30
	// RelicSpawnTicks 遗产补充间隔；RelicMaxGround 场上同时存在的无主遗产上限。
	// v1.9.1：100→70 / 2→3。对局缩短到 ~1100 tick 后，遗产科技只在 32% 的对局出现，
	// 提高补给频率与在场上限，让 R1–R4 在实战中有机会铺开。
	RelicSpawnTicks = 70
	RelicMaxGround  = 3
	// FlagPlaceTicks 采矿机在 VP 圈内静止插旗的读条。
	FlagPlaceTicks = 30

	// MothershipKillVP 母舰被毁的胜利分（终局记录用，击杀本身直接结束）。
	MothershipKillVP = 300

	// V15DamageMul v1.5 全局直伤倍率：提高战斗致命性，避免命中都被血量摊平。
	V15DamageMul = 1.80
	// V15CEPMul v1.5 的 CEP 整体缩放（越小越准）。
	V15CEPMul = 0.75
)

// RelicRadius / RelicCarrySpeedMul 遗产拾取半径与负重减速。
const (
	RelicRadius        = 2.0 * Cell * Scale
	RelicCarrySpeedMul = 0.80
	// DropoffRadius v15 的卸货半径。母舰基地外圈常被山环包住，寻路到母舰
	// 中心格会停在环外，旧口径 ResRadius（2.4 格）永远够不到，采集车在
	// 资源点与基地之间来回摆却从不卸货。v15 放宽到基地外围 6 格。
	DropoffRadius = 8.0 * Cell * Scale
)

// WeightsV15 / FieldNamesV15 v1.6 计分卡：字段 1 胜利分、字段 7 遗产吞吐。
var (
	WeightsV15    = [7]float64{0.25, 0.25, 0.10, 0.10, 0.05, 0.10, 0.15}
	FieldNamesV15 = [7]string{"胜利分", "歼敌", "保存兵力", "母舰完整度", "KD 差", "旗控制", "遗产吞吐"}
)

// VictoryScore 三本账。Total 必须与三项之和保持一致（所有写入走 addVS）。
// Delivered/Stored 是 v1.6 诊断字段，不参与 Total。
type VictoryScore struct {
	Kills     int `json:"kills"`
	Relics    int `json:"relics"`
	Flags     int `json:"flags"`
	Total     int `json:"total"`
	Delivered int `json:"delivered,omitempty"`
	Stored    int `json:"stored,omitempty"`
}

// Relic 一件遗产物。State: ground（无主）/ carried（在采矿机上）/ stored（已入库）。
type Relic struct {
	ID        int     `json:"id"`
	X         float64 `json:"x"`
	Y         float64 `json:"y"`
	State     string  `json:"state"`
	CarrierID int     `json:"carrier,omitempty"`
	VPID      int     `json:"vp,omitempty"`
	// v1.6：每方是否已从这件遗产拿到 50 分 / 是否已为每方计入科技进度。
	AwardedVS   [2]bool `json:"awardedVs,omitempty"`
	DeliveredTo [2]bool `json:"deliveredTo,omitempty"`
}

// ResearchRadius 研究舰可研究的范围：旗附近（2.5 个 VP 半径）。
// 不要求精确站进 4.2 格圈内，避免研究舰在河岸/旗外永久卡死。
const ResearchRadius = 2.5 * VPRadius

// ResearchState 研究舰正在推进的遗产科技。
type ResearchState struct {
	Node     string  `json:"node,omitempty"`
	VPID     int     `json:"vp,omitempty"`
	ShipID   int     `json:"ship,omitempty"`
	Progress float64 `json:"progress"`
	Total    float64 `json:"total"`
	Active   bool    `json:"active"`
	Paused   bool    `json:"paused,omitempty"`
	// CUPaid 已投入的 CU；拔旗/舰毁时这笔投入损失。
	CUPaid int `json:"cuPaid,omitempty"`
}

// Flag 一面旗。挂在某个 VP 上，有独立耐久，可被击毁。
type Flag struct {
	ID    int     `json:"id"`
	VPID  int     `json:"vp"`
	Side  int     `json:"side"`
	X     float64 `json:"x"`
	Y     float64 `json:"y"`
	HP    float64 `json:"hp"`
	MaxHP float64 `json:"maxHp"`
}

// V15State v1.5/v1.6 专属状态；v14 为 nil。
type V15State struct {
	Relics      []*Relic
	Flags       []*Flag
	NextRelicID int
	NextFlagID  int
	NextSite    int
	Rng         *RNG
	Sites       []PathPt
	// v1.6：每个 VP 对每个攻方只结算一次拔旗赏金。
	FlagBounty [2]map[int]bool
	// v1.6：每方当前的研究舰研究状态（同时只研究一个节点）。
	Research [2]*ResearchState
}

// FlagType 旗的结构参数（不是可生产单位，不进 Types 表）。
var FlagType = UnitType{
	HP: FlagHP, Speed: 0, Range: 0, Dmg: 0, Rof: 0, Vision: 0,
	Arc: "none", Armor: "heavy", Cost: 0, Build: 0, Pop: 0, Structure: true,
}

// TypesV15 v1.5/v1.6 单位表：在正式单位表上追加「研究舰」。
// 保留独立 map，避免把 v1.6 新单位写进 v1.4 的 Types 指纹。
var TypesV15 = func() map[string]UnitType {
	m := make(map[string]UnitType, len(Types)+1)
	for k, v := range Types {
		m[k] = v
	}
	m["RESEARCH"] = UnitType{
		HP: 80, Speed: 5.0, Range: 0, Dmg: 0, Rof: 0, Vision: 9,
		Arc: "none", Armor: "light", V0: 0, Elev: [2]int{0, 0},
		Cost: 100, Build: 25, Pop: 2,
	}
	// v1.8.1：远程攻坚兵在 v15 经济里造不出来（实测 ARTILLERY 每方每局 0.04 台），
	// 单独下调造价/ RU；v1.4 的 Types 保持原价。
	// v1.9：再补机动性（RAILGUN 1.6→2.0 格/s、ARTILLERY 1.4→1.8），
	// ARTILLERY 650→550 CU、Build 90→75，让远程路线能在一局内成形。
	if t, ok := m["RAILGUN"]; ok {
		t.Cost, t.RU = 240, 40
		t.Speed = 2.0 * Scale
		m["RAILGUN"] = t
	}
	if t, ok := m["ARTILLERY"]; ok {
		t.Cost, t.RU = 550, 180
		t.Speed = 1.8 * Scale
		t.Build = 75
		m["ARTILLERY"] = t
	}
	return m
}()

// ProducibleV15 v1.6 生产名单：研究舰只能由 v1.5+ 生产。
var ProducibleV15 = append(append([]string(nil), Producible...), "RESEARCH")

// typeOrderV15 固定枚举顺序（含研究舰），用于 v1.6 指纹与 RPS。
func typeOrderV15() []string { return append(typeOrder(), "RESEARCH") }

// ut 返回当前规则集的单位类型；v1.4 不包含研究舰。
func (g *Game) ut(kind string) UnitType {
	if g != nil && g.v15() {
		if t, ok := TypesV15[kind]; ok {
			return t
		}
	}
	return Types[kind]
}

// producible 返回当前规则集的可生产单位。
func (g *Game) producible() []string {
	if g != nil && g.v15() {
		return ProducibleV15
	}
	return Producible
}

// typeTable 返回当前规则集的单位表。
func (g *Game) typeTable() map[string]UnitType {
	if g != nil && g.v15() {
		return TypesV15
	}
	return Types
}

// TechTreesV15 在现有四棵树基础上新增「火控」分支。
var TechTreesV15 = func() map[string]struct {
	Name   string `json:"name"`
	Kind   string `json:"kind"`
	Levels int    `json:"levels"`
	Desc   string `json:"desc"`
} {
	m := map[string]struct {
		Name   string `json:"name"`
		Kind   string `json:"kind"`
		Levels int    `json:"levels"`
		Desc   string `json:"desc"`
	}{}
	for k, v := range TechTrees {
		m[k] = v
	}
	m["firecontrol"] = struct {
		Name   string `json:"name"`
		Kind   string `json:"kind"`
		Levels int    `json:"levels"`
		Desc   string `json:"desc"`
	}{"火控系统", "linear", 3, "提升炮塔转速、降低射击圆概率误差（CEP）"}
	m["relic"] = struct {
		Name   string `json:"name"`
		Kind   string `json:"kind"`
		Levels int    `json:"levels"`
		Desc   string `json:"desc"`
	}{"遗产科技", "linear", 4, "由研究舰在库存遗产≥2/3/4/5 的旗下逐级研究，提供强力战斗加成"}
	return m
}()

// TechsV15 在现有科技表上追加火控三级。C1 要求 F1、C2 要求 C1+F2，
// 让精度分支接在火力树上，而不是另起一条可裸奔的树。
var TechsV15 = func() map[string]TechDef {
	m := make(map[string]TechDef, len(Techs)+3)
	for k, v := range Techs {
		m[k] = v
	}
	m["C1"] = TechDef{Tree: "firecontrol", Tier: 1, Name: "光学测距仪", Tick: 55, CU: 90,
		Req: []string{"F1"}, Eff: map[string]any{"cep": 0.10, "traverse": 0.15}}
	m["C2"] = TechDef{Tree: "firecontrol", Tier: 2, Name: "双向稳定器", Tick: 90, CU: 150,
		Req: []string{"C1", "F2"}, Eff: map[string]any{"cep": 0.12, "traverse": 0.15}}
	m["C3"] = TechDef{Tree: "firecontrol", Tier: 3, Name: "火控计算机", Tick: 130, CU: 230,
		Req: []string{"C2"}, Eff: map[string]any{"cep": 0.15, "traverse": 0.20}}
	// v1.6 遗产科技：不是普通研究队列，必须由研究舰停在库存遗产足够的旗下推进。
	// 遗产科技不额外收 CU：库存遗产本身就是投入，代价是研究舰与旗必须被守住。
	m["R1"] = TechDef{Tree: "relic", Tier: 1, Name: "遗产电池", Tick: 60, CU: 0,
		ReqStored: 2, Eff: map[string]any{"damage": 0.10}}
	m["R2"] = TechDef{Tree: "relic", Tier: 2, Name: "磁轨超导", Tick: 85, CU: 0,
		ReqStored: 3, Req: []string{"R1"}, Eff: map[string]any{"cep": 0.10, "traverse": 0.15}}
	m["R3"] = TechDef{Tree: "relic", Tier: 3, Name: "复合遗产装甲", Tick: 110, CU: 0,
		ReqStored: 4, Req: []string{"R2"}, Eff: map[string]any{"hp": 0.10, "dr": 0.10}}
	m["R4"] = TechDef{Tree: "relic", Tier: 4, Name: "遗产核心", Tick: 135, CU: 0,
		ReqStored: 5, Req: []string{"R3"}, Eff: map[string]any{"range": 0.15, "damage": 0.15}}
	return m
}()

// CEPMax 满射程处的圆概率误差（世界格；1 格 = 10 m）。
// 近距按 r/range 线性缩小，最低 35%，避免贴脸也打飞。
var CEPMax = map[string]float64{
	"LAV": 0.60, "AAV": 0.70, "RAILGUN": 0.35,
	"ARTILLERY": 2.20, "CARRIER": 0.60, "MOTHERSHIP": 0.80,
}

// ArcHalf 车体射界半角（度）。360° 炮塔用 180。
var ArcHalf = map[string]float64{
	"LAV": 120, "AAV": 90, "RAILGUN": 45,
	"ARTILLERY": 180, "CARRIER": 120, "MOTHERSHIP": 150,
}

// TraverseRate 炮塔转速（度/tick，20 tick = 1 s）。
// 近战目标角速度很高：LAV 在 65 px 处横穿时约 17°/tick。转速太低会让
// 「有射界」直接退化成「永远打不中」，所以轻单位给到 12°/tick。
var TraverseRate = map[string]float64{
	"LAV": 12.0, "AAV": 6.0, "RAILGUN": 3.0,
	"ARTILLERY": 2.0, "CARRIER": 6.0, "MOTHERSHIP": 4.0,
}

// VetTitles 五级军衔（参考卡拉克沙漠的 Coalition 命名）。
var VetTitles = [5]string{"新兵", "老兵", "士官", "上尉", "指挥官"}

// VetThresholds 升到下一级所需的累计经验；经验只来自击杀，按目标造价/100 计。
// 分三档：轻/中/重，重单位升级更慢。
var VetThresholds = map[string][5]float64{
	"light":  {0, 1.5, 3.5, 6.0, 9.0},
	"medium": {0, 2.0, 5.0, 8.0, 12.0},
	"heavy":  {0, 3.0, 6.0, 10.0, 15.0},
}

func vetClass(kind string) string {
	switch kind {
	case "LAV", "SCOUT", "SALVAGER":
		return "light"
	case "AAV", "RAILGUN", "CARRIER":
		return "medium"
	case "ARTILLERY", "MOTHERSHIP":
		return "heavy"
	}
	return "light"
}

// VetLevel 按经验返回 1..5 级。
func VetLevel(kind string, xp float64) int {
	th := VetThresholds[vetClass(kind)]
	lv := 1
	for i := 1; i < len(th); i++ {
		if xp+1e-9 >= th[i] {
			lv = i + 1
		}
	}
	return lv
}

// KillVP 击杀一个单位的胜利分（v1.9.1：造价 ×2/3，封顶 KillVPCap=200；母舰固定 300）。
func KillVP(kind string) int {
	if kind == "MOTHERSHIP" {
		return MothershipKillVP
	}
	t, ok := TypesV15[kind]
	if !ok {
		t, ok = Types[kind]
	}
	if !ok {
		return 0
	}
	v := t.Cost * 2 / 3
	if v > KillVPCap {
		v = KillVPCap
	}
	if v < 1 {
		v = 1
	}
	return v
}

// addVS 统一写账，保证 Total 不漂移。kind: kills/relics/flags。
func (s *SideState) addVS(kind string, delta int) {
	switch kind {
	case "kills":
		s.VS.Kills += delta
	case "relics":
		s.VS.Relics += delta
	case "flags":
		s.VS.Flags += delta
	}
	s.VS.Total += delta
}

func (g *Game) v15() bool { return g.Cfg.Ruleset == RulesetV15 }

// popMax 返回当前规则集的人口上限。
func (g *Game) popMax() int {
	if g.v15() {
		return PopMaxV15
	}
	return PopMax
}

// techTable 返回当前规则集的科技表。
func (g *Game) techTable() map[string]TechDef {
	if g.v15() {
		return TechsV15
	}
	return Techs
}

func (g *Game) treeTable() any {
	if g.v15() {
		return TechTreesV15
	}
	return TechTrees
}

// RulesHashFor 按规则集选择指纹。v14 保持历史口径；v15 在 v14 基础上追加新常量。
func RulesHashFor(ruleset string, tickLimit int, vision Vision) string {
	if ruleset != RulesetV15 {
		return RulesHash(tickLimit, vision)
	}
	body := "v15|" + RulesVersionV15 + "|base:" + RulesHash(tickLimit, vision) +
		"|pop:" + itoa(PopMaxV15) +
		"|vs:" + itoa(VSWinScore) +
		"|relic:" + itoa(RelicValue) + "," + itoa(RelicVSCap) + "," + itoa(RelicSpawnTicks) + "," + itoa(RelicMaxGround) +
		"," + itoa(RelicLoadTicks) + "," + itoa(RelicDropTicks) + "," + fnum(RelicRadius) + "," + fnum(RelicCarrySpeedMul) +
		"|flag:" + itoa(FlagPlaceVP) + "," + itoa(FlagPenalty) + "," + itoa(FlagBounty) + "," + itoa(FlagHP) + "," + itoa(FlagPlaceTicks) +
		"|killcap:" + itoa(KillVPCap) + "|researchR:" + fnum(ResearchRadius) +
		"|siegeMul:" + fnum(MothershipSiegeMul) +
		"|mskill:" + itoa(MothershipKillVP) +
		"|leth:" + fnum(V15DamageMul) + "," + fnum(V15CEPMul) +
		"|drop:" + fnum(DropoffRadius) +
		"|weights:" + fmt.Sprint(WeightsV15) + "|fields:" + fmt.Sprint(FieldNamesV15) +
		"|types:" + fmt.Sprint(TypesV15) + "|producible:" + fmt.Sprint(ProducibleV15) +
		"|rps:" + rpsV15Fingerprint() +
		"|cep:" + mapFingerprintV15(CEPMax) +
		"|arc:" + mapFingerprintV15(ArcHalf) +
		"|traverse:" + mapFingerprintV15(TraverseRate) +
		"|vet:" + vetFingerprint() +
		"|techs:" + techFingerprintOf(TechsV15) +
		"|killvp:" + killVPFingerprint()
	return FNV1A(body)
}

func mapFingerprint(m map[string]float64) string {
	out := ""
	for _, k := range typeOrder() {
		if v, ok := m[k]; ok {
			out += k + ":" + fnum(v) + ";"
		}
	}
	return out
}

func rpsV15Fingerprint() string {
	out := ""
	for _, src := range typeOrderV15() {
		row := RPS[src]
		out += src + ":"
		for _, tgt := range typeOrderV15() {
			out += fmt.Sprintf("%v,", row[tgt])
		}
	}
	return out
}

func mapFingerprintV15(m map[string]float64) string {
	out := ""
	for _, k := range typeOrderV15() {
		if v, ok := m[k]; ok {
			out += k + ":" + fnum(v) + ";"
		}
	}
	return out
}

func vetFingerprint() string {
	out := ""
	for _, k := range []string{"light", "medium", "heavy"} {
		th := VetThresholds[k]
		out += k + ":"
		for _, v := range th {
			out += fnum(v) + ","
		}
		out += ";"
	}
	for _, t := range VetTitles {
		out += t + ","
	}
	out += "coef:0.6,0.04,0.03,0.15;"
	return out
}

func killVPFingerprint() string {
	out := ""
	for _, k := range typeOrderV15() {
		out += k + ":" + itoa(KillVP(k)) + ";"
	}
	return out
}

func killVPTable() map[string]int {
	m := map[string]int{}
	for _, k := range typeOrderV15() {
		m[k] = KillVP(k)
	}
	return m
}

// techFingerprintOf 与 techFingerprint 同口径，但可指定表（v15 含 C1-C3）。
func techFingerprintOf(table map[string]TechDef) string {
	out := ""
	for _, id := range techOrderV15() {
		d, ok := table[id]
		if !ok {
			continue
		}
		out += fmt.Sprintf("%s:%s,%s,%d,%d,%d,%v,%v,%v,%d,%v;", id, d.Tree, d.Name, d.Tier, d.Tick, d.CU, d.Req, d.ReqAny, d.Excl, d.ReqStored, d.Eff)
	}
	return out
}

func techOrderV15() []string {
	return append(append(techOrder(), "C1", "C2", "C3"), "R1", "R2", "R3", "R4")
}

// ---------------- 状态初始化 ----------------

func (g *Game) initV15() {
	g.V15 = &V15State{
		NextRelicID: 1,
		NextFlagID:  1,
		Rng:         NewRng(g.Seed ^ 0x5F3759DF),
	}
	g.V15.FlagBounty[0] = map[int]bool{}
	g.V15.FlagBounty[1] = map[int]bool{}
	g.V15.Sites = g.buildRelicSites()
	// 开局每方各有一件遗产，保证第一波运输必然发生。
	g.spawnRelicAt(0)
	g.spawnRelicAt(1)
}

// buildRelicSites 选 4 个镜像遗迹点：以地图中心对称，避开山、VP 与出生点。
func (g *Game) buildRelicSites() []PathPt {
	cx, cy := float64(World)/2, float64(World)/2
	dx, dy := float64(World)*0.10, float64(World)*0.06
	cands := [][2]float64{
		{cx - dx, cy - dy}, {cx + dx, cy + dy},
		{cx - dx, cy + dy}, {cx + dx, cy - dy},
	}
	// v1.6：遗迹点必须从双方出生点都能走到。否则会出现双方采矿机
	// 卡在河/崖前、1500 tick 0 遗产的无效平局（seed 20261121）。
	reach0 := g.reachFromSpawn(0)
	reach1 := g.reachFromSpawn(1)
	out := make([]PathPt, 0, 4)
	for _, c := range cands {
		x, y := g.nearestOpenPoint(c[0], c[1], 14*Cell)
		if !g.pointReachable(reach0, reach1, x, y) {
			// 这里必须写回外层 x/y：如果写成 `if x, y, ok := ...` 会在 if 作用域里
			// 重新声明同名变量，修补结果出了 if 就丢，遗迹点仍然不可达。
			if px, py, ok := g.findReachablePoint(c[0], c[1], 30*Cell, reach0, reach1); ok {
				x, y = px, py
			} else {
				// 最后退到最近的 VP（Connected 已保证 VP 可达）。
				x, y = g.nearestReachableVP(x, y, reach0, reach1)
			}
		}
		out = append(out, PathPt{X: x, Y: y})
	}
	return out
}

func (g *Game) reachFromSpawn(side int) []uint8 {
	sp := g.Map.SpawnCells[side]
	return reachSet(g.Map.Base, g.Map.Elev, Ci(sp[0], sp[1]), UnitSlope)
}

func (g *Game) pointReachable(reach0, reach1 []uint8, x, y float64) bool {
	gx, gy := GxOf(x), GyOf(y)
	if gx < 0 || gy < 0 || gx >= Grid || gy >= Grid {
		return false
	}
	i := Ci(gx, gy)
	if g.Map.Base[i] == 3 {
		return false
	}
	return reach0[i] != 0 && reach1[i] != 0
}

func (g *Game) findReachablePoint(x, y, maxR float64, reach0, reach1 []uint8) (float64, float64, bool) {
	for r := float64(0); r <= maxR; r += Cell {
		for k := 0; k < 32; k++ {
			a := float64(k) * math.Pi / 16
			px, py := x+math.Cos(a)*r, y+math.Sin(a)*r
			if g.pointReachable(reach0, reach1, px, py) {
				return px, py, true
			}
		}
	}
	return 0, 0, false
}

func (g *Game) nearestReachableVP(x, y float64, reach0, reach1 []uint8) (float64, float64) {
	var best *VPoint
	bd := math.Inf(1)
	for _, vp := range g.Vps {
		if !g.pointReachable(reach0, reach1, vp.X, vp.Y) {
			continue
		}
		d := math.Hypot(vp.X-x, vp.Y-y)
		if d < bd {
			bd = d
			best = vp
		}
	}
	if best != nil {
		return best.X, best.Y
	}
	return x, y
}

// nearestOpenPoint 从 (x,y) 向外螺旋找可通行格中心。
func (g *Game) nearestOpenPoint(x, y, maxR float64) (float64, float64) {
	ok := func(px, py float64) bool {
		gx, gy := GxOf(px), GyOf(py)
		if gx < 0 || gy < 0 || gx >= Grid || gy >= Grid {
			return false
		}
		return g.Map.Base[Ci(gx, gy)] != 3
	}
	if ok(x, y) {
		return x, y
	}
	for r := float64(Cell); r <= maxR; r += Cell {
		for k := 0; k < 16; k++ {
			a := float64(k) * math.Pi / 8
			px, py := x+math.Cos(a)*r, y+math.Sin(a)*r
			if ok(px, py) {
				return px, py
			}
		}
	}
	return x, y
}

// spawnRelicAt 在指定遗迹点放一件无主遗产（该点已有地面遗产则跳过）。
func (g *Game) spawnRelicAt(site int) bool {
	if g.V15 == nil || len(g.V15.Sites) == 0 {
		return false
	}
	s := g.V15.Sites[site%len(g.V15.Sites)]
	for _, r := range g.V15.Relics {
		if r.State == "ground" && math.Hypot(r.X-s.X, r.Y-s.Y) < Cell*2 {
			return false
		}
	}
	g.V15.Relics = append(g.V15.Relics, &Relic{
		ID: g.V15.NextRelicID, X: s.X, Y: s.Y, State: "ground",
	})
	g.V15.NextRelicID++
	return true
}

func (g *Game) groundRelics() int {
	n := 0
	for _, r := range g.V15.Relics {
		if r.State == "ground" {
			n++
		}
	}
	return n
}

// stepRelicSpawn 定期补遗产，场上最多 RelicMaxGround 件。
func (g *Game) stepRelicSpawn() {
	if g.Turn%RelicSpawnTicks != 0 || g.groundRelics() >= RelicMaxGround {
		return
	}
	for i := 0; i < len(g.V15.Sites); i++ {
		site := (g.V15.NextSite + i) % len(g.V15.Sites)
		if g.spawnRelicAt(site) {
			g.V15.NextSite = (site + 1) % len(g.V15.Sites)
			return
		}
	}
}

func (g *Game) relicByID(id int) *Relic {
	for _, r := range g.V15.Relics {
		if r.ID == id {
			return r
		}
	}
	return nil
}

func (g *Game) flagByID(id int) *Flag {
	for _, f := range g.V15.Flags {
		if f.ID == id {
			return f
		}
	}
	return nil
}

func (g *Game) flagAtVP(vpID int) *Flag {
	for _, f := range g.V15.Flags {
		if f.VPID == vpID {
			return f
		}
	}
	return nil
}

func (g *Game) sideFlagAtVP(side, vpID int) *Flag {
	f := g.flagAtVP(vpID)
	if f != nil && f.Side == side {
		return f
	}
	return nil
}

func (g *Game) unitByID(id int) *Unit {
	for _, u := range g.Units {
		if u.ID == id {
			return u
		}
	}
	return nil
}

// ---------------- 遗产运输 ----------------

// stepRelics 采矿机遗产状态机。由 stepEconomy 调用（在移动与开火之前）。
func (g *Game) stepRelics(side int) {
	for _, u := range g.Units {
		if u.Dead || u.Side != side || u.Kind != "SALVAGER" || u.Relic == nil {
			continue
		}
		r := g.relicByID(*u.Relic)
		if r == nil || r.State == "stored" {
			u.Relic, u.RState = nil, ""
			u.HState = "idle"
			continue
		}
		switch u.RState {
		case "", "toRelic":
			if r.State != "ground" {
				u.Relic, u.RState = nil, ""
				continue
			}
			u.RState = "toRelic"
			g.followPath(u, r.X, r.Y)
			if distXY(u.X, u.Y, r.X, r.Y) <= RelicRadius {
				u.RState = "loading"
				u.RelicTimer = 0
			}
		case "loading":
			if r.State != "ground" {
				u.RState = "toRelic"
				continue
			}
			u.RelicTimer++
			if u.RelicTimer >= RelicLoadTicks {
				r.State = "carried"
				r.CarrierID = u.ID
				r.X, r.Y = u.X, u.Y
				u.RState = "toDeposit"
				u.RelicTimer = 0
				g.emit(Event{Type: "relic-pick", Side: side, ID: r.ID, By: u.ID})
			}
		case "toDeposit":
			vp := g.nearestFriendlyFlagVP(side, u.X, u.Y)
			if vp == nil {
				// 没有己方旗：默认原地等；移动阶段仍接受 Bot move（v1.9.2），
				// 采矿机可以自己去无旗 VP 插旗，不丢货。
				g.applyMove(u, u.X, u.Y)
				continue
			}
			g.followPath(u, vp.X, vp.Y)
			if distXY(u.X, u.Y, vp.X, vp.Y) <= VPRadius {
				u.RState = "unloading"
				u.RelicTimer = 0
			}
		case "unloading":
			var vp *VPoint
			for _, v := range g.Vps {
				if distXY(u.X, u.Y, v.X, v.Y) <= VPRadius {
					vp = v
					break
				}
			}
			if vp == nil || g.sideFlagAtVP(side, vp.ID) == nil {
				// 目标旗没了或已离开圈：重新找。
				u.RState = "toDeposit"
				u.RelicTimer = 0
				continue
			}
			// v1.6：敌方战斗单位在圈内时，入库读条暂停（不重置）。
			if g.vpContestedByEnemy(side, vp) {
				if u.RelicBlockedTicks == 0 {
					g.emit(Event{Type: "relic-blocked", Side: side, ID: u.ID, Owner: vp.ID})
				}
				u.RelicBlockedTicks++
				continue
			}
			u.RelicBlockedTicks = 0
			u.RelicTimer++
			if u.RelicTimer >= RelicDropTicks {
				r.State = "stored"
				r.VPID = vp.ID
				r.CarrierID = 0
				r.X, r.Y = vp.X, vp.Y
				g.awardRelicVS(side, r)
				u.Relic, u.RState = nil, ""
				u.RelicTimer = 0
				u.RelicBlockedTicks = 0
				g.emit(Event{Type: "relic-in", Side: side, ID: r.ID, By: u.ID, Owner: vp.ID})
			}
		}
	}
}

// awardRelicVS v1.6：同一件遗产每方最多发一次 50 分，且每方遗产分不超过 100。
// 超过上限的遗产仍会入库并计入科技进度，但不再加分。
func (g *Game) awardRelicVS(side int, r *Relic) {
	S := g.Sides[side]
	if !r.DeliveredTo[side] {
		r.DeliveredTo[side] = true
		S.DeliveredRelics++
		S.VS.Delivered = S.DeliveredRelics
	}
	if r.AwardedVS[side] {
		return
	}
	if S.VS.Relics+RelicValue <= RelicVSCap {
		S.addVS("relics", RelicValue)
		r.AwardedVS[side] = true
	}
}

// isCombatKind 会阻止敌方入库的战斗单位；研究舰/采矿机/侦察车不算。
func isCombatKind(kind string) bool {
	switch kind {
	case "LAV", "AAV", "RAILGUN", "ARTILLERY", "CARRIER":
		return true
	}
	return false
}

// vpContestedByEnemy 返回 VP 圈内是否有敌方战斗单位。
func (g *Game) vpContestedByEnemy(side int, vp *VPoint) bool {
	for _, u := range g.Units {
		if u.Dead || u.Side == side || !isCombatKind(u.Kind) {
			continue
		}
		if distXY(u.X, u.Y, vp.X, vp.Y) <= VPRadius {
			return true
		}
	}
	return false
}

// relicsAtVP 统计某 VP 上库存（stored）的遗产件数。
func (g *Game) relicsAtVP(vpID int) int {
	n := 0
	for _, r := range g.V15.Relics {
		if r.State == "stored" && r.VPID == vpID {
			n++
		}
	}
	return n
}

// storedRelics 统计某方所有旗下库存遗产件数。
func (g *Game) storedRelics(side int) int {
	n := 0
	for _, f := range g.V15.Flags {
		if f.Side == side {
			n += g.relicsAtVP(f.VPID)
		}
	}
	return n
}

// overcharge v1.6：某一面己方旗下库存遗产超过 5 件时，每多 1 件给 +2% 伤害，封顶 +20%。
// 这是过载池，旗毁/遗产掉落后立即失效，不写入永久 Mods。
func (g *Game) overcharge(side int) float64 {
	if !g.v15() || g.V15 == nil {
		return 0
	}
	best := 0
	for _, f := range g.V15.Flags {
		if f.Side != side {
			continue
		}
		if n := g.relicsAtVP(f.VPID); n > best {
			best = n
		}
	}
	if best <= 5 {
		return 0
	}
	v := 0.02 * float64(best-5)
	if v > 0.20 {
		v = 0.20
	}
	return v
}

// nearestFriendlyFlagVP 找离 (x,y) 最近的、插着本方旗的 VP。
func (g *Game) nearestFriendlyFlagVP(side int, x, y float64) *VPoint {
	var best *VPoint
	bd := math.Inf(1)
	for _, vp := range g.Vps {
		if g.sideFlagAtVP(side, vp.ID) == nil {
			continue
		}
		d := math.Hypot(vp.X-x, vp.Y-y)
		if d < bd {
			bd = d
			best = vp
		}
	}
	return best
}

// dropRelic 把采矿机携带的遗产丢回地面（被击毁/主动取消时）。
func (g *Game) dropRelic(u *Unit) {
	if u.Relic == nil {
		return
	}
	if r := g.relicByID(*u.Relic); r != nil && r.State == "carried" && r.CarrierID == u.ID {
		r.State = "ground"
		r.CarrierID = 0
		r.VPID = 0
		r.X, r.Y = u.X, u.Y
	}
	u.Relic, u.RState = nil, ""
	u.RelicTimer = 0
	u.RelicBlockedTicks = 0
}

// ---------------- 插旗 ----------------

// stepFlags 采矿机在 VP 圈内静止 FlagPlaceTicks 后自动插旗。
func (g *Game) stepFlags() {
	for _, s := range g.sideOrder() {
		for _, u := range g.Units {
			if u.Dead || u.Side != s || u.Kind != "SALVAGER" {
				continue
			}
			if u.Harvest != nil {
				u.FlagTimer = 0
				continue
			}
			var at *VPoint
			for _, vp := range g.Vps {
				if math.Hypot(u.X-vp.X, u.Y-vp.Y) <= VPRadius {
					at = vp
					break
				}
			}
			if at == nil || g.flagAtVP(at.ID) != nil {
				u.FlagTimer = 0
				continue
			}
			// v1.6：必须基本静止，移动超过 1px 就重置读条。
			if u.FlagTimer == 0 || distXY(u.X, u.Y, u.FlagAX, u.FlagAY) > 1 {
				u.FlagAX, u.FlagAY = u.X, u.Y
				u.FlagTimer = 0
			}
			u.FlagTimer++
			if u.FlagTimer >= FlagPlaceTicks {
				g.placeFlag(u.Side, at)
				u.FlagTimer = 0
			}
		}
	}
}

func (g *Game) placeFlag(side int, vp *VPoint) *Flag {
	f := &Flag{
		ID: g.V15.NextFlagID, VPID: vp.ID, Side: side,
		X: vp.X, Y: vp.Y, HP: FlagHP, MaxHP: FlagHP,
	}
	g.V15.NextFlagID++
	g.V15.Flags = append(g.V15.Flags, f)
	// v1.6：插旗不再给胜利分；旗只是入库点，也是可被拔的风险资产。
	g.emit(Event{Type: "flag-up", Side: side, ID: f.ID, Owner: vp.ID})
	return f
}

// damageFlag 旗受伤；击毁时扣守方旗分、库存遗产掉回地面，攻击方得 50% 旗分。
func (g *Game) damageFlag(p *Projectile, f *Flag, dmg float64) {
	if f == nil || f.HP <= 0 {
		return
	}
	if p.ByUnitID != 0 {
		if u := g.unitByID(p.ByUnitID); u != nil {
			dmg *= g.vetDmgMul(u) * (1 + g.Sides[p.Side].Mods.Damage + g.overcharge(p.Side))
		}
	}
	f.HP -= dmg
	if f.HP > 0 {
		return
	}
	g.destroyFlag(f, p.Side)
}

func (g *Game) destroyFlag(f *Flag, bySide int) {
	owner := f.Side
	// v1.6：拔旗首先中断该 VP 上的遗产研究（进度与已投入 CU 损失）。
	g.cancelResearchAt(owner, f.VPID)
	// 守方每次失去 VP 都扣固定惩罚。
	g.Sides[owner].addVS("flags", -FlagPenalty)
	// 攻方同一 VP 只拿一次赏金，防止反复插拔刷分。
	if bySide >= 0 && bySide != owner && g.V15.FlagBounty[bySide] != nil && !g.V15.FlagBounty[bySide][f.VPID] {
		g.V15.FlagBounty[bySide][f.VPID] = true
		g.Sides[bySide].addVS("flags", FlagBounty)
		g.emit(Event{Type: "flag-bounty", Side: bySide, ID: f.ID, By: bySide, Owner: f.VPID})
	}
	// 该点库存遗产全部掉回地面，并逐件撤回已发给守方的遗产分。
	for _, r := range g.V15.Relics {
		if r.State == "stored" && r.VPID == f.VPID {
			if r.AwardedVS[owner] {
				g.Sides[owner].addVS("relics", -RelicValue)
				r.AwardedVS[owner] = false
			}
			r.State = "ground"
			r.VPID = 0
			r.CarrierID = 0
			r.X, r.Y = f.X, f.Y
		}
	}
	for i, x := range g.V15.Flags {
		if x.ID == f.ID {
			g.V15.Flags = append(g.V15.Flags[:i], g.V15.Flags[i+1:]...)
			break
		}
	}
	g.emit(Event{Type: "flag-down", Side: owner, ID: f.ID, By: bySide, Owner: f.VPID})
}

// ---------------- 研究舰 / 遗产科技 ----------------

// cancelResearch 取消某方当前研究；进度与已投入 CU 损失，已完成节点保留。
func (g *Game) cancelResearch(side int, reason string) {
	if g.V15 == nil || g.V15.Research[side] == nil {
		return
	}
	rs := g.V15.Research[side]
	if rs.Active {
		g.emit(Event{Type: "research-cancel", Side: side, Kind: rs.Node, Owner: rs.VPID, ID: rs.ShipID})
	}
	g.V15.Research[side] = nil
}

// cancelResearchAt 旗被拔时中断该 VP 上的研究。
func (g *Game) cancelResearchAt(side, vpID int) {
	if g.V15 == nil || g.V15.Research[side] == nil {
		return
	}
	if g.V15.Research[side].VPID == vpID {
		g.cancelResearch(side, "flag-down")
	}
}

// cancelResearchByShip 研究舰被击毁时中断其研究。
func (g *Game) cancelResearchByShip(side, shipID int) {
	if g.V15 == nil || g.V15.Research[side] == nil {
		return
	}
	if g.V15.Research[side].ShipID == shipID {
		g.cancelResearch(side, "ship-down")
	}
}

// nextRelicTech 返回该旗下下一项可研究的遗产科技。
func (g *Game) nextRelicTech(side, vpID int) (TechDef, string, bool) {
	stored := g.relicsAtVP(vpID)
	for _, id := range []string{"R1", "R2", "R3", "R4"} {
		d := TechsV15[id]
		if g.Sides[side].TechDone[id] {
			continue
		}
		okReq := true
		for _, req := range d.Req {
			if !g.Sides[side].TechDone[req] {
				okReq = false
				break
			}
		}
		if !okReq {
			continue
		}
		if stored < d.ReqStored {
			return TechDef{}, "", false
		}
		return d, id, true
	}
	return TechDef{}, "", false
}

// findResearchSpot 找一台停在「己方旗 + 库存遗产最多」VP 圈内的研究舰。
func (g *Game) findResearchSpot(side int) (*Unit, *VPoint) {
	bestN, bestShip, bestVP := 0, (*Unit)(nil), (*VPoint)(nil)
	for _, f := range g.V15.Flags {
		if f.Side != side {
			continue
		}
		vp := g.vpByID(f.VPID)
		if vp == nil {
			continue
		}
		n := g.relicsAtVP(f.VPID)
		if n < 2 {
			continue
		}
		for _, u := range g.Units {
			if u.Dead || u.Side != side || u.Kind != "RESEARCH" {
				continue
			}
			if distXY(u.X, u.Y, vp.X, vp.Y) > ResearchRadius {
				continue
			}
			if n > bestN || (n == bestN && bestShip != nil && u.ID < bestShip.ID) {
				bestN, bestShip, bestVP = n, u, vp
			}
		}
	}
	return bestShip, bestVP
}

func (g *Game) vpByID(id int) *VPoint {
	for _, vp := range g.Vps {
		if vp.ID == id {
			return vp
		}
	}
	return nil
}

// stepResearchShip 把被下达 relicresearch 的研究舰送到「己方旗 + 库存最多」的 VP。
// 引擎托管寻路，Bot 不需要自己算路线。
func (g *Game) stepResearchShip(u *Unit) {
	if g.V15 == nil || u == nil || u.Dead {
		return
	}
	var best *VPoint
	bestN, bestD := 0, math.Inf(1)
	for _, f := range g.V15.Flags {
		if f.Side != u.Side {
			continue
		}
		vp := g.vpByID(f.VPID)
		if vp == nil {
			continue
		}
		n := g.relicsAtVP(f.VPID)
		if n < 2 {
			continue
		}
		d := distXY(u.X, u.Y, vp.X, vp.Y)
		if n > bestN || (n == bestN && d < bestD) {
			bestN, bestD, best = n, d, vp
		}
	}
	if best == nil {
		return
	}
	if bestD <= ResearchRadius {
		return
	}
	g.followPath(u, best.X, best.Y)
}

// stepRelicResearch 研究舰状态机：同一时间只推进一个节点。
// 拔旗/舰毁取消；离开 VP 圈暂停；库存遗产低于节点要求取消。
func (g *Game) stepRelicResearch(side int) {
	if g.V15 == nil {
		return
	}
	S := g.Sides[side]
	if rs := g.V15.Research[side]; rs != nil && rs.Active {
		vp := g.vpByID(rs.VPID)
		ship := g.unitByID(rs.ShipID)
		if vp == nil || g.sideFlagAtVP(side, rs.VPID) == nil || ship == nil || ship.Dead {
			g.cancelResearch(side, "invalid")
			return
		}
		if distXY(ship.X, ship.Y, vp.X, vp.Y) > ResearchRadius {
			rs.Paused = true
			return
		}
		def := TechsV15[rs.Node]
		if g.relicsAtVP(rs.VPID) < def.ReqStored {
			g.cancelResearch(side, "relics-lost")
			return
		}
		rs.Paused = false
		rs.Progress++
		if rs.Progress+1e-9 >= rs.Total {
			g.applyTech(side, rs.Node)
			S.TechDone[rs.Node] = true
			g.emit(Event{Type: "research-done", Side: side, Kind: rs.Node, Owner: rs.VPID, ID: rs.ShipID})
			g.V15.Research[side] = nil
		}
		return
	}
	// 没有进行中的研究：尝试开新节点。
	ship, vp := g.findResearchSpot(side)
	if ship == nil || vp == nil {
		return
	}
	def, id, ok := g.nextRelicTech(side, vp.ID)
	if !ok || S.CU < float64(def.CU) {
		return
	}
	S.CU -= float64(def.CU)
	g.V15.Research[side] = &ResearchState{
		Node: id, VPID: vp.ID, ShipID: ship.ID,
		Progress: 0, Total: float64(def.Tick), Active: true, CUPaid: def.CU,
	}
	g.emit(Event{Type: "research-start", Side: side, Kind: id, Owner: vp.ID, ID: ship.ID})
}

// stepFlagControl 统计每方有旗 VP 的持有时长（计分卡字段 6）。
func (g *Game) stepFlagControl() {
	for _, f := range g.V15.Flags {
		if f.Side >= 0 && f.Side < 2 {
			g.Sides[f.Side].FlagControlTicks++
		}
	}
}

// ---------------- 胜利分 / 等级 ----------------

// addKillVS 击杀记账。byUnit 是开火单位（0 表示无归属，例如环境伤害）。
func (g *Game) addKillVS(bySide int, byUnit *Unit, victimKind string) {
	base := KillVP(victimKind)
	mul := 1.0
	if byUnit != nil {
		mul = g.vetVPMul(byUnit)
	}
	v := int(math.Round(float64(base) * mul))
	if v < 1 {
		v = 1
	}
	g.Sides[bySide].addVS("kills", v)
}

// addKillXP 只靠击杀获得经验（卡拉克沙漠口径：伤害/治疗不给经验）。
func (g *Game) addKillXP(u *Unit, victimKind string) {
	if u == nil || u.Dead {
		return
	}
	t := g.ut(victimKind)
	u.XP += float64(t.Cost) / 100.0
	if lv := VetLevel(u.Kind, u.XP); lv > u.Level {
		u.Level = lv
		g.emit(Event{Type: "levelup", Side: u.Side, ID: u.ID, Ready: lv, Kind: u.Kind})
	}
}

func (g *Game) vetDmgMul(u *Unit) float64 {
	if u == nil || u.Level <= 1 {
		return 1
	}
	return 1 + 0.04*float64(u.Level-1)
}

func (g *Game) vetDR(u *Unit) float64 {
	if u == nil || u.Level <= 1 {
		return 1
	}
	return 1 - 0.03*float64(u.Level-1)
}

func (g *Game) vetVPMul(u *Unit) float64 {
	if u == nil || u.Level <= 1 {
		return 1
	}
	return 1 + 0.15*float64(u.Level-1)
}

// vetCEPMul 等级带来的 CEP 缩减 = 科技缩减量的 60%（满级）。
// 没有火控科技时，等级不提供额外精度——这是「靠科技树提高的 60%」的字面实现。
func (g *Game) vetCEPMul(u *Unit) float64 {
	if u == nil || u.Level <= 1 {
		return 1
	}
	tech := g.Sides[u.Side].Mods.CEP
	return 1 - 0.6*tech*float64(u.Level-1)/4
}

// ---------------- 射击角度 / CEP ----------------

// aimV15 把炮塔转向目标，返回是否已对准且在车体射界内。
//
// 关键：炮塔方向不能在 180° 附近按「最短路径」反复翻符号，否则会在
// 正后方来回摆、永远对不准。这里锁定一个旋转方向，跨过目标时吸附。
func (g *Game) aimV15(u *Unit, tx, ty float64) bool {
	bearing := math.Atan2(ty-u.Y, tx-u.X) * 180 / math.Pi
	rate := TraverseRate[u.Kind]
	if rate <= 0 {
		return true
	}
	rate *= 1 + g.Sides[u.Side].Mods.Traverse
	d := angleDiff(u.Turret, bearing)
	if math.Abs(d) <= 6 {
		u.TurretDir = 0
		return g.arcOK(u, bearing)
	}
	if u.TurretDir == 0 {
		if d >= 0 {
			u.TurretDir = 1
		} else {
			u.TurretDir = -1
		}
	}
	next := math.Mod(u.Turret+float64(u.TurretDir)*rate+360, 360)
	nd := angleDiff(next, bearing)
	// 跨过目标方位时直接吸附；180° 附近先走出歧义区，避免误判。
	if math.Abs(d) <= 170 && d*nd <= 0 {
		u.Turret = bearing
		u.TurretDir = 0
		return g.arcOK(u, bearing)
	}
	u.Turret = next
	return false
}

func (g *Game) arcOK(u *Unit, bearing float64) bool {
	half := ArcHalf[u.Kind]
	if half > 0 && half < 180 && math.Abs(angleDiff(u.Facing, bearing)) > half {
		return false
	}
	return true
}

func (g *Game) applyCEP(u *Unit, x, y float64) (float64, float64) {
	base, ok := CEPMax[u.Kind]
	if !ok || base <= 0 {
		return x, y
	}
	rangeCells := Types[u.Kind].Range * (1 + g.Sides[u.Side].Mods.Range)
	if rangeCells <= 0 {
		return x, y
	}
	d := math.Hypot(x-u.X, y-u.Y) / Cell
	frac := Clamp(d/rangeCells, 0.35, 1)
	cep := base * frac * (1 - g.Sides[u.Side].Mods.CEP) * g.vetCEPMul(u) * V15CEPMul
	if cep <= 0 {
		return x, y
	}
	sigma := cep * Cell / 1.1774
	n1 := g.gauss()
	n2 := g.gauss()
	return x + n1*sigma, y + n2*sigma
}

// gauss Box-Muller，使用 v1.5 独立随机流。
func (g *Game) gauss() float64 {
	u1 := math.Max(1e-9, g.V15.Rng.Next())
	u2 := g.V15.Rng.Next()
	return math.Sqrt(-2*math.Log(u1)) * math.Cos(2*math.Pi*u2)
}

func angleDiff(a, b float64) float64 {
	d := math.Mod(a-b, 360)
	if d > 180 {
		d -= 360
	}
	if d < -180 {
		d += 360
	}
	return d
}

// ---------------- 帧/协议辅助 ----------------

func (g *Game) emit(ev Event) {
	ev.T = g.Turn
	g.LastEvents = append(g.LastEvents, ev)
	g.Events = append(g.Events, ev)
}

// checkEndV15 胜利分判定链；返回 true 表示已收局。
func (g *Game) checkEndV15(c0, c1 *Unit) bool {
	a, b := g.Sides[0], g.Sides[1]
	if a.VS.Total >= VSWinScore || b.VS.Total >= VSWinScore {
		if a.VS.Total > b.VS.Total {
			g.endGame(0, 2, "胜利分达到阈值")
		} else if b.VS.Total > a.VS.Total {
			g.endGame(1, 2, "胜利分达到阈值")
		} else {
			g.endGame(-1, 5, "双方同时达到胜利分阈值，判为平局")
		}
		return true
	}
	if g.Turn >= g.Cfg.TickLimit {
		if a.VS.Total != b.VS.Total {
			if a.VS.Total > b.VS.Total {
				g.endGame(0, 2, "时间到 · 按胜利分")
			} else {
				g.endGame(1, 2, "时间到 · 按胜利分")
			}
			return true
		}
		kd0, kd1 := a.Kills-a.Losses, b.Kills-b.Losses
		if kd0 != kd1 {
			if kd0 > kd1 {
				g.endGame(0, 3, "时间到 · 胜利分相同，按 KD 差")
			} else {
				g.endGame(1, 3, "时间到 · 胜利分相同，按 KD 差")
			}
			return true
		}
		h0, h1 := 0.0, 0.0
		if c0 != nil {
			h0 = c0.HP / c0.MaxHP
		}
		if c1 != nil {
			h1 = c1.HP / c1.MaxHP
		}
		if math.Abs(h0-h1) > 1e-6 {
			if h0 > h1 {
				g.endGame(0, 4, "时间到 · 按母舰剩余 HP")
			} else {
				g.endGame(1, 4, "时间到 · 按母舰剩余 HP")
			}
			return true
		}
		// v1.7.2：不再比遗产科技等级与唯一送达件数。
		// 这两项与「先手取遗产」高度相关，会把经济侧的几拍差放大成 57:43 的决胜偏置；
		// 在共享遗产供给的结构性问题解决前，只保留 KD → 母舰 HP → 旗控制。
		flagTol := int(math.Max(15, 0.05*float64(g.Turn)))
		if d := a.FlagControlTicks - b.FlagControlTicks; d > flagTol || d < -flagTol {
			if d > 0 {
				g.endGame(0, 3, "时间到 · 按旗控制时长")
			} else {
				g.endGame(1, 3, "时间到 · 按旗控制时长")
			}
			return true
		}
		g.endGame(-1, 5, "时间到 · 全部相同，平局")
		if g.ResultData != nil && a.VS.Kills == 0 && b.VS.Kills == 0 && a.VS.Relics == 0 && b.VS.Relics == 0 {
			g.ResultData.Invalid = true
			g.ResultData.InvalidReason = "no-progress: 0 kills, 0 relics at tick limit"
		}
		return true
	}
	return false
}

// computeCardsV15 七领域计分：字段 1 改为胜利分，字段 7 改为遗产吞吐。
func (g *Game) computeCardsV15() [2]Card {
	denom := math.Max(1, float64(g.Turn))
	var out [2]Card
	for s := 0; s < 2; s++ {
		S := g.Sides[s]
		cv := g.Mothership(s)
		aliveCost := 0.0
		for _, u := range g.Units {
			if u.Side == s && u.Kind != "MOTHERSHIP" {
				aliveCost += float64(g.ut(u.Kind).Cost)
			}
		}
		opp := g.Sides[1-s]
		f := []float64{
			Clamp(50+50*float64(S.VS.Total-opp.VS.Total)/float64(VSWinScore), 0, 100),
			100 * Clamp(float64(S.KillValue)/math.Max(1, float64(opp.ProducedCost)), 0, 1),
			100 * Clamp(aliveCost/math.Max(1, float64(S.ProducedCost)), 0, 1),
		}
		msHP := 0.0
		if cv != nil {
			msHP = Clamp(cv.HP/cv.MaxHP, 0, 1)
		}
		f = append(f,
			100*msHP,
			Clamp(50+10*float64(S.Kills-S.Losses), 0, 100),
			100*Clamp(float64(S.FlagControlTicks)/(denom*float64(len(g.Vps))), 0, 1),
			100*Clamp(float64(S.DeliveredRelics)/8.0, 0, 1),
		)
		total := 0.0
		for i := 0; i < 7; i++ {
			total += f[i] * WeightsV15[i]
		}
		out[s] = Card{Fields: f, Total: total, VPScore: S.VS.Total, Kills: S.Kills, Losses: S.Losses}
	}
	return out
}
