import json,sys,time
sys.argv=['x','ultrafast']
exec(open('/tmp/mc_test.py').read().split('\ndef main():')[0])
LEN='/dev/v4l/by-path/platform-fc880000.usb-usb-0:1:1.0-video-index0'
src=create_camera('ex-cam',LEN); set_mode(src,1280,720,30); time.sleep(2)
sid=vk('ex-vk',src,1280,720); time.sleep(5)
ctl=req('GET','/cameraSource/%d/controlList'%src)
for c in ctl:
    n=(c.get('Name') or '').lower()
    if any(k in n for k in ('exposure','gain','brightness','power line','backlight','frequency')): print(json.dumps({k:c[k] for k in ('Id','Name','Min','Max','Value','Default') if k in c}))
auto=[c for c in ctl if 'auto' in (c.get('Name') or '').lower() and 'exposure' in (c.get('Name') or '').lower()]
absx=[c for c in ctl if 'exposure' in (c.get('Name') or '').lower() and 'absolute' in (c.get('Name') or '').lower() or (c.get('Name') or '').lower()=='exposure time, absolute']
print('auto ctl',[c['Name'] for c in auto],'abs ctl',[c['Name'] for c in absx])
def measure(label):
    time.sleep(3); req('POST','/profile/reset'); time.sleep(12)
    p=profile()
    print(label,{k:round(p['ex-vk'][k]['MeanMs'],2) for k in ('age','detect') if k in p.get('ex-vk',{})},{k:round(p['ex-cam'][k]['MeanMs'],2) for k in ('age','decode') if k in p.get('ex-cam',{})},flush=True)
measure('as found')
if auto and absx:
    a=auto[0]; e=absx[0]
    for mode in (a['Id'],):
        pass
    # manual exposure mode is usually 1 (V4L2_EXPOSURE_MANUAL) and aperture priority 3
    print('auto value now',a.get('Value'),'abs now',e.get('Value'),'range',e.get('Min'),e.get('Max'))
    req('PATCH','/cameraSource/%d/control?controlId=%d&value=1'%(src,a['Id']))
    for v in (5,20,80,150,312):
        if e.get('Min') is not None and v<e['Min']: continue
        req('PATCH','/cameraSource/%d/control?controlId=%d&value=%d'%(src,e['Id'],v))
        measure('manual exposure %d (x100us)'%v)
    req('PATCH','/cameraSource/%d/control?controlId=%d&value=%d'%(src,a['Id'],a.get('Default',3) if a.get('Default') is not None else 3))
req('DELETE','/sink/delete?SinkID=%d'%sid); req('DELETE','/source/delete?SourceID=%d'%src)
