import type { EpisodeRegistry } from '../common/episodes';

// 第 11–12 期（回旋镖大作战 / OA 争霸）：
//   第 11 集（海选 32,904 局 + 拍卖）+ 第 12 集（5v5 团队赛 10,600 局 + 赛后矩阵 54,000 局）
// listed: false —— 视频未发先不挂首页列表，直链 /ep11/ 可完整访问。
// zhOnly: true —— 只有中文版。
export const meta: EpisodeRegistry = {
  id: 'ep11',
  games: 43504,
  cover: null,
  live: true,
  listed: false,
  zhOnly: true,
};
