"""WS-4 basement evaluation (plan §9.4 subset, geometry only), every metric beside its null.
usage: ws4_eval_basement.py <sibling model dir> <source model dir> <out.json> [--m3 points3D.txt | --no-m3] [--anchor-mod N]
Units: sparse_final/0 gauge; mm via 0.9521 m/unit (SHOWS, basement_brush/scale_config.json)."""
import json, struct, pickle, hashlib, os, sys
import numpy as np
from scipy.spatial import cKDTree
import evalcfg
ap = evalcfg.parser(__doc__)
evalcfg.add_spike(ap); evalcfg.add_m3(ap)
ap.add_argument('sibling', help='densify output dir (cameras.bin, images.bin, points3D.bin, points3D_tracks.bin, densify.json)')
ap.add_argument('source', help='the model it was densified from (cameras.bin, images.bin)')
ap.add_argument('out', help='output json')
ap.add_argument('--no-m3', action='store_true', help='score without the M3 arm (the R_over_M3 rows are then absent)')
ap.add_argument('--anchor-mod', type=int, default=0, help='also score independent anchors: sparse ids with id %% N == 0')
a = evalcfg.parse(ap)
sib, src, outp = a.sibling, a.source, a.out
for d, w in ((sib, 'sibling'), (src, 'source model')):
    for f in ('cameras.bin', 'images.bin'): evalcfg.check_path(f'{d}/{f}', f'positional <{w}>', 'file', f'{w} {f}')
for f in ('points3D.bin', 'points3D_tracks.bin', 'densify.json'): evalcfg.check_path(f'{sib}/{f}', 'positional <sibling>', 'file', f'sibling {f}')
evalcfg.check_outdir(outp, 'positional <out>')
R, SP, J = evalcfg.load_spike(a)
m3p = None if a.no_m3 else str(evalcfg.resolve(a, 'm3', '--m3', 'spike/m3_points3D.txt', 'file', 'M3 cloud (points3D.txt)'))
MPU = 0.9521
rng = np.random.default_rng(0)
mm = lambda x: float(x * MPU * 1000)

def rd_points(p):
    b = open(p, 'rb').read(); n = struct.unpack_from('<Q', b, 0)[0]; o = 8
    X = np.empty((n, 3)); C = np.empty((n, 3), np.uint8)
    for i in range(n):
        o += 8; X[i] = struct.unpack_from('<3d', b, o); o += 24; C[i] = np.frombuffer(b[o:o + 3], np.uint8); o += 3 + 8
        tl = struct.unpack_from('<Q', b, o)[0]; o += 8 + 8 * tl
    return X, C
def rd_tracks(p):
    b = open(p, 'rb').read(); assert b[:4] == b'RTK1'; n = struct.unpack_from('<Q', b, 4)[0]; o = 12; T = []
    for i in range(n):
        o += 8; k = struct.unpack_from('<I', b, o)[0]; o += 4
        T.append(np.frombuffer(b[o:o + 12 * k], dtype=[('id', '<u4'), ('x', '<f4'), ('y', '<f4')]).copy()); o += 12 * k
    return T
sha = lambda p: hashlib.sha256(open(p, 'rb').read()).hexdigest()

out = dict(sibling=sib, integrity={})
dj = json.loads(open(sib + '/densify.json').read().replace(': inf', ': null')); out['densify_json'] = dj
# --- §9.5 integrity: any failure voids the run
I = out['integrity']
I['cameras_bin_identical'] = sha(sib + '/cameras.bin') == sha(src + '/cameras.bin')
import integrity as _ig
_ia, _ra = _ig.parse(open(sib + '/images.bin', 'rb').read()); _ib, _rb = _ig.parse(open(src + '/images.bin', 'rb').read())
I['images_bin_identical'] = _ra == _rb and len(_ia) > 0 and all(i == 2**64 - 1 for i in _ia)
Rx, Rc = rd_points(sib + '/points3D.bin'); T = rd_tracks(sib + '/points3D_tracks.bin')
ids, P, PC, Tr, imgs = pickle.load(open(SP / 'model.pkl', 'rb'))
name2id = {v['name']: k for k, v in imgs.items()}
held = {name2id[n] for n in dj['held_out']}
I['held_out_in_tracks'] = int(sum(bool(set(t['id'].tolist()) & held) for t in T))
I['mask_keep'] = dj['mask_keep']; I['mask_keep_in_range'] = 0.05 <= dj['mask_keep'] <= 0.995
I['nonfinite'] = int((~np.isfinite(Rx)).any(1).sum())
M = np.load(SP / 'da360_seed_only.npy')[:, :3].astype(np.float64)
I['M_min_dist_to_anchor'] = float(cKDTree(M).query(P)[0].min()) if len(M) and len(P) else float('nan')
I['M_contains_anchors'] = _ig.contains_anchors(M, P)
I['void'] = not (I['cameras_bin_identical'] and I['images_bin_identical'] and I['held_out_in_tracks'] == 0
                 and I['mask_keep_in_range'] and I['nonfinite'] == 0 and not I['M_contains_anchors']
                 and len(Rx) > 0 and len(T) > 0 and len(M) > 0 and len(P) > 0)
refs = sorted({int(t['id'][0]) for t in T})
out['refs_from_tracks'] = len(refs)
clouds = {'R': Rx, 'M': M}
if m3p: clouds['M3'] = np.loadtxt(m3p, usecols=(1, 2, 3))
inR = R.inside(P)
seen = np.array([bool(set(refs) & set(t.tolist())) for t in Tr])
A_all, A_seen = P[inR], P[inR & seen]
out['anchors'] = dict(roi=int(inR.sum()), roi_seen_by_refs=int((inR & seen).sum()), scene=int(len(P)))
roi_c = {k: v[R.inside(v)] for k, v in clouds.items()}
out['points'] = {k: dict(total=int(len(v)), roi=int(len(roi_c[k]))) for k, v in clouds.items()}

# --- G-3b sparse-anchor distance, ROI, with the 1 cm noise null and random boxes
def anchor(c, A):
    d = cKDTree(c).query(A)[0]
    return dict(p50_mm=mm(np.median(d)), p90_mm=mm(np.percentile(d, 90)),
                c1=float((d * MPU < .01).mean()), c2=float((d * MPU < .02).mean()), c5=float((d * MPU < .05).mean()))
# Independent anchors: sparse points held out of the depth fit (id % mod == 0, --anchor-mod)
amod = a.anchor_mod
if amod:
    ind = inR & (np.asarray(ids) % amod == 0)
    out['G3b_independent'] = {k: dict(n_anchors=int(ind.sum()), **anchor(c, P[ind])) for k, c in roi_c.items()}
out['roi_fraction'] = {k: float(len(roi_c[k]) / max(1, len(v))) for k, v in clouds.items()}
G3b = out['G3b'] = {}
for k, c in roi_c.items():
    G3b[k] = dict(all=anchor(c, A_all), seen=anchor(c, A_seen))
    noisy = c + rng.normal(0, .01 / MPU, c.shape)
    G3b[k]['null_noise1cm_p50_change_pct'] = 100 * (anchor(noisy, A_all)['p50_mm'] / G3b[k]['all']['p50_mm'] - 1)
for k in [x for x in clouds if x != 'R']:
    G3b['R_over_' + k] = dict(all=G3b['R']['all']['p50_mm'] / G3b[k]['all']['p50_mm'],
                              seen=G3b['R']['seen']['p50_mm'] / G3b[k]['seen']['p50_mm'])
ctrl = []; seenP = P[seen]
for trial in range(5000):
    if len(ctrl) >= 40: break
    c0 = seenP[rng.integers(len(seenP))]; u0, w0, h0 = [x[0] for x in R.to_stair(c0[None])]
    box = dict(u=[u0 - 1.95, u0 + 1.95], w=[w0 - .65, w0 + .65], h=[h0 - 1.8, h0 + 1.8])
    if not (box['u'][1] < R.ROI['u'][0] or box['u'][0] > R.ROI['u'][1] or box['w'][1] < R.ROI['w'][0] or box['w'][0] > R.ROI['w'][1]): continue
    m = R.inside(P, box) & seen
    if m.sum() < 200: continue
    row = {}
    for k, c in clouds.items():
        cc = c[R.inside(c, box)]
        row[k] = float(np.median(cKDTree(cc).query(P[m])[0])) if len(cc) else np.nan
    ctrl.append(row)
out['G3b_random_boxes'] = {('R_over_' + k): dict(median=float(np.nanmedian([r['R'] / r[k] for r in ctrl])),
                                                   p10=float(np.nanpercentile([r['R'] / r[k] for r in ctrl], 10)),
                                                   p90=float(np.nanpercentile([r['R'] / r[k] for r in ctrl], 90)), n=len(ctrl))
                           for k in clouds if k != 'R'}

# --- G-3c completeness at matched density, density, uniform-random chance level
G3c = out['G3c'] = {}
nmin = min(len(c) for c in roi_c.values()); G3c['density_matched_n'] = int(nmin)
lo, hi = np.percentile(P, .5, 0), np.percentile(P, 99.5, 0)
for k, c in roi_c.items():
    vox = np.unique(np.floor(c / (.02 / MPU)).astype(np.int64), axis=0)
    nn = cKDTree(c).query(c[rng.choice(len(c), min(20000, len(c)), replace=False)], k=2)[0][:, 1]
    sub = [anchor(c[rng.choice(len(c), nmin, replace=False)], A_all) for _ in range(5)]
    G3c[k] = dict(per_occupied_2cm_voxel=float(len(c) / len(vox)), nn_spacing_mm=mm(np.median(nn)),
                  matched=dict(c1=float(np.mean([s['c1'] for s in sub])), c2=float(np.mean([s['c2'] for s in sub])),
                               c5=float(np.mean([s['c5'] for s in sub]))),
                  scene_c2=anchor(clouds[k], P[rng.choice(len(P), 20000, replace=False)])['c2'])
U = rng.uniform(lo, hi, (len(roi_c['R']), 3)); U = U[R.inside(U)]
G3c['uniform_random_chance'] = anchor(U, A_all) if len(U) else None

# --- local-plane bias / thickness around ROI anchors seen by refs, with its noise null
def plane(cloud, A, K=16, rmax=.05 / MPU):
    c = cloud[R.inside(cloud, dict(u=[J['u'][0] - .2, J['u'][1] + .2], w=[J['w'][0] - .2, J['w'][1] + .2], h=[J['h'][0] - .2, J['h'][1] + .2]))]
    d, i = cKDTree(c).query(A, k=K); ok = d[:, -1] < rmax
    nb = c[i[ok]]; cen = nb.mean(1); X = nb - cen[:, None]
    n = np.linalg.svd(X, full_matrices=False)[2][:, -1]
    bias = np.abs(np.einsum('ij,ij->i', A[ok] - cen, n)); th = np.sqrt((np.einsum('ikj,ij->ik', X, n) ** 2).mean(1))
    return dict(support=float(ok.mean()), bias_p50_mm=mm(np.median(bias)), bias_p90_mm=mm(np.percentile(bias, 90)),
                thick_p50_mm=mm(np.median(th)))
out['plane'] = {}
for k, c in clouds.items():
    out['plane'][k] = plane(c, A_seen)
    out['plane'][k]['null_noise1cm'] = plane(c + rng.normal(0, .01 / MPU, c.shape), A_seen)

# --- G-3e free-space violations and outside-box, with the synthetic-floater null
obs = [(a, cid) for a, t in enumerate(Tr) if inR[a] for cid in set(t.tolist())]
sel = rng.choice(len(obs), min(5000, len(obs)), replace=False)
def violations(c):
    tree = cKDTree(c); bad = set()
    for j in sel:
        a, cid = obs[j]; C0 = imgs[cid]['c']; seg = P[a] - C0; L = np.linalg.norm(seg)
        ts = np.arange(0.02, 0.95, (.01 / MPU) / L)
        for idx in tree.query_ball_point(C0 + np.outer(ts, seg), .01 / MPU):
            bad.update(idx)
    return bad
G3e = out['G3e'] = {}
for k, c in roi_c.items():
    b = violations(c); G3e[k] = dict(violation_rate=len(b) / len(c), n=len(b))
fl = []
for _ in range(int(.01 * len(roi_c['M']))):
    a, cid = obs[rng.integers(len(obs))]; C0 = imgs[cid]['c']; fl.append(C0 + rng.uniform(.1, .9) * (P[a] - C0))
Mf = np.vstack([roi_c['M'], np.array(fl)])
G3e['null_M_plus_1pct_floaters'] = len(violations(Mf)) / len(Mf)
box_lo, box_hi = lo - .5 / MPU, hi + .5 / MPU
for k, c in clouds.items():
    G3e[k + '_outside_box'] = float(((c < box_lo) | (c > box_hi)).any(1).mean())
json.dump(out, open(outp, 'w'), indent=1, default=float)
print(json.dumps({k: v for k, v in out.items() if k != 'densify_json'}, indent=1, default=float))
sys.exit(1 if I['void'] else 0)
