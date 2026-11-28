// Package acebot 是 baseline@2 的增强策略库。
//
// 相对 refbot 的强化点：
//   - 集火：全军按「价值/剩余血量/距离」评分选同一个焦点目标，集中击杀滚雪球；
//   - 伤兵后撤：低血量（高军衔更保守）撤向母舰，保存兵力与等级；
//   - 护旗：库存最多的己方旗常驻一台守卫，阻断敌方入库并防劫旗；
//   - 劫旗：敌方旗有库存时派快速单位拆旗，撤回对方遗产分；
//   - 遗产竞速：专职运输车；携货无旗时主动走去插旗（v1.9.2 语义）；
//   - face 微操：攻城单位原地转向目标，保证射界内开火；
//   - obey 全控：所有移动/攻击由策略显式下达，不受行为层接管。
package acebot

import (
	"math"

	"desertarena/workspace/botkit"
)

// Profile 是 baseline@2 的性格参数。
type Profile struct {
	Alloc        map[string]int
	Build        []string
	Techs        []string
	Aggression   float64
	Weapons      int
	RelicRunners int
	RetreatHP    float64
	VetRetreatHP float64
	Garrison     bool
	Raid         bool
	FaceMicro    bool
}

type unitState struct {
	retreating bool
	raider     bool
	garrison   bool
}

// Run 启动主循环。
func Run(p Profile) error {
	buildIdx := 0
	harvestN := 0
	harvested := map[int]bool{}
	us := map[int]*unitState{}
	return botkit.Run(func(b *botkit.Bot, st *botkit.State) []botkit.Action {
		side := st.Side
		acts := make([]botkit.Action, 0, len(st.Units)+6)
		cell := float64(b.Hello.Cell)
		vpR := 4.2 * cell * 4
		own := botkit.Own(st, side)
		enemies := botkit.Enemies(st, side)
		var myMS, enemyMS *botkit.Unit
		for i := range own {
			if own[i].Kind == "MOTHERSHIP" {
				myMS = &own[i]
			}
		}
		enemyMS = botkit.EnemyMothership(st, side)

		needsRU := false
		for _, k := range p.Build {
			if t, ok := b.Rules.Types[k]; ok && t.RU > 0 {
				needsRU = true
				break
			}
		}

		/* ---- 阵营动作 ---- */
		if !sameAlloc(st.Me.AllocTarget, p.Alloc) {
			acts = append(acts, botkit.Action{Type: "alloc", Alloc: p.Alloc})
		}
		if st.Me.Mode != "obey" {
			acts = append(acts, botkit.Action{Type: "mode", Mode: "obey"})
		}
		maxSalvagers := 3 + p.RelicRunners
		if salvagerCount(st) < maxSalvagers && st.Me.CU >= 100 {
			acts = append(acts, botkit.Action{Type: "produce", Kind: "SALVAGER"})
		} else {
			// 轮转下单：引擎队列是跳过式出队（v1.2），付不起的项原地保留不堵后面，
			// 所以这里挑第一个付得起的下单即可，高价兵攒够钱自然出队。
			for tries := 0; tries < len(p.Build); tries++ {
				kind := p.Build[buildIdx%len(p.Build)]
				buildIdx++
				if kind == "SALVAGER" && salvagerCount(st) >= maxSalvagers {
					continue
				}
				if kind == "RESEARCH" && countKind(st, "RESEARCH") >= 1 {
					continue
				}
				t, ok := b.Rules.Types[kind]
				if !ok || st.Me.Pop+t.Pop > st.Me.PopMax {
					continue
				}
				if canProduce(st, t) {
					acts = append(acts, botkit.Action{Type: "produce", Kind: kind})
					break
				}
			}
		}
		if tech := nextTech(b, st, p.Techs); tech != "" {
			acts = append(acts, botkit.Action{Type: "research", Tech: tech})
		}
		if p.Weapons > 0 && st.Me.WeaponsReady+len(st.Me.WeaponQueue) < p.Weapons {
			gate := 240.0
			if st.Me.WeaponsReady+len(st.Me.WeaponQueue) == 0 {
				gate = 120
			}
			if st.Me.CU >= gate {
				acts = append(acts, botkit.Action{Type: "weapon"})
			}
		}

		/* ---- 战术指派 ---- */
		for _, u := range own {
			if us[u.ID] == nil {
				us[u.ID] = &unitState{}
			}
		}
		focus := pickFocus(b, st, own, enemies, cell, p)
		garrisonFlag := bestOwnFlag(st, side)
		var garrisonID int
		if p.Garrison && garrisonFlag != nil && garrisonFlag.Stored >= 1 {
			garrisonID = nearestCombat(own, garrisonFlag.X, garrisonFlag.Y, 0)
		}
		raidFlags := raidTargets(st, side)
		raidIDs := map[int]bool{}
		if p.Raid && len(raidFlags) > 0 {
			for _, f := range raidFlags {
				id := nearestCombat(own, f.X, f.Y, garrisonID)
				if id != 0 && !raidIDs[id] {
					raidIDs[id] = true
					if len(raidIDs) >= 2 {
						break
					}
				}
			}
		}
		for _, u := range own {
			s := us[u.ID]
			s.garrison = u.ID == garrisonID
			s.raider = raidIDs[u.ID]
		}

		/* ---- 单位动作 ---- */
		for _, u := range own {
			t := b.Rules.Types[u.Kind]
			s := us[u.ID]
			switch u.Kind {
			case "SALVAGER":
				acts = append(acts, salvagerAct(b, st, &u, p, harvested, &harvestN, needsRU, cell)...)
				continue
			case "RESEARCH":
				acts = append(acts, botkit.Action{ID: u.ID, Type: "relicresearch"})
				continue
			case "SCOUT":
				if v := botkit.NearestVP(st, u.X, u.Y, side, true); v != nil {
					acts = append(acts, moveAlong(b, u, v.X, v.Y))
				} else {
					acts = append(acts, botkit.Action{ID: u.ID, Type: "idle"})
				}
				continue
			case "MOTHERSHIP":
				if e := botkit.Nearest(enemies, u.X, u.Y); e != nil &&
					math.Hypot(e.X-u.X, e.Y-u.Y) <= t.Range*1.05*cell {
					acts = append(acts, botkit.Action{ID: u.ID, Type: "attack", TargetID: e.ID})
				} else {
					acts = append(acts, botkit.Action{ID: u.ID, Type: "idle"})
				}
				continue
			}

			// 伤兵后撤（带滞回：撤到 60% 血以上才回归）
			thr := p.RetreatHP
			if u.Lvl >= 3 {
				thr = p.VetRetreatHP
			}
			frac := u.HP / u.MaxHP
			if frac < thr {
				s.retreating = true
			} else if frac > 0.6 {
				s.retreating = false
			}
			if s.retreating && myMS != nil {
				acts = append(acts, moveAlong(b, u, myMS.X, myMS.Y))
				continue
			}

			// 护旗守卫
			if s.garrison && garrisonFlag != nil {
				if e := botkit.Nearest(enemies, u.X, u.Y); e != nil &&
					math.Hypot(e.X-u.X, e.Y-u.Y) <= t.Range*0.95*cell {
					acts = append(acts, attackAct(u, e.ID, e.X, e.Y, p)...)
					continue
				}
				if math.Hypot(u.X-garrisonFlag.X, u.Y-garrisonFlag.Y) > vpR*0.6 {
					acts = append(acts, moveAlong(b, u, garrisonFlag.X, garrisonFlag.Y))
				} else {
					acts = append(acts, botkit.Action{ID: u.ID, Type: "idle"})
				}
				continue
			}

			// 劫旗突袭
			if s.raider {
				if f := nearestFlag(raidFlags, u.X, u.Y); f != nil {
					d := math.Hypot(f.X-u.X, f.Y-u.Y)
					if d <= t.Range*0.95*cell {
						acts = append(acts, botkit.Action{ID: u.ID, Type: "attackFlag", FlagID: f.ID})
					} else {
						acts = append(acts, moveAlong(b, u, f.X, f.Y))
					}
					continue
				}
			}

			// 攻城单位：射程内优先拆母舰
			if (u.Kind == "RAILGUN" || u.Kind == "ARTILLERY") && enemyMS != nil {
				d := math.Hypot(enemyMS.X-u.X, enemyMS.Y-u.Y)
				if d <= t.Range*0.95*cell {
					acts = append(acts, attackAct(u, enemyMS.ID, enemyMS.X, enemyMS.Y, p)...)
					continue
				}
				if focus == nil || math.Hypot(focus.X-u.X, focus.Y-u.Y) > t.Range*p.Aggression*cell {
					acts = append(acts, moveAlong(b, u, enemyMS.X, enemyMS.Y))
					continue
				}
			}

			// 集火
			if focus != nil {
				d := math.Hypot(focus.X-u.X, focus.Y-u.Y)
				if d <= t.Range*0.95*cell {
					acts = append(acts, attackAct(u, focus.ID, focus.X, focus.Y, p)...)
					continue
				}
				if d <= t.Range*p.Aggression*cell {
					x, y := b.StepToward(u, focus.X, focus.Y, b.Stride(u))
					acts = append(acts, botkit.Action{ID: u.ID, Type: "move", X: x, Y: y})
					continue
				}
			}

			// opportunistic 拆旗 / 推进
			if ef := botkit.NearestEnemyFlag(st, side, u.X, u.Y); ef != nil {
				d := math.Hypot(ef.X-u.X, ef.Y-u.Y)
				if d <= t.Range*0.95*cell {
					acts = append(acts, botkit.Action{ID: u.ID, Type: "attackFlag", FlagID: ef.ID})
					continue
				}
			}
			if enemyMS != nil && u.Kind == "LAV" {
				acts = append(acts, moveAlong(b, u, enemyMS.X, enemyMS.Y))
				continue
			}
			if v := b.EnemyVP(st, side); v != nil {
				acts = append(acts, moveAlong(b, u, v.X, v.Y))
			} else {
				acts = append(acts, botkit.Action{ID: u.ID, Type: "idle"})
			}
		}
		return acts
	})
}

// pickFocus 全军的焦点目标：价值高、残血、离我方近者优先。
func pickFocus(b *botkit.Bot, st *botkit.State, own, enemies []botkit.Unit, cell float64, p Profile) *botkit.Unit {
	var best *botkit.Unit
	bestScore := math.Inf(-1)
	for i := range enemies {
		e := &enemies[i]
		if e.Kind == "MOTHERSHIP" {
			continue
		}
		t := b.Rules.Types[e.Kind]
		dmin := math.Inf(1)
		for _, u := range own {
			if u.Kind == "SALVAGER" || u.Kind == "RESEARCH" || u.Kind == "SCOUT" || u.Kind == "MOTHERSHIP" {
				continue
			}
			ut := b.Rules.Types[u.Kind]
			d := math.Hypot(e.X-u.X, e.Y-u.Y)
			if d <= ut.Range*p.Aggression*cell && d < dmin {
				dmin = d
			}
		}
		if math.IsInf(dmin, 1) {
			continue
		}
		score := (t.Cost+150)/(e.HP+30) - dmin/(cell*40)
		if e.Kind == "RAILGUN" || e.Kind == "ARTILLERY" {
			score += 250 / (e.HP + 30)
		}
		if score > bestScore {
			bestScore, best = score, e
		}
	}
	return best
}

func bestOwnFlag(st *botkit.State, side int) *botkit.Flag {
	var best *botkit.Flag
	for i := range st.Flags {
		f := &st.Flags[i]
		if f.Side != side {
			continue
		}
		if best == nil || f.Stored > best.Stored {
			best = f
		}
	}
	return best
}

func raidTargets(st *botkit.State, side int) []botkit.Flag {
	var out []botkit.Flag
	for _, f := range st.Flags {
		if f.Side != side && f.Stored >= 1 {
			out = append(out, f)
		}
	}
	return out
}

func nearestFlag(flags []botkit.Flag, x, y float64) *botkit.Flag {
	var best *botkit.Flag
	bd := math.Inf(1)
	for i := range flags {
		d := math.Hypot(flags[i].X-x, flags[i].Y-y)
		if d < bd {
			bd, best = d, &flags[i]
		}
	}
	return best
}

func nearestCombat(own []botkit.Unit, x, y float64, excludeID int) int {
	bd := math.Inf(1)
	id := 0
	for _, u := range own {
		if u.ID == excludeID || u.Kind == "SALVAGER" || u.Kind == "RESEARCH" ||
			u.Kind == "SCOUT" || u.Kind == "MOTHERSHIP" {
			continue
		}
		d := math.Hypot(u.X-x, u.Y-y)
		if d < bd {
			bd, id = d, u.ID
		}
	}
	return id
}

// attackAct 攻击 + 攻城单位 face 微操。
// face 与 attack 同拍互斥（重复单位动作判故障）：射界外先转向，下一拍再攻击。
func attackAct(u botkit.Unit, targetID int, tx, ty float64, p Profile) []botkit.Action {
	if p.FaceMicro && (u.Kind == "RAILGUN" || u.Kind == "ARTILLERY") {
		bearing := math.Mod(math.Atan2(ty-u.Y, tx-u.X)*180/math.Pi+360, 360)
		diff := math.Abs(math.Mod(bearing-float64(u.Facing)+540, 360) - 180)
		half := 45.0
		if u.Kind == "ARTILLERY" {
			half = 180
		}
		if diff > half*0.8 {
			return []botkit.Action{{ID: u.ID, Type: "face", Deg: bearing}}
		}
	}
	return []botkit.Action{{ID: u.ID, Type: "attack", TargetID: targetID}}
}

func salvagerAct(b *botkit.Bot, st *botkit.State, u *botkit.Unit, p Profile, harvested map[int]bool, harvestN *int, needsRU bool, cell float64) []botkit.Action {
	if !harvested[u.ID] && *harvestN < 3 {
		// 两台采 cu（远矿图收入翻倍），第三台采 ru 供 CARRIER/RAILGUN。
		kind := "cu"
		if *harvestN == 2 && needsRU {
			kind = "ru"
		}
		if r := botkit.NearestResKind(st, u.X, u.Y, kind); r != nil {
			rid := r.ID
			harvested[u.ID] = true
			*harvestN++
			return []botkit.Action{{ID: u.ID, Type: "harvest", ResID: &rid}}
		}
		return []botkit.Action{{ID: u.ID, Type: "idle"}}
	}
	if u.Harvest != nil || harvested[u.ID] {
		return nil
	}
	if !st.V15() {
		return []botkit.Action{{ID: u.ID, Type: "idle"}}
	}
	if u.Relic != nil {
		// v1.9.2：携货但无己方旗时主动走去无旗 VP 插旗，不再原地死等。
		if botkit.NearestOwnFlag(st, st.Side, u.X, u.Y) == nil {
			if v := botkit.NearestUnflaggedVP(st, u.X, u.Y); v != nil {
				return []botkit.Action{moveAlong(b, *u, v.X, v.Y)}
			}
		}
		return nil
	}
	if own := botkit.NearestOwnFlag(st, st.Side, u.X, u.Y); own != nil {
		if r := botkit.NearestGroundRelic(st, u.X, u.Y); r != nil {
			return []botkit.Action{{ID: u.ID, Type: "relic", RelicID: r.ID}}
		}
		return []botkit.Action{moveAlong(b, *u, own.X, own.Y)}
	}
	if v := botkit.NearestUnflaggedVP(st, u.X, u.Y); v != nil {
		return []botkit.Action{moveAlong(b, *u, v.X, v.Y)}
	}
	return []botkit.Action{{ID: u.ID, Type: "idle"}}
}

func sameAlloc(have, want map[string]int) bool {
	for k, v := range want {
		if have[k] != v {
			return false
		}
	}
	return true
}

func canProduce(st *botkit.State, t botkit.Type) bool {
	if st.Me.Pop+t.Pop > st.Me.PopMax {
		return false
	}
	if len(st.Me.Queue)+len(st.Me.Active) >= 6 {
		return false
	}
	if float64(t.RU) > st.Me.RU {
		return false
	}
	return st.Me.CU >= t.Cost
}

func nextTech(b *botkit.Bot, st *botkit.State, order []string) string {
	done := map[string]bool{}
	doing := map[string]bool{}
	for _, id := range st.Me.TechDone {
		done[id] = true
	}
	for _, d := range st.Me.TechDoing {
		doing[d.ID] = true
	}
	if len(doing) >= st.Me.Slots.Research {
		return ""
	}
	for _, id := range order {
		if done[id] || doing[id] {
			continue
		}
		t, ok := b.Rules.Techs[id]
		if !ok || st.Me.CU < t.CU {
			continue
		}
		okReq := true
		for _, r := range t.Req {
			if !done[r] {
				okReq = false
				break
			}
		}
		if len(t.ReqAny) > 0 && len(t.Req) == 0 {
			okReq = false
			for _, r := range t.ReqAny {
				if done[r] {
					okReq = true
					break
				}
			}
		}
		blocked := false
		for _, x := range t.Excl {
			if done[x] || doing[x] {
				blocked = true
				break
			}
		}
		if okReq && !blocked {
			return id
		}
	}
	return ""
}

func moveAlong(b *botkit.Bot, u botkit.Unit, x, y float64) botkit.Action {
	if nx, ny, ok := b.StepAlong(u, b.FieldTo(x, y), b.Stride(u)); ok {
		return botkit.Action{ID: u.ID, Type: "move", X: nx, Y: ny}
	}
	if nx, ny := b.StepToward(u, x, y, b.Stride(u)); nx != u.X || ny != u.Y {
		return botkit.Action{ID: u.ID, Type: "move", X: nx, Y: ny}
	}
	return botkit.Action{ID: u.ID, Type: "idle"}
}

func salvagerCount(st *botkit.State) int {
	n := 0
	for _, u := range botkit.Own(st, st.Side) {
		if u.Kind == "SALVAGER" {
			n++
		}
	}
	for _, k := range st.Me.Queue {
		if k == "SALVAGER" {
			n++
		}
	}
	for _, a := range st.Me.Active {
		if a.Kind == "SALVAGER" {
			n++
		}
	}
	return n
}

func countKind(st *botkit.State, kind string) int {
	n := 0
	for _, u := range botkit.Own(st, st.Side) {
		if u.Kind == kind {
			n++
		}
	}
	for _, k := range st.Me.Queue {
		if k == kind {
			n++
		}
	}
	for _, a := range st.Me.Active {
		if a.Kind == kind {
			n++
		}
	}
	return n
}