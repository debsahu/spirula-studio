"""chamfer.py a=<sibling> b=<sibling> ... : symmetric nearest-neighbour distance between two dense clouds, in scene units and as a fraction of the
scene diameter (10.51 units, basement); whole cloud and steps ROI. P-5 reads p50 against 2e-3 x diameter."""
import sys, json, numpy as np
from scipy.spatial import cKDTree
from cloudlib import *
DIAM = 10.51
paths = sys.argv[1:]
def nn(a, b): return cKDTree(b).query(a)[0]
def rep(Xa, Xb):
    d1, d2 = nn(Xa, Xb), nn(Xb, Xa); d = np.concatenate([d1, d2])
    return dict(p50=float(np.median(d)), p50_over_diam=float(np.median(d) / DIAM), p90=float(np.percentile(d, 90)), p99=float(np.percentile(d, 99)), mean=float(d.mean()), p50_mm=float(np.median(d) * MPU * 1000))
for i in range(len(paths)):
    for j in range(i + 1, len(paths)):
        Xa = read_points(paths[i] + '/points3D.bin')[0]; Xb = read_points(paths[j] + '/points3D.bin')[0]
        ra = R.inside(Xa); rb = R.inside(Xb)
        print(paths[i].split('/')[-1], 'vs', paths[j].split('/')[-1], json.dumps(dict(all=rep(Xa, Xb), roi=rep(Xa[ra], Xb[rb]))))
