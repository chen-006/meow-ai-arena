// 语言：由路径决定（中文在站点根，英文在 /en/ 下），打包时由 vite.config.ts 注入 __LANG__。
// 这里放语言相关的小工具：本语言的站点根、两种语言之间的路径换算、语言切换按钮、偏好 cookie，
// 以及中文页 <head> 里同步执行的自动跳转脚本（scripts/postbuild.mjs 写进 HTML）。
import C from '@i18n/common';
import { ZH_ONLY } from './episodes';

const BASE = import.meta.env.BASE_URL;
export const LANG = __LANG__;
export const EN = __LANG__ === 'en';
/** 数字格式化用的 locale */
export const LOCALE = EN ? 'en-US' : 'zh-CN';
export const fmtNum = (n: number) => n.toLocaleString(LOCALE);
/** 本语言的站点首页（中文 /，英文 /en/），站内绝对链接都从这里拼 */
export const HOME = BASE + (EN ? 'en/' : '');
/** 正式域名（hreflang 用绝对地址） */
export const ORIGIN = 'https://arena.meowllm.top';

/** 站内路径在另一种语言下的对应路径：/ep6/play.html ↔ /en/ep6/play.html */
export function altPath(pathname: string, to: 'zh' | 'en'): string {
  let rest = pathname.startsWith(BASE) ? pathname.slice(BASE.length) : pathname.replace(/^\//, '');
  if (rest === 'en' || rest.startsWith('en/')) rest = rest.slice(rest === 'en' ? 2 : 3);
  return BASE + (to === 'en' ? 'en/' : '') + rest;
}

/** 路径属于只有中文版的期（/ep8/…）：没有对应的英文页 */
export function zhOnlyPath(pathname: string): boolean {
  const rest = pathname.startsWith(BASE) ? pathname.slice(BASE.length) : pathname.replace(/^\//, '');
  const m = rest.match(/^(ep\d+)(\/|$)/);
  return !!m && ZH_ONLY.has(m[1]);
}

/** 页头里的语言按钮（中文页显示 EN、英文页显示"中文"）。pathname 为当前页路径（预渲染时由构建脚本给出）。
 *  只有中文版的期（第 8 期）不显示：那里没有英文页可去，指向英文首页又会让人以为是这一页的英文版。 */
export function langBtnHtml(pathname: string) {
  if (zhOnlyPath(pathname)) return '';
  const to = EN ? 'zh' : 'en';
  return `<a class="mw-lang" href="${altPath(pathname, to)}" hreflang="${to === 'en' ? 'en' : 'zh-CN'}" lang="${to === 'en' ? 'en' : 'zh-CN'}" data-mw-lang="${to}" aria-label="${C.langBtnLabel}" title="${C.langBtnLabel}">${C.langBtn}</a>`;
}

/** 记住语言偏好：cookie mw_lang（四站共用，Domain=.meowllm.top）+ localStorage 兜底 */
export function saveLang(l: 'zh' | 'en') {
  try { localStorage.setItem('mw_lang', l); } catch { /* 存不了就只写 cookie */ }
  document.cookie = 'mw_lang=' + l + ';path=/;max-age=31536000;SameSite=Lax' +
    (/(^|\.)meowllm\.top$/.test(location.hostname) ? ';domain=.meowllm.top' : '') +
    (location.protocol === 'https:' ? ';Secure' : '');
}

let bound = false;
/** 语言按钮：链接带上当前的 search 和 hash；点击时记住偏好。英文页另外把共用外壳写的中文主题按钮标签换成英文 */
export function bindLang() {
  if (bound || typeof document === 'undefined') return;
  bound = true;
  const sync = () => document.querySelectorAll<HTMLAnchorElement>('a[data-mw-lang]').forEach(a => {
    a.href = altPath(location.pathname, a.dataset.mwLang as 'zh' | 'en') + location.search + location.hash;
  });
  document.addEventListener('click', e => {
    const a = (e.target as Element).closest?.('a[data-mw-lang]') as HTMLAnchorElement | null;
    if (!a) return;
    sync();
    saveLang(a.dataset.mwLang as 'zh' | 'en');
  });
  document.addEventListener('pointerover', e => { if ((e.target as Element).closest?.('a[data-mw-lang]')) sync(); });
  document.addEventListener('focusin', e => { if ((e.target as Element).closest?.('a[data-mw-lang]')) sync(); });
  if (EN) {
    // meow-shell.js（共用，只读）在 DOMContentLoaded 和切换主题时把按钮标签写成中文，随后派发 mw:theme；这里接着改成英文
    const relabel = () => {
      const p = (document.documentElement.getAttribute('data-mw-pref') || 'system') as keyof typeof C.themeName;
      const t = C.themeLabel(C.themeName[p] ?? p);
      document.querySelectorAll('[data-mw-theme]').forEach(b => { b.setAttribute('aria-label', t); (b as HTMLElement).title = t; });
    };
    document.addEventListener('mw:theme', relabel);
    if (document.readyState !== 'loading') relabel();
  }
}

/**
 * 中文页 <head> 里同步内联的自动跳转（构建时写进 HTML，在样式表之前执行，跳转前不画任何东西）：
 * 没有语言偏好、且浏览器语言里没有 zh 开头的 → 跳到对应的 /en/ 页面；偏好是 en 也跳。
 * 英文页不往回跳（分享出去的英文链接要能直接看），所以不会来回跳。爬虫不跳。
 */
export const REDIRECT_SCRIPT = `(function(){try{var n=navigator,u=n.userAgent||'';` +
  `if(/bot|crawl|spider|slurp|bingpreview|mediapartners|facebookexternalhit|embedly|preview|lighthouse|pagespeed/i.test(u))return;` +
  `var m=document.cookie.match(/(?:^|;\\s*)mw_lang=(zh|en)/),p=m&&m[1];` +
  `if(!p){try{p=localStorage.getItem('mw_lang')}catch(e){}}` +
  `var L=n.languages&&n.languages.length?n.languages:[n.language||''];` +
  `var zh=false;for(var i=0;i<L.length;i++)if(/^zh/i.test(L[i]))zh=true;` +
  `if(p==='en'||(p!=='zh'&&!zh)){var b=${JSON.stringify(BASE)},s=location.pathname;` +
  `if(s.indexOf(b)===0)s=s.slice(b.length);else s=s.replace(/^\\//,'');` +
  `document.documentElement.style.visibility='hidden';` +
  `location.replace(b+'en/'+s+location.search+location.hash)}}catch(e){}})();`;
