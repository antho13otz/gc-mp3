#!/usr/bin/env python3
# GC Station sur PC : petits serveurs de test pour SMB / FTP / FSP (bibliotheque standard seulement).
#   python3 tools/pc-sim/netsrv.py DOSSIER   -> FTP 127.0.0.1:2121 (gc / station), FSP 127.0.0.1:2100 (mot de passe : station),
#                                               SMB2 127.0.0.1:4450 (partage "musique", gc / station, signature exigee)
# Ecrits a partir des descriptions des protocoles (RFC 959 / 3659, FSP v2, MS-SMB2 / MS-NLMP) : ce ne sont pas
# des serveurs complets, juste ce que GC Station demande. Journal : ce qui est recu, sur la sortie.
import os, sys, socket, socketserver, struct, threading, time, hmac, hashlib, uuid

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else '.')
USER, PASS = 'gc', 'station'

def safe(rel):
    p = os.path.abspath(os.path.join(ROOT, rel.lstrip('/\\').replace('\\', '/')))
    return p if p == ROOT or p.startswith(ROOT + os.sep) else None

# ---------------- FTP ----------------
FTP_MAXCONN = int(os.environ.get('FTP_MAXCONN', '50'))
FTP_DELAY = float(os.environ.get('FTP_DELAY', '0'))
ftp_lock = threading.Lock(); ftp_open = 0; ftp_total = 0
class FTP(socketserver.StreamRequestHandler):
    def send(self, s): self.wfile.write((s + '\r\n').encode()); self.wfile.flush()
    # v119 : comme un petit serveur (telephone, NAS) : FTP_MAXCONN connexions a la fois au plus (au-dela :
    # connexion refusee), FTP_DELAY secondes de delai par commande ; chaque connexion est comptee
    def handle(self):
        global ftp_open, ftp_total
        with ftp_lock:
            ftp_total += 1
            if ftp_open >= FTP_MAXCONN:
                print('[ftp] REFUSEE (deja %d connexions ouvertes)' % ftp_open, flush=True)
                self.request.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
                return
            ftp_open += 1
        print('[ftp] connexion %d (%d ouvertes)' % (ftp_total, ftp_open), flush=True)
        try: self.handle2()
        finally:
            with ftp_lock: ftp_open -= 1
    def handle2(self):
        self.cwd = '/'; self.pasv = None; self.rest = 0; ok = False; user = ''
        self.send('220 GC test FTP')
        while True:
            l = self.rfile.readline()
            if not l: return
            l = l.decode('utf-8', 'replace').rstrip('\r\n')
            cmd, _, arg = l.partition(' '); cmd = cmd.upper()
            print('[ftp]', l if cmd != 'PASS' else 'PASS ***', flush=True)
            if FTP_DELAY: time.sleep(FTP_DELAY)
            if cmd == 'USER': user = arg; self.send('331 mot de passe')
            elif cmd == 'PASS':
                ok = user == USER and arg == PASS
                self.send('230 ok' if ok else '530 refuse')
            elif not ok: self.send('530 connecte-toi')
            elif cmd in ('TYPE', 'OPTS'): self.send('200 ok')
            elif cmd == 'SYST': self.send('215 UNIX Type: L8')
            elif cmd == 'PWD': self.send('257 "%s"' % self.cwd)
            elif cmd == 'CWD':
                p = safe(arg)
                self.send('250 ok' if p and os.path.isdir(p) else '550 pas un dossier')
            elif cmd in ('EPSV', 'PASV'):
                s = socket.socket(); s.bind(('127.0.0.1', 0)); s.listen(1); self.pasv = s
                port = s.getsockname()[1]
                self.send('229 Entering Extended Passive Mode (|||%d|)' % port if cmd == 'EPSV' else
                          '227 Entering Passive Mode (127,0,0,1,%d,%d)' % (port >> 8, port & 255))
            elif cmd == 'SIZE':
                p = safe(arg)
                self.send('213 %d' % os.path.getsize(p) if p and os.path.isfile(p) else '550 introuvable')
            elif cmd == 'REST': self.rest = int(arg); self.send('350 ok')
            elif cmd in ('MLSD', 'LIST', 'RETR'):
                p = safe(arg or self.cwd)
                if not p or not os.path.exists(p) or not self.pasv: self.send('550 introuvable'); self.rest = 0; continue
                self.send('150 donnees')
                c, _ = self.pasv.accept(); self.pasv.close(); self.pasv = None
                try:
                    if cmd == 'RETR':
                        with open(p, 'rb') as f:
                            f.seek(self.rest); self.rest = 0
                            while True:
                                b = f.read(16384)
                                if not b: break
                                c.sendall(b)
                    else:
                        out = []
                        for n in sorted(os.listdir(p)):
                            q = os.path.join(p, n); d = os.path.isdir(q)
                            out.append('type=%s;size=%d; %s' % ('dir' if d else 'file', 0 if d else os.path.getsize(q), n) if cmd == 'MLSD'
                                       else '%s 1 gc gc %d Jan  1 12:00 %s' % ('drwxr-xr-x' if d else '-rw-r--r--', 0 if d else os.path.getsize(q), n))
                        c.sendall(('\r\n'.join(out) + '\r\n').encode())
                    c.close(); self.send('226 fini')
                except (BrokenPipeError, ConnectionResetError):
                    c.close(); self.send('426 interrompu')
            elif cmd == 'ABOR': self.send('226 ABOR ok')
            elif cmd == 'QUIT': self.send('221 au revoir'); return
            else: self.send('502 non')

# ---------------- FSP (UDP) ----------------
def fsp_sum_reply(b):
    s = sum(b) & 0xFFFFFFFF
    return (s + (s >> 8)) & 0xFF
def fsp_server():
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(('127.0.0.1', 2100))
    key = 0x1234
    while True:
        pk, a = s.recvfrom(2048)
        if len(pk) < 12: continue
        cmd, sm = pk[0], pk[1]
        chk = bytearray(pk); chk[1] = 0
        t = sum(chk) + len(pk); t = (t + (t >> 8)) & 0xFF
        if t != sm: print('[fsp] somme fausse', flush=True); continue
        k, seq, ln, pos = struct.unpack('>HHHI', pk[2:12])
        data = pk[12:12 + ln]
        name, _, pw = data.rstrip(b'\0').partition(b'\n')
        name = name.decode('utf-8', 'replace')
        print('[fsp] cmd %02X pos %d %r' % (cmd, pos, name), flush=True)
        out = b''; rc = cmd
        p = safe(name if name != '.' else '/')
        if pw.decode() != PASS: rc, out = 0x40, b'mot de passe refuse\0'
        elif cmd == 0x41:                                  # GET_DIR : blocs de 1024 (entrees alignees sur 4)
            if not p or not os.path.isdir(p): rc, out = 0x40, b'pas un dossier\0'
            else:
                ents = b''
                blocks = []
                for n in sorted(os.listdir(p)):
                    q = os.path.join(p, n); d = os.path.isdir(q)
                    e = struct.pack('>IIB', int(os.path.getmtime(q)), 0 if d else os.path.getsize(q), 2 if d else 1) + n.encode() + b'\0'
                    e += b'\0' * (-len(e) % 4)
                    if len(ents) + len(e) + 12 > 1024:
                        blocks.append(ents + struct.pack('>IIB', 0, 0, 0x2A)); ents = b''
                    ents += e
                blocks.append(ents + struct.pack('>IIB', 0, 0, 0))
                bi = pos // 1024
                out = blocks[bi] if bi < len(blocks) else b''
        elif cmd == 0x4D:                                  # STAT
            if p and os.path.exists(p): out = struct.pack('>IIB', int(os.path.getmtime(p)), 0 if os.path.isdir(p) else os.path.getsize(p), 2 if os.path.isdir(p) else 1)
            else: out = struct.pack('>IIB', 0, 0, 0)
        elif cmd == 0x42:                                  # GET_FILE
            if not p or not os.path.isfile(p): rc, out = 0x40, b'introuvable\0'
            else:
                with open(p, 'rb') as f: f.seek(pos); out = f.read(1024)
        else: rc, out = 0x40, b'commande inconnue\0'
        r = bytearray(struct.pack('>BBHHHI', rc, 0, key, seq, len(out), pos) + out)
        r[1] = fsp_sum_reply(r)
        s.sendto(bytes(r), a)

# ---------------- SMB2 (2.0.2 / 2.1, NTLMv2, signature exigee) ----------------
def md4(m):
    def F(x, y, z): return (x & y) | (~x & z)
    def G(x, y, z): return (x & y) | (x & z) | (y & z)
    def H(x, y, z): return x ^ y ^ z
    rol = lambda x, n: ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF
    m = bytearray(m); l = len(m) * 8
    m += b'\x80'; m += b'\0' * ((56 - len(m) % 64) % 64); m += struct.pack('<Q', l)
    a, b, c, d = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476
    for i in range(0, len(m), 64):
        X = struct.unpack('<16I', m[i:i + 64]); A, B, C, D = a, b, c, d
        for j in range(16): t = (A + F(B, C, D) + X[j]) & 0xFFFFFFFF; A, D, C, B = D, C, B, rol(t, [3, 7, 11, 19][j % 4])
        for j in range(16): t = (A + G(B, C, D) + X[(j % 4) * 4 + j // 4] + 0x5A827999) & 0xFFFFFFFF; A, D, C, B = D, C, B, rol(t, [3, 5, 9, 13][j % 4])
        o = [0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15]
        for j in range(16): t = (A + H(B, C, D) + X[o[j]] + 0x6ED9EBA1) & 0xFFFFFFFF; A, D, C, B = D, C, B, rol(t, [3, 9, 11, 15][j % 4])
        a, b, c, d = (a + A) & 0xFFFFFFFF, (b + B) & 0xFFFFFFFF, (c + C) & 0xFFFFFFFF, (d + D) & 0xFFFFFFFF
    return struct.pack('<4I', a, b, c, d)

class SMB(socketserver.BaseRequestHandler):
    def rd(self, n):
        b = b''
        while len(b) < n:
            c = self.request.recv(n - len(b))
            if not c: raise EOFError
            b += c
        return b
    def reply(self, req, status, body, sid=None, tid=None):
        h = bytearray(req[:64])
        struct.pack_into('<I', h, 8, status)
        struct.pack_into('<H', h, 14, 64)                  # credits
        struct.pack_into('<I', h, 16, (struct.unpack_from('<I', h, 16)[0] | 1) & ~8)   # reponse
        if sid is not None: struct.pack_into('<Q', h, 40, sid)
        if tid is not None: struct.pack_into('<I', h, 36, tid)
        h[48:64] = b'\0' * 16
        m = bytes(h) + body
        self.request.sendall(struct.pack('>I', len(m)) + m)
    def handle(self):
        self.key = None; self.files = {}; self.sid = 0x1111; self.chal = os.urandom(8); nfid = [1]
        try:
            while True:
                n = struct.unpack('>I', self.rd(4))[0] & 0xFFFFFF
                m = self.rd(n)
                cmd = struct.unpack_from('<H', m, 12)[0]; flags = struct.unpack_from('<I', m, 16)[0]
                b = m[64:]
                # v117 : numeros de demande (une demande a N credits prend N numeros) et credits
                mid = struct.unpack_from('<Q', m, 24)[0]; charge = max(1, struct.unpack_from('<H', m, 6)[0])
                if hasattr(self, 'nmid') and mid != self.nmid: print('[smb] NUMERO DE DEMANDE FAUX', mid, 'attendu', self.nmid, flush=True)
                self.nmid = mid + charge
                if cmd not in (0, 1):                      # signature exigee : verifiee
                    if not (flags & 8): print('[smb] PAS SIGNE', cmd, flush=True); self.reply(m, 0xC0000022, struct.pack('<HH', 9, 0) + b'\0' * 4); continue
                    z = bytearray(m); z[48:64] = b'\0' * 16
                    mac = hmac.new(self.key, bytes(z), hashlib.sha256).digest()[:16]
                    if mac != m[48:64]: print('[smb] SIGNATURE FAUSSE', cmd, flush=True); self.reply(m, 0xC0000022, struct.pack('<HH', 9, 0) + b'\0' * 4); continue
                print('[smb] cmd %d' % cmd, flush=True)
                if cmd == 0:                               # NEGOTIATE
                    dc = struct.unpack_from('<H', b, 2)[0]
                    ds = struct.unpack_from('<%dH' % dc, b, 36)
                    dial = 0x0210 if 0x0210 in ds else 0x0202
                    self.dial = dial
                    big = 8388608 if dial == 0x0210 else 65536    # v117 : SMB 2.1 « large MTU » (GC Station lit quand meme par 64 Ko : plus fluide)
                    body = struct.pack('<HHHH', 65, 0x03, dial, 0) + uuid.uuid4().bytes + struct.pack('<IIIIQQHHI', 0x04 if dial == 0x0210 else 0, big, big, big, 0, 0, 128, 0, 0)
                    self.reply(m, 0, body + b'\0')
                elif cmd == 1:                             # SESSION_SETUP
                    so, sl = struct.unpack_from('<HH', b, 12)
                    tok = m[so:so + sl]
                    t = struct.unpack_from('<I', tok, 8)[0]
                    if t == 1:
                        ti = struct.pack('<HH', 2, 8) + 'TEST'.encode('utf-16-le') + struct.pack('<HHQ', 7, 8, int((time.time() + 11644473600) * 1e7)) + struct.pack('<HH', 0, 0)
                        ch = b'NTLMSSP\0' + struct.pack('<IHHII', 2, 0, 0, 56, 0x00888215) + self.chal + b'\0' * 8 + struct.pack('<HHI', len(ti), len(ti), 56) + ti
                        self.reply(m, 0xC0000016, struct.pack('<HHHH', 9, 0, 72, len(ch)) + ch, sid=self.sid)
                    else:
                        f = lambda o: tok[struct.unpack_from('<I', tok, o + 4)[0]:struct.unpack_from('<I', tok, o + 4)[0] + struct.unpack_from('<H', tok, o)[0]]
                        nt, dom, usr = f(20), f(28), f(36)
                        nth = md4(PASS.encode('utf-16-le'))
                        ow = hmac.new(nth, (usr.decode('utf-16-le').upper() + dom.decode('utf-16-le')).encode('utf-16-le'), hashlib.md5).digest()
                        proof = hmac.new(ow, self.chal + nt[16:], hashlib.md5).digest()
                        ok = usr.decode('utf-16-le') == USER and proof == nt[:16]
                        print('[smb] identification %s (%s)' % ('OK' if ok else 'REFUSEE', usr.decode('utf-16-le')), flush=True)
                        if not ok: self.reply(m, 0xC000006D, struct.pack('<HHHH', 9, 0, 0, 0), sid=self.sid); continue
                        self.key = hmac.new(ow, proof, hashlib.md5).digest()
                        self.reply(m, 0, struct.pack('<HHHH', 9, 0, 0, 0), sid=self.sid)
                elif cmd == 3:                             # TREE_CONNECT
                    po, pl = struct.unpack_from('<HH', b, 4)
                    unc = m[po:po + pl].decode('utf-16-le')
                    print('[smb] partage', unc, flush=True)
                    if not unc.lower().endswith('\\musique'): self.reply(m, 0xC00000CC, struct.pack('<HH', 9, 0) + b'\0' * 4); continue
                    self.reply(m, 0, struct.pack('<HBBII', 16, 1, 0, 0, 0x001F01FF), tid=5)
                elif cmd == 5:                             # CREATE
                    no, nl = struct.unpack_from('<HH', b, 44)
                    name = m[no:no + nl].decode('utf-16-le') if nl else ''
                    p = safe(name)
                    print('[smb] ouvrir %r' % name, flush=True)
                    if not p or not os.path.exists(p): self.reply(m, 0xC0000034, struct.pack('<HH', 9, 0) + b'\0' * 4); continue
                    fid = struct.pack('<QQ', nfid[0], nfid[0]); nfid[0] += 1
                    self.files[fid] = [p, False]
                    d = os.path.isdir(p); sz = 0 if d else os.path.getsize(p)
                    body = struct.pack('<HBBI', 89, 0, 0, 1) + struct.pack('<QQQQ', 0, 0, 0, 0) + struct.pack('<QQII', sz, sz, 0x10 if d else 0x80, 0) + fid + struct.pack('<II', 0, 0)
                    self.reply(m, 0, body)
                elif cmd == 6:                             # CLOSE
                    self.files.pop(b[8:24], None)
                    self.reply(m, 0, struct.pack('<HHI', 60, 0, 0) + b'\0' * 56)
                elif cmd == 14:                            # QUERY_DIRECTORY (FileDirectoryInformation)
                    fid = b[8:24]; e = self.files.get(fid)
                    if not e or e[1] and not (b[3] & 1): self.reply(m, 0x80000006, struct.pack('<HH', 9, 0) + b'\0' * 4); continue
                    e[1] = True
                    out = b''; ents = sorted(os.listdir(e[0]))
                    for i, n in enumerate(ents):
                        q = os.path.join(e[0], n); d = os.path.isdir(q); nm = n.encode('utf-16-le')
                        ent = struct.pack('<II', 0, 0) + struct.pack('<QQQQ', 0, 0, 133000000000000000, 0) + struct.pack('<QQII', 0 if d else os.path.getsize(q), 0, 0x10 if d else 0x80, len(nm)) + nm
                        ent += b'\0' * (-len(ent) % 8)
                        if i < len(ents) - 1: ent = struct.pack('<I', len(ent)) + ent[4:]
                        out += ent
                    self.reply(m, 0, struct.pack('<HHI', 9, 72, len(out)) + out)
                elif cmd == 8:                             # READ
                    ln, off = struct.unpack_from('<IQ', b, 4); fid = b[16:32]
                    if ln > 65536 * charge: print('[smb] LECTURE TROP GROSSE POUR LES CREDITS', ln, charge, flush=True); self.reply(m, 0xC000000D, struct.pack('<HH', 9, 0) + b'\0' * 4); continue
                    print('[smb] lire %d octets (%d credits)' % (ln, charge), flush=True)
                    e = self.files.get(fid)
                    with open(e[0], 'rb') as f: f.seek(off); d = f.read(ln)
                    if not d: self.reply(m, 0xC0000011, struct.pack('<HH', 9, 0) + b'\0' * 4); continue
                    self.reply(m, 0, struct.pack('<HBBIII', 17, 80, 0, len(d), 0, 0) + d)
                else: self.reply(m, 0xC00000BB, struct.pack('<HH', 9, 0) + b'\0' * 4)
        except EOFError: pass

class TS(socketserver.ThreadingMixIn, socketserver.TCPServer): allow_reuse_address = True; daemon_threads = True
if __name__ == '__main__':
    threading.Thread(target=lambda: TS(('127.0.0.1', 2121), FTP).serve_forever(), daemon=True).start()
    threading.Thread(target=fsp_server, daemon=True).start()
    threading.Thread(target=lambda: TS(('127.0.0.1', 4450), SMB).serve_forever(), daemon=True).start()
    print('serveurs de test : FTP 2121, FSP 2100, SMB 4450 ; dossier', ROOT, flush=True)
    while True: time.sleep(3600)
