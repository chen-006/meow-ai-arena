// 攻守模式的 Bradley–Terry 评分：攻方实力 A 与守方实力 D 分开拟合。
//
// 版本：v1.2（Desert Arena 攻守变体评分）。
//
// 为什么不能直接用 bt.go：攻守是不对称的，「谁赢」取决于「谁的进攻」打「谁的防守」。
// 如果只给每条 Bot 一个实力值，攻强守弱和攻弱守强会被平均成同一个数。
// 这里用双参数模型：
//
//	P(攻方 i 破防 | 守方 j) = 1 / (1 + 10^((D_j - A_i)/400))
//
// 可辨识性靠一个约束：mean(A) + mean(D) = 0，也就是「平均攻方」与「平均守方」同档。
// 差 400 分约等于 10:1，与 bt.go、与官方第 6 集的口径一致。
package rating

import (
	"math"
	"sort"
)

const btScale = 400.0

// SiegeGame 是一局攻守对局。Score 是攻方得分：攻方胜 1 / 平 0.5 / 败 0。
// Block 是重抽样单位（这里用地图 seed）：同一张图上的对局不能被当成互相独立的样本。
type SiegeGame struct {
	Attacker string
	Defender string
	Score    float64
	Block    string
}

// SiegeResult 是双参数 BT 的拟合结果。
type SiegeResult struct {
	Method    string                `json:"method"`
	Games     int                   `json:"games"`
	Blocks    int                   `json:"blocks"`
	Bootstrap int                   `json:"bootstrap"`
	Scale     string                `json:"scale"`
	Attack    map[string]float64    `json:"attack"`
	Defense   map[string]float64    `json:"defense"`
	Overall   map[string]float64    `json:"overall"`
	AttackCI  map[string][2]float64 `json:"attackCi"`
	DefenseCI map[string][2]float64 `json:"defenseCi"`
	OverallCI map[string][2]float64 `json:"overallCi"`
	Ties      [][]string            `json:"ties"`
}

// pAttackerWin 是模型给出的攻方破防概率。
func pAttackerWin(a, d float64) float64 {
	return 1 / (1 + math.Pow(10, (d-a)/btScale))
}

// bisect 在 [lo,hi] 上找 f 的零点，f 必须单调递增。
func bisect(f func(float64) float64, lo, hi float64, iters int) float64 {
	for i := 0; i < iters; i++ {
		mid := (lo + hi) / 2
		if f(mid) < 0 {
			lo = mid
		} else {
			hi = mid
		}
	}
	return (lo + hi) / 2
}

// solveSiege 用 MM（逐参数二分）拟合 A 与 D。每对选手加一局虚拟平局，避免全胜/全败发散。
func solveSiege(games []SiegeGame) (map[string]float64, map[string]float64) {
	names := make([]string, 0, 8)
	idx := map[string]int{}
	get := func(name string) int {
		if i, ok := idx[name]; ok {
			return i
		}
		idx[name] = len(names)
		names = append(names, name)
		return idx[name]
	}

	type cell struct{ n, w float64 }
	matrix := map[[2]int]*cell{}
	for _, g := range games {
		i, j := get(g.Attacker), get(g.Defender)
		k := [2]int{i, j}
		if matrix[k] == nil {
			matrix[k] = &cell{}
		}
		matrix[k].n++
		matrix[k].w += g.Score
	}
	/* 虚拟平局：每对 +1 局、攻方 +0.5 胜，等价于给每个参数加一个弱先验。 */
	for _, c := range matrix {
		c.n++
		c.w += 0.5
	}

	m := len(names)
	type rowA struct {
		j    int
		n, w float64
	}
	type rowD struct {
		i    int
		n, w float64
	}
	rowsA := make([][]rowA, m)
	rowsD := make([][]rowD, m)
	for k, c := range matrix {
		i, j := k[0], k[1]
		rowsA[i] = append(rowsA[i], rowA{j, c.n, c.w})
		rowsD[j] = append(rowsD[j], rowD{i, c.n, c.n - c.w})
	}

	A := make([]float64, m)
	D := make([]float64, m)
	for it := 0; it < 300; it++ {
		for i := 0; i < m; i++ {
			if len(rowsA[i]) == 0 {
				continue
			}
			win := 0.0
			for _, r := range rowsA[i] {
				win += r.w
			}
			ii := i
			A[i] = bisect(func(a float64) float64 {
				s := 0.0
				for _, r := range rowsA[ii] {
					s += r.n * pAttackerWin(a, D[r.j])
				}
				return s - win
			}, -4000, 4000, 60)
		}
		for j := 0; j < m; j++ {
			if len(rowsD[j]) == 0 {
				continue
			}
			loss := 0.0
			for _, r := range rowsD[j] {
				loss += r.w
			}
			jj := j
			D[j] = bisect(func(d float64) float64 {
				s := 0.0
				for _, r := range rowsD[jj] {
					s += r.n * (1 - pAttackerWin(A[r.i], d))
				}
				return s - loss
			}, -4000, 4000, 60)
		}
		/* 单一约束：整体平移不改变概率，把两边的平均水平对齐到 0。 */
		sa, sd := 0.0, 0.0
		for i := 0; i < m; i++ {
			sa += A[i]
			sd += D[i]
		}
		c := (sa + sd) / (2 * float64(m))
		for i := 0; i < m; i++ {
			A[i] -= c
			D[i] -= c
		}
	}

	outA := map[string]float64{}
	outD := map[string]float64{}
	for i, name := range names {
		outA[name] = A[i]
		outD[name] = D[i]
	}
	return outA, outD
}

// FitSiege 拟合攻/守双参数实力值，并按 Block（地图 seed）整块 bootstrap 给出 95% 区间。
// bootstrap<=0 时跳过重抽样，区间退化为点估计。
func FitSiege(games []SiegeGame, bootstrap int, seed int64) SiegeResult {
	res := SiegeResult{
		Method: "双参数 Bradley–Terry（攻方 A / 守方 D 分开拟合）+ 每对一局虚拟平局 + 按 seed 整块 bootstrap 95%",
		Games:  len(games), Bootstrap: bootstrap,
		Scale:  "整体均值锚定 0，攻方差 400 ≈ 10:1；区间按地图 seed 整块重抽样",
		Attack: map[string]float64{}, Defense: map[string]float64{}, Overall: map[string]float64{},
		AttackCI: map[string][2]float64{}, DefenseCI: map[string][2]float64{}, OverallCI: map[string][2]float64{},
	}
	if len(games) == 0 {
		return res
	}
	baseA, baseD := solveSiege(games)
	names := make([]string, 0, len(baseA))
	for n := range baseA {
		names = append(names, n)
	}
	sort.Strings(names)
	for _, n := range names {
		res.Attack[n] = round1(baseA[n])
		res.Defense[n] = round1(baseD[n])
		res.Overall[n] = round1((baseA[n] + baseD[n]) / 2)
	}

	blockSet := map[string]bool{}
	for _, g := range games {
		blockSet[g.Block] = true
	}
	blocks := make([]string, 0, len(blockSet))
	for b := range blockSet {
		blocks = append(blocks, b)
	}
	sort.Strings(blocks)
	res.Blocks = len(blocks)

	if bootstrap > 0 && len(blocks) > 1 {
		byBlock := map[string][]SiegeGame{}
		for _, g := range games {
			byBlock[g.Block] = append(byBlock[g.Block], g)
		}
		rng := seed
		if rng == 0 {
			rng = 20261113
		}
		rand := func() float64 {
			rng = (rng*1664525 + 1013904223) & 0xffffffff
			return float64(rng) / 4294967296.0
		}
		samplesA := map[string][]float64{}
		samplesD := map[string][]float64{}
		for b := 0; b < bootstrap; b++ {
			bag := make([]SiegeGame, 0, len(games))
			for k := 0; k < len(blocks); k++ {
				bag = append(bag, byBlock[blocks[int(rand()*float64(len(blocks)))%len(blocks)]]...)
			}
			a, d := solveSiege(bag)
			for _, n := range names {
				samplesA[n] = append(samplesA[n], a[n])
				samplesD[n] = append(samplesD[n], d[n])
			}
		}
		q := func(arr []float64, p float64) float64 {
			if len(arr) == 0 {
				return 0
			}
			sort.Float64s(arr)
			i := int(p * float64(len(arr)))
			if i >= len(arr) {
				i = len(arr) - 1
			}
			return arr[i]
		}
		for _, n := range names {
			res.AttackCI[n] = [2]float64{round1(q(samplesA[n], 0.025)), round1(q(samplesA[n], 0.975))}
			res.DefenseCI[n] = [2]float64{round1(q(samplesD[n], 0.025)), round1(q(samplesD[n], 0.975))}
			overall := make([]float64, 0, len(samplesA[n]))
			for i := range samplesA[n] {
				overall = append(overall, (samplesA[n][i]+samplesD[n][i])/2)
			}
			res.OverallCI[n] = [2]float64{round1(q(overall, 0.025)), round1(q(overall, 0.975))}
		}
	} else {
		for _, n := range names {
			res.AttackCI[n] = [2]float64{res.Attack[n], res.Attack[n]}
			res.DefenseCI[n] = [2]float64{res.Defense[n], res.Defense[n]}
			res.OverallCI[n] = [2]float64{res.Overall[n], res.Overall[n]}
		}
	}

	/* 并列判定：按综合实力降序，只看「相邻两名」的区间是否重叠
	 *（next.hi >= cur.lo）。用组内最小上界去比会把整张榜串成一串，不能那么写。 */
	sort.SliceStable(names, func(i, j int) bool { return res.Overall[names[i]] > res.Overall[names[j]] })
	var group []string
	curLo := math.Inf(1)
	for _, n := range names {
		lo, hi := res.OverallCI[n][0], res.OverallCI[n][1]
		if len(group) > 0 && hi >= curLo {
			group = append(group, n)
			if lo < curLo {
				curLo = lo
			}
			continue
		}
		if len(group) > 0 {
			res.Ties = append(res.Ties, group)
		}
		group = []string{n}
		curLo = lo
	}
	if len(group) > 0 {
		res.Ties = append(res.Ties, group)
	}
	return res
}
