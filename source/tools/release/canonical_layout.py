#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Move an existing TrackWerk installation to its canonical name, with an external rollback copy.
Only an explicitly selected app is migrated. Samples are moved by directory rename, never rewritten.
Without --apply this is a read-only plan. A fresh backup directory outside the installation is required.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,sqlite3,stat,tempfile

TEXT={'.json','.ptp','.pti','.txt','.md','.html','.sh','.port','.xml','.cfg','.conf','.url'}

def sha(p):
 h=hashlib.sha256()
 with p.open('rb') as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()

def checked(p):
 p=Path(os.path.abspath(p))
 for part in [*reversed(p.parents),p]:
  if part.is_symlink():raise RuntimeError('A migration path contains a symbolic link')
 return p

def aliases(name):
 words=re.sub(r'(?<=[a-z])(?=[A-Z])',' ',name).replace('_',' ').replace('-',' ')
 # Older forks sometimes joined the upstream words in one token.
 words=re.sub('tracker',' tracker',words,flags=re.I).split()
 if len(words)>3 and [x.lower() for x in words[-2:]]==['plus','archive']:words=words[:-2]
 values={name,' '.join(words),' '.join(w.capitalize() for w in words),''.join(w.capitalize() for w in words),'-'.join(words),'_'.join(words)}
 return sorted((v for v in values if v and v.lower() not in {'trackwerk','trackwerkplus'}),key=len,reverse=True)

def replacement(text,mapping):
 for a,b in sorted(mapping.items(),key=lambda x:len(x[0]),reverse=True):text=re.sub(re.escape(a),lambda _:b,text,flags=re.I)
 return text

def build_plan(target,profile):
 app=checked(target)
 if not app.is_dir():raise RuntimeError('Existing app folder is missing')
 dest=app.parent/('trackwerk' if profile=='brick' else ('TrackWerkPlus' if profile in {'miyoo-plus','miyoo-app-plus'} else 'TrackWerk'))
 if dest!=app and dest.exists():raise RuntimeError('Canonical destination already exists; nothing was merged or overwritten')
 data_only=profile in {'miyoo-home','miyoo-app','miyoo-app-plus'}
 if data_only and not (app/'config.json').is_file():raise RuntimeError('Selected data/shortcut folder has no configuration')
 if profile=='miyoo-home' and not (app/'Samples').is_dir():raise RuntimeError('Selected data home has no Samples folder')
 binaries=[]
 for p in app.iterdir():
  if p.is_file() and not p.is_symlink():
   with p.open('rb') as f:head=f.read(20)
   if head[:4]==b'\x7fELF' and len(head)>=20 and int.from_bytes(head[18:20],'little')==(183 if profile=='brick' else 40):binaries.append(p)
 if not data_only and len(binaries)!=1:raise RuntimeError('Expected exactly one application ELF directly in the app folder')
 program=None if data_only else binaries[0];binary_name='trackwerk.aarch64' if profile=='brick' else 'trackwerk'
 mapping={str(app):str(dest),app.name:dest.name}
 for a in aliases(app.name):mapping[a]='TrackWerk'
 # Full app directory names take precedence over the common display-name aliases.
 mapping[app.name]=dest.name
 if app==dest:mapping={}
 paths=list(app.rglob('*'));rewrites=[];renames=[]
 for p in paths:
  if p.is_symlink():raise RuntimeError('Linked file in selected installation; resolve it before migration')
  rel=p.relative_to(app)
  # Original user recordings, cache media and factory payloads remain byte-for-byte untouched.
  if any(x in {'Samples','Recordings','Renders','Cache','Soundfonts','fonts','.pte-program-backups'} for x in rel.parts):continue
  if p.is_file() and p.suffix.lower() in TEXT:
   try:text=p.read_text(encoding='utf-8')
   except UnicodeError:continue
   new=replacement(text,mapping)
   # Every current app launcher uses the canonical binary filename.
   if p.suffix=='.sh' and program is not None:new=new.replace('/'+program.name+'"','/'+binary_name+'"').replace('/'+program.name+' ','/'+binary_name+' ')
   if new!=text:rewrites.append((p,new.encode()))
  name=replacement(p.name,mapping)
  if p==program:name=binary_name
  if name!=p.name:renames.append((p,p.with_name(name)))
 # Only the selected Brick port entry is edited; other ports are kept unchanged.
 extras=[]
 if profile=='brick':
  for p in app.parent.iterdir():
   if p.is_file() and p.suffix.lower() in {'.sh','.xml'}:
    try:text=p.read_text()
    except UnicodeError:continue
    if app.name in text or p.name in [a+'.sh' for a in aliases(app.name)]:
     new=replacement(text,mapping).replace('/'+program.name+'"','/'+binary_name+'"')
     if new!=text:extras.append((p,new.encode()))
     if p.suffix=='.sh' and p.name!='TrackWerk.sh':renames.append((p,p.with_name('TrackWerk.sh')))
 for old,new in renames:
  if new!=old and new.exists():raise RuntimeError('A file rename would overwrite an existing file: '+str(new))
 return app,dest,program,mapping,rewrites+extras,renames

def migrate(target,profile,backup,apply=False):
 app,dest,program,mapping,rewrites,renames=build_plan(target,profile)
 result=dict(status='planned',profile=profile,source=str(app),destination=str(dest),program_before_sha256=sha(program) if program else None,rewrite_files=[str(p) for p,_ in rewrites],renames=[[str(a),str(b)] for a,b in renames],data_content_rewritten=any(('data' in p.relative_to(app).parts or profile == 'miyoo-home') for p,_ in rewrites if app in p.parents))
 if not apply:return result
 backup=checked(backup)
 if backup.exists() or backup==app or app in backup.parents or dest in backup.parents:raise RuntimeError('Use a fresh external backup directory')
 # The volume root is not a permitted backup destination for an SD migration.
 if str(app).startswith('/Volumes/') and str(backup).startswith('/Volumes/'):raise RuntimeError('Keep the rollback backup off removable media')
 backup.mkdir(parents=True);saved={};done=[];root_moved=False;device=app.stat().st_dev
 needed={*[p for p,_ in rewrites],*[p for p,_ in renames if p.is_file()]}
 if program:needed.add(program)
 for i,p in enumerate(sorted(needed)):
  q=backup/f'{i:05d}.bin';shutil.copy2(p,q);assert sha(p)==sha(q);saved[str(p)]=dict(file=q.name,sha256=sha(q),mode=stat.S_IMODE(p.stat().st_mode))
 journal=dict(result,saved=saved,mapping=mapping);(backup/'BEFORE.json').write_text(json.dumps(journal,indent=2))
 try:
  for p,data in rewrites:
   if app.stat().st_dev!=device:raise RuntimeError('Selected filesystem changed')
   fd,name=tempfile.mkstemp(prefix='.trackwerk-name-',dir=p.parent)
   with os.fdopen(fd,'wb') as f:f.write(data);f.flush();os.fsync(f.fileno())
   os.chmod(name,p.stat().st_mode&0o777);os.replace(name,p)
  # Children first, then the selected root. Samples themselves are not opened for writing.
  for old,new in sorted(renames,key=lambda x:len(x[0].parts),reverse=True):old.rename(new);done.append((old,new))
  if app!=dest:app.rename(dest);root_moved=True
  result.update(status='migrated',backup=str(backup),canonical_program=str(dest/('trackwerk.aarch64' if profile=='brick' else 'trackwerk')))
  if program:assert sha(Path(result['canonical_program']))==result['program_before_sha256']
  else:result['canonical_program']=None
  (backup/'AFTER.json').write_text(json.dumps(result,indent=2));return result
 except BaseException:
  if root_moved:dest.rename(app)
  for old,new in reversed(done):
   if new.exists() and not old.exists():new.rename(old)
  for name,info in saved.items():
   p=Path(name);p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(backup/info['file'],p);p.chmod(info['mode'])
  raise

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--target',required=True,type=Path);p.add_argument('--profile',choices=['brick','miyoo-mini','miyoo-plus','miyoo-home','miyoo-app','miyoo-app-plus'],required=True);p.add_argument('--backup',type=Path);p.add_argument('--apply',action='store_true');a=p.parse_args()
 if a.apply and not a.backup:p.error('--apply needs --backup outside the card')
 print(json.dumps(migrate(a.target,a.profile,a.backup,a.apply),indent=2))
if __name__=='__main__':main()
