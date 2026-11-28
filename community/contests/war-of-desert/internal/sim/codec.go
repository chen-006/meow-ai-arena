// 编解码与帧：hello / tick / actions / end 的 wire 层。
//
// 版本：v1.2（Desert Arena · Go 原生引擎 internal/sim）。
// 功能：EncodeTick（选手观测）、Decode（指令校验入栈）、Frame（观战真值帧，
// 含 waitingQueue/queueBlocked 观测字段）、哈希与确定性复核（VerifyRec）。
// 键序对齐 JS frame()；帧哈希 = FNV1A(canonical JSON)。
package sim

import (
	"encoding/json"
	"errors"
	"math"
	"strings"
)

// 协议编解码（rules.js:1012-1323 的移植）：hello / tick / end / frame、
// decode 两级校验（结构性抛错判故障 / 语义性丢弃记账）、确定性复核。

// FrameUnit / FrameVP / FrameScore / FrameShell 帧子结构（键序对齐 JS frame()）。
type FrameUnit struct {
	ID    int     `json:"id"`
	Side  int     `json:"side"`
	Kind  string  `json:"kind"`
	X     int     `json:"x"`
	Y     int     `json:"y"`
	HP    int     `json:"hp"`
	MaxHP float64 `json:"maxHp"`
	Mode  string  `json:"mode"`
	// v1.5；v14 留零值并由 omitempty 隐藏。
	Lvl    int  `json:"lvl,omitempty"`
	Kills  int  `json:"kills,omitempty"`
	Facing int  `json:"facing,omitempty"`
	Turret int  `json:"turret,omitempty"`
	Relic  *int `json:"relic,omitempty"`
	// v1.5 诊断：采集车状态（回放复盘用）
	Harvest *int   `json:"harvest,omitempty"`
	HState  string `json:"hstate,omitempty"`
	CarryCU int    `json:"carryCu,omitempty"`
	CarryRU int    `json:"carryRu,omitempty"`
	// v1.6 诊断：研究舰是否已接到 relicresearch 职责
	RDuty bool `json:"rduty,omitempty"`
}

type FrameRelic struct {
	ID    int    `json:"id"`
	X     int    `json:"x"`
	Y     int    `json:"y"`
	State string `json:"state"`
	By    int    `json:"by,omitempty"`
	VP    int    `json:"vp,omitempty"`
}

type FrameFlag struct {
	ID     int `json:"id"`
	VP     int `json:"vp"`
	Side   int `json:"side"`
	X      int `json:"x"`
	Y      int `json:"y"`
	HP     int `json:"hp"`
	Stored int `json:"stored,omitempty"`
}

type FrameResearch struct {
	Side     int     `json:"side"`
	Node     string  `json:"node"`
	VP       int     `json:"vp"`
	Ship     int     `json:"ship"`
	Progress float64 `json:"progress"`
	Total    float64 `json:"total"`
	Active   bool    `json:"active"`
	Paused   bool    `json:"paused,omitempty"`
}

type FrameVP struct {
	ID    int `json:"id"`
	Owner int `json:"owner"`
}

type FrameScore struct {
	VP     int `json:"vp"`
	Kills  int `json:"kills"`
	Losses int `json:"losses"`
	VS     int `json:"vs,omitempty"`
}

type FrameShell struct {
	ID   int `json:"id"`
	Side int `json:"side"`
	X    int `json:"x"`
	Y    int `json:"y"`
	H    int `json:"h"`
}

// Frame 观战者真值帧（键序对齐 JS frame()：turn,units,vp,score,cu,ru,alloc,techs,queue,weapons,shells）。
// v1.2 观测增强：新增 waitingQueue / queueBlocked——回放可见「等待队列」与
// 「队首堵塞」状态（此前只有生产中队列，复盘无法解释 Vanguard 式零制造）。
type Frame struct {
	Turn         int           `json:"turn"`
	Units        []FrameUnit   `json:"units"`
	VP           []FrameVP     `json:"vp"`
	Score        [2]FrameScore `json:"score"`
	CU           [2]int        `json:"cu"`
	RU           [2]int        `json:"ru"`
	Alloc        [2][4]int     `json:"alloc"`
	Techs        [2][]string   `json:"techs"`
	Queue        [2][]string   `json:"queue"`
	WaitingQueue [2][]string   `json:"waitingQueue"`
	QueueBlocked [2]bool       `json:"queueBlocked"`
	Weapons      [2]int        `json:"weapons"`
	Shells       []FrameShell  `json:"shells"`
	// v1.5；v14 为 nil，omitempty 保证旧回放字节不变。
	Relics   []FrameRelic    `json:"relics,omitempty"`
	Flags    []FrameFlag     `json:"flags,omitempty"`
	VS       []VictoryScore  `json:"vs,omitempty"`
	Research []FrameResearch `json:"research,omitempty"`
}

func (g *Game) buildFrame() *Frame {
	f := &Frame{Turn: g.Turn}
	for _, u := range g.Units {
		fu := FrameUnit{
			ID: u.ID, Side: u.Side, Kind: u.Kind,
			X: int(math.Round(u.X)), Y: int(math.Round(u.Y)),
			HP: int(math.Round(u.HP)), MaxHP: u.MaxHP, Mode: u.Mode,
		}
		if g.v15() {
			fu.Lvl, fu.Kills = u.Level, u.Kills
			fu.Facing = int(math.Round(u.Facing))
			fu.Turret = int(math.Round(u.Turret))
			fu.Relic = u.Relic
			fu.RDuty = u.RDuty
			if u.Kind == "SALVAGER" {
				fu.Harvest, fu.HState = u.Harvest, u.HState
				fu.CarryCU = int(math.Round(u.CarryCU))
				fu.CarryRU = int(math.Round(u.CarryRU))
			}
		}
		f.Units = append(f.Units, fu)
	}
	for _, vp := range g.Vps {
		f.VP = append(f.VP, FrameVP{ID: vp.ID, Owner: vp.Owner})
	}
	for s := 0; s < 2; s++ {
		S := g.Sides[s]
		f.Score[s] = FrameScore{VP: S.VPScore, Kills: S.Kills, Losses: S.Losses}
		if g.v15() {
			f.Score[s].VP = S.VS.Total
			f.Score[s].VS = S.VS.Total
		}
		f.CU[s] = int(math.Round(S.CU))
		f.RU[s] = int(math.Round(S.RU))
		f.Alloc[s] = [4]int{S.Alloc.Armour, S.Alloc.Repair, S.Alloc.Attack, S.Alloc.Range}
		f.Techs[s] = sortStrings(keysOf(S.TechDone))
		for _, a := range S.Active {
			f.Queue[s] = append(f.Queue[s], a.Kind)
		}
		if len(S.Queue) > 0 {
			f.WaitingQueue[s] = append([]string(nil), S.Queue...)
			// v1.2：堵塞 = 整队没有任何可负担项
			//（与跳过式 stepProduction 的等待条件一致）。
			blocked := true
			for _, k := range S.Queue {
				t := g.ut(k)
				cost := math.Round(float64(t.Cost) * (1 - S.Mods.Cost))
				if S.CU >= cost && S.RU >= float64(t.RU) {
					blocked = false
					break
				}
			}
			f.QueueBlocked[s] = blocked
		}
		f.Weapons[s] = S.WeaponsReady
	}
	for _, p := range g.Projectiles {
		f.Shells = append(f.Shells, FrameShell{
			ID: p.ID, Side: p.Side,
			X: int(math.Round(p.SX + p.DX*p.S/MPerPX)),
			Y: int(math.Round(p.SY + p.DY*p.S/MPerPX)),
			H: int(math.Round(p.H)),
		})
	}
	if g.v15() {
		for _, r := range g.V15.Relics {
			f.Relics = append(f.Relics, FrameRelic{
				ID: r.ID, X: int(math.Round(r.X)), Y: int(math.Round(r.Y)),
				State: r.State, By: r.CarrierID, VP: r.VPID,
			})
		}
		for _, fl := range g.V15.Flags {
			f.Flags = append(f.Flags, FrameFlag{
				ID: fl.ID, VP: fl.VPID, Side: fl.Side,
				X: int(math.Round(fl.X)), Y: int(math.Round(fl.Y)),
				HP: int(math.Round(fl.HP)), Stored: g.relicsAtVP(fl.VPID),
			})
		}
		for s := 0; s < 2; s++ {
			if rs := g.V15.Research[s]; rs != nil {
				f.Research = append(f.Research, FrameResearch{
					Side: s, Node: rs.Node, VP: rs.VPID, Ship: rs.ShipID,
					Progress: rs.Progress, Total: rs.Total, Active: rs.Active, Paused: rs.Paused,
				})
			}
		}
		for s := 0; s < 2; s++ {
			g.Sides[s].VS.Delivered = g.Sides[s].DeliveredRelics
			g.Sides[s].VS.Stored = g.storedRelics(s)
		}
		f.VS = []VictoryScore{g.Sides[0].VS, g.Sides[1].VS}
	}
	return f
}

// BuildFrame 公开帧构造。
func (g *Game) BuildFrame() *Frame { return g.buildFrame() }

// HashFrame 帧哈希：确定性重放的比对单位（fnv1a over canonical JSON）。
func (g *Game) HashFrame(f *Frame) string {
	if f == nil {
		f = g.buildFrame()
	}
	b, _ := json.Marshal(f)
	return FNV1A(string(b))
}

// ---------------- hello / tick / end ----------------

// Header hello 的静态信息块（键序对齐 JS header()）。
func (g *Game) Header() json.RawMessage {
	layers := EncodeMapLayers(g.Map)
	spawns := [][2]int{}
	for _, s := range g.Map.SpawnCells {
		spawns = append(spawns, s)
	}
	vps := []map[string]any{}
	for _, v := range g.Vps {
		vps = append(vps, map[string]any{"id": v.ID, "x": v.X, "y": v.Y})
	}
	ress := []map[string]any{}
	for _, r := range g.Resources {
		ress = append(ress, map[string]any{"id": r.ID, "x": r.X, "y": r.Y, "kind": r.Kind})
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
		"types": g.typeTable(), "producible": g.producible(), "smallProducible": SmallProducible,
		"alloc": map[string]any{
			"keys": AllocKeys, "step": AllocStep, "start": startAlloc(),
			"slotStep": SlotStep, "maxSlots": MaxSlots, "maxWeapons": MaxWeapons,
			"researchSlots": ResearchSlots,
			"subsystems": map[string]any{
				"armourReduce": ArmourReduce, "repairRate": MSRepairRate,
				"attackDmgBase": AttackDmgBase, "rangeMulBase": RangeMulBase, "rangeMulSpan": RangeMulSpan,
			},
		},
		"economy": map[string]any{
			"startCu": StartCU, "startRu": StartRU, "income": BaseIncome,
			"ruIncome": RUBaseIncome,
			"salvage":  SalvageRate, "capacity": SalvagerCapacity, "resAmount": ResAmount,
			"resRadius": ResRadius, "popMax": g.popMax(),
		},
		"weapon": map[string]any{"cost": WeaponCost, "build": WeaponBuild, "queueMax": WeaponQueueMax},
		"repair": map[string]any{"radius": RepairRadius, "rate": RepairRate},
		"vision": map[string]any{
			"enabled": g.Vision.Enabled, "fog": g.Vision.Fog, "los": g.Vision.LOS,
			"eyeH": EyeH, "losTol": LosTol, "losRange": LosRangeCells,
			"heightBonus": HeightBonusCells, "heightDiffMin": HeightDiffMin,
		},
		"modes": Modes, "defaultMode": DefaultMode, "modeDesc": ModeDesc,
		"modeRetreatCells": ModeRetreatCells,
		"techs":            g.techTable(), "trees": g.treeTable(),
		"ruleset": g.Cfg.Ruleset,
	}
	if g.v15() {
		sites := make([]map[string]any, 0, len(g.V15.Sites))
		for i, s := range g.V15.Sites {
			sites = append(sites, map[string]any{"id": i, "x": s.X, "y": s.Y})
		}
		rules["v15"] = map[string]any{
			"vsWin": VSWinScore, "relicValue": RelicValue, "relicVSCap": RelicVSCap,
			"flagPlaceVP": FlagPlaceVP, "flagPenalty": FlagPenalty, "flagBounty": FlagBounty,
			"flagHP": FlagHP, "flagPlaceTicks": FlagPlaceTicks,
			"relicLoadTicks": RelicLoadTicks, "relicDropTicks": RelicDropTicks,
			"relicSpawnTicks": RelicSpawnTicks, "relicMaxGround": RelicMaxGround,
			"relicRadius": RelicRadius, "relicCarrySpeedMul": RelicCarrySpeedMul,
			"relicSites": sites, "cep": CEPMax, "arcHalf": ArcHalf, "traverse": TraverseRate,
			"vetTitles": VetTitles, "vetThresholds": VetThresholds, "killVP": killVPTable(),
			"killVPCap": KillVPCap, "weights": WeightsV15, "fieldNames": FieldNamesV15,
			"researchUnit": "RESEARCH", "researchRadius": ResearchRadius,
		}
	}
	hello := map[string]any{
		"version": ProtoVersion, "proto": ProtoName, "protoVersion": ProtoVersion,
		"rulesHash": RulesHashFor(g.Cfg.Ruleset, g.Cfg.TickLimit, g.Vision),
		"game":      "Desert Arena · Referee MVP", "seed": g.Seed, "players": g.Players,
		"generator": g.Map.Generator, "mapAttempt": g.Map.Attempt,
		"w": Grid, "h": Grid, "cell": Cell, "world": World,
		"map": layers, "spawns": spawns, "vp": vps, "res": ress,
		"ruleset": g.Cfg.Ruleset,
		"rules":   rules,
	}
	b, _ := json.Marshal(hello)
	return b
}

// EncodeHello 开局一行。
func (g *Game) EncodeHello() string {
	b := g.Header()
	return `{"type":"hello",` + string(b)[1:] + "\n"
}

// EncodeEnd 终局一行。
func (g *Game) EncodeEnd() string {
	b, _ := json.Marshal(map[string]any{"type": "end", "turn": g.Turn, "result": g.Result()})
	return string(b) + "\n"
}

// TickState per-seat tick 行（键序对齐 JS encodeTick）。
type TickState struct {
	Type      string        `json:"type"`
	Turn      int           `json:"turn"`
	TickLimit int           `json:"tickLimit"`
	Side      int           `json:"side"`
	Seed      int           `json:"seed"`
	Timing    TickTiming    `json:"timing"`
	Units     []TickUnit    `json:"units"`
	VP        []TickVP      `json:"vp"`
	Res       []TickRes     `json:"res"`
	Score     [2]FrameScore `json:"score"`
	Me        TickMe        `json:"me"`
	Shells    []FrameShell  `json:"shells"`
	Events    []Event       `json:"events"`
	// v1.5
	Relics   []TickRelic     `json:"relics,omitempty"`
	Flags    []TickFlag      `json:"flags,omitempty"`
	VS       []VictoryScore  `json:"vs,omitempty"`
	Research []FrameResearch `json:"research,omitempty"`
}

type TickTiming struct {
	TickMS       int `json:"tickMs"`
	DecideMS     int `json:"decideMs"`
	ApplyAt      int `json:"applyAt"`
	OrderLatency int `json:"orderLatency"`
}

type TickUnit struct {
	ID      int        `json:"id"`
	Side    int        `json:"side"`
	Kind    string     `json:"kind"`
	X       float64    `json:"x"`
	Y       float64    `json:"y"`
	HP      float64    `json:"hp"`
	MaxHP   float64    `json:"maxHp"`
	Mode    string     `json:"mode,omitempty"`
	Harvest *int       `json:"harvest,omitempty"`
	HState  string     `json:"hstate,omitempty"`
	Carry   *TickCarry `json:"carry,omitempty"`
	// v1.5
	Lvl    int  `json:"lvl,omitempty"`
	Kills  int  `json:"kills,omitempty"`
	Facing int  `json:"facing,omitempty"`
	Turret int  `json:"turret,omitempty"`
	Relic  *int `json:"relic,omitempty"`
}

type TickRelic struct {
	ID    int     `json:"id"`
	X     float64 `json:"x"`
	Y     float64 `json:"y"`
	State string  `json:"state"`
	By    int     `json:"by,omitempty"`
	VP    int     `json:"vp,omitempty"`
}

type TickFlag struct {
	ID     int     `json:"id"`
	VP     int     `json:"vp"`
	Side   int     `json:"side"`
	X      float64 `json:"x"`
	Y      float64 `json:"y"`
	HP     float64 `json:"hp"`
	Stored int     `json:"stored,omitempty"`
}

type TickResearch struct {
	Node     string  `json:"node"`
	VP       int     `json:"vp"`
	Ship     int     `json:"ship"`
	Progress float64 `json:"progress"`
	Total    float64 `json:"total"`
	Active   bool    `json:"active"`
	Paused   bool    `json:"paused,omitempty"`
}

type TickCarry struct {
	CU int `json:"cu"`
	RU int `json:"ru"`
}

type TickVP struct {
	ID        int     `json:"id"`
	X         float64 `json:"x"`
	Y         float64 `json:"y"`
	Owner     int     `json:"owner"`
	Contested bool    `json:"contested"`
}

type TickRes struct {
	ID     int     `json:"id"`
	X      float64 `json:"x"`
	Y      float64 `json:"y"`
	Kind   string  `json:"kind"`
	Amount float64 `json:"amount"`
}

type TickSlots struct {
	Produce     int `json:"produce"`
	Research    int `json:"research"`
	AttackSlots int `json:"attackSlots"`
	AttackCap   int `json:"attackCap"`
}

type TickMe struct {
	CU           int           `json:"cu"`
	RU           int           `json:"ru"`
	Pop          int           `json:"pop"`
	PopMax       int           `json:"popMax"`
	Alloc        Alloc         `json:"alloc"`
	AllocTarget  Alloc         `json:"allocTarget"`
	Mode         string        `json:"mode"`
	Slots        TickSlots     `json:"slots"`
	WeaponSlots  int           `json:"weaponSlots"`
	WeaponsReady int           `json:"weaponsReady"`
	WeaponQueue  []int         `json:"weaponQueue"`
	SpeedMul     float64       `json:"speedMul"`
	TechDone     []string      `json:"techDone"`
	TechDoing    []TickTechJob `json:"techDoing"`
	Queue        []string      `json:"queue"`
	Active       []TickActive  `json:"active"`
	Carriers     []TickCarrier `json:"carriers"`
	Mods         Mods          `json:"mods"`
	VS           *VictoryScore `json:"vs,omitempty"`
	Research     *TickResearch `json:"research,omitempty"`
}

type TickTechJob struct {
	ID     string `json:"id"`
	Remain int    `json:"remain"`
}

type TickActive struct {
	Kind   string `json:"kind"`
	Remain int    `json:"remain"`
}

type TickCarrier struct {
	ID    int          `json:"id"`
	Queue []TickActive `json:"queue"`
}

// EncodeTick 发第 i 方的一行输入。
func (g *Game) EncodeTick(i int) string {
	S := g.Sides[i]
	// 视野关闭时 seen = nil：不过滤（输出与「没有视野功能」逐字节等价）
	var seen map[int]bool
	if g.Vision.Enabled {
		seen = g.visibleEnemies(i)
	}
	ts := TickState{
		Type: "tick", Turn: g.Turn, TickLimit: g.Cfg.TickLimit, Side: i, Seed: g.Seed,
		Timing: TickTiming{TickMS: g.Cfg.TickMS, DecideMS: g.Cfg.DecideMS, ApplyAt: g.Turn + OrderLatency, OrderLatency: OrderLatency},
	}
	for _, u := range g.Units {
		if seen != nil && u.Side != i && !seen[u.ID] {
			continue
		}
		tu := TickUnit{ID: u.ID, Side: u.Side, Kind: u.Kind, X: u.X, Y: u.Y, HP: u.HP, MaxHP: u.MaxHP}
		if u.Side == i {
			tu.Mode = u.Mode
			if u.Kind == "SALVAGER" {
				tu.Harvest = u.Harvest
				tu.HState = u.HState
				tu.Carry = &TickCarry{CU: int(math.Round(u.CarryCU)), RU: int(math.Round(u.CarryRU))}
			}
		}
		if g.v15() {
			tu.Lvl, tu.Kills = u.Level, u.Kills
			tu.Facing = int(math.Round(u.Facing))
			tu.Turret = int(math.Round(u.Turret))
			tu.Relic = u.Relic
		}
		ts.Units = append(ts.Units, tu)
	}
	for _, v := range g.Vps {
		ts.VP = append(ts.VP, TickVP{ID: v.ID, X: v.X, Y: v.Y, Owner: v.Owner, Contested: v.Contested})
	}
	for _, r := range g.Resources {
		ts.Res = append(ts.Res, TickRes{ID: r.ID, X: r.X, Y: r.Y, Kind: r.Kind, Amount: r.Amount})
	}
	for s := 0; s < 2; s++ {
		ts.Score[s] = FrameScore{VP: g.Sides[s].VPScore, Kills: g.Sides[s].Kills, Losses: g.Sides[s].Losses}
		if g.v15() {
			ts.Score[s].VP = g.Sides[s].VS.Total
			ts.Score[s].VS = g.Sides[s].VS.Total
		}
	}
	if g.v15() {
		for _, r := range g.V15.Relics {
			ts.Relics = append(ts.Relics, TickRelic{
				ID: r.ID, X: r.X, Y: r.Y, State: r.State, By: r.CarrierID, VP: r.VPID,
			})
		}
		for _, fl := range g.V15.Flags {
			ts.Flags = append(ts.Flags, TickFlag{
				ID: fl.ID, VP: fl.VPID, Side: fl.Side, X: fl.X, Y: fl.Y, HP: fl.HP,
				Stored: g.relicsAtVP(fl.VPID),
			})
		}
		for s := 0; s < 2; s++ {
			g.Sides[s].VS.Delivered = g.Sides[s].DeliveredRelics
			g.Sides[s].VS.Stored = g.storedRelics(s)
		}
		ts.VS = []VictoryScore{g.Sides[0].VS, g.Sides[1].VS}
	}
	ts.Me = TickMe{
		CU: int(math.Round(S.CU)), RU: int(math.Round(S.RU)), Pop: S.Pop, PopMax: g.popMax(),
		Alloc: S.Alloc, AllocTarget: S.Target, Mode: S.Mode,
		Slots:        TickSlots{Produce: g.productionLines(i), Research: ResearchSlots, AttackSlots: MaxWeapons, AttackCap: MaxWeapons},
		WeaponSlots:  MaxWeapons,
		WeaponsReady: S.WeaponsReady,
		SpeedMul:     1,
		TechDone:     sortStrings(keysOf(S.TechDone)),
		Mods:         S.Mods,
	}
	if g.v15() {
		vs := S.VS
		ts.Me.VS = &vs
		// 注意：必须放在 ts.Me = TickMe{...} 之后。之前写在前面会被整体赋值清掉，
		// 导致 tick.me.research 从来没有真正下发过。
		if rs := g.V15.Research[i]; rs != nil {
			ts.Me.Research = &TickResearch{
				Node: rs.Node, VP: rs.VPID, Ship: rs.ShipID,
				Progress: rs.Progress, Total: rs.Total, Active: rs.Active, Paused: rs.Paused,
			}
		}
	}
	for _, w := range S.WeaponQueue {
		ts.Me.WeaponQueue = append(ts.Me.WeaponQueue, int(math.Ceil(w.Remain)))
	}
	for _, t := range S.TechDoing {
		ts.Me.TechDoing = append(ts.Me.TechDoing, TickTechJob{ID: t.ID, Remain: int(math.Ceil(t.Remain))})
	}
	ts.Me.Queue = append([]string(nil), S.Queue...)
	for _, a := range S.Active {
		ts.Me.Active = append(ts.Me.Active, TickActive{Kind: a.Kind, Remain: int(math.Ceil(a.Remain))})
	}
	for _, u := range g.Units {
		if u.Side == i && u.Kind == "CARRIER" && !u.Dead {
			tc := TickCarrier{ID: u.ID}
			for _, q := range u.Queue {
				tc.Queue = append(tc.Queue, TickActive{Kind: q.Kind, Remain: int(math.Ceil(q.Remain))})
			}
			ts.Me.Carriers = append(ts.Me.Carriers, tc)
		}
	}
	for _, p := range g.Projectiles {
		px, py := p.SX+p.DX*p.S/MPerPX, p.SY+p.DY*p.S/MPerPX
		if p.Side != i && g.Vision.Enabled && !g.pointVisibleSide(i, px, py) {
			continue
		}
		ts.Shells = append(ts.Shells, FrameShell{
			ID: p.ID, Side: p.Side,
			X: int(math.Round(px)), Y: int(math.Round(py)), H: int(math.Round(p.H)),
		})
	}
	ts.Events = g.visibleEvents(i)
	b, _ := json.Marshal(ts)
	return string(b) + "\n"
}

// visibleEvents 事件也是信息：看不见的不出现在我方 tick 里（vp 公开）。
func (g *Game) visibleEvents(side int) []Event {
	if !g.Vision.Enabled {
		if len(g.LastEvents) > 12 {
			return append([]Event(nil), g.LastEvents[len(g.LastEvents)-12:]...)
		}
		return append([]Event(nil), g.LastEvents...)
	}
	var out []Event
	for _, e := range g.LastEvents {
		if e.Type == "vp" {
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

// ---------------- decode：结构性错误抛错（判故障），语义非法丢弃记账 ----------------

// Decode 解析一行回复。
func (g *Game) Decode(i int, line string) ([]Action, error) {
	if Utf8Length(line) > MaxLine {
		return nil, errors.New("line too long")
	}
	trimmed := strings.TrimRight(line, "\n")
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
			ac := Alloc{}
			bad := false
			sum := 0
			if rawAlloc == nil || len(rawAlloc) != len(AllocKeys) {
				g.noteRejected(i, 0, "alloc must have 4 keys")
				continue
			}
			for _, k := range AllocKeys {
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
			out = append(out, Action{Type: "alloc", Alloc: &ac})
		case "produce":
			if seenCamp["produce"] {
				return nil, errors.New("duplicate camp action")
			}
			seenCamp["produce"] = true
			kind, _ := a["kind"].(string)
			if !inList(g.producible(), kind) {
				g.noteRejected(i, 0, "not producible")
				continue
			}
			if g.Sides[i].Pop+g.ut(kind).Pop > g.popMax() {
				g.noteRejected(i, 0, "pop full")
				continue
			}
			if byv, present := a["by"]; present && byv != nil {
				byf, ok := byv.(float64)
				if !ok || byf != math.Trunc(byf) {
					// JS：a.by 非数值时 find 失败 → 「not a carrier」丢弃
					g.noteRejected(i, 0, "produce: not a carrier")
					continue
				}
				by := int(byf)
				var host *Unit
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
			cap := MaxWeapons
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
		case "move", "attack", "idle", "harvest":
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
			// JS：targetId !== undefined 且 typeof !== number → 抛错（null 也抛）。
			if tv, present := a["targetId"]; present {
				tf, ok := tv.(float64)
				if !ok {
					return nil, errors.New("bad targetId")
				}
				tid := int(tf)
				act.TargetID = &tid
			}
			if t == "attack" {
				var target *Unit
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
			if t == "harvest" {
				var sv *Unit
				for _, u := range g.Units {
					if u.ID == id && !u.Dead && u.Side == i {
						sv = u
						break
					}
				}
				if sv == nil || sv.Kind != "SALVAGER" {
					g.noteRejected(i, id, "harvest: not a salvager")
					continue
				}
				if ridv, present := a["resId"]; present && ridv != nil {
					ridf, ok := ridv.(float64)
					if !ok {
						g.noteRejected(i, id, "harvest: bad resId")
						continue
					}
					rid := int(ridf)
					exists := false
					for _, r := range g.Resources {
						if r.ID == rid {
							exists = true
							break
						}
					}
					if !exists {
						g.noteRejected(i, id, "harvest: bad resId")
						continue
					}
					act.ResID = &rid
				}
				out = append(out, act)
				continue
			}
			out = append(out, act)
		case "relicresearch":
			if !g.v15() {
				g.noteRejected(i, 0, "relicresearch: not in v15")
				continue
			}
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
			u := g.unitByID(id)
			if u == nil || u.Kind != "RESEARCH" {
				g.noteRejected(i, id, "relicresearch: not a research ship")
				continue
			}
			out = append(out, Action{Type: "relicresearch", ID: &id})
		case "relic":
			if !g.v15() {
				g.noteRejected(i, 0, "relic: not in v15")
				continue
			}
			idf, ok := a["id"].(float64)
			if !ok || idf != math.Trunc(idf) {
				return nil, errors.New("bad unit id")
			}
			id := int(idf)
			if !mine[id] || seenUnit[id] {
				return nil, errors.New("bad unit id")
			}
			sv := g.unitByID(id)
			if sv == nil || sv.Kind != "SALVAGER" {
				g.noteRejected(i, id, "relic: not a salvager")
				continue
			}
			ridf, ok := a["relicId"].(float64)
			if !ok || ridf != math.Trunc(ridf) {
				g.noteRejected(i, id, "relic: bad relicId")
				continue
			}
			rid := int(ridf)
			r := g.relicByID(rid)
			if r == nil || r.State != "ground" {
				g.noteRejected(i, id, "relic: not a ground relic")
				continue
			}
			seenUnit[id] = true
			out = append(out, Action{Type: "relic", ID: &id, RelicID: &rid})
		case "attackFlag":
			if !g.v15() {
				g.noteRejected(i, 0, "attackFlag: not in v15")
				continue
			}
			idf, ok := a["id"].(float64)
			if !ok || idf != math.Trunc(idf) {
				return nil, errors.New("bad unit id")
			}
			id := int(idf)
			if !mine[id] || seenUnit[id] {
				return nil, errors.New("bad unit id")
			}
			fidf, ok := a["flagId"].(float64)
			if !ok || fidf != math.Trunc(fidf) {
				g.noteRejected(i, id, "attackFlag: bad flagId")
				continue
			}
			fid := int(fidf)
			f := g.flagByID(fid)
			if f == nil || f.Side == i {
				g.noteRejected(i, id, "attackFlag: bad flag")
				continue
			}
			seenUnit[id] = true
			out = append(out, Action{Type: "attackFlag", ID: &id, FlagID: &fid})
		case "face":
			if !g.v15() {
				g.noteRejected(i, 0, "face: not in v15")
				continue
			}
			idf, ok := a["id"].(float64)
			if !ok || idf != math.Trunc(idf) {
				return nil, errors.New("bad unit id")
			}
			id := int(idf)
			if !mine[id] || seenUnit[id] {
				return nil, errors.New("bad unit id")
			}
			deg, ok := a["deg"].(float64)
			if !ok || math.IsNaN(deg) || math.IsInf(deg, 0) || deg < 0 || deg > 360 {
				g.noteRejected(i, id, "face: bad deg")
				continue
			}
			seenUnit[id] = true
			out = append(out, Action{Type: "face", ID: &id, Deg: &deg})
		default:
			return nil, errors.New("bad action type")
		}
	}
	return out, nil
}

// ---------------- 确定性复核（verifyDeterminism 的移植） ----------------

// VerifyRec 复核记录（回放里的确定性基础设施块）。
type VerifyRec struct {
	Seed        int             `json:"seed"`
	TickLimit   int             `json:"tickLimit,omitempty"`
	Ruleset     string          `json:"ruleset,omitempty"`
	Generator   string          `json:"generator,omitempty"`
	Vision      *Vision         `json:"vision,omitempty"`
	MapAttempt  *int            `json:"mapAttempt,omitempty"`
	OrderLog    []OrderLogEntry `json:"orderLog"`
	FrameHashes []string        `json:"frameHashes,omitempty"`
	// Forfeited 裁判红牌判负的座位（-1/未设置 = 无）。重放时必须复现这一外部收局，
	// 否则 Forfeit/EndGame 终止的局在重放里 Result 为 nil，复核会空转通过。
	Forfeited *int `json:"forfeited,omitempty"`
	// EndWinner 裁判手动 EndGame 的胜者（-1 = 平局），用于双方同时故障的收局。
	EndWinner *int `json:"endWinner,omitempty"`
	// SiegeCfg 攻守局的完整配置。母舰 HP / 要塞预算 / 初始 CU 都会影响对局，
	// 只带 TickLimit 重放会在第 0 帧就与记录不一致。
	SiegeCfg    *SiegeCfg    `json:"siegeCfg,omitempty"`
	Result      *Result      `json:"result,omitempty"`
	SiegeResult *SiegeResult `json:"siegeResult,omitempty"` // 攻守复核用
}

// VerifyReply 复核结论（形状对齐 JS：ok/turns/frames/mismatch/resultMismatch/result）。
type VerifyReply struct {
	OK             bool          `json:"ok"`
	Turns          int           `json:"turns"`
	Frames         int           `json:"frames"`
	Mismatch       *Mismatch     `json:"mismatch"`
	ResultMismatch *ResultMis    `json:"resultMismatch"`
	Result         *VerifyResult `json:"result"`
}

type Mismatch struct {
	Turn     int    `json:"turn"`
	Expected string `json:"expected"`
	Got      string `json:"got"`
}

type ResultMis struct {
	Expected string `json:"expected"`
	Got      string `json:"got"`
}

type VerifyResult struct {
	Winner int `json:"winner"`
	Level  int `json:"level"`
}

// VerifyDeterminism 同 seed + 同 orderLog 重放，比对逐帧哈希与终局。
func VerifyDeterminism(rec *VerifyRec) VerifyReply {
	if rec == nil || rec.OrderLog == nil {
		return VerifyReply{OK: false}
	}
	cfg := GameCfg{TickLimit: rec.TickLimit, Ruleset: rec.Ruleset}
	if cfg.TickLimit <= 0 {
		cfg.TickLimit = TickLimit
	}
	if rec.Generator != "" {
		cfg.Generator = rec.Generator
	}
	if rec.Vision != nil {
		cfg.Vision = *rec.Vision
	}
	g, err := NewGame(rec.Seed, cfg)
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
	// 帧哈希必须与指令日志一一对应（NewGame 的第 0 帧 + 每 Step 一帧）。
	// 缺哈希等于「删掉证据」：旧实现会跳过对应的逐帧比对并判 OK。
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
	// 裁判收局（红牌/双方出局）不在 orderLog 里，必须在重放末尾补上，
	// 否则重放不会产生终局，Result 永远为 nil。
	if mismatch == nil && rec.Forfeited != nil {
		g.Forfeit(*rec.Forfeited, "")
	} else if mismatch == nil && rec.EndWinner != nil {
		g.EndGame(*rec.EndWinner, 0, "")
	}
	r := g.Result()
	var resultMismatch *ResultMis
	if mismatch == nil && rec.Result != nil {
		if r == nil {
			resultMismatch = &ResultMis{
				Expected: itoa(rec.Result.Winner) + "/" + itoa(rec.Result.Level),
				Got:      "nil",
			}
		} else if rec.Result.Winner != r.Winner || rec.Result.Level != r.Level {
			resultMismatch = &ResultMis{
				Expected: itoa(rec.Result.Winner) + "/" + itoa(rec.Result.Level),
				Got:      itoa(r.Winner) + "/" + itoa(r.Level),
			}
		}
	}
	vr := (*VerifyResult)(nil)
	if r != nil {
		vr = &VerifyResult{Winner: r.Winner, Level: r.Level}
	}
	return VerifyReply{
		OK:    mismatch == nil && resultMismatch == nil,
		Turns: g.Turn, Frames: len(g.FrameHashes),
		Mismatch: mismatch, ResultMismatch: resultMismatch, Result: vr,
	}
}
