package sim

import (
	"math"
	"sort"
)

// 「我的世界」风格生成器（mapgen_mc.js 的完整移植）。
//
// 版本：v1.2（Desert Arena · Go 原生引擎 internal/sim）。
// 功能：气候参数 → 样条整形 → 河流/峡谷/洞穴/矿脉，复用 finalize 收尾。
// 对齐 MC 1.18+ Noise Router 的生成结构，不是方块外观。

const mcGain = 3.4

// mcN11 低频气候噪声，输出压到 [-1,1]。
func mcN11(seed int32, x, y float64) float64 {
	return Clamp((FBMN(seed, x, y, 4)-0.5)*mcGain, -1, 1)
}

// mcRidged 脊状噪声：折叠一次得到「刀脊」（peaks & valleys 同构）。
func mcRidged(seed int32, x, y float64) float64 {
	return 2*(1-math.Abs(2*FBMN(seed, x, y, 4)-1)) - 1
}

func climateFields(salt int32) (C, E, PV, T, H, W []float32) {
	C = make([]float32, N)
	E = make([]float32, N)
	PV = make([]float32, N)
	T = make([]float32, N)
	H = make([]float32, N)
	W = make([]float32, N)
	sC := salt ^ int32(0x1c0f31)
	sE := salt ^ int32(0x2e77a5)
	sP := salt ^ int32(0x3b91d7)
	sT := salt ^ int32(0x4a2c63)
	sH := salt ^ int32(0x5d17b9)
	sW := salt ^ int32(0x6f3e81)
	for gy := 0; gy < Grid; gy++ {
		for gx := 0; gx < Grid; gx++ {
			i := Ci(gx, gy)
			nx := float64(gx) / Grid
			ny := float64(gy) / Grid
			C[i] = float32(mcN11(sC, nx*3.00, ny*3.00))
			E[i] = float32(mcN11(sE, nx*3.40, ny*3.40))
			PV[i] = float32(mcRidged(sP, nx*6.20, ny*6.20))
			T[i] = float32(mcN11(sT, nx*2.20, ny*2.20))
			H[i] = float32(mcN11(sH, nx*2.40, ny*2.40))
			W[i] = float32(mcN11(sW, nx*2.60, ny*2.60))
		}
	}
	return C, E, PV, T, H, W
}

// heightKnots 大陆度 → 基准高度的分段样条节点。
var heightKnots = [][2]float64{
	{-1.00, 74}, {-0.62, 80}, {-0.40, 88}, {-0.22, 101},
	{-0.04, 114}, {0.16, 128}, {0.40, 156}, {0.70, 188}, {1.00, 208},
}

// BaseHeightSpline 单调节点表的 Catmull-Rom 三次 Hermite（mapgen_mc.js:90-106）。
func BaseHeightSpline(c float64) float64 {
	k := heightKnots
	n := len(k)
	if c <= k[0][0] {
		return k[0][1]
	}
	if c >= k[n-1][0] {
		return k[n-1][1]
	}
	i := 0
	for i < n-2 && c > k[i+1][0] {
		i++
	}
	x0, y0 := k[i][0], k[i][1]
	x1, y1 := k[i+1][0], k[i+1][1]
	h := x1 - x0
	t := (c - x0) / h
	// 切线用相邻节点差商；两端退化为单侧差分
	ym := y0 - (y1 - y0)
	if i > 0 {
		ym = k[i-1][1]
	}
	yp := y1 + (y1 - y0)
	if i+2 < n {
		yp = k[i+2][1]
	}
	m0 := (y1 - ym) / (x1 - func() float64 {
		if i > 0 {
			return k[i-1][0]
		}
		return x0 - h
	}())
	m1 := (yp - y0) / (func() float64 {
		if i+2 < n {
			return k[i+2][0]
		}
		return x1 + h
	}() - x0)
	t2 := t * t
	t3 := t2 * t
	return (2*t3-3*t2+1)*y0 + (t3-2*t2+t)*h*m0 + (-2*t3+3*t2)*y1 + (t3-t2)*h*m1
}

// mcCarveRivers 沿低频噪声零等值线下切（MC 的河按等值线不按汇水）。
func mcCarveRivers(elev []float32, f *genFields, salt int32) ([]float32, []uint8) {
	flow := make([]float32, N)
	river := make([]uint8, N)
	for i := 0; i < N; i++ {
		gx, gy := IdxGx(i), IdxGy(i)
		n := FBMN(salt^int32(0x7a11c3), float64(gx)/Grid*2.4, float64(gy)/Grid*2.4, 4)
		d := math.Abs(2*n - 1)
		if d > 0.075 {
			continue
		}
		// 越靠近等值线越深；高处（>190）不断河
		highCut := Smoothstep(196, 168, float64(elev[i]))
		depth := Smoothstep(0.075, 0.008, d) * (12 + 8*(1-f.ero)) * highCut
		if depth <= 0.4 {
			continue
		}
		elev[i] = float32(float64(elev[i]) - depth)
		if d < 0.028 {
			flow[i] = 2
		} else {
			flow[i] = 1
		}
		river[i] = 1
	}
	// 河漫滩：两侧一格给 0.45 流量权重。注意 JS 的 spread 是 flow 的**拷贝**
	//（new Float32Array(flow)），后续 max 在拷贝上更新。
	spread := make([]float32, N)
	copy(spread, flow)
	for i := 0; i < N; i++ {
		if river[i] == 0 {
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
				if spread[j] < flow[i]*0.45 {
					spread[j] = flow[i] * 0.45
				}
			}
		}
	}
	return spread, river
}

// mcCarveRavines 峡谷：随机点出发的折线，V 形剖面，条数 1~3。
func mcCarveRavines(elev []float32, rng *RNG, salt int32) []uint8 {
	_ = salt
	mask := make([]uint8, N)
	count := 1 + int(rng.Next()*3)
	for k := 0; k < count; k++ {
		gx := 6*Scale + rng.Next()*(Grid-13*Scale)
		gy := 6*Scale + rng.Next()*(Grid-13*Scale)
		ang := rng.Next() * math.Pi * 2
		length := (18 + rng.Next()*16) * Scale
		width := (1.6 + rng.Next()*1.6) * Scale
		depth := 16 + rng.Next()*16
		bend := (rng.Next() - 0.5) * 0.22
		for s := 0.0; s < length; s++ {
			ang += bend*math.Sin(s*0.35) + (rng.Next()-0.5)*0.12
			gx += math.Cos(ang) * 0.9 * Scale
			gy += math.Sin(ang) * 0.9 * Scale
			if gx < 2 || gy < 2 || gx > Grid-3 || gy > Grid-3 {
				break
			}
			// 越到中段越深，两端收口
			taper := math.Sin(math.Pi * (s / length))
			gxr := math.Round(gx)
			gyr := math.Round(gy)
			for oy := -3 * Scale; oy <= 3*Scale; oy++ {
				for ox := -3 * Scale; ox <= 3*Scale; ox++ {
					x := int(gxr) + ox
					y := int(gyr) + oy
					if x < 1 || y < 1 || x >= Grid-1 || y >= Grid-1 {
						continue
					}
					d := math.Hypot(float64(ox), float64(oy))
					if d > width {
						continue
					}
					i := Ci(x, y)
					cut := depth * taper * (1 - d/width)
					if cut > 0 {
						elev[i] = float32(float64(elev[i]) - cut)
						mask[i] = 1
					}
				}
			}
		}
	}
	return mask
}

// mcCarveCaves 洞穴只做「与地表相交」的部分：塌陷坑与窄缝，软地表才开。
func mcCarveCaves(elev, rock []float32, salt int32) []uint8 {
	mask := make([]uint8, N)
	cut := make([]float32, N)
	for i := 0; i < N; i++ {
		if float64(rock[i]) > 0.45 {
			continue
		}
		gx, gy := IdxGx(i), IdxGy(i)
		cx := float64(gx) / Grid
		cy := float64(gy) / Grid
		cheese := math.Abs(2*FBMN(salt^int32(0x9c0f), cx*3.1, cy*3.1, 3) - 1)
		s1 := math.Abs(2*FBMN(salt^int32(0x3d71), cx*4.0, cy*4.0, 3) - 1)
		s2 := math.Abs(2*FBMN(salt^int32(0x51ab), cx*4.0+3.3, cy*4.0+5.1, 3) - 1)
		d := 0.0
		if cheese < 0.008 {
			d = Smoothstep(0.008, 0.0015, cheese) * 12
		} else if s1 < 0.007 && s2 < 0.007 {
			d = 7 * Smoothstep(0.007, 0.0015, math.Max(s1, s2))
		}
		if d > 0.5 {
			cut[i] = float32(d)
		}
	}
	// 边缘羽化：8 邻域一半深度，坑从「针孔」变「漏斗」
	for i := 0; i < N; i++ {
		if cut[i] <= 0 {
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
				if float64(rock[j]) > 0.45 {
					continue
				}
				v := cut[i] * 0.5
				if v > cut[j] {
					cut[j] = v
				}
			}
		}
	}
	for i := 0; i < N; i++ {
		if cut[i] <= 0.5 {
			continue
		}
		elev[i] = float32(float64(elev[i]) - float64(cut[i]))
		mask[i] = 1
	}
	return mask
}

// mcAddDunes 低幅沙丘：只在沙漠群系长，幅度 4~16 级（排除法定落点）。
func mcAddDunes(elev []float32, f *genFields, rng *RNG, windRad float64, flow []float32) (dune, relief []float32, params DuneParams) {
	dune = make([]float32, N)
	relief = make([]float32, N)
	lam := (16 + rng.Next()*6) * Scale
	a := 14 + rng.Next()*12
	cw, sw := math.Cos(windRad), math.Sin(windRad)
	sD := f.salt ^ int32(0x2f7e11)
	for i := 0; i < N; i++ {
		gx, gy := IdxGx(i), IdxGy(i)
		flowV := 0.0
		if flow != nil {
			flowV = float64(flow[i])
		}
		gate := float64(f.sandMask[i]) *
			(1 - Smoothstep(0.12, 0.45, float64(f.rock[i]))) *
			(1 - float64(f.playa[i])) *
			(1 - Smoothstep(0.2, 0.8, flowV))
		if gate < 0.22 {
			continue
		}
		ampScale := Smoothstep(0.22, 0.62, gate)
		proj := float64(gx)*cw + float64(gy)*sw
		ph := (proj/lam)*math.Pi*2 + 2.0*(FBMN(sD, float64(gx)/Grid*4.2, float64(gy)/Grid*4.2, 3)-0.5)*2
		t := ph / (math.Pi * 2)
		t -= math.Floor(t)
		var shape float64
		if t < 0.68 {
			shape = t / 0.68
		} else {
			shape = (1 - t) / 0.32
		}
		mod := 0.5 + 0.5*FBMN(sD^int32(0x77), float64(gx)/Grid*3.1+7, float64(gy)/Grid*3.1+3, 3)
		dh := a * shape * mod * ampScale
		elev[i] = float32(float64(elev[i]) + dh)
		relief[i] = float32(dh)
		// 分类用「覆盖度」（包络）而不是逐格脊高
		dune[i] = float32(ampScale)
	}
	return dune, relief, DuneParams{Lam1: lam, Lam2: 0, A1: a, A2: 0}
}

// mcPlaceOres 矿脉：随机游走团块，只在反对角线一侧生成，镜像补另一半。
func mcPlaceOres(f *genFields, rng *RNG) (veins []struct {
	Gx, Gy, Size int
	Mirror       bool
}, oreMask []uint8) {
	type veinT = struct {
		Gx, Gy, Size int
		Mirror       bool
	}
	oreMask = make([]uint8, N)
	tries := 14
	for k := 0; k < tries; k++ {
		gx := 5*Scale + int(math.Floor(rng.Next()*(Grid-10*Scale)))
		gy := 5*Scale + int(math.Floor(rng.Next()*(Grid-10*Scale)))
		if gx+gy > Grid-1 {
			// 只取一侧，镜像补另一半
			continue
		}
		ii := Ci(gx, gy)
		if float64(f.rock[ii]) > 0.55 || float64(f.playa[ii]) > 0.5 {
			// 矿脉不长在岩壁与盐壳上
			continue
		}
		var blobs [][2]int
		size := (2 + int(math.Floor(rng.Next()*4))) * Scale
		for s := 0; s < size; s++ {
			i := Ci(gx, gy)
			oreMask[i] = 1
			blobs = append(blobs, [2]int{gx, gy})
			dir := int(math.Floor(rng.Next() * 4))
			dx := [4]int{1, -1, 0, 0}[dir] * Scale
			dy := [4]int{0, 0, 1, -1}[dir] * Scale
			gx = int(Clamp(float64(gx+dx), 2*Scale, Grid-3*Scale))
			gy = int(Clamp(float64(gy+dy), 2*Scale, Grid-3*Scale))
		}
		veins = append(veins, veinT{Gx: blobs[0][0], Gy: blobs[0][1], Size: len(blobs)})
	}
	// 镜像出一半，保证矿脉成对
	mirrored := append([]veinT(nil), veins...)
	for _, v := range veins {
		mx, my := Grid-1-v.Gx, Grid-1-v.Gy
		oreMask[Ci(mx, my)] = 1
		mirrored = append(mirrored, veinT{Gx: mx, Gy: my, Size: v.Size, Mirror: true})
	}
	return mirrored, oreMask
}

// mcBiomeMasks 群系只由气候参数决定；输出连续权重，分类仍走 classify。
// 忠实复刻 JS 的一个「怪」语义：playa 的岩性因子读的是**本函数局部 rock 数组
// 在赋值前的当前值**（恒为 0）——即 playa 实际不受岩性约束。保持原样。
func mcBiomeMasks(C, E, T, H, W, elev []float32) (sandMask, rock, playa []float32) {
	// 盐滩面积上限：大陆度 5% 分位当门槛
	sortedC := make([]float32, N)
	copy(sortedC, C)
	sort.Slice(sortedC, func(a, b int) bool { return sortedC[a] < sortedC[b] })
	cutC := float64(sortedC[int(math.Floor(float64(len(sortedC))*0.05))])
	sandMask = make([]float32, N)
	rock = make([]float32, N)
	playa = make([]float32, N)
	for i := 0; i < N; i++ {
		c, e, t, h, w := float64(C[i]), float64(E[i]), float64(T[i]), float64(H[i]), float64(W[i])
		hgt := float64(elev[i])
		playa[i] = float32(Smoothstep(cutC, cutC-0.16, c) * Smoothstep(98, 84, hgt) * (1 - Smoothstep(0.35, 0.65, 0)))
		high := Smoothstep(152, 198, hgt)
		mesa := Smoothstep(-0.30, -0.55, e) * Smoothstep(0.10, -0.25, h) * (0.6 + 0.4*math.Max(0, w))
		rock[i] = float32(Clamp(0.95*high+0.8*mesa, 0, 1))
		dry := Smoothstep(0.35, -0.05, h)
		warm := Smoothstep(-0.50, 0.00, t)
		sandMask[i] = float32(Clamp(dry*warm*(1-float64(rock[i]))*(1-float64(playa[i])), 0, 1))
	}
	return sandMask, rock, playa
}

// minecraftBuildOnce MC 生成器的一次直接生成（mapgen_mc.js:336-423）。
// rng 消耗顺序：windRad → addDunes(lam,A) → ravines → ores，与 JS 严格一致。
func minecraftBuildOnce(salt int32, o *GenOptions) *GameMap {
	rng := NewRng(int(uint32(salt) ^ 0x6b1d3f))
	c, e, pv, t, h, w := climateFields(salt)
	f := &genFields{salt: salt}

	// 基准高度：样条(大陆度) + 峰谷 × 侵蚀调制
	elev := make([]float32, N)
	f.ero = 0
	for i := 0; i < N; i++ {
		// 0 陡 … 1 平
		ero := (float64(e[i]) + 1) / 2
		f.ero += ero
		base := BaseHeightSpline(float64(c[i]))
		amp := Lerp(26, 3.5, ero)
		elev[i] = float32(base + float64(pv[i])*amp*0.5)
	}
	f.ero /= N

	// 群系掩膜用「未切割」的高度
	sand, rk, pl := mcBiomeMasks(c, e, t, h, w, elev)
	f.sandMask = sand
	f.rock = rk
	f.playa = pl
	// 砾漠只在「真的够岩石」的地方出现；MC 低幅沙丘整体振幅小，用 0.15 下限
	f.regCut = 0.32
	f.duneFloor = 0.15

	windRad := (0.25 + rng.Next()*0.5) * math.Pi
	flow, river := mcCarveRivers(elev, f, salt)
	dune, relief, params := mcAddDunes(elev, f, rng, windRad, flow)
	ravine := mcCarveRavines(elev, rng, salt)
	cave := mcCarveCaves(elev, f.rock, salt)
	veins, oreMask := mcPlaceOres(f, rng)

	f.elev = elev
	f.dune = dune
	f.duneRelief = relief
	f.duneParams = params
	f.wind = windRad
	f.flow = flow
	f.river = river
	// 视觉层的「冲积扇」：河岸边一圈低权重，分类成砾漠
	f.fan = make([]float32, N)
	for i := 0; i < N; i++ {
		if flow[i] > 0.5 {
			f.fan[i] = 0.5
		}
	}

	// 盐沼中心：盐滩里最低的一格
	sinkIdx, low := 0, 1e9
	for i := 0; i < N; i++ {
		if float64(f.playa[i]) > 0.5 && float64(elev[i]) < low {
			low = float64(elev[i])
			sinkIdx = i
		}
	}
	f.sink = MapSink{Gx: IdxGx(sinkIdx), Gy: IdxGy(sinkIdx), Floor: low}

	// 资源点候选：从矿脉中心挑（成对，finalize 复核可达性）
	type cell = [2]int
	oreCells := make([]cell, 0, len(veins))
	for _, v := range veins {
		oreCells = append(oreCells, cell{v.Gx, v.Gy})
	}
	sort.SliceStable(oreCells, func(a, b int) bool { return cellLess(oreCells[a], oreCells[b]) })
	// 必须成对收集：只截前 8 个会拿到单侧若干点，镜像不在候选里就凑不成对
	var resCand []cell
	seenPair := map[cell]bool{}
	for _, cc := range oreCells {
		m := cell{Grid - 1 - cc[0], Grid - 1 - cc[1]}
		if seenPair[cc] {
			continue
		}
		has := false
		for _, d := range oreCells {
			if d == m {
				has = true
				break
			}
		}
		if !has {
			continue
		}
		resCand = append(resCand, cc, m)
		seenPair[cc] = true
		seenPair[m] = true
		if len(resCand) >= 8 {
			break
		}
	}

	f.mirrorMasks = map[string][]uint8{"ravine": ravine, "cave": cave, "ore": oreMask}
	f.mapExtra = &MapExtra{Ravine: ravine, Cave: cave, Ore: oreMask, BiomeSand: sand}
	f.climateExtra = &ClimateExtra{Family: "minecraft"}

	opt2 := *o
	opt2.Generator = "minecraft"
	opt2.ForcedRes = resCand
	return finalize(f, &opt2)
}

// GenerateMapMC 「我的世界」风格生成器公开入口（默认生成器）。
func GenerateMapMC(seed int, opts *GenOptions) *GameMap {
	o := opts.withDefaults()
	o.Generator = "minecraft"
	return runGenerate(seed, &o, minecraftBuildOnce)
}

// GenerateMap 按名字选生成器（未知名字报错，对应 JS generatorOf）。
func GenerateMap(seed int, opts *GenOptions) (*GameMap, error) {
	o := opts.withDefaults()
	switch o.Generator {
	case "minecraft":
		return GenerateMapMC(seed, &o), nil
	case "geomorphic":
		return GenerateMapGeomorphic(seed, &o), nil
	}
	return nil, errorsNew("未知的地图生成器：" + o.Generator + "（可选 minecraft / geomorphic）")
}

func errorsNew(s string) error { return &simpleError{s} }

type simpleError struct{ msg string }

func (e *simpleError) Error() string { return e.msg }
