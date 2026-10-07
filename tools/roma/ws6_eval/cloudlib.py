import struct, numpy as np, json, sys, pickle
SP = '/private/tmp/claude-502/-Users-debsahu-Workspace-slam/8bf7354b-69a0-4115-87fb-3a148414b12e/scratchpad/rv2spike'
sys.path.insert(0, SP); import roi as R
J = json.load(open(SP + '/steps_roi.json')); R.ROI.update(dict(u=J['u'], w=J['w'], h=J['h']))
MPU = 0.9521
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
    return pickle.load(open(SP + '/model.pkl', 'rb'))   # ids, P, PC, Tr, imgs
