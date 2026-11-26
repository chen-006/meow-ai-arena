// 第 11–12 期静态数据、地图文本、压缩回放、快捷预设加载器
import { genUrl } from '../../common/wasm';
import type { BotMeta, Ep11Data, GameMode } from './types';

let dataPromise: Promise<Ep11Data> | null = null;
const mapCache = new Map<string, Promise<string>>();

export function loadEp11(): Promise<Ep11Data> {
  if (!dataPromise) {
    dataPromise = fetch(genUrl('ep11/data.json')).then(r => {
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      return r.json();
    });
  }
  return dataPromise;
}

export function loadMapText(mapId: string): Promise<string> {
  let p = mapCache.get(mapId);
  if (!p) {
    p = fetch(genUrl(`ep11/maps/${mapId}.txt`)).then(r => {
      if (!r.ok) throw new Error(`地图 ${mapId} 加载失败 (${r.status})`);
      return r.text();
    });
    mapCache.set(mapId, p);
  }
  return p;
}

/** 加载精选回放原始文本（构建期由 compress.mjs 自动生成 .txt.br/.txt.gz 供生产静态站透明传输） */
export async function loadCuratedReplayText(id: string): Promise<string> {
  const res = await fetch(genUrl(`ep11/replays/${id}.txt`));
  if (!res.ok) throw new Error(`回放 ${id} 加载失败 (${res.status})`);
  return res.text();
}

export function botById(d: Ep11Data, id: string): BotMeta | undefined {
  return d.bots.find(b => b.id === id);
}

export function charKeyForName(name: string): string {
  const k = name.toLowerCase().replace(/-t$/, '');
  const map: Record<string, string> = {
    astra: 'astra', sol: 'sol', fable: 'fable', opus: 'opus',
    carbon: 'gemini', hydrogen: 'gemini_s', gemini: 'gemini', gemini_s: 'gemini_s',
    grok: 'grok', glm: 'glm', deepseek: 'deepseek', kimi: 'kimi',
    qwen: 'qwen', '千问': 'qwen', mimo: 'mimo', doubao: 'doubao', '豆包': 'doubao',
    strong: 'opus', mid: 'sol', weak: 'doubao', human: 'astra', '你': 'astra',
  };
  return map[k] || map[k.replace(/\d+$/, '')] || 'astra';
}

export function colorForName(name: string, fallback = '#10C08A'): string {
  const k = name.toLowerCase().replace(/-t$/, '').replace(/\d+$/, '');
  const map: Record<string, string> = {
    astra: '#10C08A', fable: '#FF7A2F', opus: '#E0553B', carbon: '#5E7CE2',
    sol: '#F5B82E', hydrogen: '#7B6BFF', grok: '#F0A500', glm: '#3699FF',
    deepseek: '#2F6BFF', kimi: '#2EC4E6', qwen: '#8E6BF5', mimo: '#FF6B8A',
    doubao: '#FF5A4E', strong: '#DA5C44', mid: '#488AC6', weak: '#6E8CA5',
    human: '#F5A524', '你': '#F5A524',
  };
  return map[k] || fallback;
}

export function avatarForName(name: string): string {
  const k = name.toLowerCase().replace(/-t$/, '').replace(/\d+$/, '');
  const valid = new Set(['astra', 'sol', 'fable', 'opus', 'carbon', 'hydrogen', 'grok', 'glm', 'deepseek', 'kimi', 'qwen', 'mimo', 'doubao', 'weak', 'mid', 'strong', 'human']);
  if (valid.has(k)) return `ep11/${k}`;
  if (k === '千问') return 'ep11/qwen';
  if (k === '豆包') return 'ep11/doubao';
  if (k === '你') return 'ep11/human';
  return 'ep11/astra';
}

// ---- Bot vs Bot & 5v5 梦幻阵容实验台快捷预设 ----

export interface BotMatchPreset {
  id: string;
  label: string;
  badge: string;
  desc: string;
  mode: GameMode;
  map: string;
  bots: string[];
  names?: string[];
  roundPlan?: { r: number; astraPlan: string; fablePlan: string };
}

export const DUEL_PRESETS: BotMatchPreset[] = [
  {
    id: 'spar-ladder',
    label: '官方陪练：强陪练 vs 弱陪练',
    badge: '陪练热身',
    desc: '三档官方陪练里最强和最弱的对打，先看清楚扔镖、回镖、近战的基本节奏。',
    mode: 0, map: 'twinlakes', bots: ['strong', 'weak'],
  },
  {
    id: 'spar-ffa',
    label: '三档陪练 + 海选 Kimi 混战',
    badge: '陪练 · 4 人',
    desc: '弱、中、强三档陪练加一位海选 AI，四人混战看谁先出局。',
    mode: 1, map: 'stonecircle', bots: ['weak', 'mid', 'strong', 'kimi'],
  },
  {
    id: 'final-astra-vs-fable',
    label: '终版 Astra vs 终版 Fable',
    badge: '双队长巅峰',
    desc: '两位队长的第 12 集终版正面单挑。',
    mode: 0, map: 'stonecircle', bots: ['astra-t', 'fable-t'],
  },
  {
    id: 'glm-evolution',
    label: '终版 GLM vs 海选 GLM',
    badge: '回炉 +71 分',
    desc: '现场重跑：亲手改过的终版 GLM 回炉实力分比海选版高 71.0 分。',
    mode: 0, map: 'yinyang', bots: ['glm-t', 'glm'],
  },
  {
    id: 'kimi-evolution',
    label: '终版 Kimi vs 海选 Kimi',
    badge: '¥1 逆变对照',
    desc: '团队终版 Kimi 打自己的海选原版：为团队赛做的改动在单挑里是亏的。',
    mode: 0, map: 'twinlakes', bots: ['kimi-t', 'kimi'],
  },
  {
    id: 'top4-ffa',
    label: '海选前四混战',
    badge: '4人混战',
    desc: '海选前四名在阴阳图四人混战。',
    mode: 1, map: 'yinyang', bots: ['astra', 'fable', 'opus', 'carbon'],
  },
];

/** 第 11 集 4v4 预设（团队赛前传：第 12 集两队的海选原版） */
export const SQUAD_PRESETS: BotMatchPreset[] = [
  {
    id: 'spar-squad',
    label: '4 个中陪练 vs 4 个强陪练 · 沙漏',
    badge: '陪练 4v4',
    desc: '官方陪练组队打 4v4：先看懂两队怎么抢沙漏颈，再换成海选 AI。',
    mode: 2, map: 'hourglass', bots: ['mid', 'mid', 'mid', 'mid', 'strong', 'strong', 'strong', 'strong'],
  },
  {
    id: 'squad-canyon',
    label: '两队海选版 · 峡谷 4v4',
    badge: '4v4',
    desc: 'Astra / Sol / Hydrogen / Grok 对 Fable / Opus / Carbon / GLM，隔着峡谷对扔、过石桥近身。',
    mode: 2, map: 'canyon', bots: ['astra', 'sol', 'hydrogen', 'grok', 'fable', 'opus', 'carbon', 'glm'],
  },
  {
    id: 'squad-oldtown',
    label: '海选前四 vs 第五到第八 · 老城 4v4',
    badge: '4v4',
    desc: '海选前四（Astra、Fable、Opus、Carbon）合兵一处，打 Sol、Hydrogen、Grok、GLM。',
    mode: 2, map: 'oldtown', bots: ['astra', 'fable', 'opus', 'carbon', 'sol', 'hydrogen', 'grok', 'glm'],
  },
];

/** 真人对战的四档对手（第 11 集海选原版，括号里是海选 BT 实力分） */
export interface Tier { id: string; label: string; lead: string; ffa: string[]; squad: string[] }
export const TIERS: Tier[] = [
  { id: 'easy', label: '入门', lead: 'doubao', ffa: ['doubao', 'mimo', 'qwen'], squad: ['doubao', 'mimo', 'qwen', 'kimi'] },
  { id: 'mid', label: '中坚', lead: 'glm', ffa: ['glm', 'deepseek', 'kimi'], squad: ['glm', 'deepseek', 'kimi', 'grok'] },
  { id: 'hard', label: '强敌', lead: 'fable', ffa: ['fable', 'opus', 'carbon'], squad: ['fable', 'opus', 'carbon', 'glm'] },
  { id: 'boss', label: '终极', lead: 'astra', ffa: ['astra', 'fable', 'opus'], squad: ['astra', 'fable', 'opus', 'carbon'] },
];
/** 4v4 里和你同队的三位 AI */
export const SQUAD_MATES = ['sol', 'hydrogen', 'grok'];

/** 正片体素小人的主色（脚下色环、镖、拖尾；照正片 srcoxchars 各角色的 COLOR） */
export const VOX_COLOR: Record<string, string> = {
  astra: '#12B886', sol: '#7FE0A8', fable: '#9E2340', opus: '#FFA436', gemini: '#E8459B', grok: '#FFD43B', kimi: '#5DC8F8',
  deepseek: '#1B4FD8', doubao: '#F0504A', qwen: '#8A6AF2', mimo: '#FF6900', glm: '#8892A8', gemini_s: '#FFA6D6',
};
/** 可以选作"你的造型"的 13 位 */
export const VOX_KEYS: [string, string][] = [
  ['astra', 'Astra'], ['sol', 'Sol'], ['fable', 'Fable'], ['opus', 'Opus'], ['gemini', 'Carbon'], ['gemini_s', 'Hydrogen'], ['grok', 'Grok'],
  ['kimi', 'Kimi'], ['deepseek', 'DeepSeek'], ['doubao', '豆包'], ['qwen', '千问'], ['mimo', 'MiMo'], ['glm', 'GLM'],
];
export const YOU_COLOR = '#F5A524';

export const TEAM_PRESETS: BotMatchPreset[] = [
  {
    id: 'official-caldera',
    label: '正式赛原阵 · 火山口歼灭',
    badge: 'R2 正式',
    desc: 'Astra队（Astra/Sol/Hydrogen/Grok/Kimi）vs Fable队（Fable/Opus/Carbon/GLM/DeepSeek）',
    mode: 3, map: 'caldera',
    bots: ['astra-t', 'sol-t', 'hydrogen-t', 'grok-t', 'kimi-t', 'fable-t', 'opus-t', 'carbon-t', 'glm-t', 'deepseek-t'],
    names: ['astra', 'sol', 'hydrogen', 'grok', 'kimi', 'fable', 'opus', 'carbon', 'glm', 'deepseek'],
    roundPlan: { r: 2, astraPlan: '正式', fablePlan: '正式' },
  },
  {
    id: 'official-piers',
    label: '正式赛原阵 · 三座栈桥占点',
    badge: 'R4 悬崖局',
    desc: 'Fable 队 3A-2C、放空 B 点；阵亡 3 秒复活，三条线来回抢点。',
    mode: 4, map: 'piers',
    bots: ['astra-t', 'sol-t', 'hydrogen-t', 'grok-t', 'kimi-t', 'fable-t', 'opus-t', 'carbon-t', 'glm-t', 'deepseek-t'],
    names: ['astra', 'sol', 'hydrogen', 'grok', 'kimi', 'fable', 'opus', 'carbon', 'glm', 'deepseek'],
    roundPlan: { r: 4, astraPlan: '正式', fablePlan: '正式' },
  },
  {
    id: 'official-keeps',
    label: '正式赛原阵 · 双堡刺王',
    badge: 'R6 收官战',
    desc: '两队国王各 3 条命，地块分波塌陷，最后都被逼到中央广场。',
    mode: 5, map: 'keeps',
    bots: ['astra-t', 'sol-t', 'hydrogen-t', 'grok-t', 'kimi-t', 'fable-t', 'opus-t', 'carbon-t', 'glm-t', 'deepseek-t'],
    names: ['astra', 'sol', 'hydrogen', 'grok', 'kimi', 'fable', 'opus', 'carbon', 'glm', 'deepseek'],
    roundPlan: { r: 6, astraPlan: '正式', fablePlan: '正式' },
  },
  {
    id: 'allstar-vs-5astra',
    label: '非 Astra 全明星 vs 5×Astra',
    badge: '实验10 · 11%:88%',
    desc: 'Fable、Opus、Carbon、Hydrogen、Sol 组队打 5 个 Astra（实验三胜率 11.4%）。',
    mode: 3, map: 'caldera',
    bots: ['fable-t', 'opus-t', 'carbon-t', 'hydrogen-t', 'sol-t', 'astra-t', 'astra-t', 'astra-t', 'astra-t', 'astra-t'],
    names: ['fable', 'opus', 'carbon', 'hydrogen', 'sol', 'astra', 'astra2', 'astra3', 'astra4', 'astra5'],
  },
  {
    id: 'auction-swap',
    label: '拍卖平行宇宙：¥1 Kimi ↔ ¥599 Carbon',
    badge: '实验11 · +24.4pp',
    desc: '如果拍卖时 Astra 买下 ¥599 的 Carbon、Fable 拿到 ¥1 的 Kimi。',
    mode: 4, map: 'piers',
    bots: ['astra-t', 'sol-t', 'hydrogen-t', 'grok-t', 'carbon-t', 'fable-t', 'opus-t', 'kimi-t', 'glm-t', 'deepseek-t'],
    names: ['astra', 'sol', 'hydrogen', 'grok', 'carbon', 'fable', 'opus', 'kimi', 'glm', 'deepseek'],
  },
  {
    id: 'carry-1v4',
    label: '一拖四极限承压：Astra + 4×weak vs Fable队',
    badge: '实验13 · 双堡66%',
    desc: 'Astra 带 4 个最弱陪练打完整的 Fable 队：双堡刺王胜率 66.0%，三图合计 33.0%。',
    mode: 5, map: 'keeps',
    bots: ['astra-t', 'weak', 'weak', 'weak', 'weak', 'fable-t', 'opus-t', 'carbon-t', 'glm-t', 'deepseek-t'],
    names: ['astra', 'weak1', 'weak2', 'weak3', 'weak4', 'fable', 'opus', 'carbon', 'glm', 'deepseek'],
  },
];

/** 多局定级赛赛程模板（6 局快速 / 12 局标准 / 0 = 无限模式） */
export interface RatingFixture {
  mode: 0 | 1;
  map: string;
  opps: string[];
  label: string;
}

export const SOLO_MAPS = ['twinlakes', 'yinyang', 'icefloes', 'ruins', 'skybridge', 'stonecircle'] as const;

export function buildRatingSchedule(count: 6 | 12 | 0, includeTeamFinals: boolean): RatingFixture[] {
  const f = (id: string) => includeTeamFinals ? `${id}-t` : id;
  const base6: RatingFixture[] = [
    { mode: 0, map: 'twinlakes',   opps: ['doubao'], label: '热身 1v1 · 豆包（-251）' },
    { mode: 0, map: 'yinyang',     opps: ['kimi'], label: '进阶 1v1 · Kimi（-84）' },
    { mode: 0, map: 'ruins',       opps: [f('glm')], label: includeTeamFinals ? '中坚 1v1 · GLM 终版（+62）' : '中坚 1v1 · GLM（-9）' },
    { mode: 1, map: 'stonecircle', opps: ['qwen', 'deepseek', 'grok'], label: '四人混战 · 千问 / DeepSeek / Grok' },
    { mode: 0, map: 'skybridge',   opps: [f('fable')], label: includeTeamFinals ? '强敌 1v1 · Fable 终版（+110）' : '强敌 1v1 · Fable（+132）' },
    { mode: 0, map: 'icefloes',    opps: [f('astra')], label: includeTeamFinals ? '终极试炼 · Astra 终版（+296）' : '终极试炼 · Astra（+306）' },
  ];
  if (count === 0) return [base6[0]];
  if (count === 6) return base6;
  const extra6: RatingFixture[] = [
    { mode: 0, map: 'icefloes',    opps: ['mimo'], label: '单挑 7 · MiMo（-167）' },
    { mode: 0, map: 'stonecircle', opps: ['deepseek'], label: '单挑 8 · DeepSeek（-30）' },
    { mode: 1, map: 'twinlakes',   opps: ['mimo', 'kimi', 'hydrogen'], label: '四人混战 9 · MiMo / Kimi / Hydrogen' },
    { mode: 0, map: 'ruins',       opps: [f('carbon')], label: includeTeamFinals ? '单挑 10 · Carbon 终版（+105）' : '单挑 10 · Carbon（+51）' },
    { mode: 0, map: 'yinyang',     opps: [f('opus')], label: includeTeamFinals ? '单挑 11 · Opus 终版（+86）' : '单挑 11 · Opus（+76）' },
    { mode: 1, map: 'skybridge',   opps: [f('sol'), f('opus'), f('fable')], label: '压轴四人混战 · Sol / Opus / Fable' },
  ];
  return [...base6, ...extra6];
}

/** 无限模式自适应匹配：根据当前实时 BT 实力分挑选最合适的下一局（每 4 局穿插一场 4 人混战） */
export function pickEndlessFixture(
  idx: number,
  estRating: number | null,
  includeTeamFinals: boolean,
  history: RatingFixture[],
  data: Ep11Data,
): RatingFixture {
  const fmt = (v: number) => (v > 0 ? '+' : '') + v.toFixed(0);
  const recentMaps = new Set(history.slice(-2).map(h => h.map));
  const mapPool = SOLO_MAPS.filter(m => !recentMaps.has(m));
  const map = mapPool[idx % mapPool.length] || SOLO_MAPS[idx % SOLO_MAPS.length];

  type Cand = { id: string; name: string; rating: number };
  const cands: Cand[] = [];
  for (const s of data.standings11) {
    cands.push({ id: s.id, name: s.short, rating: s.rating });
    if (includeTeamFinals && s.retest) {
      cands.push({ id: `${s.id}-t`, name: `${s.short} 终版`, rating: s.retest.final });
    }
  }
  const target = (estRating ?? -80) + (idx % 2 === 1 ? 35 : -35);

  // 每 4 局打一场 4 人混战（第 4、8、12... 局）
  if (idx % 4 === 3) {
    const sorted = cands.slice().sort((a, b) => Math.abs(a.rating - target) - Math.abs(b.rating - target));
    const chosen: Cand[] = [];
    const usedBase = new Set<string>();
    for (const c of sorted) {
      const base = c.id.replace(/-t$/, '');
      if (usedBase.has(base)) continue;
      usedBase.add(base);
      chosen.push(c);
      if (chosen.length === 3) break;
    }
    return {
      mode: 1,
      map,
      opps: chosen.map(c => c.id),
      label: `匹配混战 · ${chosen.map(c => c.name).join(' / ')}`,
    };
  }

  const recentOpps = new Set(history.slice(-2).filter(h => h.mode === 0).map(h => h.opps[0]));
  const avail = cands.filter(c => !recentOpps.has(c.id));
  const pool = (avail.length ? avail : cands).slice().sort((a, b) => Math.abs(a.rating - target) - Math.abs(b.rating - target));
  const pick = pool[Math.min(pool.length - 1, idx % 3 === 2 ? 1 : 0)];
  return {
    mode: 0,
    map,
    opps: [pick.id],
    label: `匹配 1v1 · ${pick.name}（${fmt(pick.rating)}）`,
  };
}
