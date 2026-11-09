import json,sys,time,urllib.parse
sys.argv=['x','ultrafast']
exec(open('/tmp/mc_test.py').read().split('\ndef main():')[0])
LEN='/dev/v4l/by-path/platform-fc880000.usb-usb-0:1:1.0-video-index0'
src=create_camera('lb-cam',LEN)
for (w,h) in ((320,180),(864,480),(1280,720),(1600,896)):
    set_mode(src,w,h,30); time.sleep(2)
    sid=vk('lb-vk',src,w,h)
    time.sleep(10); req('POST','/profile/reset'); time.sleep(20)
    prof=profile()
    row={}
    for node in ('lb-cam','lb-vk'):
        for st,v in prof.get(node,{}).items():
            if st in ('age','decode','detect','process','interval','wait','grab','dequeue','capture','copy'):
                row['%s %s'%(node,st)]=round(v['MeanMs'],2)
    print(w,h,json.dumps(row),flush=True)
    req('DELETE','/sink/delete?SinkID=%d'%sid)
req('DELETE','/source/delete?SourceID=%d'%src)
