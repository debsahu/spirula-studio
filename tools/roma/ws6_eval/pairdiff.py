"""pairdiff.py <pair.rwm> label=dir ... --ref LABEL : pairwise EPE (px at 640) between the match sets that hold <pair>, over the pixels where
the --ref set's certainty > 0.5. Labels used in the WS-6 note: native, mpsM5, dumpM4, cpu (cpu is the reference)."""
import os, numpy as np
import evalcfg
from epi import rd
ap = evalcfg.parser(__doc__)
ap.add_argument('pair', help='file name, e.g. frame_0001_back__frame_0002_back.rwm')
ap.add_argument('sets', nargs='+', metavar='label=dir')
ap.add_argument('--ref', default='cpu', help='label of the reference set (default cpu)')
a = evalcfg.parse(ap)
n = a.pair
S = dict(s.split('=', 1) for s in a.sets)
if a.ref not in S: evalcfg.die(f'--ref {a.ref} is not among the given labels {list(S)}')
for k, v in S.items(): evalcfg.check_path(v, f'label {k}=<dir>', 'dir', f'match dir for {k}')
D = {k: rd(f'{v}/{n}') for k, v in S.items() if os.path.exists(f'{v}/{n}')}
if a.ref not in D: evalcfg.die(f'{n} not found in the --ref set {S[a.ref]}')
ref = D[a.ref][1] > 0.5
print(n, f'{a.ref} cert>0.5 frac %.3f' % ref.mean())
ks = list(D)
for i, x in enumerate(ks):
    for b in ks[i+1:]:
        e = np.linalg.norm(D[x][0] - D[b][0], axis=-1) * 320; m = ref
        print(f'  {x:7s} vs {b:7s} c05({a.ref}) epe p50 {np.median(e[m]):7.3f} p90 {np.percentile(e[m],90):7.3f}')
