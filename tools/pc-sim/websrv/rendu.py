#!/usr/bin/env python3
# v201 : rend un fichier PCSIM_WEBDUMP (JSON) en PNG : rendu.py page.json page.png [y0] [hauteur] [colonnes]
# (polices DejaVu comme la console ; le gras est dessine deux fois, comme sur la console)
import json, sys
from PIL import Image, ImageDraw, ImageFont
D='/usr/share/fonts/truetype/dejavu/'
F={0:ImageFont.truetype(D+'DejaVuSansCondensed.ttf',12),1:ImageFont.truetype(D+'DejaVuSansCondensed.ttf',15),2:ImageFont.truetype(D+'DejaVuSansCondensed-Bold.ttf',22),4:ImageFont.truetype(D+'DejaVuSansMono.ttf',13),5:ImageFont.truetype(D+'DejaVuSansCondensed.ttf',18)}
FB={0:ImageFont.truetype(D+'DejaVuSansCondensed-Bold.ttf',12),1:ImageFont.truetype(D+'DejaVuSansCondensed-Bold.ttf',15),2:F[2]}
FI={0:ImageFont.truetype(D+'DejaVuSansCondensed-Oblique.ttf',12),1:ImageFont.truetype(D+'DejaVuSansCondensed-Oblique.ttf',15),2:F[2],4:F[4],5:ImageFont.truetype(D+'DejaVuSansCondensed-Oblique.ttf',18)}
ASC={0:12,1:14,2:21,4:13,5:17}
d=json.load(open(sys.argv[1]))
y0=int(sys.argv[3]) if len(sys.argv)>3 else 0
H=int(sys.argv[4]) if len(sys.argv)>4 else min(d['h'],3000)
ncol=int(sys.argv[5]) if len(sys.argv)>5 else 1
W=d['w']+12
def col(c): return ((c>>24)&255,(c>>16)&255,(c>>8)&255)
segs=[]
for k in range(ncol):
    sy=y0+k*H
    im=Image.new('RGB',(W,H),col(d['bg']) if d['bg'] else (255,255,255)); dr=ImageDraw.Draw(im)
    for b in d['boxes']:
        x0,by0,x1,by1,bg,bd,sd=b
        if by1<sy or by0>sy+H or by1<=by0 or x1<=x0: continue
        if bg: dr.rectangle([x0+6,by0-sy,x1+6-1,by1-sy-1],fill=col(bg))
        if bd:
            s=sd or 15; c=col(bd)
            if s&1: dr.line([x0+6,by0-sy,x1+5,by0-sy],fill=c)
            if s&4: dr.line([x0+6,by1-sy-1,x1+5,by1-sy-1],fill=c)
            if s&8: dr.line([x0+6,by0-sy,x0+6,by1-sy-1],fill=c)
            if s&2: dr.line([x1+5,by0-sy,x1+5,by1-sy-1],fill=c)
    for it in d['items']:
        x,y,w,h,f,st,fx,c,bg,lk,img,t=it
        if y<sy-40 or y>sy+H+40: continue
        yy=y-sy
        if st&128:
            dr.rectangle([x+6,yy-h,x+6+w-1,yy-1],fill=(220,224,232),outline=(150,160,180)); dr.text((x+9,yy-h+2),t[:30],font=F[0],fill=(90,90,90)); continue
        if st&(16|32):
            dr.rectangle([x+6,yy-ASC[1]-3,x+6+w,yy-ASC[1]-3+24],outline=(110,110,110),fill=(239,239,239) if st&32 else (255,255,255)); dr.text((x+12,yy-ASC[1]),t,font=F[1],fill=(30,30,30)); continue
        fnt=FI[f] if fx&4 else F[f]
        if bg: dr.rectangle([x+5,yy-ASC[f]-1,x+6+w,yy-ASC[f]+{0:15,1:18,2:27,4:16,5:22}[f]],fill=col(bg))
        dr.text((x+6,yy-ASC[f]),t,font=fnt,fill=col(c))
        if fx&2: dr.text((x+7,yy-ASC[f]),t,font=fnt,fill=col(c))
        if fx&1: dr.line([x+6,yy+2,x+6+w,yy+2],fill=col(c))
        if fx&8: dr.line([x+6,yy-ASC[f]//2,x+6+w,yy-ASC[f]//2],fill=col(c))
    segs.append(im)
out=Image.new('RGB',(W*ncol+8*(ncol-1),H),(40,40,40))
for k,im in enumerate(segs): out.paste(im,(k*(W+8),0))
out.save(sys.argv[2])
print(d['title'],d['h'],d['ms'],'ms',len(d['items']),'items')
