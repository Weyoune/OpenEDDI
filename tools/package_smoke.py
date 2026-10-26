#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run the extracted ZIP from an unrelated directory, without asset overrides."""
import argparse,hashlib,json,os,subprocess,tempfile,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--dist',type=Path,required=True);p.add_argument('--platform',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
archive=next(a.dist.glob('*-'+a.platform+'.zip'))
assert hashlib.sha256(archive.read_bytes()).hexdigest()==Path(str(archive)+'.sha256').read_text().split()[0]
with tempfile.TemporaryDirectory() as td:
 tmp=Path(td)
 with zipfile.ZipFile(archive) as z:
  assert all(not n.startswith('/') and '..' not in Path(n).parts for n in z.namelist())
  z.extractall(tmp)
 root=next(tmp.glob('OpenEDDI-*'));meta=json.loads((root/'build.json').read_text())
 if a.platform=='macos-arm64':binary=root/'OpenEDDI.app/Contents/MacOS/OpenEDDI'
 elif a.platform=='windows-x64':binary=root/'OpenEDDI.exe'
 else:binary=root/'OpenEDDI'
 binary.chmod(0o755)
 assert hashlib.sha256(binary.read_bytes()).hexdigest()==meta['binary_sha256']
 version=subprocess.run([str(binary),'--version'],cwd=tmp,capture_output=True,text=True,timeout=15)
 assert version.returncode==0 and 'OpenEDDI 0.1.0' in version.stdout
 options=tmp/'options.json';options.write_text(json.dumps({'language':'en','theme':'silver','mcp_enabled':False}))
 png=a.output.resolve()/'packaged.png'
 cmd=[str(binary),'--demo','--settings',str(options),'--screenshot',str(png),'--quit-after-ms','1200']
 if a.platform=='linux-x64' and not os.environ.get('DISPLAY'):cmd=['xvfb-run','-a']+cmd
 env=os.environ.copy()
 if a.platform=='windows-x64':env['OPENBMH_RML_DRIVER']='warp'
 result=subprocess.run(cmd,cwd=tmp,env=env,capture_output=True,text=True,timeout=45)
 (a.output/'packaged.log').write_text(result.stdout+result.stderr)
 assert result.returncode==0 and png.is_file(),result.stderr
 receipt={'platform':a.platform,'commit':meta['commit'],'binary_sha256':meta['binary_sha256'],'archive_sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'version':version.stdout.strip(),'native_gui_exit':result.returncode,'no_asset_override':True,'unrelated_working_directory':True,'screenshot_sha256':hashlib.sha256(png.read_bytes()).hexdigest()}
 (a.output/'package-smoke.json').write_text(json.dumps(receipt,indent=2)+'\n')
 print('Packaged GUI and version pass:',a.platform)
