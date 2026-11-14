"""机位几何约束；单位米。与 stage/camera_safety.js 保持一致。

围板内边、倒角、门洞来自 stage.js/buildBoards，最高 0.358m。
用有厚度的盒体而非无限细线检查视线；留 0.10m 高度余量。
特写强制在场内拍，远景允许在高处越墙俯拍。
"""
import math

SEGMENTS = [((-5,-4),(5,-4)),((5,-4),(6,-3)),((6,-3),(6,-1)),
            ((6,1),(6,3)),((6,3),(5,4)),((5,4),(-5,4)),
            ((-5,4),(-6,3)),((-6,3),(-6,1)),((-6,-1),(-6,-3)),((-6,-3),(-5,-4))]
HEIGHT = .46
MARGIN = .18

def inside(x, z, margin=MARGIN):
    return abs(x) <= 6-margin and abs(z) <= 4-margin and abs(x)+abs(z) <= 9-margin*math.sqrt(2)

def project_field(x, z, margin=.4):
    k = min(1., (6-margin)/max(abs(x),1e-9), (4-margin)/max(abs(z),1e-9),
            (9-margin*math.sqrt(2))/max(abs(x)+abs(z),1e-9))
    return x*k, z*k

def constrain_player(x, z):
    """仅庆祝外推使用：圆盘半径0.3，倒角按垂距留足半径。"""
    x=max(-5.7,min(5.7,x));z=max(-3.7,min(3.7,z))
    excess=abs(x)+abs(z)-(9-.3*math.sqrt(2))
    if excess>0:
        x-=math.copysign(excess/2,x);z-=math.copysign(excess/2,z)
    return x,z

def position(c, f=None):
    cp = math.cos(c['pitch'])
    p = [c['tx']+c['d']*math.sin(c['yaw'])*cp, c.get('ty',0)+c['d']*math.sin(c['pitch']),
         c['tz']+c['d']*math.cos(c['yaw'])*cp]
    if f and f.get('shake',0):
        sh, t = f['shake'], f['wall']
        p[0] += sh*math.sin(t*37.1)*.06
        p[1] += sh*math.sin(t*41.7+1)*.05
        p[2] += sh*math.sin(t*29.3+2)*.04
    return p

def blocked(p, q):
    for (ax,az),(bx,bz) in SEGMENTS:
        length = math.hypot(bx-ax,bz-az)
        ux,uz = (bx-ax)/length,(bz-az)/length
        def local(v):
            x,z = v[0]-ax,v[2]-az
            return (x*ux+z*uz, v[1], -x*uz+z*ux)
        a,b = local(p),local(q)
        lo,hi=0.,1.
        for k,(mn,mx) in enumerate(((-.035,length+.035),(-.01,HEIGHT),(-.045,.045))):
            d=b[k]-a[k]
            if abs(d)<1e-12:
                if a[k]<mn or a[k]>mx: hi=-1; break
            else:
                t0,t1=(mn-a[k])/d,(mx-a[k])/d
                lo=max(lo,min(t0,t1)); hi=min(hi,max(t0,t1))
        if lo<=hi and hi>0 and lo<1: return True
    return False

def footprint(c, f=None, extent=.25):
    """主体构图区与地面的交点；避免主体周围拍成黑底，保留原场馆远景。"""
    p=position(c,f);sy,cy=math.sin(c['yaw']),math.cos(c['yaw'])
    sp,cp=math.sin(c['pitch']),math.cos(c['pitch'])
    forward=(-sy*cp,-sp,-cy*cp);right=(cy,0,-sy);up=(-sy*sp,cp,-cy*sp)
    cr,sr=math.cos(c.get('roll',0)),math.sin(c.get('roll',0));scale=math.tan(math.radians(c['fov']/2))
    out=[]
    for x,z in ((-1,-1),(1,-1),(1,1),(-1,1)):
        x*=extent;z*=extent
        ray=[forward[k]+scale*(x*16/9*(right[k]*cr+up[k]*sr)+z*(up[k]*cr-right[k]*sr)) for k in range(3)]
        if ray[1]>=-1e-9:return []
        t=-p[1]/ray[1];out.append((p[0]+ray[0]*t,p[2]+ray[2]*t))
    return out

def violations(c, kind, f=None, subjects=()):
    p=position(c,f)
    if not all(math.isfinite(v) for v in (*p,c['tx'],c.get('ty',0),c['tz'],c['fov'],c['pitch'])):
        return ['非有限机位']
    out=[]
    if p[1]<.65 or (not inside(p[0],p[2]) and (kind=='close' or p[1]<2.0)):
        out.append('场外低机位或特写越界')
    if c['pitch'] < math.radians(c['fov']/2+5)+abs(c.get('roll',0)):
        out.append('视锥接近地平线/场外虚空')
    if kind=='close':
        corners=footprint(c,f)
        if len(corners)!=4 or not all(inside(x,z,-.04) for x,z in corners):out.append('特写主体构图区越出场地')
    obstructed=blocked(p,(c['tx'],c.get('ty',0),c['tz']))
    for q in subjects:
        # 球/圆盘有体积：中心在门柱边缘不等于主体完全不可见。
        radius=q[3] if len(q)>3 else .15
        dx,dz=p[0]-q[0],p[2]-q[2]; length=math.hypot(dx,dz) or 1
        front=(q[0]+dx/length*radius,q[1],q[2]+dz/length*radius)
        top=(q[0],q[1]+radius,q[2])
        if all(blocked(p,v) for v in (q,front,top)): obstructed=True
    if obstructed: out.append('围墙/广告牌遮挡视线')
    return out

def safe_camera(c, kind):
    """朝场心一侧退让、提高俯角；只改变机位，保留原焦点与视角。"""
    q=dict(c)
    tx,tz=project_field(c['tx'],c['tz'],.4)
    # 从场内朝边缘拍摄；无径向方向时沿主机位 +Z 一侧。
    r=math.hypot(tx,tz)
    dx,dz=(-tx/r,-tz/r) if r>.1 else (0.,1.)
    if kind=='close':
        # 陡俯拍并向内平移构图区，主体留在场边，避免半幅场外黑底。
        # 平移量逐帧由同一解析轨迹决定，不依赖上一帧，便于随机抽帧复现。
        q.update(ty=.25,d=3.6,pitch=math.radians(68),yaw=math.atan2(dx,dz),roll=0.)
        for k in range(81):
            shift=k*.04
            q['tx'],q['tz']=tx+dx*shift,tz+dz*shift
            corners=footprint(q)
            if all(inside(x,z,.06) for x,z in corners):return q
        raise ValueError('无法在场内放置特写视锥')
    reach=2.6 if kind=='close' else 4.0
    px,pz=project_field(tx+dx*reach,tz+dz*reach,.65)
    # 目标轻微朝场内移，避免主体后面全是场外地面。
    q['tx'],q['tz']=tx+dx*.35,tz+dz*.35
    q['ty']=.25 if kind=='close' else .1
    py=3.8 if kind=='close' else 6.0
    vx,vz=px-q['tx'],pz-q['tz']; vy=py-q['ty']
    q.update(d=math.sqrt(vx*vx+vy*vy+vz*vz),pitch=math.atan2(vy,math.hypot(vx,vz)),
             yaw=math.atan2(vx,vz),roll=0.)
    return q

def protect_shots(frames, subject_fn):
    """同一 shot 的任意一帧违规，整镜头使用安全轨迹，避免单帧跳机位。"""
    start=0
    while start<len(frames):
        end=start+1
        while end<len(frames) and frames[end]['shot']==frames[start]['shot']: end+=1
        shot=frames[start:end]
        issues=[violations(f['cam'],f['kind'],f,subject_fn(f)) for f in shot]
        if any(issues):
            for f,reasons in zip(shot,issues):
                original=f['cam']
                f['cam']=safe_camera(original,f['kind'])
                f['cameraSafety']={'original':original,'reasons':reasons or ['同镜头连续安全轨迹']}
                remaining=violations(f['cam'],f['kind'],f,subject_fn(f))
                if remaining: raise ValueError(f"安全机位仍违规：帧{f['i']} {remaining}")
        start=end
