#!/usr/bin/env python3
# Essai de tools/lan-relais.py sans GameCube : deux reseaux simules (paires de sockets), un relais, deux ponts.
import importlib.util, os, socket, sys, threading, time
spec = importlib.util.spec_from_file_location('lr', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lan-relais.py'))
lr = importlib.util.module_from_spec(spec); spec.loader.exec_module(lr)

class FakeEth:                                              # un cote de la paire : le pont ; l'autre : « le reseau »
    def __init__(s): s.a, s.b = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    def fileno(s): return s.a.fileno()
    def recv(s): return s.a.recv(2048)
    def send(s, f): s.a.send(f)
lanA, lanB = FakeEth(), FakeEth()
threading.Thread(target=lr.relais, kwargs=dict(port=51299, duree=6, quiet=True), daemon=True).start()
time.sleep(0.3)
pa = lr.Pont(('127.0.0.1', 51299), 'partie-test', lanA, quiet=True)
pb = lr.Pont(('127.0.0.1', 51299), 'partie-test', lanB, quiet=True)
for p in (pa, pb): threading.Thread(target=p.run, kwargs=dict(duree=5), daemon=True).start()
time.sleep(0.5)                                             # (les ponts se signalent au relais)
gc1, gc2, pc = bytes.fromhex('0009bf000001'), bytes.fromhex('0009bf000002'), bytes.fromhex('a4b1c2d3e4f5')
bc = b'\xff' * 6
f1 = bc + gc1 + b'\x08\x00' + b'kirby air ride : qui est la ?' + bytes(30)
f2 = gc1 + gc2 + b'\x08\x00' + b'moi !' + bytes(40)
fpc = bc + pc + b'\x08\x00' + b'un PC (ne doit pas passer)' + bytes(30)
lanA.b.send(f1); lanA.b.send(fpc)
time.sleep(0.4)
lanB.b.setblocking(False); lanA.b.setblocking(False)
def drain(s):
    out = []
    while True:
        try: out.append(s.recv(2048))
        except BlockingIOError: return out
gotB = drain(lanB.b)
lanB.b.send(f2)
lanB.b.send(gotB[0] if gotB else b'')                     # (le reseau B « renvoie » la trame remise : le pont ne doit pas la reprendre)
time.sleep(0.4)
gotA = drain(lanA.b)
ok = gotB == [f1] and gotA == [f2]
print(('OK   ' if ok else 'FAUX ') + 'relais LAN : A -> B %d trame(s) (attendu 1, le PC filtre), B -> A %d trame(s) (attendu 1, pas de boucle)' % (len(gotB), len(gotA)))
print('TOUT OK' if ok else 'ECHEC'); sys.exit(0 if ok else 1)
