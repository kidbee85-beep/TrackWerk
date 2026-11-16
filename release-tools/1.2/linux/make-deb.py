from pathlib import Path
import os,sys,json,hashlib,shutil,tarfile,io,gzip,struct,subprocess
HERE=Path(__file__).parent;SOURCE=Path(os.environ.get('TW_SOURCE_TREE',str(HERE.parents[2]/'source')));arch=sys.argv[1];revision=sys.argv[2] if len(sys.argv)>2 else '1';assert arch in ['amd64','arm64'] and revision.isdigit();version='1.2.0-'+revision
binary=HERE/'binaries'/arch/'trackwerk.bin';b=binary.read_bytes();assert b[:6]==b'\x7fELF\x02\x01' and int.from_bytes(b[18:20],'little')==(62 if arch=='amd64' else 183)
build=json.loads((HERE/('BUILD-'+arch+'.json')).read_text());assert hashlib.sha256(b).hexdigest()==build['sha256']
epoch=int(os.environ.get('SOURCE_DATE_EPOCH','1794858871'));stage=HERE/'stages'/(arch+'-'+revision);assert not stage.exists();stage.mkdir(parents=True)
def put(rel,data,mode=0o644):
 p=stage/rel;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(data.encode() if isinstance(data,str) else data);p.chmod(mode)
def copy(source,relative,mode=0o644):put(relative,Path(source).read_bytes(),mode)
copy(binary,'usr/lib/trackwerk/trackwerk.bin',0o755)
launcher=(HERE/'packaging/trackwerk').read_text().replace('PACKAGE_VERSION="1.2.0~dev20261116+git6eac812-1"','PACKAGE_VERSION="'+version+'"');put('usr/bin/trackwerk',launcher,0o755)
copy(HERE/'packaging/org.kidbunka.TrackWerk.desktop','usr/share/applications/org.kidbunka.TrackWerk.desktop')
icon=(SOURCE/'branding/trackwerk-icon.png').read_bytes();w,h=struct.unpack_from('>II',icon,16);assert w==h and w>0;put('usr/share/icons/hicolor/'+str(w)+'x'+str(h)+'/apps/org.kidbunka.TrackWerk.png',icon)
for src in (SOURCE/'app/src/main/assets').rglob('*'):
 if src.is_file() and not src.is_symlink() and not any(x.startswith('.') for x in src.relative_to(SOURCE/'app/src/main/assets').parts):copy(src,'usr/lib/trackwerk/'+src.relative_to(SOURCE/'app/src/main/assets').as_posix())
for src in (SOURCE/'factory').rglob('*'):
 if src.is_file() and not src.is_symlink() and not any(x.startswith('.') for x in src.relative_to(SOURCE/'factory').parts):copy(src,'usr/share/trackwerk/factory/'+src.relative_to(SOURCE/'factory').as_posix())
fixture=HERE/'qa/Synthetic-Linux-QA.ptp';assert fixture.is_file();copy(fixture,'usr/share/trackwerk/examples/Synthetic-Linux-QA.ptp')
for name in ['LICENSE','CREDITS.md','README.md','STUDIO-NOTES.txt']:copy(SOURCE/name,'usr/share/doc/trackwerk/'+name)
copy(HERE/'packaging/README-LINUX.txt','usr/share/doc/trackwerk/README-LINUX.txt')
for src in (SOURCE/'docs/licenses').rglob('*'):
 if src.is_file():copy(src,'usr/share/doc/trackwerk/licenses/'+src.relative_to(SOURCE/'docs/licenses').as_posix())
for name,rel in [('libogg-COPYING','native/vendor/ogg/COPYING'),('libopus-COPYING','native/vendor/opus/COPYING'),('libopus-LICENSE_PLEASE_READ.txt','native/vendor/opus/LICENSE_PLEASE_READ.txt'),('Mutable-stmlib-LICENSE','native/vendor/mutable/stmlib/LICENSE')]:copy(SOURCE/rel,'usr/share/doc/trackwerk/licenses/'+name)
zigroot=Path('/home/YOUR_USER/Library/Application Support/TrackWerk/Toolchains/zig-aarch64-macos-0.14.1')
for name,rel in [('Zig-LICENSE','LICENSE'),('LLVM-libcxx-LICENSE','lib/libcxx/LICENSE.TXT'),('LLVM-libcxxabi-LICENSE','lib/libcxxabi/LICENSE.TXT'),('LLVM-libunwind-LICENSE','lib/libunwind/LICENSE.TXT')]:copy(zigroot/rel,'usr/share/doc/trackwerk/licenses/'+name)
put('usr/share/doc/trackwerk/copyright','Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\nUpstream-Name: TrackWerk\nSource: https://github.com/kidbee85-beep/TrackWerk\nComment: Independent PocketTracker fork by Kid Bunka. Original author and all\n component copyrights remain in CREDITS.md and licenses/THIRD-PARTY-NOTICES.md.\n Corresponding frozen source and build/packaging recipes accompany this release.\n\nFiles: *\nCopyright: Original PocketTracker contributors, Kid Bunka and component authors; see CREDITS.md\nLicense: GPL-3.0-or-later\n Full text in LICENSE and /usr/share/common-licenses/GPL-3.\n\nFiles: usr/lib/trackwerk/fonts/*\nLicense: OFL-1.1\n Full texts in licenses/OFL-1.1-LiberationSans.txt and licenses/OFL-1.1-QuinqueFive.txt.\n')
put('usr/share/doc/trackwerk/BUILD-IDENTITY.json',json.dumps({k:build[k] for k in ['arch','target','sha256','sourceCommit','sourceTree','PT_HANDHELD','linkSDK']},indent=2)+'\n')
changes=('trackwerk ('+version+') trixie; urgency=low\n\n  * TrackWerk 1.2.0 native desktop integration of source b24ad7.\n  * User data remains outside package payload and survives upgrades/removal.\n\n -- Kid Bunka <noreply@localhost>  Mon, 16 Nov 2026 12:00:00 +0100\n').encode();put('usr/share/doc/trackwerk/changelog.Debian.gz',gzip.compress(changes,mtime=epoch))
files=sorted(x for x in stage.rglob('*') if x.is_file());assert all(x.relative_to(stage).parts[0]=='usr' for x in files);assert not any(x.name.startswith('libSDL') for x in files);assert not any(x.suffix.lower() in ['.wav','.mp3','.flac','.sf2','.apk','.jks'] for x in files)
manifest=[dict(path=x.relative_to(stage).as_posix(),bytes=x.stat().st_size,sha256=hashlib.sha256(x.read_bytes()).hexdigest()) for x in files]
control=('Package: trackwerk\nVersion: '+version+'\nArchitecture: '+arch+'\nMaintainer: Kid Bunka <noreply@localhost>\nInstalled-Size: '+str(sum((x.stat().st_size+1023)//1024 for x in files))+'\nSection: sound\nPriority: optional\nDepends: libc6 (>= 2.31), libsdl2-2.0-0 (>= 2.32.4), python3 (>= 3.9), libcurl4t64 | libcurl4, ca-certificates, ffmpeg\nRecommends: alsa-utils\nHomepage: https://github.com/kidbee85-beep/TrackWerk\nDescription: TrackWerk music tracker, synthesis studio and source sampler\n Native desktop build for Mutable Dreams Linux, derived from PocketTracker.\n Includes factory synthesis and effect presets; no private samples. User data\n stays in the user profile and is never removed by package maintenance.\n')
put('DEBIAN/control',control);put('DEBIAN/md5sums',''.join(hashlib.md5(x.read_bytes()).hexdigest()+'  '+x.relative_to(stage).as_posix()+'\n' for x in files))
def tarbytes(items):
 buf=io.BytesIO()
 with tarfile.open(fileobj=buf,mode='w',format=tarfile.GNU_FORMAT) as t:
  for path,rel in items:
   assert not path.is_symlink();i=tarfile.TarInfo(rel);i.uid=i.gid=0;i.uname=i.gname='root';i.mtime=epoch
   if path.is_dir():i.type=tarfile.DIRTYPE;i.mode=0o755;i.size=0;t.addfile(i)
   else:
    assert path.is_file();data=path.read_bytes();i.size=len(data);i.mode=path.stat().st_mode&0o777;t.addfile(i,io.BytesIO(data))
 return gzip.compress(buf.getvalue(),compresslevel=9,mtime=epoch)
controlbytes=tarbytes([(stage/'DEBIAN/control','./control'),(stage/'DEBIAN/md5sums','./md5sums')]);dirs=sorted({parent for x in files for parent in x.parents if parent!=stage and stage in parent.parents},key=lambda q:(len(q.parts),str(q)));databytes=tarbytes([(stage,'./')]+[(x,'./'+x.relative_to(stage).as_posix()+'/') for x in dirs]+[(x,'./'+x.relative_to(stage).as_posix()) for x in files]);deb=b'!<arch>\n'
for name,data in [('debian-binary',b'2.0\n'),('control.tar.gz',controlbytes),('data.tar.gz',databytes)]:
 hdr=(name.ljust(16)+str(epoch).ljust(12)+'0'.ljust(6)+'0'.ljust(6)+'100644'.ljust(8)+str(len(data)).ljust(10)+'`\n').encode();assert len(hdr)==60;deb+=hdr+data+(b'\n' if len(data)%2 else b'')
dest=HERE/'packages'/('trackwerk_'+version+'_'+arch+'.deb');dest.parent.mkdir(exist_ok=True);assert not dest.exists();dest.write_bytes(deb)
# Read the final data archive again; every payload file must match exactly.
with tarfile.open(fileobj=io.BytesIO(gzip.decompress(databytes)),mode='r:') as tar:
 members={m.name.removeprefix('./'):m for m in tar.getmembers() if m.isfile()};assert len(members)==len(manifest)
 for row in manifest:assert hashlib.sha256(tar.extractfile(members[row['path']]).read()).hexdigest()==row['sha256']
meta=dict(file=dest.name,bytes=len(deb),sha256=hashlib.sha256(deb).hexdigest(),architecture=arch,debianVersion=version,sourceCommit=build['sourceCommit'],sourceTree=build['sourceTree'],payloadFiles=len(manifest),sampleFiles=0,maintainerScripts=[],userDataPathsInPayload=[],runtimeTest='PENDING',files=manifest)
(dest.with_suffix('.deb.json')).write_text(json.dumps(meta,indent=2));print(json.dumps({k:v for k,v in meta.items() if k!='files'}))
