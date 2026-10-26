#!/usr/bin/env python3
# GC Station : jeux en reseau local (LAN) par Internet, avec un relais.
#
# Kirby Air Ride, Mario Kart : Double Dash!!, 1080 Avalanche... jouent en LAN : chaque GameCube a son Broadband
# Adapter, et elles se parlent directement sur le reseau local (trames Ethernet). Pour jouer avec quelqu'un qui
# n'est pas chez toi, un PC de chaque cote recopie les trames des GameCube de son reseau vers un relais sur
# Internet, et remet sur son reseau celles qui viennent de l'autre cote : les consoles se croient sur le meme LAN.
#
#   1) le relais (une seule fois, sur une machine joignable des deux cotes : serveur, ou le PC d'un des joueurs avec
#      le port UDP 51240 ouvert dans la box) :
#        python3 lan-relais.py relais [port]
#   2) chez chaque joueur, sur un PC branche au meme reseau que sa GameCube (cable conseille) :
#        python3 lan-relais.py pont <adresse du relais>[:port] <nom de la partie> [interface]
#      Linux : en root (sudo). Windows / Mac : Npcap (Windows) et le module scapy (pip install scapy), en administrateur.
#   3) les GameCube lancent le jeu en mode LAN (Pulse ou disque), comme si elles etaient cote a cote.
#
# Seules les trames des GameCube passent (adresses Ethernet 00:09:BF:..., celles des Broadband Adapter), et les
# reponses qui leur sont destinees ; une meme trame n'est jamais renvoyee deux fois (pas de boucle).
# Le temps de trajet s'ajoute au jeu : jouable si le ping est faible (meme pays).
import hashlib, os, select, socket, struct, sys, time

MAGIC = b'GCLR1'
NINTENDO = (bytes([0x00, 0x09, 0xBF]), bytes([0x00, 0x17, 0xAB]), bytes([0x00, 0x1F, 0x32]))   # (BBA, et d'autres consoles Nintendo)

def is_gc(mac): return mac[:3] in NINTENDO

# ---------------- relais (Internet) ----------------
def relais(port=51240, duree=None, quiet=False):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind(('', port))
    rooms = {}                                              # partie -> {adresse: dernier paquet}
    t_end = time.time() + duree if duree else None
    if not quiet: print('Relais pret sur le port UDP %d' % port)
    while t_end is None or time.time() < t_end:
        r, _, _ = select.select([s], [], [], 1.0)
        now = time.time()
        for room in list(rooms):                            # (un pont muet depuis 30 s : parti)
            for a in [a for a, t in rooms[room].items() if now - t > 30]: del rooms[room][a]
            if not rooms[room]: del rooms[room]
        if not r: continue
        d, a = s.recvfrom(2048)
        if len(d) < 9 or d[:5] != MAGIC: continue
        room = d[5:9]
        members = rooms.setdefault(room, {})
        if a not in members and not quiet: print('pont %s:%d dans la partie %s' % (a[0], a[1], room.hex()))
        members[a] = now
        if len(d) == 9: continue                            # (signe de vie)
        for b in members:
            if b != a: s.sendto(d, b)

# ---------------- interfaces Ethernet (prendre et remettre des trames) ----------------
class Raw:                                                  # Linux : AF_PACKET
    def __init__(s, ifname):
        s.k = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.htons(3))
        s.k.bind((ifname, 0))
    def fileno(s): return s.k.fileno()
    def recv(s): return s.k.recv(2048)
    def send(s, f): s.k.send(f)

class Scapy:                                                # Windows / Mac : scapy + Npcap / libpcap
    def __init__(s, ifname):
        from scapy.all import conf
        s.sock = conf.L2socket(iface=ifname) if ifname else conf.L2socket()
    def fileno(s): return s.sock.fileno()
    def recv(s):
        p = s.sock.recv()
        return bytes(p) if p is not None else b''
    def send(s, f): s.sock.send(f)

def interface(ifname):
    if hasattr(socket, 'AF_PACKET'): return Raw(ifname or 'eth0')
    return Scapy(ifname)

# ---------------- pont (chez chaque joueur) ----------------
class Pont:
    def __init__(s, relay, room, eth, quiet=False):
        s.relay = relay; s.room = hashlib.sha256(room.encode()).digest()[:4]; s.eth = eth; s.quiet = quiet
        s.u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.u.bind(('', 0))
        s.remote = {}                                       # adresses Ethernet des GameCube de l'autre cote
        s.local = set()
        s.seen = {}                                         # empreintes des trames remises (pas de boucle)
        s.sent = s.recvd = 0
        s.t_alive = 0
    def mark(s, f):
        h = hashlib.blake2b(f, digest_size=8).digest()
        now = time.time()
        if h in s.seen and now - s.seen[h] < 2: return False
        s.seen[h] = now
        if len(s.seen) > 4096: s.seen = {k: v for k, v in s.seen.items() if now - v < 2}
        return True
    def from_lan(s, f):
        if len(f) < 14: return
        dst, src = f[0:6], f[6:12]
        if src in s.remote or not is_gc(src): return        # (une trame remise par nous, ou pas une GameCube)
        if not s.mark(f): return
        if src not in s.local:
            s.local.add(src)
            if not s.quiet: print('GameCube de ce reseau : %s' % src.hex(':'))
        s.u.sendto(MAGIC + s.room + f, s.relay); s.sent += 1
    def from_relay(s, d):
        if len(d) < 9 + 14 or d[:5] != MAGIC or d[5:9] != s.room: return
        f = d[9:]
        src = f[6:12]
        if src not in s.remote:
            s.remote[src] = 1
            if not s.quiet: print('GameCube de l\'autre cote : %s' % src.hex(':'))
        if not s.mark(f): return
        s.eth.send(f); s.recvd += 1
    def tick(s):
        if time.time() - s.t_alive > 5:
            s.t_alive = time.time(); s.u.sendto(MAGIC + s.room, s.relay)
    def run(s, duree=None):
        t_end = time.time() + duree if duree else None
        last = time.time()
        while t_end is None or time.time() < t_end:
            s.tick()
            r, _, _ = select.select([s.eth, s.u], [], [], 1.0)
            if s.eth in r: s.from_lan(s.eth.recv())
            if s.u in r: s.from_relay(s.u.recv(2048))
            if not s.quiet and time.time() - last > 10:
                last = time.time(); print('trames : %d envoyees, %d recues' % (s.sent, s.recvd))

def addr(a, port=51240):
    h, _, p = a.partition(':')
    return (socket.gethostbyname(h), int(p) if p else port)

if __name__ == '__main__':
    if len(sys.argv) >= 2 and sys.argv[1] == 'relais':
        relais(int(sys.argv[2]) if len(sys.argv) > 2 else 51240)
    elif len(sys.argv) >= 4 and sys.argv[1] == 'pont':
        Pont(addr(sys.argv[2]), sys.argv[3], interface(sys.argv[4] if len(sys.argv) > 4 else None)).run()
    else:
        print(__doc__ if __doc__ else ''); print(open(__file__, encoding='utf-8').read().split('import hashlib')[0])
