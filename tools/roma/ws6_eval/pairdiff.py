import sys, numpy as np, struct
def rd(p):
    f=open(p,'rb'); f.read(4); w,h,e=struct.unpack('<iii',f.read(12)); n=w*h
    if e==0: wp=np.frombuffer(f.read(8*n),'<f4').reshape(h,w,2).astype(float); c=np.frombuffer(f.read(4*n),'<f4').reshape(h,w).astype(float)
    else: wp=np.frombuffer(f.read(4*n),'<i2').reshape(h,w,2)/32767.; c=np.frombuffer(f.read(2*n),'<u2').reshape(h,w)/65535.
    return wp,c
n=sys.argv[1]
S={'native':'native_m5_sub','mpsM5':'floor_mps','dumpM4':'../ws4/basement_export/matches','cpu':'cpu_out'}
D={k:rd(f'{v}/{n}') for k,v in S.items() if __import__('os').path.exists(f'{v}/{n}')}
ref=D['cpu'][1]>0.5
print(n,'cpu cert>0.5 frac %.3f'%ref.mean())
ks=list(D)
for i,a in enumerate(ks):
    for b in ks[i+1:]:
        e=np.linalg.norm(D[a][0]-D[b][0],axis=-1)*320; m=ref
        print(f'  {a:7s} vs {b:7s} c05(cpu) epe p50 {np.median(e[m]):7.3f} p90 {np.percentile(e[m],90):7.3f}')
