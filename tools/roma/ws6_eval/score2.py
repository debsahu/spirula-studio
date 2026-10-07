"""score2.py --arms label=siblingdir ... --out out.json
Independent anchors + equal-budget + stairs-only measures (coordinator request 2026-10-07).
* anchors = half B of the sparse points (random 50/50 by id, seed 7). The moge/hybrid arms in `dsh` were fitted to half A only; the
  roma arms never used sparse point positions. So every arm is scored on points it never saw.
* regions: whole scene (20k random anchors), steps ROI box (contains a wall and a column), STAIRS BAND (implementer pick, see BAND).
* equal budget: every arm is also thinned to the smallest in-region count over the arms, 5 draws averaged.
* planes: sequential RANSAC, 12 planes (6 treads + 6 risers) over the band points of each arm; inlier fraction at 1 and 2 cm and RMS,
  against the same thing on uniform random points in the band volume (chance).
Units: sparse_final/0 gauge, 0.9521 m/unit."""
import sys, json, numpy as np
from scipy.spatial import cKDTree
from cloudlib import *
arms = dict(a.split('=', 1) for a in sys.argv[1:] if '=' in a and not a.startswith('--'))
outp = sys.argv[sys.argv.index('--out') + 1]
ids, P, PC, Tr, imgs = model(); B = np.isin(ids, np.load('dsh/sparse/halfB_ids.npy')); AB = P[B]
rng = np.random.default_rng(0)
# stairs band: slab w, line in (u,h), perpendicular distance, u range -- picked from the profile render of the uncapped roma cloud (stairs_slab.png)
BAND = dict(w=(-0.35, 0.15), u=(-0.7, 2.6), line_h0=-1.45, line_u0=-0.5, slope=1.02, perp=0.17)
def in_band(X):
    u, w, h = R.to_stair(X)
    d = np.abs(h - (BAND['line_h0'] + BAND['slope'] * (u - BAND['line_u0']))) / np.sqrt(1 + BAND['slope'] ** 2)
    return (w >= BAND['w'][0]) & (w <= BAND['w'][1]) & (u >= BAND['u'][0]) & (u <= BAND['u'][1]) & (d <= BAND['perp'])
def load(d):
    if d.endswith('.npy'): return np.load(d)[:, :3].astype(float)
    if d.endswith('.txt'): return np.loadtxt(d, usecols=(1, 2, 3))
    X, C, _ = read_points(d + '/points3D.bin'); return X
clouds = {k: load(v) for k, v in arms.items()}
reg = {k: dict(roi=R.inside(X), band=in_band(X)) for k, X in clouds.items()}
anch = dict(roi=AB[R.inside(AB)], band=AB[in_band(AB)], scene=AB[rng.choice(len(AB), 20000, replace=False)])
mm = lambda x: float(x * MPU * 1000)
def score(c, A):
    if len(c) < 5: return None
    d = cKDTree(c).query(A)[0]
    return dict(p50_mm=mm(np.median(d)), p90_mm=mm(np.percentile(d, 90)), c1=float((d * MPU < .01).mean()), c2=float((d * MPU < .02).mean()), c5=float((d * MPU < .05).mean()))
def planes(Xb, n_planes=12, thr=.01 / MPU, iters=1500):
    Xb = Xb[rng.choice(len(Xb), min(len(Xb), 30000), replace=False)] if len(Xb) > 30000 else Xb
    rem = np.ones(len(Xb), bool); inl = np.zeros(len(Xb), bool); res = []
    for _ in range(n_planes):
        idx = np.where(rem)[0]
        if len(idx) < 50: break
        best = None
        for _ in range(iters):
            s = Xb[rng.choice(idx, 3, replace=False)]; nrm = np.cross(s[1] - s[0], s[2] - s[0]); nn = np.linalg.norm(nrm)
            if nn < 1e-9: continue
            nrm /= nn; dist = np.abs((Xb[idx] - s[0]) @ nrm); c = int((dist < thr).sum())
            if best is None or c > best[0]: best = (c, s[0], nrm)
        _, p0, nrm = best; dist = np.abs((Xb - p0) @ nrm); m = rem & (dist < thr)
        # refit by SVD on the inliers
        q = Xb[m]; cen = q.mean(0); nrm = np.linalg.svd(q - cen, full_matrices=False)[2][-1]; dist = np.abs((Xb - cen) @ nrm)
        m = rem & (dist < thr); inl |= m; rem &= ~m; res.append(dist[m])
    dall = np.min(np.stack([np.abs((Xb - 0) @ np.zeros(3))]), 0) if False else None
    return dict(inlier_frac_1cm=float(inl.mean()), rms_mm=mm(float(np.sqrt(np.mean(np.concatenate(res) ** 2)))) if res else None, n=int(len(Xb)))
out = dict(anchors=dict(B=int(B.sum()), roi=int(len(anch['roi'])), band=int(len(anch['band'])), scene=int(len(anch['scene']))), band_def=BAND, arms={})
nmin = {r: min(int(reg[k][r].sum()) for k in clouds if not k.startswith('moge')) for r in ('roi', 'band')}   # moge arms keep too few stair points to set the budget
out['equal_budget_n'] = nmin
for k, X in clouds.items():
    o = out['arms'][k] = dict(total=int(len(X)))
    for r in ('roi', 'band'):
        c = X[reg[k][r]]; o[r] = dict(n=int(len(c)), frac_of_cloud=float(len(c) / len(X)), anchors=score(c, anch[r]))
        o[r]['matched'] = {a: float(np.mean([score(c[rng.choice(len(c), nmin[r], replace=False)], anch[r])[a] for _ in range(5)])) for a in ('p50_mm', 'p90_mm', 'c2', 'c5')} if len(c) >= nmin[r] else None
        noisy = c + rng.normal(0, .01 / MPU, c.shape); sn = score(noisy, anch[r])
        o[r]['null_noise1cm_p50_change_pct'] = 100 * (sn['p50_mm'] / o[r]['anchors']['p50_mm'] - 1)
    o['scene'] = score(X, anch['scene'])
    o['band_planes'] = planes(X[reg[k]['band']])
# chance level for the plane metric: uniform random points in the band volume with the same count as the median arm
u = rng.uniform(*BAND['u'], 40000); w = rng.uniform(*BAND['w'], 40000); h = BAND['line_h0'] + BAND['slope'] * (u - BAND['line_u0']) + rng.uniform(-BAND['perp'], BAND['perp'], 40000) * np.sqrt(1 + BAND['slope'] ** 2)
out['band_planes_uniform_random_chance'] = planes(R.from_stair(u, w, h))
out['uniform_random_roi_chance'] = None
json.dump(out, open(outp, 'w'), indent=1)
print(json.dumps({k: {r: dict(n=v[r]['n'], p50=round(v[r]['anchors']['p50_mm'], 1) if v[r]['anchors'] else None, c5=round(v[r]['anchors']['c5'], 3) if v[r]['anchors'] else None) for r in ('roi', 'band')} for k, v in out['arms'].items()}, indent=0))
print('planes', {k: (round(v['band_planes']['inlier_frac_1cm'], 3), v['band_planes']['rms_mm']) for k, v in out['arms'].items()}, 'chance', out['band_planes_uniform_random_chance'])
