"""Mutation check of the WS-6 parity harness: make wrong 'native' warps from real native output and confirm parity2.py sees each.
shift: +0.3 px in x (a half-pixel-mapping style bug at match res); swap: x and y channels exchanged; flipcert: certainty = 1 - certainty.
usage: mutants_parity.py --native DIR --torch DIR --model DIR [--work-dir DIR] [--n 20]"""
import struct, numpy as np, glob, os, subprocess, json, sys
import evalcfg
ap = evalcfg.parser(__doc__)
ap.add_argument('--native', default=None, help='dir of native .rwm (float32) to mutate (default <root>/native_m5_sub)')
ap.add_argument('--torch', default=None, help='dir of the torch-side .rwm (default <root>/matches)')
ap.add_argument('--model', default=None, help='COLMAP model dir for the poses (default <root>/basement_ds/sparse/0)')
ap.add_argument('--work-dir', default=None, help='where mut_* dirs and p2_mut_*.json go (default <root>/mutants)')
ap.add_argument('--n', type=int, default=20, help='pairs to mutate')
a = evalcfg.parse(ap)
src = str(evalcfg.resolve(a, 'native', '--native', 'native_m5_sub', 'dir', 'native match dir'))
tor = str(evalcfg.resolve(a, 'torch', '--torch', 'matches', 'dir', 'torch match dir'))
mdl = str(evalcfg.resolve(a, 'model', '--model', 'basement_ds/sparse/0', 'dir', 'poses model dir'))
wd = a.work_dir or str(a.root / 'mutants'); os.makedirs(wd, exist_ok=True)
files = sorted(glob.glob(src + '/*.rwm'))[:a.n]
if not files: evalcfg.die(f'no .rwm files in {src} (--native)')
for mut in ('shift', 'swap', 'flipcert', 'identity'):
    d = f'{wd}/mut_{mut}'; os.makedirs(d, exist_ok=True)
    for f in files:
        b = open(f, 'rb').read(); w, h, e = struct.unpack('<iii', b[4:16]); n = w * h
        wp = np.frombuffer(b[16:16 + 8 * n], '<f4').reshape(h, w, 2).copy(); c = np.frombuffer(b[16 + 8 * n:], '<f4').copy()
        if mut == 'shift': wp[..., 0] += 0.3 / 320
        elif mut == 'swap': wp = wp[..., ::-1].copy()
        elif mut == 'flipcert': c = 1 - c
        elif mut == 'identity':
            ys, xs = np.mgrid[0:h, 0:w]; wp = np.stack([(xs + .5) / 320 - 1, (ys + .5) / 320 - 1], -1).astype('<f4')
        open(f'{d}/{os.path.basename(f)}', 'wb').write(b[:16] + wp.astype('<f4').tobytes() + c.astype('<f4').tobytes())
    outj = f'{wd}/p2_mut_{mut}.json'
    out = subprocess.run([sys.executable, str(evalcfg.HERE / 'parity2.py'), '--root', str(a.root), d, tor, mdl, outj], capture_output=True, text=True)
    if out.returncode: evalcfg.die(f'parity2.py failed on mutant {mut}:\n{out.stderr}')
    j = json.load(open(outj)); p = j['pooled_valid1']; r = j['rows']
    print(mut.ljust(9), 'pooled valid1 p50 %.3f p99 %.2f | median per-pair inl1 native %.3f (torch %.3f) | n valid1 %d' % (
        p['p50'], p['p99'], np.nanmedian([x['inl1_n'] for x in r]), np.nanmedian([x['inl1_t'] for x in r]), p['n']))
