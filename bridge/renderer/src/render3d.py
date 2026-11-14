"""第 9 集造桥 · 3D 小剧场渲染器：无头 Chrome 逐帧渲染 theater3d.html → JPEG → ffmpeg x264 1080p。

用法：
  python render3d.py <回放.html|回放.json> [-o 输出.mp4] [--t0 秒] [--t1 秒] [--title 字] [--sub 字]
  python render3d.py --config 剪辑表.json [-o 输出.mp4]          多段拼接 + 每段自定义机位、慢放等（格式见 README）
  其它：--list（只打印时长）  --still 0,120,300（只截这几帧 PNG）  --at 0:3.0,0:5.5（按 段号:模拟秒 截帧）
        --fps 30  --ss 2（超采样倍数）  --analysis（受力上色）  --noao（关掉环境光遮蔽，快一些）  --browser chrome|msedge|chromium

回放：仓库 bridge/replays/*.html（回放网页，里面内嵌了回放 JSON）或 `bridge.py run ... --replay x.json` 导出的 JSON。
物理一律来自回放逐帧数据，这里只改"播放哪一帧时刻"（慢放 = 时间重映射）。同一帧号画面完全相同。
"""
import json, mimetypes, os, shutil, subprocess, sys, time, base64
from urllib.parse import urlparse, unquote

HERE = os.path.dirname(os.path.abspath(__file__))
RENDERER = os.path.normpath(os.path.join(HERE, '..'))
THREE_DIR = os.environ.get('A12_THREE_DIR') or os.path.join(RENDERER, 'node_modules', 'three')
FFMPEG = os.environ.get('A12_FFMPEG') or shutil.which('ffmpeg') or 'ffmpeg'
FONT = os.environ.get('A12_FONT')
ORIGIN = 'http://bridge3d.local'
CHROME_ARGS = ['--enable-gpu', '--ignore-gpu-blocklist', '--force-device-scale-factor=1', '--force-high-performance-gpu',
               '--disable-background-timer-throttling', '--disable-renderer-backgrounding']
if sys.platform == 'win32':
    CHROME_ARGS.insert(0, '--use-angle=d3d11')


def serve(route):
    path = unquote(urlparse(route.request.url).path)
    if path == '/font/main':
        fp = FONT if FONT and os.path.isfile(FONT) else None
    elif path.startswith('/three/'):
        fp = os.path.normpath(os.path.join(THREE_DIR, path[len('/three/'):]))
        if not fp.startswith(os.path.normpath(THREE_DIR)):
            fp = None
    else:
        fp = os.path.normpath(os.path.join(HERE, path.lstrip('/') or 'theater3d.html'))
        if not fp.startswith(HERE):
            fp = None
    if not fp or not os.path.isfile(fp):
        route.fulfill(status=404, body='not found ' + path)
        return
    ct = 'text/javascript' if fp.endswith('.js') else (mimetypes.guess_type(fp)[0] or 'application/octet-stream')
    with open(fp, 'rb') as f:
        route.fulfill(status=200, body=f.read(), headers={'Content-Type': ct})


def load_replay(fn):
    """读回放：.json 直接读；.html（bridge.py --html 生成的回放网页）取出内嵌的 `const EMBED = {...}`。"""
    with open(fn, encoding='utf-8') as f:
        s = f.read()
    if fn.lower().endswith(('.html', '.htm')):
        key = 'const EMBED = '
        i = s.find(key)
        if i < 0:
            raise SystemExit(f'{fn} 里没有内嵌回放（找不到 {key}）')
        d, _ = json.JSONDecoder().raw_decode(s[i + len(key):])
        if d is None:
            raise SystemExit(f'{fn} 是空的回放查看器，没有内嵌回放')
        return d
    return json.loads(s)


def arg(args, name, default=None):
    return args[args.index(name) + 1] if name in args else default


def main(argv):
    args = argv[1:]
    if not args or args[0] in ('-h', '--help'):
        print(__doc__); return 0
    fade = None
    if '--config' in args:
        cfgp = arg(args, '--config')
        clips = json.load(open(cfgp, encoding='utf-8'))
        if isinstance(clips, dict):   # {"fade": 0, "clips": [...]}（fade = 段间淡黑秒数，0 = 硬切）
            fade = clips.get('fade'); clips = clips['clips']
        base = os.path.dirname(os.path.abspath(cfgp))
        name = os.path.splitext(os.path.basename(cfgp))[0]
    else:
        src = args[0]
        c = {'file': os.path.abspath(src)}
        for k in ('t0', 't1'):
            if f'--{k}' in args:
                c[k] = float(arg(args, f'--{k}'))
        for k in ('title', 'sub'):
            if f'--{k}' in args:
                c[k] = arg(args, f'--{k}')
        clips, base, name = [c], os.getcwd(), os.path.splitext(os.path.basename(src))[0]
    prog = []
    for c in clips:
        c = dict(c)
        fn = c.pop('file')
        c['replay'] = load_replay(fn if os.path.isabs(fn) else os.path.join(base, fn))
        if '--analysis' in args:
            c['analysis'] = True
        prog.append(c)
    prog = dict(clips=prog)
    if fade is not None:
        prog['fade'] = fade
    fps = int(arg(args, '--fps', '30'))
    ss = arg(args, '--ss', '2')
    out = arg(args, '-o') or arg(args, '--out') or os.path.join(os.environ.get('A12_WORK') or os.path.join(RENDERER, 'renders'), name + '.mp4')
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    if not os.path.isfile(os.path.join(THREE_DIR, 'build', 'three.module.js')):
        raise SystemExit(f'找不到 three.js：{THREE_DIR}\n在 bridge/renderer/ 下运行 npm install（见 README）。')
    browser = arg(args, '--browser', os.environ.get('A12_BROWSER', 'chrome'))

    from playwright.sync_api import sync_playwright
    with sync_playwright() as pw:
        br = pw.chromium.launch(channel=None if browser == 'chromium' else browser, headless=True, args=CHROME_ARGS)
        pg = br.new_page(viewport={'width': 1920, 'height': 1080}, device_scale_factor=1)
        errs = []
        pg.on('pageerror', lambda e: errs.append(str(e)))
        pg.on('console', lambda m: errs.append('console: ' + m.text) if m.type == 'error' else None)
        pg.route(ORIGIN + '/**', serve)
        pg.goto(f'{ORIGIN}/theater3d.html?ss={ss}&fps={fps}' + ('&ao=0' if '--noao' in args else ''))
        pg.wait_for_function('window.THEATER3D && window.THEATER3D.ready', timeout=60000)
        pg.evaluate('async () => { await document.fonts.load("600 46px A12Font").catch(() => 0); await document.fonts.ready; }')
        t0 = time.time()
        info = pg.evaluate('p => THEATER3D.setProgram(p)', prog)
        print(f'场景构建 {time.time() - t0:.1f} 秒；共 {info["frames"]} 帧 = {info["frames"] / fps:.1f} 秒')
        for i, c in enumerate(info['clips']):
            print(f"  段 {i}: 模拟 {c['t0']:.2f}~{c['t1']:.2f} 秒 → 视频 {c['vdur']:.1f} 秒（起始帧 {c['start']}）；慢放 " +
                  ('，'.join(f"{w['kind']} {w['a']:.2f}~{w['b']:.2f}×{w['rate']}" for w in c['slow']) or '无'))
        if errs:
            print('页面错误：', errs[:10])
        if '--list' in args:
            br.close(); return 0
        shots = []
        if '--still' in args:
            shots = [(int(x), f'帧{int(x):04d}') for x in arg(args, '--still').split(',')]
        if '--at' in args:
            for s in arg(args, '--at').split(','):
                ci, t = s.split(':')
                n = pg.evaluate(f'THEATER3D.frameOf({int(ci)}, {float(t)})')
                shots.append((n, f'段{ci}_t{float(t):.2f}_帧{n:04d}'))
        if shots:
            stem = os.path.splitext(out)[0]
            for n, nm in shots:
                pg.evaluate(f'THEATER3D.draw({n})')
                p = f'{stem}_{nm}.png'
                pg.screenshot(path=p, clip={'x': 0, 'y': 0, 'width': 1920, 'height': 1080})
                print('静帧', p)
            print('页面错误：', errs or '无')
            br.close(); return 0
        cdp = pg.context.new_cdp_session(pg)
        N = info['frames']
        if '--max-frames' in args:
            N = min(N, int(arg(args, '--max-frames')))
        ff = subprocess.Popen([FFMPEG, '-y', '-hide_banner', '-loglevel', 'error', '-f', 'image2pipe', '-framerate', str(fps), '-c:v', 'mjpeg', '-i', '-',
                               '-vf', 'scale=in_range=pc:out_range=tv,format=yuv420p', '-color_range', 'tv', '-colorspace', 'bt709',
                               '-color_primaries', 'bt709', '-color_trc', 'bt709',
                               '-c:v', 'libx264', '-preset', arg(args, '--preset', 'medium'), '-crf', arg(args, '--crf', '16'),
                               '-movflags', '+faststart', out], stdin=subprocess.PIPE)
        t0 = time.time()
        for n in range(N):
            pg.evaluate(f'THEATER3D.draw({n})')
            r = cdp.send('Page.captureScreenshot', {'format': 'jpeg', 'quality': 95, 'optimizeForSpeed': True, 'captureBeyondViewport': False})
            ff.stdin.write(base64.b64decode(r['data']))
            if n % 120 == 0:
                print(f'  {n}/{N}  {n / max(1e-6, time.time() - t0):.2f} 帧/秒', flush=True)
        ff.stdin.close(); rc = ff.wait()
        print('完成', out, f'{time.time() - t0:.0f} 秒', '页面错误：', errs or '无')
        br.close()
    return 0 if rc == 0 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
