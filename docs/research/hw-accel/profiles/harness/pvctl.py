import json, time, urllib.request, msgpack, websocket, pvws
BASE="http://192.168.68.125:5800"
def post(path, obj):
    r=urllib.request.Request(BASE+path, data=json.dumps(obj).encode(), method="POST", headers={"Content-Type":"application/json"})
    try:
        with urllib.request.urlopen(r, timeout=30) as resp: return resp.status, resp.read().decode()[:200]
    except urllib.error.HTTPError as e: return e.code, e.read().decode()[:200]
def latest(ws, key, seconds=4):
    out=None
    for d in pvws.drain(ws, seconds):
        if key in d: out=d[key]
    return out
def send(ws, obj): ws.send_binary(msgpack.packb(obj, use_bin_type=True))
