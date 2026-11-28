// 地图生成共享机械层 + 地貌过程（geomorphic）生成器。
//
// 版本：v1.2（Desert Arena · Go 原生引擎 internal/sim）。
// 功能：GenOptions/进度监控、地 Launcher 机械层、massif/escarpment 规划、
// 河流/风场/地貌量化；minecraft 生成器在 mapgen_mc.go，
// 收尾在 mapgen_finalize.go。
// 移植自 Dev/MVP/referee/mapgen.js（1479 行），逐函数对照：
//   - 所有 JS Float32Array 存储点在 Go 用 []float32 + float32() 显式取整；
//   - JS 的 gxOf/gyOf（mapgen 版）吃线性 index（0..N-1），rules 版吃像素——
//     这里用 IdxGx/IdxGy 区分，像素版 GxOf/GyOf 在 terrain.go；
//   - rng 消耗顺序与 JS 严格一致（mulberry32 是状态机）。
package sim

import (
	"math"
	"sort"
)

// GenOptions 生成参数（mapgen.js:84-95 DEFAULT_OPTS）。
type GenOptions struct {
	// 26*S 由 rules 传入（mapgen.js 收到的是已 ×S 的值）
	UnitSlope int
	// CarrierSlope 同上
	CarrierSlope int
	// 4.2
	VpRadiusCells float64
	// 4
	VpCount int
	// 4
	ResCount int
	WallBand [2]float64
	// 0.10
	WallTarget float64
	// 32
	Attempts int
	// 'geomorphic' | 'minecraft'
	Generator string
	ForcedRes [][2]int
	// finalize 时由 runGenerate 填
	Seed    int
	Attempt int
}

// DefaultGenOptions 与 JS DEFAULT_OPTS 一致（unitSlope/carrierSlope 由规则层传入）。
func DefaultGenOptions() GenOptions {
	return GenOptions{
		UnitSlope:     UnitSlope,
		CarrierSlope:  CarrierSlope,
		VpRadiusCells: 4.2,
		VpCount:       4,
		ResCount:      4,
		WallBand:      [2]float64{0.05, 0.15},
		WallTarget:    0.10,
		Attempts:      32,
		Generator:     "geomorphic",
	}
}

func (o *GenOptions) withDefaults() GenOptions {
	d := DefaultGenOptions()
	if o == nil {
		return d
	}
	out := *o
	if out.UnitSlope == 0 {
		out.UnitSlope = d.UnitSlope
	}
	if out.CarrierSlope == 0 {
		out.CarrierSlope = d.CarrierSlope
	}
	if out.VpRadiusCells == 0 {
		out.VpRadiusCells = d.VpRadiusCells
	}
	if out.VpCount == 0 {
		out.VpCount = d.VpCount
	}
	if out.ResCount == 0 {
		out.ResCount = d.ResCount
	}
	if out.WallBand == [2]float64{} {
		out.WallBand = d.WallBand
	}
	if out.WallTarget == 0 {
		out.WallTarget = d.WallTarget
	}
	if out.Attempts == 0 {
		out.Attempts = d.Attempts
	}
	if out.Generator == "" {
		out.Generator = d.Generator
	}
	return out
}

// IdxGx / IdxGy 线性 index → 格坐标（mapgen.js:106-107 的 gxOf/gyOf）。
func IdxGx(i int) int { return i % Grid }
func IdxGy(i int) int { return i / Grid }

// genFields 生成期的连续场工作区（对应 JS 的 fields 对象）。
// 除 salt/ero/regCut/duneFloor/wind 等标量外全部是 float32 存储口径。
type genFields struct {
	salt       int32
	elev       []float32
	rock       []float32
	dune       []float32
	duneRelief []float32
	playa      []float32
	fan        []float32
	flow       []float32
	flowSpread []float32
	river      []uint8
	accLog     []float32
	slope      []uint8
	// quantize 产物，classify 消费
	elev8 []uint8
	// 平台掩膜（finalize 在 classify 后填）
	pad        []uint8
	wind       float64
	reached    []bool
	sink       MapSink
	duneParams DuneParams
	duneCut    float64
	// MC 生成器专用
	ero      float64
	sandMask []float32
	// 0 表示未设（用默认 0.04）
	regCut       float64
	duneFloor    float64
	climateExtra *ClimateExtra
	mirrorMasks  map[string][]uint8
	mapExtra     *MapExtra
}

// DuneParams 沙丘场参数（协议气候块用）。
type DuneParams struct {
	Lam1 float64 `json:"lam1"`
	Lam2 float64 `json:"lam2"`
	A1   float64 `json:"A1"`
	A2   float64 `json:"A2"`
}

// MapSink 盐沼汇水终点。
type MapSink struct {
	Gx    int     `json:"gx"`
	Gy    int     `json:"gy"`
	Floor float64 `json:"floor"`
}

// ClimateExtra MC 生成器的气候参数（不进 wire，测试/渲染用）。
type ClimateExtra struct {
	Family string `json:"family"`
}

// MapExtra 生成器私有层（MC：峡谷/洞穴/矿脉/群系）。
type MapExtra struct {
	Ravine    []uint8
	Cave      []uint8
	Ore       []uint8
	BiomeSand []float32
}

var dx8 = [8]int{1, -1, 0, 0, 1, 1, -1, -1}
var dy8 = [8]int{0, 0, 1, -1, 1, -1, 1, -1}

// ---------------------- 1. 构造地貌（mapgen.js:159-274） ----------------------

type massif struct {
	cx, cy, rx, ry, ang, peak float64
	mesa                      bool
}

type escarpment struct {
	ang, off, peak, w float64
}

func planMassifs(rng *RNG) []massif {
	var list []massif
	// 2~3 → 对称化后 4~6
	n := 2 + int(rng.Next()*2)
	near := func(cx, cy, r float64) bool {
		for _, m := range list {
			if math.Hypot(cx-m.cx, cy-m.cy) < r ||
				math.Hypot(cx-(1-m.cx), cy-(1-m.cy)) < r {
				return true
			}
		}
		return false
	}
	for k := 0; k < n; k++ {
		cx, cy, ok := 0.5, 0.5, false
		for t := 0; t < 24; t++ {
			ang := rng.Next() * math.Pi * 2
			rad := 0.22 + rng.Next()*0.24
			cx = 0.5 + math.Cos(ang)*rad
			cy = 0.5 + math.Sin(ang)*rad
			if cx < 0.05 || cy < 0.05 || cx > 0.95 || cy > 0.95 {
				continue
			}
			if math.Abs(cx+cy-1) < 0.18 {
				// 别压在对称缝上
				continue
			}
			if near(cx, cy, 0.26) {
				continue
			}
			ok = true
			break
		}
		if !ok {
			continue
		}
		list = append(list, massif{
			cx: cx, cy: cy,
			rx:   0.065 + rng.Next()*0.075,
			ry:   0.045 + rng.Next()*0.075,
			ang:  rng.Next() * math.Pi,
			peak: 96 + rng.Next()*58,
			mesa: rng.Next() < 0.38,
		})
	}
	return list
}

func planEscarpments(rng *RNG) []escarpment {
	var list []escarpment
	if rng.Next() < 0.85 {
		list = append(list, escarpment{
			ang:  rng.Next() * math.Pi,
			off:  0.18 + rng.Next()*0.64,
			peak: 78 + rng.Next()*52,
			w:    0.032 + rng.Next()*0.028,
		})
	}
	return list
}

func buildFields(salt int32, rng *RNG) *genFields {
	f := &genFields{
		elev: make([]float32, N),
		rock: make([]float32, N),
		salt: salt,
	}
	massifs := planMassifs(rng)
	escs := planEscarpments(rng)
	// 主风向 50°~130°
	wind := (0.28 + rng.Next()*0.44) * math.Pi
	f.wind = wind

	sMass := salt ^ int32(0x7f1b3c9d)
	sEsc := salt ^ int32(0x2ab1d7e3)
	sWarp := salt ^ int32(0x5bf03635)

	for gy := 0; gy < Grid; gy++ {
		for gx := 0; gx < Grid; gx++ {
			i := Ci(gx, gy)
			nx := (float64(gx) + 0.5) / Grid
			ny := (float64(gy) + 0.5) / Grid

			// 低地：缓起伏沙质平原，向盆地中心倾斜
			e := 92 + (FBM(salt^int32(0x51ed), nx*2.6, ny*2.6)-0.5)*26
			e += 44 * Smoothstep(0.08, 0.62, math.Hypot(nx-0.5, ny-0.5)/0.7071)
			rk := 0.0

			for k := 0; k < len(massifs); k++ {
				m := &massifs[k]
				ca, sa := math.Cos(m.ang), math.Sin(m.ang)
				dx, dy := nx-m.cx, ny-m.cy
				u := (dx*ca + dy*sa) / m.rx
				v := (-dx*sa + dy*ca) / m.ry
				d := math.Hypot(u, v)
				ang := math.Atan2(v, u)
				d *= 1 + 0.5*(FBM(sWarp, nx*7.5+11, ny*7.5+5)-0.5)*2 +
					0.13*math.Sin(ang*float64(3+(k%2))+m.cx*9.7+m.cy*5.3)
				if d > 1.06 {
					continue
				}
				var prof float64
				if m.mesa {
					prof = Smoothstep(1.04, 0.74, d)
				} else {
					prof = math.Pow(Smoothstep(1.04, 0.10, d), 1.15)
				}
				hh := 92 + m.peak*prof + (FBM(sMass, nx*16, ny*16)-0.5)*12*prof
				if hh > e {
					e = hh
				}
				r2 := Smoothstep(0.62, 0.92, prof)
				if r2 > rk {
					rk = r2
				}
			}

			for k := 0; k < len(escs); k++ {
				s := &escs[k]
				sd := (nx*math.Sin(s.ang) - ny*math.Cos(s.ang)) - s.off +
					(FBM(sEsc, nx*3.4, ny*3.4)-0.5)*0.10
				band := math.Exp(-math.Pow(sd/s.w, 2))
				hh := 96 + s.peak*band
				if hh > e {
					e = hh
				}
				r2 := Smoothstep(0.42, 0.90, band)
				if r2 > rk {
					rk = r2
				}
			}

			f.elev[i] = float32(e)
			f.rock[i] = float32(rk)
		}
	}
	return f
}

// ---------------------- 2. 水系（mapgen.js:288-421） ----------------------

func pickOutlet(elev []float32) int {
	cx := float64(Grid-1) / 2
	cy := float64(Grid-1) / 2
	best, bestE := -1, math.Inf(1)
	lo := int(math.Round(cx - 10*Scale))
	hi := int(math.Round(cx + 10*Scale))
	loY := int(math.Round(cy - 10*Scale))
	hiY := int(math.Round(cy + 10*Scale))
	for gy := loY; gy <= hiY; gy++ {
		for gx := lo; gx <= hi; gx++ {
			if gx < 1 || gy < 1 || gx > Grid-2 || gy > Grid-2 {
				continue
			}
			i := Ci(gx, gy)
			if float64(elev[i]) < bestE {
				bestE = float64(elev[i])
				best = i
			}
		}
	}
	return best
}

func pickSources(elev, rock []float32, rng *RNG, count int) []int {
	type cand struct {
		i   int
		key float64
	}
	var cs []cand
	for i := 0; i < N; i++ {
		if float64(rock[i]) > 0.55 {
			continue
		}
		cs = append(cs, cand{i, float64(elev[i]) + rng.Next()*26})
	}
	sort.SliceStable(cs, func(a, b int) bool {
		if cs[a].key != cs[b].key {
			return cs[a].key > cs[b].key
		}
		return cs[a].i < cs[b].i
	})
	var picked []int
	for _, c := range cs {
		if len(picked) >= count {
			break
		}
		gx, gy := IdxGx(c.i), IdxGy(c.i)
		ok := true
		for _, p := range picked {
			if math.Hypot(float64(IdxGx(p)-gx), float64(IdxGy(p)-gy)) < 9*Scale {
				ok = false
				break
			}
		}
		if ok {
			picked = append(picked, c.i)
		}
	}
	return picked
}

func chainReaches(start int, down []int32, outlet int) bool {
	ox, oy := IdxGx(outlet), IdxGy(outlet)
	c, g := start, 0
	for c >= 0 && g < 520*Scale {
		g++
		if abs(IdxGx(c)-ox) <= 1 && abs(IdxGy(c)-oy) <= 1 {
			return true
		}
		c = int(down[c])
	}
	return false
}

func abs(x int) int {
	if x < 0 {
		return -x
	}
	return x
}

type riverNet struct {
	river   []uint8
	flow    []float32
	down    []int32
	reached []bool
}

func growRivers(elev, rock []float32, outlet int, sources []int) riverNet {
	river := make([]uint8, N)
	down := make([]int32, N)
	for i := range down {
		down[i] = -1
	}
	flow := make([]float32, N)
	var reached []bool
	ox, oy := IdxGx(outlet), IdxGy(outlet)
	atBasin := func(i int) bool {
		return abs(IdxGx(i)-ox) <= Scale && abs(IdxGy(i)-oy) <= Scale
	}
	walked := make([]bool, N)
	for _, src := range sources {
		cur, guard, ok := src, 0, false
		// JS 里 walked 是每条支流一个 Set（新建），不跨支流共享
		local := make([]bool, N)
		_ = walked
		for guard < 420*Scale {
			guard++
			if atBasin(cur) {
				ok = true
				break
			}
			if !local[cur] {
				local[cur] = true
				river[cur] = 1
				flow[cur] += 1
			}
			gx, gy := IdxGx(cur), IdxGy(cur)
			next, bestScore := -1, math.Inf(1)
			for k := 0; k < 8; k++ {
				nx, ny := gx+dx8[k], gy+dy8[k]
				if nx < 0 || ny < 0 || nx >= Grid || ny >= Grid {
					continue
				}
				j := Ci(nx, ny)
				if local[j] {
					continue
				}
				ej := float64(elev[j]) * Scale
				ec := float64(elev[cur]) * Scale
				sc := ej
				if ej > ec {
					sc += 3.0 + (ej-ec)*0.8
				}
				if river[j] != 0 {
					sc -= 5.0
				}
				if float64(rock[j]) > 0.6 {
					sc += 14
				}
				sc += 2.4 * (Hash2(0x5eed5eed, nx*3, ny*3) - 0.5) * 2
				if nx <= 0 || ny <= 0 || nx >= Grid-1 || ny >= Grid-1 {
					sc += 60
				}
				if sc < bestScore {
					bestScore = sc
					next = j
				}
			}
			if next < 0 {
				break
			}
			down[cur] = int32(next)
			cur = next
			if river[cur] != 0 {
				c2, g2 := cur, 0
				for c2 >= 0 && g2 < 420*Scale {
					g2++
					if !local[c2] {
						local[c2] = true
						flow[c2] += 1
						river[c2] = 1
					}
					c2 = int(down[c2])
				}
				ok = chainReaches(cur, down, outlet)
				break
			}
		}
		reached = append(reached, ok)
	}
	return riverNet{river: river, flow: flow, down: down, reached: reached}
}

func carveRiver(elev, rock, flow []float32) []float32 {
	carve := make([]float32, N)
	for i := 0; i < N; i++ {
		if flow[i] <= 0 {
			continue
		}
		depth := math.Min(34, 5.0*math.Log2(1+float64(flow[i])))
		carve[i] = float32(depth * (1 - 0.5*float64(rock[i])))
	}
	for i := 0; i < N; i++ {
		if carve[i] <= 0 {
			continue
		}
		gx, gy := IdxGx(i), IdxGy(i)
		for oy := -Scale; oy <= Scale; oy++ {
			for ox := -Scale; ox <= Scale; ox++ {
				if ox == 0 && oy == 0 {
					continue
				}
				nx, ny := gx+ox, gy+oy
				if nx < 0 || ny < 0 || nx >= Grid || ny >= Grid {
					continue
				}
				j := Ci(nx, ny)
				if carve[j] >= carve[i]*0.5 {
					continue
				}
				carve[j] = carve[i] * 0.5
			}
		}
	}
	for i := 0; i < N; i++ {
		elev[i] = float32(float64(elev[i]) - float64(carve[i]))
	}
	return carve
}

func makePlaya(elev []float32, outlet int) ([]float32, MapSink) {
	playa := make([]float32, N)
	sx, sy := IdxGx(outlet), IdxGy(outlet)
	floor, sum, cnt := 1e9, 0.0, 0
	y0, y1 := maxInt(0, sy-6), minInt(Grid-1, sy+6)
	x0, x1 := maxInt(0, sx-6), minInt(Grid-1, sx+6)
	for gy := y0; gy <= y1; gy++ {
		for gx := x0; gx <= x1; gx++ {
			i := Ci(gx, gy)
			if float64(elev[i]) < floor {
				floor = float64(elev[i])
			}
			sum += float64(elev[i])
			cnt++
		}
	}
	flat := Lerp(floor, sum/math.Max(1, float64(cnt)), 0.35)
	for gy := 0; gy < Grid; gy++ {
		for gx := 0; gx < Grid; gx++ {
			d := math.Hypot(float64(gx-sx), float64(gy-sy))
			m := Smoothstep(7.5*Scale, 3.0*Scale, d)
			if m <= 0 {
				continue
			}
			i := Ci(gx, gy)
			playa[i] = float32(m)
			elev[i] = float32(Lerp(float64(elev[i]), flat+(Hash2(0x9a1f, gx, gy)-0.5)*3, m))
		}
	}
	return playa, MapSink{Gx: sx, Gy: sy, Floor: floor}
}

func maxInt(a, b int) int {
	if a > b {
		return a
	}
	return b
}

func minInt(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func applyFans(f *genFields) {
	rock := f.rock
	fan := make([]float32, N)
	for i := 0; i < N; i++ {
		if f.river[i] == 0 || float64(rock[i]) > 0.04 {
			continue
		}
		gx, gy := IdxGx(i), IdxGy(i)
		peak := 0.0
		for y := gy - 3*Scale; y <= gy+3*Scale; y++ {
			for x := gx - 3*Scale; x <= gx+3*Scale; x++ {
				if x < 0 || y < 0 || x >= Grid || y >= Grid {
					continue
				}
				r := float64(rock[Ci(x, y)])
				if r > peak {
					peak = r
				}
			}
		}
		if peak < 0.55 {
			continue
		}
		for y := gy - Scale; y <= gy+Scale; y++ {
			for x := gx - Scale; x <= gx+Scale; x++ {
				if x < 0 || y < 0 || x >= Grid || y >= Grid {
					continue
				}
				d := math.Hypot(float64(x-gx), float64(y-gy))
				v := Smoothstep(1.6*Scale, 0.2*Scale, d) * Smoothstep(0.55, 0.85, peak)
				j := Ci(x, y)
				if v > float64(fan[j]) {
					fan[j] = float32(v)
				}
			}
		}
	}
	f.fan = fan
}

// ---------------------- 风成地貌（mapgen.js:488-539） ----------------------

func applyAeolian(f *genFields, rng *RNG) {
	elev, rock, playa, accLog, wind := f.elev, f.rock, f.playa, f.accLog, f.wind
	lam1 := (9 + rng.Next()*5) * Scale
	lam2 := (3.6 + rng.Next()*2.2) * Scale
	a1 := 24 + rng.Next()*22
	a2 := 5 + rng.Next()*9
	sDune := f.salt ^ int32(0x44c1a7)
	sSec := f.salt ^ int32(0x1f7d3b)
	sErg := f.salt ^ int32(0x61b7c5)
	dune := make([]float32, N)
	relief := make([]float32, N)
	cw, sw := math.Cos(wind), math.Sin(wind)

	for gy := 0; gy < Grid; gy++ {
		for gx := 0; gx < Grid; gx++ {
			i := Ci(gx, gy)
			erg := Smoothstep(0.42, 0.62, FBM(sErg, float64(gx)/Grid*1.7+2.3, float64(gy)/Grid*1.7+6.1))
			sand := (1 - Smoothstep(0.05, 0.55, float64(rock[i]))) *
				(1 - Smoothstep(0.05, 0.55, float64(playa[i]))) *
				(1 - Smoothstep(0.10, 0.45, float64(accLog[i]))) *
				(0.30 + 0.70*erg)
			if sand <= 0.01 {
				continue
			}
			proj := float64(gx)*cw + float64(gy)*sw
			ph := (proj/lam1)*math.Pi*2 + 2.0*(FBMN(sDune, float64(gx)/Grid*4.2, float64(gy)/Grid*4.2, 3)-0.5)*2
			t := ph / (math.Pi * 2)
			t -= math.Floor(t)
			var shape float64
			if t < 0.68 {
				shape = t / 0.68
			} else {
				shape = (1 - t) / 0.32
			}
			ph2 := (proj/lam2)*math.Pi*2 + 2.4*(FBMN(sSec, float64(gx)/Grid*9.0, float64(gy)/Grid*9.0, 3)-0.5)*2
			u := ph2 / (math.Pi * 2)
			u -= math.Floor(u)
			var shape2 float64
			if u < 0.6 {
				shape2 = u / 0.6
			} else {
				shape2 = (1 - u) / 0.4
			}
			ampMod := 0.55 + 0.45*FBMN(sDune^int32(0x9e37), float64(gx)/Grid*3.1+7, float64(gy)/Grid*3.1+3, 3)
			dh := (a1*shape + a2*shape2) * sand * ampMod
			elev[i] = float32(float64(elev[i]) + dh)
			relief[i] = float32(dh)
			dune[i] = float32(Clamp(dh/(a1*0.8), 0, 1))
		}
	}
	f.dune = dune
	f.duneRelief = relief
	f.duneParams = DuneParams{Lam1: lam1, Lam2: lam2, A1: a1, A2: a2}
}

// ---------------------- 对称化 / 量化（mapgen.js:546-607） ----------------------

const seamB = 2.5 * Scale

func symmetrize(field []float32) []float32 {
	out := make([]float32, N)
	for i := 0; i < N/2; i++ {
		gx, gy := IdxGx(i), IdxGy(i)
		d := (float64(gx+gy) - float64(Grid-1)) / math.Sqrt2
		w := 0.5 + 0.5*Smoothstep(-seamB, seamB, d)
		j := Rev(i)
		v := (1-w)*float64(field[i]) + w*float64(field[j])
		out[i] = float32(v)
		out[j] = float32(v)
	}
	return out
}

func quantize(f *genFields) []uint8 {
	elev, rock := f.elev, f.rock
	e := make([]uint8, N)
	for i := 0; i < N; i++ {
		e[i] = uint8(Clamp(math.Round(float64(elev[i])), 1, 255))
	}
	slopeCap := 22 * Scale
	for pass := 0; pass < 3; pass++ {
		src := make([]uint8, N)
		copy(src, e)
		for i := 0; i < N; i++ {
			if float64(rock[i]) >= 0.28 {
				continue
			}
			gx, gy := IdxGx(i), IdxGy(i)
			excess := 0.0
			for k := 0; k < 8; k++ {
				nx, ny := gx+dx8[k], gy+dy8[k]
				if nx < 0 || ny < 0 || nx >= Grid || ny >= Grid {
					continue
				}
				j := Ci(nx, ny)
				if float64(rock[j]) >= 0.28 {
					continue
				}
				d := float64(src[i]) - float64(src[j])
				if d > float64(slopeCap) && d-float64(slopeCap) > excess {
					excess = d - float64(slopeCap)
				}
			}
			if excess > 0 {
				e[i] = uint8(Clamp(float64(src[i])-math.Ceil(excess*0.7), 1, 255))
			}
		}
		for i := 0; i < N/2; i++ {
			e[Rev(i)] = e[i]
		}
	}
	return e
}

func SlopeField(e []uint8) []uint8 {
	s := make([]uint8, N)
	for i := 0; i < N; i++ {
		gx, gy := IdxGx(i), IdxGy(i)
		m := 0
		for k := 0; k < 8; k++ {
			nx, ny := gx+dx8[k], gy+dy8[k]
			if nx < 0 || ny < 0 || nx >= Grid || ny >= Grid {
				continue
			}
			d := abs(int(e[i]) - int(e[Ci(nx, ny)]))
			if d > m {
				m = d
			}
		}
		s[i] = uint8(m)
	}
	return s
}

// ---------------------- 岩壁阈值（mapgen.js:615-654） ----------------------

func classifyWall(e, slope []uint8, rock []float32, target int) []uint8 {
	var cand []int
	for i := 0; i < N; i++ {
		if float64(rock[i]) >= 0.30 && e[i] >= 126 {
			cand = append(cand, i)
		}
	}
	if len(cand) == 0 {
		return make([]uint8, N)
	}
	hi0 := 60 * Scale
	lo, hi, best, bestGap := 1, hi0, hi0, math.Inf(1)
	for it := 0; it < 12; it++ {
		mid := (lo + hi) >> 1
		n := 0
		for _, c := range cand {
			if slope[c] >= uint8(mid) {
				n++
			}
		}
		gap := abs(n - target)
		if float64(gap) < bestGap {
			bestGap = float64(gap)
			best = mid
		}
		if n > target {
			lo = mid + 1
		} else {
			hi = mid - 1
		}
		if lo > hi {
			break
		}
	}
	wall := make([]uint8, N)
	for _, i := range cand {
		if slope[i] >= uint8(best) {
			wall[i] = 1
		}
	}
	for pass := 0; pass < 2; pass++ {
		snap := make([]uint8, N)
		copy(snap, wall)
		for i := 0; i < N; i++ {
			if snap[i] == 0 {
				continue
			}
			gx, gy := IdxGx(i), IdxGy(i)
			nb := 0
			for k := 0; k < 8; k++ {
				nx, ny := gx+dx8[k], gy+dy8[k]
				if nx < 0 || ny < 0 || nx >= Grid || ny >= Grid {
					continue
				}
				if snap[Ci(nx, ny)] != 0 {
					nb++
				}
			}
			if nb < 2 {
				wall[i] = 0
			}
		}
	}
	return wall
}

// ---------------------- 地表分类（mapgen.js:657-697） ----------------------

func classify(f *genFields, wall []uint8) ([]uint8, []uint8) {
	e := f.elev8
	regCut := 0.04
	if f.regCut > 0 {
		regCut = f.regCut
	}
	slope := SlopeField(e)
	base := make([]uint8, N)
	surf := make([]uint8, N)

	for i := 0; i < N; i++ {
		var s uint8
		switch {
		case wall[i] != 0:
			s = SurfRock
		case f.pad[i] != 0:
			s = SurfPad
		case float64(f.playa[i]) > 0.45:
			s = SurfPlaya
		case float64(f.flow[i]) >= 1.0 && slope[i] < 22*Scale:
			s = SurfWadi
		case float64(f.fan[i]) > 0.45:
			s = SurfFan
		case float64(f.rock[i]) > regCut:
			s = SurfReg
		default:
			s = SurfSand
		}
		surf[i] = s
	}
	var sandCells []float32
	for i := 0; i < N; i++ {
		if surf[i] == SurfSand {
			sandCells = append(sandCells, f.dune[i])
		}
	}
	sort.Slice(sandCells, func(a, b int) bool { return sandCells[a] < sandCells[b] })
	p70 := 1.0
	if len(sandCells) > 0 {
		p70 = float64(sandCells[int(math.Floor(float64(len(sandCells))*0.70))])
	}
	duneFloor := 0.30
	if f.duneFloor > 0 {
		duneFloor = f.duneFloor
	}
	duneCut := math.Max(duneFloor, p70)
	f.duneCut = duneCut
	for i := 0; i < N; i++ {
		if surf[i] == SurfSand && float64(f.dune[i]) > duneCut {
			surf[i] = SurfDune
		}
		base[i] = SurfBase[surf[i]]
	}
	f.slope = slope
	return base, surf
}

// ---------------------- 通行与连通（mapgen.js:700-744） ----------------------

func passableAt(base []uint8, i int) bool { return base[i] != 3 }

func passableFrom(base, e []uint8, a, b, limit int) bool {
	if !passableAt(base, b) {
		return false
	}
	eff := limit
	if base[a] == 4 || base[b] == 4 {
		eff = maxInt(eff, 55*Scale)
	}
	return abs(int(e[b])-int(e[a])) <= eff
}

func reachSet(base, e []uint8, from, limit int) []uint8 {
	seen := make([]uint8, N)
	q := make([]int32, N)
	h, t := 0, 0
	q[t] = int32(from)
	t++
	seen[from] = 1
	for h < t {
		cur := int(q[h])
		h++
		cx, cy := IdxGx(cur), IdxGy(cur)
		for k := 0; k < 8; k++ {
			nx, ny := cx+dx8[k], cy+dy8[k]
			if nx < 0 || ny < 0 || nx >= Grid || ny >= Grid {
				continue
			}
			ni := Ci(nx, ny)
			if seen[ni] != 0 || !passableFrom(base, e, cur, ni, limit) {
				continue
			}
			seen[ni] = 1
			q[t] = int32(ni)
			t++
		}
	}
	return seen
}

// Connected 两出生点互通，且所有 VP / 资源点可达（8 邻域）。
func Connected(m *GameMap, opts *GenOptions) bool {
	o := opts.withDefaults()
	sp := m.SpawnCells
	seen := reachSet(m.Base, m.Elev, Ci(sp[0][0], sp[0][1]), o.UnitSlope)
	if seen[Ci(sp[1][0], sp[1][1])] == 0 {
		return false
	}
	for _, c := range m.VPCells {
		if seen[Ci(c[0], c[1])] == 0 {
			return false
		}
	}
	for _, c := range m.ResCells {
		if seen[Ci(c[0], c[1])] == 0 {
			return false
		}
	}
	return true
}

// SpawnConnected 只问两个出生点通不通（兜底通道回归用）。
func SpawnConnected(m *GameMap, opts *GenOptions) bool {
	o := opts.withDefaults()
	sp := m.SpawnCells
	seen := reachSet(m.Base, m.Elev, Ci(sp[0][0], sp[0][1]), o.UnitSlope)
	return seen[Ci(sp[1][0], sp[1][1])] != 0
}

// ---------------------- 协议编码（mapgen.js:749-848） ----------------------

// SurfString 地表类型 → 协议字符串（'0'..'7' 一字节一格）。
func SurfString(surf []uint8) string {
	out := make([]byte, N)
	for i := 0; i < N; i++ {
		out[i] = byte(48 + surf[i])
	}
	return string(out)
}

// SurfFromString 协议字符串 → 地表数组。
func SurfFromString(str string) []uint8 {
	a := make([]uint8, N)
	for i := 0; i < N && i < len(str); i++ {
		a[i] = uint8(str[i]) - 48
	}
	return a
}

const b64c = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"

func bytesToB64(bytes []byte) string {
	out := make([]byte, 0, (len(bytes)+2)/3*4)
	for i := 0; i < len(bytes); i += 3 {
		b0 := bytes[i]
		b1, b2 := byte(0), byte(0)
		if i+1 < len(bytes) {
			b1 = bytes[i+1]
		}
		if i+2 < len(bytes) {
			b2 = bytes[i+2]
		}
		out = append(out, b64c[b0>>2], b64c[(b0&3)<<4|(b1>>4)])
		if i+1 < len(bytes) {
			out = append(out, b64c[(b1&15)<<2|(b2>>6)])
		} else {
			out = append(out, '=')
		}
		if i+2 < len(bytes) {
			out = append(out, b64c[b2&63])
		} else {
			out = append(out, '=')
		}
	}
	return string(out)
}

func b64ToBytes(str string) []byte {
	// 过滤非法字符（与 JS replace(/[^A-Za-z0-9+/]/g,'') 一致）
	clean := make([]byte, 0, len(str))
	for i := 0; i < len(str); i++ {
		c := str[i]
		if (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' {
			clean = append(clean, c)
		}
	}
	n := len(clean) * 3 / 4
	out := make([]byte, n)
	p := 0
	b64i := func(c byte) int {
		for idx := 0; idx < len(b64c); idx++ {
			if b64c[idx] == c {
				return idx
			}
		}
		return 0
	}
	for i := 0; i < len(clean); i += 4 {
		c0 := b64i(clean[i])
		c1, c2, c3 := 0, 0, 0
		if i+1 < len(clean) {
			c1 = b64i(clean[i+1])
		}
		if i+2 < len(clean) {
			c2 = b64i(clean[i+2])
		}
		if i+3 < len(clean) {
			c3 = b64i(clean[i+3])
		}
		if p < n {
			out[p] = byte(c0<<2 | c1>>4)
			p++
		}
		if p < n {
			out[p] = byte((c1&15)<<4 | c2>>2)
			p++
		}
		if p < n {
			out[p] = byte((c2&3)<<6 | c3)
			p++
		}
	}
	return out
}

// RLEEncode 成对写 (value, count-1)，count 上限 256。
func RLEEncode(arr []uint8) []byte {
	var out []byte
	for i := 0; i < len(arr); {
		v := arr[i]
		n := 1
		for i+n < len(arr) && arr[i+n] == arr[i] && n < 256 {
			n++
		}
		out = append(out, v, byte(n-1))
		i += n
	}
	return out
}

// RLEDecode RLE 解码到 n 格。
func RLEDecode(bytes []byte, n int) []uint8 {
	out := make([]uint8, n)
	p := 0
	for i := 0; i+1 < len(bytes) && p < n; i += 2 {
		v, c := bytes[i], int(bytes[i+1])+1
		for k := 0; k < c && p < n; k++ {
			out[p] = v
			p++
		}
	}
	return out
}

// MapLayers wire 上的地图三编码层。
type MapLayers struct {
	W    int    `json:"w"`
	H    int    `json:"h"`
	Enc  string `json:"enc"`
	Base string `json:"base"`
	Surf string `json:"surf"`
	Elev string `json:"elev"`
}

// EncodeMapLayers base/surf RLE+b64，elev 直接 b64。
func EncodeMapLayers(m *GameMap) MapLayers {
	return MapLayers{
		W: Grid, H: Grid, Enc: "rle+b64/1",
		Base: bytesToB64(RLEEncode(m.Base)),
		Surf: bytesToB64(RLEEncode(m.Surf)),
		Elev: bytesToB64(m.Elev),
	}
}

// DecodedLayers 解码后的三层。
type DecodedLayers struct {
	Base, Surf, Elev []uint8
}

// DecodeMapLayers wire → 数组（未知编码报错）。
func DecodeMapLayers(enc *MapLayers) (*DecodedLayers, error) {
	if enc == nil {
		return nil, nil
	}
	if enc.Enc != "rle+b64/1" {
		return nil, errUnknownEncoding(enc.Enc)
	}
	n := enc.W * enc.H
	elev := b64ToBytes(enc.Elev)
	if len(elev) > n {
		elev = elev[:n]
	}
	return &DecodedLayers{
		Base: RLEDecode(b64ToBytes(enc.Base), n),
		Surf: RLEDecode(b64ToBytes(enc.Surf), n),
		Elev: elev,
	}, nil
}

func errUnknownEncoding(enc string) error { return &mapEncodingError{enc} }

type mapEncodingError struct{ enc string }

func (e *mapEncodingError) Error() string { return "未知的地图层编码：" + e.enc }

// WallRatio 岩壁占比。
func WallRatio(m *GameMap) float64 {
	n := 0
	for i := 0; i < N; i++ {
		if m.Base[i] == 3 {
			n++
		}
	}
	return float64(n) / N
}

// CarveCorridor 兜底通道：沿两出生点折线开挖，clear 格同步镜像。
func CarveCorridor(m *GameMap, width int) {
	a := m.SpawnCells[0]
	b := m.SpawnCells[1]
	steps := maxInt(abs(b[0]-a[0]), abs(b[1]-a[1])) * 2
	for k := 0; k <= steps; k++ {
		gx := int(math.Round(float64(a[0]) + float64(b[0]-a[0])*float64(k)/float64(steps)))
		gy := int(math.Round(float64(a[1]) + float64(b[1]-a[1])*float64(k)/float64(steps)))
		for oy := -width; oy <= width; oy++ {
			for ox := -width; ox <= width; ox++ {
				x, y := gx+ox, gy+oy
				if x < 1 || y < 1 || x >= Grid-1 || y >= Grid-1 {
					continue
				}
				i := Ci(x, y)
				j := Rev(i)
				m.Base[i] = 0
				m.Elev[i] = 112
				m.Surf[i] = SurfWadi
				m.Base[j] = 0
				m.Elev[j] = 112
				m.Surf[j] = SurfWadi
			}
		}
	}
}
