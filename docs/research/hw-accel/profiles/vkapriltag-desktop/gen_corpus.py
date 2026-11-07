import cv2, numpy as np, json, os, math, random
random.seed(7); np.random.seed(7)
W,H=1280,800
D=cv2.aruco.getPredefinedDictionary(cv2.aruco.DICT_APRILTAG_36h11)
CELL=16
def tag_img(i):
    m=cv2.aruco.generateImageMarker(D,i,8*CELL)        # black border included: 8 cells
    pad=CELL
    return cv2.copyMakeBorder(m,pad,pad,pad,pad,cv2.BORDER_CONSTANT,value=255)  # 1-cell white quiet zone
def place(canvas,i,cx,cy,size,tilt,yaw,roll,persp=0.0):
    t=tag_img(i); n=t.shape[0]; s=size*n/(8*CELL)  # px scale so black square == size px
    # 3D rotate a plane of side s centred at origin, project with f
    f=1000.0
    R,_=cv2.Rodrigues(np.array([math.radians(tilt),0,0])); R2,_=cv2.Rodrigues(np.array([0,math.radians(yaw),0])); R3,_=cv2.Rodrigues(np.array([0,0,math.radians(roll)]))
    Rm=R3@R2@R
    pts=np.array([[-s/2,-s/2,0],[s/2,-s/2,0],[s/2,s/2,0],[-s/2,s/2,0]],float)  # full padded image corners
    z0=f*3.0
    proj=[]
    for p in pts:
        q=Rm@p; q=q+np.array([0,0,z0]); proj.append([f*q[0]/q[2]+cx, f*q[1]/q[2]+cy])
    proj=np.array(proj,np.float32)
    src=np.array([[0,0],[n,0],[n,n],[0,n]],np.float32)
    M=cv2.getPerspectiveTransform(src,proj)
    layer=cv2.warpPerspective(t,M,(W,H),flags=cv2.INTER_AREA,borderValue=0)
    mask=cv2.warpPerspective(np.full_like(t,255),M,(W,H),flags=cv2.INTER_NEAREST,borderValue=0)
    canvas[mask>0]=layer[mask>0]
    # ground-truth corners: outer corners of the black square (inset 1 cell of padding)
    k=CELL
    gsrc=np.array([[[k,k],[n-k,k],[n-k,n-k],[k,n-k]]],np.float32)
    g=cv2.perspectiveTransform(gsrc,M)[0]
    return g.tolist()
def finish(canvas,blur,noise,lo,hi,seed):
    img=canvas.astype(np.float32)/255.0
    img=lo+(hi-lo)*img
    if blur>0: img=cv2.GaussianBlur(img,(0,0),blur)
    rng=np.random.default_rng(seed)
    if noise>0: img=img+rng.normal(0,noise/255.0,img.shape)
    return np.clip(img*255+0.5,0,255).astype(np.uint8)
scen=[]
def add(name,tags,blur=0,noise=0,lo=0.0,hi=1.0):
    scen.append((name,tags,blur,noise,lo,hi))
ids=iter(random.sample(range(587),587))
def one(size,tilt=0,yaw=0,roll=None,pos=None): return [(next(ids),640+random.uniform(-380,380),400+random.uniform(-250,250),size,tilt,yaw,random.uniform(0,360) if roll is None else roll)]
R=6
for s in (16,24,32,48,64,100,200,320):
  for r in range(R): add(f"size{s}_{r}",one(s,tilt=20,yaw=15))
for t in (0,30,50,60,68,75):
  for r in range(R): add(f"tilt{t}_{r}",one(120,tilt=t,yaw=20))
for t in (50,70):
  for r in range(R): add(f"yaw{t}_{r}",one(120,tilt=0,yaw=t,roll=33))
for b in (0.8,1.2,1.6,2):
  for r in range(R): add(f"blur{b}_{r}",one(60,tilt=25,yaw=10),blur=b)
for n in (8,20,40):
  for r in range(R): add(f"noise{n}_{r}",one(60,tilt=25,yaw=10),noise=n)
for r in range(R):
  add(f"lowcontrast_{r}",one(70,tilt=20,yaw=10),lo=0.35,hi=0.55,noise=3)
  add(f"dark_{r}",one(70,tilt=20,yaw=10),lo=0.0,hi=0.2,noise=2)
  add(f"bright_{r}",one(70,tilt=20,yaw=10),lo=0.8,hi=1.0,noise=2)
def grid(n,size,jit_tilt):
    cols=int(math.ceil(math.sqrt(n*W/H))); rows=int(math.ceil(n/cols)); out=[]
    for k in range(n):
        r,c=divmod(k,cols)
        out.append((next(ids),(c+0.5)*W/cols,(r+0.5)*H/rows,size,random.uniform(0,jit_tilt),random.uniform(-jit_tilt,jit_tilt),random.uniform(0,360)))
    return out
for n,sz in ((4,120),(12,80),(30,50),(40,40)): add(f"dense{n}",grid(n,sz,50),noise=4,blur=0.8)
for k in range(10): add(f"mixed{k}",grid(8,random.choice([40,60,90]),65),blur=random.choice([0,1,1.5]),noise=random.choice([2,8,15]))
gt={}
for name,tags,blur,noise,lo,hi in scen:
    canvas=np.full((H,W),128,np.uint8); recs=[]
    for (i,cx,cy,size,tilt,yaw,roll) in tags:
        g=place(canvas,i,cx,cy,size,tilt,yaw,roll)
        recs.append({"id":i,"corners":g})
    img=finish(canvas,blur,noise,lo,hi,hash(name)&0xffff)
    cv2.imwrite(f"/c/lumen-bench/corpus/{name}.pgm" if False else f"C:/lumen-bench/corpus/{name}.pgm",img)
    gt[name]=recs
json.dump(gt,open("C:/lumen-bench/gt.json","w"))
print(len(scen),"images")
