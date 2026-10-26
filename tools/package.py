#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package only the application, its own UI and licenses. Never game installs."""
import argparse,hashlib,json,os,shutil,subprocess,tempfile,zipfile
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--build',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--platform',required=True);p.add_argument('--source',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[1];build=a.build.resolve();a.out.mkdir(parents=True,exist_ok=True)
version='0.1.0';revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
def digest(f):return hashlib.sha256(f.read_bytes()).hexdigest()
def archive(folder,name):
 target=a.out/(name+'.zip')
 with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for f in sorted(folder.rglob('*')):
   if f.is_file():z.write(f,Path(name)/f.relative_to(folder))
 (a.out/(target.name+'.sha256')).write_text(digest(target)+'  '+target.name+'\n')
with tempfile.TemporaryDirectory() as td:
 name=f'OpenEDDI-{version}-{a.platform}';folder=Path(td)/name;folder.mkdir()
 binary=build/('OpenEDDI.exe' if a.platform.startswith('windows') else 'OpenEDDI')
 dest=folder
 if a.platform=='macos-arm64':
  app=folder/'OpenEDDI.app';dest=app/'Contents/MacOS';dest.mkdir(parents=True)
  (app/'Contents/Info.plist').write_text(f'''<?xml version="1.0"?><!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd"><plist version="1.0"><dict><key>CFBundleName</key><string>OpenEDDI</string><key>CFBundleIdentifier</key><string>org.openeddi.editor</string><key>CFBundleExecutable</key><string>OpenEDDI</string><key>CFBundlePackageType</key><string>APPL</string><key>CFBundleShortVersionString</key><string>{version}</string><key>NSHighResolutionCapable</key><true/></dict></plist>''')
 shutil.copy2(binary,dest/binary.name);(dest/binary.name).chmod(0o755)
 asset_dest=(app/'Contents/Resources' if a.platform=='macos-arm64' else dest)
 shutil.copytree(build/'assets/editor',asset_dest/'assets/editor')
 shutil.copy2(root/'LICENSE',folder/'LICENSE');shutil.copy2(root/'README.md',folder/'README.md')
 upstream=build/'_deps/openbmh-src'
 cache=(build/'CMakeCache.txt').read_text()
 for line in cache.splitlines():
  if line.startswith('FETCHCONTENT_SOURCE_DIR_OPENBMH:') and '=' in line and line.split('=',1)[1]:upstream=Path(line.split('=',1)[1])
 shutil.copytree(upstream/'docs/licenses',folder/'licenses')
 (folder/'build.json').write_text(json.dumps({'product':'OpenEDDI','version':version,'commit':revision,'platform':a.platform,'binary_sha256':digest(binary),'upstream':'b879d5be008739f69afef562fb4bce56f1bdc778'},indent=2)+'\n')
 if a.platform=='macos-arm64':subprocess.run(['codesign','--force','--deep','--sign','-',str(app)],check=True)
 metadata=json.loads((folder/'build.json').read_text());metadata['binary_sha256']=digest(dest/binary.name);(folder/'build.json').write_text(json.dumps(metadata,indent=2)+'\n')
 archive(folder,name)
 if a.source:
  source=Path(td)/'source';source.mkdir()
  for file in subprocess.check_output(['git','ls-files'],cwd=root,text=True).splitlines():
   f=root/file
   if f.is_file():target=source/file;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(f,target)
  # Corresponding dependency source, including retained upstream notices.
  deps=source/'deps';deps.mkdir()
  mapping={'openbmh':upstream}
  for line in cache.splitlines():
   if line.startswith('FETCHCONTENT_SOURCE_DIR_') and '=' in line:
    key,value=line.split('=',1);key=key.split(':',1)[0];value=value.strip()
    if value:mapping[key.removeprefix('FETCHCONTENT_SOURCE_DIR_').lower()]=Path(value)
  for key in ['sdl2','miniz','lzma_sdk','nfd','rmlui','freetype','editor_json','editor_http','editor_images','editor_svg','editor_xml']:
   mapping.setdefault(key,build/'_deps'/(key+'-src'))
  for key,path in mapping.items():
   if path.is_dir():shutil.copytree(path,deps/key,ignore=shutil.ignore_patterns('.git','__pycache__'),symlinks=False)
  archive(source,f'OpenEDDI-{version}-source')
