"""Mutation check of the WS-6 parity harness: make wrong 'native' warps from real native output and confirm parity2.py sees each.
shift: +0.3 px in x (a half-pixel-mapping style bug at match res); swap: x and y channels exchanged; flipcert: certainty = 1 - certainty."""
import struct, numpy as np, glob, os, subprocess, json, sys
src = 'native_m5_sub'
for mut in ('shift', 'swap', 'flipcert', 'identity'):
    d = f'mut_{mut}'; os.makedirs(d, exist_ok=True)
    for f in sorted(glob.glob(src + '/*.rwm'))[:20]:
        b = open(f, 'rb').read(); w, h, e = struct.unpack('<iii', b[4:16]); n = w * h
        wp = np.frombuffer(b[16:16 + 8 * n], '<f4').reshape(h, w, 2).copy(); c = np.frombuffer(b[16 + 8 * n:], '<f4').copy()
        if mut == 'shift': wp[..., 0] += 0.3 / 320
        elif mut == 'swap': wp = wp[..., ::-1].copy()
        elif mut == 'flipcert': c = 1 - c
        elif mut == 'identity':
            ys, xs = np.mgrid[0:h, 0:w]; wp = np.stack([(xs + .5) / 320 - 1, (ys + .5) / 320 - 1], -1).astype('<f4')
        open(f'{d}/{os.path.basename(f)}', 'wb').write(b[:16] + wp.astype('<f4').tobytes() + c.astype('<f4').tobytes())
    out = subprocess.run(['../romav2/venv/bin/python', 'parity2.py', d, '../ws4/basement_export/matches', '../ws4/basement_ds/sparse/0', f'p2_mut_{mut}.json'], capture_output=True, text=True)
    j = json.load(open(f'p2_mut_{mut}.json')); p = j['pooled_valid1']; r = j['rows']
    print(mut.ljust(9), 'pooled valid1 p50 %.3f p99 %.2f | median per-pair inl1 native %.3f (torch %.3f) | n valid1 %d' % (
        p['p50'], p['p99'], np.nanmedian([x['inl1_n'] for x in r]), np.nanmedian([x['inl1_t'] for x in r]), p['n']))
