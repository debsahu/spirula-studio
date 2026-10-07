import numpy as np, json, pickle
from PIL import Image, ImageDraw
import sys, struct, os
sys.path.insert(0,'/private/tmp/claude-502/-Users-debsahu-Workspace-slam/8bf7354b-69a0-4115-87fb-3a148414b12e/scratchpad/rv2spike')
import roi as R
OUT=sys.argv[1]; ARMS_IN=dict(a.split('=',1) for a in sys.argv[2:])
J=json.load(open('/private/tmp/claude-502/-Users-debsahu-Workspace-slam/8bf7354b-69a0-4115-87fb-3a148414b12e/scratchpad/rv2spike/steps_roi.json')); R.ROI.update(dict(u=J['u'],w=J['w'],h=J['h']))
ids,P,C,T,imgs=pickle.load(open('/private/tmp/claude-502/-Users-debsahu-Workspace-slam/8bf7354b-69a0-4115-87fb-3a148414b12e/scratchpad/rv2spike/model.pkl','rb'))
def rd_bin(p):
    b=open(p,'rb').read(); n=struct.unpack_from('<Q',b,0)[0]; o=8; X=np.empty((n,3)); Cc=np.empty((n,3),np.uint8)
    for i in range(n):
        o+=8; X[i]=struct.unpack_from('<3d',b,o); o+=24; Cc[i]=np.frombuffer(b[o:o+3],np.uint8); o+=3+8; tl=struct.unpack_from('<Q',b,o)[0]; o+=8+8*tl
    return X,Cc
def L(name):
    p=ARMS_IN[name]
    if p.endswith('.bin'): return rd_bin(p)
    if p.endswith('.npz'): z=np.load(p); return z['xyz'].astype(np.float64),(z['rgb']*255).clip(0,255).astype(np.uint8)
    if p.endswith('.npy'): d=np.load(p); return d[:,:3].astype(np.float64), d[:,3:6].astype(np.uint8)
    if p=='sparse': return P.astype(np.float64), np.array(C,np.uint8)
    d=np.loadtxt(p,usecols=(1,2,3,4,5,6)); return d[:,:3], d[:,3:6].astype(np.uint8)
ARMS=list(ARMS_IN)
LABEL={a:a for a in ARMS}
# stair-frame coordinates
def stair(X):
    u,w,h=R.to_stair(X); return np.stack([u,w,h],1)
W,H=900,700
def splat(px,depth,col,rad):
    img=np.full((H,W,3),255,np.uint8); zb=np.full((H,W),np.inf)
    order=np.argsort(-depth)   # far first, near overwrite
    px=px[order]; col=col[order]; depth=depth[order]
    for dy in range(-rad,rad+1):
        for dx in range(-rad,rad+1):
            if dx*dx+dy*dy>rad*rad: continue
            x=px[:,0]+dx; y=px[:,1]+dy; ok=(x>=0)&(x<W)&(y>=0)&(y<H)
            x,y,c,d=x[ok],y[ok],col[ok],depth[ok]
            # painter's algorithm in far->near order: later (nearer) writes win
            img[y,x]=c
    return img
VIEWS={
 'oblique':dict(kind='persp',eye=[-1.15,0.42,0.75],look=[1.6,-0.15,-0.35],fov=70),
 'profile':dict(kind='ortho_uh',wslab=[-0.35,0.15],rng_u=[-0.8,3.3],rng_h=[-1.6,2.1]),
 'topdown':dict(kind='ortho_uw',hmax=1.35,rng_u=[-0.8,3.3],rng_w=[-0.75,0.7]),
}
def render(X,col,v):
    S=stair(X); inb=R.inside(X); S=S[inb]; col=col[inb]
    if v['kind']=='persp':
        e=np.array(v['eye']); f=np.array(v['look'])-e; f/=np.linalg.norm(f)
        up=np.array([0,0,1.0]); r=np.cross(f,up); r/=np.linalg.norm(r); u=np.cross(r,f)
        Q=S-e; z=Q@f; ok=z>0.05; Q,z,c=Q[ok],z[ok],col[ok]
        fpx=(W/2)/np.tan(np.radians(v['fov']/2))
        px=np.stack([W/2+fpx*(Q@r)/z, H/2-fpx*(Q@u)/z],1).round().astype(int)
        return splat(px,z,c,2)
    if v['kind']=='ortho_uh':
        m=(S[:,1]>v['wslab'][0])&(S[:,1]<v['wslab'][1]); S,c=S[m],col[m]
        su=W/np.diff(v['rng_u'])[0]; sh=H/np.diff(v['rng_h'])[0]; s=min(su,sh)
        px=np.stack([(S[:,0]-v['rng_u'][0])*s, H-(S[:,2]-v['rng_h'][0])*s],1).round().astype(int)
        return splat(px,S[:,1],c,1)
    if v['kind']=='ortho_uw':
        m=S[:,2]<v['hmax']; S,c=S[m],col[m]
        s=W/np.diff(v['rng_u'])[0]
        px=np.stack([(S[:,0]-v['rng_u'][0])*s, H/2-(S[:,1]-np.mean(v['rng_w']))*s],1).round().astype(int)
        return splat(px,-S[:,2],c,1)

import random, hashlib
random.seed(20261007)
codes={a:''.join(random.choice('abcdefghjkmnpqrstuvwxyz') for _ in range(5)) for a in ARMS}
os.makedirs(OUT,exist_ok=True)
stats={}; lo,hi=np.percentile(P,0.5,axis=0)-0.5,np.percentile(P,99.5,axis=0)+0.5
for a in ARMS:
    X,col=L(a); stats[a]=dict(n=int(len(X)),n_roi=int(R.inside(X).sum()))
    for vn,v in VIEWS.items():
        Image.fromarray(render(X,col,v)).save(f'{OUT}/{vn}_{codes[a]}.jpg',quality=90)
json.dump(dict(codes=codes,stats=stats),open(f'{OUT}/MAPPING_do_not_open_before_ranking.json','w'),indent=1)
print('blind renders written', {vn:[f'{vn}_{codes[a]}.jpg' for a in ARMS] for vn in VIEWS})
