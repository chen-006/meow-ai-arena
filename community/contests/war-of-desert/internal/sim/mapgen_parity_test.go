package sim

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

// 地图生成测试：JS 逐格比对（诊断锚）+ 移植不变量。
// 基线文件由 engine/parity/dump_maps.js 生成；若 JS/Go 存在超越函数 ulp
// 造成的少量格子漂移，逐格比对按容差报告，不变量测试才是硬门禁。

type jsMapDump struct {
	Generator string    `json:"generator"`
	Seed      int       `json:"seed"`
	Attempt   int       `json:"attempt"`
	OutOfBand bool      `json:"outOfBand"`
	Fallback  bool      `json:"fallback"`
	WallRatio float64   `json:"wallRatio"`
	Layers    MapLayers `json:"layers"`
	Spawns    [][2]int  `json:"spawns"`
	VPCells   [][2]int  `json:"vpCells"`
	ResCells  [][2]int  `json:"resCells"`
}

func loadJSMaps(t *testing.T) []jsMapDump {
	t.Helper()
	data, err := os.ReadFile(filepath.Join("..", "..", "engine", "parity", "baselines", "maps-js.json"))
	if err != nil {
		t.Skipf("JS 地图基线不存在（dump_maps.js 未跑），跳过逐格比对: %v", err)
	}
	var dump struct {
		Maps []jsMapDump `json:"maps"`
	}
	if err := json.Unmarshal(data, &dump); err != nil {
		t.Fatalf("基线 JSON 解析失败: %v", err)
	}
	return dump.Maps
}

func TestMapParityAgainstJS(t *testing.T) {
	for _, jm := range loadJSMaps(t) {
		var m *GameMap
		switch jm.Generator {
		case "geomorphic":
			m = GenerateMapGeomorphic(jm.Seed, nil)
		case "minecraft":
			m = GenerateMapMC(jm.Seed, nil)
		}
		if m.Attempt != jm.Attempt {
			t.Errorf("[%s/%d] mapAttempt got %d want %d", jm.Generator, jm.Seed, m.Attempt, jm.Attempt)
		}
		// wire 三层逐格比对
		gl := EncodeMapLayers(m)
		if gl.Base != jm.Layers.Base {
			t.Errorf("[%s/%d] base 层不一致（len %d vs %d）", jm.Generator, jm.Seed, len(gl.Base), len(jm.Layers.Base))
		}
		if gl.Surf != jm.Layers.Surf {
			t.Errorf("[%s/%d] surf 层不一致", jm.Generator, jm.Seed)
		}
		if gl.Elev != jm.Layers.Elev {
			t.Errorf("[%s/%d] elev 层不一致", jm.Generator, jm.Seed)
		}
	}
}
