"""chamfer.py a=<sibling> b=<sibling> ... : symmetric nearest-neighbour distance between two dense clouds, in scene units and as a fraction of the
scene diameter (10.51 units, basement); whole cloud and steps ROI. P-5 reads p50 against 2e-3 x diameter.
A bare <sibling> (no label=) is accepted; each sibling dir holds points3D.bin."""
import json, numpy as np
from scipy.spatial import cKDTree
import evalcfg, cloudlib as CL

ap = evalcfg.parser(__doc__)
evalcfg.add_spike(ap)
ap.add_argument('--diam', type=float, default=10.51, help='scene diameter in units (default 10.51, basement)')
ap.add_argument('siblings', nargs='+', metavar='SIBLING', help='dir with points3D.bin (optionally label=dir)')
a = evalcfg.parse(ap)
R = CL.init(a)
paths = [s.split('=', 1)[-1] for s in a.siblings]
for p in paths: evalcfg.check_path(p + '/points3D.bin', 'positional <sibling>', 'file', 'sibling points3D.bin')
DIAM = a.diam; MPU = CL.MPU
def nn(x, y): return cKDTree(y).query(x)[0]
def rep(Xa, Xb):
    d1, d2 = nn(Xa, Xb), nn(Xb, Xa); d = np.concatenate([d1, d2])
    return dict(p50=float(np.median(d)), p50_over_diam=float(np.median(d) / DIAM), p90=float(np.percentile(d, 90)), p99=float(np.percentile(d, 99)), mean=float(d.mean()), p50_mm=float(np.median(d) * MPU * 1000))
for i in range(len(paths)):
    for j in range(i + 1, len(paths)):
        Xa = CL.read_points(paths[i] + '/points3D.bin')[0]; Xb = CL.read_points(paths[j] + '/points3D.bin')[0]
        ra = R.inside(Xa); rb = R.inside(Xb)
        print(paths[i].rstrip('/').split('/')[-1], 'vs', paths[j].rstrip('/').split('/')[-1], json.dumps(dict(all=rep(Xa, Xb), roi=rep(Xa[ra], Xb[rb]))))
