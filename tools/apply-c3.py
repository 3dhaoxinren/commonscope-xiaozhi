#!/usr/bin/env python3
"""Apply the public board add-on to a *clean*, pinned XiaoZhi checkout."""
from pathlib import Path
import argparse, subprocess
here=Path(__file__).resolve().parents[1]
pin='1b7644a6fba2b025ed36b4ac6309f122cc3cea14'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('checkout',type=Path)
parser.add_argument('--variant',choices=['prototype','current'],default='current')
args=parser.parse_args()
target=args.checkout.resolve(strict=True)
kit=here/'firmware/c3'/args.variant
patch=kit/'integration.patch'
def git(*a):
 return subprocess.check_output(['git','-C',str(target),*map(str,a)])
def inside(dest):
 return dest.resolve().is_relative_to(target)
if git('rev-parse','HEAD').decode().strip()!=pin or git('status','--porcelain','--untracked-files=all'):
 parser.error('必须使用固定上游提交上的干净 checkout；不会覆盖已有工作。')
if Path(git('rev-parse','--show-toplevel').decode().strip()).resolve()!=target:
 parser.error('指定的目录不是 Git 仓库根目录。')
src=kit/'overlay'
plan=[(f,target/f.relative_to(src)) for f in src.rglob('*') if f.is_file()]
if not plan or any(not inside(dest) or dest.exists() for f,dest in plan):
 parser.error('目标越界、冲突或覆盖层缺失。')
git('apply','--check',patch)
created=[]
try:
 for f,dest in plan:
  dest.parent.mkdir(parents=True,exist_ok=True)
  with dest.open('xb') as w:
   created.append(dest)
   w.write(f.read_bytes())
 git('apply',patch)
except (OSError,subprocess.CalledProcessError):
 for dest in reversed(created):dest.unlink()
 raise
print(f'Applied {args.variant}: {len(plan)} new source files (no firmware written).')
