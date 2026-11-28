package schedule

import "testing"

func TestBuildFreezesSeedsAndMirror(t *testing.T) {
	entries := []Entry{{Name: "A"}, {Name: "B"}, {Name: "C"}, {Name: "D"}}
	cfg := Config{TickLimit: 600, DecideMs: 120, MaxStrikes: 3, StrikeMode: "consecutive"}
	p := Build(entries, []int{100, 200}, true, cfg)
	if len(p.Matches) != 2*6 {
		t.Fatalf("4 人 2 seed 应有 12 场，得到 %d", len(p.Matches))
	}
	if p.Matches[0].Seed != 100 || p.Matches[6].Seed != 206 {
		t.Fatalf("seed 应按全局场次递增（第二组从 200+6 起）：%d / %d", p.Matches[0].Seed, p.Matches[6].Seed)
	}
	if !p.Matches[0].Mirror {
		t.Fatal("mirror 未传递")
	}
	if p.Config.TickLimit != 600 {
		t.Fatal("规则配置未冻结")
	}
	// 同一输入必须得到同一赛程（除 JSON 序列化外，逐字段一致）
	p2 := Build(entries, []int{100, 200}, true, cfg)
	for i := range p.Matches {
		if p.Matches[i] != p2.Matches[i] {
			t.Fatalf("第 %d 场不可复现", i)
		}
	}
}
