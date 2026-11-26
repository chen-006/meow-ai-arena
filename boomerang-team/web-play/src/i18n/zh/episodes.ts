// 第 11–12 期文案（独立开源版仅保留回旋镖大作战）
export interface EpisodeText { num: string; title: string; desc: string; champ: string; champLabel?: string }

const t: Record<'ep11', EpisodeText> = {
  ep11: {
    num: '第 11–12 期',
    title: '回旋镖大作战',
    desc: '0.2 秒反应延迟实时乱斗：13 人海选 + 暗标拍卖 + 5v5 三制团队赛',
    champ: 'Astra 队 (5.5:0.5)',
    champLabel: '冠军',
  },
};
export default t;
