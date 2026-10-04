"""外交选手客户端（复制到选手目录后名为 game.py）。只用 Python 标准库。"""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request
import uuid

HOME = Path(__file__).resolve().parent


def call(config, route, payload=None):
    request = urllib.request.Request(config['url'] + route,
        data=json.dumps(payload, ensure_ascii=False).encode() if payload is not None else None,
        headers={'Authorization': 'Bearer ' + config['token'], 'Content-Type': 'application/json'})
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        return json.loads(exc.read().decode('utf-8'))
    except urllib.error.URLError as exc:
        return dict(ok=False, text='连接裁判失败：' + str(exc.reason))


def main():
    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding='utf-8')
    p = argparse.ArgumentParser(prog='python game.py', description='七国外交选手客户端')
    sub = p.add_subparsers(dest='cmd', required=True)
    sub.add_parser('brief', help='查看完整当前局面')
    s = sub.add_parser('send', help='发消息：send 国家 "内容"；公开发言用 GLOBAL')
    s.add_argument('to')
    s.add_argument('text', nargs='+')
    o = sub.add_parser('orders', help='提交或修改军令：orders "A PAR - BUR" "F BRE H" ...')
    o.add_argument('orders', nargs='*')
    o.add_argument('--file', help='从 JSON 数组文件读取军令')
    sub.add_parser('draw', help='赞成协议和局')
    sub.add_parser('undraw', help='撤回和局赞成')
    l = sub.add_parser('legal', help='查看合法命令：legal [地点...]')
    l.add_argument('locs', nargs='*')
    i = sub.add_parser('inbox', help='查看历史消息')
    i.add_argument('--with', dest='other')
    i.add_argument('--phase')
    h = sub.add_parser('history', help='查看过去的结算')
    h.add_argument('--phase')
    m = sub.add_parser('simulate', help='假设推演：simulate 文件.json')
    m.add_argument('file')
    a = p.parse_args()
    config = json.loads((HOME / 'connection.json').read_text(encoding='utf-8'))

    if a.cmd == 'brief':
        r = call(config, '/brief')
    elif a.cmd == 'legal':
        r = call(config, '/legal?' + urllib.parse.urlencode(dict(locs=','.join(a.locs))))
    elif a.cmd == 'inbox':
        r = call(config, '/inbox?' + urllib.parse.urlencode({k: v for k, v in (('with', a.other), ('phase', a.phase)) if v}))
    elif a.cmd == 'history':
        r = call(config, '/history?' + urllib.parse.urlencode({'phase': a.phase} if a.phase else {}))
    elif a.cmd == 'simulate':
        snap = call(config, '/snapshot')
        if 'state' not in snap:
            r = snap
        else:
            # 快照写在本目录：Codex 沙箱里系统临时目录清理时会被拒绝访问
            path = HOME / f'.snapshot_{uuid.uuid4().hex}.json'
            path.write_text(json.dumps(snap, ensure_ascii=False), encoding='utf-8')
            try:
                done = subprocess.run([config['sim_python'], '-X', 'utf8', str(HOME / 'simulate.py'), str(path),
                                       str(Path(a.file).resolve())], capture_output=True, text=True, encoding='utf-8')
            finally:
                try:
                    path.unlink()
                except OSError:
                    pass
            r = dict(text=(done.stdout + done.stderr).strip())
    else:
        payload = dict(action=a.cmd, request_id=str(uuid.uuid4()))
        if a.cmd == 'send':
            payload.update(to=a.to, text=' '.join(a.text))
        elif a.cmd == 'orders':
            orders = a.orders
            if a.file:
                orders = json.loads(Path(a.file).read_text(encoding='utf-8-sig'))
            payload['orders'] = orders
        r = call(config, '/action', payload)
    print(r.get('text', json.dumps(r, ensure_ascii=False)))
    sys.exit(0 if r.get('ok', True) else 1)


if __name__ == '__main__':
    main()
