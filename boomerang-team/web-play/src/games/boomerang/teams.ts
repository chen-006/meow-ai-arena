// 队伍显示口径（用户 11-24 定案）：Astra 队永远在左、蓝色 #2E8CFF；Fable 队永远在右、红色 #FF5B4D（与换边无关）。
// 回放里的 team 0/1 只是“开局在地图哪一边”，第 2/4/6 回合两队换边后 team 0 会变成 Fable 队，
// 所以 HUD、3D 底圈、旗帜、占点颜色一律先过 teamSlot() 换算成“显示位”（0 = 左 / 蓝，1 = 右 / 红）。
// 不含 Astra / Fable 的对局（第 11 集 4v4 用陪练等）：原始 0 号队在左（蓝队）、1 号队在右（红队）。
import type { ParsedReplay } from './types';

export const TEAM_HEX = ['#2E8CFF', '#FF5B4D'] as const;

const PRETTY: Record<string, string> = {
  astra: 'Astra', sol: 'Sol', hydrogen: 'Hydrogen', grok: 'Grok', kimi: 'Kimi',
  fable: 'Fable', opus: 'Opus', carbon: 'Carbon', glm: 'GLM', deepseek: 'DeepSeek',
  qwen: '千问', mimo: 'MiMo', doubao: '豆包', weak: '弱陪练', mid: '中陪练', strong: '强陪练',
};

/** 回放文件里的选手名是小写 id（astra / glm …），显示时换成正式写法 */
export function displayName(name: string): string {
  const k = name.toLowerCase().replace(/-t$/, '');
  return PRETTY[k] || name;
}

const keyOf = (p: { name: string; botId?: string }) => (p.botId || p.name || '').toLowerCase();

const slotCache = new WeakMap<ParsedReplay, [number, number]>();

/** [team0 的显示位, team1 的显示位] */
export function teamSlots(rp: ParsedReplay): [number, number] {
  let s = slotCache.get(rp);
  if (s) return s;
  const has = (t: number, k: string) => rp.players.some(p => p.team === t && keyOf(p).startsWith(k));
  let swap = false;
  if (has(1, 'astra') && !has(0, 'astra')) swap = true;
  else if (!has(0, 'astra') && !has(1, 'astra') && has(0, 'fable') && !has(1, 'fable')) swap = true;
  s = swap ? [1, 0] : [0, 1];
  slotCache.set(rp, s);
  return s;
}

export const teamSlot = (rp: ParsedReplay, team: number) => teamSlots(rp)[team & 1];
export const teamHex = (rp: ParsedReplay, team: number) => TEAM_HEX[teamSlot(rp, team)];

/** 原始 team 编号对应的队名（Astra 队 / Fable 队；都不含时按显示位叫蓝队 / 红队） */
export function teamName(rp: ParsedReplay, team: number): string {
  const t = team & 1;
  if (rp.players.some(p => p.team === t && keyOf(p).startsWith('astra'))) return 'Astra 队';
  if (rp.players.some(p => p.team === t && keyOf(p).startsWith('fable'))) return 'Fable 队';
  return teamSlot(rp, t) === 0 ? '蓝队' : '红队';
}

/** 显示位 slot（0 左 / 1 右）对应的原始 team 编号 */
export const teamAtSlot = (rp: ParsedReplay, slot: number) => (teamSlots(rp)[0] === slot ? 0 : 1);
