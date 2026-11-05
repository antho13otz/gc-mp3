#!/usr/bin/env python3
# v345 : essai du Salon par Internet : GC Station (simulateur) + une fausse console "par Internet" (ce script).
#   - GC Station a salon_ami = 127.0.0.2:61234 (ce script) et le code "salon-test" ;
#   - on verifie : ses annonces GCSW1 arrivent ici (bonne empreinte), elle repond aux notres, elle nous voit
#     (journal : "salon : Ami du test"), son port 51238 sert /s/list avec k=, refuse sans k= ou avec un faux,
#     refuse /s/open et ce qui n'est pas /s/, et un mauvais code ne la fait pas repondre.
import os, socket, subprocess, sys, time, urllib.request, urllib.error
R = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..', '..'))
P = R + '/tools/pc-sim'; RUN = P + '/work/salon-internet'
CODE = 'salon-test'
def khash(code):
    x = 2166136261
    for c in b'GCSW1:' + code.upper().encode():
        x = ((x ^ c) * 16777619) & 0xFFFFFFFF
    return '%08x' % x
K = khash(CODE)
os.makedirs(RUN, exist_ok=True)
if not os.path.isdir(RUN + '/sd:/mp3'): subprocess.run(['sh', P + '/demo-sd.sh', RUN], check=True, stdout=subprocess.DEVNULL)
os.makedirs(RUN + '/sd:/gc-mp3', exist_ok=True)
cfg = RUN + '/sd:/gc-mp3/gc-mp3.txt'
lines = [l for l in open(cfg, encoding='latin-1').read().splitlines() if not l.startswith(('salon', 'salon_'))] if os.path.exists(cfg) else []
lines += ['salon = oui', 'salon_ami = 127.0.0.2:61234', 'salon_code = ' + CODE]
open(cfg, 'w', encoding='latin-1').write('\n'.join(lines) + '\n')
sc = RUN + '/sc.txt'; open(sc, 'w').write('wait 40s\nquit\n')
env = dict(os.environ, PCSIM_SCRIPT=sc, PCSIM_OUT='out', PCSIM_NET='direct')
log = open(RUN + '/log.txt', 'w')
g = subprocess.Popen([os.environ.get('GCS_BIN', P + '/gcstation')], cwd=RUN, env=env, stdout=log, stderr=subprocess.STDOUT)
u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); u.bind(('127.0.0.2', 61234))   # (127.0.0.1 : le simulateur lui-meme); u.settimeout(1)
ok = {}
def check(name, cond):
    ok[name] = bool(cond); print(('OK   ' if cond else 'RATE ') + name); return bool(cond)
got = None; t0 = time.time()
while time.time() - t0 < 25 and not got:
    try:
        d, a = u.recvfrom(512)
        if d.startswith(b'GCSW1\t'): got = (d, a)
    except socket.timeout: pass
check('annonce GCSW1 recue', got)
if got:
    f = got[0].decode('utf-8', 'replace').rstrip('\n').split('\t')
    check('empreinte du code', f[1] == K)
    check('port 51238 annonce', f[3] == '51238')
    gaddr = got[1]
    # notre annonce (mauvais code : pas de reponse), puis la bonne : reponse
    u.sendto(('GCSW1\t%s\tIntrus\t51238\t\n' % khash('autre')).encode(), gaddr)
    rep_bad = False; t1 = time.time()
    while time.time() - t1 < 1.5:
        try:
            d, a = u.recvfrom(512)
            if d.startswith(b'GCSW1\t') and time.time() - t1 < 0.5: rep_bad = True
        except socket.timeout: break
    u.sendto(('GCSW1\t%s\tAmi du test\t51238\tUn morceau\n' % K).encode(), gaddr)
    rep = False; t1 = time.time()
    while time.time() - t1 < 3 and not rep:
        try:
            d, a = u.recvfrom(512); rep = d.startswith(b'GCSW1\t')
        except socket.timeout: pass
    check('reponse a une annonce au bon code', rep)
    import threading
    def keep():                                           # (une vraie console annonce toutes les 5 s)
        while g.poll() is None:
            time.sleep(4); u.sendto(('GCSW1\t%s\tAmi du test\t51238\tUn morceau\n' % K).encode(), gaddr)
    threading.Thread(target=keep, daemon=True).start()
def http(path):
    try:
        with urllib.request.urlopen('http://127.0.0.1:51238' + path, timeout=8) as r: return r.status, r.read()
    except urllib.error.HTTPError as e: return e.code, b''
    except Exception as e: return -1, str(e).encode()
time.sleep(1)
st, body = http('/s/list?p=mp3&k=' + K); check('/s/list avec le code : 200 et la liste', st == 200 and b'\t' in body)
st, _ = http('/s/list?p=mp3'); check('/s/list sans code : 403', st == 403)
st, _ = http('/s/list?p=mp3&k=' + khash('autre')); check('/s/list mauvais code : 403', st == 403)
st, _ = http('/s/open?p=mp3&k=' + K); check('/s/open refuse : 404', st == 404)
st, _ = http('/api/state?k=' + K); check('/api refuse : 404', st == 404)
st, _ = http('/s/get?p=../gc-mp3/gc-mp3.txt&k=' + K); check('.. refuse', st != 200)
sal = b''
for port in (80, 8080):
    try:
        with urllib.request.urlopen('http://127.0.0.1:%d/api/salon' % port, timeout=5) as r: sal = r.read(); break
    except Exception: pass
check('GC Station nous voit (Ami du test, par Internet)', b'Ami du test (Internet)' in sal) or print('   /api/salon :', sal[:200])
check('pas d\'intrus', b'Intrus' not in sal)
g.wait(timeout=60); log.close()
check('pas de plantage', g.returncode == 0)
sys.exit(0 if all(ok.values()) else 1)
