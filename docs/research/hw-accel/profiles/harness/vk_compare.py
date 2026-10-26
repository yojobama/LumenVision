import json, sys, collections
def load(path):
    rows = [json.loads(l) for l in open(path) if l.startswith('{')]
    by = collections.defaultdict(list)
    for r in rows:
        by[r['workload']].append(r)
    return by
def mean(v):
    v = [x for x in v if x is not None]
    return sum(v) / len(v) if v else None
def val(r, k, i=0):
    x = r.get(k)
    if x is None: return None
    return x[i] if isinstance(x, list) else x
def agg(rs, k, i=0):
    return mean([val(r, k, i) for r in rs])
old = load(sys.argv[1]); new = load(sys.argv[2]) if len(sys.argv) > 2 else None
metrics = [('service_cores', 0), ('gpu_load', 0), ('lenovo-vk detect', 0), ('lenovo-vk detect', 1), ('lenovo-vk vk-gpu:submit+wait', 0), ('lenovo-vk vk-quads', 0), ('lenovo-vk vk-tags', 0),
           ('lenovo-vk age', 0), ('lenovo-vk interval', 0), ('left-vk detect', 0), ('left-vk detect', 1), ('left-vk vk-gpu:submit+wait', 0), ('left-vk vk-quads', 0), ('left-vk age', 0),
           ('right-vk detect', 0), ('right-vk age', 0), ('left-npu infer', 0), ('left-npu age', 0), ('lenovo decode', 0), ('stereo decode', 0)]
for w in ('V', 'V1', 'W', 'W1', 'B'):
    if w not in old: continue
    print('== workload %s (%d runs old%s)' % (w, len(old[w]), ', %d new' % len(new[w]) if new and w in new else ''))
    for k, i in metrics:
        a = agg(old[w], k, i)
        if a is None: continue
        name = k + (' p95' if i == 1 else (' mean' if isinstance(old[w][0].get(k), list) else ''))
        b = agg(new[w], k, i) if new and w in new else None
        if b is None:
            print('   %-34s %8.2f' % (name, a))
        else:
            print('   %-34s %8.2f -> %8.2f  (%+.1f%%)' % (name, a, b, 100.0 * (b - a) / a if a else 0))
