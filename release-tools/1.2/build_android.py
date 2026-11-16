"""TrackWerk 1.1.0-dev.2 Android APK (Claude Code), after Codex' 1.0 apk() in
work/trackwerk-1.0-public/build_release.py: offline Gradle 8.14.3, the local JDK 21 and Android SDK
from toolchains.env, release signing with the existing key. The signing properties file is only passed
to Gradle as a path; it is never read here or copied. The build runs from a clean `git archive` of
HEAD in ~/Library/Caches (outside iCloud); only the APK and its report come back to port/android.

The APK keeps package com.kidbunka.trackwerk and the 1.0 signer, so it installs over 1.0
(versionCode 100000 → 101002) as an in-place update with data kept; the same file is the
first install on a device without TrackWerk.

usage: python3 build_android.py
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

from build_port import OUT_ROOT, TREE, VERSION, sha, source_state

CODE = 102000
SIGNER = '01adb954fffe9c5c89f65c891887b90fe65088e98b4c12e860a9d7b7161161d9'
SIGNING = Path('/home/YOUR_USER/Documents/Codex/2026-11-08/k/work/public-beta/private-signing/release-signing.properties')
GRADLE = Path('/home/YOUR_USER/.gradle/wrapper/dists/gradle-8.14.3-bin/cv11ve7ro1n3o1j4so8xd9n66/gradle-8.14.3/bin/gradle')
GRADLE_HOME_1_0 = Path('/home/YOUR_USER/Library/Caches/TrackWerk/claude-android/gradle-home')
WORK = Path(os.environ['TW_ANDROID_WORK'])
TOOLCHAINS = Path('/home/YOUR_USER/Library/Application Support/TrackWerk/Toolchains')
SDK = Path(os.environ.get('ANDROID_HOME', TOOLCHAINS / 'android-sdk'))
JAVA = Path(os.environ.get('JAVA_HOME', TOOLCHAINS / 'jdk-21.0.13+9/Contents/Home'))


def main():
    assert SIGNING.is_file() and GRADLE.is_file() and (JAVA / 'bin/java').is_file() and SDK.is_dir()
    state = source_state()
    commit = state['commit']
    assert state['tree_clean'], 'working copy has changes'
    WORK.mkdir(parents=True, exist_ok=True)
    source = WORK / 'source'
    if source.exists():
        shutil.rmtree(source)
    source.mkdir()
    if state['source'] == 'git':
        archive = subprocess.Popen(['git', '-C', str(TREE), 'archive', '--format=tar', 'HEAD'], stdout=subprocess.PIPE)
        subprocess.run(['tar', '-x', '-C', str(source)], stdin=archive.stdout, check=True)
        assert archive.wait() == 0
    else:   # the unpacked Source ZIP, already checked against QUELLSTAND.json
        shutil.rmtree(source)
        shutil.copytree(TREE, source, symlinks=True)
    home = WORK / 'gradle-home-current'
    if not home.exists():   # APFS clone of Codex' 1.0 dependency cache; theirs is only read
        subprocess.run(['cp', '-c', '-R', str(GRADLE_HOME_1_0), str(home)], check=True)
    env = dict(os.environ, ANDROID_SDK_ROOT=str(SDK), ANDROID_HOME=str(SDK), ANDROID_USER_HOME=str(WORK / 'android-home'),
               JAVA_HOME=str(JAVA), GRADLE_USER_HOME=str(home), CMAKE_BUILD_PARALLEL_LEVEL='2')
    out = Path(os.environ.get('TW_ANDROID_OUT', OUT_ROOT / 'android'))   # keep a verified APK apart
    out.mkdir(exist_ok=True)
    log = out / 'build.log'
    args = [str(GRADLE), '--offline', '--no-daemon', '--console=plain', '--max-workers=2', '--project-dir', str(source),
            f'-PptSigningProperties={SIGNING}', ':app:assembleRelease']
    with log.open('w') as stream:
        stream.write('COMMAND ' + ' '.join(args) + '\n')
        stream.flush()
        subprocess.run(args, check=True, stdout=stream, stderr=subprocess.STDOUT, env=env)
    apk = out / f'TrackWerk-{VERSION}-Android.apk'
    shutil.copy2(source / 'app/build/outputs/apk/release/app-release.apk', apk)
    tools = SDK / 'build-tools/36.0.0'
    run = lambda a: subprocess.check_output([str(x) for x in a], text=True, env=env)
    badging = run([tools / 'aapt2', 'dump', 'badging', apk])
    assert f"name='com.kidbunka.trackwerk' versionCode='{CODE}' versionName='{VERSION}'" in badging, badging[:300]
    signer = run([tools / 'apksigner', 'verify', '--verbose', '--print-certs', apk])
    assert SIGNER in signer, 'signer differs from 1.0'
    subprocess.run([str(tools / 'zipalign'), '-c', '-P', '16', '4', str(apk)], check=True, env=env)
    abis = sorted(a for line in badging.splitlines() if line.startswith('native-code') for a in line.split("'")[1::2])
    report = {'profile': 'android', 'version': VERSION, 'version_code': CODE, 'previous_version_code': 100000,
              'commit': commit, 'tree_clean': True, 'package': 'com.kidbunka.trackwerk', 'bytes': apk.stat().st_size,
              'sha256': sha(apk), 'signer_sha256': SIGNER, 'signer_matches_1_0': True, 'apk_16k_zipalign': True,
              'abis': abis, 'physical_device_tested': False}
    (out / 'BUILD-QA.json').write_text(json.dumps(report, indent=2) + '\n')
    print('android built', report['sha256'], report['bytes'], flush=True)


if __name__ == '__main__':
    if any(a in ('-h', '--help') for a in sys.argv[1:]):
        print(__doc__)
        sys.exit(0)
    main()
