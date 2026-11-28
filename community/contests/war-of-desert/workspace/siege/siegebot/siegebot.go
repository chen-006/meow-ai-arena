// Package siegebot 是攻守实验用的最小 Bot 框架。
//
// 版本：v1.2（攻守变体，-ldflags 注入 v1..v4 风格）。
// 功能：攻/守双参数 BT 评分用的可变体 Bot。
//
// 它刻意不依赖 workspace/botkit：旧 botkit 把 rules.types.range 当像素用，
// 而引擎实际按“世界格”判定射程（d/CELL <= range）。这里统一用：
//
//	rangePx = type.Range * hello.Cell
//	stride  = type.Speed * hello.Cell * 0.1
//
// （引擎移动积分当前用 0.1s/tick）这样 Bot 看到的世界和引擎一致。
package siegebot

import (
	"bufio"
	"encoding/json"
	"fmt"
	"math"
	"os"

	"desertarena/internal/protocol"
)

type Type struct {
	HP        float64 `json:"hp"`
	Speed     float64 `json:"speed"`
	Range     float64 `json:"range"`
	Vision    float64 `json:"vision"`
	Damage    float64 `json:"dmg"`
	ROF       float64 `json:"rof"`
	Pop       int     `json:"pop"`
	Cost      int     `json:"cost"`
	Arc       string  `json:"arc"`
	Armor     string  `json:"armor"`
	Structure bool    `json:"structure"`
}

type Siege struct {
	DefenderSide         int            `json:"defenderSide"`
	AttackerSide         int            `json:"attackerSide"`
	MothershipHP         int            `json:"mothershipHp"`
	AttackerMothershipHP int            `json:"attackerMothershipHp"`
	FortBudget           int            `json:"fortBudget"`
	PlaceTicks           int            `json:"placeTicks"`
	FortCost             map[string]int `json:"fortCost"`
	FortRadius           float64        `json:"fortRadius"`
	FortTypes            []string       `json:"fortTypes"`
	MineRate             float64        `json:"mineRate"`
	MineAmount           float64        `json:"mineAmount"`
	MineSalvage          float64        `json:"mineSalvage"`
	MineRadius           float64        `json:"mineRadius"`
}

type Economy struct {
	Income     float64 `json:"income"`
	MineRate   float64 `json:"mineRate"`
	MineAmount float64 `json:"mineAmount"`
}

type Rules struct {
	TickLimit int             `json:"tickLimit"`
	Types     map[string]Type `json:"types"`
	Economy   Economy         `json:"economy"`
}

type Mine struct {
	ID     int     `json:"id"`
	X      float64 `json:"x"`
	Y      float64 `json:"y"`
	Amount float64 `json:"amount"`
}

type Hello struct {
	ProtoVersion int                 `json:"protoVersion"`
	Mode         string              `json:"mode"`
	Roles        []string            `json:"roles"`
	Seed         int                 `json:"seed"`
	W            int                 `json:"w"`
	H            int                 `json:"h"`
	Cell         int                 `json:"cell"`
	World        int                 `json:"world"`
	Map          protocol.EncodedMap `json:"map"`
	Spawns       [][2]int            `json:"spawns"`
	Mines        []Mine              `json:"mines"`
	Siege        Siege               `json:"siege"`
	Rules        Rules               `json:"rules"`
}

type Unit struct {
	ID    int     `json:"id"`
	Side  int     `json:"side"`
	Kind  string  `json:"kind"`
	X     float64 `json:"x"`
	Y     float64 `json:"y"`
	HP    float64 `json:"hp"`
	MaxHP float64 `json:"maxHp"`
	Mode  string  `json:"mode,omitempty"`
}

type MineState struct {
	ID        int     `json:"id"`
	X         float64 `json:"x"`
	Y         float64 `json:"y"`
	Owner     int     `json:"owner"`
	Contested bool    `json:"contested"`
	Amount    float64 `json:"amount"`
}

type Active struct {
	Kind   string `json:"kind"`
	Remain int    `json:"remain"`
}

type Me struct {
	Role        string         `json:"role"`
	CU          float64        `json:"cu"`
	Pop         int            `json:"pop"`
	PopMax      int            `json:"popMax"`
	Alloc       map[string]int `json:"alloc"`
	AllocTarget map[string]int `json:"allocTarget"`
	Mode        string         `json:"mode"`
	FortBudget  int            `json:"fortBudget"`
	FortsPlaced int            `json:"fortsPlaced"`
	MinesOwned  int            `json:"minesOwned"`
	MineIncome  float64        `json:"mineIncome"`
	Phase       string         `json:"phase"`
	TechDone    []string       `json:"techDone"`
	TechDoing   []struct {
		ID     string `json:"id"`
		Remain int    `json:"remain"`
	} `json:"techDoing"`
	Queue        []string `json:"queue"`
	Active       []Active `json:"active"`
	WeaponsReady int      `json:"weaponsReady"`
	WeaponQueue  []int    `json:"weaponQueue"`
}

type Tick struct {
	Type      string      `json:"type"`
	Turn      int         `json:"turn"`
	TickLimit int         `json:"tickLimit"`
	Side      int         `json:"side"`
	Units     []Unit      `json:"units"`
	Mines     []MineState `json:"mines"`
	Me        Me          `json:"me"`
}

type Action struct {
	ID       int            `json:"id,omitempty"`
	Type     string         `json:"type"`
	X        float64        `json:"x,omitempty"`
	Y        float64        `json:"y,omitempty"`
	TargetID int            `json:"targetId,omitempty"`
	Alloc    map[string]int `json:"alloc,omitempty"`
	Kind     string         `json:"kind,omitempty"`
	Tech     string         `json:"tech,omitempty"`
	Mode     string         `json:"mode,omitempty"`
}

type Field struct {
	Dist []int32
	W, H int
}

type Bot struct {
	Hello    Hello
	Layers   protocol.MapLayers
	Role     string
	Cell     int
	fields   map[int]*Field
	fortIdx  int
	buildIdx int
	// moded 记录已经单独下发过行动模式的单位，避免每拍重复发 mode
	moded map[int]string
	// station 记录每台单位的驻防点，只在没到位时下发一次
	station map[int][2]float64
	// mem 记录上一拍位置与卡住计数，用来发现「引擎不认这一步」
	mem map[int]*unitMem
}

// unitMem 是逐台单位的移动记忆。
type unitMem struct {
	x, y  float64
	stuck int
	swing int
}

// Variant 由 -ldflags -X 注入，用来编译出风格不同的参考 Bot。
// 存在的意义不是「多几个 bot」，而是让 BT 评分有一个真实的选手池可评。
var Variant = "v1"

// RunDual 让同一条 Bot 按引擎给的角色自动切换攻/守逻辑。
func RunDual() error {
	return Run("dual", func(b *Bot, t *Tick) []Action {
		if t.Me.Role == "attacker" {
			return Attack(b, t)
		}
		return Defend(b, t)
	})
}

// assaultNeed 是发起总攻所需的最低战斗单位数。
// 太大（v3）会等太久、剩余时间不够；太小（v2）会以劣势兵力送人头。
func assaultNeed() int {
	switch Variant {
	case "v2":
		return 4
	case "v3":
		return 8
	}
	return 6
}

func attackBuildOrder() []string {
	switch Variant {
	case "v2":
		// 速攻：只堆轻甲，不出远程
		return []string{"LAV", "LAV", "LAV", "AAV", "AAV"}
	case "v3":
		// 重炮：先攒远程，起手慢
		return []string{"LAV", "RAILGUN", "ARTILLERY", "AAV", "RAILGUN"}
	case "v4":
		// 筑垒：偏装甲与防守
		return []string{"LAV", "AAV", "AAV", "RAILGUN"}
	}
	return []string{"LAV", "LAV", "RAILGUN", "AAV", "RAILGUN", "AAV", "ARTILLERY"}
}

func defendBuildOrder() []string {
	switch Variant {
	case "v2":
		return []string{"LAV", "LAV", "AAV", "LAV"}
	case "v3":
		return []string{"AAV", "AAV", "RAILGUN", "LAV"}
	case "v4":
		return []string{"AAV", "LAV", "AAV", "RAILGUN"}
	}
	return []string{"LAV", "AAV", "LAV", "AAV", "RAILGUN"}
}

func Run(role string, decide func(*Bot, *Tick) []Action) error {
	in := bufio.NewReaderSize(os.Stdin, 1<<20)
	line, err := in.ReadString('\n')
	if err != nil {
		return fmt.Errorf("读 hello: %w", err)
	}
	var h Hello
	if err := json.Unmarshal([]byte(line), &h); err != nil {
		return fmt.Errorf("hello 非法: %w", err)
	}
	if h.ProtoVersion != protocol.ProtoVersion {
		return fmt.Errorf("协议版本不匹配: %d", h.ProtoVersion)
	}
	layers, err := protocol.DecodeMapLayers(h.Map)
	if err != nil {
		return fmt.Errorf("地图解码: %w", err)
	}
	b := &Bot{
		Hello: h, Layers: layers, Role: role, Cell: h.Cell,
		fields: map[int]*Field{}, moded: map[int]string{}, station: map[int][2]float64{},
		mem: map[int]*unitMem{},
	}
	out := bufio.NewWriterSize(os.Stdout, 1<<16)
	if _, err := out.WriteString("{\"ready\":true}\n"); err != nil {
		return err
	}
	if err := out.Flush(); err != nil {
		return err
	}
	for {
		line, err := in.ReadString('\n')
		if err != nil {
			return nil
		}
		var probe struct {
			Type string `json:"type"`
		}
		if json.Unmarshal([]byte(line), &probe) != nil {
			continue
		}
		if probe.Type == "end" {
			return nil
		}
		var t Tick
		if err := json.Unmarshal([]byte(line), &t); err != nil {
			continue
		}
		acts := decide(b, &t)
		if acts == nil {
			acts = []Action{}
		}
		raw, _ := json.Marshal(map[string]any{"actions": acts})
		if _, err := out.WriteString(string(raw) + "\n"); err != nil {
			return err
		}
		if err := out.Flush(); err != nil {
			return err
		}
	}
}

/* ---------------- 地图与移动 ---------------- */

func (b *Bot) cellOf(x, y float64) (int, int) {
	gx := int(x / float64(b.Cell))
	gy := int(y / float64(b.Cell))
	if gx < 0 {
		gx = 0
	}
	if gy < 0 {
		gy = 0
	}
	if gx >= b.Hello.W {
		gx = b.Hello.W - 1
	}
	if gy >= b.Hello.H {
		gy = b.Hello.H - 1
	}
	return gx, gy
}

func (b *Bot) walkable(gx, gy int) bool {
	if gx < 0 || gy < 0 || gx >= b.Hello.W || gy >= b.Hello.H {
		return false
	}
	i := gy*b.Hello.W + gx
	return b.Layers.Base[i] != 3
}

func (b *Bot) slopeOK(gx, gy, nx, ny int) bool {
	if !b.walkable(nx, ny) {
		return false
	}
	i := gy*b.Hello.W + gx
	j := ny*b.Hello.W + nx
	d := int(b.Layers.Elev[j]) - int(b.Layers.Elev[i])
	if d < 0 {
		d = -d
	}
	// UNIT_SLOPE = 26 * SCALE
	return d <= 104
}

// FieldTo 从目标点做 BFS，得到全图到目标的步数场。目标不可走时退到最近可走格。
func (b *Bot) FieldTo(tx, ty float64) *Field {
	gx, gy := b.cellOf(tx, ty)
	if !b.walkable(gx, gy) {
		found := false
		for r := 1; r < 12 && !found; r++ {
			for dy := -r; dy <= r && !found; dy++ {
				for dx := -r; dx <= r && !found; dx++ {
					if b.walkable(gx+dx, gy+dy) {
						gx, gy, found = gx+dx, gy+dy, true
					}
				}
			}
		}
		if !found {
			return nil
		}
	}
	key := gy*b.Hello.W + gx
	if f, ok := b.fields[key]; ok {
		return f
	}
	w, h := b.Hello.W, b.Hello.H
	dist := make([]int32, w*h)
	for i := range dist {
		dist[i] = -1
	}
	q := make([]int, 0, w*h)
	start := gy*w + gx
	dist[start] = 0
	q = append(q, start)
	dirs := [8][2]int{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}
	for head := 0; head < len(q); head++ {
		i := q[head]
		cx, cy := i%w, i/w
		for _, d := range dirs {
			nx, ny := cx+d[0], cy+d[1]
			if !b.slopeOK(cx, cy, nx, ny) {
				continue
			}
			j := ny*w + nx
			if dist[j] >= 0 {
				continue
			}
			dist[j] = dist[i] + 1
			q = append(q, j)
		}
	}
	f := &Field{Dist: dist, W: w, H: h}
	b.fields[key] = f
	return f
}

// StepAlong 沿步数场走一步；返回下一个落点。走不动时返回 false。
func (b *Bot) StepAlong(u Unit, f *Field, stride float64) (float64, float64, bool) {
	if f == nil {
		return 0, 0, false
	}
	gx, gy := b.cellOf(u.X, u.Y)
	cur := gy*f.W + gx
	if f.Dist[cur] <= 0 {
		return 0, 0, false
	}
	best, bestD := -1, f.Dist[cur]
	dirs := [8][2]int{{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}}
	for _, d := range dirs {
		nx, ny := gx+d[0], gy+d[1]
		if !b.slopeOK(gx, gy, nx, ny) {
			continue
		}
		j := ny*f.W + nx
		if f.Dist[j] >= 0 && f.Dist[j] < bestD {
			bestD, best = f.Dist[j], j
		}
	}
	if best < 0 {
		return 0, 0, false
	}
	bx, by := best%f.W, best/f.W
	tx := (float64(bx) + 0.5) * float64(b.Cell)
	ty := (float64(by) + 0.5) * float64(b.Cell)
	dx, dy := tx-u.X, ty-u.Y
	d := math.Hypot(dx, dy)
	if d < 1e-6 {
		return tx, ty, true
	}
	if d < stride {
		return tx, ty, true
	}
	return u.X + dx/d*stride, u.Y + dy/d*stride, true
}

func (b *Bot) StepToward(u Unit, tx, ty, stride float64) (float64, float64, bool) {
	dx, dy := tx-u.X, ty-u.Y
	d := math.Hypot(dx, dy)
	if d < 1e-6 {
		return u.X, u.Y, false
	}
	s := math.Min(stride, d)
	nx, ny := u.X+dx/d*s, u.Y+dy/d*s
	gx, gy := b.cellOf(u.X, u.Y)
	nxCell, nyCell := b.cellOf(nx, ny)
	if !b.slopeOK(gx, gy, nxCell, nyCell) {
		return u.X, u.Y, false
	}
	return nx, ny, true
}

// MoveToward 用 BFS 步数场走一步；没有通路时直着蹭过去。
// 墙在步数场里是可走格，撞上去会被 applyMove 拦停，顺便成为射程内最近的靶子。
func (b *Bot) MoveToward(t *Tick, u Unit, tx, ty float64) (float64, float64, bool) {
	if f := b.FieldTo(tx, ty); f != nil {
		if x, y, ok := b.StepAlong(u, f, b.StridePx(u.Kind)); ok {
			return x, y, true
		}
	}
	return b.StepToward(u, tx, ty, b.StridePx(u.Kind))
}

// Advance 朝目标点推进。连续几拍位置没变就改走侧向。
// 引擎的 stepAllowed 按「前方半个格」采样，Bot 的 BFS 按格心建模，
// 个别格子上两者会不一致：BFS 认为能过、引擎过不去，单位就会永久贴住。
// 侧向绕行是最省事的解法——参考 Bot 不需要完美寻路，只需要不卡死。
func (b *Bot) Advance(t *Tick, u Unit, tx, ty float64) (float64, float64, bool) {
	m := b.mem[u.ID]
	if m == nil {
		m = &unitMem{x: u.X, y: u.Y}
		b.mem[u.ID] = m
	}
	if math.Hypot(u.X-m.x, u.Y-m.y) < 0.6 {
		m.stuck++
	} else {
		m.stuck = 0
	}
	m.x, m.y = u.X, u.Y
	if m.stuck <= 3 {
		return b.MoveToward(t, u, tx, ty)
	}
	dx, dy := tx-u.X, ty-u.Y
	d := math.Hypot(dx, dy)
	if d < 1e-6 {
		return 0, 0, false
	}
	ux, uy := dx/d, dy/d
	m.swing++
	sign := 1.0
	if m.swing%2 == 0 {
		sign = -1
	}
	step := b.StridePx(u.Kind) * 2
	for _, a := range []float64{math.Pi / 2, math.Pi / 4, math.Pi * 3 / 4} {
		ang := a * sign
		nx := u.X + (ux*math.Cos(ang)-uy*math.Sin(ang))*step
		ny := u.Y + (ux*math.Sin(ang)+uy*math.Cos(ang))*step
		if x, y, ok := b.StepToward(u, nx, ny, b.StridePx(u.Kind)); ok {
			return x, y, true
		}
	}
	return 0, 0, false
}

func (b *Bot) clampWorld(x, y float64) (float64, float64) {
	c := float64(b.Cell)
	w := float64(b.Hello.World)
	if x < c {
		x = c
	}
	if y < c {
		y = c
	}
	if x > w-c {
		x = w - c
	}
	if y > w-c {
		y = w - c
	}
	return x, y
}

func (b *Bot) TypeOf(kind string) Type { return b.Hello.Rules.Types[kind] }

func (b *Bot) RangePx(kind string) float64 {
	t := b.TypeOf(kind)
	return t.Range * float64(b.Cell)
}

func (b *Bot) StridePx(kind string) float64 {
	t := b.TypeOf(kind)
	if t.Speed <= 0 {
		return 0
	}
	return t.Speed * float64(b.Cell) * 0.1 * 0.85
}

func (b *Bot) Own(t *Tick) []Unit {
	out := make([]Unit, 0, len(t.Units))
	for _, u := range t.Units {
		if u.Side == t.Side && u.HP > 0 {
			out = append(out, u)
		}
	}
	return out
}

func (b *Bot) Enemies(t *Tick) []Unit {
	out := make([]Unit, 0, len(t.Units))
	for _, u := range t.Units {
		if u.Side != t.Side && u.HP > 0 {
			out = append(out, u)
		}
	}
	return out
}

func (b *Bot) Mothership(t *Tick, side int) *Unit {
	for i := range t.Units {
		if t.Units[i].Side == side && t.Units[i].Kind == "MOTHERSHIP" && t.Units[i].HP > 0 {
			return &t.Units[i]
		}
	}
	return nil
}

func (b *Bot) NearestMine(t *Tick, u Unit) *MineState {
	var best *MineState
	bd := math.Inf(1)
	for i := range t.Mines {
		m := &t.Mines[i]
		if m.Amount <= 0 {
			continue
		}
		d := math.Hypot(m.X-u.X, m.Y-u.Y)
		if d < bd {
			bd, best = d, m
		}
	}
	return best
}

func (b *Bot) NearestEnemyWithin(t *Tick, u Unit, maxPx float64, structuresFirst bool) *Unit {
	var best *Unit
	bd := maxPx
	for i := range t.Units {
		e := &t.Units[i]
		if e.Side == u.Side || e.HP <= 0 {
			continue
		}
		if structuresFirst && !b.TypeOf(e.Kind).Structure {
			continue
		}
		d := math.Hypot(e.X-u.X, e.Y-u.Y)
		if d <= bd {
			bd, best = d, e
		}
	}
	return best
}

func (b *Bot) NearestEnemy(t *Tick, u Unit, maxPx float64) *Unit {
	var best *Unit
	bd := maxPx
	for i := range t.Units {
		e := &t.Units[i]
		if e.Side == u.Side || e.HP <= 0 {
			continue
		}
		d := math.Hypot(e.X-u.X, e.Y-u.Y)
		if d <= bd {
			bd, best = d, e
		}
	}
	return best
}

/* ---------------- 通用生产/研究 ---------------- */

func (b *Bot) CanProduce(t *Tick, kind string) bool {
	ty := b.TypeOf(kind)
	if ty.Cost <= 0 {
		return false
	}
	if t.Me.Pop+ty.Pop > t.Me.PopMax {
		return false
	}
	if len(t.Me.Queue)+len(t.Me.Active) >= 4 {
		return false
	}
	return t.Me.CU >= float64(ty.Cost)
}

// ProduceNext 从建造序列里挑「第一件买得起的」。
// 不能只看队首：序列里一旦排到买不起的单位（比如 950 CU 的 ARTILLERY），
// 整个生产链就会停摆，军队规模永远起不来。
func (b *Bot) ProduceNext(t *Tick, order []string) string {
	n := len(order)
	for i := 0; i < n; i++ {
		k := order[(b.buildIdx+i)%n]
		if b.CanProduce(t, k) {
			b.buildIdx = (b.buildIdx + i + 1) % n
			return k
		}
	}
	return ""
}

func (b *Bot) NextTech(t *Tick, order []string) string {
	done := map[string]bool{}
	for _, id := range t.Me.TechDone {
		done[id] = true
	}
	for _, d := range t.Me.TechDoing {
		done[d.ID] = true
	}
	for _, id := range order {
		if done[id] {
			continue
		}
		return id
	}
	return ""
}

// BreachPoint 挑「我方母舰 → 敌方母舰」连线附近最近的工事当突破口。
// 所有突击单位挤到一个点打，而不是沿着城墙均匀铺开——这是攻方能不能破防的关键。
func (b *Bot) BreachPoint(t *Tick, ownM, enemyM *Unit) *Unit {
	if ownM == nil || enemyM == nil {
		return nil
	}
	var best *Unit
	bestCost := math.Inf(1)
	for i := range t.Units {
		e := &t.Units[i]
		if e.Side == ownM.Side || e.HP <= 0 {
			continue
		}
		if e.Kind != "WALL" && e.Kind != "TURRET" {
			continue
		}
		cost := math.Hypot(e.X-ownM.X, e.Y-ownM.Y) + math.Hypot(enemyM.X-e.X, enemyM.Y-e.Y)
		if cost < bestCost {
			bestCost, best = cost, e
		}
	}
	return best
}

/* ---------------- 攻方 ----------------
 * 任务：在时限内摧毁守方母舰。
 * 指挥方式按兵种分工（Bot 只定任务与目标，具体走位/交战归单位 AI）：
 *   AAV        aggressive  装甲矛头，命令只当目标，一路推过去
 *   LAV        neutral     火力队，命令为主：到点、遇敌停步开火
 *   RAILGUN / ARTILLERY  obey  远程火力，Bot 逐台点名 + 卡射程边缘
 * 只有当「服从」模式的单位需要逐台下发 move/attack。 */
func Attack(b *Bot, t *Tick) []Action {
	acts := make([]Action, 0, 24)
	if t.Turn == 0 {
		acts = append(acts,
			Action{Type: "mode", Mode: "neutral"},
			Action{Type: "alloc", Alloc: map[string]int{"produce": 40, "attack": 30, "research": 10, "move": 20}},
		)
	}
	// 军队规模不设固定编制：有 CU 就补，人口/队列满了引擎自然拒绝
	// 远程单位必须排在前面：炮塔射程 216 px，只有 RAILGUN(228)/ARTILLERY(336)
	// 能反制；全靠 LAV 冲脸只会被炮塔白打。
	buildOrder := attackBuildOrder()
	if kind := b.ProduceNext(t, buildOrder); kind != "" {
		acts = append(acts, Action{Type: "produce", Kind: kind})
	}
	if tech := b.NextTech(t, []string{"F1", "A1", "F2", "A2"}); tech != "" && t.Me.CU >= 80 {
		acts = append(acts, Action{Type: "research", Tech: tech})
	}

	enemyM := b.Mothership(t, 0)
	ownM := b.Mothership(t, t.Side)
	// 导航目标用敌方母舰：它在工事圈里，BFS 场一定从走廊可达；
	// 具体某面墙可能从攻方一侧根本走不到（山脉隔断），拿它当目标会让全军卡死。
	nav := enemyM
	breach := b.BreachPoint(t, ownM, enemyM)
	if breach == nil {
		breach = enemyM
	}
	// 集结门槛：兵力不够就先在集结点等，不要一台一台送进炮塔火力网。
	// 试过抬到 9 台 + 必须有远程：更差 —— 等太久，剩下的时间不够打完。
	combat := 0
	for _, u := range b.Own(t) {
		if u.Kind != "MOTHERSHIP" && u.Kind != "SALVAGER" {
			combat++
		}
	}
	assault := combat >= assaultNeed()

	for _, u := range b.Own(t) {
		switch u.Kind {
		case "MOTHERSHIP":
			if e := b.NearestEnemyWithin(t, u, b.RangePx(u.Kind)*0.95, false); e != nil {
				acts = append(acts, Action{ID: u.ID, Type: "attack", TargetID: e.ID})
			}
			continue
		case "SALVAGER":
			// 采集车按命令走矿场；无武器，三种模式对它等价
			if m := b.NearestMine(t, u); m != nil {
				if x, y, ok := b.Advance(t, u, m.X, m.Y); ok {
					acts = append(acts, Action{ID: u.ID, Type: "move", X: x, Y: y})
				}
			}
			continue
		}

		// 兵种分工：只给一次模式，之后靠单位 AI 维持
		want := "neutral"
		if u.Kind == "AAV" || u.Kind == "LAV" {
			want = "aggressive"
		}
		if u.Kind == "RAILGUN" || u.Kind == "ARTILLERY" {
			want = "obey"
		}
		if b.moded[u.ID] != want {
			acts = append(acts, Action{ID: u.ID, Type: "mode", Mode: want})
			b.moded[u.ID] = want
		}

		// 远程火力组：不参与集结，直接去卡射程边缘
		if want == "obey" {
			rng := b.RangePx(u.Kind)
			if e := b.NearestEnemyWithin(t, u, rng*0.9, true); e != nil {
				acts = append(acts, Action{ID: u.ID, Type: "attack", TargetID: e.ID})
				continue
			}
			next := breach
			if inRange := b.NearestEnemyWithin(t, u, 1e9, true); inRange != nil {
				next = inRange
			}
			if next != nil {
				d := math.Hypot(next.X-u.X, next.Y-u.Y)
				if d > rng*0.8 {
					dx, dy := next.X-u.X, next.Y-u.Y
					n := math.Hypot(dx, dy)
					if n == 0 {
						n = 1
					}
					if x, y, ok := b.Advance(t, u, next.X-dx/n*rng*0.75, next.Y-dy/n*rng*0.75); ok {
						acts = append(acts, Action{ID: u.ID, Type: "move", X: x, Y: y})
					}
				}
				continue
			}
			if enemyM != nil {
				if x, y, ok := b.Advance(t, u, enemyM.X, enemyM.Y); ok {
					acts = append(acts, Action{ID: u.ID, Type: "move", X: x, Y: y})
				}
			}
			continue
		}

		// 未集结：战斗单位去集结点待命
		if !assault && ownM != nil && enemyM != nil {
			rx := ownM.X + (nav.X-ownM.X)*0.5
			ry := ownM.Y + (nav.Y-ownM.Y)*0.5
			if math.Hypot(rx-u.X, ry-u.Y) > float64(b.Cell)*2 {
				if x, y, ok := b.Advance(t, u, rx, ry); ok {
					acts = append(acts, Action{ID: u.ID, Type: "move", X: x, Y: y})
				}
			}
			continue
		}

		// 突击：给目标点就够，怎么打由单位 AI 决定
		if nav == nil {
			continue
		}
		if x, y, ok := b.Advance(t, u, nav.X, nav.Y); ok {
			acts = append(acts, Action{ID: u.ID, Type: "move", X: x, Y: y})
		}
	}
	return acts
}

/* ---------------- 守方 ---------------- */

type Fort struct {
	Kind string
	X, Y float64
}

func (b *Bot) fortPlan(t *Tick) []Fort {
	m := b.Mothership(t, t.Side)
	if m == nil {
		return nil
	}
	out := make([]Fort, 0, 24)
	add := func(kind string, x, y float64) {
		if x < float64(b.Cell) || y < float64(b.Cell) || x > float64(b.Hello.World-b.Cell) || y > float64(b.Hello.World-b.Cell) {
			return
		}
		out = append(out, Fort{Kind: kind, X: x, Y: y})
	}
	// 炮塔先放：它是守方唯一能超距压制 LAV 的火力，预算优先给它，
	// 这样「预算越多 → 火力越强」才是单调的。v4「筑垒」反过来先堆墙。
	turretFirst := Variant != "v4"
	if turretFirst {
		for i := 0; i < 6; i++ {
			a := float64(i)*math.Pi/3 + math.Pi/6
			add("TURRET", m.X+math.Cos(a)*155, m.Y+math.Sin(a)*155)
		}
	}
	// 墙只放一圈（r=130）。旧版预算有剩时会补一圈 r=110 的内墙，
	// 内墙会把突击单位堵在炮口下 —— 结果 16 点预算比 18 点还难打，
	// 预算与强度的关系直接翻转，没法用来标定。
	for i := 0; i < 12; i++ {
		a := float64(i) * math.Pi / 6
		add("WALL", m.X+math.Cos(a)*130, m.Y+math.Sin(a)*130)
	}
	if !turretFirst {
		for i := 0; i < 6; i++ {
			a := float64(i)*math.Pi/3 + math.Pi/6
			add("TURRET", m.X+math.Cos(a)*155, m.Y+math.Sin(a)*155)
		}
	}
	return out
}

/* 守方任务：在时限内保住母舰。
 * 指挥方式：全队「命令为主」（neutral）——只下发驻防点，到位后由单位 AI 自行
 * 停步开火/高差后撤；只有 2 台机动哨兵由 Bot 逐台微操（obey）补墙缺口。 */
func Defend(b *Bot, t *Tick) []Action {
	acts := make([]Action, 0, 24)
	if t.Turn == 0 {
		acts = append(acts,
			Action{Type: "mode", Mode: "neutral"},
			Action{Type: "alloc", Alloc: map[string]int{"produce": 35, "attack": 35, "research": 10, "move": 20}},
		)
	}
	// 布置阶段：每 tick 放一个工事；预算用完就停。
	if t.Me.Phase == "place" && t.Me.FortBudget > 0 {
		plan := b.fortPlan(t)
		idx := b.fortIdx
		if idx < t.Me.FortsPlaced {
			idx = t.Me.FortsPlaced
		}
		if idx < len(plan) {
			f := plan[idx]
			acts = append(acts, Action{Type: "fort", Kind: f.Kind, X: f.X, Y: f.Y})
			b.fortIdx = idx + 1
		}
	}
	// 军队规模不设固定编制：守方靠矿场 + 基础后勤持续补兵
	defOrder := defendBuildOrder()
	if kind := b.ProduceNext(t, defOrder); kind != "" {
		acts = append(acts, Action{Type: "produce", Kind: kind})
	}
	if t.Me.WeaponsReady < 2 && t.Me.CU >= 120 {
		acts = append(acts, Action{Type: "weapon"})
	}
	if tech := b.NextTech(t, []string{"A1", "F1", "A2"}); tech != "" && t.Me.CU >= 80 {
		acts = append(acts, Action{Type: "research", Tech: tech})
	}
	m := b.Mothership(t, t.Side)
	if m == nil {
		return acts
	}
	for _, u := range b.Own(t) {
		switch u.Kind {
		case "MOTHERSHIP":
			if e := b.NearestEnemyWithin(t, u, b.RangePx(u.Kind)*0.95, false); e != nil {
				acts = append(acts, Action{ID: u.ID, Type: "attack", TargetID: e.ID})
			}
			continue
		case "SALVAGER":
			// 守方采集车不出墙，留在母舰旁
			continue
		}
		// 其余全是「命令为主」：下一个驻防点，到位后不再逐台指挥
		st, seen := b.station[u.ID]
		if !seen {
			a := float64(u.ID) * 2.399
			sx, sy := b.clampWorld(m.X+math.Cos(a)*130, m.Y+math.Sin(a)*130)
			st = [2]float64{sx, sy}
			b.station[u.ID] = st
		}
		if math.Hypot(st[0]-u.X, st[1]-u.Y) > float64(b.Cell)*1.5 {
			if x, y, ok := b.Advance(t, u, st[0], st[1]); ok {
				acts = append(acts, Action{ID: u.ID, Type: "move", X: x, Y: y})
			}
		}
	}
	return acts
}
