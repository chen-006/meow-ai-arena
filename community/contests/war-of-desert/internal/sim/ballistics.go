// 外弹道（ballistics.js 的完整移植）。
//
// 版本：v1.2（Desert Arena · Go 原生引擎 internal/sim）。
// 功能：弹道积分、仰角求解、地形遮挡判定与伤害/溅射公式。
//
// 运动方程：dv/dt = g_vec − k·|v|·v（二次空气阻力 + 重力标度）。
// 伤害只看落速与弹种；弹道参与判定（山脉 base==3 截断，曲射可越顶）。
//
// 全部常量与 JS 逐一对齐；积分用 RK4 固定步长。浮点口径按 Go float64，
// 与 V8 存在末位 ulp 差异——这正是 golden trace 口径不与 JS 逐位对拍的原因。
package sim

import "math"

// 物理常量（与 ballistics.js 逐一相等）。
const (
	// kg/m³ 海平面 20℃
	Rho = 1.225
	// 1/m 阻力常数 k = ρCdA/2m
	AirK = 4.0e-4
	// m/s² 重力标度（≈30 g，见 ballistics.js 文件头）
	GravityG = 300
	// 1 格 = 10 m（256² 网格，世界 2.56 km）
	MPerCell = 10.0
	// 像素/格，必须与 CELL 一致（靠测试守）
	CellPX = 6
	// 10.0/6 → 浮点常数除法（整数会截断成 1）
	MPerPX = MPerCell / CellPX
	// 每级高程 = 0.30 m
	ElevM = 0.30
	// 山体等效高度（m）
	MountainH = 55
	// 单位中心离地高度（m）
	UnitH = 2.4
	// 命中半径（m）≈0.19 格
	HitRM = 7.5
	// 积分步长（s）
	BallDT = 0.01
	// 单发最长飞行时间（s），超时自毁
	BallMaxT = 8
	// 伤害标定参考落速（文档/测试用）
	VRef = 300
)

// ShellArmor 对不同装甲等级的修正。
//
// wire 形状与 JS 一致：嵌套 armor 对象。
type ShellArmor struct {
	Light float64 `json:"light"`
	Heavy float64 `json:"heavy"`
}

// Shell 一种炮弹的全部系数。
//
// wire 形状对齐 ballistics.js:71-84。
type Shell struct {
	ID        string     `json:"id"`
	Name      string     `json:"name"`
	Mass      float64    `json:"mass"`
	VMul      float64    `json:"vMul"`
	Exp       float64    `json:"exp"`
	D0        float64    `json:"d0"`
	Armor     ShellArmor `json:"armor"`
	Splash    float64    `json:"splash"`
	SplashDmg float64    `json:"splashDmg"`
	// nil = 开局就有
	Unlock *string `json:"unlock"`
}

// Shells 弹种表（ballistics.js:71-93）。
var Shells = map[string]Shell{
	"AP":   {ID: "AP", Name: "穿甲弹", Mass: 7.0, VMul: 1.00, Exp: 2.0, D0: 1.00, Armor: ShellArmor{1.00, 1.15}, Splash: 0, SplashDmg: 0, Unlock: nil},
	"HE":   {ID: "HE", Name: "高爆弹", Mass: 12.0, VMul: 0.92, Exp: 0.6, D0: 0.75, Armor: ShellArmor{1.25, 0.65}, Splash: 30, SplashDmg: 0.55, Unlock: strPtr("F2")},
	"APCR": {ID: "APCR", Name: "硬芯穿甲弹", Mass: 4.2, VMul: 1.28, Exp: 2.0, D0: 0.90, Armor: ShellArmor{0.65, 1.50}, Splash: 0, SplashDmg: 0, Unlock: strPtr("F3")},
}

// strPtr 取字符串指针（弹种解锁科技字段用）。
func strPtr(s string) *string { return &s }

// ShellIDs 弹种枚举顺序（wire 上的 shellIds）。
var ShellIDs = []string{"AP", "HE", "APCR"}

// ArmorOf 按装甲等级取修正系数。
//
// 未知等级按 1（无修正）。
func (sh Shell) ArmorOf(class string) float64 {
	switch class {
	case "light":
		return sh.Armor.Light
	case "heavy":
		return sh.Armor.Heavy
	}
	return 1
}

// ShellState 弹道积分状态。
//
// s = 沿射向水平距离（m），h = 高度（m）。
type ShellState struct {
	S, H, Vx, Vh, T float64
}

// MakeShellState 从 (0, h0) 以 thetaDeg 度仰角、初速 v0 发射。
func MakeShellState(v0, thetaDeg, h0 float64) ShellState {
	th := thetaDeg * math.Pi / 180
	return ShellState{S: 0, H: h0, Vx: v0 * math.Cos(th), Vh: v0 * math.Sin(th)}
}

// ballDeriv 弹道微分方程 dv/dt = g − k|v|v。
//
// ballistics.js:102-108；out 布局 [s', h', vx', vh']。
func ballDeriv(s, h, vx, vh float64, out *[4]float64) {
	v := math.Hypot(vx, vh)
	out[0] = vx
	out[1] = vh
	out[2] = -AirK * v * vx
	out[3] = -GravityG - AirK*v*vh
}

// Advance RK4 推进一步（固定 dt）。
//
// 返回推进后的状态副本（ballistics.js:110-124）。
func Advance(st ShellState, dt float64) ShellState {
	var k1, k2, k3, k4 [4]float64
	ballDeriv(st.S, st.H, st.Vx, st.Vh, &k1)
	ballDeriv(st.S+dt/2*k1[0], st.H+dt/2*k1[1], st.Vx+dt/2*k1[2], st.Vh+dt/2*k1[3], &k2)
	ballDeriv(st.S+dt/2*k2[0], st.H+dt/2*k2[1], st.Vx+dt/2*k2[2], st.Vh+dt/2*k2[3], &k3)
	ballDeriv(st.S+dt*k3[0], st.H+dt*k3[1], st.Vx+dt*k3[2], st.Vh+dt*k3[3], &k4)
	return ShellState{
		S:  st.S + dt/6*(k1[0]+2*k2[0]+2*k3[0]+k4[0]),
		H:  st.H + dt/6*(k1[1]+2*k2[1]+2*k3[1]+k4[1]),
		Vx: st.Vx + dt/6*(k1[2]+2*k2[2]+2*k3[2]+k4[2]),
		Vh: st.Vh + dt/6*(k1[3]+2*k2[3]+2*k3[3]+k4[3]),
		T:  st.T + dt,
	}
}

// ShellSpeed 弹体速率（m/s）。
func ShellSpeed(st ShellState) float64 {
	return math.Hypot(st.Vx, st.Vh)
}

// NoDragAngle 无阻力解析解，只给求解器做初值估算。
//
// ballistics.js:128-136。
func NoDragAngle(d, dh, v0 float64) (low, high float64, ok bool) {
	v2 := v0 * v0
	disc := v2*v2 - GravityG*(GravityG*d*d+2*dh*v2)
	if disc < 0 {
		return 0, 0, false
	}
	root := math.Sqrt(disc)
	low = math.Atan((v2-root)/(GravityG*d)) * 180 / math.Pi
	high = math.Atan((v2+root)/(GravityG*d)) * 180 / math.Pi
	return low, high, true
}

// PathPoint 弹道采样点。
//
// arc 距离（m）与高度（m）。
type PathPoint struct {
	S float64 `json:"s"`
	H float64 `json:"h"`
}

// FlyResult 一次纯弹道积分的结果。
type FlyResult struct {
	Theta  float64     `json:"theta"`
	Points []PathPoint `json:"points"`
	T      float64     `json:"t"`
	// 降到 targetH 时的水平距离（ok=false 时为 0）
	S       float64 `json:"s"`
	H       float64 `json:"h"`
	VImpact float64 `json:"vImpact"`
	Apex    float64 `json:"apex"`
	OK      bool    `json:"ok"`
}

// Fly 从 (0, h0) 以 theta 发射，积分到「从上方降到 targetH」或落地或超时。
//
// 纯弹道不掺地形（遮挡由 BlockedByTerrain 单独判），ballistics.js:142-176。
func Fly(v0, thetaDeg, h0, targetH float64, dt, maxT float64) FlyResult {
	if dt <= 0 {
		dt = BallDT
	}
	if maxT <= 0 {
		maxT = BallMaxT
	}
	st := MakeShellState(v0, thetaDeg, h0)
	apex := st.H
	nextSample := 4.0
	points := []PathPoint{{S: 0, H: h0}}
	// 只有「从上方降下来穿过 targetH」才算到达：打上坡时炮口本来就在
	// 目标高度以下，不区分的话任何仰角都会立刻返回 s≈0，
	// 求解器永远找不到解。
	above := h0 > targetH
	for st.T < maxT {
		nxt := Advance(st, dt)
		if nxt.H > targetH {
			above = true
		}
		if above && nxt.H <= targetH {
			lo, hi := st, nxt
			for i := 0; i < 14; i++ {
				mid := Advance(lo, (hi.T-lo.T)/2)
				if mid.H <= targetH {
					hi = mid
				} else {
					lo = mid
				}
			}
			v := ShellSpeed(hi)
			points = append(points, PathPoint{S: hi.S, H: hi.H})
			return FlyResult{Theta: thetaDeg, Points: points, T: hi.T, S: hi.S, H: hi.H, VImpact: v, Apex: apex, OK: true}
		}
		st = nxt
		if st.H > apex {
			apex = st.H
		}
		// 采样按「弧长每 4 m 一个点」：按步数采样在高速弹上
		// 会整段跳过几十米宽的山脉。
		if st.S >= nextSample {
			points = append(points, PathPoint{S: st.S, H: st.H})
			nextSample = st.S + 4
		}
		if st.H <= 0 && st.T > 0.05 {
			// 打到地面以下：够不到那个高度
			break
		}
	}
	return FlyResult{Theta: thetaDeg, Points: points, T: st.T, S: 0, H: st.H, VImpact: ShellSpeed(st), Apex: apex, OK: false}
}

// SolveParams 求解参数（ballistics.js:218-230 的 params）。
type SolveParams struct {
	V0, H0 float64
	// 目标水平距离与相对炮口的高度差
	D, DH float64
	// 仰角下限（deg）
	ElevMin float64
	// 仰角上限（deg）
	ElevMax float64
	// 0 = BallDT
	Dt float64
	// 0 = 16
	Scan int
	// 该 arc 距离处的阻挡高度（默认 −∞）
	GroundAt func(arcM float64) float64
}

// Solution 一个收敛的仰角解。
type Solution struct {
	FlyResult
	Theta float64 `json:"theta"`
	S     float64 `json:"s"`
}

// Solutions 解「打到水平距离 d、高度 targetH」的所有仰角（低伸解在前）。
//
// 阻力让高抛/低伸解不对称，
// 用「扫描找跨零点 + 二分」（ballistics.js:183-214）。
func Solutions(p SolveParams) []Solution {
	dt := p.Dt
	if dt <= 0 {
		dt = BallDT
	}
	n := p.Scan
	if n <= 0 {
		n = 16
	}
	targetH := p.H0 + p.DH
	rangeAt := func(th float64) float64 { return Fly(p.V0, th, p.H0, targetH, dt, 0).S }
	var out []Solution
	prevTh, prevR := 0.0, 0.0
	for i := 0; i <= n; i++ {
		th := p.ElevMin + (p.ElevMax-p.ElevMin)*float64(i)/float64(n)
		r := rangeAt(th)
		if i > 0 {
			f0, f1 := prevR-p.D, r-p.D
			if (f0 <= 0 && f1 >= 0) || (f0 >= 0 && f1 <= 0) {
				lo, hi, rl := prevTh, th, prevR
				for k := 0; k < 10; k++ {
					mid := (lo + hi) / 2
					rm := rangeAt(mid)
					if (rl-p.D)*(rm-p.D) <= 0 {
						hi = mid
					} else {
						lo = mid
						rl = rm
					}
				}
				th2 := (lo + hi) / 2
				r2 := rangeAt(th2)
				if math.Abs(r2-p.D) <= math.Max(4, p.D*0.02) {
					fr := Fly(p.V0, th2, p.H0, targetH, dt, 0)
					out = append(out, Solution{FlyResult: fr, Theta: th2, S: r2})
				}
			}
		}
		prevTh, prevR = th, r
	}
	// 排序：仰角小的先（飞得短、掉速少、到得快）
	for i := 1; i < len(out); i++ {
		for j := i; j > 0 && out[j].Theta < out[j-1].Theta; j-- {
			out[j], out[j-1] = out[j-1], out[j]
		}
	}
	// 相邻 0.15° 内去重
	uniq := out[:0]
	for _, s := range out {
		if len(uniq) == 0 || math.Abs(s.Theta-uniq[len(uniq)-1].Theta) > 0.15 {
			uniq = append(uniq, s)
		}
	}
	return uniq
}

// SolveResult 挑解结果。
//
// OK=false 时给 reason（out-of-envelope / blocked）。
type SolveResult struct {
	OK      bool        `json:"ok"`
	Reason  string      `json:"reason,omitempty"`
	Theta   float64     `json:"theta,omitempty"`
	T       float64     `json:"t,omitempty"`
	VImpact float64     `json:"vImpact,omitempty"`
	Apex    float64     `json:"apex,omitempty"`
	S       float64     `json:"s,omitempty"`
	Path    []PathPoint `json:"path,omitempty"`
}

// Solve 在解集里挑一个「打得到且不被山挡住」的弹道。
//
// ballistics.js:218-230。
func Solve(p SolveParams) SolveResult {
	sols := Solutions(p)
	if len(sols) == 0 {
		return SolveResult{OK: false, Reason: "out-of-envelope"}
	}
	gAt := p.GroundAt
	if gAt == nil {
		gAt = func(float64) float64 { return math.Inf(-1) }
	}
	for _, s := range sols {
		if !BlockedByTerrain(s.Points, gAt) {
			return SolveResult{OK: true, Theta: s.Theta, T: s.T, VImpact: s.VImpact, Apex: s.Apex, S: s.S, Path: s.Points}
		}
	}
	return SolveResult{OK: false, Reason: "blocked", Theta: sols[0].Theta}
}

// BlockedByTerrain 弹道是否被山挡住。
//
// 只有调用方 groundAt 判定的实体（山脉）会截断弹道，
// 其余地形可掠过（ballistics.js:236-239）。
func BlockedByTerrain(points []PathPoint, groundAt func(float64) float64) bool {
	for _, p := range points {
		if p.H < groundAt(p.S) {
			return true
		}
	}
	return false
}

// DamageOf 落速伤害公式。
//
// 伤害 = dmg · d0 · max(0.02, vImpact/baseV0)^exp · armor
// （ballistics.js:246-251）。基础科技、零距离时恰好等于 TYPES[k].dmg。
func DamageOf(shellID string, dmg, baseV0, vImpact float64, armorClass string) float64 {
	sh, ok := Shells[shellID]
	if !ok {
		sh = Shells["AP"]
	}
	ratio := math.Max(0.02, vImpact/math.Max(1, baseV0))
	return dmg * sh.D0 * math.Pow(ratio, sh.Exp) * sh.ArmorOf(armorClass)
}

// SplashFactor 溅射衰减。
//
// 中心全伤，边缘按 1−r/R 线性衰减（ballistics.js:253-256）。
func SplashFactor(dist, radius float64) float64 {
	if radius <= 0 || dist >= radius {
		return 0
	}
	return 1 - dist/radius
}

// NoDragRange 无阻力射程解析解（测试用：校验积分器）。
func NoDragRange(v0, thetaDeg float64) float64 {
	return v0 * v0 * math.Sin(2*thetaDeg*math.Pi/180) / GravityG
}
