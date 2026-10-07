"""Shared readers and the spike model. init(args) must run before R / model() are used."""
import struct, numpy as np, pickle
import evalcfg

MPU = 0.9521          # m per unit, sparse_final/0 gauge (basement_brush/scale_config.json)
R = None              # roi module, set by init()
SPIKE = None
_J = None

def init(a):
    """a: parsed args carrying root and spike_dir. Returns the roi module."""
    global R, SPIKE, _J
    R, SPIKE, _J = evalcfg.load_spike(a)
    return R

def steps_roi():
    return _J

def read_points(p):
    """points3D.bin with empty tracks (the sibling's): id, xyz, rgb, err, 0."""
    b = open(p, 'rb').read(); n = struct.unpack_from('<Q', b, 0)[0]
    dt = np.dtype([('id', '<u8'), ('xyz', '<3f8'), ('rgb', '3u1'), ('err', '<f8'), ('tl', '<u8')])
    a = np.frombuffer(b, dt, count=n, offset=8); assert (a['tl'] == 0).all() and 8 + n * dt.itemsize == len(b)
    return a['xyz'].copy(), a['rgb'].copy(), a['err'].copy()

def read_tracks(p):
    b = open(p, 'rb').read(); assert b[:4] == b'RTK1'; n = struct.unpack_from('<Q', b, 4)[0]; o = 12; T = []
    for i in range(n):
        o += 8; k = struct.unpack_from('<I', b, o)[0]; o += 4
        T.append(np.frombuffer(b[o:o + 12 * k], dtype=[('id', '<u4'), ('x', '<f4'), ('y', '<f4')]).copy()); o += 12 * k
    return T

def model():
    return pickle.load(open(SPIKE / 'model.pkl', 'rb'))   # ids, P, PC, Tr, imgs

def da360():
    return np.load(SPIKE / 'da360_seed_only.npy')
