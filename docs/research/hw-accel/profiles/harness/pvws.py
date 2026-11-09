import websocket, msgpack, json, sys, time
def connect(): return websocket.create_connection("ws://192.168.68.125:5800/websocket_data", timeout=10)
def drain(ws, seconds=3, keys=None):
    out=[]; t0=time.time()
    ws.settimeout(1.0)
    while time.time()-t0 < seconds:
        try: m = ws.recv()
        except Exception: continue
        if isinstance(m,bytes):
            d = msgpack.unpackb(m, raw=False)
            out.append(d)
    return out
if __name__=="__main__":
    ws=connect()
    msgs=drain(ws,5)
    for d in msgs:
        for k,v in d.items():
            if k in ("log",): continue
            s=json.dumps(v,default=repr)
            print(k, len(s)); 
            if k in ("fullsettings","settings","cameraSettings","unmatched"): print(s[:200])
    full=[d for d in msgs if "settings" in d and isinstance(d["settings"],dict)]
    if full:
        st=full[0]["settings"]; print("settings keys:",list(st.keys()))
        for k in st:
            if "camera" in k.lower() or "match" in k.lower(): print(k, json.dumps(st[k],default=repr)[:1500])
    for d in msgs:
        if "cameraSettings" in d: print("cameraSettings", json.dumps(d["cameraSettings"],default=repr)[:500])
