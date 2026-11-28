// 战斗与行为层：索敌、开火、伤害结算、移动与意图绑定。
//
// 版本：v1.2（Desert Arena · Go 原生引擎 internal/sim）。
// 功能：指令归一化（normalizeOrders→orderLog）、Step 主循环的阵营动作与
// 单位行为、直击/溅射伤害、克制表应用、kill/spawn/vp 事件产出。
// 管线顺序逐条对齐 JS 合并版，禁止单方面重排。
package sim

import (
	"math"
	"sort"
)

// 战斗与终局（rules.js:1328-1791 的移植）：移动、弹道胶水、开火循环、
// 炮弹推进、VP、七领域计分、五级判定链、step 管线。

// applyMove 一步移动：坡度系数 × 地表系数 × TICK_MS/1000 时间基准。
func (g *Game) applyMove(u *Unit, tx, ty float64) {
	dx, dy := tx-u.X, ty-u.Y
	d := math.Hypot(dx, dy)
	if d < 2.5 {
		return
	}
	here := Ci(GxOf(u.X), GyOf(u.Y))
	aheadX := math.Floor((u.X + (dx/d)*Cell) / Cell)
	aheadY := math.Floor((u.Y + (dy/d)*Cell) / Cell)
	aheadX = Clamp(aheadX, 0, Grid-1)
	aheadY = Clamp(aheadY, 0, Grid-1)
	ahead := Ci(int(aheadX), int(aheadY))
	// 24 级 = 100% 坡度；格变细后除数跟缩
	slope := (float64(g.Map.Elev[ahead]) - float64(g.Map.Elev[here])) / (24.0 / Scale)
	f := Clamp(1-0.55*math.Max(0, slope)+0.18*math.Max(0, -slope), 0.35, 1.30)
	b := int(g.Map.Base[here])
	if b < len(BaseSurface) && BaseSurface[b] != 0 {
		f *= BaseSurface[b]
	}
	S := g.Sides[u.Side]
	speed := g.ut(u.Kind).Speed * (1 + S.Mods.Speed)
	if g.v15() && u.Relic != nil {
		speed *= RelicCarrySpeedMul
	}
	// DoK：母舰固定慢速；时间基准 = TICK_MS/1000（v0.11 修复，不写死 0.1s）
	travel := math.Min(speed*Cell*(float64(TickMS)/1000)*f, d)
	steps := int(math.Max(1, math.Ceil(travel/(Cell*0.5))))
	step := travel / float64(steps)
	for s := 0; s < steps; s++ {
		nx := u.X + (dx/d)*step
		ny := u.Y + (dy/d)*step
		allowed := StepAllowed(g.Map, u.Kind, u.X, u.Y, nx, ny)
		// v1.7：研究舰是悬浮舰，允许跨越水面/崖壁，避免一侧研究舰被地形卡死
		// 造成遗产科技 tie-break 的系统性座位偏差。
		if g.v15() && u.Kind == "RESEARCH" {
			allowed = true
		}
		if !allowed {
			// v1.6：斜向被挡时尝试沿单轴滑动，避免在河岸/崖角永久卡死。
			// v1.4 保持历史行为，避免改正式赛语义。
			if g.v15() {
				if StepAllowed(g.Map, u.Kind, u.X, u.Y, nx, u.Y) {
					u.X = nx
					continue
				}
				if StepAllowed(g.Map, u.Kind, u.X, u.Y, u.X, ny) {
					u.Y = ny
					continue
				}
			}
			break
		}
		u.X = nx
		u.Y = ny
	}
}

// groundH 地形高度（m）：炮口/目标的离地高度。
func (g *Game) groundH(x, y float64) float64 {
	return ElevM * float64(g.Map.Elev[Ci(GxOf(x), GyOf(y))])
}

// solidH 弹道碰撞高度：只有山脉是实体。
func (g *Game) solidH(x, y float64) float64 {
	if g.Map.Base[Ci(GxOf(x), GyOf(y))] == 3 {
		return MountainH
	}
	return 0
}

func (g *Game) muzzleH(u *Unit) float64 {
	return g.groundH(u.X, u.Y) + UnitH
}

// v0Of 初速：基础初速 × 弹种倍率 × (1 + 发射药科技)。
func (g *Game) v0Of(u *Unit, shellID string) float64 {
	t := g.ut(u.Kind)
	S := g.Sides[u.Side]
	sh, ok := Shells[shellID]
	if !ok {
		sh = Shells["AP"]
	}
	return t.V0 * sh.VMul * (1 + S.Mods.Muzzle)
}

// shellsOf 已解锁弹种。
func (g *Game) shellsOf(side int) []string {
	out := []string{}
	for _, id := range ShellIDs {
		req := Shells[id].Unlock
		if req == nil || g.Sides[side].TechDone[*req] {
			out = append(out, id)
		}
	}
	return out
}

// pickShell 自动选弹：对本目标装甲等级期望最高；溅射弹给 0.12 固定加成。
func (g *Game) pickShell(side int, target *Unit) string {
	best, bestScore := "AP", -1.0
	for _, id := range g.shellsOf(side) {
		sh := Shells[id]
		armor := sh.ArmorOf(g.ut(target.Kind).Armor)
		score := sh.D0*armor + boolF(sh.Splash > 0)*0.12
		if score > bestScore {
			bestScore = score
			best = id
		}
	}
	return best
}

func boolF(b bool) float64 {
	if b {
		return 1
	}
	return 0
}

type shotSolution struct {
	ok      bool
	reason  string
	theta   float64
	t       float64
	vImpact float64
	apex    float64
	aimX    float64
	aimY    float64
	dx, dy  float64
}

// solveShot 解一发：目标在飞，迭代两次用飞行时间估提前量。
func (g *Game) solveShot(u *Unit, shellID string, ex, ey, evx, evy float64) shotSolution {
	t := g.ut(u.Kind)
	h0 := g.muzzleH(u)
	v0 := g.v0Of(u, shellID)
	// 提前量：先夹速度，再夹偏移量（防瞬移目标飞出包线）
	vCap := 12.0 * Cell
	lvx := Clamp(evx, -vCap, vCap)
	lvy := Clamp(evy, -vCap, vCap)
	dRaw := math.Hypot(ex-u.X, ey-u.Y)
	if dRaw < MPerPX*2 {
		return shotSolution{ok: false, reason: "too-close"}
	}
	tGuess := 0.6
	var sol SolveResult
	aimX, aimY, dx, dy := ex, ey, 1.0, 0.0
	for it := 0; it < 2; it++ {
		offX, offY := lvx*tGuess, lvy*tGuess
		offLen := math.Hypot(offX, offY)
		maxOff := dRaw * 0.5
		k := 1.0
		if offLen > maxOff {
			k = maxOff / offLen
		}
		aimX = ex + offX*k
		aimY = ey + offY*k
		dPx := math.Hypot(aimX-u.X, aimY-u.Y)
		if dPx < MPerPX*2 {
			return shotSolution{ok: false, reason: "too-close"}
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
			return shotSolution{ok: false, reason: sol.Reason}
		}
		tGuess = sol.T
	}
	return shotSolution{ok: true, theta: sol.Theta, t: sol.T, vImpact: sol.VImpact,
		apex: sol.Apex, aimX: aimX, aimY: aimY, dx: dx, dy: dy}
}

// weaponCount 母舰伤害乘「已造好的炮数」。
func (g *Game) weaponCount(u *Unit) int {
	if u.Kind == "MOTHERSHIP" {
		return g.Sides[u.Side].WeaponsReady
	}
	return 1
}

// fire 开火：解一次弹道，按炮数生成若干发（多门炮 = 同一解轻微散布）。
func (g *Game) fire(u, e *Unit) bool {
	t := g.ut(u.Kind)
	S := g.Sides[u.Side]
	if g.weaponCount(u) <= 0 {
		return false
	}
	d := math.Hypot(u.X-e.X, u.Y-e.Y) / Cell
	lim := t.Range * (1 + S.Mods.Range)
	if u.Kind == "MOTHERSHIP" {
		lim *= RangeMulBase + RangeMulSpan*float64(S.Alloc.Range)/100
	}
	if d > lim {
		return false
	}
	shellID := g.pickShell(u.Side, e)
	ex, ey := e.X, e.Y
	if g.v15() {
		// 炮塔锁定：旧目标还活着且在射程内就不换，否则炮塔会在两个
		// 距离相近的目标之间来回摆，永远对不准。锁 60 拍仍打不出去就换。
		if u.AimTargetID != 0 && g.Turn-u.AimLockTick > 60 {
			u.AimTargetID = 0
			u.TurretDir = 0
		}
		if u.AimTargetID != 0 && u.AimTargetID != e.ID {
			old := g.unitByID(u.AimTargetID)
			if old != nil && !old.Dead && math.Hypot(old.X-u.X, old.Y-u.Y)/Cell <= lim {
				return false
			}
			u.AimTargetID = 0
			u.TurretDir = 0
		}
		if u.AimTargetID == 0 {
			u.AimTargetID = e.ID
			u.AimLockTick = g.Turn
		}
		if !g.aimV15(u, e.X, e.Y) {
			return false
		}
		ex, ey = g.applyCEP(u, e.X, e.Y)
	}
	sol := g.solveShot(u, shellID, ex, ey, e.VX, e.VY)
	if !sol.ok {
		return false
	}
	n := int(math.Max(1, float64(g.weaponCount(u))))
	for i := 0; i < n; i++ {
		spread := 0.0
		if n > 1 {
			spread = (float64(i) - (float64(n)-1)/2) * 0.4
		}
		g.spawnShot(u, shellID, sol, spread, e.ID, 0)
	}
	S.Shots += n
	return true
}

// fireAtFlag 对旗开火。旗不是单位，弹道只做直线命中判定。
func (g *Game) fireAtFlag(u *Unit, f *Flag) bool {
	t := g.ut(u.Kind)
	S := g.Sides[u.Side]
	d := math.Hypot(u.X-f.X, u.Y-f.Y) / Cell
	lim := t.Range * (1 + S.Mods.Range)
	if d > lim {
		return false
	}
	shellID := "AP"
	fx, fy := f.X, f.Y
	if g.v15() {
		if !g.aimV15(u, f.X, f.Y) {
			return false
		}
		fx, fy = g.applyCEP(u, f.X, f.Y)
	}
	sol := g.solveShot(u, shellID, fx, fy, 0, 0)
	if !sol.ok {
		return false
	}
	g.spawnShot(u, shellID, sol, 0, 0, f.ID)
	S.Shots++
	return true
}

func (g *Game) spawnShot(u *Unit, shellID string, sol shotSolution, spreadDeg float64, targetID, targetFlagID int) {
	v0 := g.v0Of(u, shellID)
	st := MakeShellState(v0, sol.theta+spreadDeg, g.muzzleH(u))
	g.Projectiles = append(g.Projectiles, &Projectile{
		ID: g.NextShot, Side: u.Side, SrcKind: u.Kind, Shell: shellID,
		SX: u.X, SY: u.Y, DX: sol.dx, DY: sol.dy,
		S: 0, H: st.H, VX: st.Vx, VH: st.Vh, T: 0,
		V0: v0, TargetID: targetID, TargetFlagID: targetFlagID, ByUnitID: u.ID,
	})
	g.NextShot++
}

// hitUnit 伤害结算：落速 + 弹种 + 装甲 + 兵种克制 + 四子系统。
func (g *Game) hitUnit(p *Projectile, target *Unit, factor float64) {
	t := g.ut(p.SrcKind)
	dmg := DamageOf(p.Shell, t.Dmg, t.V0, p.VImpact, g.ut(target.Kind).Armor)
	// 克制倍率：显式 0 必须保持 0（SCOUT/SALVAGER 无武器）
	if row, ok := RPS[p.SrcKind]; ok {
		if mul, ok := row[target.Kind]; ok {
			dmg *= mul
		}
	}
	// v1.8.2：远程攻坚兵对母舰额外 ×1.5，让拆舰路线在一局内可完成。
	if g.v15() && target.Kind == "MOTHERSHIP" && (p.SrcKind == "RAILGUN" || p.SrcKind == "ARTILLERY") {
		dmg *= MothershipSiegeMul
	}
	dmg *= factor
	// 四子系统：armour 减伤（目标母舰）、attack 加伤（来源母舰）
	if target.Kind == "MOTHERSHIP" {
		dmg *= 1 - ArmourReduce*(float64(g.Sides[target.Side].Alloc.Armour)/100)
	}
	if p.SrcKind == "MOTHERSHIP" {
		dmg *= AttackDmgBase + float64(g.Sides[p.Side].Alloc.Attack)/100
	}
	if g.v15() {
		dmg *= g.vetDmgMul(g.unitByID(p.ByUnitID)) * V15DamageMul
		dmg *= 1 + g.Sides[p.Side].Mods.Damage + g.overcharge(p.Side)
	}
	g.damageUnit(target, dmg, p.Side, p.ByUnitID)
}

func (g *Game) damageUnit(e *Unit, dmg float64, bySide, byUnitID int) {
	if e.Dead {
		return
	}
	if g.v15() {
		dmg *= g.vetDR(e)
		dmg *= 1 - Clamp(g.Sides[e.Side].Mods.DR, 0, 0.5)
	}
	e.LastHit = g.Turn
	e.HP -= dmg
	if e.HP > 0 {
		return
	}
	e.Dead = true
	if g.v15() && e.Kind == "RESEARCH" {
		g.cancelResearchByShip(e.Side, e.ID)
	}
	def := g.ut(e.Kind)
	if def.Pop > 0 {
		g.Sides[e.Side].Pop = int(math.Max(0, float64(g.Sides[e.Side].Pop-def.Pop)))
	}
	g.Sides[e.Side].Losses++
	g.Sides[bySide].Kills++
	g.Sides[bySide].KillValue += def.Cost
	if g.v15() {
		killer := g.unitByID(byUnitID)
		if killer != nil {
			killer.Kills++
		}
		g.addKillVS(bySide, killer, e.Kind)
		g.addKillXP(killer, e.Kind)
		if e.Kind == "SALVAGER" {
			g.dropRelic(e)
		}
	}
	ev := Event{T: g.Turn, Type: "kill", By: bySide, ID: e.ID, Kind: e.Kind}
	g.LastEvents = append(g.LastEvents, ev)
	g.Events = append(g.Events, ev)
}

// explode 一发落地：直击结算 → 溅射。
func (g *Game) explode(p *Projectile, x, y float64, direct *Unit) {
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

// stepShots 炮弹推进：每 tick 走 TICK_MS，内部 5 个 10ms 子步，线段 vs 单位圆判命中。
func (g *Game) stepShots() {
	if len(g.Projectiles) == 0 {
		return
	}
	dt := float64(TickMS) / 1000
	const SUB = 5
	projs := g.Projectiles
	if g.v15() {
		// v1.6：炮弹结算也按交替座位顺序，避免固定 side0 先结算命中。
		ordered := make([]*Projectile, 0, len(g.Projectiles))
		for _, s := range g.sideOrder() {
			for _, p := range g.Projectiles {
				if p.Side == s {
					ordered = append(ordered, p)
				}
			}
		}
		projs = ordered
	}
	for _, p := range projs {
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
			// 1) 撞山
			if solid := g.solidH(x1, y1); solid > 0 && p.H <= solid {
				p.VImpact = ShellSpeed(ShellState{Vx: p.VX, Vh: p.VH})
				g.explode(p, x1, y1, nil)
				break
			}
			// 2) 落地
			if p.H <= 0 {
				p.VImpact = ShellSpeed(ShellState{Vx: p.VX, Vh: p.VH})
				g.explode(p, x1, y1, nil)
				break
			}
			// 3) 命中单位：线段到圆心最近距离 < 命中半径
			var hit *Unit
			for _, u := range g.Units {
				if u.Dead || u.Side == p.Side {
					// 直击只打敌人（友军只吃溅射）
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
			// 3b) v1.5 旗目标：旗不是单位，单独做线段-圆判定。
			if g.v15() && p.TargetFlagID != 0 {
				if f := g.flagByID(p.TargetFlagID); f != nil {
					vx, vy := x1-x0, y1-y0
					len2 := vx*vx + vy*vy
					tt := 0.0
					if len2 > 0 {
						tt = ((f.X-x0)*vx + (f.Y-y0)*vy) / len2
					}
					tt = math.Max(0, math.Min(1, tt))
					cx, cy := x0+vx*tt, y0+vy*tt
					if math.Hypot(f.X-cx, f.Y-cy) <= HitRM/MPerPX {
						hAt := h0 + (p.H-h0)*tt
						if math.Abs(hAt-(g.groundH(f.X, f.Y)+UnitH)) <= 8 {
							p.VImpact = ShellSpeed(ShellState{Vx: p.VX, Vh: p.VH})
							fv := DamageOf(p.Shell, g.ut(p.SrcKind).Dmg, g.ut(p.SrcKind).V0, p.VImpact, "heavy")
							g.damageFlag(p, f, fv)
							p.Dead = true
							break
						}
					}
				}
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

// updateVP VP 占控结算。
func (g *Game) updateVP() {
	for _, vp := range g.Vps {
		c0, c1 := 0, 0
		for _, u := range g.Units {
			if u.Dead || u.Kind == "MOTHERSHIP" {
				continue
			}
			if math.Hypot(u.X-vp.X, u.Y-vp.Y) <= VPRadius {
				if u.Side == 0 {
					c0++
				} else {
					c1++
				}
			}
		}
		g.Sides[0].ControlTicks += c0
		g.Sides[1].ControlTicks += c1
		prev := vp.Owner
		vp.Contested = c0 > 0 && c1 > 0
		vp.Owner = -1
		if vp.Contested {
			vp.Owner = -1
		} else if c0 > 0 {
			vp.Owner = 0
		} else if c1 > 0 {
			vp.Owner = 1
		}
		if vp.Owner >= 0 {
			if !g.v15() {
				g.Sides[vp.Owner].VPScore++
			}
			g.Sides[vp.Owner].VPTicks++
		}
		if prev != vp.Owner {
			ev := Event{T: g.Turn, Type: "vp", ID: vp.ID, Owner: vp.Owner}
			g.LastEvents = append(g.LastEvents, ev)
			g.Events = append(g.Events, ev)
		}
	}
}

// computeCards 七领域确定性计分（墙钟耗时只作诊断，不进计分）。
func (g *Game) computeCards() [2]Card {
	denom := math.Max(1, float64(g.Turn))
	var out [2]Card
	for s := 0; s < 2; s++ {
		S := g.Sides[s]
		cv := g.Mothership(s)
		aliveCost := 0.0
		for _, u := range g.Units {
			if u.Side == s && u.Kind != "MOTHERSHIP" {
				aliveCost += float64(Types[u.Kind].Cost)
			}
		}
		avgBot := 0.0
		if g.BotCalls[s] > 0 {
			avgBot = g.BotMS[s] / float64(g.BotCalls[s])
		}
		f := []float64{
			100 * Clamp(float64(S.VPTicks)/(float64(len(g.Vps))*denom), 0, 1),
			100 * Clamp(float64(S.KillValue)/math.Max(1, float64(g.Sides[1-s].InitialCost)), 0, 1),
			100 * Clamp(aliveCost/math.Max(1, float64(S.ProducedCost)), 0, 1),
		}
		msHP := 0.0
		if cv != nil {
			msHP = Clamp(cv.HP/cv.MaxHP, 0, 1)
		}
		f = append(f,
			100*msHP,
			Clamp(50+10*float64(S.Kills-S.Losses), 0, 100),
			100*Clamp(float64(S.ControlTicks)/(denom*float64(len(g.Vps))*1.5), 0, 1),
			100*Clamp((float64(S.VPScore)+float64(S.KillValue)/60)/(denom/6), 0, 1),
		)
		total := 0.0
		for i := 0; i < 7; i++ {
			total += f[i] * Weights[i]
		}
		out[s] = Card{Fields: f, Total: total, VPScore: S.VPScore, Kills: S.Kills, Losses: S.Losses, BotMS: avgBot}
	}
	return out
}

// EndGame 由裁判直接收局（双方同时失去资格时用）。
func (g *Game) EndGame(winner, level int, reason string) { g.endGame(winner, level, reason) }

func (g *Game) endGame(winner, level int, reason string) {
	if g.ResultData != nil {
		return
	}
	cards := g.computeCards()
	if g.v15() {
		cards = g.computeCardsV15()
	}
	techs := [2][]string{sortStrings(keysOf(g.Sides[0].TechDone)), sortStrings(keysOf(g.Sides[1].TechDone))}
	score := [2]int{g.Sides[0].VPScore, g.Sides[1].VPScore}
	var vs *[2]VictoryScore
	if g.v15() {
		score = [2]int{g.Sides[0].VS.Total, g.Sides[1].VS.Total}
		vs = &[2]VictoryScore{g.Sides[0].VS, g.Sides[1].VS}
	}
	g.ResultData = &Result{
		Winner: winner, Level: level, Reason: reason, Turn: g.Turn, Seed: g.Seed,
		Mode: "duel", Players: g.Players, Turns: g.Turn, MapAttempt: g.Map.Attempt,
		Score: score,
		VP:    score,
		Cards: cards, Events: len(g.Events),
		Alloc: [2]Alloc{g.Sides[0].Alloc, g.Sides[1].Alloc},
		Techs: techs,
		Shots: [2]int{g.Sides[0].Shots, g.Sides[1].Shots},
		Hits:  [2]int{g.Sides[0].Hits, g.Sides[1].Hits},
		VS:    vs,
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
		g.ResultData.Decide[s] = d
	}
}

func roundTo(v float64, digits int) float64 {
	p := math.Pow(10, float64(digits))
	return math.Round(v*p) / p
}

func keysOf(m map[string]bool) []string {
	out := make([]string, 0, len(m))
	for k := range m {
		out = append(out, k)
	}
	return out
}

// Forfeit 判负（红牌）。
func (g *Game) Forfeit(seat int, reason string) {
	if g.ResultData != nil {
		return
	}
	g.Forfeited = seat
	if reason == "" {
		reason = "故障出局：连续失败达到上限（红牌）"
	}
	g.endGame(1-seat, 0, reason)
}

// checkEnd 五级判定链（rules.js:1621-1638）。
func (g *Game) checkEnd() {
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
		g.endGame(-1, 1, "双方母舰同时被摧毁，判为平局")
		return
	}
	if dead0 {
		g.endGame(1, 1, "红方母舰被摧毁")
		return
	}
	if dead1 {
		g.endGame(0, 1, "蓝方母舰被摧毁")
		return
	}
	if g.v15() {
		g.checkEndV15(c0, c1)
		return
	}
	if g.Turn >= g.Cfg.TickLimit {
		a, b := g.Sides[0], g.Sides[1]
		if a.VPScore != b.VPScore {
			if a.VPScore > b.VPScore {
				g.endGame(0, 2, "时间到 · 按 VP 累计分")
			} else {
				g.endGame(1, 2, "时间到 · 按 VP 累计分")
			}
			return
		}
		kd0, kd1 := a.Kills-a.Losses, b.Kills-b.Losses
		if kd0 != kd1 {
			if kd0 > kd1 {
				g.endGame(0, 3, "时间到 · VP 相同，按 KD 差")
			} else {
				g.endGame(1, 3, "时间到 · VP 相同，按 KD 差")
			}
			return
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
			return
		}
		g.endGame(-1, 5, "时间到 · 全部相同，平局")
	}
}

// Result 终局记录（未结束则先跑判定）。
func (g *Game) Result() *Result {
	if g.ResultData == nil {
		g.checkEnd()
	}
	return g.ResultData
}

// normalizeOrders 归一化一方的指令（进 orderLog 的形态）。
func normalizeOrders(orders []Action) []Action {
	out := make([]Action, 0, len(orders))
	for _, a := range orders {
		switch a.Type {
		case "alloc":
			if a.Alloc != nil {
				ac := *a.Alloc
				out = append(out, Action{Type: "alloc", Alloc: &ac})
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
		case "harvest":
			out = append(out, Action{Type: "harvest", ID: a.ID, ResID: a.ResID})
		case "relic":
			out = append(out, Action{Type: "relic", ID: a.ID, RelicID: a.RelicID})
		case "attackFlag":
			out = append(out, Action{Type: "attackFlag", ID: a.ID, FlagID: a.FlagID})
		case "face":
			out = append(out, Action{Type: "face", ID: a.ID, Deg: a.Deg})
		case "weapon":
			out = append(out, Action{Type: "weapon"})
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

// Step 推进一回合（orders[i] 可为 nil）。管线顺序逐条对齐 JS。
// sideOrder v1.6：v15+ 每拍交替阵营结算顺序，消除固定 side0 先手。
func (g *Game) sideOrder() [2]int {
	if g.v15() && g.Turn%2 == 1 {
		return [2]int{1, 0}
	}
	return [2]int{0, 1}
}

func (g *Game) Step(orders [2][]Action) *Frame {
	if g.Done() {
		return g.buildFrame()
	}
	// 记录归一化指令（确定性重放的全部外部输入）
	rec := [2][]Action{normalizeOrders(orders[0]), normalizeOrders(orders[1])}
	g.OrderLog = append(g.OrderLog, OrderLogEntry{T: g.Turn + 1, O: rec})
	g.Turn++
	g.LastEvents = nil

	// 1. 阵营动作
	for _, s := range g.sideOrder() {
		for i := range orders[s] {
			a := &orders[s][i]
			switch a.Type {
			case "alloc":
				if a.Alloc != nil {
					g.Sides[s].Target = *a.Alloc
				}
			case "produce":
				if a.By != nil {
					var host *Unit
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
					host.Queue = append(host.Queue, ProdJob{Kind: a.Kind, Remain: float64(g.ut(a.Kind).Build)})
				} else {
					g.Sides[s].Queue = append(g.Sides[s].Queue, a.Kind)
				}
			case "weapon":
				S := g.Sides[s]
				if S.WeaponsReady+len(S.WeaponQueue) >= MaxWeapons {
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
				def := g.techTable()[a.Tech]
				g.Sides[s].CU -= float64(def.CU)
				g.Sides[s].TechDoing = append(g.Sides[s].TechDoing, TechJob{ID: a.Tech, Remain: float64(def.Tick)})
				ev := Event{T: g.Turn, Type: "research-start", Side: s, Kind: a.Tech}
				g.LastEvents = append(g.LastEvents, ev)
				g.Events = append(g.Events, ev)
			case "mode":
				if !inList(Modes, a.Mode) {
					g.noteRejected(s, 0, "mode: unknown "+a.Mode)
					continue
				}
				if a.ID != nil {
					var one *Unit
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
			case "relicresearch":
				if !g.v15() || a.ID == nil {
					continue
				}
				if u := g.unitByID(*a.ID); u != nil && !u.Dead && u.Side == s && u.Kind == "RESEARCH" {
					u.RDuty = true
				}
			case "relic":
				if !g.v15() || a.ID == nil || a.RelicID == nil {
					continue
				}
				u := g.unitByID(*a.ID)
				if u == nil || u.Side != s || u.Kind != "SALVAGER" || u.Relic != nil {
					continue
				}
				if r := g.relicByID(*a.RelicID); r == nil || r.State != "ground" {
					continue
				}
				// 转去拉遗产：取消采集运输线，避免两个状态机打架。
				u.Harvest = nil
				u.HState = "idle"
				u.FlagTimer = 0
				rid := *a.RelicID
				u.Relic = &rid
				u.RState = "toRelic"
				u.RelicTimer = 0
			case "face":
				if !g.v15() || a.ID == nil || a.Deg == nil {
					continue
				}
				if u := g.unitByID(*a.ID); u != nil && u.Side == s {
					u.Facing = math.Mod(*a.Deg+360, 360)
				}
			}
		}
	}

	// 2. 分配过渡 → 经济 → 生产 → 运载舰 → 武器 → 研究
	for _, s := range g.sideOrder() {
		g.stepAlloc(s)
		g.stepEconomy(s)
		g.stepProduction(s)
		g.stepCarriers(s)
		g.stepWeapons(s)
		g.stepResearch(s)
	}

	// 3. 单位动作绑定 + 行为层意图 + 移动
	for _, s := range g.sideOrder() {
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
	for _, s := range g.sideOrder() {
		for _, u := range g.Units {
			if u.Dead || u.Side != s {
				continue
			}
			u.AITargetID = 0
			if u.Order != nil && u.Order.Type == "harvest" {
				// v1.9.2：携带遗产时转采集先把遗产丢回地面，
				// 避免采集/遗产两个状态机争夺同一台采矿机。
				if u.Relic != nil {
					g.dropRelic(u)
				}
				if u.Order.ResID == nil {
					u.Harvest = nil
				} else {
					res := *u.Order.ResID
					u.Harvest = &res
				}
				if u.Harvest == nil {
					u.HState = "idle"
				} else {
					u.HState = "toResource"
				}
			}
			// 引擎托管的采集车/遗产运输车不走 Bot 移动指令。
			// v1.9.2：携带遗产但场上无己方旗时恢复 Bot 移动——否则采矿机
			// 原地死等、永远走不到 VP 去插旗（运货死锁）。
			if u.Kind == "SALVAGER" && (u.Harvest != nil ||
				(u.Relic != nil && g.nearestFriendlyFlagVP(u.Side, u.X, u.Y) != nil)) {
				continue
			}
			if g.v15() && u.Kind == "RESEARCH" && u.RDuty {
				g.stepResearchShip(u)
				continue
			}
			it := g.unitIntent(u)
			if it.hasMove {
				g.applyMove(u, it.moveX, it.moveY)
			}
			if g.v15() {
				dx, dy := u.X-u.PX, u.Y-u.PY
				if dx*dx+dy*dy > 0.01 {
					u.Facing = math.Mod(math.Atan2(dy, dx)*180/math.Pi+360, 360)
				}
			}
			u.AITargetID = it.targetID
		}
	}
	// 3b. 速度估计（弹道提前量）
	for _, u := range g.Units {
		if u.Dead {
			continue
		}
		u.VX = (u.X - u.PX) / (float64(TickMS) / 1000)
		u.VY = (u.Y - u.PY) / (float64(TickMS) / 1000)
		u.PX, u.PY = u.X, u.Y
	}
	// 4. 开火（v1.6 按座位交替顺序，避免固定 side0 先手）
	ordered := make([]*Unit, 0, len(g.Units))
	for _, s := range g.sideOrder() {
		for _, u := range g.Units {
			if u.Side == s {
				ordered = append(ordered, u)
			}
		}
	}
	for _, u := range ordered {
		if u.Dead {
			continue
		}
		t := g.ut(u.Kind)
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
		// 首选：Bot 显式目标 → 行为层目标 → 射程内最近敌人（都要过视线）
		fired := false
		// v1.5：显式拆旗优先。旗不是单位，走独立弹道目标。
		if g.v15() && u.Order != nil && u.Order.Type == "attackFlag" && u.Order.FlagID != nil {
			if f := g.flagByID(*u.Order.FlagID); f != nil && f.Side != u.Side {
				fired = g.fireAtFlag(u, f)
			}
		}
		// 首选：Bot 显式目标 → 行为层目标 → 射程内最近敌人（都要过视线）
		var prefID int
		hasPref := false
		if u.Order != nil && u.Order.TargetID != nil {
			prefID, hasPref = *u.Order.TargetID, true
		} else if u.AITargetID != 0 {
			prefID, hasPref = u.AITargetID, true
		}
		var pref *Unit
		if !fired && hasPref {
			for _, e := range g.Units {
				if e.ID == prefID && !e.Dead && e.Side != u.Side {
					pref = e
					break
				}
			}
		}
		if !fired && pref != nil && (!g.Vision.Enabled || g.canSeeBySide(u.Side, pref.X, pref.Y)) {
			fired = g.fire(u, pref)
		}
		if !fired {
			lim := t.Range * (1 + g.Sides[u.Side].Mods.Range)
			type candT struct {
				e   *Unit
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
			// 平局判定必须镜像不变：距离之后用 |dx|、|dy|，不能用 ID——
			// 全局 ID 两侧分属不同区间，用 ID 决胜会让镜像对局失去协变性（座位偏差）。
			sort.SliceStable(cands, func(a, b int) bool {
				if cands[a].d != cands[b].d {
					return cands[a].d < cands[b].d
				}
				if cands[a].adx != cands[b].adx {
					return cands[a].adx < cands[b].adx
				}
				return cands[a].ady < cands[b].ady
			})
			// v15 的炮塔转速有限：一次只咬一个目标，不能在同一拍里
			// 对 3 个候选各转一点，否则炮塔会来回摆、永远对不准。
			candLimit := 3
			if g.v15() {
				if pref != nil {
					candLimit = 0
				} else {
					candLimit = 1
				}
			}
			for i := 0; i < len(cands) && i < candLimit && !fired; i++ {
				fired = g.fire(u, cands[i].e)
			}
		}
		// v1.5 兜底：没有单位可打时，射程内的敌旗也可打（aggressive/neutral）。
		if !fired && g.v15() && u.Mode != "obey" {
			lim := t.Range * (1 + g.Sides[u.Side].Mods.Range)
			for _, f := range g.V15.Flags {
				if f.Side == u.Side {
					continue
				}
				if math.Hypot(f.X-u.X, f.Y-u.Y)/Cell > lim {
					continue
				}
				if g.fireAtFlag(u, f) {
					fired = true
					break
				}
			}
		}
		if fired {
			rof := float64(t.Rof) * (1 - math.Min(0.5, g.Sides[u.Side].Mods.Rof))
			u.CD = math.Max(1, rof)
		}
	}
	// 5. 炮弹推进 + 清尸 + VP + 终局 + 帧哈希
	g.stepShots()
	var alive []*Unit
	for _, u := range g.Units {
		if !u.Dead {
			alive = append(alive, u)
		}
	}
	g.Units = alive
	if g.v15() {
		g.stepRelicSpawn()
		g.stepFlags()
		g.stepFlagControl()
		for _, s := range g.sideOrder() {
			g.stepRelicResearch(s)
		}
	}
	g.updateVP()
	g.checkEnd()
	f := g.buildFrame()
	g.FrameHashes = append(g.FrameHashes, g.HashFrame(f))
	return f
}

func inList(list []string, v string) bool {
	for _, s := range list {
		if s == v {
			return true
		}
	}
	return false
}
