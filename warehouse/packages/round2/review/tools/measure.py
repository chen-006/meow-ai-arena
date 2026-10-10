"""编译与计时工具（bench.py 与正式评测共用）。

计时指标是进程的 CPU 总时间（用户态 + 内核态，所有线程相加）。
"""
import hashlib
import os
import subprocess
import sys
import time

FLAGS = ['-O2', '-std=c++20']
if sys.platform == 'win32':
    FLAGS.append('-static')
EXE = '.exe' if sys.platform == 'win32' else ''


def build(src_dir, out_path):
    """用固定参数编译 src_dir 下所有 .cpp；失败时抛出 RuntimeError。"""
    sources = sorted(os.path.join(src_dir, f) for f in os.listdir(src_dir) if f.endswith('.cpp'))
    if not sources:
        raise RuntimeError('%s 下没有 .cpp 文件' % src_dir)
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    cmd = [os.environ.get('CXX', 'g++')] + FLAGS + ['-o', out_path] + sources
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError('编译失败：%s\n%s' % (' '.join(cmd), r.stderr[-4000:]))
    return out_path


def normalized(data):
    return data.replace(b'\r\n', b'\n')


def digest(data):
    return hashlib.sha256(normalized(data)).hexdigest()


if sys.platform == 'win32':
    import ctypes
    from ctypes import wintypes

    class _FILETIME(ctypes.Structure):
        _fields_ = [('lo', wintypes.DWORD), ('hi', wintypes.DWORD)]

    class _PMC(ctypes.Structure):
        _fields_ = [('cb', wintypes.DWORD), ('PageFaultCount', wintypes.DWORD),
                    ('PeakWorkingSetSize', ctypes.c_size_t), ('WorkingSetSize', ctypes.c_size_t),
                    ('QuotaPeakPagedPoolUsage', ctypes.c_size_t), ('QuotaPagedPoolUsage', ctypes.c_size_t),
                    ('QuotaPeakNonPagedPoolUsage', ctypes.c_size_t), ('QuotaNonPagedPoolUsage', ctypes.c_size_t),
                    ('PagefileUsage', ctypes.c_size_t), ('PeakPagefileUsage', ctypes.c_size_t)]

    _k32 = ctypes.WinDLL('kernel32', use_last_error=True)
    _k32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(_FILETIME)] * 4
    _k32.K32GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.POINTER(_PMC), wintypes.DWORD]

    def _usage(proc):
        c, e, k, u = _FILETIME(), _FILETIME(), _FILETIME(), _FILETIME()
        h = wintypes.HANDLE(int(proc._handle))
        _k32.GetProcessTimes(h, ctypes.byref(c), ctypes.byref(e), ctypes.byref(k), ctypes.byref(u))
        cpu = ((k.hi << 32 | k.lo) + (u.hi << 32 | u.lo)) / 1e7
        pmc = _PMC()
        pmc.cb = ctypes.sizeof(_PMC)
        _k32.K32GetProcessMemoryInfo(h, ctypes.byref(pmc), pmc.cb)
        return cpu, max(pmc.PeakWorkingSetSize, pmc.PeakPagefileUsage) / 2 ** 20


def run(exe, input_path, timeout=None):
    """运行一次。返回 dict：cpu 秒、wall 秒、峰值内存 MB、输出摘要、退出码、是否超时、原始输出。"""
    with open(input_path, 'rb') as fin:
        t0 = time.perf_counter()
        p = subprocess.Popen([exe], stdin=fin, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
        timed_out = False
        if sys.platform == 'win32':
            try:
                out, _ = p.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                p.kill()
                out, _ = p.communicate()
                timed_out = True
            wall = time.perf_counter() - t0
            cpu, mem = _usage(p)
            code = p.returncode
        else:
            import threading
            chunks = []
            reader = threading.Thread(target=lambda: chunks.append(p.stdout.read()))
            reader.start()
            timer = None
            if timeout:
                def _kill():
                    nonlocal timed_out
                    timed_out = True
                    p.kill()
                timer = threading.Timer(timeout, _kill)
                timer.start()
            _, status, ru = os.wait4(p.pid, 0)
            wall = time.perf_counter() - t0
            if timer:
                timer.cancel()
            reader.join()
            p.returncode = os.waitstatus_to_exitcode(status)
            out = chunks[0] if chunks else b''
            cpu = ru.ru_utime + ru.ru_stime
            mem = ru.ru_maxrss / 1024
            code = p.returncode
    return {'cpu': cpu, 'wall': wall, 'mem_mb': mem, 'digest': digest(out), 'code': code,
            'timeout': timed_out, 'output': out}
