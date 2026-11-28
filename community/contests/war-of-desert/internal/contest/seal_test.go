package contest

import (
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func writeFile(t *testing.T, p string, body []byte) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, body, 0o644); err != nil {
		t.Fatal(err)
	}
}

// 封存记录按赛季分册：先 build 正式赛季、再 build 实验赛季，前一本不能被盖掉。
// 老的单本 bots/封存记录.json 装不下两个名册，v1.5 实验一 build，正式赛季的
// 封存灯就红了——那不是源码被改，是账本被顶掉了。
func TestSealPerSeason(t *testing.T) {
	root := t.TempDir()
	src := filepath.Join(root, "workspace", "bots", "alpha", "main.go")
	writeFile(t, src, []byte("package main\n"))
	sum, err := hashFile(src)
	if err != nil {
		t.Fatal(err)
	}

	v14 := &Seal{Entries: []SealEntry{{
		Name: "Starter", Source: "workspace/bots/alpha", Bin: "bots/bin/starter.exe",
		Files: map[string]string{"main.go": sum},
	}}}
	if err := v14.Save(root, "formal_v14"); err != nil {
		t.Fatal(err)
	}
	v15 := &Seal{Entries: []SealEntry{{
		Name: "Hunter", Source: "workspace/bots/alpha", Bin: "bots/bin/hunter.exe",
		Files: map[string]string{"main.go": sum},
	}}}
	if err := v15.Save(root, "experiment_v15"); err != nil {
		t.Fatal(err)
	}

	// 两本都要在，且各自的名册只能是自己的。
	for _, want := range []string{"Starter", "Hunter"} {
		t.Run(want, func(t *testing.T) {
			season := map[string]string{"Starter": "formal_v14", "Hunter": "experiment_v15"}[want]
			seal, err := LoadSeal(root, season)
			if err != nil {
				t.Fatalf("读 %s 的封存失败：%v", season, err)
			}
			if len(seal.Entries) != 1 || seal.Entries[0].Name != want {
				t.Fatalf("%s 的封存被别的赛季改了：%+v", season, seal.Entries)
			}
			if bad := seal.CheckEntryCoverage([]string{want}); len(bad) != 0 {
				t.Fatalf("名册应当对上：%v", bad)
			}
			// 拿另一个赛季的名册去校验，必须报「不在封存记录里」——
			// 这正是 v14 名册跑在 v15 封存上的样子。
			other := "Hunter"
			if want == "Hunter" {
				other = "Starter"
			}
			if bad := seal.CheckEntryCoverage([]string{other}); len(bad) == 0 {
				t.Fatalf("%s 的封存不该认 %s", season, other)
			}
		})
	}

	// 没 build 过的赛季要给出可执行的下一步，而不是一句「文件不存在」。
	if _, err := LoadSeal(root, "formal_v13"); err == nil {
		t.Fatal("没 build 过的赛季应当报错")
	} else if !strings.Contains(err.Error(), "arena build -config tournament/formal_v13.json") {
		t.Fatalf("错误信息没给命令：%v", err)
	}
}

func TestSealVerifyDetectsSourceChange(t *testing.T) {
	root := t.TempDir()
	src := filepath.Join(root, "workspace", "bots", "alpha", "main.go")
	writeFile(t, src, []byte("package main\n"))
	sum, err := hashFile(src)
	if err != nil {
		t.Fatal(err)
	}
	s := &Seal{Entries: []SealEntry{{
		Name: "Alpha", Source: "workspace/bots/alpha", Bin: "bots/bin/alpha.exe",
		Files: map[string]string{"main.go": sum},
	}}}
	if err := s.Save(root, "formal_v14"); err != nil {
		t.Fatal(err)
	}
	writeFile(t, src, []byte("package main // 改了\n"))
	if bad := s.Verify(root); len(bad) != 1 {
		t.Fatalf("源码改动应当被抓住，实际 %v", bad)
	}
}
