"""Compile a bot with the official flags. The compiler is taken from $CXX (default g++)."""
import hashlib
import os
import subprocess
import sys

FLAGS = ['-O2', '-std=c++20']
if sys.platform == 'win32':
    FLAGS.append('-static')


def compile_bot(src, cache_dir=None):
    """Compile src (.cpp) and return the executable path. Rebuilds only when the source changes."""
    src = os.path.abspath(src)
    cache_dir = cache_dir or os.path.join(os.path.dirname(src), '.build')
    os.makedirs(cache_dir, exist_ok=True)
    with open(src, 'rb') as f:
        digest = hashlib.sha1(f.read()).hexdigest()[:12]
    name = os.path.splitext(os.path.basename(src))[0] + '_' + digest
    exe = os.path.join(cache_dir, name + ('.exe' if sys.platform == 'win32' else ''))
    if not os.path.exists(exe):
        cmd = [os.environ.get('CXX', 'g++')] + FLAGS + ['-o', exe, src]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError('compile failed: %s\n%s' % (' '.join(cmd), r.stderr[-4000:]))
    return exe


def resolve(path, cache_dir=None):
    """Accept a .cpp (compiled on demand) or an executable."""
    if path.endswith('.cpp'):
        return compile_bot(path, cache_dir)
    return os.path.abspath(path)
