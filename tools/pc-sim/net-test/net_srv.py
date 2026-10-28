#!/usr/bin/env python3
# Faux Internet pour le simulateur GC Station : HTTPS (443) et HTTP (80) pour tous les sites de hosts.txt.
# Delais realistes : LAT s avant chaque reponse, HS s avant la poignee de main TLS (comme un serveur lointain).
#   radio.test : /redir (302 -> radio2.test/flux), /flux (MP3 + ICY), /aac, /ogg, /liste.m3u, /liste.pls,
#                /hls/live.m3u8 (HLS en direct, segments AAC), /coupe (MP3 coupe apres 8 s), /lent (en-tetes apres 2,5 s)
#   icecast.radiofrance.fr/*.mp3 : MP3 + ICY (les radios de radio.txt)
#   musicbrainz.org, coverartarchive.org, lrclib.net, itunes.apple.com : reponses JSON / images
#   le reste : 404
import os, ssl, socket, socketserver, threading, time, json, re, sys
from urllib.parse import urlparse, parse_qs, unquote

D = os.path.dirname(os.path.abspath(__file__))   # (fichiers generes par setup.sh a cote)
LAT = float(os.environ.get('LAT', '0.08'))
HS = float(os.environ.get('HS', '0.15'))
LOG = open(os.path.join(D, 'net_srv.log'), 'a')
def log(*a):
    LOG.write(time.strftime('%H:%M:%S ') + ' '.join(str(x) for x in a) + '\n'); LOG.flush()

def rd(n): return open(os.path.join(D, n), 'rb').read()
MP3, AAC, OGG = rd('flux.mp3'), rd('flux.aac'), rd('flux.ogg')
RATE = {'mp3': len(MP3) / 120.0, 'aac': len(AAC) / 60.0, 'ogg': len(OGG) / 60.0}
SEGS = sorted([f for f in os.listdir(os.path.join(D, 'hls')) if f.endswith('.ts')], key=lambda f: int(re.sub(r'\D', '', f)))
T0 = time.time()

def stream(w, data, rate, icy, cut=0):
    """flux continu au rythme reel (1 s d'avance au debut), metadonnees ICY toutes les 16000 octets"""
    t0 = time.time(); sent = 0; i = 0; meta_n = 0; until_meta = 16000
    while True:
        if cut and time.time() - t0 > cut:
            log('coupure'); return
        n = min(4096, len(data) - i)
        if n <= 0: i = 0; continue
        if icy: n = min(n, until_meta)
        chunk = data[i:i + n]; i += n
        w.write(chunk); sent += n
        if icy:
            until_meta -= n
            if until_meta == 0:
                meta_n += 1
                t = ("StreamTitle='Artiste %d - Titre %d';" % (meta_n // 6, meta_n // 6)).encode() if meta_n % 6 == 1 else b''
                L = (len(t) + 15) // 16
                w.write(bytes([L]) + t.ljust(L * 16, b'\0'))
                until_meta = 16000
        wait = (sent - rate) / rate - (time.time() - t0)
        if wait > 0: time.sleep(wait)

def jpeg(w, name):
    b = rd(name)
    w.write(b'HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: %d\r\nConnection: close\r\n\r\n' % len(b) + b)

def jsn(w, obj):
    b = json.dumps(obj).encode()
    w.write(b'HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %d\r\nConnection: close\r\n\r\n' % len(b) + b)

def txt(w, code, body, ctype='text/plain', extra=''):
    b = body.encode()
    w.write(('HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %d\r\n%sConnection: close\r\n\r\n' % (code, ctype, len(b), extra)).encode() + b)

def answer(w, host, path, hdrs, scheme):
    u = urlparse(path); p = u.path; q = parse_qs(u.query)
    icy = hdrs.get('icy-metadata') == '1'
    def audio(kind, data, cut=0):
        h = 'HTTP/1.0 200 OK\r\nContent-Type: %s\r\nicy-name: Test %s\r\nicy-br: 128\r\n' % (
            {'mp3': 'audio/mpeg', 'aac': 'audio/aac', 'ogg': 'application/ogg'}[kind], kind)
        if icy and kind == 'mp3': h += 'icy-metaint: 16000\r\n'
        w.write((h + '\r\n').encode())
        stream(w, data, RATE[kind], icy and kind == 'mp3', cut)
    if host in ('radio.test', 'radio2.test'):
        if p == '/redir': return txt(w, '302 Found', '', extra='Location: https://radio2.test/flux\r\n')
        if p == '/flux': return audio('mp3', MP3)
        if p == '/coupe': return audio('mp3', MP3, cut=8)
        if p == '/lent': time.sleep(2.5); return audio('mp3', MP3)
        if p == '/aac': return audio('aac', AAC)
        if p == '/ogg': return audio('ogg', OGG)
        if p == '/liste.m3u': return txt(w, '200 OK', '#EXTM3U\n#EXTINF:-1,Test\nhttp://icecast.radiofrance.fr/test-liste.mp3\n', 'audio/x-mpegurl')
        if p == '/liste.pls': return txt(w, '200 OK', '[playlist]\nNumberOfEntries=1\nFile1=https://radio.test/flux\nTitle1=Test\n', 'audio/x-scpls')
        if p == '/hls/live.m3u8':
            seq = int((time.time() - T0) / 4)
            body = '#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:4\n#EXT-X-MEDIA-SEQUENCE:%d\n' % seq
            for k in range(seq, seq + 3):
                body += '#EXTINF:4.0,\nseg/%d.ts\n' % k
            return txt(w, '200 OK', body, 'application/vnd.apple.mpegurl')
        m = re.match(r'/hls/seg/(\d+)\.ts', p)
        if m:
            b = rd(os.path.join('hls', SEGS[int(m.group(1)) % len(SEGS)]))
            w.write(b'HTTP/1.1 200 OK\r\nContent-Type: video/mp2t\r\nContent-Length: %d\r\nConnection: close\r\n\r\n' % len(b) + b); return
    if host == 'icecast.radiofrance.fr' and p.endswith('.mp3'): return audio('mp3', MP3)
    if host == 'musicbrainz.org' and p.startswith('/ws/2/recording'):
        qs = unquote(q.get('query', [''])[0])
        t = re.search(r'recording:"([^"]*)"', qs); a = re.search(r'artist:"([^"]*)"', qs)
        t = t.group(1) if t else 'Titre'; a = a.group(1) if a else 'Artiste'
        return jsn(w, {'recordings': [{'id': 'r1', 'score': 100, 'title': t, 'artist-credit': [{'name': a, 'artist': {'name': a}}],
                    'releases': [{'id': '11111111-1111-1111-1111-111111111111', 'title': 'Demo Album', 'status': 'Official', 'date': '2020-01-01',
                                  'release-group': {'id': '22222222-2222-2222-2222-222222222222', 'primary-type': 'Album'}}]}]})
    if host == 'coverartarchive.org': return jpeg(w, 'front-250.jpg')
    if host == 'lrclib.net':
        t = q.get('track_name', q.get('q', ['Titre']))[0]; a = q.get('artist_name', ['Artiste'])[0]
        lrc = '\n'.join('[%02d:%02d.00]Ligne %d de %s' % (s // 60, s % 60, s // 4, t) for s in range(0, 240, 4))
        return jsn(w, [{'trackName': t, 'artistName': a, 'albumName': 'Demo Album', 'duration': 180, 'syncedLyrics': lrc, 'plainLyrics': ''}])
    if host == 'images.test': return jpeg(w, 'grande.jpg')
    return txt(w, '404 Not Found', 'introuvable')

class H(socketserver.BaseRequestHandler):
    def handle(self):
        sock = self.request
        try:
            if self.server.tls:
                time.sleep(HS)
                sock = self.server.ctx.wrap_socket(sock, server_side=True)
            f = sock.makefile('rwb', buffering=0)
            line = f.readline().decode('latin-1').strip()
            hdrs = {}
            while True:
                l = f.readline()
                if l in (b'\r\n', b'\n', b''): break
                k, _, v = l.decode('latin-1').partition(':'); hdrs[k.strip().lower()] = v.strip()
            host = hdrs.get('host', '').split(':')[0]
            parts = line.split(' ')
            log(self.server.port, host, line)
            time.sleep(LAT)
            if len(parts) >= 2: answer(f, host, parts[1], hdrs, 'https' if self.server.tls else 'http')
        except (BrokenPipeError, ConnectionResetError, ssl.SSLError, OSError) as e:
            pass
        finally:
            try: sock.close()
            except OSError: pass

class S(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True; daemon_threads = True

ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
ctx.load_cert_chain(os.path.join(D, 'srv2.pem'), os.path.join(D, 'srv2.key'))
for port, tls in ((443, True), (80, False)):
    s = S(('127.0.0.1', port), H); s.tls = tls; s.ctx = ctx; s.port = port
    threading.Thread(target=s.serve_forever, daemon=True).start()
log('pret')
print('pret', flush=True)
while True: time.sleep(3600)
