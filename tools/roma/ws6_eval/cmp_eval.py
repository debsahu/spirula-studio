"""cmp_eval.py NAME ... : side-by-side table of <eval-dir>/NAME.json from ws4_eval_basement.py."""
import json, sys
import evalcfg
ap = evalcfg.parser(__doc__)
ap.add_argument('--eval-dir', default=None, help='dir of ws4_eval_basement.py outputs (default <root>/eval)')
ap.add_argument('names', nargs='+', metavar='NAME')
a = evalcfg.parse(ap)
ed = evalcfg.resolve(a, 'eval_dir', '--eval-dir', 'eval', 'dir', 'eval directory')
names = a.names
D = {n: json.load(open(evalcfg.check_path(ed / f'{n}.json', 'positional NAME (looked up in --eval-dir)', 'file', 'eval json'))) for n in names}
def row(lbl, f): print(lbl.ljust(34), *[str(f(D[n])).ljust(16) for n in names])
print(' '.ljust(34), *[n.ljust(16) for n in names])
row('void', lambda d: d['integrity']['void'])
row('points total', lambda d: d['points']['R']['total'])
row('points ROI', lambda d: d['points']['R']['roi'])
g = lambda d, k: d['G3b']['R'][k]
row('anchor p50 mm (ROI all)', lambda d: round(g(d, 'all')['p50_mm'], 2))
row('anchor p90 mm', lambda d: round(g(d, 'all')['p90_mm'], 2))
row('anchor p50 mm (seen)', lambda d: round(g(d, 'seen')['p50_mm'], 2))
row('within 1/2/5cm (all)', lambda d: tuple(round(g(d, 'all')[k], 3) for k in ('c1', 'c2', 'c5')))
row('null noise1cm p50 change %', lambda d: round(d['G3b']['R']['null_noise1cm_p50_change_pct'], 2))
row('R/M3 p50 ratio (all)', lambda d: round(d['G3b']['R_over_M3']['all'], 3))
row('R/M p50 ratio (all)', lambda d: round(d['G3b']['R_over_M']['all'], 3))
row('random box R/M3 median', lambda d: round(d['G3b_random_boxes']['R_over_M3']['median'], 3))
row('density-matched c2', lambda d: round(d['G3c']['R']['matched']['c2'], 3))
row('per occupied 2cm voxel', lambda d: round(d['G3c']['R']['per_occupied_2cm_voxel'], 3))
row('nn spacing mm', lambda d: round(d['G3c']['R']['nn_spacing_mm'], 2))
row('scene c2', lambda d: round(d['G3c']['R']['scene_c2'], 3))
row('plane bias p50 mm', lambda d: round(d['plane']['R']['bias_p50_mm'], 2))
row('free-space violation rate', lambda d: round(d['G3e']['R']['violation_rate'], 5))
row('outside box', lambda d: round(d['G3e']['R_outside_box'], 5))
