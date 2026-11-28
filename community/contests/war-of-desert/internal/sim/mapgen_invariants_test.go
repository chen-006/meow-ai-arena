package sim

import (
	"testing"
)

// mapgen 不变量测试（移植 mapgen_test.js / mapgen_mc_test.js 的核心口径）。
// 同 seed 逐位复现是硬要求；对称性/连通性/占比带是公平性门禁。

func TestMapRegenIdentical(t *testing.T) {
	for _, gen := range []string{"geomorphic", "minecraft"} {
		a := mustGen(t, gen, 777)
		b := mustGen(t, gen, 777)
		if string(layersKey(a)) != string(layersKey(b)) {
			t.Fatalf("[%s] 同 seed 两次生成不一致", gen)
		}
	}
}

func TestMapSymmetry180(t *testing.T) {
	for _, gen := range []string{"geomorphic", "minecraft"} {
		for _, seed := range []int{1, 5, 42} {
			m := mustGen(t, gen, seed)
			for i := 0; i < N/2; i++ {
				j := Rev(i)
				if m.Elev[i] != m.Elev[j] || m.Base[i] != m.Base[j] || m.Surf[i] != m.Surf[j] {
					t.Fatalf("[%s seed=%d] 180° 对称破坏 @ %d (elev %d/%d base %d/%d)",
						gen, seed, i, m.Elev[i], m.Elev[j], m.Base[i], m.Base[j])
				}
			}
		}
	}
}

func TestMapWallBandAndConnectivity(t *testing.T) {
	for _, gen := range []string{"geomorphic", "minecraft"} {
		for _, seed := range []int{1, 2, 3, 42, 20261109} {
			m := mustGen(t, gen, seed)
			if m.WallRatio < 0.05 || m.WallRatio > 0.15 {
				t.Errorf("[%s seed=%d] 岩壁占比 %.4f 超带 [0.05,0.15]", gen, seed, m.WallRatio)
			}
			if !Connected(m, nil) {
				t.Fatalf("[%s seed=%d] 出生点+VP+资源点不连通", gen, seed)
			}
			// 点位公平性：VP/资源点成镜像对
			if len(m.VPCells) != 4 || len(m.ResCells) != 4 {
				t.Fatalf("[%s seed=%d] VP=%d res=%d 应各 4 个", gen, seed, len(m.VPCells), len(m.ResCells))
			}
			for _, c := range m.VPCells {
				if !containsCell(m.VPCells, [2]int{Grid - 1 - c[0], Grid - 1 - c[1]}) {
					t.Fatalf("[%s seed=%d] VP %v 镜像不在集合", gen, seed, c)
				}
			}
			for _, c := range m.ResCells {
				if !containsCell(m.ResCells, [2]int{Grid - 1 - c[0], Grid - 1 - c[1]}) {
					t.Fatalf("[%s seed=%d] 资源点 %v 镜像不在集合", gen, seed, c)
				}
			}
		}
	}
}

func TestMapLayersRoundtrip(t *testing.T) {
	m := mustGen(t, "minecraft", 20261109)
	enc := EncodeMapLayers(m)
	dec, err := DecodeMapLayers(&enc)
	if err != nil {
		t.Fatalf("解码失败: %v", err)
	}
	for i := 0; i < N; i++ {
		if dec.Base[i] != m.Base[i] || dec.Surf[i] != m.Surf[i] || dec.Elev[i] != m.Elev[i] {
			t.Fatalf("wire 三层解码还原不一致 @ %d", i)
		}
	}
	// surfString 往返
	ss := SurfString(m.Surf)
	back := SurfFromString(ss)
	for i := 0; i < N; i++ {
		if back[i] != m.Surf[i] {
			t.Fatalf("surfString 往返不一致 @ %d", i)
		}
	}
	// 协议体积锚点：256² hello 的 elev 层 ≈ 87KB、base/surf 各 ~10KB
	if len(enc.Elev) < 86000 || len(enc.Elev) > 90000 {
		t.Errorf("elev b64 体积 %d 超出预期范围", len(enc.Elev))
	}
}

func mustGen(t *testing.T, gen string, seed int) *GameMap {
	t.Helper()
	var m *GameMap
	switch gen {
	case "geomorphic":
		m = GenerateMapGeomorphic(seed, nil)
	case "minecraft":
		m = GenerateMapMC(seed, nil)
	default:
		t.Fatalf("未知生成器 %s", gen)
	}
	return m
}

func layersKey(m *GameMap) []byte {
	l := EncodeMapLayers(m)
	return []byte(l.Base + l.Surf + l.Elev)
}

func containsCell(cells [][2]int, c [2]int) bool {
	for _, x := range cells {
		if x == c {
			return true
		}
	}
	return false
}
