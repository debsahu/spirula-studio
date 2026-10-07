"""outside.py <sibling dir> <out.json> [--render dir]: classify the points outside the sparse model's p0.5-p99.5 box + 0.5 m.
Needs <sibling>/points3D.bin AND <sibling>/points3D_tracks.bin. Writes <out.json> and <out>.npz."""
import json, numpy as np
from scipy.spatial import cKDTree
import evalcfg, cloudlib as CL
ap = evalcfg.parser(__doc__)
evalcfg.add_spike(ap)
ap.add_argument('sibling'); ap.add_argument('out')
ap.add_argument('--render', default=None, help='dir for renders (accepted for compatibility; render_outside.py makes the image)')
a = evalcfg.parse(ap)
sib, outp = a.sibling, a.out
evalcfg.check_outdir(outp, 'positional <out.json>')
for f in ('points3D.bin', 'points3D_tracks.bin'): evalcfg.check_path(f'{sib}/{f}', 'positional <sibling>', 'file', f'sibling {f}')
R = CL.init(a); MPU = CL.MPU
X, C, E = CL.read_points(sib + '/points3D.bin'); T = CL.read_tracks(sib + '/points3D_tracks.bin')
ids, P, PC, Tr, imgs = CL.model()
M = CL.da360()[:, :3].astype(float)
lo, hi = np.percentile(P, .5, 0) - .5 / MPU, np.percentile(P, 99.5, 0) + .5 / MPU
out = ((X < lo) | (X > hi)).any(1); idx = np.where(out)[0]
res = dict(n=len(X), n_out=int(out.sum()), frac_out=float(out.mean()))
# how far outside the box, in metres
d_out = np.maximum(np.maximum(lo - X, X - hi), 0).max(1) * MPU
res['dist_outside_box_m'] = {q: float(np.percentile(d_out[idx], q)) for q in (10, 50, 90, 99)}
ntrack = np.array([len(set(t['id'].tolist())) for t in T]); ref = np.array([int(t['id'][0]) for t in T])
res['track_len'] = dict(inside_mean=float(ntrack[~out].mean()), outside_mean=float(ntrack[out].mean()),
                        outside_hist={int(k): int(v) for k, v in zip(*np.unique(ntrack[out], return_counts=True))})
res['err_px_med'] = dict(inside=float(np.median(E[~out])), outside=float(np.median(E[out])))
cen = np.array([imgs[i]['c'] for i in sorted(imgs)]); ctree = cKDTree(cen)
rng_cam = ctree.query(X[idx])[0] * MPU
res['dist_to_nearest_camera_m'] = {q: float(np.percentile(rng_cam, q)) for q in (10, 50, 90)}
res['dist_to_sparse_m'] = {q: float(np.percentile(cKDTree(P).query(X[idx])[0] * MPU, q)) for q in (10, 50, 90)}
res['dist_to_da360_m'] = {q: float(np.percentile(cKDTree(M).query(X[idx])[0] * MPU, q)) for q in (10, 50, 90)}
# local density: neighbours within 10 cm among all R points, outside vs inside (inside subsample)
tree = cKDTree(X); r10 = .10 / MPU
nb_out = np.array([len(x) - 1 for x in tree.query_ball_point(X[idx], r10)])
sub = np.random.default_rng(0).choice(np.where(~out)[0], 20000, replace=False)
nb_in = np.array([len(x) - 1 for x in tree.query_ball_point(X[sub], r10)])
res['neighbours_within_10cm'] = dict(outside_median=float(np.median(nb_out)), inside_median=float(np.median(nb_in)), outside_isolated_le2=float((nb_out <= 2).mean()),
                                    inside_isolated_le2=float((nb_in <= 2).mean()))
# behind a surface: from the reference camera of each point, the ray to X crosses a DA360 or sparse surface (within 3 cm) before X
solid = P; stree = cKDTree(solid); rad = .03 / MPU
behind = np.zeros(len(idx), bool); first_hit = np.full(len(idx), np.nan)
for j, i in enumerate(idx):
    c = imgs[int(ref[i])]['c']; seg = X[i] - c; L = np.linalg.norm(seg)
    ts = np.arange(.02, .92, (.03 / MPU) / L)
    pts = c + np.outer(ts, seg)
    hit = stree.query(pts, distance_upper_bound=rad)[0] < rad
    if hit.any(): behind[j] = True; first_hit[j] = ts[np.argmax(hit)] * L * MPU
res['behind_a_sparse_surface_frac'] = float(behind.mean())
res['behind_surface_depth_gap_m_med'] = float(np.nanmedian(L_gap := (np.linalg.norm(X[idx] - np.array([imgs[int(ref[i])]['c'] for i in idx]), axis=1) * MPU - first_hit)))
iso = nb_out <= 2
dM = cKDTree(M).query(X[idx])[0] * MPU
dM_in = cKDTree(M).query(X[sub])[0] * MPU
res['da360_support'] = {k: dict(outside=float((dM < v).mean()), inside=float((dM_in < v).mean())) for k, v in (('within_3cm', .03), ('within_5cm', .05), ('within_10cm', .10))}
bins = [(0, .5), (.5, 2), (2, 1e9)]
res['by_distance_outside_box'] = {f'{a}-{b if b<1e8 else "inf"}m': dict(n=int(((d_out[idx] >= a) & (d_out[idx] < b)).sum()), frac_isolated=float(iso[(d_out[idx] >= a) & (d_out[idx] < b)].mean()),
                                   da360_within_5cm=float((dM[(d_out[idx] >= a) & (d_out[idx] < b)] < .05).mean()), behind_sparse=float(behind[(d_out[idx] >= a) & (d_out[idx] < b)].mean())) for a, b in bins}
sup = dM < .05
cls = np.where(iso & ~sup, 'isolated_no_da360_support', np.where(iso, 'isolated_with_da360_support', np.where(sup, 'coherent_with_da360_support', np.where(behind, 'coherent_behind_sparse_surface', 'coherent_unsupported'))))
res['classes'] = {k: int((cls == k).sum()) for k in np.unique(cls)}
res['classes_frac_of_outside'] = {k: float((cls == k).mean()) for k in np.unique(cls)}
res['classes_frac_of_all_points'] = {k: float((cls == k).sum() / len(X)) for k in np.unique(cls)}
res['height_above_highest_camera_m'] = float(((-X[idx][:, 1]) * MPU).max() - ((-cen[:, 1]) * MPU).max())
json.dump(res, open(outp, 'w'), indent=1)
np.savez(outp.replace('.json', '.npz'), idx=idx, cls=cls, X=X[idx], C=C[idx])
print(json.dumps(res, indent=1))
