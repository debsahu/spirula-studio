"""floor.py : run-to-run floor, dump vs native arms, three seeds each, from <eval-dir>/{dump,native}_s{0,1,2}.json
(ws4_eval_basement.py outputs). Prints mean (sd) per metric and the arm difference in pooled-sd units."""
import json, numpy as np
import evalcfg
ap = evalcfg.parser(__doc__)
ap.add_argument('--eval-dir', default=None, help='dir of ws4_eval_basement.py outputs (default <root>/eval)')
ap.add_argument('--arms', nargs=2, default=['dump', 'native'], metavar=('A', 'B'), help='file prefixes of the two arms (default dump native)')
ap.add_argument('--seeds', type=int, nargs='+', default=[0, 1, 2])
a = evalcfg.parse(ap)
ed = evalcfg.resolve(a, 'eval_dir', '--eval-dir', 'eval', 'dir', 'eval directory')
get = {'anchor p50 mm': lambda d: d['G3b']['R']['all']['p50_mm'], 'anchor p90 mm': lambda d: d['G3b']['R']['all']['p90_mm'],
       'within 5cm': lambda d: d['G3b']['R']['all']['c5'], 'ROI points': lambda d: d['points']['R']['roi'],
       'matched c2': lambda d: d['G3c']['R']['matched']['c2'], 'viol rate': lambda d: d['G3e']['R']['violation_rate'],
       'outside box': lambda d: d['G3e']['R_outside_box'], 'plane bias p50 mm': lambda d: d['plane']['R']['bias_p50_mm'],
       'R/M3 p50 (all)': lambda d: d['G3b']['R_over_M3']['all']}
A = {k: [json.load(open(evalcfg.check_path(ed / f'{k}_s{s}.json', '--arms/--seeds (looked up in --eval-dir)', 'file', 'eval json'))) for s in a.seeds] for k in a.arms}
ka, kb = a.arms
print('metric'.ljust(20), f'{ka} mean (sd)'.ljust(22), f'{kb} mean (sd)'.ljust(22), 'diff', ' diff/sd_pooled')
for m, f in get.items():
    x = np.array([f(d) for d in A[ka]]); y = np.array([f(d) for d in A[kb]])
    sd = np.sqrt((x.var(ddof=1) + y.var(ddof=1)) / 2)
    print(m.ljust(20), f'{x.mean():.5g} ({x.std(ddof=1):.3g})'.ljust(22), f'{y.mean():.5g} ({y.std(ddof=1):.3g})'.ljust(22), f'{y.mean()-x.mean():+.3g}', f'{(y.mean()-x.mean())/sd:+.2f}' if sd > 0 else '')
