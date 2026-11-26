"""第 12 集团队赛组队工具的共用部分（只用标准库）。

赛事配置（例如 赛事配置_演练.json）定比赛根目录、选手包、两队名单；生成后的目录：
  <root>\\<队名>\\<名字>\\        每人一个文件夹（选手包 + 自己的 player.cpp + 队友程序\\）
  <root>\\_主办方\\               主办方目录，选手不碰：
      阶段.json                   现在是开发 / 冻结 / 第几回合决策、截止时刻
      kit\\                       生成时复制的选手包（编译检查、冻结编译都用它）
      队伍\\<队名>\\team.json      给 team_cli（ask.cmd）用的配置；状态\\、日志\\ 是派活状态和每次调用的完整记录
      亲手改\\<队名>.json          亲手改的次数和每次的记录；备份\\、差异\\ 是旧文件和完整差异
      冻结\\<队名>\\<名字>\\        冻结版 player.cpp、player.exe、来源.txt（回合脚本从这里拿程序）
      回合\\第N回合\\<队名>\\       回合收卷：队长交的三套布阵 + 当时的冻结程序快照
"""
import difflib
import hashlib
import importlib.util
import json
import os
import re
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from common import Lock, now, read_json, write_json  # noqa: E402


class Reject(Exception):
    """拒绝这次操作（不扣次数、不算一次调用），消息直接给队长看。"""


# ------------------------------------------------------------------ 配置和路径

def load_match(path):
    m = read_json(path)
    m['_path'] = str(Path(path).resolve())
    for t in m['teams']:
        names = [p['name'] for p in t['players']]
        if len(names) != len(set(names)) or names[0] != t['captain']:
            raise ValueError(f"{t['name']}：名单要不重复，队长写在第一个")
    return m


def root(match): return Path(match['root'])
def host(match): return root(match) / '_主办方'
def team_of(match, team_name): return next(t for t in match['teams'] if t['name'] == team_name)
def pdir(match, team, name):
    player = next(p for p in team['players'] if p['name'] == name)
    return Path(player.get('cwd', root(match) / team['name'] / name)).resolve()
def team_json(match, team): return host(match) / '队伍' / team['name'] / 'team.json'
def frozen_dir(match, team, name): return host(match) / '冻结' / team['name'] / name


def phase(match):
    f = host(match) / '阶段.json'
    return read_json(f) if f.exists() else dict(phase='ready')


def set_phase(match, **kw):
    write_json(host(match) / '阶段.json', dict(kw, time=now()))


def hm(t): return time.strftime('%H:%M:%S', time.localtime(t))


def window(match):
    """现在能不能叫队员：返回 ('dev'|'round', 阶段信息)，窗口关着就 Reject。"""
    ph = phase(match)
    left = float(ph.get('deadline', 0)) - time.time()
    if ph.get('phase') == 'dev' and left > 0:
        return 'dev', ph
    if ph.get('phase') == 'round' and left > 0:
        return 'round', ph
    if ph.get('phase') == 'postgame':
        return 'postgame', ph
    if ph.get('phase') == 'ready':
        raise Reject('开发还没开始，现在叫不来队员。等主办方宣布开始。')
    if ph.get('phase') == 'dev':
        raise Reject('开发时间已经到了，代码正在冻结，现在叫不来队员。等主办方宣布第 1 回合开始。')
    if ph.get('phase') == 'frozen':
        raise Reject('代码已经冻结，第 1 回合还没开始，现在叫不来队员。等主办方宣布第 1 回合开始。')
    raise Reject('现在不在回合决策时间里（上一回合已收卷，下一回合还没开始），叫不来队员。等主办方宣布下一回合开始。')


# ------------------------------------------------------------------ 任务书里不能夹代码

CODE_START = re.compile(r'^(#\s*(include|define|pragma)\b|for\s*\(|if\s*\(|else\b|while\s*\(|switch\s*\(|return\b|'
                        r'(const\s+|static\s+|unsigned\s+)*(int|auto|double|float|bool|char|void|long|short|size_t|'
                        r'struct|class|using|namespace|template|std::)\b)')


def looks_like_code(line):
    s = line.strip()
    if not s:
        return False
    return s.endswith((';', '{', '}')) or bool(CODE_START.match(s))


def code_check(text):
    """常识级检查（设计稿 7.1 第 2 条）：有三个反引号，或连续 3 行以上像 C++，就拒发。返回问题说明或 None。"""
    if '```' in text:
        return '任务书里有代码块标记（三个反引号）'
    lines = text.replace('\r\n', '\n').split('\n')
    run = 0
    for i, line in enumerate(lines, 1):
        run = run + 1 if looks_like_code(line) else 0
        if run >= 3:
            j = i
            while j < len(lines) and looks_like_code(lines[j]):
                j += 1
            return f'任务书第 {i - 2}～{j} 行连续像 C++ 代码'
    return None


# ------------------------------------------------------------------ 队员前言和派活前的处理

def template(name, **kw):
    text = (HERE / name).read_text(encoding='utf-8')
    for k, v in kw.items():
        text = text.replace('{' + k + '}', str(v))
    return text


def notices_file(match): return host(match) / '亲手改' / '待告知.json'


def before_ask(cfg, m, prompt, dry=False):
    """team_cli 派活前调用：检查代码 → 选前言 → 加亲手改提醒。返回 (最终任务书, 待成功回复后核销的提醒)。"""
    match = load_match(cfg['match_config'])
    team = team_of(match, cfg['team'])
    problem = code_check(prompt)
    if problem:
        log_reject(match, team, m['name'], prompt, problem)
        raise Reject(f'没有发出（不算一次调用）：{problem}。请用文字说明，不要直接发代码；'
                     '句子里写算式、函数名、参数名都可以。')
    if dry and phase(match).get('phase') == 'ready':
        return '【主办方】准备阶段命令预检。\n' + prompt, None
    kind, ph = window(match)
    teammates = '、'.join(p['name'] for p in team['players'] if p['name'] != m['name'])
    if kind == 'dev':
        pre = template('前言_开发.txt', OWN=m['cwd'], NAME=m['name'], CAPTAIN=team['captain'], TEAM=team['name'],
                       MATES=teammates, DEADLINE=hm(ph['deadline']), TIMES=match['hand_edit']['times'],
                       LINES=match['hand_edit']['lines'])
    elif kind == 'postgame':
        pre = '【主办方】比赛已经结束。请在自己的文件夹完成赛后复盘，把要留给观众的内容写进 notes.md。\n下面是主办方的复盘要求。\n'
    else:
        pre = template('前言_商量.txt', OWN=m['cwd'], NAME=m['name'], CAPTAIN=team['captain'], TEAM=team['name'],
                       N=ph['round'], DEADLINE=hm(ph['deadline']))
    with Lock(notices_file(match).with_suffix('.lock'), wait=5):
        pending = (read_json(notices_file(match)) if notices_file(match).exists() else {}).get(team['name'] + '/' + m['name'], [])
    extra = ''.join('\n【主办方】' + n + '\n' for n in pending)
    full = pre + extra + '\n' + prompt

    return full, pending


def acknowledge_notices(cfg, member, pending):
    match = load_match(cfg['match_config'])
    key = cfg['team'] + '/' + member
    file = notices_file(match)
    with Lock(file.with_suffix('.lock'), wait=5):
        current = read_json(file) if file.exists() else {}
        current[key] = [x for x in current.get(key, []) if x not in pending]
        write_json(file, current)


def log_reject(match, team, name, prompt, problem):
    d = host(match) / '队伍' / team['name'] / '日志'
    write_json(d / ('拒发_' + time.strftime('%Y%m%d_%H%M%S') + '_' + hashlib.sha1(prompt.encode()).hexdigest()[:6] + '.json'),
               dict(time=now(), caller=team['captain'], callee=name, rejected=True, reason=problem, prompt=prompt))


# ------------------------------------------------------------------ 编译和共享

_kit_mod = {}


def kit_build(match):
    """主办方 kit\\ 里原样的 build.py（和正式比赛同一种编译方式）。"""
    path = host(match) / 'kit' / 'build.py'
    if str(path) not in _kit_mod:
        spec = importlib.util.spec_from_file_location('kit_build_' + hashlib.sha1(str(path).encode()).hexdigest()[:8], path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        _kit_mod[str(path)] = mod
    return _kit_mod[str(path)]


def compile_file(match, src):
    """编译一个 .cpp，返回 (exe 路径, None) 或 (None, 错误文字)。"""
    b = kit_build(match)
    try:
        return Path(b.compile_cpp(src, quiet=True)), None
    except b.BuildError as e:
        return None, str(e)


def put_exe(exe, dest, deadline=None):
    """复制 exe；目标正被运行（Windows 锁文件）时重试几次，还不行就返回 False。"""
    dest = Path(dest)
    dest.parent.mkdir(parents=True, exist_ok=True)
    tmp = dest.with_name(dest.name + '.tmp')
    shutil.copyfile(exe, tmp)
    for _ in range(10):
        try:
            if deadline is not None and time.time() >= deadline:
                raise Reject("亲手改提交已截止，未生效，不扣次数。")
            os.replace(tmp, dest)
            return True
        except PermissionError:
            time.sleep(.5)
    tmp.unlink(missing_ok=True)
    return False


def distribute_frozen(match, team, name, exe):
    failed = []
    for player in team['players']:
        dest = pdir(match, team, player['name']) / '冻结版' / (name + '.exe')
        try:
            if put_exe(exe, dest):
                continue
        except OSError:
            pass
        failed.append(str(dest))
    return failed


def share(match, team, name, exe):
    """把某人的 exe 发给其余四人：<队友>\\队友程序\\<名字>.exe。返回没更新成功的队友。"""
    failed = []
    for p in team['players']:
        if p['name'] != name and not put_exe(exe, pdir(match, team, p['name']) / '队友程序' / (name + '.exe')):
            failed.append(p['name'])
    return failed


def last_build(match, team, name):
    return host(match) / '开发编译' / team['name'] / name


def remember_build(match, team, name, src, exe):
    from uuid import uuid4
    base = last_build(match, team, name)
    version = base / uuid4().hex
    version.mkdir(parents=True)
    shutil.copyfile(src, version/'player.cpp')
    shutil.copyfile(exe, version/'player.exe')
    if time.time() < phase(match).get('deadline', 0):
        write_json(base/'最后成功.json', dict(version=version.name))


BUILD_WRAPPER = '''"""编译飞镖乱斗程序（团队赛版）。编译方式和正式比赛完全相同，真正干活的是同目录的 build_core.py。

  python build.py player.cpp              编译成 player.exe（放在源文件旁边）
  python build.py player.cpp -o x.exe     指定输出
  python build.py --show                  只显示编译器和正式编译命令

团队赛多做一件事：编译的是你自己的 player.cpp（本目录这一份）并且成功时，exe 会自动复制给四名队友，
出现在他们的 队友程序\\\\{me}.exe；队友的最新程序也会这样出现在你的 队友程序\\\\ 里。
代码冻结以后（队友程序\\\\已冻结.txt 存在）就不再发了，队友程序\\\\ 里是冻结的版本。
"""
import os
import shutil
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_core  # noqa: E402
from build_core import *  # noqa: E402,F401,F403

_ME = {me!r}
_MATES = {mates!r}
_HERE = Path(__file__).resolve().parent
_core_compile = build_core.compile_cpp


def _share(exe):
    if (_HERE / '队友程序' / '已冻结.txt').exists():
        return
    missed = []
    for mate, directory in _MATES.items():
        dest = Path(directory) / '队友程序' / (_ME + '.exe')
        tmp = dest.with_name(dest.name + '.tmp')
        try:
            shutil.copyfile(exe, tmp)
            for _ in range(10):
                try:
                    os.replace(tmp, dest)
                    break
                except PermissionError:
                    time.sleep(.5)
            else:
                tmp.unlink()
                missed.append(mate)
        except OSError:
            missed.append(mate)
    if missed:
        print('（' + '、'.join(missed) + ' 正在用你的旧版程序，这次没能更新给他们；下次编译会再发）')
    else:
        print('已发给队友：' + '、'.join(_MATES))


def compile_cpp(src, out=None, quiet=False):
    if Path(src).resolve() != (_HERE / 'player.cpp').resolve():
        return _core_compile(src, out, quiet)
    import tempfile
    sys.path.insert(0, {tools!r})
    import team12
    match = team12.load_match({config!r})
    team = team12.team_of(match, {team!r})
    if team12.phase(match).get('phase') != 'dev':
        return _core_compile(src, out, quiet)
    build_core.BUILD.mkdir(exist_ok=True)
    stable = Path(tempfile.mkdtemp(dir=build_core.BUILD)) / 'player.cpp'
    stable.write_bytes(Path(src).read_bytes())
    exe = _core_compile(stable, None, quiet)
    with team12.stage_lock(match):
        ph = team12.phase(match)
        if ph.get('phase') == 'dev' and time.time() < ph['deadline']:
            team12.remember_build(match, team, _ME, stable, exe)
            _share(exe)
    if out is not None:
        shutil.copyfile(exe, out)
        return Path(out)
    return exe


build_core.compile_cpp = compile_cpp

if __name__ == '__main__':
    build_core.main()
'''


# ------------------------------------------------------------------ 亲手改

def norm(text):
    """[(原文件行号, 去掉行尾空白的内容)]，空行不算。"""
    return [(i, x.rstrip()) for i, x in enumerate(text.replace('\r\n', '\n').replace('\r', '\n').split('\n'), 1) if x.strip()]


def count_changes(old, new):
    """改了一行算一行、加一行算一行、删一行算一行（空行和行尾空白不算）。返回 (行数, 给队员看的位置说明)。"""
    a, b = norm(old), norm(new)
    sm = difflib.SequenceMatcher(None, [x for _, x in a], [x for _, x in b], autojunk=False)
    total, where = 0, []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == 'equal':
            continue
        total += max(i2 - i1, j2 - j1)
        if j2 > j1:
            lo, hi = b[j1][0], b[j2 - 1][0]
            where.append(f'第 {lo} 行' if lo == hi else f'第 {lo}～{hi} 行')
            if i2 - i1 > j2 - j1:
                where[-1] += f'（另删了 {i2 - i1 - (j2 - j1)} 行）'
        else:
            at = b[j1][0] if j1 < len(b) else (b[-1][0] + 1 if b else 1)
            where.append(f'第 {at} 行前删了 {i2 - i1} 行')
    return total, where


def busy(match, team, name):
    sys.path.insert(0, str(HERE))
    import team_cli
    cfg = read_json(team_json(match, team))
    return any(t['status'] != 'done' for _, t in team_cli.tasks(cfg, [name]))


def edits_file(match, team): return host(match) / '亲手改' / (team['name'] + '.json')


def edits_state(match, team):
    f = edits_file(match, team)
    return read_json(f) if f.exists() else dict(used=0, records=[])


def stage_lock(match):
    return Lock(host(match) / '交卷.lock', wait=360)


def edit_window(match):
    ph = phase(match)
    if ph.get('phase') not in ('dev', 'round') or time.time() >= ph['deadline']:
        raise Reject('当前不在开发或回合决策窗口，亲手改未生效，不扣次数。')
    return ph


def hand_edit(match, team, target, new_file):
    initial = edit_window(match)
    with stage_lock(match):
        if edit_window(match) != initial:
            raise Reject('提交期间阶段已切换，未扣次数。')
        return _hand_edit(match, team, target, new_file)


def _hand_edit(match, team, target, new_file):
    """队长亲手改（设计稿 7.3）。返回给队长看的结果文字；不通过就 Reject（不扣次数）。"""
    times, limit = match['hand_edit']['times'], match['hand_edit']['lines']
    names = [p['name'] for p in team['players']]
    if target not in names:
        raise Reject(f'没有这个人：{target}（本队是 ' + '、'.join(names) + '）')
    ph = phase(match)
    stage = ph.get('phase')
    if stage not in ('dev', 'frozen', 'round', 'between'):
        raise Reject('开发还没开始，现在不能用亲手改。')
    dev = stage == 'dev'
    if dev and target == team['captain']:
        raise Reject('开发期间你自己的程序随便改，直接改你文件夹里的 player.cpp 就行，不用这个命令，也不扣次数。')
    new_path = Path(new_file)
    if not new_path.is_absolute():
        new_path = Path.cwd() / new_path
    if not new_path.is_file():
        raise Reject(f'找不到改好的文件：{new_path}')
    new = new_path.read_bytes().decode('utf-8', errors='replace')
    with Lock(edits_file(match, team).with_suffix('.lock'), wait=10):
        st = edits_state(match, team)
        if st['used'] >= times:
            raise Reject(f'全场 {times} 次已经用完了。')
        base = (pdir(match, team, target) if dev else frozen_dir(match, team, target)) / 'player.cpp'
        old = base.read_bytes().decode('utf-8', errors='replace')
        n, where = count_changes(old, new)
        if n == 0:
            raise Reject('和' + ('他现在的 player.cpp' if dev else '冻结版本') + '比没有改动（空行和行尾空白不算），没有扣次数。')
        if n > limit:
            raise Reject(f'改动 {n} 行，超过每次 {limit} 行的上限，没有写入，也没有扣次数。'
                         f'（改一行算一行、加一行算一行、删一行算一行，空行和行尾空白不算）')
        if dev and busy(match, team, target):
            raise Reject(f'{target} 正在干你派的活（可能正在改这个文件），没有写入，也没有扣次数。等他交回结果后再用。')
        tmpdir = host(match) / '亲手改' / '编译检查'
        tmpdir.mkdir(parents=True, exist_ok=True)
        probe = tmpdir / (team['name'] + '_' + target + '.cpp')
        probe.write_bytes(new.encode('utf-8'))
        exe, err = compile_file(match, probe)
        if not exe:
            raise Reject('改后的程序编译不过，没有写入，也没有扣次数。编译器输出：\n' + err[-1500:])
        k = st['used'] + 1
        stamp = time.strftime('%Y%m%d_%H%M%S')
        bdir = host(match) / '亲手改' / '备份'
        bdir.mkdir(parents=True, exist_ok=True)
        backup = bdir / f"{team['name']}_第{k}次_{target}_{stamp}_改前.cpp"
        shutil.copyfile(base, backup)
        diff = ''.join(difflib.unified_diff(old.splitlines(True), new.splitlines(True), 'player.cpp（改前）', 'player.cpp（改后）'))
        ddir = host(match) / '亲手改' / '差异'
        ddir.mkdir(parents=True, exist_ok=True)
        (ddir / f"{team['name']}_第{k}次_{target}_{stamp}.diff").write_text(diff, encoding='utf-8')
        own = pdir(match, team, target) / 'player.cpp'
        edit_window(match)
        if dev:
            # 选手文件原子替换；采集器只会看见一份完整源码。
            import tempfile
            with tempfile.NamedTemporaryFile(dir=own.parent,delete=False) as staged:
                staged.write(new.encode('utf-8'))
            edit_window(match)
            os.replace(staged.name,own)
            remember_build(match, team, target, probe, exe)
            missed = share(match, team, target, exe)
            effect = '已写进他文件夹的 player.cpp，并把新编译的程序发给了队友' + (f"（{'、'.join(missed)} 正在用旧版，没能更新）" if missed else '')
        else:
            fd = frozen_dir(match, team, target)
            if not put_exe(exe, fd / 'player.exe', ph['deadline']):
                raise Reject('主办方冻结程序被占用，未生效；请稍后重试。')
            (fd / 'player.cpp').write_bytes(new.encode('utf-8'))
            (fd / '来源.txt').write_text(f'{now()} 队长亲手改第 {k} 次（{n} 行）后的冻结版本\n', encoding='utf-8')
            if own.exists():
                shutil.copyfile(own, bdir / f"{team['name']}_第{k}次_{target}_{stamp}_文件夹里原来的.cpp")
            own.write_bytes(new.encode('utf-8'))
            cap = pdir(match, team, team['captain']) / '冻结版'
            (cap / (target + '.cpp')).write_bytes(new.encode('utf-8'))
            failed = distribute_frozen(match, team, target, exe)
            missed = share(match, team, target, exe)
            effect = '上场程序从下一次收卷起生效（决策时间里改的当回合就用上）'
            if failed or missed:
                effect += '。有本地副本未更新，请主办方在文件解除占用后运行组队工具的“同步冻结版”'
            else:
                effect += '，全队推演程序已同步'
        rec = dict(time=now(), k=k, target=target, lines=n, where=where, phase=stage, round=ph.get('round'),
                   source=str(new_path), backup=str(backup), sha256=hashlib.sha256(new.encode('utf-8')).hexdigest())
        st['used'] = k
        st['records'].append(rec)
        write_json(edits_file(match, team), st)
    if target != team['captain']:
        msg = (f"队长 {team['captain']} 亲手改了你的 player.cpp：{'、'.join(where)}（改动共 {n} 行）。请先看一下这几行再继续。"
               + ('' if dev else '改动从下一次收卷起生效（决策时间里改的当回合就用上）；你文件夹里的 player.cpp 已经换成改后的版本。'))
        with Lock(notices_file(match).with_suffix('.lock'), wait=5):
            d = read_json(notices_file(match)) if notices_file(match).exists() else {}
            d.setdefault(team['name'] + '/' + target, []).append(msg)
            write_json(notices_file(match), d)
    return (f"已写入：{target} 的 player.cpp，改动 {n} 行（{'、'.join(where)}）。{effect}。"
            f"这是第 {k} 次，全场还剩 {times - k} 次。"
            + ('下次给他派活时，主办方会自动告诉他你改了哪几行。' if target != team['captain'] else ''))


def remaining(match, team):
    st = edits_state(match, team)
    times = match['hand_edit']['times']
    lines = [f"亲手改：全场 {times} 次，已用 {st['used']} 次，还剩 {times - st['used']} 次；每次不超过 {match['hand_edit']['lines']} 行。"]
    for r in st['records']:
        lines.append(f"- 第 {r['k']} 次：{r['target']}，{r['lines']} 行（{'、'.join(r['where'])}），{r['time'][:19].replace('T', ' ')} UTC")
    return '\n'.join(lines)


def hand_edit_cli(match_path, team_name, args):
    """队长文件夹里 亲手改.cmd 的入口：亲手改.cmd 队员名 改好的文件.cpp ／ 亲手改.cmd 剩余"""
    sys.stdout.reconfigure(encoding='utf-8')
    match = load_match(match_path)
    team = team_of(match, team_name)
    log = host(match) / '亲手改' / '调用记录.jsonl'
    log.parent.mkdir(parents=True, exist_ok=True)
    if len(args) == 1 and args[0] in ('剩余', 'left', 'remaining'):
        print(remaining(match, team))
        return 0
    if len(args) != 2:
        print('用法：亲手改.cmd 队员名 改好的文件.cpp      查还剩几次：亲手改.cmd 剩余')
        return 64
    try:
        msg, code = hand_edit(match, team, args[0], args[1]), 0
    except Reject as e:
        msg, code = str(e), 1
    with open(log, 'a', encoding='utf-8') as f:
        f.write(json.dumps(dict(time=now(), team=team_name, args=args, ok=code == 0, reply=msg), ensure_ascii=False) + '\n')
    print(msg)
    return code
