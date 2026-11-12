# 从母版（竖版透明立绘原图，2160×3840）做表情差分的两张参考图（浅灰底 RGB）：全身 full_*.png、上半身 upper_*.png
# 在工作目录里运行；母版目录用环境变量 PERSONA_MASTER 指定（默认 ./master），参考图写进 ./expr/refs/
import os, numpy as np
from PIL import Image
M = os.environ.get('PERSONA_MASTER') or 'master'
R = os.path.join('expr', 'refs')
os.makedirs(R, exist_ok=True)
CHARS = ['GPT_大_Astra', 'GPT_小_Sol', 'Claude_特大_Fable', 'Claude_大_Opus', 'DeepSeek_大', 'Gemini_大',
         'Kimi_大', 'GLM_大', 'Grok_大', '千问_大', 'MiMo_大', '文心_大']
for c in CHARS:
    im = Image.open(os.path.join(M, c + '.png')).convert('RGBA'); a = np.array(im)[:, :, 3]
    ys, xs = np.where(a > 128); pad = 30
    x0, y0, x1, y1 = max(0, xs.min() - pad), max(0, ys.min() - pad), min(im.width, xs.max() + pad), min(im.height, ys.max() + pad)
    def save(box, dst):
        t = im.crop(box); bg = Image.new('RGBA', t.size, (236, 236, 236, 255)); bg.alpha_composite(t)
        bg.convert('RGB').save(dst); print(os.path.basename(dst), bg.size)
    save((x0, y0, x1, y1), os.path.join(R, f'full_{c}.png'))
    # 上半身：从头顶往下取人物高度的 40%，宽度取这段里人物的左右范围
    yb = y0 + int((y1 - y0) * 0.40)
    sub = a[y0:yb]; xs2 = np.where(sub.max(0) > 128)[0]
    save((max(0, xs2.min() - pad), y0, min(im.width, xs2.max() + pad), yb), os.path.join(R, f'upper_{c}.png'))
