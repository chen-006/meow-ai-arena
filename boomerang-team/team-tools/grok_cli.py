"""Grok Build 命令行的薄包装：提示词从标准输入读、写进临时文件用 --prompt-file 传（不走命令行参数）；
输出 JSON 里的模型思考字段（thought 等）在这里丢掉，再打印到标准输出，所以主办方日志里不会有思考内容。

用法：python grok_cli.py <grok.exe> [grok 参数...]   （提示词走标准输入，UTF-8）
标准输出：一行 JSON（去掉思考字段后的 grok 输出）；grok 输出不是 JSON 时原样丢弃，只报一行错误 JSON。
"""
import json
import os
import subprocess
import sys
import tempfile

DROP = {'thought', 'thoughts', 'thinking', 'reasoning', 'reasoning_content', 'reasoningContent'}


def scrub(value):
    if isinstance(value, dict):
        return {k: scrub(v) for k, v in value.items() if k not in DROP}
    if isinstance(value, list):
        return [scrub(v) for v in value if not (isinstance(v, dict) and v.get('type') in DROP)]
    return value


def clean_output(raw):
    """grok 的 json 输出是一个对象；也兼容逐行 JSON。返回去掉思考字段后的单行 JSON 文本。"""
    raw = raw.strip()
    try:
        return json.dumps(scrub(json.loads(raw)), ensure_ascii=False)
    except ValueError:
        pass
    events = []
    for line in raw.splitlines():
        try: events.append(scrub(json.loads(line)))
        except ValueError: continue
    if events:
        merged = next((e for e in reversed(events) if isinstance(e, dict) and ('text' in e or 'sessionId' in e)), events[-1])
        return json.dumps(merged, ensure_ascii=False)
    return json.dumps(dict(type='error', error=dict(type='grok_output_not_json', message='grok 没有输出可解析的 JSON')), ensure_ascii=False)


def main():
    if len(sys.argv) < 2:
        print('用法：grok_cli.py <grok.exe> [参数...]', file=sys.stderr); return 64
    exe, args = sys.argv[1], sys.argv[2:]
    prompt = sys.stdin.buffer.read().decode('utf-8-sig')
    fd, path = tempfile.mkstemp(prefix='grok_prompt_', suffix='.txt')
    try:
        with os.fdopen(fd, 'w', encoding='utf-8', newline='') as f:
            f.write(prompt)
        p = subprocess.run([exe, '--prompt-file', path] + args, stdin=subprocess.DEVNULL,
                           capture_output=True)
    finally:
        try: os.unlink(path)
        except OSError: pass
    out = clean_output(p.stdout.decode('utf-8', 'replace'))
    sys.stdout.write(out + '\n'); sys.stdout.flush()
    err = p.stderr.decode('utf-8', 'replace')
    if err.strip(): sys.stderr.write(err[-2000:])
    return p.returncode


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8'); sys.stderr.reconfigure(encoding='utf-8')
    sys.exit(main())
