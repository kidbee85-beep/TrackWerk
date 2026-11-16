#!/usr/bin/env python3
"""Local HTTP server for ptlinux-progressive-check (TrackWerk 1.2, Sources part B).

Every archive item is one scenario. Downloads send a prefix, then hold the rest until the test
writes allow-<id> (or a deadline passes); they record eof-<id> after the last byte and gone-<id> when
the client hung up early. MP3s are made here from a synthetic tone with FFmpeg (no foreign audio).

usage: linux_sources_progressive_fixture.py <ptlinux-progressive-check> <new run directory>
"""
import http.server, json, math, os, pathlib, select, shutil, socket, struct, subprocess, sys, threading, time, wave

exe = pathlib.Path(sys.argv[1]).resolve()
out = pathlib.Path(sys.argv[2]).resolve()
assert not out.exists(), 'use a fresh run directory'
out.mkdir(parents=True)
ffmpeg = os.environ.get('PT_SOURCE_FIXTURE_FFMPEG', shutil.which('ffmpeg') or '/Applications/Krita.app/Contents/MacOS/ffmpeg')
if not pathlib.Path(ffmpeg).is_file():
    print('SKIP no FFmpeg for MP3 fixtures'); sys.exit(0)

def tone(path, seconds, rate, hz):
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
        frames = bytearray()
        for i in range(int(seconds * rate)):
            v = int(11000 * math.sin(2 * math.pi * hz * i / rate))
            frames += struct.pack('<hh', v, v // 2)
        w.writeframes(bytes(frames))

def mp3(name, seconds, rate, hz, extra=()):
    source, target = out / f'{name}.wav', out / f'{name}.mp3'
    tone(source, seconds, rate, hz)
    subprocess.run([ffmpeg, '-hide_banner', '-loglevel', 'error', '-nostdin', '-y', '-i', str(source),
                    '-codec:a', 'libmp3lame', *extra, '-id3v2_version', '0', '-write_xing', '0', str(target)], check=True)
    return target.read_bytes()

long_mp3 = mp3('long', 40, 44100, 440, ['-b:a', '128k'])
vbr_mp3 = mp3('vbr', 12, 44100, 330, ['-q:a', '4'])
other_mp3 = mp3('other', 8, 44100, 550, ['-b:a', '128k'])
high_mp3 = mp3('high', 6, 48000, 660, ['-b:a', '128k'])

def id3(payload_bytes):
    # ID3v2.3, no frames, `payload_bytes` of padding: a valid, large tag (cover art stands in).
    size = payload_bytes
    syncsafe = bytes([(size >> 21) & 127, (size >> 14) & 127, (size >> 7) & 127, size & 127])
    return b'ID3\x03\x00\x00' + syncsafe + b'\x00' * size

# For the decoder's own checks (fed in 4096-byte blocks, as Codex' STUFE2-ID3-BEFUND does).
(out / 'plain.mp3').write_bytes(long_mp3)
(out / 'id3-300k.mp3').write_bytes(id3(300 * 1024) + long_mp3)

items = {
    # id: (bytes, prefix sent before the hold, chunked, title, duration ms)
    'held':    (long_mp3, 96 * 1024, False, 'Held download', 40000),
    'vbr':     (vbr_mp3, 32 * 1024, True, 'VBR chunked unknown length', 0),
    'id3':     (id3(300 * 1024) + long_mp3, 300 * 1024 + 10 + 64 * 1024, False, 'Large ID3 tag', 40000),
    'format':  (other_mp3 + high_mp3, len(other_mp3) + 16 * 1024, False, 'Format change', 14000),
    'broken':  (bytes((i * 73 + 11) % 251 for i in range(400 * 1024)), 400 * 1024, False, 'Not audio', 0),
    'second':  (other_mp3, len(other_mp3), False, 'Second source', 8000),
}

class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *args): pass
    def handle(self):
        try: super().handle()
        except (ConnectionResetError, BrokenPipeError): pass   # a client that hung up between requests
    def client_gone(self):
        # While holding: the client closed its side (readable, and nothing to read).
        readable, _, _ = select.select([self.connection], [], [], 0)
        if not readable: return False
        try: return self.connection.recv(1, socket.MSG_PEEK) == b''
        except OSError: return True
    def blob(self, data, kind='application/json'):
        self.send_response(200); self.send_header('Content-Type', kind); self.send_header('Content-Length', str(len(data)))
        self.end_headers(); self.wfile.write(data)
    def do_GET(self):
        if self.path.startswith('/archive/advancedsearch.php'):
            docs = [{'identifier': k, 'title': v[3], 'creator': 'TrackWerk fixture', 'subject': ['Jazz']} for k, v in items.items()]
            return self.blob(json.dumps({'response': {'numFound': len(docs), 'docs': docs}}).encode())
        if self.path.startswith('/archive/metadata/'):
            key = self.path.rsplit('/', 1)[1].split('?')[0]
            data, _, _, title, duration = items[key]
            files = [{'name': f'{key}.mp3', 'title': title, 'size': str(len(data))}]
            if duration: files[0]['length'] = str(duration / 1000)
            return self.blob(json.dumps({'metadata': {'creator': 'TrackWerk fixture',
                'licenseurl': 'https://creativecommons.org/publicdomain/zero/1.0/'}, 'files': files}).encode())
        if self.path.startswith('/archive/download/'):
            key = self.path.split('/')[3]
            data, prefix, chunked, _, _ = items[key]
            self.send_response(200); self.send_header('Content-Type', 'audio/mpeg')
            if chunked: self.send_header('Transfer-Encoding', 'chunked')
            else: self.send_header('Content-Length', str(len(data)))
            self.end_headers()
            def send(part):
                if chunked: self.wfile.write(b'%x\r\n' % len(part) + part + b'\r\n')
                else: self.wfile.write(part)
                self.wfile.flush()
            try:
                for at in range(0, prefix, 4096): send(data[at:min(prefix, at + 4096)])
                (out / f'prefix-{key}').write_text(str(prefix))
                until = time.monotonic() + 20
                while not (out / f'allow-{key}').exists() and time.monotonic() < until:
                    if self.client_gone():
                        (out / f'gone-{key}').write_text('client closed while held'); return
                    time.sleep(.01)
                for at in range(prefix, len(data), 4096): send(data[at:at + 4096])
                if chunked: self.wfile.write(b'0\r\n\r\n'); self.wfile.flush()
                (out / f'eof-{key}').write_text('complete')
            except (BrokenPipeError, ConnectionResetError):
                (out / f'gone-{key}').write_text('client closed')
            return
        self.send_error(404)

server = Server(('127.0.0.1', 0), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
env = os.environ.copy(); env['PT_SOURCE_TEST_BASE'] = f'http://127.0.0.1:{server.server_port}'
try:
    code = subprocess.run([str(exe), str(out)], env=env, timeout=150).returncode
    if code == 0:
        # The same full-disk case in a child with a file-size limit (the cache write fails part-way).
        code = subprocess.run([str(exe), str(out), '--disk-limit'], env=env, timeout=60).returncode
    sys.exit(code)
finally:
    server.shutdown(); server.server_close()
