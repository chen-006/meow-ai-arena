// 文件：internal/sim/resources.go
// 版本：v1.3
// 功能：资源点类型分配。资源格由 mapgen 成对生成（互为 180° 镜像），
//       类型必须按「对」分配，否则同一张图上两侧资源结构不同，座位本身就是优势。
//
// 历史缺陷（v1.2 及以前）：按排序后的下标 i%3 赋 RU，而 ResCells 在返回前
// 已按 (y,x) 排序，配对关系被打散——实测每张图只剩 1 个 RU 矿且只归属一侧，
// 同 Bot 镜像自对弈 VP 差可达 895（927:32）。
package sim

import "math"

// ResourceKinds 按镜像配对返回每个资源格的类型（"cu" / "ru"）。
// 规则：配对以规范格（排序靠前者）为准；地图上有成对资源时固定一对为 RU，
// 选离地图中心最近的一对（中路争夺点）；同对两格必然同类。
func ResourceKinds(cells [][2]int) []string {
	kinds := make([]string, len(cells))
	for i := range kinds {
		kinds[i] = "cu"
	}
	if len(cells) == 0 {
		return kinds
	}
	idx := make(map[[2]int]int, len(cells))
	for i, c := range cells {
		idx[c] = i
	}
	type pair struct{ a, b int }
	var pairs []pair
	seen := make([]bool, len(cells))
	for i, c := range cells {
		if seen[i] {
			continue
		}
		m := [2]int{Grid - 1 - c[0], Grid - 1 - c[1]}
		j, ok := idx[m]
		if !ok || seen[j] {
			// 落单格（mapgen 不应产生）：自成一对，按 CU 处理
			seen[i] = true
			pairs = append(pairs, pair{i, -1})
			continue
		}
		seen[i], seen[j] = true, true
		pairs = append(pairs, pair{i, j})
	}
	center := float64(Grid-1) / 2
	ruPair, best := -1, math.Inf(1)
	for k, p := range pairs {
		if p.b < 0 {
			continue
		}
		d := math.Hypot(float64(cells[p.a][0])-center, float64(cells[p.a][1])-center)
		if d < best-1e-9 {
			best, ruPair = d, k
		}
	}
	if ruPair >= 0 {
		p := pairs[ruPair]
		kinds[p.a] = "ru"
		kinds[p.b] = "ru"
	}
	return kinds
}
