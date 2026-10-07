"""epi.py <model dir> <pair.rwm name> <label=dir> ... : Sampson epipolar error (px at 640) of certain warp pixels against the COLMAP poses.
An arm-independent judge: poses come from SfM, not from either matcher."""
import sys, struct, numpy as np, os
AX = np.array([[[1,0,0],[0,1,0],[0,0,1]],[[0,0,-1],[0,1,0],[1,0,0]],[[-1,0,0],[0,0,1],[0,1,0]],
               [[0,0,1],[0,1,0],[-1,0,0]],[[1,0,0],[0,0,1],[0,-1,0]],[[-1,0,0],[0,1,0],[0,0,-1]]], float)
FACE = ['front','right','down','left','up','back']
def poses(p):
    b = open(p+'/images.bin','rb').read(); n = struct.unpack_from('<Q',b,0)[0]; o = 8; out = {}
    for _ in range(n):
        iid, = struct.unpack_from('<I',b,o); q = struct.unpack_from('<4d',b,o+4); t = struct.unpack_from('<3d',b,o+36); o += 4+56+4
        e = b.index(b'\0',o); name = b[o:e].decode(); o = e+1; k, = struct.unpack_from('<Q',b,o); o += 8+24*k
        w,x,y,z = q
        R = np.array([[1-2*(y*y+z*z),2*(x*y-w*z),2*(x*z+w*y)],[2*(x*y+w*z),1-2*(x*x+z*z),2*(y*z-w*x)],[2*(x*z-w*y),2*(y*z+w*x),1-2*(x*x+y*y)]])
        out[name.rsplit('.',1)[0]] = (R, np.array(t))
    return out
def rd(p):
    f=open(p,'rb'); f.read(4); w,h,e=struct.unpack('<iii',f.read(12)); n=w*h
    if e==0: wp=np.frombuffer(f.read(8*n),'<f4').reshape(h,w,2).astype(float); c=np.frombuffer(f.read(4*n),'<f4').reshape(h,w).astype(float)
    else: wp=np.frombuffer(f.read(4*n),'<i2').reshape(h,w,2)/32767.; c=np.frombuffer(f.read(2*n),'<u2').reshape(h,w)/65535.
    return wp,c
def view(P, name):
    s, f = name.rsplit('_',1); R, t = P[s]; F = AX[FACE.index(f)]; return F@R, F@t
def sampson(P, pair, wp, cert, thr=0.5, W=640):
    a, b = pair[:-4].split('__'); Ra, ta = view(P,a); Rb, tb = view(P,b)
    Rr = Rb@Ra.T; tr = tb - Rr@ta; tx = np.array([[0,-tr[2],tr[1]],[tr[2],0,-tr[0]],[-tr[1],tr[0],0]]); E = tx@Rr
    ys, xs = np.mgrid[0:W,0:W]; m = cert > thr
    xa = np.stack([(xs[m]+.5-W/2)/(W/2),(ys[m]+.5-W/2)/(W/2),np.ones(m.sum())],1)
    xb = np.stack([wp[m][:,0],wp[m][:,1],np.ones(m.sum())],1)   # warp is already [-1,1] over B
    Ex = xa@E.T; Etx = xb@E
    num = np.einsum('ij,ij->i',xb,Ex)**2; den = Ex[:,0]**2+Ex[:,1]**2+Etx[:,0]**2+Etx[:,1]**2
    return np.sqrt(num/np.maximum(den,1e-30))*(W/2)
def main():
    import evalcfg
    ap = evalcfg.parser(__doc__)
    ap.add_argument('model', help='COLMAP model dir (images.bin) giving the poses')
    ap.add_argument('pair', help='pair file name, e.g. A__B.rwm')
    ap.add_argument('arms', nargs='+', help='label=dir holding the .rwm files')
    a = evalcfg.parse(ap)
    evalcfg.check_path(a.model + '/images.bin', 'positional <model>', 'file', 'poses model images.bin')
    P = poses(a.model)
    for lab_dir in a.arms:
        lab, d = lab_dir.split('=')
        evalcfg.check_path(f'{d}/{a.pair}', f'arm {lab}=<dir>', 'file', 'match file')
        wp, c = rd(f'{d}/{a.pair}'); s = sampson(P, a.pair, wp, c)
        print(f'{lab:8s} n={len(s):7d} Sampson px p50 {np.median(s):.3f} p90 {np.percentile(s,90):.3f} p99 {np.percentile(s,99):.3f}')
if __name__ == '__main__':
    main()
