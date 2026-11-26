// 第 11–12 期（回旋镖大作战 / OA 争霸）的 WASM 构建配置（独立开源版）。
// 直接指向 meow-ai-arena 仓库内的源码，无需外部比赛目录：
//   - 双模式裁判引擎 engine.wasm（native/boomerang/engine_api.cpp + boomerang-team/workspace/dart.hpp）
//   - 26 份完整程序型 bot（Asyncify stdio）：
//       · 第 11 集海选原版 13 位（../../boomerang/bots/*/player.cpp）
//       · 第 12 集团队赛终版 10 位（../../boomerang-team/bots/*/player.cpp；astra-t 自动应用 2 行四人局兼容补丁）
//       · 3 档官方陪练 weak / mid / strong（../../boomerang-team/engine/sparring/*.cpp）
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
export const ARENA_REPO = path.resolve(process.env.MEOW_REPO || path.join(ROOT, '..', '..'));

export const ROSTER13 = [
  // [id, short, model, tool, charKey, color, team12, ep11Dir, ep12Dir]
  ['astra',    'Astra',    'GPT-6.2 Astra',       'Codex',       'astra',    '#10C08A', 0,  'gpt-6.2-astra',        'gpt-6.2-astra'],
  ['fable',    'Fable',    'Claude Fable 5.2',    'Claude Code', 'fable',    '#FF7A2F', 1,  'claude-fable-5.2',     'claude-fable-5.2'],
  ['opus',     'Opus',     'Claude Opus 5.5',     'Claude Code', 'opus',     '#E0553B', 1,  'claude-opus-5.5',      'claude-opus-5.5'],
  ['carbon',   'Carbon',   'Gemini 4 Carbon',     'Antigravity', 'gemini',   '#5E7CE2', 1,  'gemini-4-carbon',      'gemini-4-carbon'],
  ['sol',      'Sol',      'GPT-6.2 Sol',         'Codex',       'sol',      '#F5B82E', 0,  'gpt-6.2-sol',          'gpt-6.2-sol'],
  ['hydrogen', 'Hydrogen', 'Gemini 4 Hydrogen',   'Antigravity', 'gemini_s', '#7B6BFF', 0,  'gemini-4-hydrogen',    'gemini-4-hydrogen'],
  ['grok',     'Grok',     'Grok 4.7',            'Grok Build',  'grok',     '#F0A500', 0,  'grok-4.7',             'grok-4.7'],
  ['glm',      'GLM',      'GLM 5.3',             'OpenCode',    'glm',      '#3699FF', 1,  'glm-5.3',              'glm-5.3'],
  ['deepseek', 'DeepSeek', 'DeepSeek V4.1',       'dsh',         'deepseek', '#2F6BFF', 1,  'deepseek-v4.1-flash',  'deepseek-v4.1-flash'],
  ['kimi',     'Kimi',     'Kimi K3.1 Pro',       'Kimi Code',   'kimi',     '#2EC4E6', 0,  'kimi-k3.1-pro',        'kimi-k3.1-pro'],
  ['qwen',     '千问',      'Qwen 4',              'OpenCode',    'qwen',     '#8E6BF5', -1, 'qwen4-max',            null],
  ['mimo',     'MiMo',     'MiMo v2.6 Pro',       'OpenCode',    'mimo',     '#FF6B8A', -1, 'mimo-v2.6-pro',        null],
  ['doubao',   '豆包',      'Doubao Seed',         'OpenCode',    'doubao',   '#FF5A4E', -1, 'doubao-seed-evolving', null],
];

function patchAstraFfa(src) {
  const a1 = 'side=int(gi->team[seat]); ownKing=gi->king[side]; enemyKing=gi->king[1-side];';
  const b1 = 'side=int(gi->team[seat]);\n        if(c->mode==M_KING) { ownKing=gi->king[side]; enemyKing=gi->king[1-side]; }';
  const a2 = 'if(q==w.formation.king[1-p.team] && w.p[q].inv<3 && distance<7500)';
  const b2 = 'if(w.cf.mode==M_KING && q==w.formation.king[1-p.team] && w.p[q].inv<3 && distance<7500)';
  if (!src.includes(a1) || !src.includes(a2)) return src;
  return src.replace(a1, b1).replace(a2, b2);
}

const bots = [];

for (const [id, short, model, tool, charKey, color, team12, ep11Dir] of ROSTER13) {
  bots.push({
    id, short, model, tool, charKey, color, team12,
    group: 'solo',
    version: '海选版',
    name: `${short}（海选版）`,
    full: `${model} · 第 11 集海选原版`,
    avatar: `ep11/${id}`,
    src: `boomerang/bots/${ep11Dir}/player.cpp`,
  });
}

for (const [id, short, model, tool, charKey, color, team12, _ep11Dir, ep12Dir] of ROSTER13) {
  if (!ep12Dir) continue;
  const note = id === 'astra'
    ? 'R6 终版（含亲手改防塌边界 + 2 行四人局国王索引兼容修补）'
    : id === 'glm'
      ? 'R4–R6 亲手改终版（防误杀补丁，回炉个人赛实力分暴涨 +71.0）'
      : '第 12 集团队赛冻结终版（支持 5v5 电台/战术单与个人赛）';
  bots.push({
    id: `${id}-t`, baseId: id, short, model, tool, charKey, color, team12,
    group: 'team',
    version: '团队终版',
    name: `${short}（团队终版）`,
    full: `${model} · 第 12 集团队赛终版`,
    avatar: `ep11/${id}`,
    note,
    src: `boomerang-team/bots/${ep12Dir}/player.cpp`,
    patch: id === 'astra' ? patchAstraFfa : undefined,
  });
}

for (const [id, short, cn, note] of [
  ['weak',   '弱陪练', '官方陪练·弱', '基础追击与直线投镖，不预判也不避险，适合新手上手或一拖四承压测试。'],
  ['mid',    '中陪练', '官方陪练·中', '具备基础走位躲镖、捡镖与近战反击，贴近入门选手水平。'],
  ['strong', '强陪练', '官方陪练·强', '主办方高强陪练：具备提前量预判、冲刺近战与基础塌陷避让，用来检验队伍上限。'],
]) {
  bots.push({
    id, short, model: null, tool: '主办方', charKey: id === 'strong' ? 'opus' : id === 'mid' ? 'sol' : 'doubao',
    color: id === 'strong' ? '#DA5C44' : id === 'mid' ? '#488AC6' : '#6E8CA5',
    team12: -1,
    group: 'ref',
    version: '官方陪练',
    name: cn,
    full: cn,
    avatar: `ep11/${id}`,
    note,
    src: `boomerang-team/engine/sparring/${id}.cpp`,
  });
}

export default {
  id: 'ep11',
  game: 'boomerang',
  title: '第 11–12 期 · 回旋镖大作战（OA 争霸）',
  root: ARENA_REPO,
  include: 'boomerang-team/engine/sparring',   // 三档陪练共用的 objective.hpp
  stageCopy: {
    'include/dart.hpp': 'boomerang-team/workspace/dart.hpp',
  },
  engineShim: 'native/boomerang/engine_api.cpp',
  botKind: 'stdio',
  flags: ['-sSTACK_SIZE=16777216', '-sINITIAL_MEMORY=67108864'],
  roster: ROSTER13,
  bots,
};
