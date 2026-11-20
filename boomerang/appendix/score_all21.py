"""附录：全员大混战榜（21 位，参考用，不改正式榜）从公开数据重算并核对。

    python appendix/score_all21.py        约 1 分钟

21 位 = 正式 13 位（Astra、Sol 是 6.2 正式版）+ 灰度版 Astra、Sol + 6 位对照选手。比赛时没有让 21 位重新打满一整届，
而是把已有的对局拼起来、只补没打过的：
- 灰度版海选（灰度 Astra、Sol 和另外 11 位，赛程同正式）：appendix/results/灰度版海选/obs.json.gz
- 正式海选里含 6.2 Astra、Sol 的局（其余局和灰度版海选重复）：results/games.jsonl.gz
- 对照赛（6 位对照选手和 13 位的一对一，加 19 人四人局）：appendix/results/对照赛/obs_新打的.json.gz
- 补打（6.2 两位对灰度两位和 6 位对照的一对一，加 210 轮 21 人四人局）：appendix/results/全员大混战/obs_补打.json.gz
实力值用正式出榜程序（tournament/rating.py，重抽样 500 次），再平移到"正式 13 位的平均 = 0"。
"""
import gzip
import json
import random
import sys
from pathlib import Path

EP = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(EP / 'tournament'))
import rating  # noqa: E402
from score import load_games, observations, NAMES as NAMES13  # noqa: E402

DRAW_SEED = 'a11-ep11-super21-20261117'
official = ['astra62', 'sol62', 'fable', 'opus', 'carbon', 'hydrogen', 'grok', 'deepseek', 'kimi', 'qwen', 'glm', 'mimo', 'doubao']
gray = ['astra', 'sol']
controls = ['astra6', 'sol61', 'terra', 'luna', 'sonnet', 'haiku']
names = official + gray + controls
D = EP / 'appendix' / 'results'


def gz(p):
    with gzip.open(p, 'rt', encoding='utf-8') as f:
        return [tuple(x) for x in json.load(f)]


def ren(obs, mp):
    out = []
    for g, v in obs:
        g = dict(g)
        if 'names' in g:
            g['names'] = [mp.get(x, x) for x in g['names']]
        out.append((g, {mp.get(k, k): x for k, x in v.items()}))
    return out


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    old13 = gz(D / '灰度版海选' / 'obs.json.gz')
    rows = load_games()
    obs62 = observations(rows, NAMES13)
    new62 = ren([o for r, o in zip(rows, obs62) if {'astra', 'sol'} & set(r['names'])], {'astra': 'astra62', 'sol': 'sol62'})
    ctl = gz(D / '对照赛' / 'obs_新打的.json.gz')
    extra = gz(D / '全员大混战' / 'obs_补打.json.gz')
    allobs = old13 + new62 + ctl + extra
    print(f'共 {len(allobs)} 局：灰度版海选 {len(old13)}、正式海选含 6.2 两位 {len(new62)}、对照赛 {len(ctl)}、补打 {len(extra)}')
    lot = names.copy()
    random.Random(DRAW_SEED + ':ranking').shuffle(lot)
    res = rating.individual(allobs, names, lot, DRAW_SEED, boot=500)
    ref = json.load(open(D / '全员大混战' / '全员榜.json', encoding='utf-8'))
    bad = [n for n in names for k in ('rating', 'duel_rating', 'ffa_rating', 'rating_ci95', 'rank')
           if res['players'][n][k] != ref['原始']['players'][n][k]
           and not (isinstance(res['players'][n][k], float) and abs(res['players'][n][k] - ref['原始']['players'][n][k]) < 1e-9)]
    sh = sum(res['players'][n]['rating'] for n in official) / len(official)
    for i, n in enumerate(sorted(names, key=lambda n: -res['players'][n]['rating']), 1):
        grp = '正式' if n in official else ('灰度' if n in gray else '对照')
        print(f'{i:>3} {n:<9} {grp}  {res["players"][n]["rating"] - sh:+7.1f}')
    if len(allobs) != ref['局数'] or bad:
        print('核对不通过：', len(allobs), ref['局数'], bad[:10])
        sys.exit(1)
    print('核对通过：21 位的实力值、区间和名次与 appendix/results/全员大混战/全员榜.json 完全一致。')


if __name__ == '__main__':
    main()
