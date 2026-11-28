// Package protocol 定义 Desert Arena 正式赛的线上契约。
//
// 版本：v1.2（Desert Arena 正式赛执行链）。
// 功能：hello / tick / actions / end 的字段形状、地图层编解码、
// 规则常量的读取口径；裁判、引擎宿主与选手工具共用这一份定义。
//
// 只放「线上真的会出现的东西」。
package protocol

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"strings"
)

const (
	// ProtoName / ProtoVersion 与冻结 oracle 的 PROTO 对齐；不匹配必须拒绝开赛。
	ProtoName    = "wod-arena"
	ProtoVersion = 5

	// MaxLine 是单行上限（UTF-8 字节）。tick / actions 都必须满足。
	MaxLine = 128 * 1024

	// Grid / Cell / World 是 v1.0 固定地图口径：256×256 格，1 格 = 6 像素，
	// 像素世界 1536×1536；物理尺度上 1 格 = 10 m，即全图 2.56 km。
	Grid  = 256
	Cell  = 6
	World = Grid * Cell

	// OrderLatency 是协议保证：本 tick 提交的指令在下一 tick 生效。
	OrderLatency = 1
)

// EncodedMap 是 hello 里的地图三层编码：base/surf 走 RLE+base64，elev 原样 base64。
// 该格式与冻结 oracle 的 encodeMapLayers 完全一致，编码名为 rle+b64/1。
type EncodedMap struct {
	W    int    `json:"w"`
	H    int    `json:"h"`
	Enc  string `json:"enc"`
	Base string `json:"base"`
	Surf string `json:"surf"`
	Elev string `json:"elev"`
}

// MapLayers 是解码后的三层地图。base=地形类型，surf=地表材质，elev=高程（0..255）。
type MapLayers struct {
	Base []byte
	Surf []byte
	Elev []byte
}

// DecodeMapLayers 还原地图三层。任何长度不符都报错，不做截断兜底。
func DecodeMapLayers(m EncodedMap) (MapLayers, error) {
	if m.Enc != "rle+b64/1" {
		return MapLayers{}, fmt.Errorf("未知的地图层编码：%s", m.Enc)
	}
	if m.W <= 0 || m.H <= 0 || m.W > 1<<16 || m.H > 1<<16 {
		return MapLayers{}, errors.New("地图尺寸非法")
	}
	n := m.W * m.H
	baseRaw, err := b64(m.Base)
	if err != nil {
		return MapLayers{}, fmt.Errorf("base 解码失败: %w", err)
	}
	surfRaw, err := b64(m.Surf)
	if err != nil {
		return MapLayers{}, fmt.Errorf("surf 解码失败: %w", err)
	}
	elevRaw, err := b64(m.Elev)
	if err != nil {
		return MapLayers{}, fmt.Errorf("elev 解码失败: %w", err)
	}
	base, ok := rleDecode(baseRaw, n)
	if !ok {
		return MapLayers{}, fmt.Errorf("base 层解码后长度应为 %d（RLE 数据不匹配）", n)
	}
	surf, ok := rleDecode(surfRaw, n)
	if !ok {
		return MapLayers{}, fmt.Errorf("surf 层解码后长度应为 %d（RLE 数据不匹配）", n)
	}
	if len(elevRaw) != n {
		return MapLayers{}, fmt.Errorf("elev 层长度 %d 应为 %d", len(elevRaw), n)
	}
	elev := make([]byte, n)
	copy(elev, elevRaw)
	return MapLayers{Base: base, Surf: surf, Elev: elev}, nil
}

func b64(s string) ([]byte, error) {
	// JS 端会先剥掉非字母数字字符；Go 侧同样宽进严出，但长度必须对得上。
	clean := strings.Map(func(r rune) rune {
		switch {
		case r >= 'A' && r <= 'Z', r >= 'a' && r <= 'z', r >= '0' && r <= '9', r == '+', r == '/', r == '=':
			return r
		default:
			return -1
		}
	}, s)
	return base64.StdEncoding.DecodeString(clean)
}

func rleDecode(src []byte, n int) ([]byte, bool) {
	if len(src)%2 != 0 {
		return nil, false
	}
	out := make([]byte, n)
	p := 0
	for i := 0; i < len(src); i += 2 {
		v := src[i]
		count := int(src[i+1]) + 1
		if p+count > n {
			return nil, false
		}
		for k := 0; k < count; k++ {
			out[p] = v
			p++
		}
	}
	return out, p == n
}

// Hello 是开局握手。地图只给编码形态，Bot 自己调 DecodeMapLayers。
type Hello struct {
	Type         string          `json:"type"`
	Version      int             `json:"version"`
	Proto        string          `json:"proto"`
	ProtoVersion int             `json:"protoVersion"`
	RulesHash    string          `json:"rulesHash"`
	Game         string          `json:"game"`
	Seed         int             `json:"seed"`
	Players      int             `json:"players"`
	Generator    string          `json:"generator"`
	MapAttempt   int             `json:"mapAttempt"`
	W            int             `json:"w"`
	H            int             `json:"h"`
	Cell         int             `json:"cell"`
	World        int             `json:"world"`
	Map          EncodedMap      `json:"map"`
	Spawns       [][2]int        `json:"spawns"`
	VP           []VPPoint       `json:"vp"`
	Res          []ResPoint      `json:"res"`
	Rules        json.RawMessage `json:"rules"`
}

type VPPoint struct {
	ID int     `json:"id"`
	X  float64 `json:"x"`
	Y  float64 `json:"y"`
}

type ResPoint struct {
	ID int     `json:"id"`
	X  float64 `json:"x"`
	Y  float64 `json:"y"`
}

// Rules 是 hello.rules 里裁判也会用到的部分；其余字段留给选手包。
type Rules struct {
	TickLimit int `json:"tickLimit"`
	Grid      int `json:"grid"`
	CellPx    int `json:"cellPx"`
	Timing    struct {
		TickMs       int `json:"tickMs"`
		DecideMs     int `json:"decideMs"`
		InitMs       int `json:"initMs"`
		OrderLatency int `json:"orderLatency"`
	} `json:"timing"`
}

// Card 是计分卡（对称赛七领域 / 攻守赛八领域）。
type Card struct {
	Fields  []float64 `json:"fields"`
	Total   float64   `json:"total"`
	VPScore int       `json:"vpScore"`
	Kills   int       `json:"kills"`
	Losses  int       `json:"losses"`
	BotMs   float64   `json:"botMs"`
}

// DecideStat 是逐方决策统计。
type DecideStat struct {
	Calls  int     `json:"calls"`
	AvgMS  float64 `json:"avg_ms"`
	MaxMS  float64 `json:"max_ms"`
	Late   int     `json:"late"`
	Faults int     `json:"faults"`
}

// VSScore 是 v1.5/v1.6 胜利分三本账。
type VSScore struct {
	Kills     int `json:"kills"`
	Relics    int `json:"relics"`
	Flags     int `json:"flags"`
	Total     int `json:"total"`
	Delivered int `json:"delivered,omitempty"`
	Stored    int `json:"stored,omitempty"`
}

// Result 是 end 段与结果归档的核心字段。
type Result struct {
	Winner               int          `json:"winner"`
	Level                int          `json:"level"`
	Reason               string       `json:"reason"`
	Turn                 int          `json:"turn"`
	MothershipHP         int          `json:"mothershipHp,omitempty"`
	AttackerMothershipHP int          `json:"attackerMothershipHp,omitempty"`
	Destroyed            bool         `json:"destroyed,omitempty"`
	DestroyedAt          int          `json:"destroyedAt,omitempty"`
	Turns                int          `json:"turns"`
	Seed                 int          `json:"seed"`
	Mode                 string       `json:"mode"`
	Players              int          `json:"players"`
	MapAttempt           int          `json:"mapAttempt"`
	Generator            string       `json:"generator"`
	Score                []int        `json:"score"`
	VP                   []int        `json:"vp"`
	Cards                []Card       `json:"cards"`
	Decide               []DecideStat `json:"decide"`
	Shots                []int        `json:"shots"`
	Hits                 []int        `json:"hits"`
	Events               int          `json:"events"`
	OrderTurns           int          `json:"orderTurns"`
	FrameCount           int          `json:"frameCount"`
	VS                   []VSScore    `json:"vs,omitempty"`
	Invalid              bool         `json:"invalid,omitempty"`
	InvalidReason        string       `json:"invalidReason,omitempty"`
	Timing               struct {
		TickMs       int `json:"tickMs"`
		DecideMs     int `json:"decideMs"`
		InitMs       int `json:"initMs"`
		OrderLatency int `json:"orderLatency"`
		WallMs       int `json:"wallMs"`
	} `json:"timing"`
}

// WinnerName 把座位号翻译成名字，-1 表示平局。
func WinnerName(winner int, seats []string) string {
	if winner < 0 || winner >= len(seats) {
		return "draw"
	}
	return seats[winner]
}
