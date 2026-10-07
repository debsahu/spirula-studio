"""farset.py <filtered sibling> <unfiltered sibling> <out.json>: what the far-isolated filter removed, checked
against a second implementation (scipy KD-tree) and against DA360. Both siblings must come from the same run
settings with the filter on and off, UNCAPPED (an explicit cap resamples, so the clouds are not nested).
Needs points3D.bin in both and densify.json in the filtered one."""
import json, numpy as np
from scipy.spatial import cKDTree
import evalcfg, cloudlib as CL
ap = evalcfg.parser(__doc__)
evalcfg.add_spike(ap)
ap.add_argument('filtered'); ap.add_argument('unfiltered'); ap.add_argument('out')
a = evalcfg.parse(ap)
evalcfg.check_outdir(a.out, 'positional <out.json>')
for s, n, fs in ((a.filtered, '<filtered sibling>', ('points3D.bin', 'densify.json')), (a.unfiltered, '<unfiltered sibling>', ('points3D.bin',))):
    for f in fs: evalcfg.check_path(f'{s}/{f}', n, 'file', f)
R = CL.init(a); MPU = CL.MPU
Xk, _, _ = CL.read_points(a.filtered + '/points3D.bin'); Xr, _, _ = CL.read_points(a.unfiltered + '/points3D.bin')
dj = json.load(open(a.filtered + '/densify.json'))['far_isolated']
assert dj['state'] == 'on', dj
lo, hi, margin, radius, mx = np.array(dj['box_lo']), np.array(dj['box_hi']), dj['margin'], dj['radius'], dj['max_neighbours']
key = lambda X: [x.tobytes() for x in np.ascontiguousarray(X)]
kept = set(key(Xk)); ref_keys = key(Xr)
removed = np.array([k not in kept for k in ref_keys]); rset = set(ref_keys)
res = dict(n_unfiltered=len(Xr), n_filtered=len(Xk), n_removed=int(removed.sum()),
           filtered_points_not_in_unfiltered=int(sum(k not in rset for k in key(Xk))),
           margin_units=margin, margin_m=margin * MPU, radius_units=radius, radius_m=radius * MPU)
exc = np.maximum(np.maximum(lo - Xr, Xr - hi), 0).max(1)           # max-norm distance outside the box, units
tree = cKDTree(Xr); nb = np.array([len(x) - 1 for x in tree.query_ball_point(Xr, radius, workers=-1)])
far = exc > margin
ind = far & (nb <= mx)
res['far_points'] = int(far.sum()); res['independent_far_isolated'] = int(ind.sum())
res['removed_equals_independent_rule'] = bool((ind == removed).all())
res['removed_not_far'] = int((removed & ~far).sum()); res['removed_with_more_than_max_neighbours'] = int((removed & (nb > mx)).sum())
res['far_kept_with_le_max_neighbours'] = int((far & ~removed & (nb <= mx)).sum())
res['removed_dist_outside_box_m'] = {q: float(np.percentile(exc[removed], q) * MPU) for q in (0, 50, 100)} if removed.any() else None
M = CL.da360()[:, :3].astype(float); dM = cKDTree(M).query(Xr)[0] * MPU
res['da360_within_5cm'] = dict(removed=float((dM[removed] < .05).mean()) if removed.any() else None,
                               far_kept=float((dM[far & ~removed] < .05).mean()),
                               outside_box_0_to_margin=float((dM[(exc > 0) & ~far] < .05).mean()))
res['removed_in_steps_roi'] = int(R.inside(Xr[removed]).sum()) if removed.any() else 0
res['outside_box_zone_counts_before_after'] = {'0-margin': [int(((exc > 0) & ~far).sum()), int(((exc > 0) & ~far & ~removed).sum())],
                                               'beyond margin': [int(far.sum()), int((far & ~removed).sum())]}
res['removed_fraction_of_cloud'] = float(removed.mean())
json.dump(res, open(a.out, 'w'), indent=1); print(json.dumps(res, indent=1))
