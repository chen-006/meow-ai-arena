/* =====================================================================
 * oracle_host.js — 正式赛引擎宿主（冻结 oracle 的独立进程封装）
 *
 * 定位：Go 裁判唯一的规则来源。宿主只做「确定性模拟」，不做计时、不做判罚、
 *       不做进程管理；这些全部在 Go 侧（见 internal/judge）。
 *
 * 为什么要有这一层：
 *   · 冻结 MVP 的 rules.js 是只读预言机，直接 require 不会修改它；
 *   · 引擎跑在独立进程里，裁判崩溃不会污染模拟，Bot 也无法通过原型链摸到规则；
 *   · 协议是行分隔 JSON，未来换 Go 引擎时替换本文件即可，裁判与选手包不动。
 *
 * 命令（stdin 一行一条）：
 *   {"cmd":"new", seed, generator, tickLimit, tickMs, decideMs, initMs, vision}
 *        → {ok, hello, seed, generator, mapAttempt, rulesHash, protoVersion}
 *   {"cmd":"encode","seat":0}                     → {ok, line}
 *   {"cmd":"apply","seat":0,"line":"..."}          → {ok, count} | {ok:false,error}
 *   {"cmd":"step"}                                 → {ok, done, turn, frame?}
 *   {"cmd":"note","seat":0,"ms":12.5,"fault":false}→ {ok}
 *   {"cmd":"forfeit","seat":0,"reason":"..."}      → {ok, done}
 *   {"cmd":"result"}                               → {ok, result}
 *   {"cmd":"verify"}                               → {ok, verify}
 *   {"cmd":"bye"}                                  → 退出
 *
 * 纪律：
 *   · 本文件不读墙钟、不写文件、不生成随机数；同一 seed + orderLog 必得同一结果。
 *   · 唯一输出通道是 stdout 的单行 JSON，禁止任何 console.log 噪声。
 * ===================================================================== */
'use strict';

const readline = require('readline');
const path = require('path');
/* 规则文件可由命令行参数指定：默认旧 rules.js；攻守实验传 rules_siege.js。
 * 冻结快照本身不改，变体是独立文件。只允许 engine/oracle/ 白名单内的规则文件，
 * 参数给完整路径也只取文件名，require 全部字面量，杜绝任意文件加载。 */
const ALLOWED_RULES = new Set(['rules.js', 'rules_siege.js']);
const RULES_NAME = process.argv[2] ? path.basename(process.argv[2]) : 'rules.js';
let R;
if (RULES_NAME === 'rules_siege.js') {
  R = require('./oracle/rules_siege.js');
} else if (RULES_NAME === 'rules.js') {
  R = require('./oracle/rules.js');
} else {
  process.stderr.write('规则变体不在白名单: ' + RULES_NAME + '（允许: rules.js, rules_siege.js）\n');
  process.exit(2);
}

const FRAME_EVERY = 5;      // 回放抽帧间隔（tick）
let game = null;
let pending = [null, null];

function write(obj) {
  process.stdout.write(JSON.stringify(obj) + '\n');
}

function strip(line) {
  return String(line).replace(/\n+$/, '');
}

function handle(req) {
  switch (req.cmd) {
    case 'new': {
      game = new R.Game(2, req.seed, {
        tickLimit: req.tickLimit,
        tickMs: req.tickMs,
        decideMs: req.decideMs,
        initMs: req.initMs,
        generator: req.generator,
        vision: req.vision === true,
        mode: req.mode || 'duel',
        mothershipHp: req.mothershipHp,
        attackerMothershipHp: req.attackerMothershipHp,
        fortBudget: req.fortBudget,
        placeTicks: req.placeTicks,
        attackerCu: req.attackerCu,
        defenderCu: req.defenderCu,
        attackerStartLav: req.attackerStartLav,
        defenderStartLav: req.defenderStartLav
      });
      pending = [null, null];
      const header = game.header();
      return {
        ok: true,
        hello: strip(game.encodeHello()),
        seed: game.seed,
        generator: game.map.generator,
        mapAttempt: game.map.attempt || 0,
        rulesHash: header.rulesHash,
        protoVersion: R.PROTO.version,
        tickLimit: game.cfg.tickLimit
      };
    }
    case 'encode': {
      if (!game) return { ok: false, error: 'no game' };
      if (game.done) return { ok: false, error: 'game done' };
      return { ok: true, line: strip(game.encodeTick(req.seat | 0)) };
    }
    case 'apply': {
      if (!game) return { ok: false, error: 'no game' };
      const orders = game.decode(req.seat | 0, req.line);
      pending[req.seat | 0] = orders;
      return { ok: true, count: orders.length };
    }
    case 'step': {
      if (!game) return { ok: false, error: 'no game' };
      const frame = game.step([pending[0] || [], pending[1] || []]);
      pending = [null, null];
      const out = { ok: true, done: game.done, turn: game.turn };
      /* 只在抽帧点与终局回传帧，避免每 tick 都搬运整张局面 */
      if (game.done || game.turn % FRAME_EVERY === 0) out.frame = frame;
      return out;
    }
    case 'note': {
      if (!game) return { ok: false, error: 'no game' };
      const seat = req.seat | 0;
      game.noteBotTime(seat, Number(req.ms) || 0);
      if (req.fault) game.noteFault(seat);
      return { ok: true };
    }
    case 'forfeit': {
      if (!game) return { ok: false, error: 'no game' };
      game.forfeit(req.seat | 0, req.reason || '故障出局');
      return { ok: true, done: game.done };
    }
    /* 双方同时失去资格时不能走 forfeit（那会判另一方赢），用 end 显式收局。 */
    case 'end': {
      if (!game) return { ok: false, error: 'no game' };
      game.endGame(req.winner == null ? -1 : req.winner, req.level || 0, req.reason || '裁判终止');
      return { ok: true, done: game.done };
    }
    case 'result': {
      if (!game) return { ok: false, error: 'no game' };
      const r = game.result();
      return {
        ok: true,
        result: Object.assign({}, r, {
          generator: game.map.generator,
          mapAttempt: game.map.attempt || 0,
          orderTurns: game.orderLog.length,
          frameCount: game.frameHashes.length
        })
      };
    }
    case 'verify': {
      if (!game) return { ok: false, error: 'no game' };
      const rec = {
        seed: game.seed,
        tickLimit: game.cfg.tickLimit,
        generator: game.map.generator,
        vision: game.vision,
        orderLog: game.orderLog,
        frameHashes: game.frameHashes,
        mapAttempt: game.map.attempt || 0,
        result: game.resultData,
        mothershipHp: game.cfg.mothershipHp,
        attackerMothershipHp: game.cfg.attackerMothershipHp,
        fortBudget: game.cfg.fortBudget,
        placeTicks: game.cfg.placeTicks,
        attackerCu: game.cfg.attackerCu,
        defenderCu: game.cfg.defenderCu,
        attackerStartLav: game.cfg.attackerStartLav,
        defenderStartLav: game.cfg.defenderStartLav
      };
      return { ok: true, verify: R.verifyDeterminism(rec) };
    }
    case 'bye':
      process.exit(0);
      return { ok: true };
    default:
      return { ok: false, error: 'unknown cmd ' + String(req.cmd) };
  }
}

const rl = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
rl.on('line', line => {
  if (!line.trim()) return;
  let req;
  try {
    req = JSON.parse(line);
  } catch (e) {
    write({ ok: false, error: 'bad json' });
    return;
  }
  try {
    write(handle(req));
  } catch (e) {
    write({ ok: false, error: (e && e.message) ? e.message : String(e) });
  }
});
rl.on('close', () => process.exit(0));
