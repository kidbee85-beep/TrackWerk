#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare an existing TrackWerk card for canonical installation names. Dry-run by default.
Keep the external backup until the updated application has been tested on the device.
"""
from pathlib import Path
import argparse,hashlib,json,os,shutil,sqlite3,subprocess,plistlib,tempfile
import canonical_layout as layout

def sha(p):return layout.sha(p)
def safe(p):return layout.checked(p)
def metadata_value(text,maps):
 for old,new in sorted(maps.items(),key=lambda x:len(x[0]),reverse=True):text=text.replace(old,new)
 return text

def prepare(card,device,backup,apply=False,target=None,identity_guard=None):
 card=safe(card);backup=safe(backup)
 if backup.exists():raise RuntimeError('Use a fresh external backup folder')
 if card==backup or card in backup.parents or str(backup).startswith('/Volumes/'):raise RuntimeError('Keep the backup outside the card')
 if identity_guard:identity_guard()
 tasks=[];maps={};metadata={};renames=[]
 if device=='brick':
  if target is None:raise RuntimeError('Select the existing application with --target')
  target=safe(target)
  if card not in target.parents:raise RuntimeError('Application is outside the selected card')
  tasks.append((target,'brick'))
 else:
  if not (card/'.tmp_update/onionVersion/version.txt').is_file():raise RuntimeError('Expected an existing Onion card')
  package=json.loads((Path(__file__).parent/'UPDATE-MANIFEST.json').read_text());programs=package['programs'];known={}
  for path,row in programs.items():
   for digest in [row['sha256'],*row['previous_sha256s']]:known[digest]=('miyoo-plus' if 'TrackWerkPlus/' in path else 'miyoo-mini')
  games=card/'Roms/PORTS/Games'
  for app in sorted(games.iterdir()):
   if not app.is_dir() or app.is_symlink():continue
   matches=[]
   for p in app.iterdir():
    if p.is_file() and not p.is_symlink() and p.stat().st_size>=52:
     with p.open('rb') as f:magic=f.read(4)
     if magic==b'\x7fELF' and sha(p) in known:matches.append((p,known[sha(p)]))
   if not matches:continue
   if len(matches)!=1:raise RuntimeError('Ambiguous application programs')
   p,profile=matches[0]
   if device=='miyoo-mini' and profile=='miyoo-plus':continue
   tasks.append((app,profile));new='TrackWerkPlus' if profile=='miyoo-plus' else 'TrackWerk';maps[app.name]=new
   shortcut=card/'App'/app.name
   if shortcut.is_dir():tasks.append((shortcut,'miyoo-app-plus' if profile=='miyoo-plus' else 'miyoo-app'))
  if not tasks:raise RuntimeError('No program matches the verified predecessor hashes')
  homes=[p for p in card.iterdir() if p.is_dir() and not p.is_symlink() and (p/'config.json').is_file() and (p/'Samples').is_dir() and (p.name in maps or p.name=='TrackWerk')]
  if len(homes)!=1:raise RuntimeError('Select a card with exactly one recognised data home; nothing was written')
  tasks.append((homes[0],'miyoo-home'));maps[homes[0].name]='TrackWerk'
  # Read only the selected program's menu paths, never the sample library.
  for root in [card/'Roms/PORTS/Shortcuts',card/'Roms/PORTS/Imgs']:
   if not root.exists():continue
   for p in root.iterdir():
    if p.is_symlink():raise RuntimeError('Linked menu path refused')
    if not p.is_file():continue
    new=metadata_value(p.name,maps)
    if new!=p.name:renames.append((p,p.with_name(new)))
    if p.suffix=='.port':
     before=p.read_text();after=metadata_value(before,maps)
     if after!=before:metadata[p]=after.encode()
  for p in [card/'Roms/PORTS/miyoogamelist.xml',card/'Roms/PORTS/Shortcuts/miyoogamelist.xml',*list((card/'.tmp_update').glob('**/*history*.json'))]:
   if p.is_file():
    before=p.read_text();after=metadata_value(before,maps)
    if after!=before:metadata[p]=after.encode()
  for p in (card/'Roms/PORTS').rglob('*_cache6.db'):
   if any(Path(str(p)+suffix).exists() for suffix in ['-wal','-shm','-journal']):raise RuntimeError('Pending menu cache journal; close the menu first')
   with tempfile.TemporaryDirectory(prefix='trackwerk-cache-') as temp:
    q=Path(temp)/'cache.db';shutil.copy2(p,q);db=sqlite3.connect(q);changed=False
    for (table,) in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'").fetchall():
     qt='"'+table.replace('"','""')+'"';cols=[x[1] for x in db.execute('PRAGMA table_info('+qt+')')]
     for row in db.execute('SELECT rowid,* FROM '+qt).fetchall():
      values={k:metadata_value(v,maps) for k,v in zip(cols,row[1:]) if isinstance(v,str) and metadata_value(v,maps)!=v}
      if values:
       keys=list(values);db.execute('UPDATE '+qt+' SET '+','.join('"'+k.replace('"','""')+'"=?' for k in keys)+' WHERE rowid=?',[values[k] for k in keys]+[row[0]]);changed=True
    db.commit();assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok';db.close()
    if changed:metadata[p]=q.read_bytes()
 for old,new in renames:
  if new.exists() and new!=old:raise RuntimeError('Menu filename collision; nothing was written')
 plans=[layout.migrate(app,profile,None,False) for app,profile in tasks]
 result=dict(status='planned',device=device,steps=plans,metadata_files=[str(p) for p in metadata],menu_renames=[[str(a),str(b)] for a,b in renames])
 if not apply:return result
 if identity_guard:identity_guard()
 backup.mkdir(parents=True);(backup/'PLAN.json').write_text(json.dumps(result,indent=2));completed=[]
 for i,(app,profile) in enumerate(tasks):
  if identity_guard:identity_guard()
  completed.append(layout.migrate(app,profile,backup/('layout-'+str(i)),True))
  (backup/'PROGRESS.json').write_text(json.dumps(completed,indent=2))
 if device=='brick':
  app=Path(completed[0]['destination']);payload=Path(__file__).parent/'helper-payload'
  for src in sorted(payload.iterdir()):
   dst=app/'tools'/src.name
   if not dst.is_file():raise RuntimeError('Existing helper is missing; external layout backup retained')
   shutil.copy2(dst,backup/(src.name+'.previous'));fd,name=tempfile.mkstemp(prefix='.trackwerk-helper-',dir=dst.parent)
   with os.fdopen(fd,'wb') as f:f.write(src.read_bytes());f.flush();os.fsync(f.fileno())
   assert sha(Path(name))==sha(src);os.chmod(name,0o755);os.replace(name,dst)
 if device=='brick':
  src=Path(__file__).parent/'artwork-payload/menu.png'
  for dst in app.glob('menu-cover*.png'):
   safe(dst);shutil.copy2(dst,backup/('previous-'+dst.name))
   fd,temp=tempfile.mkstemp(prefix='.trackwerk-art-',dir=dst.parent)
   with os.fdopen(fd,'wb') as f:f.write(src.read_bytes());f.flush();os.fsync(f.fileno())
   assert sha(Path(temp))==sha(src);os.chmod(temp,0o644);os.replace(temp,dst)
 meta=backup/'menu-files';meta.mkdir()
 for i,(p,data) in enumerate(metadata.items()):
  safe(p);q=meta/str(i);shutil.copy2(p,q);assert sha(p)==sha(q);fd,name=tempfile.mkstemp(prefix='.trackwerk-',dir=p.parent)
  with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
  os.chmod(name,p.stat().st_mode&0o777);os.replace(name,p)
 for old,new in renames:old.rename(new)
 if device!='brick':
  shortcuts=card/'Roms/PORTS/Shortcuts';shortcuts.mkdir(parents=True,exist_ok=True)
  for app,profile in tasks:
   if profile not in ['miyoo-mini','miyoo-plus']:continue
   name='TrackWerkPlus' if profile=='miyoo-plus' else 'TrackWerk'
   matching=[p for p in shortcuts.glob('*.port') if ('GameDir="'+name+'"') in p.read_text()]
   if not matching:
    shortcut=shortcuts/(name+'.port')
    if shortcut.exists():raise RuntimeError('Unrelated shortcut retained')
    text='#!/bin/sh\n# TrackWerk 1.1 / Onion Ports Collection\nGameName="TrackWerk 1.1"\nGameDir="'+name+'"\nGameExecutable="launch.sh"\nGameDataFile="trackwerk"\nKillAudioserver=0\nPerformanceMode=0\nArguments=""\n/mnt/SDCARD/Emu/PORTS/launch_standalone.sh "$GameName" "$GameDir" "$GameExecutable" "$Arguments" "$GameDataFile" "$KillAudioserver" "$PerformanceMode"\n'
    with shortcut.open('x') as f:f.write(text)
    shortcut.chmod(0o755)
  import fix_miyoo_names
  result['menu']=fix_miyoo_names.fix(card,device,backup/'display-names',True)
  art=backup/'artwork';art.mkdir()
  for app,profile in tasks:
   if profile not in ['miyoo-mini','miyoo-plus']:continue
   name='TrackWerkPlus' if profile=='miyoo-plus' else 'TrackWerk'
   label='TrackWerk 1.1 Offline' if device=='miyoo-plus' and profile=='miyoo-mini' else 'TrackWerk 1.1'
   pairs=[(shortcuts.parent/'Imgs'/(label+'.png'),'boxart.png')]
   if (card/'App'/name/'config.json').is_file():pairs.append((card/'App'/name/'icon.png','icon.png'))
   for dst,filename in pairs:
    safe(dst);dst.parent.mkdir(parents=True,exist_ok=True)
    if dst.exists():shutil.copy2(dst,art/(name+'-'+filename))
    src=Path(__file__).parent/'artwork-payload'/filename
    fd,temp=tempfile.mkstemp(prefix='.trackwerk-art-',dir=dst.parent)
    with os.fdopen(fd,'wb') as f:f.write(src.read_bytes());f.flush();os.fsync(f.fileno())
    assert sha(Path(temp))==sha(src);os.chmod(temp,0o644);os.replace(temp,dst)

 result.update(status='prepared',backup=str(backup),completed_steps=completed);(backup/'AFTER.json').write_text(json.dumps(result,indent=2));return result

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--card',type=Path,required=True);p.add_argument('--device',choices=['brick','miyoo-mini','miyoo-plus'],required=True);p.add_argument('--target',type=Path);p.add_argument('--backup',type=Path,required=True);p.add_argument('--apply',action='store_true');a=p.parse_args()
 before=plistlib.loads(subprocess.check_output(['/usr/sbin/diskutil','info','-plist',str(a.card)]));assert before['MountPoint']==str(a.card) and before['WritableVolume'] and before['FilesystemType'] in ['msdos','exfat']
 def guard():
  now=plistlib.loads(subprocess.check_output(['/usr/sbin/diskutil','info','-plist',str(a.card)]));assert now['VolumeUUID']==before['VolumeUUID'] and now['MountPoint']==before['MountPoint']
 print(json.dumps(prepare(a.card,a.device,a.backup,a.apply,a.target,guard),indent=2))
if __name__=='__main__':main()
