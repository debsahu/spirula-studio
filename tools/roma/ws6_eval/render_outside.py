"""render_outside.py <sibling> <outside.npz> <out.jpg>: top and side tiles of the points outside the sparse box, coloured by outside.py's class."""
import numpy as np
from PIL import Image
import evalcfg, cloudlib as CL
ap = evalcfg.parser(__doc__)
evalcfg.add_spike(ap)
ap.add_argument('sibling'); ap.add_argument('npz'); ap.add_argument('out')
a = evalcfg.parse(ap)
sib, npz, outp = a.sibling, a.npz, a.out
evalcfg.check_path(sib + '/points3D.bin', 'positional <sibling>', 'file', 'sibling points3D.bin')
evalcfg.check_path(npz, 'positional <outside.npz>', 'file', 'outside.py class file'); evalcfg.check_outdir(outp, 'positional <out.jpg>')
CL.init(a)
X, C, _ = CL.read_points(sib + '/points3D.bin'); z = np.load(npz, allow_pickle=True); idx, cls = z['idx'], z['cls']
ids, P, PC, Tr, imgs = CL.model(); lo, hi = np.percentile(P, .5, 0), np.percentile(P, 99.5, 0)
col = {'coherent_with_da360_support': (0, 150, 0), 'isolated_no_da360_support': (230, 0, 0), 'isolated_with_da360_support': (255, 140, 0),
       'coherent_unsupported': (0, 60, 255), 'coherent_behind_sparse_surface': (160, 0, 200)}
mid = (lo + hi) / 2; ext = np.max(hi - lo) * 1.9
def tile(a, b, W=900):
    s = W / ext; im = np.full((W, W, 3), 255, np.uint8)
    def px(Y): return np.stack([(Y[:, a] - mid[a]) * s + W / 2, (Y[:, b] - mid[b]) * s + W / 2], 1).round().astype(int)
    p = px(X); ok = (p >= 0).all(1) & (p < W).all(1); im[p[ok][:, 1], p[ok][:, 0]] = (205, 205, 205)
    for k, c in col.items():
        m = cls == k
        if not m.any(): continue
        q = px(X[idx[m]]); ok = (q >= 0).all(1) & (q < W).all(1)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                im[np.clip(q[ok][:, 1] + dy, 0, W - 1), np.clip(q[ok][:, 0] + dx, 0, W - 1)] = c
    x0, x1 = (np.array([lo[a], hi[a]]) - mid[a]) * s + W / 2; y0, y1 = (np.array([lo[b], hi[b]]) - mid[b]) * s + W / 2
    im[int(y0):int(y1) + 1, int(x0)] = 0; im[int(y0):int(y1) + 1, int(x1)] = 0; im[int(y0), int(x0):int(x1) + 1] = 0; im[int(y1), int(x0):int(x1) + 1] = 0
    return im
Image.fromarray(np.hstack([tile(0, 2), tile(0, 1)])).save(outp, quality=90)
print({k: int((cls == k).sum()) for k in col})
