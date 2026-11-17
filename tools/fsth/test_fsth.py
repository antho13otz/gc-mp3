import os,sys,struct,random,subprocess
sys.path.insert(0,'/root/gc-station/tools/natif-test/work/unicorn/bindings/python')
os.environ.setdefault('LIBUNICORN_PATH','/root/gc-station/tools/natif-test/work/unicorn/build')
from unicorn import *; from unicorn.ppc_const import *
random.seed(1)
# FST synthétique : racine, ~60 dossiers dont EVENT, ~2700 fichiers, noms en majuscules/minuscules
names=[]; ents=[]  # (isdir, name, parent, a, b)
def add(isdir,name,parent): ents.append([isdir,name,parent,0,0]); return len(ents)
root=[1,'',0,0,0]; ents.append(root)
def mkdir(parent,name,depth):
    i=len(ents); ents.append([1,name,parent,parent,0])
    for k in range(random.randint(20,50)):
        if depth<2 and random.random()<0.01: mkdir(i,'D%d_%d'%(depth,k),depth+1)
        else: ents.append([0,'file%03d_%s.%s'%(k,random.choice(['prs','gvr','PRS','adx']),random.choice(['','x'])),i,0,0])
    ents[i][4]=len(ents)
for k in range(15): ents.append([0,'TOP%02d.bin'%k,0,0,0])
mkdir(0,'EVENT',0)
for k in range(55): mkdir(0,"dir%02d"%k,0)
while len(ents)<2821: ents.append([0,'pad%04d.prs'%len(ents),0,0,0])
root[4]=len(ents)
n=len(ents); strs=b''; offs=[]
for e in ents: offs.append(len(strs)); strs+=e[1].encode()+b'\0'
fst=b''
for i,e in enumerate(ents):
    if i==0: fst+=struct.pack('>III',0x01000000,0,n)
    elif e[0]: fst+=struct.pack('>III',0x01000000|offs[i],e[3],e[4])
    else: fst+=struct.pack('>III',offs[i],0x1000*i,100+i)
fst+=strs; open('fst.bin','wb').write(fst)
CUR=-30056
out=subprocess.run(['./build','fst.bin',str(CUR)],capture_output=True)
print(out.stderr.decode().strip())
blk=out.stdout
n12=n*12; bsz=struct.unpack('>I',blk[n12+8:n12+12])[0]*8+20+(n-1)*4
mem=blk[:n12+bsz]
# chemin complet de chaque entrée
def path(i):
    p=[]
    while i: p.append(ents[i][1]); i=ents[i][2]
    return '/'.join(reversed(p))
mu=Uc(UC_ARCH_PPC,UC_MODE_PPC32|UC_MODE_BIG_ENDIAN)
mu.mem_map(0x80000000,0x01800000)
code=open('/tmp/fsth/fsth.bin','rb').read()
FST=0x817E0000; R13=0x803B52C0; CODE=0x817F0000; STR=0x80100000; STK=0x80200000
mu.mem_write(FST,mem); mu.mem_write(0x80000038,struct.pack('>I',FST)); mu.mem_write(CODE,code)
def call(p,cur=0):
    mu.mem_write(R13+CUR,struct.pack('>I',cur)); mu.mem_write(STR,p.encode()+b'\0')
    mu.reg_write(UC_PPC_REG_3,STR); mu.reg_write(UC_PPC_REG_13,R13); mu.reg_write(UC_PPC_REG_1,STK)
    mu.reg_write(UC_PPC_REG_LR,0x80000100); mu.emu_start(CODE,0x80000100,count=200000)
    r=mu.reg_read(UC_PPC_REG_3); return r-(1<<32) if r&0x80000000 else r
bad=0
for i in range(1,n):
    for p in (path(i), '/'+path(i), path(i).upper()):
        if call(p)!=i: bad+=1; print('FAUX',p,call(p),i) if bad<5 else None
ev=next(i for i,e in enumerate(ents) if e[1]=='EVENT')
f=next(i for i,e in enumerate(ents) if e[2]==ev and not e[0])
tests=[(ents[f][1],ev,f),('../TOP03.bin',ev,next(i for i,e in enumerate(ents) if e[1]=='TOP03.bin')),('..',ev,0),('',ev,ev),('EVENT',0,ev),('nexiste_pas.prs',0,-1),('EVENT/nexiste.prs',0,-1),('./TOP01.bin',0,next(i for i,e in enumerate(ents) if e[1]=='TOP01.bin')),('TOP01.bin/x',0,-1)]
for p,cur,exp in tests:
    r=call(p,cur); print('%-22s cur=%4d -> %5d attendu %5d %s'%(p,cur,r,exp,'OK' if r==exp else 'FAUX'))
print('entrees',n,'erreurs',bad,'taille bloc',bsz,'routine',len(code))
