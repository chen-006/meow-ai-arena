// Package sim 是 Desert Arena 的规则核心：随机源、哈希、外弹道、地形与
// （Phase 2/3 起补全的）地图生成器与规则引擎。引擎与选手包共用这一份实现，
// 公式禁止复制（Plan_Beta §4.1）。
//
// 版本：v1.2（队列跳过 + RU 基础收入，详见 engine/README.md 口径备忘）。
// 移植基线：Dev/MVP 工作树（b9643b0 + v0.11 收尾），见 engine/parity/BASELINE.md。
// 对拍口径：Plan_Beta §5.2 —— 自确定性 + golden trace + 绝对锚点，不与 JS 逐位对拍。
package sim

import "math"

// mulberry32（mapgen.js:113-122）：32 位环绕整数运算，输出 [0,1) 浮点。
// JS 的 Math.imul 是 int32 溢出乘法，Go 用 int32 乘法回卷等价复刻。
type RNG struct {
	a uint32
}

// NewRng 构造一个 mulberry32 生成器。seed 按无符号 32 位截断（JS `seed >>> 0`）。
func NewRng(seed int) *RNG {
	return &RNG{a: uint32(seed)}
}

// Next 返回下一个 [0,1) 随机数。逐步复刻 JS：
//
//	t = imul(t ^ (t>>>15), t|1);
//	t ^= t + imul(t ^ (t>>>7), t|61);   // 即 t = t ^ (t + imul(...))，uint32 自然环绕
//	return (t ^ (t>>>14)) / 2^32
func (r *RNG) Next() float64 {
	r.a += 0x6D2B79F5
	t := r.a
	t = imul32(t^(t>>15), t|1)
	t ^= t + imul32(t^(t>>7), t|61)
	return float64(t^(t>>14)) / 4294967296
}

// imul32 复刻 JS Math.imul：按 int32 溢出乘法回卷。
func imul32(x, y uint32) uint32 {
	return uint32(int32(x) * int32(y))
}

// Hash2 复刻 mapgen.js:123-128 的整数格点哈希，输出 [0,1)。
func Hash2(seed int32, x, y int) float64 {
	h := uint32(seed) ^ imul32(uint32(x), 374761393) ^ imul32(uint32(y), 668265263)
	h = imul32(h^(h>>16), 2246822507)
	h = imul32(h^(h>>13), 3266489909)
	return float64(h^(h>>16)) / 4294967296
}

// SmoothT 是 smoothstep 的一维核（mapgen.js:129）。
func SmoothT(t float64) float64 {
	return t * t * (3 - 2*t)
}

// ValueNoise 双线性值噪声（mapgen.js:130-136）。
func ValueNoise(seed int32, x, y float64) float64 {
	xi, yi := math.Floor(x), math.Floor(y)
	tx := SmoothT(x - xi)
	ty := SmoothT(y - yi)
	a := Hash2(seed, int(xi), int(yi))
	b := Hash2(seed, int(xi)+1, int(yi))
	c := Hash2(seed, int(xi), int(yi)+1)
	d := Hash2(seed, int(xi)+1, int(yi)+1)
	return (a+(b-a)*tx)*(1-ty) + (c+(d-c)*tx)*ty
}

// FBM 六倍频分形噪声（mapgen.js:137）。
func FBM(seed int32, x, y float64) float64 {
	return FBMN(seed, x, y, 6)
}

// FBMN 可指定倍频数的分形噪声（mapgen.js:141-145）。
func FBMN(seed int32, x, y float64, oct int) float64 {
	v, amp, f := 0.0, 0.5, 1.0
	if oct <= 0 {
		oct = 6
	}
	for o := 0; o < oct; o++ {
		v += amp * ValueNoise(seed, x*f, y*f)
		f *= 2
		amp *= 0.5
	}
	return v
}

// SeedSalt 复刻 mapgen.js:150-154：(seed, attempt) → 32 位盐值。
// 混入 attempt 避免不同 seed 的第 n 次重试撞图。
func SeedSalt(seed int, attempt int) int32 {
	h := uint32(seed) ^ imul32(uint32(attempt+1), 0x9E3779B1)
	h = imul32(h^(h>>15), 0x85EBCA6B)
	return int32(h ^ (h >> 13))
}

// Clamp 把 v 夹回 [lo, hi]（mapgen.js:98-104 同款）。
func Clamp(v, lo, hi float64) float64 {
	if v < lo {
		return lo
	}
	if v > hi {
		return hi
	}
	return v
}

// Lerp 线性插值（mapgen.js:99 同款）。
func Lerp(a, b, t float64) float64 {
	return a + (b-a)*t
}

// Smoothstep 三段式平滑（mapgen.js:100-104）。
//
// a==b 时退化为阶跃。
func Smoothstep(a, b, x float64) float64 {
	if a == b {
		if x < a {
			return 0
		}
		return 1
	}
	t := Clamp((x-a)/(b-a), 0, 1)
	return t * t * (3 - 2*t)
}
