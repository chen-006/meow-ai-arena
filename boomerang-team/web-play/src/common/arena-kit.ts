// 五期对局页面（人机对战 / bot 对打 / 回放）共用的小工具。原来每期的 arena-page.ts 各抄一份，现在合并到这里（打包进公共的 arena 块）。
//   - 顶栏：橙猫 + 面包屑"算法对抗赛 › 第 N 期 · 标题 › 人机对战"+ 主题按钮；顶栏跟随全站主题，对局区（棋盘、面板、控制条）固定深色
//   - 开局设置：地图种子 / 风格 / 思考预算这类不常改的收进"高级设置"，开始按钮固定在弹窗底部
//   - 选手面板里的"幽默榜第 N 名"联动
//   - toast、棋盘覆盖层、复制、地址栏参数
import { ALL_EPISODES } from './episodes';
import { arenaCrumbsHtml, THEME_BTN } from './shell';
import { fillBench } from './bench';
import { esc } from './ui';
import { bindLang, EN, langBtnHtml } from './lang';
import C from '@i18n/common';

export type Mode = 'play' | 'watch' | 'replay';
export const MODE_CN: Record<Mode, string> = C.mode;
export const $ = (id: string) => document.getElementById(id)!;

export function hashArgs(): Record<string, string> {
  const o: Record<string, string> = {};
  for (const kv of location.hash.slice(1).split('&')) {
    if (!kv) continue;
    const i = kv.indexOf('=');
    o[decodeURIComponent(i < 0 ? kv : kv.slice(0, i))] = i < 0 ? '1' : decodeURIComponent(kv.slice(i + 1));
  }
  return o;
}

/** 顶部提示条（点一下就收起）。手机上放在顶栏下面，不再压住底部的方向键和控制条 */
export function toast(text: string, ms = 2600) {
  let t = document.getElementById('toast');
  if (!t) {
    t = document.createElement('div'); t.id = 'toast'; t.setAttribute('role', 'status');
    t.addEventListener('click', () => t!.classList.remove('on'));
    document.body.append(t);
  }
  t.textContent = text;
  t.classList.add('on');
  clearTimeout((t as any)._h);
  (t as any)._h = setTimeout(() => t!.classList.remove('on'), ms);
}

/** 棋盘上的覆盖层：载入中 / 出错 / 关掉 */
let EP_LABEL = C.thisEpisode;
export function overlay(kind: 'loading' | 'error' | null, text = '') {
  let el = document.getElementById('stagemsg');
  if (!el) { el = document.createElement('div'); el.id = 'stagemsg'; document.getElementById('stage')!.append(el); }
  el.className = kind ? 'on ' + kind : '';
  if (kind === 'loading') el.innerHTML = `<div class="box"><span class="spinner"></span><span>${esc(text)}</span></div>`;
  else if (kind === 'error') el.innerHTML = `<div class="box"><b>${C.error}</b><span>${esc(text)}</span><span class="row"><button class="btn btn-primary" type="button" onclick="location.reload()">${C.reload}</button><a class="btn" href="./">${esc(C.backTo(EP_LABEL))}</a></span></div>`;
  else el.innerHTML = '';
}

export async function copyText(text: string) {
  try { await navigator.clipboard.writeText(text); return true; } catch { /* 下面的退路 */ }
  const ta = document.createElement('textarea');
  ta.value = text; document.body.append(ta); ta.select();
  const ok = document.execCommand('copy'); ta.remove(); return ok;
}

/**
 * 对局页的外壳：在查看器（createViewer）建好之后调用。
 * 顶栏换成橙猫 + 面包屑 + 主题按钮；对局区（main / 控制条 / 帮助 / 悬停提示）写 data-theme="dark"，
 * 顶栏和开局设置弹窗跟随全站主题，从浅色的期页面进来时不会整页突然变黑。
 */
export function arenaChrome(epId: string, mode: Mode) {
  const ep = ALL_EPISODES.find(e => e.id === epId)!;
  document.body.classList.add('arena', 'mode-' + mode);
  const h1 = document.querySelector('#app>header h1');
  if (h1) {
    const nav = document.createElement('div');
    nav.className = 'arena-crumbs';
    nav.innerHTML = arenaCrumbsHtml(ep, MODE_CN[mode]);
    h1.replaceWith(nav);
  }
  const head = document.querySelector('#app>header');
  if (head && !head.querySelector('.mw-theme')) head.insertAdjacentHTML('beforeend', langBtnHtml(location.pathname) + THEME_BTN);
  bindLang();
  for (const sel of ['#app>main', '#app>#main', '#app>footer', '#help', '#tip', '.tip', '#drop', '#dropzone'])
    document.querySelectorAll(sel).forEach(el => el.setAttribute('data-theme', 'dark'));
  EP_LABEL = ep.num;
}

/**
 * 开局设置弹窗：把"地图种子 / 地图风格 / 思考预算"和长说明收进"高级设置"（默认收起），节奏和人数这类常用项留在外面。
 * 各期的模板不用改：按 #s-seed 所在的 .fgrid 找，节奏（#s-pace）和人数之类留下，其余字段搬进 <details>。
 */
export function foldAdvanced(dlg: HTMLElement) {
  const grid = dlg.querySelector('#s-seed')?.closest('.fgrid');
  if (!grid || dlg.querySelector('details.adv')) return;
  const keep = new Set(['s-pace', 's-n', 's-count', 's-me']);
  const moved = [...grid.querySelectorAll<HTMLElement>(':scope > .field')].filter(f => ![...f.querySelectorAll('[id]')].some(x => keep.has(x.id)));
  if (!moved.length) return;
  const det = document.createElement('details');
  det.className = 'adv';
  const names = moved.map(f => f.querySelector('span')?.textContent?.trim()).filter(Boolean).join(C.listSep);
  det.innerHTML = `<summary>${C.advanced}<small>${esc(names)}</small><svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m4 6 4 4 4-4"/></svg></summary><div class="fgrid"></div>`;
  const inner = det.querySelector('.fgrid')!;
  moved.forEach(f => inner.append(f));
  const hint = grid.nextElementSibling;
  grid.after(det);
  if (hint && hint.matches('p.hint')) det.append(hint);
  // 原来的小标题"地图与节奏"：留在外面的只剩节奏了
  const h3 = grid.previousElementSibling;
  // 外面只剩一个字段（节奏）时，小标题就不要了（字段自己有标签），这个字段占满一行
  if (h3 && h3.tagName === 'H3' && (h3.textContent || '').trim() === C.mapAndPace) h3.textContent = grid.children.length > 1 ? C.pace : '';
  if (grid.children.length === 1) grid.classList.add('one');
  if (!grid.children.length) grid.remove();
}

/** 选手下拉框下面显示"这个模型在幽默榜第 N 名"（跨站联动；拿不到数据就什么都不显示） */
export function seatBench(dlg: HTMLElement, bots: { id: string; name: string }[]) {
  let busy = false;
  const run = () => {
    if (busy) return;
    busy = true;
    requestAnimationFrame(() => {
      dlg.querySelectorAll<HTMLSelectElement>('.seatpick select').forEach(sel => {
        const box = sel.closest('.seatpick')!;
        let slot = box.querySelector<HTMLElement>(':scope > .bench-link');
        if (!slot) { slot = document.createElement('span'); slot.className = 'bench-link'; box.append(slot); }
        const name = bots.find(b => b.id === sel.value)?.name ?? '';
        if (slot.dataset.bench !== name) { slot.dataset.bench = name; slot.innerHTML = ''; }
      });
      fillBench(dlg).finally(() => { busy = false; });
    });
  };
  dlg.addEventListener('change', e => { if ((e.target as HTMLElement).closest('.seatpick')) run(); });
  new MutationObserver(muts => { if (muts.some(m => [...m.addedNodes].some(n => n instanceof HTMLElement && (n.matches('.seatpick') || n.querySelector('.seatpick'))))) run(); })
    .observe(dlg, { childList: true, subtree: true });
  run();
}

/** 开局设置弹窗的统一处理：高级设置折叠 + 幽默榜联动 */
export function enhanceSetup(dlg: HTMLElement, bots: { id: string; name: string }[]) {
  foldAdvanced(dlg);
  if (!EN) seatBench(dlg, bots);   // 英文版不做站间联动
}
