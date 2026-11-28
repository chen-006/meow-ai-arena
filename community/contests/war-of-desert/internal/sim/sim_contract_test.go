package sim

import (
	"math"
	"strings"
	"testing"
)

// 契约测试：与 JS（Dev/MVP 工作树）逐位/近逐位对齐的移植锚点。
// 向量来源：engine/parity/vectors.json（node engine/parity/vectors.js 生成）。
// rng/hash 是纯整数运算，必须逐位一致；ballistics 浮点允许 1e-9 相对误差
// （Math.hypot/pow 与 Go math 包的 ulp 差异）。

func approxEq(got, want float64) bool {
	if want == 0 {
		return math.Abs(got) <= 1e-12
	}
	return math.Abs(got-want) <= 1e-9*math.Abs(want)
}

func TestMulberry32AgainstJS(t *testing.T) {
	vectors := map[int][]float64{
		20261109: {0.5692986291833222, 0.22896213573403656, 0.7234888540115207, 0.2961047347635031,
			0.4008527167607099, 0.7535514736082405, 0.03901658905670047, 0.6905814073979855},
		1: {0.6270739405881613, 0.002735721180215478, 0.5274470399599522, 0.9810509674716741,
			0.9683778982143849, 0.281103502959013, 0.6128388606011868, 0.7207431411370635},
		4294967295: {0.8964226141106337, 0.189478256739676, 0.7156526781618595, 0.9440599093213677,
			0.8452364315744489, 0.5391399988438934, 0.6804977387655526, 0.4755720964167267},
	}
	for seed, want := range vectors {
		r := NewRng(seed)
		for i, w := range want {
			if got := r.Next(); got != w {
				t.Fatalf("mulberry32 seed=%d 第 %d 个值不一致: got %v want %v", seed, i, got, w)
			}
		}
	}
}

func TestHash2AgainstJS(t *testing.T) {
	tcs := []struct {
		seed int32
		x, y int
		want float64
	}{
		{20261109, 0, 0, 0.12553007737733424},
		{20261109, 1, 2, 0.7014403184875846},
		{7, 255, 255, 0.45004814001731575},
	}
	for _, c := range tcs {
		if got := Hash2(c.seed, int(c.x), int(c.y)); got != c.want {
			t.Fatalf("hash2(%d,%v,%v): got %v want %v", c.seed, c.x, c.y, got, c.want)
		}
	}
	/* 负数格点：JS 侧 x|0，Go 侧 uint32 回卷 —— 位模式一致 */
	if got, want := Hash2(42, -3, 7), 0.6351017453707755; got != want {
		t.Fatalf("hash2(42,-3,7): got %v want %v", got, want)
	}
}

func TestSeedSaltAgainstJS(t *testing.T) {
	cases := []struct {
		seed, attempt int
		want          int32
	}{
		{20261109, 0, -1946967937},
		{20261109, 1, 2060209692},
		{20261109, 31, -1568515663},
		{1, 0, 1821981464},
		{20261110, 0, 498840283},
	}
	for _, c := range cases {
		if got := SeedSalt(c.seed, c.attempt); got != c.want {
			t.Fatalf("seedSalt(%d,%d): got %v want %v", c.seed, c.attempt, got, c.want)
		}
	}
}

func TestNoiseAgainstJS(t *testing.T) {
	type nv struct{ vn, fbm6, fbm3 float64 }
	cases := []struct {
		seed int32
		x, y float64
		want nv
	}{
		{20261109, 12.4, 8.9, nv{0.7609777478111609, 0.6612166219561302, 0.5900957385235132}},
		{7, 0.5, 0.5, nv{0.2628041062853299, 0.3413633414420474, 0.3230736378172878}},
		{99, 123.75, 456.25, nv{0.30535691645195584, 0.3089695924230682, 0.291500282170432}},
	}
	for _, c := range cases {
		vn := ValueNoise(c.seed, c.x, c.y)
		f6 := FBM(c.seed, c.x, c.y)
		f3 := FBMN(c.seed, c.x, c.y, 3)
		if vn != c.want.vn || f6 != c.want.fbm6 || f3 != c.want.fbm3 {
			t.Fatalf("noise(%d,%v,%v): got (%v,%v,%v) want (%v,%v,%v)",
				c.seed, c.x, c.y, vn, f6, f3, c.want.vn, c.want.fbm6, c.want.fbm3)
		}
	}
}

func TestFNV1AAgainstJS(t *testing.T) {
	cases := []struct{ in, want string }{
		{"", "811c9dc5"},
		{"a", "e40c292c"},
		{"hello", "4f9f2cab"},
		{"wod-arena", "d915c6bf"},
		{"中文中文中文", "c051b6ed"},
		{"VP 控制", "a4ba246c"},
		{`{"json":true,"n":1.5}`, "91a563b3"},
		{strings.Repeat("x", 1000), "07d18165"},
	}
	for _, c := range cases {
		if got := FNV1A(c.in); got != c.want {
			t.Fatalf("fnv1a(%q): got %v want %v", c.in, got, c.want)
		}
	}
}

func TestBallisticsConstants(t *testing.T) {
	if Rho != 1.225 || AirK != 4.0e-4 || GravityG != 300 || MPerCell != 10 || CellPX != 6 ||
		ElevM != 0.30 || MountainH != 55 || UnitH != 2.4 || HitRM != 7.5 || BallDT != 0.01 || BallMaxT != 8 {
		t.Fatal("弹道常数与 JS 基线不一致")
	}
	if MPerPX != MPerCell/float64(CellPX) {
		t.Fatal("MPerPx 口径错")
	}
}

func TestBallisticsFlyAgainstJS(t *testing.T) {
	cases := []struct {
		name                 string
		v0, theta, h0, tgt   float64
		ok                   bool
		s, tt, vImpact, apex float64
		nPoints              int
	}{
		{"240m/s θ2°", 240, 2, 2.4, 2.4, true, 13.345701928778354, 0.055789794921875, 238.72300896204655, 2.5160446099127713, 4},
		{"240m/s θ30°", 240, 30, 2.4, 2.4, true, 158.8658906699729, 0.790527954101563, 226.98565243473536, 25.836081755786733, 36},
		{"180m/s θ45° h→0", 180, 45, 2.4, 0, true, 106.76324311502344, 0.859754028320313, 178.0690754360304, 28.92213431828302, 23},
	}
	for _, c := range cases {
		fr := Fly(c.v0, c.theta, c.h0, c.tgt, 0, 0)
		if fr.OK != c.ok {
			t.Fatalf("%s: ok=%v want %v", c.name, fr.OK, c.ok)
		}
		if !approxEq(fr.S, c.s) || !approxEq(fr.T, c.tt) || !approxEq(fr.VImpact, c.vImpact) || !approxEq(fr.Apex, c.apex) {
			t.Fatalf("%s: got (s=%v t=%v v=%v apex=%v) want (s=%v t=%v v=%v apex=%v)",
				c.name, fr.S, fr.T, fr.VImpact, fr.Apex, c.s, c.tt, c.vImpact, c.apex)
		}
		if len(fr.Points) != c.nPoints {
			t.Fatalf("%s: 采样点数 got %d want %d", c.name, len(fr.Points), c.nPoints)
		}
	}
}

func TestBallisticsSolutionsAgainstJS(t *testing.T) {
	/* d=128：240 m/s 有唯一低伸解；d=300：出包线，无解 */
	s1 := Solutions(SolveParams{V0: 240, H0: 2.4, D: 128, DH: 0, ElevMin: 0, ElevMax: 45})
	if len(s1) != 1 || !approxEq(s1[0].Theta, 21.844940185546875) || !approxEq(s1[0].S, 127.99634280728569) {
		t.Fatalf("d=128 解集不符: %+v", s1)
	}
	s2 := Solutions(SolveParams{V0: 240, H0: 2.4, D: 300, DH: 0, ElevMin: 0, ElevMax: 45})
	if len(s2) != 0 {
		t.Fatalf("d=300 应无解，得到 %d 个", len(s2))
	}
}

func TestBallisticsDamageAgainstJS(t *testing.T) {
	cases := []struct {
		name string
		got  float64
		want float64
	}{
		{"AP 零距离基准=dmg", DamageOf("AP", 10, 300, 300, "light"), 10},
		{"AP 落速减半（exp=2）", DamageOf("AP", 10, 300, 150, "light"), 2.5},
		{"HE 半速重甲", DamageOf("HE", 10, 300, 150, "heavy"), 3.2163005325089298},
		{"APCR 半速轻甲", DamageOf("APCR", 10, 300, 150, "light"), 1.4625000000000001},
	}
	for _, c := range cases {
		if !approxEq(c.got, c.want) {
			t.Fatalf("%s: got %v want %v", c.name, c.got, c.want)
		}
	}
}

func TestBallisticsSplashAndSolve(t *testing.T) {
	sp := []float64{SplashFactor(0, 30), SplashFactor(15, 30), SplashFactor(30, 30), SplashFactor(31, 30), SplashFactor(10, 0)}
	want := []float64{1, 0.5, 0, 0, 0}
	for i := range want {
		if !approxEq(sp[i], want[i]) {
			t.Fatalf("splash[%d]: got %v want %v", i, sp[i], want[i])
		}
	}
	/* 同输入必同解（确定性契约） */
	a := Solve(SolveParams{V0: 240, H0: 2.4, D: 128, DH: 0, ElevMin: 0, ElevMax: 45})
	b := Solve(SolveParams{V0: 240, H0: 2.4, D: 128, DH: 0, ElevMin: 0, ElevMax: 45})
	if a.Theta != b.Theta || a.S != b.S {
		t.Fatal("Solve 同输入不同解")
	}
}

func TestNoDragReference(t *testing.T) {
	/* 积分器含阻力必然短于解析解；且与 JS 向量一致到 1e-9 */
	analytic := NoDragRange(300, 20)
	if !approxEq(analytic, 192.83628290596178) {
		t.Fatalf("解析解 %v", analytic)
	}
	fr := Fly(300, 20, 0, 0, 0.001, 30)
	if !approxEq(fr.S, 183.28594109820955) {
		t.Fatalf("积分射程 %v", fr.S)
	}
	if fr.S >= analytic {
		t.Fatalf("阻力射程 %v 不应超过解析解 %v", fr.S, analytic)
	}
}

/* ---------------------- terrain 契约 ---------------------- */

// flatMap 构造一张平图：elev=100、base=砾石(1)。
func flatMap() *GameMap {
	m := &GameMap{Base: make([]uint8, N), Elev: make([]uint8, N), Surf: make([]uint8, N)}
	for i := range m.Elev {
		m.Elev[i] = 100
		m.Base[i] = 1
	}
	return m
}

func TestStepAllowedContract(t *testing.T) {
	m := flatMap()
	/* 平地随便走 */
	if !StepAllowed(m, "LAV", 100, 100, 106, 100) {
		t.Fatal("平地 LAV 应可走")
	}
	/* 山不可进入 */
	m.Base[Ci(10, 10)] = 3
	if StepAllowed(m, "LAV", 9*Cell+3, 10*Cell+3, 10*Cell+3, 10*Cell+3) {
		t.Fatal("山格必须硬阻挡")
	}
	/* 坡限：普通单位 26*S=104 级、母舰 8*S=32 级、运载舰 10*S=40 级 */
	m.Base[Ci(20, 20)] = 1
	m.Elev[Ci(21, 20)] = 100 + CarrierSlope + 1 // 相邻差 41 级：母舰/运载舰拒绝，普通单位允许
	if StepAllowed(m, "MOTHERSHIP", 20*Cell+3, 20*Cell+3, 21*Cell+3, 20*Cell+3) {
		t.Fatal("母舰坡限 32 级：差 41 级必须拒绝")
	}
	if StepAllowed(m, "CARRIER", 20*Cell+3, 20*Cell+3, 21*Cell+3, 20*Cell+3) {
		t.Fatal("运载舰坡限 40 级：差 41 级必须拒绝")
	}
	if !StepAllowed(m, "LAV", 20*Cell+3, 20*Cell+3, 21*Cell+3, 20*Cell+3) {
		t.Fatal("普通单位坡限 104 级：差 41 级应允许")
	}
	/* 平台放宽到 220 级（uint8 高程上限内构造） */
	m.Base[Ci(30, 30)] = 4
	m.Elev[Ci(30, 30)] = 10
	m.Elev[Ci(31, 30)] = 10 + PlatformSlope // = 230
	if !StepAllowed(m, "CARRIER", 30*Cell+3, 30*Cell+3, 31*Cell+3, 30*Cell+3) {
		t.Fatal("平台格应放宽到 PlatformSlope")
	}
}

func TestLosClearContract(t *testing.T) {
	m := flatMap()
	/* 平地通透；A→B 与 B→A 严格一致 */
	p, q := 100.0, 200.0
	if !LosClear(m, p, p, q, q) || !LosClear(m, q, q, p, p) {
		t.Fatal("平地应通透且对称")
	}
	/* 1 格厚山脊：中央立山，两侧推平（vision_test.js 的构造式山脊） */
	m2 := flatMap()
	rx, ry := 32, 40
	for y := ry - 2; y <= ry+2; y++ {
		for x := rx - 3; x <= rx+3; x++ {
			m2.Base[Ci(x, y)] = 1
			m2.Elev[Ci(x, y)] = 100
		}
	}
	for y := ry - 2; y <= ry+2; y++ {
		m2.Base[Ci(rx, y)] = 3
		m2.Elev[Ci(rx, y)] = 200
	}
	cx, cy := float64(rx*Cell+3), float64(ry*Cell+3)
	lx, lyv := float64((rx-4)*Cell+3), float64(ry*Cell+3)
	if LosClear(m2, lx, lyv, float64((rx+4)*Cell+3), lyv) {
		t.Fatal("1 格厚山脊必须挡住视线")
	}
	_ = cx
	_ = cy
	/* 高程 55m 山体恰好是 MOUNTAIN_H：视线必须被截 */
	if TerrainHeightAt(m2, cx, cy) != MountainH {
		t.Fatalf("山格高度应为 %v，got %v", MountainH, TerrainHeightAt(m2, cx, cy))
	}
}

func TestNormalizeVisionContract(t *testing.T) {
	if NormalizeVision(Vision{}) != VisionOff {
		t.Fatal("零值应归一为关闭")
	}
	if VisionFromBool(true) != VisionAll || VisionFromBool(false) != VisionOff {
		t.Fatal("VisionFromBool 对应 JS 的 true/false/缺省入口")
	}
	if NormalizeVision(Vision{Enabled: true, Fog: true, LOS: true}) != VisionAll {
		t.Fatal("全量全开应保持全开")
	}
	/* JS：{enabled:true, fog:false, los:false} → enabled = (fog||los) = false → 关闭 */
	if NormalizeVision(Vision{Enabled: true}) != VisionOff {
		t.Fatal("enabled 开但 fog/los 全关 → JS 语义归一为关闭")
	}
	/* JS：{enabled:true, fog:true}（los 缺省 true）——Go 全量表达为 fog-only */
	if v := NormalizeVision(Vision{Enabled: true, Fog: true, LOS: false}); v != (Vision{Enabled: true, Fog: true, LOS: false}) {
		t.Fatalf("fog-only 应保持 fog-only: %+v", v)
	}
	if NormalizeVision(Vision{Enabled: false, Fog: true, LOS: true}) != VisionOff {
		t.Fatal("显式关闭即关闭")
	}
}

func TestHeightBonusContract(t *testing.T) {
	m := flatMap()
	m.Elev[Ci(50, 50)] = 100 + 4 // 差 4 级 = 1.2 m ≥ 1.0 m 门槛
	if HeightBonusAt(m, 49*Cell+3, 50*Cell+3, 50*Cell+3, 50*Cell+3) != HeightBonusCells {
		t.Fatal("1.2m 海拔差应给 +2S 视距加成")
	}
	m.Elev[Ci(60, 60)] = 101 // 差 1 级 = 0.3 m < 门槛
	if HeightBonusAt(m, 59*Cell+3, 60*Cell+3, 60*Cell+3, 60*Cell+3) != 0 {
		t.Fatal("0.3m 微起伏不应给加成")
	}
}
