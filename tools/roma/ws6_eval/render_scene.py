"""render_scene.py <outdir> arm=path ... : whole-scene cloud from 3 fixed views (top-down cut at camera height, two interior perspectives).
Paths: sibling dir (points3D.bin), da360_seed_only.npy, or points3D.txt. Same views for every arm; positions in views_scene.json."""
import sys, os, json, numpy as np
from PIL import Image, ImageDraw
from cloudlib import *
out = sys.argv[1]; arms = dict(a.split('=', 1) for a in sys.argv[2:]); os.makedirs(out, exist_ok=True)
ids, P, PC, Tr, imgs = model()
cen = np.array([imgs[i]['c'] for i in sorted(imgs)]); name2c = {imgs[i]['name']: imgs[i]['c'] for i in imgs}
def load(p):
    if os.path.isdir(p): X, C, _ = read_points(p + '/points3D.bin'); return X, C
    if p.endswith('.npy'): d = np.load(p); return d[:, :3].astype(float), d[:, 3:6].astype(np.uint8)
    d = np.loadtxt(p, usecols=(1, 2, 3, 4, 5, 6)); return d[:, :3], d[:, 3:6].astype(np.uint8)
lo, hi = np.percentile(P, .5, 0), np.percentile(P, 99.5, 0); mid = (lo + hi) / 2
camy = np.median(cen[:, 1])      # up is -y
VIEWS = {
 'topdown': dict(kind='ortho'),
 'persp_A': dict(kind='persp', eye=name2c['frame_0050.jpg'].tolist(), look=(mid + np.array([0, 0, 0])).tolist(), fov=100),
 'persp_B': dict(kind='persp', eye=name2c['frame_0120.jpg'].tolist(), look=name2c['frame_0020.jpg'].tolist(), fov=100),
}
json.dump(VIEWS, open(out + '/views_scene.json', 'w'), indent=1)
W, H = 900, 700
def splat(px, depth, col, rad):
    img = np.full((H, W, 3), 255, np.uint8); o = np.argsort(-depth); px, col = px[o], col[o]
    for dy in range(-rad, rad + 1):
        for dx in range(-rad, rad + 1):
            if dx * dx + dy * dy > rad * rad: continue
            x = px[:, 0] + dx; y = px[:, 1] + dy; ok = (x >= 0) & (x < W) & (y >= 0) & (y < H); img[y[ok], x[ok]] = col[ok]
    return img
def render(X, col, v):
    if v['kind'] == 'ortho':
        m = X[:, 1] > camy - 0.0      # keep what is below camera height (y down): floor, lower walls, furniture
        X, col = X[m], col[m]; sx = W / (hi[0] - lo[0] + 1); sz = H / (hi[2] - lo[2] + 1); s = min(sx, sz)
        px = np.stack([(X[:, 0] - mid[0]) * s + W / 2, (X[:, 2] - mid[2]) * s + H / 2], 1).round().astype(int)
        return splat(px, -X[:, 1], col, 1)
    e = np.array(v['eye']); f = np.array(v['look']) - e; f /= np.linalg.norm(f)
    up = np.array([0, -1.0, 0]); r = np.cross(f, up); r /= np.linalg.norm(r); u = np.cross(r, f)
    Q = X - e; z = Q @ f; ok = z > .05; Q, z, c = Q[ok], z[ok], col[ok]; fp = (W / 2) / np.tan(np.radians(v['fov'] / 2))
    px = np.stack([W / 2 + fp * (Q @ r) / z, H / 2 - fp * (Q @ u) / z], 1)
    inb = (np.abs(px[:, 0] - W / 2) < W) & (np.abs(px[:, 1] - H / 2) < H); px = px[inb].round().astype(int)
    return splat(px, z[inb], c[inb], 1)
tiles = {}
for a, p in arms.items():
    X, C = load(p)
    for vn, v in VIEWS.items():
        im = Image.fromarray(render(X, C, v)); d = ImageDraw.Draw(im); d.rectangle([0, 0, W, 22], fill=(0, 0, 0)); d.text((6, 5), f'{a} | {vn} | {len(X):,} pts', fill=(255, 255, 0)); tiles[(a, vn)] = im
for vn in VIEWS:
    sheet = Image.new('RGB', (W * len(arms), H), 'white')
    for i, a in enumerate(arms): sheet.paste(tiles[(a, vn)], (i * W, 0))
    sheet.save(f'{out}/scene_{vn}.jpg', quality=88)
print('ok')
