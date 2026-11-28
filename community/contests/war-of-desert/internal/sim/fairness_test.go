// 文件：internal/sim/fairness_test.go
// 版本：v1.3
// 功能：公平性门禁。对称赛的两条硬不变量：
//       1) 资源类型按镜像配对分配——同对两格必须同类，且两侧 RU 数量相等；
//       2) 引擎在一组「旋转不变策略」下必须产生精确平局——任何座位优势都是引擎缺陷。
//
// 这两条针对的是 v1.2 及以前的两个真实缺陷：
//   - 资源类型按排序后下标 i%3 赋，导致每张图只剩 1 个 RU 矿且只归一侧；
//   - 索敌平局用全局 ID 决胜，而 ID 两侧分属不同区间，破坏镜像协变性。
package sim

import (
	"math"
	"testing"
)

var fairnessSeeds = []int{20261109, 20261110, 20261112, 20261119, 20261132}

// 资源类型必须按镜像配对同类，且两侧各拿到相同数量的 RU。
func TestResourceKindsMirrorSymmetry(t *testing.T) {
	for _, seed := range fairnessSeeds {
		m, err := GenerateMap(seed, &GenOptions{Generator: "minecraft"})
		if err != nil {
			t.Fatalf("seed=%d 生成地图失败: %v", seed, err)
		}
		kinds := ResourceKinds(m.ResCells)
		if len(kinds) != len(m.ResCells) {
			t.Fatalf("seed=%d kinds 长度不符", seed)
		}
		idx := make(map[[2]int]int, len(m.ResCells))
		for i, c := range m.ResCells {
			idx[c] = i
		}
		ru := 0
		for i, c := range m.ResCells {
			if kinds[i] == "ru" {
				ru++
			}
			mir := [2]int{Grid - 1 - c[0], Grid - 1 - c[1]}
			j, ok := idx[mir]
			if !ok {
				t.Fatalf("[seed=%d] %v 没有镜像格", seed, c)
			}
			if kinds[j] != kinds[i] {
				t.Fatalf("[seed=%d] 镜像对异类：%v=%s 但 %v=%s", seed, c, kinds[i], mir, kinds[j])
			}
		}
		if len(m.ResCells) >= 2 && ru == 0 {
			t.Fatalf("[seed=%d] 没有任何 RU 资源点", seed)
		}
		if ru%2 != 0 {
			t.Fatalf("[seed=%d] RU 资源点数 %d 为奇数，两侧不可能相等", seed, ru)
		}
		side := [2]int{}
		for i, c := range m.ResCells {
			if kinds[i] != "ru" {
				continue
			}
			d0 := math.Hypot(float64(c[0]-m.SpawnCells[0][0]), float64(c[1]-m.SpawnCells[0][1]))
			d1 := math.Hypot(float64(c[0]-m.SpawnCells[1][0]), float64(c[1]-m.SpawnCells[1][1]))
			if d0 < d1 {
				side[0]++
			} else {
				side[1]++
			}
		}
		if side[0] != side[1] {
			t.Fatalf("[seed=%d] 两侧 RU 归属不均：%v", seed, side)
		}
	}
}

// 旋转不变策略：所有非母舰单位向地图中心推进。
// 该策略在 180° 旋转下不变，镜像对局必须逐项相同。
func TestMirrorSymmetryInvariantPolicy(t *testing.T) {
	cx := float64(World) / 2
	for _, seed := range fairnessSeeds {
		g, err := NewGame(seed, GameCfg{TickLimit: 600, Generator: "minecraft"})
		if err != nil {
			t.Fatalf("seed=%d 开局失败: %v", seed, err)
		}
		for !g.Done() && g.Turn < g.Cfg.TickLimit {
			var orders [2][]Action
			for _, u := range g.Units {
				if u.Dead || u.Kind == "MOTHERSHIP" || u.Kind == "SALVAGER" {
					continue
				}
				id, x, y := u.ID, cx, cx
				orders[u.Side] = append(orders[u.Side], Action{
					Type: "move", ID: &id, X: &x, Y: &y,
				})
			}
			g.Step(orders)
		}
		s0, s1 := g.Sides[0], g.Sides[1]
		if s0.VPScore != s1.VPScore {
			t.Fatalf("[seed=%d] 座位优势：VP %d vs %d（旋转不变策略下必须相等）", seed, s0.VPScore, s1.VPScore)
		}
		if s0.VPTicks != s1.VPTicks {
			t.Fatalf("[seed=%d] 占点 tick 不等：%d vs %d", seed, s0.VPTicks, s1.VPTicks)
		}
		if a, b := aliveMilitary(g, 0), aliveMilitary(g, 1); a != b {
			t.Fatalf("[seed=%d] 存活单位数不等：%d vs %d", seed, a, b)
		}
		if a, b := militaryHP(g, 0), militaryHP(g, 1); math.Abs(a-b) > 1e-6 {
			t.Fatalf("[seed=%d] 存活单位总血量不等：%.3f vs %.3f", seed, a, b)
		}
		if a, b := g.Mothership(0), g.Mothership(1); (a == nil) != (b == nil) {
			t.Fatalf("[seed=%d] 母舰存活状态不一致", seed)
		} else if a != nil && math.Abs(a.HP-b.HP) > 1e-6 {
			t.Fatalf("[seed=%d] 母舰血量不等：%.3f vs %.3f", seed, a.HP, b.HP)
		}
	}
}

func aliveMilitary(g *Game, side int) int {
	n := 0
	for _, u := range g.Units {
		if !u.Dead && u.Side == side && u.Kind != "MOTHERSHIP" {
			n++
		}
	}
	return n
}

func militaryHP(g *Game, side int) float64 {
	s := 0.0
	for _, u := range g.Units {
		if !u.Dead && u.Side == side && u.Kind != "MOTHERSHIP" {
			s += u.HP
		}
	}
	return s
}
