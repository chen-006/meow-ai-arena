// 文件：internal/sim/victory_test.go
// 版本：v1.3
// 功能：胜负链可达性门禁。「母舰被毁」是五级判定链的第一条，
//
//	如果它在数值上不可达，正式赛就退化成一维 VP 赛跑（v1.2 实测 40/40 局 level=2）。
//
// 本测试用一支「够得着」的最小兵力验证：3 台 RAILGUN（射程 9.5 > 母舰 7）
// 在母舰射程外持续输出，必须在 tickLimit 内打穿 3000 HP。
package sim

import "testing"

func TestMothershipKillReachable(t *testing.T) {
	for _, seed := range fairnessSeeds[:3] {
		g, err := NewGame(seed, GameCfg{TickLimit: 1200, Generator: "minecraft"})
		if err != nil {
			t.Fatalf("seed=%d 开局失败: %v", seed, err)
		}
		ms := g.Mothership(1)
		if ms == nil {
			t.Fatalf("seed=%d 没有母舰", seed)
		}
		// 3 台 RAILGUN 站在母舰射程之外（25 格 < railgun 38 格，> 母舰 28 格 × 0.82 实际射程）
		for i := 0; i < 3; i++ {
			u := g.spawn(0, "RAILGUN", ms.X+float64(25*Cell), ms.Y+float64(i*Cell))
			u.Mode = "obey"
		}
		hp0 := ms.HP
		for !g.Done() && g.Turn < g.Cfg.TickLimit {
			g.Step([2][]Action{nil, nil})
			if ms.Dead {
				break
			}
		}
		if !ms.Dead {
			t.Fatalf("[seed=%d] 3 台 RAILGUN 打满 %d 拍仍未击毁母舰（HP %.0f/%.0f）——斩舰不可达",
				seed, g.Cfg.TickLimit, ms.HP, hp0)
		}
		if g.Turn > 1000 {
			t.Fatalf("[seed=%d] 斩舰耗时 %d 拍，已接近 tickLimit，实战不可能达成", seed, g.Turn)
		}
	}
}

// 脱战自愈：母舰在持续挨打期间不得回血，否则小股部队的伤害会被完全抹平。
func TestMothershipNoRepairUnderFire(t *testing.T) {
	g, err := NewGame(fairnessSeeds[0], GameCfg{TickLimit: 400, Generator: "minecraft"})
	if err != nil {
		t.Fatal(err)
	}
	ms := g.Mothership(1)
	if ms == nil {
		t.Fatal("没有母舰")
	}
	g.Sides[1].Target.Repair = 100
	g.Sides[1].Alloc.Repair = 100
	ms.HP = 1000
	// 每 40 拍挨一下（小于 MSRepairDelay，保持脱战门禁关闭）
	for i := 0; i < 10; i++ {
		g.damageUnit(ms, 1, 0, 0)
		before := ms.HP
		for j := 0; j < MSRepairDelay/2; j++ {
			g.Step([2][]Action{nil, nil})
		}
		if ms.HP > before {
			t.Fatalf("挨打期间母舰回血：%.3f → %.3f", before, ms.HP)
		}
	}
	// 脱战后必须恢复自愈
	before := ms.HP
	for j := 0; j < MSRepairDelay*2; j++ {
		g.Step([2][]Action{nil, nil})
	}
	if ms.HP <= before {
		t.Fatalf("脱战 %d 拍后母舰仍未自愈：%.3f → %.3f", MSRepairDelay*2, before, ms.HP)
	}
}
