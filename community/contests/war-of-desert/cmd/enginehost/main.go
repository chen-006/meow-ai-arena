// enginehost —— Go 原生规则引擎宿主。
//
// 版本：v1.2（与 Go 原生引擎 internal/sim 同步）。
// 功能：与 oracle_host.js 实现同一套行 JSON 命令流（new/encode/apply/step/note/
// forfeit/end/result/verify/bye），语义逐命令对齐（engine/README.md:12-26）。
//
// 纪律（与 oracle_host.js:24-27 一致）：不读墙钟、不写文件、不取系统随机数；
// 同一 seed + orderLog 必得同一结果；唯一输出通道是 stdout 单行 JSON。
package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"os"
	"strings"

	"desertarena/internal/sim"
)

// hostCfg 与 internal/engine.HostConfig 的 new 请求字段一致。
type hostCfg struct {
	Seed       int    `json:"seed"`
	Generator  string `json:"generator"`
	TickLimit  int    `json:"tickLimit"`
	TickMs     int    `json:"tickMs"`
	DecideMs   int    `json:"decideMs"`
	InitMs     int    `json:"initMs"`
	Vision     bool   `json:"vision"`
	FrameEvery int    `json:"frameEvery"`
	Ruleset    string `json:"ruleset"`

	Mode                 string `json:"mode"`
	MothershipHP         int    `json:"mothershipHp"`
	AttackerMothershipHP int    `json:"attackerMothershipHp"`
	FortBudget           int    `json:"fortBudget"`
	PlaceTicks           int    `json:"placeTicks"`
	AttackerCU           int    `json:"attackerCu"`
	DefenderCU           int    `json:"defenderCu"`
	AttackerStartLav     int    `json:"attackerStartLav"`
	DefenderStartLav     int    `json:"defenderStartLav"`
}

type envelope struct {
	OK           bool            `json:"ok"`
	Error        string          `json:"error,omitempty"`
	Cmd          string          `json:"cmd"`
	Hello        string          `json:"hello,omitempty"`
	Line         string          `json:"line,omitempty"`
	Seed         int             `json:"seed,omitempty"`
	Generator    string          `json:"generator,omitempty"`
	MapAttempt   int             `json:"mapAttempt,omitempty"`
	RulesHash    string          `json:"rulesHash,omitempty"`
	ProtoVersion int             `json:"protoVersion,omitempty"`
	TickLimit    int             `json:"tickLimit,omitempty"`
	Done         bool            `json:"done,omitempty"`
	Turn         int             `json:"turn,omitempty"`
	Count        int             `json:"count,omitempty"`
	Frame        json.RawMessage `json:"frame,omitempty"`
	Result       json.RawMessage `json:"result,omitempty"`
	Verify       json.RawMessage `json:"verify,omitempty"`
}

type server struct {
	out        *bufio.Writer
	game       *sim.Game
	siege      *sim.SiegeGame
	mode       string
	frameEvery int
	pending    [2][]sim.Action
}

func (s *server) respond(e envelope) {
	e.OK = true
	b, err := json.Marshal(e)
	if err != nil {
		// envelope 全部可序列化，不应发生
		panic(err)
	}
	s.out.Write(b)
	s.out.WriteByte('\n')
	s.out.Flush()
}

func (s *server) fail(cmd, msg string) {
	e := envelope{OK: false, Cmd: cmd, Error: msg}
	b, _ := json.Marshal(e)
	s.out.Write(b)
	s.out.WriteByte('\n')
	s.out.Flush()
}

func (s *server) handle(raw []byte) {
	var probe struct {
		Cmd string `json:"cmd"`
	}
	if err := json.Unmarshal(raw, &probe); err != nil {
		s.fail("?", "bad json: "+err.Error())
		return
	}
	switch probe.Cmd {
	case "new":
		var cfg hostCfg
		if err := json.Unmarshal(raw, &cfg); err != nil {
			s.fail("new", err.Error())
			return
		}
		s.mode = cfg.Mode
		s.frameEvery = cfg.FrameEvery
		if s.frameEvery <= 0 {
			s.frameEvery = 5
		}
		s.pending = [2][]sim.Action{}
		if cfg.Mode == "siege" {
			sg, err := sim.NewSiegeGame(cfg.Seed, sim.SiegeCfg{
				TickLimit: cfg.TickLimit, TickMS: cfg.TickMs, DecideMS: cfg.DecideMs, InitMS: cfg.InitMs,
				Generator: cfg.Generator, Vision: sim.VisionFromBool(cfg.Vision),
				MothershipHP:         float64(cfg.MothershipHP),
				AttackerMothershipHP: float64(cfg.AttackerMothershipHP),
				FortBudget:           cfg.FortBudget, PlaceTicks: cfg.PlaceTicks,
				AttackerCU: float64(cfg.AttackerCU), DefenderCU: float64(cfg.DefenderCU),
				AttackerStartLav: cfg.AttackerStartLav, DefenderStartLav: cfg.DefenderStartLav,
			})
			if err != nil {
				s.fail("new", err.Error())
				return
			}
			s.siege = sg
			s.game = nil
			s.respond(envelope{
				Cmd: "new", Hello: strings.TrimRight(sg.EncodeHello(), "\n"), Seed: sg.Seed,
				Generator: sg.Map.Generator, MapAttempt: sg.Map.Attempt,
				RulesHash: sim.SiegeRulesHash(sg.Cfg), ProtoVersion: sim.ProtoVersion,
				TickLimit: sg.Cfg.TickLimit,
			})
			return
		}
		gg, err := sim.NewGame(cfg.Seed, sim.GameCfg{
			TickLimit: cfg.TickLimit, TickMS: cfg.TickMs, DecideMS: cfg.DecideMs, InitMS: cfg.InitMs,
			Generator: cfg.Generator, Vision: sim.VisionFromBool(cfg.Vision), Ruleset: cfg.Ruleset,
		})
		if err != nil {
			s.fail("new", err.Error())
			return
		}
		s.game = gg
		s.siege = nil
		s.respond(envelope{
			Cmd: "new", Hello: strings.TrimRight(gg.EncodeHello(), "\n"), Seed: gg.Seed,
			Generator: gg.Map.Generator, MapAttempt: gg.Map.Attempt,
			RulesHash:    sim.RulesHashFor(gg.Cfg.Ruleset, gg.Cfg.TickLimit, gg.Vision),
			ProtoVersion: sim.ProtoVersion, TickLimit: gg.Cfg.TickLimit,
		})

	case "encode":
		switch {
		case s.siege != nil:
			s.respond(envelope{Cmd: "encode", Line: strings.TrimRight(s.siege.EncodeTick(probeSeat(raw)), "\n")})
		case s.game != nil:
			s.respond(envelope{Cmd: "encode", Line: strings.TrimRight(s.game.EncodeTick(probeSeat(raw)), "\n")})
		default:
			s.fail("encode", "no game")
		}

	case "apply":
		seat := probeSeat(raw)
		var line struct {
			Line string `json:"line"`
		}
		_ = json.Unmarshal(raw, &line)
		var acts []sim.Action
		var err error
		switch {
		case s.siege != nil:
			acts, err = s.siege.Decode(seat, line.Line)
		case s.game != nil:
			acts, err = s.game.Decode(seat, line.Line)
		default:
			err = fmt.Errorf("no game")
		}
		if err != nil {
			s.fail("apply", err.Error())
			return
		}
		s.pending[seat] = acts
		s.respond(envelope{Cmd: "apply", Count: len(acts)})

	case "step":
		var done bool
		var turn int
		var frame *sim.Frame
		switch {
		case s.siege != nil:
			frame = s.siege.Step(s.pending)
			done, turn = s.siege.Done(), s.siege.Turn
		case s.game != nil:
			frame = s.game.Step(s.pending)
			done, turn = s.game.Done(), s.game.Turn
		default:
			s.fail("step", "no game")
			return
		}
		s.pending = [2][]sim.Action{}
		e := envelope{Cmd: "step", Done: done, Turn: turn}
		// 帧抽点：turn % frameEvery == 0 或终局（默认 5，与 oracle_host.js 一致）
		if turn%s.frameEvery == 0 || done {
			b, _ := json.Marshal(frame)
			e.Frame = b
		}
		s.respond(e)

	case "note":
		var note struct {
			Seat  int     `json:"seat"`
			MS    float64 `json:"ms"`
			Fault bool    `json:"fault"`
		}
		_ = json.Unmarshal(raw, &note)
		if s.siege != nil {
			if note.Fault {
				s.siege.NoteFault(note.Seat)
			}
			s.siege.NoteBotTime(note.Seat, note.MS)
		} else if s.game != nil {
			if note.Fault {
				s.game.NoteFault(note.Seat)
			}
			s.game.NoteBotTime(note.Seat, note.MS)
		}
		s.respond(envelope{Cmd: "note"})

	case "forfeit":
		var f struct {
			Seat   int    `json:"seat"`
			Reason string `json:"reason"`
		}
		_ = json.Unmarshal(raw, &f)
		if s.siege != nil {
			s.siege.Forfeit(f.Seat, f.Reason)
		} else if s.game != nil {
			s.game.Forfeit(f.Seat, f.Reason)
		}
		done := (s.siege != nil && s.siege.Done()) || (s.game != nil && s.game.Done())
		s.respond(envelope{Cmd: "forfeit", Done: done})

	case "end":
		var f struct {
			Winner int    `json:"winner"`
			Level  int    `json:"level"`
			Reason string `json:"reason"`
		}
		_ = json.Unmarshal(raw, &f)
		if s.siege != nil {
			s.siege.EndGame(f.Winner, f.Level, f.Reason)
		} else if s.game != nil {
			s.game.EndGame(f.Winner, f.Level, f.Reason)
		}
		done := (s.siege != nil && s.siege.Done()) || (s.game != nil && s.game.Done())
		s.respond(envelope{Cmd: "end", Done: done})

	case "result":
		switch {
		case s.siege != nil:
			b, _ := json.Marshal(s.siege.Result())
			s.respond(envelope{Cmd: "result", Result: b})
		case s.game != nil:
			b, _ := json.Marshal(s.game.Result())
			s.respond(envelope{Cmd: "result", Result: b})
		default:
			s.fail("result", "no game")
		}

	case "verify":
		rec := s.verifyRec()
		var rep sim.VerifyReply
		switch {
		case s.siege != nil:
			rep = sim.VerifySiegeDeterminism(rec)
		case s.game != nil:
			rep = sim.VerifyDeterminism(rec)
		default:
			s.fail("verify", "no game")
			return
		}
		b, _ := json.Marshal(rep)
		s.respond(envelope{Cmd: "verify", Verify: b})

	case "bye":
		s.respond(envelope{Cmd: "bye"})
		os.Exit(0)
	}
}

// verifyRec 组装复核记录（orderLog + 帧哈希 + 终局摘要）。
func (s *server) verifyRec() *sim.VerifyRec {
	rec := &sim.VerifyRec{}
	if s.siege != nil {
		rec.Seed = s.siege.Seed
		rec.TickLimit = s.siege.Cfg.TickLimit
		rec.Generator = s.siege.Cfg.Generator
		rec.MapAttempt = &s.siege.Map.Attempt
		rec.OrderLog = s.siege.OrderLog
		rec.FrameHashes = s.siege.FrameHashes
		v := s.siege.Vision
		rec.Vision = &v
		// 攻守局的母舰 HP / 要塞预算 / 初始 CU 等必须一并带给复核，
		// 否则重放会用默认规则，与记录对局不是同一局。
		sc := s.siege.Cfg
		rec.SiegeCfg = &sc
		if s.siege.Forfeited >= 0 {
			f := s.siege.Forfeited
			rec.Forfeited = &f
		}
		if r := s.siege.Result(); r != nil {
			rec.SiegeResult = r
			// level=0 且无红牌 = 裁判手动 EndGame（双方同时故障），重放要补同一步。
			if r.Level == 0 && s.siege.Forfeited < 0 {
				w := r.Winner
				rec.EndWinner = &w
			}
		}
		return rec
	}
	if s.game != nil {
		rec.Seed = s.game.Seed
		rec.TickLimit = s.game.Cfg.TickLimit
		rec.Ruleset = s.game.Cfg.Ruleset
		rec.Generator = s.game.Cfg.Generator
		rec.MapAttempt = &s.game.Map.Attempt
		rec.OrderLog = s.game.OrderLog
		rec.FrameHashes = s.game.FrameHashes
		v := s.game.Vision
		rec.Vision = &v
		if s.game.Forfeited >= 0 {
			f := s.game.Forfeited
			rec.Forfeited = &f
		}
		if r := s.game.Result(); r != nil {
			rec.Result = r
			if r.Level == 0 && s.game.Forfeited < 0 {
				w := r.Winner
				rec.EndWinner = &w
			}
		}
	}
	return rec
}

func probeSeat(raw []byte) int {
	var p struct {
		Seat int `json:"seat"`
	}
	_ = json.Unmarshal(raw, &p)
	return p.Seat
}

func jsonEscape(s string) string {
	b, _ := json.Marshal(s)
	return string(b[1 : len(b)-1])
}

func main() {
	sc := bufio.NewScanner(os.Stdin)
	sc.Buffer(make([]byte, 1<<20), 4<<20)
	out := bufio.NewWriter(os.Stdout)
	defer out.Flush()
	s := &server{out: out}
	for sc.Scan() {
		s.handle(sc.Bytes())
	}
}
