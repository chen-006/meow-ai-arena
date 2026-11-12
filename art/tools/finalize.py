# 母版 -> 交付立绘：清理孤立小块、按 alpha>128 包围盒归一、贴进透明画布
# 用法: python finalize.py 母版.png 文件名  （输出到 <输出目录>/portraits/文件名.png（1672×941）和 portraits_2x/文件名.png，
#       并在 <输出目录>/tmp/check_文件名.jpg 生成三底检查图；输出目录用环境变量 ART_OUT 指定，默认当前目录）
import sys, os, numpy as np, cv2
from PIL import Image
OUT = os.environ.get('ART_OUT') or os.getcwd()
src, name = sys.argv[1], sys.argv[2]
im = Image.open(src).convert('RGBA'); arr = np.array(im)
a = arr[:, :, 3].astype(np.float32)
# 接口透明输出的人物内部 alpha 只有 ~252，拉到 255；远离主体的低 alpha 噪点清零
a = np.clip(a * 255.0 / 248.0, 0, 255)
solid = (a > 128).astype(np.uint8)
near = cv2.dilate(solid, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (31, 31)))
a[near == 0] = 0
arr[:, :, 3] = a.round().astype(np.uint8)
a = arr[:, :, 3]
# 删除与主体不连通的小块（alpha>16 的连通域 < 2000 px 且不是最大的）
mask = (a > 16).astype(np.uint8)
n, lab, stats, _ = cv2.connectedComponentsWithStats(mask, connectivity=8)
removed = 0
if n > 1:
    main = 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])
    for i in range(1, n):
        if i != main and stats[i, cv2.CC_STAT_AREA] < 2000:
            arr[lab == i, 3] = 0; removed += stats[i, cv2.CC_STAT_AREA]
a = arr[:, :, 3]
ys, xs = np.where(a > 128)
x0, y0, x1, y1 = xs.min(), ys.min(), xs.max() + 1, ys.max() + 1
im = Image.fromarray(arr)
fig = im.crop((max(0, x0 - 2), max(0, y0 - 2), min(im.width, x1 + 2), min(im.height, y1 + 2)))
touch = dict(top=y0 <= 1, bottom=y1 >= im.height - 1, left=x0 <= 1, right=x1 >= im.width - 1)

def place(scale):
    W, H, FH, TOP, MAXW = 1672 * scale, 941 * scale, 927 * scale, 7 * scale, 1600 * scale
    # 包围盒(含2px边) 高 -> FH+4*scale 近似；按 alpha 包围盒高精确缩放
    s = FH / (y1 - y0)
    if (x1 - x0) * s > MAXW: s = MAXW / (x1 - x0)
    fw, fh = round(fig.width * s), round(fig.height * s)
    # 预乘 alpha 再缩放，避免边缘出灰/黑边
    f = np.array(fig).astype(np.float32) / 255.0
    pm = f.copy(); pm[:, :, :3] *= pm[:, :, 3:4]
    pm = cv2.resize(pm, (fw, fh), interpolation=cv2.INTER_LANCZOS4 if s < 1 else cv2.INTER_CUBIC)
    pm = np.clip(pm, 0, 1); al = pm[:, :, 3:4]
    rgb = np.where(al > 1e-4, pm[:, :, :3] / np.maximum(al, 1e-4), 0)
    out = np.concatenate([np.clip(rgb, 0, 1), al], 2)
    out = (out * 255 + 0.5).astype(np.uint8)
    canvas = np.zeros((H, W, 4), np.uint8)
    off = round(2 * s)
    px = W // 2 - fw // 2; py = TOP - off if s * (y1 - y0) >= FH - 1 else (H - fh) // 2
    canvas[max(0, py):py + fh, px:px + fw] = out[max(0, -py):H - py if py + fh > H else fh, :]
    return Image.fromarray(canvas), s

for d in ('portraits', 'portraits_2x', 'tmp'):
    os.makedirs(os.path.join(OUT, d), exist_ok=True)
c1, s1 = place(1); c1.save(os.path.join(OUT, 'portraits', name + '.png'))
c2, s2 = place(2); c2.save(os.path.join(OUT, 'portraits_2x', name + '.png'))
# 检查
aa = np.array(c1)[:, :, 3]; yy, xx = np.where(aa > 128)
semi = ((aa > 0) & (aa < 255)).sum(); solid = (aa == 255).sum()
print(f'{name}: src={im.size} bbox=({x0},{y0},{x1},{y1}) touch={ {k:v for k,v in touch.items() if v} } removed_px={removed} '
      f'final_bbox_h={yy.max()-yy.min()+1} top={yy.min()} cx={(xx.max()+xx.min())//2} w={xx.max()-xx.min()+1} semi/solid={semi/solid:.3f}')
# 三底检查图（全身 + 头部放大）
tiles = []
for col in [(255, 255, 255), (0, 0, 0), (255, 0, 255)]:
    bg = Image.new('RGBA', c2.size, col + (255,)); bg.alpha_composite(c2)
    cx = (xx.max() + xx.min()); w = (xx.max() - xx.min()) * 2
    head = bg.crop((max(0, cx - 420), 0, min(c2.width, cx + 420), 700)).convert('RGB')
    tiles.append(head)
sheet = Image.new('RGB', (sum(t.width for t in tiles) + 20, 700), (128, 128, 128)); x = 0
for t in tiles: sheet.paste(t, (x, 0)); x += t.width + 10
sheet.save(os.path.join(OUT, 'tmp', f'check_{name}.jpg'), quality=88)
