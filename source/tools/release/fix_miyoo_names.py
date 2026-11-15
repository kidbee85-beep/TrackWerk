#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Repair TrackWerk's visible Miyoo names without touching music or programs."""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, sqlite3, subprocess, tempfile, uuid
import xml.etree.ElementTree as ET

def sha(b): return hashlib.sha256(b).hexdigest()
def safe(p):
    p=Path(p)
    if any(q.is_symlink() for q in [p,*p.parents]): raise RuntimeError('Linked path refused: '+str(p))
    return p
def rename_ref(value, mappings):
    for old,(new,label) in mappings.items():
        if value.rsplit('/',1)[-1] in [old+'.port',new+'.port']:
            return value.rsplit('/',1)[0]+'/'+new+'.port' if '/' in value else new+'.port', label
    return value,None
def change_history(v,mappings):
    changed=False
    if isinstance(v,dict):
        target=None
        for k,x in list(v.items()):
            if isinstance(x,str):
                new,label=rename_ref(x,mappings)
                if label is not None: target=label
                if new!=x: v[k]=new;changed=True
        if target:
            for k in ['label','name','disp']:
                if k in v and isinstance(v[k],str) and v[k]!=target: v[k]=target;changed=True
        for x in v.values(): changed=change_history(x,mappings) or changed
    elif isinstance(v,list):
        for x in v: changed=change_history(x,mappings) or changed
    return changed
def fix(card,device,backup,apply=False):
    card=safe(Path(card).absolute())
    if device not in ['miyoo-mini','miyoo-plus']:raise RuntimeError('Unknown device')
    if not (card/'PocketTrackerExtreme').is_dir():raise RuntimeError('Missing existing data directory')
    apps=[]
    if device=='miyoo-mini' or (card/'Roms/PORTS/Games/PocketTrackerExtreme/pockettracker').is_file():
        apps.append(('PocketTrackerExtreme','TrackWerk 1.1' if device=='miyoo-mini' else 'TrackWerk 1.1 Offline'))
    if device=='miyoo-plus':apps.insert(0,('PocketTrackerExtremePlusArchive','TrackWerk 1.1'))
    maps={app:(label,label) for app,label in apps}
    writes={};removes={};original={};modes={};protected={}
    def remember(p):
        p=safe(p)
        if p not in original:original[p]=p.read_bytes() if p.exists() else None
        return original[p]
    def put(p,b,mode=0o644):
        old=remember(p)
        if old!=b:writes[p]=b;modes[p]=mode
    for app,label in apps:
        appdir=card/'App'/app;cfg=appdir/'config.json'
        if cfg.exists():
            obj=json.loads(remember(cfg));obj['label']=label
            put(cfg,(json.dumps(obj,indent=2)+'\n').encode())
        source=card/'Roms/PORTS/Shortcuts'/(app+'.port')
        dest=source.with_name(label+'.port')
        data=remember(source)
        if data is None:data=remember(dest)
        if data is None:raise RuntimeError('Missing TrackWerk shortcut '+app)
        text=data.decode()
        match=re.search(r'^GameDir="([^"]+)"$',text,re.M)
        if not match or match.group(1)!=app:raise RuntimeError('Unexpected shortcut game directory')
        updated,n=re.subn(r'^GameName="[^"]*"$','GameName="'+label+'"',text,flags=re.M)
        if n!=1:raise RuntimeError('Unexpected GameName')
        olddest=remember(dest)
        if olddest is not None and olddest!=updated.encode():
            if re.search(r'^GameDir="'+re.escape(app)+'"$',olddest.decode(),re.M) is None:
                raise RuntimeError('Unrelated shortcut kept '+str(dest))
        put(dest,updated.encode(),0o755)
        if source!=dest and remember(source) is not None:removes[source]=remember(source)
        image=card/'Roms/PORTS/Imgs'/(app+'.png');newimage=image.with_name(label+'.png')
        if image.is_file() and not newimage.exists():put(newimage,remember(image))
        p=card/'Roms/PORTS/Games'/app/'pockettracker'
        protected[str(p.relative_to(card))]=sha(p.read_bytes())
        for rel in ['launch.sh']:
            q=p.parent/rel
            protected[str(q.relative_to(card))]=sha(q.read_bytes())
    for name in ['config.json','settings.json','autosave.ptp','.factory-presets-pack.id']:
        p=card/'PocketTrackerExtreme'/name
        if p.is_file():protected[str(p.relative_to(card))]=sha(p.read_bytes())
    dbreports=[]
    for cache in (card/'Roms/PORTS').rglob('*_cache6.db'):
        if cache.name.startswith('.'):continue
        if any((Path(str(cache)+s)).exists() for s in ['-wal','-shm','-journal']):
            raise RuntimeError('Pending cache journal retained '+str(cache))
        data=remember(cache)
        with tempfile.TemporaryDirectory(prefix='trackwerk-menu-') as temp:
            p=Path(temp)/'cache.db';p.write_bytes(data)
            db=sqlite3.connect(p)
            changes=[];others={}
            for (table,) in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'").fetchall():
                qt='"'+table.replace('"','""')+'"'
                cols=[r[1] for r in db.execute('PRAGMA table_info('+qt+')')]
                if not {'id','path','disp'}<=set(cols):continue
                rows=db.execute('SELECT * FROM '+qt).fetchall();kept=[]
                for row in rows:
                    item=dict(zip(cols,row));new,label=rename_ref(item['path'],maps)
                    if label is None:
                        kept.append(row);continue
                    values={'path':new,'disp':label}
                    if 'imgpath' in cols:
                        oldstem=Path(item['path']).stem
                        value=item['imgpath']
                        if Path(value).stem in maps:values['imgpath']=value[:-len(Path(value).name)]+label+'.png'
                    for k in ['pinyin','cpinyin']:
                        if k in cols:values[k]=label
                    if any(item[k]!=v for k,v in values.items()):
                        keys=list(values)
                        sql='UPDATE '+qt+' SET '+','.join('"'+k+'"=?' for k in keys)+' WHERE id=?'
                        db.execute(sql,[values[k] for k in keys]+[item['id']]);changes.append({'table':table,'id':item['id'],'label':label})
                others[table]=(cols,kept)
            db.commit()
            assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
            for table,(cols,kept) in others.items():
                qt='"'+table.replace('"','""')+'"'
                now=db.execute('SELECT * FROM '+qt).fetchall()
                nowkept=[r for r in now if rename_ref(dict(zip(cols,r))['path'],maps)[1] is None and Path(dict(zip(cols,r))['path']).stem not in {v[0] for v in maps.values()}]
                assert nowkept==kept,'Unrelated cache entries changed'
            db.close()
            if changes:put(cache,p.read_bytes());dbreports.extend(changes)
    for p in (card/'Roms').glob('*.json'):
        if p.name.startswith('.'):continue
        if not any(s in p.name.lower() for s in ['recent','fav']):continue
        source=remember(p);s=source.decode();dec=json.JSONDecoder();pos=0;items=[]
        while pos<len(s):
            while pos<len(s) and s[pos].isspace():pos+=1
            if pos>=len(s):break
            obj,pos=dec.raw_decode(s,pos);items.append(obj)
        changed=False
        for obj in items:changed=change_history(obj,maps) or changed
        if changed:put(p,('\n'.join(json.dumps(x,ensure_ascii=False,separators=(',',':')) for x in items)+'\n').encode())
    for p in [card/'Roms/PORTS/miyoogamelist.xml',card/'Roms/PORTS/Shortcuts/miyoogamelist.xml']:
        if not p.exists():continue
        tree=ET.fromstring(remember(p));changed=False
        for g in tree.findall('game'):
            path=g.find('path')
            if path is None:continue
            new,label=rename_ref(path.text or '',maps)
            if label:
                path.text=new
                name=g.find('name')
                if name is None:name=ET.SubElement(g,'name')
                name.text=label;changed=True
        if changed:put(p,ET.tostring(tree,encoding='utf-8',xml_declaration=True)+b'\n')
    result={'device':device,'labels':{app:label for app,label in apps},
            'writes':[str(p.relative_to(card)) for p in writes],
            'old_shortcuts_removed':[str(p.relative_to(card)) for p in removes],
            'cache_rows':dbreports,'protected_before':protected,'applied':False}
    if not apply:return result
    if writes or removes:
        backup=safe(Path(backup).absolute());backup.mkdir(parents=True,exist_ok=False)
        for p,b in original.items():
            if b is None or not (p in writes or p in removes):continue
            dst=backup/p.relative_to(card);dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(b)
            assert sha(dst.read_bytes())==sha(b)
        (backup/'BEFORE.json').write_text(json.dumps(result,indent=2))
        for p,b in writes.items():
            assert (p.read_bytes() if p.exists() else None)==original[p],str(p)+' changed'
            fd,name=tempfile.mkstemp(prefix='.trackwerk-name-',dir=p.parent)
            try:
                with os.fdopen(fd,'wb') as f:f.write(b);f.flush();os.fsync(f.fileno())
                os.chmod(name,modes[p]);os.replace(name,p)
            finally:
                if os.path.exists(name):os.unlink(name)
            assert p.read_bytes()==b
        for p,b in removes.items():
            assert p.read_bytes()==b;p.unlink()
        result['backup']=str(backup)
    for rel,h in protected.items():assert sha((card/rel).read_bytes())==h
    for app,label in apps:
        cfg=card/'App'/app/'config.json'
        if cfg.exists():assert json.loads(cfg.read_text())['label']==label
        p=card/'Roms/PORTS/Shortcuts'/(label+'.port')
        r=subprocess.run(['/bin/sh','-n',str(p)],capture_output=True,text=True)
        assert r.returncode==0,r.stderr
        assert not (p.parent/(app+'.port')).exists()
    result['applied']=True;result['protected_unchanged']=True
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--card',required=True,type=Path)
    parser.add_argument('--device',required=True,choices=['miyoo-mini','miyoo-plus'])
    parser.add_argument('--backup',required=True,type=Path)
    parser.add_argument('--apply',action='store_true')
    a=parser.parse_args()
    print(json.dumps(fix(a.card,a.device,a.backup,a.apply),indent=2))
