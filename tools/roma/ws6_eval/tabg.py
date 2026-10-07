import json, sys
for f in sys.argv[1:]:
    d = json.load(open(f)); print(f, 'pairs', d['n_pairs_used'])
    for r in ('all', 'roi', 'band'):
        print(' region', r, 'n', d['geometric']['M'][r]['n_region'])
        for k, v in d['geometric'].items():
            g = v[r]; n = v['null_' + r]
            print('   ', k.ljust(14), 'coverage %.3f | err p50 %s p90 %s | within 3px of region %.3f, 10px %.3f | null within 3px %.3f' % (g['coverage'], 'NA' if g['p50'] is None else '%.2f' % g['p50'], 'NA' if g['p90'] is None else '%.1f' % g['p90'], g['frac_within_3px_of_region'], g['frac_within_10px_of_region'], n['frac_within_3px_of_region']))
    print('   photometric', {k: (round(v['p50'], 1), round(v['null_p50'], 1), v['n']) for k, v in d['photometric'].items()})
