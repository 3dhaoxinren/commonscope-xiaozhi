#!/usr/bin/env python3
"""Compile and run the standalone host tests. No device or network I/O."""
import argparse, os, subprocess, tempfile
from pathlib import Path
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--cc',default=os.environ.get('CC','cc'))
args=ap.parse_args()
repo=Path(__file__).resolve().parents[1]
firm=repo/'firmware'
tool=[args.cc]+(['cc'] if Path(args.cc).stem.lower()=='zig' else [])
cases={'bridge':[firm/'shared/test_bridge.c'],
       'gesture':[firm/'shared/test_ptt.c'],
       'panel_wifi':[firm/'panel/wifi/test_runtime.c',firm/'panel/wifi/wifi_runtime.c']}
with tempfile.TemporaryDirectory(prefix='commonscope-host-') as d:
 for case,files in cases.items():
  out=Path(d)/(case+('.exe' if os.name=='nt' else ''))
  subprocess.run([*tool,'-std=c11','-Wall','-Wextra','-UNDEBUG',*map(str,files),'-o',str(out)],check=True)
  subprocess.run([str(out)],check=True)
 subprocess.run([*tool,'-std=c11','-Wall','-Wextra','-c',
                 str(firm/'panel/ptt/commonscope_ptt.c'),'-o',str(Path(d)/'panel-ptt.o')],check=True)
