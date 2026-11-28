package rating

import "testing"

func TestFitOrdersByStrength(t *testing.T) {
	// A 稳定胜 B，B 稳定胜 C，A 稳定胜 C。
	games := []Game{
		{A: "A", B: "B", Score: 1}, {A: "A", B: "B", Score: 1},
		{A: "B", B: "C", Score: 1}, {A: "B", B: "C", Score: 1},
		{A: "A", B: "C", Score: 1}, {A: "A", B: "C", Score: 1},
	}
	out := Fit(games, "A", 20, 1)
	if out.Ratings["A"] != 0 {
		t.Fatalf("锚定选手必须为 0，得到 %v", out.Ratings["A"])
	}
	if !(out.Ratings["B"] > out.Ratings["C"]) {
		t.Fatalf("B 应高于 C：%v vs %v", out.Ratings["B"], out.Ratings["C"])
	}
	if out.Games != 6 {
		t.Fatalf("局数应为 6，得到 %d", out.Games)
	}
}

func TestFitHandlesDraws(t *testing.T) {
	games := []Game{
		{A: "A", B: "B", Score: 0.5},
		{A: "A", B: "B", Score: 0.5},
	}
	out := Fit(games, "A", 0, 1)
	if out.Ratings["A"] != 0 || out.Ratings["B"] != 0 {
		t.Fatalf("全平局应同为基准 0，得到 %v / %v", out.Ratings["A"], out.Ratings["B"])
	}
	if out.Intervals["B"] != [2]float64{0, 0} {
		t.Fatalf("bootstrap=0 时区间应退化，得到 %v", out.Intervals["B"])
	}
}
