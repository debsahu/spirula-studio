"""torch_dump.py <export_dir> <out_dir> --device cpu|mps [--setting base]: upstream RoMaV2 on the exported views,
float32 .rwm (encoding 0). Same call as reference/python/roma_dump_matches.py but patches romav2's module-level
`device` so CPU really runs on the CPU (compare_torch.py build_model does the same)."""
import sys, argparse, struct, time
from pathlib import Path
import numpy as np
import evalcfg
ap = argparse.ArgumentParser(description=__doc__); ap.add_argument('export', help='dir with pairs.txt and views/<name>.png'); ap.add_argument('out', help='dir for the .rwm files (created; existing files are kept)')
ap.add_argument('--device', default='cpu'); ap.add_argument('--setting', default='base'); a = ap.parse_args()
evalcfg.check_path(Path(a.export) / 'pairs.txt', 'positional <export_dir>', 'file', 'export pairs.txt')
evalcfg.check_path(Path(a.export) / 'views', 'positional <export_dir>', 'dir', 'export views dir')
import torch
from PIL import Image
import romav2
dev = torch.device(a.device)
for name, mod in list(sys.modules.items()):
    if name.startswith('romav2') and isinstance(getattr(mod, 'device', None), torch.device):
        mod.device = dev
from romav2 import RoMaV2
torch.set_float32_matmul_precision('highest')
model = RoMaV2(RoMaV2.Cfg(compile=False)); model.apply_setting(a.setting); model.to(dev).eval()
out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
pairs = [l.split() for l in (Path(a.export) / 'pairs.txt').read_text().splitlines() if l.strip()]
t0 = time.time(); n = 0
for A, B in pairs:
    dst = out / f'{A}__{B}.rwm'
    if dst.exists(): continue
    ia = Image.open(Path(a.export) / 'views' / f'{A}.png').convert('RGB'); ib = Image.open(Path(a.export) / 'views' / f'{B}.png').convert('RGB')
    with torch.inference_mode(): p = model.match(ia, ib)
    w = p['warp_AB'][0].float().cpu().numpy(); c = p['overlap_AB'][0, ..., 0].float().cpu().numpy()
    h, wd = c.shape
    with open(dst.with_suffix('.part'), 'wb') as f:
        f.write(b'RWM1'); f.write(struct.pack('<iii', wd, h, 0)); f.write(np.ascontiguousarray(w, '<f4').tobytes()); f.write(np.ascontiguousarray(c, '<f4').tobytes())
    dst.with_suffix('.part').rename(dst); n += 1
    if n % 5 == 0: print(n, len(pairs), f'{(time.time()-t0)/n:.1f} s/pair', flush=True)
print('done', n, f'{time.time()-t0:.0f} s')
