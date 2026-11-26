// 各期的登记表：自动收集 src/episodes/epN.ts 导出的 meta，按期号排序；文字（期号、标题、简介、冠军）来自 @i18n/episodes。
import T from '@i18n/episodes';

/** src/episodes/epN.ts 里登记的、和语言无关的部分 */
export interface EpisodeRegistry {
  id: string;
  games: number; cover: string | null;
  live: boolean;    // 网站上的页面做好了没有
  listed: boolean;  // 是否在首页和顶栏显示（视频还没发布的期设 false，页面仍可直接访问）
  zhOnly?: boolean; // 只有中文版（英文构建不生成这一期的页面，中文页不写 hreflang、不自动跳英文、不显示语言按钮）
}
export interface EpisodeMeta extends EpisodeRegistry {
  num: string; title: string; desc: string; champ: string;
  champLabel?: string;  // 首页卡片上冠军徽标的前缀，默认"冠军"
}

const mods = import.meta.glob<{ meta: EpisodeRegistry }>('../episodes/ep*.ts', { eager: true });
/** 只有中文版的期（英文字典里也没有它们的文字） */
export const ZH_ONLY = new Set(Object.values(mods).filter(m => m.meta.zhOnly).map(m => m.meta.id));
export const ALL_EPISODES: EpisodeMeta[] = Object.values(mods)
  .filter(m => (T as Record<string, unknown>)[m.meta.id])     // 本语言没有文字的期（英文版的 zhOnly 期）整个不出现
  .map(m => ({ ...m.meta, ...(T as Record<string, typeof T.ep11>)[m.meta.id] }))
  .sort((a, b) => +a.id.slice(2) - +b.id.slice(2));
/** 首页和导航里显示的期 */
export const EPISODES = ALL_EPISODES.filter(e => e.listed);
