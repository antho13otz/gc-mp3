#!/usr/bin/env python3
# GC Station : serveur de jeux pour « Pulse par le reseau », sur un PC (Windows, Mac, Linux : Python 3).
#
#   python3 pulse-serveur.py <dossier des jeux> [nom affiche]
#
# Le PC apparait sur les GameCube comme une autre GC Station du Salon (START > Reglages > Reseau > Salon,
# active sur la GameCube) : onglet FICHIERS > Appareils. Choisir un .iso / .gcm puis « Jouer par le reseau,
# sans copier ». Le PC et la GameCube doivent etre sur le meme reseau. Ports : UDP 51234 (annonce),
# TCP 8080 (liste des fichiers ; la GameCube l'apprend par l'annonce), UDP 51235 (lecture du jeu).
# Seuls les fichiers du dossier donne sont visibles.
import os, socket, sys, threading, time, urllib.parse, http.server, struct

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '.')
NAME = sys.argv[2] if len(sys.argv) > 2 else 'PC ' + socket.gethostname()
HTTP_PORT, ANN_PORT, ISO_PORT = int(os.environ.get('PULSE_HTTP', 8080)), 51234, 51235
BIND = os.environ.get('PULSE_IP', '')
files = {}                                                    # numero -> chemin
lock = threading.Lock()

def safe(rel):
    full = os.path.abspath(os.path.join(ROOT, rel.lstrip('/')))
    return full if full == ROOT or full.startswith(ROOT + os.sep) else None

def announce():
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    if BIND: u.bind((BIND, 0))
    msg = ('GCST1\t%s\t%d\t\n' % (NAME, HTTP_PORT)).encode()
    while True:
        for dst in (os.environ.get('PULSE_ANNONCE', '255.255.255.255'),):
            try: u.sendto(msg, (dst, ANN_PORT))
            except OSError: pass
        time.sleep(5)

class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body=b'', ctype='text/plain'):
        self.send_response(code); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        u = urllib.parse.urlparse(self.path); q = urllib.parse.parse_qs(u.query)
        rel = q.get('p', [''])[0]
        if u.path == '/s/now': return self.send(200, b'\t0\t0\t\n')
        full = safe(rel)
        if not full or not os.path.exists(full): return self.send(404)
        if u.path == '/s/stat':
            return self.send(200, (('d 0' if os.path.isdir(full) else 'f %d' % os.path.getsize(full))).encode())
        if u.path == '/s/list' and os.path.isdir(full):
            out = []
            for n in sorted(os.listdir(full)):
                if n.startswith('.'): continue
                p = os.path.join(full, n)
                out.append('%s\t%d\t%s\n' % ('d' if os.path.isdir(p) else 'f', 0 if os.path.isdir(p) else os.path.getsize(p), n))
            return self.send(200, ''.join(out).encode('utf-8'), 'text/plain; charset=utf-8')
        if u.path == '/s/open' and os.path.isfile(full):
            with lock:
                tok = next((k for k, v in files.items() if v == full), None)
                if tok is None: tok = len(files) + 1 + (int(time.time()) & 0xFFFF) * 256; files[tok] = full
            return self.send(200, ('%d %d %d' % (tok, os.path.getsize(full), ISO_PORT)).encode())
        if u.path == '/s/get' and os.path.isfile(full):
            size = os.path.getsize(full); a, z = 0, size - 1
            r = self.headers.get('Range')
            if r and r.startswith('bytes='):
                s, _, e = r[6:].partition('-'); a = int(s or 0); z = min(int(e) if e else size - 1, size - 1)
            with open(full, 'rb') as f:
                f.seek(a); body = f.read(z - a + 1)
            self.send_response(206 if r else 200)
            if r: self.send_header('Content-Range', 'bytes %d-%d/%d' % (a, z, size))
            self.send_header('Content-Length', str(len(body))); self.end_headers(); self.wfile.write(body); return
        self.send(404)

def iso_server():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((BIND, ISO_PORT))
    cache = {}
    while True:
        rq, src = s.recvfrom(64)
        if len(rq) < 18 or rq[:4] != b'PNR1': continue
        seq, tok, off, ln = struct.unpack('>III', rq[4:16]) + struct.unpack('>H', rq[16:18])
        path = files.get(tok)
        if not path: continue
        f = cache.get(tok) or open(path, 'rb'); cache[tok] = f
        f.seek(off); data = f.read(min(ln, 2048)); data += bytes(min(ln, 2048) - len(data))
        for o in range(0, len(data), 1024):
            s.sendto(b'PNA1' + struct.pack('>IIH', seq, off + o, len(data[o:o + 1024])) + data[o:o + 1024], src)

if __name__ == '__main__':
    print('GC Station - serveur Pulse : %s (%s)' % (ROOT, NAME))
    threading.Thread(target=announce, daemon=True).start()
    threading.Thread(target=iso_server, daemon=True).start()
    http.server.ThreadingHTTPServer((BIND, HTTP_PORT), H).serve_forever()
