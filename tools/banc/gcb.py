#!/usr/bin/env python3
# v171 : banc d'essai : lit une image reduite (.gcb, exportee par GC Station : Jeux > Options > Banc d'essai)
import struct

def lire(path):
    f = open(path, 'rb')
    h = f.read(0x5C)
    if h[:4] != b'GCB1': raise ValueError('pas un fichier .gcb')
    ver, = struct.unpack_from('>I', h, 4)
    gid = h[8:14].decode('latin-1'); disc = h[14]
    iso, n, total = struct.unpack_from('>III', h, 0x10)
    title = h[0x1C:0x5C].split(b'\0')[0].decode('latin-1', 'replace')
    tab = [struct.unpack('>II', f.read(8)) for _ in range(n)]
    pos = 0x5C + 8 * n
    return {'id': gid, 'disc': disc, 'iso': iso, 'title': title, 'parts': tab, 'data_at': pos, 'path': path}

def morceaux(g):
    """(position dans l'ISO, octets) pour chaque morceau"""
    f = open(g['path'], 'rb'); f.seek(g['data_at'])
    for off, ln in g['parts']:
        yield off, f.read(ln)

def ecrire_iso(g, out):
    """ISO creuse (sparse) : seuls les morceaux exportes sont ecrits, le reste vaut zero"""
    with open(out, 'wb') as o:
        o.truncate(g['iso'])
        for off, data in morceaux(g):
            o.seek(off); o.write(data)

def fichier(g, off):
    """nom du fichier du disque qui contient cette position (d'apres la FST exportee)"""
    hd = dict(morceaux(g)).get(0)
    if not hd: return '%08X' % off
    fst = struct.unpack_from('>I', hd, 0x424)[0]
    ft = None
    for o, d in morceaux(g):
        if o == fst: ft = d
    if not ft: return '%08X' % off
    ne = struct.unpack_from('>I', ft, 8)[0]; st = ne * 12
    names = {}
    def nom(i): e = ft[i * 12:(i + 1) * 12]; n0 = struct.unpack_from('>I', e, 0)[0] & 0xFFFFFF; return ft[st + n0:ft.index(b'\0', st + n0)].decode('latin-1')
    def walk(i, end, path):
        while i < end:
            e = ft[i * 12:(i + 1) * 12]
            if e[0]: walk(i + 1, struct.unpack_from('>I', e, 8)[0], path + nom(i) + '/'); i = struct.unpack_from('>I', e, 8)[0]
            else:
                o, z = struct.unpack_from('>II', e, 4)
                if o <= off < o + z: names['r'] = path + nom(i)
                i += 1
    try: walk(1, ne, '')
    except Exception: pass
    return names.get('r', '%08X' % off)
