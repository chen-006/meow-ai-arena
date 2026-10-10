"""第二阶段（需求变更后）负载生成器（公开版）：在第一阶段配置的基础上，PARAMS 行末尾追加 agingEvery carryCost。

  python tools/gen2.py 配置名 种子 > 输出.in
  python tools/gen2.py --list
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen  # noqa: E402

# 配置名: (第一阶段的基础配置, agingEvery, carryCost)
BASE = {
    # ---- 公开（随变更说明一起发给选手）
    'p2_small':      ('pub_small', 80, 2),
    'p2_flood':      ('pub_flood', 60, 2),
    'p2_crowded':    ('pub_crowded', 150, 3),
    'p2_dynamic':    ('pub_dynamic', 100, 2),
}
PROFILES = {k: dict(gen.PROFILES[b], extra=(a, c)) for k, (b, a, c) in BASE.items() if b in gen.PROFILES}

# ---- 大负载（公开）：规模接近正式评测，用来看快慢。workloads2/big/ 里是这三个配置、种子 1 生成的，带标准输出。
# 换种子可以生成更多同类输入（没有标准输出）。正式评测的隐藏负载规模相当或更大，参数不同。
BIG = {
    'p2_big_flood':   dict(W=56, H=43, T=4000, R=36,  chargers=5,  rate=2.2, cancel=0.10, block=0.03, dur=(30, 150), extra=(60, 2)),
    'p2_big_dynamic': dict(W=100, H=70, T=4000, R=80,  chargers=12, rate=0.8, cancel=0.05, block=0.5,  dur=(5, 60),   extra=(100, 2)),
    'p2_big_crowded': dict(W=90,  H=64, T=5000, R=160, chargers=24, rate=1.3, cancel=0.03, block=0.2,  dur=(10, 100), extra=(150, 3)),
}
PROFILES.update(BIG)


def generate(name, seed):
    return gen.generate_from(name, PROFILES[name], seed)


if __name__ == '__main__':
    if len(sys.argv) == 2 and sys.argv[1] == '--list':
        for k, v in PROFILES.items():
            print(k, v)
        sys.exit(0)
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(1)
    sys.stdout.write(generate(sys.argv[1], int(sys.argv[2])))
