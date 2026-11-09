import subprocess, json, os, re, numpy as np
env=dict(os.environ); env["PATH"]=env["PATH"]+r";C:\Users\yojob\vcpkg\installed\x64-windows\bin;C:\lumen-deps\x64\vc18\bin;C:\lumen-deps\bin"
gt=json.load(open(r"C:\lumen-bench\gt.json"))
exe=r"C:\lumen-bench\b-new\tools\apriltag_vulkan_validate.exe"
def parse(path):
    out=[]
    for l in open(path):
        if l.startswith("F") or not l.strip(): continue
        p=l.split(); out.append((int(p[0]),np.array(list(map(float,p[5:13]))).reshape(4,2)))
    return out
def err(det,g):
    g=np.array(g)+0.5; best=1e9
    for rev in (False,True):
        d=det[::-1] if rev else det
        for s in range(4): best=min(best,np.sqrt(((np.roll(d,s,0)-g)**2).sum(1)).mean())
    return best
groups={}
for name in sorted(gt):
    base=re.sub(r"_\d+$","",name)
    if not (base.startswith(("blur","noise","lowcontrast","dark","bright")) or base in ("size64","size100")): continue
    dump=r"C:\lumen-bench\dump-g.txt"
    if os.path.exists(dump): os.remove(dump)
    subprocess.run([exe,"--data",rf"C:\lumen-bench\corpus\{name}.pgm","--decimation","1","--dump-detections",dump],capture_output=True,env=env)
    dets=parse(dump) if os.path.exists(dump) else []
    g={t["id"]:t["corners"] for t in gt[name]}
    rec=groups.setdefault(base,{"total":0,"found":0,"errs":[]})
    for i,c in dets:
        if i in g: rec["found"]+=1; rec["errs"].append(err(c,g[i]))
    rec["total"]+=len(g)
for base,r in sorted(groups.items()):
    e=np.array(r["errs"])
    print("%-14s found %2d/%2d  median %.3f px  p95 %.3f px"%(base,r["found"],r["total"],np.median(e) if len(e) else float('nan'),np.percentile(e,95) if len(e) else float('nan')))
