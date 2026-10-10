"""打包发给主程的 workspace.zip（不含编译产物）。

攻防包、接手包是按队生成的，见 生成攻防包.py、生成接手包.py。
"""
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SKIP_DIRS = {'build', '__pycache__'}


def pack(src_root, base, out):
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED) as z:
        for d, dirs, files in os.walk(os.path.join(src_root, base)):
            dirs[:] = [x for x in dirs if x not in SKIP_DIRS]
            for f in files:
                full = os.path.join(d, f)
                z.write(full, os.path.relpath(full, src_root).replace('\\', '/'))
    print('已写入', os.path.abspath(out))
    with zipfile.ZipFile(out) as z:
        for n in z.namelist():
            print('  ', n)


pack(ROOT, 'workspace', os.path.join(ROOT, 'workspace.zip'))
