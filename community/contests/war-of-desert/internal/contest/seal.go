// 文件：internal/contest/seal.go
// 版本：v1.4
// 功能：封存记录的读取、写入与开赛校验。
//
// 为什么要有这层：v1.2 的 bots/封存记录.json 是 11-15 生成的，源码在 11-16
// 的 v1.2 改动里全部重写过，7 个文件哈希一个都对不上——但 arena run 照样
// 开跑，赛后审计第 1 条（源码哈希与封存记录一致）直接过不去。
// 现在把校验做成开赛硬门槛：对不上就中止，并指出是哪个文件。
//
// v1.4 起封存记录按赛季分册：bots/seal-records/<配置名>.json。老的单本
// bots/封存记录.json 只够装一个名册，v1.5 实验赛季（Hunter）一 build 就把
// 正式赛季（Starter）那本盖掉，于是「赛季页封存灯永远是红的」——名册差异
// 被误报成源码被篡改。一本一赛季，两个赛季并存互不影响。
package contest

import (
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
)

// SealEntry 是一名选手的封存项。
type SealEntry struct {
	Name   string            `json:"name"`
	Source string            `json:"source"`
	Bin    string            `json:"bin"`
	Files  map[string]string `json:"files"`
	// Prebuilt 项没有源码：只校验可执行文件的哈希。
	Prebuilt  bool   `json:"prebuilt,omitempty"`
	BinSHA256 string `json:"binSha256,omitempty"`
}

// Seal 是一个赛季的封存记录（bots/seal-records/<赛季>.json）。
type Seal struct {
	Season      string            `json:"season,omitempty"`
	GeneratedAt string            `json:"generatedAt"`
	Shared      map[string]string `json:"shared"`
	Entries     []SealEntry       `json:"entries"`
	Note        string            `json:"note,omitempty"`
}

// SealDir 是封存记录的存放目录，文件名 = 赛季标识（配置文件名去掉 .json）。
const SealDir = "seal-records"

// SealPath 返回某赛季封存记录的路径。
//
// season 为空时退回一个显式的 default 本，避免悄悄写进别的赛季。
func SealPath(root, season string) string {
	if season == "" {
		season = "default"
	}
	return filepath.Join(root, "bots", SealDir, season+".json")
}

// LoadSeal 读某赛季封存记录。
func LoadSeal(root, season string) (*Seal, error) {
	path := SealPath(root, season)
	raw, err := os.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("读不到本赛季封存记录 %s（先跑 arena build -config tournament/%s.json）: %w",
			filepath.ToSlash(filepath.Join("bots", SealDir, season+".json")), season, err)
	}
	var s Seal
	if err := json.Unmarshal(raw, &s); err != nil {
		return nil, fmt.Errorf("封存记录 %s 不是合法 JSON: %w", path, err)
	}
	return &s, nil
}

// Save 把封存记录写回本赛季节的那一本。
//
// 只覆盖同一个 season 的文件：另一个赛季的封存是它自己那次 build 的证据，
// 不能被顺手改掉。
func (s *Seal) Save(root, season string) error {
	path := SealPath(root, season)
	if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
		return err
	}
	if s.Season == "" {
		s.Season = season
	}
	raw, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, append(raw, '\n'), 0o644)
}

// Verify 逐文件比对当前工作区的哈希，返回不一致清单。
//
// 空清单表示封存链完整。清单非空时调用方必须中止开赛：
// 带着失配的封存开跑，赛后无法证明「跑的就是收卷的那份代码」。
func (s *Seal) Verify(root string) []string {
	var bad []string
	for rel, want := range s.Shared {
		got, err := hashFile(filepath.Join(root, filepath.FromSlash(rel)))
		if err != nil {
			bad = append(bad, fmt.Sprintf("%s：%v", rel, err))
			continue
		}
		if got != want {
			bad = append(bad, fmt.Sprintf("%s：封存 %s，实际 %s", rel, short(want), short(got)))
		}
	}
	for _, e := range s.Entries {
		if e.Prebuilt {
			got, err := hashFile(resolveBin(root, e.Bin))
			if err != nil {
				bad = append(bad, fmt.Sprintf("%s（预编译）：%v", e.Name, err))
				continue
			}
			if got != e.BinSHA256 {
				bad = append(bad, fmt.Sprintf("%s（预编译）：封存 %s，实际 %s",
					e.Name, short(e.BinSHA256), short(got)))
			}
			continue
		}
		dir := filepath.Join(root, filepath.FromSlash(e.Source))
		have, err := hashTree(dir)
		if err != nil {
			bad = append(bad, fmt.Sprintf("%s/%s：%v", e.Name, e.Source, err))
			continue
		}
		for name, want := range e.Files {
			got, ok := have[name]
			if !ok {
				bad = append(bad, fmt.Sprintf("%s/%s：源码已删除", e.Name, name))
				continue
			}
			if got != want {
				bad = append(bad, fmt.Sprintf("%s/%s：封存 %s，实际 %s", e.Name, name, short(want), short(got)))
			}
		}
		for name := range have {
			if _, ok := e.Files[name]; !ok {
				bad = append(bad, fmt.Sprintf("%s/%s：封存后新增的源码文件", e.Name, name))
			}
		}
		// 源码型选手也要锁定实际执行的 bin。老封存记录可能没有 binSha256，
		// 这里只在有值时校验，避免误伤历史记录。
		if e.BinSHA256 != "" {
			got, err := hashFile(resolveBin(root, e.Bin))
			if err != nil {
				bad = append(bad, fmt.Sprintf("%s：编译产物不可用：%v", e.Name, err))
				continue
			}
			if got != e.BinSHA256 {
				bad = append(bad, fmt.Sprintf("%s：编译产物封存 %s，实际 %s（bin 与源码不一致，需要重新 arena build）",
					e.Name, short(e.BinSHA256), short(got)))
			}
		}
	}
	sort.Strings(bad)
	return bad
}

// CheckEntryCoverage 确认配置里的选手与封存记录一一对应。
func (s *Seal) CheckEntryCoverage(names []string) []string {
	sealed := map[string]bool{}
	for _, e := range s.Entries {
		sealed[e.Name] = true
	}
	var bad []string
	for _, n := range names {
		if !sealed[n] {
			bad = append(bad, n+"：不在封存记录里（需要重新 arena build）")
		}
	}
	for _, e := range s.Entries {
		found := false
		for _, n := range names {
			if n == e.Name {
				found = true
				break
			}
		}
		if !found {
			bad = append(bad, e.Name+"：封存了但不在本赛季名单里")
		}
	}
	sort.Strings(bad)
	return bad
}

// resolveBin 解析 bin 路径：绝对路径原样用（基线 exe 在 Go 模块之外，
// 用 filepath.Join 拼会得到 "root/D:/..." 这种非法路径）。
func resolveBin(root, bin string) string {
	if filepath.IsAbs(bin) || (len(bin) > 1 && bin[1] == ':') {
		return filepath.FromSlash(bin)
	}
	return filepath.Join(root, filepath.FromSlash(bin))
}

func hashFile(p string) (string, error) {
	raw, err := os.ReadFile(p)
	if err != nil {
		return "", err
	}
	sum := sha256.Sum256(raw)
	return hex.EncodeToString(sum[:]), nil
}

func hashTree(dir string) (map[string]string, error) {
	out := map[string]string{}
	err := filepath.Walk(dir, func(p string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		if info.IsDir() || !strings.HasSuffix(p, ".go") {
			return nil
		}
		h, err := hashFile(p)
		if err != nil {
			return err
		}
		rel, _ := filepath.Rel(dir, p)
		out[filepath.ToSlash(rel)] = h
		return nil
	})
	return out, err
}

func short(h string) string {
	if len(h) <= 12 {
		return h
	}
	return h[:12]
}
