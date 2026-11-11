import re,sys
src=open('cliff12.py').read()
src=src.split("rock = CF.rock_cubes")[0]
exec(compile(src,'c','exec'))
import numpy as np
CF.max_stretch=99
rock=CF.rock_cubes((0,W),(0,H))
from cliffs import cube_stretch
rows=[]
for (x,y),(k,uv) in rock.items():
    if x<130 or y>180 or y<85: continue
    rows.append((x,y,k,cube_stretch(CF.height,x,y,uv)))
r=np.array([(a,b,c) for a,b,k,c in rows if k=='body'])
print(len(r),np.percentile(r[:,2],[10,50,75,90,95,99]))
# left ribbon region: x 135-150, y 125-170 vs right
for nm,(x0,x1,y0,y1) in {'A-left':(133,150,125,170),'B-upper':(145,185,115,140)}.items():
    s=r[(r[:,0]>=x0)&(r[:,0]<x1)&(r[:,1]>=y0)&(r[:,1]<y1)][:,2]
    print(nm,len(s),np.percentile(s,[10,50,90]).round(2))
