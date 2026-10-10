"""把本期所有成绩汇成一份文档（视频和开源都从这里取数）：赛事/结果/最终成绩.md、最终成绩.json。

  python 裁判/成绩汇总.py

前提：团队计分.py、盲评汇总.py、终版复测判定.py、赛后流水线.sh 都已跑完。只读已有结果，不重新评测。
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import EVENT, HERE, teams  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')
R = os.path.join(EVENT, '结果')


def J(p):
    with open(p, encoding='utf-8') as f:
        return json.load(f)


def JL(p):
    with open(p, encoding='utf-8') as f:
        return [json.loads(x) for x in f if x.strip()]


def geo(v):
    return math.exp(sum(math.log(x) for x in v) / len(v)) if v else None


def main():
    ts = teams()
    names = [t['team'] for t in ts]
    who = {t['team']: t for t in ts}
    res = J(os.path.join(R, '团队成绩.json'))['teams']
    res0 = J(os.path.join(R, '团队成绩_初测.json'))['teams']
    m1 = J(os.path.join(HERE, '隐藏负载', 'manifest.json'))
    m2 = J(os.path.join(HERE, '隐藏负载2', 'manifest.json'))
    r1 = {(r['entry'].split('#')[0], r['workload']): r for r in JL(os.path.join(R, 'results1.jsonl'))}
    r2 = {(r['entry'], r['workload']): r for r in JL(os.path.join(R, 'results2.jsonl'))}
    r20 = {(r['entry'], r['workload']): r for r in JL(os.path.join(R, 'results2_初测.jsonl'))}
    lead = {(r['entry'], r['workload']): r for r in JL(os.path.join(R, '主程基准.jsonl'))}
    ws = list(m2)
    label = lambda t: '%s → %s' % (who[t]['主程'], who[t]['攻手'])
    L = ['# 第 5 期 · 最终成绩（重赛，精确计时）', '',
         '> 由 `裁判/成绩汇总.py` 生成，数据来自 `赛事/结果/`、`赛事/盲评/`、`赛事/击穿结果/`、`赛事/终版复测/`。计分口径见 `赛事/重赛记录.md` 和 `说明.md`。', '']
    L += [open(os.path.join(R, '团队成绩.md'), encoding='utf-8').read().replace('# 团体攻防接力赛 成绩', '## 一、三张榜').replace('\n## ', '\n### '), '']

    # ---- 第二阶段逐组
    L += ['## 二、第 3 轮终版在 9 组隐藏负载上的表现（精确计时）', '',
          '每格：终版 CPU 秒 / 相对参考实现的加速比。参考实现是干净但没有优化的写法。', '',
          '| 负载 | 参考实现 | ' + ' | '.join(label(t) for t in names) + ' |', '|---|---:|' + '---:|' * len(names)]
    for w in ws:
        row = []
        for t in names:
            r = r2[(t, w)]
            row.append('%.3f 秒 / %.0f 倍' % (r['cpu'], r['baseline_cpu'] / max(r['cpu'], 1e-4)) if r['status'] == 'ok' else '出错')
        L.append('| %s | %.1f 秒 | %s |' % (w[3:], m2[w]['baseline_cpu'], ' | '.join(row)))
    L += ['', '重负载 `heavy_a/b/c` 是重赛时新增的 3 组；其余 6 组和第 1 轮的隐藏负载是同一张图、同一批事件，只多了两个新参数。', '']

    # ---- 相对屎山基线
    L += ['## 三、相对原始屎山基线快了多少（6 组有基线成绩的负载，几何平均）', '',
          '屎山基线不认识新格式，这里用同图旧格式上的基线用时（第 1 轮测的），所以是近似对比。新增的 3 组重负载没有测屎山基线。', '',
          '| 队 | 第 1 轮主程原版 | 第 3 轮终版 | 接手后又快了（9 组，含重负载） |', '|---|---:|---:|---:|']
    for t in names:
        v1, v2 = [], []
        for w in ws:
            h = 'hid_' + w[3:]
            if h in m1:
                a = r1.get((t, h))
                if a and a['status'] == 'ok':
                    v1.append(m1[h]['baseline_cpu'] / max(a['cpu'], 1e-4))
                b = r2[(t, w)]
                if b['status'] == 'ok':
                    v2.append(m1[h]['baseline_cpu'] / max(b['cpu'], 1e-4))
        g = res[t].get('gain_geomean')
        L.append('| %s | %s | %s | %s |' % (label(t), '%.0f 倍' % geo(v1) if v1 else '—', '%.0f 倍' % geo(v2) if v2 else '出错',
                                           '%.1f 倍' % g if g and res[t]['correct_rate'] > 0 else '—'))
    L.append('')

    # ---- 初测 vs 精确
    L += ['## 四、"快到碰到计时精度下限"：初测和精确重测的对比', '',
          'Windows 进程 CPU 计时的最小刻度约 0.016 秒。初测每组只跑 1～3 遍取中位数，前三名在 6 组轻负载上只用了 2～3 个刻度（0.03～0.05 秒），好几组数字完全相同。',
          '精确重测把每组连跑多遍直到累计 CPU 达到 2 秒再取平均，规则和公式不变。名次没有变化。', '',
          '| 负载 | ' + ' | '.join(who[t]['攻手'] for t in names if res[t]['correct_rate'] > 0) + ' |', '|---|' + '---:|' * sum(1 for t in names if res[t]['correct_rate'] > 0)]
    for w in ws:
        cells = []
        for t in names:
            if res[t]['correct_rate'] > 0:
                a, b = r20[(t, w)], r2[(t, w)]
                cells.append('%.3f → %.4f 秒（%d 遍）' % (a['cpu'], b['cpu'], b.get('precise', {}).get('runs', len(b['runs']))))
        L.append('| %s | %s |' % (w[3:], ' | '.join(cells)))
    L += ['', '| 攻手 | 接手速度 初测 → 精确 | 增值 初测 → 精确 | 攻手榜 初测 → 精确 | 团队总分 初测 → 精确 |', '|---|---:|---:|---:|---:|']
    for t in sorted(names, key=lambda x: -res[x]['attacker']):
        a, b = res0[t], res[t]
        L.append('| %s | %.1f → %.1f | %.1f → %.1f | %.1f → %.1f | %.1f → %.1f |' % (who[t]['攻手'], a['S2'], b['S2'], a['V'], b['V'], a['attacker'], b['attacker'], a['team_total'], b['team_total']))
    L.append('')

    # ---- 盲评和百分制
    blind = J(os.path.join(EVENT, '盲评', '汇总.json'))
    table = J(os.path.join(EVENT, '盲评', '对照表.json'))
    judges = ['GPT-6 Astra', 'Opus 5.5', 'DeepSeek V4.1 Flash']
    pct = {j: J(os.path.join(EVENT, '盲评', '结果', j, '百分制.json')) for j in judges if os.path.exists(os.path.join(EVENT, '盲评', '结果', j, '百分制.json'))}
    L += ['## 五、盲评（可维护性）', '', '正式分 0～10：三位裁判在同一个对话里评 12 份匿名代码，评到本队代码的那一票不计（括号里的就是没计入的）。百分制是评完后追问的整体印象分，只展示、不计分（屎山基线 10、干净参考 75）。', '',
          '| 代码 | 作者 | ' + ' | '.join(judges) + ' | 正式分 | 百分制（' + ' / '.join(j.split()[-1] if j.startswith('GPT') else j.split()[0] for j in judges) + '） | 百分制平均 |', '|---|---|---:|---:|---:|---:|---|---:|']
    rows = []
    for code, info in table.items():
        t, kind = info['team'], info['kind']
        d = blind[t]['明细'][kind]
        members = {who[t]['主程'], who[t]['攻手']}
        ps = [pct[j][code]['分'] for j in judges if j in pct and code in pct[j]]
        rows.append((-blind[t][kind], '| %s %s | %s | %s | **%.1f** | %s | %s |' % (
            t, kind, who[t]['主程'] if kind == '主程' else label(t),
            ' | '.join(('（%.1f）' if j in members else '%.1f') % d['各裁判'][j] for j in judges), blind[t][kind],
            ' / '.join(str(pct[j][code]['分']) if j in pct and code in pct[j] else '—' for j in judges),
            '%.0f' % (sum(ps) / len(ps)) if ps else '—')))
    L += [r for _, r in sorted(rows)]
    L += ['', '一句话点评见 `赛事/盲评/汇总.md`。', '']

    # ---- 找反例、复测、体检
    pts = J(os.path.join(EVENT, '击穿结果', '缺陷点归类.json'))['points']
    L += ['## 六、第 2 轮找到的 7 个缺陷点', '', '| 缺陷点 | 类型 | 找到的攻手 | 分值 |', '|---|---|---|---:|']
    for p in pts:
        L.append('| %s（%s） | %s | %s | %.1f |' % (p['id'], who[p['target']]['主程'], p['kind'], '、'.join(who[a]['攻手'] for a in p['found_by']), 1 + (5 - len(p['found_by'])) / 5))
    L += ['', '详见 `赛事/击穿结果/缺陷点归类.md`。', '']
    rt = J(os.path.join(EVENT, '终版复测', '判定.json'))
    L += ['## 七、终版复测（赛后额外项目，不计入总分）', '', 'Astra、Opus 各开一个对话，给别队的 5 份终版找反例，45 分钟，参考实现公开给他们。', '',
          '| 裁判 | 终版 | 用例 | 判定 | 说明 |', '|---|---|---|---|---|']
    for r in sorted(rt, key=lambda r: (r['队'], r['裁判'], r['file'])):
        L.append('| %s | %s（%s） | %s | %s | %s |' % (r['裁判'], r['队'], r['终版作者'], r['file'], '成立' if r['verdict'] == '击穿' else '不成立', r['why'][:120]))
    L += ['', '成立的缺陷点 2 个：Grok 的终版没有实现新需求（两位裁判都找到）；Fable 的终版在"上千个被封的取货点超过 1024 槽距离场池 + 每 tick 封锁抖动"时性能退化到超时（只有 Astra 找到，Opus 试过同方向但没超过池容量）。',
          'Sonnet、Sol、Opus、Astra 四份终版没有被找到反例。两位裁判的检查记录在 `赛事/终版复测/提交/<裁判>/cases/*/说明.txt`。', '']
    probe = J(os.path.join(R, '终版体检', '性能探针.json'))
    L += ['## 八、终版体检（主办方，不计入总分）', '',
          '- 边界对拍：每份终版 1500 个随机小用例（新格式），和参考实现逐字节比较。Sonnet、Fable、Sol、Opus、Astra 五份都是 **0 个不一致**。',
          '- 极端对拍：1500 个里跳过 196 个参考实现太慢的，五份都是 **0 个不一致**。',
          '- 性能探针：第 2 轮成立的 %d 个超时类反例换成新格式后各跑一次（限时 60 秒）。没有超时，也没有输出不同。有三处比参考实现慢：' % len(probe), '',
          '| 用例 | 参考实现 | ' + ' | '.join(who[t]['攻手'] for t in names) + ' |', '|---|---:|' + '---:|' * len(names)]
    for r in probe:
        L.append('| %s | %.1f 秒 | %s |' % (r['用例'], r['参考实现秒'], ' | '.join(r['终版'][t] for t in names)))
    L += ['', '日志：`赛事/运行记录/赛后流水线.log`。Grok 的终版在新格式上全部异常退出，没有参加对拍。', '']
    with open(os.path.join(R, '最终成绩.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(L) + '\n')
    print('已写入', os.path.join(R, '最终成绩.md'), len('\n'.join(L)), '字')


if __name__ == '__main__':
    main()
