// 普通页面（首页、各期页面、看源码）的外壳和小工具。页头 / 页脚的标记在 shell.ts（和预渲染共用），
// 主题由共用的 meow-shell.js 管（HTML 里同步引入），这里不再管主题。各期的登记在 src/episodes/epN.ts。
import './site.css';
import { esc, moreRow } from './ui';
import { fillBench } from './bench';
import { ALL_EPISODES } from './episodes';
import { footerHtml, headerHtml } from './shell';
import { bindLang } from './lang';
import C from '@i18n/common';
export { asset, avatarHtml, esc } from './ui';
export { EPISODES } from './episodes';

/** 插入页头页脚（预渲染过的页面已经有了，就不重复插） */
export function siteChrome(active: string) {
  if (!document.querySelector('.mw-header')) document.body.insertAdjacentHTML('afterbegin', headerHtml(ALL_EPISODES, active, location.pathname));
  bindLang();
  if (!document.querySelector('.mw-footer')) document.body.insertAdjacentHTML('beforeend', footerHtml());
}

/** 预渲染过的内容区：#page 上有 data-pre 时，客户端只需挂事件，不用重画 */
export const prerendered = (page: HTMLElement) => page.dataset.pre === '1';

export function loadingHtml(text = C.loading) {
  return `<div class="state"><span class="spinner"></span><span>${esc(text)}</span></div>`;
}
export function errorHtml(msg: string) {
  return `<div class="state error"><b>${C.error}</b><span>${esc(msg)}</span><button class="btn" type="button" onclick="location.reload()">${C.reload}</button></div>`;
}

/** 手机上长列表的"展开全部"按钮（标记由 ui.ts 的 clipOpen / moreRow 生成） */
export function bindClips(root: ParentNode = document) {
  root.querySelectorAll<HTMLButtonElement>('[data-unclip]').forEach(b => (b.onclick = () => {
    const row = b.closest('.more-row')!;
    row.previousElementSibling?.classList.remove('clipped');
    row.remove();
  }));
}

/** 换掉列表内容（切换分组时用）：同时更新手机上的折叠状态和"幽默榜第 N"联动 */
export function setList(el: HTMLElement, items: string[]) {
  el.innerHTML = items.join('');
  const n = items.length;
  el.classList.toggle('clip', n > 5);
  el.classList.toggle('clipped', n > 5);
  const next = el.nextElementSibling;
  if (next?.classList.contains('more-row')) next.remove();
  if (n > 5) { el.insertAdjacentHTML('afterend', moreRow(n)); bindClips(el.parentElement!); }
  fillBench(el);
}
