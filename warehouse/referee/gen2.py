"""第二阶段（需求变更后）负载生成器：在第一阶段配置的基础上，PARAMS 行末尾追加 agingEvery carryCost。

  python 裁判/gen2.py 配置名 种子 > 输出.in
  python 裁判/gen2.py --list
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
    # ---- 隐藏（第二阶段正式评测）
    'h2_medium':     ('hid_medium', 120, 2),
    'h2_flood':      ('hid_flood', 50, 2),
    'h2_lowbattery': ('hid_lowbattery', 90, 3),
    'h2_dynamic':    ('hid_dynamic', 100, 2),
    'h2_mixed':      ('hid_mixed', 70, 2),
    'h2_long':       ('hid_long', 200, 2),
    # ---- 重负载（2026-10-09 重赛新增）
    'h2_heavy_a':    ('hid_heavy_a', 80, 2),
    'h2_heavy_b':    ('hid_heavy_b', 120, 3),
    'h2_heavy_c':    ('hid_heavy_c', 60, 2),
}
PROFILES = {k: dict(gen.PROFILES[b], extra=(a, c)) for k, (b, a, c) in BASE.items() if b in gen.PROFILES}


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
