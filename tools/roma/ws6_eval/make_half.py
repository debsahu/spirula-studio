"""make_half.py <src model dir> <dst model dir> <seed>: split the sparse points 50/50 by id. dst keeps half A (images.bin's references to
half B are set to the invalid id); half B ids are written to <dst>/../halfB_ids.npy. The fit inside moge/hybrid then never sees half B."""
import struct, os, shutil, numpy as np
import evalcfg
ap = evalcfg.parser(__doc__)
ap.add_argument('src'); ap.add_argument('dst'); ap.add_argument('seed', type=int)
a = evalcfg.parse(ap)
src, dst, seed = a.src, a.dst, a.seed
for f in ('images.bin', 'points3D.bin'): evalcfg.check_path(f'{src}/{f}', 'positional <src>', 'file', f'source model {f}')
if os.path.exists(dst + '/points3D.bin'): evalcfg.die(f'refusing to overwrite an existing model: {dst}/points3D.bin (positional <dst>)')
os.makedirs(dst, exist_ok=True)
for f in os.listdir(src):
    if f not in ('images.bin', 'points3D.bin'): shutil.copy(f'{src}/{f}', f'{dst}/{f}')
b = open(src + '/points3D.bin', 'rb').read(); n, = struct.unpack_from('<Q', b, 0); o = 8; recs = []; ids = []
for _ in range(n):
    s = o; pid, = struct.unpack_from('<Q', b, o); o += 8 + 24 + 3 + 8; tl, = struct.unpack_from('<Q', b, o); o += 8 + 8 * tl
    recs.append(b[s:o]); ids.append(pid)
ids = np.array(ids, np.uint64); rng = np.random.default_rng(seed); isB = rng.random(n) < 0.5
Bset = set(ids[isB].tolist())
open(dst + '/points3D.bin', 'wb').write(struct.pack('<Q', int((~isB).sum())) + b''.join(r for r, bb in zip(recs, isB) if not bb))
np.save(os.path.dirname(dst.rstrip('/')) + '/halfB_ids.npy', ids[isB]); np.save(os.path.dirname(dst.rstrip('/')) + '/halfA_ids.npy', ids[~isB])
ib = bytearray(open(src + '/images.bin', 'rb').read()); m, = struct.unpack_from('<Q', ib, 0); o = 8; removed = 0
for _ in range(m):
    o += 4 + 56 + 4; e = ib.index(b'\0', o); o = e + 1; k, = struct.unpack_from('<Q', ib, o); o += 8
    for j in range(k):
        pid, = struct.unpack_from('<Q', ib, o + 16)
        if pid in Bset: struct.pack_into('<Q', ib, o + 16, 0xFFFFFFFFFFFFFFFF); removed += 1
        o += 24
assert o == len(ib)
open(dst + '/images.bin', 'wb').write(bytes(ib))
print(f'{n} points: A {int((~isB).sum())} kept, B {int(isB.sum())} held for scoring; {removed} 2D observations detached')
