from pathlib import Path
import zipfile,json,hashlib,re,sys,importlib.util,shutil,uuid,os
W=Path(__file__).resolve().parent;OUT=W/'packages';B=Path('/private/tmp/trackwerk-release-1.2.0-20261116/builds');Q=Path('/private/tmp/trackwerk-release-1.2.0-20261116/package-fixtures');Q.mkdir(exist_ok=True)
H=lambda b:hashlib.sha256(b).hexdigest();results=[]
for p in sorted(OUT.glob('*.zip')):
 with zipfile.ZipFile(p) as z:
  assert z.testzip() is None;files={n:z.read(n) for n in z.namelist() if not n.endswith('/')}
  for n,b in files.items():
   assert not Path(n).is_absolute() and '..' not in Path(n).parts
   if n.endswith('.py'):compile(b,n,'exec')
   if n.endswith('SHA256SUMS.txt'):
    prefix=str(Path(n).parent);prefix='' if prefix=='.' else prefix+'/'
    for line in b.decode().splitlines():
     h,rel=line.split('  ',1);key=prefix+rel;assert key in files and H(files[key])==h,(p.name,n,key)
   if Path(n).name=='PROGRAM-MANIFEST.json':
    j=json.loads(b)
    for row in j['files']:assert H(files['Card-Payload/'+row['path']])==row['sha256'],(p.name,row['path'])
   if Path(n).name in ['UPDATE-MANIFEST.json','update-manifest.json']:
    j=json.loads(b);assert j['version']=='1.2.0'
    for item in j.get('programs',{}).values():assert H(files[item['file']])==item['sha256'] and len(files[item['file']])==item['bytes']
    if 'program' in j:assert H(files[j['program']])==j['sha256']
   if b.startswith(b'\x7fELF') and Path(n).name in ['trackwerk','trackwerk.aarch64','trackwerk-offline.armv7','trackwerk-plus-online.armv7']:
    assert b'1.2.0' in b;assert H(b) in [H((B/profile/'pockettracker').read_bytes()) for profile in ['brick','amberelec','miyoo-mini','miyoo-plus']]
  results.append(dict(package=p.name,archivePassed=True,files=len(files),sha256=H(p.read_bytes())))
# Exercise canonical and existing legacy Miyoo updates with exact shipped programs.
update=OUT/'TrackWerk-1.2.0-Miyoo-Program-Update.zip';bundle=Q/'bundle';bundle.mkdir(exist_ok=True)
with zipfile.ZipFile(update) as z:z.extractall(bundle)
spec=importlib.util.spec_from_file_location('updater12',bundle/'update_miyoo.py');mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
spec=importlib.util.spec_from_file_location('metadata12',bundle/'release_metadata.py');meta=importlib.util.module_from_spec(spec);spec.loader.exec_module(meta)
base=Path('/home/YOUR_USER/TrackWerk FW/Release-1.1-Abnahme/Packages')
with zipfile.ZipFile(base/'TrackWerk-1.1.0-Miyoo-Program-Update.zip') as z:old={False:z.read('trackwerk-offline.armv7'),True:z.read('trackwerk-plus-online.armv7')}
fixture_results=[]
for legacy in [False,True]:
 for profile in ['miyoo-mini','miyoo-plus']:
  card=Q/(profile+('-legacy' if legacy else '-canonical'));card.mkdir(exist_ok=True);onion=card/'.tmp_update/onionVersion';onion.mkdir(parents=True,exist_ok=True);(onion/'version.txt').write_text('v4.3.1-1');data=card/('PocketTrackerExtreme' if legacy else 'TrackWerk');data.mkdir(exist_ok=True)
  for rel in ['Samples/own.wav','Projects/own.ptp','Presets/my-preset.pti','config.json','settings.json']:
   f=data/rel;f.parent.mkdir(exist_ok=True,parents=True);f.write_text('my own '+rel)
  before={str(f.relative_to(data)):H(f.read_bytes()) for f in data.rglob('*') if f.is_file()}
  names=['PocketTrackerExtreme' if legacy else 'TrackWerk']+(['PocketTrackerExtremePlusArchive' if legacy else 'TrackWerkPlus'] if profile=='miyoo-plus' else [])
  for i,name in enumerate(names):
   app=card/'Roms/PORTS/Games'/name;app.mkdir(parents=True,exist_ok=True);(app/('pockettracker' if legacy else 'trackwerk')).write_bytes(old[bool(i)]);cfg=card/'App'/name/'config.json';cfg.parent.mkdir(parents=True,exist_ok=True);cfg.write_text(json.dumps(dict(label='Old label',launch='keep-my-launch.sh',icon='keep-my-icon.png')))
  identity=dict(mount=str(card),volume_uuid=str(uuid.uuid4()),filesystem='msdos',st_dev=card.stat().st_dev,onion_version='v4.3.1-1',externally_verified=True)
  guard=lambda c,e: c==card and e==identity
  r=mod.run_bundle(card,bundle,profile,identity,guard,apply=False);assert r['status'] in ['planned','already_current'],r
  r=mod.run_bundle(card,bundle,profile,identity,guard,apply=True);assert r['status'] in ['updated','already_current'],r
  c,programs,changes=meta.plan(card,profile);meta.apply(c,programs,changes)
  after={str(f.relative_to(data)):H(f.read_bytes()) for f in data.rglob('*') if f.is_file()};assert before==after
  for name in names:
   j=json.loads((card/'App'/name/'config.json').read_text());assert '1.2.0' in j['label'] and j['launch']=='keep-my-launch.sh' and j['icon']=='keep-my-icon.png'
  assert not (card/('TrackWerk' if legacy else 'PocketTrackerExtreme')).exists()
  fixture_results.append(dict(profile=profile,legacyLayout=legacy,programAndMenuUpdate=True,userDataUnchanged=True,layoutUnchanged=True))
(W/'PACKAGE-QA.json').write_text(json.dumps(dict(packages=results,generatedCardFixtures=fixture_results,physicalDevicesSimulated=False),indent=2));print('PACKAGES_AND_4_LAYOUT_FIXTURES_PASS')
