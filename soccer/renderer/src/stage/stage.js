// 第 8 集足球 · 电影感转播渲染（页面端）。
// Python（render_soccer.py）负责：读回放、排镜头和时间线；本页只做一件事：
//   window.setup(data)   载入一局比赛（状态表、事件、选手、队色），建场景
//   window.renderFrame(f) 按帧描述画一帧（f 里写死了 比赛时刻 / 机位 / 字幕 / 转场），不读时钟、不用随机数，同样输入同样输出。
// 世界坐标：X = x/1000 - 6，Z = y/1000 - 4（场地中心为原点，单位 = 1 米 = 回放里的 1000），Y 向上。

import * as THREE from 'three';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { ShaderPass } from 'three/addons/postprocessing/ShaderPass.js';
import { BokehPass } from 'three/addons/postprocessing/BokehPass.js';
import { position as cameraPosition, violations as cameraViolations, safeCamera, constrainPlayer } from './camera_safety.js';

const W = 1920, H = 1080;
const RP = 0.3, RB = 0.15;           // 球员、球半径
const NET_D = 0.7, GOAL_H = 0.62, GOAL_HW = 1.0;
const $ = (id) => document.getElementById(id);

// ------------------------------------------------------------ 小工具
function hash(n) {               // 确定性伪随机 [0,1)
  let x = (n | 0) ^ 0x9e3779b9;
  x = Math.imul(x ^ (x >>> 16), 0x85ebca6b);
  x = Math.imul(x ^ (x >>> 13), 0xc2b2ae35);
  x ^= x >>> 16;
  return (x >>> 0) / 4294967296;
}
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
const lerp = (a, b, k) => a + (b - a) * k;
const smooth = (k) => { k = clamp(k, 0, 1); return k * k * (3 - 2 * k); };
const easeOut = (k) => 1 - Math.pow(1 - clamp(k, 0, 1), 3);
function catmull(p0, p1, p2, p3, t) {
  const t2 = t * t, t3 = t2 * t;
  return 0.5 * ((2 * p1) + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 + (-p0 + 3 * p1 - 3 * p2 + p3) * t3);
}
function col(hex) { return new THREE.Color(hex); }
function lum(hex) { const h = hex.replace('#', ''); const r = parseInt(h.slice(0, 2), 16) / 255, g = parseInt(h.slice(2, 4), 16) / 255, b = parseInt(h.slice(4, 6), 16) / 255; return 0.2126 * r + 0.7152 * g + 0.0722 * b; }
function loadImg(src) {
  return new Promise((res, rej) => { const im = new Image(); im.onload = () => res(im); im.onerror = () => rej(new Error('载入失败 ' + src)); im.src = src; });
}
function canvasTex(cv, srgb = true) {
  const t = new THREE.CanvasTexture(cv);
  if (srgb) t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 8;
  return t;
}
function radialTex(size = 128, stops = [[0, 'rgba(255,255,255,1)'], [1, 'rgba(255,255,255,0)']]) {
  const cv = document.createElement('canvas'); cv.width = cv.height = size;
  const g = cv.getContext('2d'); const gr = g.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
  for (const [k, c] of stops) gr.addColorStop(k, c);
  g.fillStyle = gr; g.fillRect(0, 0, size, size);
  return canvasTex(cv, false);
}

// ------------------------------------------------------------ 渲染器与后期
const canvas = $('gl');
const renderer = new THREE.WebGLRenderer({ canvas, antialias: false, preserveDrawingBuffer: true, powerPreference: 'high-performance' });
renderer.setPixelRatio(1);
renderer.setSize(W, H, false);
renderer.shadowMap.enabled = true;
renderer.shadowMap.type = THREE.PCFSoftShadowMap;
renderer.toneMapping = THREE.ACESFilmicToneMapping;
renderer.toneMappingExposure = 1.0;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x05070c);
scene.fog = new THREE.Fog(0x05070c, 30, 70);
const camera = new THREE.PerspectiveCamera(30, W / H, 0.1, 200);

const rt = new THREE.WebGLRenderTarget(W, H, { type: THREE.HalfFloatType, samples: 4 });
const composer = new EffectComposer(renderer, rt);
composer.setPixelRatio(1);
composer.setSize(W, H);
composer.addPass(new RenderPass(scene, camera));
const bokeh = new BokehPass(scene, camera, { focus: 8, aperture: 0.0, maxblur: 0.006 });
bokeh.enabled = false;
composer.addPass(bokeh);
const bloom = new UnrealBloomPass(new THREE.Vector2(W, H), 0.45, 0.55, 0.82);
composer.addPass(bloom);
composer.addPass(new OutputPass());
const grade = new ShaderPass({
  uniforms: {
    tDiffuse: { value: null }, uVig: { value: 0.32 }, uGrain: { value: 0.035 }, uSeed: { value: 0 },
    uFlash: { value: 0 }, uSat: { value: 1.0 }, uWarm: { value: 0.0 }, uFade: { value: 0 },
  },
  vertexShader: `varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position,1.0); }`,
  fragmentShader: `
    uniform sampler2D tDiffuse; uniform float uVig, uGrain, uSeed, uFlash, uSat, uWarm, uFade; varying vec2 vUv;
    float h(vec2 p){ return fract(sin(dot(p, vec2(12.9898,78.233)) + uSeed*0.618) * 43758.5453); }
    void main(){
      vec3 c = texture2D(tDiffuse, vUv).rgb;
      float l = dot(c, vec3(0.2126,0.7152,0.0722));
      c = mix(vec3(l), c, uSat);
      // 轻微分离色调：暗部偏青、亮部偏暖
      c += vec3(-0.010, 0.004, 0.014) * (1.0 - l) + vec3(0.016, 0.006, -0.012) * l;
      c = mix(c, c * vec3(1.06, 0.98, 0.88), uWarm);
      vec2 q = vUv - 0.5; q.x *= 1.25;
      float v = smoothstep(0.85, 0.18, length(q));
      c *= mix(1.0 - uVig, 1.0, v);
      c += (h(vUv * 1000.0) - 0.5) * uGrain;
      c += uFlash;
      c *= 1.0 - uFade;
      gl_FragColor = vec4(c, 1.0);
    }`,
});
composer.addPass(grade);

// ------------------------------------------------------------ 灯光
scene.add(new THREE.HemisphereLight(0xaac4ff, 0x1d2a18, 0.55));
scene.add(new THREE.AmbientLight(0xffffff, 0.12));
function shadowLight(colr, inten, pos, size) {
  const l = new THREE.DirectionalLight(colr, inten);
  l.position.set(...pos); l.target.position.set(0, 0, 0);
  l.castShadow = true;
  l.shadow.mapSize.set(size, size);
  const c = l.shadow.camera; c.left = -9; c.right = 9; c.top = 7; c.bottom = -7; c.near = 1; c.far = 60;
  l.shadow.bias = -0.0004; l.shadow.normalBias = 0.02; l.shadow.radius = 3;
  scene.add(l); scene.add(l.target);
  return l;
}
const key = shadowLight(0xfff1dc, 2.3, [-9, 16, 7], 2048);       // 主泛光灯（偏暖）
const fill = shadowLight(0xcfe0ff, 1.0, [10, 13, -6], 1024);     // 对角补光（偏冷），双影子是夜场泛光的样子
const goalLight = new THREE.PointLight(0xffffff, 0, 6, 1.6);
scene.add(goalLight);

// ------------------------------------------------------------ 草皮与球场线
const FIELD_PX = 256;           // 每米像素
const GX0 = -6.0 - NET_D - 0.1, GX1 = 6.0 + NET_D + 0.1, GZ0 = -4, GZ1 = 4;
function grassCanvas() {
  const w = Math.round((GX1 - GX0) * FIELD_PX), h = Math.round((GZ1 - GZ0) * FIELD_PX);
  const cv = document.createElement('canvas'); cv.width = w; cv.height = h;
  const g = cv.getContext('2d');
  const X = (x) => (x - GX0) * FIELD_PX, Z = (z) => (z - GZ0) * FIELD_PX;
  g.fillStyle = '#2f7a35'; g.fillRect(0, 0, w, h);
  // 修剪条纹（沿 x 方向一米一条）
  for (let i = -7; i < 7; i++) {
    g.fillStyle = (i & 1) ? 'rgba(255,255,230,0.055)' : 'rgba(0,30,0,0.06)';
    g.fillRect(X(i), 0, FIELD_PX, h);
  }
  // 细噪点（草叶）
  const id = g.getImageData(0, 0, w, h); const d = id.data;
  let s = 12345;
  for (let p = 0; p < d.length; p += 4) {
    s = (Math.imul(s, 1103515245) + 12345) | 0;
    const r = ((s >>> 8) & 255) / 255 - 0.5;
    const k = 1 + r * 0.16;
    d[p] = d[p] * k; d[p + 1] = d[p + 1] * (k + r * 0.04); d[p + 2] = d[p + 2] * k;
  }
  g.putImageData(id, 0, 0);
  // 大块明暗（磨损、浇水不均）
  for (let i = 0; i < 90; i++) {
    const x = hash(i * 3 + 1) * w, y = hash(i * 3 + 2) * h, r = (0.4 + hash(i * 3 + 3) * 1.4) * FIELD_PX;
    const gr = g.createRadialGradient(x, y, 0, x, y, r);
    const dark = hash(i * 7) < 0.5;
    gr.addColorStop(0, dark ? 'rgba(10,30,5,0.045)' : 'rgba(200,220,120,0.03)'); gr.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = gr; g.fillRect(x - r, y - r, 2 * r, 2 * r);
  }
  // 球门区域磨损
  for (const sx of [-1, 1]) {
    const x = X(sx * 5.3), y = Z(0), r = 1.6 * FIELD_PX;
    const gr = g.createRadialGradient(x, y, 0, x, y, r);
    gr.addColorStop(0, 'rgba(90,80,40,0.16)'); gr.addColorStop(1, 'rgba(0,0,0,0)');
    g.fillStyle = gr; g.fillRect(x - r, y - r, 2 * r, 2 * r);
  }
  // 白线
  const LW = 0.05 * FIELD_PX;
  g.strokeStyle = 'rgba(245,248,240,0.92)'; g.lineWidth = LW; g.lineJoin = 'miter';
  const ins = 0.12, c = 1.0;
  g.beginPath();
  const oct = [[-6 + c, -4], [6 - c, -4], [6, -4 + c], [6, 4 - c], [6 - c, 4], [-6 + c, 4], [-6, 4 - c], [-6, -4 + c]];
  // 往里缩 ins
  const octIn = oct.map(([x, z]) => [x - Math.sign(x) * ins * (Math.abs(x) > 5.5 ? 1 : 0.414), z - Math.sign(z) * ins * (Math.abs(z) > 3.5 ? 1 : 0.414)]);
  octIn.forEach(([x, z], i) => (i ? g.lineTo(X(x), Z(z)) : g.moveTo(X(x), Z(z))));
  g.closePath(); g.stroke();
  g.beginPath(); g.moveTo(X(0), Z(-4 + ins)); g.lineTo(X(0), Z(4 - ins)); g.stroke();
  g.beginPath(); g.arc(X(0), Z(0), 1.0 * FIELD_PX, 0, Math.PI * 2); g.stroke();
  g.fillStyle = 'rgba(245,248,240,0.92)';
  g.beginPath(); g.arc(X(0), Z(0), 0.07 * FIELD_PX, 0, Math.PI * 2); g.fill();
  for (const sx of [-1, 1]) {
    const gx = sx * (6 - ins);
    // 禁区、小禁区、点球点、弧（装饰：本规则没有点球，只为让观众一眼认出"这是足球场"）
    g.strokeRect(Math.min(X(gx), X(gx - sx * 1.8)), Z(-2.2), 1.8 * FIELD_PX, 4.4 * FIELD_PX);
    g.strokeRect(Math.min(X(gx), X(gx - sx * 0.6)), Z(-1.4), 0.6 * FIELD_PX, 2.8 * FIELD_PX);
    g.beginPath(); g.arc(X(gx - sx * 1.25), Z(0), 0.05 * FIELD_PX, 0, Math.PI * 2); g.fill();
    g.beginPath();
    const a0 = Math.acos(0.55 / 0.9);
    if (sx > 0) g.arc(X(gx - 1.25), Z(0), 0.9 * FIELD_PX, Math.PI - a0, Math.PI + a0);
    else g.arc(X(gx + 1.25), Z(0), 0.9 * FIELD_PX, -a0, a0);
    g.stroke();
  }
  return cv;
}

function fieldShape() {
  const c = 1.0, s = new THREE.Shape();
  // 带两个球门口凹槽的八边形（Shape 的 y 对应 -Z，下面旋转平面时翻过来）
  const pts = [[-6 + c, -4], [6 - c, -4], [6, -4 + c], [6, -GOAL_HW], [6 + NET_D, -GOAL_HW], [6 + NET_D, GOAL_HW], [6, GOAL_HW], [6, 4 - c], [6 - c, 4],
    [-6 + c, 4], [-6, 4 - c], [-6, GOAL_HW], [-6 - NET_D, GOAL_HW], [-6 - NET_D, -GOAL_HW], [-6, -GOAL_HW], [-6, -4 + c]];
  pts.forEach(([x, z], i) => (i ? s.lineTo(x, z) : s.moveTo(x, z)));
  s.closePath();
  return s;
}

function buildField() {
  const geo = new THREE.ShapeGeometry(fieldShape(), 1);
  // ShapeGeometry 在 XY 平面：把 (x, y) 当作 (X, Z) 放到地上
  const pos = geo.attributes.position; const uv = geo.attributes.uv;
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i), z = pos.getY(i);
    pos.setXYZ(i, x, 0, z);
    uv.setXY(i, (x - GX0) / (GX1 - GX0), 1 - (z - GZ0) / (GZ1 - GZ0));
  }
  geo.index && geo.index.array.reverse();   // 翻面朝上
  geo.computeVertexNormals();
  const tex = canvasTex(grassCanvas());
  const mat = new THREE.MeshStandardMaterial({ map: tex, roughness: 0.92, metalness: 0.0 });
  const m = new THREE.Mesh(geo, mat); m.receiveShadow = true;
  scene.add(m);
  // 场外地面（跑道色）
  const out = new THREE.Mesh(new THREE.PlaneGeometry(80, 60), new THREE.MeshStandardMaterial({ color: 0x0d1310, roughness: 1 }));
  out.rotation.x = -Math.PI / 2; out.position.y = -0.01; out.receiveShadow = true; scene.add(out);
}

// ------------------------------------------------------------ 围板（球撞墙反弹）+ LED 广告条
let ledTex = null, ledMat = null, boardTopMat = null;
function ledCanvas(text, bg0, bg1, fg) {
  const cv = document.createElement('canvas'); cv.width = 2048; cv.height = 96;
  const g = cv.getContext('2d');
  const gr = g.createLinearGradient(0, 0, 0, 96); gr.addColorStop(0, bg0); gr.addColorStop(1, bg1);
  g.fillStyle = gr; g.fillRect(0, 0, 2048, 96);
  g.font = '900 54px NSC'; g.fillStyle = fg; g.textBaseline = 'middle';
  const wd = g.measureText(text).width; let x = 20;
  while (x < 2048) { g.fillText(text, x, 50); x += wd + 60; }
  // LED 点阵质感
  g.fillStyle = 'rgba(0,0,0,0.28)';
  for (let y = 0; y < 96; y += 4) g.fillRect(0, y, 2048, 1);
  for (let xx = 0; xx < 2048; xx += 4) g.fillRect(xx, 0, 1, 96);
  return cv;
}
function buildBoards(brand) {
  const BH = 0.34, T = 0.08;
  ledTex = canvasTex(ledCanvas(`${brand}   ·   2v2 足球   ·   `, '#0b1a3a', '#06102a', '#e8f0ff'));
  ledTex.wrapS = THREE.RepeatWrapping;
  ledMat = new THREE.MeshStandardMaterial({ color: 0x000000, emissive: 0xffffff, emissiveMap: ledTex, emissiveIntensity: 0.55, roughness: 0.4 });
  const bodyMat = new THREE.MeshStandardMaterial({ color: 0x141a26, roughness: 0.35, metalness: 0.3 });
  boardTopMat = new THREE.MeshStandardMaterial({ color: 0x000000, emissive: 0x9fd8ff, emissiveIntensity: 1.2 });
  const c = 1.0;
  const segs = [   // 顺时针，墙的内侧边
    [[-6 + c, -4], [6 - c, -4]], [[6 - c, -4], [6, -4 + c]], [[6, -4 + c], [6, -GOAL_HW]], [[6, GOAL_HW], [6, 4 - c]], [[6, 4 - c], [6 - c, 4]],
    [[6 - c, 4], [-6 + c, 4]], [[-6 + c, 4], [-6, 4 - c]], [[-6, 4 - c], [-6, GOAL_HW]], [[-6, -GOAL_HW], [-6, -4 + c]], [[-6, -4 + c], [-6 + c, -4]],
  ];
  for (const [[x0, z0], [x1, z1]] of segs) {
    const L = Math.hypot(x1 - x0, z1 - z0), ang = Math.atan2(z1 - z0, x1 - x0);
    const nx = (z1 - z0) / L, nz = -(x1 - x0) / L;     // 朝场外的法线（顺时针走向的左手边）
    const grp = new THREE.Group();
    grp.position.set((x0 + x1) / 2 + nx * T / 2, 0, (z0 + z1) / 2 + nz * T / 2);
    grp.rotation.y = -ang;
    const body = new THREE.Mesh(new THREE.BoxGeometry(L + T * 0.8, BH, T), bodyMat);
    body.position.y = BH / 2; body.castShadow = true; body.receiveShadow = true; grp.add(body);
    // LED 面朝场内
    const pg = new THREE.PlaneGeometry(L, BH * 0.62);
    const uv = pg.attributes.uv; for (let i = 0; i < uv.count; i++) uv.setX(i, uv.getX(i) * L / 6.0);
    const led = new THREE.Mesh(pg, ledMat);
    led.position.set(0, BH * 0.52, T / 2 + 0.002); led.rotation.y = Math.PI; led.rotation.y = 0;
    // 平面默认朝 +Z（组局部坐标），组局部 +Z = 场外法线方向的反面？按几何算：局部 +Z 经旋转后指向 (sin(ang)... ) 直接两面都放
    const led2 = led.clone(); led2.position.z = -T / 2 - 0.002; led2.rotation.y = Math.PI;
    grp.add(led); grp.add(led2);
    const top = new THREE.Mesh(new THREE.BoxGeometry(L + T * 0.8, 0.018, T + 0.01), boardTopMat);
    top.position.y = BH + 0.009; grp.add(top);
    scene.add(grp);
  }
}

// ------------------------------------------------------------ 球门与球网
const nets = [];
function netTex() {
  const cv = document.createElement('canvas'); cv.width = cv.height = 128;
  const g = cv.getContext('2d'); g.clearRect(0, 0, 128, 128);
  g.strokeStyle = 'rgba(255,255,255,0.85)'; g.lineWidth = 3;
  g.beginPath();
  for (let i = 0; i <= 128; i += 32) { g.moveTo(i, 0); g.lineTo(i, 128); g.moveTo(0, i); g.lineTo(128, i); }
  g.stroke();
  const t = canvasTex(cv); t.wrapS = t.wrapT = THREE.RepeatWrapping; return t;
}
function buildGoals() {
  const postMat = new THREE.MeshStandardMaterial({ color: 0xf8f8f8, roughness: 0.3, metalness: 0.1, emissive: 0x222222 });
  const nt = netTex();
  for (const s of [-1, 1]) {
    const g = new THREE.Group();
    const R = 0.045;
    for (const z of [-GOAL_HW, GOAL_HW]) {
      const p = new THREE.Mesh(new THREE.CylinderGeometry(R, R, GOAL_H, 16), postMat);
      p.position.set(s * 6, GOAL_H / 2, z); p.castShadow = true; g.add(p);
      const bp = new THREE.Mesh(new THREE.CylinderGeometry(R * 0.6, R * 0.6, GOAL_H, 8), postMat);
      bp.position.set(s * (6 + NET_D), GOAL_H / 2, z); g.add(bp);
      const top = new THREE.Mesh(new THREE.CylinderGeometry(R * 0.6, R * 0.6, NET_D, 8), postMat);
      top.rotation.z = Math.PI / 2; top.position.set(s * (6 + NET_D / 2), GOAL_H, z); g.add(top);
    }
    const bar = new THREE.Mesh(new THREE.CylinderGeometry(R, R, 2 * GOAL_HW + 2 * R, 16), postMat);
    bar.rotation.x = Math.PI / 2; bar.position.set(s * 6, GOAL_H, 0); bar.castShadow = true; g.add(bar);
    const netMat = new THREE.MeshStandardMaterial({ map: nt, alphaMap: null, transparent: true, side: THREE.DoubleSide, depthWrite: false, roughness: 1, color: 0xffffff, opacity: 0.75 });
    const mk = (w, h, nu, nv) => { const t = nt.clone(); t.repeat.set(nu, nv); t.needsUpdate = true; const m = netMat.clone(); m.map = t; return m; };
    // 后网（可鼓起）
    const back = new THREE.PlaneGeometry(2 * GOAL_HW, GOAL_H, 24, 8);
    const backMesh = new THREE.Mesh(back, mk(0, 0, 14, 4.5));
    backMesh.rotation.y = Math.PI / 2; backMesh.position.set(s * (6 + NET_D), GOAL_H / 2, 0);
    g.add(backMesh);
    const base = back.attributes.position.array.slice();
    // 侧网、顶网
    for (const z of [-GOAL_HW, GOAL_HW]) {
      const sm = new THREE.Mesh(new THREE.PlaneGeometry(NET_D, GOAL_H), mk(0, 0, 5, 4.5));
      sm.position.set(s * (6 + NET_D / 2), GOAL_H / 2, z); g.add(sm);
    }
    const tm = new THREE.Mesh(new THREE.PlaneGeometry(NET_D, 2 * GOAL_HW), mk(0, 0, 5, 14));
    tm.rotation.x = -Math.PI / 2; tm.position.set(s * (6 + NET_D / 2), GOAL_H, 0); g.add(tm);
    scene.add(g);
    nets.push({ s, back, base });
  }
}
function setNetBulge(s, amt, z) {   // amt：后网被顶出去多远（米）
  for (const n of nets) {
    const a = n.back.attributes.position, arr = a.array;
    for (let i = 0; i < a.count; i++) {
      const px = n.base[i * 3], py = n.base[i * 3 + 1];
      // 平面局部：x 沿球门宽（旋转后对应 -Z/+Z），y 沿高度；局部 z 是鼓出方向
      let d = 0;
      if (n.s === s && amt > 0) {
        const lz = -px * (s > 0 ? 1 : 1);
        const dz = (s > 0 ? -px : -px) - z, dy = py + GOAL_H / 2 - 0.15;
        d = amt * Math.exp(-(dz * dz) / 0.18 - (dy * dy) / 0.12);
      }
      arr[i * 3 + 2] = n.base[i * 3 + 2] - d * (n.s > 0 ? -1 : 1) * -1;
    }
    a.needsUpdate = true;
  }
}

// ------------------------------------------------------------ 看台、灯塔（远景，只为氛围）
function buildStadium() {
  const crowd = document.createElement('canvas'); crowd.width = 1024; crowd.height = 256;
  const g = crowd.getContext('2d'); g.fillStyle = '#0a0d14'; g.fillRect(0, 0, 1024, 256);
  const cc = ['#3b4b6b', '#5a6a8a', '#2a3550', '#6b5a4a', '#4a5f80', '#7a8090'];
  for (let y = 4; y < 256; y += 10) {
    g.fillStyle = 'rgba(40,48,66,0.9)'; g.fillRect(0, y + 6, 1024, 2);
    for (let x = 0; x < 1024; x += 5) {
      const hh = hash(x * 977 + y * 13);
      if (hh < 0.55) { g.fillStyle = cc[Math.floor(hash(x + y * 31) * cc.length)]; g.globalAlpha = 0.35 + 0.4 * hash(x * 3 + y); g.fillRect(x, y, 3, 5); }
    }
  }
  g.globalAlpha = 1;
  const ct = canvasTex(crowd); ct.wrapS = THREE.RepeatWrapping; ct.repeat.set(3, 1);
  const mat = new THREE.MeshStandardMaterial({ map: ct, roughness: 1, color: 0x6c7488, emissive: 0x0c1018, emissiveIntensity: 0.5 });
  const mk = (w, x, z, ry) => {
    const m = new THREE.Mesh(new THREE.PlaneGeometry(w, 6), mat);
    m.position.set(x, 2.2, z); m.rotation.set(-0.0, ry, 0);
    const tilt = new THREE.Group(); tilt.add(m); scene.add(tilt);
    m.rotation.x = 0; m.rotateX(-0.55);
    return m;
  };
  mk(26, 0, -7.6, 0);
  mk(16, -9.6, 0, Math.PI / 2);
  mk(16, 9.6, 0, -Math.PI / 2);
  // 灯塔光晕
  const glow = radialTex(128, [[0, 'rgba(255,255,255,1)'], [0.15, 'rgba(255,244,220,0.9)'], [0.5, 'rgba(255,220,170,0.15)'], [1, 'rgba(0,0,0,0)']]);
  for (const [x, z] of [[-11, -8], [11, -8], [-12, 7], [12, 7]]) {
    const sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: glow, color: 0xfff4e0, blending: THREE.AdditiveBlending, depthWrite: false, fog: false }));
    sp.position.set(x, 9, z); sp.scale.set(3.2, 3.2, 1); scene.add(sp);
    const pole = new THREE.Mesh(new THREE.CylinderGeometry(0.08, 0.12, 9, 8), new THREE.MeshStandardMaterial({ color: 0x1a1f2a }));
    pole.position.set(x, 4.5, z); scene.add(pole);
  }
}

// ------------------------------------------------------------ 球
function soccerTex() {
  const w = 1024, h = 512;
  const cv = document.createElement('canvas'); cv.width = w; cv.height = h;
  const g = cv.getContext('2d'); const id = g.createImageData(w, h); const d = id.data;
  const phi = (1 + Math.sqrt(5)) / 2;
  const iv = [[-1, phi, 0], [1, phi, 0], [-1, -phi, 0], [1, -phi, 0], [0, -1, phi], [0, 1, phi], [0, -1, -phi], [0, 1, -phi], [phi, 0, -1], [phi, 0, 1], [-phi, 0, -1], [-phi, 0, 1]]
    .map((v) => { const l = Math.hypot(...v); return v.map((x) => x / l); });
  // 20 个面心（六边形中心）
  const faces = [];
  for (let a = 0; a < 12; a++) for (let b = a + 1; b < 12; b++) for (let c = b + 1; c < 12; c++) {
    const dab = iv[a][0] * iv[b][0] + iv[a][1] * iv[b][1] + iv[a][2] * iv[b][2];
    const dac = iv[a][0] * iv[c][0] + iv[a][1] * iv[c][1] + iv[a][2] * iv[c][2];
    const dbc = iv[b][0] * iv[c][0] + iv[b][1] * iv[c][1] + iv[b][2] * iv[c][2];
    if (dab > 0.4 && dac > 0.4 && dbc > 0.4) {
      const v = [0, 1, 2].map((k) => iv[a][k] + iv[b][k] + iv[c][k]); const l = Math.hypot(...v); faces.push(v.map((x) => x / l));
    }
  }
  const cent = iv.map((v) => [v, 1]).concat(faces.map((v) => [v, 0]));
  for (let y = 0; y < h; y++) {
    const th = (y + 0.5) / h * Math.PI;
    for (let x = 0; x < w; x++) {
      const ph = (x + 0.5) / w * Math.PI * 2;
      const p = [Math.sin(th) * Math.cos(ph), Math.cos(th), Math.sin(th) * Math.sin(ph)];
      let b1 = -2, b2 = -2, k1 = 0;
      for (const [c, k] of cent) {
        const dd = (c[0] * p[0] + c[1] * p[1] + c[2] * p[2]) * (k ? 1.035 : 1.0);   // 五边形略放大
        if (dd > b1) { b2 = b1; b1 = dd; k1 = k; } else if (dd > b2) b2 = dd;
      }
      const seam = b1 - b2 < 0.012;
      let v = k1 ? 28 : 238;
      if (seam) v = k1 ? 40 : 150;
      const o = (y * w + x) * 4; d[o] = v; d[o + 1] = v; d[o + 2] = v + (k1 ? 4 : 0); d[o + 3] = 255;
    }
  }
  g.putImageData(id, 0, 0);
  return canvasTex(cv);
}
let ball, ballShadow;
function buildBall() {
  ball = new THREE.Mesh(new THREE.SphereGeometry(RB, 48, 32), new THREE.MeshStandardMaterial({ map: soccerTex(), roughness: 0.42, metalness: 0.0, emissive: 0xffffff, emissiveIntensity: 0.0 }));
  ball.castShadow = true; scene.add(ball);
  ballShadow = new THREE.Mesh(new THREE.CircleGeometry(RB * 1.5, 32), new THREE.MeshBasicMaterial({ map: radialTex(64, [[0, 'rgba(0,0,0,0.55)'], [1, 'rgba(0,0,0,0)']]), transparent: true, depthWrite: false }));
  ballShadow.rotation.x = -Math.PI / 2; ballShadow.position.y = 0.004; scene.add(ballShadow);
}

// ------------------------------------------------------------ 拖尾
const TRAIL_N = 28;
let trail;
function buildTrail() {
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(TRAIL_N * 2 * 3), 3));
  geo.setAttribute('color', new THREE.BufferAttribute(new Float32Array(TRAIL_N * 2 * 4), 4));
  const idx = [];
  for (let i = 0; i < TRAIL_N - 1; i++) { const a = i * 2; idx.push(a, a + 1, a + 2, a + 1, a + 3, a + 2); }
  geo.setIndex(idx);
  trail = new THREE.Mesh(geo, new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide, fog: false }));
  trail.frustumCulled = false; scene.add(trail);
}

// ------------------------------------------------------------ 球员
const players = [];
async function avatarCanvas(url, ring, label, size = 256) {
  const im = await loadImg(url);
  const cv = document.createElement('canvas'); cv.width = size; cv.height = Math.round(size * 1.32);
  const g = cv.getContext('2d');
  const cx = size / 2, cy = size * 0.47, r = size * 0.43;
  g.save(); g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); g.closePath(); g.clip();
  g.fillStyle = '#121a30'; g.fillRect(0, 0, size, size);
  // 往期头像自带一圈青色描边：放大 8% 把它裁到圈外
  const k = 1.1, s2 = 2 * r * k; g.drawImage(im, cx - s2 / 2, cy - s2 / 2, s2, s2);
  g.restore();
  g.lineWidth = size * 0.05; g.strokeStyle = ring; g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); g.stroke();
  g.lineWidth = size * 0.012; g.strokeStyle = 'rgba(0,0,0,0.6)'; g.beginPath(); g.arc(cx, cy, r + size * 0.03, 0, Math.PI * 2); g.stroke();
  if (label) {
    g.font = `800 ${Math.round(size * 0.165)}px NSC`; g.textAlign = 'center'; g.textBaseline = 'middle';
    const tw = g.measureText(label).width + size * 0.14, ph = size * 0.23, py = size * 1.08;
    g.fillStyle = ring; roundRect(g, cx - tw / 2, py - ph / 2, tw, ph, ph / 2); g.fill();
    g.fillStyle = lum(ring) > 0.55 ? '#0b1020' : '#ffffff';
    g.fillText(label, cx, py + size * 0.008);
  }
  return cv;
}
function roundRect(g, x, y, w, h, r) { g.beginPath(); g.moveTo(x + r, y); g.arcTo(x + w, y, x + w, y + h, r); g.arcTo(x + w, y + h, x, y + h, r); g.arcTo(x, y + h, x, y, r); g.arcTo(x, y, x + w, y, r); g.closePath(); }

function puckGeo() {
  const pts = [[0, 0], [0.27, 0], [0.293, 0.012], [0.3, 0.04], [0.3, 0.11], [0.292, 0.138], [0.27, 0.152], [0.2, 0.158], [0, 0.158]].map(([x, y]) => new THREE.Vector2(x, y));
  return new THREE.LatheGeometry(pts, 64);
}
const glowRingTex = (() => {
  const cv = document.createElement('canvas'); cv.width = cv.height = 256; const g = cv.getContext('2d');
  const gr = g.createRadialGradient(128, 128, 0, 128, 128, 128);
  gr.addColorStop(0, 'rgba(255,255,255,0)'); gr.addColorStop(0.62, 'rgba(255,255,255,0)'); gr.addColorStop(0.68, 'rgba(255,255,255,0.9)');
  gr.addColorStop(0.74, 'rgba(255,255,255,0.35)'); gr.addColorStop(1, 'rgba(255,255,255,0)');
  g.fillStyle = gr; g.fillRect(0, 0, 256, 256); return canvasTex(cv, false);
})();
async function buildPlayers(info) {
  const geo = puckGeo();
  for (let p = 0; p < 4; p++) {
    const pi = info[p], c = col(pi.color);
    const grp = new THREE.Group();
    const body = new THREE.Mesh(geo, new THREE.MeshPhysicalMaterial({ color: c, roughness: 0.32, metalness: 0.05, clearcoat: 1, clearcoatRoughness: 0.12 }));
    body.castShadow = true; body.receiveShadow = true; grp.add(body);
    const ringM = new THREE.MeshStandardMaterial({ color: 0x000000, emissive: c, emissiveIntensity: 2.2 });
    const ring = new THREE.Mesh(new THREE.TorusGeometry(0.235, 0.012, 8, 64), ringM);
    ring.rotation.x = Math.PI / 2; ring.position.y = 0.159; grp.add(ring);
    const avc = await avatarCanvas(pi.avatar, pi.color, null, 256);
    const topTex = canvasTex(avc);
    topTex.repeat.set(1, 256 / avc.height); topTex.offset.set(0, 1 - 256 / avc.height);
    const top = new THREE.Mesh(new THREE.CircleGeometry(0.215, 48), new THREE.MeshStandardMaterial({ map: topTex, roughness: 0.25, metalness: 0.0 }));
    top.rotation.x = -Math.PI / 2; top.position.y = 0.1595;
    // 头像正对机位方向（机位默认在 +Z 一侧）：圆盘的贴图上方朝 -Z
    grp.add(top);
    const glow = new THREE.Mesh(new THREE.PlaneGeometry(1.25, 1.25), new THREE.MeshBasicMaterial({ map: glowRingTex, color: c, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, opacity: 0.8 }));
    glow.rotation.x = -Math.PI / 2; glow.position.y = 0.006;
    scene.add(glow);
    const sc = await avatarCanvas(pi.avatar, pi.color, pi.label, 256);
    const sp = new THREE.Sprite(new THREE.SpriteMaterial({ map: canvasTex(sc), depthTest: false, depthWrite: false, fog: false }));
    sp.center.set(0.5, 0.0); sp.renderOrder = 10;
    scene.add(sp);
    scene.add(grp);
    players.push({ grp, body, ring, ringM, glow, sp, top, aspect: sc.height / sc.width, color: c });
  }
}

// ------------------------------------------------------------ 踢球特效
const KICK_LIFE = 9;   // 步
const flareTex = radialTex(128, [[0, 'rgba(255,255,255,1)'], [0.25, 'rgba(255,255,255,0.55)'], [1, 'rgba(255,255,255,0)']]);
const sparkTex = radialTex(64, [[0, 'rgba(255,255,255,1)'], [0.4, 'rgba(255,255,255,0.6)'], [1, 'rgba(255,255,255,0)']]);
const kickPool = [];
function buildKickPool() {
  for (let i = 0; i < 6; i++) {
    const ring = new THREE.Mesh(new THREE.RingGeometry(0.9, 1.0, 72), new THREE.MeshBasicMaterial({ transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide, fog: false }));
    ring.rotation.x = -Math.PI / 2; ring.position.y = 0.012; scene.add(ring);
    const flare = new THREE.Sprite(new THREE.SpriteMaterial({ map: flareTex, blending: THREE.AdditiveBlending, depthWrite: false, transparent: true, fog: false }));
    scene.add(flare);
    const N = 26;
    const pg = new THREE.BufferGeometry(); pg.setAttribute('position', new THREE.BufferAttribute(new Float32Array(N * 3), 3));
    const pts = new THREE.Points(pg, new THREE.PointsMaterial({ map: sparkTex, size: 0.07, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, fog: false }));
    pts.frustumCulled = false; scene.add(pts);
    kickPool.push({ ring, flare, pts, N });
  }
}

// ------------------------------------------------------------ 进球彩带
const CONF_N = 240;
let confetti;
function buildConfetti() {
  const g = new THREE.PlaneGeometry(0.045, 0.028);
  confetti = new THREE.InstancedMesh(g, new THREE.MeshStandardMaterial({ side: THREE.DoubleSide, roughness: 0.6, emissive: 0xffffff, emissiveIntensity: 0.08 }), CONF_N);
  confetti.instanceMatrix.setUsage(THREE.DynamicDrawUsage);
  confetti.frustumCulled = false; confetti.visible = false; scene.add(confetti);
}

// ============================================================ 比赛数据与插值
let D = null;       // setup 传进来的数据
let S = null;       // Float64Array 视图：S[t*27 + k]
const NS = 27;
let goalAt = new Map(), resetAt = new Set(), ballQ = [];
const post = [];    // 每个进球之后的外推轨迹（球、4 名球员），每步一项，世界坐标

function sv(t, k) { return S[t * NS + k]; }
function wx(x) { return x / 1000 - 6; }
function wz(y) { return y / 1000 - 4; }

function buildPost() {
  for (const g of D.goals) {
    const s = g.team === 0 ? 1 : -1;      // A 队（0）进右门
    let bx = wx(g.endB[0]), bz = wz(g.endB[1]), vx = g.endB[2] / 1000, vz = g.endB[3] / 1000;
    const back = 6 + NET_D - RB;
    const B = [], P = [[], [], [], []];
    let maxBulge = 0;
    for (let u = 0; u <= 400; u++) {
      const depth = s * bx;
      const bulge = Math.max(0, depth - back);
      B.push([bx, bz, bulge]);
      maxBulge = Math.max(maxBulge, bulge);
      bx += vx; bz += vz;
      vx *= 0.97; vz *= 0.97;
      if (s * bx > 6) {                       // 在网里：网吸能
        vx *= 0.9; vz *= 0.9;
        if (Math.abs(bz) > GOAL_HW - RB) { bz = Math.sign(bz) * (GOAL_HW - RB); vz *= -0.35; }
        if (s * bx > back) {
          const pen = s * bx - back;
          vx -= s * pen * 0.6;                // 网像弹簧往回拉
          vx *= 0.55;
          if (pen > 0.2) { bx = s * (back + 0.2); if (s * vx > 0) vx *= -0.25; }
        }
        if (s * bx < 6.05 && s * vx < 0) { vx *= -0.3; }   // 别让球滚回场里
      }
    }
    for (let p = 0; p < 4; p++) {
      let x = wx(g.endP[p][0]), z = wz(g.endP[p][1]), vx2 = g.endP[p][2] / 1000, vz2 = g.endP[p][3] / 1000;
      [x, z] = constrainPlayer(x, z);
      for (let u = 0; u <= 400; u++) {
        P[p].push([x, z]);
        x += vx2; z += vz2; vx2 *= 0.9; vz2 *= 0.9;
        [x, z] = constrainPlayer(x, z);
      }
    }
    post.push({ s, B, P });
  }
}

function buildBallRotation() {
  ballQ = [new THREE.Quaternion()];
  const q = new THREE.Quaternion(), axis = new THREE.Vector3(), dq = new THREE.Quaternion();
  for (let t = 0; t < D.n; t++) {
    let dx, dz;
    if (goalAt.has(t)) { const g = D.goals[goalAt.get(t)]; dx = wx(g.endB[0]) - wx(sv(t, 3)); dz = wz(g.endB[1]) - wz(sv(t, 4)); }
    else if (resetAt.has(t + 1)) { dx = 0; dz = 0; }
    else { dx = wx(sv(t + 1, 3)) - wx(sv(t, 3)); dz = wz(sv(t + 1, 4)) - wz(sv(t, 4)); }
    const L = Math.hypot(dx, dz);
    if (L > 1e-6) { axis.set(dz / L, 0, -dx / L); dq.setFromAxisAngle(axis, L / RB); q.premultiply(dq); }
    ballQ.push(q.clone());
  }
}

// 某虚拟时刻的局面。gi != null 且 tau >= 该球进球步+1 时走外推。
function stateAt(tau, gi) {
  const out = { b: [0, 0], p: [[0, 0], [0, 0], [0, 0], [0, 0]], bulge: 0, side: 0, q: new THREE.Quaternion() };
  if (gi != null && gi >= 0) {
    const g = D.goals[gi];
    if (tau >= g.t + 1) {
      const u = tau - (g.t + 1), pp = post[gi];
      const i = Math.min(Math.floor(u), 399), f = Math.min(u - i, 1);
      const A = pp.B[i], Bn = pp.B[i + 1];
      out.b = [lerp(A[0], Bn[0], f), lerp(A[1], Bn[1], f)]; out.bulge = lerp(A[2], Bn[2], f); out.side = pp.s;
      for (let p = 0; p < 4; p++) { const a = pp.P[p][i], b = pp.P[p][i + 1]; out.p[p] = [lerp(a[0], b[0], f), lerp(a[1], b[1], f)]; }
      // 球在网里继续转一点
      const q = ballQ[g.t + 1] ? ballQ[g.t + 1].clone() : new THREE.Quaternion();
      const dq = new THREE.Quaternion().setFromAxisAngle(new THREE.Vector3(0, 0, -pp.s), Math.min(u, 30) * 0.25);
      out.q = q.premultiply(dq);
      return out;
    }
  }
  tau = clamp(tau, 0, D.n);
  let i = Math.floor(tau), f = tau - i;
  if (i >= D.n) { i = D.n - 1; f = 1; }
  const A = (k) => sv(i, k);
  let Bf;
  if (goalAt.has(i)) {
    const g = D.goals[goalAt.get(i)];
    Bf = (k) => {   // 进球那一步的终点：球越过门线的位置
      if (k === 3) return g.endB[0]; if (k === 4) return g.endB[1];
      const p = Math.floor((k - 7) / 5), r = (k - 7) % 5;
      if (p >= 0 && r < 2) return g.endP[p][r];
      return sv(i, k);
    };
  } else if (resetAt.has(i + 1)) Bf = A;
  else Bf = (k) => sv(i + 1, k);
  out.b = [wx(lerp(A(3), Bf(3), f)), wz(lerp(A(4), Bf(4), f))];
  const smoothOK = !goalAt.has(i) && !resetAt.has(i + 1) && i >= 1 && !resetAt.has(i) && i + 2 <= D.n && !goalAt.has(i + 1) && !resetAt.has(i + 2);
  for (let p = 0; p < 4; p++) {
    const kx = 7 + p * 5, kz = kx + 1;
    if (smoothOK) {
      out.p[p] = [wx(catmull(sv(i - 1, kx), sv(i, kx), sv(i + 1, kx), sv(i + 2, kx), f)), wz(catmull(sv(i - 1, kz), sv(i, kz), sv(i + 1, kz), sv(i + 2, kz), f))];
    } else out.p[p] = [wx(lerp(A(kx), Bf(kx), f)), wz(lerp(A(kz), Bf(kz), f))];
  }
  out.q = ballQ[i].clone().slerp(ballQ[Math.min(i + 1, ballQ.length - 1)], f);
  return out;
}

// 这一段连续比赛从哪一步开始（拖尾不跨过重开）
function segStart(tau) {
  let s = 0;
  for (const r of D.resets) if (r <= tau + 1e-9 && r > s) s = r;
  return s;
}

// ============================================================ HUD
let teamInfo = null;
function setupHud() {
  const sides = document.querySelectorAll('#bug .side');
  [0, 1].forEach((k) => {
    const el = sides[k], t = D.teams[k];
    el.querySelector('.chip').style.background = t.color;
    el.querySelector('.tname').textContent = t.name;
    const avs = el.querySelector('.avs'); avs.innerHTML = '';
    for (const p of [k * 2, k * 2 + 1]) {
      const a = document.createElement('div'); a.className = 'av';
      a.style.backgroundImage = `url("${D.players[p].avatar}")`; a.style.borderColor = t.color; avs.appendChild(a);
    }
  });
  $('stage').textContent = D.stageText || '';
  $('slogo').style.opacity = 1;
}
function hud(f) {
  const h = f.hud || {};
  $('bug').style.opacity = h.show === 0 ? 0 : (h.alpha ?? 1);
  $('stage').style.opacity = h.show === 0 ? 0 : (h.alpha ?? 1);
  $('sL').textContent = h.sL ?? 0; $('sR').textContent = h.sR ?? 0;
  const step = Math.max(0, Math.min(D.steps, Math.floor(h.step ?? 0)));
  $('clk').innerHTML = `第 ${step} 步<small>/ ${D.steps}</small>`;
  $('clkbar').style.width = (100 * step / D.steps).toFixed(2) + '%';
  // 比分跳动
  const pop = h.pop || 0;
  document.querySelector('#bug .score').style.transform = `scale(${1 + 0.18 * pop})`;
  document.querySelector('#bug .score').style.background = pop > 0.01 ? `color-mix(in srgb, #ffce54 ${Math.round(pop * 100)}%, #f4f6fa)` : '#f4f6fa';
  const mode = h.mode || 'live';
  $('live').style.display = (mode === 'live' && h.show !== 0) ? 'flex' : 'none';
  $('replay').style.display = mode === 'replay' ? 'flex' : 'none';
  $('rpspd').textContent = h.speedText || '慢动作';
  // 进球字幕
  const cap = f.cap;
  const gEl = $('goal');
  if (cap && cap.k > 0 && cap.k < 1) {
    gEl.style.display = 'block';
    const g = D.goals[cap.gi];
    const team = D.teams[g.team];
    const kin = easeOut(cap.k / 0.12), kout = smooth((cap.k - 0.88) / 0.12);
    gEl.style.opacity = (1 - kout).toFixed(3);   // 退场：整体淡出（不再滑出画面，避免残留色块）
    const slab = gEl.querySelector('.slab');
    slab.querySelector('.bg').style.background = team.color;
    slab.querySelector('.bg2').style.width = `${Math.round(lerp(0, 330, smooth((cap.k - 0.04) / 0.1)))}px`;
    slab.style.transform = `translateX(${lerp(-1300, 0, kin) - 40 * kout}px)`;
    const big = slab.querySelector('.big');
    big.style.color = lum(team.color) > 0.6 ? '#0b1020' : '#ffffff';
    const who = slab.querySelector('.who');
    who.style.color = lum(team.color) > 0.6 ? '#0b1020' : '#ffffff';
    let whoText;
    if (g.scorer >= 0 && !g.own) whoText = `${D.players[g.scorer].label}<small>${team.name}</small>`;
    else if (g.own) whoText = `乌龙球<small>${team.name} 得分</small>`;
    else whoText = `${team.name}`;
    who.innerHTML = whoText;
    const gav = gEl.querySelector('.gav');
    if (g.scorer >= 0 && !g.own) { gav.style.display = 'block'; gav.style.backgroundImage = `url("${D.players[g.scorer].avatar}")`; gav.style.borderColor = team.color; }
    else gav.style.display = 'none';
    gav.style.transform = `translateX(${lerp(-500, 0, easeOut((cap.k - 0.03) / 0.12)) - 40 * kout}px) scale(${lerp(0.6, 1, easeOut((cap.k - 0.03) / 0.12))})`;
    const gsc = gEl.querySelector('.gsc');
    $('gL').textContent = g.sA; $('gR').textContent = g.sB;
    $(g.team === 0 ? 'gL' : 'gR').style.color = team.color === '#f1f5f9' ? '#0b1020' : team.color;
    $(g.team === 0 ? 'gR' : 'gL').style.color = '#0b1020';
    const kin2 = easeOut((cap.k - 0.06) / 0.10);
    gsc.style.opacity = kin2.toFixed(3);
    gsc.style.transform = `translateX(${lerp(90, 0, kin2) + 40 * kout}px)`;   // 比分框在画面内淡入，不从屏幕外滑进
  } else gEl.style.display = 'none';
  // 转场
  const st = f.sting;
  const sEl = $('sting');
  if (st && st.p > 0 && st.p < 1) {
    sEl.style.display = 'block';
    const cs = [st.c1 || '#ffce54', '#ffffff', '#0b1020'];
    const ws = [3300, 3150, 3000];
    ['sb1', 'sb2', 'sb3'].forEach((id, i) => {
      const e = $(id);
      const p = clamp((st.p - 0.5) * 0.9 + 0.5 - (1 - i) * 0.035, 0, 1);
      const x = lerp(-3700, 2500, p * p * (3 - 2 * p)) - (ws[i] - 3000) / 2;
      e.style.background = cs[i];
      e.style.transform = `translateX(${x}px) skewX(-18deg)`;
      e.style.left = '0px'; e.style.zIndex = i;
      e.style.width = `${ws[i]}px`;
    });
    const lp = clamp(1 - Math.abs(st.p - 0.5) / 0.2, 0, 1);
    $('slogo').style.opacity = smooth(lp);
    $('slogo').style.transform = `translate(-50%, -50%) scale(${lerp(0.92, 1.0, lp)})`;
  } else sEl.style.display = 'none';
  $('fade').style.opacity = f.fadeHtml || 0;
  const lb = (f.letterbox || 0) * 70;
  $('lbT').style.height = lb + 'px'; $('lbB').style.height = lb + 'px';
}

// ============================================================ 每帧
const tmpV = new THREE.Vector3();
function ballAtSafe(tau, gi, t0) { return stateAt(Math.max(tau, t0), gi).b; }

function checkedCamera(f, st) {
  let c = f.cam;
  const subject = f.kind === 'close' && f.avFocus >= 0 ? st.p[f.avFocus] : st.b;
  const subjects = f.kind === 'close' && f.avFocus >= 0 ? [[subject[0], RB, subject[1], RP]] : [];
  const unsafe = cameraViolations(c, f.kind, f, subjects);
  if (unsafe.length) c = safeCamera(c, f.kind);
  const remaining = cameraViolations(c, f.kind, f, subjects);
  return {frame:f.i, corrected:unsafe.length>0, reasons:unsafe, remaining, camera:c};
}

// 不绘制画面也能逐帧核验真实球员插值，供批量检查脚本调用。
window.auditCameras = frames => frames.map(f => checkedCamera(f, stateAt(f.t, f.goal ?? null)));

function renderFrame(f) {
  const tau = f.t, gi = (f.goal == null ? null : f.goal);
  const st = stateAt(tau, gi);
  // ---- 机位
  window.lastCameraSafety = checkedCamera(f, st);
  const {camera:c, remaining} = window.lastCameraSafety;
  if (remaining.length) throw new Error(`机位安全检查失败：帧 ${f.i} ${remaining.join('；')}`);
  const tx = c.tx, ty = c.ty ?? 0, tz = c.tz;
  const [px, py, pz] = cameraPosition(c, f);
  camera.position.set(px, py, pz);
  camera.fov = c.fov; camera.updateProjectionMatrix();
  camera.lookAt(tx, ty, tz);
  if (c.roll) camera.rotateZ(c.roll);
  // ---- 球
  ball.position.set(st.b[0], RB, st.b[1]);
  ball.quaternion.copy(st.q);
  ballShadow.position.set(st.b[0] + 0.03, 0.004, st.b[1] + 0.02);
  // ---- 球员
  const hl = f.hl || null;
  for (let p = 0; p < 4; p++) {
    const P = players[p], [x, z] = st.p[p];
    P.grp.position.set(x, 0, z);
    P.top.rotation.z = Math.atan2(camera.position.x - x, camera.position.z - z);   // 顶面头像转正对着镜头
    P.glow.position.set(x, 0.006, z);
    let ringK = 2.2, glowO = 0.75, spScale = 1;
    if (hl && hl.p === p) { ringK = 2.2 + 4 * hl.k; glowO = 0.75 + 0.25 * hl.k; spScale = 1 + 0.25 * hl.k; }
    P.ringM.emissiveIntensity = ringK; P.glow.material.opacity = glowO;
    P.glow.scale.setScalar(1 + (hl && hl.p === p ? 0.25 * hl.k : 0));
    // 头像牌：屏幕上保持 70~150 像素
    const dist = camera.position.distanceTo(tmpV.set(x, 0.9, z));
    const ppu = H / (2 * dist * Math.tan(THREE.MathUtils.degToRad(camera.fov / 2)));
    // 特写镜头：进球人的牌放大、画在最上层，其他人的牌缩小变淡（仍然显示）
    const focus = (f.avFocus != null && f.avFocus >= 0);
    const isF = focus && f.avFocus === p;
    const wpx = clamp(0.46 * ppu, f.avMin || 84, f.avMax || 138) * spScale * (focus && !isF ? 0.72 : 1);
    const ws = wpx / ppu;
    P.sp.scale.set(ws, ws * P.aspect, 1);
    P.sp.renderOrder = isF ? 20 : 10;
    // 默认挂在圆盘上方；如果牌顶会伸进画面顶部的包装区（比分条、LIVE 牌），渐变成挂在圆盘下方
    const hUp = 0.42 + 0.08 * ws;
    camera.updateMatrixWorld();
    tmpV.set(x, hUp, z).project(camera);
    const topPx = (1 - tmpV.y) / 2 * H - wpx * P.aspect;
    const TOPLIM = (isF && f.topLimF != null) ? f.topLimF : 150;   // 特写里进球人的牌只需躲开比分条
    const kf = clamp((TOPLIM - topPx) / 70, 0, 1);
    const kk = kf * kf * (3 - 2 * kf);
    P.sp.center.set(0.5, kk);
    P.sp.position.set(x, lerp(hUp, 0.02, kk), z + lerp(0, RP + 0.06, kk));
    P.sp.material.opacity = (f.avAlpha ?? 1) * (focus && !isF ? 0.5 : 1);
    P.sp.visible = (f.avAlpha ?? 1) > 0.01;
  }
  // ---- 踢球特效
  const tEff = (gi != null && gi >= 0 && tau >= D.goals[gi].t + 1) ? tau : tau;
  let slot = 0;
  for (const kp of kickPool) { kp.ring.visible = false; kp.flare.visible = false; kp.pts.visible = false; }
  for (let ki = D.kickIdx(tEff - KICK_LIFE); ki < D.kicks.length && slot < kickPool.length; ki++) {
    const k = D.kicks[ki];
    if (k.t > tEff) break;
    if (gi != null && gi >= 0 && k.t > D.goals[gi].t) break;     // 进球后的外推阶段：不画重新开球之后的踢球
    const age = tEff - k.t;
    if (age < 0 || age >= KICK_LIFE) continue;
    // 进球后的外推阶段，之前的踢球特效照常衰减；回到开球之后的不画
    const a = age / KICK_LIFE, kp = kickPool[slot++];
    const str = clamp(Math.hypot(k.vx, k.vy) / 800, 0.25, 1);
    const pc = players[k.p].color;
    const kx = wx(k.bx), kz = wz(k.by);
    kp.ring.visible = true; kp.ring.position.set(kx, 0.012, kz);
    kp.ring.scale.setScalar((0.3 + 0.75 * Math.sqrt(a)) * (0.6 + 0.5 * str));
    kp.ring.material.color.copy(pc).multiplyScalar(1.6); kp.ring.material.opacity = Math.pow(1 - a, 2.5) * 0.85;
    const fa = clamp(1 - age / 3, 0, 1);
    kp.flare.visible = fa > 0; kp.flare.position.set(kx, 0.2, kz);
    kp.flare.scale.setScalar(0.9 * str * (0.6 + 0.6 * fa)); kp.flare.material.opacity = fa * fa; kp.flare.material.color.set(0xffffff).lerp(pc, 0.35);
    // 火花：沿踢球方向的扇形
    const ang = Math.atan2(k.vy, k.vx), arr = kp.pts.geometry.attributes.position.array;
    for (let i = 0; i < kp.N; i++) {
      const h1 = hash(ki * 131 + i * 7), h2 = hash(ki * 131 + i * 7 + 3), h3 = hash(ki * 131 + i * 7 + 5);
      const th = ang + (h1 - 0.5) * 1.6, sp = (0.6 + 1.4 * h2) * (0.5 + str);
      const dist2 = sp * (1 - Math.pow(1 - a, 2.2)) * 0.9;
      arr[i * 3] = kx + Math.cos(th) * dist2; arr[i * 3 + 2] = kz + Math.sin(th) * dist2;
      arr[i * 3 + 1] = 0.05 + h3 * 0.35 * Math.sin(Math.PI * Math.min(1, a * 1.3)) * (0.5 + str);
    }
    kp.pts.geometry.attributes.position.needsUpdate = true;
    kp.pts.visible = true; kp.pts.material.opacity = Math.pow(1 - a, 1.5); kp.pts.material.color.set(0xffffff).lerp(pc, 0.5);
    kp.pts.material.size = 0.06 + 0.04 * str;
  }
  // ---- 拖尾（往回取样 6 步，不跨过开球）
  {
    const t0 = segStart(Math.min(tau, (gi != null && gi >= 0) ? D.goals[gi].t + 0.999 : tau));
    const span = 6.0;
    const pos = trail.geometry.attributes.position.array, cl = trail.geometry.attributes.color.array;
    const camDir = tmpV.copy(camera.position);
    let prev = null;
    // 最近一次踢球者的颜色
    let tc = new THREE.Color(0xffffff);
    const lk = D.lastKick((gi != null && gi >= 0) ? Math.min(tau, D.goals[gi].t + 0.5) : tau);
    if (lk) tc = players[lk.p].color.clone();
    let speed = 0;
    const samples = [];
    for (let i = 0; i < TRAIL_N; i++) {
      const tt = tau - span * i / (TRAIL_N - 1);
      samples.push(tt < t0 ? null : stateAt(tt, gi).b);
    }
    if (samples[0] && samples[3]) speed = Math.hypot(samples[0][0] - samples[3][0], samples[0][1] - samples[3][1]) / (3 * span / (TRAIL_N - 1));
    const vis = clamp((speed - 0.08) / 0.35, 0, 1);
    for (let i = 0; i < TRAIL_N; i++) {
      const s = samples[i] || samples[Math.max(0, i - 1)] || samples[0];
      let sIdx = i; while (sIdx > 0 && !samples[sIdx]) sIdx--;
      const p = samples[sIdx] || [ball.position.x, ball.position.z];
      const n = samples[Math.min(TRAIL_N - 1, sIdx + 1)] || p, b = samples[Math.max(0, sIdx - 1)] || p;
      let tx = b[0] - n[0], tz = b[1] - n[1]; const tl = Math.hypot(tx, tz) || 1; tx /= tl; tz /= tl;
      const dx = camera.position.x - p[0], dy = camera.position.y - RB, dz = camera.position.z - p[1];
      // 侧向 = 切线 × 视线
      let sx = 0 * dz - dy * tz * 0 + (tz * dy), sy = tz * dx - tx * dz, sz = -tx * dy;
      // 简化：切线 (tx,0,tz) × 视线 (dx,dy,dz)
      sx = 0 * dz - tz * dy; sy = tz * dx - tx * dz; sz = tx * dy - 0 * dx;
      const sl = Math.hypot(sx, sy, sz) || 1;
      const k = i / (TRAIL_N - 1), wdt = RB * 0.95 * (1 - k) * (samples[i] ? 1 : 0);
      for (let side = 0; side < 2; side++) {
        const sg = side ? -1 : 1, o = (i * 2 + side) * 3;
        pos[o] = p[0] + sg * sx / sl * wdt; pos[o + 1] = RB + sg * sy / sl * wdt; pos[o + 2] = p[1] + sg * sz / sl * wdt;
        const c = new THREE.Color(1, 1, 1).lerp(tc, Math.min(1, k * 1.6));
        const al = Math.pow(1 - k, 1.6) * vis * (samples[i] ? 1 : 0) * 0.9;
        const oc = (i * 2 + side) * 4; cl[oc] = c.r * al; cl[oc + 1] = c.g * al; cl[oc + 2] = c.b * al; cl[oc + 3] = al;
      }
    }
    trail.geometry.attributes.position.needsUpdate = true; trail.geometry.attributes.color.needsUpdate = true;
  }
  // ---- 进球：网鼓、灯光、彩带、围板
  setNetBulge(st.side, st.bulge * 1.0, st.b[1]);
  const gf = f.goalFx || null;
  if (gf) {
    const g = D.goals[gf.gi], s = g.team === 0 ? 1 : -1, tc = col(D.teams[g.team].color);
    goalLight.position.set(s * 6.2, 1.2, 0); goalLight.color.copy(tc); goalLight.intensity = 8 * Math.exp(-gf.u / 8) * (gf.u >= 0 ? 1 : 0);
    ball.material.emissiveIntensity = 0.6 * Math.exp(-gf.u / 6);
    boardTopMat.emissive.copy(col('#9fd8ff')).lerp(tc, clamp(1 - gf.u / 120, 0, 1));
    boardTopMat.emissiveIntensity = 1.2 + 0.9 * Math.max(0, Math.sin(gf.u * 0.5)) * clamp(1 - gf.u / 90, 0, 1);
    ledMat.emissive.copy(new THREE.Color(1, 1, 1)).lerp(tc, 0.7 * clamp(1 - gf.u / 120, 0, 1));
    // 彩带：从门口喷出，重力下落
    const cu = gf.u;
    if (cu >= 0 && cu < 140) {
      confetti.visible = true;
      const m = new THREE.Matrix4(), q = new THREE.Quaternion(), e = new THREE.Euler(), sc = new THREE.Vector3(1, 1, 1), p = new THREE.Vector3();
      const palette = [tc, tc, new THREE.Color(0xffce54), new THREE.Color(0xffffff)];
      for (let i = 0; i < CONF_N; i++) {
        const h = (k) => hash(gf.gi * 7919 + i * 13 + k);
        const delay = h(1) * 10, tt = Math.max(0, cu - delay) * 0.05;   // 秒级
        const vx = -s * (0.8 + 2.4 * h(2)), vy = 2.2 + 2.6 * h(3), vz = (h(4) - 0.5) * 3.2;
        const drag = 1.6;
        const ex = (1 - Math.exp(-drag * tt)) / drag;
        p.set(s * 6 + (h(5) - 0.5) * 0.3 + vx * ex, Math.max(0.01, 0.4 + vy * ex - 1.2 * tt * tt * 0.5 - 0.35 * tt), (h(6) - 0.5) * 1.8 + vz * ex + Math.sin(tt * 3 + i) * 0.08);
        e.set(tt * (4 + 6 * h(7)), tt * (3 + 5 * h(8)), tt * 2);
        q.setFromEuler(e);
        const vis = cu >= delay ? 1 : 0;
        sc.setScalar(vis);
        m.compose(p, q, sc); confetti.setMatrixAt(i, m);
        confetti.setColorAt(i, palette[i % 4]);
      }
      confetti.instanceMatrix.needsUpdate = true; if (confetti.instanceColor) confetti.instanceColor.needsUpdate = true;
    } else confetti.visible = false;
  } else {
    goalLight.intensity = 0; ball.material.emissiveIntensity = 0; confetti.visible = false;
    boardTopMat.emissive.set('#9fd8ff'); boardTopMat.emissiveIntensity = 1.2; ledMat.emissive.set(0xffffff);
  }
  ledTex.offset.x = (f.wall ?? 0) * 0.04;
  // ---- 景深（特写时）
  if (f.dof && f.dof > 0) {
    bokeh.enabled = true;
    const fp = (hl && players[hl.p]) ? players[hl.p].grp.position : tmpV.set(tx, ty, tz);
    bokeh.uniforms.focus.value = camera.position.distanceTo(tmpV.set(fp.x, 0.2, fp.z));
    bokeh.uniforms.aperture.value = 0.00035 * f.dof; bokeh.uniforms.maxblur.value = 0.006 * f.dof;
  } else bokeh.enabled = false;
  // ---- 调色
  grade.uniforms.uSeed.value = (f.i || 0) % 997;
  grade.uniforms.uFlash.value = f.flash || 0;
  grade.uniforms.uSat.value = f.sat ?? 1.0;
  grade.uniforms.uWarm.value = f.warm ?? 0.0;
  grade.uniforms.uFade.value = f.fade || 0;
  bloom.strength = 0.45 + (f.bloomAdd || 0);
  hud(f);
  composer.render();
  return true;
}

// ============================================================ 载入
async function setup(data) {
  D = data;
  S = new Float64Array(data.S);
  D.n = data.S.length / NS - 1;   // 最后一项是第 n 步开始时（终场）的状态
  data.goals.forEach((g, i) => goalAt.set(g.t, i));
  for (const r of data.resets) resetAt.add(r);
  data.kicks.sort((a, b) => a.t - b.t);
  D.kickIdx = (t) => { let lo = 0, hi = D.kicks.length; while (lo < hi) { const m = (lo + hi) >> 1; if (D.kicks[m].t < t) lo = m + 1; else hi = m; } return lo; };
  D.lastKick = (t) => { const i = D.kickIdx(t + 1e-9) - 1; if (i < 0) return null; const k = D.kicks[i]; return (t - k.t < 40 && segStart(t) <= k.t) ? k : null; };
  await document.fonts.load('800 40px NSC');
  await document.fonts.load('900 40px NSC');
  await document.fonts.ready;
  buildField();
  buildBoards(data.brand || 'AI 算法对抗大赛');
  buildGoals();
  buildStadium();
  buildBall();
  buildTrail();
  buildKickPool();
  buildConfetti();
  await buildPlayers(data.players);
  buildPost();
  buildBallRotation();
  setupHud();
  // 预载 HUD 用的头像（截图前必须已解码）
  await Promise.all(data.players.map((p) => loadImg(p.avatar)));
  // 预热：编译着色器
  renderer.compile(scene, camera);
  return true;
}

window.setup = setup;
window.renderFrame = renderFrame;
window.stageReady = true;
