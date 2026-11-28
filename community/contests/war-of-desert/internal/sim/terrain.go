// 网格与地形（mapgen.js:71-82 + rules.js 的地形判定）。
//
// 版本：v1.2（Desert Arena · Go 原生引擎 internal/sim）。
// 功能：网格常数、地表/坡度规则、地图容器、移动判定与视野/LOS。
// GRID=256、CELL=6：世界 1536² px = 2.56 km，1 格 10 m。
// S = GRID/64 是「相对 64 格标定的密度倍数」：
// 一切以格为单位的量乘 S 保持物理尺度。
package sim

import "math"

// 网格常数。
const (
	Grid = 256
	Cell = 6
	// 1536
	World = Grid * Cell
	// 65536
	N = Grid * Grid
	// 4
	Scale = Grid / 64
)

// Surf 地表类型（渲染/分析用；规则只读 base）。
const (
	SurfSand  = 0
	SurfReg   = 1
	SurfDune  = 2
	SurfRock  = 3
	SurfWadi  = 4
	SurfPlaya = 5
	SurfFan   = 6
	SurfPad   = 7
)

// SurfNames 地表名（wire/渲染）。
var SurfNames = []string{"沙席", "砾漠", "沙丘", "岩壁", "干河谷", "盐沼", "冲积扇", "平台"}

// SurfBase surf → base。
//
// 0 沙 0.9×、1 砾 1.1×、2 丘 0.7×、3 山不可通行、4 平台 1.0×。
var SurfBase = [8]uint8{0, 1, 2, 3, 1, 1, 1, 4}

// BaseSurface 速度系数表（applyMove 用）：[0.9, 1.1, 0.7, 0, 1]。
var BaseSurface = [5]float64{0.9, 1.1, 0.7, 0, 1}

// 坡度上限（rules.js:63-68）：按格差 × Scale。
const (
	UnitSlope    = 26 * Scale
	CarrierSlope = 10 * Scale
	// DoK F-19：母舰是最大地面单位，专用坡限。
	// v1.4：8*Scale（=32）在实际地形上完全不起作用——实测邻格高差 p90 只有 2~3、
	// 全图最大 23，32 等于无限通行。改成绝对 4 个高程单位，母舰才真正被陡坎挡住。
	MothershipSlope = 4
	PlatformSlope   = 55 * Scale
)

// Ci 格坐标 → index。
func Ci(gx, gy int) int { return gy*Grid + gx }

// GxOf 世界像素坐标 → 格坐标（x 轴）。
//
// rules.js:227-228 口径：floor(x/CELL) 并 clamp 到 [0, GRID-1]。
// 注意与 mapgen.js 的线性 index 助手同名不同义——
// mapgen 那两个吃的是 0..N-1 的线性 index，这里吃的是世界像素。
func GxOf(x float64) int {
	g := int(math.Floor(x / Cell))
	if g < 0 {
		return 0
	}
	if g > Grid-1 {
		return Grid - 1
	}
	return g
}

// GyOf 世界像素坐标 → 格坐标（y 轴）。
//
// 口径与 GxOf 相同。
func GyOf(y float64) int {
	g := int(math.Floor(y / Cell))
	if g < 0 {
		return 0
	}
	if g > Grid-1 {
		return Grid - 1
	}
	return g
}

// Rev 180° 点旋转（mapgen.js:109）。
func Rev(i int) int { return N - 1 - i }

// GameMap 一张生成完毕的地图。
//
// 数组长度恒为 N；Elev/Base/Surf 是量化后的 uint8。
type GameMap struct {
	Seed      int    `json:"seed"`
	Generator string `json:"generator"`
	Attempt   int    `json:"mapAttempt"`

	// 0 沙 1 砾 2 丘 3 山 4 平台
	Base []uint8 `json:"-"`
	Surf []uint8 `json:"-"`
	Elev []uint8 `json:"-"`

	// 双方出生格（对称）
	SpawnCells [][2]int `json:"-"`
	// VP 点格
	VPCells [][2]int `json:"-"`
	// 资源点格（第 3 个是 RU，kind 由规则层定）
	ResCells [][2]int `json:"-"`

	// 8 邻域最大高差
	Slope     []uint8 `json:"-"`
	WallRatio float64 `json:"-"`
	// 连通但占比未落带
	OutOfBand bool `json:"-"`
	// 开了兜底通道
	Fallback       bool    `json:"-"`
	Sink           MapSink `json:"-"`
	River          []uint8 `json:"-"`
	SourcesReached []bool  `json:"-"`
	DuneCut        float64 `json:"-"`

	// 连续地貌层（不进 wire；搜索/测试/渲染用）。
	// float32 口径与 JS 一致。
	AccLog     []float32 `json:"-"`
	DuneAmp    []float32 `json:"-"`
	DuneRelief []float32 `json:"-"`
	Playa      []float32 `json:"-"`
	Rock       []float32 `json:"-"`
	Fan        []float32 `json:"-"`
	Flow       []float32 `json:"-"`

	// MC 生成器私有层
	Extra *MapExtra `json:"-"`
}

// TerrainHeightAt 某一格的地形高度（m）。
//
// 山按 MountainH 实体处理，与弹道遮挡同一口径（rules.js:308-311）。
func TerrainHeightAt(m *GameMap, x, y float64) float64 {
	idx := Ci(GxOf(x), GyOf(y))
	if m.Base[idx] == 3 {
		return MountainH
	}
	return float64(m.Elev[idx]) * ElevM
}

// StepAllowed 相邻格移动是否允许。
//
// 山硬阻挡 + 按单位类别的坡限（平台格放宽到 PlatformSlope）。
// fx/fy/tx/ty 是世界像素坐标（rules.js:525-533）；
// kind 仅需区分 MOTHERSHIP / CARRIER / 其它，传空串按普通单位。
func StepAllowed(m *GameMap, kind string, fx, fy, tx, ty float64) bool {
	gi := Ci(GxOf(tx), GyOf(ty))
	if m.Base[gi] == 3 {
		return false
	}
	hi := Ci(GxOf(fx), GyOf(fy))
	lim := float64(UnitSlope)
	switch kind {
	case "MOTHERSHIP":
		lim = float64(MothershipSlope)
	case "CARRIER":
		lim = float64(CarrierSlope)
	}
	if m.Base[gi] == 4 || m.Base[hi] == 4 {
		lim = math.Max(lim, float64(PlatformSlope))
	}
	return math.Abs(float64(m.Elev[gi])-float64(m.Elev[hi])) <= lim
}

// ---------------------- 视野 / LOS（rules.js:278-431） ----------------------

// Vision 统一视野开关（rules.js:292-304）。
//
// 关闭时行为与「没有视野功能」等价。
type Vision struct {
	Enabled bool `json:"enabled"`
	Fog     bool `json:"fog"`
	LOS     bool `json:"los"`
}

// VisionAll / VisionOff 两个极端态。
var (
	VisionAll = Vision{Enabled: true, Fog: true, LOS: true}
	VisionOff = Vision{}
)

// NormalizeVision 归一化开关。
//
// 显式 Enabled=false 或两项全关 → 视为关闭；
// 所有入口（Game/header/rulesHash/visionGrid）都过这一层。
// 注意：JS 的「字段缺省 = true」在 Go bool 上不可表达——
// Go 调用方要么传全量结构，要么用 VisionFromBool；
// 本函数语义与 JS 的显式对象输入逐条一致。
func NormalizeVision(v Vision) Vision {
	if !v.Enabled {
		return VisionOff
	}
	if !v.Fog && !v.LOS {
		return VisionOff
	}
	return Vision{Enabled: true, Fog: v.Fog, LOS: v.LOS}
}

// VisionFromBool 对应 JS 传 true/false/缺省（undefined）的入口。
//
// true → 全开，false → 全关。
func VisionFromBool(on bool) Vision {
	if on {
		return VisionAll
	}
	return VisionOff
}

// 视野常数（rules.js:278-291）。
const (
	// 视线起点离地高度（m），与炮口同源
	EyeH = UnitH
	// 视线容差（m）
	LosTol = 0.5
	// 采样步长（格）：小于 1 格，1 格厚山脊不会被跳过
	LosStep = 0.5
	// 高度加成视距（格）
	HeightBonusCells = 2 * Scale
	// 高度差门槛（m）
	HeightDiffMin = 1.0
	// 只开 LOS 时的视距硬上限
	LosRangeCells = 32 * Scale
)

// LosClear 视线是否通畅。
//
// 沿两点连线按 LosStep 采样，比较地形高度与视线插值高度。
// 三个细节：① 端点按字典序规范化保证严格对称；② 不取端点 t=1；
// ③ 跳过起点格/终点格内的采样点（rules.js:328-349）。
func LosClear(m *GameMap, ax, ay, bx, by float64) bool {
	if ax > bx || (ax == bx && ay > by) {
		ax, bx = bx, ax
		ay, by = by, ay
	}
	dx, dy := bx-ax, by-ay
	dpx := math.Hypot(dx, dy)
	if dpx < 1e-6 {
		return true
	}
	aIdx := Ci(GxOf(ax), GyOf(ay))
	bIdx := Ci(GxOf(bx), GyOf(by))
	h0 := TerrainHeightAt(m, ax, ay) + EyeH
	h1 := TerrainHeightAt(m, bx, by) + EyeH
	steps := int(math.Max(1, math.Ceil((dpx/Cell)/LosStep)))
	for i := 1; i < steps; i++ {
		t := float64(i) / float64(steps)
		x, y := ax+dx*t, ay+dy*t
		idx := Ci(GxOf(x), GyOf(y))
		if idx == aIdx || idx == bIdx {
			continue
		}
		line := h0 + (h1-h0)*t
		if TerrainHeightAt(m, x, y) > line+LosTol {
			return false
		}
	}
	return true
}

// HeightDiffAt 两端海拔差（m）（rules.js:352-354）。
func HeightDiffAt(m *GameMap, ax, ay, bx, by float64) float64 {
	return TerrainHeightAt(m, bx, by) - TerrainHeightAt(m, ax, ay)
}

// HeightBonusAt 存在高度差则视距加成。
//
// ≥ HeightDiffMin 加 HeightBonusCells，否则 0；
// 只加「看多远」，不加「看不看得穿」（rules.js:358-360）。
func HeightBonusAt(m *GameMap, ax, ay, bx, by float64) int {
	if math.Abs(HeightDiffAt(m, ax, ay, bx, by)) >= HeightDiffMin {
		return HeightBonusCells
	}
	return 0
}
