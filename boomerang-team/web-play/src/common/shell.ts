// meowllm 共用外壳的页头 / 页脚标记（以 shared\shell.html 的"算法对抗赛"一节为准；样式在 /mw/meow.<hash>.css）。
// 不碰 DOM，构建时的预渲染（src/prerender.ts）和浏览器里都用这一份。
import type { EpisodeMeta } from './episodes';
import C from '@i18n/common';
import { EN, HOME, langBtnHtml } from './lang';

export const CAT = '<svg class="mw-logo" viewBox="0 0 36 36" aria-hidden="true"><path d="M5 25V6l9 7h8l9-7v19q-13 10-26 0Z" fill="currentColor"/><path d="M12 21h2m8 0h2m-9 5q3 3 6 0" stroke-width="2.4" stroke-linecap="round" fill="none"/></svg>';
const CHEVRON = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="m4 6 4 4 4-4"/></svg>';
export const THEME_BTN = `<button class="mw-theme" type="button" data-mw-theme aria-label="${C.themeBtn}"><svg class="mw-i-system" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" aria-hidden="true"><circle cx="12" cy="12" r="8"/><path d="M12 4a8 8 0 0 1 0 16Z" fill="currentColor"/></svg><svg class="mw-i-light" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2.5v2m0 15v2M2.5 12h2m15 0h2M5.3 5.3l1.4 1.4m10.6 10.6 1.4 1.4M5.3 18.7l1.4-1.4M17.3 6.7l1.4-1.4"/></svg><svg class="mw-i-dark" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linejoin="round" aria-hidden="true"><path d="M19.5 14.6A8 8 0 0 1 9.4 4.5a8 8 0 1 0 10.1 10.1Z"/></svg></button>`;
/** 幽默榜的站点图标（奖杯，DESIGN.md 的 ICO） */
export const ICO_BENCH = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M5 2.5h6v3.5a3 3 0 0 1-6 0Z"/><path d="M5 3.5H3a2 2 0 0 0 2 3M11 3.5h2a2 2 0 0 1-2 3M8 9v2.5M5.5 13.5h5M6.5 11.5h3"/></svg>';

// 英文版不做站间联动：不显示站点切换菜单，页脚也不列其他站（SITES 只在中文版用）
const SITES: [string, string, string, string][] = [];
const CUR = C.shortName;

export const brandHtml = (tag = 'a') =>
  tag === 'a' ? `<a class="mw-brand" href="${HOME}">${CAT}<span>meow<span>llm</span></span></a>` : `<span class="mw-brand">${CAT}<span>meow<span>llm</span></span></span>`;

export function switchHtml() {
  return '';
  return `<details class="mw-switch">
      <summary aria-label="${C.switchLabel(CUR)}">${CUR}${CHEVRON}</summary>
      <nav class="mw-switch-menu" aria-label="${C.switchMenu}">
        ${SITES.map(([u, n, d, s]) => n === CUR ? `<a href="${u}" aria-current="page">${n}<i>${C.current}</i><small>${s}</small></a>` : `<a href="${u}">${n}<i>${d}</i><small>${s}</small></a>`).join('\n        ')}
      </nav>
    </details>`;
}

/** 普通页面（首页、各期页面、看源码）的页头。active = 'home' 或期的 id；pathname = 当前页路径（语言按钮的链接） */
export function headerHtml(episodes: EpisodeMeta[], active: string, pathname: string) {
  const shown = episodes.filter(e => e.live && (e.listed || e.id === active));
  // 导航只写"期号小字 + 标题"（title 里是完整的"第 N 期 · 标题"）：原来的"第 N 期 · 标题"八期就放不下一行，
  // 共用外壳的导航条超出部分是隐藏滚动，桌面上最右边一项会被切掉半个字
  const nav = shown.map(e => `<a href="${HOME}${e.id}/" title="${e.num} · ${e.title}"${e.id === active ? ' aria-current="page"' : ''}><span class="nav-no">${e.id.replace(/\D/g, '')}</span>${e.title}</a>`).join('');
  return `<header class="mw-header">
  <div class="mw-container mw-header-in">
    ${brandHtml()}
    ${switchHtml()}
    <nav class="mw-nav" aria-label="${C.siteNav}">${nav}</nav>
    <div class="mw-actions">${langBtnHtml(pathname)}${THEME_BTN}</div>
  </div>
</header>`;
}

export function footerHtml() {
  return `<footer class="mw-footer">
  <div class="mw-container mw-footer-in">
    <div class="mw-footer-brand">${brandHtml('span')}<small>${C.footerTag}</small></div>
    
    <p class="mw-footer-meta">${C.footerMeta}</p>
    <p class="mw-footer-meta">${C.footerArt}</p>
  </div>
</footer>`;
}

/** 对局页（人机对战 / bot 对打 / 回放）顶栏左侧：橙猫 + 面包屑。ep = { id, num, title }，mode 是当前页的名字 */
export function arenaCrumbsHtml(ep: { id: string; num: string; title: string }, mode: string) {
  return `<a class="mw-brand" href="${HOME}" title="${C.crumbsHomeTitle}">${CAT}<span class="mw-sr-only">${C.crumbsHomeSr}</span></a>
    <nav class="crumbs" aria-label="${C.crumbsLabel}"><a href="${HOME}">${C.crumbsHome}</a><span aria-hidden="true">›</span><a href="${HOME}${ep.id}/">${EN ? `${ep.num}<span class="crt"> · ${ep.title}</span>` : `${ep.num} · ${ep.title}`}</a><span aria-hidden="true">›</span><b aria-current="page" id="vmode">${mode}</b></nav>`;
}
