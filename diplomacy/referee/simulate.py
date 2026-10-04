"""离线假设推演：只读公开局面，不接触真实对局。"""
import argparse
import json
from pathlib import Path
from diplomacy import Game

END_YEAR = 1920  # 1901 至 1920 年


def surviving_powers(game):
    return [p for p in game.powers if game.get_units(p) or game.get_centers(p)]


def final_result(game, reason, phase):
    solo = reason == 'victory'
    winners = list(game.outcome[1:]) if solo else []
    return dict(type='solo' if solo else 'draw', reason=reason, phase=phase, winners=winners,
                draw_participants=[] if solo else surviving_powers(game),
                scores=scores(game.get_centers(), winners),
                centers=game.get_centers(), units=game.get_units())


def scores(centers, winners):
    # 单独胜利者 100 分；否则按补给中心数的平方分配 100 分（SoS）
    if winners:
        return {p: 100.0 if p in winners else 0.0 for p in centers}
    squares = {p: len(c) ** 2 for p, c in centers.items()}
    total = sum(squares.values()) or 1
    return {p: round(100 * v / total, 2) for p, v in squares.items()}


def finish_after_fall(game, processed_phase):
    # 18 中心胜利在秋季撤退之后判定，先于年限和局。
    if game.is_game_done:
        return final_result(game, 'victory', processed_phase)
    if (processed_phase.startswith('F') and int(processed_phase[1:5]) >= END_YEAR
            and not game.get_current_phase().endswith('R')):
        game.draw(winners=surviving_powers(game))
        return final_result(game, 'turn_limit', processed_phase)
    return None


def simulate(snapshot, orders):
    if not isinstance(orders, dict):
        raise ValueError('假设文件应为 {"国家": ["命令", ...]} 格式')
    game = Game()
    game.set_state(snapshot['state'])
    possible = game.get_all_possible_orders()
    for country, commands in orders.items():
        country = country.upper()
        if country not in game.powers or not isinstance(commands, list):
            raise ValueError('国家或命令格式无效：' + country)
        commands = [' '.join(str(c).upper().split()) for c in commands]
        legal = {o for loc in game.get_orderable_locations(country) for o in possible[loc]}
        bad = [c for c in commands if c not in legal]
        if bad:
            raise ValueError(f'{country} 的假设命令不合法：' + '；'.join(bad))
        game.set_orders(country, commands)
    phase = game.get_current_phase()
    game.process()
    status = game.get_order_status()
    lines = [f'假设推演 {phase}（未写命令的部队按驻守处理）']
    for country, units in status.items():
        failed = [f'{u}（{"、".join(map(str, r))}）' for u, r in units.items() if r]
        if failed:
            lines.append(f'{country} 失败：' + '；'.join(failed))
    lines.append('推演后部队：')
    for country, units in game.get_units().items():
        if units:
            lines.append(f'  {country}：{", ".join(units)}')
    return '\n'.join(lines)


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('snapshot')
    p.add_argument('orders')
    a = p.parse_args()
    try:
        print(simulate(json.loads(Path(a.snapshot).read_text(encoding='utf-8-sig')),
                       json.loads(Path(a.orders).read_text(encoding='utf-8-sig'))))
    except ValueError as exc:
        print('推演失败：' + str(exc))
