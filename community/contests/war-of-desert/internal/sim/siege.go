// 攻守实验（siege）引擎 —— rules_siege.js（oracle 快照）的移植。
//
// 版本：v1.2（攻守变体独立演进，不动对称赛口径）。
// 功能：守方工事布置、矿场经济、攻守非对称计分与 BT 双参数模型。
//
// 与对称赛的关键差异：
//   - v1.0 冻结物理：0.1s 步长、母舰速度由 move 分配驱动、平台坡限 Math.max(lim, 55)
//     （未乘 Scale —— fork 的原样保留，不「顺手修复」）
//   - 分配键 produce/attack/research/move（生产线/研究槽/炮位/速度全由分配驱动）
//   - 经济：被动收入 1.5/tick + 矿场占领收入（无运输线、无 RU、无 harvest 指令）
//   - 守方布置阶段（fort 动作：WALL/TURRET），工事算单位（全灭判定含工事）
//   - 胜负：只看母舰是否被摧毁（攻方胜 L1 / 守方守到时限 L3 / 全灭 L2）
//   - 八领域计分（含墙钟「算力表现」，不参与确定性复核）
package sim

import (
	"encoding/json"
	"errors"
	"math"
	"sort"
)

// Siege 常量（rules_siege.js:41-78）。
const (
	// 63s：80 局实测攻方胜率 45.0%
	SiegeTickLimit      = 1260
	MineRate            = 0.8
	MineAmount          = 600.0
	MineSalvageBonus    = 1.2
	SiegeFortBudget     = 16
	SiegeFortCostWall   = 1
	SiegeFortCostTurret = 3
	SiegeFortRadius     = 34 * Cell
	SiegePlaceTicks     = 80
	// fork 保持 64KB（与对称赛 v0.11 的 128KB 不同）
	SiegeMaxLine = 65536
)

// SiegeIncome 攻守赛被动收入（/tick，进规则指纹与 stepEconomy）。
const SiegeIncome = 1.5

// SiegeWeights 八领域权重（第 8 域「算力表现」吃墙钟，复核豁免）。
var SiegeWeights = [8]float64{0.25, 0.20, 0.15, 0.15, 0.10, 0.05, 0.05, 0.05}

var SiegeFieldNames = [8]string{"VP 控制", "歼敌", "保存兵力", "母舰完整度", "KD 差", "战场控制", "推进效率", "算力表现"}

// SiegeAlloc v1.0 四键分配。
type SiegeAlloc struct {
	Produce  int `json:"produce"`
	Attack   int `json:"attack"`
	Research int `json:"research"`
	Move     int `json:"move"`
}

var siegeAllocKeys = [...]string{"produce", "attack", "research", "move"}

func siegeStartAlloc() SiegeAlloc { return SiegeAlloc{15, 35, 15, 35} }

func (a SiegeAlloc) get(k string) int {
	switch k {
	case "produce":
		return a.Produce
	case "attack":
		return a.Attack
	case "research":
		return a.Research
	case "move":
		return a.Move
	}
	return 0
}

func (a *SiegeAlloc) set(k string, v int) {
	switch k {
	case "produce":
		a.Produce = v
	case "attack":
		a.Attack = v
	case "research":
		a.Research = v
	case "move":
		a.Move = v
	}
}

// siegeBaseTypes v1.0 单位表（ARTILLERY 150 / SALVAGER 130，无 RU）+ 工事结构。
func siegeBaseTypes() map[string]UnitType {
	return map[string]UnitType{
		"LAV":        {HP: 90, Speed: 3.2, Range: 3.2, Dmg: 8, Rof: 5, Vision: 9, Arc: "direct", Armor: "light", V0: 265, Elev: [2]int{-10, 28}, Cost: 100, Build: 25, Pop: 2},
		"AAV":        {HP: 260, Speed: 1.9, Range: 3.0, Dmg: 14, Rof: 6, Vision: 8, Arc: "direct", Armor: "heavy", V0: 256, Elev: [2]int{-10, 28}, Cost: 240, Build: 45, Pop: 3},
		"RAILGUN":    {HP: 110, Speed: 1.6, Range: 9.5, Dmg: 60, Rof: 20, Vision: 10, Arc: "direct", Armor: "light", V0: 490, Elev: [2]int{-8, 28}, Cost: 320, Build: 60, Pop: 3},
		"ARTILLERY":  {HP: 150, Speed: 1.4, Range: 14, Dmg: 45, Rof: 30, Vision: 7, Arc: "lob", Armor: "light", V0: 628, Elev: [2]int{-5, 75}, Cost: 950, Build: 90, Pop: 5},
		"SALVAGER":   {HP: 130, Speed: 2.2, Range: 0, Dmg: 0, Rof: 0, Vision: 7, Arc: "none", Armor: "light", V0: 0, Elev: [2]int{0, 0}, Cost: 100, Build: 20, Pop: 1},
		"SCOUT":      {HP: 40, Speed: 4.6, Range: 0, Dmg: 0, Rof: 0, Vision: 16, Arc: "none", Armor: "light", V0: 0, Elev: [2]int{0, 0}, Cost: 80, Build: 15, Pop: 1},
		"MOTHERSHIP": {HP: 3000, Speed: 0.55, Range: 7, Dmg: 22, Rof: 12, Vision: 14, Arc: "direct", Armor: "heavy", V0: 408, Elev: [2]int{-8, 30}, Cost: 0, Build: 0, Pop: 0},
		"CARRIER":    {HP: 800, Speed: 1.6, Range: 5, Dmg: 10, Rof: 15, Vision: 12, Arc: "direct", Armor: "heavy", V0: 337, Elev: [2]int{-10, 28}, Cost: 300, Build: 50, Pop: 4},
		"WALL":       {HP: 520, Speed: 0, Range: 0, Dmg: 0, Rof: 0, Vision: 4, Arc: "none", Armor: "heavy", V0: 0, Elev: [2]int{0, 0}, Cost: 0, Build: 0, Pop: 0, Structure: true},
		"TURRET":     {HP: 280, Speed: 0, Range: 9, Dmg: 14, Rof: 10, Vision: 10, Arc: "direct", Armor: "heavy", V0: 300, Elev: [2]int{-8, 28}, Cost: 0, Build: 0, Pop: 0, Structure: true},
	}
}

// SiegeTypes 攻守单位表（已 ×Scale）。
var SiegeTypes = func() map[string]UnitType {
	t := siegeBaseTypes()
	for k, v := range t {
		v.Speed *= Scale
		v.Range *= Scale
		v.Vision = int(float64(v.Vision) * Scale)
		t[k] = v
	}
	return t
}()

var SiegeFortTypes = []string{"WALL", "TURRET"}

// siegeStepAllowed v1.0 坡限（无母舰专用档；平台放宽到 55 —— fork 原样）。
func siegeStepAllowed(m *GameMap, kind string, fx, fy, tx, ty float64) bool {
	gi := Ci(GxOf(tx), GyOf(ty))
	if m.Base[gi] == 3 {
		return false
	}
	hi := Ci(GxOf(fx), GyOf(fy))
	lim := float64(UnitSlope)
	if kind == "CARRIER" {
		lim = float64(CarrierSlope)
	}
	if m.Base[gi] == 4 || m.Base[hi] == 4 {
		lim = math.Max(lim, 55)
	}
	return math.Abs(float64(m.Elev[gi])-float64(m.Elev[hi])) <= lim
}

// SiegeCfg 攻守对局配置。
type SiegeCfg struct {
	TickLimit            int     `json:"tickLimit"`
	TickMS               int     `json:"tickMs"`
	DecideMS             int     `json:"decideMs"`
	InitMS               int     `json:"initMs"`
	Generator            string  `json:"generator"`
	Vision               Vision  `json:"vision"`
	MothershipHP         float64 `json:"mothershipHp"`
	AttackerMothershipHP float64 `json:"attackerMothershipHp"`
	FortBudget           int     `json:"fortBudget"`
	PlaceTicks           int     `json:"placeTicks"`
	AttackerCU           float64 `json:"attackerCu"`
	DefenderCU           float64 `json:"defenderCu"`
	AttackerStartLav     int     `json:"attackerStartLav"`
	DefenderStartLav     int     `json:"defenderStartLav"`
}

func (c SiegeCfg) withDefaults() SiegeCfg {
	if c.TickLimit <= 0 {
		c.TickLimit = SiegeTickLimit
	}
	if c.TickMS <= 0 {
		c.TickMS = TickMS
	}
	if c.DecideMS <= 0 {
		c.DecideMS = DecideMS
	}
	if c.InitMS <= 0 {
		c.InitMS = InitMS
	}
	if c.Generator == "" {
		c.Generator = "minecraft"
	}
	if c.MothershipHP <= 0 {
		c.MothershipHP = 1250
	}
	if c.AttackerMothershipHP <= 0 {
		c.AttackerMothershipHP = 1250
	}
	if c.FortBudget <= 0 {
		c.FortBudget = SiegeFortBudget
	}
	if c.PlaceTicks <= 0 {
		c.PlaceTicks = SiegePlaceTicks
	}
	if c.AttackerCU <= 0 {
		c.AttackerCU = 900
	}
	if c.DefenderCU <= 0 {
		c.DefenderCU = 700
	}
	if c.AttackerStartLav <= 0 {
		c.AttackerStartLav = 4
	}
	if c.DefenderStartLav <= 0 {
		c.DefenderStartLav = 3
	}
	c.Vision = NormalizeVision(c.Vision)
	return c
}

// SiegeSide 一方状态（守方 = side 0，攻方 = side 1）。
type SiegeSide struct {
	CU float64
	// defender | attacker
	Role         string
	Pop          int
	FortBudget   int
	FortsPlaced  int
	MineTicks    int
	MineIncome   float64
	MinesOwned   int
	Kills        int
	Losses       int
	Shots        int
	Hits         int
	KillValue    int
	ProducedCost int
	InitialCost  int
	ControlTicks int
	Alloc        SiegeAlloc
	Target       SiegeAlloc
	TechDone     map[string]bool
	TechDoing    []TechJob
	Queue        []string
	Active       []ProdJob
	Mode         string
	WeaponsReady int
	WeaponQueue  []WeaponJob
	Mods         Mods
}

// SiegeUnit 攻守单位（在 Unit 基础上加 structure 标记与 v1.0 语义字段）。
type SiegeUnit struct {
	ID         int
	Side       int
	Kind       string
	X, Y       float64
	HP         float64
	MaxHP      float64
	CD         float64
	Dead       bool
	Order      *Action
	Mode       string
	AITargetID int
	PX, PY     float64
	VX, VY     float64
	Queue      []ProdJob
	Structure  bool
}

// SiegeMine 矿场（位置沿用旧 VP 点）。
type SiegeMine struct {
	ID        int     `json:"id"`
	X         float64 `json:"x"`
	Y         float64 `json:"y"`
	Owner     int     `json:"owner"`
	Contested bool    `json:"contested"`
	Amount    float64 `json:"amount"`
}

// SiegeGame 攻守对局。
type SiegeGame struct {
	Cfg         SiegeCfg
	Vision      Vision
	Map         *GameMap
	Seed        int
	Turn        int
	Units       []*SiegeUnit
	NextID      int
	Mines       []*SiegeMine
	Sides       [2]*SiegeSide
	BotMS       [2]float64
	BotMax      [2]float64
	BotCalls    [2]int
	BotFaults   [2]int
	BotLate     [2]int
	Events      []Event
	LastEvents  []Event
	Projectiles []*Projectile
	NextShot    int
	ResultData  *SiegeResult
	Forfeited   int
	OrderLog    []OrderLogEntry
	FrameHashes []string
	Rejected    []Rejected
}

// SiegeResult 攻守终局（带 destroyed 与 roles 附加字段）。
type SiegeResult struct {
	Winner               int            `json:"winner"`
	Level                int            `json:"level"`
	Reason               string         `json:"reason"`
	Turn                 int            `json:"turn"`
	Seed                 int            `json:"seed"`
	Mode                 string         `json:"mode"`
	Players              int            `json:"players"`
	Turns                int            `json:"turns"`
	MapAttempt           int            `json:"mapAttempt"`
	Score                [2]int         `json:"score"`
	VP                   [2]int         `json:"vp"`
	Mines                []SiegeMineOut `json:"mines"`
	MothershipHP         float64        `json:"mothershipHp"`
	AttackerMothershipHP float64        `json:"attackerMothershipHp"`
	Roles                [2]string      `json:"roles"`
	Cards                [2]SiegeCard   `json:"cards"`
	Events               int            `json:"events"`
	Alloc                [2]SiegeAlloc  `json:"alloc"`
	Techs                [2][]string    `json:"techs"`
	Shots                [2]int         `json:"shots"`
	Hits                 [2]int         `json:"hits"`
	Timing               ResultTiming   `json:"timing"`
	Decide               [2]DecideStat  `json:"decide"`
	Destroyed            bool           `json:"destroyed"`
	DestroyedAt          *int           `json:"destroyedAt"`
}

type SiegeMineOut struct {
	ID     int `json:"id"`
	Owner  int `json:"owner"`
	Amount int `json:"amount"`
}

type SiegeCard struct {
	Fields     []float64 `json:"fields"`
	Total      float64   `json:"total"`
	VPScore    int       `json:"vpScore"`
	MineTicks  int       `json:"mineTicks"`
	MinesOwned int       `json:"minesOwned"`
	Kills      int       `json:"kills"`
	Losses     int       `json:"losses"`
	BotMS      float64   `json:"botMs"`
}

// NewSiegeGame 构造攻守对局。
func NewSiegeGame(seed int, cfg SiegeCfg) (*SiegeGame, error) {
	cfg = cfg.withDefaults()
	m, err := GenerateMap(seed, &GenOptions{Generator: cfg.Generator})
	if err != nil {
		return nil, err
	}
	g := &SiegeGame{
		Cfg: cfg, Vision: cfg.Vision, Map: m, Seed: m.Seed,
		NextID: 1, NextShot: 1, Forfeited: -1,
	}
	for _, c := range m.VPCells {
		g.Mines = append(g.Mines, &SiegeMine{
			ID:    len(g.Mines),
			X:     float64(c[0]*Cell) + Cell/2,
			Y:     float64(c[1]*Cell) + Cell/2,
			Owner: -1, Amount: MineAmount,
		})
	}
	for s := 0; s < 2; s++ {
		g.Sides[s] = &SiegeSide{
			Role: "defender", TechDone: map[string]bool{}, Mode: DefaultMode,
			Alloc: siegeStartAlloc(), Target: siegeStartAlloc(),
		}
		if s == 0 {
			g.Sides[s].CU = cfg.DefenderCU
			g.Sides[s].FortBudget = cfg.FortBudget
		} else {
			g.Sides[s].Role = "attacker"
			g.Sides[s].CU = cfg.AttackerCU
		}
	}
	// 初始兵力：守方 3 LAV + SALVAGER；攻方 4 LAV + SALVAGER（无工事）
	for s := 0; s < 2; s++ {
		sp := m.SpawnCells[s]
		cx := float64(sp[0]*Cell) + Cell/2
		cy := float64(sp[1]*Cell) + Cell/2
		g.spawn(s, "MOTHERSHIP", cx, cy)
		n := cfg.AttackerStartLav
		if s == 0 {
			n = cfg.DefenderStartLav
		}
		for i := 0; i < n; i++ {
			a := math.Pi*float64(s) + (float64(i)/math.Max(1, float64(n)))*math.Pi*2
			g.spawn(s, "LAV", cx+math.Cos(a)*Cell*2.8*Scale, cy+math.Sin(a)*Cell*2.8*Scale)
		}
		g.spawn(s, "SALVAGER",
			cx+math.Cos(math.Pi*float64(s)+1.2)*Cell*3.4*Scale,
			cy+math.Sin(math.Pi*float64(s)+1.2)*Cell*3.4*Scale)
	}
	g.FrameHashes = append(g.FrameHashes, g.HashFrame(nil))
	return g, nil
}

func (g *SiegeGame) Done() bool { return g.ResultData != nil }

func (g *SiegeGame) spawn(side int, kind string, x, y float64) *SiegeUnit {
	t := SiegeTypes[kind]
	S := g.Sides[side]
	maxHP := math.Round(t.HP * (1 + S.Mods.HP))
	if kind == "MOTHERSHIP" {
		if side == 0 {
			maxHP = g.Cfg.MothershipHP
		} else {
			maxHP = g.Cfg.AttackerMothershipHP
		}
	}
	u := &SiegeUnit{
		ID: g.NextID, Side: side, Kind: kind, X: x, Y: y,
		HP: maxHP, MaxHP: maxHP, Mode: S.Mode, PX: x, PY: y,
		Structure: t.Structure,
	}
	g.NextID++
	g.Units = append(g.Units, u)
	if kind != "MOTHERSHIP" && !t.Structure {
		S.Pop += t.Pop
		S.InitialCost += t.Cost
		S.ProducedCost += t.Cost
	}
	return u
}

func (g *SiegeGame) Mothership(side int) *SiegeUnit {
	for _, u := range g.Units {
		if u.Side == side && u.Kind == "MOTHERSHIP" && !u.Dead {
			return u
		}
	}
	return nil
}

func (g *SiegeGame) NoteBotTime(seat int, ms float64) {
	g.BotMS[seat] += ms
	g.BotCalls[seat]++
	if ms > g.BotMax[seat] {
		g.BotMax[seat] = ms
	}
	if ms > float64(g.Cfg.TickMS) {
		g.BotLate[seat]++
	}
}

func (g *SiegeGame) NoteFault(seat int) { g.BotFaults[seat]++ }

func (g *SiegeGame) noteRejected(seat, id int, reason string) {
	// Turn 口径与 Game 一致：记本应生效的 tick（g.Turn+1），见 game.go noteRejected。
	if len(g.Rejected) < 200 {
		g.Rejected = append(g.Rejected, Rejected{Turn: g.Turn + 1, Seat: seat, ID: id, Reason: reason})
	}
}

// placeFort 守方布置工事（非法放置只记账不判故障）。
func (g *SiegeGame) placeFort(s int, a *Action) {
	if s != 0 {
		g.noteRejected(s, 0, "fort: only defender")
		return
	}
	if g.Turn > g.Cfg.PlaceTicks+1 {
		g.noteRejected(s, 0, "fort: placement over")
		return
	}
	kind := a.Kind
	cost := SiegeFortCostWall
	if kind == "TURRET" {
		cost = SiegeFortCostTurret
	} else if kind != "WALL" {
		cost = 99
	}
	S := g.Sides[s]
	if S.FortBudget < cost {
		g.noteRejected(s, 0, "fort: no budget")
		return
	}
	m := g.Mothership(s)
	if m == nil {
		return
	}
	x, y := 0.0, 0.0
	if a.X != nil {
		x = *a.X
	}
	if a.Y != nil {
		y = *a.Y
	}
	if math.Hypot(x-m.X, y-m.Y) > SiegeFortRadius {
		g.noteRejected(s, 0, "fort: too far")
		return
	}
	gi := Ci(GxOf(x), GyOf(y))
	if g.Map.Base[gi] == 3 {
		g.noteRejected(s, 0, "fort: mountain")
		return
	}
	for _, u := range g.Units {
		if !u.Dead && math.Hypot(x-u.X, y-u.Y) < Cell*2 {
			g.noteRejected(s, 0, "fort: occupied")
			return
		}
	}
	u := g.spawn(s, kind, x, y)
	S.FortBudget -= cost
	S.FortsPlaced++
	ev := Event{T: g.Turn, Type: "fort", Side: s, Kind: kind, ID: u.ID}
	g.LastEvents = append(g.LastEvents, ev)
	g.Events = append(g.Events, ev)
}

// ---------------- 行为层（siege：attack 命令翻译成目标点） ----------------

func (g *SiegeGame) orderGoal(u *SiegeUnit, order *Action) (float64, float64, bool) {
	if order == nil {
		return 0, 0, false
	}
	if order.Type == "move" && order.X != nil && order.Y != nil {
		return *order.X, *order.Y, true
	}
	if order.Type == "attack" && order.TargetID != nil {
		for _, e := range g.Units {
			if e.ID == *order.TargetID && !e.Dead && e.Side != u.Side {
				return e.X, e.Y, true
			}
		}
	}
	return 0, 0, false
}

type siegeIntent struct {
	moveX, moveY float64
	hasMove      bool
	targetID     int
}

func (g *SiegeGame) nearestVisibleEnemy(u *SiegeUnit) (*SiegeUnit, float64) {
	best, bestD := (*SiegeUnit)(nil), math.Inf(1)
	for _, e := range g.Units {
		if e.Dead || e.Side == u.Side {
			continue
		}
		if g.Vision.Enabled && !g.canSeeBySide(u.Side, e.X, e.Y) {
			continue
		}
		d := math.Hypot(u.X-e.X, u.Y-e.Y)
		if d < bestD {
			bestD = d
			best = e
		}
	}
	if best == nil {
		return nil, 0
	}
	return best, bestD / Cell
}

func (g *SiegeGame) canSeeBySide(side int, tx, ty float64) bool {
	if !g.Vision.Enabled {
		return true
	}
	for _, u := range g.Units {
		if u.Dead || u.Side != side {
			continue
		}
		rad := 0
		if t, ok := SiegeTypes[u.Kind]; ok {
			rad = t.Vision
		}
		if g.Vision.Fog {
			rad += HeightBonusAt(g.Map, u.X, u.Y, tx, ty)
			if math.Hypot(u.X-tx, u.Y-ty) > float64(rad)*Cell {
				continue
			}
		}
		if !g.Vision.LOS || LosClear(g.Map, u.X, u.Y, tx, ty) {
			return true
		}
	}
	return false
}

func (g *SiegeGame) pointVisibleSide(side int, x, y float64) bool {
	if !g.Vision.Enabled {
		return true
	}
	for _, u := range g.Units {
		if u.Dead || u.Side != side {
			continue
		}
		rad := 0
		if t, ok := SiegeTypes[u.Kind]; ok {
			rad = t.Vision
		}
		if math.Hypot(u.X-x, u.Y-y) <= float64(rad)*Cell {
			return true
		}
	}
	return false
}

func (g *SiegeGame) clampToWorld(x, y float64) (float64, float64) {
	lo := Cell * 0.5
	hi := World - Cell*0.5
	return Clamp(x, lo, hi), Clamp(y, lo, hi)
}

func (g *SiegeGame) inWeaponRange(u, e *SiegeUnit) bool {
	t, ok := SiegeTypes[u.Kind]
	if !ok || t.Arc == "none" || t.Range <= 0 {
		return false
	}
	return math.Hypot(u.X-e.X, u.Y-e.Y)/Cell <= t.Range*(1+g.Sides[u.Side].Mods.Range)
}

func (g *SiegeGame) hasHeightAdvantage(u, e *SiegeUnit) bool {
	if e == nil {
		return false
	}
	myH := TerrainHeightAt(g.Map, u.X, u.Y)
	hisH := TerrainHeightAt(g.Map, e.X, e.Y)
	return myH-hisH >= HeightDiffMin
}

func (g *SiegeGame) unitIntent(u *SiegeUnit) siegeIntent {
	mode := u.Mode
	if mode == "" {
		mode = DefaultMode
	}
	order := u.Order
	goalX, goalY, hasGoal := g.orderGoal(u, order)
	canFight := false
	if t, ok := SiegeTypes[u.Kind]; ok {
		canFight = t.Arc != "none" && t.Range > 0 && t.Dmg > 0
	}
	if !canFight {
		return siegeIntent{moveX: goalX, moveY: goalY, hasMove: hasGoal}
	}
	if mode == "obey" {
		it := siegeIntent{}
		if order != nil && order.Type == "move" && order.X != nil {
			it.hasMove, it.moveX, it.moveY = true, *order.X, *order.Y
		}
		if order != nil && order.TargetID != nil {
			it.targetID = *order.TargetID
		}
		return it
	}
	foe, foeD := g.nearestVisibleEnemy(u)
	if mode == "neutral" {
		if foe == nil {
			return siegeIntent{moveX: goalX, moveY: goalY, hasMove: hasGoal}
		}
		if g.inWeaponRange(u, foe) {
			myH := TerrainHeightAt(g.Map, u.X, u.Y)
			hisH := TerrainHeightAt(g.Map, foe.X, foe.Y)
			if hisH-myH >= HeightDiffMin {
				dx, dy := u.X-foe.X, u.Y-foe.Y
				d := math.Hypot(dx, dy)
				if d == 0 {
					d = 1
				}
				px, py := g.clampToWorld(u.X+(dx/d)*ModeRetreatCells*Cell, u.Y+(dy/d)*ModeRetreatCells*Cell)
				return siegeIntent{moveX: px, moveY: py, hasMove: true, targetID: foe.ID}
			}
			return siegeIntent{targetID: foe.ID}
		}
		// 射程外：只有「这个敌人真能威胁到我」（视距内 + 自己占高）才停步
		near := foeD <= float64(SiegeTypes[u.Kind].Vision)
		it := siegeIntent{moveX: goalX, moveY: goalY, hasMove: hasGoal, targetID: foe.ID}
		if near && g.hasHeightAdvantage(u, foe) {
			it.hasMove = false
		}
		return it
	}
	// aggressive：命令只是目标
	if hasGoal {
		atGoal := math.Hypot(goalX-u.X, goalY-u.Y) <= Cell*0.75
		it := siegeIntent{targetID: 0}
		if !atGoal {
			it.hasMove, it.moveX, it.moveY = true, goalX, goalY
		}
		if foe != nil {
			it.targetID = foe.ID
		}
		return it
	}
	if foe == nil {
		return siegeIntent{}
	}
	if g.inWeaponRange(u, foe) {
		return siegeIntent{targetID: foe.ID}
	}
	vis := float64(SiegeTypes[u.Kind].Vision)
	if g.Vision.Enabled {
		vis += HeightBonusCells
	}
	if foeD > vis {
		return siegeIntent{}
	}
	keep := math.Max(1, SiegeTypes[u.Kind].Range*0.8) * Cell
	dx, dy := u.X-foe.X, u.Y-foe.Y
	d := math.Hypot(dx, dy)
	if d == 0 {
		d = 1
	}
	px, py := g.clampToWorld(foe.X+(dx/d)*keep, foe.Y+(dy/d)*keep)
	return siegeIntent{moveX: px, moveY: py, hasMove: true, targetID: foe.ID}
}

// ---------------- 分配 / 科技 / 经济 / 生产（v1.0 语义） ----------------

func slotsOfPct(pct int) int {
	return minInt(MaxSlots, int(math.Floor(Clamp(float64(pct), 0, 100)/SlotStep)))
}

func (g *SiegeGame) stepAlloc(side int) {
	S := g.Sides[side]
	budget := AllocStep
	moved := 0
	for _, k := range siegeAllocKeys {
		if budget <= 0 {
			break
		}
		surplus := S.Alloc.get(k) - S.Target.get(k)
		if surplus <= 0 {
			continue
		}
		take := minInt(surplus, budget)
		S.Alloc.set(k, S.Alloc.get(k)-take)
		budget -= take
		moved += take
	}
	for _, k := range siegeAllocKeys {
		if moved <= 0 {
			break
		}
		need := S.Target.get(k) - S.Alloc.get(k)
		if need <= 0 {
			continue
		}
		give := minInt(need, moved)
		S.Alloc.set(k, S.Alloc.get(k)+give)
		moved -= give
	}
}

func (g *SiegeGame) techAvailable(side int, id string) techCheck {
	def, ok := Techs[id]
	if !ok {
		return techCheck{false, "unknown tech"}
	}
	S := g.Sides[side]
	if S.TechDone[id] {
		return techCheck{false, "already researched"}
	}
	for _, t := range S.TechDoing {
		if t.ID == id {
			return techCheck{false, "already researching"}
		}
	}
	for _, r := range def.Req {
		if !S.TechDone[r] {
			return techCheck{false, "missing prereq " + r}
		}
	}
	if len(def.ReqAny) > 0 {
		any := false
		for _, r := range def.ReqAny {
			if S.TechDone[r] {
				any = true
				break
			}
		}
		if !any {
			return techCheck{false, "missing prereq (one of " + joinSlash(def.ReqAny) + ")"}
		}
	}
	for _, x := range def.Excl {
		if S.TechDone[x] {
			return techCheck{false, "excluded by " + x}
		}
		for _, t := range S.TechDoing {
			if t.ID == x {
				return techCheck{false, "excluded by " + x}
			}
		}
	}
	if len(S.TechDoing) >= slotsOfPct(S.Alloc.Research) {
		return techCheck{false, "no free research slot"}
	}
	if S.CU < float64(def.CU) {
		return techCheck{false, "not enough cu"}
	}
	return techCheck{true, ""}
}

func (g *SiegeGame) applyTech(side int, id string) {
	S := g.Sides[side]
	eff := Techs[id].Eff
	for _, k := range []string{"muzzle", "hp", "range", "speed", "rof", "income", "produce", "harvest", "cost"} {
		if v, ok := eff[k].(float64); ok {
			switch k {
			case "muzzle":
				S.Mods.Muzzle += v
			case "hp":
				S.Mods.HP += v
			case "range":
				S.Mods.Range += v
			case "speed":
				S.Mods.Speed += v
			case "rof":
				S.Mods.Rof += v
			case "income":
				S.Mods.Income += v
			case "produce":
				S.Mods.Produce += v
			case "harvest":
				S.Mods.Harvest += v
			case "cost":
				S.Mods.Cost += v
			}
		}
	}
	if _, has := eff["hp"]; has {
		for _, u := range g.Units {
			if u.Side != side || u.Dead {
				continue
			}
			ratio := u.HP / u.MaxHP
			// 母舰基础 HP 是实验变量，不能重算回 TYPES 的 3000
			baseHP := SiegeTypes[u.Kind].HP
			if u.Kind == "MOTHERSHIP" {
				if u.Side == 0 {
					baseHP = g.Cfg.MothershipHP
				} else {
					baseHP = g.Cfg.AttackerMothershipHP
				}
			}
			u.MaxHP = math.Round(baseHP * (1 + S.Mods.HP))
			u.HP = u.MaxHP * ratio
		}
	}
}

func (g *SiegeGame) stepResearch(side int) {
	S := g.Sides[side]
	var keep []TechJob
	for _, r := range S.TechDoing {
		r.Remain--
		if r.Remain <= 0 {
			S.TechDone[r.ID] = true
			g.applyTech(side, r.ID)
			ev := Event{T: g.Turn, Type: "tech", Side: side, Kind: r.ID}
			g.LastEvents = append(g.LastEvents, ev)
			g.Events = append(g.Events, ev)
		} else {
			keep = append(keep, r)
		}
	}
	S.TechDoing = keep
}

func (g *SiegeGame) stepEconomy(side int) {
	S := g.Sides[side]
	S.CU += SiegeIncome * (1 + S.Mods.Income)
	// 采集车：站在资源点圈内产 CU（siege 无资源点，此循环为 fork 保留的死路径）
	for _, u := range g.Units {
		if u.Dead || u.Side != side || u.Kind != "SALVAGER" {
			continue
		}
		_ = u
	}
}

func (g *SiegeGame) stepProduction(side int) {
	S := g.Sides[side]
	slots := slotsOfPct(S.Alloc.Produce)
	for len(S.Active) < slots && len(S.Queue) > 0 {
		kind := S.Queue[0]
		t := SiegeTypes[kind]
		cost := math.Round(float64(t.Cost) * (1 - S.Mods.Cost))
		if S.CU < cost {
			break
		}
		S.CU -= cost
		S.Queue = S.Queue[1:]
		S.Active = append(S.Active, ProdJob{Kind: kind, Remain: float64(t.Build)})
	}
	for i := range S.Active {
		S.Active[i].Remain -= 1 * (1 + S.Mods.Produce)
	}
	var finished []ProdJob
	var still []ProdJob
	for _, a := range S.Active {
		if a.Remain <= 0 {
			finished = append(finished, a)
		} else {
			still = append(still, a)
		}
	}
	S.Active = still
	if len(finished) == 0 {
		return
	}
	cv := g.Mothership(side)
	if cv == nil {
		return
	}
	for _, f := range finished {
		a := math.Mod(float64(g.Turn)*1.7+float64(g.NextID)*2.3, math.Pi*2)
		u := g.spawn(side, f.Kind, cv.X+math.Cos(a)*Cell*2.6*Scale, cv.Y+math.Sin(a)*Cell*2.6*Scale)
		ev := Event{T: g.Turn, Type: "spawn", Side: side, Kind: f.Kind, ID: u.ID}
		g.LastEvents = append(g.LastEvents, ev)
		g.Events = append(g.Events, ev)
	}
}

func (g *SiegeGame) stepWeapons(side int) {
	S := g.Sides[side]
	if len(S.WeaponQueue) == 0 {
		return
	}
	job := &S.WeaponQueue[0]
	job.Remain--
	if job.Remain > 0 {
		return
	}
	if S.CU < WeaponCost {
		job.Remain = 1
		return
	}
	S.CU -= WeaponCost
	S.WeaponQueue = S.WeaponQueue[1:]
	S.WeaponsReady++
	ev := Event{T: g.Turn, Type: "weapon", Side: side, Ready: S.WeaponsReady}
	g.LastEvents = append(g.LastEvents, ev)
	g.Events = append(g.Events, ev)
}

func (g *SiegeGame) stepCarriers(side int) {
	S := g.Sides[side]
	for _, c := range g.Units {
		if c.Dead || c.Side != side || c.Kind != "CARRIER" {
			continue
		}
		for _, u := range g.Units {
			if u.Dead || u.Side != side || u.HP >= u.MaxHP {
				continue
			}
			if math.Hypot(u.X-c.X, u.Y-c.Y) > RepairRadius {
				continue
			}
			u.HP = math.Min(u.MaxHP, u.HP+RepairRate)
		}
		if len(c.Queue) == 0 {
			continue
		}
		job := &c.Queue[0]
		cost := math.Round(float64(SiegeTypes[job.Kind].Cost) * (1 - S.Mods.Cost))
		if job.Remain == float64(SiegeTypes[job.Kind].Build) {
			if S.CU < cost {
				continue
			}
			S.CU -= cost
		}
		job.Remain--
		if job.Remain > 0 {
			continue
		}
		c.Queue = c.Queue[1:]
		a := math.Mod(float64(g.Turn)*2.1+float64(g.NextID)*1.7, math.Pi*2)
		u := g.spawn(side, job.Kind, c.X+math.Cos(a)*Cell*2*Scale, c.Y+math.Sin(a)*Cell*2*Scale)
		ev := Event{T: g.Turn, Type: "spawn", Side: side, Kind: job.Kind, ID: u.ID, By: c.ID}
		g.LastEvents = append(g.LastEvents, ev)
		g.Events = append(g.Events, ev)
	}
}

// ---------------- 移动 / 弹道 / 矿场 / 计分 / 终局 ----------------

func (g *SiegeGame) applyMove(u *SiegeUnit, tx, ty float64) {
	dx, dy := tx-u.X, ty-u.Y
	d := math.Hypot(dx, dy)
	if d < 2.5 {
		return
	}
	here := Ci(GxOf(u.X), GyOf(u.Y))
	aheadX := Clamp(math.Floor((u.X+(dx/d)*Cell)/Cell), 0, Grid-1)
	aheadY := Clamp(math.Floor((u.Y+(dy/d)*Cell)/Cell), 0, Grid-1)
	ahead := Ci(int(aheadX), int(aheadY))
	slope := (float64(g.Map.Elev[ahead]) - float64(g.Map.Elev[here])) / (24.0 / Scale)
	f := Clamp(1-0.55*math.Max(0, slope)+0.18*math.Max(0, -slope), 0.35, 1.30)
	b := int(g.Map.Base[here])
	if b < len(BaseSurface) && BaseSurface[b] != 0 {
		f *= BaseSurface[b]
	}
	S := g.Sides[u.Side]
	speed := SiegeTypes[u.Kind].Speed * (1 + S.Mods.Speed)
	// v1.0 冻结物理：母舰速度由 move 分配驱动；步长写死 0.1s（fork 快照原样）
	if u.Kind == "MOTHERSHIP" {
		speed *= SpeedMulOf(S.Alloc.Move)
	}
	travel := math.Min(speed*Cell*0.1*f, d)
	steps := int(math.Max(1, math.Ceil(travel/(Cell*0.5))))
	step := travel / float64(steps)
	for s := 0; s < steps; s++ {
		nx := u.X + (dx/d)*step
		ny := u.Y + (dy/d)*step
		if !siegeStepAllowed(g.Map, u.Kind, u.X, u.Y, nx, ny) {
			break
		}
		u.X = nx
		u.Y = ny
	}
}

func (g *SiegeGame) groundH(x, y float64) float64 {
	return ElevM * float64(g.Map.Elev[Ci(GxOf(x), GyOf(y))])
}

func (g *SiegeGame) solidH(x, y float64) float64 {
	if g.Map.Base[Ci(GxOf(x), GyOf(y))] == 3 {
		return MountainH
	}
	return 0
}

func (g *SiegeGame) muzzleH(u *SiegeUnit) float64 {
	return g.groundH(u.X, u.Y) + UnitH
}

func (g *SiegeGame) v0Of(u *SiegeUnit, shellID string) float64 {
	t := SiegeTypes[u.Kind]
	S := g.Sides[u.Side]
	sh, ok := Shells[shellID]
	if !ok {
		sh = Shells["AP"]
	}
	return t.V0 * sh.VMul * (1 + S.Mods.Muzzle)
}

func (g *SiegeGame) shellsOf(side int) []string {
	out := []string{}
	for _, id := range ShellIDs {
		req := Shells[id].Unlock
		if req == nil || g.Sides[side].TechDone[*req] {
			out = append(out, id)
		}
	}
	return out
}

func (g *SiegeGame) pickShell(side int, target *SiegeUnit) string {
	best, bestScore := "AP", -1.0
	for _, id := range g.shellsOf(side) {
		sh := Shells[id]
		armor := sh.ArmorOf(SiegeTypes[target.Kind].Armor)
		score := sh.D0*armor + boolF(sh.Splash > 0)*0.12
		if score > bestScore {
			bestScore = score
			best = id
		}
	}
	return best
}

type siegeShotSolution struct {
	ok      bool
	reason  string
	theta   float64
	t       float64
	vImpact float64
	apex    float64
	dx, dy  float64
}

func (g *SiegeGame) solveShot(u *SiegeUnit, shellID string, ex, ey, evx, evy float64) siegeShotSolution {
	t := SiegeTypes[u.Kind]
	h0 := g.muzzleH(u)
	v0 := g.v0Of(u, shellID)
	vCap := 12.0 * Cell
	lvx := Clamp(evx, -vCap, vCap)
	lvy := Clamp(evy, -vCap, vCap)
	dRaw := math.Hypot(ex-u.X, ey-u.Y)
	if dRaw < MPerPX*2 {
		return siegeShotSolution{ok: false, reason: "too-close"}
	}
	tGuess := 0.6
	var sol SolveResult
	dx, dy := 1.0, 0.0
	for it := 0; it < 2; it++ {
		offX, offY := lvx*tGuess, lvy*tGuess
		offLen := math.Hypot(offX, offY)
		maxOff := dRaw * 0.5
		k := 1.0
		if offLen > maxOff {
			k = maxOff / offLen
		}
		aimX := ex + offX*k
		aimY := ey + offY*k
		dPx := math.Hypot(aimX-u.X, aimY-u.Y)
		if dPx < MPerPX*2 {
			return siegeShotSolution{ok: false, reason: "too-close"}
		}
		dx = (aimX - u.X) / dPx
		dy = (aimY - u.Y) / dPx
		d := dPx * MPerPX
		dh := g.groundH(aimX, aimY) - g.groundH(u.X, u.Y)
		sol = Solve(SolveParams{
			V0: v0, H0: h0, D: d, DH: dh,
			ElevMin: float64(t.Elev[0]), ElevMax: float64(t.Elev[1]),
			GroundAt: func(sM float64) float64 {
				return g.solidH(u.X+dx*sM/MPerPX, u.Y+dy*sM/MPerPX)
			},
		})
		if !sol.OK {
			return siegeShotSolution{ok: false, reason: sol.Reason}
		}
		tGuess = sol.T
	}
	return siegeShotSolution{ok: true, theta: sol.Theta, t: sol.T, vImpact: sol.VImpact, apex: sol.Apex, dx: dx, dy: dy}
}

func (g *SiegeGame) weaponCount(u *SiegeUnit) int {
	if u.Kind == "MOTHERSHIP" {
		return g.Sides[u.Side].WeaponsReady
	}
	return 1
}

func (g *SiegeGame) fire(u, e *SiegeUnit) bool {
	t := SiegeTypes[u.Kind]
	S := g.Sides[u.Side]
	if g.weaponCount(u) <= 0 {
		return false
	}
	d := math.Hypot(u.X-e.X, u.Y-e.Y) / Cell
	if d > t.Range*(1+S.Mods.Range) {
		return false
	}
	shellID := g.pickShell(u.Side, e)
	sol := g.solveShot(u, shellID, e.X, e.Y, e.VX, e.VY)
	if !sol.ok {
		return false
	}
	n := int(math.Max(1, float64(g.weaponCount(u))))
	for i := 0; i < n; i++ {
		spread := 0.0
		if n > 1 {
			spread = (float64(i) - (float64(n)-1)/2) * 0.4
		}
		g.spawnShot(u, shellID, sol, spread, e.ID)
	}
	S.Shots += n
	return true
}

func (g *SiegeGame) spawnShot(u *SiegeUnit, shellID string, sol siegeShotSolution, spreadDeg float64, targetID int) {
	v0 := g.v0Of(u, shellID)
	st := MakeShellState(v0, sol.theta+spreadDeg, g.muzzleH(u))
	g.Projectiles = append(g.Projectiles, &Projectile{
		ID: g.NextShot, Side: u.Side, SrcKind: u.Kind, Shell: shellID,
		SX: u.X, SY: u.Y, DX: sol.dx, DY: sol.dy,
		S: 0, H: st.H, VX: st.Vx, VH: st.Vh, T: 0,
		V0: v0, TargetID: targetID,
	})
	g.NextShot++
}

func (g *SiegeGame) hitUnit(p *Projectile, target *SiegeUnit, factor float64) {
	t := SiegeTypes[p.SrcKind]
	dmg := DamageOf(p.Shell, t.Dmg, t.V0, p.VImpact, SiegeTypes[target.Kind].Armor)
	if row, ok := RPS[p.SrcKind]; ok {
		if mul, ok := row[target.Kind]; ok {
			dmg *= mul
		}
	}
	dmg *= factor
	g.damageUnit(target, dmg, p.Side)
}

func (g *SiegeGame) damageUnit(e *SiegeUnit, dmg float64, bySide int) {
	if e.Dead {
		return
	}
	e.HP -= dmg
	if e.HP > 0 {
		return
	}
	e.Dead = true
	def := SiegeTypes[e.Kind]
	if def.Pop > 0 {
		g.Sides[e.Side].Pop = int(math.Max(0, float64(g.Sides[e.Side].Pop-def.Pop)))
	}
	g.Sides[e.Side].Losses++
	g.Sides[bySide].Kills++
	g.Sides[bySide].KillValue += def.Cost
	ev := Event{T: g.Turn, Type: "kill", By: bySide, ID: e.ID, Kind: e.Kind}
	g.LastEvents = append(g.LastEvents, ev)
	g.Events = append(g.Events, ev)
}

func (g *SiegeGame) explode(p *Projectile, x, y float64, direct *SiegeUnit) {
	sh := Shells[p.Shell]
	p.Dead = true
	if direct != nil {
		g.hitUnit(p, direct, 1)
		g.Sides[p.Side].Hits++
	}
	if sh.Splash <= 0 {
		return
	}
	R := sh.Splash / MPerPX
	for _, u := range g.Units {
		if u.Dead || u == direct {
			continue
		}
		dPx := math.Hypot(u.X-x, u.Y-y)
		if dPx > R {
			continue
		}
		falloff := SplashFactor(dPx, R) * sh.SplashDmg
		friendMul := 1.0
		if u.Side == p.Side {
			friendMul = 0.5
		}
		g.hitUnit(p, u, falloff*friendMul)
	}
}

func (g *SiegeGame) stepShots() {
	if len(g.Projectiles) == 0 {
		return
	}
	dt := float64(TickMS) / 1000
	const SUB = 5
	for _, p := range g.Projectiles {
		if p.Dead {
			continue
		}
		for i := 0; i < SUB && !p.Dead; i++ {
			s0, h0 := p.S, p.H
			st := Advance(ShellState{S: p.S, H: p.H, Vx: p.VX, Vh: p.VH, T: p.T}, dt/SUB)
			p.S, p.H, p.VX, p.VH, p.T = st.S, st.H, st.Vx, st.Vh, st.T
			mp1 := p.S / MPerPX
			mp0 := s0 / MPerPX
			x1, y1 := p.SX+p.DX*mp1, p.SY+p.DY*mp1
			x0, y0 := p.SX+p.DX*mp0, p.SY+p.DY*mp0
			if solid := g.solidH(x1, y1); solid > 0 && p.H <= solid {
				p.VImpact = ShellSpeed(ShellState{Vx: p.VX, Vh: p.VH})
				g.explode(p, x1, y1, nil)
				break
			}
			if p.H <= 0 {
				p.VImpact = ShellSpeed(ShellState{Vx: p.VX, Vh: p.VH})
				g.explode(p, x1, y1, nil)
				break
			}
			var hit *SiegeUnit
			for _, u := range g.Units {
				if u.Dead || u.Side == p.Side {
					continue
				}
				ux, uy := u.X, u.Y
				vx, vy := x1-x0, y1-y0
				len2 := vx*vx + vy*vy
				tt := 0.0
				if len2 > 0 {
					tt = ((ux-x0)*vx + (uy-y0)*vy) / len2
				}
				tt = math.Max(0, math.Min(1, tt))
				cx, cy := x0+vx*tt, y0+vy*tt
				if math.Hypot(ux-cx, uy-cy) > HitRM/MPerPX {
					continue
				}
				hAt := h0 + (p.H-h0)*tt
				if math.Abs(hAt-(g.groundH(ux, uy)+UnitH)) > 6 {
					continue
				}
				hit = u
				break
			}
			if hit != nil {
				p.VImpact = ShellSpeed(ShellState{Vx: p.VX, Vh: p.VH})
				g.explode(p, hit.X, hit.Y, hit)
				break
			}
		}
		if p.T >= BallMaxT {
			p.Dead = true
		}
	}
	var alive []*Projectile
	for _, p := range g.Projectiles {
		if !p.Dead {
			alive = append(alive, p)
		}
	}
	g.Projectiles = alive
}

// updateMines 矿场结算（占控 + 收入）。
func (g *SiegeGame) updateMines() {
	for _, m := range g.Mines {
		c0, c1, salv := 0, 0, 0
		for _, u := range g.Units {
			if u.Dead || u.Kind == "MOTHERSHIP" || u.Structure {
				continue
			}
			if math.Hypot(u.X-m.X, u.Y-m.Y) <= VPRadius {
				if u.Side == 0 {
					c0++
				} else {
					c1++
				}
				if u.Kind == "SALVAGER" {
					salv++
				}
			}
		}
		g.Sides[0].ControlTicks += c0
		g.Sides[1].ControlTicks += c1
		prev := m.Owner
		m.Contested = c0 > 0 && c1 > 0
		m.Owner = -1
		if !m.Contested {
			if c0 > 0 {
				m.Owner = 0
			} else if c1 > 0 {
				m.Owner = 1
			}
		}
		if m.Owner >= 0 && m.Amount > 0 {
			S := g.Sides[m.Owner]
			rate := MineRate
			if salv > 0 {
				rate += MineSalvageBonus * float64(salv)
			}
			rate = math.Min(rate, m.Amount)
			S.CU += rate
			S.MineIncome += rate
			S.MineTicks++
			m.Amount -= rate
		}
		if prev != m.Owner {
			ev := Event{T: g.Turn, Type: "mine", ID: m.ID, Owner: m.Owner}
			g.LastEvents = append(g.LastEvents, ev)
			g.Events = append(g.Events, ev)
		}
	}
	for s := 0; s < 2; s++ {
		n := 0
		for _, m := range g.Mines {
			if m.Owner == s {
				n++
			}
		}
		g.Sides[s].MinesOwned = n
	}
}

// computeCards 八领域（第 8 域墙钟，复核豁免）。
func (g *SiegeGame) computeCards() [2]SiegeCard {
	denom := math.Max(1, float64(g.Turn))
	var out [2]SiegeCard
	for s := 0; s < 2; s++ {
		S := g.Sides[s]
		cv := g.Mothership(s)
		aliveCost := 0.0
		for _, u := range g.Units {
			if u.Side == s && u.Kind != "MOTHERSHIP" && !u.Structure {
				aliveCost += float64(SiegeTypes[u.Kind].Cost)
			}
		}
		avgBot := 0.0
		if g.BotCalls[s] > 0 {
			avgBot = g.BotMS[s] / float64(g.BotCalls[s])
		}
		limit := float64(g.Cfg.DecideMS)
		faultPenalty := math.Min(1, float64(g.BotFaults[s])*0.25)
		msHP := 0.0
		if cv != nil {
			msHP = Clamp(cv.HP/cv.MaxHP, 0, 1)
		}
		f := []float64{
			100 * Clamp(float64(S.MineTicks)/math.Max(1, float64(len(g.Mines))*denom), 0, 1),
			100 * Clamp(float64(S.KillValue)/math.Max(1, float64(g.Sides[1-s].InitialCost)), 0, 1),
			100 * Clamp(aliveCost/math.Max(1, float64(S.ProducedCost)), 0, 1),
			100 * msHP,
			Clamp(50+10*float64(S.Kills-S.Losses), 0, 100),
			100 * Clamp(float64(S.ControlTicks)/math.Max(1, denom*float64(len(g.Mines))*1.5), 0, 1),
			100 * Clamp(S.MineIncome/math.Max(1, denom*MineRate*float64(len(g.Mines))), 0, 1),
			100 * Clamp(1-avgBot/limit-faultPenalty, 0, 1),
		}
		total := 0.0
		for i := 0; i < 8; i++ {
			total += f[i] * SiegeWeights[i]
		}
		out[s] = SiegeCard{Fields: f, Total: total, VPScore: S.MineTicks, MineTicks: S.MineTicks,
			MinesOwned: S.MinesOwned, Kills: S.Kills, Losses: S.Losses, BotMS: avgBot}
	}
	return out
}

// EndGame 由裁判直接收局。
func (g *SiegeGame) EndGame(winner, level int, reason string) { g.endGame(winner, level, reason) }

func (g *SiegeGame) endGame(winner, level int, reason string) {
	if g.ResultData != nil {
		return
	}
	cards := g.computeCards()
	techs := [2][]string{sortStrings(keysOf(g.Sides[0].TechDone)), sortStrings(keysOf(g.Sides[1].TechDone))}
	var mines []SiegeMineOut
	for _, m := range g.Mines {
		mines = append(mines, SiegeMineOut{ID: m.ID, Owner: m.Owner, Amount: int(math.Round(m.Amount))})
	}
	r := &SiegeResult{
		Winner: winner, Level: level, Reason: reason, Turn: g.Turn, Seed: g.Seed,
		Mode: "siege", Players: 2, Turns: g.Turn, MapAttempt: g.Map.Attempt,
		Score:        [2]int{g.Sides[0].MinesOwned, g.Sides[1].MinesOwned},
		VP:           [2]int{0, 0},
		Mines:        mines,
		MothershipHP: g.Cfg.MothershipHP, AttackerMothershipHP: g.Cfg.AttackerMothershipHP,
		Roles: [2]string{"defender", "attacker"},
		Cards: cards, Events: len(g.Events),
		Alloc: [2]SiegeAlloc{g.Sides[0].Alloc, g.Sides[1].Alloc},
		Techs: techs,
		Shots: [2]int{g.Sides[0].Shots, g.Sides[1].Shots},
		Hits:  [2]int{g.Sides[0].Hits, g.Sides[1].Hits},
		Timing: ResultTiming{
			TickMS: g.Cfg.TickMS, DecideMS: g.Cfg.DecideMS, InitMS: g.Cfg.InitMS,
			OrderLatency: OrderLatency, WallMS: g.Turn * g.Cfg.TickMS,
		},
	}
	for s := 0; s < 2; s++ {
		d := DecideStat{Calls: g.BotCalls[s], Late: g.BotLate[s], Faults: g.BotFaults[s]}
		if g.BotCalls[s] > 0 {
			d.AvgMS = roundTo(g.BotMS[s]/float64(g.BotCalls[s]), 3)
		}
		d.MaxMS = roundTo(g.BotMax[s], 3)
		r.Decide[s] = d
	}
	r.Destroyed = g.Mothership(0) == nil
	if r.Destroyed {
		at := g.Turn
		r.DestroyedAt = &at
	}
	g.ResultData = r
}

func (g *SiegeGame) Forfeit(seat int, reason string) {
	if g.ResultData != nil {
		return
	}
	g.Forfeited = seat
	if reason == "" {
		reason = "故障出局：连续失败达到上限（红牌）"
	}
	g.endGame(1-seat, 0, reason)
}

func (g *SiegeGame) checkEnd() {
	if g.ResultData != nil {
		return
	}
	if g.Forfeited >= 0 {
		g.endGame(1-g.Forfeited, 0, "故障出局：失败达到上限（红牌）")
		return
	}
	c0, c1 := g.Mothership(0), g.Mothership(1)
	dead0 := c0 == nil || c0.HP <= 0
	dead1 := c1 == nil || c1.HP <= 0
	if dead0 && dead1 {
		g.endGame(-1, 0, "双方母舰同时被摧毁，判为平局")
		return
	}
	if dead0 {
		g.endGame(1, 1, "守方母舰被摧毁 · 进攻方胜")
		return
	}
	if dead1 {
		g.endGame(0, 1, "攻方母舰被摧毁 · 守方胜")
		return
	}
	alive0 := false
	alive1 := false
	for _, u := range g.Units {
		if !u.Dead {
			if u.Side == 0 {
				alive0 = true
			} else {
				alive1 = true
			}
		}
	}
	if !alive0 {
		g.endGame(1, 2, "守方单位全灭 · 进攻方胜")
		return
	}
	if !alive1 {
		g.endGame(0, 2, "攻方单位全灭 · 守方胜")
		return
	}
	if g.Turn >= g.Cfg.TickLimit {
		g.endGame(0, 3, "时间到 · 守方守住母舰")
	}
}

func (g *SiegeGame) Result() *SiegeResult {
	if g.ResultData == nil {
		g.checkEnd()
	}
	return g.ResultData
}

// normalizeSiegeOrders 归一化（fort 动作进 orderLog）。
func normalizeSiegeOrders(orders []Action) []Action {
	out := make([]Action, 0, len(orders))
	for _, a := range orders {
		switch a.Type {
		case "alloc":
			if a.SiegeAlloc != nil {
				ac := *a.SiegeAlloc
				out = append(out, Action{Type: "alloc", SiegeAlloc: &ac})
			} else {
				out = append(out, Action{Type: "alloc"})
			}
		case "produce":
			if a.By != nil {
				by := *a.By
				out = append(out, Action{Type: "produce", Kind: a.Kind, By: &by})
			} else {
				out = append(out, Action{Type: "produce", Kind: a.Kind})
			}
		case "research":
			out = append(out, Action{Type: "research", Tech: a.Tech})
		case "weapon":
			out = append(out, Action{Type: "weapon"})
		case "fort":
			out = append(out, Action{Type: "fort", Kind: a.Kind, X: a.X, Y: a.Y})
		case "mode":
			if a.ID != nil {
				id := *a.ID
				out = append(out, Action{Type: "mode", ID: &id, Mode: a.Mode})
			} else {
				out = append(out, Action{Type: "mode", Mode: a.Mode})
			}
		default:
			out = append(out, Action{Type: a.Type, ID: a.ID, X: a.X, Y: a.Y, TargetID: a.TargetID})
		}
	}
	return out
}

// （allocFieldOf 已删：siege alloc 归一化直接复制 SiegeAlloc 结构）

// Step 推进一回合（攻守版管线）。
func (g *SiegeGame) Step(orders [2][]Action) *Frame {
	if g.Done() {
		return g.buildFrame()
	}
	rec := [2][]Action{normalizeSiegeOrders(orders[0]), normalizeSiegeOrders(orders[1])}
	g.OrderLog = append(g.OrderLog, OrderLogEntry{T: g.Turn + 1, O: rec})
	g.Turn++
	g.LastEvents = nil

	// 1. 阵营动作
	for s := 0; s < 2; s++ {
		for i := range orders[s] {
			a := &orders[s][i]
			switch a.Type {
			case "alloc":
				if a.SiegeAlloc != nil {
					g.Sides[s].Target = *a.SiegeAlloc
				}
			case "produce":
				if a.By != nil {
					var host *SiegeUnit
					for _, u := range g.Units {
						if u.ID == *a.By && !u.Dead && u.Side == s && u.Kind == "CARRIER" {
							host = u
							break
						}
					}
					if host == nil {
						g.noteRejected(s, *a.By, "produce: carrier gone")
						continue
					}
					host.Queue = append(host.Queue, ProdJob{Kind: a.Kind, Remain: float64(SiegeTypes[a.Kind].Build)})
				} else {
					g.Sides[s].Queue = append(g.Sides[s].Queue, a.Kind)
				}
			case "weapon":
				S := g.Sides[s]
				if S.WeaponsReady+len(S.WeaponQueue) >= weaponCapOfAlloc(S.Alloc.Attack) {
					g.noteRejected(s, 0, "weapon: no free weapon slot")
					continue
				}
				S.WeaponQueue = append(S.WeaponQueue, WeaponJob{Remain: WeaponBuild})
			case "research":
				chk := g.techAvailable(s, a.Tech)
				if !chk.ok {
					g.noteRejected(s, 0, "research: "+chk.reason)
					continue
				}
				def := Techs[a.Tech]
				g.Sides[s].CU -= float64(def.CU)
				g.Sides[s].TechDoing = append(g.Sides[s].TechDoing, TechJob{ID: a.Tech, Remain: float64(def.Tick)})
				ev := Event{T: g.Turn, Type: "research-start", Side: s, Kind: a.Tech}
				g.LastEvents = append(g.LastEvents, ev)
				g.Events = append(g.Events, ev)
			case "fort":
				g.placeFort(s, a)
			case "mode":
				if !inList(Modes, a.Mode) {
					g.noteRejected(s, 0, "mode: unknown "+a.Mode)
					continue
				}
				if a.ID != nil {
					var one *SiegeUnit
					for _, u := range g.Units {
						if u.ID == *a.ID && !u.Dead && u.Side == s {
							one = u
							break
						}
					}
					if one == nil {
						g.noteRejected(s, *a.ID, "mode: unit gone")
						continue
					}
					one.Mode = a.Mode
				} else {
					g.Sides[s].Mode = a.Mode
					for _, u := range g.Units {
						if !u.Dead && u.Side == s {
							u.Mode = a.Mode
						}
					}
				}
			}
		}
	}

	// 2. 分配 → 经济 → 生产 → 运载舰 → 武器 → 研究
	for s := 0; s < 2; s++ {
		g.stepAlloc(s)
		g.stepEconomy(s)
		g.stepProduction(s)
		g.stepCarriers(s)
		g.stepWeapons(s)
		g.stepResearch(s)
	}

	// 3. 单位指令绑定 + 行为层 + 移动
	for s := 0; s < 2; s++ {
		byID := map[int]*Action{}
		for i := range orders[s] {
			a := &orders[s][i]
			if a.ID != nil {
				byID[*a.ID] = a
			}
		}
		for _, u := range g.Units {
			if !u.Dead && u.Side == s {
				u.Order = byID[u.ID]
			}
		}
	}
	for _, u := range g.Units {
		if u.Dead {
			continue
		}
		u.AITargetID = 0
		it := g.unitIntent(u)
		if it.hasMove {
			g.applyMove(u, it.moveX, it.moveY)
		}
		u.AITargetID = it.targetID
	}
	for _, u := range g.Units {
		if u.Dead {
			continue
		}
		u.VX = (u.X - u.PX) / (float64(TickMS) / 1000)
		u.VY = (u.Y - u.PY) / (float64(TickMS) / 1000)
		u.PX, u.PY = u.X, u.Y
	}
	// 4. 开火
	for _, u := range g.Units {
		if u.Dead {
			continue
		}
		t := SiegeTypes[u.Kind]
		if t.Arc == "none" || t.Range <= 0 {
			continue
		}
		if u.CD > 0 {
			u.CD--
			continue
		}
		if g.weaponCount(u) <= 0 {
			continue
		}
		var prefID int
		hasPref := false
		if u.Order != nil && u.Order.TargetID != nil {
			prefID, hasPref = *u.Order.TargetID, true
		} else if u.AITargetID != 0 {
			prefID, hasPref = u.AITargetID, true
		}
		var pref *SiegeUnit
		if hasPref {
			for _, e := range g.Units {
				if e.ID == prefID && !e.Dead && e.Side != u.Side {
					pref = e
					break
				}
			}
		}
		fired := false
		if pref != nil && (!g.Vision.Enabled || g.canSeeBySide(u.Side, pref.X, pref.Y)) {
			fired = g.fire(u, pref)
		}
		if !fired {
			lim := t.Range * (1 + g.Sides[u.Side].Mods.Range)
			type candT struct {
				e   *SiegeUnit
				d   float64
				adx float64
				ady float64
			}
			var cands []candT
			for _, e := range g.Units {
				if e.Dead || e.Side == u.Side {
					continue
				}
				dx, dy := u.X-e.X, u.Y-e.Y
				d := math.Hypot(dx, dy) / Cell
				if d > lim {
					continue
				}
				if g.Vision.Enabled && !g.canSeeBySide(u.Side, e.X, e.Y) {
					continue
				}
				cands = append(cands, candT{e, d, math.Abs(dx), math.Abs(dy)})
			}
			// 同对称赛：平局判定用 |dx|、|dy|，不用全局 ID（镜像协变性）
			sort.SliceStable(cands, func(a, b int) bool {
				if cands[a].d != cands[b].d {
					return cands[a].d < cands[b].d
				}
				if cands[a].adx != cands[b].adx {
					return cands[a].adx < cands[b].adx
				}
				return cands[a].ady < cands[b].ady
			})
			for i := 0; i < len(cands) && i < 3 && !fired; i++ {
				fired = g.fire(u, cands[i].e)
			}
		}
		if fired {
			rof := float64(t.Rof) * (1 - math.Min(0.5, g.Sides[u.Side].Mods.Rof))
			u.CD = math.Max(1, rof)
		}
	}
	// 5. 炮弹 + 清尸 + 矿场 + 终局 + 帧哈希
	g.stepShots()
	var alive []*SiegeUnit
	for _, u := range g.Units {
		if !u.Dead {
			alive = append(alive, u)
		}
	}
	g.Units = alive
	g.updateMines()
	g.checkEnd()
	f := g.buildFrame()
	g.FrameHashes = append(g.FrameHashes, g.HashFrame(f))
	return f
}

// weaponCapOfAlloc v1.0：attack 分配决定炮位上限（每 20% 一门，封顶 5）。
func weaponCapOfAlloc(attackPct int) int {
	return minInt(MaxWeapons, int(math.Floor(Clamp(float64(attackPct), 0, 100)/20)))
}

// ---------------- 编解码（siege 版 hello/tick/frame/decode） ----------------

func (g *SiegeGame) buildFrame() *Frame {
	f := &Frame{Turn: g.Turn}
	for _, u := range g.Units {
		f.Units = append(f.Units, FrameUnit{
			ID: u.ID, Side: u.Side, Kind: u.Kind,
			X: int(math.Round(u.X)), Y: int(math.Round(u.Y)),
			HP: int(math.Round(u.HP)), MaxHP: u.MaxHP, Mode: u.Mode,
		})
	}
	// siege 帧：vp 槽放矿场归属
	for _, m := range g.Mines {
		f.VP = append(f.VP, FrameVP{ID: m.ID, Owner: m.Owner})
	}
	for s := 0; s < 2; s++ {
		f.Score[s] = FrameScore{VP: g.Sides[s].MinesOwned, Kills: g.Sides[s].Kills, Losses: g.Sides[s].Losses}
		f.CU[s] = int(math.Round(g.Sides[s].CU))
		f.RU[s] = 0
		f.Alloc[s] = [4]int{g.Sides[s].Alloc.Produce, g.Sides[s].Alloc.Attack, g.Sides[s].Alloc.Research, g.Sides[s].Alloc.Move}
		f.Techs[s] = sortStrings(keysOf(g.Sides[s].TechDone))
		for _, a := range g.Sides[s].Active {
			f.Queue[s] = append(f.Queue[s], a.Kind)
		}
		f.Weapons[s] = g.Sides[s].WeaponsReady
	}
	for _, p := range g.Projectiles {
		f.Shells = append(f.Shells, FrameShell{
			ID: p.ID, Side: p.Side,
			X: int(math.Round(p.SX + p.DX*p.S/MPerPX)),
			Y: int(math.Round(p.SY + p.DY*p.S/MPerPX)),
			H: int(math.Round(p.H)),
		})
	}
	return f
}

func (g *SiegeGame) BuildFrame() *Frame { return g.buildFrame() }

func (g *SiegeGame) HashFrame(f *Frame) string {
	if f == nil {
		f = g.buildFrame()
	}
	b, _ := json.Marshal(f)
	return FNV1A(string(b))
}

// EncodeHello 攻守 hello（mode/roles/mines + siege 规则块）。
func (g *SiegeGame) EncodeHello() string {
	layers := EncodeMapLayers(g.Map)
	spawns := [][2]int{}
	for _, s := range g.Map.SpawnCells {
		spawns = append(spawns, s)
	}
	mines := []map[string]any{}
	for _, m := range g.Mines {
		mines = append(mines, map[string]any{"id": m.ID, "x": m.X, "y": m.Y, "amount": int(math.Round(m.Amount))})
	}
	rules := map[string]any{
		"tickLimit": g.Cfg.TickLimit, "vpRadius": VPRadius,
		"unitSlope": UnitSlope, "carrierSlope": CarrierSlope,
		"grid": Grid, "cellPx": Cell, "scale": Scale, "platformSlope": PlatformSlope,
		"timing": map[string]any{
			"tickMs": g.Cfg.TickMS, "decideMs": g.Cfg.DecideMS, "initMs": g.Cfg.InitMS,
			"orderLatency": OrderLatency,
		},
		"ballistics": map[string]any{
			"airK": AirK, "gravity": GravityG, "mPerCell": MPerCell, "elevM": ElevM,
			"mountainH": MountainH, "hitRadius": HitRM,
			"shells": Shells, "shellIds": ShellIDs,
		},
		"types": SiegeTypes, "producible": Producible, "smallProducible": SmallProducible,
		"fortTypes":  SiegeFortTypes,
		"fortCost":   map[string]int{"WALL": SiegeFortCostWall, "TURRET": SiegeFortCostTurret},
		"fortRadius": SiegeFortRadius,
		"alloc": map[string]any{
			"keys": siegeAllocKeys, "step": AllocStep, "start": siegeStartAlloc(),
			"slotStep": SlotStep, "maxSlots": MaxSlots, "maxWeapons": MaxWeapons,
		},
		"economy": map[string]any{
			"startCu": StartCU, "income": 1.5, "salvage": 0, "resAmount": 0,
			"resRadius": ResRadius, "popMax": PopMax,
			"mineRate": MineRate, "mineAmount": MineAmount, "mineSalvage": MineSalvageBonus, "mineRadius": VPRadius,
		},
		"weapon": map[string]any{"cost": WeaponCost, "build": WeaponBuild, "queueMax": WeaponQueueMax},
		"repair": map[string]any{"radius": RepairRadius, "rate": RepairRate},
		"vision": map[string]any{
			"enabled": g.Vision.Enabled, "fog": g.Vision.Fog, "los": g.Vision.LOS,
			"eyeH": EyeH, "losTol": LosTol, "losRange": LosRangeCells,
			"heightBonus": HeightBonusCells, "heightDiffMin": HeightDiffMin,
		},
		"modes": Modes, "defaultMode": DefaultMode,
		"modeRetreatCells": ModeRetreatCells,
		"techs":            Techs, "trees": TechTrees,
		"fortBudget": g.Cfg.FortBudget, "placeTicks": g.Cfg.PlaceTicks,
	}
	hello := map[string]any{
		"version": ProtoVersion, "proto": ProtoName, "protoVersion": ProtoVersion,
		"rulesHash": SiegeRulesHash(g.Cfg),
		"game":      "Desert Arena · Referee MVP", "seed": g.Seed, "players": 2,
		"generator": g.Map.Generator, "mapAttempt": g.Map.Attempt,
		"w": Grid, "h": Grid, "cell": Cell, "world": World,
		"map": layers, "spawns": spawns,
		"mode": "siege", "roles": []string{"defender", "attacker"},
		"mines": mines, "res": []any{}, "vp": []any{},
		"rules": rules,
	}
	b, _ := json.Marshal(hello)
	return `{"type":"hello",` + string(b)[1:] + "\n"
}

// SiegeRulesHash 攻守规则指纹（含 siege 专属常量与 HP 配置）。
func SiegeRulesHash(cfg SiegeCfg) string {
	c := cfg.withDefaults()
	body := "siege:" + ProtoName + "." + itoa(ProtoVersion) +
		"|tick:" + itoa(c.TickLimit) + "," + itoa(c.TickMS) + "," + itoa(c.DecideMS) + "," + itoa(c.InitMS) +
		"|slopes:" + itoa(UnitSlope) + "," + itoa(CarrierSlope) +
		"|vp:" + fnum(VPRadius) +
		"|weights:" + fnum(SiegeWeights[0]) + "," + fnum(SiegeWeights[1]) + "," + fnum(SiegeWeights[2]) + "," + fnum(SiegeWeights[3]) + "," + fnum(SiegeWeights[4]) + "," + fnum(SiegeWeights[5]) + "," + fnum(SiegeWeights[6]) + "," + fnum(SiegeWeights[7]) +
		"|alloc:produce,attack,research,move," + itoa(AllocStep) + "," + itoa(siegeStartAlloc().Produce) + "," + itoa(siegeStartAlloc().Attack) + "," + itoa(siegeStartAlloc().Research) + "," + itoa(siegeStartAlloc().Move) +
		"|economy:" + itoa(StartCU) + "," + fnum(SiegeIncome) + ",0," + fnum(ResRadius) + "," + itoa(PopMax) +
		"|mine:" + fnum(MineRate) + "," + fnum(MineAmount) + "," + fnum(MineSalvageBonus) +
		"|fort:" + itoa(c.FortBudget) + "," + itoa(c.PlaceTicks) + "," + itoa(SiegeFortCostWall) + "," + itoa(SiegeFortCostTurret) + "," + fnum(SiegeFortRadius) +
		"|hp:" + fnum(c.MothershipHP) + "," + fnum(c.AttackerMothershipHP) + "," + itoa(c.AttackerStartLav) + "," + itoa(c.DefenderStartLav) + "," + fnum(c.AttackerCU) + "," + fnum(c.DefenderCU) +
		"|weapon:" + itoa(WeaponCost) + "," + itoa(WeaponBuild) + "," + itoa(WeaponQueueMax) +
		"|ballistics:" + fnum(AirK) + "," + fnum(GravityG) + "," + fnum(MountainH) + "," + fnum(HitRM) +
		"|types:" + siegeTypesFingerprint() +
		"|vision:" + itoa(b2i(c.Vision.Enabled)) + itoa(b2i(c.Vision.Fog)) + itoa(b2i(c.Vision.LOS))
	return FNV1A(body)
}

func b2i(b bool) int {
	if b {
		return 1
	}
	return 0
}

func siegeTypesFingerprint() string {
	out := ""
	for _, k := range []string{"LAV", "AAV", "RAILGUN", "ARTILLERY", "SALVAGER", "SCOUT", "MOTHERSHIP", "CARRIER", "WALL", "TURRET"} {
		t := SiegeTypes[k]
		out += k + ":" + fnum(t.HP) + "," + fnum(t.Speed) + "," + fnum(t.Range) + "," + fnum(t.Dmg) + "," +
			itoa(t.Rof) + "," + itoa(t.Vision) + "," + t.Arc + "," + t.Armor + "," + fnum(t.V0) + "," +
			itoa(t.Cost) + "," + itoa(t.Build) + "," + itoa(t.Pop) + ";"
	}
	return out
}

// EncodeTick 攻守 per-seat tick（mines/roles/phase/fortBudget 增量）。
func (g *SiegeGame) EncodeTick(i int) string {
	S := g.Sides[i]
	var seen map[int]bool
	if g.Vision.Enabled {
		seen = map[int]bool{}
		for _, e := range g.Units {
			if e.Dead || e.Side == i {
				continue
			}
			if g.canSeeBySide(i, e.X, e.Y) {
				seen[e.ID] = true
			}
		}
	}
	type tickUnit struct {
		ID    int     `json:"id"`
		Side  int     `json:"side"`
		Kind  string  `json:"kind"`
		X     float64 `json:"x"`
		Y     float64 `json:"y"`
		HP    float64 `json:"hp"`
		MaxHP float64 `json:"maxHp"`
		Mode  string  `json:"mode,omitempty"`
	}
	type tickMine struct {
		ID        int     `json:"id"`
		X         float64 `json:"x"`
		Y         float64 `json:"y"`
		Owner     int     `json:"owner"`
		Contested bool    `json:"contested"`
		Amount    int     `json:"amount"`
	}
	state := map[string]any{
		"type": "tick", "turn": g.Turn, "tickLimit": g.Cfg.TickLimit, "side": i, "seed": g.Seed,
		"timing": map[string]any{"tickMs": g.Cfg.TickMS, "decideMs": g.Cfg.DecideMS,
			"applyAt": g.Turn + OrderLatency, "orderLatency": OrderLatency},
		"mines": func() []tickMine {
			out := []tickMine{}
			for _, m := range g.Mines {
				out = append(out, tickMine{ID: m.ID, X: m.X, Y: m.Y, Owner: m.Owner, Contested: m.Contested, Amount: int(math.Round(m.Amount))})
			}
			return out
		}(),
		"res": []any{},
		"score": []map[string]any{
			{"mines": g.Sides[0].MinesOwned, "kills": g.Sides[0].Kills, "losses": g.Sides[0].Losses},
			{"mines": g.Sides[1].MinesOwned, "kills": g.Sides[1].Kills, "losses": g.Sides[1].Losses},
		},
		"me": map[string]any{
			"cu": int(math.Round(S.CU)), "pop": S.Pop, "popMax": PopMax,
			"role":       S.Role,
			"fortBudget": S.FortBudget, "fortsPlaced": S.FortsPlaced,
			"minesOwned": S.MinesOwned, "mineIncome": roundTo(S.MineIncome, 1),
			"phase": map[string]string{},
			"alloc": S.Alloc, "allocTarget": S.Target,
			"mode": S.Mode,
			"slots": map[string]any{
				"produce": slotsOfPct(S.Alloc.Produce), "research": slotsOfPct(S.Alloc.Research),
				"attackSlots": roundTo(float64(weaponCapOfAlloc(S.Alloc.Attack)), 2), "attackCap": weaponCapOfAlloc(S.Alloc.Attack),
			},
			"weaponSlots":  roundTo(float64(weaponCapOfAlloc(S.Alloc.Attack)), 2),
			"weaponsReady": S.WeaponsReady,
			"speedMul":     roundTo(SpeedMulOf(S.Alloc.Move), 3),
			"techDone":     sortStrings(keysOf(S.TechDone)),
			"queue":        append([]string(nil), S.Queue...),
		},
	}
	// phase
	if g.Turn < g.Cfg.PlaceTicks {
		state["me"].(map[string]any)["phase"] = "place"
	} else {
		state["me"].(map[string]any)["phase"] = "battle"
	}
	wq := []int{}
	for _, w := range S.WeaponQueue {
		wq = append(wq, int(math.Ceil(w.Remain)))
	}
	state["me"].(map[string]any)["weaponQueue"] = wq
	doing := []map[string]any{}
	for _, t := range S.TechDoing {
		doing = append(doing, map[string]any{"id": t.ID, "remain": int(math.Ceil(t.Remain))})
	}
	state["me"].(map[string]any)["techDoing"] = doing
	active := []map[string]any{}
	for _, a := range S.Active {
		active = append(active, map[string]any{"kind": a.Kind, "remain": int(math.Ceil(a.Remain))})
	}
	state["me"].(map[string]any)["active"] = active
	carriers := []map[string]any{}
	for _, u := range g.Units {
		if u.Side == i && u.Kind == "CARRIER" && !u.Dead {
			q := []map[string]any{}
			for _, job := range u.Queue {
				q = append(q, map[string]any{"kind": job.Kind, "remain": int(math.Ceil(job.Remain))})
			}
			carriers = append(carriers, map[string]any{"id": u.ID, "queue": q})
		}
	}
	state["me"].(map[string]any)["carriers"] = carriers
	state["me"].(map[string]any)["mods"] = S.Mods

	units := []tickUnit{}
	for _, u := range g.Units {
		if seen != nil && u.Side != i && !seen[u.ID] {
			continue
		}
		tu := tickUnit{ID: u.ID, Side: u.Side, Kind: u.Kind, X: u.X, Y: u.Y, HP: u.HP, MaxHP: u.MaxHP}
		if u.Side == i {
			tu.Mode = u.Mode
		}
		units = append(units, tu)
	}
	state["units"] = units
	shells := []FrameShell{}
	for _, p := range g.Projectiles {
		px, py := p.SX+p.DX*p.S/MPerPX, p.SY+p.DY*p.S/MPerPX
		if p.Side != i && g.Vision.Enabled && !g.pointVisibleSide(i, px, py) {
			continue
		}
		shells = append(shells, FrameShell{ID: p.ID, Side: p.Side,
			X: int(math.Round(px)), Y: int(math.Round(py)), H: int(math.Round(p.H))})
	}
	state["shells"] = shells
	state["events"] = g.siegeVisibleEvents(i)
	b, _ := json.Marshal(state)
	return string(b) + "\n"
}

func (g *SiegeGame) siegeVisibleEvents(side int) []Event {
	if !g.Vision.Enabled {
		if len(g.LastEvents) > 12 {
			return append([]Event(nil), g.LastEvents[len(g.LastEvents)-12:]...)
		}
		return append([]Event(nil), g.LastEvents...)
	}
	var out []Event
	for _, e := range g.LastEvents {
		if e.Type == "mine" || e.Type == "fort" {
			out = append(out, e)
		} else if e.Type == "kill" {
			if e.By == side {
				out = append(out, e)
			}
		} else if e.Side == side {
			out = append(out, e)
		}
	}
	if len(out) > 12 {
		out = out[len(out)-12:]
	}
	return out
}

// EncodeEnd 终局一行。
func (g *SiegeGame) EncodeEnd() string {
	b, _ := json.Marshal(map[string]any{"type": "end", "turn": g.Turn, "result": g.Result()})
	return string(b) + "\n"
}

// Decode 攻守版解析（fort 动作；无 harvest）。
func (g *SiegeGame) Decode(i int, line string) ([]Action, error) {
	if Utf8Length(line) > SiegeMaxLine {
		return nil, errors.New("line too long")
	}
	trimmed := stringsTrimRightNewlines(line)
	var obj struct {
		Actions []map[string]any `json:"actions"`
	}
	if err := json.Unmarshal([]byte(trimmed), &obj); err != nil {
		return nil, errors.New("bad json")
	}
	if obj.Actions == nil {
		return nil, errors.New("actions must be array")
	}
	mine := map[int]bool{}
	for _, u := range g.Units {
		if u.Side == i && !u.Dead {
			mine[u.ID] = true
		}
	}
	if len(obj.Actions) > len(mine)*2+4 {
		return nil, errors.New("too many actions")
	}
	seenUnit := map[int]bool{}
	seenCamp := map[string]bool{}
	var out []Action
	for _, a := range obj.Actions {
		t, _ := a["type"].(string)
		if t == "" {
			return nil, errors.New("bad action type")
		}
		switch t {
		case "alloc":
			if seenCamp["alloc"] {
				return nil, errors.New("duplicate camp action")
			}
			seenCamp["alloc"] = true
			rawAlloc, _ := a["alloc"].(map[string]any)
			ac := SiegeAlloc{}
			if rawAlloc == nil || len(rawAlloc) != len(siegeAllocKeys) {
				g.noteRejected(i, 0, "alloc must have 4 keys")
				continue
			}
			bad := false
			sum := 0
			for _, k := range siegeAllocKeys {
				v, ok := rawAlloc[k].(float64)
				if !ok || v != math.Trunc(v) || v < 0 || v > 100 {
					bad = true
					break
				}
				ac.set(k, int(v))
				sum += int(v)
			}
			if bad {
				g.noteRejected(i, 0, "alloc must be integer 0..100")
				continue
			}
			if sum != 100 {
				g.noteRejected(i, 0, "alloc sum must be 100 (got "+itoa(sum)+")")
				continue
			}
			out = append(out, Action{Type: "alloc", SiegeAlloc: &ac})
		case "produce":
			if seenCamp["produce"] {
				return nil, errors.New("duplicate camp action")
			}
			seenCamp["produce"] = true
			kind, _ := a["kind"].(string)
			if !inList(Producible, kind) {
				g.noteRejected(i, 0, "not producible")
				continue
			}
			if g.Sides[i].Pop+SiegeTypes[kind].Pop > PopMax {
				g.noteRejected(i, 0, "pop full")
				continue
			}
			if byv, present := a["by"]; present && byv != nil {
				byf, ok := byv.(float64)
				if !ok || byf != math.Trunc(byf) {
					g.noteRejected(i, 0, "produce: not a carrier")
					continue
				}
				by := int(byf)
				var host *SiegeUnit
				for _, u := range g.Units {
					if u.ID == by && !u.Dead && u.Side == i {
						host = u
						break
					}
				}
				if host == nil || host.Kind != "CARRIER" {
					g.noteRejected(i, 0, "produce: not a carrier")
					continue
				}
				if !inList(SmallProducible, kind) {
					g.noteRejected(i, 0, "produce: carrier cannot build "+kind)
					continue
				}
				if len(host.Queue) >= 3 {
					g.noteRejected(i, 0, "carrier queue full")
					continue
				}
				out = append(out, Action{Type: "produce", Kind: kind, By: &by})
				continue
			}
			if len(g.Sides[i].Queue)+len(g.Sides[i].Active) >= 6 {
				g.noteRejected(i, 0, "queue full")
				continue
			}
			out = append(out, Action{Type: "produce", Kind: kind})
		case "weapon":
			if seenCamp["weapon"] {
				return nil, errors.New("duplicate camp action")
			}
			seenCamp["weapon"] = true
			S := g.Sides[i]
			cap := weaponCapOfAlloc(S.Alloc.Attack)
			if S.WeaponsReady+len(S.WeaponQueue) >= cap {
				g.noteRejected(i, 0, "weapon: no free weapon slot (cap "+itoa(cap)+")")
				continue
			}
			if len(S.WeaponQueue) >= WeaponQueueMax {
				g.noteRejected(i, 0, "weapon queue full")
				continue
			}
			out = append(out, Action{Type: "weapon"})
		case "research":
			if seenCamp["research"] {
				return nil, errors.New("duplicate camp action")
			}
			seenCamp["research"] = true
			tech, _ := a["tech"].(string)
			chk := g.techAvailable(i, tech)
			if !chk.ok {
				g.noteRejected(i, 0, "research: "+chk.reason)
				continue
			}
			out = append(out, Action{Type: "research", Tech: tech})
		case "fort":
			if seenCamp["fort"] {
				return nil, errors.New("duplicate camp action")
			}
			seenCamp["fort"] = true
			kind, _ := a["kind"].(string)
			act := Action{Type: "fort", Kind: kind}
			if xf, ok := a["x"].(float64); ok {
				act.X = &xf
			}
			if yf, ok := a["y"].(float64); ok {
				act.Y = &yf
			}
			out = append(out, act)
		case "mode":
			mode, _ := a["mode"].(string)
			if !inList(Modes, mode) {
				g.noteRejected(i, 0, "mode: unknown "+mode)
				continue
			}
			if idv, present := a["id"]; present && idv != nil {
				idf, ok := idv.(float64)
				if !ok || idf != math.Trunc(idf) {
					return nil, errors.New("bad unit id")
				}
				id := int(idf)
				if !mine[id] {
					return nil, errors.New("bad unit id")
				}
				out = append(out, Action{Type: "mode", ID: &id, Mode: mode})
				continue
			}
			out = append(out, Action{Type: "mode", Mode: mode})
		case "move", "attack", "idle":
			idf, ok := a["id"].(float64)
			if !ok || idf != math.Trunc(idf) {
				return nil, errors.New("bad unit id")
			}
			id := int(idf)
			if !mine[id] {
				return nil, errors.New("bad unit id")
			}
			if seenUnit[id] {
				return nil, errors.New("duplicate unit action")
			}
			seenUnit[id] = true
			act := Action{Type: t, ID: &id}
			if t == "move" {
				xf, okx := a["x"].(float64)
				yf, oky := a["y"].(float64)
				if !okx || !oky || math.IsNaN(xf) || math.IsInf(xf, 0) || math.IsNaN(yf) || math.IsInf(yf, 0) {
					return nil, errors.New("bad coords")
				}
				if xf < 0 || yf < 0 || xf > World || yf > World {
					return nil, errors.New("coords out of range")
				}
				if g.Map.Base[Ci(GxOf(xf), GyOf(yf))] == 3 {
					g.noteRejected(i, id, "target on mountain")
					continue
				}
				act.X, act.Y = &xf, &yf
			}
			if tv, present := a["targetId"]; present {
				tf, ok := tv.(float64)
				if !ok {
					return nil, errors.New("bad targetId")
				}
				tid := int(tf)
				act.TargetID = &tid
			}
			if t == "attack" {
				var target *SiegeUnit
				if act.TargetID != nil {
					for _, u := range g.Units {
						if u.ID == *act.TargetID && !u.Dead && u.Side != i {
							target = u
							break
						}
					}
				}
				if target == nil {
					g.noteRejected(i, id, "bad attack target")
					continue
				}
			}
			out = append(out, act)
		default:
			return nil, errors.New("bad action type")
		}
	}
	return out, nil
}

func stringsTrimRightNewlines(s string) string {
	for len(s) > 0 && (s[len(s)-1] == '\n' || s[len(s)-1] == '\r') {
		s = s[:len(s)-1]
	}
	return s
}

// VerifySiegeDeterminism 攻守重放复核。
func VerifySiegeDeterminism(rec *VerifyRec) VerifyReply {
	if rec == nil || rec.OrderLog == nil {
		return VerifyReply{OK: false}
	}
	cfg := SiegeCfg{}
	if rec.SiegeCfg != nil {
		cfg = *rec.SiegeCfg
	}
	if rec.TickLimit > 0 {
		cfg.TickLimit = rec.TickLimit
	}
	if rec.Generator != "" {
		cfg.Generator = rec.Generator
	}
	if rec.Vision != nil {
		cfg.Vision = *rec.Vision
	}
	g, err := NewSiegeGame(rec.Seed, cfg)
	if err != nil {
		return VerifyReply{OK: false}
	}
	var mismatch *Mismatch
	if rec.MapAttempt != nil && *rec.MapAttempt != g.Map.Attempt {
		mismatch = &Mismatch{Turn: 0,
			Expected: "mapAttempt " + itoa(*rec.MapAttempt),
			Got:      "mapAttempt " + itoa(g.Map.Attempt)}
	}
	if mismatch == nil && len(rec.FrameHashes) > 0 && rec.FrameHashes[0] != g.FrameHashes[0] {
		mismatch = &Mismatch{Turn: 0, Expected: rec.FrameHashes[0], Got: g.FrameHashes[0]}
	}
	if mismatch == nil && len(rec.FrameHashes) != len(rec.OrderLog)+1 {
		mismatch = &Mismatch{
			Turn:     0,
			Expected: "frameHashes=" + itoa(len(rec.OrderLog)+1),
			Got:      "frameHashes=" + itoa(len(rec.FrameHashes)),
		}
	}
	for k := range rec.OrderLog {
		if mismatch != nil {
			break
		}
		e := &rec.OrderLog[k]
		g.Step([2][]Action{e.O[0], e.O[1]})
		if k+1 < len(rec.FrameHashes) {
			got := g.FrameHashes[len(g.FrameHashes)-1]
			if rec.FrameHashes[k+1] != got {
				mismatch = &Mismatch{Turn: g.Turn, Expected: rec.FrameHashes[k+1], Got: got}
			}
		}
	}
	if mismatch == nil && rec.Forfeited != nil {
		g.Forfeit(*rec.Forfeited, "")
	} else if mismatch == nil && rec.EndWinner != nil {
		g.EndGame(*rec.EndWinner, 0, "")
	}
	r := g.Result()
	var resultMismatch *ResultMis
	if mismatch == nil && rec.SiegeResult != nil {
		if r == nil {
			resultMismatch = &ResultMis{
				Expected: itoa(rec.SiegeResult.Winner) + "/" + itoa(rec.SiegeResult.Level),
				Got:      "nil",
			}
		} else if rec.SiegeResult.Winner != r.Winner || rec.SiegeResult.Level != r.Level {
			resultMismatch = &ResultMis{
				Expected: itoa(rec.SiegeResult.Winner) + "/" + itoa(rec.SiegeResult.Level),
				Got:      itoa(r.Winner) + "/" + itoa(r.Level),
			}
		}
	}
	var vr *VerifyResult
	if r != nil {
		vr = &VerifyResult{Winner: r.Winner, Level: r.Level}
	}
	return VerifyReply{
		OK:    mismatch == nil && resultMismatch == nil,
		Turns: g.Turn, Frames: len(g.FrameHashes),
		Mismatch: mismatch, ResultMismatch: resultMismatch, Result: vr,
	}
}
