"""WS-6 Phase A step 1: match-level parity, native RomaMatcher vs PyTorch dumps, on identical views.

usage: parity.py <native_dir> <torch_dir> <out.json>

Both dirs hold <A>__<B>.rwm. torch_dir is int16/uint16 encoded (the WS-4 dumps): the quantisation floor is
reported separately. EPE is in px at the match size (warp is normalised over B, so px = u * W / 2).
Every number is per pair first, then pooled; the distribution is reported, not only means.
"""
import json, struct, sys, glob, os
import numpy as np


def rd(path):
    with open(path, 'rb') as f:
        assert f.read(4) == b'RWM1'
        w, h, enc = struct.unpack('<iii', f.read(12))
        n = w * h
        if enc == 0:
            warp = np.frombuffer(f.read(8 * n), '<f4').reshape(h, w, 2).astype(np.float64)
            cert = np.frombuffer(f.read(4 * n), '<f4').reshape(h, w).astype(np.float64)
        else:
            warp = np.frombuffer(f.read(4 * n), '<i2').reshape(h, w, 2).astype(np.float64) / 32767.0
            cert = np.frombuffer(f.read(2 * n), '<u2').reshape(h, w).astype(np.float64) / 65535.0
    return warp, cert, enc


def pct(x, q):
    return float(np.percentile(x, q)) if len(x) else float('nan')


def main():
    import evalcfg
    ap = evalcfg.parser(__doc__)
    ap.add_argument('native', help='dir of native .rwm (float32)'); ap.add_argument('torch', help='dir of torch-side .rwm')
    ap.add_argument('out', help='output json')
    a = evalcfg.parse(ap)
    nat, tor, outp = a.native, a.torch, a.out
    evalcfg.check_outdir(outp, 'positional <out.json>')
    evalcfg.check_path(nat, 'positional <native_dir>', 'dir', 'native match dir'); evalcfg.check_path(tor, 'positional <torch_dir>', 'dir', 'torch match dir')
    names = sorted(os.path.basename(p) for p in glob.glob(nat + '/*.rwm'))
    names = [n for n in names if os.path.exists(os.path.join(tor, n))]
    if not names: evalcfg.die(f'no common .rwm names between {nat} and {tor}')
    rows = []
    pool = {k: [] for k in ('all', 'c05', 'c02', 'c09')}
    for n in names:
        wn, cn, en = rd(os.path.join(nat, n))
        wt, ct, et = rd(os.path.join(tor, n))
        assert wn.shape == wt.shape and cn.shape == ct.shape, n
        assert en == 0
        # impossible values: non-finite, certainty outside [0,1], warp far outside [-1,1] by more than the model emits
        assert np.isfinite(wn).all() and np.isfinite(cn).all(), n
        assert cn.min() >= -1e-6 and cn.max() <= 1 + 1e-6, n
        W = wn.shape[1]
        epe = np.linalg.norm(wn - wt, axis=-1) * (W / 2.0)
        dc = np.abs(cn - ct)
        r = dict(pair=n, epe_all_p50=pct(epe, 50), epe_all_p99=pct(epe, 99), epe_all_max=float(epe.max()))
        for tag, thr in (('c02', 0.2), ('c05', 0.5), ('c09', 0.9)):
            m = ct >= thr
            r['n_' + tag] = int(m.sum())
            e = epe[m]
            r['epe_%s_p50' % tag] = pct(e, 50); r['epe_%s_p99' % tag] = pct(e, 99)
            r['epe_%s_max' % tag] = float(e.max()) if len(e) else float('nan')
            pool[tag].append(e)
            # crossing of the threshold by the two certainties
            r['cross_' + tag] = float(((cn >= thr) != (ct >= thr)).mean())
            both = (cn >= thr) | (ct >= thr)
            r['iou_' + tag] = float(((cn >= thr) & (ct >= thr)).sum() / max(1, both.sum()))
        pool['all'].append(epe)
        r['cert_maxabs'] = float(dc.max()); r['cert_meanabs'] = float(dc.mean()); r['cert_p99abs'] = pct(dc, 99)
        r['cert_mean_t'] = float(ct.mean()); r['cert_mean_n'] = float(cn.mean())
        r['cert_corr'] = float(np.corrcoef(cn.ravel(), ct.ravel())[0, 1]) if ct.std() > 0 and cn.std() > 0 else float('nan')
        rows.append(r)
    A = lambda k: np.array([r[k] for r in rows], float)
    out = dict(n_pairs=len(rows), torch_encoding='int16 warp (step 1/32767 = %.4f px at 640, so <= 0.0049 px per axis), uint16 cert' % (320 / 32767.0))
    summ = {}
    for k in ('epe_all_p50', 'epe_all_p99', 'epe_c05_p50', 'epe_c05_p99', 'epe_c05_max', 'epe_c02_p50', 'epe_c02_p99',
              'epe_c09_p50', 'epe_c09_p99', 'cert_maxabs', 'cert_meanabs', 'cert_p99abs', 'cross_c02', 'cross_c05', 'cross_c09',
              'iou_c02', 'iou_c05', 'iou_c09', 'cert_corr'):
        a = A(k); a = a[np.isfinite(a)]
        summ[k] = dict(p05=pct(a, 5), p50=pct(a, 50), p95=pct(a, 95), p99=pct(a, 99), max=float(a.max()), mean=float(a.mean()))
    out['per_pair_distribution'] = summ
    pooled = {}
    for tag, e in pool.items():
        e = np.concatenate(e)
        pooled[tag] = dict(n=int(len(e)), p50=pct(e, 50), p90=pct(e, 90), p99=pct(e, 99), p999=pct(e, 99.9), max=float(e.max()),
                           frac_gt_0p1px=float((e > 0.1).mean()), frac_gt_1px=float((e > 1).mean()), frac_gt_5px=float((e > 5).mean()))
    out['pooled_epe_px'] = pooled
    # failure cases
    key = lambda r: r['epe_c05_p99'] if np.isfinite(r['epe_c05_p99']) else -1
    out['worst_by_epe_c05_p99'] = [dict(pair=r['pair'], epe_c05_p99=r['epe_c05_p99'], epe_c05_p50=r['epe_c05_p50'], n_c05=r['n_c05'],
                                         cross_c05=r['cross_c05'], cert_mean_t=r['cert_mean_t']) for r in sorted(rows, key=key, reverse=True)[:25]]
    out['worst_by_cross_c02'] = [dict(pair=r['pair'], cross_c02=r['cross_c02'], iou_c02=r['iou_c02'], cert_mean_t=r['cert_mean_t'], cert_mean_n=r['cert_mean_n'])
                                 for r in sorted(rows, key=lambda r: -r['cross_c02'])[:15]]
    lowcert = [r for r in rows if r['n_c05'] < 2000]
    out['pairs_with_fewer_than_2000_certain_px'] = len(lowcert)
    out['pairs_with_epe_c05_p99_gt_1px'] = int(sum(1 for r in rows if r['epe_c05_p99'] > 1))
    out['pairs_with_epe_c05_p99_gt_0p5px'] = int(sum(1 for r in rows if r['epe_c05_p99'] > 0.5))
    out['pairs_with_epe_c05_p50_gt_0p05px'] = int(sum(1 for r in rows if r['epe_c05_p50'] > 0.05))
    out['rows'] = rows
    json.dump(out, open(outp, 'w'), indent=1)
    print(json.dumps({k: v for k, v in out.items() if k not in ('rows', 'worst_by_epe_c05_p99', 'worst_by_cross_c02')}, indent=1))


if __name__ == '__main__':
    main()
