// 不带样式的小工具（对局页面也用，所以不能引入 site.css）
import C from '@i18n/common';
import { EN } from './lang';

const BASE = import.meta.env.BASE_URL;

export const esc = (t: unknown) => String(t ?? '').replace(/[&<>"]/g, c => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]!));
/** 站内静态文件地址。图片目录打包后带内容哈希（img.<哈希>/…，见 scripts/postbuild.mjs），可以 immutable 长缓存 */
export const asset = (p: string) => BASE + (__IMG_VER__ && p.startsWith('img/') ? `img.${__IMG_VER__}/` + p.slice(4) : p);

/** 头像：有图用图，没有就画个首字母圆章 */
export function avatarHtml(name: string | null | undefined, label: string, cls = '') {
  if (name) return `<img class="avatar ${cls}" src="${asset('img/avatars/' + name + '.webp')}" alt="${esc(label)}" loading="lazy" decoding="async">`;
  const ch = [...label.replace(/[（(].*$/, '').trim()][0] ?? '?';
  return `<span class="avatar-mono ${cls}" aria-hidden="true">${esc(ch)}</span>`;
}

/** 手机上长列表先显示前 4 项（CSS 只在窄屏下生效）：clipCls(n) 加在列表容器的 class 上，moreRow(n) 紧跟在容器后面 */
export const clipCls = (n: number) => (n > 5 ? ' clip clipped' : '');
export const moreRow = (n: number) => (n > 5 ? `<div class="more-row"><button class="btn" type="button" data-unclip>${C.showAll(n)}</button></div>` : '');

/** <picture>：AVIF 优先、WebP 兜底。variants = [[不带扩展名的路径, 宽度], …]（从小到大），只给一张时不写 srcset 宽度 */
export function pictureHtml(variants: [string, number][], opts: { sizes?: string; alt?: string; cls?: string; eager?: boolean; w?: number; h?: number } = {}) {
  const { sizes = '100vw', alt = '', cls = '', eager = false, w, h } = opts;
  const multi = variants.length > 1;
  const set = (ext: string) => multi ? variants.map(([p, x]) => `${asset(`${p}.${ext}`)} ${x}w`).join(', ') : asset(`${variants[0][0]}.${ext}`);
  const sz = multi ? ` sizes="${sizes}"` : '';
  const load = eager ? 'fetchpriority="high"' : 'loading="lazy"';
  const dim = w && h ? ` width="${w}" height="${h}"` : '';
  return `<picture><source type="image/avif" srcset="${set('avif')}"${sz}><img${cls ? ` class="${cls}"` : ''} src="${asset(variants[variants.length - 1][0] + '.webp')}"${multi ? ` srcset="${set('webp')}"${sz}` : ''} alt="${esc(alt)}"${dim} ${load} decoding="async"></picture>`;
}
/** 第 1/2/3/7 期的视频封面上印着中文大字：英文版改用同一张图的无字版（文件名多一段 -en） */
const enArt = (id: string) => EN && /^ep[1237]$/.test(id);
/** 首页卡片封面的路径前缀（不带扩展名）：英文版第 1/2/3/7 期换成无字版 */
export const coverBase = (id: string, base: string) => (enArt(id) ? base + '-en' : base);
/** 首屏背景图（主图保留）。第 6 期 / 首页、第 7～10 期用多尺寸主视觉；第 1~3 期中文版用 640 宽的封面，英文版用多尺寸无字版 */
export const heroBg = (which: 'kv' | 'ep7' | 'ep8' | 'ep9' | 'ep10' | 'ep11' | 'ep1' | 'ep2' | 'ep3') => {
  const kv = which === 'kv' ? 'ep6-kv' : which === 'ep7' || which === 'ep8' || which === 'ep9' || which === 'ep10' || which === 'ep11' ? `${which}-kv` : enArt(which) ? `${which}-kv-en` : '';
  const variants: [string, number][] = kv ? [640, 1024, 1600].map(w => [`img/art/${kv}-${w}`, w]) : [[`img/art/${which}-card`, 640]];
  return `<div class="hero-bg" aria-hidden="true">${pictureHtml(variants, { eager: true })}</div>`;
};
