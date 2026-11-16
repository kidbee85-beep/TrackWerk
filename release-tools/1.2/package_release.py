from pathlib import Path
import json,re,hashlib,zipfile,stat,shutil
W=Path(__file__).resolve().parent;BASE=Path('/home/YOUR_USER/TrackWerk FW/Release-1.1-Abnahme/Packages');OUT=W/'packages';OUT.mkdir(exist_ok=True);B=Path('/private/tmp/trackwerk-release-1.2.0-20261116/builds');COMMIT='b24ad7abe43f6784cb8ef34dee7661db0d02dade';OLD='affd28d34a1bac40ffaee27ce60a985dab31eb34'
H=lambda b:hashlib.sha256(b).hexdigest();dump=lambda j:(json.dumps(j,indent=2,ensure_ascii=False)+'\n').encode()
def patch_miyoo(t):
 t=t.replace("ALLOWLIST = {'miyoo-mini': {OFFLINE}, 'miyoo-plus': {OFFLINE, ONLINE}}", "LEGACY_OFFLINE = 'Roms/PORTS/Games/PocketTrackerExtreme/pockettracker'\nLEGACY_ONLINE = 'Roms/PORTS/Games/PocketTrackerExtremePlusArchive/pockettracker'\nALLOWLIST = {'miyoo-mini': {OFFLINE, LEGACY_OFFLINE}, 'miyoo-plus': {OFFLINE, ONLINE, LEGACY_OFFLINE, LEGACY_ONLINE}}")
 t=t.replace("if not checked(card / DATA_HOME).is_dir():", "if not any(checked(card / name).is_dir() for name in (DATA_HOME, 'PocketTrackerExtreme')):")
 t=t.replace("required = ONLINE if profile == 'miyoo-plus' else OFFLINE", "required = {ONLINE, LEGACY_ONLINE} if profile == 'miyoo-plus' else {OFFLINE, LEGACY_OFFLINE}")
 t=t.replace("required not in paths", "not required.intersection(paths)")
 a=t.index('    paths = [OFFLINE] if device');b=t.index('        predecessors = item',a)
 t=t[:a]+'''    choices = [(OFFLINE, LEGACY_OFFLINE)] if device == 'miyoo-mini' else [(ONLINE, LEGACY_ONLINE), (OFFLINE, LEGACY_OFFLINE)]
    paths = []
    for index, candidates in enumerate(choices):
        found = [p for p in candidates if checked(card / p).exists()]
        if len(found) > 1:
            raise UpdateError('Ambiguous duplicate installations; no directories have been moved.')
        if not found:
            if index == 0:
                raise UpdateError('Matching installed program is missing.')
            continue
        paths.append(found[0])
    rows = []
    for target in paths:
        online = target in (ONLINE, LEGACY_ONLINE)
        item = package['programs'][ONLINE if online else OFFLINE]
        filename = 'trackwerk-plus-online.armv7' if online else 'trackwerk-offline.armv7'
'''+t[b:]
 return t
for f in sorted(BASE.iterdir()):
 if f.suffix!='.zip' or 'Source.zip' in f.name:continue
 if 'Factory-Presets' in f.name:
  shutil.copy2(f,OUT/f.name);continue
 with zipfile.ZipFile(f) as z:rr={m.filename:(z.read(m),(m.external_attr>>16)&0o777) for m in z.infolist() if not m.is_dir()}
 brick='Brick' in f.name;amber='AmberELEC' in f.name;miyoo='Miyoo' in f.name
 for n,(data,mode) in list(rr.items()):
  if data.startswith(b'\x7fELF') and Path(n).name in ['trackwerk','trackwerk.aarch64','trackwerk-offline.armv7','trackwerk-plus-online.armv7']:
   profile='brick' if brick else 'amberelec' if amber else 'miyoo-plus' if ('Plus' in f.name or 'TrackWerkPlus/' in n or 'plus-online' in n) else 'miyoo-mini';data=(B/profile/'pockettracker').read_bytes()
  elif n.endswith(('.txt','.json','.py','.sh','.port','.xml','.md')) and '/licenses/' not in n and Path(n).name not in ['LICENSE','CREDITS.md']:
   try:t=data.decode()
   except UnicodeError:pass
   else:
    t=t.replace('1.1.0-dev.2','1.2.0').replace('1.1.0','1.2.0').replace('TrackWerk 1.1','TrackWerk 1.2.0').replace(OLD,COMMIT)
    if Path(n).name=='update_miyoo.py':t=patch_miyoo(t)
    data=t.encode()
  rr[n]=(data,mode)
 # The full matching binaries are the only executable application replacements.
 for n,(data,mode) in list(rr.items()):
  if Path(n).name in ['UPDATE-MANIFEST.json','update-manifest.json']:
   j=json.loads(data);j['version']='1.2.0';j['source_commit']=COMMIT
   if 'programs' in j:
    for target,item in j['programs'].items():
     old=item['sha256'];raw=rr[item['file']][0];item.update(bytes=len(raw),sha256=H(raw));item['previous_sha256s']=list(dict.fromkeys([*item['previous_sha256s'],old]))
   else:raw=rr[j['program']][0];j.update(bytes=len(raw),sha256=H(raw))
   rr[n]=(dump(j),mode)
  elif Path(n).name=='EXPERIMENTAL-STATUS.json':
   rr[n]=(dump(dict(name='TrackWerk',version='1.2.0',sourceCommit=COMMIT,softwareQA='88 host checks and exact ARM guest startup passed',physicalDeviceTested=False,privateSamplesIncluded=False)),mode)
 if 'Program-' in f.name or 'Program-Factory-' in f.name:
  rr['release_metadata.py']=((W/'release_metadata.py').read_bytes(),0o755)
  rr['START-HERE-1.2.0.txt']=(('TrackWerk 1.2.0 program update\n\nKeep your established data folders and back them up first. Do not copy a First Install payload over an existing installation.\n\n'+('Brick: python3 update_brick.py --target /ABSOLUTE/CARD/roms/ports/trackwerk\nThen: python3 release_metadata.py --card /ABSOLUTE/CARD --device brick --apply\n' if brick else 'RGB20S: python3 update_amberelec.py --target /ABSOLUTE/CARD/ports/trackwerk-amberelec\nReview the plan, then repeat with --apply.\nThen: python3 release_metadata.py --card /ABSOLUTE/CARD --device rgb20s --apply\n' if amber else 'Miyoo: python3 update_miyoo.py --card /ABSOLUTE/CARD --device miyoo-plus\nUse miyoo-mini for the normal Mini. Review the plan, then repeat with --apply. Canonical and older PocketTrackerExtreme layouts are supported in place; nothing is renamed.\nThen: python3 release_metadata.py --card /ABSOLUTE/CARD --device miyoo-plus --apply\nUse miyoo-mini for the normal Mini.\n')+'\nThe final metadata step updates menu and VERSION/SOURCE labels only after the installed binary matches 1.2.0. It backs up those labels. It does not change user preferences, music or directory names. Eject safely.\nRollback is a separate operation: restore the corresponding saved program and its saved metadata together. Never label a rollback as the new version.\n\nOnline sources require working network support. Mini is offline; RGB20S needs an external Wi-Fi adapter. Physical iPhone USB audio is not guaranteed.\n').encode(),0o644)
 # Refresh every shipped inventory after changing binaries and labels.
 for n,(data,mode) in list(rr.items()):
  if Path(n).name=='SHA256SUMS.txt' and str(Path(n).parent)!='.':
   prefix=str(Path(n).parent)+'/'
   rr[n]=(''.join(H(v[0])+'  '+k.removeprefix(prefix)+'\n' for k,v in sorted(rr.items()) if k.startswith(prefix) and k!=n).encode(),mode)
 for n,(data,mode) in list(rr.items()):
  if Path(n).name=='PROGRAM-MANIFEST.json':
   j=json.loads(data);j['source_commit']=COMMIT;j['version']='1.2.0';j['files']=[dict(path=k.removeprefix('Card-Payload/'),bytes=len(v[0]),sha256=H(v[0]),mode=v[1]) for k,v in sorted(rr.items()) if k.startswith('Card-Payload/')];rr[n]=(dump(j),mode)
  elif Path(n).name=='MANIFEST.json' and amber:
   j=json.loads(data);j.update(version='1.2.0',source_commit=COMMIT,program_sha256=H((B/'amberelec/pockettracker').read_bytes()));j['files']=[dict(path=k,bytes=len(v[0]),sha256=H(v[0]),executable=bool(v[1]&0o111)) for k,v in sorted(rr.items()) if k!=n and Path(k).name!='SHA256SUMS.txt'];j['checksum_inventories_covered_by_SHA256SUMS']=True;rr[n]=(dump(j),mode)
 for n,(data,mode) in list(rr.items()):
  if Path(n).name=='SHA256SUMS.txt':
   prefix=str(Path(n).parent);prefix='' if prefix=='.' else prefix+'/'
   rr[n]=(''.join(H(v[0])+'  '+k.removeprefix(prefix)+'\n' for k,v in sorted(rr.items()) if k.startswith(prefix) and k!=n).encode(),mode)
 dest=OUT/f.name.replace('1.1.0','1.2.0')
 with zipfile.ZipFile(dest,'w',zipfile.ZIP_DEFLATED,compresslevel=5) as z:
  for n,(raw,mode) in sorted(rr.items()):
   assert not Path(n).is_absolute() and '..' not in Path(n).parts
   i=zipfile.ZipInfo(n,(2026,11,16,0,0,0));i.create_system=3;i.external_attr=(stat.S_IFREG|(mode or 0o644))<<16;i.compress_type=zipfile.ZIP_DEFLATED;z.writestr(i,raw)
 with zipfile.ZipFile(dest) as z:assert z.testzip() is None
 print('PACKAGED',dest.name,dest.stat().st_size,flush=True)
for f in BASE.glob('*.tar.gz'):shutil.copy2(f,OUT/f.name)
for n in ['TrackWerk-1.2.0-Android.apk','TrackWerk-1.2.0-Android-Update.apk']:shutil.copy2(B/'android/TrackWerk-1.2.0-Android.apk',OUT/n)
