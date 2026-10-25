#!/usr/bin/env python3
# Pages de test generees a la demande (pas gardees dans le depot) : long.html et huge.html (page.html
# repetee), et trois sites mkdocs (themes mkdocs, readthedocs, material) si mkdocs est installe.
import os, re, subprocess, sys
D = os.path.dirname(os.path.abspath(__file__))
S = os.path.join(D, 'site')
h = open(os.path.join(S, 'page.html')).read()
m = re.search(r'<main>(.*)</main>', h, re.S).group(1)
for name, k in (('long.html', 25), ('huge.html', 75)):
    p = os.path.join(S, name)
    if not os.path.exists(p): open(p, 'w').write(h.replace(m, m * k))
mk = os.path.join(D, 'mk')
for th in ('mkdocs', 'readthedocs', 'material'):
    out = os.path.join(S, th)
    if os.path.exists(os.path.join(out, 'index.html')): continue
    open(os.path.join(mk, 'mkdocs.yml'), 'w').write('site_name: Wiki GameCube\ntheme:\n  name: %s\nmarkdown_extensions:\n  - tables\n  - admonition\n' % th)
    r = subprocess.run([sys.executable, '-m', 'mkdocs', 'build', '-q', '-d', out], cwd=mk, capture_output=True)
    if r.returncode: print('mkdocs (%s) : pas genere' % th)
