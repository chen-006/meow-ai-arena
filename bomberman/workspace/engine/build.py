"""Compile a bot with an existing compiler; never run the compiled program."""
import argparse,json,os,platform,shutil,subprocess,sys,tempfile,time
from pathlib import Path

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--compile',action='store_true');ap.add_argument('--output',default='bot.exe' if os.name=='nt' else 'bot')
    ap.add_argument('--source',default='baseline.cpp',help='C++ source to compile')
    args=ap.parse_args();base=Path(__file__).resolve().parent.parent
    compiler=os.environ.get('CXX') or shutil.which('g++') or shutil.which('clang++')
    if not compiler and os.name=='nt':  # 没加进 PATH 的 w64devkit：仓库根目录下的 tools/，或 C 盘根目录
        for root in [*(a/'tools' for a in base.parents),Path('C:/')]:
            guess=root/'w64devkit/bin/g++.exe'
            if guess.is_file():compiler=str(guess);break
    info=dict(python=sys.executable,version=sys.version.split()[0],platform=platform.platform(),
              standard_library_only=True,compiler=compiler,clock=time.get_clock_info('perf_counter').__dict__)
    try:
        with tempfile.TemporaryFile(dir=base) as f:f.write(b'probe');f.flush()
        info['workspace_writable']=True
    except OSError as e:info['workspace_writable']=False;info['write_error']=str(e)
    if args.compile:
        if not compiler:raise SystemExit('No C++ compiler found. Report the environment failure; do not install software or change permissions.')
        cmd=[compiler,'-std=c++20','-O2',str(base/args.source),'-o',str(Path(args.output).resolve())]
        if os.name=='nt':cmd.insert(1,'-static')
        try:
            subprocess.run(cmd,check=True,timeout=60)
        except subprocess.TimeoutExpired:
            raise SystemExit('Compilation exceeded 60 seconds; no bot was launched.')
        except subprocess.CalledProcessError as e:
            raise SystemExit(f'Compiler failed with exit code {e.returncode}; see diagnostics above.')
        except OSError as e:
            raise SystemExit(f'Cannot start compiler: {e}')
        info['compiled_program']=str(Path(args.output).resolve())
    print(json.dumps(info,ensure_ascii=False,indent=2))
    if sys.version_info<(3,10) or not info['workspace_writable']:raise SystemExit(1)
if __name__=='__main__':main()
