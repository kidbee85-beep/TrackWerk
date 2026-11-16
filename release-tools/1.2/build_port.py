"""TrackWerk 1.2.0 device builds (Claude Code), after Codex' 1.0 pipeline
(work/trackwerk-1.0-public/build_release.py): the same targets, CPU flags, SDL builds and CMake
switches, but from Claude's working copy and with the local Zig 0.14.1 outside iCloud. Nothing is
written into Codex' folders; their SDKs are only read.

usage: python3 build_port.py miyoo-mini | miyoo-plus | brick | amberelec | all-linux

Source tree (TREE), first match: TW_TREE; Claude's working copy ../arbeitskopie/trackwerk-dev2-stufe1;
the kit layout ../quelle/TrackWerk-1.1.0-dev.2 (the Source ZIP unpacked beside bausatz/).
Outputs (one folder per profile, Pakete/, android/) go to TW_OUT, else beside this script.
Source state for BUILD-QA (source_state): git only when TREE itself is the top of a checkout,
otherwise QUELLSTAND.json (commit, tree, file digest) found in TREE or up to two folders above;
the unpacked files are checked against its digest. A parent checkout is never used.
"""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

HERE = Path(__file__).resolve().parent
# Codex' local toolchains outside iCloud (work/toolchains-local-2026-11-14/toolchains.env); a variable
# already set in the environment wins.
TOOLCHAINS_ENV = Path(os.environ.get('TW_TOOLCHAINS_ENV', HERE.parents[3] / 'work/toolchains-local-2026-11-14/toolchains.env'))
if TOOLCHAINS_ENV.is_file():
    for line in TOOLCHAINS_ENV.read_text().splitlines():
        if line.startswith('export ') and '=' in line:
            key, value = line[len('export '):].split('=', 1)
            os.environ.setdefault(key.strip(), value.strip().strip('"'))
VERSION = '1.2.0'


def _source_tree():
    if os.environ.get('TW_TREE'):
        return Path(os.environ['TW_TREE']).resolve()
    work = HERE.parent / 'arbeitskopie/trackwerk-dev2-stufe1'   # Claude's working copy
    if work.is_dir():
        return work
    return (HERE.parent / 'quelle' / f'TrackWerk-{VERSION}').resolve()   # the kit


TREE = _source_tree()
OUT_ROOT = Path(os.environ.get('TW_OUT', HERE)).resolve()
ZIG = Path(os.environ.get('TW_ZIG', '/home/YOUR_USER/Library/Application Support/TrackWerk/Toolchains/zig-aarch64-macos-0.14.1/zig'))
ZIG_CACHE = Path('/home/YOUR_USER/Library/Caches/TrackWerk/zig-0.14.1')
# Overridable while tools move out of iCloud (Documents evicts rarely used files; a read then blocks).
LOCAL_TOOLCHAINS = Path('/home/YOUR_USER/Library/Application Support/TrackWerk/Toolchains')   # outside iCloud
CMAKE = Path(os.environ.get('TW_CMAKE', LOCAL_TOOLCHAINS / 'cmake-4.4.4/bin/cmake'))
CODEX = Path(os.environ.get('TW_MIYOO_SDK_ROOT', '/home/YOUR_USER/Documents/Codex/2026-11-08/k/work/trackwerk-studio-next'))   # holds miyoo-dev13/
TRIMUI_SDK = Path(os.environ.get('TW_TRIMUI_SDK', LOCAL_TOOLCHAINS / 'trimui-sdk/usr'))
PROFILES = {
    # profile: (zig target, cpu, SDL dir, SDL library)
    'brick': ('aarch64-linux-gnu.2.31', 'cortex_a53', TRIMUI_SDK, 'lib/libSDL2-2.0.so.0.2600.1'),
    'amberelec': ('aarch64-linux-gnu.2.31', 'cortex_a35', TRIMUI_SDK, 'lib/libSDL2-2.0.so.0.2600.1'),
    'miyoo-mini': ('arm-linux-gnueabihf.2.28', 'cortex_a7', CODEX / 'miyoo-dev13/offline/sdk', 'lib/libSDL2-2.0.so.0'),
    'miyoo-plus': ('arm-linux-gnueabihf.2.28', 'cortex_a7', CODEX / 'miyoo-dev13/online/sdk', 'lib/libSDL2-2.0.so.0'),
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def git(*args):
    return subprocess.check_output(['git', '-C', str(TREE), *args], text=True).strip()


def tree_digest(root):
    """SHA-256 over every file below `root` in sorted order: relative path, NUL, kind, NUL, content
    (a symlink's target instead). The same for a git archive unpacked anywhere."""
    root = Path(root)
    h = hashlib.sha256()
    files = sorted(p for p in root.rglob('*') if p.is_symlink() or p.is_file())
    for p in files:
        rel = p.relative_to(root).as_posix()
        if p.is_symlink():
            h.update(f'{rel}\0link\0{os.readlink(p)}\0'.encode())
        else:
            h.update(f'{rel}\0file\0'.encode())
            h.update(p.read_bytes())
            h.update(b'\0')
    return h.hexdigest(), len(files)


def is_checkout_top(tree):
    if not (tree / '.git').exists():
        return False
    try:
        top = subprocess.check_output(['git', '-C', str(tree), 'rev-parse', '--show-toplevel'], text=True,
                                      stderr=subprocess.DEVNULL).strip()
    except (subprocess.CalledProcessError, FileNotFoundError):
        return False
    return Path(top).resolve() == tree.resolve()


_STATE = None


def source_state():
    """{'commit', 'tree_clean', 'source'}; asked only when a build or package needs it."""
    global _STATE
    if _STATE is None:
        if not TREE.is_dir():
            raise SystemExit(f'source tree not found: {TREE}\nUnpack the Source ZIP (see PORTIEREN.md) or set TW_TREE.')
        if is_checkout_top(TREE):
            _STATE = {'commit': git('rev-parse', 'HEAD'),
                      'tree_clean': git('status', '--porcelain', '--untracked-files=no') == '', 'source': 'git'}
        else:
            metas = [d / 'QUELLSTAND.json' for d in (TREE, TREE.parent, TREE.parent.parent)]
            meta = next((m for m in metas if m.is_file()), None)
            if meta is None:
                raise SystemExit(f'{TREE} is not a git checkout and no QUELLSTAND.json was found beside it.')
            info = json.loads(meta.read_text())
            digest, count = tree_digest(TREE)
            if digest != info['files_sha256'] or count != info['files']:
                raise SystemExit(f'{TREE} differs from {meta} ({count} files, {digest[:12]}); unpack the Source ZIP afresh.')
            _STATE = {'commit': info['commit'], 'tree_clean': True, 'source': f'QUELLSTAND.json {digest[:12]}'}
    return _STATE


def build(profile):
    target, cpu, sdl_dir, sdl_lib = PROFILES[profile]
    miyoo = profile.startswith('miyoo')
    sdl = sdl_dir / sdl_lib
    assert (sdl_dir / 'include/SDL2/SDL.h').is_file() and sdl.is_file(), sdl_dir
    state = source_state()
    out = OUT_ROOT / profile
    tools = out / 'tools'
    tools.mkdir(parents=True, exist_ok=True)
    cache = ZIG_CACHE / f'port-{profile}'
    for alias, command in [('cc', 'cc'), ('cxx', 'c++'), ('ar', 'ar'), ('ranlib', 'ranlib')]:
        args = [str(ZIG), command] + (['-target', target, f'-mcpu={cpu}'] if alias in ('cc', 'cxx') else [])
        path = tools / alias
        path.write_text('#!/bin/sh\n'
                        f'export ZIG_GLOBAL_CACHE_DIR={shlex.quote(str(ZIG_CACHE / "global"))}\n'
                        f'export ZIG_LOCAL_CACHE_DIR={shlex.quote(str(cache))}\n'
                        f'exec {shlex.join(args)} "$@"\n')
        path.chmod(0o755)
    tc = tools / 'toolchain.cmake'
    tc.write_text(f'set(CMAKE_SYSTEM_NAME Linux)\nset(CMAKE_SYSTEM_PROCESSOR {"arm" if miyoo else "aarch64"})\n' +
                  ''.join(f'set({key} "{tools / alias}")\n' for key, alias in
                          [('CMAKE_C_COMPILER', 'cc'), ('CMAKE_CXX_COMPILER', 'cxx'),
                           ('CMAKE_AR', 'ar'), ('CMAKE_RANLIB', 'ranlib')]) +
                  'set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)\n')
    sdk = tools / 'sdl2'
    sdk.mkdir(exist_ok=True)
    (sdk / 'SDL2Config.cmake').write_text(
        'add_library(SDL2::SDL2 SHARED IMPORTED)\n'
        f'set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_LOCATION "{sdl}" '
        f'INTERFACE_INCLUDE_DIRECTORIES "{sdl_dir / "include/SDL2"}")\nset(SDL2_FOUND TRUE)\n')
    prefix = shlex.join([f'-ffile-prefix-map={TREE}=/src/trackwerk','-ffile-prefix-map=/home/YOUR_USER=/build'])
    env = dict(os.environ, CFLAGS=prefix, CXXFLAGS=prefix)
    b = out / 'cmake'
    log = out / 'build.log'
    commands = [
        [CMAKE, '-S', TREE / 'shell', '-B', b, '-G', 'Unix Makefiles', '-DCMAKE_BUILD_TYPE=Release',
         f'-DCMAKE_TOOLCHAIN_FILE={tc}', f'-DSDL2_DIR={sdk}', '-DCMAKE_SKIP_RPATH=ON', '-DPT_STUDIO_CHECK=ON',
         f'-DPT_HANDHELD={"OFF" if miyoo else "ON"}', f'-DPT_MIYOO_OFFLINE={"ON" if profile == "miyoo-mini" else "OFF"}',
         f'-DPT_MIYOO_PLUS_ARCHIVE={"ON" if profile == "miyoo-plus" else "OFF"}',
         '-DCMAKE_EXE_LINKER_FLAGS=-Wl,--allow-shlib-undefined,--strip-all'],
        [CMAKE, '--build', b, '--target', 'pockettracker-sdl', '-j', '2'],
    ]
    with log.open('w') as stream:
        for args in commands:
            stream.write('COMMAND ' + shlex.join(map(str, args)) + '\n')
            stream.flush()
            subprocess.run(args, check=True, stdout=stream, stderr=subprocess.STDOUT, env=env)
    binary = out / 'pockettracker'
    binary.write_bytes((b / 'pockettracker-sdl').read_bytes())
    binary.chmod(0o755)
    data = binary.read_bytes()
    assert data[:6] == (b'\x7fELF\x01\x01' if miyoo else b'\x7fELF\x02\x01'), profile
    assert int.from_bytes(data[18:20], 'little') == (40 if miyoo else 183), profile
    report = {'profile': profile, 'version': VERSION, 'commit': state['commit'],
              'tree_clean': state['tree_clean'], 'source': state['source'],
              'target': target, 'cpu': cpu, 'bytes': len(data), 'sha256': sha(binary),
              'SDL_link': str(sdl), 'SDL_link_sha256': sha(sdl), 'zig': str(ZIG)}
    (out / 'BUILD-QA.json').write_text(json.dumps(report, indent=2) + '\n')
    print(profile, 'built', report['sha256'], report['bytes'], flush=True)
    return report


if __name__ == '__main__':
    if any(a in ('-h', '--help') for a in sys.argv[1:]):
        print(__doc__)
        sys.exit(0)
    wanted = sys.argv[1:] or ['all-linux']
    if wanted == ['all-linux']:
        wanted = list(PROFILES)
    for profile in wanted:
        if profile not in PROFILES:
            raise SystemExit(__doc__)
        build(profile)
