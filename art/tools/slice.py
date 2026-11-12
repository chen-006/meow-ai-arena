# 把一位角色的两张 3x2 表情母版切成 12 张 1024x1024 透明方图 + 头像（方 512、圆 512）
# 用法: python slice.py 角色名 [A母版路径 B母版路径]   （缺省取 ./expr/raw/ 下该角色 _A/_B 最新一张）
# 在工作目录里运行；切好的图写进 <输出目录>/<角色>/，输出目录用环境变量 EXPR_OUT 指定（默认 ./expressions）。
# 格名默认是通用的 12 个表情；./expr/<角色>_表情名.json 存在时用它（军需官用的是自己的 12 个表情）。
# 只切、不判断好坏；格子不规整（每格不是恰好一个主体）就报 FAIL 退出码 2
import sys, os, glob, json, numpy as np, cv2
from PIL import Image, ImageDraw
ROOT = 'expr'
OUT = os.environ.get('EXPR_OUT') or 'expressions'
NAMES = {'A': ['常态', '开心', '得意', '生气', '伤心', '震惊'], 'B': ['无语', '思考', '流口水', '头顶铁盆', '尴尬地笑', '犯困']}
COLS, ROWS, S, TGT, MAXF = 3, 2, 1024, 860, 1000


def latest(c, k):
    fs = sorted(glob.glob(os.path.join(ROOT, 'raw', f'{c}_{k}__*.png')))
    return fs[-1] if fs else None


def cut(path):
    arr = np.array(Image.open(path).convert('RGBA'))
    H, W = arr.shape[:2]
    a = np.clip(arr[:, :, 3].astype(np.float32) * 255.0 / 248.0, 0, 255)
    solid = (a > 128).astype(np.uint8)
    near = cv2.dilate(solid, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (31, 31)))
    a[near == 0] = 0
    arr[:, :, 3] = a.round().astype(np.uint8)
    mask = cv2.dilate((arr[:, :, 3] > 24).astype(np.uint8), np.ones((5, 5), np.uint8))
    n, lab, st, cen = cv2.connectedComponentsWithStats(mask, connectivity=8)
    big_thr = W * H * 0.01
    cw, ch = W / COLS, H / ROWS
    cells = [[] for _ in range(COLS * ROWS)]
    bigs = [[] for _ in range(COLS * ROWS)]
    for i in range(1, n):
        cx, cy = cen[i]
        k = min(ROWS - 1, int(cy // ch)) * COLS + min(COLS - 1, int(cx // cw))
        cells[k].append(i)
        if st[i, cv2.CC_STAT_AREA] > big_thr:
            bigs[k].append(i)
    bad = [k for k in range(COLS * ROWS) if len(bigs[k]) != 1]
    if bad:
        return None, f'{os.path.basename(path)}: 格子不规整，主体数 {[len(b) for b in bigs]}'
    out = []
    for k in range(COLS * ROWS):
        m = np.isin(lab, cells[k])
        # 小碎块离主体太远（>150px）的丢掉
        x, y, w, h = st[bigs[k][0], :4]
        keep = np.zeros_like(m)
        for i in cells[k]:
            bx, by, bw, bh = st[i, :4]
            if i == bigs[k][0] or (bx < x + w + 150 and bx + bw > x - 150 and by < y + h + 150 and by + bh > y - 150):
                keep |= lab == i
        im = arr.copy(); im[~keep, 3] = 0
        al = im[:, :, 3]; ys, xs = np.where(al > 8)
        box = (xs.min(), ys.min(), xs.max() + 1, ys.max() + 1)
        crop = im[box[1]:box[3], box[0]:box[2]]
        ca = crop[:, :, 3]; ys2, xs2 = np.where(ca > 128)
        bh = ys2.max() - ys2.min() + 1
        low = ca[int(ca.shape[0] * 0.85):] > 128
        bx = np.where(low.any(0))[0]
        cxb = (bx.min() + bx.max()) / 2 if len(bx) else crop.shape[1] / 2
        out.append(dict(img=crop, h=bh, w=crop.shape[1], ch=crop.shape[0], cxb=cxb))
    return out, None


def resize_pm(rgba, fw, fh):
    f = rgba.astype(np.float32) / 255.0
    pm = f.copy(); pm[:, :, :3] *= pm[:, :, 3:4]
    s = fw / rgba.shape[1]
    pm = np.clip(cv2.resize(pm, (fw, fh), interpolation=cv2.INTER_AREA if s < 1 else cv2.INTER_CUBIC), 0, 1)
    al = pm[:, :, 3:4]
    rgb = np.where(al > 1e-4, pm[:, :, :3] / np.maximum(al, 1e-4), 0)
    return (np.concatenate([np.clip(rgb, 0, 1), al], 2) * 255 + 0.5).astype(np.uint8)


def main():
    global NAMES
    c = sys.argv[1]
    nf = os.path.join(ROOT, f'{c}_表情名.json')
    if os.path.exists(nf):
        NAMES = json.load(open(nf, encoding='utf-8'))
    paths = {'A': sys.argv[2] if len(sys.argv) > 3 else latest(c, 'A'), 'B': sys.argv[3] if len(sys.argv) > 3 else latest(c, 'B')}
    sheets = {}
    for k, p in paths.items():
        if not p:
            print(f'FAIL {c} 缺 {k} 母版'); sys.exit(2)
        r, err = cut(p)
        if err:
            print('FAIL', c, err); sys.exit(2)
        sheets[k] = r
    # 每张母版按该张主体高度中位数（不含铁盆格）定比例，两张统一到同一目标高度
    med = {k: float(np.median([d['h'] for i, d in enumerate(v) if NAMES[k][i] != '头顶铁盆'])) for k, v in sheets.items()}
    T = TGT
    for k, v in sheets.items():
        for d in v:
            T = min(T, MAXF * med[k] / max(d['ch'], d['w']))
    od = os.path.join(OUT, c); os.makedirs(od, exist_ok=True)
    info = {}
    for k, v in sheets.items():
        s = T / med[k]
        for i, d in enumerate(v):
            fw, fh = max(1, round(d['w'] * s)), max(1, round(d['ch'] * s))
            im = resize_pm(d['img'], fw, fh)
            canvas = np.zeros((S, S, 4), np.uint8)
            px = int(round(S / 2 - d['cxb'] * s)); px = min(max(px, 0), S - fw)
            py = S - fh
            canvas[py:py + fh, px:px + fw] = im
            Image.fromarray(canvas).save(os.path.join(od, NAMES[k][i] + '.png'))
            info[NAMES[k][i]] = dict(scale=round(s, 3), h=int(d['h'] * s), top=py)
    # 头像：从常态图按比例取脸部方块
    base = np.array(Image.open(os.path.join(od, '常态.png')))
    al = base[:, :, 3]; ys, xs = np.where(al > 128)
    top = ys.min(); bh = S - top
    low = al[S - int(bh * 0.15):] > 128; bx = np.where(low.any(0))[0]
    cx = (bx.min() + bx.max()) / 2
    side = int(bh * 0.62); cy = top + bh * 0.38
    x0, y0 = int(cx - side / 2), int(cy - side / 2)
    pad = np.zeros((S + 2 * side, S + 2 * side, 4), np.uint8); pad[side:side + S, side:side + S] = base
    sq = pad[y0 + side:y0 + 2 * side, x0 + side:x0 + 2 * side]
    sq = resize_pm(sq, 512, 512)
    Image.fromarray(sq).save(os.path.join(od, '头像_方.png'))
    # 圆形：浅色圆底 + 人物，圆外透明（4 倍超采样抗锯齿）
    big = Image.new('L', (2048, 2048), 0); ImageDraw.Draw(big).ellipse((8, 8, 2040, 2040), fill=255)
    circ = big.resize((512, 512), Image.LANCZOS)
    disc = Image.new('RGBA', (512, 512), (244, 242, 238, 255)); disc.alpha_composite(Image.fromarray(sq))
    disc.putalpha(circ)
    disc.save(os.path.join(od, '头像_圆.png'))
    print('OK', c, json.dumps(dict(T=round(T), med={k: round(v) for k, v in med.items()}, src={k: os.path.basename(p) for k, p in paths.items()}), ensure_ascii=False))


main()
