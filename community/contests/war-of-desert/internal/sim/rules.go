// 规则常数表（rules.js:58-236 的移植，v1.1 合并版语义）。
//
// 版本：v1.2（Desert Arena · Go 原生引擎 internal/sim）。
// 功能：单位/弹种/克制/模式/科技/经济全部常数与表格。
// 所有表在初始化后视为只读；wire 上的键名与 JS 保持一致。
package sim

import (
	"fmt"
	"math"
	"strconv"
)

// 时序（rules.js:58-71）。
const (
	// v1.1：600→1200（60s @ 50ms/tick）
	TickLimit = 1200
	TickMS    = 50
	DecideMS  = 120
	InitMS    = 2000
	// OrderLatency 是协议保证：本 tick 提交的指令在下一 tick 生效。
	OrderLatency = 1
	VPRadius     = 4.2 * Cell * Scale
	// v0.11：64KB→128KB（256² hello 实测约 110KB）
	MaxLine = 131072
)

// 单位类型表（rules.js:84-106）。speed/range/vision 以「格」标定，
// 初始化时 ×Scale 保持物理尺度；hp/dmg/cost 与网格无关。
type UnitType struct {
	HP    float64 `json:"hp"`
	Speed float64 `json:"speed"`
	Range float64 `json:"range"`
	Dmg   float64 `json:"dmg"`
	Rof   int     `json:"rof"`
	// 格（×Scale 后）
	Vision int `json:"vision"`
	// direct | lob | none
	Arc string `json:"arc"`
	// light | heavy
	Armor string `json:"armor"`
	// m/s 炮口初速
	V0 float64 `json:"v0"`
	// 仰角范围（deg）
	Elev  [2]int `json:"elev"`
	Cost  int    `json:"cost"`
	Build int    `json:"build"`
	Pop   int    `json:"pop"`
	// v1.1：高阶单位兼耗 RU
	RU int `json:"ru"`
	// siege 工事（WALL/TURRET）
	Structure bool `json:"structure,omitempty"`
}

func baseTypes() map[string]UnitType {
	return map[string]UnitType{
		"LAV":       {HP: 90, Speed: 3.2, Range: 3.2, Dmg: 8, Rof: 5, Vision: 9, Arc: "direct", Armor: "light", V0: 265, Elev: [2]int{-10, 28}, Cost: 100, Build: 25, Pop: 2},
		"AAV":       {HP: 260, Speed: 1.9, Range: 3.0, Dmg: 14, Rof: 6, Vision: 8, Arc: "direct", Armor: "heavy", V0: 256, Elev: [2]int{-10, 28}, Cost: 240, Build: 45, Pop: 3},
		"RAILGUN":   {HP: 110, Speed: 1.6, Range: 9.5, Dmg: 60, Rof: 20, Vision: 10, Arc: "direct", Armor: "light", V0: 490, Elev: [2]int{-8, 28}, Cost: 320, Build: 60, Pop: 3},
		"ARTILLERY": {HP: 400, Speed: 1.4, Range: 14, Dmg: 45, Rof: 30, Vision: 7, Arc: "lob", Armor: "light", V0: 628, Elev: [2]int{-5, 75}, Cost: 950, Build: 90, Pop: 5},
		"SALVAGER":  {HP: 160, Speed: 2.2, Range: 0, Dmg: 0, Rof: 0, Vision: 7, Arc: "none", Armor: "light", V0: 0, Elev: [2]int{0, 0}, Cost: 100, Build: 20, Pop: 1},
		"SCOUT":     {HP: 40, Speed: 4.6, Range: 0, Dmg: 0, Rof: 0, Vision: 16, Arc: "none", Armor: "light", V0: 0, Elev: [2]int{0, 0}, Cost: 80, Build: 15, Pop: 1},
		// v1.4 削弱：HP 3000→1800、Dmg 22→13、Speed 0.55→0.30。
		// 射程/射速/视野/装甲/弹道/四子系统全部保留——只降低战斗与机动数值，不减功能。
		"MOTHERSHIP": {HP: 1800, Speed: 0.30, Range: 7, Dmg: 13, Rof: 12, Vision: 14, Arc: "direct", Armor: "heavy", V0: 408, Elev: [2]int{-8, 30}, Cost: 0, Build: 0, Pop: 0},
		"CARRIER":    {HP: 800, Speed: 1.6, Range: 5, Dmg: 10, Rof: 15, Vision: 12, Arc: "direct", Armor: "heavy", V0: 337, Elev: [2]int{-10, 28}, Cost: 300, Build: 50, Pop: 4},
	}
}

var ruCost = map[string]int{
	"LAV": 0, "AAV": 30, "RAILGUN": 60, "ARTILLERY": 250,
	"SALVAGER": 0, "SCOUT": 0, "MOTHERSHIP": 0, "CARRIER": 100,
}

// Types 单位表（speed/range/vision 已 ×Scale；RU 已并入）。
var Types = buildTypes()

func buildTypes() map[string]UnitType {
	t := baseTypes()
	for k, v := range t {
		v.Speed *= Scale
		v.Range *= Scale
		floatVision := float64(v.Vision) * Scale
		v.Vision = int(floatVision)
		v.RU = ruCost[k]
		t[k] = v
	}
	return t
}

// 注意：Types 在包初始化后不得修改（对应 JS 的 deepFreeze）。

// Producible / SmallProducible 生产名单。
var (
	Producible      = []string{"LAV", "AAV", "RAILGUN", "ARTILLERY", "SALVAGER", "SCOUT", "CARRIER"}
	SmallProducible = []string{"LAV", "SALVAGER", "SCOUT"}
)

// RPS 兵种克制表（8×8；SCOUT/SALVAGER 行显式全 0，取值用存在性判断不用兜底 1）。
//
// v1.3 调整：RAILGUN/ARTILLERY 对母舰的倍率 0.8 → 2.5 / 2.0。
// 原倍率下「斩舰」在真实经济里不可达（母舰射程 7 格，只有这两种兵够得着；
// 单台 railgun 净输出 ~1.2 HP/tick，打穿 3000 HP 需要 2500 拍，而单局只有 1200 拍）。
// 现在攻坚兵真正承担拆舰角色：LAV/AAV 依旧啃不动母舰，必须换兵种。
var RPS = map[string]map[string]float64{
	"LAV":        {"LAV": 1.0, "AAV": 0.6, "RAILGUN": 1.8, "ARTILLERY": 1.6, "SALVAGER": 1.3, "SCOUT": 1.2, "MOTHERSHIP": 0.8, "CARRIER": 1.0, "RESEARCH": 1.0},
	"AAV":        {"LAV": 1.8, "AAV": 1.0, "RAILGUN": 0.7, "ARTILLERY": 0.9, "SALVAGER": 1.6, "SCOUT": 1.4, "MOTHERSHIP": 0.8, "CARRIER": 1.2, "RESEARCH": 1.0},
	"RAILGUN":    {"LAV": 0.6, "AAV": 1.8, "RAILGUN": 1.0, "ARTILLERY": 1.2, "SALVAGER": 0.9, "SCOUT": 0.8, "MOTHERSHIP": 2.5, "CARRIER": 1.3, "RESEARCH": 1.0},
	"ARTILLERY":  {"LAV": 0.8, "AAV": 1.1, "RAILGUN": 1.3, "ARTILLERY": 1.0, "SALVAGER": 1.0, "SCOUT": 1.4, "MOTHERSHIP": 2.0, "CARRIER": 1.1, "RESEARCH": 1.0},
	"SALVAGER":   {"LAV": 0, "AAV": 0, "RAILGUN": 0, "ARTILLERY": 0, "SALVAGER": 0, "SCOUT": 0, "MOTHERSHIP": 0, "CARRIER": 0, "RESEARCH": 0},
	"SCOUT":      {"LAV": 0, "AAV": 0, "RAILGUN": 0, "ARTILLERY": 0, "SALVAGER": 0, "SCOUT": 0, "MOTHERSHIP": 0, "CARRIER": 0, "RESEARCH": 0},
	"MOTHERSHIP": {"LAV": 1.2, "AAV": 1.0, "RAILGUN": 1.0, "ARTILLERY": 1.0, "SALVAGER": 1.2, "SCOUT": 1.3, "MOTHERSHIP": 1.0, "CARRIER": 1.1, "RESEARCH": 1.0},
	"CARRIER":    {"LAV": 0.8, "AAV": 0.6, "RAILGUN": 0.7, "ARTILLERY": 0.8, "SALVAGER": 1.0, "SCOUT": 1.0, "MOTHERSHIP": 0.7, "CARRIER": 1.0, "RESEARCH": 1.0},
	"RESEARCH":   {"LAV": 0, "AAV": 0, "RAILGUN": 0, "ARTILLERY": 0, "SALVAGER": 0, "SCOUT": 0, "MOTHERSHIP": 0, "CARRIER": 0, "RESEARCH": 0},
}

// 行动模式（rules.js:137-144）。
const (
	DefaultMode      = "obey"
	ModeRetreatCells = 2 * Scale
)

var Modes = []string{"obey", "neutral", "aggressive"}

// ModeDesc 模式说明（进 hello）。
var ModeDesc = map[string]string{
	"obey":       "服从：只执行 Bot 的 move/attack，不因视野内出现敌人而自行离开位置",
	"neutral":    "中间：按 Bot 意图行动，途中遇敌停步开火；被高差压制时后撤脱离，威胁消失后回归",
	"aggressive": "激进：Bot 只给方向，索敌/追击/占位由引擎接管，看得见就压上去打",
}

// 母舰四子系统（DoK F-62；rules.js:146-165）。
const (
	AllocStep     = 5
	SlotStep      = 5
	MaxSlots      = 3
	MaxWeapons    = 5
	ResearchSlots = 2
	ArmourReduce  = 0.5
	MSRepairRate  = 2
	// MSRepairDelay 母舰自愈的脱战门槛（tick）。
	// v1.3：持续挨打时不自愈——否则「斩舰」在数值上不可达（实测单局最多掉 87 点即回满）。
	MSRepairDelay = 60
	AttackDmgBase = 0.5
	RangeMulBase  = 0.7
	RangeMulSpan  = 0.6
)

// AllocKeys 四子系统键序（wire 与 stepAlloc 的过渡顺序）。
var AllocKeys = [...]string{"armour", "repair", "attack", "range"}

// StartAlloc 初始分配（四项各 25）。
const StartAllocValue = 25

// 经济与人口（rules.js:167-183）。
const (
	StartCU    = 500
	StartRU    = 0
	BaseIncome = 0
	// v1.2：母舰 RU 基础收入；v1.3 由 0.05 提到 0.20。
	// 0.05 时单局只能攒 60 RU，正好等于一台 RAILGUN 的价钱，等于把攻坚兵锁到第 1200 拍，
	// 「斩舰」在经济上不可达。0.20 让第一台攻坚兵约在第 300 拍解锁，仍远慢于轻甲。
	RUBaseIncome     = 0.20
	SalvageRate      = 3.0
	SalvagerCapacity = 200.0
	ResAmount        = 3000.0
	ResRadius        = 2.4 * Cell * Scale
	PopMax           = 24
	WeaponCost       = 120
	WeaponBuild      = 20
	WeaponQueueMax   = 5
	RepairRadius     = 4 * Cell * Scale
	RepairRate       = 2
)

// Alloc 四子系统分配值（固定四键，顺序即 AllocKeys）。
type Alloc struct {
	Armour int `json:"armour"`
	Repair int `json:"repair"`
	Attack int `json:"attack"`
	Range  int `json:"range"`
}

func (a Alloc) get(k string) int {
	switch k {
	case "armour":
		return a.Armour
	case "repair":
		return a.Repair
	case "attack":
		return a.Attack
	case "range":
		return a.Range
	}
	return 0
}

func (a *Alloc) set(k string, v int) {
	switch k {
	case "armour":
		a.Armour = v
	case "repair":
		a.Repair = v
	case "attack":
		a.Attack = v
	case "range":
		a.Range = v
	}
}

func startAlloc() Alloc {
	return Alloc{StartAllocValue, StartAllocValue, StartAllocValue, StartAllocValue}
}

// TechDef 科技节点（rules.js:195-223；v1.1 研究耗时 ×≈0.55）。
type TechDef struct {
	Tree   string   `json:"tree"`
	Tier   int      `json:"tier"`
	Name   string   `json:"name"`
	Tick   int      `json:"tick"`
	CU     int      `json:"cu"`
	Req    []string `json:"req,omitempty"`
	ReqAny []string `json:"reqAny,omitempty"`
	Excl   []string `json:"excl,omitempty"`
	// ReqStored v1.6：遗产科技节点需要的「同一面旗库存遗产」数量。
	// 普通科技为 0；遗产科技由研究舰自动推进，不走普通研究队列。
	ReqStored int            `json:"reqStored,omitempty"`
	Eff       map[string]any `json:"eff"`
}

var TechTrees = map[string]struct {
	Name   string `json:"name"`
	Kind   string `json:"kind"`
	Levels int    `json:"levels"`
	Desc   string `json:"desc"`
}{
	"fire":     {"火力校准", "linear", 3, "全程提升所有单位伤害"},
	"armor":    {"复合装甲", "linear", 3, "全程提升所有单位耐久"},
	"maneuver": {"机动学说", "cross", 5, "2 级分叉（速度/射程）→ 4 级汇合 → 5 级双修"},
	"industry": {"工业体系", "cross", 5, "2 级分叉（产能/采集）→ 4 级汇合 → 5 级双修"},
}

var Techs = map[string]TechDef{
	"F1":  {Tree: "fire", Tier: 1, Name: "改良发射药 I", Tick: 50, CU: 80, Eff: map[string]any{"muzzle": 0.10}},
	"F2":  {Tree: "fire", Tier: 2, Name: "高爆弹头", Tick: 85, CU: 140, Req: []string{"F1"}, Eff: map[string]any{"muzzle": 0.12, "unlock": "HE"}},
	"F3":  {Tree: "fire", Tier: 3, Name: "硬芯穿甲弹", Tick: 120, CU: 220, Req: []string{"F2"}, Eff: map[string]any{"muzzle": 0.15, "unlock": "APCR"}},
	"A1":  {Tree: "armor", Tier: 1, Name: "复合装甲 I", Tick: 50, CU: 80, Eff: map[string]any{"hp": 0.10}},
	"A2":  {Tree: "armor", Tier: 2, Name: "复合装甲 II", Tick: 85, CU: 140, Req: []string{"A1"}, Eff: map[string]any{"hp": 0.12}},
	"A3":  {Tree: "armor", Tier: 3, Name: "复合装甲 III", Tick: 120, CU: 220, Req: []string{"A2"}, Eff: map[string]any{"hp": 0.15}},
	"M1":  {Tree: "maneuver", Tier: 1, Name: "机动学说 I", Tick: 45, CU: 70, Eff: map[string]any{"speed": 0.05}},
	"M2a": {Tree: "maneuver", Tier: 2, Name: "机动 · 疾行", Tick: 65, CU: 110, Req: []string{"M1"}, Excl: []string{"M2b"}, Eff: map[string]any{"speed": 0.08}},
	"M2b": {Tree: "maneuver", Tier: 2, Name: "机动 · 增程", Tick: 65, CU: 110, Req: []string{"M1"}, Excl: []string{"M2a"}, Eff: map[string]any{"range": 0.08}},
	"M3a": {Tree: "maneuver", Tier: 3, Name: "机动 · 突进", Tick: 90, CU: 160, Req: []string{"M2a"}, Eff: map[string]any{"speed": 0.10}},
	"M3b": {Tree: "maneuver", Tier: 3, Name: "机动 · 校准", Tick: 90, CU: 160, Req: []string{"M2b"}, Eff: map[string]any{"range": 0.10}},
	"M4":  {Tree: "maneuver", Tier: 4, Name: "机动 · 协同", Tick: 115, CU: 200, ReqAny: []string{"M3a", "M3b"}, Eff: map[string]any{"rof": 0.10}},
	"M5":  {Tree: "maneuver", Tier: 5, Name: "机动 · 决胜", Tick: 150, CU: 260, Req: []string{"M4"}, Eff: map[string]any{"speed": 0.10, "range": 0.10}},
	"I1":  {Tree: "industry", Tier: 1, Name: "工业体系 I", Tick: 45, CU: 70, Eff: map[string]any{"income": 0.20}},
	"I2a": {Tree: "industry", Tier: 2, Name: "工业 · 流水线", Tick: 65, CU: 110, Req: []string{"I1"}, Excl: []string{"I2b"}, Eff: map[string]any{"produce": 0.25}},
	"I2b": {Tree: "industry", Tier: 2, Name: "工业 · 采掘", Tick: 65, CU: 110, Req: []string{"I1"}, Excl: []string{"I2a"}, Eff: map[string]any{"harvest": 0.30}},
	"I3a": {Tree: "industry", Tier: 3, Name: "工业 · 量产", Tick: 90, CU: 160, Req: []string{"I2a"}, Eff: map[string]any{"produce": 0.25}},
	"I3b": {Tree: "industry", Tier: 3, Name: "工业 · 精炼", Tick: 90, CU: 160, Req: []string{"I2b"}, Eff: map[string]any{"harvest": 0.30}},
	"I4":  {Tree: "industry", Tier: 4, Name: "工业 · 标准件", Tick: 115, CU: 200, ReqAny: []string{"I3a", "I3b"}, Eff: map[string]any{"cost": 0.10}},
	"I5":  {Tree: "industry", Tier: 5, Name: "工业 · 总动员", Tick: 150, CU: 260, Req: []string{"I4"}, Eff: map[string]any{"produce": 0.20, "income": 0.30}},
}

// 七领域确定性计分（v1.1：移除墙钟「算力表现」域，rules.js:74-75）。
var (
	Weights    = [7]float64{0.30, 0.20, 0.15, 0.15, 0.10, 0.05, 0.05}
	FieldNames = [7]string{"VP 控制", "歼敌", "保存兵力", "母舰完整度", "KD 差", "战场控制", "推进效率"}
)

// ProtoName / ProtoVersion 协议指纹（与 JS 一致保留 v5；hello 自描述规则块）。
const (
	ProtoName    = "wod-arena"
	ProtoVersion = 5
)

// RulesVersion 规则语义版本。
//
// 任何会改变对局语义的改动都必须递增——不只是数值常量，也包括代码逻辑
// （例如队列出队方式、索敌平局判定）。它参与 RulesHash，从而保证
// 「同一指纹 = 同一套规则」，审计时不必再靠版本号自证。
const RulesVersion = "1.4"

// SpeedMulOf 已废弃（母舰固定慢速），保留导出兼容。
func SpeedMulOf(pct int) float64 {
	return 0.4 + (1.6-0.4)*Clamp(float64(pct), 0, 100)/100
}

// RulesHash 规则指纹：fnv1a 覆盖 RulesVersion + 全部影响胜负的常量。
//
// 契约（v1.3 收紧）：
//   - 任何数值常量改动 → 指纹变；
//   - 任何语义改动 → 必须递增 RulesVersion，指纹随之变。
//
// 历史缺陷：v1.1→v1.2 新增 RUBaseIncome 与队列跳过式出队，但拼串漏了
// RUBaseIncome，两版共用 9874da0a，导致「按指纹审计」形同虚设。
func RulesHash(tickLimit int, vision Vision) string {
	body := fmt.Sprintf(
		"ver:%s|proto:%s.%d|grid:%d,%d,%d|slopes:%d,%d,%d,%d|vp:%.4f|tickLimit:%d|timing:%d,%d,%d,%d|"+
			"weights:%v|fields:%v|"+
			"alloc:%d,%d,%d,%d,%d,%v|economy:%v,%v,%v,%v,%v,%v,%d,%v,%v|"+
			"weapon:%d,%d,%d|repair:%v,%d,%d,%d|modes:%v,%s,%d|"+
			"mothership:%v,%v,%v,%v|"+
			"ballistics:%v,%v,%v,%v,%v,%v,%v,%v|los:%v,%v,%v,%v,%v|shells:%v|"+
			"types:%v|rps:%v|producible:%v,%v|techs:%v|vision:%v",
		RulesVersion, ProtoName, ProtoVersion, Grid, Cell, World,
		UnitSlope, CarrierSlope, MothershipSlope, PlatformSlope,
		VPRadius, tickLimit, TickMS, DecideMS, InitMS, OrderLatency,
		Weights, FieldNames,
		AllocStep, SlotStep, MaxSlots, MaxWeapons, ResearchSlots, startAlloc(),
		float64(StartCU), float64(StartRU), BaseIncome, SalvageRate, SalvagerCapacity, ResAmount, PopMax, RUBaseIncome, ResRadius,
		WeaponCost, WeaponBuild, WeaponQueueMax, RepairRadius, RepairRate, MSRepairDelay, MSRepairRate,
		Modes, DefaultMode, ModeRetreatCells,
		ArmourReduce, AttackDmgBase, RangeMulBase, RangeMulSpan,
		AirK, GravityG, MPerCell, ElevM, MountainH, HitRM, BallMaxT, UnitH,
		EyeH, LosTol, LosRangeCells, HeightBonusCells, HeightDiffMin,
		shellFingerprint(), Types, rpsFingerprint(), Producible, SmallProducible,
		techFingerprint(), vision,
	)
	return FNV1A(body)
}

func shellFingerprint() string {
	out := ""
	for _, id := range ShellIDs {
		sh := Shells[id]
		out += sh.ID + ":" +
			fnum(sh.Mass) + "," + fnum(sh.VMul) + "," + fnum(sh.Exp) + "," + fnum(sh.D0) + "," +
			fnum(sh.Armor.Light) + "," + fnum(sh.Armor.Heavy) + "," +
			fnum(sh.Splash) + "," + fnum(sh.SplashDmg) + "," + ptrStr(sh.Unlock) + ";"
	}
	return out
}

func fnum(v float64) string { return strconv.FormatFloat(v, 'g', -1, 64) }

func rpsFingerprint() string {
	out := ""
	for _, src := range typeOrder() {
		row := RPS[src]
		out += src + ":"
		for _, tgt := range typeOrder() {
			out += fmt.Sprintf("%v,", row[tgt])
		}
	}
	return out
}

func techFingerprint() string {
	out := ""
	for _, id := range techOrder() {
		d := Techs[id]
		out += fmt.Sprintf("%s:%s,%s,%d,%d,%d,%v,%v,%v,%v;", id, d.Tree, d.Name, d.Tier, d.Tick, d.CU, d.Req, d.ReqAny, d.Excl, d.Eff)
	}
	return out
}

// typeOrder / techOrder 固定枚举顺序（map 遍历无序，指纹必须确定）。
func typeOrder() []string {
	return []string{"LAV", "AAV", "RAILGUN", "ARTILLERY", "SALVAGER", "SCOUT", "MOTHERSHIP", "CARRIER"}
}

func techOrder() []string {
	return []string{"F1", "F2", "F3", "A1", "A2", "A3", "M1", "M2a", "M2b", "M3a", "M3b", "M4", "M5",
		"I1", "I2a", "I2b", "I3a", "I3b", "I4", "I5"}
}

func ptrStr(p *string) string {
	if p == nil {
		return "-"
	}
	return *p
}

// mathFloorInt = Math.floor；给 mapgen/rule 内部用。
func mathFloorInt(v float64) int { return int(math.Floor(v)) }
