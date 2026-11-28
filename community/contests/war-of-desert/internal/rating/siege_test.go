package rating

import (
	"math"
	"testing"
)

/* 合成数据：给定真值 A/D，按模型概率生成对局，看能不能把差值估回来。
 * 只要 A_i - D_j 的排序与差值恢复，评分口径就是自洽的。 */
func TestFitSiegeRecoversDifference(t *testing.T) {
	truthA := map[string]float64{"强攻": 240, "中攻": 60, "弱攻": -300, "守匠": 0}
	truthD := map[string]float64{"强攻": -120, "中攻": 80, "弱攻": 40, "守匠": 320}
	names := []string{"强攻", "中攻", "弱攻", "守匠"}

	// 用固定序列的伪随机，保证测试可重复
	seed := int64(7)
	rand := func() float64 {
		seed = (seed*1664525 + 1013904223) & 0xffffffff
		return float64(seed) / 4294967296.0
	}

	var games []SiegeGame
	for round := 0; round < 4000; round++ {
		for _, a := range names {
			for _, d := range names {
				if a == d {
					continue
				}
				if rand() > 0.25 { // 抽样一部分对局，避免完全图过强
					continue
				}
				p := pAttackerWin(truthA[a], truthD[d])
				score := 0.0
				if rand() < p {
					score = 1
				}
				games = append(games, SiegeGame{Attacker: a, Defender: d, Score: score, Block: "b" + string(rune('0'+round%20))})
			}
		}
	}

	gotA, gotD := solveSiege(games)
	for _, a := range names {
		for _, d := range names {
			want := truthA[a] - truthD[d]
			got := gotA[a] - gotD[d]
			if math.Abs(want-got) > 90 { // 4000 轮抽样下允许 ~90 分的估计误差
				t.Fatalf("%s 攻 vs %s 守：真值差 %.0f，估计差 %.0f", a, d, want, got)
			}
		}
	}
}

/* 区间与并列：数据量足够时，明显更强的选手不应与最弱者并列。 */
func TestFitSiegeIntervalsAndTies(t *testing.T) {
	var games []SiegeGame
	strong := []string{"甲", "乙"}
	weak := []string{"丙", "丁"}
	add := func(a, d string, score float64, block string) {
		games = append(games, SiegeGame{Attacker: a, Defender: d, Score: score, Block: block})
	}
	for b := 0; b < 12; b++ {
		block := "seed" + string(rune('A'+b))
		for _, s := range strong {
			for _, w := range weak {
				add(s, w, 1, block) // 强攻破弱守
				add(w, s, 0, block) // 弱攻打不穿强守
			}
		}
	}
	res := FitSiege(games, 200, 42)
	if res.Blocks != 12 {
		t.Fatalf("block 数应为 12，实际 %d", res.Blocks)
	}
	for _, n := range strong {
		if res.Overall[n] <= res.Overall[weak[0]] {
			t.Fatalf("%s 的综合实力应高于 %s（%.1f vs %.1f）", n, weak[0], res.Overall[n], res.Overall[weak[0]])
		}
	}
	// 强组内部数据完全对称，应当并列
	if len(res.Ties) == 0 || len(res.Ties[0]) < 2 {
		t.Fatalf("强组应当并列，实际 ties=%v", res.Ties)
	}
}
