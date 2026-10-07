"""G-3d: reprojection consistency over held-out views (plan 9.4), geometric (i) and photometric (ii).
usage: g3d.py <arm=sibling_dir_or_cloud> ... --out out.json [--matches g3d_matches] [--export export_all]
arms: 'R=<sibling dir>', 'M' (DA360 seed only) and 'M3' (DA360 same refs) are built in.
Deviation from the plan, forced by the data: M/M3 points carry no pixel association, so every arm is read through a z-buffer
of its OWN cloud in the reference face (nearest point, holes filled out to 6 px), the same way for every arm; 'same pixels' = the
pixels where all arms have a depth."""
import sys, os, json, glob, numpy as np
from scipy import ndimage
from PIL import Image
from cloudlib import *
exec(open('epi.py').read().split("if __name__")[0])
W = 640; F = 320.
args = [a for a in sys.argv[1:] if '=' in a and not a.startswith('--')]
opt = lambda k, d=None: sys.argv[sys.argv.index(k) + 1] if k in sys.argv else d
outp = opt('--out'); MD = opt('--matches', 'g3d_matches'); EX = opt('--export', 'export_all'); NSAMP = int(opt('--nsamp', 20000))
ids, Psp, PCsp, Tr, imgs = model(); POS = poses('/private/tmp/claude-502/-Users-debsahu-Workspace-slam/8bf7354b-69a0-4115-87fb-3a148414b12e/scratchpad/ws4/basement_ds/sparse/0')
clouds = {}
for a in args:
    k, v = a.split('=', 1)
    if os.path.isdir(v): X, C, _ = read_points(v + '/points3D.bin'); clouds[k] = (X, C)
    else: d = np.loadtxt(v, usecols=(1, 2, 3, 4, 5, 6)); clouds[k] = (d[:, :3], d[:, 3:6].astype(np.uint8))
M = np.load(SP + '/da360_seed_only.npy'); clouds['M'] = (M[:, :3].astype(float), M[:, 3:6].astype(np.uint8))
m3 = np.loadtxt('/private/tmp/claude-502/-Users-debsahu-Workspace-slam/8bf7354b-69a0-4115-87fb-3a148414b12e/scratchpad/ws4/m3/out/points3D.txt', usecols=(1, 2, 3, 4, 5, 6))
clouds['M3'] = (m3[:, :3], m3[:, 3:6].astype(np.uint8))
ARMS = list(clouds)
def project(X, Rv, tv):
    Xc = X @ Rv.T + tv; z = Xc[:, 2]; ok = z > 1e-6
    u = np.where(ok, F + F * Xc[:, 0] / np.where(ok, z, 1), -1); v = np.where(ok, F + F * Xc[:, 1] / np.where(ok, z, 1), -1)
    return u, v, z, ok
def depth_map(X, Rv, tv, rad=6):
    u, v, z, ok = project(X, Rv, tv); px = np.floor(u).astype(int); py = np.floor(v).astype(int)
    ok &= (px >= 0) & (px < W) & (py >= 0) & (py < W); idx = py[ok] * W + px[ok]; zz = z[ok]
    o = np.argsort(-zz); zb = np.full(W * W, np.inf); zb[idx[o]] = zz[o]; zb = zb.reshape(W, W)
    valid = np.isfinite(zb); dist, (iy, ix) = ndimage.distance_transform_edt(~valid, return_indices=True)
    out = zb[iy, ix]; out[dist > rad] = np.inf
    return out
BAND = dict(w=(-0.35, 0.15), u=(-0.7, 2.6), line_h0=-1.45, line_u0=-0.5, slope=1.02, perp=0.17)
def in_band(X):
    u, w, h = R.to_stair(X); d = np.abs(h - (BAND['line_h0'] + BAND['slope'] * (u - BAND['line_u0']))) / np.sqrt(1 + BAND['slope'] ** 2)
    return (w >= BAND['w'][0]) & (w <= BAND['w'][1]) & (u >= BAND['u'][0]) & (u <= BAND['u'][1]) & (d <= BAND['perp'])
pairs = sorted(os.path.basename(p)[:-4] for p in glob.glob(MD + '/*.rwm'))
rng = np.random.default_rng(0)
cache = {}
res_geo = {k: dict(err=[]) for k in ARMS}; null_geo = {k: [] for k in ARMS}
cover = {k: [0, 0] for k in ARMS}
for n in pairs:
    A, B = n.split('__'); wp, cert = rd(f'{MD}/{n}.rwm')
    Ra, ta = view(POS, A); Rb, tb = view(POS, B)
    m = cert > .5
    # keep only matches the poses can vouch for (Sampson < 2 px): independent of every arm
    s = sampson(POS, n + '.rwm', wp, cert)  # on cert>.5 pixels, same order as the mask
    ys, xs = np.nonzero(m); keep = s < 2.0
    ys, xs = ys[keep], xs[keep]
    if len(xs) < 200: continue
    sel = rng.choice(len(xs), min(NSAMP, len(xs)), replace=False); ys, xs = ys[sel], xs[sel]
    qx = (wp[ys, xs, 0] + 1) / 2 * W; qy = (wp[ys, xs, 1] + 1) / 2 * W
    ok = (qx >= 0) & (qx < W) & (qy >= 0) & (qy < W); ys, xs, qx, qy = ys[ok], xs[ok], qx[ok], qy[ok]
    pa = np.stack([xs + .5, ys + .5], 1)
    Zs = {}
    for k in ARMS:
        key = (B, k)
        if key not in cache: cache[key] = depth_map(clouds[k][0], Rb, tb)
        z = cache[key][np.clip(qy.astype(int), 0, W - 1), np.clip(qx.astype(int), 0, W - 1)]
        Zs[k] = z
        cover[k][0] += int(np.isfinite(z).sum()); cover[k][1] += len(z)
    common = np.isfinite(Zs['M'])            # M answers every pixel here; it defines the region, not the arm under test
    if common.sum() < 100: continue
    def err_for(z):
        Xc = np.stack([(qx - F) / F * z, (qy - F) / F * z, z], 1); Xw = (Xc - tb) @ Rb      # R^T (Xc - t) as row vectors
        Xa = Xw @ Ra.T + ta; za = Xa[:, 2]; okz = za > 1e-6
        pu = F + F * Xa[:, 0] / np.where(okz, za, 1); pv = F + F * Xa[:, 1] / np.where(okz, za, 1)
        e = np.hypot(pu - pa[:, 0], pv - pa[:, 1]); e[~okz] = np.inf
        return e, Xw
    per = {}
    for k in ARMS:
        valid = np.isfinite(Zs[k])
        e, Xw = err_for(np.where(valid, Zs[k], 1.0)); e[~valid] = np.inf
        zz = Zs[k].copy(); vi = np.where(valid)[0]; zz[vi] = zz[rng.permutation(vi)]      # null: depth permuted among the arm's own valid samples
        en, _ = err_for(np.where(valid, zz, 1.0)); en[~valid] = np.inf
        per[k] = (e, en, valid, Xw)
    # region = UNION over the arms of where their own point lands: no single arm (M above all) defines the stairs
    reg_roi = np.zeros(len(qx), bool); reg_band = np.zeros(len(qx), bool)
    for k in ARMS:
        _, _, valid, Xw = per[k]; reg_roi |= valid & R.inside(Xw); reg_band |= valid & in_band(Xw)
    regions = dict(all=np.ones(len(qx), bool), roi=reg_roi, band=reg_band)
    for k in ARMS:
        res_geo[k]['err'].append(per[k][0]); null_geo[k].append(per[k][1])
        for r, m in regions.items(): res_geo[k].setdefault('reg_' + r, []).append(m)
def stats(e, n_region):
    f = e[np.isfinite(e)]
    return dict(n_region=int(n_region), n_valid=int(len(f)), coverage=float(len(f) / max(1, n_region)), p50=float(np.median(f)) if len(f) else None, p90=float(np.percentile(f, 90)) if len(f) else None,
                frac_within_3px_of_region=float((f < 3).sum() / max(1, n_region)), frac_within_10px_of_region=float((f < 10).sum() / max(1, n_region)))
out = dict(n_pairs_used=len(res_geo[ARMS[0]]['err']), arms=ARMS, geometric={})
for k in ARMS:
    e = np.concatenate(res_geo[k]['err']); nl = np.concatenate(null_geo[k]); out['geometric'][k] = {}
    for r in ('all', 'roi', 'band'):
        m = np.concatenate(res_geo[k]['reg_' + r]); out['geometric'][k][r] = stats(e[m], m.sum()); out['geometric'][k]['null_' + r] = stats(nl[m], m.sum())
# --- (ii) photometric, steps ROI points of each arm, visible in a held-out face of its own z-buffer at 1/4 res
import json as _j
held = _j.load(open('g3d_export/meta.json'))['held']
phot = {k: dict(err=[], null=[]) for k in ARMS}
Q = W // 4
for h in held:
    for f in FACE:
        nm = f'{h}_{f}'; Rv, tv = view(POS, nm); im = np.array(Image.open(f'{EX}/views/{nm}.png').convert('RGB')).astype(float)
        nonblack = im.sum(2) > 0
        for k in ARMS:
            Xa, Ca = clouds[k]; inr = R.inside(Xa); Xr, Cr = Xa[inr], Ca[inr].astype(float)
            u, v, z, okp = project(Xr, Rv, tv); px = np.floor(u).astype(int); py = np.floor(v).astype(int)
            okp &= (px >= 0) & (px < W) & (py >= 0) & (py < W)
            if okp.sum() < 50: continue
            # z-buffer of the whole arm cloud at 1/4 res
            u2, v2, z2, ok2 = project(Xa, Rv, tv); gx = np.floor(u2 / 4).astype(int); gy = np.floor(v2 / 4).astype(int)
            ok2 &= (gx >= 0) & (gx < Q) & (gy >= 0) & (gy < Q); zb = np.full(Q * Q, np.inf); o = np.argsort(-z2[ok2]); zb[(gy[ok2] * Q + gx[ok2])[o]] = z2[ok2][o]
            zb = ndimage.minimum_filter(zb.reshape(Q, Q), size=3)
            gi = np.clip(py // 4, 0, Q - 1) * Q + np.clip(px // 4, 0, Q - 1)
            vis = okp & (z <= zb.ravel()[gi] * 1.03 + 0.03 / MPU) & nonblack[np.clip(py, 0, W - 1), np.clip(px, 0, W - 1)]
            if vis.sum() < 50: continue
            hc = im[py[vis], px[vis]]; pc = Cr[vis]
            lum = lambda c: c.mean(1)
            gain = np.median(lum(hc) / np.maximum(lum(pc), 1)); e = np.abs(hc - pc * gain).mean(1)
            phot[k]['err'].append(e)
            sh = hc.copy(); rng.shuffle(sh); phot[k]['null'].append(np.abs(sh - pc * gain).mean(1))
out['photometric'] = {k: dict(n=int(sum(len(x) for x in phot[k]['err'])), p50=float(np.median(np.concatenate(phot[k]['err']))), p90=float(np.percentile(np.concatenate(phot[k]['err']), 90)),
                              null_p50=float(np.median(np.concatenate(phot[k]['null'])))) for k in ARMS if phot[k]['err']}
json.dump(out, open(outp, 'w'), indent=1); print('done')
