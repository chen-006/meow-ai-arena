// Package rating 实现 Bradley–Terry 实力值拟合。
//
// 版本：v1.2（Desert Arena 正式赛执行链）。
// 功能：对称赛单参数 BT、bootstrap 95% 区间、加权总分。
//
// 口径与 UP 主原版 arena/bt.js 一致：基准锚定 0，差 400 分约等于 10:1；
// 每对选手加一局虚拟平局抑制全胜/全败导致的最大似然发散；
// 95% 区间按「对局整块」重抽样（bootstrap），不做单局独立假设。
package rating

import (
	"math"
	"sort"
)

// Game 是一局的结果，Score 是 A 的得分：胜 1 / 平 0.5 / 负 0。
//
// Group 是 bootstrap 的整块编号（>0 才生效）：同一场对阵的换边两局必须一起
// 重抽样，否则会把「同一张图、同一对选手」的两局当成独立样本，区间偏窄。
type Game struct {
	A     string
	B     string
	Score float64
	Group int
}

// Result 是拟合结果。
type Result struct {
	Method    string                `json:"method"`
	Anchor    string                `json:"anchor"`
	Games     int                   `json:"games"`
	Bootstrap int                   `json:"bootstrap"`
	Ratings   map[string]float64    `json:"ratings"`
	Intervals map[string][2]float64 `json:"intervals"`
	Scale     string                `json:"scale"`
}

type solved struct {
	names   []string
	ratings map[string]float64
	anchor  string
}

func solve(games []Game, anchorName string, iterations int) solved {
	var names []string
	idx := map[string]int{}
	for _, g := range games {
		for _, n := range []string{g.A, g.B} {
			if _, ok := idx[n]; !ok {
				idx[n] = len(names)
				names = append(names, n)
			}
		}
	}
	n := len(names)
	wins := make([]float64, n)
	pair := make([][]float64, n)
	for i := range pair {
		pair[i] = make([]float64, n)
	}
	for _, g := range games {
		i, j := idx[g.A], idx[g.B]
		if i == j {
			continue
		}
		wins[i] += g.Score
		wins[j] += 1 - g.Score
		pair[i][j]++
		pair[j][i]++
	}
	/* 每对加一局虚拟平局：避免完全分离让 MM 迭代发散。 */
	for i := 0; i < n; i++ {
		for j := i + 1; j < n; j++ {
			if pair[i][j] == 0 {
				continue
			}
			wins[i] += 0.5
			wins[j] += 0.5
			pair[i][j]++
			pair[j][i]++
		}
	}
	s := make([]float64, n)
	for i := range s {
		s[i] = 1
	}
	for it := 0; it < iterations; it++ {
		next := make([]float64, n)
		var sumLog float64
		for i := 0; i < n; i++ {
			var denom float64
			for j := 0; j < n; j++ {
				if i != j && pair[i][j] > 0 {
					denom += pair[i][j] / (s[i] + s[j])
				}
			}
			if denom > 0 {
				next[i] = wins[i] / denom
			} else {
				next[i] = s[i]
			}
			sumLog += math.Log(math.Max(1e-9, next[i]))
		}
		gm := math.Exp(sumLog / math.Max(1, float64(n)))
		for i := 0; i < n; i++ {
			s[i] = math.Max(1e-9, next[i]/gm)
		}
	}
	ai, ok := idx[anchorName]
	if !ok {
		ai = 0
	}
	out := solved{names: names, ratings: map[string]float64{}, anchor: names[ai]}
	for i, name := range names {
		out.ratings[name] = 400 * math.Log10(s[i]/s[ai])
	}
	return out
}

// buildBlocks 按 Group 把对局分块；没有设置 Group 时每局自成一块。
func buildBlocks(games []Game) [][]Game {
	grouped := false
	for _, g := range games {
		if g.Group > 0 {
			grouped = true
			break
		}
	}
	if !grouped {
		out := make([][]Game, 0, len(games))
		for _, g := range games {
			out = append(out, []Game{g})
		}
		return out
	}
	idx := map[int]int{}
	var out [][]Game
	for _, g := range games {
		key := g.Group
		if key <= 0 {
			out = append(out, []Game{g})
			continue
		}
		if i, ok := idx[key]; ok {
			out[i] = append(out[i], g)
			continue
		}
		idx[key] = len(out)
		out = append(out, []Game{g})
	}
	return out
}

// Fit 拟合全池实力值并给出 bootstrap 区间。bootstrap<=0 时跳过重抽样。
func Fit(games []Game, anchorName string, bootstrap int, seed int64) Result {
	if len(games) == 0 {
		return Result{Method: "Bradley–Terry (MM)", Games: 0, Ratings: map[string]float64{}, Intervals: map[string][2]float64{}}
	}
	if anchorName == "" {
		anchorName = games[0].A
	}
	base := solve(games, anchorName, 200)
	out := Result{
		Method:    "Bradley–Terry (MM) + 每对一局虚拟平局 + bootstrap 95%",
		Anchor:    base.anchor,
		Games:     len(games),
		Bootstrap: bootstrap,
		Ratings:   map[string]float64{},
		Intervals: map[string][2]float64{},
		Scale:     "基准 = 0，差 400 ≈ 10:1；区间按对局整块重抽样",
	}
	samples := map[string][]float64{}
	for _, n := range base.names {
		out.Ratings[n] = round1(base.ratings[n])
		samples[n] = nil
	}
	if bootstrap > 0 && len(games) > 1 {
		rng := seed
		if rng == 0 {
			rng = 12345
		}
		rand := func() float64 {
			rng = (rng*1664525 + 1013904223) & 0xffffffff
			return float64(rng) / 4294967296.0
		}
		// 按 Group 整块抽样；没有 Group 时退化为逐局抽样（与旧行为一致）。
		blocks := buildBlocks(games)
		for b := 0; b < bootstrap; b++ {
			resampled := make([]Game, 0, len(games))
			for k := 0; k < len(blocks); k++ {
				resampled = append(resampled, blocks[int(rand()*float64(len(blocks)))%len(blocks)]...)
			}
			r := solve(resampled, anchorName, 80)
			for _, n := range base.names {
				v, ok := r.ratings[n]
				if !ok {
					v = 0
				}
				samples[n] = append(samples[n], v)
			}
		}
	}
	for _, n := range base.names {
		arr := samples[n]
		if len(arr) == 0 {
			out.Intervals[n] = [2]float64{out.Ratings[n], out.Ratings[n]}
			continue
		}
		sort.Float64s(arr)
		q := func(p float64) float64 {
			i := int(p * float64(len(arr)))
			if i >= len(arr) {
				i = len(arr) - 1
			}
			return arr[i]
		}
		out.Intervals[n] = [2]float64{round1(q(0.025)), round1(q(0.975))}
	}
	return out
}

func round1(v float64) float64 { return math.Round(v*10) / 10 }
