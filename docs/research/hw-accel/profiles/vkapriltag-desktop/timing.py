import subprocess, os, csv, statistics as st, json
env=dict(os.environ); env["PATH"]=env["PATH"]+r";C:\Users\yojob\vcpkg\installed\x64-windows\bin;C:\lumen-deps\x64\vc18\bin;C:\lumen-deps\bin"
out={}
for rep in range(3):
  for v in ("old","new"):
    for dec in (1,2):
      f=rf"C:\lumen-bench\t-{v}-d{dec}-{rep}.csv"
      if os.path.exists(f): os.remove(f)
      subprocess.run([rf"C:\lumen-bench\b-{v}\tools\apriltag_vulkan_validate.exe","--data",r"C:\lumen-bench\corpus","--decimation",str(dec),"--iterations","300","--csv",f],capture_output=True,env=env,timeout=1800)
      rows=list(csv.DictReader(open(f)))
      out.setdefault(f"{v}-d{dec}",[]).append(dict(
        gpu=st.median(float(r["gpu_ms_median"]) for r in rows),
        quad=st.median(float(r["quad_decode_ms_median"]) for r in rows),
        tag=st.median(float(r["tag_decode_ms_median"]) for r in rows),
        total=st.median(float(r["pipeline_ms_median"]) for r in rows),
        mean_total=st.mean(float(r["pipeline_ms_median"]) for r in rows),
        n=len(rows)))
      print(v,dec,rep,out[f"{v}-d{dec}"][-1],flush=True)
json.dump(out,open(r"C:\lumen-bench\timing.json","w"))
