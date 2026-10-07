"""Equal-budget comparison of densify settings over one fixed set of .rwm matches.

Every arm runs `spirula densify --matches <dumps>` on the same dataset, pair list
and --max-points, once per seed; the WS-4 basement scorer (eval_basement.py) scores
each output, and this script adds held-out anchors, ROI-matched thinning, a
stairs-only subset and the floor (baseline sd over seeds), then reads the bars.

  eval_equal_budget.py --spirula BIN --dataset DS --matches DUMPS --pairs pairs.txt \
      --scorer eval_basement.py --python PY --spike DIR --out WORK \
      --arm base= --arm cycle="--cycle 1.0" [--seeds 0,1,2] [--max-points 1000000]

`--spike` holds the scorer's fixtures (model.pkl, roi.py, steps_roi.json). Re-runs
skip finished arms whose densify.json records the requested settings.
"""
import argparse, json, os, shlex, subprocess, sys, time
import numpy as np
from scipy.spatial import cKDTree

ap = argparse.ArgumentParser()
ap.add_argument('--spirula', required=True)
ap.add_argument('--dataset', required=True)
ap.add_argument('--matches', required=True)
ap.add_argument('--pairs', required=True, help="the plan's pairs.txt (densify --export-pairs)")
ap.add_argument('--scorer', required=True)
ap.add_argument('--python', default=sys.executable, help='interpreter for the scorer')
ap.add_argument('--spike', required=True)
ap.add_argument('--out', required=True)
ap.add_argument('--arm', action='append', required=True, help='name=extra densify flags; the first is the baseline')
ap.add_argument('--seeds', default='0,1,2')
ap.add_argument('--max-points', type=int, default=1000000)
ap.add_argument('--holdout-every', type=int, default=8)
ap.add_argument('--anchor-mod', type=int, default=8)
ap.add_argument('--draws', type=int, default=5)
ap.add_argument('--score-only', action='store_true')
a = ap.parse_args()

sys.path.insert(0, a.spike)
import roi as R
import pickle
J = json.load(open(os.path.join(a.spike, 'steps_roi.json')))
R.ROI.update(dict(u=J['u'], w=J['w'], h=J['h']))
MPU = 0.9521   # m per model unit, basement_brush/scale_config.json (as the scorer)
ids, P, PC, Tr, imgs = pickle.load(open(os.path.join(a.spike, 'model.pkl'), 'rb'))
ids = np.asarray(ids)
seeds = [int(s) for s in a.seeds.split(',')]
if len(seeds) < 3: sys.exit('the floor needs n >= 3 seeds')
arms = []
for spec in a.arm:
    name, _, flags = spec.partition('=')
    arms.append((name, shlex.split(flags)))
os.makedirs(a.out, exist_ok=True)
src = os.path.join(a.dataset, 'sparse', '0')


def fail(msg):
    sys.exit('IMPOSSIBLE: ' + msg)


# ---- the pair list the dumps must cover, both directions for a cycle arm
pairs = [tuple(l.split()) for l in open(a.pairs) if l.strip()]
if not pairs: fail('empty pair list')
have = set(os.listdir(a.matches))
missing_fwd = [p for p in pairs if f'{p[0]}__{p[1]}.rwm' not in have]
missing_rev = [p for p in pairs if f'{p[1]}__{p[0]}.rwm' not in have]
if missing_fwd: fail(f'{len(missing_fwd)} plan pairs have no dump, e.g. {missing_fwd[0]}')


def expect(flags):
    """What densify.json must record for these flags (cycle px, refine sigmas)."""
    want = {}
    for i, f in enumerate(flags):
        if f == '--cycle': want['cycle'] = flags[i + 1]
        if f == '--refine': want['refine'] = flags[i + 1]
    return want


def recorded_ok(dj, flags, seed):
    w = expect(flags)
    c, r = dj.get('cycle', {}), dj.get('refine', {})
    if dj.get('seed') != seed or dj.get('max_points') != a.max_points: return False
    if 'cycle' in w:
        v = w['cycle']
        if v == 'measure' and c.get('px') != 'measure': return False
        if v not in ('measure', 'auto', 'off') and not np.isclose(c.get('px') or -1, float(v)): return False
        if v == 'off' and not (c.get('px') is not None and c['px'] < 0): return False
    elif c.get('px') is None or (c['px'] != 'measure' and c['px'] > 0): return False   # baseline: off
    if 'refine' in w and w['refine'] not in ('auto', 'off'):
        if not np.isclose(r.get('huber_sigmas') or -1, float(w['refine'])): return False
    elif 'refine' not in w and (r.get('huber_sigmas') or -1) > 0: return False
    return True


def run(name, flags, seed):
    rel = f'sparse/0-eq-{name}-s{seed}'
    sib = os.path.join(a.dataset, rel)
    dj_path = os.path.join(sib, 'densify.json')
    if os.path.exists(dj_path) and recorded_ok(json.load(open(dj_path)), flags, seed):
        return sib
    if a.score_only: fail(f'{sib} missing or recorded other settings')
    if '--cycle' in flags and missing_rev: fail(f'cycle arm {name}: {len(missing_rev)} pairs lack B__A dumps')
    cmd = [a.spirula, 'densify', a.dataset, '--matches', a.matches, '--holdout-every', str(a.holdout_every),
           '--depth-fit-holdout', str(a.anchor_mod), '--seed', str(seed), '--max-points', str(a.max_points),
           '--source', 'roma', '--out', rel, '--overwrite'] + flags
    log = os.path.join(a.out, f'{name}-s{seed}.densify.log')
    t0 = time.time()
    with open(log, 'w') as f:
        f.write(' '.join(map(shlex.quote, cmd)) + '\n'); f.flush()
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT)
    if rc: fail(f'densify {name} s{seed} rc={rc}, see {log}')
    dj = json.load(open(dj_path))
    if not recorded_ok(dj, flags, seed): fail(f'{name} s{seed}: densify.json records other settings than requested')
    dj['_harness_wall_s'] = time.time() - t0
    json.dump(dj, open(os.path.join(a.out, f'{name}-s{seed}.densify.json'), 'w'), indent=1)
    return sib


def score(name, sib, seed):
    outp = os.path.join(a.out, f'{name}-s{seed}.score.json')
    if not (os.path.exists(outp) and os.path.getmtime(outp) > os.path.getmtime(os.path.join(sib, 'points3D.bin'))):
        with open(outp + '.log', 'w') as f:
            rc = subprocess.call([a.python, a.scorer, sib, src, outp, '--anchor-mod', str(a.anchor_mod)],
                                 stdout=f, stderr=subprocess.STDOUT)
        if rc: fail(f'scorer {name} s{seed} rc={rc}')
    return json.load(open(outp))


def rd_points(p):
    import struct
    b = open(p, 'rb').read(); n = struct.unpack_from('<Q', b, 0)[0]; o = 8; X = np.empty((n, 3))
    for i in range(n):
        o += 8; X[i] = struct.unpack_from('<3d', b, o); o += 24 + 3 + 8
        tl = struct.unpack_from('<Q', b, o)[0]; o += 8 + 8 * tl
    return X


# ---- anchor sets
name2id = {v['name']: k for k, v in imgs.items()}
inR = R.inside(P)
rng0 = np.random.default_rng(0)


def anchor_sets(dj):
    held = {name2id[n] for n in dj['held_out']}
    nh = np.array([len(set(t.tolist()) & held) for t in Tr])
    return {'roi_all': inR, 'roi_mod': inR & (ids % a.anchor_mod == 0), 'roi_heldout2': inR & (nh >= 2)}


def cover(c, A):
    if len(c) == 0 or len(A) == 0: return dict(n=int(len(A)), c2=np.nan, c5=np.nan, p50_mm=np.nan)
    d = cKDTree(c).query(A)[0] * MPU
    return dict(n=int(len(A)), c2=float((d < .02).mean()), c5=float((d < .05).mean()), p50_mm=float(np.median(d) * 1000))


# Violations as the scorer counts them (G-3e), so the stairs-only subset is comparable.
obs = [(i, cid) for i, t in enumerate(Tr) if inR[i] for cid in set(t.tolist())]
sel = np.random.default_rng(0).choice(len(obs), min(5000, len(obs)), replace=False)


def violations(c):
    if len(c) == 0: return np.nan
    tree = cKDTree(c); bad = set()
    for j in sel:
        i, cid = obs[j]; C0 = imgs[cid]['c']; seg = P[i] - C0; L = np.linalg.norm(seg)
        for idx in tree.query_ball_point(C0 + np.outer(np.arange(0.02, 0.95, (.01 / MPU) / L), seg), .01 / MPU):
            bad.update(idx)
    return len(bad) / len(c)


def stairs_only(c):
    # Treads and risers: drop points whose local normal runs across the flight (the side walls).
    if len(c) < 32: return c[:0]
    _, nb = cKDTree(c).query(c, k=16)
    X = c[nb] - c[nb].mean(1, keepdims=True)
    n = np.linalg.svd(X, full_matrices=False)[2][:, -1]
    wdir = np.array([J['w_dir_xz'][0], 0, J['w_dir_xz'][1]])
    return c[np.abs(n @ wdir) < 0.5]


sparse_tree = cKDTree(P)


def rd_first_image(p):
    import struct
    b = open(p, 'rb').read(); assert b[:4] == b'RTK1'; n = struct.unpack_from('<Q', b, 4)[0]; o = 12
    out = np.empty(n, np.int64)
    for i in range(n):
        o += 8; k = struct.unpack_from('<I', b, o)[0]; o += 4
        out[i] = struct.unpack_from('<I', b, o)[0]; o += 12 * k
    return out


def shuffled_depth(sib, X):
    # Each ROI point keeps its first image's ray but takes another point's distance.
    first = rd_first_image(os.path.join(sib, 'points3D_tracks.bin'))
    m = R.inside(X); C = np.array([imgs[int(i)]['c'] for i in first[m]])
    d = X[m] - C; r = np.linalg.norm(d, axis=1)
    return C + d / r[:, None] * np.random.default_rng(2).permutation(r)[:, None]


def extra(sib, dj):
    X = rd_points(os.path.join(sib, 'points3D.bin'))
    if len(X) == 0: fail(f'{sib}: zero points')
    if not np.isfinite(X).all(): fail(f'{sib}: non-finite coordinates')
    roi = X[R.inside(X)]
    if len(roi) == 0: fail(f'{sib}: no points in the ROI')
    st = stairs_only(roi)
    acc = lambda c: dict(n=int(len(c)), w2=float((sparse_tree.query(c)[0] * MPU < .02).mean()) if len(c) else np.nan,
                         w5=float((sparse_tree.query(c)[0] * MPU < .05).mean()) if len(c) else np.nan)
    sets = anchor_sets(dj)
    return X, roi, dict(points=int(len(X)), roi_points=int(len(roi)), roi_fraction=float(len(roi) / len(X)),
                        anchors={k: cover(roi, P[m]) for k, m in sets.items()},
                        roi_to_sparse=acc(roi), stairs=dict(points=int(len(st)), violation_rate=violations(st), **acc(st)))


# ---- run and score
rows = {}
for name, flags in arms:
    for s in seeds:
        sib = run(name, flags, s)
        sc = score(name, sib, s)
        if sc['integrity']['void']: fail(f'{name} s{s}: scorer integrity void {sc["integrity"]}')
        dj = sc['densify_json']
        X, roi, ex = extra(sib, dj)
        rows[(name, s)] = dict(sc=sc, ex=ex, roi=roi, dj=dj)
        print(f'{name} s{s}: {ex["points"]} points, ROI {ex["roi_points"]}', flush=True)
if len({r['dj']['pairs'] for r in rows.values()}) != 1 or next(iter(rows.values()))['dj']['pairs'] != len(pairs):
    fail('arms matched different pair lists, or not the pairs.txt one')


def metrics(r):
    sc, ex, dj = r['sc'], r['ex'], r['dj']
    rp = dj.get('reprojection', {})
    return {
        'violation_rate': sc['G3e']['R']['violation_rate'],
        'stairs_violation_rate': ex['stairs']['violation_rate'],
        'plane_bias_p50_mm': sc['plane']['R']['bias_p50_mm'],
        'plane_thick_p50_mm': sc['plane']['R']['thick_p50_mm'],
        'reproj_p95_px': rp.get('p95_px', np.nan),
        'roi_fraction': ex['roi_fraction'],
        'roi_points': ex['roi_points'],
        'points': ex['points'],
        'anchor_c2': ex['anchors']['roi_all']['c2'], 'anchor_c5': ex['anchors']['roi_all']['c5'],
        'anchor_mod_c2': ex['anchors']['roi_mod']['c2'], 'anchor_mod_c5': ex['anchors']['roi_mod']['c5'],
        'anchor_held_c2': ex['anchors']['roi_heldout2']['c2'], 'anchor_held_c5': ex['anchors']['roi_heldout2']['c5'],
        'anchor_p50_mm': ex['anchors']['roi_all']['p50_mm'],
        'roi_within2cm_of_sparse': ex['roi_to_sparse']['w2'], 'roi_within5cm_of_sparse': ex['roi_to_sparse']['w5'],
        'stairs_within2cm_of_sparse': ex['stairs']['w2'], 'stairs_within5cm_of_sparse': ex['stairs']['w5'],
        'outside_box': sc['G3e']['R_outside_box'],
        'seconds_total': dj['seconds_total'], 'seconds_match': dj['seconds_match'],
    }


def matched(base_roi, arm_roi, dj):
    """Anchor cover with the denser ROI cloud thinned to the sparser's count, mean of the draws."""
    n = min(len(base_roi), len(arm_roi)); rng = np.random.default_rng(1); out = {}
    sets = anchor_sets(dj)
    for tag, c in (('base', base_roi), ('arm', arm_roi)):
        draws = [c[rng.choice(len(c), n, replace=False)] if len(c) > n else c for _ in range(a.draws)]
        for k in ('roi_all', 'roi_held_out' if False else 'roi_heldout2'):
            cs = [cover(d, P[sets[k]]) for d in draws]
            out[f'{tag}_{k}_c2'] = float(np.mean([x['c2'] for x in cs]))
            out[f'{tag}_{k}_c5'] = float(np.mean([x['c5'] for x in cs]))
    out['n'] = int(n)
    return out


base = arms[0][0]
M = {(n, s): metrics(r) for (n, s), r in rows.items()}
keys = list(next(iter(M.values())).keys())
summary = {'base': base, 'seeds': seeds, 'max_points': a.max_points, 'pairs': len(pairs),
           'anchor_sets_n': {k: int(m.sum()) for k, m in anchor_sets(rows[(base, seeds[0])]['dj']).items()},
           'null': {}, 'arms': {}}
b0 = rows[(base, seeds[0])]['sc']
summary['null'] = {'violation_floaters_1pct': b0['G3e']['null_M_plus_1pct_floaters'],
                   'plane_bias_noise1cm_change_pct': 100 * (b0['plane']['R']['null_noise1cm']['bias_p50_mm'] /
                                                            b0['plane']['R']['bias_p50_mm'] - 1)}
summary['null']['violation_shuffled_depth'] = violations(shuffled_depth(os.path.join(a.dataset, f'sparse/0-eq-{base}-s{seeds[0]}'),
                                                                     rd_points(os.path.join(a.dataset, f'sparse/0-eq-{base}-s{seeds[0]}', 'points3D.bin'))))
summary['plane_bias_void'] = summary['null']['plane_bias_noise1cm_change_pct'] < 30
# Plan G-4: 1 % floaters must add >= 0.9 pp, shuffled depth must read >= 5x; else the row is void.
v0 = rows[(base, seeds[0])]['sc']['G3e']['R']['violation_rate']
summary['violation_void'] = not (summary['null']['violation_floaters_1pct'] - v0 >= 0.009 and
                                 summary['null']['violation_shuffled_depth'] >= 5 * v0)
for name, _ in arms:
    per = {k: [M[(name, s)][k] for s in seeds] for k in keys}
    arm = {k: dict(mean=float(np.nanmean(v)), sd=float(np.nanstd(v, ddof=1)), values=v) for k, v in per.items()}
    if name != base:
        arm['matched'] = [matched(rows[(base, s)]['roi'], rows[(name, s)]['roi'], rows[(base, s)]['dj']) for s in seeds]
    summary['arms'][name] = arm
B = summary['arms'][base]
sd = lambda k: B[k]['sd']
for name, _ in arms[1:]:
    A = summary['arms'][name]
    d = lambda k: A[k]['mean'] - B[k]['mean']
    mc5 = np.mean([m['arm_roi_all_c5'] - m['base_roi_all_c5'] for m in A['matched']])
    mc5_sd = np.std([m['base_roi_all_c5'] for m in A['matched']], ddof=1)   # thinning's own scatter
    A['bars'] = {
        'violations_improve_beyond_floor': None if summary['violation_void'] else d('violation_rate') <= -3 * sd('violation_rate'),
        'violations_not_worse': None if summary['violation_void'] else d('violation_rate') <= 3 * sd('violation_rate'),
        'stairs_violations_improve_beyond_floor': d('stairs_violation_rate') <= -3 * sd('stairs_violation_rate'),
        'plane_bias_improves_beyond_floor': None if summary['plane_bias_void'] else d('plane_bias_p50_mm') <= -3 * sd('plane_bias_p50_mm'),
        'plane_bias_not_worse': None if summary['plane_bias_void'] else d('plane_bias_p50_mm') <= 3 * sd('plane_bias_p50_mm'),
        'reproj_p95_improves_beyond_floor': d('reproj_p95_px') <= -3 * sd('reproj_p95_px'),
        'reproj_p95_not_worse': d('reproj_p95_px') <= 3 * sd('reproj_p95_px'),
        'roi_within2cm_of_sparse_improves_beyond_floor': d('roi_within2cm_of_sparse') >= 3 * sd('roi_within2cm_of_sparse'),
        'coverage_matched_c5_not_worse': mc5 >= -3 * max(sd('anchor_c5'), mc5_sd),
        'roi_points_not_fewer_beyond_floor': d('roi_points') >= -3 * sd('roi_points'),
    }
    A['delta'] = {k: d(k) for k in keys}
    A['delta_in_floor_sd'] = {k: (d(k) / sd(k) if sd(k) > 0 else None) for k in keys}
    A['matched_c5_delta'] = float(mc5)
json.dump(summary, open(os.path.join(a.out, 'summary.json'), 'w'), indent=1, default=float)

# ---- table
lines = [f'| metric | {base} mean (sd) | ' + ' | '.join(f'{n} mean, delta/sd' for n, _ in arms[1:]) + ' |',
         '|---|---|' + '---|' * (len(arms) - 1)]
for k in keys:
    row = f'| {k} | {B[k]["mean"]:.5g} ({B[k]["sd"]:.2g}) |'
    for n, _ in arms[1:]:
        A = summary['arms'][n]; z = A['delta_in_floor_sd'][k]
        row += f' {A[k]["mean"]:.5g}, {"n/a" if z is None else f"{z:+.1f}"} |'
    lines.append(row)
for n, _ in arms[1:]:
    lines.append(f'\n{n} bars: ' + ', '.join(f'{k}={v}' for k, v in summary['arms'][n]['bars'].items()))
lines.append(f'\nnulls: {summary["null"]}; plane bias void: {summary["plane_bias_void"]}; violations void: '
             f'{summary["violation_void"]}; anchors: {summary["anchor_sets_n"]}')
open(os.path.join(a.out, 'summary.md'), 'w').write('\n'.join(lines) + '\n')
print('\n'.join(lines))
