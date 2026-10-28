#!/usr/bin/env python3
# Ecrit source/cb/lang_fr.h a partir de fr.py et verifie chaque cle (presente dans le code, memes %).
import os, re, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, here)
from fr import TR
src = os.path.join(here, "..", "..", "source", "cb")

def c_unescape(s):     # texte C (echappements) -> octets
    return s.encode('latin-1').decode('unicode_escape').encode('latin-1')

def c_lit(b):          # octets -> litteral C sur, coupe apres chaque \xHH
    out, prev_hex = '"', False
    for ch in b:
        c = chr(ch)
        if ch >= 0x80 or ch < 0x20:
            out += '\\x%02X' % ch; prev_hex = True; continue
        if prev_hex and c in '0123456789abcdefABCDEF': out += '" "'
        prev_hex = False
        out += '\\"' if c == '"' else '\\\\' if c == '\\' else c
    return out + '"'

# tous les textes du code (morceaux colles compris)
code = ""
for f in os.listdir(src):
    if f.endswith('.c'): code += open(os.path.join(src, f), encoding='latin-1').read()
code = re.sub(r'"\s*\n\s*"', '', code)
code = re.sub(r'"\s*\n\s*\n\s*"', '', code)
bad = 0
spec = re.compile(r'%[-+ #0]*\d*(?:\.\d+)?(?:ll|l|z|h)?[a-zA-Z%]')
for en, fr in TR.items():
    if '"' + en.replace('"', '\\"') + '"' not in code and not en.startswith('Language'):
        print("absent du code :", en[:70]); bad += 1
    if spec.findall(en) != spec.findall(fr.replace('%%', '')) and spec.findall(en) != spec.findall(fr):
        print("% differents :", en[:60]); bad += 1
entries = sorted((c_unescape(en), fr.encode('latin-1')) for en, fr in TR.items())
with open(os.path.join(src, "lang_fr.h"), "w", encoding='latin-1') as o:
    o.write("/* Genere par tools/cb-lang/gen.py (depuis fr.py) - ne pas modifier a la main. */\n")
    o.write("static const char *const tr_fr[][2] = {\n")
    for en, fr in entries: o.write("    { %s, %s },\n" % (c_lit(en), c_lit(fr)))
    o.write("};\n")
print(len(entries), "textes", "(%d problemes)" % bad)
sys.exit(1 if bad else 0)
