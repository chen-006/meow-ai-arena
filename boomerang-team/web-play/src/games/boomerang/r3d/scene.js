// 正片场景：按图加载 public/gen/ep11/scenes/<图>.json + .bin + .webp（正片渲染器 env.build_env 搭好、Blender 无界面导出的那一份），
// 材质照 src\env.py mat_terrain、src\scene_lib.py mat_vcol / mat_cloud / mat_water / mat_lava / mat_sea 用 GLSL 重写；
// 塌陷照 env.set_collapse，高草被碰弯照 env.set_ambient，飘落物照 env.Fall，液面跟着塌陷排干照 env.set_liquids（第 12 集）。
// 整个场景挂在一个转了 -90° 的根节点下，里面直接用 Blender 坐标（X = 地图 x，Y = -地图 y，Z 向上）。
// 底子是第 11 集网页查看器的 scene.js（原样逐行移植），第 12 集的液面、发光地缝、队旗、岩浆点光是这里加的。
import * as THREE from 'three';
import { genUrl } from '../../../common/wasm';
import * as LQ from './liquid.js';

const SMR = `float smr(float a, float b, float x) { float t = clamp((x - a) / (b - a), 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }
`;
const lin = (hex) => new THREE.Color(hex);
const S2L = new Float32Array(256);
for (let i = 0; i < 256; i++) { const c = i / 255; S2L[i] = c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4); }

// ---------------------------------------------------------------- 载入（按图缓存）
const cache = new Map();
/** 下载一张图的场景包：{meta, buf, tex, glow}（同一张图只下载一次） */
export function loadScenePack(map) {
  if (cache.has(map)) return cache.get(map);
  const u = (f) => genUrl('ep11/scenes/' + f);
  const texP = (f) => new Promise((res, rej) => new THREE.TextureLoader().load(u(f), res, undefined, () => rej(new Error('贴图下载失败'))));
  const p = fetch(u(map + '.json')).then((r) => { if (!r.ok) throw new Error('场景 ' + map + ' 下载失败（' + r.status + '）'); return r.json(); })
    .then((meta) => Promise.all([
      fetch(u(map + '.bin')).then((r) => { if (!r.ok) throw new Error('场景网格下载失败'); return r.arrayBuffer(); }),
      texP(meta.tex),
      meta.glow ? texP(meta.glow) : Promise.resolve(null),
    ]).then(([buf, tex, glow]) => ({ meta, buf, tex, glow })));
  p.catch(() => cache.delete(map));
  cache.set(map, p);
  return p;
}
export function hasScene(map) { return SCENE_MAPS.has(map); }
export const SCENE_MAPS = new Set(['twinlakes', 'yinyang', 'stonecircle', 'skybridge', 'ruins', 'icefloes', 'hourglass', 'canyon', 'oldtown', 'forest', 'caldera', 'piers', 'keeps']);

function decodeObjects(buf, meta) {
  const out = [];
  let off = 0;
  for (const o of meta.objects) {
    const nv = o.nv, ni = o.ni;
    const pb = nv * 6, pbp = pb + (pb % 4);
    const pd = new Int16Array(buf, off, nv * 3); off += pbp;
    const nb = new Int8Array(buf, off, nv * 4); off += nv * 4;
    const cb = new Uint8Array(buf, off, nv * 4); off += nv * 4;
    const ib = o.i32 ? new Uint32Array(buf, off, ni) : new Uint16Array(buf, off, ni);
    off += o.i32 ? ni * 4 : ni * 2 + ((ni * 2) % 4);
    const P = new Float32Array(nv * 3), N = new Float32Array(nv * 3);
    for (let c = 0; c < 3; c++) {
      let acc = 0;
      for (let i = 0; i < nv; i++) { acc = ((acc + pd[c * nv + i]) << 16) >> 16; P[i * 3 + c] = acc * o.q; }
      for (let i = 0; i < nv; i++) N[i * 3 + c] = nb[c * nv + i] / 127;
    }
    const terr = o.kind === 'state' || o.kind === 'piece_terrain' || o.kind === 'decor_terrain';
    const C = new Float32Array(nv * 4);
    for (let i = 0; i < nv; i++) {
      for (let c = 0; c < 3; c++) C[i * 4 + c] = terr ? cb[c * nv + i] / 255 : S2L[cb[c * nv + i]];
      C[i * 4 + 3] = cb[3 * nv + i] / 255;
    }
    const I = o.i32 ? new Uint32Array(ni) : new Uint16Array(ni);
    let acc = 0;
    if (o.i32) for (let i = 0; i < ni; i++) { acc = (acc + ib[i]) >>> 0; I[i] = acc; }
    else for (let i = 0; i < ni; i++) { acc = (acc + ib[i]) & 0xffff; I[i] = acc; }
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(P, 3));
    g.setAttribute('normal', new THREE.BufferAttribute(N, 3));
    g.setAttribute('color', new THREE.BufferAttribute(C, 4));
    g.setIndex(new THREE.BufferAttribute(I, 1));
    for (const [mi, st, cnt] of o.groups) g.addGroup(st, cnt, mi);
    g.computeBoundingSphere();
    out.push(g);
  }
  return out;
}

// ---------------------------------------------------------------- 材质
function inject(mat, fn, key) {
  mat.onBeforeCompile = fn;
  mat.customProgramCacheKey = () => key;
  return mat;
}
const VERT_VARY = `varying vec3 vWP; varying vec3 vLP; varying vec3 vLN;
`;
function vertPatch(sh) {
  sh.vertexShader = VERT_VARY + sh.vertexShader.replace('#include <project_vertex>',
    `#include <project_vertex>
    vWP = (modelMatrix * vec4(transformed, 1.0)).xyz; vLP = position; vLN = normal;`);
}
const NOISE = `float h31(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float vnoise(vec3 x) { vec3 i = floor(x), f = fract(x); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(h31(i), h31(i + vec3(1,0,0)), f.x), mix(h31(i + vec3(0,1,0)), h31(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(h31(i + vec3(0,0,1)), h31(i + vec3(1,0,1)), f.x), mix(h31(i + vec3(0,1,1)), h31(i + vec3(1,1,1)), f.x), f.y), f.z); }
`;
// Blender 噪声纹理（Perlin 梯度噪声 + 分形，输出 0..1，均值 0.5）的近似：岩浆 / 海水的纹路阈值是照它调的
const GNOISE = `vec3 gh3(vec3 p) { p = vec3(dot(p, vec3(127.1, 311.7, 74.7)), dot(p, vec3(269.5, 183.3, 246.1)), dot(p, vec3(113.5, 271.9, 124.6)));
  return -1.0 + 2.0 * fract(sin(p) * 43758.5453123); }
float gnoise(vec3 p) { vec3 i = floor(p), f = fract(p); vec3 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
  return mix(mix(mix(dot(gh3(i), f), dot(gh3(i + vec3(1,0,0)), f - vec3(1,0,0)), u.x), mix(dot(gh3(i + vec3(0,1,0)), f - vec3(0,1,0)), dot(gh3(i + vec3(1,1,0)), f - vec3(1,1,0)), u.x), u.y),
             mix(mix(dot(gh3(i + vec3(0,0,1)), f - vec3(0,0,1)), dot(gh3(i + vec3(1,0,1)), f - vec3(1,0,1)), u.x), mix(dot(gh3(i + vec3(0,1,1)), f - vec3(0,1,1)), dot(gh3(i + vec3(1,1,1)), f - vec3(1,1,1)), u.x), u.y), u.z); }
float bnoise(vec3 p, float scale, float detail, float rough) {
  p *= scale; float s = 0.0, a = 1.0, n = 0.0;
  for (int i = 0; i < 4; i++) { if (float(i) > detail) break; s += a * gnoise(p); n += a; a *= rough; p *= 2.0; }
  return 0.5 + 0.5 * s / n * 1.15; }
float vor(vec3 p) { vec3 i = floor(p), f = fract(p); float d = 8.0;
  for (int x = -1; x <= 1; x++) for (int y = -1; y <= 1; y++) for (int z = -1; z <= 1; z++) {
    vec3 g = vec3(float(x), float(y), float(z)); vec3 o = 0.5 + 0.5 * gh3(i + g); d = min(d, length(g + o - f)); }
  return d; }
`;

/** 顶点色材质（mat_vcol）；team：双堡的队色占位换色；flag：旗面飘（顶点色 alpha = 离旗杆多远） */
function vcolMat(info, shared, opts = {}) {
  const m = new THREE.MeshLambertMaterial({ vertexColors: true, side: THREE.DoubleSide });
  const haze = info.haze, emis = info.emis || 0, team = !!opts.team, flag = !!opts.flag;
  const u = { uPh: { value: 0 } };
  m.userData.u = u;
  return inject(m, (sh) => {
    vertPatch(sh);
    sh.uniforms.uHaze = shared.uHaze;
    Object.assign(sh.uniforms, shared, u);
    if (flag) {
      sh.vertexShader = 'uniform float uPh;\n' + sh.vertexShader.replace('#include <begin_vertex>', `#include <begin_vertex>
        { float fu = color.a; transformed.y += 0.045 * sin(uPh - fu * 5.0) * fu; transformed.z += 0.012 * sin(uPh * 1.3 - fu * 4.0) * fu; }`);
    }
    sh.fragmentShader = VERT_VARY + SMR + 'uniform vec3 uHaze; uniform vec3 uFlagL; uniform vec3 uRoofL; uniform vec3 uFlagR; uniform vec3 uRoofR;\n' + sh.fragmentShader
      .replace('#include <color_fragment>', `#include <color_fragment>
      ${flag ? 'diffuseColor.a = 1.0;' : ''}
      ${team ? `{ vec3 c = diffuseColor.rgb;
        if (c.g < 0.02 && c.r > 0.2 && c.b > 0.08) diffuseColor.rgb = (c.b / c.r > 0.75 ? uFlagL : uRoofL) * (c.r / 0.75);
        else if (c.r < 0.02 && c.b > 0.2 && c.g > 0.08) diffuseColor.rgb = (c.g / c.b > 0.75 ? uFlagR : uRoofR) * (c.b / 0.75); }` : ''}
      ${haze ? 'diffuseColor.rgb = mix(diffuseColor.rgb, uHaze, smr(-1.2, -11.0, vWP.y) * 0.85);' : ''}`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
      totalEmissiveRadiance += diffuseColor.rgb * ${emis.toFixed(3)};`);
  }, 'vcol' + (haze ? 'h' : '') + (team ? 't' : '') + (flag ? 'f' : '') + emis.toFixed(3));
}

function terrainMat(shared, tex, glow, center, proc = false) {
  const m = new THREE.MeshLambertMaterial({ vertexColors: false });
  const u = { uCenter: { value: new THREE.Vector2(center[0], center[1]) }, uWarn: { value: 0 } };
  m.userData.u = u;
  const hasGlow = !!glow && !proc;
  return inject(m, (sh) => {
    vertPatch(sh);
    sh.vertexShader = 'attribute vec4 color;\nvarying float vFacet; varying float vEs; varying float vDep;\n' + sh.vertexShader.replace('#include <project_vertex>', '#include <project_vertex>\nvFacet = color.r * 2.0 - 1.0; vEs = (color.g - 0.5) * 2.0; vDep = color.b * 12.0;');
    Object.assign(sh.uniforms, shared, u, { uTex: { value: tex }, uGlow: { value: glow } });
    const P = (k) => `uniform vec3 ${k};`;
    sh.fragmentShader = VERT_VARY + SMR + NOISE + `varying float vFacet; varying float vEs; varying float vDep; uniform vec3 uGrass; uniform vec3 uGrass2; uniform vec3 uRim; uniform sampler2D uTex; uniform sampler2D uGlow; uniform vec4 uRect; uniform vec2 uCenter; uniform float uWarn;
      ${['uHaze', 'uRockA', 'uRockB', 'uRockC', 'uRockDeep', 'uRockUnder', 'uLedge', 'uSoil', 'uLip'].map(P).join(' ')}
      float vTop;
      vec3 rockRamp(float t) {
        if (t <= 0.04) return uRockA;
        if (t <= 0.2) return mix(uRockA, uRockB, (t - 0.04) / 0.16);
        if (t <= 0.42) return mix(uRockB, uRockC, (t - 0.2) / 0.22);
        if (t <= 0.75) return mix(uRockC, uRockDeep, (t - 0.42) / 0.33);
        return uRockDeep;
      }
      vec2 vGuv;
      ` + sh.fragmentShader.replace('#include <color_fragment>', `
      vec3 rp = vLP + vec3(uCenter, 0.0);
      float nz = normalize(vLN).z;
      float dz = vDep;
      float top = smr(0.55, 0.85, nz) * smr(0.16, 0.05, dz);
      vTop = top;
      vec2 guv = vec2((rp.x - uRect.x) / uRect.z, (rp.y - uRect.y) / uRect.w);
      vGuv = guv;
      ${proc ? 'float n1 = 0.6 * vnoise(rp * 0.2 * 2.0) + 0.4 * vnoise(rp * 0.8); vec3 ground = mix(uGrass, uGrass2, smr(0.45, 0.62, n1)); ground = mix(ground, uRim, smr(0.2, 0.05, vEs));' : 'vec3 ground = texture2D(uTex, guv).rgb;'}
      vec3 rock = rockRamp(dz / 7.0) * (1.0 + vFacet * 0.09);
      rock = mix(rock, uLedge, smr(0.25, 0.6, nz) * 0.5);
      rock = mix(rock, uRockUnder, smr(-0.15, -0.6, nz));
      vec3 soil = mix(uSoil, rock, smr(0.2, 0.34, dz));
      vec3 side = mix(uLip, soil, smr(0.09, 0.17, dz));
      vec3 col = mix(side, ground, top);
      col = mix(col, vec3(1.0, 0.195, 0.027), uWarn);
      col = mix(col, uHaze, smr(-1.6, -11.0, vWP.y) * 0.88);
      diffuseColor.rgb = col;`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
      ${hasGlow ? 'totalEmissiveRadiance += vec3(1.0, 0.1022, 0.0130) * 1.1 * texture2D(uGlow, vGuv).r * vTop;' : ''}`);
  }, (proc ? 'terrainp' : 'terrain') + (hasGlow ? 'g' : ''));
}

function cloudMat(shared) {
  const m = new THREE.MeshLambertMaterial({});
  return inject(m, (sh) => {
    vertPatch(sh);
    Object.assign(sh.uniforms, shared);
    sh.fragmentShader = VERT_VARY + SMR + 'uniform vec3 uHaze; uniform vec3 uCloud; uniform vec3 uCloudShade;\n' + sh.fragmentShader
      .replace('#include <color_fragment>', `
      vec3 cc = mix(uCloud, uCloudShade, smr(0.2, -0.6, normalize(vLN).z));
      diffuseColor.rgb = mix(cc, uHaze, smr(-9.0, -18.0, vWP.y) * 0.55);`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
      totalEmissiveRadiance += vec3(0.8148, 0.7913, 0.9301) * 0.32;`);
  }, 'cloud');
}

function waterMat(shared, flow) {
  const m = new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, depthWrite: false, side: THREE.DoubleSide });
  return inject(m, (sh) => {
    vertPatch(sh);
    Object.assign(sh.uniforms, shared);
    sh.fragmentShader = VERT_VARY + SMR + NOISE + 'uniform float uTime;\n' + sh.fragmentShader.replace('#include <color_fragment>', `#include <color_fragment>
      ${flow ? `vec3 q = (vLP + vec3(0.0, 0.0, uTime)) * vec3(3.0, 3.0, 0.55);
      float nn = 0.5 * vnoise(q) + 0.3 * vnoise(q * 2.0) + 0.2 * vnoise(q * 4.0);
      diffuseColor.a *= 0.4 + 0.6 * smr(0.36, 0.64, nn);` : ''}`);
  }, flow ? 'water' : 'alpha');
}

// 岩浆（scene_lib.mat_lava）：亮橙黄的熔岩上漂着慢慢翻滚的暗红硬壳，零星冒泡的亮点；岸边一圈更亮（顶点色 alpha = 离岸近）
function lavaMat(shared) {
  const m = new THREE.MeshLambertMaterial({ vertexColors: true });
  return inject(m, (sh) => {
    vertPatch(sh);
    Object.assign(sh.uniforms, shared);
    sh.fragmentShader = VERT_VARY + SMR + GNOISE + 'uniform float uLiqT;\nvec3 lvEm;\n' + sh.fragmentShader
      .replace('#include <color_fragment>', `#include <color_fragment>
      { float t = uLiqT; vec3 p = vLP + vec3(0.035, 0.02, 0.0) * t; float edge = diffuseColor.a;
        float n1 = bnoise(vec3(p.xy, t * 0.05 * 1.7), 0.7, 3.0, 0.55);
        float n2 = bnoise(vec3(p.xy, t * 0.09 * 1.3), 2.6, 2.0, 0.5);
        float f = n1 * 0.62 + n2 * 0.38;
        float crust = smr(0.47, 0.57, f) * clamp(1.0 - smr(0.55, 0.95, edge), 0.0, 1.0);
        float hot = smr(0.45, 0.7, n2);
        vec3 bright = mix(vec3(0.8879, 0.0513, 0.0047), vec3(1.0, 0.2541, 0.0160), hot);
        bright = mix(bright, vec3(1.0, 0.4793, 0.0648), smr(0.7, 1.0, edge) * 0.7);
        float bub = smr(0.16, 0.05, vor(vec3(vLP.xy * 2.2, t * 0.22))) * smr(0.35, 0.55, n1);
        vec3 em = mix(bright, vec3(0.0232, 0.0070, 0.0091), crust);
        em = mix(em, vec3(1.0, 0.6445, 0.1946), bub);
        lvEm = em * ((1.0 - crust) * 0.85 + 0.3);
        diffuseColor.rgb = mix(vec3(1.0, 0.1946, 0.0232), vec3(0.0482, 0.0252, 0.0409), crust); diffuseColor.a = 1.0; }`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
      totalEmissiveRadiance += lvEm;`);
  }, 'lava');
}

// 海水（scene_lib.mat_sea）：顶点色 = 深浅（离岸越远越深），alpha = 离岸近；慢慢漂的波光、岸边一圈白浪花
function seaMat(shared) {
  const m = new THREE.MeshLambertMaterial({ vertexColors: true });
  return inject(m, (sh) => {
    vertPatch(sh);
    Object.assign(sh.uniforms, shared);
    sh.fragmentShader = VERT_VARY + SMR + GNOISE + 'uniform float uLiqT;\nvec3 seEm;\n' + sh.fragmentShader
      .replace('#include <color_fragment>', `#include <color_fragment>
      { float t = uLiqT; vec3 p = vLP + vec3(0.05, -0.03, 0.0) * t; float edge = diffuseColor.a; vec3 col = diffuseColor.rgb;
        float n1 = bnoise(vec3(p.x, p.y * 2.2, t * 0.12 * 1.6), 1.6, 2.0, 0.5);
        float n2 = bnoise(vec3(p.xy, t * 0.2 * 4.0), 4.0, 1.0, 0.5);
        float glint = smr(0.58, 0.64, n1) * (1.0 - smr(0.66, 0.72, n1));
        col = mix(col, vec3(0.5089, 0.8879, 0.9047), glint * 0.55);
        col = mix(col, vec3(0.0123, 0.1589, 0.3916), smr(0.45, 0.7, n2) * 0.12);
        float foam = smr(0.72, 0.92, edge) * smr(0.32, 0.5, n2) + smr(0.93, 0.99, edge);
        col = mix(col, vec3(0.9047, 0.9734, 1.0), min(foam, 1.0));
        seEm = col * 0.22;
        diffuseColor.rgb = col; diffuseColor.a = 1.0; }`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
      totalEmissiveRadiance += seEm;`);
  }, 'sea');
}

// ---------------------------------------------------------------- 场景
export class SceneTerrain {
  /** pack：loadScenePack 的结果；rp：rp.js 的 Replay；opts.side = {left, right} 两侧城堡的队色（双堡换色用） */
  constructor(rp, pack, scene, opts = {}) {
    const meta = pack.meta;
    this.R = rp; this.meta = meta; this.cs = rp.cell;
    rp.liquid = meta.liquid || null;
    const pal = meta.pal;
    const tex = pack.tex, glow = pack.glow;
    tex.colorSpace = THREE.SRGBColorSpace;
    tex.wrapS = tex.wrapT = THREE.ClampToEdgeWrapping;
    tex.anisotropy = 8;
    if (glow) { glow.colorSpace = THREE.NoColorSpace; glow.wrapS = glow.wrapT = THREE.ClampToEdgeWrapping; }
    const side = opts.side || { left: '#2E8CFF', right: '#FF5B4D' };
    const roofOf = (h) => (h.toUpperCase() === '#FF5B4D' ? '#D9573F' : '#4A78C8');
    const shared = this.shared = {
      uHaze: { value: lin(pal.haze) }, uRect: { value: new THREE.Vector4(...meta.uv_rect) },
      uRockA: { value: lin(pal.rock_a) }, uRockB: { value: lin(pal.rock_b) }, uRockC: { value: lin(pal.rock_c) },
      uRockDeep: { value: lin(pal.rock_deep) }, uRockUnder: { value: lin(pal.rock_under) }, uLedge: { value: lin(pal.ledge_hi) },
      uSoil: { value: lin(pal.soil) }, uLip: { value: lin(pal.lip) }, uCloud: { value: lin(pal.cloud) }, uCloudShade: { value: lin(pal.cloud_shade) }, uGrass: { value: lin(pal.grass) }, uGrass2: { value: lin(pal.grass2) }, uRim: { value: lin(pal.grass_rim) },
      uTime: { value: 0 }, uLiqT: { value: 0 },
      uFlagL: { value: lin(side.left) }, uRoofL: { value: lin(roofOf(side.left)) }, uFlagR: { value: lin(side.right) }, uRoofR: { value: lin(roofOf(side.right)) },
    };
    const root = this.root = new THREE.Group();
    root.rotation.x = -Math.PI / 2;
    scene.add(root);
    const geoms = decodeObjects(pack.buf, meta);
    const team = !!meta.team_sentinel;
    const baseMats = meta.mats.map((info) => {
      if (info.kind === 'terrain' || info.kind === 'terrain_proc') return null;
      if (info.kind === 'cloud') return cloudMat(shared);
      if (info.kind === 'water') return waterMat(shared, true);
      if (info.kind === 'alpha') return waterMat(shared, false);
      if (info.kind === 'lava') return lavaMat(shared);
      if (info.kind === 'sea') return seaMat(shared);
      return vcolMat(info, shared, { team });
    });
    const tmat0 = terrainMat(shared, tex, glow, [0, 0]), tproc = terrainMat(shared, tex, null, [0, 0], true);
    const matsFor = (tm) => meta.mats.map((info, i) => info.kind === 'terrain' ? tm : info.kind === 'terrain_proc' ? tproc : baseMats[i]);
    const mats0 = matsFor(tmat0);
    this.states = []; this.pieces = []; this.grass = []; this.clouds = []; this.liquids = []; this.flags = [];
    const pieceGroups = {};
    meta.objects.forEach((o, i) => {
      const g = geoms[i];
      if (o.kind === 'state') {
        const ms = new THREE.Mesh(g, mats0); ms.castShadow = ms.receiveShadow = true;
        ms.visible = o.k === 0; root.add(ms); this.states[o.k] = ms;
      } else if (o.kind === 'piece_terrain') {
        const grp = new THREE.Group(); grp.rotation.order = 'ZYX';
        grp.position.set(o.cx, -o.cy, 0);
        const tm = terrainMat(shared, tex, glow, [o.cx, -o.cy]);
        const ms = new THREE.Mesh(g, matsFor(tm)); ms.castShadow = ms.receiveShadow = true;
        grp.add(ms); root.add(grp);
        const p = { o, grp, mesh: ms, u: tm.userData.u };
        this.pieces[o.piece] = p; pieceGroups[o.piece] = p;
      } else if (o.kind === 'piece_deco') {
        const ms = new THREE.Mesh(g, mats0); ms.castShadow = ms.receiveShadow = true;
        pieceGroups[o.piece].grp.add(ms);
      } else if (o.kind === 'flag') {
        const fm = meta.mats.map((info) => vcolMat(info, shared, { team, flag: true }));
        const ms = new THREE.Mesh(g, fm); ms.castShadow = true; ms.frustumCulled = false;
        const par = o.piece != null ? pieceGroups[o.piece] : null;
        if (par) par.grp.add(ms); else root.add(ms);
        this.flags.push(fm);
      } else if (o.kind === 'grass') {
        const ms = new THREE.Mesh(g, mats0); ms.castShadow = true; ms.receiveShadow = true;
        ms.rotation.order = 'ZYX';
        const par = o.piece != null ? pieceGroups[o.piece] : null;
        if (par) { ms.position.set(o.gx - par.o.cx, -o.gy + par.o.cy, 0); par.grp.add(ms); }
        else { ms.position.set(o.gx, -o.gy, 0); root.add(ms); }
        this.grass.push({ ms, gx: o.gx, gy: o.gy, ph: o.ph, dirty: false });
      } else if (o.kind === 'cloud') {
        const ms = new THREE.Mesh(g, mats0); ms.receiveShadow = false;
        root.add(ms); this.clouds.push([ms, o.speed]);
      } else if (o.kind === 'liquid' || o.kind === 'liqfall') {
        const ms = new THREE.Mesh(g, mats0); ms.receiveShadow = o.kind === 'liquid'; ms.renderOrder = o.kind === 'liqfall' ? 3 : 0;
        root.add(ms);
        this.liquids.push({ ms, kind: o.kind === 'liquid' ? 'sheet' : 'curtain', w: o.w < 0 ? null : o.w, w_from: o.w_from == null || o.w_from < 0 ? null : o.w_from });
      } else {
        const ms = new THREE.Mesh(g, mats0); ms.castShadow = ms.receiveShadow = true;
        root.add(ms);
      }
    });
    for (const L of meta.lights || []) {
      const pl = new THREE.PointLight(new THREE.Color(1.0, 0.42, 0.14), 30 / (4 * Math.PI) * 1.6, 4.5, 2);
      pl.position.set(L.x, -L.y, L.z);
      root.add(pl);
      this.liquids.push({ ms: pl, kind: 'light', w: L.w < 0 ? null : L.w });
    }
    this.dmax = {};
    for (const p of this.pieces) if (p) this.dmax[p.o.wave] = Math.max(this.dmax[p.o.wave] || 0.1, p.o.d_in);
    this.falls = [];
    const amb = meta.ambient;
    if (amb) {
      this.falls.push(new Fall(root, rp, amb, 11));
      if (amb.extra) this.falls.push(new Fall(root, rp, amb.extra, 17));
    }
    this.lastT = null;
    this.pieceList = this.pieces.filter(Boolean).map((p) => p.o);
  }

  setSideColors(side) {
    const roofOf = (h) => (h.toUpperCase() === '#FF5B4D' ? '#D9573F' : '#4A78C8');
    this.shared.uFlagL.value.set(side.left); this.shared.uRoofL.value.set(roofOf(side.left));
    this.shared.uFlagR.value.set(side.right); this.shared.uRoofR.value.set(roofOf(side.right));
  }

  dispose() {
    this.root.traverse((o) => { if (o.geometry) o.geometry.dispose(); if (o.material) [].concat(o.material).forEach((m) => m && m.dispose()); });
    this.root.parent && this.root.parent.remove(this.root);
  }

  // 灯光、天空：照 env.std_light + scene_lib.setup_world / add_sun
  addLights(scene, cx, cz, span) {
    const L = this.meta.light, pal = this.meta.pal;
    const amb = new THREE.AmbientLight(lin(pal.ambient), L.ambient * Math.PI);
    scene.add(amb);
    const az = L.az * Math.PI / 180, el = L.el * Math.PI / 180;
    const to = new THREE.Vector3(Math.cos(az) * Math.cos(el), Math.sin(el), -Math.sin(az) * Math.cos(el));
    const sun = new THREE.DirectionalLight(new THREE.Color(L.color[0], L.color[1], L.color[2]), L.energy);
    sun.position.set(cx, 0, cz).addScaledVector(to, span * 2.5);
    sun.target.position.set(cx, 0, cz);
    sun.castShadow = true;
    const big = span > 24;
    sun.shadow.mapSize.set(big ? 4096 : 3072, big ? 4096 : 3072);
    const sc = sun.shadow.camera, half = span * 0.72;
    sc.left = -half; sc.right = half; sc.top = half; sc.bottom = -half; sc.near = span * 0.5; sc.far = span * 5;
    sun.shadow.bias = -0.0004; sun.shadow.normalBias = 0.03; sun.shadow.radius = 3;
    scene.add(sun, sun.target);
    this.sun = sun;
    this.sunDir = to;
    const sky = new THREE.Mesh(new THREE.SphereGeometry(300, 32, 16), new THREE.ShaderMaterial({
      side: THREE.BackSide, depthWrite: false, fog: false,
      uniforms: { uSky: { value: lin(pal.sky) }, uDeep: { value: lin(pal.sky_deep) } },
      vertexShader: 'varying vec3 vD; void main(){ vD = position; vec4 p = modelViewMatrix * vec4(position,1.0); gl_Position = projectionMatrix * p; }',
      fragmentShader: SMR + 'uniform vec3 uSky; uniform vec3 uDeep; varying vec3 vD; void main(){ float z = normalize(vD).y; gl_FragColor = vec4(mix(uSky, uDeep, smr(-0.35, -0.98, z)), 1.0);\n#include <colorspace_fragment>\n}',
    }));
    sky.renderOrder = -10;
    sky.onBeforeRender = (r, s, cam) => sky.position.copy(cam.position);
    scene.add(sky);
  }

  // movers：[[地图 x, 地图 y, 速度]]，角色和低飞的镖（照 env.set_ambient）
  update(gt, movers = []) {
    this.shared.uTime.value = gt / 20 * 2.4;
    this.shared.uLiqT.value = gt / 20;
    for (const [ob, v] of this.clouds) ob.position.x = v * gt;
    this.flags.forEach((fm, k) => { const ph = gt * 0.42 + k * 1.7; for (const m of fm) m.userData.u.uPh.value = ph; });
    for (const g of this.grass) {
      let best = 0, ux = 0, uy = 0, sp = 0;
      for (const [mx, my, v] of movers) {
        const dx = g.gx - mx, dy = g.gy - my;
        if (Math.abs(dx) > 0.9 || Math.abs(dy) > 0.9) continue;
        const d = Math.hypot(dx, dy), k = 1 - d / 0.88;
        if (k > best) { best = k; sp = v; [ux, uy] = d > 1e-4 ? [dx / d, -dy / d] : [0, 0]; }
      }
      if (best > 0) {
        const k = best ** 1.3;
        const tilt = 0.32 * k + 0.08 * k * Math.sin(gt * 2.3 + g.ph) * Math.min(1, sp / 0.08);
        g.ms.rotation.set(-uy * tilt, ux * tilt, 0);
        g.ms.scale.set(1 + 0.1 * k, 1 + 0.1 * k, 1 - 0.3 * k);
        g.dirty = true;
      } else if (g.dirty) { g.ms.rotation.set(0, 0, 0); g.ms.scale.set(1, 1, 1); g.dirty = false; }
    }
    for (const f of this.falls) f.update(gt);
    if (this.lastT === gt) return;
    this.lastT = gt;
    this.collapse(gt);
    this.setLiquids(gt);
  }

  // 液面跟着塌陷走（env.set_liquids）：这一组的岸塌了就往下沉、消失；岸先塌了的边上的瀑布在两次塌陷之间出现
  setLiquids(gt) {
    if (!this.liquids.length) return;
    const rp = this.R, live = LQ.live_waves(rp, gt);
    const gone = (w) => (w === null || !live[w] ? null : LQ.gone_step(rp, w));
    for (const L of this.liquids) {
      const g_ = gone(L.w);
      let vis;
      if (L.kind === 'curtain') {
        const g0 = gone(L.w_from);
        vis = g0 !== null && gt >= g0 && (g_ === null || gt < g_ + 3);
      } else if (g_ === null || gt < g_) {
        vis = true;
        if (L.kind === 'sheet') L.ms.position.z = 0;
      } else {
        const u = (gt - g_) / 10;
        vis = u < 1 && L.kind === 'sheet';
        if (vis) L.ms.position.z = -0.6 * u * u;
      }
      L.ms.visible = vis;
    }
  }

  collapse(gt) {
    const R = this.R, warnN = R.cfg.COL_WARN ?? 30, ws = R.wave_steps, T = R.over ? R.T : 1e9;
    const live = ws.map((s) => !(gt > T + 1 && s > T));
    let started = 0;
    ws.forEach((s, i) => { if (gt >= s - warnN && live[i]) started++; });
    this.states.forEach((ms, k) => { if (ms) ms.visible = k === started; });
    for (const p of this.pieces) {
      if (!p) continue;
      const o = p.o, w = o.wave, r = o.rnd;
      if (w >= ws.length) { p.grp.visible = true; p.mesh.visible = false; continue; }
      const tCol = ws[w];
      let vis, kids = true, lx = o.cx, ly = -o.cy, lz = 0, rx = 0, ry = 0, rz = 0, warn = 0;
      if (gt < tCol - warnN || !live[w]) { vis = false; }
      else {
        const u = gt - tCol;
        const delay = (1 - o.d_in / this.dmax[w]) * 5 + r[0] * 2;
        if (u < delay) {
          const k = Math.min(1, (u + warnN) / (warnN + delay));
          const amp = 0.006 + 0.03 * k * k;
          const ph = gt * (2.2 + 2.5 * k) + r[1] * 20;
          lz = -0.02 - 0.06 * k * k + Math.sin(ph * 2.1) * amp * 0.5;
          lx = o.cx + Math.sin(ph) * amp; ly = -o.cy + Math.cos(ph * 1.3) * amp;
          const tilt = 0.012 + 0.05 * k * k;
          rx = Math.sin(ph * 0.9 + r[2] * 6) * tilt; ry = Math.cos(ph * 1.1 + r[3] * 6) * tilt;
          const pulse = 0.5 + 0.5 * Math.sin(gt * (0.5 + 0.9 * k) * 2 * Math.PI / 3);
          warn = 0.5 * (0.25 + 0.75 * pulse) * (0.4 + 0.6 * k);
          vis = true;
        } else {
          const tau = (u - delay) / 20;
          const drop = 0.5 * 9.8 * 1.6 * tau * tau + 0.6 * tau;
          vis = kids = drop < 16;
          const ox = o.cx - R.W * this.cs / 2, oy = o.cy - R.H * this.cs / 2, L = Math.hypot(ox, oy) + 1e-6, out = 0.5 * tau;
          lx = o.cx + ox / L * out; ly = -(o.cy + oy / L * out); lz = -0.08 - drop;
          const ax = r[4] * 2 * Math.PI, ang = (0.3 + 1.6 * r[5]) * tau * 1.6;
          rx = Math.cos(ax) * ang; ry = Math.sin(ax) * ang; rz = (r[2] - 0.5) * tau;
        }
      }
      p.grp.visible = kids;
      p.mesh.visible = vis;
      p.grp.position.set(lx, ly, lz);
      p.grp.rotation.set(rx, ry, rz);
      p.u.uWarn.value = warn;
    }
  }
}

// 漫天飘落的小片（照 env.Fall；随机数不同，规律相同）。第 12 集：rise = 火星从岩浆湖面往上升，glow = 自发光
class Fall {
  constructor(root, R, spec, seed = 11) {
    let s = seed;
    const rnd = () => { s = (s * 1103515245 + 12345) & 0x7fffffff; return s / 0x7fffffff; };
    const Wm = R.Wm, Hm = R.Hm;
    const n = this.n = Math.floor((spec.n || 40) * Wm * Hm / 216);
    const A = (f) => Float32Array.from({ length: n }, f);
    this.x0 = A(() => -3 + rnd() * (Wm + 5)); this.y0 = A(() => -2 + rnd() * (Hm + 5));
    this.rise = !!spec.rise;
    if (this.rise) {
      const lc = [...LQ.cells(R)].map((c) => c.split(',').map(Number));
      if (lc.length) for (let i = 0; i < n; i++) if (rnd() < 0.7) {
        const c = lc[Math.floor(rnd() * lc.length) % lc.length];
        this.x0[i] = (c[0] + rnd()) * R.cell; this.y0[i] = (c[1] + rnd()) * R.cell;
      }
    }
    this.per = A(() => (80 + rnd() * 70) / (spec.speed || 1)); this.ph = A(() => rnd()); this.rot = A(() => rnd() * 6.28);
    this.sz = A(() => (0.6 + 0.4 * rnd()) * (spec.size || 0.06));
    const cols = spec.cols.map((c) => new THREE.Color(c));
    const pos = new Float32Array(n * 12), col = new Float32Array(n * 12), idx = [];
    for (let i = 0; i < n; i++) {
      const c = cols[Math.floor(rnd() * cols.length) % cols.length];
      for (let k = 0; k < 4; k++) col.set([c.r, c.g, c.b], i * 12 + k * 3);
      idx.push(4 * i, 4 * i + 1, 4 * i + 2, 4 * i, 4 * i + 2, 4 * i + 3);
    }
    const g = this.g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    g.setAttribute('color', new THREE.BufferAttribute(col, 3));
    g.setIndex(idx);
    const emis = spec.glow ? +spec.glow : 0.3;
    const m = new THREE.MeshLambertMaterial({ vertexColors: true, side: THREE.DoubleSide });
    m.onBeforeCompile = (sh) => { sh.fragmentShader = sh.fragmentShader.replace('#include <emissivemap_fragment>', '#include <emissivemap_fragment>\ntotalEmissiveRadiance += diffuseColor.rgb * ' + emis.toFixed(2) + ';'); };
    m.customProgramCacheKey = () => 'fall' + emis;
    const ms = this.ms = new THREE.Mesh(g, m); ms.frustumCulled = false;
    root.add(ms);
  }
  update(gt) {
    const P = this.g.attributes.position.array;
    for (let i = 0; i < this.n; i++) {
      const u = ((gt / this.per[i] + this.ph[i]) % 1 + 1) % 1;
      const z = this.rise ? 3.4 * u - 0.45 : 3.6 * (1 - u) - 0.25;
      const x = this.x0[i] + 1.1 * u + 0.32 * Math.sin(gt * 0.11 + this.rot[i] * 3);
      const y = -this.y0[i] + 0.25 * Math.cos(gt * 0.09 + this.rot[i] * 2);
      const a = this.rot[i] + gt * 0.19, t = gt * 0.16 + this.rot[i] * 1.7;
      const s = this.sz[i] * Math.max(0, Math.min(1, Math.min(u / 0.06, (1 - u) / 0.08)));
      const e1 = [Math.cos(a) * s, Math.sin(a) * s, 0], e2 = [-Math.sin(a) * Math.cos(t) * s * 0.6, Math.cos(a) * Math.cos(t) * s * 0.6, Math.sin(t) * s * 0.6];
      const o = i * 12;
      P[o] = x + e1[0]; P[o + 1] = y + e1[1]; P[o + 2] = z + e1[2];
      P[o + 3] = x + e2[0]; P[o + 4] = y + e2[1]; P[o + 5] = z + e2[2];
      P[o + 6] = x - e1[0]; P[o + 7] = y - e1[1]; P[o + 8] = z - e1[2];
      P[o + 9] = x - e2[0]; P[o + 10] = y - e2[1]; P[o + 11] = z - e2[2];
    }
    this.g.attributes.position.needsUpdate = true;
  }
}
