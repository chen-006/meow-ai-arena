// 文件：internal/sim/mobility_test.go
// 版本：v1.4
// 功能：母舰机动性门禁。
//
// v1.4 把母舰坡限从 8*Scale（=32，实测地形上等于无限）压到 4 个高程单位。
// 削弱坡度必须止步于「还能开出基地」——否则母舰连点位都到不了，
// 那就不是削弱而是砍功能。
package sim

import "testing"

func TestMothershipCanLeaveBase(t *testing.T) {
	for _, seed := range fairnessSeeds {
		g, err := NewGame(seed, GameCfg{TickLimit: 600, Generator: "minecraft"})
		if err != nil {
			t.Fatalf("seed=%d 开局失败: %v", seed, err)
		}
		ms := g.Mothership(0)
		if ms == nil {
			t.Fatalf("seed=%d 没有母舰", seed)
		}
		x0, y0 := ms.X, ms.Y
		// 指挥母舰开向地图中心
		tx, ty := float64(World)/2, float64(World)/2
		for i := 0; i < 400 && !g.Done(); i++ {
			id := ms.ID
			g.Step([2][]Action{{{Type: "move", ID: &id, X: &tx, Y: &ty}}, nil})
		}
		moved := hypot(ms.X-x0, ms.Y-y0)
		if moved < 60 {
			t.Fatalf("[seed=%d] 母舰 400 拍只移动 %.1f px，坡限把它焊在基地上了", seed, moved)
		}
	}
}

func hypot(dx, dy float64) float64 {
	if dx < 0 {
		dx = -dx
	}
	if dy < 0 {
		dy = -dy
	}
	if dx > dy {
		return dx
	}
	return dy
}
