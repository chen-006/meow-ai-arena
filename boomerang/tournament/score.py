"""第 11 集《回旋镖大作战》海选个人榜：从公开的逐局结果重算，并和比赛时的成绩表逐项核对。

    python tournament/score.py            重算 results/海选成绩.json（2000 次重抽样，约 20 秒），逐项核对，打印榜单
    python tournament/score.py --quick    只算点估计（不做重抽样，1 秒），核对实力值和名次

读 results/games.jsonl.gz（32,904 局逐局结果），按比赛时的出榜代码换算成每局的得分：
一对一：名次高的赢；四人局：名次分（3/2/1/0，并列平分）+ 每次击杀 1 分（workspace/match.py 的 pts_for_ranks、KILL_POINTS）。
再交给比赛时的出榜程序 tournament/rating.py（Bradley-Terry 实力值，bt.py）拟合。抽签种子和 lot 与比赛时相同。
不需要编译任何东西，也不需要第三方 Python 包。重打全部对局见 tournament/rerun.py。
"""
import gzip
import json
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EP = HERE.parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(EP / 'workspace'))
import rating  # noqa: E402
from match import KILL_POINTS, pts_for_ranks  # noqa: E402

DRAW_SEED = 'a11-ep11-haixuan-20261116'
NAMES = ['astra', 'sol', 'fable', 'opus', 'carbon', 'hydrogen', 'grok', 'deepseek', 'kimi', 'qwen', 'glm', 'mimo', 'doubao']
SHOW = {'astra': 'GPT-6.2 Astra', 'sol': 'GPT-6.2 Sol', 'fable': 'Claude Fable 5.2', 'opus': 'Claude Opus 5.5',
        'carbon': 'Gemini 4 Carbon', 'hydrogen': 'Gemini 4 Hydrogen', 'grok': 'Grok 4.7', 'glm': 'GLM 5.3',
        'deepseek': 'DeepSeek-V4.1-Flash', 'kimi': 'Kimi K3.1 Pro', 'qwen': '千问 Qwen4-Max', 'mimo': 'MiMo V2.6 Pro',
        'doubao': '豆包 Seed-Evolving'}


def load_games(path=EP / 'results' / 'games.jsonl.gz'):
    with gzip.open(path, 'rt', encoding='utf-8') as f:
        return [json.loads(line) for line in f if line.strip()]


def observations(rows, names=NAMES):
    """逐局结果 → 出榜程序的输入（和比赛时 formal62.py 的换算逐字相同）。"""
    obs = []
    for r in rows:
        pl = sorted((dict(seat=r['names'].index(p['name']), **p) for p in r['players']), key=lambda p: p['seat'])
        if r['mode'] == 0:
            obs.append((dict(mode=0, map=r['map'], names=sorted(r['names'], key=names.index)), {p['name']: -p['rank'] for p in pl}))
        else:
            pts = pts_for_ranks(pl)
            obs.append((dict(mode=1, map=r['map']), {p['name']: pts[p['seat']] + KILL_POINTS * p['kills'] for p in pl}))
    return obs


def _same_score_value(a, b):
    if isinstance(a, list):
        return isinstance(b, list) and len(a) == len(b) and all(
            _same_score_value(x, y) for x, y in zip(a, b)
        )
    if isinstance(a, float):
        return abs(a - b) < 1e-9
    return a == b


def main():
    quick = '--quick' in sys.argv
    sys.stdout.reconfigure(encoding='utf-8')
    rows = load_games()
    obs = observations(rows)
    nd = sum(1 for g, _ in obs if g['mode'] == 0)
    print(f'逐局结果 {len(rows)} 局：一对一 {nd}，四人局 {len(rows) - nd}')
    lot = NAMES.copy()
    random.Random(DRAW_SEED + ':ranking').shuffle(lot)
    res = rating.individual(obs, NAMES, lot, DRAW_SEED, boot=0 if quick else 2000)
    off = json.load(open(EP / 'results' / '海选成绩.json', encoding='utf-8'))
    keys = ['rating', 'duel_rating', 'ffa_rating', 'rank'] + ([] if quick else ['rating_ci95', 'duel_rating_ci95', 'ffa_rating_ci95', 'rank_ci95'])
    bad = []
    if off['lot'] != lot:
        bad.append('抽签顺序 lot')
    if res['ranking'] != off['ranking']:
        bad.append('名次顺序')
    for n in NAMES:
        for k in keys:
            a, b = res['players'][n][k], off['players'][n][k]
            if not _same_score_value(a, b):
                bad.append(f'{n}.{k}: 重算 {a} / 成绩表 {b}')
    if not quick:
        for g1, g2 in zip(res['gaps'], off['gaps']):
            if (g1['higher'], g1['lower'], g1['within_error']) != (g2['higher'], g2['lower'], g2['within_error']):
                bad.append(f'误差判断 {g1["higher"]}-{g1["lower"]}')
    print('名次  选手                  个人实力值  一对一   四人局   95% 区间')
    for n in res['ranking']:
        p = res['players'][n]
        ci = p['rating_ci95']
        ci_s = f'[{ci[0]:+.0f}, {ci[1]:+.0f}]' if ci else ''
        print(f"{p['rank']:>3}   {SHOW[n]:<20} {p['rating']:+8.1f}  {p['duel_rating']:+7.1f}  {p['ffa_rating']:+7.1f}   {ci_s}")
    if not quick:
        tie = [f"{SHOW[g['higher']]} / {SHOW[g['lower']]}" for g in res['gaps'] if g['within_error']]
        print('差距在误差内（名次照排）：' + '；'.join(tie))
    if bad:
        print('核对不通过：')
        for b in bad[:20]:
            print('  ' + b)
        sys.exit(1)
    print(f'核对通过：{len(NAMES)} 位选手的{"实力值和名次" if quick else "实力值、区间、名次和误差判断"}与 results/海选成绩.json 完全一致。')


if __name__ == '__main__':
    main()
