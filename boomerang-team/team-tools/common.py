"""仅标准库；不联网。所有子进程均同步、有超时、无 shell 拼接。"""
import json
import os
import subprocess
import time
import uuid
from pathlib import Path
from datetime import datetime, timezone


def read_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    tmp.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')
    os.replace(tmp, path)


def now():
    return datetime.now(timezone.utc).isoformat()


def run_process(argv, cwd, env, text, timeout):
    run_process.last_process = {}
    if timeout <= 0:
        raise ValueError('超时必须大于 0')
    # exe / Python 均直接执行；禁止 .cmd/.bat，避免 Windows 隐式 shell 的转义问题。
    if Path(argv[0]).suffix.lower() in ('.cmd', '.bat', '.ps1'):
        raise ValueError('子进程请配置真实 exe，不能配置 cmd/bat/ps1 包装器')
    p = subprocess.Popen(argv, cwd=cwd, env=env, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    run_process.last_process = dict(pid=p.pid, timeout=False)
    try:
        out, err = p.communicate(text.encode('utf-8'), timeout=timeout)
        code = p.returncode
    except subprocess.TimeoutExpired:
        run_process.last_process['timeout'] = True
        if os.name == 'nt':
            run_process.last_process['tree_pids'] = process_tree(p.pid)
        if os.name == 'nt':
            # 只操作本函数刚启动的 PID 及其后代，绝不按进程名结束。
            subprocess.run(['taskkill', '/PID', str(p.pid), '/T', '/F'],
                           capture_output=True, timeout=10)
        else:
            p.kill()
        out, err = p.communicate(timeout=10)
        code = 124
        err += '\n调用超时；已结束本次启动的进程树。'.encode('utf-8')
        if os.name == 'nt':
            run_process.last_process['remaining_pids'] = [pid for pid in run_process.last_process['tree_pids'] if process_alive(pid)]
    return code, out.decode('utf-8', errors='replace'), err.decode('utf-8', errors='replace')


def process_alive(pid):
    import ctypes
    from ctypes import wintypes
    k = ctypes.WinDLL('kernel32', use_last_error=True)
    k.OpenProcess.restype = wintypes.HANDLE
    k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    k.CloseHandle.argtypes = [wintypes.HANDLE]
    handle = k.OpenProcess(0x1000, False, pid)
    if not handle:
        if ctypes.get_last_error() == 87:  # PID 已不存在
            return False
        raise OSError('无法确认任务进程已经退出')

    try:
        code = wintypes.DWORD()
        if not k.GetExitCodeProcess(handle, ctypes.byref(code)):
            raise OSError('无法查询任务进程退出状态')
        return code.value == 259
    finally: k.CloseHandle(handle)


def process_tree(pid):
    # 只查询 PID/父 PID；不抓取命令行、环境或其他进程内容。
    proc = subprocess.run(['powershell', '-NoProfile', '-Command',
        'Get-CimInstance Win32_Process | Select-Object ProcessId,ParentProcessId | ConvertTo-Json -Compress'],
        capture_output=True, timeout=10)
    entries = json.loads(proc.stdout.decode('utf-8-sig'))
    found = {pid}
    while True:
        children = {int(x['ProcessId']) for x in entries if int(x['ParentProcessId']) in found}
        if children <= found: return sorted(found)
        found |= children


class Busy(Exception):
    pass


class Lock:
    """操作系统文件锁；进程崩溃自动释放，锁文件保留不表示忙。"""
    def __init__(self, path, wait=0):
        self.path = Path(path)
        self.wait = wait

    def __enter__(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.f = self.path.open('a+b')
        self.f.seek(0)
        deadline = time.monotonic() + self.wait
        while True:
            try:
                if os.name == 'nt':
                    import msvcrt
                    msvcrt.locking(self.f.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(self.f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    self.f.close()
                    raise Busy('队员正忙')
                time.sleep(.01)
        return self

    def __exit__(self, *args):
        self.f.close()
