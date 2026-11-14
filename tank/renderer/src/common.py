"""渲染器公用的小工具：字体、ffmpeg、选手头像、从回放网页里取出内嵌的回放数据。

各集的 renderer/src/ 里各放一份同样的 common.py，互不依赖。
环境变量：
    ARENA_FONT        中文字体文件（.ttf / .ttc / .otf）。不设时按 FONT_CANDIDATES 依次找系统字体。
    ARENA_FFMPEG      ffmpeg 可执行文件。不设时用 PATH 里的 ffmpeg。
    ARENA_AVATAR_DIR  可选：放 <名字>.png 的目录，优先于 renderer/assets/avatars/，用来换成自己的头像。
"""
import json, os, re, shutil, subprocess

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))

_WIN = os.path.join(os.environ.get('WINDIR', r'C:\Windows'), 'Fonts')
FONT_CANDIDATES = [
    os.path.join(_WIN, 'NotoSansSC-VF.ttf'),            # Noto Sans SC 可变字体（视频里用的就是它）
    os.path.join(_WIN, 'msyhbd.ttc'),                   # 微软雅黑粗体
    os.path.join(_WIN, 'msyh.ttc'),
    '/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc',
    '/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc',
    '/usr/share/fonts/google-noto-cjk/NotoSansCJK-Bold.ttc',
    '/System/Library/Fonts/PingFang.ttc',
    '/System/Library/Fonts/STHeiti Medium.ttc',
]

_font_path = None
_fc = {}


def font_path():
    global _font_path
    if _font_path is None:
        env = os.environ.get('ARENA_FONT')
        if env:
            if not os.path.exists(env):
                raise FileNotFoundError(f'ARENA_FONT 指的字体不存在：{env}')
            _font_path = env
        else:
            for p in FONT_CANDIDATES:
                if os.path.exists(p):
                    _font_path = p
                    break
            else:
                raise FileNotFoundError('找不到中文字体：请用环境变量 ARENA_FONT 指定一个中文字体文件（.ttf/.ttc/.otf）')
    return _font_path


def font(size, weight='Bold'):
    """weight 只对可变字体起作用（Noto Sans SC VF 的 Thin…Black）；普通字体忽略。"""
    size = max(6, int(size))
    key = (size, weight)
    if key not in _fc:
        f = ImageFont.truetype(font_path(), size)
        try:
            f.set_variation_by_name(weight)
        except Exception:
            pass
        _fc[key] = f
    return _fc[key]


def ffmpeg():
    exe = os.environ.get('ARENA_FFMPEG') or shutil.which('ffmpeg')
    if not exe:
        raise FileNotFoundError('找不到 ffmpeg：把 ffmpeg 放进 PATH，或用环境变量 ARENA_FFMPEG 指定')
    return exe


def open_video(out, w, h, fps, crf=18):
    """返回一个往 stdin 写 rgb24 原始帧的 ffmpeg 进程。"""
    d = os.path.dirname(os.path.abspath(out))
    os.makedirs(d, exist_ok=True)
    return subprocess.Popen([ffmpeg(), '-y', '-loglevel', 'error', '-f', 'rawvideo', '-pix_fmt', 'rgb24', '-s', f'{w}x{h}',
                             '-r', str(fps), '-i', '-', '-c:v', 'libx264', '-preset', 'medium', '-tune', 'animation',
                             '-pix_fmt', 'yuv420p', '-crf', str(crf), out], stdin=subprocess.PIPE)


# ---------------------------------------------------------------- 头像
AVATAR_DIR = os.path.normpath(os.path.join(HERE, '..', 'assets', 'avatars'))   # 视频里用的选手头像


def avatar_path(name):
    """头像文件名（不带 .png）→ 路径；先找 ARENA_AVATAR_DIR，再找 renderer/assets/avatars/。没有就返回 None（调用方画占位圆）。"""
    dirs = [d for d in (os.environ.get('ARENA_AVATAR_DIR'), AVATAR_DIR) if d]
    for d in dirs:
        for n in (str(name), str(name).split('#')[0].strip()):
            p = os.path.join(d, n + '.png')
            if os.path.exists(p):
                return p
    return None


def round_avatar(src, px):
    """读头像图片，缩到 px×px 并裁成圆。"""
    im = Image.open(src).convert('RGBA').resize((px, px), Image.LANCZOS)
    m = Image.new('L', (px * 4, px * 4), 0)
    ImageDraw.Draw(m).ellipse([0, 0, px * 4 - 1, px * 4 - 1], fill=255)
    m = m.resize((px, px), Image.LANCZOS)
    a = Image.new('L', (px, px), 0)
    a.paste(im.getchannel('A'), (0, 0), m)
    im.putalpha(a)
    return im


def placeholder(label, col, px):
    """纯色圆 + 名字首字（没有对应角色时的头像）。"""
    S = 128
    im = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    bg = tuple(int(c * .65 + b * .35) for c, b in zip(col[:3], (20, 24, 36)))
    d.ellipse([0, 0, S - 1, S - 1], fill=bg)
    ch = str(label).strip()[:1] or '?'
    ch = ch.upper() if ch.isascii() else ch
    d.text((S / 2, S / 2 + 2), ch, font=font(70, 'Black'), fill=(255, 255, 255), anchor='mm')
    return im.resize((max(4, int(px)), max(4, int(px))), Image.LANCZOS)


# ---------------------------------------------------------------- 回放
def load_json_or_html(path, var_names=('EMBED', 'R', 'DATA', 'REPLAY')):
    """读 .json / .json.gz，或者从仓库里那种"查看器 + 回放打包在一起"的 .html 里取出内嵌的回放对象
    （脚本里的 `const EMBED = {...}` 之类）。"""
    if path.endswith('.gz'):
        import gzip
        with gzip.open(path, 'rt', encoding='utf-8') as fp:
            return json.load(fp)
    with open(path, encoding='utf-8') as fp:
        text = fp.read()
    if not path.lower().endswith(('.html', '.htm')):
        return json.loads(text)
    for v in var_names:
        m = re.search(r'(?:const|let|var)\s+' + re.escape(v) + r'\s*=\s*', text)
        if m and text[m.end():m.end() + 1] in '{[':
            obj, _ = json.JSONDecoder().raw_decode(text[m.end():])
            return obj
    raise ValueError(f'{path} 里没找到内嵌的回放数据（找过 {", ".join(var_names)}）')


def parse_range(t_from, t_to, total):
    a = 0.0 if t_from is None else max(0.0, float(t_from))
    b = total if t_to is None else min(total, float(t_to))
    if b <= a:
        raise SystemExit(f'--from {a} 不小于 --to {b}')
    return a, b
