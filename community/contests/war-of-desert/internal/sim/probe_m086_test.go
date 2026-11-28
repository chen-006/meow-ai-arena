package sim

import (
	"fmt"
	"testing"
)

// 探针：m086 地图（seed 20261209）采集运输线是否通畅。
func TestProbeM086Economy(t *testing.T) {
	g, err := NewGame(20261209, GameCfg{Generator: "minecraft", TickLimit: 1500, Ruleset: RulesetV15})
	if err != nil {
		t.Fatal(err)
	}
	side := 1
	// 模拟 acebot：第一台采 cu，第二台采 ru，只指派一次。
	n := 0
	for _, u := range g.Units {
		if u.Side != side || u.Kind != "SALVAGER" || n >= 2 {
			continue
		}
		kind := "cu"
		if n == 1 {
			kind = "ru"
		}
		var best *Resource
		bd := 1e18
		for _, r := range g.Resources {
			if r.Kind != kind {
				continue
			}
			d := distXY(u.X, u.Y, r.X, r.Y)
			if d < bd {
				bd, best = d, r
			}
		}
		if best != nil {
			rid := best.ID
			u.Harvest = &rid
			u.HState = "toResource"
			t.Logf("assign sv%d -> res%d (%s) at (%.0f,%.0f)", u.ID, best.ID, kind, best.X, best.Y)
			n++
		}
	}
	for _, r := range g.Resources {
		t.Logf("res%d %s (%.0f,%.0f) amount=%.0f", r.ID, r.Kind, r.X, r.Y, r.Amount)
	}
	cv := g.Mothership(side)
	t.Logf("MS at (%.0f,%.0f)", cv.X, cv.Y)
	for i := 0; i < 900; i++ {
		g.Step([2][]Action{nil, nil})
		if g.Turn%100 == 0 {
			for _, u := range g.Units {
				if u.Side == side && u.Kind == "SALVAGER" && u.Harvest != nil {
					t.Logf("t%4d sv%d (%.0f,%.0f) h=%s carryCU=%.0f carryRU=%.0f", g.Turn, u.ID, u.X, u.Y, u.HState, u.CarryCU, u.CarryRU)
				}
			}
			t.Logf("t%4d CU=%.0f RU=%.0f", g.Turn, g.Sides[side].CU, g.Sides[side].RU)
		}
	}
	if g.Sides[side].CU < 200 {
		t.Fatalf("经济死锁：900 拍 CU=%.0f", g.Sides[side].CU)
	}
	fmt.Println("probe done")
}