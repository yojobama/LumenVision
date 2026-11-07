import subprocess, json, os, re, sys, csv, statistics as st, math, glob
import numpy as np
env=dict(os.environ); env["PATH"]=env["PATH"]+r";C:\Users\yojob\vcpkg\installed\x64-windows\bin;C:\lumen-deps\x64\vc18\bin;C:\lumen-deps\bin"
gt=json.load(open(r"C:\lumen-bench\gt.json"))
names=sorted(gt)
def exe(v): return rf"C:\lumen-bench\b-{v}\tools\apriltag_vulkan_validate.exe"
def run(v,args,timeout=300):
    return subprocess.run([exe(v)]+args,capture_output=True,text=True,env=env,timeout=timeout)
def parse_dump(path):
    out=[]
    for l in open(path):
        if l.startswith("F") or not l.strip(): continue
        p=l.split(); out.append((int(p[0]),float(p[2]),np.array(list(map(float,p[5:13]))).reshape(4,2)))
    return out
def corner_err(det,g):
    g=np.array(g)+GT_SHIFT; best=1e9
    for rev in (False,True):
        d=det[::-1] if rev else det
        for s in range(4):
            e=np.sqrt(((np.roll(d,s,0)-g)**2).sum(1)).mean()
            best=min(best,e)
    return best
GT_SHIFT=float(os.environ.get("GT_SHIFT","0.5"))
res={}
for v in ("old","new"):
  for dec in (1,2):
    key=f"{v}-d{dec}"; found=wrong=total=0; errs=[]; refFound=0; per={}
    for n in names:
        img=rf"C:\lumen-bench\corpus\{n}.pgm"; dump=rf"C:\lumen-bench\dump-{key}.txt"
        if os.path.exists(dump): os.remove(dump)
        r=run(v,["--data",img,"--decimation",str(dec),"--dump-detections",dump])
        dets=parse_dump(dump) if os.path.exists(dump) else []
        refids=set()
        m=re.search(r"Reference tag IDs: \[(.*?)\]",r.stdout)
        if m and m.group(1).strip(): refids=set(int(x) for x in m.group(1).split(","))
        gids={t["id"]:t["corners"] for t in gt[n]}
        total+=len(gids)
        f=0
        for (i,marg,c) in dets:
            if i in gids:
                f+=1; errs.append(corner_err(c,gids[i]))
            else: wrong+=1
        found+=f; refFound+=len(refids & set(gids)); per[n]=(f,len(gids))
    errs=np.array(errs)
    res[key]=dict(total=total,found=found,wrong=wrong,refFound=refFound,median=float(np.median(errs)),p95=float(np.percentile(errs,95)),mean=float(errs.mean()),per=per)
    print(key,{k:v2 for k,v2 in res[key].items() if k!='per'},flush=True)
json.dump(res,open(r"C:\lumen-bench\accuracy.json","w"))
