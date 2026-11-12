# 预览：每位角色一张（6x2，格下标表情名），以及总览（11 行 x 12 列）
# 用法: python preview.py [角色名 ...]   （缺省全部；总览只画已切好的角色）
import sys, os
from PIL import Image, ImageDraw, ImageFont
OUT = os.environ.get('EXPR_OUT') or 'expressions'   # 切好的差分所在目录（slice.py 的输出）
FONT = r'C:\Windows\Fonts\msyh.ttc'
BG = (244, 242, 238)
CHARS = ['GPT_大_Astra', 'GPT_小_Sol', 'Claude_特大_Fable', 'Claude_大_Opus', 'DeepSeek_大', 'Gemini_大',
         'Kimi_大', 'GLM_大', 'Grok_大', '千问_大', 'MiMo_大', '文心_大']
EX = ['常态', '开心', '得意', '生气', '伤心', '震惊', '无语', '思考', '流口水', '头顶铁盆', '尴尬地笑', '犯困']


def cell(c, e, size):
    im = Image.open(rf'{OUT}\{c}\{e}.png').convert('RGBA')
    return im.resize((size, size), Image.LANCZOS)


def one(c):
    cs, gap, lab = 470, 18, 46
    f = ImageFont.truetype(FONT, 30); ft = ImageFont.truetype(FONT, 40)
    W = 6 * cs + 7 * gap; top = 70
    out = Image.new('RGB', (W, top + 2 * (cs + lab) + 3 * gap), BG); d = ImageDraw.Draw(out)
    d.text((W / 2, 16), c.replace('_', ' '), font=ft, fill=(50, 50, 50), anchor='mt')
    for i, e in enumerate(EX):
        r, k = divmod(i, 6); x = gap + k * (cs + gap); y = top + gap + r * (cs + lab + gap)
        d.rectangle((x, y, x + cs - 1, y + cs - 1), fill=(252, 251, 249))
        out.paste(cell(c, e, cs), (x, y), cell(c, e, cs))
        d.text((x + cs / 2, y + cs + 6), e, font=f, fill=(40, 40, 40), anchor='mt')
    out.save(rf'{OUT}\预览_{c}.jpg', quality=90); print('预览', c, out.size)


def overview():
    done = [c for c in CHARS if all(os.path.exists(rf'{OUT}\{c}\{e}.png') for e in EX)]
    cs, gap, left, top = 290, 8, 230, 60
    f = ImageFont.truetype(FONT, 26)
    W = left + 12 * (cs + gap) + gap
    out = Image.new('RGB', (W, top + len(done) * (cs + gap) + gap), BG); d = ImageDraw.Draw(out)
    for k, e in enumerate(EX):
        d.text((left + gap + k * (cs + gap) + cs / 2, top / 2), e, font=f, fill=(40, 40, 40), anchor='mm')
    for r, c in enumerate(done):
        y = top + gap + r * (cs + gap)
        d.text((left / 2, y + cs / 2), c.replace('_', ' '), font=f, fill=(40, 40, 40), anchor='mm')
        for k, e in enumerate(EX):
            x = left + gap + k * (cs + gap); t = cell(c, e, cs)
            d.rectangle((x, y, x + cs - 1, y + cs - 1), fill=(252, 251, 249))
            out.paste(t, (x, y), t)
    out.save(rf'{OUT}\预览_总览.jpg', quality=88); print('总览', len(done), out.size)


for c in (sys.argv[1:] or [c for c in CHARS if os.path.exists(rf'{OUT}\{c}\常态.png')]):
    one(c)
overview()
