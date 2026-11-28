// 第 11–12 期《回旋镖大作战（OA 争霸）》对局页：
//   - 画面：正片 1:1 的 3D（view.ts：13 张图实导场景、体素小人、特效、正片机位与顶栏），60 fps 连续插值
//   - 真人对战：第 11 集（1v1 / 4 人混战 / 4v4，10 张图）、第 12 集（5v5 歼灭 / 占点 / 刺王）；开局浮层、终局战报、实时技能栏
//   - 多局定级：由弱到强连打几局，按比赛同款 Bradley–Terry 实力分插进总榜
//   - 程序互打：26 份 WASM 程序任意编排，含 4v4 和赛后实验阵容
//   - 精选回放：第 12 集名场面 + 第 11 集代表局（含 4v4），带事件刻度的进度条、分享链接重算
import './arena.css';
import { ALL_EPISODES } from '../../common/episodes';
import { arenaCrumbsHtml, THEME_BTN } from '../../common/shell';
import { asset, esc } from '../../common/ui';
import { copyText, hashArgs, toast } from '../../common/arena-kit';
import {
  botById, buildRatingSchedule, DUEL_PRESETS, loadCuratedReplayText, loadEp11, loadMapText, pickEndlessFixture, SQUAD_MATES, SQUAD_PRESETS, TEAM_PRESETS, TIERS,
  VOX_COLOR, VOX_KEYS, YOU_COLOR, avatarForName, charKeyForName, type BotMatchPreset, type RatingFixture,
} from './data';
import { BoomerangEngine, parseReplayText, parseStateLine, recomputeSharedReplay } from './engine';
import { BoomerangMatch, type SeatSpec } from './match';
import { BoomerangHuman } from './human';
import { BoomerangView, INTRO, type ViewSeat } from './view';
import { BoomerangViewer2D } from './viewer2d';
import { estimateHumanRating, ffaScores, type HumanMatchRecord } from './bt';
import { displayName, TEAM_HEX, teamAtSlot, teamName, teamSlots } from './teams';
import { sfx } from './sfx';
import type { Ep11Data, GameMode, MapMeta, ParsedReplay } from './types';

type Tab = 'play' | 'rating' | 'watch' | 'replay';
type RoundPlan = { r: number; astraPlan: string; fablePlan: string };

interface RatingMatchDone { fixture: RatingFixture; seed: number; humanRank: number; humanKills: number; score: number; records: HumanMatchRecord[]; shareCode?: string }
interface SavedRatingSession { ts: number; count: 6 | 12 | 0; includeTeam: boolean; delayMode: boolean; rating: number; ci95: [number, number]; games: number; points: number; matches: RatingMatchDone[] }
const LS_RATING_KEY = 'ep11_boomerang_rating_v1';
const LS_PREF = 'ep11_boomerang_pref_v2';

const svg = (d: string) => `<svg class="bm-ico" viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${d}</svg>`;
const ICO = {
  play: svg('<path d="M5 3.5v9l7-4.5z" fill="currentColor" stroke="none"/>'),
  pause: svg('<path d="M5.5 3.5v9M10.5 3.5v9"/>'),
  prev: svg('<path d="M10 4 6 8l4 4"/>'),
  next: svg('<path d="m6 4 4 4-4 4"/>'),
  ghost: svg('<path d="M3.5 13V7.5a4.5 4.5 0 0 1 9 0V13l-1.5-1-1.5 1-1.5-1-1.5 1-1.5-1z"/><circle cx="6.5" cy="7.5" r=".6" fill="currentColor"/><circle cx="9.5" cy="7.5" r=".6" fill="currentColor"/>'),
  eye: svg('<path d="M1.5 8s2.5-4.5 6.5-4.5S14.5 8 14.5 8 12 12.5 8 12.5 1.5 8 1.5 8z"/><circle cx="8" cy="8" r="2"/>'),
  link: svg('<path d="M7 9a3 3 0 0 0 4.2 0l2-2a3 3 0 0 0-4.2-4.2L8 3.8"/><path d="M9 7a3 3 0 0 0-4.2 0l-2 2A3 3 0 0 0 7 13.2l1-1"/>'),
  redo: svg('<path d="M3 8a5 5 0 1 0 1.5-3.5"/><path d="M3 2.5v3h3"/>'),
  sound: svg('<path d="M2.5 6v4h2.5l3.5 3V3L5 6z"/><path d="M11 5.5a3.5 3.5 0 0 1 0 5M12.8 3.8a6 6 0 0 1 0 8.4"/>'),
  mute: svg('<path d="M2.5 6v4h2.5l3.5 3V3L5 6z"/><path d="m11 6 3.5 4M14.5 6 11 10"/>'),
  home: svg('<path d="M2.5 7.5 8 3l5.5 4.5M4 6.5V13h8V6.5"/>'),
  follow: svg('<circle cx="8" cy="8" r="2.2"/><path d="M8 1.5v2.5M8 12v2.5M1.5 8H4M12 8h2.5"/>'),
  map2d: svg('<path d="m2 4 4-1.5 4 1.5 4-1.5v9.5l-4 1.5-4-1.5-4 1.5z"/><path d="M6 2.5v9.5M10 4v9.5"/>'),
  film: svg('<rect x="2" y="3" width="12" height="10" rx="1.5"/><path d="M5 3v10M11 3v10M2 6.3h3M2 9.7h3M11 6.3h3M11 9.7h3"/>'),
  swap: svg('<path d="M3 5.5h9.5L10 3M13 10.5H3.5L6 13"/>'),
  fsEnter: svg('<path d="M2.5 6V2.5H6M10 2.5h3.5V6M13.5 10v3.5H10M6 13.5H2.5V10"/>'),
  fsExit: svg('<path d="M6 2.5V6H2.5M10 2.5V6h3.5M10 13.5V10h3.5M6 13.5V10H2.5"/>'),
  shield: svg('<path d="M8 2 13 4v4c0 3.2-2.2 5.2-5 6-2.8-.8-5-2.8-5-6V4z"/>'),
};
const MODE_NAME: Record<number, string> = { 0: '1v1 单挑', 1: '4 人混战', 2: '4v4 团队乱斗', 3: '5v5 歼灭', 4: '5v5 占点', 5: '5v5 刺王' };
const MAP_OF_MODE: Record<number, string> = { 3: 'caldera', 4: 'piers', 5: 'keeps' };
const TEAM_A = ['astra-t', 'sol-t', 'hydrogen-t', 'grok-t', 'kimi-t'];
const TEAM_F = ['fable-t', 'opus-t', 'carbon-t', 'glm-t', 'deepseek-t'];
/** 三档官方陪练（新手先拿它们热身） */
const SPARS = ['weak', 'mid', 'strong'];
const SPAR_NOTE: Record<string, string> = { weak: '慢、好躲，先熟悉手感', mid: '会扔会躲，练接镖和走位', strong: '比赛里的强参照，练实战' };
const HOW = 'WASD 移动 · 鼠标瞄准 · 左键扔镖 · 右键 / F 近战 · 空格冲刺 · E 召回';

function detectWebGL2(): boolean {
  try { return !!document.createElement('canvas').getContext('webgl2'); } catch { return false; }
}
const loadPref = (): Record<string, any> => { try { return JSON.parse(localStorage.getItem(LS_PREF) || '{}'); } catch { return {}; } };
const savePref = (p: Record<string, any>) => { try { localStorage.setItem(LS_PREF, JSON.stringify(p)); } catch { /* 无痕模式 */ } };

export async function startBoomerangArena(initialMode: 'play' | 'watch' | 'replay') {
  const args = hashArgs();
  const data = await loadEp11();
  const ep = ALL_EPISODES.find((e) => e.id === 'ep11') || { id: 'ep11', num: '第 11–12 期', title: '回旋镖大作战' };
  const mapMeta = (id: string): MapMeta => data.maps.find((m) => m.id === id) || data.maps[0];
  const thumbUrl = (id: string) => asset('img/ep11/maps/' + id + '.webp');
  const thumb = !!args.thumb;

  let tab: Tab = (args.tab as Tab) || (initialMode === 'replay' ? 'replay' : initialMode);
  if (args.r || args.f) tab = 'replay';

  // ---------------------------------------------------------------- 骨架
  document.body.className = 'mw-page bm-page' + (thumb ? ' bm-thumb' : '');
  document.body.innerHTML = `
    <header class="bm-head">
      <div class="arena-crumbs">${arenaCrumbsHtml(ep as any, '对战')}</div>
      <nav class="bm-tabs" role="tablist" id="bm-tabs">
        <button role="tab" data-tab="play">真人对战</button>
        <button role="tab" data-tab="rating">多局定级</button>
        <button role="tab" data-tab="watch">程序互打</button>
        <button role="tab" data-tab="replay">精选回放</button>
      </nav>
      ${THEME_BTN}
    </header>
    <main class="bm-main">
      <section class="bm-stage-col" id="stage-col">
        <div class="bm-stage" id="stage" tabindex="-1">
          <div class="bm-view" id="view-host"></div>
          <div class="bm-reticle" id="reticle" hidden><i></i><b></b></div>
          <div class="bm-corner" id="corner">
            <button type="button" class="bm-icon-btn" id="c-sound" title="音效开关">${ICO.sound}</button>
            <button type="button" class="bm-icon-btn" id="c-follow" title="镜头跟随你" hidden>${ICO.follow}</button>
            <button type="button" class="bm-icon-btn" id="c-home" title="回到全景（R）">${ICO.home}</button>
            <button type="button" class="bm-icon-btn" id="c-fs" title="全屏" aria-pressed="false">${ICO.fsEnter}</button>
          </div>
          <div class="bm-ov" id="ov-start" hidden></div>
          <div class="bm-ov bm-ov-end" id="ov-end" hidden></div>
          <div class="bm-status" id="status" hidden><span class="bm-spin"></span><span id="status-txt">载入中…</span></div>
          <div class="bm-touch" id="touch" hidden>
            <div class="bm-joy" id="joy"><div class="bm-joy-knob" id="joy-knob"></div></div>
            <div class="bm-tbtns">
              <button type="button" data-act="1" class="t-throw"><b>扔镖</b></button>
              <button type="button" data-act="2" class="t-melee"><b id="t-melee-l">近战</b></button>
              <button type="button" data-act="3" class="t-dash"><b>冲刺</b></button>
              <button type="button" data-act="4" class="t-recall"><b>召回</b></button>
            </div>
          </div>
        </div>
        <div class="bm-bar" id="bar"></div>
      </section>
      <aside class="bm-side" id="side"></aside>
    </main>`;
  const $ = <T extends HTMLElement = HTMLElement>(id: string) => document.getElementById(id) as T;
  const stageColEl = $('stage-col'), stageEl = $('stage'), hostEl = $('view-host'), sideEl = $('side'), barEl = $('bar');
  const ovStart = $('ov-start'), ovEnd = $('ov-end'), statusEl = $('status'), statusTxt = $('status-txt');
  const reticle = $('reticle'), touchEl = $('touch');

  // ---------------------------------------------------------------- 画面
  const gl = detectWebGL2();
  const view = gl ? new BoomerangView(hostEl) : null;
  let v2d: BoomerangViewer2D | null = gl ? null : new BoomerangViewer2D(hostEl);
  let use2d = !gl;
  (window as any).__ep11View = view;
  const human = new BoomerangHuman(stageEl, { screenToWorld: (x, y) => (use2d ? v2d?.screenToWorld(x, y) : view?.screenToWorld(x, y)) || null });

  const focusStage = () => {
    try { window.focus(); } catch { /* ignore */ }
    stageEl.focus({ preventScroll: true });
  };
  let lastPointer: { x: number; y: number } | null = null;
  const syncReticle = () => {
    const on = S.kind === 'live' && S.humanIdx >= 0 && human.active && !!lastPointer;
    if (!on || !lastPointer) { reticle.hidden = true; stageEl.classList.remove('aiming'); return; }
    const r = stageEl.getBoundingClientRect();
    const inside = lastPointer.x >= r.left && lastPointer.x <= r.right && lastPointer.y >= r.top && lastPointer.y <= r.bottom;
    reticle.hidden = !inside;
    stageEl.classList.toggle('aiming', inside);
    if (inside) reticle.style.transform = `translate(${lastPointer.x - r.left}px, ${lastPointer.y - r.top}px)`;
  };
  const trackPointer = (e: PointerEvent) => {
    if (e.pointerType === 'touch') return;
    lastPointer = { x: e.clientX, y: e.clientY };
  };
  window.addEventListener('pointermove', trackPointer, { passive: true });
  window.addEventListener('pointerdown', trackPointer, { passive: true });

  /** 构建防坠安全格判定：非边界外、非坑/已塌陷格，并带 0.12m 容差防止半只脚探出边缘 */
  const makeSafeChecker = (P: ParsedReplay) => {
    const { nx, ny, cell, grid, wave } = P;
    const cellOk = (x: number, y: number, fallen: number) => {
      const gx = Math.floor(x / cell), gy = Math.floor(y / cell);
      if (gx < 0 || gy < 0 || gx >= nx || gy >= ny) return false;
      const idx = gy * nx + gx;
      if (grid[idx] === 2) return false;
      const wv = wave[idx] ?? -1;
      if (wv >= 0 && wv < fallen) return false;
      return true;
    };
    const pad = 0.12;
    return (x: number, y: number, frame: { fallen: number }) => {
      const f = frame.fallen;
      return cellOk(x, y, f)
        && cellOk(x + pad, y, f) && cellOk(x - pad, y, f)
        && cellOk(x, y + pad, f) && cellOk(x, y - pad, f);
    };
  };

  let enginePromise: Promise<BoomerangEngine> | null = null;
  const getEngine = () => (enginePromise ??= BoomerangEngine.load());
  const setStatus = (t: string) => { statusEl.hidden = !t; statusTxt.textContent = t; };

  // 座位 -> 画面用的造型、主色、名字、头像
  const botSeat = (id: string, name?: string): SeatSpec => {
    const b = botById(data, id);
    return { bot: id, name: name || id.replace(/-t$/, ''), charKey: b?.charKey || charKeyForName(id), avatar: b?.avatar || avatarForName(id) };
  };
  const humanSeat = (key: string): SeatSpec => ({ human: true, name: '你', charKey: key, color: YOU_COLOR, avatar: 'ep11/human' });
  const viewSeats = (seats: SeatSpec[]): ViewSeat[] => {
    const base = seats.map((s) => (s.human ? '你' : displayName(s.bot || s.name)));
    const cnt: Record<string, number> = {};
    for (const b of base) cnt[b] = (cnt[b] || 0) + 1;
    const ver = (s: SeatSpec) => (s.bot?.endsWith('-t') ? '终版' : s.bot && ['weak', 'mid', 'strong'].includes(s.bot) ? '' : '海选');
    const seen: Record<string, number> = {};
    return seats.map((s, k) => {
      let label = base[k];
      if (cnt[label] > 1) {
        const byVer = seats.filter((x, j) => base[j] === label && ver(x) === ver(s)).length === 1;
        label = byVer && ver(s) ? label + '·' + ver(s) : label + ' ' + (seen[label] = (seen[label] || 0) + 1);
      }
      const key = s.charKey || 'astra';
      return { key, color: s.human ? YOU_COLOR : VOX_COLOR[key] || '#888', label, avatar: asset('img/avatars/' + (s.avatar || avatarForName(s.name)) + '.webp') };
    });
  };
  const seatsFromReplay = (P: ParsedReplay, humanIdx = -1): SeatSpec[] => P.players.map((p, k) => {
    if (k === humanIdx) return humanSeat(p.charKey || 'opus');
    const id = p.botId || p.name.toLowerCase().replace(/\d+$/, '');
    const b = botById(data, id) || botById(data, id.replace(/-t$/, ''));
    return { bot: b?.id || id, name: p.name, charKey: b?.charKey || charKeyForName(p.name), avatar: b?.avatar || avatarForName(p.name) };
  });
  const viewOpts = (P: ParsedReplay, humanIdx: number) => {
    const slots = teamSlots(P);
    return {
      human: humanIdx, teamHex: [TEAM_HEX[0], TEAM_HEX[1]] as [string, string], slotOfTeam: slots,
      teamNames: [teamName(P, teamAtSlot(P, 0)), teamName(P, teamAtSlot(P, 1))] as [string, string],
    };
  };

  // ---------------------------------------------------------------- 会话
  type Session = { kind: 'none' | 'preview' | 'live' | 'replay'; P?: ParsedReplay; seats?: SeatSpec[]; match?: BoomerangMatch; humanIdx: number;
    mode?: GameMode; map?: string; seed?: number; onEnd?: (P: ParsedReplay, m: BoomerangMatch) => void; shareCode?: string; restart?: () => void; title?: string };
  let S: Session = { kind: 'none', humanIdx: -1 };
  let loadToken = 0;

  const stopLive = () => {
    if (S.match) { S.match.stop(); S.match = undefined; }
    human.setEnabled(false);
    view?.setLive(null);
    touchEl.hidden = true;
    reticle.hidden = true;
    stageEl.classList.remove('aiming');
  };

  /** 把任意一份回放（完整的、或只有第 0 步的预览）放进画面 */
  async function showP(P: ParsedReplay, seats: SeatSpec[], humanIdx: number, startGt?: number): Promise<boolean> {
    const tok = ++loadToken;
    if (view && !use2d) {
      setStatus('正在载入 3D 场景…');
      try {
        const ok = await view.load(P, viewSeats(seats), { ...viewOpts(P, humanIdx), startGt });
        if (!ok || tok !== loadToken) return false;
      } catch (e: any) {
        setStatus('3D 场景载入失败：' + (e?.message || e));
        return false;
      }
      setStatus('');
    }
    if (v2d) v2d.update(P, P.frames[Math.max(0, Math.min(P.frames.length - 1, Math.floor(startGt ?? 0)))], { showGhost: false, humanDelayView: false });
    return true;
  }

  // ---- 预览：开局前把这张图、这些人摆出来（引擎开一局取第 0 步）
  async function preview(mode: GameMode, map: string, seats: SeatSpec[], roundPlan?: RoundPlan) {
    stopLive();
    const eng = await getEngine();
    const mapText = await loadMapText(map);
    const form = resolveRoundFormation(roundPlan, map);
    const ng = eng.newGame({ mode, seed: 7, mapText, names: seats.map((s) => s.name), kings: form.kings, spawns: form.spawns, plans: form.plans });
    const P = parseReplayText(ng.headerText);
    const tr = eng.turn();
    P.frames.push(parseStateLine(tr.sLine, P.players.length, P.mode, P.kings));
    P.players.forEach((p, k) => { p.botId = seats[k]?.bot; p.charKey = seats[k]?.charKey || p.charKey; });
    const hi = seats.findIndex((s) => s.human);
    S = { kind: 'preview', P, seats, humanIdx: hi, mode, map };
    view?.setInteractive(true);
    await showP(P, seats, hi, 0);
  }

  // ---- 实时对局
  async function startLive(o: { mode: GameMode; map: string; seed: number; seats: SeatSpec[]; roundPlan?: RoundPlan; speed?: number; delay?: boolean;
    title?: string; onEnd?: (P: ParsedReplay, m: BoomerangMatch) => void; restart?: () => void }) {
    stopLive();
    hideOverlays();
    sfx.resume();
    const eng = await getEngine();
    const mapText = await loadMapText(o.map);
    const form = resolveRoundFormation(o.roundPlan, o.map);
    const hi = o.seats.findIndex((s) => s.human);
    const hasHuman = hi >= 0;
    human.setEnabled(hasHuman);
    if (hasHuman) {
      if (lastPointer) human.syncPointer(lastPointer.x, lastPointer.y);
      focusStage();
    }
    const match: BoomerangMatch = new BoomerangMatch(eng, {
      mode: o.mode, mapId: o.map, mapText, seed: o.seed, seats: o.seats, spawns: form.spawns, kings: form.kings, plans: form.plans,
      budgetScale: 2, speed: hasHuman ? 1 : (o.speed ?? watchSpeed), intro: INTRO, human: hasHuman ? human : undefined,
      onStatus: setStatus,
      beforeStart: async (P) => {
        P.players.forEach((p, k) => { p.botId = o.seats[k]?.bot; p.charKey = o.seats[k]?.charKey || p.charKey; });
        human.teamOf = (k) => P.players[k]?.team ?? k;
        human.isSafePos = makeSafeChecker(P);
        await showP(P, o.seats, hi, -INTRO);
        if (S.match !== match) return;
        view?.setLive(() => match.clock.now());
        view?.setInteractive(!hasHuman);
        view?.setDelayView(!!o.delay && hasHuman);
        if (view) { view.follow = followPref(o.mode, hasHuman); view.setFollow(view.follow); }
        if (hasHuman) {
          touchEl.hidden = !touchWanted();
          if (touchWanted() || window.innerWidth <= 1040) stageEl.scrollIntoView({ block: 'start', behavior: 'smooth' });
          if (lastPointer) human.syncPointer(lastPointer.x, lastPointer.y);
          focusStage();
          syncReticle();
          sfx.play('start');
        }
        renderBar();
      },
      onEnd: (P) => {
        (window as any).__ep11LastMatch = { done: true, mode: P.mode, map: P.mapName, steps: P.end?.steps ?? P.frames.length - 1, winner: P.end?.winner ?? -1, digest: P.end?.digest ?? '', stats: match.stats };
        match.shareCode().then((c) => { if (S.match === match || S.P === P) S.shareCode = c; }).catch(() => {});
        o.onEnd?.(P, match);
        if (!view || use2d) onLiveFinished();
      },
    });
    S = { kind: 'live', P: match.replay, seats: o.seats, match, humanIdx: hi, mode: o.mode, map: o.map, seed: o.seed, onEnd: o.onEnd, restart: o.restart, title: o.title };
    (window as any).__ep11Match = match;
    renderBar();
    match.run().catch((e) => setStatus('对局出错：' + (e?.message || e)));
  }

  // ---- 回放
  async function showReplay(P: ParsedReplay, focusSec = 0, humanIdx = -1, title = '') {
    stopLive();
    hideOverlays();
    const seats = seatsFromReplay(P, humanIdx);
    S = { kind: 'replay', P, seats, humanIdx, mode: P.mode, map: P.mapName, title };
    view?.setInteractive(true);
    view?.setFollow(false);
    const ok = await showP(P, seats, humanIdx, focusSec > 0 ? focusSec * 20 : -INTRO);
    if (!ok) return;
    view?.play(true);
    (window as any).__ep11Replay = { map: P.mapName, mode: P.mode, steps: P.end?.steps ?? P.frames.length - 1, winner: P.end?.winner ?? -1, digest: P.end?.digest ?? '', frames: P.frames.length };
    renderBar();
  }

  function onLiveFinished() {
    if (S.kind !== 'live' || !S.P) return;
    human.setEnabled(false);
    touchEl.hidden = true; reticle.hidden = true; stageEl.classList.remove('aiming');
    const P = S.P, me = S.humanIdx;
    if (me >= 0) {
      const won = didWin(P, me);
      sfx.play(won === 1 ? 'win' : 'lose');
    }
    showEnd();
  }
  if (view) view.onLiveEnd = onLiveFinished;

  /** 1 胜 0.5 平 0 负（四人局按名次：第一名算胜） */
  function didWin(P: ParsedReplay, me: number) {
    const w = P.end?.winner ?? -1;
    if (P.mode >= 2) return w < 0 ? 0.5 : w === P.players[me].team ? 1 : 0;
    if (P.mode === 1) { const r = P.end?.results.find((x) => x.seat === me); return r?.rank === 1 ? 1 : 0; }
    return w < 0 ? 0.5 : w === me ? 1 : 0;
  }

  // ---------------------------------------------------------------- 浮层
  function hideOverlays() { ovStart.hidden = true; ovEnd.hidden = true; }

  function showStart(title: string, sub: string, onGo: () => void, extra = '') {
    ovStart.innerHTML = `<div class="bm-ov-card">
      <div class="bm-ov-kicker">${esc(sub)}</div>
      <h2>${esc(title)}</h2>
      <p class="bm-ov-how">${touchWanted() ? '左边摇杆移动，右边四个键：扔镖、近战、冲刺、召回' : esc(HOW)}</p>
      ${extra}
      <button type="button" class="bm-go" id="ov-go">开始对战</button>
      <div class="bm-ov-hint">${touchWanted() ? '点按钮开打' : '或按 <kbd>空格</kbd> 直接开打'}</div>
    </div>`;
    ovStart.hidden = false;
    ovEnd.hidden = true;
    startHandler = onGo;
    $('ov-go').onclick = () => { startHandler = null; onGo(); };
  }
  let startHandler: (() => void) | null = null;

  function showEnd() {
    if (!S.P) return;
    const P = S.P, me = S.humanIdx, steps = P.end?.steps ?? P.frames.length - 1;
    const secs = (steps / 20).toFixed(1);
    let head = '', cls = '', line = '';
    if (me >= 0) {
      const r = P.end?.results.find((x) => x.seat === me);
      const kills = r?.kills ?? 0;
      const w = didWin(P, me);
      if (P.mode === 1) { head = r ? `第 ${r.rank} 名` : '结束'; cls = r?.rank === 1 ? 'win' : 'loss'; }
      else { head = w === 1 ? '胜利' : w === 0.5 ? '平局' : '惜败'; cls = w === 1 ? 'win' : w === 0.5 ? 'draw' : 'loss'; }
      line = `击倒 ${kills} · 用时 ${secs} 秒` + (P.mode === 4 ? ` · 比分 ${scoreText(P)}` : '');
    } else {
      const w = P.end?.winner ?? -1;
      head = w < 0 ? '平局' : P.mode >= 2 ? teamName(P, w) + ' 获胜' : displayName(P.players[w]?.name || '') + ' 获胜';
      cls = 'draw';
      line = `用时 ${secs} 秒` + (P.mode === 4 ? ` · 比分 ${scoreText(P)}` : '');
    }
    ovEnd.innerHTML = `<div class="bm-ov-card ${cls}">
      <div class="bm-ov-kicker">${esc(S.title || MODE_NAME[P.mode] + ' · ' + mapMeta(P.mapName).name)}</div>
      <h2 class="bm-result">${esc(head)}</h2>
      <p class="bm-ov-line">${esc(line)}</p>
      <div class="bm-ov-btns">
        <button type="button" class="bm-go" id="e-again">${ICO.redo}${S.kind === 'live' && rating.active && rating.runningIdx < 0 && (rating.count === 0 || rating.done.length < rating.schedule.length) ? '下一局' : '再来一局'}</button>
        <button type="button" class="bm-ghost-btn" id="e-change">${ICO.swap}换对手 / 地图</button>
        <button type="button" class="bm-ghost-btn" id="e-replay">${ICO.film}3D 复盘本局</button>
        <button type="button" class="bm-ghost-btn" id="e-share">${ICO.link}分享回放</button>
      </div>
    </div>`;
    ovEnd.hidden = false;
    $('e-again').onclick = () => { hideOverlays(); S.restart ? S.restart() : null; };
    $('e-change').onclick = () => { hideOverlays(); sideEl.scrollIntoView({ block: 'start', behavior: 'smooth' }); renderSide(true); };
    $('e-replay').onclick = () => { const P2 = S.P!; const t = S.title; showReplay(P2, 0, S.humanIdx, t); };
    $('e-share').onclick = () => shareCurrent();
  }
  const scoreText = (P: ParsedReplay) => {
    const f = P.frames[P.frames.length - 1], sl = teamSlots(P);
    if (!f?.control) return '';
    const raw0 = sl[0] === 0 ? 0 : 1;
    return `${Math.floor(f.control.score[raw0] / 20)} : ${Math.floor(f.control.score[1 - raw0] / 20)}`;
  };

  async function shareCurrent() {
    try {
      let code = S.shareCode;
      if (!code && S.match) code = await S.match.shareCode();
      const url = code ? new URL(`replay.html#r=${code}`, location.href).href : location.href;
      (window as any).__shareUrl = url;
      await copyText(url);
      toast(code ? `回放链接已复制（${code.length} 字符），打开就能重算这一局` : '已复制当前页面链接');
    } catch (e: any) { toast('生成链接失败：' + (e?.message || e)); }
  }

  // ---------------------------------------------------------------- 底部条：实时技能栏 / 回放进度条
  let barKind = '';
  let speedSel = 1;
  let watchSpeed = 1;
  const touchWanted = () => matchMedia('(pointer: coarse)').matches || window.innerWidth <= 760;
  const followPref = (mode: GameMode, hasHuman: boolean) => hasHuman && mode >= 2 && window.innerWidth <= 760;

  function renderBar() {
    const live = S.kind === 'live', hum = live && S.humanIdx >= 0;
    const kind = hum || (S.kind === 'preview' && (tab === 'play' || tab === 'rating')) ? 'skills' : 'transport';
    if (kind === barKind && barEl.childElementCount) { syncBarToggles(); return; }
    barKind = kind;
    if (kind === 'skills') {
      barEl.className = 'bm-bar skills';
      barEl.innerHTML = `
        <div class="bm-skills">
          <div class="bm-skill" id="sk-throw"><kbd>左键</kbd><b>扔镖</b><span>持镖就绪</span></div>
          <div class="bm-skill" id="sk-melee"><kbd>右键 / F</kbd><b id="sk-melee-n">挥砍</b><span>就绪</span></div>
          <div class="bm-skill" id="sk-dash"><kbd>空格</kbd><b>冲刺</b><span>就绪</span><i class="cd"></i></div>
          <div class="bm-skill" id="sk-recall"><kbd>E</kbd><b>召回</b><span>—</span></div>
        </div>
        <div class="bm-bar-tools">
          <button type="button" class="bm-chip" id="b-guard" aria-pressed="true" title="普通走路靠近坑/塌陷边缘时自动沿边滑行（冲刺越坑不受限）">${ICO.shield}防坠保护</button>
          <button type="button" class="bm-chip" id="b-ghost" aria-pressed="false" title="显示 0.2 秒（4 步）前 AI 看到的位置">${ICO.ghost}0.2s 残影</button>
          <button type="button" class="bm-chip" id="b-delay" aria-pressed="false" title="你也和 AI 一样晚 0.2 秒看到对手">${ICO.eye}AI 延迟挑战</button>
          <button type="button" class="bm-chip" id="b-restart">${ICO.redo}重开</button>
        </div>`;
      $('b-restart').onclick = () => { if (S.restart) S.restart(); };
      $('b-guard').onclick = () => { human.edgeGuard = !human.edgeGuard; persist(); syncBarToggles(); if (human.active) focusStage(); };
      $('b-delay').onclick = () => { const on = !view?.delayView; view?.setDelayView(on); syncBarToggles(); if (human.active) focusStage(); };
    } else {
      barEl.className = 'bm-bar transport';
      barEl.innerHTML = `
        <button type="button" class="bm-chip icon" id="b-play" title="播放 / 暂停（空格）">${ICO.pause}</button>
        <button type="button" class="bm-chip icon" id="b-prev" title="后退一步（←）">${ICO.prev}</button>
        <button type="button" class="bm-chip icon" id="b-next" title="前进一步（→）">${ICO.next}</button>
        <div class="bm-scrub"><div class="bm-ticks" id="ticks"></div><input type="range" id="scrub" min="0" max="1000" step="1" value="0" aria-label="进度"></div>
        <span class="bm-tlab" id="tlab">0.0 s</span>
        <div class="bm-seg" id="b-speed">${[0.5, 1, 2, 4].map((v) => `<button type="button" data-sp="${v}">${v}×</button>`).join('')}${live ? '<button type="button" data-sp="0">极速</button>' : ''}</div>
        <button type="button" class="bm-chip" id="b-ghost" aria-pressed="false" title="显示 0.2 秒（4 步）前 AI 看到的位置">${ICO.ghost}0.2s 残影</button>
        <button type="button" class="bm-chip" id="b-share">${ICO.link}分享</button>
        <button type="button" class="bm-chip icon" id="b-2d" aria-pressed="false" title="2D 俯视">${ICO.map2d}</button>`;
      $('b-play').onclick = () => togglePlay();
      $('b-prev').onclick = () => stepBy(-1);
      $('b-next').onclick = () => stepBy(1);
      $('b-share').onclick = () => shareCurrent();
      $('b-2d').onclick = () => toggle2d();
      const sc = $<HTMLInputElement>('scrub');
      sc.oninput = () => { if (!view || S.kind === 'live') return; view.play(false); view.seek(view.minT + (+sc.value / 1000) * (view.maxT - view.minT)); };
      barEl.querySelectorAll<HTMLButtonElement>('[data-sp]').forEach((b) => b.onclick = () => {
        const v = +b.dataset.sp!;
        if (S.kind === 'live' && S.match) { watchSpeed = v; S.match.setSpeed(v); } else { speedSel = v || 1; if (view) view.speed = speedSel; }
        syncBarToggles();
      });
    }
    $('b-ghost').onclick = () => { const on = !view?.ghost; view?.setGhost(on); syncBarToggles(); if (human.active) focusStage(); };
    syncBarToggles();
    ticksFor = null;
  }
  const isFs = () => !!(document.fullscreenElement || (document as any).webkitFullscreenElement);
  function syncBarToggles() {
    const gd = document.getElementById('b-guard'); if (gd) gd.setAttribute('aria-pressed', String(human.edgeGuard));
    const g = document.getElementById('b-ghost'); if (g) g.setAttribute('aria-pressed', String(!!view?.ghost));
    const d = document.getElementById('b-delay'); if (d) d.setAttribute('aria-pressed', String(!!view?.delayView));
    const t = document.getElementById('b-2d'); if (t) t.setAttribute('aria-pressed', String(use2d));
    const sp = S.kind === 'live' ? (S.match?.clock.fast ? 0 : watchSpeed) : speedSel;
    barEl.querySelectorAll<HTMLButtonElement>('[data-sp]').forEach((b) => b.setAttribute('aria-pressed', String(+b.dataset.sp! === sp)));
    const snd = $('c-sound'); snd.innerHTML = sfx.on ? ICO.sound : ICO.mute; snd.setAttribute('aria-pressed', String(sfx.on)); snd.title = sfx.on ? '音效：开（点一下静音）' : '音效：关';
    const fl = $('c-follow'); fl.hidden = !(S.kind === 'live' && S.humanIdx >= 0 && (S.mode ?? 0) >= 2); fl.setAttribute('aria-pressed', String(!!view?.follow));
    const fs = $('c-fs'); const fOn = isFs(); fs.innerHTML = fOn ? ICO.fsExit : ICO.fsEnter; fs.setAttribute('aria-pressed', String(fOn)); fs.title = fOn ? '退出全屏（Esc）' : '全屏';
  }
  function togglePlay() {
    if (!view) return;
    if (S.kind === 'live' && S.match) { S.match.paused = !S.match.paused; return; }
    view.play(!view.playing);
  }
  function stepBy(d: number) {
    if (!view) return;
    if (S.kind === 'live' && S.match) { S.match.paused = true; if (d > 0) S.match.stepOnce(); return; }
    view.stepBy(d);
  }
  function toggle2d() {
    if (!gl) return;
    use2d = !use2d;
    if (use2d && !v2d) v2d = new BoomerangViewer2D(hostEl);
    hostEl.classList.toggle('show2d', use2d);
    syncBarToggles();
  }

  // 进度条上的事件刻度：出局红、挡镖 / 镖撞镖黄、塌陷灰
  let ticksFor: ParsedReplay | null = null;
  function drawTicks() {
    const el = document.getElementById('ticks');
    if (!el || !view || !S.P || ticksFor === S.P || !view.rp?.over) return;
    ticksFor = S.P;
    const span = view.maxT - view.minT, pos = (t: number) => ((t - view.minT) / span * 100).toFixed(2) + '%';
    let html = '';
    for (const f of S.P.frames) for (const e of f.events) {
      const c = e.type === 'DEATH' ? 'k' : (e.type === 'CLASH' || e.type === 'BLOCK') ? 'c' : e.type === 'COLLAPSE' ? 'w' : e.type === 'CAPTURE' || e.type === 'LIFE' ? 'g' : '';
      if (c) html += `<i class="${c}" style="left:${pos(e.t + 1)}"></i>`;
    }
    el.innerHTML = html;
  }

  // 每帧：进度条、技能栏、准星
  let lastSkill = '';
  function onFrame(gt: number) {
    if (use2d && v2d && S.P) v2d.update(S.P, S.P.frames[Math.max(0, Math.min(S.P.frames.length - 1, Math.floor(gt)))], { showGhost: !!view?.ghost, humanDelayView: !!view?.delayView, aimWorld: human.getAimWorld() });
    if (!view) return;
    if (barKind === 'transport') {
      const sc = document.getElementById('scrub') as HTMLInputElement | null;
      if (sc && document.activeElement !== sc) sc.value = String(Math.round((gt - view.minT) / Math.max(1, view.maxT - view.minT) * 1000));
      const tl = document.getElementById('tlab');
      if (tl) tl.textContent = gt < 0 ? '入场' : `${(Math.min(gt, view.rp?.T ?? gt) / 20).toFixed(1)} s`;
      const pb = document.getElementById('b-play');
      const playing = S.kind === 'live' ? !S.match?.paused : view.playing;
      if (pb && pb.dataset.on !== String(playing)) { pb.dataset.on = String(playing); pb.innerHTML = playing ? ICO.pause : ICO.play; }
      drawTicks();
    } else if (barKind === 'skills') updateSkills();
    if (S.kind === 'live' && S.humanIdx >= 0 && human.hasMouse()) {
      human.refreshAim();
      view.setAim(human.getAimWorld());
    } else view.setAim(null);
  }
  if (view) view.onFrame = onFrame;

  function updateSkills() {
    const hs = view?.humanState();
    if (!hs) return;
    const P = S.P!;
    const dead = !hs.alive;
    const dartTxt = dead ? '—' : hs.dart === 'ready' ? '持镖就绪' : hs.dart === 'out' ? '飞行中' : hs.dart === 'ground' ? '掉在地上' : '—';
    const busy = [1, 2, 3, 4, 5, 6, 8].includes(hs.st);
    const meleeName = hs.dart === 'ready' ? '挥砍' : '飞踢';
    const meleeTxt = dead ? '—' : hs.st === 8 ? '被踢硬直' : busy ? '出招中' : '就绪';
    const dashTxt = dead ? '—' : hs.st === 7 ? '冲刺中' : hs.cd > 0 ? `冷却 ${(hs.cd / 20).toFixed(1)}s` : '就绪';
    const recallTxt = dead ? '—' : hs.st === 9 ? '召回中，站稳' : hs.dart === 'ground' ? '镖在地上，按 E' : hs.dart === 'out' ? '飞行中' : '镖在手上';
    const key = [dead, dartTxt, meleeTxt, meleeName, dashTxt, recallTxt, hs.cd > 0 ? Math.ceil(hs.cd / 2) : 0].join('|');
    if (key === lastSkill) return;
    lastSkill = key;
    const set = (id: string, txt: string, state: string) => { const el = document.getElementById(id); if (!el) return; el.querySelector('span')!.textContent = txt; el.dataset.s = state; };
    set('sk-throw', dartTxt, dead ? 'off' : hs.dart === 'ready' ? 'ready' : hs.dart === 'ground' ? 'warn' : 'busy');
    set('sk-melee', meleeTxt, dead ? 'off' : busy ? 'busy' : 'ready');
    const mn = document.getElementById('sk-melee-n'); if (mn) mn.textContent = meleeName;
    const tm = document.getElementById('t-melee-l'); if (tm) tm.textContent = meleeName;
    set('sk-dash', dashTxt, dead ? 'off' : hs.cd > 0 || hs.st === 7 ? 'busy' : 'ready');
    const cd = document.querySelector<HTMLElement>('#sk-dash .cd'); if (cd) cd.style.transform = `scaleX(${dead ? 0 : Math.min(1, hs.cd / ((P.cfg.DASH_CD ?? 20) || 20))})`;
    set('sk-recall', recallTxt, dead ? 'off' : hs.st === 9 ? 'busy' : hs.dart === 'ground' ? 'hot' : 'off');
    reticle.dataset.s = dead ? 'off' : hs.dart;
    reticle.querySelector('b')!.textContent = dead ? '' : hs.dart === 'ground' ? 'E 召回 / 右键飞踢' : hs.dart === 'out' ? '飞行中' : '';
  }

  // 准星（跟着鼠标，比 3D 里画的快一帧）
  stageEl.addEventListener('pointermove', (e) => {
    if (e.pointerType === 'touch') return;
    const on = S.kind === 'live' && S.humanIdx >= 0 && human.active;
    reticle.hidden = !on;
    stageEl.classList.toggle('aiming', on);
    if (!on) return;
    const r = stageEl.getBoundingClientRect();
    reticle.style.transform = `translate(${e.clientX - r.left}px, ${e.clientY - r.top}px)`;
  });
  stageEl.addEventListener('pointerleave', () => { reticle.hidden = true; });

  // ---------------------------------------------------------------- 角落按钮、全屏、键盘、触屏
  $('c-sound').onclick = () => { sfx.set(!sfx.on); syncBarToggles(); if (human.active) focusStage(); };
  $('c-home').onclick = () => { view?.resetCamera(); if (human.active) focusStage(); };
  $('c-follow').onclick = () => { if (view) view.setFollow(!view.follow); syncBarToggles(); if (human.active) focusStage(); };
  async function toggleFullscreen() {
    try {
      if (isFs()) {
        if (document.exitFullscreen) await document.exitFullscreen();
        else if ((document as any).webkitExitFullscreen) await (document as any).webkitExitFullscreen();
      } else {
        if (stageColEl.requestFullscreen) await stageColEl.requestFullscreen();
        else if ((stageColEl as any).webkitRequestFullscreen) await (stageColEl as any).webkitRequestFullscreen();
        else toast('当前浏览器不支持网页全屏');
      }
    } catch {
      toast('当前环境禁止切全屏，请在独立浏览器标签页打开');
    }
    if (human.active) focusStage();
  }
  $('c-fs').onclick = () => { toggleFullscreen(); };
  const onFsChange = () => {
    syncBarToggles();
    requestAnimationFrame(() => { view?.resize(); v2d?.resize(); syncReticle(); if (human.active) focusStage(); });
  };
  document.addEventListener('fullscreenchange', onFsChange);
  document.addEventListener('webkitfullscreenchange', onFsChange as EventListener);
  if (view) view.onCue = (kind, mine) => {
    const hum = S.kind === 'live' && S.humanIdx >= 0;
    if (hum && mine && S.match) {
      if (kind === 'ko') S.match.clock.hold(110);
      else if (kind === 'clash' || kind === 'kickhit') S.match.clock.hold(60);
    }
    if (kind === 'ko' && mine && hum) { sfx.play('koYou'); return; }
    const vol = hum ? (mine ? 1 : 0.45) : (['ko', 'clash', 'capture', 'king'].includes(kind) ? 0.5 : 0.22);
    sfx.play(kind, vol);
  };
  window.addEventListener('keydown', (e) => {
    const t = (e.target as HTMLElement)?.tagName;
    if (t === 'INPUT' || t === 'TEXTAREA' || t === 'SELECT') return;
    if (e.code === 'Space' && !ovStart.hidden && startHandler) { e.preventDefault(); const h = startHandler; startHandler = null; h(); return; }
    if (human.active) return;
    if (e.code === 'Space') { e.preventDefault(); (document.activeElement as HTMLElement | null)?.blur?.(); togglePlay(); }
    else if (e.code === 'ArrowLeft') { e.preventDefault(); stepBy(e.shiftKey ? -10 : -1); }
    else if (e.code === 'ArrowRight') { e.preventDefault(); stepBy(e.shiftKey ? 10 : 1); }
    else if (e.key === 'r' || e.key === 'R') view?.resetCamera();
  });
  {
    const joy = $('joy'), knob = $('joy-knob');
    let tid: number | null = null;
    const upd = (cx: number, cy: number) => {
      const r = joy.getBoundingClientRect(), dx = cx - (r.left + r.width / 2), dy = cy - (r.top + r.height / 2), maxR = r.width * 0.36;
      const d = Math.hypot(dx, dy), k = d > maxR ? maxR / d : 1;
      knob.style.transform = `translate(calc(-50% + ${dx * k}px), calc(-50% + ${dy * k}px))`;
      human.setJoystick((dx * k) / maxR, (dy * k) / maxR);
    };
    joy.addEventListener('pointerdown', (e) => { e.preventDefault(); tid = e.pointerId; joy.setPointerCapture(tid); upd(e.clientX, e.clientY); });
    joy.addEventListener('pointermove', (e) => { if (e.pointerId === tid) upd(e.clientX, e.clientY); });
    const end = () => { tid = null; knob.style.transform = 'translate(-50%, -50%)'; human.setJoystick(0, 0); };
    joy.addEventListener('pointerup', end); joy.addEventListener('pointercancel', end);
    touchEl.querySelectorAll<HTMLButtonElement>('[data-act]').forEach((b) => b.addEventListener('pointerdown', (e) => {
      e.preventDefault(); e.stopPropagation(); human.setTouchMode(); human.trigger(+b.dataset.act! as 1 | 2 | 3 | 4);
      b.classList.add('hit'); setTimeout(() => b.classList.remove('hit'), 120);
    }));
  }

  // ---------------------------------------------------------------- 5v5 正式布阵
  function resolveRoundFormation(rp?: RoundPlan, mapId?: string) {
    if (!rp) return {} as { spawns?: [number, number][]; kings?: [number, number]; plans?: [string, string] };
    const rd = data.ep12.rounds.find((x) => x.r === rp.r);
    if (!rd) return {};
    const p0 = rd.plans.astra[rp.astraPlan], p1 = rd.plans.fable[rp.fablePlan];
    if (!p0 || !p1) return {};
    const dim: Record<string, [number, number]> = { caldera: [28, 20], piers: [32, 20], keeps: [30, 17] };
    const [W, H] = dim[mapId || rd.map] || [28, 20];
    const aOrder = ['astra', 'sol', 'hydrogen', 'grok', 'kimi'], fOrder = ['fable', 'opus', 'carbon', 'glm', 'deepseek'];
    const spawns: [number, number][] = [];
    for (const id of aOrder) { const [x, y] = p0.formation.站位[id] || [4, 10]; spawns.push([Math.round(x * 1000), Math.round(y * 1000)]); }
    for (const id of fOrder) { const [x, y] = p1.formation.站位[id] || [4, 10]; spawns.push([Math.round((W - x) * 1000), Math.round((H - y) * 1000)]); }
    const k0 = p0.formation.国王 ? Math.max(0, aOrder.indexOf(p0.formation.国王)) : 0;
    const k1 = p1.formation.国王 ? 5 + Math.max(0, fOrder.indexOf(p1.formation.国王)) : 5;
    return { spawns, kings: [k0, k1] as [number, number], plans: [p0.plan || '', p1.plan || ''] as [string, string] };
  }

  // ---------------------------------------------------------------- 侧栏：共用小件
  const pref = loadPref();
  human.edgeGuard = pref.guard !== false;
  const anchorOf = (id: string) => data.btAnchors[id]?.rating;
  const fmtR = (v?: number) => (v == null ? '' : (v > 0 ? '+' : '') + v.toFixed(0));
  const avImg = (id: string, cls = 'av') => `<img class="${cls}" src="${asset('img/avatars/' + (botById(data, id)?.avatar || avatarForName(id)) + '.webp')}" alt="">`;
  const botOptions = (sel: string, teamFirst = true) => {
    const g = (label: string, list: typeof data.bots) => `<optgroup label="${esc(label)}">${list.map((b) => `<option value="${b.id}"${b.id === sel ? ' selected' : ''}>${esc(b.name)}${anchorOf(b.id) != null ? '　' + fmtR(anchorOf(b.id)) : ''}</option>`).join('')}</optgroup>`;
    const solo = g('第 11 集海选原版（13 位）', data.bots.filter((b) => b.group === 'solo'));
    const team = g('第 12 集团队终版（10 位）', data.bots.filter((b) => b.group === 'team'));
    const ref = g('官方陪练（弱 / 中 / 强，适合热身）', SPARS.map((id) => botById(data, id)!).filter(Boolean));
    return teamFirst ? team + solo + ref : ref + solo + team;
  };
  const mapCards = (maps: MapMeta[], sel: string, attr: string) => `<div class="bm-maps">${maps.map((m) => `
    <button type="button" class="bm-mapcard${m.id === sel ? ' on' : ''}" ${attr}="${m.id}" aria-pressed="${m.id === sel}">
      <span class="thumb" style="background-image:url('${thumbUrl(m.id)}')"></span>
      <span class="nm">${esc(m.name)}</span><span class="th">${esc(m.theme)}</span>
    </button>`).join('')}</div>`;
  const seg = (items: [string, string][], sel: string, attr: string) => `<div class="bm-segbar">${items.map(([v, l]) => `<button type="button" ${attr}="${v}" aria-pressed="${v === sel}">${l}</button>`).join('')}</div>`;
  const keyOptions = (sel: string) => VOX_KEYS.map(([k, l]) => `<option value="${k}"${k === sel ? ' selected' : ''}>${esc(l)}</option>`).join('');

  // ---------------------------------------------------------------- 真人对战
  const P0 = { ep: (pref.ep as 11 | 12) || 11, mode: (pref.mode as GameMode) ?? 0, map: pref.map || 'twinlakes', tier: pref.tier || 'mid', opp: pref.opp || '', side: (pref.side as 0 | 1) ?? 0, me: pref.me || 'opus', delay: !!pref.delay };
  const play = P0;
  if (args.mode) { play.mode = +args.mode as GameMode; play.ep = play.mode >= 3 ? 12 : 11; }
  if (args.map) play.map = args.map;
  if (args.opp) play.opp = args.opp;
  const fixMap = () => {
    if (play.mode >= 3) play.map = MAP_OF_MODE[play.mode];
    else if (play.mode === 2) { if (mapMeta(play.map).group !== 'team4') play.map = 'canyon'; }
    else if (mapMeta(play.map).group !== 'solo') play.map = 'twinlakes';
  };
  const playSeats = (): { seats: SeatSpec[]; roundPlan?: RoundPlan } => {
    const tier = TIERS.find((t) => t.id === play.tier) || TIERS[1];
    const me = humanSeat(play.me);
    if (play.mode === 0) return { seats: [me, botSeat(play.opp || tier.lead)] };
    const spar = SPARS.includes(play.opp);
    if (play.mode === 1) {
      const lead = play.opp || tier.lead;
      const rest = spar ? [lead, lead] : tier.ffa.filter((x) => x !== lead).slice(0, 2);
      return { seats: [me, botSeat(lead), ...rest.map((x) => botSeat(x))] };
    }
    if (play.mode === 2) {
      const opp = spar ? [play.opp, play.opp, play.opp, play.opp] : play.opp ? [play.opp, ...tier.squad.filter((x) => x !== play.opp)].slice(0, 4) : tier.squad;
      return { seats: [me, ...SQUAD_MATES.map((x) => botSeat(x)), ...opp.map((x) => botSeat(x))] };
    }
    const seats = [...TEAM_A, ...TEAM_F].map((id) => botSeat(id));
    seats[play.side === 0 ? 4 : 9] = humanSeat(play.me);
    return { seats, roundPlan: { r: play.mode === 3 ? 2 : play.mode === 4 ? 4 : 6, astraPlan: '正式', fablePlan: '正式' } };
  };
  const persist = () => savePref({ ep: play.ep, mode: play.mode, map: play.map, tier: play.tier, opp: play.opp, side: play.side, me: play.me, delay: play.delay, guard: human.edgeGuard });

  function renderPlay() {
    fixMap();
    const ep11 = play.ep === 11;
    const modes: [string, string][] = ep11 ? [['0', '1v1 单挑'], ['1', '4 人混战'], ['2', '4v4 团队']] : [['3', '歼灭'], ['4', '占点'], ['5', '刺王']];
    const maps = ep11 ? data.maps.filter((m) => m.group === (play.mode === 2 ? 'team4' : 'solo')) : data.maps.filter((m) => m.group === 'team');
    const tierCards = TIERS.map((t) => `
      <button type="button" class="bm-tier${t.id === play.tier && !play.opp ? ' on' : ''}" data-tier="${t.id}" aria-pressed="${t.id === play.tier && !play.opp}">
        ${avImg(t.lead)}<span class="tl">${t.label}</span><span class="tn">${esc(displayName(t.lead))}</span><span class="tr">${fmtR(anchorOf(t.lead))}</span>
      </button>`).join('');
    const sparCards = SPARS.map((id) => `
      <button type="button" class="bm-spar${play.opp === id ? ' on' : ''}" data-spar="${id}" aria-pressed="${play.opp === id}">
        ${avImg(id)}<span class="sp-n">${esc(displayName(id))}</span><span class="sp-r">${fmtR(anchorOf(id))}</span><span class="sp-d">${esc(SPAR_NOTE[id])}</span>
      </button>`).join('');
    const sideCards = [0, 1].map((s) => {
      const ids = s === 0 ? TEAM_A : TEAM_F;
      return `<button type="button" class="bm-side-card s${s}${play.side === s ? ' on' : ''}" data-side="${s}" aria-pressed="${play.side === s}">
        <span class="sn">${s === 0 ? 'Astra 队 · 左 · 蓝' : 'Fable 队 · 右 · 红'}</span>
        <span class="sav">${ids.slice(0, 4).map((id) => avImg(id)).join('')}<i>你</i></span>
        <span class="sd">${s === 0 ? '顶替 Kimi，和 Astra、Sol、Hydrogen、Grok 并肩' : '顶替 DeepSeek，和 Fable、Opus、Carbon、GLM 并肩'}</span>
      </button>`;
    }).join('');
    sideEl.innerHTML = `
      <div class="bm-card">
        ${seg([['11', '第 11 集<small>单挑 · 混战 · 4v4</small>'], ['12', '第 12 集<small>5v5 团队赛</small>']], String(play.ep), 'data-ep')}
        <div class="bm-label">赛制</div>
        ${seg(modes, String(play.mode), 'data-mode')}
        <div class="bm-label">地图<span class="muted">${ep11 ? (play.mode === 2 ? '4 张 4v4 专属图' : '6 张单挑 / 混战图') : '第 12 集三图三赛制'}</span></div>
        ${mapCards(maps, play.map, ep11 ? 'data-map' : 'data-map12')}
        <p class="bm-mapdesc">${esc(mapMeta(play.map).desc)}</p>
        ${ep11 ? `
          <div class="bm-label">先热身：官方陪练<span class="muted">${play.mode === 2 ? '对面四个都是它' : play.mode === 1 ? '三席都是它' : '新手从弱陪练开始'}</span></div>
          <div class="bm-spars">${sparCards}</div>
          <div class="bm-label">${play.mode === 2 ? '再挑战：对面四人（你和 Sol、Hydrogen、Grok 一队）' : play.mode === 1 ? '再挑战：海选 AI（另两席同档自动搭配）' : '再挑战：海选 AI 四档'}</div>
          <div class="bm-tiers">${tierCards}</div>
          <label class="bm-field"><span>或者从全部 26 位里挑</span><select id="p-opp"><option value="">按上面的档位</option>${botOptions(play.opp, false)}</select></label>
        ` : `
          <div class="bm-label">加入哪一队</div>
          <div class="bm-sides">${sideCards}</div>
        `}
        <div class="bm-row2">
          <label class="bm-field"><span>你的造型</span><select id="p-me">${keyOptions(play.me)}</select></label>
          <label class="bm-check"><input type="checkbox" id="p-delay"${play.delay ? ' checked' : ''}><span>和 AI 一样晚 0.2 秒看到对手</span></label>
        </div>
        <button type="button" class="bm-go wide" id="p-start">开始对战</button>
      </div>
      <div class="bm-card bm-help">
        <h3>怎么玩</h3>
        <ul>
          <li><kbd>WASD</kbd> 移动，<b>鼠标</b>瞄准（准星就是镖的落点方向）</li>
          <li><kbd>左键</kbd> 扔镖，按住不放 = 镖回手就再扔</li>
          <li><kbd>右键</kbd> / <kbd>F</kbd> 近战：手里有镖是大范围挥砍，空手是飞踢（踢下坑就出局）</li>
          <li><kbd>空格</kbd> 冲刺：朝移动方向冲 3 米，能越过窄坑；冲刺中按右键直接接近战</li>
          <li><kbd>E</kbd> 镖掉在地上时召回，要站定 0.6 秒</li>
        </ul>
      </div>`;
    bindPlay();
  }
  function bindPlay() {
    const re = () => { persist(); renderPlay(); doPreview(); };
    sideEl.querySelectorAll<HTMLButtonElement>('[data-ep]').forEach((b) => b.onclick = () => { play.ep = +b.dataset.ep! as 11 | 12; play.mode = play.ep === 11 ? 0 : 3; play.opp = ''; re(); });
    sideEl.querySelectorAll<HTMLButtonElement>('[data-mode]').forEach((b) => b.onclick = () => { play.mode = +b.dataset.mode! as GameMode; re(); });
    sideEl.querySelectorAll<HTMLButtonElement>('[data-map]').forEach((b) => b.onclick = () => { play.map = b.dataset.map!; re(); });
    sideEl.querySelectorAll<HTMLButtonElement>('[data-map12]').forEach((b) => b.onclick = () => { play.map = b.dataset.map12!; play.mode = (+Object.entries(MAP_OF_MODE).find(([, v]) => v === play.map)![0]) as GameMode; re(); });
    sideEl.querySelectorAll<HTMLButtonElement>('[data-tier]').forEach((b) => b.onclick = () => { play.tier = b.dataset.tier!; play.opp = ''; re(); });
    sideEl.querySelectorAll<HTMLButtonElement>('[data-spar]').forEach((b) => b.onclick = () => { play.opp = b.dataset.spar!; re(); });
    sideEl.querySelectorAll<HTMLButtonElement>('[data-side]').forEach((b) => b.onclick = () => { play.side = +b.dataset.side! as 0 | 1; re(); });
    const opp = sideEl.querySelector<HTMLSelectElement>('#p-opp'); if (opp) opp.onchange = () => { play.opp = opp.value; re(); };
    const me = sideEl.querySelector<HTMLSelectElement>('#p-me')!; me.onchange = () => { play.me = me.value; re(); };
    const dl = sideEl.querySelector<HTMLInputElement>('#p-delay')!; dl.onchange = () => { play.delay = dl.checked; persist(); };
    sideEl.querySelector<HTMLButtonElement>('#p-start')!.onclick = () => startPlay();
  }
  function playTitle() { return `${MODE_NAME[play.mode]} · ${mapMeta(play.map).name}`; }
  let previewSeq = 0;
  async function doPreview() {
    if (tab !== 'play') return;
    const seq = ++previewSeq;
    const { seats, roundPlan } = playSeats();
    await preview(play.mode, play.map, seats, roundPlan);
    if (seq !== previewSeq) return;
    if (tab === 'play' && S.kind === 'preview') {
      const opp = play.mode >= 3 ? (play.side === 0 ? '加入 Astra 队' : '加入 Fable 队') : play.mode === 2 ? '4v4：你 + 3 位 AI 队友' : '对手：' + seats.slice(1).map((s) => displayName(s.bot || s.name)).join('、');
      showStart(playTitle(), opp, () => startPlay());
    }
    renderBar();
  }
  function startPlay() {
    const { seats, roundPlan } = playSeats();
    const go = () => startLive({ mode: play.mode, map: play.map, seed: Math.floor(Math.random() * 89999) + 10001, seats, roundPlan, delay: play.delay, title: playTitle(), restart: go });
    go();
  }

  // ---------------------------------------------------------------- 多局定级（6 局快速 / 12 局标准 / 0 = 无限模式）
  const loadSaved = (): SavedRatingSession[] => { try { return JSON.parse(localStorage.getItem(LS_RATING_KEY) || '[]'); } catch { return []; } };
  const saveSession = (s: SavedRatingSession) => { const l = loadSaved(); l.unshift(s); try { localStorage.setItem(LS_RATING_KEY, JSON.stringify(l.slice(0, 12))); } catch { /* ignore */ } };
  const rating = { active: false, count: 6 as 6 | 12 | 0, includeTeam: true, delayMode: false, schedule: buildRatingSchedule(6, true), done: [] as RatingMatchDone[], runningIdx: -1 };

  function renderRating() {
    const isEndless = rating.count === 0;
    const recs = rating.done.flatMap((d) => d.records), nDone = rating.done.length;
    const complete = !isEndless && rating.active && nDone >= rating.schedule.length, running = rating.runningIdx >= 0;
    const settled = complete || (isEndless && nDone >= 6);
    const est = recs.length ? estimateHumanRating(recs, 0.5, settled ? 0 : 200) : null;
    const saved = loadSaved();
    type Row = { id: string; name: string; version: string; rating: number; avatar: string; you?: boolean };
    const board: Row[] = data.standings11.map((s) => ({ id: s.id, name: s.short, version: '海选版', rating: s.rating, avatar: s.avatar }));
    if (rating.includeTeam) for (const s of data.standings11) if (s.retest) board.push({ id: s.id + '-t', name: s.short, version: '团队终版', rating: s.retest.final, avatar: s.avatar });
    if (est) board.push({ id: '__you__', name: settled ? '你' : '你（校准中）', version: isEndless ? `无限 ${nDone} 局` : complete ? `${nDone} 局定级` : `${nDone}/${rating.schedule.length} 局`, rating: est.rating, avatar: 'ep11/human', you: true });
    else if (saved[0]) board.push({ id: '__you__', name: '你（上次）', version: saved[0].count === 0 ? `无限 ${saved[0].games} 局` : `${saved[0].games} 局`, rating: saved[0].rating, avatar: 'ep11/human', you: true });
    board.sort((a, b) => b.rating - a.rating);
    const youRank = board.findIndex((r) => r.you) + 1;
    sideEl.innerHTML = `
      <div class="bm-card">
        <h2>多局定级 · 插进总榜</h2>
        <p class="sub">${isEndless ? '无限模式：每局结束后按你的实时实力分自动匹配最接近的 AI（每 4 局穿插一场 4 人混战），满 6 局给出正式分，随时可继续冲分或保存战绩。' : '由弱到强连打几局，按比赛同款 Bradley–Terry 实力分（400 / ln10）算出你的分数，插进第 11 集总榜。'}</p>
        ${seg([['6', '6 局快速'], ['12', '12 局标准'], ['0', '无限模式']], String(rating.count), 'data-rcnt')}
        <div class="bm-row2">
          <label class="bm-check"><input type="checkbox" id="rt-pool"${rating.includeTeam ? ' checked' : ''}${rating.active ? ' disabled' : ''}><span>对手含第 12 集团队终版</span></label>
          <label class="bm-check"><input type="checkbox" id="rt-delay"${rating.delayMode ? ' checked' : ''}${rating.active ? ' disabled' : ''}><span>AI 同款 0.2 秒延迟</span></label>
        </div>
        ${!rating.active ? `<button type="button" class="bm-go wide" id="rt-start">${isEndless ? '开始无限冲分' : `开始 ${rating.count} 局定级`}</button>`
          : running ? `<div class="bm-row"><button type="button" class="bm-go wide" disabled>${isEndless ? `第 ${rating.runningIdx + 1} 局进行中` : `第 ${rating.runningIdx + 1} / ${rating.schedule.length} 局进行中`}</button><button type="button" class="bm-ghost-btn" id="rt-reset">放弃</button></div>`
            : isEndless ? `<div class="bm-row"><button type="button" class="bm-go wide" id="rt-next">下一局（第 ${nDone + 1} 局）</button><button type="button" class="bm-ghost-btn" id="rt-save">保存</button><button type="button" class="bm-ghost-btn" id="rt-reset">重置</button></div>`
              : !complete ? `<div class="bm-row"><button type="button" class="bm-go wide" id="rt-next">下一局（${nDone + 1} / ${rating.schedule.length}）</button><button type="button" class="bm-ghost-btn" id="rt-reset">重置</button></div>`
                : `<button type="button" class="bm-go wide" id="rt-reset">再定级一轮</button>`}
        ${est ? `<div class="bm-rating-hero"><div><div class="rh-t">${settled ? `你的实力分 · 第 ${youRank} / ${board.length} 名` : isEndless ? `无限校准中 · 已打 ${nDone} 局` : `校准中 · 已打 ${nDone}/${rating.schedule.length} 局`}</div>
          <div class="rh-s">${settled ? `95% 区间 [${fmtR(est.ci95[0])}, ${fmtR(est.ci95[1])}]` : `暂估第 ${youRank} 名，打满 6 局给出正式分`}</div></div>
          <div class="rh-v${settled ? '' : ' calib'}">${settled ? '' : '≈'}${fmtR(est.rating)}</div></div>` : ''}
        <div class="bm-fx">${rating.schedule.map((fx, i) => {
          const d = rating.done[i], cur = rating.active && i === nDone;
          const cls = !d ? '' : d.score > 0.5 ? 'win' : d.score < 0.5 ? 'loss' : 'draw';
          const res = !d ? (cur ? '待开打' : '') : fx.mode === 0 ? (d.score === 1 ? '胜' : d.score === 0.5 ? '平' : '负') : `第 ${d.humanRank} 名`;
          return `<div class="bm-fx-i${cur ? ' cur' : ''}"><span><b>${i + 1}</b>${esc(fx.label)}</span><span class="res ${cls}">${res}</span>${d?.shareCode ? `<button type="button" class="bm-mini" data-rt-replay="${i}">复盘</button>` : ''}</div>`;
        }).join('')}</div>
      </div>
      <div class="bm-card">
        <h3>实力分总榜（${board.length} 席）</h3>
        <div class="bm-lb">${board.map((r, i) => `<div class="bm-lb-r${r.you ? ' you' : ''}"><span class="rk">${i + 1}</span><img src="${asset('img/avatars/' + r.avatar + '.webp')}" alt=""><span class="nm">${esc(r.name)}<i>${esc(r.version)}</i></span><span class="sc">${fmtR(r.rating)}</span></div>`).join('')}</div>
      </div>`;
    sideEl.querySelectorAll<HTMLButtonElement>('[data-rcnt]').forEach((b) => b.onclick = () => { if (rating.active) return; rating.count = +b.dataset.rcnt! as 6 | 12 | 0; rating.schedule = buildRatingSchedule(rating.count, rating.includeTeam); renderRating(); ratingPreview(); });
    const pool = sideEl.querySelector<HTMLInputElement>('#rt-pool')!; pool.onchange = () => { rating.includeTeam = pool.checked; rating.schedule = buildRatingSchedule(rating.count, rating.includeTeam); renderRating(); };
    const dl = sideEl.querySelector<HTMLInputElement>('#rt-delay')!; dl.onchange = () => { rating.delayMode = dl.checked; };
    const st = sideEl.querySelector<HTMLButtonElement>('#rt-start'); if (st) st.onclick = () => { rating.done = []; rating.schedule = buildRatingSchedule(rating.count, rating.includeTeam); playFixture(); };
    const nx = sideEl.querySelector<HTMLButtonElement>('#rt-next'); if (nx) nx.onclick = () => playFixture();
    const sv = sideEl.querySelector<HTMLButtonElement>('#rt-save'); if (sv) sv.onclick = () => {
      if (!rating.done.length) return;
      const f = estimateHumanRating(rating.done.flatMap((d) => d.records), 0.5, rating.done.length >= 6 ? 0 : 200);
      saveSession({ ts: Date.now(), count: 0, includeTeam: rating.includeTeam, delayMode: rating.delayMode, rating: f.rating, ci95: f.ci95, games: rating.done.length, points: f.points, matches: rating.done });
      toast(`已保存无限模式 ${rating.done.length} 局战绩，当前实力分：${fmtR(f.rating)}`);
      renderRating();
    };
    const rs = sideEl.querySelector<HTMLButtonElement>('#rt-reset'); if (rs) rs.onclick = () => { stopLive(); rating.active = false; rating.runningIdx = -1; rating.done = []; rating.schedule = buildRatingSchedule(rating.count, rating.includeTeam); renderRating(); ratingPreview(); };
    sideEl.querySelectorAll<HTMLButtonElement>('[data-rt-replay]').forEach((b) => b.onclick = async () => {
      const it = rating.done[+b.dataset.rtReplay!]; if (!it?.shareCode) return;
      setStatus('正在重算回放…');
      const { replay } = await recomputeSharedReplay(await getEngine(), it.shareCode);
      setStatus('');
      showReplay(replay, 0, 0, it.fixture.label);
    });
  }
  async function ratingPreview() {
    if (tab !== 'rating' || rating.runningIdx >= 0) return;
    const fx = rating.schedule[rating.done.length] || rating.schedule[0];
    const seats = [humanSeat(play.me), ...fx.opps.map((id) => botSeat(id))];
    await preview(fx.mode, fx.map, seats);
    if (tab === 'rating' && S.kind === 'preview') showStart(`第 ${rating.done.length + 1} 局 · ${fx.label}`, `${rating.count === 0 ? '无限冲分' : '多局定级'} · ${MODE_NAME[fx.mode]} · ${mapMeta(fx.map).name}`, () => playFixture());
    renderBar();
  }
  function playFixture() {
    const idx = rating.done.length, fx = rating.schedule[idx];
    if (!fx) return;
    rating.active = true; rating.runningIdx = idx;
    renderRating();
    const seed = 202611 + idx * 137 + Math.floor(Math.random() * 900);
    const seats = [humanSeat(play.me), ...fx.opps.map((id) => botSeat(id))];
    const tag = rating.count === 0 ? '无限冲分' : '多局定级';
    startLive({ mode: fx.mode, map: fx.map, seed, seats, delay: rating.delayMode, title: `${tag} 第 ${idx + 1} 局 · ${fx.label}`,
      restart: () => {
        if (rating.runningIdx < 0 && (rating.count === 0 || rating.done.length < rating.schedule.length)) playFixture();
        else if (rating.count !== 0 && rating.done.length >= rating.schedule.length) { rating.active = false; rating.done = []; rating.schedule = buildRatingSchedule(rating.count, rating.includeTeam); renderRating(); playFixture(); }
      },
      onEnd: async (P, match) => {
        if (!rating.active || rating.runningIdx !== idx) return;
        const results = P.end?.results || [];
        const h = results.find((r) => r.seat === 0) || { seat: 0, rank: 2, kills: 0 } as any;
        const recs: HumanMatchRecord[] = [];
        let avg = 0.5;
        if (fx.mode === 0) {
          const oppId = fx.opps[0], o = results.find((r) => r.seat === 1) || { rank: 2 } as any;
          const sc = h.rank < o.rank ? 1 : h.rank > o.rank ? 0 : 0.5;
          avg = sc; recs.push({ oppId, oppRating: data.btAnchors[oppId]?.duel ?? data.btAnchors[oppId]?.rating ?? 0, score: sc, mode: 0 });
        } else {
          const sm = ffaScores(results.map((r) => ({ seat: r.seat, id: r.seat === 0 ? '__human__' : fx.opps[r.seat - 1], rank: r.rank, kills: r.kills })));
          const my = sm.__human__ ?? 0;
          let sum = 0;
          for (const oppId of fx.opps) { const o = sm[oppId] ?? 0, s = my > o ? 1 : my < o ? 0 : 0.5; sum += s; recs.push({ oppId, oppRating: data.btAnchors[oppId]?.ffa ?? data.btAnchors[oppId]?.rating ?? 0, score: s, mode: 1 }); }
          avg = sum / fx.opps.length;
        }
        let shareCode: string | undefined;
        try { shareCode = await match.shareCode(); } catch { /* ignore */ }
        rating.done.push({ fixture: fx, seed, humanRank: h.rank, humanKills: h.kills, score: avg, records: recs, shareCode });
        rating.runningIdx = -1;
        if (rating.count === 0) {
          const allRecs = rating.done.flatMap((d) => d.records);
          const curEst = estimateHumanRating(allRecs, 0.5, rating.done.length >= 6 ? 0 : 200);
          rating.schedule.push(pickEndlessFixture(rating.done.length, curEst.rating, rating.includeTeam, rating.done.map((d) => d.fixture), data));
          if (rating.done.length === 6) toast(`已完成 6 局校准，当前实力分：${fmtR(curEst.rating)}（可继续无限冲分）`);
        } else if (rating.done.length >= rating.schedule.length) {
          const f = estimateHumanRating(rating.done.flatMap((d) => d.records));
          saveSession({ ts: Date.now(), count: rating.count, includeTeam: rating.includeTeam, delayMode: rating.delayMode, rating: f.rating, ci95: f.ci95, games: rating.done.length, points: f.points, matches: rating.done });
          toast(`定级完成，你的实力分：${fmtR(f.rating)}`);
        }
        if (tab === 'rating') renderRating();
      } });
  }

  // ---------------------------------------------------------------- 程序互打
  const W0 = { mode: (args.mode ? +args.mode : 0) as GameMode, map: args.map || 'stonecircle', bots: args.bots ? args.bots.split(',') : [] as string[], seed: args.seed ? +args.seed : 20261116 };
  const watch = W0;
  const SQUAD_CANYON = (SQUAD_PRESETS.find((p) => p.id === 'squad-canyon') || SQUAD_PRESETS[0]).bots;
  const needOf = (m: number) => (m === 0 ? 2 : m === 1 ? 4 : m === 2 ? 8 : 10);
  const defBots = (m: number) => (m === 0 ? ['astra-t', 'fable-t'] : m === 1 ? ['astra', 'fable', 'opus', 'carbon'] : m === 2 ? SQUAD_CANYON.slice() : [...TEAM_A, ...TEAM_F]);
  const fixWatch = () => {
    if (watch.bots.length !== needOf(watch.mode)) watch.bots = defBots(watch.mode);
    if (watch.mode >= 3) watch.map = MAP_OF_MODE[watch.mode];
    else if (watch.mode === 2) { if (mapMeta(watch.map).group !== 'team4') watch.map = 'canyon'; }
    else if (mapMeta(watch.map).group !== 'solo') watch.map = 'stonecircle';
  };
  function renderWatch() {
    fixWatch();
    const m = watch.mode, team = m >= 2;
    const maps = m >= 3 ? data.maps.filter((x) => x.group === 'team') : data.maps.filter((x) => x.group === (m === 2 ? 'team4' : 'solo'));
    const presets: BotMatchPreset[] = m >= 3 ? TEAM_PRESETS : m === 2 ? SQUAD_PRESETS : DUEL_PRESETS;
    const sel = (i: number) => `<select data-wseat="${i}">${botOptions(watch.bots[i], m >= 3)}</select>`;
    const half = needOf(m) / 2;
    sideEl.innerHTML = `
      <div class="bm-card">
        <h2>程序互打</h2>
        <p class="sub">26 份 C++ 程序编成 WebAssembly 在你的浏览器里实时对打，海选版、团队终版、陪练随意编排。</p>
        ${seg([['0', '1v1'], ['1', '4 人'], ['2', '4v4'], ['3', '歼灭'], ['4', '占点'], ['5', '刺王']], String(m), 'data-wmode')}
        ${mapCards(maps, watch.map, m >= 3 ? 'data-wmap12' : 'data-wmap')}
        ${team ? `<div class="bm-teams">
            <div class="bm-teamcol s0"><h4>${m >= 3 ? 'Astra 队 · 左 · 蓝' : '左队 · 蓝'}</h4>${Array.from({ length: half }, (_, i) => sel(i)).join('')}</div>
            <div class="bm-teamcol s1"><h4>${m >= 3 ? 'Fable 队 · 右 · 红' : '右队 · 红'}</h4>${Array.from({ length: half }, (_, i) => sel(i + half)).join('')}</div>
          </div>` : `<div class="bm-seats">${watch.bots.map((_, i) => `<label class="bm-field"><span>座位 ${i + 1}</span>${sel(i)}</label>`).join('')}</div>`}
        <button type="button" class="bm-go wide" id="w-start">开始对打</button>
      </div>
      <div class="bm-card">
        <h3>${m >= 3 ? '5v5 正式阵容与赛后实验' : m === 2 ? '4v4 阵容' : '常用对决'}</h3>
        <div class="bm-presets">${presets.map((p) => `<button type="button" class="bm-preset" data-preset="${p.id}"><span class="pt">${esc(p.label)}<i>${esc(p.badge)}</i></span><span class="pd">${esc(p.desc)}</span></button>`).join('')}</div>
      </div>`;
    const re = () => { renderWatch(); watchPreview(); };
    sideEl.querySelectorAll<HTMLButtonElement>('[data-wmode]').forEach((b) => b.onclick = () => { watch.mode = +b.dataset.wmode! as GameMode; watch.bots = defBots(watch.mode); re(); });
    sideEl.querySelectorAll<HTMLButtonElement>('[data-wmap]').forEach((b) => b.onclick = () => { watch.map = b.dataset.wmap!; re(); });
    sideEl.querySelectorAll<HTMLButtonElement>('[data-wmap12]').forEach((b) => b.onclick = () => { watch.map = b.dataset.wmap12!; watch.mode = (+Object.entries(MAP_OF_MODE).find(([, v]) => v === watch.map)![0]) as GameMode; re(); });
    sideEl.querySelectorAll<HTMLSelectElement>('[data-wseat]').forEach((s) => s.onchange = () => { watch.bots[+s.dataset.wseat!] = s.value; watchPreview(); });
    sideEl.querySelector<HTMLButtonElement>('#w-start')!.onclick = () => runWatch();
    sideEl.querySelectorAll<HTMLButtonElement>('[data-preset]').forEach((b) => b.onclick = () => {
      const p = [...DUEL_PRESETS, ...SQUAD_PRESETS, ...TEAM_PRESETS].find((x) => x.id === b.dataset.preset);
      if (!p) return;
      watch.mode = p.mode; watch.map = p.map; watch.bots = p.bots.slice();
      renderWatch(); runWatch(p);
    });
  }
  async function watchPreview() {
    if (tab !== 'watch') return;
    fixWatch();
    const seats = watch.bots.map((id) => botSeat(id));
    await preview(watch.mode, watch.map, seats, watch.mode >= 3 ? { r: watch.mode === 3 ? 2 : watch.mode === 4 ? 4 : 6, astraPlan: '正式', fablePlan: '正式' } : undefined);
    renderBar();
  }
  function runWatch(p?: BotMatchPreset, pace?: number) {
    const m = p ? p.mode : watch.mode, mp = p ? p.map : watch.map, bs = p ? p.bots : watch.bots;
    const seats = bs.map((id, i) => botSeat(id, p?.names?.[i]));
    const rp = p?.roundPlan || (m >= 3 ? { r: m === 3 ? 2 : m === 4 ? 4 : 6, astraPlan: '正式', fablePlan: '正式' } : undefined);
    const go = () => { const seed = watch.seed++; startLive({ mode: m, map: mp, seed, seats, roundPlan: rp, speed: pace ?? watchSpeed, title: p ? p.label : `${MODE_NAME[m]} · ${mapMeta(mp).name}`, restart: go }); };
    go();
  }

  // ---------------------------------------------------------------- 精选回放
  let curReplay = args.f || data.replays[0]?.id || '';
  function renderReplays() {
    const grp = (ep: number, h: string) => {
      const list = data.replays.filter((r) => r.ep === ep);
      return `<h3>${h}</h3><div class="bm-presets">${list.map((r) => `<button type="button" class="bm-preset${r.id === curReplay ? ' on' : ''}" data-rid="${r.id}">
        <span class="pt">${esc(r.title)}<i>${esc(r.tag)}</i></span><span class="pd">${esc(r.desc)}</span></button>`).join('')}</div>`;
    };
    const cur = data.replays.find((r) => r.id === curReplay);
    sideEl.innerHTML = `
      <div class="bm-card">
        <h2>精选回放</h2>
        <p class="sub">全部来自正式比赛（第 11 集 4v4 样局由同一份引擎和海选原版程序现场重跑），可以暂停、单步、拖动，或打开 0.2 秒残影看 AI 当时看到的位置。</p>
        ${cur && cur.focusSec > 0 ? `<button type="button" class="bm-go wide" id="rp-focus">${ICO.play}跳到高光（${cur.focusSec.toFixed(1)} 秒）</button>` : ''}
        ${grp(12, '第 12 集 · 5v5 名场面')}
        ${grp(11, '第 11 集 · 海选与 4v4')}
      </div>`;
    const fb = sideEl.querySelector<HTMLButtonElement>('#rp-focus');
    if (fb && cur) fb.onclick = () => { view?.seek(cur.focusSec * 20); view?.play(true); };
    sideEl.querySelectorAll<HTMLButtonElement>('[data-rid]').forEach((b) => b.onclick = () => loadCurated(b.dataset.rid!, false));
  }
  async function loadCurated(id: string, focus: boolean) {
    const meta = data.replays.find((r) => r.id === id);
    if (!meta) return;
    curReplay = id;
    if (tab === 'replay') renderReplays();
    setStatus(`正在载入：${meta.title}…`);
    try {
      const P = parseReplayText(await loadCuratedReplayText(id));
      setStatus('');
      await showReplay(P, focus ? meta.focusSec : 0, -1, meta.title);
    } catch (e: any) { setStatus('载入回放失败：' + (e?.message || e)); }
  }

  // ---------------------------------------------------------------- 标签切换
  const TAB_CN: Record<Tab, string> = { play: '真人对战', rating: '多局定级', watch: '程序互打', replay: '精选回放' };
  function renderSide(keepStage = false) {
    document.querySelectorAll<HTMLButtonElement>('#bm-tabs button').forEach((b) => b.setAttribute('aria-selected', String(b.dataset.tab === tab)));
    const cr = document.querySelector('.bm-head .arena-crumbs'); if (cr) cr.innerHTML = arenaCrumbsHtml(ep as any, TAB_CN[tab]);
    document.body.dataset.tab = tab;
    if (tab === 'play') { renderPlay(); if (!keepStage && S.kind !== 'live') doPreview(); }
    else if (tab === 'rating') { renderRating(); if (!keepStage && S.kind !== 'live') ratingPreview(); }
    else if (tab === 'watch') { renderWatch(); if (!keepStage && S.kind !== 'live') watchPreview(); }
    else { renderReplays(); if (!keepStage) loadCurated(curReplay, false); }
  }
  document.querySelectorAll<HTMLButtonElement>('#bm-tabs button').forEach((b) => b.onclick = () => {
    if (S.kind === 'live') { stopLive(); S = { kind: 'none', humanIdx: -1 }; }
    tab = b.dataset.tab as Tab;
    hideOverlays();
    renderSide();
  });

  // ---------------------------------------------------------------- 缩略图模式（scripts/ep11-r3d/thumbs.mjs 用）
  if (thumb) {
    const m = mapMeta(args.thumb!);
    const mode = (m.modes[0] ?? 0) as GameMode;
    const bots = mode >= 3 ? [...TEAM_A, ...TEAM_F] : mode === 2 ? SQUAD_CANYON : ['astra', 'fable', 'opus', 'carbon'].slice(0, needOf(mode));
    await preview(mode, m.id, bots.map((id) => botSeat(id)), mode >= 3 ? { r: mode === 3 ? 2 : mode === 4 ? 4 : 6, astraPlan: '正式', fablePlan: '正式' } : undefined);
    (window as any).__thumbReady = true;
    return;
  }

  // ---------------------------------------------------------------- 开场
  renderSide(true);
  if (args.r) {
    setStatus('正在用原版引擎重算分享回放…');
    try {
      const { replay, header } = await recomputeSharedReplay(await getEngine(), args.r);
      setStatus('');
      await showReplay(replay, 0, header.human ?? -1, '分享的对局');
    } catch (e: any) { setStatus('分享链接解析失败：' + (e?.message || e)); }
  } else if (args.f) {
    await loadCurated(args.f, args.focus === '1');
  } else if (args.preset) {
    const p = [...DUEL_PRESETS, ...SQUAD_PRESETS, ...TEAM_PRESETS].find((x) => x.id === args.preset);
    tab = 'watch';
    if (p) { watch.mode = p.mode; watch.map = p.map; watch.bots = p.bots.slice(); }
    renderSide(true);
    if (p && args.go === '1') runWatch(p, args.pace != null ? (+args.pace ? 50 / +args.pace : 0) : undefined);
    else watchPreview();
  } else if (args.go === '1') {
    if (tab === 'watch') { fixWatch(); runWatch(undefined, args.pace != null ? (+args.pace ? 50 / +args.pace : 0) : undefined); }
    else if (tab === 'play') { fixMap(); startPlay(); }
  } else {
    renderSide();
  }
}
