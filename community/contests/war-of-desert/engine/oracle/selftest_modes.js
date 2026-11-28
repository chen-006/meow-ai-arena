#!/usr/bin/env node
/* 攻守规则变体 · 三种行动模式的行为契约自检
 *
 *   node engine/oracle/selftest_modes.js
 *
 * 契约（Bot 的任务是完成攻击/防御目标，不是逐台点鼠标）：
 *   obey       引擎不做任何自主移动；只有 Bot 显式 move 才动
 *   neutral    命令为主：射程内停步开火，不追击；命令之外只做高差后撤
 *   aggressive 命令只是目标：朝目标推进，途中边走边打；到点驻守；无命令才自主索敌
 * 开火与移动是分开结算的，模式约束的是移动，不是扣扳机。
 */
'use strict';

const R = require('./rules_siege.js');

let pass = 0, fail = 0;
function ok(name, cond, extra) {
  if (cond) { pass++; console.log('  OK   ' + name); }
  else { fail++; console.log('  FAIL ' + name + (extra ? '  → ' + JSON.stringify(extra) : '')); }
}

const g = new R.Game(2, 20261201);
const CELL = 6;
const lav0 = g.units.find(u => u.kind === 'LAV' && u.side === 0);
const lav1 = g.units.find(u => u.kind === 'LAV' && u.side === 1);
const rangePx = R.TYPES.LAV.range * CELL;          // LAV 射程（像素）
const goal = { x: lav0.x + 200, y: lav0.y };       // 远离战线的目标点

/* 把两个 LAV 摆成指定间距，其余单位挪远，避免干扰索敌。
 * 地形会影响 neutral 的高差分支，所以沿圆周找一个「双方同高」的落点，
 * 保证测的是模式语义而不是随机地形。 */
function reset(dPx) {
  lav0.x = 400; lav0.y = 400; lav0.hp = lav0.maxHp = 90;
  lav0.order = null; lav1.order = null;
  for (const u of g.units) {
    if (u === lav0 || u === lav1) continue;
    u.x = -10000; u.y = -10000;
  }
  for (let a = 0; a < Math.PI * 2; a += 0.02) {
    lav1.x = lav0.x + Math.cos(a) * dPx;
    lav1.y = lav0.y + Math.sin(a) * dPx;
    if (!g.hasHeightAdvantage(lav0, lav1)) return;
  }
}
function intent(side, mode, order, dPx) {
  reset(dPx);
  const u = side === 0 ? lav0 : lav1;
  u.mode = mode; u.order = order;
  return g.unitIntent(u);
}

console.log('射程 ' + rangePx + ' px ／ 视野 ' + R.TYPES.LAV.vision + ' 格\n');

const IN = rangePx * 0.5;          // 射程内
const MID = rangePx * 2;           // 射程外、视野内（6.4 格 < 9 格）
const FAR = rangePx * 4;           // 视野外（12.8 格 > 9 格）

/* ---- obey：Bot 不动它就不动 ---- */
console.log('obey · 引擎零自主移动');
ok('无命令 + 敌人在射程内 → 不移动', intent(0, 'obey', null, IN).move === null);
ok('无命令 + 敌人在射程内 → 不锁定目标', intent(0, 'obey', null, IN).targetId === null);
let it = intent(0, 'obey', { type: 'move', x: 1000, y: 1000 }, IN);
ok('有 move 命令 → 照命令走（哪怕敌人在射程内）', it.move && it.move.x === 1000);
ok('有 attack 命令 → 锁定该目标', intent(0, 'obey', { type: 'attack', targetId: lav1.id }, IN).targetId === lav1.id);

/* ---- neutral：命令为主，不追击 ---- */
console.log('\nneutral · 命令为主');
it = intent(0, 'neutral', { type: 'move', x: goal.x, y: goal.y }, IN);
ok('敌人在射程内 → 停步开火', it.move === null && it.targetId === lav1.id, it);
it = intent(0, 'neutral', { type: 'move', x: goal.x, y: goal.y }, MID);
ok('敌人在射程外、视野内 → 继续执行命令', it.move && it.move.x === goal.x && it.move.y === goal.y, it);
it = intent(0, 'neutral', null, MID);
ok('没有命令 + 敌人在视野内 → 原地不动（不追击）', it.move === null, it);
it = intent(0, 'neutral', { type: 'move', x: goal.x, y: goal.y }, FAR);
ok('敌人在视野外 → 不会因为「高差」原地罚站（回归用例）', it.move && it.move.x === goal.x, it);

/* ---- aggressive：命令只是目标 ---- */
console.log('\naggressive · 命令只是目标');
it = intent(0, 'aggressive', { type: 'move', x: goal.x, y: goal.y }, IN);
ok('敌人在射程内 → 仍朝目标推进（边走边打）', it.move && it.move.x === goal.x, it);
ok('同拍锁定射程内敌人（开火与移动分开结算）', it.targetId === lav1.id, it);
it = intent(0, 'aggressive', { type: 'move', x: lav0.x + 2, y: lav0.y }, IN);
ok('已到目标 → 驻守，不再挪动', it.move === null, it);
it = intent(0, 'aggressive', { type: 'attack', targetId: lav1.id }, IN);
ok('attack 命令当目标 → 抵近该目标（不原地站桩）', it.move && it.move.x === lav1.x, it);
it = intent(0, 'aggressive', null, MID);
ok('没有命令 + 敌人在视野内 → 自主压上', it.move !== null, it);
it = intent(0, 'aggressive', null, FAR);
ok('没有命令 + 敌人在视野外 → 不乱跑', it.move === null && it.targetId === null, it);
it = intent(0, 'aggressive', null, IN);
ok('没有命令 + 敌人在射程内 → 停下打', it.move === null && it.targetId === lav1.id, it);

/* ---- 非战斗单位：三种模式都只照命令走 ---- */
console.log('\nSCOUT / SALVAGER · 无武器，三模式同构');
const sv = g.units.find(u => u.kind === 'SALVAGER' && u.side === 0);
sv.mode = 'aggressive'; sv.order = { type: 'move', x: sv.x + 30, y: sv.y };
const sit = g.unitIntent(sv);
ok('激进采集车不会被敌人牵走', sit.move && sit.move.x === sv.x + 30 && sit.targetId === null, sit);

console.log('\n' + pass + ' 通过 / ' + fail + ' 失败');
process.exit(fail ? 1 : 0);
