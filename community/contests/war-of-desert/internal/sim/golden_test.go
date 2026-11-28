package sim

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
)

// golden trace 回归门禁：sim 包任何改动都必须让全部冻结 trace 重放全对
// （逐帧哈希 + 终局 winner/level 一致）。重生成 trace 用 go run ./cmd/tracegen。
func TestGoldenTraces(t *testing.T) {
	dir := filepath.Join("..", "..", "engine", "golden")
	files, err := os.ReadDir(dir)
	if err != nil {
		t.Skipf("engine/golden 不存在（tracegen 未跑），跳过: %v", err)
	}
	verified := 0
	for _, f := range files {
		if filepath.Ext(f.Name()) != ".json" || f.Name() == "MANIFEST.json" {
			continue
		}
		data, err := os.ReadFile(filepath.Join(dir, f.Name()))
		if err != nil {
			t.Fatalf("读 %s: %v", f.Name(), err)
		}
		var tr struct {
			Name        string          `json:"name"`
			Mode        string          `json:"mode"`
			Seed        int             `json:"seed"`
			Generator   string          `json:"generator"`
			TickLimit   int             `json:"tickLimit"`
			TicksRun    int             `json:"ticksRun"`
			MapAttempt  int             `json:"mapAttempt"`
			OrderLog    []OrderLogEntry `json:"orderLog"`
			FrameHashes []string        `json:"frameHashes"`
			Result      json.RawMessage `json:"result"`
		}
		if err := json.Unmarshal(data, &tr); err != nil {
			t.Fatalf("%s 解析失败: %v", f.Name(), err)
		}
		attempt := tr.MapAttempt
		rec := &VerifyRec{
			Seed: tr.Seed, TickLimit: tr.TickLimit, Generator: tr.Generator,
			MapAttempt: &attempt, OrderLog: tr.OrderLog, FrameHashes: tr.FrameHashes,
		}
		var rep VerifyReply
		hasResult := len(tr.Result) > 0 && string(tr.Result) != "null"
		if tr.Mode == "siege" {
			if hasResult {
				var sr SiegeResult
				if err := json.Unmarshal(tr.Result, &sr); err != nil {
					t.Fatalf("%s result 解析失败: %v", f.Name(), err)
				}
				rec.SiegeResult = &sr
			}
			rep = VerifySiegeDeterminism(rec)
		} else {
			if hasResult {
				var r Result
				if err := json.Unmarshal(tr.Result, &r); err != nil {
					t.Fatalf("%s result 解析失败: %v", f.Name(), err)
				}
				rec.Result = &r
			}
			rep = VerifyDeterminism(rec)
		}
		if !rep.OK {
			t.Errorf("GOLDEN %s 重放失败: mismatch=%+v resultMismatch=%+v", tr.Name, rep.Mismatch, rep.ResultMismatch)
			continue
		}
		if rep.Turns != tr.TicksRun || rep.Frames != len(tr.FrameHashes) {
			t.Errorf("GOLDEN %s 对局长度漂移: turns %d/%d frames %d/%d",
				tr.Name, rep.Turns, tr.TicksRun, rep.Frames, len(tr.FrameHashes))
			continue
		}
		verified++
	}
	if verified == 0 {
		t.Skip("没有可验证的 golden trace")
	}
	t.Logf("GOLDEN_OK %d 条 trace 全部重放一致", verified)
}
