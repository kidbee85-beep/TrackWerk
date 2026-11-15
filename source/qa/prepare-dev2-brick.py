from pathlib import Path
from struct import pack
from zlib import compress, crc32
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile

source = Path(__file__).resolve().parent.parent
root = source.parent.parent
qa = source / 'qa'
out = root / 'outputs/TrackWerk-1.1.0-dev.2-Brick'
out.mkdir(exist_ok=True)
program = 'trackwerk.aarch64'
version = '1.1.0-dev.2'
binary = (source / 'build-brick/pockettracker-sdl').read_bytes()
checksum = hashlib.sha256(binary).hexdigest()
vm = json.loads((qa / 'brick-dev2/result.json').read_text())
assert vm['exit'] == 0 and vm['sha256'] == checksum and all(vm['checks'].values())

instructions = """TrackWerk 1.1.0-dev.2 · Testupdate für Trimui Brick mit KNULLI

Die ausgeschaltete Brick-Karte am Mac einstecken und das ZIP entpacken.
Im entpackten Ordner den folgenden Befehl ausführen. SHARE bei Bedarf
durch den Namen der eingebundenen Karte ersetzen.

python3 update_brick.py --target "/Volumes/SHARE/roms/ports/trackwerk"

Das Skript sichert die bisherige Programmdatei und ersetzt diese.
Samples, Projekte, Presets und Einstellungen bleiben auf der Karte.
Danach die Karte im Finder sicher auswerfen und TrackWerk über Ports starten.

Neue Layouts
Settings → Appearance → Design collection → A.
Links/Rechts wählt die Familie, Hoch/Runter den Look. A übernimmt ihn.
DOS / COMMAND, UNSCII / BLOCK PARTY, ASCII / INK ON PAPER und
TERM / GREEN PHOSPHOR zeigen vier Positionen für Spuren und Oszilloskop.
Im Sample-Editor liegen die Schnittmarken bei einigen Looks in einer
eigenen Spalte. Im Studio wechseln Kopfzeile und Punch-Monitor ihre Plätze.
Die Schrift lässt sich unter Appearance → Typeface separat wählen.

Zurück zur vorherigen Version
Im Update-Protokoll steht previous_sha256. Dessen Wert einsetzen.

python3 update_brick.py --target "/Volumes/SHARE/roms/ports/trackwerk" --rollback SHA256

Die Screenshots zeigen die echte Oberfläche mit Testdaten.
Start und SDL-Testaufnahme sind in ARM64-Emulation geprüft.
"""
manifest = dict(platform='trimui-brick',program=program,version=version,bytes=len(binary),sha256=checksum)
previous = root / 'outputs/TrackWerk-1.1.0-dev.1-Brick/TrackWerk-1.1.0-dev.1-Brick-Update.zip'
package = qa / f'TrackWerk-{version}-Brick-Update.zip'
with zipfile.ZipFile(package,'w') as archive:
    def add(name, data, mode=0o644):
        item = zipfile.ZipInfo(name)
        item.create_system = 3
        item.external_attr = (0o100000 | mode) << 16
        archive.writestr(item,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=8)
    add(program,binary,0o755)
    add('update_brick.py',(source/'tools/release/update_brick.py').read_bytes(),0o755)
    add('update-manifest.json',json.dumps(manifest,indent=2)+'\n')
    add('START-HIER.txt',instructions)
    add('LICENSE',(source/'LICENSE').read_bytes())
    add('CREDITS.md',(source/'CREDITS.md').read_bytes())
    # The binary statically links third-party code; its notices travel with it, as in the
    # PortMaster/Linux/Miyoo packages (shell/build-*.sh). THIRD-PARTY-NOTICES.md cites these names.
    for name, rel in (('THIRD-PARTY-NOTICES.md','docs/licenses/THIRD-PARTY-NOTICES.md'),
                      ('libogg-COPYING','native/vendor/ogg/COPYING'),
                      ('libopus-COPYING','native/vendor/opus/COPYING'),
                      ('libopus-LICENSE_PLEASE_READ.txt','native/vendor/opus/LICENSE_PLEASE_READ.txt'),
                      ('OFL-1.1-LiberationSans.txt','docs/licenses/OFL-1.1-LiberationSans.txt'),
                      ('OFL-1.1-QuinqueFive.txt','docs/licenses/OFL-1.1-QuinqueFive.txt'),
                      ('Mutable-Instruments-MIT.txt','docs/licenses/Mutable-Instruments-MIT.txt'),
                      ('Mutable-Instruments-PROVENANCE.txt','docs/licenses/Mutable-Instruments-PROVENANCE.txt'),
                      ('Mutable-stmlib-LICENSE','native/vendor/mutable/stmlib/LICENSE'),
                      ('DaisySP-GPL-CONVERSION.txt','docs/licenses/DaisySP-GPL-CONVERSION.txt')):
        data=(source/rel).read_bytes()
        assert len(data)>100, rel
        add('licenses/'+name,data)

with tempfile.TemporaryDirectory(dir=qa,prefix='dev2-update-test-') as scratch:
    tmp = Path(scratch)
    unpacked = tmp / 'package'
    target = tmp / 'roms/ports/trackwerk'
    target.mkdir(parents=True)
    with zipfile.ZipFile(package) as archive:
        assert archive.testzip() is None and (archive.getinfo(program).external_attr >> 16) & 0o111
        archive.extractall(unpacked)
    with zipfile.ZipFile(previous) as archive:
        old = archive.read(program)
    oldhash = hashlib.sha256(old).hexdigest()
    installed = target / program
    installed.write_bytes(old)
    installed.chmod(0o755)
    files = {
        'data/Samples/Break.wav': b'sample',
        'data/Projects/Beat.ptp': b'project',
        'data/Presets/User/Lead.pti': b'preset',
        'data/Themes/Local.ptt': b'theme',
        'data/settings.json': b'{"abxy":2,"fontStyle":15}',
        'factory/Presets/keep.txt': b'factory',
        'control-map.json': b'controls',
    }
    for rel, data in files.items():
        path = target / rel
        path.parent.mkdir(parents=True,exist_ok=True)
        path.write_bytes(data)
    def run(*args):
        res = subprocess.run([sys.executable,str(unpacked/'update_brick.py'),'--target',str(target),*args],
                             capture_output=True, text=True, check=True)
        return json.JSONDecoder().raw_decode(res.stdout)[0]
    result = run()
    assert result['status'] == 'updated' and result['previous_sha256'] == oldhash
    assert installed.read_bytes() == binary and not result['data_modified']
    assert run()['status'] == 'already_current'
    assert run('--rollback',oldhash)['status'] == 'updated' and installed.read_bytes() == old
    assert all((target/rel).read_bytes() == data for rel,data in files.items())

def ppm_rows(name):
    with (qa/'dev2-ptscene-presets-check'/f'{name}.ppm').open('rb') as stream:
        assert stream.readline()==b'P6\n' and stream.readline()==b'640 480\n' and stream.readline()==b'255\n'
        data=stream.read()
    assert len(data)==640*480*3
    return [data[y*1920:(y+1)*1920] for y in range(480)]

def tile(name,names,columns):
    tiles=[ppm_rows(n) for n in names]
    rows=[]
    for start in range(0,len(tiles),columns):
        rows.extend(b''.join(t[y] for t in tiles[start:start+columns]) for y in range(480))
    def chunk(tag, data):
        return pack('>I',len(data))+tag+data+pack('>I',crc32(tag+data)&0xffffffff)
    header=pack('>2I5B',columns*640,len(rows),8,2,0,0,0)
    image=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',header)+chunk(b'IDAT',compress(b''.join(b'\0'+row for row in rows),7))
    (out/(name+'.png')).write_bytes(image+chunk(b'IEND',b''))

tile('Vier-Phrase-Layouts',['phrase-8','phrase-0','phrase-5','phrase-12'],2)
tile('Sample-Editor',['sample-5','sample-0'],2)
tile('Studio-Punch',['punch-8','punch-12'],2)
tile('Chain-Transpose',['chain-20'],1)
tile('Scene-Browser',['picker-12'],1)
shutil.copyfile(package,out/package.name)
(out/'START-HIER.txt').write_text(instructions)
print(f'PASS {out/package.name}')
print(f'SHA-256 {checksum}')
print('Verified update from Dev.1, repeated update and rollback. All seven file fixtures preserved.')
