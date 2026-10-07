import json,sys
d=json.load(open(sys.argv[1])); p=d['pooled_epe_px']['c05']; s=d['per_pair_distribution']
print(sys.argv[1], 'n',d['n_pairs'],'pooled c05 p50 %.4f p90 %.3f p99 %.3f max %.1f'%(p['p50'],p['p90'],p['p99'],p['max']),
 '| per-pair c05 p50: med %.4f p95 %.3f max %.3f'%(s['epe_c05_p50']['p50'],s['epe_c05_p50']['p95'],s['epe_c05_p50']['max']),
 '| cross05 med %.4f max %.4f | certabs max med %.3f'%(s['cross_c05']['p50'],s['cross_c05']['max'],s['cert_maxabs']['p50']))
