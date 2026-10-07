import json, sys
d = json.load(open(sys.argv[1])); A = d['arms']
print('anchors', d['anchors'], 'equal-budget n', d['equal_budget_n'])
hdr = ['arm', 'total', 'ROI n', 'ROI frac', 'ROI p50', 'p90', 'c5', 'matched p50', 'matched c5', 'noise null %', '| band n', 'band frac', 'band p50', 'c5', 'matched p50', 'matched c5', 'planes 1cm', 'rms', '| scene p50', 'scene c5']
print(' | '.join(hdr))
for k, a in A.items():
    r, b = a['roi'], a['band']
    f = lambda x, n=1: 'NA' if x is None else f'{x:.{n}f}'
    print(' | '.join([k, str(a['total']), str(r['n']), f(100 * r['frac_of_cloud'], 2) + '%', f(r['anchors']['p50_mm']), f(r['anchors']['p90_mm']), f(r['anchors']['c5'], 3),
        f(r['matched']['p50_mm'] if r['matched'] else None), f(r['matched']['c5'] if r['matched'] else None, 3), f(r['null_noise1cm_p50_change_pct']),
        '|', str(b['n']), f(100 * b['frac_of_cloud'], 3) + '%', f(b['anchors']['p50_mm']) if b['anchors'] else 'NA', f(b['anchors']['c5'], 3) if b['anchors'] else 'NA',
        f(b['matched']['p50_mm'] if b['matched'] else None), f(b['matched']['c5'] if b['matched'] else None, 3), f(a['band_planes']['inlier_frac_1cm'], 3), f(a['band_planes']['rms_mm']), '|', f(a['scene']['p50_mm']), f(a['scene']['c5'], 3)]))
print('plane chance', d['band_planes_uniform_random_chance'])
