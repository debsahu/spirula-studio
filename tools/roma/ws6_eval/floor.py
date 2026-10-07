import json, numpy as np
get = {'anchor p50 mm': lambda d: d['G3b']['R']['all']['p50_mm'], 'anchor p90 mm': lambda d: d['G3b']['R']['all']['p90_mm'],
       'within 5cm': lambda d: d['G3b']['R']['all']['c5'], 'ROI points': lambda d: d['points']['R']['roi'],
       'matched c2': lambda d: d['G3c']['R']['matched']['c2'], 'viol rate': lambda d: d['G3e']['R']['violation_rate'],
       'outside box': lambda d: d['G3e']['R_outside_box'], 'plane bias p50 mm': lambda d: d['plane']['R']['bias_p50_mm'],
       'R/M3 p50 (all)': lambda d: d['G3b']['R_over_M3']['all']}
A = {k: [json.load(open(f'eval/{k}_s{s}.json')) for s in (0, 1, 2)] for k in ('dump', 'native')}
print('metric'.ljust(20), 'dump mean (sd)'.ljust(22), 'native mean (sd)'.ljust(22), 'diff', ' diff/sd_pooled')
for m, f in get.items():
    a = np.array([f(d) for d in A['dump']]); b = np.array([f(d) for d in A['native']])
    sd = np.sqrt((a.var(ddof=1) + b.var(ddof=1)) / 2)
    print(m.ljust(20), f'{a.mean():.5g} ({a.std(ddof=1):.3g})'.ljust(22), f'{b.mean():.5g} ({b.std(ddof=1):.3g})'.ljust(22), f'{b.mean()-a.mean():+.3g}', f'{(b.mean()-a.mean())/sd:+.2f}' if sd > 0 else '')
