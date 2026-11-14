// 与 ../camera_safety.py 相同的几何；前端在震动后、真实插值位置上二次校验。
const segments = [[[-5,-4],[5,-4]],[[5,-4],[6,-3]],[[6,-3],[6,-1]],[[6,1],[6,3]],[[6,3],[5,4]],[[5,4],[-5,4]],[[-5,4],[-6,3]],[[-6,3],[-6,1]],[[-6,-1],[-6,-3]],[[-6,-3],[-5,-4]]];
export function inside(x,z,m=.18) {
  return Math.abs(x)<=6-m && Math.abs(z)<=4-m && Math.abs(x)+Math.abs(z)<=9-m*Math.sqrt(2);
}
export function constrainPlayer(x,z) {
  x=Math.max(-5.7,Math.min(5.7,x));z=Math.max(-3.7,Math.min(3.7,z));
  const excess=Math.abs(x)+Math.abs(z)-(9-.3*Math.sqrt(2));
  if(excess>0){x-=Math.sign(x)*excess/2;z-=Math.sign(z)*excess/2;}
  return [x,z];
}
function project(x,z,m=.4) {
  const k=Math.min(1,(6-m)/Math.max(Math.abs(x),1e-9),(4-m)/Math.max(Math.abs(z),1e-9),(9-m*Math.sqrt(2))/Math.max(Math.abs(x)+Math.abs(z),1e-9));
  return [x*k,z*k];
}
export function position(c,f={}) {
  const cp=Math.cos(c.pitch), sh=f.shake||0, t=f.wall||0;
  return [c.tx+c.d*Math.sin(c.yaw)*cp+sh*Math.sin(t*37.1)*.06,
    (c.ty||0)+c.d*Math.sin(c.pitch)+sh*Math.sin(t*41.7+1)*.05,
    c.tz+c.d*Math.cos(c.yaw)*cp+sh*Math.sin(t*29.3+2)*.04];
}
export function blocked(p,q) {
  for(const [[ax,az],[bx,bz]] of segments) {
    const len=Math.hypot(bx-ax,bz-az), ux=(bx-ax)/len, uz=(bz-az)/len;
    const local=v=>[(v[0]-ax)*ux+(v[2]-az)*uz,v[1],-(v[0]-ax)*uz+(v[2]-az)*ux];
    const a=local(p), b=local(q), bounds=[[-.035,len+.035],[-.01,.46],[-.045,.045]];
    let lo=0,hi=1;
    for(let k=0;k<3;k++) {
      const [mn,mx]=bounds[k], d=b[k]-a[k];
      if(Math.abs(d)<1e-12) { if(a[k]<mn||a[k]>mx) {hi=-1;break;} }
      else {const t0=(mn-a[k])/d,t1=(mx-a[k])/d;lo=Math.max(lo,Math.min(t0,t1));hi=Math.min(hi,Math.max(t0,t1));}
    }
    if(lo<=hi&&hi>0&&lo<1)return true;
  }
  return false;
}
export function footprint(c,f={},extent=.25) {
  const p=position(c,f),sy=Math.sin(c.yaw),cy=Math.cos(c.yaw),sp=Math.sin(c.pitch),cp=Math.cos(c.pitch);
  const forward=[-sy*cp,-sp,-cy*cp],right=[cy,0,-sy],up=[-sy*sp,cp,-cy*sp];
  const cr=Math.cos(c.roll||0),sr=Math.sin(c.roll||0),scale=Math.tan(c.fov*Math.PI/360),out=[];
  for(let [x,z] of [[-1,-1],[1,-1],[1,1],[-1,1]]) {
    x*=extent;z*=extent;
    const ray=forward.map((v,k)=>v+scale*(x*16/9*(right[k]*cr+up[k]*sr)+z*(up[k]*cr-right[k]*sr)));
    if(ray[1]>=-1e-9)return [];
    const t=-p[1]/ray[1];out.push([p[0]+ray[0]*t,p[2]+ray[2]*t]);
  }
  return out;
}
export function violations(c,kind,f={},subjects=[]) {
  const p=position(c,f), out=[];
  if(![...p,c.tx,c.ty||0,c.tz,c.fov,c.pitch].every(Number.isFinite)) return ['非有限机位'];
  if(p[1]<.65||(!inside(p[0],p[2])&&(kind==='close'||p[1]<2)))out.push('场外低机位或特写越界');
  if(c.pitch<(c.fov/2+5)*Math.PI/180+Math.abs(c.roll||0))out.push('视锥接近地平线/场外虚空');
  if(kind==='close') {
    const corners=footprint(c,f);
    if(corners.length!==4||!corners.every(([x,z])=>inside(x,z,-.04)))out.push('特写主体构图区越出场地');
  }
  let obstructed=blocked(p,[c.tx,c.ty||0,c.tz]);
  for(const q of subjects) {
    const r=q[3]??.15, dx=p[0]-q[0],dz=p[2]-q[2],len=Math.hypot(dx,dz)||1;
    const front=[q[0]+dx/len*r,q[1],q[2]+dz/len*r],top=[q[0],q[1]+r,q[2]];
    if([q,front,top].every(v=>blocked(p,v)))obstructed=true;
  }
  if(obstructed)out.push('围墙/广告牌遮挡视线');
  return out;
}
export function safeCamera(c,kind) {
  const q={...c}, [tx,tz]=project(c.tx,c.tz), r=Math.hypot(tx,tz);
  const [dx,dz]=r>.1?[-tx/r,-tz/r]:[0,1], reach=kind==='close'?2.6:4;
  if(kind==='close') {
    Object.assign(q,{ty:.25,d:3.6,pitch:68*Math.PI/180,yaw:Math.atan2(dx,dz),roll:0});
    for(let k=0;k<=80;k++) {
      q.tx=tx+dx*k*.04;q.tz=tz+dz*k*.04;
      if(footprint(q).every(([x,z])=>inside(x,z,.06)))return q;
    }
    throw new Error('无法在场内放置特写视锥');
  }
  const [px,pz]=project(tx+dx*reach,tz+dz*reach,.65);
  q.tx=tx+dx*.35;q.tz=tz+dz*.35;q.ty=kind==='close'?.25:.1;
  const py=kind==='close'?3.8:6,vx=px-q.tx,vz=pz-q.tz,vy=py-q.ty;
  return {...q,d:Math.hypot(vx,vy,vz),pitch:Math.atan2(vy,Math.hypot(vx,vz)),yaw:Math.atan2(vx,vz),roll:0};
}
