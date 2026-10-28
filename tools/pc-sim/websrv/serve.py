#!/usr/bin/env python3
# Petit serveur de test du navigateur (simulateur) : gzip si demande, delai par requete (comme Internet).
import http.server, socketserver, gzip, os, sys, time, threading
ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'site')
DELAY = float(os.environ.get('WEB_DELAY', '0.08'))
LOG = []
class H(http.server.SimpleHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def __init__(self, *a, **k): super().__init__(*a, directory=ROOT, **k)
    def log_message(self, f, *a): print('[web] %s %s gz=%s' % (self.command, self.path, 'gzip' in self.headers.get('Accept-Encoding', '')), flush=True)
    def do_GET(self):
        time.sleep(DELAY)
        p = self.translate_path(self.path.split('?')[0])
        if not os.path.isfile(p): self.send_error(404); return
        d = open(p, 'rb').read()
        ct = self.guess_type(p)
        if p.endswith('.webp'): ct = 'image/webp'
        gz = 'gzip' in self.headers.get('Accept-Encoding', '') and (ct.startswith('text/') or 'css' in ct)
        if gz: d = gzip.compress(d)
        self.send_response(200)
        self.send_header('Content-Type', ct + ('; charset=utf-8' if ct.startswith('text/') else ''))
        if gz: self.send_header('Content-Encoding', 'gzip')
        self.send_header('Content-Length', str(len(d)))
        self.end_headers()
        self.wfile.write(d)
    def do_POST(self):                          # (v201 : formulaires envoyes : une page qui dit ce qui est arrive)
        n = int(self.headers.get('Content-Length', '0') or 0)
        body = self.rfile.read(n).decode('utf-8', 'replace')
        d = ('<html><body><h1>Formulaire recu</h1><p>%s</p><p><a href="/rc/login.html">Retour</a></p></body></html>' % body.replace('<', '&lt;')).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(d)))
        self.end_headers()
        self.wfile.write(d)
class TS(socketserver.ThreadingMixIn, http.server.HTTPServer): daemon_threads = True; allow_reuse_address = True
TS(('127.0.0.1', int(sys.argv[1]) if len(sys.argv) > 1 else 8765), H).serve_forever()
