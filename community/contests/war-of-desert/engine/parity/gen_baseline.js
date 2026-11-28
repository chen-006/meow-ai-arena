'use strict';
/* gen_baseline.js · 生成移植基线的 JS 行为指纹
 *
 * 从 Dev/MVP 工作树（移植基线：合并提交 b9643b0 + v0.11 收尾 WIP）直接 require
 * referee，跑 greedy vs tactical 若干 seed 的镜像赛，把每局摘要写成 JSON。
 *
 * 产物定位：**行为对照参考（非门禁）**。移植口径已定为 Plan_Beta §5.2——
 * Go 引擎按自确定性 + golden trace 验收，不与 JS 逐位对拍（规则版本分叉 +
 * 超越函数 ulp 双重不可行）。这份指纹用于 Go 引擎完成后做同 seed 的
 * 结构性对照（局数/回合数/胜负/VP/击杀量级），差异大即人工复核。
 *
 * 运行：node engine/parity/gen_baseline.js   （在 war_of_desert/ 下）
 */
const path = require('path');
const fs = require('fs');

const Ref = require('../../../Dev/MVP/referee/referee.js');
const greedy = require('../../../Dev/MVP/referee/bots/greedy.js');
const tactical = require('../../../Dev/MVP/referee/bots/tactical.js');

const SEEDS = [20261109, 20261110, 20261111, 20261112, 20261113];
const TICK_LIMIT = 1200;

const R = Ref.R || require('../../../Dev/MVP/referee/rules.js');
const rulesHash = R.rulesHash(TICK_LIMIT);
if (!/^[0-9a-f]{8}$/.test(rulesHash)) {
  throw new Error('规则指纹格式异常，拒绝落盘: ' + rulesHash);
}

const out = {
  generatedAt: new Date().toISOString(),
  rulesHash,
  source: 'Dev/MVP working tree @ b9643b0 + v0.11 WIP (diff ad32ea3a23e035a8), rulesHash=' + rulesHash,
  config: { bots: ['greedy', 'tactical'], seeds: SEEDS, tickLimit: TICK_LIMIT, mirror: true, generator: 'minecraft', vision: false },
  games: [],
};

for (const seed of SEEDS) {
  const res = Ref.runMatchSync([greedy, tactical], { seed, mirror: true, tickLimit: TICK_LIMIT });
  for (const rec of res.games) {
    const r = rec.result;
    out.games.push({
      seed, game: rec.index, swap: rec.swap,
      winner: r.winner, level: r.level, turns: r.turn, reason: r.reason || null,
      vp: (r.vp || [0, 0]).slice(),
      kills: (r.totals || []).map(t => t.kills == null ? null : t.kills),
      cardsTotal: (r.cards || []).map(c => Math.round(c.total * 100) / 100),
      faults: rec.faults.map(f => f.length),
    });
  }
}

/* 固定文件名落盘；规则版本记录在 JSON 内部（rulesHash 字段），
 * 重生成时整体覆盖，不做拼接路径。 */
const dest = path.join(__dirname, 'baselines', 'mvp.json');
fs.mkdirSync(path.dirname(dest), { recursive: true });
fs.writeFileSync(dest, JSON.stringify(out, null, 2) + '\n');
console.log('BASELINE_WRITTEN ' + path.relative(process.cwd(), dest) + '  games=' + out.games.length + '  rulesHash=' + rulesHash);
