# 调 OpenAI 图像接口出一张图。密钥从环境变量 OPENAI_API_KEY 读，不打印。
# 在工作目录里运行：原图写进 ./raw/<tag>__<时间>.png，每次调用的记录追加到 ./logs/calls.jsonl。
# 用法: python gen.py --tag 名字 --prompt prompts/x.txt [--ref a.png --ref b.jpg ...] [--size 1440x2560] [--quality max] [--bg transparent|opaque] [--model gpt-image-2.6-sunburst]
import argparse, base64, json, os, sys, time, datetime, requests
ROOT = os.getcwd()
PRICE_OUT, PRICE_IN_IMG, PRICE_IN_TXT = 30e-6, 8e-6, 5e-6
ap = argparse.ArgumentParser()
ap.add_argument('--tag', required=True); ap.add_argument('--prompt', required=True)
ap.add_argument('--ref', action='append', default=[]); ap.add_argument('--size', default='1440x2560')
ap.add_argument('--quality', default='max'); ap.add_argument('--bg', default='transparent')
ap.add_argument('--model', default='gpt-image-2.6-sunburst'); ap.add_argument('--complexity', default=None)
a = ap.parse_args()
key = os.environ.get('OPENAI_API_KEY', '').strip()
if not key:
    sys.exit('请先设置环境变量 OPENAI_API_KEY')
os.makedirs(os.path.join(ROOT, 'raw'), exist_ok=True); os.makedirs(os.path.join(ROOT, 'logs'), exist_ok=True)
H = {'Authorization': 'Bearer ' + key}
prompt = open(a.prompt, encoding='utf-8').read()
data = {'model': a.model, 'prompt': prompt, 'size': a.size, 'quality': a.quality,
        'background': a.bg, 'output_format': 'png', 'n': '1'}
if a.complexity: data['complexity'] = a.complexity
ts = datetime.datetime.now().strftime('%m%d_%H%M%S')
out = os.path.join(ROOT, 'raw', f'{a.tag}__{ts}.png')
log = {'tag': a.tag, 'ts': ts, 'model': a.model, 'size': a.size, 'quality': a.quality, 'bg': a.bg,
       'prompt_file': a.prompt, 'refs': a.ref, 'out': out}
t0 = time.time()
for attempt in range(6):
    try:
        if a.ref:
            files = [('image[]', (os.path.basename(p), open(p, 'rb'), 'image/png' if p.lower().endswith('.png') else 'image/jpeg')) for p in a.ref]
            r = requests.post('https://api.openai.com/v1/images/edits', headers=H, data=data, files=files, timeout=900)
        else:
            r = requests.post('https://api.openai.com/v1/images/generations', headers=H, json=data, timeout=900)
    except Exception as e:
        log['err'] = 'conn: ' + repr(e)[:300]; time.sleep(20 * (attempt + 1)); continue
    if r.status_code == 429 or r.status_code >= 500:
        log['err'] = f'{r.status_code}: {r.text[:300]}'; time.sleep(30 * (attempt + 1)); continue
    break
log['secs'] = round(time.time() - t0, 1)
try:
    j = r.json()
except Exception:
    j = {'raw': r.text[:500]}
log['status'] = r.status_code
if r.status_code == 200 and j.get('data'):
    open(out, 'wb').write(base64.b64decode(j['data'][0]['b64_json']))
    u = j.get('usage', {}); log['usage'] = u
    det = u.get('input_tokens_details', {}) or {}
    cost = u.get('output_tokens', 0) * PRICE_OUT + det.get('image_tokens', 0) * PRICE_IN_IMG + det.get('text_tokens', 0) * PRICE_IN_TXT
    log['cost'] = round(cost, 4); log.pop('err', None)
    for k in ('size', 'quality', 'background', 'output_format'):
        if k in j: log['resp_' + k] = j[k]
else:
    log['out'] = None; log['error_body'] = json.dumps(j, ensure_ascii=False)[:1500]
with open(os.path.join(ROOT, 'logs', 'calls.jsonl'), 'a', encoding='utf-8') as f:
    f.write(json.dumps(log, ensure_ascii=False) + '\n')
print(json.dumps({k: log.get(k) for k in ('tag', 'status', 'secs', 'cost', 'out', 'usage', 'error_body')}, ensure_ascii=False))
