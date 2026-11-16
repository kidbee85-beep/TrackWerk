#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Refresh TrackWerk program/menu labels after a verified program update. Read-only unless --apply."""
import argparse,hashlib,json,os,re,stat,tempfile,uuid
from pathlib import Path
VERSION='1.2.0'
COMMIT='b24ad7abe43f6784cb8ef34dee7661db0d02dade'
HASHES={'brick':'b27cf0bd4ad197aa79285e39a9ca47fb30497014795ba3a33a16c5681a6f69e2','rgb20s':'3fea0f54cb77b76c2e3d4d861577143a221402473c5613845b968f6b58943198','miyoo-mini':'92b36ce2cea76be4d361204a63ed17c841558784a86ad1575211ab6e45af1c07','miyoo-plus':'9827f94a09a95951a8fcc00d3d775afe1e589da1a45aaf48d5f9a143d33e9ce3'}
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def checked(p):
 p=Path(os.path.abspath(p))
 if any(x.is_symlink() for x in [p,*p.parents]):raise ValueError('Linked path refused')
 return p
def plan(card,device):
 card=checked(card)
 if not card.is_dir():raise ValueError('Select the mounted card root')
 changes={};programs=[]
 if device in ['brick','rgb20s']:
  candidates=['roms/ports/trackwerk'] if device=='brick' else ['ports/trackwerk-amberelec']
  for rel in candidates:
   a=checked(card/rel);p=checked(a/'trackwerk.aarch64')
   if not p.is_file() or sha(p)!=HASHES[device]:raise ValueError('Install the matching 1.2.0 program first')
   programs.append(p);changes[a/'VERSION.txt']=f'TrackWerk {VERSION}\nPlatform: {device}\nApplication source: {COMMIT}\nProgram SHA-256: {HASHES[device]}\n'.encode()
   changes[a/'SOURCE.txt']=f'Corresponding source: TrackWerk-1.2.0-Source.zip\nApplication code: {COMMIT}\n'.encode()
   launcher=a.parent/'TrackWerk.sh'
   if launcher.is_file():
    old=launcher.read_text();new=re.sub(r'TrackWerk (?:1\.1(?:\.0)?(?:-dev\.2)?|1\.2\.0)(?=[-: /])','TrackWerk '+VERSION,old)
    if old!=new:changes[launcher]=new.encode()
   menu=a.parent/'gamelist.xml'
   if menu.is_file():
    text=menu.read_text();pat=r'(<game>\s*<path>\./TrackWerk\.sh</path>)(.*?)(</game>)'
    def sub(m):return m[1]+re.sub(r'<name>.*?</name>','<name>TrackWerk '+VERSION+'</name>',m[2],count=1,flags=re.S)+m[3]
    new=re.sub(pat,sub,text,flags=re.S)
    if new!=text:changes[menu]=new.encode()
 else:
  variants=[('miyoo-mini',['TrackWerk','PocketTrackerExtreme'])]
  if device=='miyoo-plus':variants.append(('miyoo-plus',['TrackWerkPlus','PocketTrackerExtremePlusArchive']))
  for profile,folders in variants:
   existing=[name for name in folders if (card/'Roms/PORTS/Games'/name).is_dir()]
   if len(existing)>1:raise ValueError('Ambiguous duplicate installation; select one layout before updating')
   if not existing:
    if profile==device:raise ValueError('Matching installation not found')
    continue
   name=existing[0];a=checked(card/'Roms/PORTS/Games'/name);p=next((a/n for n in ['trackwerk','pockettracker'] if (a/n).is_file()),None)
   if p is None or sha(checked(p))!=HASHES[profile]:raise ValueError('Install the matching 1.2.0 program first')
   programs.append(p);changes[a/'VERSION.txt']=f'TrackWerk {VERSION}\nPlatform: {profile}\nApplication source: {COMMIT}\nProgram SHA-256: {HASHES[profile]}\n'.encode()
   changes[a/'SOURCE.txt']=f'Corresponding source: TrackWerk-1.2.0-Source.zip\nApplication code: {COMMIT}\n'.encode()
   config=checked(card/'App'/name/'config.json')
   if config.is_file():
    j=json.loads(config.read_text());j['label']='TrackWerk '+VERSION+(' Offline' if profile=='miyoo-mini' else '');j['description']='Kid Bunka / TrackWerk '+VERSION+' / '+profile;changes[config]=(json.dumps(j,indent=2)+'\n').encode()
 for p in changes:checked(p)
 return card,programs,changes
def apply(card,programs,changes):
 # Metadata only; no Samples, Projects, Presets or preference file is an allowed target.
 before={p:p.read_bytes() if p.exists() else None for p in changes};assert all(p.name in ['VERSION.txt','SOURCE.txt','TrackWerk.sh','gamelist.xml','config.json'] for p in changes)
 assert all(not any(x in ['Samples','Projects','Songs','Presets'] for x in p.relative_to(card).parts) for p in changes)
 backup=checked(card/'.TrackWerk-Metadata-Backups'/('1.2.0-'+uuid.uuid4().hex));backup.mkdir(parents=True)
 for p,b in before.items():
  if b is not None:
   dest=backup/p.relative_to(card);dest.parent.mkdir(parents=True,exist_ok=True);dest.write_bytes(b)
 written=[]
 try:
  for p,b in changes.items():
   fd,n=tempfile.mkstemp(prefix='.trackwerk-meta-',dir=p.parent);temp=Path(n)
   try:
    with os.fdopen(fd,'wb') as f:f.write(b);f.flush();os.fsync(f.fileno())
    if p.exists():temp.chmod(stat.S_IMODE(p.stat().st_mode))
    os.replace(temp,p);written.append(p);assert p.read_bytes()==b
   finally:temp.unlink(missing_ok=True)
 except BaseException:
  for p in reversed(written):
   b=before[p]
   if b is None:p.unlink()
   else:p.write_bytes(b)
  raise
 return str(backup.relative_to(card))
def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--card',required=True,type=Path);parser.add_argument('--device',required=True,choices=HASHES);parser.add_argument('--apply',action='store_true');args=parser.parse_args()
 card,programs,changes=plan(args.card,args.device);result=dict(version=VERSION,programs=[str(p.relative_to(card)) for p in programs],metadata=[str(p.relative_to(card)) for p in changes],applied=args.apply)
 if args.apply:result['metadataBackup']=apply(card,programs,changes)
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
