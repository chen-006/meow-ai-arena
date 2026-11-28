// 生成收尾层（mapgen.js:880-1464）。
//
// 版本：v1.2（Desert Arena · Go 原生引擎 internal/sim）。
// 功能：出生点搜索、工程平台、VP/资源点搜索、finalize 组装、
// runGenerate 重试驱动；与生成算法无关，两个生成器共用。
package sim

import (
	"errors"
	"math"
	"sort"
	"sync"
)

// ---------------------- 出生点（mapgen.js:880-955） ----------------------

func pickSpawn(m *GameMap) (int, int) {
	base, elev, surf := m.Base, m.Elev, m.Surf
	slope := SlopeField(elev)
	best, bestScore := -1, math.Inf(-1)
	r0, r1 := 6*Scale, 24*Scale
	for gy := r0; gy <= r1; gy++ {
		for gx := r0; gx <= r1; gx++ {
			i := Ci(gx, gy)
			if base[i] == 3 {
				continue
			}
			open, tot := 0, 0
			for y := gy - 4*Scale; y <= gy+4*Scale; y++ {
				for x := gx - 4*Scale; x <= gx+4*Scale; x++ {
					if x < 0 || y < 0 || x >= Grid || y >= Grid {
						continue
					}
					tot++
					j := Ci(x, y)
					if base[j] != 3 && slope[j] <= 18*Scale {
						open++
					}
				}
			}
			openF := float64(open) / float64(tot)
			flat := 0.0
			for y := gy - 2*Scale; y <= gy+2*Scale; y++ {
				for x := gx - 2*Scale; x <= gx+2*Scale; x++ {
					if x < 0 || y < 0 || x >= Grid || y >= Grid {
						continue
					}
					flat = math.Max(flat, math.Abs(float64(elev[Ci(x, y)])-float64(elev[i])))
				}
			}
			rockD := 99.0
			for y := gy - 5*Scale; y <= gy+5*Scale; y++ {
				for x := gx - 5*Scale; x <= gx+5*Scale; x++ {
					if x < 0 || y < 0 || x >= Grid || y >= Grid {
						continue
					}
					if base[Ci(x, y)] == 3 {
						rockD = math.Min(rockD, math.Hypot(float64(x-gx), float64(y-gy)))
					}
				}
			}
			sc := 0.42*openF +
				0.30*Smoothstep(24*Scale, 4*Scale, flat) +
				0.18*Smoothstep(Scale, 6*Scale, rockD)
			sc += 0.10 * (1 - math.Abs((float64(gx+gy))/Grid-0.30)/0.30)
			if surf[i] == SurfPlaya {
				sc -= 0.5
			}
			if sc > bestScore {
				bestScore = sc
				best = i
			}
		}
	}
	if best < 0 {
		best = Ci(10*Scale, 10*Scale)
	}
	return IdxGx(best), IdxGy(best)
}

func makePad(m *GameMap, cx, cy, r int) {
	elev, base, surf := m.Elev, m.Base, m.Surf
	sum, cnt := 0.0, 0
	for y := cy - r; y <= cy+r; y++ {
		for x := cx - r; x <= cx+r; x++ {
			if x < 0 || y < 0 || x >= Grid || y >= Grid {
				continue
			}
			if math.Hypot(float64(x-cx), float64(y-cy)) > float64(r)+0.25*Scale {
				continue
			}
			sum += float64(elev[Ci(x, y)])
			cnt++
		}
	}
	mid := int(math.Round(sum / math.Max(1, float64(cnt))))
	for y := cy - r; y <= cy+r; y++ {
		for x := cx - r; x <= cx+r; x++ {
			if x < 0 || y < 0 || x >= Grid || y >= Grid {
				continue
			}
			if math.Hypot(float64(x-cx), float64(y-cy)) > float64(r)+0.25 {
				continue
			}
			i := Ci(x, y)
			j := Rev(i)
			e := int(math.Round(Lerp(float64(elev[i]), float64(mid), 0.75)))
			elev[i] = uint8(e)
			elev[j] = uint8(e)
			base[i] = 4
			base[j] = 4
			surf[i] = SurfPad
			surf[j] = SurfPad
		}
	}
}

// ---------------------- 目标点打分（mapgen.js:974-1091） ----------------------

type mapAux struct {
	satPass  []int32
	satRoad  []int32
	rad      int
	flat     []float32
	rockDist []float32
}

func integral(mask []uint8) []int32 {
	w := Grid + 1
	sat := make([]int32, w*w)
	for y := 0; y < Grid; y++ {
		row := int32(0)
		for x := 0; x < Grid; x++ {
			row += int32(mask[y*Grid+x])
			sat[(y+1)*w+(x+1)] = sat[y*w+(x+1)] + row
		}
	}
	return sat
}

func windowSum(sat []int32, x0, y0, x1, y1 int) int32 {
	w := Grid + 1
	return sat[(y1+1)*w+(x1+1)] - sat[y0*w+(x1+1)] - sat[(y1+1)*w+x0] + sat[y0*w+x0]
}

func slide1D(src []uint8, out []float32, r int, wantMax bool) {
	for y := 0; y < Grid; y++ {
		for x := 0; x < Grid; x++ {
			a := maxInt(0, x-r)
			b := minInt(Grid-1, x+r)
			m := float32(src[y*Grid+a])
			for k := a + 1; k <= b; k++ {
				v := float32(src[y*Grid+k])
				if wantMax {
					if v > m {
						m = v
					}
				} else {
					if v < m {
						m = v
					}
				}
			}
			out[y*Grid+x] = m
		}
	}
}

func slide2DMinMax(elev []uint8, r int) []float32 {
	tmpMax := make([]float32, N)
	tmpMin := make([]float32, N)
	rowMax := make([]float32, N)
	rowMin := make([]float32, N)
	slide1D(elev, rowMax, r, true)
	slide1D(elev, rowMin, r, false)
	for x := 0; x < Grid; x++ {
		for y := 0; y < Grid; y++ {
			a := maxInt(0, y-r)
			b := minInt(Grid-1, y+r)
			mx := rowMax[a*Grid+x]
			mn := rowMin[a*Grid+x]
			for k := a + 1; k <= b; k++ {
				if rowMax[k*Grid+x] > mx {
					mx = rowMax[k*Grid+x]
				}
				if rowMin[k*Grid+x] < mn {
					mn = rowMin[k*Grid+x]
				}
			}
			tmpMax[y*Grid+x] = mx
			tmpMin[y*Grid+x] = mn
		}
	}
	flat := make([]float32, N)
	for i := 0; i < N; i++ {
		flat[i] = float32(math.Max(float64(tmpMax[i])-float64(elev[i]), float64(elev[i])-float64(tmpMin[i])))
	}
	return flat
}

func rockDistance(base []uint8) []float32 {
	dist := make([]float32, N)
	for i := range dist {
		dist[i] = 1e9
	}
	q := make([]int32, N)
	h, t := 0, 0
	for i := 0; i < N; i++ {
		if base[i] == 3 {
			dist[i] = 0
			q[t] = int32(i)
			t++
		}
	}
	for h < t {
		cur := int(q[h])
		h++
		cx, cy := IdxGx(cur), IdxGy(cur)
		for k := 0; k < 8; k++ {
			nx, ny := cx+dx8[k], cy+dy8[k]
			if nx < 0 || ny < 0 || nx >= Grid || ny >= Grid {
				continue
			}
			j := Ci(nx, ny)
			nd := dist[cur] + 1
			if nd < dist[j] {
				dist[j] = nd
				q[t] = int32(j)
				t++
			}
		}
	}
	return dist
}

func buildAux(m *GameMap) *mapAux {
	pass := make([]uint8, N)
	road := make([]uint8, N)
	for i := 0; i < N; i++ {
		if m.Base[i] != 3 && m.Slope[i] <= 20*Scale {
			pass[i] = 1
		}
		if m.AccLog != nil && float64(m.AccLog[i]) > 0.42 {
			road[i] = 1
		}
	}
	return &mapAux{
		satPass:  integral(pass),
		satRoad:  integral(road),
		rad:      4 * Scale,
		flat:     slide2DMinMax(m.Elev, 2*Scale),
		rockDist: rockDistance(m.Base),
	}
}

func objectiveScore(m *GameMap, aux *mapAux, i int, spawnDist float64) float64 {
	if m.Base[i] == 3 {
		return -1
	}
	if m.Base[i] == 4 {
		return -1
	}
	gx, gy := IdxGx(i), IdxGy(i)
	r := aux.rad
	x0, x1 := maxInt(0, gx-r), minInt(Grid-1, gx+r)
	y0, y1 := maxInt(0, gy-r), minInt(Grid-1, gy+r)
	tot := (x1 - x0 + 1) * (y1 - y0 + 1)
	open := float64(windowSum(aux.satPass, x0, y0, x1, y1)) / math.Max(1, float64(tot))
	near := windowSum(aux.satRoad, x0, y0, x1, y1)
	flat := aux.flat[i]
	rockD := aux.rockDist[i]
	road := Clamp(float64(near)/math.Max(1, float64(tot))*4, 0, 1)
	sc := 0.32*open +
		0.24*Smoothstep(20*Scale, 3*Scale, float64(flat)) +
		0.20*road +
		0.12*Smoothstep(Scale, 4*Scale, float64(rockD)) +
		0.12*(1-math.Abs(spawnDist-12*Scale)/(16*Scale))
	if m.Surf[i] == SurfPlaya {
		sc -= 0.35
	}
	if m.Surf[i] == SurfRock {
		sc -= 1
	}
	return sc
}

func cellPair(gx, gy int) ([2]int, [2]int) {
	a := Ci(gx, gy)
	b := Rev(a)
	ga := [2]int{IdxGx(a), IdxGy(a)}
	gb := [2]int{IdxGx(b), IdxGy(b)}
	if ga[1] < gb[1] || (ga[1] == gb[1] && ga[0] <= gb[0]) {
		return ga, gb
	}
	return gb, ga
}

type objCand struct {
	i          int
	gx, gy     int
	centerDist float64
	spawnDist  float64
	sc         float64
	ang        float64
}

func angDiff(a, b float64) float64 {
	d := math.Mod(math.Abs(a-b), math.Pi*2)
	if d > math.Pi {
		return math.Pi*2 - d
	}
	return d
}

func placeObjectives(m *GameMap, o *GenOptions) ([][2]int, [][2]int) {
	sp := m.SpawnCells[0]
	cx := float64(Grid-1) / 2
	cy := float64(Grid-1) / 2
	seen := reachSet(m.Base, m.Elev, Ci(sp[0], sp[1]), o.UnitSlope)
	aux := buildAux(m)

	var cand, loose []objCand
	for i := 0; i < N; i++ {
		if seen[i] == 0 {
			continue
		}
		if m.Base[i] == 3 {
			continue
		}
		gx, gy := IdxGx(i), IdxGy(i)
		if gx < 4*Scale || gy < 4*Scale || gx > Grid-5*Scale || gy > Grid-5*Scale {
			continue
		}
		centerDist := math.Hypot(float64(gx)-cx, float64(gy)-cy)
		if centerDist < 2.5*Scale {
			// 正中心是 R 的不动点，做不成一对
			continue
		}
		spawnDist := math.Hypot(float64(gx-sp[0]), float64(gy-sp[1]))
		if spawnDist < 7*Scale {
			continue
		}
		sc := objectiveScore(m, aux, i, spawnDist)
		if sc < 0 {
			continue
		}
		item := objCand{
			i: i, gx: gx, gy: gy,
			centerDist: centerDist, spawnDist: spawnDist, sc: sc,
			ang: math.Atan2(float64(gy)-cy, float64(gx)-cx),
		}
		loose = append(loose, item)
		flat := 0.0
		for y := gy - 2*Scale; y <= gy+2*Scale; y++ {
			for x := gx - 2*Scale; x <= gx+2*Scale; x++ {
				if x < 0 || y < 0 || x >= Grid || y >= Grid {
					continue
				}
				flat = math.Max(flat, math.Abs(float64(m.Elev[Ci(x, y)])-float64(m.Elev[i])))
			}
		}
		if flat <= 12*Scale {
			cand = append(cand, item)
		}
	}
	if len(cand) < 4 {
		cand = loose
	}
	sort.SliceStable(cand, func(a, b int) bool {
		if cand[a].sc != cand[b].sc {
			return cand[a].sc > cand[b].sc
		}
		return cand[a].i < cand[b].i
	})

	type cell = [2]int
	var used []cell
	farEnough := func(c objCand, minD float64) bool {
		for _, u := range used {
			if math.Hypot(float64(c.gx-u[0]), float64(c.gy-u[1])) < minD {
				return false
			}
			if math.Hypot(float64(c.gx-(Grid-1-u[0])), float64(c.gy-(Grid-1-u[1]))) < minD {
				return false
			}
		}
		return true
	}

	// 两对：近点对 r∈[8,14]，侧翼对 r∈[14,21]；轴向差 55°/40°/20° 三档放宽。
	bands := [2][2]float64{{8 * Scale, 14 * Scale}, {14 * Scale, 21 * Scale}}
	var pairs [][2]cell
	var first *objCand
	for b := 0; b < 2; b++ {
		lo, hi := bands[b][0], bands[b][1]
		var pick *objCand
		passes := [3]struct {
			ang float64
			sep float64
			pad float64
		}{
			{0.96, 8 * Scale, 0},
			{0.70, 6.5 * Scale, 2.5 * Scale},
			{0.35, 5 * Scale, 5 * Scale},
		}
		for pi := range passes {
			p := &passes[pi]
			for ci := range cand {
				c := &cand[ci]
				if c.centerDist < lo-p.pad || c.centerDist > hi+p.pad {
					continue
				}
				if first != nil && angDiff(c.ang, first.ang) < p.ang {
					continue
				}
				if !farEnough(*c, p.sep) {
					continue
				}
				cc := *c
				pick = &cc
				break
			}
			if pick != nil {
				break
			}
		}
		if pick == nil {
			continue
		}
		if first == nil {
			cp := *pick
			first = &cp
		}
		pa, pb := cellPair(pick.gx, pick.gy)
		used = append(used, pa, pb)
		pairs = append(pairs, [2]cell{pa, pb})
	}

	// 兜底：带内凑不满 → 沿中心两侧取最平的点（仍成对、仍可达）。
	if len(pairs) < 2 {
		ideals := [4][2]float64{{0.62, 0.38}, {0.34, 0.66}, {0.5, 0.72}, {0.28, 0.5}}
		for _, d := range ideals {
			if len(pairs) >= 2 {
				break
			}
			ix := int(math.Round(d[0] * (Grid - 1)))
			iy := int(math.Round(d[1] * (Grid - 1)))
			var best *objCand
			bestD := math.Inf(1)
			for ci := range cand {
				c := &cand[ci]
				if !farEnough(*c, 5*Scale) {
					continue
				}
				dd := math.Hypot(float64(c.gx-ix), float64(c.gy-iy))
				if dd < bestD {
					bestD = dd
					cc := *c
					best = &cc
				}
			}
			if best == nil {
				continue
			}
			pa, pb := cellPair(best.gx, best.gy)
			used = append(used, pa, pb)
			pairs = append(pairs, [2]cell{pa, pb})
		}
	}

	var vpCells [][2]int
	for _, p := range pairs {
		vpCells = append(vpCells, p[0], p[1])
	}

	// ---------- 资源点：MC 矿脉优先（成对且两侧都可达），否则打分选点 ----------
	if len(o.ForcedRes) >= 4 {
		okCell := func(c [2]int) bool {
			i := Ci(c[0], c[1])
			return seen[i] != 0 && m.Base[i] != 3 && m.Base[i] != 4
		}
		var fpairs [][2][2]int
		taken := map[[2]int]bool{}
		for _, c := range o.ForcedRes {
			if taken[c] {
				continue
			}
			mm := [2]int{Grid - 1 - c[0], Grid - 1 - c[1]}
			if !okCell(c) || !okCell(mm) {
				continue
			}
			has := false
			for _, d := range o.ForcedRes {
				if d == mm {
					has = true
					break
				}
			}
			if !has {
				continue
			}
			fpairs = append(fpairs, [2][2]int{c, mm})
			taken[c] = true
			taken[mm] = true
			if len(fpairs) == 2 {
				break
			}
		}
		if len(fpairs) >= 2 {
			cells := [][2]int{fpairs[0][0], fpairs[0][1], fpairs[1][0], fpairs[1][1]}
			ordSlice := func(a [][2]int) { sort.SliceStable(a, func(i, j int) bool { return cellLess(a[i], a[j]) }) }
			vp := vpCells
			ordSlice(vp)
			ordSlice(cells)
			return vp, cells
		}
	}
	type resC struct {
		i         int
		gx, gy    int
		sc        float64
		spawnDist float64
	}
	var resCand []resC
	for i := 0; i < N; i++ {
		if seen[i] == 0 {
			continue
		}
		if m.Base[i] == 3 || m.Base[i] == 4 || m.Base[i] == 2 {
			// 矿点不打在沙丘脊上
			continue
		}
		gx, gy := IdxGx(i), IdxGy(i)
		if gx < 3*Scale || gy < 3*Scale || gx > Grid-4*Scale || gy > Grid-4*Scale {
			continue
		}
		spawnDist := math.Hypot(float64(gx-sp[0]), float64(gy-sp[1]))
		if spawnDist < 4*Scale || spawnDist > 13*Scale {
			continue
		}
		if m.Surf[i] == SurfPlaya {
			continue
		}
		vpD := 99.0 * Scale
		for _, c := range vpCells {
			vpD = math.Min(vpD, math.Hypot(float64(gx-c[0]), float64(gy-c[1])))
		}
		if vpD < 4.5*Scale {
			continue
		}
		if m.Slope[i] > 18*Scale {
			continue
		}
		hard := 0.0
		if m.Base[i] == 1 {
			hard = 1
		}
		acc := 0.0
		if m.AccLog != nil {
			acc = Clamp((float64(m.AccLog[i])-0.25)*2, 0, 1)
		}
		sc := 0.42*hard +
			0.24*(1-math.Abs(spawnDist-7.5*Scale)/(6*Scale)) +
			0.18*(1-math.Min(1, float64(m.Slope[i])/(18*Scale))) +
			0.16*acc
		resCand = append(resCand, resC{i: i, gx: gx, gy: gy, sc: sc, spawnDist: spawnDist})
	}
	sort.SliceStable(resCand, func(a, b int) bool {
		if resCand[a].sc != resCand[b].sc {
			return resCand[a].sc > resCand[b].sc
		}
		return resCand[a].i < resCand[b].i
	})
	var resCells, resUsed [][2]int
	for _, c := range resCand {
		if len(resCells) >= minInt(4, o.ResCount) {
			break
		}
		skip := false
		for _, u := range resUsed {
			if math.Hypot(float64(c.gx-u[0]), float64(c.gy-u[1])) < 5*Scale {
				skip = true
				break
			}
			if math.Hypot(float64(c.gx-(Grid-1-u[0])), float64(c.gy-(Grid-1-u[1]))) < 5*Scale {
				skip = true
				break
			}
		}
		if skip {
			continue
		}
		nearVP := false
		for _, v := range vpCells {
			if math.Hypot(float64(c.gx-v[0]), float64(c.gy-v[1])) < 4.5*Scale {
				nearVP = true
				break
			}
		}
		if nearVP {
			continue
		}
		pa, pb := cellPair(c.gx, c.gy)
		resCells = append(resCells, pa, pb)
		resUsed = append(resUsed, pa, pb)
	}
	// 资源点兜底：至少一对，否则经济无法展开
	if len(resCells) < 2 {
		for _, c := range cand {
			if len(resCells) >= 2 {
				break
			}
			d := math.Hypot(float64(c.gx-sp[0]), float64(c.gy-sp[1]))
			if d < 4*Scale || d > 16*Scale {
				continue
			}
			if m.Base[c.i] == 2 {
				continue
			}
			nearVP := false
			for _, v := range vpCells {
				if math.Hypot(float64(c.gx-v[0]), float64(c.gy-v[1])) < 4*Scale {
					nearVP = true
					break
				}
			}
			if nearVP {
				continue
			}
			pa, pb := cellPair(c.gx, c.gy)
			resCells = append(resCells, pa, pb)
		}
	}

	// 输出顺序固定：先按 y 再按 x（同图同协议）
	sort.SliceStable(vpCells, func(i, j int) bool { return cellLess(vpCells[i], vpCells[j]) })
	sort.SliceStable(resCells, func(i, j int) bool { return cellLess(resCells[i], resCells[j]) })
	return vpCells, resCells
}

func cellLess(a, b [2]int) bool {
	if a[1] != b[1] {
		return a[1] < b[1]
	}
	return a[0] < b[0]
}

// ---------------------- 组装（mapgen.js:1273-1399） ----------------------

func geomorphicBuildOnce(salt int32, o *GenOptions) *GameMap {
	rng := NewRng(int(uint32(salt) ^ 0x2f6e2b1))
	f := buildFields(salt, rng)

	// 水系：汇水终点 → 水源 → 河道生长 → 流量下切
	outlet := pickOutlet(f.elev)
	sources := pickSources(f.elev, f.rock, rng, 6+int(rng.Next()*5))
	net := growRivers(f.elev, f.rock, outlet, sources)
	carveRiver(f.elev, f.rock, net.flow)

	// 盐沼压在河道末端之上
	pl, sink := makePlaya(f.elev, outlet)
	f.playa = pl
	f.sink = sink

	// 河漫滩：河道两侧 1 格给 45% 流量权重
	flowSpread := make([]float32, N)
	for i := 0; i < N; i++ {
		if net.flow[i] > flowSpread[i] {
			flowSpread[i] = net.flow[i]
		}
		if net.flow[i] <= 0 {
			continue
		}
		gx, gy := IdxGx(i), IdxGy(i)
		for k := 0; k < 8; k++ {
			nx, ny := gx+dx8[k], gy+dy8[k]
			if nx < 0 || ny < 0 || nx >= Grid || ny >= Grid {
				continue
			}
			j := Ci(nx, ny)
			v := net.flow[i] * 0.45
			if v > flowSpread[j] {
				flowSpread[j] = v
			}
		}
	}
	f.flowSpread = flowSpread
	f.river = net.river
	f.reached = net.reached

	// 流量先对称化，再算对数流量（accLog 必须严格对称，且在铺沙丘之前算好）
	symFlow := symmetrize(flowSpread)
	maxFlow := 1e-6
	for i := 0; i < N; i++ {
		if float64(symFlow[i]) > maxFlow {
			maxFlow = float64(symFlow[i])
		}
	}
	lmax := math.Log2(1 + maxFlow)
	accLog := make([]float32, N)
	for i := 0; i < N; i++ {
		accLog[i] = float32(math.Log2(1+float64(symFlow[i])) / lmax)
	}
	f.accLog = accLog
	f.flow = symFlow

	applyFans(f)
	applyAeolian(f, rng)

	return finalize(f, o)
}

// finalize 生成器无关收尾：对称化 → 量化 → 岩壁 → 分类 → 出生点 → 平台 → 目标点。
func finalize(f *genFields, o *GenOptions) *GameMap {
	// 连续场对称化（离散 base/surf 由对称后的连续场分类得到）
	if f.elev != nil {
		f.elev = symmetrize(f.elev)
	}
	if f.rock != nil {
		f.rock = symmetrize(f.rock)
	}
	if f.dune != nil {
		f.dune = symmetrize(f.dune)
	}
	if f.duneRelief != nil {
		f.duneRelief = symmetrize(f.duneRelief)
	}
	if f.playa != nil {
		f.playa = symmetrize(f.playa)
	}
	if f.fan != nil {
		f.fan = symmetrize(f.fan)
	}
	if f.flow != nil {
		f.flow = symmetrize(f.flow)
	}
	// 离散掩膜镜像：取或（布尔事实，不是强度）
	if f.mirrorMasks != nil {
		for _, m := range f.mirrorMasks {
			for i := 0; i < N/2; i++ {
				j := N - 1 - i
				v := uint8(0)
				if m[i] != 0 || m[j] != 0 {
					v = 1
				}
				m[i] = v
				m[j] = v
			}
		}
	}

	// 对数流量（分类的「干河谷」与目标点打分都要）
	maxFlow := 1e-6
	if f.flow != nil {
		for i := 0; i < N; i++ {
			if float64(f.flow[i]) > maxFlow {
				maxFlow = float64(f.flow[i])
			}
		}
	}
	lmax := math.Log2(1 + maxFlow)
	accLog := make([]float32, N)
	for i := 0; i < N; i++ {
		if f.flow != nil {
			accLog[i] = float32(math.Log2(1+float64(f.flow[i])) / lmax)
		}
	}
	f.accLog = accLog

	elev8 := quantize(f)
	f.elev8 = elev8
	slope := SlopeField(elev8)
	target := int(math.Round(o.WallTarget * N))
	wall := classifyWall(elev8, slope, f.rock, target)

	pad := make([]uint8, N)
	f.pad = pad
	m := &GameMap{
		Elev: elev8, Base: make([]uint8, N), Surf: make([]uint8, N),
		Slope:      slope,
		SpawnCells: [][2]int{{10, 10}, {Grid - 11, Grid - 11}},
		Seed:       o.Seed,
		Attempt:    o.Attempt,
		Generator:  o.Generator,
		Sink:       f.sink,
		AccLog:     accLog,
		DuneAmp:    f.dune, DuneRelief: f.duneRelief,
		Playa: f.playa, Rock: f.rock, Fan: f.fan,
		DuneCut:        f.duneCut,
		River:          f.river,
		SourcesReached: f.reached,
		Extra:          f.mapExtra,
	}
	// wallRatio 由 finalize 末尾再算（与 JS 一致：分类后的 base）

	base, surf := classify(f, wall)
	m.Base = base
	m.Surf = surf
	m.Slope = SlopeField(m.Elev)

	// 出生点：左下象限搜索 → 镜像对手 → 压平台
	sx, sy := pickSpawn(m)
	m.SpawnCells = [][2]int{{sx, sy}, {Grid - 1 - sx, Grid - 1 - sy}}
	makePad(m, sx, sy, 3*Scale)
	makePad(m, m.SpawnCells[1][0], m.SpawnCells[1][1], 3*Scale)
	for i := 0; i < N; i++ {
		if m.Base[i] == 4 {
			pad[i] = 1
		}
	}
	f.pad = pad
	m.Slope = SlopeField(m.Elev)

	vp, res := placeObjectives(m, o)
	m.VPCells = vp
	m.ResCells = res
	m.WallRatio = WallRatio(m)
	return m
}

// ---------------------- 驱动（mapgen.js:1410-1464） ----------------------

var (
	mapCacheMu  sync.Mutex
	mapCache    = map[string]*GameMap{}
	mapCacheOrd []string
)

const mapCacheMax = 4

func mapCacheGet(key string) (*GameMap, bool) {
	mapCacheMu.Lock()
	defer mapCacheMu.Unlock()
	m, ok := mapCache[key]
	if !ok {
		return nil, false
	}
	// LRU：命中挪到队尾
	for i, k := range mapCacheOrd {
		if k == key {
			mapCacheOrd = append(mapCacheOrd[:i], mapCacheOrd[i+1:]...)
			break
		}
	}
	mapCacheOrd = append(mapCacheOrd, key)
	return cloneMap(m), true
}

func mapCachePut(key string, m *GameMap) {
	mapCacheMu.Lock()
	defer mapCacheMu.Unlock()
	mapCache[key] = m
	mapCacheOrd = append(mapCacheOrd, key)
	for len(mapCacheOrd) > mapCacheMax {
		delete(mapCache, mapCacheOrd[0])
		mapCacheOrd = mapCacheOrd[1:]
	}
}

// cloneMap 深拷贝（调用方可能就地改地图，绝不能污染缓存）。
func cloneMap(m *GameMap) *GameMap {
	out := *m
	cp := func(s []uint8) []uint8 {
		if s == nil {
			return nil
		}
		d := make([]uint8, len(s))
		copy(d, s)
		return d
	}
	cpf := func(s []float32) []float32 {
		if s == nil {
			return nil
		}
		d := make([]float32, len(s))
		copy(d, s)
		return d
	}
	out.Base = cp(m.Base)
	out.Surf = cp(m.Surf)
	out.Elev = cp(m.Elev)
	out.Slope = cp(m.Slope)
	out.River = cp(m.River)
	out.AccLog = cpf(m.AccLog)
	out.DuneAmp = cpf(m.DuneAmp)
	out.DuneRelief = cpf(m.DuneRelief)
	out.Playa = cpf(m.Playa)
	out.Rock = cpf(m.Rock)
	out.Fan = cpf(m.Fan)
	out.Flow = cpf(m.Flow)
	out.SpawnCells = append([][2]int(nil), m.SpawnCells...)
	out.VPCells = append([][2]int(nil), m.VPCells...)
	out.ResCells = append([][2]int(nil), m.ResCells...)
	out.SourcesReached = append([]bool(nil), m.SourcesReached...)
	if m.Extra != nil {
		e := *m.Extra
		e.Ravine = cp(m.Extra.Ravine)
		e.Cave = cp(m.Extra.Cave)
		e.Ore = cp(m.Extra.Ore)
		e.BiomeSand = cpf(m.Extra.BiomeSand)
		out.Extra = &e
	}
	return &out
}

// runGenerate 重试驱动：最多 attempts 次，要求 ①连通 ②岩壁占比落带；
// 否则取「最接近目标占比的连通图」标 OutOfBand；连连通图都没有 → 兜底通道标 Fallback。
// build 是生成器入口（geomorphic / minecraft）。
func runGenerate(seed int, opts *GenOptions, build func(salt int32, o *GenOptions) *GameMap) *GameMap {
	o := opts.withDefaults()
	requested := seed
	cacheKey := itoa(requested) + "|" + o.Generator + "|" + itoa(o.Attempts)
	if hit, ok := mapCacheGet(cacheKey); ok {
		return hit
	}
	var best *GameMap
	bestGap := math.Inf(1)
	for a := 0; a < o.Attempts; a++ {
		attempt := o
		attempt.Seed = requested
		attempt.Attempt = a
		m := build(SeedSalt(requested, a), &attempt)
		if m == nil {
			continue
		}
		if !Connected(m, &attempt) {
			continue
		}
		r := m.WallRatio
		if r >= o.WallBand[0] && r <= o.WallBand[1] {
			mapCachePut(cacheKey, m)
			return cloneMap(m)
		}
		gap := math.Abs(r - o.WallTarget)
		if gap < bestGap {
			bestGap = gap
			best = m
		}
	}
	if best != nil {
		best.OutOfBand = true
		mapCachePut(cacheKey, best)
		return cloneMap(best)
	}
	attempt := o
	attempt.Seed = requested
	attempt.Attempt = o.Attempts - 1
	m := build(SeedSalt(requested, o.Attempts-1), &attempt)
	CarveCorridor(m, 2*Scale)
	if !SpawnConnected(m, &attempt) {
		CarveCorridor(m, 5*Scale)
	}
	m.Fallback = true
	m.WallRatio = WallRatio(m)
	mapCachePut(cacheKey, m)
	return cloneMap(m)
}

func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	neg := n < 0
	if neg {
		n = -n
	}
	var buf [20]byte
	p := len(buf)
	for n > 0 {
		p--
		buf[p] = byte('0' + n%10)
		n /= 10
	}
	if neg {
		p--
		buf[p] = '-'
	}
	return string(buf[p:])
}

// GenerateMapGeomorphic 地貌过程生成器公开入口。
func GenerateMapGeomorphic(seed int, opts *GenOptions) *GameMap {
	o := opts.withDefaults()
	o.Generator = "geomorphic"
	return runGenerate(seed, &o, geomorphicBuildOnce)
}

// Compile-time guards。
var (
	_ = errors.New
)
