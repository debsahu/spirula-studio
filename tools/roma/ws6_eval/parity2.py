"""parity2.py <native_dir> <torch_dir> <model_dir> <out.json>
Pose-aware stratification of the match parity. For every pair: Sampson error against the COLMAP poses for BOTH arms
(independent of either matcher), then native-vs-torch EPE only on pixels the torch arm itself places on the epipolar
line (Sampson < 1 px at 640, cert > 0.5 in both): the pixels densify can use. Also the inlier rate of each arm."""
import glob, os, json, numpy as np
import evalcfg
from epi import poses, rd, view, FACE
ap = evalcfg.parser(__doc__)
ap.add_argument('native', help='dir of native .rwm'); ap.add_argument('torch', help='dir of torch-side .rwm')
ap.add_argument('model', help='COLMAP model dir (images.bin) for the poses'); ap.add_argument('out', help='output json')
a = evalcfg.parse(ap)
nat, tor, mdl, outp = a.native, a.torch, a.model, a.out
evalcfg.check_outdir(outp, 'positional <out.json>')
evalcfg.check_path(nat, 'positional <native_dir>', 'dir', 'native match dir'); evalcfg.check_path(tor, 'positional <torch_dir>', 'dir', 'torch match dir')
evalcfg.check_path(mdl + '/images.bin', 'positional <model_dir>', 'file', 'poses model images.bin')
P = poses(mdl)
names = sorted(os.path.basename(p) for p in glob.glob(nat + '/*.rwm') if os.path.exists(tor + '/' + os.path.basename(p)))
if not names: evalcfg.die(f'no common .rwm names between {nat} and {tor}')
def pct(x, q): return float(np.percentile(x, q)) if len(x) else float('nan')
def samp_full(Pz, pair, wp, W=640):
    a, b = pair[:-4].split('__'); Ra, ta = view(Pz, a); Rb, tb = view(Pz, b)
    Rr = Rb@Ra.T; tr = tb - Rr@ta; tx = np.array([[0,-tr[2],tr[1]],[tr[2],0,-tr[0]],[-tr[1],tr[0],0]]); E = tx@Rr
    ys, xs = np.mgrid[0:W, 0:W]
    xa = np.stack([(xs+.5-W/2)/(W/2), (ys+.5-W/2)/(W/2), np.ones((W, W))], -1).reshape(-1, 3)
    xb = np.concatenate([wp.reshape(-1, 2), np.ones((W*W, 1))], 1)
    Ex = xa@E.T; Etx = xb@E
    num = np.einsum('ij,ij->i', xb, Ex)**2; den = Ex[:, 0]**2+Ex[:, 1]**2+Etx[:, 0]**2+Etx[:, 1]**2
    return (np.sqrt(num/np.maximum(den, 1e-30))*(W/2)).reshape(W, W)
rows = []; pool_epe = []; pool_epe_by_thr = {1: [], 5: []}
for n in names:
    wn, cn, _ = [*rd(nat + '/' + n), 0]; wt, ct, _ = [*rd(tor + '/' + n), 0]
    sn = samp_full(P, n, wn); st = samp_full(P, n, wt)
    epe = np.linalg.norm(wn - wt, axis=-1)*320
    mt = ct > .5; mn = cn > .5
    r = dict(pair=n[:-4], n_certain_t=int(mt.sum()), n_certain_n=int(mn.sum()),
             samp_med_t=pct(st[mt], 50), samp_med_n=pct(sn[mn], 50),
             inl1_t=float((st[mt] < 1).mean()) if mt.any() else float('nan'), inl1_n=float((sn[mn] < 1).mean()) if mn.any() else float('nan'))
    for thr in (1, 5):
        v = mt & mn & (st < thr)
        r[f'n_valid{thr}'] = int(v.sum()); r[f'epe_valid{thr}_p50'] = pct(epe[v], 50); r[f'epe_valid{thr}_p99'] = pct(epe[v], 99)
        pool_epe_by_thr[thr].append(epe[v])
    rows.append(r)
out = dict(n_pairs=len(rows), rows=rows)
for thr in (1, 5):
    e = np.concatenate(pool_epe_by_thr[thr]); out[f'pooled_valid{thr}'] = dict(n=int(len(e)), p50=pct(e, 50), p90=pct(e, 90), p99=pct(e, 99), p999=pct(e, 99.9), max=float(e.max()), frac_gt_0p1=float((e > .1).mean()), frac_gt_0p5=float((e > .5).mean()), frac_gt_1=float((e > 1).mean()))
A = lambda k: np.array([r[k] for r in rows], float)
for k in ('epe_valid1_p50', 'epe_valid1_p99', 'samp_med_t', 'samp_med_n', 'inl1_t', 'inl1_n'):
    a = A(k); a = a[np.isfinite(a)]; out['dist_' + k] = dict(p05=pct(a, 5), p50=pct(a, 50), p95=pct(a, 95), max=float(a.max()), min=float(a.min()))
valid_pairs = [r for r in rows if r['inl1_t'] > .5]
out['pairs_geometrically_valid_torch_inl1_gt_0p5'] = len(valid_pairs)
out['pairs_invalid'] = [r['pair'] for r in rows if not r['inl1_t'] > .5]
json.dump(out, open(outp, 'w'), indent=1)
print(json.dumps({k: v for k, v in out.items() if k != 'rows'}, indent=1))
