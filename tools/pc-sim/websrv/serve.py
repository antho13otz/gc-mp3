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
        coupe = self.path.startswith('/coupe/')          # (v203 : connexion coupee a 40 % : essai de la reprise)
        if coupe: self.path = self.path[6:]
        p = self.translate_path(self.path.split('?')[0])
        rg = self.headers.get('Range', '')
        if rg.startswith('bytes=') and os.path.isfile(p):   # (reprise : 206)
            d = open(p, 'rb').read(); a = int(rg[6:].split('-')[0] or 0)
            part = d[a:]
            self.send_response(206)
            self.send_header('Content-Type', 'application/octet-stream')
            self.send_header('Accept-Ranges', 'bytes')
            self.send_header('Content-Range', 'bytes %d-%d/%d' % (a, len(d) - 1, len(d)))
            self.send_header('Content-Length', str(len(part)))
            self.end_headers()
            self.wfile.write(part)
            return
        if not os.path.isfile(p): self.send_error(404); return
        d = open(p, 'rb').read()
        ct = self.guess_type(p)
        if p.endswith('.webp'): ct = 'image/webp'
        gz = 'gzip' in self.headers.get('Accept-Encoding', '') and (ct.startswith('text/') or 'css' in ct)
        if gz: d = gzip.compress(d)
        self.send_response(200)
        self.send_header('Content-Type', ct + ('; charset=utf-8' if ct.startswith('text/') else ''))
        if gz: self.send_header('Content-Encoding', 'gzip')
        if not gz: self.send_header('Accept-Ranges', 'bytes')
        self.send_header('Content-Length', str(len(d)))
        self.end_headers()
        if coupe:
            self.wfile.write(d[:len(d) * 4 // 10]); self.wfile.flush()
            self.close_connection = True
            try: self.connection.shutdown(2)
            except Exception: pass
            return
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
