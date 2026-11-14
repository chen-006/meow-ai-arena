// =====================================================================
//  造桥小剧场 3D v2（低多边形台地 + 桥为主角）
//  【第 9 集制作副本 · 搭桥动画版】在原版上加 cfg.build：开头 pre 秒模拟时间停在 t0，杆件按顺序"飞入生长"拼成桥，
//  镜头从高空俯冲到常规机位，叠层弹出材料价签；pre 之后照常播放回放。原版（原版小剧场渲染器）不改。
//  只播放引擎导出的逐帧数据（回放 JSON），网页里不另算物理。
//  THEATER3D.setProgram(prog) → {frames, clips}；THEATER3D.draw(n) 确定性地画第 n 帧（30 fps）。
//  prog = {clips:[{replay, t0, t1, title, sub, slow:true|false|[...], speed, analysis, cam:{yaw,pitch,dist,tx,ty,tz,fov,push,fill}}]}
//  同一帧号 → 同一画面：所有动画都是 (模拟时间 t, 视频时间 vt) 的纯函数，随机数全部用固定种子。
//  v2 构图：镜头拉远、略高斜俯视，桥占画面宽度约七成；两岸是方正的台地（正面切开的"沙盘"），
//  中间深落差 + 暗色低多边形水面，远处水面雾化融进天空；环境极简，杆件颜色饱和、按材料一眼区分。
//  横向尺寸（车宽、车道、桥面宽、桁架间距）全部由回放里的车辆物理尺寸推出，车辆按物理数据画。
// =====================================================================
import * as THREE from 'three';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { GTAOPass } from 'three/addons/postprocessing/GTAOPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { ShaderPass } from 'three/addons/postprocessing/ShaderPass.js';
import { mergeGeometries } from 'three/addons/utils/BufferGeometryUtils.js';

// ------------------------------------------------------------ 确定性
function mulberry(seed){ let a = seed >>> 0; return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; }
Math.random = mulberry(20261106);   // three 附加件里用到 Math.random 的噪声纹理也变成固定的

const QS = new URLSearchParams(location.search);
// 空气透视（替换 three 的线性雾）：起雾距离 fogNear 以内清晰，往外按指数变淡、偏向天际色，最多 94%
//  另加"低处雾"：桥面（y=0）以下越低越容易起雾（深峡谷谷底、远处海面的底部化进薄雾里）
THREE.ShaderChunk.fog_pars_vertex = THREE.ShaderChunk.fog_pars_vertex.replace('varying float vFogDepth;', 'varying float vFogDepth;\nvarying float vFogY;');
THREE.ShaderChunk.fog_vertex = THREE.ShaderChunk.fog_vertex.replace('vFogDepth = - mvPosition.z;', 'vFogDepth = - mvPosition.z;\n#ifdef USE_INSTANCING\nvFogY = (modelMatrix * instanceMatrix * vec4(transformed, 1.0)).y;\n#else\nvFogY = (modelMatrix * vec4(transformed, 1.0)).y;\n#endif');
THREE.ShaderChunk.fog_pars_fragment = THREE.ShaderChunk.fog_pars_fragment.replace('varying float vFogDepth;', 'varying float vFogDepth;\nvarying float vFogY;');
THREE.ShaderChunk.fog_fragment = `#ifdef USE_FOG
  float fogD = max(vFogDepth - fogNear, 0.0);
  float fogFactor = 0.94 * (1.0 - exp(-fogD / fogFar));
  fogFactor = max(fogFactor, 0.9 * (1.0 - exp(-fogD / (fogFar * 0.3))) * smoothstep(-4.0, -24.0, vFogY));
  gl_FragColor.rgb = mix(gl_FragColor.rgb, fogColor, fogFactor);
#endif`;
const WV = 1920, HV = 1080, FPS = +(new URLSearchParams(location.search).get('fps') || 30);   // 11-07：?fps=60 出 60 帧
const SS = +(QS.get('ss') || 2);          // 超采样倍数（内部 3840×2160，截图时由浏览器 2:1 缩小）

const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
const lerp = (a, b, t) => a + (b - a) * t;
const sstep = (a, b, x) => { const t = clamp((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); };
function h2(i, j, s = 0){ let h = (Math.imul(i | 0, 374761393) + Math.imul(j | 0, 668265263) + Math.imul(s | 0, 1442695041)) | 0; h = Math.imul(h ^ (h >>> 13), 1274126177); h ^= h >>> 16; return (h >>> 0) / 4294967296; }
function vnoise(x, y, s = 0){ const i = Math.floor(x), j = Math.floor(y), fx = x - i, fy = y - j; const u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy); return lerp(lerp(h2(i, j, s), h2(i + 1, j, s), u), lerp(h2(i, j + 1, s), h2(i + 1, j + 1, s), u), v); }
function fbm(x, y, s = 0, o = 4){ let a = 0.5, f = 1, sum = 0, nrm = 0; for (let k = 0; k < o; k++){ sum += a * vnoise(x * f, y * f, s + k * 17); nrm += a; a *= 0.5; f *= 2.03; } return sum / nrm; }
const col = hex => new THREE.Color(hex);

// ------------------------------------------------------------ 配色（自定）
//  环境：嫩绿草皮台面 + 暖砂岩分层崖壁 + 深蓝绿水面，饱和度压低；桥：饱和、按材料分色。
const PAL = {
  grass: ['#88aa50', '#90b257', '#80a34b', '#97b75d'], grassEdge: '#7a9a45', soil: '#7a5638',
  rock: ['#d29a62', '#c38a55', '#dcaa72', '#b97f4e', '#cf9f6c', '#c49060'], rockDark: '#8a5d3c', wet: '#5b4232',
  asphalt: '#55585e', lane: '#efece2', gravel: '#b3a68f', concrete: '#a59c8e', concreteDark: '#8b8275',
  water: '#1a4a6e', waterDeep: '#123a55',
  wood: '#e2a250', woodEnd: '#b98142', steel: '#e4572e', cable: '#2d3036', spring: '#79c94a', hyd: '#2eb7c4', chrome: '#e9edf1', joint: '#3a3f46',
  deckTop: '#4f5258', deckSide: '#d8d2c4', deckBeam: '#5d6670',
};
const SKY_TOP = col('#1f5cb8'), SKY_MID = col('#4489d6'), SKY_HOR = col('#86b7e6'), FOG = SKY_HOR;
const SUN_DIR = new THREE.Vector3(-0.6, 0.56, 0.5).normalize();   // 太阳在镜头左前方、高度角约 35°：暖色侧光、较长阴影

// ------------------------------------------------------------ 回放准备（慢放/时间重映射逻辑同 v1）
function parseEv(s){ const p = String(s).trim().split(/\s+/); return {t: parseFloat(p[0]), type: p[1], a: p.slice(2)}; }
function frameF(C, t){ return clamp((t - C.F[0].t) / C.dtf, 0, C.F.length - 1); }
function allP(C, t){ const f = frameF(C, t), i = Math.floor(f), fr = f - i, F = C.F; if (i >= F.length - 1 || fr < 1e-9) return F[i].p; const a = F[i].p, b = F[i + 1].p, o = new Array(a.length); for (let k = 0; k < a.length; k++) o[k] = a[k] + (b[k] - a[k]) * fr; return o; }
function frameAt(C, t){ return C.F[Math.floor(frameF(C, t) + 1e-9)]; }
function vehCenter(C, v, t){ const P = allP(C, t); let x = 0, y = 0; for (const k of v.idx){ x += P[2 * k]; y += P[2 * k + 1]; } return [x / v.idx.length, y / v.idx.length]; }
function memMid(C, i, t){ const m = C.R.members[i]; const P = allP(C, t); return [(P[2 * m[0]] + P[2 * m[1]]) / 2, (P[2 * m[0] + 1] + P[2 * m[1] + 1]) / 2]; }
function groundYAt(lv, x){ let best = null; for (const l of (lv.terrain || [])) for (let k = 0; k + 1 < l.length; k++){ const [x1, y1] = l[k], [x2, y2] = l[k + 1]; if (x1 !== x2 && x >= Math.min(x1, x2) && x <= Math.max(x1, x2)){ const y = y1 + (y2 - y1) * (x - x1) / (x2 - x1); if (best == null || y > best) best = y; } } return best ?? 0; }

// 车辆物理尺寸（标准车辆：质点顺序 后轮、前轮、后车身、前车身）
function vehDims(C, v){
  const P = C.F[0].p, k = v.p0, pr = C.R.particles;
  const wb = Math.hypot(P[2 * k + 2] - P[2 * k], P[2 * k + 3] - P[2 * k + 1]) || 1;
  const r = pr[k].r || 0.3, br = (pr[k + 2] && pr[k + 2].r) || 0.15;
  const h = (P[2 * k + 5] + P[2 * k + 7]) / 2 - (P[2 * k + 1] + P[2 * k + 3]) / 2;
  const H = Math.max(r * 1.6, h + br);
  const W = Math.min(0.5 * (wb + 2 * r) + 0.12, 1.5 * (H + r) + 0.2);
  return {wb, r, H, W};
}

function prepClip(cfg){
  const R = cfg.replay, F = R.frames, lv = R.level;
  const C = {cfg, R, F, lv};
  C.dtf = (F[F.length - 1].t - F[0].t) / Math.max(1, F.length - 1);
  C.t0 = cfg.t0 ?? F[0].t; C.t1 = Math.min(cfg.t1 ?? F[F.length - 1].t, F[F.length - 1].t);
  C.NB = R.members.filter(m => m[3] === -1).length;
  C.ev = (R.events || []).map(parseEv);
  C.breakT = {}; C.breaks = [];
  for (const e of C.ev) if (e.type === 'BREAK'){ const i = +e.a[0]; if (!(i in C.breakT)){ C.breakT[i] = e.t; C.breaks.push({t: e.t, i, f: +e.a[1]}); } }
  { let cs = -9, cnt = 0; for (const b of C.breaks){ if (b.t - cs > 0.6){ cs = b.t; cnt = 0; } b.fx = cnt++ < 8; } }
  C.L0 = R.members.map(m => Math.hypot(F[0].p[2 * m[1]] - F[0].p[2 * m[0]], F[0].p[2 * m[1] + 1] - F[0].p[2 * m[0] + 1]));
  C.realWater = lv.water_y != null && lv.water_y > -1e5;
  // 车辆
  const cnt = {};
  C.veh = R.vehicles.map((v, vi) => {
    const idx = []; R.particles.forEach((p, k) => { if (p.o === vi) idx.push(k); });
    const tp = v.sab ? 'saboteur' : (['car', 'van', 'truck'].includes(v.type) ? v.type : 'car');
    const n = cnt[tp] = (cnt[tp] || 0) + 1;
    return {...v, vi, idx, tp, nth: n - 1, tArr: null, tFail: null, reason: 0, entry: null};
  });
  for (const v of C.veh) v.dims = (v.p0 != null && v.idx.length === 4) ? vehDims(C, v) : {wb: 1.2, r: 0.25, H: 0.6, W: 0.9};
  // 到达/失败时刻（素材库 11-07 补：v3 一直没读这两个事件，到达后的车停在终点线上、翻车慢放不触发）
  for (const e of C.ev){
    const v = C.veh[+e.a[0]]; if (!v) continue;
    if (e.type === 'VARRIVE' && v.tArr == null) v.tArr = e.t;
    else if (e.type === 'VFAIL' && v.tFail == null){ v.tFail = e.t; v.reason = +e.a[1] || 0; }
  }
  // 横向尺寸：由最宽的车推出
  const Wmax = Math.max(0.6, ...C.veh.filter(v => !v.sab).map(v => v.dims.W));
  const Hmax = Math.max(0.6, ...C.veh.map(v => v.dims.H + v.dims.r));
  const LANE = 0.5 * Wmax + 0.1, DW = LANE + 0.5 * Wmax + 0.22;
  C.D = {Wmax, Hmax, LANE, DW, TW: DW + 0.1, ROAD_HW: DW, SHOULDER: DW + 0.45, PH: DW + 0.7, CLEAR: Hmax + 0.45};
  for (const v of C.veh) v.lane = v.laneZ != null ? v.laneZ : (v.sab ? 0 : (v.dir > 0 ? C.D.LANE : -C.D.LANE));   // 11-08 甲方3D：laneZ 指定车道
  C.T = terrainInfo(C);
  // 没有真实水面的关（深峡谷）：谷底画一条河，只做画面
  C.waterY = C.realWater ? lv.water_y : (cfg.dry ? C.T.floor - 0.6 : C.T.floor + 0.35);   // 11-08 甲方3D：dry = 无水峡谷（水面压到谷底以下）
  C.hasWater = true;
  C.killY = lv.kill_y ?? C.waterY - 4;
  for (const v of C.veh){
    for (let f = 1; f < F.length && !v.entry; f++){
      let lo = 1e9; for (const k of v.idx) lo = Math.min(lo, F[f].p[2 * k + 1] - (R.particles[k].r || 0));
      if (lo < C.waterY){ const c = vehCenter(C, v, F[f].t); if (c[0] > C.T.main.x0 - 0.5 && c[0] < C.T.main.x1 + 0.5) v.entry = {t: F[f].t, x: c[0], y: C.waterY}; else break; }
    }
  }
  // 桥节点落水 → 小水花
  C.debris = [];
  for (let k = R.n_anchor; k < R.particles.length; k++){
    if (R.particles[k].o !== -1) continue;
    for (let f = 1; f < F.length; f++){
      const y0 = F[f - 1].p[2 * k + 1], y1 = F[f].p[2 * k + 1];
      if (y0 >= C.waterY && y1 < C.waterY){ const x = F[f].p[2 * k]; if (!C.debris.some(d => Math.abs(d.x - x) < 1.0 && Math.abs(d.t - F[f].t) < 0.4)) C.debris.push({t: F[f].t, x, k}); break; }
    }
  }
  C.debris = C.debris.slice(0, 16);
  C.hydIdx = []; R.members.forEach((m, i) => { if (m[3] === -1 && m[2] === 5) C.hydIdx.push(i); });
  prepStress(C); buildSlow(C); buildRemap(C);
  prepBuild(C);
  return C;
}
// 搭桥：每根杆件的出现时刻（视频秒）。顺序 = 先路面（从左到右），再按 x 排其余杆件（同 x 先下后上）。
function prepBuild(C){
  const b = C.cfg.build; C.pre = 0; C.bt = null;
  if (!b) return;
  C.pre = b.pre ?? 6; C.nframes += Math.round(C.pre * FPS);
  const P = C.F[0].p, R = C.R, ids = [];
  for (let i = 0; i < C.NB; i++){ const m = R.members[i]; ids.push({i, k: m[2], x: (P[2 * m[0]] + P[2 * m[1]]) / 2, y: (P[2 * m[0] + 1] + P[2 * m[1] + 1]) / 2}); }
  const deck = ids.filter(q => q.k === 0).sort((a, b) => a.x - b.x), rest = ids.filter(q => q.k !== 0).sort((a, b) => (a.x - b.x) || (a.y - b.y));
  const a0 = b.a0 ?? 0.8, a1 = b.a1 ?? (C.pre - 1.2), d1 = a0 + (a1 - a0) * (b.split ?? 0.38);
  C.bt = new Array(R.members.length).fill(0); C.grow = b.grow ?? 0.45;
  deck.forEach((q, n) => { C.bt[q.i] = a0 + (d1 - a0) * n / Math.max(1, deck.length - 1); });
  rest.forEach((q, n) => { C.bt[q.i] = d1 + 0.15 + (a1 - d1 - 0.15) * n / Math.max(1, rest.length - 1); });
  C.firstOfKind = {}; for (const q of [...deck, ...rest]) if (!(q.k in C.firstOfKind) || C.bt[q.i] < C.bt[C.firstOfKind[q.k]]) C.firstOfKind[q.k] = q.i;
}
function buildK(C, i, vt){ if (!C.bt) return 1; return clamp((vt - C.bt[i]) / C.grow, 0, 1); }
function buildSlow(C){
  const W = [], cfg = C.cfg;
  if (Array.isArray(cfg.slow)) for (const s of cfg.slow) W.push({rate: 0.3, zoom: 0.8, kind: 'custom', ...s});
  else if (cfg.slow !== false){
    const bs = [...C.breaks].sort((a, b) => a.t - b.t);
    let k = 0;
    while (k < bs.length){
      let j = k; while (j + 1 < bs.length && bs[j + 1].t - bs[j].t < 0.6) j++;
      const a = bs[k].t, b = Math.min(bs[j].t, a + 1.0);
      const pts = bs.slice(k, Math.min(j + 1, k + 6)).map(e => memMid(C, e.i, e.t - 0.02));
      if (W.filter(w => w.kind === 'break').length < (cfg.maxBreakSlow ?? 1e9)) W.push({a: a - 0.35, b: b + 0.35, rate: cfg.breakRate ?? 0.25, fx: pts.reduce((s, p) => s + p[0], 0) / pts.length, fy: pts.reduce((s, p) => s + p[1], 0) / pts.length, zoom: 0.88, kind: 'break'});   // maxBreakSlow：素材库用，只慢放前几簇断裂
      k = j + 1;
    }
    let nWater = 0;
    for (const v of [...C.veh].sort((p, q) => ((p.entry ? p.entry.t : p.tFail) ?? 1e9) - ((q.entry ? q.entry.t : q.tFail) ?? 1e9))){
      if ((v.entry || (v.tFail != null && [2, 3, 5].includes(v.reason))) && nWater++ >= (cfg.maxWaterSlow ?? 1e9)) continue;   // maxWaterSlow：素材库用，只慢放最先出事的几辆
      if (v.entry) W.push({a: v.entry.t - 0.4, b: v.entry.t + 0.55, rate: cfg.waterRate ?? 0.35, fx: v.entry.x, fy: C.waterY + 1.0, zoom: 0.9, kind: 'water'});
      else if (v.tFail != null && [2, 3, 5].includes(v.reason)){ const c = vehCenter(C, v, v.tFail); W.push({a: v.tFail - 0.4, b: v.tFail + 0.4, rate: 0.35, fx: c[0], fy: c[1], zoom: 0.84, kind: 'fail'}); }
    }
    for (const e of C.ev) if (e.type === 'SHIPHIT'){ const m = memMid(C, +e.a[0], e.t); W.push({a: e.t - 0.4, b: e.t + 0.6, rate: 0.35, fx: m[0], fy: m[1], zoom: 0.85, kind: 'shiphit'}); break; }
    if (cfg.hydSlow !== false){
      let lastAct = -9;
      for (const e of C.ev) if (e.type === 'ACT' && e.a[0] === 'hyd' && C.hydIdx.length && Math.abs(+e.a[2] - 1) > 1e-3 && e.t - lastAct > 0.3){
        lastAct = e.t;
        const pts = C.hydIdx.map(i => memMid(C, i, e.t));
        W.push({a: e.t - 0.1, b: e.t + 0.8, rate: cfg.hydRate ?? 0.5, fx: pts.reduce((s, p) => s + p[0], 0) / pts.length, fy: pts.reduce((s, p) => s + p[1], 0) / pts.length + 1, zoom: 0.88, kind: 'hyd'});
      }
    }
  }
  if (cfg.peakSlow){   // 受力峰值附近慢放：cfg.peakSlow = {at: 阈值%, rate}
    const ps = cfg.peakSlow, thr = ps.at ?? 96; let fb = -1;
    for (let f = 0; f < C.F.length; f++) if (C.F[f].t >= C.t0 && C.F[f].t <= C.t1 && C.smax[f] >= thr && (fb < 0 || C.smax[f] > C.smax[fb])) fb = f;
    if (fb >= 0){ const tp = C.F[fb].t, i = C.mark[fb], m = i >= 0 ? memMid(C, i, tp) : null; W.push({a: tp - (ps.pre ?? 0.5), b: tp + (ps.post ?? 0.4), rate: ps.rate ?? 0.4, fx: m ? m[0] : null, fy: m ? m[1] : null, zoom: ps.zoom ?? 0.9, kind: 'peak'}); }
  }
  C.slow = W.filter(w => w.b > C.t0 && w.a < C.t1);
  C.ramp = cfg.ramp ?? 0.3;
}
function slowW(C, t){
  let best = 0, win = null, mins = 1;
  for (const w of C.slow){
    const b = sstep(w.a - C.ramp, w.a, t) * (1 - sstep(w.b, w.b + C.ramp, t));
    const s = 1 - (1 - w.rate) * b;
    if (s < mins) mins = s;
    if (b > best){ best = b; win = w; }
  }
  return {w: best, s: mins, win};
}
function buildRemap(C){
  const dt = 1 / 600, T = [C.t0], V = [0];
  let t = C.t0, v = 0;
  const base = C.cfg.speed || 1;
  while (t < C.t1 - 1e-9){ const h = Math.min(dt, C.t1 - t); v += h / (base * slowW(C, t + h / 2).s); t += h; T.push(t); V.push(v); }
  C.TT = T; C.VV = V; C.vdur = v;
  C.nframes = Math.max(1, Math.round(v * FPS));
}
function simAt(C, vt){ const V = C.VV; if (vt <= 0) return C.t0; if (vt >= C.vdur) return C.t1; let lo = 0, hi = V.length - 1; while (hi - lo > 1){ const m = (lo + hi) >> 1; if (V[m] <= vt) lo = m; else hi = m; } return lerp(C.TT[lo], C.TT[hi], (vt - V[lo]) / (V[hi] - V[lo] || 1)); }
function vidAt(C, t){ const T = C.TT; if (t <= C.t0) return (t - C.t0); if (t >= C.t1) return C.vdur + (t - C.t1); let lo = 0, hi = T.length - 1; while (hi - lo > 1){ const m = (lo + hi) >> 1; if (T[m] <= t) lo = m; else hi = m; } return lerp(C.VV[lo], C.VV[hi], (t - T[lo]) / (T[hi] - T[lo] || 1)); }

// ------------------------------------------------------------ 地形信息（主峡谷、坑、桥墩、台地轮廓）
function terrainInfo(C){
  const lv = C.lv, TR = lv.terrain || [], pts = TR.flat(), D = C.D;
  const top = Math.max(...pts.map(q => q[1]));
  const xa = Math.min(...pts.map(q => q[0])), xb = Math.max(...pts.map(q => q[0]));
  const deps = []; let cur = null;
  for (let x = xa; x <= xb + 1e-9; x += 0.05){
    const y = groundYAt(lv, x), low = y < top - 0.05;
    if (low){ if (!cur) cur = {x0: x, x1: x, floor: y}; cur.x1 = x; cur.floor = Math.min(cur.floor, y); }
    else if (cur){ deps.push(cur); cur = null; }
  }
  if (cur) deps.push(cur);
  const vx = [...new Set(pts.map(q => q[0]))];
  const snap = x => { let b = x, bd = 0.2; for (const v of vx){ const d = Math.abs(v - x); if (d < bd){ bd = d; b = v; } } return b; };
  for (const d of deps){ d.x0 = snap(d.x0 - 0.05); d.x1 = snap(d.x1 + 0.05); }
  let main = deps[0] || {x0: 0, x1: 10, floor: top - 8};
  for (const d of deps) if ((d.x1 - d.x0) * (top - d.floor) > (main.x1 - main.x0) * (top - main.floor)) main = d;
  const I = {top, main, floor: main.floor};
  const hasPit = deps.length > 1;
  // 台地正面（朝镜头）：一般向镜头伸出较多，画面下方两角是台地和崖面、中间是水道；有坑的关正面贴近路边，坑从正面切开看得见机关
  I.hasPit = hasPit;
  I.ZF = C.cfg.zf ?? (hasPit ? D.SHOULDER + 1.7 : D.SHOULDER + 6);   // 无坑的关在 baseFrame 里按镜头重算
  I.pits = deps.filter(d => d !== main).map(d => ({...d, z0: -D.PH, z1: I.ZF + 0.5}));
  I.piers = TR.filter(l => l.every(q => q[0] > main.x0 + 0.05 && q[0] < main.x1 - 0.05));
  I.cx = (main.x0 + main.x1) / 2;
  I.span = main.x1 - main.x0;
  // 台地背面轮廓（参差），离桥越远越深
  I.ZB = (side, d) => -(D.DW + 13 + 20 * sstep(3, 45, d) + 6 * fbm(d * 0.07, side * 3.1, 211 + side, 3));
  return I;
}

// ------------------------------------------------------------ 几何工具
class Geo {   // 非索引三角形 + 逐面颜色 + 平面法线
  constructor(){ this.p = []; this.c = []; this.n = []; }
  tri(a, b, c, color, hint){
    let ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2], vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
    let nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
    const L = Math.hypot(nx, ny, nz); if (L < 1e-9) return;
    if (hint && nx * hint[0] + ny * hint[1] + nz * hint[2] < 0){ [b, c] = [c, b]; nx = -nx; ny = -ny; nz = -nz; }
    nx /= L; ny /= L; nz /= L;
    const cc = typeof color === 'function' ? color(nx, ny, nz) : color;
    for (const q of [a, b, c]){ this.p.push(q[0], q[1], q[2]); this.c.push(cc.r, cc.g, cc.b); this.n.push(nx, ny, nz); }
  }
  quad(a, b, c, d, color, hint, flip){ if (flip){ this.tri(a, b, d, color, hint); this.tri(b, c, d, color, hint); } else { this.tri(a, b, c, color, hint); this.tri(a, c, d, color, hint); } }
  build(){
    const g = new THREE.BufferGeometry();
    g.setAttribute('position', new THREE.Float32BufferAttribute(this.p, 3));
    g.setAttribute('color', new THREE.Float32BufferAttribute(this.c, 3));
    g.setAttribute('normal', new THREE.Float32BufferAttribute(this.n, 3));
    g.computeBoundingSphere();
    return g;
  }
}
function jit(c, h, amt = 0.07){ const k = 1 - amt + 2 * amt * h; return new THREE.Color(c.r * k, c.g * k, c.b * k); }
function mixC(a, b, t){ return a.clone().lerp(b, clamp(t, 0, 1)); }
function stdMat(hex, o = {}){ return new THREE.MeshStandardMaterial({color: hex, roughness: o.r ?? 0.85, metalness: o.m ?? 0, flatShading: o.flat ?? true, side: o.side ?? THREE.FrontSide, ...(o.x || {})}); }
const VCMAT = () => new THREE.MeshStandardMaterial({vertexColors: true, roughness: 0.95, metalness: 0, flatShading: true});
function shadowAll(o, cast = true, recv = true){ o.traverse(m => { if (m.isMesh){ m.castShadow = cast; m.receiveShadow = recv; } }); return o; }
function displace(geo, f){ const p = geo.attributes.position; for (let i = 0; i < p.count; i++){ const x = p.getX(i), y = p.getY(i), z = p.getZ(i); const d = f(x, y, z); p.setXYZ(i, x + d[0], y + d[1], z + d[2]); } p.needsUpdate = true; return geo; }
function colorize(geo, f){ geo = geo.index ? geo.toNonIndexed() : geo; geo.computeVertexNormals(); const p = geo.attributes.position, n = geo.attributes.normal, c = []; for (let i = 0; i < p.count; i += 3){ const cy = (p.getY(i) + p.getY(i + 1) + p.getY(i + 2)) / 3; const cc = f(cy, n.getY(i), i / 3); for (let k = 0; k < 3; k++) c.push(cc.r, cc.g, cc.b); } geo.setAttribute('color', new THREE.Float32BufferAttribute(c, 3)); return geo; }
function boxAt(sx, sy, sz, x, y, z, mat){ const m = new THREE.Mesh(new THREE.BoxGeometry(sx, sy, sz), mat); m.position.set(x, y, z); return m; }

// ------------------------------------------------------------ 天空
function skyMaterial(){
  return new THREE.ShaderMaterial({
    side: THREE.BackSide, depthWrite: false, fog: false,
    uniforms: {top: {value: SKY_TOP}, mid: {value: SKY_MID}, hor: {value: SKY_HOR}, sun: {value: SUN_DIR}},
    vertexShader: 'varying vec3 vD; void main(){ vD = normalize(position); vec4 p = projectionMatrix * modelViewMatrix * vec4(position,1.0); gl_Position = p.xyww; }',
    fragmentShader: `uniform vec3 top, mid, hor, sun; varying vec3 vD;
      void main(){ float h = clamp(vD.y, -0.3, 1.0);
        vec3 c = mix(hor, mid, smoothstep(0.0, 0.14, h)); c = mix(c, top, smoothstep(0.12, 0.55, h));
        float s = max(dot(normalize(vD), sun), 0.0); c += vec3(1.0,0.9,0.7) * (pow(s, 10.0) * 0.12);
        if (vD.y < 0.0) c = hor;
        gl_FragColor = vec4(c, 1.0); }`,
  });
}

// ------------------------------------------------------------ 世界：台地（两岸方块，正面切开）
//  每块台地 = 顶面网格（草皮）+ 沿轮廓一圈的分层崖壁（正面 → 峡谷面 → 背面）。坑从正面切开。
function buildTerrain(C, scene){
  const I = C.T, D = C.D, top = I.top, main = I.main, ZF = I.ZF;
  const vmat = VCMAT();
  const grass = PAL.grass.map(col), rock = PAL.rock.map(col);
  const cEdge = col(PAL.grassEdge), cSoil = col(PAL.soil), cWet = col(PAL.wet), cGravel = col(PAL.gravel), cRockD = col(PAL.rockDark);
  const ybot = Math.min(I.floor, C.waterY) - 3;
  const EXT = 110;
  const pitAt = x => I.pits.find(p => x > p.x0 + 1e-6 && x < p.x1 - 1e-6);
  const block = side => {
    const xe = side < 0 ? main.x0 : main.x1, xFar = xe + side * EXT;
    // 沿 x 的采样（离桥越近越密），包含坑边
    let ds = []; for (let d = 0; d < EXT; ){ ds.push(d); d += d < 18 ? 1.0 : d < 40 ? 2.0 : d < 70 ? 4 : 8; } ds.push(EXT);
    const pe = []; for (const p of I.pits) for (const x of [p.x0, p.x1]){ const d = (x - xe) * side; if (d > 0.01 && d < EXT) pe.push(d); }
    ds = ds.filter(d => !pe.some(e => Math.abs(e - d) < 0.35)).concat(pe).sort((a, b) => a - b);
    const xs = ds.map(d => xe + side * d);
    // ---- 顶面：前段固定 z 行（含路面/路肩/坑边约束线），后段按背面轮廓归一化
    const zFront = [ZF]; for (let z = ZF - 0.9; z > -D.PH - 0.01; z -= 0.9) zFront.push(z);
    const cons = [D.SHOULDER, -D.SHOULDER, D.ROAD_HW, -D.ROAD_HW, -D.PH, 0];
    let zf = zFront.filter(z => !cons.some(c => Math.abs(c - z) < 0.3)).concat(cons).sort((a, b) => b - a);
    zf = zf.filter(z => z <= ZF + 1e-6 && z >= -D.PH - 1e-6); if (zf[0] < ZF - 1e-6) zf.unshift(ZF);
    const NB = 9;
    const zAt = (i, j) => j < zf.length ? zf[j] : lerp(-D.PH, I.ZB(side, ds[i]), (j - zf.length + 1) / NB);
    const NJ = zf.length + NB;
    const G = new Geo(), V = [];
    for (let i = 0; i < xs.length; i++){
      const row = [];
      for (let j = 0; j < NJ; j++){
        let x = xs[i], z = zAt(i, j), y = top;
        const pit = pitAt(x);
        const az = Math.abs(z), dEdge = Math.min(ds[i], j < zf.length ? ZF - z : 99, Math.abs(z - I.ZB(side, ds[i])));
        const inner = i > 0 && i < xs.length - 1 && j > 0 && j < NJ - 1;
        if (inner && az > D.SHOULDER + 0.6 && !pe.some(e => Math.abs(e - ds[i]) < 0.01)){
          x += (h2(i, j, 3 + side) - 0.5) * 0.35; z += (h2(i, j, 9 + side) - 0.5) * 0.3;
          const m = sstep(D.SHOULDER + 0.6, D.SHOULDER + 3, az) * sstep(0.6, 3.0, dEdge);
          y = top + m * (0.32 * (fbm(x * 0.13, z * 0.13, 5, 3) - 0.45) + 0.6 * Math.max(0, fbm(x * 0.05, z * 0.05, 8, 2) - 0.55));
        }
        row.push([x, y, z, pit && j < zf.length && z > -D.PH + 1e-6]);
      }
      V.push(row);
    }
    for (let i = 0; i + 1 < xs.length; i++) for (let j = 0; j + 1 < NJ; j++){
      const a = V[i][j], b = V[i + 1][j], c = V[i + 1][j + 1], d = V[i][j + 1];
      const cx = (a[0] + b[0] + c[0] + d[0]) / 4, cz = (a[2] + b[2] + c[2] + d[2]) / 4;
      if (I.pits.some(p => cx > p.x0 && cx < p.x1 && cz > p.z0)) continue;
      const hh = h2(i, j, 41 + side), az = Math.abs(cz);
      const de = Math.min(Math.abs(cx - xe), ZF - cz, Math.abs(cz - I.ZB(side, Math.abs(cx - xe))));
      const colorF = (nx, ny) => {
        if (az < D.ROAD_HW + 0.05) return jit(col(PAL.asphalt), hh, 0.02);
        if (az < D.SHOULDER) return jit(cGravel, hh, 0.05);
        const gn = fbm(cx * 0.06, cz * 0.06, 55, 3) * (grass.length - 1);
        let g = mixC(grass[Math.floor(gn)], grass[Math.min(grass.length - 1, Math.floor(gn) + 1)], gn - Math.floor(gn));
        if (de < 0.7) g = mixC(g, cEdge, 0.5);
        if (ny < 0.9) g = mixC(g, col(PAL.rock[2]), (0.9 - ny) * 3);
        return jit(g, hh, 0.045);
      };
      G.quad(a, b, c, d, colorF, [0, 1, 0], h2(i, j, 5) > 0.5);
    }
    // ---- 崖壁：沿轮廓（正面 → 峡谷面 → 背面）的一整条带
    const outline = [];   // {x, z, n:[nx,nz], yt, corner}
    for (let i = xs.length - 1; i >= 0; i--){
      const x = xs[i], pit = pitAt(x);
      const isPitEdge = pe.some(e => Math.abs(e - ds[i]) < 1e-6);
      if (isPitEdge){
        // 坑边：两个高度的列（坑外台面、坑底）
        const p = I.pits.find(q => Math.abs(q.x0 - x) < 1e-6 || Math.abs(q.x1 - x) < 1e-6);
        const goingIntoPit = side < 0 ? Math.abs(p.x1 - x) < 1e-6 : Math.abs(p.x0 - x) < 1e-6;   // 沿轮廓从远端走向桥：左岸先遇到 x0
        const hiFirst = side < 0 ? Math.abs(p.x0 - x) < 1e-6 : Math.abs(p.x1 - x) < 1e-6;
        const pair = hiFirst ? [top, p.floor] : [p.floor, top];
        for (const yt of pair) outline.push({x, z: ZF, n: [0, 1], yt, pitEdge: true});
        void goingIntoPit;
        continue;
      }
      outline.push({x, z: ZF, n: [0, 1], yt: pit ? pit.floor : top});
    }
    outline[outline.length - 1].corner = true; outline[outline.length - 1].n = [-side * 0.7071, 0.7071];
    for (let j = 1; j < NJ; j++){ const o = {x: xe, z: zAt(0, j), n: [-side, 0], yt: top}; outline.push(o); }   // 峡谷面朝向河道（左岸朝 +x）
    outline[outline.length - 1].corner = true; outline[outline.length - 1].n = [-side * 0.7071, -0.7071];
    for (let i = 1; i < xs.length; i++) outline.push({x: xs[i], z: I.ZB(side, ds[i]), n: [0, -1], yt: top});
    // 轮廓弧长
    let s = 0; outline.forEach((o, k) => { if (k) s += Math.hypot(o.x - outline[k - 1].x, o.z - outline[k - 1].z); o.s = s; });
    const K = Math.max(5, Math.ceil((top - ybot) / 0.75));
    const W = [];
    for (let k = 0; k <= K; k++){
      const row = [];
      for (let q = 0; q < outline.length; q++){
        const o = outline[q], y = lerp(o.yt, ybot, k / K);
        let disp = 0, along = 0, yj = 0;
        if (k > 0){
          const band = Math.floor((top - y) / 0.95);
          disp = 0.42 * (h2(band, side, 5) - 0.5) + 0.85 * (fbm(o.s * 0.16, y * 0.35, 40 + side, 3) - 0.5) - 0.1;
          if (k === 1 && o.yt === top) disp -= 0.12;
          if (!o.corner && !o.pitEdge && q > 0 && q < outline.length - 1) along = (h2(k, q, 77 + side) - 0.5) * 0.45;
          if (k < K) yj = (h2(q, k, 91 + side) - 0.5) * 0.22 * (o.yt - ybot) / K;
          // 桥台附近、坑边：崖面收平，不挡桁架/不穿坑壁
          if (o.n[0] !== 0 && Math.abs(o.z) < D.DW + 1.2) disp = Math.min(disp, -0.05) * 0.5;
          if (o.pitEdge || I.pits.some(p => Math.abs(o.x - p.x0) < 0.7 || Math.abs(o.x - p.x1) < 0.7)) disp = Math.min(disp, 0) * 0.3;
        }
        // 切向（沿轮廓）方向
        const tx = -o.n[1] * side, tz = o.n[0] * side;
        row.push([o.x + o.n[0] * disp + tx * along, y + yj, o.z + o.n[1] * disp + tz * along, y]);
      }
      W.push(row);
    }
    for (let k = 0; k < K; k++) for (let q = 0; q + 1 < outline.length; q++){
      const a = W[k][q], b = W[k][q + 1], c = W[k + 1][q + 1], d = W[k + 1][q];
      const o = outline[q], o2 = outline[q + 1];
      if (Math.abs(o.x - o2.x) < 1e-6 && Math.abs(o.z - o2.z) < 1e-6) continue;   // 坑边退化列
      const nx = (o.n[0] + o2.n[0]) / 2, nz = (o.n[1] + o2.n[1]) / 2;
      const cy = (a[3] + c[3]) / 2, hh = h2(k, q, 88 + side), yt = Math.min(o.yt, o2.yt);
      const band = Math.floor((top - cy) / 0.95 + 0.6 * fbm(o.s * 0.05, 3.3, 61 + side, 2));
      const colorF = (fx, fy) => {
        let cc;
        if (yt >= top - 1e-6 && cy > top - 0.42) cc = mixC(cSoil, cRockD, 0.25);
        else cc = rock[((band % rock.length) + rock.length) % rock.length];
        if (fy > 0.5) cc = mixC(cc, col('#e2b98a'), 0.45);
        if (cy < C.waterY + 0.35) cc = mixC(cc, cWet, sstep(C.waterY + 0.35, C.waterY - 1.5, cy) * 0.8 + 0.25);
        return jit(cc, hh, 0.07);
      };
      G.quad(a, b, c, d, colorF, [nx, 0, nz], h2(k, q, 13) > 0.5);
    }
    // 草皮边缘的一圈薄"唇"（顶面外沿略突出，读出台面厚度）
    const m = new THREE.Mesh(G.build(), vmat); m.receiveShadow = true; m.castShadow = true; scene.add(m);
  };
  block(-1); block(1);
  // ---- 桥台（混凝土）
  const conc = stdMat(PAL.concrete, {r: 0.9}), concD = stdMat(PAL.concreteDark, {r: 0.92});
  const anchors = C.lv.anchors || [];
  const usedA = new Set(), P0 = C.F[0].p; for (let i = 0; i < C.NB; i++){ const m = C.R.members[i]; for (const k of [m[0], m[1]]) if (k < C.R.n_anchor) usedA.add(P0[2 * k].toFixed(2) + ',' + P0[2 * k + 1].toFixed(2)); }
  for (const [side, xe] of [[-1, main.x0], [1, main.x1]]){
    // 桥台：顶部一段承台 + 往下连续用到的锚点（相距 ≤ 2.6 米）连成一体；更深处单独用到的锚点只做一块混凝土垫座
    const onWall = anchors.map((a, k) => ({a, k})).filter(o => Math.abs(o.a[0] - xe) < 0.05 && o.a[1] < top - 0.05 && usedA.has(o.a[0].toFixed(2) + ',' + o.a[1].toFixed(2))).map(o => o.a[1]).sort((u, v) => v - u);
    let yb = top - 1.3; const pads = [];
    for (const y of onWall){ if (y - 0.7 > yb - 0.01) continue; if (yb - y < 2.6) yb = y - 0.7; else pads.push(y); }
    let room = 1.2; for (const p of I.pits){ if (side < 0 && p.x1 <= xe + 1e-6) room = Math.min(room, xe - p.x1); if (side > 0 && p.x0 >= xe - 1e-6) room = Math.min(room, p.x0 - xe); }
    const h = top - 0.02 - yb, w = Math.max(0.3, Math.min(0.7, room - 0.05)), zw = 2 * D.DW + 0.5;
    const mm = boxAt(w + 0.1, h, zw, xe + side * (w / 2 - 0.12), yb + h / 2, 0, conc); shadowAll(mm); scene.add(mm);
    const cap = boxAt(w + 0.2, 0.22, zw + 0.2, xe + side * (w / 2 - 0.17), top - 0.42, 0, concD); shadowAll(cap); scene.add(cap);
    for (const y of pads){ const pd = boxAt(w + 0.1, 1.3, zw, xe + side * (w / 2 - 0.12), y, 0, conc); shadowAll(pd); scene.add(pd); }
  }
  // ---- 坑（混凝土衬砌，正面敞开）
  for (const p of I.pits){
    const dep = top - p.floor, Wd = p.x1 - p.x0, z0 = p.z0, z1 = ZF + 0.02, Dz = z1 - z0;
    const add = (sx, sy, sz, x, y, z, mat) => { const m = boxAt(sx, sy, sz, x, y, z, mat); shadowAll(m); scene.add(m); };
    const zc = (z0 + z1) / 2;
    add(Wd + 0.3, 0.3, Dz, (p.x0 + p.x1) / 2, p.floor - 0.15, zc, concD);
    add(0.25, dep, Dz, p.x0 - 0.125 + 0.01, p.floor + dep / 2 - 0.01, zc, conc);
    add(0.25, dep, Dz, p.x1 + 0.125 - 0.01, p.floor + dep / 2 - 0.01, zc, conc);
    add(Wd, dep, 0.25, (p.x0 + p.x1) / 2, p.floor + dep / 2 - 0.01, z0 - 0.125, conc);
    for (let x = p.x0 + 2; x < p.x1 - 0.8; x += 2) add(0.05, 0.015, Dz - 0.3, x, p.floor + 0.005, zc, conc);
    { const m = boxAt(Wd + 0.5, p.floor - (Math.min(I.floor, C.waterY) - 3), Dz, (p.x0 + p.x1) / 2, (p.floor + Math.min(I.floor, C.waterY) - 3) / 2 - 0.3, zc, stdMat(PAL.rock[1], {r: 0.95})); m.receiveShadow = true; scene.add(m); }
    // 沟壁：竖向分缝 + 顶部压顶 + 底部一道深色水渍，免得两面大灰墙太平
    const seam = stdMat('#7d766b', {r: 0.95}), stain = stdMat('#857d70', {r: 0.95});
    for (const [xw, sd] of [[p.x0, 1], [p.x1, -1]]){
      for (let z = z0 + 1.6; z < z1 - 0.3; z += 1.6) add(0.02, dep - 0.05, 0.04, xw + sd * 0.005, p.floor + dep / 2, z, seam);
      add(0.32, 0.08, Dz, xw - sd * 0.12, top + 0.03, zc, concD);
      add(0.015, 0.35, Dz - 0.1, xw + sd * 0.004, p.floor + 0.18, zc, stain);
    }
    for (let x = p.x0 + 1.5; x < p.x1 - 0.3; x += 1.5) add(0.04, dep - 0.05, 0.02, x, p.floor + dep / 2, z0 + 0.005, seam);
    // 坑口边缘的黄黑警示条
    const warn = stdMat('#e8b923', {r: 0.6});
    for (const x of [p.x0 - 0.12, p.x1 + 0.12]) add(0.26, 0.04, Dz, x, top + 0.005, zc, warn);
  }
  // ---- 河中礁石/桥墩（低多边形岩柱）
  for (const l of I.piers){
    const xa = Math.min(...l.map(q => q[0])), xb = Math.max(...l.map(q => q[0])), yt = Math.max(...l.map(q => q[1]));
    const g = new THREE.CylinderGeometry((xb - xa) * 0.32, (xb - xa) * 0.6, yt - ybot, 7, 5);
    displace(g, (x, y, z) => { const n = h2(Math.round(x * 13), Math.round(y * 13), Math.round(z * 13)); return [x * 0.18 * (n - 0.5), 0, z * 0.18 * (n - 0.5)]; });
    g.scale(1, 1, (2 * D.DW + 1.2) / (xb - xa));
    const gc = colorize(g, (cy, ny, f) => { let cc = col(PAL.rock[f % PAL.rock.length]); if (cy < -(yt - ybot) / 2 + (C.waterY - ybot) + 0.3) cc = mixC(cc, cWet, 0.6); return jit(cc, h2(f, 3, 3), 0.07); });
    const m = new THREE.Mesh(gc, vmat); m.position.set((xa + xb) / 2, (yt + ybot) / 2, 0); m.castShadow = m.receiveShadow = true; scene.add(m);
  }
  // ---- 引道路面 + 标线
  const asph = stdMat(PAL.asphalt, {r: 0.93, x: {polygonOffset: true, polygonOffsetFactor: -1, polygonOffsetUnits: -1}});
  const laneM = stdMat(PAL.lane, {r: 0.7, x: {polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2}});
  const segs = [];
  const cut = (a, b) => { let parts = [[a, b]]; for (const p of I.pits) parts = parts.flatMap(([u, v]) => (p.x1 <= u || p.x0 >= v) ? [[u, v]] : [[u, p.x0 - 0.25], [p.x1 + 0.25, v]].filter(q => q[1] - q[0] > 0.05)); return parts; };
  segs.push(...cut(main.x0 - EXT, main.x0 - 0.02), ...cut(main.x1 + 0.02, main.x1 + EXT));
  const dashL = Math.max(0.6, D.DW * 0.55), dashP = dashL * 2.4, lw = Math.max(0.05, D.DW * 0.045);
  for (const [a, b] of segs){
    const L = b - a, g = new THREE.PlaneGeometry(L, 2 * D.ROAD_HW); g.rotateX(-Math.PI / 2);
    const m = new THREE.Mesh(g, asph); m.position.set((a + b) / 2, top + 0.012, 0); m.receiveShadow = true; scene.add(m);
    for (const z of [-(D.ROAD_HW - 0.12), D.ROAD_HW - 0.12]){ const g2 = new THREE.PlaneGeometry(L, lw); g2.rotateX(-Math.PI / 2); const m2 = new THREE.Mesh(g2, laneM); m2.position.set((a + b) / 2, top + 0.016, z); m2.receiveShadow = true; scene.add(m2); }
    for (let k = Math.ceil(a / dashP); k * dashP < b; k++){
      const xa2 = Math.max(a + 0.1, k * dashP - dashL / 2), xb2 = Math.min(b - 0.1, k * dashP + dashL / 2); if (xb2 - xa2 < 0.15) continue;
      const g2 = new THREE.PlaneGeometry(xb2 - xa2, lw); g2.rotateX(-Math.PI / 2); const m2 = new THREE.Mesh(g2, laneM); m2.position.set((xa2 + xb2) / 2, top + 0.016, 0); m2.receiveShadow = true; scene.add(m2);
    }
  }
  // ---- 道路设施：桥头两侧的轮廓标（白柱 + 黑带 + 橙色反光片），离桥头约 9 米处的窄桥警示牌
  {
    const postP = [], skip = x => I.pits.some(p => x > p.x0 - 0.8 && x < p.x1 + 0.8) || (C.R.lights || []).some(l => Math.abs(l.x - x) < 0.8);
    for (const [side, xe] of [[-1, main.x0], [1, main.x1]]) for (let d = 1.4; d < 24; d += 2.6){ const x = xe + side * d; if (skip(x)) continue; for (const z of [D.ROAD_HW + 0.28, -(D.ROAD_HW + 0.28)]) postP.push([x, z]); }
    const ph = 0.5, gP = new THREE.BoxGeometry(0.07, ph, 0.07).translate(0, ph / 2, 0), gB = new THREE.BoxGeometry(0.075, 0.07, 0.075).translate(0, ph - 0.1, 0), gR = new THREE.BoxGeometry(0.08, 0.05, 0.05).translate(0, ph - 0.1, 0);
    const mats = [stdMat('#f1efe8', {r: 0.6}), stdMat('#1d1f22', {r: 0.6}), stdMat('#ff8a1e', {r: 0.3, x: {emissive: new THREE.Color('#ff7a10'), emissiveIntensity: 0.6}})];
    [gP, gB, gR].forEach((g, k) => { const im = new THREE.InstancedMesh(g, mats[k], Math.max(1, postP.length)); const M = new THREE.Matrix4(); postP.forEach(([x, z], i) => { M.makeTranslation(x, top, z); im.setMatrixAt(i, M); }); shadowAll(im); scene.add(im); });
    const yel = stdMat('#f2c12e', {r: 0.55}), blk = stdMat('#1d1f22', {r: 0.6}), pole = stdMat('#8e959c', {r: 0.4, m: 0.7});
    for (const [side, xe] of [[-1, main.x0], [1, main.x1]]){
      const x = xe + side * 9.5; if (skip(x) || I.pits.some(p => Math.abs(x - p.x0) < 3 || Math.abs(x - p.x1) < 3)) continue;
      const z = side < 0 ? D.ROAD_HW + 0.6 : -(D.ROAD_HW + 0.6), g = new THREE.Group(); g.position.set(x, top, z);
      g.add(boxAt(0.05, 1.05, 0.05, 0, 0.525, 0, pole));
      const sg = new THREE.Group(); sg.position.set(0, 1.05, 0); sg.rotation.y = (side < 0 ? -1 : 1) * Math.PI / 2 + (side < 0 ? 0.6 : -0.6);
      const dia = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.34, 0.02), yel); dia.rotation.z = Math.PI / 4; sg.add(dia);
      const rim = new THREE.Mesh(new THREE.BoxGeometry(0.38, 0.38, 0.015), blk); rim.rotation.z = Math.PI / 4; rim.position.z = -0.006; sg.add(rim);
      for (const dx of [-0.06, 0.06]) sg.add(boxAt(0.03, 0.2, 0.025, dx, 0, 0.004, blk));
      g.add(sg); shadowAll(g); scene.add(g);
    }
  }
  // ---- 地上的锚点基座
  for (const a of anchors){
    const onEdge = Math.abs(a[0] - main.x0) < 0.05 || Math.abs(a[0] - main.x1) < 0.05;
    const inPit = I.pits.some(p => a[0] > p.x0 - 0.05 && a[0] < p.x1 + 0.05 && a[1] < top - 0.05);
    if (onEdge) continue;
    const g = new THREE.BoxGeometry(0.8, 0.4, 2 * C.D.TW + 0.6); const m = new THREE.Mesh(g, concD);
    m.position.set(a[0], a[1] - 0.17 + (inPit ? 0.0 : 0.03), 0); shadowAll(m); scene.add(m);
  }
}

// ------------------------------------------------------------ 世界：水（近处细密动态，远处粗、静止，雾化融进天际）
function buildWater(C, scene){
  const I = C.T, wy = C.waterY, cx = I.cx;
  const axis = (c, near, far) => { const o = []; let d = 0; while (d < far){ o.push(d); d += d < near ? 1.15 : d < near + 40 ? 3 : d < near + 150 ? 10 : 40; } o.push(far); return o; };
  const ax = axis(0, 55, 600), xs = [...ax.slice(1).map(d => cx - d).reverse(), ...ax.map(d => cx + d)];
  const zb = axis(0, 60, 900), zf = axis(0, 45, 300), zs = [...zb.slice(1).map(d => -d).reverse(), ...zf];
  const U = [], AMP = [];
  for (let j = 0; j < zs.length; j++) for (let i = 0; i < xs.length; i++){
    const inner = i > 0 && i < xs.length - 1 && j > 0 && j < zs.length - 1;
    const sx = inner ? (xs[i + 1] - xs[i - 1]) / 2 : 0, sz = inner ? (zs[j + 1] - zs[j - 1]) / 2 : 0;
    const x = xs[i] + (inner ? (h2(i, j, 101) - 0.5) * 0.45 * sx : 0), z = zs[j] + (inner ? (h2(i, j, 102) - 0.5) * 0.45 * sz : 0);
    U.push([x, z]);
    AMP.push(1 - sstep(48, 70, Math.max(Math.abs(x - cx), z > 0 ? z * 1.2 : -z * 0.85)));
  }
  const idx = [], nx = xs.length;
  const pitHole = (qx, qz) => I.pits.some(p => qx > p.x0 - 0.3 && qx < p.x1 + 0.3 && qz > p.z0 - 0.3 && qz < I.ZF - 0.05);
  for (let j = 0; j + 1 < zs.length; j++) for (let i = 0; i + 1 < xs.length; i++){
    const a = j * nx + i, b = a + 1, c = a + nx + 1, d = a + nx;
    const qx = (U[a][0] + U[c][0]) / 2, qz = (U[a][1] + U[c][1]) / 2;
    if (pitHole(qx, qz)) continue;
    if (h2(i, j, 103) > 0.5) idx.push(a, d, b, b, d, c); else idx.push(a, d, c, a, c, b);
  }
  const pos = new Float32Array(idx.length * 3);
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.BufferAttribute(new Float32Array(idx.length * 3), 3));
  // 离岸距离（崖脚、桥墩附近是浅水，偏青绿）
  const D = C.D, main = I.main;
  const dBlock = (x, z) => {
    const zbL = I.ZB(-1, Math.max(0, main.x0 - x)), zbR = I.ZB(1, Math.max(0, x - main.x1));
    const dL = Math.hypot(Math.max(0, x - main.x0), Math.max(0, z - I.ZF, zbL - z)), dR = Math.hypot(Math.max(0, main.x1 - x), Math.max(0, z - I.ZF, zbR - z));
    let d = Math.min(dL, dR);
    for (const l of I.piers){ const xa = Math.min(...l.map(q => q[0])), xb = Math.max(...l.map(q => q[0])), yt = Math.max(...l.map(q => q[1])); if (yt < wy - 1.5) continue; d = Math.min(d, Math.hypot(Math.max(0, xa - x, x - xb), Math.max(0, Math.abs(z) - D.DW - 0.6))); }
    return d;
  };
  C.dBlock = dBlock;
  { const cc = new Float32Array(idx.length * 3), deep = col(PAL.water), shallow = col('#2b8394');
    for (let q = 0; q < idx.length; q += 3){
      const k = 0.94 + 0.12 * h2(q, 7, 909), cxq = (U[idx[q]][0] + U[idx[q + 1]][0] + U[idx[q + 2]][0]) / 3, czq = (U[idx[q]][1] + U[idx[q + 1]][1] + U[idx[q + 2]][1]) / 3;
      const sh = 1 - sstep(0.2, 3.2, dBlock(cxq, czq)), c = mixC(deep, shallow, sh * 0.75);
      for (let r = 0; r < 3; r++){ cc[3 * (q + r)] = c.r * k; cc[3 * (q + r) + 1] = c.g * k; cc[3 * (q + r) + 2] = c.b * k; }
    }
    g.setAttribute('color', new THREE.BufferAttribute(cc, 3)); }   // 每个三角面亮度略有差别：低多边形水面
  const mat = new THREE.MeshStandardMaterial({color: '#ffffff', vertexColors: true, roughness: 0.3, metalness: 0.0, flatShading: true, envMapIntensity: 1.0});   // 不透明：水下的崖壁/桥墩不透出来
  const mesh = new THREE.Mesh(g, mat); mesh.receiveShadow = true; mesh.renderOrder = 1; mesh.userData.noAO = true; scene.add(mesh);   // 水面不参与 AO（远处深度精度不够，会在天际线下压出一道暗带）
  const H = new Float32Array(U.length);
  const update = t => {
    for (let k = 0; k < U.length; k++){
      const [x, z] = U[k], a = AMP[k];
      const st = 0.06 * (h2(Math.round(x * 3), Math.round(z * 3), 7) - 0.5);
      H[k] = wy + a * (0.05 * Math.sin(0.83 * x + 0.47 * z - 1.25 * t) + 0.04 * Math.sin(-0.52 * x + 1.07 * z - 1.6 * t + 1.3)
        + 0.028 * Math.sin(1.55 * x + 0.31 * z + 1.4 * t + 0.4) + 0.03 * Math.sin(0.42 * z - 0.95 * t + 0.6 * Math.sin(0.2 * x))) + st * (0.8 * a + 4 * (1 - a));
    }
    for (let q = 0; q < idx.length; q++){ const k = idx[q]; pos[3 * q] = U[k][0]; pos[3 * q + 1] = H[k]; pos[3 * q + 2] = U[k][1]; }
    g.attributes.position.needsUpdate = true; g.computeVertexNormals(); g.computeBoundingSphere();
  };
  // 崖脚白浪：沿两岸崖壁水线的一条半透明白带，随时间起伏（礁石/桥墩露出水面的也围一圈）
  const foamLines = [];
  for (const side of [-1, 1]){
    const xe = side < 0 ? main.x0 : main.x1, line = [];
    for (let x = xe + side * 70; side < 0 ? x <= xe : x >= xe; x -= side * 0.8) line.push([x, I.ZF, 0, 1]);   // 正面（法线 +z）
    const zb = I.ZB(side, 0);
    for (let z = I.ZF; z >= zb; z -= 0.8) line.push([xe, z, -side, 0]);                           // 峡谷面（法线朝河道）
    foamLines.push(line);
  }
  for (const l of I.piers){
    const xa = Math.min(...l.map(q => q[0])), xb = Math.max(...l.map(q => q[0])), yt = Math.max(...l.map(q => q[1])); if (yt < wy - 0.6) continue;
    const cxp = (xa + xb) / 2, ra = (xb - xa) * 0.5 + 0.1, rb = D.DW + 0.7, line = [];
    for (let k = 0; k <= 40; k++){ const a = k / 40 * Math.PI * 2; line.push([cxp + ra * Math.cos(a), rb * Math.sin(a), Math.cos(a), Math.sin(a)]); }
    foamLines.push(line);
  }
  const fl = foamLines.map(line => {
    const n = line.length, pos = new Float32Array(n * 2 * 3), colA = new Float32Array(n * 2 * 4), ix = [];
    let sAcc = 0;
    line.forEach(([x, z, nx, nz], k) => {
      if (k) sAcc += Math.hypot(x - line[k - 1][0], z - line[k - 1][1]); line[k].s = sAcc;
      const wob = 0.25 * (h2(k, 3, 55) - 0.5);
      pos.set([x - nx * 0.6, wy + 0.1, z - nz * 0.6], (2 * k) * 3); pos.set([x + nx * (1.0 + wob), wy + 0.09, z + nz * (1.0 + wob)], (2 * k + 1) * 3);
      colA.set([0.93, 0.97, 0.97, 0.6], (2 * k) * 4); colA.set([0.93, 0.97, 0.97, 0], (2 * k + 1) * 4);
      if (k + 1 < n) ix.push(2 * k, 2 * k + 1, 2 * k + 2, 2 * k + 1, 2 * k + 3, 2 * k + 2);
    });
    const gg = new THREE.BufferGeometry(); gg.setAttribute('position', new THREE.BufferAttribute(pos, 3)); gg.setAttribute('color', new THREE.BufferAttribute(colA, 4)); gg.setIndex(ix);
    const m = new THREE.Mesh(gg, new THREE.MeshBasicMaterial({vertexColors: true, transparent: true, depthWrite: false, side: THREE.DoubleSide, fog: true}));
    m.userData.noAO = true; m.renderOrder = 2; scene.add(m);
    return {line, colA, gg};
  });
  const update2 = t => {
    update(t);
    for (const f of fl){ f.line.forEach((q, k) => { const a = 0.38 + 0.2 * Math.sin(q.s * 1.3 - t * 1.4) + 0.12 * Math.sin(q.s * 3.7 + t * 2.3 + 1.1); f.colA[(2 * k) * 4 + 3] = clamp(a, 0.08, 0.75); }); f.gg.attributes.color.needsUpdate = true; }
  };
  update2(0);
  return {mesh, update: update2};
}

// ------------------------------------------------------------ 世界：少量点缀（石块、矮灌木、几棵树、台地后方的岩丘、远处水中岩柱）
function propGeos(){
  const trunkC = col('#6b4b30');
  const pine = (() => {
    const parts = [];
    const tr = new THREE.CylinderGeometry(0.07, 0.11, 0.8, 5); tr.translate(0, 0.4, 0); parts.push(colorize(tr, () => trunkC));
    [[0.75, 1.3, 0.6], [0.56, 1.1, 1.25], [0.36, 0.9, 1.8]].forEach(([r, h, y], k) => { const c = new THREE.ConeGeometry(r, h, 7, 1); c.translate(0, y + h / 2, 0); const cc = col(['#4c7d3e', '#558944', '#5f944a'][k]); parts.push(colorize(c, (cy, ny, f) => jit(cc, h2(f, k, 7), 0.07))); });
    return mergeGeometries(parts.map(p => p.index ? p.toNonIndexed() : p));
  })();
  const broad = (() => {
    const parts = [];
    const tr = new THREE.CylinderGeometry(0.07, 0.12, 1.0, 5); tr.translate(0, 0.5, 0); parts.push(colorize(tr, () => trunkC));
    for (const [r, x, y, z] of [[0.8, 0, 1.5, 0], [0.55, 0.4, 1.3, 0.2], [0.5, -0.35, 1.35, -0.18]]){
      const b = new THREE.IcosahedronGeometry(r, 0); displace(b, (x0, y0, z0) => { const n = h2(Math.round(x0 * 50), Math.round(y0 * 50), Math.round(z0 * 50)); return [x0 * 0.12 * (n - 0.5), y0 * 0.1 * (n - 0.5), z0 * 0.12 * (n - 0.5)]; });
      b.scale(1, 0.85, 1); b.translate(x, y, z);
      parts.push(colorize(b, (cy, ny, f) => jit(ny > 0.3 ? col('#6f9a45') : col('#5d863b'), h2(f, 3, 9), 0.08)));
    }
    return mergeGeometries(parts.map(p => p.index ? p.toNonIndexed() : p));
  })();
  const bush = (() => { const b = new THREE.IcosahedronGeometry(0.42, 0); b.scale(1.2, 0.7, 1); b.translate(0, 0.2, 0); return colorize(b, (cy, ny, f) => jit(col('#6b9142'), h2(f, 1, 2), 0.1)); })();
  const boulder = (() => { const b = new THREE.IcosahedronGeometry(1, 0); displace(b, (x, y, z) => { const n = h2(Math.round(x * 97), Math.round(y * 97), Math.round(z * 97)); return [x * 0.25 * (n - 0.5), y * 0.2 * (n - 0.5), z * 0.25 * (n - 0.5)]; }); b.scale(1, 0.7, 1); return colorize(b, (cy, ny, f) => jit(col(ny > 0.5 ? '#d9b48a' : '#bf9467'), h2(f, 2, 3), 0.08)); })();
  const tuft = (() => { const parts = []; for (let q = 0; q < 5; q++){ const b = new THREE.ConeGeometry(0.035, 0.26 + 0.07 * (q % 3), 3, 1); b.translate(0, 0.13, 0); b.rotateZ((q - 2) * 0.22); b.rotateY(q * 1.3); b.translate((q - 2) * 0.045, 0, ((q * 7) % 5 - 2) * 0.04); parts.push(colorize(b, (cy, ny, f) => jit(col(q % 2 ? '#7e9f42' : '#93b34f'), h2(f, q, 4), 0.06))); } return mergeGeometries(parts); })();
  return {pine, broad, bush, boulder, tuft};
}
// 分层砂岩岩丘（棱面）
function buttGeo(r, h, seed, flatTop = true){
  const rings = Math.max(3, Math.round(h / 1.4));
  const g = new THREE.CylinderGeometry(r * 0.78, r * 1.18, h, 8, rings);
  displace(g, (x0, y1, z0) => { const a = Math.atan2(z0, x0), ring = Math.round((y1 + h / 2) / (h / rings)); const n = fbm(a * 1.1 + seed * 3, ring * 0.7, 500 + seed, 2); const k = 0.3 * (n - 0.5) + 0.1 * (h2(ring, seed, 7) - 0.5) - (y1 > h / 2 - 0.01 && !flatTop ? 0.3 : 0); return [x0 * k, (h2(Math.round(a * 9), ring, seed) - 0.5) * 0.3 * (y1 > h / 2 - 0.01 ? 0.3 : 1), z0 * k]; });
  return colorize(g, (cy, ny, f) => { const band = Math.floor((cy + h / 2) / 1.2 + seed); let cc = col(PAL.rock[band % PAL.rock.length]); if (ny > 0.6) cc = mixC(col(PAL.grass[1]), cc, 0.25); else if (ny > 0.35) cc = mixC(cc, col('#e2b98a'), 0.5); return jit(cc, h2(f, seed, 4), 0.07); });
}
function buildProps(C, scene){
  const I = C.T, D = C.D, main = I.main, R = mulberry(4242 + Math.round(main.x1 * 10));
  const G = propGeos(), mat = VCMAT();
  const lists = {pine: [], broad: [], bush: [], boulder: [], tuft: []};
  const fw = C.frameW || I.span * 1.45;
  const ok = (x, z, margin) => {
    if (Math.abs(z) < D.SHOULDER + 0.5 + margin) return false;
    if (x > main.x0 - 1.2 - margin && x < main.x1 + 1.2 + margin) return false;
    const side = x < main.x0 ? -1 : 1, d = side < 0 ? main.x0 - x : x - main.x1;
    if (z > I.ZF - 0.7 - margin || z < I.ZB(side, d) + 0.9 + margin) return false;
    for (const p of I.pits){ if (x > p.x0 - 1.0 - margin && x < p.x1 + 1.0 + margin && z > p.z0 - 1.0 - margin) return false; }
    if (C.R.lights) for (const l of C.R.lights) if (Math.abs(x - l.x) < 1.2 && z > 0) return false;
    return true;
  };
  // 树：只在台地后半、离桥头 2.5 米以外，稀疏几棵
  for (let n = 0; n < 400 && lists.pine.length + lists.broad.length < 14; n++){
    const side = (lists.pine.length + lists.broad.length) % 2 ? 1 : -1, d = 4.5 + R() * fw * 0.6, x = side < 0 ? main.x0 - d : main.x1 + d;
    const z = -D.SHOULDER - 1.6 - R() * 9;
    if (!ok(x, z, 0.4)) continue;
    if (fbm(x * 0.12, z * 0.12, 300, 2) < 0.5) continue;
    lists[R() < 0.55 ? 'pine' : 'broad'].push([x, I.top - 0.05, z, 0.75 + R() * 0.5, R() * 6.28]);
  }
  for (let n = 0; n < 2400; n++){
    const side = R() < 0.5 ? -1 : 1, d = 0.5 + R() * fw * 0.8, x = side < 0 ? main.x0 - d : main.x1 + d;
    const z = lerp(I.ZF - 0.4, -D.DW - 15, R()), r = R();
    if (!ok(x, z, 0)) continue;
    if (r < 0.035) lists.boulder.push([x, I.top - 0.08, z, 0.18 + R() * 0.45, R() * 6.28]);
    else if (r < 0.09) lists.bush.push([x, I.top - 0.04, z, 0.6 + R() * 0.7, R() * 6.28]);
    else if (r < 0.5 && fbm(x * 0.2, z * 0.2, 808, 2) > 0.46) lists.tuft.push([x, I.top - 0.02, z, 0.8 + R() * 0.6, R() * 6.28]);
  }
  // 台地边缘一圈草丛和碎石（读出"草皮唇"）
  for (let n = 0; n < 500; n++){
    const side = R() < 0.5 ? -1 : 1, onGap = R() < 0.4, xe = side < 0 ? main.x0 : main.x1;
    const x = onGap ? xe + side * (0.25 + R() * 0.6) : xe + side * (1.2 + R() * fw * 0.7), z = onGap ? lerp(I.ZF - 0.3, D.SHOULDER + 0.6, R()) * (R() < 0.5 ? 1 : -1) : I.ZF - 0.25 - R() * 0.7;
    if (Math.abs(z) < D.SHOULDER + 0.3 || I.pits.some(p => x > p.x0 - 0.6 && x < p.x1 + 0.6)) continue;
    if (onGap && z < I.ZB(side, 0) + 0.5) continue;
    if (R() < 0.8) lists.tuft.push([x, I.top - 0.02, z, 0.7 + R() * 0.7, R() * 6.28]); else lists.boulder.push([x, I.top - 0.05, z, 0.08 + R() * 0.14, R() * 6.28]);
  }
  // 水边落石（崖脚）
  for (let n = 0; n < 90; n++){
    const side = R() < 0.5 ? -1 : 1, onGap = R() < 0.45, xe = side < 0 ? main.x0 : main.x1;
    let x, z;
    if (onGap){ x = xe - side * (0.35 + R() * 0.9); z = lerp(I.ZF, I.ZB(side, 0) + 1, Math.pow(R(), 0.7)); if (Math.abs(z) < C.D.DW + 1.2) continue; }
    else { x = side < 0 ? xe - 0.5 - R() * fw * 0.7 : xe + 0.5 + R() * fw * 0.7; z = I.ZF + 0.35 + R() * 0.9; }
    if (I.pits.some(p => x > p.x0 - 1 && x < p.x1 + 1)) continue;
    const big = R() < 0.25;
    lists.boulder.push([x, C.waterY - 0.15 + R() * 0.1, z, big ? 0.5 + R() * 0.6 : 0.15 + R() * 0.35, R() * 6.28, 0.78]);
  }
  const M4 = new THREE.Matrix4(), Q = new THREE.Quaternion(), S = new THREE.Vector3(), P = new THREE.Vector3(), E = new THREE.Euler();
  for (const k of Object.keys(lists)){
    const L = lists[k]; if (!L.length) continue;
    const im = new THREE.InstancedMesh(G[k], mat, L.length);
    L.forEach(([x, y, z, s, rot, tint], i) => {
      E.set(k === 'boulder' ? (h2(i, 1, 5) - 0.5) * 0.6 : 0, rot, k === 'boulder' ? (h2(i, 2, 5) - 0.5) * 0.6 : 0); Q.setFromEuler(E);
      const sy = k === 'pine' ? s * (1.0 + h2(i, 3, 5) * 0.5) : s;
      M4.compose(P.set(x, y, z), Q, S.set(s, sy, s)); im.setMatrixAt(i, M4);
      im.setColorAt(i, new THREE.Color(1, 1, 1).multiplyScalar((0.92 + 0.16 * h2(i, 4, 5)) * (tint ?? 1)));
    });
    im.castShadow = k !== 'tuft'; im.receiveShadow = true; scene.add(im);
  }
  // 台地后方两座岩丘（画面上角的"书挡"），远离桥头
  const ybot = Math.min(I.floor, C.waterY) - 3;
  const butt = (x, z, r, h, seed) => { const g = buttGeo(r, h, seed); const m = new THREE.Mesh(g, mat); m.position.set(x, I.top + h / 2 - 0.3, z); m.castShadow = m.receiveShadow = true; scene.add(m); };
  const bL = C.cfg.buttes ?? true;
  if (bL){
    butt(main.x0 - Math.max(7, fw * 0.36), I.ZB(-1, fw * 0.36) + 4.2, 2.6, 6.5, 3);
    butt(main.x1 + Math.max(8, fw * 0.42), I.ZB(1, fw * 0.42) + 4.6, 3.0, 8.5, 7);
  }
}

// ------------------------------------------------------------ 桥
function unitBox(sy, sz){ const g = new THREE.BoxGeometry(1, sy, sz); g.translate(0.5, 0, 0); return g; }
function memberFactory(D){
  const wood = stdMat(PAL.wood, {r: 0.78});
  const steel = stdMat(PAL.steel, {r: 0.42, m: 0.25}), cable = stdMat(PAL.cable, {r: 0.35, m: 0.8}), spring = stdMat(PAL.spring, {r: 0.35, m: 0.3});
  const hydM = stdMat(PAL.hyd, {r: 0.35, m: 0.25}), chrome = stdMat(PAL.chrome, {r: 0.22, m: 0.85}), dark = stdMat('#2c2f33', {r: 0.5, m: 0.5});
  const asph = stdMat(PAL.deckTop, {r: 0.9}), side = stdMat(PAL.deckSide, {r: 0.85}), under = stdMat('#8d887f', {r: 0.95});
  const laneM = stdMat(PAL.lane, {r: 0.7, x: {polygonOffset: true, polygonOffsetFactor: -2, polygonOffsetUnits: -2}});
  const girder = stdMat(PAL.deckBeam, {r: 0.5, m: 0.4});
  const T = 0.2;  // 杆件截面
  const deckT = 0.2, gap = 0.035;
  const G = {
    wood: unitBox(T, T),
    steel: (() => { const a = unitBox(0.04, T); a.translate(0, T / 2 - 0.02, 0); const b = unitBox(0.04, T); b.translate(0, -T / 2 + 0.02, 0); const c = unitBox(T - 0.06, 0.05); return mergeGeometries([a, b, c]); })(),
    cable: (() => { const g = new THREE.CylinderGeometry(0.035, 0.035, 1, 6); g.rotateZ(-Math.PI / 2); g.translate(0.5, 0, 0); return g; })(),
    spring: (() => {
      class Helix extends THREE.Curve { getPoint(s, tgt = new THREE.Vector3()){ const u = clamp((s - 0.1) / 0.8, 0, 1), a = u * Math.PI * 2 * 7; const r = s < 0.1 || s > 0.9 ? 0 : 0.13; return tgt.set(s, r * Math.cos(a), r * Math.sin(a)); } }
      const coil = new THREE.TubeGeometry(new Helix(), 220, 0.032, 5, false);
      const p1 = new THREE.CylinderGeometry(0.16, 0.16, 0.035, 10); p1.rotateZ(-Math.PI / 2); p1.translate(0.1, 0, 0);
      const p2 = p1.clone(); p2.translate(0.8, 0, 0);
      return mergeGeometries([coil.toNonIndexed(), p1.toNonIndexed(), p2.toNonIndexed()]);
    })(),
    hydBarrel: (() => { const g = new THREE.CylinderGeometry(0.14, 0.14, 1, 12); g.rotateZ(-Math.PI / 2); g.translate(0.5, 0, 0); return g; })(),
    hydRod: (() => { const g = new THREE.CylinderGeometry(0.055, 0.055, 1, 10); g.rotateZ(-Math.PI / 2); g.translate(0.5, 0, 0); return g; })(),
    hydCap: (() => { const g = new THREE.CylinderGeometry(0.16, 0.16, 0.08, 12); g.rotateZ(-Math.PI / 2); return g; })(),
    hydBand: (() => { const g = new THREE.CylinderGeometry(0.152, 0.152, 0.05, 12); g.rotateZ(-Math.PI / 2); return g; })(),
    hydPort: new THREE.BoxGeometry(0.08, 0.06, 0.08),
    lug: new THREE.BoxGeometry(0.2, 0.15, 0.11),
    deck: new THREE.BoxGeometry(1, deckT, 2 * D.DW).translate(0.5, -deckT / 2, 0),
    dash: (() => { const g = new THREE.PlaneGeometry(0.42, Math.max(0.05, D.DW * 0.045)); g.rotateX(-Math.PI / 2); g.translate(0.5, 0.003, 0); return g; })(),
    edge: (() => { const g = new THREE.PlaneGeometry(0.98, Math.max(0.05, D.DW * 0.045)); g.rotateX(-Math.PI / 2); g.translate(0.5, 0.003, 0); return g; })(),
    girder: unitBox(0.22, 0.12),
    curb: unitBox(0.07, 0.08),
  };
  const deckMats = [side, side, asph, under, side, side];
  const simple = (geo, mat) => { const m = new THREE.Mesh(geo, mat); const o = new THREE.Group(); o.add(m); o.userData.setLen = L => { m.scale.x = Math.max(0.01, L); }; return shadowAll(o); };
  return {
    mats: {wood, steel, cable, spring, hydM, chrome},
    make(kind, L0, stub, seed = 0){
      // 每根木杆/钢杆颜色略有差别（同一根的两片桁架、断开的两截颜色一致）
      if (kind === 1){ const m = wood.clone(); m.color.multiplyScalar(0.92 + 0.14 * h2(seed, 1, 17)); m.color.offsetHSL((h2(seed, 2, 17) - 0.5) * 0.02, 0, 0); return simple(G.wood, m); }
      if (kind === 2){ const m = steel.clone(); m.color.multiplyScalar(0.95 + 0.08 * h2(seed, 3, 17)); return simple(G.steel, m); }
      if (kind === 3) return simple(G.cable, cable);
      if (kind === 4) return simple(G.spring, spring);
      if (kind === 5){
        if (stub) return simple(G.hydBarrel, hydM);
        const o = new THREE.Group(), barrel = new THREE.Mesh(G.hydBarrel, hydM), rod = new THREE.Mesh(G.hydRod, chrome), cap = new THREE.Mesh(G.hydCap, dark), l1 = new THREE.Mesh(G.lug, dark), l2 = new THREE.Mesh(G.lug, dark);
        const b1 = new THREE.Mesh(G.hydBand, dark), b2 = new THREE.Mesh(G.hydBand, dark), port = new THREE.Mesh(G.hydPort, dark);
        o.add(barrel, rod, cap, l1, l2, b1, b2, port);
        const bl0 = 0.5 * L0;
        o.userData.setLen = L => { const bl = Math.max(0.2, Math.min(bl0, L - 0.25)); barrel.scale.x = bl; cap.position.x = bl; rod.position.x = bl; rod.scale.x = Math.max(0.01, L - bl - 0.08); l2.position.x = L - 0.06; l1.position.x = 0.06; b1.position.x = 0.14; b2.position.x = bl - 0.1; port.position.set(bl * 0.35, 0.14, 0); };
        return shadowAll(o);
      }
      if (kind === 0){
        // 桥面板：一块一块的板（两端留缝），沥青面 + 浅色侧边 + 板下两道纵梁
        const o = new THREE.Group(), inner = new THREE.Group(); o.add(inner);
        const slab = new THREE.Mesh(G.deck, deckMats), g1 = new THREE.Mesh(G.girder, girder), g2 = new THREE.Mesh(G.girder, girder);
        g1.position.set(0, -deckT - 0.11, D.DW - 0.35); g2.position.set(0, -deckT - 0.11, -(D.DW - 0.35));
        const parts = [slab, g1, g2];
        if (!stub){ const d = new THREE.Mesh(G.dash, laneM); const e1 = new THREE.Mesh(G.edge, laneM), e2 = new THREE.Mesh(G.edge, laneM); e1.position.z = D.DW - 0.14; e2.position.z = -(D.DW - 0.14); parts.push(d, e1, e2); }
        for (const sz of [1, -1]){ const c = new THREE.Mesh(G.curb, side); c.position.set(0, 0.035, sz * (D.DW - 0.04)); parts.splice(3, 0, c); }   // 板边路缘
        inner.add(...parts);
        o.userData.setLen = L => { const g = Math.min(gap, L * 0.1); inner.position.x = g / 2; for (const p of parts) p.scale.x = Math.max(0.01, L - g); };
        shadowAll(o); for (const p of parts.slice(3)) p.castShadow = false;
        return o;
      }
      return simple(G.steel, steel);
    },
  };
}
function buildBridge(C, scene){
  const R = C.R, NB = C.NB, F0 = C.F[0].p, D = C.D, fac = memberFactory(D);
  const TW = D.TW;
  const grp = new THREE.Group(); scene.add(grp);
  const items = [];
  const deckNodes = new Set(), allNodes = new Set();
  for (let i = 0; i < NB; i++){
    const m = R.members[i], kind = m[2];
    for (const k of [m[0], m[1]]){ allNodes.add(k); if (kind === 0) deckNodes.add(k); }
    const zs = kind === 0 ? [0] : [TW, -TW];
    const sides = zs.map(z => { const full = fac.make(kind, C.L0[i], false, i), sa = fac.make(kind, C.L0[i], true, i), sb = fac.make(kind, C.L0[i], true, i); grp.add(full, sa, sb); return {z, full, sa, sb}; });
    items.push({i, m, kind, sides});
  }
  // 节点：圆形连接件（贯穿杆件、两面各露一个圆帽）
  const nodeG = (() => {
    const L = 0.2 + 0.11;
    const body = new THREE.CylinderGeometry(0.15, 0.15, L, 14); body.rotateX(Math.PI / 2);
    const cap1 = new THREE.CylinderGeometry(0.085, 0.11, 0.05, 12); cap1.rotateX(Math.PI / 2); cap1.translate(0, 0, L / 2 + 0.02);
    const cap2 = cap1.clone(); cap2.rotateY(Math.PI);
    const parts = [body, cap1, cap2].map(g => g.toNonIndexed()); parts.forEach((g, k) => { const n = g.attributes.position.count, c = new Float32Array(n * 3), cc = k ? [0.42, 0.45, 0.5] : [0.2, 0.22, 0.25]; for (let q = 0; q < n; q++) c.set(cc, q * 3); g.setAttribute('color', new THREE.BufferAttribute(c, 3)); });
    return mergeGeometries(parts);   // 本体深色、两面圆帽浅一档的钢灰
  })();
  const nodes = [...allNodes].filter(k => k >= R.n_anchor && R.particles[k].o === -1);
  const anchorsUsed = [...allNodes].filter(k => k < R.n_anchor);
  const jointMat = stdMat('#ffffff', {r: 0.36, m: 0.6, flat: false, x: {vertexColors: true}});
  const gusIM = new THREE.InstancedMesh(nodeG, jointMat, (nodes.length + anchorsUsed.length) * 2); shadowAll(gusIM); grp.add(gusIM);
  // 横梁：桥面节点在板下；非桥面节点在车辆净空外才加
  const deckYAt0 = x => { let best = null; for (let i = 0; i < NB; i++){ const m = R.members[i]; if (m[2] !== 0) continue; const ax = F0[2 * m[0]], bx = F0[2 * m[1]], ay = F0[2 * m[0] + 1], by = F0[2 * m[1] + 1]; if (x >= Math.min(ax, bx) - 0.05 && x <= Math.max(ax, bx) + 0.05){ const y = ax === bx ? Math.max(ay, by) : ay + (by - ay) * (x - ax) / (bx - ax); if (best == null || y > best) best = y; } } return best; };
  const strutNodes = nodes.filter(k => { if (deckNodes.has(k)) return false; const x = F0[2 * k], y = F0[2 * k + 1], d = deckYAt0(x); return !(d != null && y - d > -0.3 && y - d < D.CLEAR); });
  const beamG = new THREE.BoxGeometry(0.16, 0.2, 2 * TW + 0.1), strutG = new THREE.CylinderGeometry(0.06, 0.06, 2 * TW, 6); strutG.rotateX(Math.PI / 2);
  const beamIM = new THREE.InstancedMesh(beamG, stdMat(PAL.deckBeam, {r: 0.5, m: 0.4}), Math.max(1, [...deckNodes].length)); shadowAll(beamIM); grp.add(beamIM);
  const strutIM = new THREE.InstancedMesh(strutG, stdMat('#4a525c', {r: 0.45, m: 0.5}), Math.max(1, strutNodes.length)); shadowAll(strutIM); grp.add(strutIM);
  // 锚点支座（静止）
  const brk = stdMat('#3d434a', {r: 0.5, m: 0.6});
  const I = C.T;
  for (const k of anchorsUsed){
    const x = F0[2 * k], y = F0[2 * k + 1];
    const onWall = (Math.abs(x - I.main.x0) < 0.05 || Math.abs(x - I.main.x1) < 0.05) && y < I.top - 0.05;
    const inward = onWall ? (Math.abs(x - I.main.x0) < 0.05 ? [-1, 0] : [1, 0]) : [0, -1];
    for (const z of [TW, -TW]){
      const b = boxAt(0.36, 0.36, 0.3, x + inward[0] * 0.14, y + inward[1] * 0.14, z, brk); shadowAll(b); grp.add(b);
    }
  }
  const M4 = new THREE.Matrix4(), Q = new THREE.Quaternion(), S1 = new THREE.Vector3(1, 1, 1), V = new THREE.Vector3(), Zax = new THREE.Vector3(0, 0, 1);
  const place = (o, ax, ay, bx, by, z, L) => { o.visible = true; o.position.set(ax, ay, z); o.rotation.set(0, 0, Math.atan2(by - ay, bx - ax)); o.userData.setLen(L); };
  const analysis = !!C.cfg.analysis;
  // 受力标记：最吃力的那根杆件本身微微发光（自发光颜色随受力变暖），换杆时旧的淡出、新的淡入
  const hlOn = C.cfg.hud !== false && !analysis;
  if (hlOn) for (const it of items){ it.mats = []; for (const sd of it.sides) for (const o of [sd.full, sd.sa, sd.sb]) o.traverse(m => { if (!m.isMesh) return; m.material = Array.isArray(m.material) ? m.material.map(q => q.clone()) : m.material.clone(); (Array.isArray(m.material) ? m.material : [m.material]).forEach(q => { if (q.emissive) it.mats.push(q); }); }); }
  const hlSet = (i, w, rgb) => { const it = items[i]; if (!it || !it.mats) return; for (const q of it.mats){ q.emissive.setRGB(rgb[0], rgb[1], rgb[2]); q.emissiveIntensity = w; } };
  let hlPrev = [];
  if (analysis) for (const it of items) for (const s of it.sides) for (const o of [s.full]) o.traverse(m => { if (m.isMesh && !Array.isArray(m.material)){ m.material = m.material.clone(); m.userData.base = col('#c9cdd2'); m.material.color.copy(m.userData.base); } });   // 受力上色：杆件先统一成浅灰，红=拉、蓝=压
  const hot = col('#ff2a1a'), cold = col('#2a6bff');
  // 近侧桁架半透明（11-07 冷开场，近景看清桥里的车）：cfg.cutaway = [{t: 模拟秒, a: 不透明度}, ...]，按时间平滑插值
  const cutMats = [], cutObjs = [];
  if (C.cfg.cutaway) for (const it of items) for (const sd of it.sides) if (sd.z > 0) for (const o of [sd.full, sd.sa, sd.sb]) cutObjs.push(o);
  if (C.cfg.cutaway) for (const it of items) for (const sd of it.sides) if (sd.z > 0) for (const o of [sd.full, sd.sa, sd.sb]) o.traverse(m => { if (!m.isMesh) return; m.material = Array.isArray(m.material) ? m.material.map(q => q.clone()) : m.material.clone(); for (const q of (Array.isArray(m.material) ? m.material : [m.material])){ q.transparent = true; cutMats.push(q); } });
  const cutA = t => { const K = C.cfg.cutaway; if (!K) return 1; if (t <= K[0].t) return K[0].a; for (let q = 0; q + 1 < K.length; q++) if (t < K[q + 1].t) return lerp(K[q].a, K[q + 1].a, sstep(K[q].t, K[q + 1].t, t)); return K[K.length - 1].a; };
  return {
    update(P, fr, t, vt = 1e9){
      if (cutMats.length){ const a = cutA(t); for (const q of cutMats){ q.opacity = a; q.depthWrite = a > 0.98; } }
      if (hlOn && (!C.bt || vt >= C.pre)){
        for (const i of hlPrev) hlSet(i, 0, [0, 0, 0]); hlPrev = [];
        const f = Math.floor(frameF(C, t) + 1e-9), cur = C.mark[f], v = stressAt(C, t), rgb = heatColor(v);
        let sw = null; for (const q of C.markSw){ if (q.t <= t + 1e-9) sw = q; else break; }
        const pulse = v > 92 ? 0.82 + 0.18 * Math.sin(t * 16) : 1, base = (0.12 + 0.75 * sstep(35, 100, v)) * sstep(12, 30, v) * pulse;
        let u = 1; if (sw && sw.from >= 0 && t - sw.t < 0.35 && fr.s[sw.from] !== 999){ u = sstep(0, 1, (t - sw.t) / 0.35); hlSet(sw.from, base * (1 - u), rgb); hlPrev.push(sw.from); }
        if (cur >= 0 && fr.s[cur] !== 999){ hlSet(cur, base * u, rgb); hlPrev.push(cur); }
      }
      for (const it of items){
        const m = it.m; let ax = P[2 * m[0]], ay = P[2 * m[0] + 1], bx = P[2 * m[1]], by = P[2 * m[1] + 1];
        if (it.kind === 0 && bx < ax){ [ax, bx] = [bx, ax]; [ay, by] = [by, ay]; }   // 桥面板始终从左往右摆，避免翻面
        const L = Math.hypot(bx - ax, by - ay) || 1e-3;
        const broken = fr.s[it.i] === 999;
        const gk = buildK(C, it.i, vt);
        if (gk < 1){                                   // 搭桥中：从一端长出来，带一点下落和回弹
          for (const s of it.sides){ s.sa.visible = s.sb.visible = false; }
          if (gk <= 0){ for (const s of it.sides) s.full.visible = false; continue; }
          const e = 1 - Math.pow(1 - gk, 3), drop = 1.6 * Math.pow(1 - gk, 2);
          const L2 = Math.max(1e-3, L * e);
          for (const s of it.sides) place(s.full, ax, ay + drop, ax + (bx - ax) * e, ay + drop + (by - ay) * e, s.z, L2);
          continue;
        }
        for (const s of it.sides){
          if (!broken){ place(s.full, ax, ay, bx, by, s.z, L); s.sa.visible = s.sb.visible = false;
            if (analysis){ const st = fr.s[it.i]; s.full.traverse(o => { if (o.isMesh && o.userData.base){ const a = Math.abs(st); o.material.color.copy(o.userData.base).lerp(st > 0 ? hot : cold, sstep(30, 100, a)); } }); }
          } else {
            if (C.cfg.breakGlow && C.breakT[it.i] != null){ const bg = C.cfg.breakGlow, u = (t - C.breakT[it.i]) / (bg.dur ?? 0.3); const w = u >= 0 && u < 1 ? (1 - u) * (bg.k ?? 3) : 0; for (const o of [s.sa, s.sb]) o.traverse(m => { if (m.isMesh && m.material && m.material.emissive){ if (!m.userData.bgc){ m.material = m.material.clone(); m.userData.bgc = 1; } m.material.emissive.set(bg.color || '#ffd040'); m.material.emissiveIntensity = w; } }); }
            s.full.visible = false;
            const f = Math.min(0.42, 0.36 * C.L0[it.i] / L) * L;
            place(s.sa, ax, ay, ax + (bx - ax) * f / L, ay + (by - ay) * f / L, s.z, f);
            place(s.sb, bx - (bx - ax) * f / L, by - (by - ay) * f / L, bx, by, s.z, f);
          }
        }
      }
      if (cutObjs.length){ const a = cutA(t); for (const o of cutObjs){ if (a < 0.02) o.visible = false; o.traverse(m => { if (m.isMesh) m.castShadow = a > 0.98; }); } }
      let n = 0;
      const nodeK = k => { if (!C.bt) return 1; let g = 0; for (const it of items){ const m = it.m; if (m[0] === k || m[1] === k) g = Math.max(g, buildK(C, it.i, vt)); } return g; };
      const S0 = new THREE.Vector3();
      for (const k of [...nodes, ...anchorsUsed]){
        const g = nodeK(k), sc = g >= 1 ? 1 : sstep(0, 0.6, g) * (1 + 0.25 * Math.sin(Math.PI * clamp(g, 0, 1)));
        S0.set(sc, sc, sc);
        const hideNear = cutMats.length && cutA(t) < 0.3;
        for (const sz of [1, -1]){ V.set(P[2 * k], P[2 * k + 1], sz * TW); M4.compose(V, Q.identity(), hideNear && sz > 0 ? S0.clone().setScalar(0) : S0); gusIM.setMatrixAt(n++, M4); }
      }
      gusIM.instanceMatrix.needsUpdate = true;
      n = 0;
      for (const k of deckNodes){
        if (k < R.n_anchor){ M4.makeScale(0, 0, 0); beamIM.setMatrixAt(n++, M4); continue; }
        let ang = 0, c = 0; for (const it of items){ if (it.kind !== 0 || fr.s[it.i] === 999) continue; const m = it.m; if (m[0] === k || m[1] === k){ let a = Math.atan2(P[2 * m[1] + 1] - P[2 * m[0] + 1], P[2 * m[1]] - P[2 * m[0]]); if (a > Math.PI / 2) a -= Math.PI; if (a < -Math.PI / 2) a += Math.PI; ang += a; c++; } }
        ang = c ? ang / c : 0; Q.setFromAxisAngle(Zax, ang);
        const gb = nodeK(k); S0.set(1, gb >= 1 ? 1 : sstep(0.3, 1, gb), gb >= 1 ? 1 : sstep(0.3, 1, gb));
        V.set(P[2 * k] + Math.sin(ang) * 0.3, P[2 * k + 1] - Math.cos(ang) * 0.3, 0); M4.compose(V, Q, S0); beamIM.setMatrixAt(n++, M4);
      }
      beamIM.instanceMatrix.needsUpdate = true;
      n = 0;
      for (const k of strutNodes){ const gs = nodeK(k); S0.set(gs >= 1 ? 1 : sstep(0.4, 1, gs), gs >= 1 ? 1 : sstep(0.4, 1, gs), gs >= 1 ? 1 : sstep(0.4, 1, gs)); V.set(P[2 * k], P[2 * k + 1], 0); M4.compose(V, Q.identity(), S0); strutIM.setMatrixAt(n++, M4); }
      strutIM.instanceMatrix.needsUpdate = true;
    },
  };
}

// ------------------------------------------------------------ 车辆（低多边形，无五官；尺寸取自物理：轴距、轮径、车身高）
const VCOL = {car: ['#c23b30', '#e8e4da', '#2f5f9e', '#f0b532'], van: ['#eceae3', '#7fb2c9'], truck: ['#2a5d8a', '#c4482a']};
function archPts(cx, r, yb, n = 7){ const a = Math.asin(clamp(-yb / r, -1, 1)), p0 = Math.PI + a, p1 = -a, o = []; for (let k = 0; k <= n; k++){ const ph = p0 + (p1 - p0) * k / n; o.push([cx + r * Math.cos(ph), r * Math.sin(ph)]); } return o; }
function extrudeProfile(pts, w, mat){ const sh = new THREE.Shape(pts.map(p => new THREE.Vector2(p[0], p[1]))); const g = new THREE.ExtrudeGeometry(sh, {depth: w, bevelEnabled: false}); g.translate(0, 0, -w / 2); return new THREE.Mesh(g, mat); }
function flatPoly(pts, z, mat){ const sh = new THREE.Shape(pts.map(p => new THREE.Vector2(p[0], p[1]))); const g = new THREE.ShapeGeometry(sh); g.translate(0, 0, z); if (z < 0) g.scale(1, 1, 1); return new THREE.Mesh(g, mat); }
function quadMesh(a, b, c, d, mat){ const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.Float32BufferAttribute([...a, ...b, ...c, ...a, ...c, ...d], 3)); g.computeVertexNormals(); return new THREE.Mesh(g, mat); }
function glassOnEdge(p, q, hw, mat, inset){
  const dx = q[0] - p[0], dy = q[1] - p[1], L = Math.hypot(dx, dy), ux = dx / L, uy = dy / L, nx = uy, ny = -ux, o = 0.006;
  const a = [p[0] + ux * inset + nx * o, p[1] + uy * inset + ny * o], b = [q[0] - ux * inset + nx * o, q[1] - uy * inset + ny * o];
  return quadMesh([a[0], a[1], -hw], [b[0], b[1], -hw], [b[0], b[1], hw], [a[0], a[1], hw], mat);
}
function wheelGeo(r, w){
  const tire = new THREE.CylinderGeometry(r, r, w, 16); tire.rotateX(Math.PI / 2);
  const rim = new THREE.CylinderGeometry(r * 0.6, r * 0.6, w + 0.012, 8); rim.rotateX(Math.PI / 2);
  const hub = new THREE.CylinderGeometry(r * 0.16, r * 0.16, w + 0.03, 6); hub.rotateX(Math.PI / 2);
  const spokes = []; for (let q = 0; q < 5; q++){ const s = new THREE.BoxGeometry(r * 0.5, r * 0.1, w + 0.02); s.translate(r * 0.25, 0, 0); s.rotateZ(q * Math.PI * 2 / 5); spokes.push(s.toNonIndexed()); }
  return {tire, rim, hub, spokes: mergeGeometries(spokes)};
}
function buildVehicleModel(v){
  const tp = v.tp, cset = VCOL[tp] || VCOL.car, paintHex = v.paint || cset[v.nth % cset.length];   // 11-08 甲方3D：paint 指定车漆
  const {wb, r, H, W} = v.dims, ax = wb / 2, hw = W / 2;
  const paint = stdMat(paintHex, {r: 0.36, m: 0.3}), glass = stdMat('#1b2633', {r: 0.06, m: 0.4, side: THREE.DoubleSide});
  const trim = stdMat('#25272b', {r: 0.6, m: 0.2}), plastic = stdMat('#33363b', {r: 0.75}), tireM = stdMat('#1f2023', {r: 0.9}), rimM = stdMat('#c4c9cf', {r: 0.3, m: 0.85});
  const headM = stdMat('#f4f0e2', {r: 0.2, x: {emissive: new THREE.Color('#fff4d6'), emissiveIntensity: 0.3}}), plateM = stdMat('#e9e6dc', {r: 0.5});
  const tailM = stdMat('#7a1410', {r: 0.3, x: {emissive: new THREE.Color('#ff2010'), emissiveIntensity: 0.25}});
  const root = new THREE.Group(), body = new THREE.Group(); root.add(body);
  const wheels = [];
  const tw = Math.max(0.06, r * 0.62);
  const addWheel = (x, z, dual) => {
    const Wg = wheelGeo(r, tw); const g = new THREE.Group();
    g.add(new THREE.Mesh(Wg.tire, tireM), new THREE.Mesh(Wg.rim, rimM), new THREE.Mesh(Wg.hub, trim), new THREE.Mesh(Wg.spokes, rimM));
    g.position.set(x, 0, z); root.add(g); wheels.push(g);
    if (dual){ const g2 = g.clone(); g2.position.z = z - Math.sign(z) * (tw + 0.02); root.add(g2); wheels.push(g2); }
  };
  const zw = hw - tw / 2 - 0.01;
  const y0 = -0.3 * r;                                  // 车身下沿（轮心以下）
  const sideGlass = (pts, inset) => { for (const s of [-1, 1]) body.add(flatPoly(pts, s * (inset + 0.004), glass)); };
  if (tp === 'car'){
    const xF = ax + 1.08 * r + 0.07 * wb, xR = -ax - 1.0 * r - 0.06 * wb, Lc = xF - xR;
    const yb = Math.max(y0 + 0.5 * (H - y0), 1.32 * r), dh = yb - y0, ar = 1.17 * r;
    const prof = [[xR, y0 + 0.12 * dh], ...archPts(-ax, ar, y0), ...archPts(ax, ar, y0), [xF - 0.03 * Lc, y0], [xF, y0 + 0.25 * dh], [xF, yb - 0.22 * dh], [xF - 0.1 * Lc, yb], [xR + 0.04 * Lc, yb + 0.03 * dh], [xR, yb - 0.18 * dh]];
    body.add(extrudeProfile(prof, W, paint));
    const cw = W * 0.86;
    const cab = [[ax - 0.04 * Lc, yb - 0.005], [ax * 0.05, H], [-ax - 0.12 * Lc, H * 0.985], [xR + 0.1 * Lc, yb - 0.005]];
    body.add(extrudeProfile(cab, cw, paint));
    body.add(glassOnEdge(cab[0], cab[1], cw / 2 - 0.03, glass, 0.03 * Lc), glassOnEdge(cab[2], cab[3], cw / 2 - 0.03, glass, 0.03 * Lc));
    const gy0 = yb + 0.12 * (H - yb), gy1 = H - 0.14 * (H - yb), xm = lerp(cab[1][0], cab[2][0], 0.45);
    const fx = (y) => lerp(cab[0][0], cab[1][0], (y - cab[0][1]) / (cab[1][1] - cab[0][1])), rx = (y) => lerp(cab[3][0], cab[2][0], (y - cab[3][1]) / (cab[2][1] - cab[3][1]));
    sideGlass([[fx(gy0) - 0.04 * Lc, gy0], [fx(gy1) - 0.03 * Lc, gy1], [xm + 0.02 * Lc, gy1], [xm + 0.02 * Lc, gy0]], cw / 2);
    sideGlass([[xm - 0.02 * Lc, gy0], [xm - 0.02 * Lc, gy1], [rx(gy1) + 0.03 * Lc, gy1], [rx(gy0) + 0.04 * Lc, gy0]], cw / 2);
    for (const s of [-1, 1]){
      body.add(boxAt(0.02 * Lc, 0.12 * dh, 0.16 * W, xF + 0.004, yb - 0.38 * dh, s * 0.32 * W, headM), boxAt(0.02 * Lc, 0.14 * dh, 0.17 * W, xR - 0.004, yb - 0.3 * dh, s * 0.33 * W, tailM));
    }
    body.add(boxAt(0.04 * Lc, 0.16 * dh, W * 1.02, xF, y0 + 0.08 * dh, 0, plastic), boxAt(0.04 * Lc, 0.16 * dh, W * 1.02, xR, y0 + 0.1 * dh, 0, plastic));
    // 细节：后视镜、进气格栅、车门缝、腰线饰条、车牌
    for (const s of [-1, 1]){
      body.add(boxAt(0.05 * Lc, 0.05 * (H - y0), 0.07 * W, cab[0][0] - 0.03 * Lc, yb + 0.1 * (H - yb), s * (cw / 2 + 0.05 * W), paint), boxAt(0.01 * Lc, 0.04 * (H - y0), 0.05 * W, cab[0][0] - 0.055 * Lc, yb + 0.1 * (H - yb), s * (cw / 2 + 0.05 * W), glass));
      for (const xs of [xm, lerp(xm, cab[3][0], 0.75)]) body.add(flatPoly([[xs - 0.004 * Lc, y0 + 0.3 * dh], [xs + 0.004 * Lc, y0 + 0.3 * dh], [xs + 0.004 * Lc, yb - 0.02 * dh], [xs - 0.004 * Lc, yb - 0.02 * dh]], s * (hw + 0.003), trim));
      body.add(flatPoly([[xR + 0.06 * Lc, yb - 0.1 * dh], [xF - 0.12 * Lc, yb - 0.1 * dh], [xF - 0.12 * Lc, yb - 0.06 * dh], [xR + 0.06 * Lc, yb - 0.06 * dh]], s * (hw + 0.003), rimM));
    }
    body.add(boxAt(0.012 * Lc, 0.16 * dh, 0.4 * W, xF + 0.004, yb - 0.4 * dh, 0, trim), boxAt(0.01 * Lc, 0.12 * dh, 0.24 * W, xR - 0.006, y0 + 0.42 * dh, 0, plateM));
    for (const x of [-ax, ax]) for (const z of [-zw, zw]) addWheel(x, z);
  } else if (tp === 'van'){
    const xF = ax + 1.0 * r + 0.06 * wb, xR = -ax - 0.95 * r - 0.05 * wb, Lc = xF - xR;
    const yb = Math.max(y0 + 0.38 * (H - y0), 1.3 * r), dh = yb - y0, ar = 1.17 * r;
    const hoodX = xF - 0.1 * Lc, hoodY = yb + 0.14 * (H - yb), wsX = ax - 0.02 * Lc;
    const prof = [[xR, y0], ...archPts(-ax, ar, y0), ...archPts(ax, ar, y0), [xF - 0.02 * Lc, y0], [xF, y0 + 0.3 * dh], [xF, yb], [hoodX, hoodY], [wsX, H * 0.97], [wsX - 0.06 * Lc, H], [xR + 0.02 * Lc, H], [xR, H - 0.04 * (H - y0)]];
    body.add(extrudeProfile(prof, W, paint));
    body.add(glassOnEdge([hoodX, hoodY], [wsX, H * 0.97], hw - 0.04, glass, 0.025 * Lc));
    const gy0 = hoodY + 0.08 * (H - hoodY), gy1 = H - 0.13 * (H - yb);
    const stripe = stdMat('#2f6ea5', {r: 0.4, m: 0.3});
    const wf = x => lerp(hoodX, wsX, 0.5);
    sideGlass([[wf() - 0.0 * Lc, gy0], [wsX - 0.03 * Lc, gy1], [wsX - 0.2 * Lc, gy1], [wsX - 0.2 * Lc, gy0]], hw);
    for (const [a, b] of [[-0.05, 0.28], [0.32, 0.6]]) sideGlass([[wsX - (0.24 + a) * Lc, gy0], [wsX - (0.24 + a) * Lc, gy1], [wsX - (0.24 + b) * Lc, gy1], [wsX - (0.24 + b) * Lc, gy0]].map(([x, y]) => [Math.max(x, xR + 0.06 * Lc), y]), hw);
    for (const s of [-1, 1]){
      body.add(flatPoly([[xR, yb - 0.1 * dh], [xF, yb - 0.1 * dh], [xF, yb + 0.06 * dh], [xR, yb + 0.06 * dh]], s * (hw + 0.006), stripe));
      body.add(boxAt(0.02 * Lc, 0.14 * dh, 0.16 * W, xF + 0.004, yb - 0.32 * dh, s * 0.33 * W, headM), boxAt(0.02 * Lc, 0.3 * (H - y0), 0.1 * W, xR - 0.004, yb + 0.1 * (H - yb), s * 0.4 * W, tailM));
    }
    body.add(boxAt(0.04 * Lc, 0.16 * dh, W * 1.02, xF, y0 + 0.08 * dh, 0, plastic), boxAt(0.04 * Lc, 0.16 * dh, W * 1.02, xR, y0 + 0.1 * dh, 0, plastic));
    // 细节：后视镜、格栅、侧滑门缝与导轨、车顶行李架、车牌
    for (const s of [-1, 1]){
      body.add(boxAt(0.04 * Lc, 0.08 * (H - y0), 0.06 * W, wsX - 0.02 * Lc, hoodY + 0.12 * (H - hoodY), s * (hw + 0.04 * W), trim));
      const dx = wsX - 0.25 * Lc;
      body.add(flatPoly([[dx - 0.004 * Lc, y0 + 0.25 * dh], [dx + 0.004 * Lc, y0 + 0.25 * dh], [dx + 0.004 * Lc, gy1], [dx - 0.004 * Lc, gy1]], s * (hw + 0.004), trim));
      body.add(flatPoly([[xR + 0.08 * Lc, gy0 - 0.03 * (H - y0)], [dx, gy0 - 0.03 * (H - y0)], [dx, gy0 - 0.015 * (H - y0)], [xR + 0.08 * Lc, gy0 - 0.015 * (H - y0)]], s * (hw + 0.004), trim));
      body.add(boxAt(0.62 * Lc, 0.025 * H, 0.03 * W, (xR + wsX) / 2, H + 0.03 * H, s * 0.36 * W, trim));
    }
    for (const xr of [-0.3, 0.1]) body.add(boxAt(0.02 * Lc, 0.02 * H, 0.75 * W, xr * Lc, H + 0.045 * H, 0, trim));
    body.add(boxAt(0.012 * Lc, 0.14 * dh, 0.5 * W, xF + 0.004, yb - 0.55 * dh, 0, trim), boxAt(0.01 * Lc, 0.12 * dh, 0.24 * W, xR - 0.006, y0 + 0.45 * dh, 0, plateM));
    for (const x of [-ax, ax]) for (const z of [-zw, zw]) addWheel(x, z);
  } else if (tp === 'truck'){
    const xF = ax + 1.0 * r + 0.06 * wb, xR = -ax - 1.0 * r - 0.06 * wb, Lc = xF - xR;
    const cabBack = ax - 1.05 * r, Hc = H * 0.86, ybed = 1.08 * r, ar = 1.12 * r;
    const cabProf = [[cabBack, y0], ...archPts(ax, ar, y0, 6), [xF - 0.02 * Lc, y0], [xF, y0 + 0.2 * (Hc - y0)], [xF, Hc * 0.62], [xF - 0.04 * Lc, Hc * 0.97], [xF - 0.07 * Lc, Hc], [cabBack, Hc]];
    body.add(extrudeProfile(cabProf, W * 0.96, paint));
    body.add(glassOnEdge([xF, Hc * 0.64], [xF - 0.04 * Lc, Hc * 0.95], W * 0.48 - 0.04, glass, 0.01 * Lc));
    sideGlass([[cabBack + 0.08 * (xF - cabBack), Hc * 0.6], [xF - 0.1 * (xF - cabBack), Hc * 0.6], [xF - 0.12 * (xF - cabBack), Hc * 0.93], [cabBack + 0.08 * (xF - cabBack), Hc * 0.93]], W * 0.48);
    for (const s of [-1, 1]) body.add(boxAt(0.02 * Lc, 0.08 * Hc, 0.15 * W, xF + 0.004, y0 + 0.4 * (Hc - y0), s * 0.34 * W, headM));
    body.add(boxAt(0.05 * Lc, 0.14 * (Hc - y0), W, xF, y0 + 0.07 * (Hc - y0), 0, plastic));
    const frameM = stdMat('#2a2c30', {r: 0.6, m: 0.4});
    body.add(boxAt(cabBack - xR, 0.18 * r, W * 0.45, (cabBack + xR) / 2, ybed - 0.12 * r, 0, frameM));
    const boxM = stdMat(['#dfe1e3', '#d6cdb6'][v.nth % 2], {r: 0.55, m: 0.2}), rib = stdMat('#b2b6ba', {r: 0.5, m: 0.4});
    const bx0 = xR, bx1 = cabBack - 0.03 * Lc;
    body.add(boxAt(bx1 - bx0, H - ybed, W, (bx0 + bx1) / 2, (H + ybed) / 2, 0, boxM));
    for (let q = 1; q < 7; q++){ const x = lerp(bx0, bx1, q / 7); for (const s of [-1, 1]) body.add(boxAt(0.012 * Lc, (H - ybed) * 0.94, 0.02, x, (H + ybed) / 2, s * (hw + 0.008), rib)); }
    body.add(boxAt(bx1 - bx0 + 0.02, 0.04 * H, W + 0.03, (bx0 + bx1) / 2, H, 0, rib));
    for (const s of [-1, 1]) body.add(boxAt(0.02 * Lc, 0.16 * H, 0.1 * W, xR - 0.004, ybed + 0.12 * H, s * 0.4 * W, tailM));
    // 细节：大后视镜（带支架）、格栅、油箱、挡泥板、驾驶室顶示廓灯
    for (const s of [-1, 1]){
      body.add(boxAt(0.012 * Lc, 0.012 * Lc, 0.1 * W, xF - 0.08 * (xF - cabBack), Hc * 0.72, s * (W * 0.48 + 0.05 * W), trim));
      body.add(boxAt(0.02 * Lc, 0.16 * Hc, 0.03 * W, xF - 0.08 * (xF - cabBack), Hc * 0.72, s * (W * 0.48 + 0.1 * W), trim));
      const tank = new THREE.Mesh(new THREE.CylinderGeometry(0.42 * r, 0.42 * r, 0.16 * Lc, 10).rotateZ(Math.PI / 2), stdMat('#9aa1a8', {r: 0.3, m: 0.8})); tank.position.set(cabBack - 0.12 * Lc, ybed - 0.1 * r, s * W * 0.32); body.add(tank);
      body.add(boxAt(0.01 * Lc, 0.9 * r, 0.22 * W, -ax - 1.05 * r, y0 + 0.2 * r, s * zw, plastic));
    }
    body.add(boxAt(0.012 * Lc, 0.26 * (Hc - y0), 0.62 * W, xF + 0.004, Hc * 0.42, 0, trim));
    for (let q = -1; q <= 1; q++) body.add(boxAt(0.03 * Lc, 0.025 * H, 0.06 * W, xF - 0.1 * (xF - cabBack), Hc + 0.012 * H, q * 0.22 * W, stdMat('#ffb43a', {r: 0.3, x: {emissive: new THREE.Color('#ff9a20'), emissiveIntensity: 0.5}})));
    addWheel(ax, zw); addWheel(ax, -zw);
    addWheel(-ax, zw, true); addWheel(-ax, -zw, true);
  }
  if (v.cargo){   // 11-08 甲方3D：超载货物（车顶一大摞捆扎的箱子）
    const cm = ['#b98a55', '#a87a48', '#c99c66', '#8f6a3e'].map(c => stdMat(c, {r: 0.85}));
    const strap = stdMat('#d8452f', {r: 0.6});
    const L0 = (wb + 2 * r) * 0.95, n = 3;
    for (let q = 0; q < n; q++){
      const ww = L0 / n * 0.96, hh = 0.32 + 0.08 * (q % 2), x = -L0 / 2 + L0 / n * (q + 0.5);
      body.add(boxAt(ww, hh, W * 0.92, x, H + 0.05 + hh / 2, 0, cm[q % 4]));
      if (q !== 1) body.add(boxAt(ww * 0.8, 0.26, W * 0.8, x + 0.03, H + 0.05 + hh + 0.13, 0.02, cm[(q + 2) % 4]));
    }
    for (const x of [-L0 * 0.3, L0 * 0.3]) body.add(boxAt(0.05, 0.75, W * 0.95, x, H + 0.4, 0, strap));
  }
  if (v.flat){   // 11-08 甲方3D：压瘪的轮胎（不随轮子转的胎底鼓包）
    const bulge = new THREE.CylinderGeometry(r * 1.08, r * 1.08, tw * 1.08, 16); bulge.rotateX(Math.PI / 2);
    for (const w of wheels){ const b = new THREE.Mesh(bulge, tireM); b.scale.set(1.12, 0.34, 1); b.position.set(w.position.x, -r * 0.7, w.position.z); root.add(b); }
  }
  v.model = {r, hw};
  shadowAll(root);
  for (const w of wheels) w.traverse(m => { m.receiveShadow = true; });
  return {root, body, wheels, tailM, headM};
}
function vehicleKinematics(C, v){
  const F = C.F, k = v.p0, N = F.length, dt = C.dtf;
  const O = [], U = [];
  for (let f = 0; f < N; f++){ const p = F[f].p, rw = [p[2 * k], p[2 * k + 1]], fw = [p[2 * k + 2], p[2 * k + 3]]; O.push([(rw[0] + fw[0]) / 2, (rw[1] + fw[1]) / 2]); let ux = fw[0] - rw[0], uy = fw[1] - rw[1]; const L = Math.hypot(ux, uy) || 1; U.push([ux / L, uy / L]); }
  const fArr = v.tArr != null ? Math.min(N - 1, Math.round(frameF(C, v.tArr))) : N;
  const S = new Float32Array(N), pitch = new Float32Array(N), heave = new Float32Array(N), alon = new Float32Array(N);
  for (let f = 1; f < N; f++){ const d = (O[f][0] - O[f - 1][0]) * U[f][0] + (O[f][1] - O[f - 1][1]) * U[f][1]; S[f] = S[f - 1] + (f <= fArr ? d : 0); }
  const a2 = (f, q) => (f < 1 || f > N - 2 || f >= fArr - 1) ? 0 : (O[f + 1][q] - 2 * O[f][q] + O[f - 1][q]) / (dt * dt);
  const acc = f => [(a2(f - 1, 0) + 2 * a2(f, 0) + a2(f + 1, 0)) / 4, (a2(f - 1, 1) + 2 * a2(f, 1) + a2(f + 1, 1)) / 4];
  const sc = v.dims.wb / 2.6;
  let th = 0, thv = 0, hy = 0, hyv = 0; const w = 2 * Math.PI * 1.35, z = 0.32, sub = 8;
  for (let f = 0; f < N; f++){
    const a = acc(f), u = U[f], n = [-u[1], u[0]];
    const al = clamp(a[0] * u[0] + a[1] * u[1], -14, 14), an = clamp(a[0] * n[0] + a[1] * n[1], -25, 25); alon[f] = al;
    for (let s = 0; s < sub; s++){ const h = dt / sub; thv += (-w * w * th - 2 * z * w * thv + 0.2 * al) * h; th += thv * h; hyv += (-w * w * hy - 2 * z * w * hyv - 0.2 * an) * h; hy += hyv * h; }
    pitch[f] = clamp(th, -0.06, 0.06); heave[f] = clamp(hy, -0.05, 0.05) * sc;
  }
  let vArr = [0, 0]; if (fArr < N){ const a = Math.max(0, fArr - 4); vArr = [(O[fArr][0] - O[a][0]) / ((fArr - a) * dt || 1), (O[fArr][1] - O[a][1]) / ((fArr - a) * dt || 1)]; vArr[1] = 0; }
  return {S, pitch, heave, alon, fArr, vArr};
}
function sampleArr(C, arr, t){ const f = frameF(C, t), i = Math.floor(f), fr = f - i; return i >= arr.length - 1 ? arr[arr.length - 1] : arr[i] + (arr[i + 1] - arr[i]) * fr; }
function buildVehicles(C, scene){
  const out = [];
  for (const v of C.veh){
    if (v.sab && v.idx.filter(q => C.R.particles[q].k === 1).length >= 2 && v.idx.some(q => C.R.particles[q].k !== 1)){ out.push(buildSaboteur(C, v, scene)); continue; }   // 素材库 11-07：任意结构（≥2 轮 + ≥1 车身质点）都用专门造型
    if (v.sab || v.p0 == null || v.idx.length !== 4 || !['car', 'van', 'truck'].includes(v.tp)){
      // 通用画法（捣蛋车等）：钢管车架 + 轮胎
      const grp = new THREE.Group(); scene.add(grp);
      const tube = stdMat('#6b4fa0', {r: 0.4, m: 0.5}), tireM = stdMat('#1f2023', {r: 0.9});
      const mems = C.R.members.filter(m => m[3] === v.vi).map(m => { const o = new THREE.Mesh(new THREE.CylinderGeometry(0.05, 0.05, 1, 6).rotateZ(-Math.PI / 2).translate(0.5, 0, 0), tube); shadowAll(o); grp.add(o); return {m, o}; });
      const parts = v.idx.map(q => { const pa = C.R.particles[q]; const r = Math.max(0.1, pa.r); const g = pa.k === 1 ? new THREE.CylinderGeometry(r, r, 0.2, 14).rotateX(Math.PI / 2) : new THREE.IcosahedronGeometry(r, 0); const o = new THREE.Mesh(g, pa.k === 1 ? tireM : tube); shadowAll(o); grp.add(o); return {q, o}; });
      out.push({v, generic: true, update(P){ for (const {m, o} of mems){ const ax = P[2 * m[0]], ay = P[2 * m[0] + 1], bx = P[2 * m[1]], by = P[2 * m[1] + 1]; o.position.set(ax, ay, v.lane); o.rotation.set(0, 0, Math.atan2(by - ay, bx - ax)); o.scale.x = Math.hypot(bx - ax, by - ay); } for (const {q, o} of parts) o.position.set(P[2 * q], P[2 * q + 1], v.lane); }});
      continue;
    }
    const M = buildVehicleModel(v); scene.add(M.root);
    const K = vehicleKinematics(C, v);
    const basis = new THREE.Matrix4(), X = new THREE.Vector3(), Y = new THREE.Vector3(), Z = new THREE.Vector3();
    out.push({v, M, K, update(P, t){
      const k = v.p0; let rw = [P[2 * k], P[2 * k + 1]], fw = [P[2 * k + 2], P[2 * k + 3]], rb = [P[2 * k + 4], P[2 * k + 5]], fb = [P[2 * k + 6], P[2 * k + 7]];
      let S = sampleArr(C, K.S, t);
      if (v.tArr != null && t > v.tArr){ // 到达后引擎冻结了车辆：按到达时速度继续开走
        const Pa = allP(C, v.tArr), dt = t - v.tArr, dx = K.vArr[0] * dt;
        rw = [Pa[2 * k] + dx, Pa[2 * k + 1]]; fw = [Pa[2 * k + 2] + dx, Pa[2 * k + 3]]; rb = [Pa[2 * k + 4] + dx, Pa[2 * k + 5]]; fb = [Pa[2 * k + 6] + dx, Pa[2 * k + 7]];
        S = K.S[K.fArr] + Math.abs(dx);
      }
      const O = [(rw[0] + fw[0]) / 2, (rw[1] + fw[1]) / 2];
      let ux = fw[0] - rw[0], uy = fw[1] - rw[1]; const L = Math.hypot(ux, uy) || 1; ux /= L; uy /= L;
      let nx = -uy, ny = ux; const bx = (rb[0] + fb[0]) / 2 - O[0], by = (rb[1] + fb[1]) / 2 - O[1]; if (nx * bx + ny * by < 0){ nx = -nx; ny = -ny; }
      X.set(ux, uy, 0); Y.set(nx, ny, 0); Z.crossVectors(X, Y);
      basis.makeBasis(X, Y, Z); basis.setPosition(O[0], O[1], v.lane);
      M.root.matrixAutoUpdate = false; M.root.matrix.copy(basis); M.root.matrixWorldNeedsUpdate = true;
      const post = v.tArr != null && t > v.tArr;
      M.body.rotation.z = post ? 0 : sampleArr(C, K.pitch, t); M.body.position.y = post ? 0 : sampleArr(C, K.heave, t);
      const ang = -S / v.model.r; for (const w of M.wheels) w.rotation.z = ang;
      const brake = !post && sampleArr(C, K.alon, t) * (v.dir || 1) < -1.2 && vStatus(C, v, t) === 1;
      M.tailM.emissiveIntensity = brake ? 2.2 : 0.25;
    }});
  }
  return out;
}
function vStatus(C, v, t){ const fr = frameAt(C, t); return fr.v ? fr.v[v.vi] : 1; }

// ------------------------------------------------------------ 捣蛋车（按物理结构画：两个轮质点、两个车身质点、钢梁 + 液压杆；外观像一辆改装过的跺脚车）
function knobbyTireGeo(r, w){
  const g = new THREE.CylinderGeometry(r, r, w, 20, 1);
  displace(g, (x, y, z) => { const a = Math.atan2(z, x), k = Math.round(a / (Math.PI * 2) * 20); const rr = Math.hypot(x, z); if (rr < r * 0.95) return [0, 0, 0]; const bump = (k % 2 ? 0.07 : -0.02) * r; return [Math.cos(a) * bump, 0, Math.sin(a) * bump]; });
  g.rotateX(Math.PI / 2); return g;
}
function buildSaboteur(C, v, scene){
  const R = C.R, grp = new THREE.Group(); scene.add(grp);
  const mems = R.members.filter(m => m[3] === v.vi);
  const wheelsQ = v.idx.filter(q => R.particles[q].k === 1), bodyQ = v.idx.filter(q => R.particles[q].k !== 1);
  const zS = 0.34;
  const frameM = stdMat('#2a2d32', {r: 0.5, m: 0.55}), cageM = stdMat('#ff6a1a', {r: 0.4, m: 0.3}), armorM = stdMat('#56603f', {r: 0.7, m: 0.2});
  const hazY = stdMat('#f0bd2a', {r: 0.6}), hazK = stdMat('#1b1d20', {r: 0.6}), plateM = stdMat('#646b73', {r: 0.4, m: 0.75});
  const HI = C.cfg.sabHi;   // 11-07 冷开场：液压缸发光青色 + 顶端画一块黄黑配重（物理上配重就在这个质点）
  const barrelM = HI ? stdMat('#18c9da', {r: 0.35, m: 0.3, x: {emissive: new THREE.Color('#0aa6b8'), emissiveIntensity: 0.55}}) : stdMat('#202327', {r: 0.4, m: 0.6}), chromeM = stdMat(PAL.chrome, {r: 0.22, m: 0.9}), tireM = stdMat('#1c1d20', {r: 0.92}), rimM = stdMat('#e2661d', {r: 0.4, m: 0.4});
  const unit = (sy, sz, mat) => { const g = new THREE.BoxGeometry(1, sy, sz); g.translate(0.5, 0, 0); return new THREE.Mesh(g, mat); };
  const cylX = (r, mat) => { const g = new THREE.CylinderGeometry(r, r, 1, 10); g.rotateZ(-Math.PI / 2); g.translate(0.5, 0, 0); return new THREE.Mesh(g, mat); };
  const items = mems.map(m => {
    const list = [];
    for (const z of [zS, -zS]){
      const o = new THREE.Group(); o.position.z = z;
      if (m[2] === 5){
        const b = cylX(HI ? 0.11 : 0.075, barrelM), rod = cylX(HI ? 0.06 : 0.032, HI ? barrelM : chromeM); o.add(b, rod);
        o.userData.setLen = L => { const bl = Math.max(0.15, L * 0.55); b.scale.x = bl; rod.position.x = bl; rod.scale.x = Math.max(0.01, L - bl); };
      } else {
        const b = unit(0.11, 0.09, frameM); o.add(b); o.userData.setLen = L => { b.scale.x = Math.max(0.01, L); };
      }
      grp.add(o); list.push(o);
    }
    return {m, list};
  });
  // 横梁（四个质点处）
  const cross = v.idx.map(q => { const isW = R.particles[q].k === 1; const g = new THREE.CylinderGeometry(isW ? 0.05 : 0.045, isW ? 0.05 : 0.045, 2 * zS + 0.1, 8); g.rotateX(Math.PI / 2); const o = new THREE.Mesh(g, isW ? frameM : cageM); grp.add(o); return {q, o}; });
  // 轮子：越野胎（每个轮质点两侧各一只）
  const wheels = wheelsQ.map(q => {
    const r = R.particles[q].r || 0.22, w = r * 0.95, grpW = new THREE.Group(); grp.add(grpW);
    for (const s of [1, -1]){ const t = new THREE.Mesh(knobbyTireGeo(r, w), tireM); t.position.z = s * (zS + w / 2 + 0.06); const rim = new THREE.Mesh(new THREE.CylinderGeometry(r * 0.55, r * 0.55, w + 0.02, 6).rotateX(Math.PI / 2), rimM); rim.position.z = t.position.z; grpW.add(t, rim); }
    return {q, g: grpW, r};
  });
  // 车身：装甲壳 + 防滚架 + 配重钢板 + 黄黑警示前杠 + 两根排气管，挂在两个车身质点连线上
  const body = new THREE.Group(); grp.add(body);
  // 车身挂在两个质点连线上：车身质点 ≥2 个时取初始相距最远的一对；只有 1 个车身质点（三角跺脚车）时挂在两个相距最远的轮质点上
  const P0 = C.F[0].p;
  const far = Q => { let best = [Q[0], Q[1] ?? Q[0]], bd = -1; for (let i = 0; i < Q.length; i++) for (let j = i + 1; j < Q.length; j++){ const d = Math.hypot(P0[2 * Q[j]] - P0[2 * Q[i]], P0[2 * Q[j] + 1] - P0[2 * Q[i] + 1]); if (d > bd){ bd = d; best = [Q[i], Q[j]]; } } return best; };
  const hang = bodyQ.length >= 2 ? far(bodyQ) : far(wheelsQ);
  const Lb = Math.hypot(P0[2 * hang[1]] - P0[2 * hang[0]], P0[2 * hang[1] + 1] - P0[2 * hang[0] + 1]) || 2;
  {
    const W = 2 * zS + 0.16, L = Lb + 0.5, H = 0.3;
    const shell = new THREE.Shape([[-L / 2, 0], [L / 2 - 0.25, 0], [L / 2, 0.12], [L / 2 - 0.12, H], [-L / 2 + 0.1, H], [-L / 2, H - 0.08]].map(p => new THREE.Vector2(p[0], p[1])));
    const sg = new THREE.ExtrudeGeometry(shell, {depth: W, bevelEnabled: false}); sg.translate(0, 0.07, -W / 2);
    body.add(new THREE.Mesh(sg, armorM));
    // 侧面装甲铆接板（略深色的条）
    for (const s of [1, -1]) body.add(boxAt(L * 0.8, 0.05, 0.01, -0.05, 0.2, s * (W / 2 + 0.006), plateM));
    // 黄黑警示前杠
    for (let k = 0; k < 6; k++) body.add(boxAt(0.06, 0.1, W / 6, L / 2 + 0.03, 0.12, -W / 2 + W / 12 + k * W / 6, k % 2 ? hazK : hazY));
    // 防滚架（橙色管）
    const tube = (a, b) => { const d = new THREE.Vector3().subVectors(b, a), len = d.length(); const g = new THREE.CylinderGeometry(0.028, 0.028, len, 6); const m = new THREE.Mesh(g, cageM); m.position.copy(a).add(b).multiplyScalar(0.5); m.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), d.normalize()); body.add(m); };
    const V3 = (x, y, z) => new THREE.Vector3(x, y, z), hy = 0.07 + H, top = hy + 0.32;
    for (const s of [1, -1]){ tube(V3(L * 0.18, hy, s * W * 0.42), V3(L * 0.05, top, s * W * 0.36)); tube(V3(-L * 0.3, hy, s * W * 0.42), V3(-L * 0.22, top, s * W * 0.36)); tube(V3(L * 0.05, top, s * W * 0.36), V3(-L * 0.22, top, s * W * 0.36)); }
    tube(V3(L * 0.05, top, W * 0.36), V3(L * 0.05, top, -W * 0.36)); tube(V3(-L * 0.22, top, W * 0.36), V3(-L * 0.22, top, -W * 0.36));
    tube(V3(L * 0.05, top, W * 0.36), V3(-L * 0.22, top, -W * 0.36));
    // 配重：后部三块叠放的钢板
    for (let k = 0; k < 3; k++) body.add(boxAt(L * 0.26, 0.07, W * 0.72, -L * 0.3, hy + 0.04 + k * 0.075, 0, plateM));
    // 排气管
    for (const s of [1, -1]){ const ex = new THREE.Mesh(new THREE.CylinderGeometry(0.03, 0.035, 0.36, 8), chromeM); ex.position.set(-L * 0.05, hy + 0.15, s * W * 0.47); body.add(ex); }
  }
  let wt = null;
  if (HI && bodyQ.length === 1){
    wt = new THREE.Group(); grp.add(wt);
    const cv = document.createElement('canvas'); cv.width = 256; cv.height = 128; const g2 = cv.getContext('2d');
    g2.fillStyle = '#ffd21f'; g2.fillRect(0, 0, 256, 128); g2.fillStyle = '#16181b'; for (let k = -4; k < 12; k++){ g2.beginPath(); g2.moveTo(k * 40, 128); g2.lineTo(k * 40 + 20, 128); g2.lineTo(k * 40 + 84, 0); g2.lineTo(k * 40 + 64, 0); g2.closePath(); g2.fill(); }
    g2.fillStyle = '#ffd21f'; g2.fillRect(70, 30, 116, 68); g2.fillStyle = '#16181b'; g2.font = '900 60px Arial'; g2.textAlign = 'center'; g2.textBaseline = 'middle'; g2.fillText('2T', 128, 66);
    const tx = new THREE.CanvasTexture(cv); tx.colorSpace = THREE.SRGBColorSpace;
    const side = new THREE.MeshStandardMaterial({map: tx, roughness: 0.55, metalness: 0.15, emissive: new THREE.Color('#ffb000'), emissiveIntensity: 0.12}), plain = stdMat('#e8b512', {r: 0.55, m: 0.15});
    const blk = new THREE.Mesh(new THREE.BoxGeometry(0.62, 0.36, 2 * zS + 0.12), [plain, plain, plain, plain, side, side]); wt.add(blk);
  }
  shadowAll(grp);
  return {v, update(P){
    if (wt){ const q = bodyQ[0]; wt.position.set(P[2 * q], P[2 * q + 1] + 0.12, 0); }
    for (const it of items){ const m = it.m, ax = P[2 * m[0]], ay = P[2 * m[0] + 1], bx = P[2 * m[1]], by = P[2 * m[1] + 1], L = Math.hypot(bx - ax, by - ay) || 1e-3; for (const o of it.list){ o.position.x = ax; o.position.y = ay; o.rotation.set(0, 0, Math.atan2(by - ay, bx - ax)); o.userData.setLen(L); } }
    for (const c of cross) c.o.position.set(P[2 * c.q], P[2 * c.q + 1], 0);
    for (const w of wheels){ w.g.position.set(P[2 * w.q], P[2 * w.q + 1], 0); w.g.rotation.z = -P[2 * w.q] / w.r; }
    const a = hang[0], b = hang[1]; let ax = P[2 * a], ay = P[2 * a + 1], bx = P[2 * b], by = P[2 * b + 1];
    if (bx < ax){ [ax, bx] = [bx, ax]; [ay, by] = [by, ay]; }
    const dir = (v.dir || 1) >= 0 ? 1 : -1;
    body.position.set((ax + bx) / 2, (ay + by) / 2, 0); body.rotation.set(0, dir > 0 ? 0 : Math.PI, dir > 0 ? Math.atan2(by - ay, bx - ax) : -Math.atan2(by - ay, bx - ax));
  }};
}

// ------------------------------------------------------------ 受力读数：每帧最吃力的杆件（|受力%| 最大、未断），带迟滞的场景标记
//  回放 frames[].s 是每根杆件的受力百分比（正拉负压，±100 = 断裂线，999 = 已断），这里只读不算。
function prepStress(C){
  const F = C.F, NB = C.NB, N = F.length;
  C.smax = new Float32Array(N); C.mark = new Int32Array(N).fill(-1); C.markSw = [];
  let cur = -1, since = -9;
  for (let f = 0; f < N; f++){
    const s = F[f].s; let best = -1, bv = 0;
    for (let i = 0; i < NB; i++){ const x = s[i]; if (x === 999) continue; const a = Math.abs(x); if (a > bv){ bv = a; best = i; } }
    C.smax[f] = bv;
    const t = F[f].t, curV = cur >= 0 && s[cur] !== 999 ? Math.abs(s[cur]) : -1;
    // 迟滞：当前标记的杆件没断、且受力不比最大值低 7 个百分点以上，就不换；换过之后至少保持 0.35 秒（除非断了）
    if (best >= 0 && best !== cur && (curV < 0 || (bv - curV > 7 && t - since > 0.35))){ C.markSw.push({t, from: cur, to: best}); cur = best; since = t; }
    C.mark[f] = cur;
  }
  // 断裂提示：每一簇断裂取第一根
  C.breakHud = []; let last = -9;
  for (const b of [...C.breaks].sort((p, q) => p.t - q.t)){ if (b.t - last > 0.8) C.breakHud.push(b.t); last = b.t; }
}
function stressAt(C, t){ const f = frameF(C, t), i = Math.floor(f), fr = f - i, a = C.smax; return i >= a.length - 1 ? a[a.length - 1] : a[i] + (a[i + 1] - a[i]) * fr; }
function heatColor(v){   // 冷青 → 琥珀 → 橙 → 红
  const stops = [[0, [0.50, 0.83, 0.90]], [55, [0.50, 0.83, 0.90]], [75, [0.95, 0.76, 0.31]], [90, [1.0, 0.54, 0.24]], [100, [1.0, 0.23, 0.19]]];
  v = clamp(v, 0, 100); let k = 0; while (k + 1 < stops.length && stops[k + 1][0] < v) k++;
  const [v0, c0] = stops[k], [v1, c1] = stops[Math.min(k + 1, stops.length - 1)], u = v1 > v0 ? (v - v0) / (v1 - v0) : 0;
  return [lerp(c0[0], c1[0], u), lerp(c0[1], c1[1], u), lerp(c0[2], c1[2], u)];
}
function buildStressMarker(C, scene){
  if (C.cfg.hud === false) return null;
  const D = C.D, mk = (sy, sz) => { const g = new THREE.BoxGeometry(1, sy, sz); g.translate(0.5, 0, 0); return g; };
  const mat = new THREE.MeshBasicMaterial({color: '#ffffff', transparent: true, opacity: 0.5, depthWrite: false, fog: false});   // 内层：半透明有色"套管"
  const matSoft = new THREE.MeshBasicMaterial({color: '#ffffff', transparent: true, opacity: 0.15, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false, fog: false});   // 外层：淡淡的辉光
  const truss = [D.TW, -D.TW].map(z => { const a = new THREE.Mesh(mk(0.26, 0.26), mat), b = new THREE.Mesh(mk(0.42, 0.42), matSoft); a.position.z = b.position.z = z; return [a, b]; }).flat();
  const deck = [D.DW + 0.02, -(D.DW + 0.02)].map(z => { const a = new THREE.Mesh(mk(0.24, 0.12), mat), b = new THREE.Mesh(mk(0.38, 0.3), matSoft); a.position.z = b.position.z = z; return [a, b]; }).flat();   // 桥面板：只描两侧边缘
  const all = [...truss, ...deck];
  for (const m of all){ m.userData.noAO = true; m.renderOrder = 6; m.frustumCulled = false; m.visible = false; scene.add(m); }
  const ends = (P, i) => { const m = C.R.members[i]; let ax = P[2 * m[0]], ay = P[2 * m[0] + 1], bx = P[2 * m[1]], by = P[2 * m[1] + 1]; if (bx < ax){ [ax, bx] = [bx, ax]; [ay, by] = [by, ay]; } return [ax, ay, bx, by]; };
  return {update(P, t, fr){
    for (const m of all) m.visible = false;
    const f = Math.floor(frameF(C, t) + 1e-9), cur = C.mark[f]; if (cur < 0) return;
    const v = stressAt(C, t);
    let sw = null; for (const s of C.markSw){ if (s.t <= t + 1e-9) sw = s; else break; }
    const TR = 0.3; let e = ends(P, cur), kind = C.R.members[cur][2];
    if (sw && sw.from >= 0 && t - sw.t < TR && fr.s[sw.from] !== 999){
      const u = sstep(0, 1, (t - sw.t) / TR), e0 = ends(P, sw.from);
      e = e.map((x, k) => lerp(e0[k], x, u)); if (u < 0.5) kind = C.R.members[sw.from][2];
    }
    const L = Math.hypot(e[2] - e[0], e[3] - e[1]) || 1e-3, ang = Math.atan2(e[3] - e[1], e[2] - e[0]);
    const [r, g, b] = heatColor(v), k = sstep(15, 35, v) * (fr.s[cur] === 999 ? 0 : 1);
    const pulse = v > 92 ? 0.85 + 0.15 * Math.sin(t * 18) : 1;
    mat.color.setRGB(r, g, b); matSoft.color.setRGB(r, g, b);
    mat.opacity = (0.3 + 0.45 * sstep(40, 100, v)) * k * pulse; matSoft.opacity = (0.05 + 0.18 * sstep(50, 100, v)) * k * pulse;
    for (const m of kind === 0 ? deck : truss){ m.visible = k > 0.01; m.position.x = e[0]; m.position.y = e[1] - (kind === 0 ? 0.1 : 0); m.rotation.set(0, 0, ang); m.scale.x = L; }
  }};
}
// 画面左上角的读数面板（与关卡字条、慢放角标同一套样式）
function drawStressHud(C, vt, t){
  if (C.cfg.hud === false || !C.smax) return;
  const a = sstep(0.2, 0.7, vt);
  if (a < 0.01) return;
  let v = stressAt(C, t), broken = false;
  for (const tb of C.breakHud){ const vb = vidAt(C, tb); if (vt >= vb && vt < vb + 1.8){ broken = true; break; } }
  const [r, g, b] = broken ? [1.0, 0.23, 0.19] : heatColor(v), css = `rgb(${Math.round(r * 255)},${Math.round(g * 255)},${Math.round(b * 255)})`;
  og.save(); og.globalAlpha = a;
  const x0 = 70, y0 = 64, w = 300, h = 108;
  og.fillStyle = 'rgba(14,20,28,.58)'; rr(x0, y0, w, h, 14); og.fill();
  og.fillStyle = 'rgba(225,233,242,.9)'; og.font = `26px ${FONT}`; og.textBaseline = 'alphabetic'; og.fillText('最大受力', x0 + 24, y0 + 38);
  og.fillStyle = css; og.font = `700 48px ${FONT}`; og.textAlign = 'right';
  og.fillText(broken ? '断裂' : `${Math.round(Math.min(v, 999))}%`, x0 + w - 24, y0 + 56); og.textAlign = 'left';
  const bx = x0 + 24, by = y0 + 78, bw = w - 48, bh = 8;
  og.fillStyle = 'rgba(255,255,255,.16)'; rr(bx, by, bw, bh, 4); og.fill();
  const fillW = broken ? bw : bw * clamp(v / 100, 0, 1);
  if (fillW > 1){ og.fillStyle = css; rr(bx, by, Math.max(fillW, 8), bh, 4); og.fill(); }
  og.fillStyle = 'rgba(255,255,255,.55)'; og.fillRect(bx + bw - 1, by - 3, 2, bh + 6);   // 100% 断裂线
  og.restore();
}

// ------------------------------------------------------------ 特效：断裂碎片、火花、尘土、水花
function softTex(){ const c = document.createElement('canvas'); c.width = c.height = 64; const g = c.getContext('2d'); const r = g.createRadialGradient(32, 32, 2, 32, 32, 31); r.addColorStop(0, 'rgba(255,255,255,1)'); r.addColorStop(0.5, 'rgba(255,255,255,.45)'); r.addColorStop(1, 'rgba(255,255,255,0)'); g.fillStyle = r; g.fillRect(0, 0, 64, 64); const t = new THREE.CanvasTexture(c); t.colorSpace = THREE.SRGBColorSpace; return t; }
function buildFX(C, scene){
  const I = C.T, tex = softTex(), g = 9.81, TW = C.D.TW;
  const pools = {
    splinter: new THREE.InstancedMesh(new THREE.BoxGeometry(1, 0.035, 0.05), stdMat('#d6a464', {r: 0.85}), 600),
    chunk: new THREE.InstancedMesh(new THREE.IcosahedronGeometry(1, 0), stdMat('#b7b1a5', {r: 0.9}), 200),
    spark: new THREE.InstancedMesh(new THREE.BoxGeometry(1, 0.022, 0.022), new THREE.MeshBasicMaterial({color: new THREE.Color(6.0, 2.6, 0.7), toneMapped: true}), 600),
    frag: new THREE.InstancedMesh(new THREE.BoxGeometry(1, 1, 1), stdMat(PAL.steel, {r: 0.5, m: 0.3}), 200),
    drop: new THREE.InstancedMesh(new THREE.IcosahedronGeometry(1, 0), new THREE.MeshStandardMaterial({color: '#cfe6ee', roughness: 0.2, metalness: 0, transparent: true, opacity: 0.6, flatShading: true, depthWrite: false}), 1600),
  };
  for (const k in pools){ const im = pools[k]; im.castShadow = k !== 'spark' && k !== 'drop'; im.frustumCulled = false; scene.add(im); }
  pools.spark.userData.noAO = true; pools.drop.userData.noAO = true; pools.drop.renderOrder = 3;
  const mkPoints = (n, size) => { const p = new THREE.Points(new THREE.BufferGeometry(), new THREE.PointsMaterial({map: tex, size, sizeAttenuation: true, transparent: true, depthWrite: false, vertexColors: true})); p.geometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(n * 3), 3)); p.geometry.setAttribute('color', new THREE.BufferAttribute(new Float32Array(n * 4), 4)); p.frustumCulled = false; p.renderOrder = 4; scene.add(p); return p; };
  const DMAX = 400, MMAX = 600, dust = mkPoints(DMAX, 1.3), mist = mkPoints(MMAX, 1.0);
  const evs = [];
  for (const b of C.breaks){
    if (!b.fx) continue;
    const m = C.R.members[b.i], kind = m[2], mid = memMid(C, b.i, b.t - 0.02), R = mulberry(b.i * 131 + 7);
    const zs = kind === 0 ? [0] : [TW, -TW];
    for (const z0 of zs){
      const front = z0 >= 0, parts = [];
      const add = (type, n, spd, life, size) => { for (let q = 0; q < n; q++){ const a = R() * Math.PI * 2, sp = spd * (0.35 + R() * 0.75); parts.push({type, p: [mid[0] + (R() - 0.5) * 0.3, mid[1] + (R() - 0.5) * 0.2, z0 + (R() - 0.5) * (kind === 0 ? 2 * C.D.DW * 0.7 : 0.2)], v: [Math.cos(a) * sp, Math.sin(a) * sp + spd * 0.25, (R() - 0.5) * spd * 0.8], life: life * (0.6 + R() * 0.6), s: size * (0.6 + R() * 0.8), rot: [R() * 6, R() * 6, R() * 6], w: [(R() - 0.5) * 18, (R() - 0.5) * 18, (R() - 0.5) * 18]}); } };
      if (kind === 1){ add('splinter', front ? 18 : 8, 4.2, 1.6, 0.26); add('dust', front ? 8 : 3, 1.0, 1.3, 1); }
      else if (kind === 0){ add('chunk', 12, 3.0, 1.8, 0.1); add('dust', 12, 1.1, 1.5, 1.3); }
      else if (kind === 2 || kind === 3 || kind === 5){ add('spark', front ? 34 : 14, 7.5, 0.55, 1); add('frag', kind === 3 ? 0 : 4, 3.2, 1.6, 0.06); }
      else { add('frag', 5, 3, 1.4, 0.05); }
      evs.push({t: b.t, parts});
    }
  }
  const splashes = [];
  for (const v of C.veh) if (v.entry){ const Lv = v.dims.wb + 2.2 * v.dims.r + 0.2 * v.dims.wb; splashes.push({t: v.entry.t, x: v.entry.x, z: v.lane, big: true, seed: v.vi * 97 + 5, len: Lv, k: clamp(Lv / 4, 0.45, 1.4) * (v.tp === 'truck' ? 1.2 : 1)}); }
  for (const d of C.debris) splashes.push({t: d.t, x: d.x, z: (h2(d.k, 1, 1) - 0.5) * 2 * TW, big: false, seed: d.k * 13 + 1, len: 0.5, k: 1});
  const foamMat = () => new THREE.MeshStandardMaterial({color: '#e3f1f2', roughness: 0.95, transparent: true, opacity: 0.85, depthWrite: false, flatShading: true});
  for (const s of splashes){
    const R = mulberry(s.seed); s.drops = []; s.mist = [];
    const n = s.big ? 260 : 18, k = s.k, ks = Math.sqrt(k);
    for (let q = 0; q < n; q++){ const a = R() * Math.PI * 2, rr = R(); const up = (s.big ? 3.0 + R() * 6.0 : 1.4 + R() * 2.4) * ks; const out = (s.big ? 0.6 + R() * 2.6 : 0.3 + R() * 1.0) * ks; s.drops.push({p: [s.x + (R() - 0.5) * s.len, 0, s.z + (R() - 0.5) * (s.big ? 1.6 * k : 0.3)], v: [Math.cos(a) * out, up * (0.4 + 0.6 * (1 - rr * rr)), Math.sin(a) * out], r: ((s.big ? 0.028 : 0.025) + R() * (s.big ? 0.05 : 0.025)) * (0.6 + 0.4 * k), life: 0.7 + R() * 1.0}); }
    const nm = s.big ? 110 : 8;
    for (let q = 0; q < nm; q++){ const a = R() * Math.PI * 2; s.mist.push({p: [s.x + (R() - 0.5) * s.len * 0.8, 0, s.z + (R() - 0.5) * 1.2 * k], v: [Math.cos(a) * (0.4 + R() * 1.6) * ks, ((s.big ? 1.5 : 0.6) + R() * (s.big ? 4.5 : 1.0)) * ks, Math.sin(a) * (0.4 + R() * 1.6) * ks], life: 1.0 + R() * 1.2, a: 0.25 + R() * 0.3}); }
    if (s.big){
      const cg = new THREE.CylinderGeometry(1, 0.6, 1, 22, 2, true);
      displace(cg, (x, y, z) => { if (y < 0.4) return [0, 0, 0]; const a = Math.round(Math.atan2(z, x) * 22 / (2 * Math.PI)); const kk = h2(a, 3, s.seed); return [x * 0.12 * (kk - 0.5), 0.55 * (kk - 0.3), z * 0.12 * (kk - 0.5)]; });
      const crown = new THREE.Mesh(cg, new THREE.MeshStandardMaterial({color: '#bfe0ea', roughness: 0.3, transparent: true, opacity: 0.3, side: THREE.DoubleSide, flatShading: true, depthWrite: false}));
      crown.userData.noAO = true; crown.renderOrder = 2; crown.visible = false; scene.add(crown); s.crown = crown;
    }
    const sh = new THREE.Shape(); for (let q = 0; q <= 20; q++){ const a = q / 20 * Math.PI * 2, r = 0.75 + 0.25 * h2(q % 20, 9, s.seed); q ? sh.lineTo(Math.cos(a) * r, Math.sin(a) * r) : sh.moveTo(Math.cos(a) * r, Math.sin(a) * r); }
    const foam = new THREE.Mesh(new THREE.ShapeGeometry(sh).rotateX(-Math.PI / 2), foamMat());
    const ring = new THREE.Mesh(new THREE.RingGeometry(0.8, 1, 32, 1).rotateX(-Math.PI / 2), foamMat());
    for (const o of [foam, ring]){ o.userData.noAO = true; o.renderOrder = 2; o.visible = false; scene.add(o); }
    const churn = new THREE.Mesh(new THREE.CircleGeometry(1, 20).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({color: '#3f9fb2', roughness: 0.4, transparent: true, opacity: 0.5, depthWrite: false, flatShading: true}));
    churn.userData.noAO = true; churn.renderOrder = 1.5; churn.visible = false; scene.add(churn);
    s.foam = foam; s.ring = ring; s.churn = churn;
  }
  const M4 = new THREE.Matrix4(), Q = new THREE.Quaternion(), E = new THREE.Euler(), P = new THREE.Vector3(), S = new THREE.Vector3(), Z0 = new THREE.Matrix4().makeScale(0, 0, 0), dir = new THREE.Vector3(), X1 = new THREE.Vector3(1, 0, 0);
  const groundAt = (x, z) => { if (x > I.main.x0 && x < I.main.x1) return C.waterY - 0.3; const p = I.pits.find(q => x > q.x0 && x < q.x1 && z > q.z0); if (p) return p.floor; return I.top; };
  return {
    update(t){
      const cnt = {splinter: 0, chunk: 0, spark: 0, frag: 0, drop: 0}; let nd = 0, nm = 0;
      const dpos = dust.geometry.attributes.position.array, dcol = dust.geometry.attributes.color.array;
      const mpos = mist.geometry.attributes.position.array, mcol = mist.geometry.attributes.color.array;
      for (const e of evs){
        const dt = t - e.t; if (dt < 0 || dt > 2.2) continue;
        for (const p of e.parts){
          if (dt > p.life) continue;
          if (p.type === 'dust'){ if (nd >= DMAX) continue; const k = dt / p.life, ex = 1 - Math.exp(-dt * 2); dpos.set([p.p[0] + p.v[0] * 0.5 * ex, p.p[1] + p.v[1] * 0.4 * ex + 0.25 * dt, p.p[2] + p.v[2] * 0.3 * ex], nd * 3); dcol.set([0.8, 0.72, 0.6, (1 - k) * 0.5 * sstep(0, 0.08, dt)], nd * 4); nd++; continue; }
          const drag = p.type === 'spark' ? 1.2 : 0.3, ed = (1 - Math.exp(-drag * dt)) / drag;
          let x = p.p[0] + p.v[0] * ed, y = p.p[1] + p.v[1] * ed - 0.5 * g * dt * dt, z = p.p[2] + p.v[2] * ed;
          const gy = groundAt(x, z); if (y < gy){ if (p.type === 'spark') continue; y = gy; if (gy < I.top - 0.1 && x > I.main.x0 && x < I.main.x1) continue; }
          const im = pools[p.type]; if (cnt[p.type] >= im.count) continue;
          if (p.type === 'spark'){
            const vx = p.v[0] * Math.exp(-drag * dt), vy = p.v[1] * Math.exp(-drag * dt) - g * dt, vz = p.v[2] * Math.exp(-drag * dt);
            const sp = Math.hypot(vx, vy, vz) || 1, len = clamp(sp * 0.035, 0.04, 0.32) * (1 - dt / p.life * 0.5);
            dir.set(vx / sp, vy / sp, vz / sp); Q.setFromUnitVectors(X1, dir); M4.compose(P.set(x, y, z), Q, S.set(len, 1, 1));
          } else {
            E.set(p.rot[0] + p.w[0] * dt, p.rot[1] + p.w[1] * dt, p.rot[2] + p.w[2] * dt); Q.setFromEuler(E);
            const sc = p.type === 'splinter' ? S.set(p.s, 1, 1) : S.set(p.s, p.s * 0.8, p.s * 0.9);
            M4.compose(P.set(x, y, z), Q, sc);
          }
          im.setMatrixAt(cnt[p.type]++, M4);
        }
      }
      for (const s of splashes){
        const dt = t - s.t, show = dt >= 0 && dt < 3.0, wy = C.waterY, stretch = s.big ? s.len / (3.0 * s.k) : 1;
        if (s.crown) s.crown.visible = false; s.foam.visible = s.ring.visible = s.churn.visible = false; if (!show) continue;
        { const u = dt / 3.0, r = (s.big ? 1.6 + 3.2 * Math.sqrt(u) : 0.45 + 1.0 * Math.sqrt(u)) * s.k; s.churn.visible = true; s.churn.position.set(s.x, C.waterY + 0.06, s.z); s.churn.scale.set(r * (s.big ? s.len / (3.0 * s.k) : 1), 1, r); s.churn.material.opacity = 0.5 * (1 - u); }
        for (const d of s.drops){
          if (dt > d.life || cnt.drop >= pools.drop.count) continue;
          const x = d.p[0] + d.v[0] * dt, y = wy + d.v[1] * dt - 0.5 * g * dt * dt, z = d.p[2] + d.v[2] * dt; if (y < wy - 0.05) continue;
          const r = d.r * (1 - 0.5 * dt / d.life); E.set(d.v[0], d.v[1], dt * 3); Q.setFromEuler(E); M4.compose(P.set(x, y, z), Q, S.set(r, r * 1.3, r)); pools.drop.setMatrixAt(cnt.drop++, M4);
        }
        for (const m of s.mist){
          if (dt > m.life || nm >= MMAX) continue;
          const k = dt / m.life, ex = (1 - Math.exp(-dt * 1.6)) / 1.6;
          mpos.set([m.p[0] + m.v[0] * ex, wy + 0.2 + m.v[1] * ex - 0.6 * dt * dt, m.p[2] + m.v[2] * ex], nm * 3);
          mcol.set([0.95, 0.97, 0.98, m.a * (1 - k) * sstep(0, 0.06, dt)], nm * 4); nm++;
        }
        const K = s.k;
        if (s.crown && dt < 1.2){ const u = dt / 1.2, h = Math.sin(Math.min(1, u * 1.3) * Math.PI) * 1.6 * K, r = (0.9 + 1.8 * Math.sqrt(u)) * K; s.crown.visible = h > 0.05; s.crown.position.set(s.x, wy + h / 2 - 0.05, s.z); s.crown.scale.set(r * stretch, Math.max(0.01, h), r); s.crown.material.opacity = 0.32 * (1 - u * u); }
        { const u = dt / 3.0, r = (s.big ? 1.2 + 2.6 * Math.sqrt(u) : 0.35 + 0.9 * Math.sqrt(u)) * K; s.foam.visible = true; s.foam.position.set(s.x, wy + 0.07, s.z); s.foam.scale.set(r * stretch, 1, r); s.foam.material.opacity = 0.5 * (1 - u) * (1 - u); }
        { const u = dt / 3.0, r = (s.big ? 1.6 + 5.0 * u : 0.5 + 1.8 * u) * K; s.ring.visible = true; s.ring.position.set(s.x, wy + 0.08, s.z); s.ring.scale.set(r * stretch, 1, r); s.ring.material.opacity = 0.32 * (1 - u) * sstep(0, 0.15, u); }
      }
      for (const k in pools){ const im = pools[k]; for (let q = cnt[k]; q < im.count; q++){ im.setMatrixAt(q, Z0); } im.instanceMatrix.needsUpdate = true; }
      for (let q = nd; q < DMAX; q++){ dpos[q * 3 + 1] = -1e4; } for (let q = nm; q < MMAX; q++){ mpos[q * 3 + 1] = -1e4; }
      dust.geometry.attributes.position.needsUpdate = true; dust.geometry.attributes.color.needsUpdate = true;
      mist.geometry.attributes.position.needsUpdate = true; mist.geometry.attributes.color.needsUpdate = true;
    },
  };
}

// ------------------------------------------------------------ 船、红绿灯
function buildShip(C, scene){
  const s = C.R.ship; if (!s) return null;
  const TW = C.D.TW;
  const wy = C.waterY, beam = (s.x[1] - s.x[0]) * 0.8, Ls = beam * 4.2, cx = (s.x[0] + s.x[1]) / 2;
  const grp = new THREE.Group(); scene.add(grp);
  const hullProf = (w, h) => { const sh = new THREE.Shape(); sh.moveTo(-w / 2, -Ls / 2 + 0.06 * Ls); sh.lineTo(-w / 2, Ls / 2 - 0.2 * Ls); sh.quadraticCurveTo(-w / 2, Ls / 2 - 0.04 * Ls, 0, Ls / 2); sh.quadraticCurveTo(w / 2, Ls / 2 - 0.04 * Ls, w / 2, Ls / 2 - 0.2 * Ls); sh.lineTo(w / 2, -Ls / 2 + 0.06 * Ls); sh.lineTo(w / 2 - 0.08 * w, -Ls / 2); sh.lineTo(-w / 2 + 0.08 * w, -Ls / 2); sh.closePath(); const g = new THREE.ExtrudeGeometry(sh, {depth: h, bevelEnabled: false, curveSegments: 4}); g.rotateX(-Math.PI / 2); return g; };
  const fb = beam * 0.3;   // 干舷
  const lower = new THREE.Mesh(hullProf(beam * 0.96, 1.0), stdMat('#8a2f27', {r: 0.7})); lower.position.y = wy - 1.0;
  const upper = new THREE.Mesh(hullProf(beam, fb), stdMat('#24384f', {r: 0.55, m: 0.2})); upper.position.y = wy;
  const deck = new THREE.Mesh(hullProf(beam - 0.25, 0.04), stdMat('#7a6e5d', {r: 0.9})); deck.position.y = wy + fb - 0.02;
  grp.add(lower, upper, deck);
  const hT = Math.max(1.2, Math.min(s.y[1] - wy - fb - 0.4, beam * 1.1));
  const white = stdMat('#eceae4', {r: 0.6}), glass = stdMat('#1d2833', {r: 0.1, m: 0.4});
  const sz = -Ls / 2 + 0.14 * Ls;
  grp.add(boxAt(beam * 0.78, hT * 0.55, 0.18 * Ls, 0, wy + fb + hT * 0.275, sz, white));
  grp.add(boxAt(beam * 0.92, hT * 0.24, 0.12 * Ls, 0, wy + fb + hT * 0.67, sz + 0.01 * Ls, white));
  grp.add(boxAt(beam * 0.9, hT * 0.1, 0.03, 0, wy + fb + hT * 0.68, sz + 0.07 * Ls, glass));
  const fun = new THREE.Mesh(new THREE.CylinderGeometry(beam * 0.08, beam * 0.1, hT * 0.5, 8), stdMat('#d0692b', {r: 0.6})); fun.position.set(0, wy + fb + hT * 0.8, sz - 0.06 * Ls); grp.add(fun);
  const ccols = ['#b5452e', '#2f6a8f', '#cf9b2f', '#3e7a4b', '#8a8f96'];
  const cl = 0.13 * Ls, ch = Math.min(hT * 0.3, beam * 0.26);
  for (let i = 0; i < 4; i++) for (let j = 0; j < 2; j++){ const c = boxAt(beam * 0.4, ch, cl, (j - 0.5) * beam * 0.44, wy + fb + ch / 2, -Ls / 2 + 0.32 * Ls + i * (cl + 0.02 * Ls), stdMat(ccols[(i * 2 + j * 3) % 5], {r: 0.6, m: 0.2})); grp.add(c); }
  {
    const foamM = new THREE.MeshStandardMaterial({color: '#f2f7f7', roughness: 0.95, transparent: true, opacity: 0.55, depthWrite: false, flatShading: true});
    // 船身一圈白浪（船头厚、两舷细）+ 船尾两道向后张开、逐段变淡的尾迹。xz 平面多边形：世界坐标 [x, z]
    const flatXZ = (pts, holes = []) => { const sh = new THREE.Shape(pts.map(([x, z]) => new THREE.Vector2(x, -z))); for (const h of holes) sh.holes.push(new THREE.Path(h.map(([x, z]) => new THREE.Vector2(x, -z)))); const g = new THREE.ShapeGeometry(sh, 6); g.rotateX(-Math.PI / 2); return g; };
    const arc = (cx, cz, rx, rz, a0, a1, n) => { const o = []; for (let k = 0; k <= n; k++){ const a = a0 + (a1 - a0) * k / n; o.push([cx + rx * Math.cos(a), cz + rz * Math.sin(a)]); } return o; };
    const bz = Ls / 2 - 0.22 * Ls;   // 船头圆弧起点
    const outer = [[-beam / 2 - 0.14, -Ls / 2 - 0.02 * Ls], [beam / 2 + 0.14, -Ls / 2 - 0.02 * Ls], ...arc(0, bz, beam / 2 + 0.3, 0.22 * Ls + 0.08 * Ls, 0, Math.PI, 12)];
    const inner = [[-beam / 2 + 0.02, -Ls / 2 + 0.01 * Ls], [beam / 2 - 0.02, -Ls / 2 + 0.01 * Ls], ...arc(0, bz, beam / 2 - 0.02, 0.22 * Ls - 0.01 * Ls, 0, Math.PI, 12)];
    const foam = new THREE.Mesh(flatXZ(outer, [inner]), foamM); foam.position.y = wy + 0.09; foam.userData.noAO = true; foam.renderOrder = 2; grp.add(foam);
    const segN = 5;
    for (const sd of [-1, 1]) for (let q = 0; q < segN; q++){
      const u0 = q / segN, u1 = (q + 1) / segN;
      const P = u => [sd * (beam / 2 - 0.05 + 0.2 * Ls * u), -Ls / 2 + 0.25 * Ls - 0.75 * Ls * u], Wd = u => 0.35 + 1.1 * u;
      const a = P(u0), b = P(u1);
      const pts = [[a[0], a[1]], [a[0] + sd * Wd(u0), a[1]], [b[0] + sd * Wd(u1), b[1]], [b[0], b[1]]];
      const m = new THREE.MeshStandardMaterial({color: '#eef5f6', roughness: 0.95, transparent: true, opacity: 0.22 * Math.pow(1 - u0, 2.2), depthWrite: false, flatShading: true});
      const wm = new THREE.Mesh(flatXZ(sd > 0 ? pts : pts.slice().reverse()), m); wm.position.y = wy + 0.08; wm.userData.noAO = true; wm.renderOrder = 2; grp.add(wm);
    }
    const sternM = new THREE.MeshStandardMaterial({color: '#eef5f6', roughness: 0.95, transparent: true, opacity: 0.22, depthWrite: false, flatShading: true});
    const stern = new THREE.Mesh(new THREE.CircleGeometry(1, 16).rotateX(-Math.PI / 2), sternM); stern.scale.set(beam * 0.42, 1, 0.16 * Ls); stern.position.set(0, wy + 0.085, -Ls / 2 - 0.14 * Ls); stern.userData.noAO = true; stern.renderOrder = 2; grp.add(stern);
  }
  shadowAll(grp); grp.traverse(o => { if (o.userData.noAO){ o.castShadow = false; } }); grp.position.x = cx;
  const v = (Ls + 2 * TW + 1) / s.dur;
  let tHit = null; for (const e of C.ev) if (e.type === 'SHIPHIT'){ tHit = e.t; break; }
  const zb = t => -TW - 0.6 - v * (s.t - t) - Ls / 2;   // 船中心 z（船头朝 +z）
  // 素材库 11-07：船过桥后继续朝镜头开会用大船体挡住画面 3～5 秒 → 船尾离开桥面后约 2 秒内淡出。
  // 淡出时先画一遍只写深度的替身（不写颜色），船体再按透明度画，只留最外一层表面，看不到船舱里面。
  const solids = [], films = [];
  grp.traverse(o => { if (!o.isMesh) return; if (o.userData.noAO) films.push({o, op: o.material.opacity}); else solids.push(o); });
  const depthM = new THREE.MeshBasicMaterial({colorWrite: false, transparent: true, depthWrite: true});   // 透明队列里、在水面等不透明物体之后、船体之前画（否则船后面的水被挡掉，透出黑底）
  const twins = solids.map(o => { const tw = new THREE.Mesh(o.geometry, depthM); tw.userData.noAO = true; tw.castShadow = false; tw.receiveShadow = false; tw.visible = false; tw.renderOrder = 3.9; o.add(tw); return tw; });
  const zf0 = TW + 1.0 + Ls / 2, zf1 = zf0 + 0.42 * Ls;
  let fading = false;
  const setFade = k => {
    const on = k < 0.999;
    if (on !== fading){
      fading = on;
      for (const o of solids){ o.material.transparent = on; o.material.needsUpdate = true; o.userData.noAO = on; o.renderOrder = on ? 4 : 0; }
      for (const tw of twins) tw.visible = on;
    }
    for (const o of solids){ o.material.opacity = on ? k : 1; o.castShadow = k > 0.5; }
    for (const f of films) f.o.material.opacity = f.op * k;
  };
  return {update(t){
    let z = zb(t);
    if (tHit != null && t > tHit){ const z0 = zb(tHit), tau = 0.25; z = z0 + v * tau * (1 - Math.exp(-(t - tHit) / tau)); }
    const k = C.cfg.shipFade === false || tHit != null ? 1 : 1 - sstep(zf0, zf1, z);
    setFade(k);
    grp.visible = z > -600 && k > 0.004; grp.position.z = z; grp.position.y = 0.03 * Math.sin(t * 1.3);
    grp.rotation.x = 0.005 * Math.sin(t * 0.9);
  }};
}
function buildLights(C, scene){
  const L = C.R.lights || []; if (!L.length) return null;
  const D = C.D;
  const items = L.map(l => {
    const overPit = C.T.pits.some(p => l.x > p.x0 - 0.3 && l.x < p.x1 + 0.3);
    const zz = D.ROAD_HW + 0.35, z = overPit ? -(D.PH + 0.45) : (l.dir > 0 ? zz : -zz), y0 = overPit ? C.T.top : groundYAt(C.lv, l.x);
    const sc = clamp(D.Hmax / 1.2, 0.6, 1.2);
    const g = new THREE.Group(); g.position.set(l.x, y0, z); g.scale.setScalar(sc);
    const poleM = stdMat('#5b636b', {r: 0.5, m: 0.6}), house = stdMat('#22262b', {r: 0.6});
    g.add(boxAt(0.1, 2.6, 0.1, 0, 1.3, 0, poleM));
    g.add(boxAt(0.3, 0.85, 0.32, 0, 2.5, 0, house));
    const red = new THREE.MeshStandardMaterial({color: '#2a0504', emissive: new THREE.Color('#ff0800'), emissiveIntensity: 0, roughness: 0.3});
    const grn = new THREE.MeshStandardMaterial({color: '#05300f', emissive: new THREE.Color('#25ff6a'), emissiveIntensity: 0, roughness: 0.3});
    const face = -(l.dir || 1) * 0.16;
    const lr = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.11, 0.04, 12).rotateZ(Math.PI / 2), red); lr.position.set(face, 2.7, 0);
    const lg = new THREE.Mesh(new THREE.CylinderGeometry(0.11, 0.11, 0.04, 12).rotateZ(Math.PI / 2), grn); lg.position.set(face, 2.3, 0);
    // 朝镜头一侧也看得到灯色
    const lr2 = lr.clone(); lr2.rotation.set(Math.PI / 2, 0, 0); lr2.position.set(0, 2.7, 0.165);
    const lg2 = lg.clone(); lg2.rotation.set(Math.PI / 2, 0, 0); lg2.position.set(0, 2.3, 0.165);
    g.add(lr, lg, lr2, lg2); shadowAll(g); scene.add(g);
    return {red, grn};
  });
  return {update(fr){ items.forEach((it, i) => { const st = fr.l && fr.l[i] != null ? fr.l[i] : 0; it.red.emissiveIntensity = st === 1 ? 2.6 : 0; it.grn.emissiveIntensity = st === 0 ? 2.6 : 0; }); }};
}

// ------------------------------------------------------------ 镜头基准取景（桥占画面宽度约七成）
function baseFrame(C){
  const F0 = C.F[0].p, R = C.R, I = C.T, c = C.cfg.cam || {};
  let bx0 = 1e9, bx1 = -1e9, by1 = -1e9, by0 = 1e9;
  R.particles.forEach((p, k) => { if (p.o !== -1) return; bx0 = Math.min(bx0, F0[2 * k]); bx1 = Math.max(bx1, F0[2 * k]); by1 = Math.max(by1, F0[2 * k + 1]); by0 = Math.min(by0, F0[2 * k + 1]); });
  let x0 = Math.min(bx0, I.main.x0), x1 = Math.max(bx1, I.main.x1);
  for (const p of I.pits){ x0 = Math.min(x0, p.x0 - 1); x1 = Math.max(x1, p.x1 + 1); }
  const fill = c.fill ?? 0.7;
  const W = Math.max((x1 - x0) / fill, (x1 - x0) + 8);
  const fov = c.fov ?? 26;
  const htan = Math.tan(fov * Math.PI / 360) * WV / HV;
  const dist = c.dist ?? (W / 2) / htan;
  const y0 = Math.min(by0, C.waterY), y1 = Math.max(by1, I.top + C.D.Hmax);
  C.frameW = W;
  // 无坑的关：台地正面放在画面中线以下约两成半高处（zfScreen，默认 0.22）（正面崖壁落在画面左下、右下两角，像参考图的"切开的台地"）
  if (I.hasPit && C.cfg.zf == null){ I.ZF = C.D.SHOULDER + 3.2; for (const p of I.pits) p.z1 = I.ZF + 0.5; }   // 有坑的关：台地正面离路边 3 米多，坑切到正面
  else if (C.cfg.zf == null){
    const pitch0 = c.pitch ?? 0.13, yaw0 = c.yaw ?? -0.15, tyA = c.ty ?? (Math.max(by0, I.top - 4) + Math.max(by1, I.top + C.D.Hmax)) / 2 + 0.01 * W;
    const hc = tyA + dist * Math.sin(pitch0) - I.top, camZ = (c.tz ?? -0.6) + dist * Math.cos(yaw0) * Math.cos(pitch0);
    const a = pitch0 + Math.atan((C.cfg.zfScreen ?? 0.22) * Math.tan(fov * Math.PI / 360));
    I.ZF = clamp(camZ - hc / Math.tan(a), C.D.SHOULDER + 1.7, C.D.SHOULDER + 40);
    for (const p of I.pits) p.z1 = I.ZF + 0.5;   // 坑一直切到台地正面（一条看得见机关的沟）
  }
  // 竖直方向：桥（结构中线）略低于画面中心，上方留出台地和远景
  const bmid = (Math.max(by0, I.top - 4) + Math.max(by1, I.top + C.D.Hmax)) / 2;
  C.frame = {x0, x1, y0, y1, W, dist, fov, tx: c.tx ?? (x0 + x1) / 2, ty: c.ty ?? bmid + 0.01 * W, tz: c.tz ?? -0.6, yaw: c.yaw ?? -0.15, pitch: c.pitch ?? 0.13};
}
// 剪辑表里的镜头关键帧（按模拟时间平滑插值）：cam.keys = [{t, tx, ty, dist, yaw, pitch}, ...]，缺省项用基准取景
function camKeys(C, t){
  const fr = C.frame, keys = (C.cfg.cam || {}).keys; const base = {tx: fr.tx, ty: fr.ty, dist: fr.dist, yaw: fr.yaw, pitch: fr.pitch};
  if (!keys || !keys.length) return base;
  const val = (k, n) => k[n] ?? base[n];
  const out = {};
  for (const n of Object.keys(base)){
    if (t <= keys[0].t) out[n] = val(keys[0], n);
    else if (t >= keys[keys.length - 1].t) out[n] = val(keys[keys.length - 1], n);
    else { let i = 0; while (keys[i + 1].t < t) i++; const a = keys[i], b = keys[i + 1]; out[n] = lerp(val(a, n), val(b, n), sstep(a.t, b.t, t)); }
  }
  return out;
}

// ------------------------------------------------------------ 远景层（空气透视：越远越淡、偏蓝；每关不同气质，同一套美术语言）
//  远景物体用单独的"雾"（指数，比近景线性雾淡得多），这样 300～900 米外的台地/岛屿还看得见、但对比被压低。
function hazeMat(_k, _m, extra = {}){ return new THREE.MeshStandardMaterial({vertexColors: true, roughness: 1, metalness: 0, flatShading: true, ...extra}); }
const desat = (c, k = 0.35) => { const l = c.r * 0.3 + c.g * 0.59 + c.b * 0.11; return new THREE.Color(lerp(c.r, l, k), lerp(c.g, l, k), lerp(c.b, l, k)); };
// 远景台地/海蚀柱：分层砂岩，顶上一层草
function farMesaGeo(r, h, seed, o = {}){
  const rings = Math.max(3, Math.round(h / ((o.band ?? 4) * 2.2)));
  const g = new THREE.CylinderGeometry(r * (o.topK ?? 0.82), r, h, o.seg ?? 9, rings);
  displace(g, (x0, y1, z0) => {
    const a = Math.atan2(z0, x0), ring = Math.round((y1 + h / 2) / (h / rings));
    const n = fbm(a * 1.3 + seed * 3, ring * 0.6, 700 + seed, 2), top = y1 > h / 2 - 0.01;
    const k = 0.18 * (n - 0.5) + 0.04 * (h2(ring, seed, 7) - 0.5) + (ring % 3 === 1 ? 0.025 : 0);
    return [x0 * k, top ? (h2(Math.round(a * 7), seed, 3) - 0.5) * h * 0.04 : (h2(Math.round(a * 9), ring, seed) - 0.5) * (h / rings) * 0.3, z0 * k];
  });
  if (o.sx) g.scale(o.sx, 1, 1);
  const grass = desat(col(o.grass ?? '#86a650'), 0.2), rockA = ['#b98d68', '#a87d5b', '#c49c78', '#9c7354'].map(col), dark = col('#6e5240');
  if (o.sil != null){   // 远景剪影色：按距离从灰蓝褐过渡到浅灰蓝（不用暖色岩石，避免和天光混成紫色）
    const near = col('#7f8b96'), far = col('#a9bccd'), topC = col('#93a59a');
    const base = mixC(near, far, o.sil);
    return colorize(g, (cy, ny, f) => ny > 0.6 ? jit(mixC(topC, far, o.sil * 0.8), h2(f, seed, 5), 0.04) : jit(mixC(base, col('#6f7883'), sstep(0.2, -0.6, (cy) / h) * 0.3), h2(f, seed, 4), 0.07));
  }
  return colorize(g, (cy, ny, f) => {
    if (ny > 0.6) return jit(grass, h2(f, seed, 5), 0.06);
    const band = Math.floor((cy + h / 2) / (o.band ?? 4) + seed + 0.5 * Math.sin(f * 0.7));
    let cc = rockA[((band % rockA.length) + rockA.length) % rockA.length];
    if (ny > 0.3) cc = mixC(cc, col('#e2b98a'), 0.4);
    if (cy > h / 2 - h * 0.07) cc = mixC(cc, dark, 0.35);
    return jit(cc, h2(f, seed, 4), 0.06);
  });
}
// 远景岛屿：低多边形圆丘，岸边一圈岩石
function islandGeo(r, h, seed){
  const g = new THREE.IcosahedronGeometry(1, 2);
  displace(g, (x, y, z) => { const n = fbm(x * 1.7 + seed, z * 1.7 - seed, 900 + seed, 3); return [x * 0.25 * (n - 0.5), y * 0.35 * (n - 0.4), z * 0.25 * (n - 0.5)]; });
  g.scale(r, h, r * (0.6 + 0.4 * h2(seed, 1, 2)));
  const grass = [col('#7f9f4c'), col('#6f9346'), col('#8aa956')], rock = col('#b98a5c');
  return colorize(g, (cy, ny, f) => (cy < h * 0.12 || ny < 0.45) ? jit(mixC(rock, col('#8a6a4a'), cy < 0 ? 0.4 : 0), h2(f, seed, 3), 0.08) : jit(grass[f % 3], h2(f, seed, 6), 0.05));
}
// 远山脊（一整条带）
function ridgeGeo(x0, x1, zFn, base, hFn, seed, colA, colB){
  const G = new Geo(), n = 90, pts = [];
  for (let i = 0; i <= n; i++){ const x = lerp(x0, x1, i / n), z = zFn(x); pts.push([[x, base, z], [x + (h2(i, 3, seed) - 0.5) * 6, base + hFn(x), z + (h2(i, 5, seed) - 0.5) * 30]]); }
  const cA = col(colA), cB = col(colB);
  for (let i = 0; i < n; i++){
    const [a, b] = pts[i], [c, d] = pts[i + 1];
    G.quad(a, c, d, b, (nx, ny) => jit(mixC(cA, cB, ny * 0.8 + 0.2 * h2(i, 1, seed)), h2(i, 2, seed), 0.05), [0, 0, 1]);
  }
  return G.build();
}
// 低多边形积云：一簇扁的二十面体，底面略带蓝灰
function cloudGeo(w, seed){
  const R = mulberry(seed), parts = [];
  const n = 5 + Math.floor(R() * 4);
  for (let q = 0; q < n; q++){
    const r = w * (0.16 + R() * 0.16), g = new THREE.IcosahedronGeometry(r, 1);
    displace(g, (x, y, z) => { const k = h2(Math.round(x * 13 / r), Math.round(y * 13 / r), Math.round(z * 13 / r) + seed); return [x * 0.1 * (k - 0.5), y * 0.1 * (k - 0.5), z * 0.1 * (k - 0.5)]; });
    const u = (q / (n - 1) - 0.5);
    g.scale(1, 0.62, 0.8); g.translate(u * w * 0.8 + (R() - 0.5) * w * 0.1, (1 - Math.abs(u) * 1.6) * w * 0.12 + R() * w * 0.05, (R() - 0.5) * w * 0.25);
    parts.push(g.index ? g.toNonIndexed() : g);
  }
  const m = mergeGeometries(parts);
  // 压平底面
  const p = m.attributes.position; for (let i = 0; i < p.count; i++) if (p.getY(i) < -w * 0.04) p.setY(i, -w * 0.04 - (p.getY(i) + w * 0.04) * 0.15);
  return colorize(m, (cy, ny) => ny < -0.3 ? col('#c9d6e4') : ny < 0.2 ? col('#e6edf4') : col('#ffffff'));
}
function boatGeo(seed){
  const parts = [];
  const hull = new THREE.BoxGeometry(1.0, 0.45, 3.6); displace(hull, (x, y, z) => [z > 1.2 ? -x * 0.6 * (z - 1.2) / 0.6 : 0, 0, 0]); hull.translate(0, 0.15, 0);
  parts.push(colorize(hull, () => col(['#f2efe8', '#2f4f6f', '#9b3a2f'][seed % 3])));
  const mast = new THREE.CylinderGeometry(0.04, 0.05, 4.6, 5); mast.translate(0, 2.6, 0.1); parts.push(colorize(mast, () => col('#d8d4cc')));
  const sail = new THREE.BufferGeometry(); sail.setAttribute('position', new THREE.Float32BufferAttribute([0, 0.6, 0.15, 0, 4.7, 0.12, 0, 0.6, -1.5,  0, 0.6, 0.35, 0, 3.9, 0.3, 0, 0.6, 1.7], 3)); sail.computeVertexNormals();
  const sail2 = sail.clone(); sail2.scale(-1, 1, 1);
  parts.push(colorize(sail, () => col('#fbfaf6')), colorize(sail2, () => col('#fbfaf6')));
  return mergeGeometries(parts.map(p => { p = p.index ? p.toNonIndexed() : p; p.deleteAttribute('uv'); return p; }));
}
function birdGeo(){
  const g = new THREE.BufferGeometry();
  // 两片翅膀（中线在 x=0），翼尖 y 由顶点着色器外的缩放控制：这里给出平展形状，扇动时整体绕 z 轴各自旋转
  g.setAttribute('position', new THREE.Float32BufferAttribute([0, 0, 0.12, 0.55, 0.0, -0.05, 0, 0, -0.18], 3)); g.computeVertexNormals();
  return g;
}
function buildBackground(C, scene){
  const I = C.T, fr = C.frame, cx = I.cx;
  const preset = C.cfg.bg ?? (C.R.ship ? 'bay' : I.floor < -20 ? 'canyon' : 'river');
  const R = mulberry(31337 + (preset === 'bay' ? 1 : preset === 'canyon' ? 2 : 3) * 101 + Math.round(I.span * 10));
  const camY = fr.ty + fr.dist * Math.sin(fr.pitch), camZ = fr.tz + fr.dist * Math.cos(fr.yaw) * Math.cos(fr.pitch), camX = fr.tx + fr.dist * Math.sin(fr.yaw);
  const base = C.waterY - 1.5;
  const at = (ang, d) => [camX + Math.sin(ang) * d, camZ - Math.cos(ang) * d];
  const elevY = (d, deg) => camY + d * Math.tan(deg * Math.PI / 180);
  const add = (geo, mat, x, y, z, rotY = 0) => { const m = new THREE.Mesh(geo, mat); m.position.set(x, y, z); m.rotation.y = rotY; m.userData.noAO = true; scene.add(m); return m; };
  const matNear = hazeMat(1 / 520, 0.8), matFar = hazeMat(1 / 380, 0.9), matRidge = hazeMat(1 / 300, 0.93);
  // 避开桥身正后方（画面中央）放高物体：角度 |a| < 0.2 只放低矮的
  const pickAng = (lo, hi, avoid = 0.2) => { for (let k = 0; k < 20; k++){ const a = (R() < 0.5 ? -1 : 1) * lerp(lo, hi, R()); if (Math.abs(a) > avoid) return a; } return hi; };
  // ---- 远山脊（最远层，两条，偏蓝）
  const ridgeD = [1500, 1150];
  ridgeD.forEach((D, q) => {
    const seed = 50 + q * 7;
    const g = ridgeGeo(cx - D * 1.2, cx + D * 1.2, x => camZ - D - 120 * Math.sin(x * 0.004 + q), base, x => Math.max(0, (D * Math.tan((preset === 'canyon' ? 1.3 : 1.2) * Math.PI / 180) + camY - base) * (0.35 + 0.65 * fbm(x * 0.006, q * 3.1, seed, 3))), seed,
      preset === 'canyon' ? '#b88a62' : '#7d9a6a', preset === 'canyon' ? '#d2aa80' : '#9cb58a');
    add(g, matRidge, 0, 0, 0);
  });
  // ---- 中远景主体：按关卡气质手工布局（角度相对镜头正前方，桥身正后方只放低矮的远景）
  //  [角度(弧度), 距离(米), 顶部仰角(度), 半径(米)]
  const LAY = {
    canyon: {mesa: [[-0.92, 360, 2.6, 34], [-0.6, 480, 1.9, 44], [-0.33, 700, 1.1, 52], [0.42, 640, 1.3, 55], [0.7, 430, 2.2, 38], [1.0, 320, 2.9, 28]],
             spire: [[-0.5, 260, 2.8, 7], [-0.45, 290, 1.9, 5], [0.56, 290, 2.5, 6], [0.84, 240, 3.1, 8], [-0.25, 520, 1.2, 9], [0.27, 560, 1.3, 10]], isl: [], boats: 0},
    bay: {mesa: [], spire: [[-0.64, 300, 2.3, 6], [-0.59, 325, 1.5, 4], [0.6, 265, 2.6, 7], [0.67, 285, 1.7, 5], [-0.95, 420, 1.8, 8]],
          isl: [[-0.86, 430, 1.5, 95], [-0.45, 720, 0.9, 150], [0.33, 620, 1.1, 130], [0.78, 390, 1.8, 85], [0.06, 1150, 0.5, 220]], boats: 4, light: 3},
    river: {mesa: [[-0.8, 520, 1.3, 70], [0.7, 600, 1.1, 80]], spire: [[-0.68, 290, 1.9, 6], [0.62, 310, 2.0, 6]],
            isl: [[-0.95, 360, 1.3, 115], [-0.48, 680, 0.8, 170], [0.44, 580, 0.9, 160], [0.88, 330, 1.5, 95], [0.0, 1250, 0.45, 280]], boats: 2},
  }[preset];
  const fl = (C.cfg.bgFlip ? -1 : 1);
  const groundY = preset === 'canyon' ? I.floor - 2 : base;
  const clearOfChan = () => true;
  LAY.mesa.forEach(([a, d, e, r], k) => { const [x, z] = at(a * fl, d), h = elevY(d, e) - groundY; if (!clearOfChan(x, z, r) || h < 3) return; add(farMesaGeo(r * 0.7, h, 100 + k, {band: 5.5, sx: 1 + 0.4 * h2(k, 1, 9), topK: 0.78, seg: 7, sil: clamp((d - 250) / 700, 0, 1)}), matFar, x, groundY + h / 2, z, h2(k, 2, 9) * 6.28); });
  LAY.spire.forEach(([a, d, e, r], k) => { const [x, z] = at(a * fl, d), h = elevY(d, e) - groundY; if (!clearOfChan(x, z, r) || h < 3) return; add(farMesaGeo(r, h, 300 + k, {band: 4, topK: 0.7, seg: 7, sil: clamp((d - 200) / 700, 0, 1)}), matNear, x, groundY + h / 2, z, h2(k, 3, 9) * 6.28); });
  LAY.isl.forEach(([a, d, e, r], k) => {
    const [x, z] = at(a * fl, d), h = Math.max(5, elevY(d, e) - base);
    add(islandGeo(r, h, 400 + k), matFar, x, base, z, h2(k, 4, 9) * 6.28);
    if (LAY.light === k){
      const hb = h * 0.82, tw = colorize(new THREE.CylinderGeometry(2.0, 2.8, 15, 10, 5), cy => col(Math.floor((cy + 7.5) / 3) % 2 ? '#c8402f' : '#f3f1ea'));
      add(tw, matNear, x + r * 0.15, base + hb + 7.5, z);
      add(colorize(new THREE.CylinderGeometry(1.5, 2.2, 2.6, 8), () => col('#2b2f35')), matNear, x + r * 0.15, base + hb + 16.3, z);
    }
  });
  // ---- 云（少量，静止或极慢漂移）
  const clouds = [];
  for (let k = 0; k < 11; k++){
    const ang = (R() - 0.5) * 2.2, d = lerp(700, 1600, R());
    const [x, z] = at(ang, d), y = elevY(d, lerp(2.0, 9.0, Math.pow(R(), 0.8))), w = lerp(90, 260, R());
    const m = add(cloudGeo(w, 800 + k), hazeMat(1 / 1400, 0.55, {emissive: new THREE.Color('#5f6c7c')}), x, y, z, (R() - 0.5) * 0.6);
    clouds.push({m, x, v: 0.6 + R() * 0.8});
  }
  // ---- 小动态：帆船（海湾/河口）、海鸟
  const boats = [];
  if (preset !== 'canyon'){
    const bm = hazeMat(1 / 700, 0.75), n = LAY.boats;
    for (let k = 0; k < n; k++){
      const ang = pickAng(0.3, 0.95, 0.28), d = lerp(140, 420, R()), [x, z] = at(ang, d);
      const s = lerp(1.0, 1.6, R()), m = add(boatGeo(k), bm, x, C.waterY, z, R() < 0.5 ? Math.PI / 2 : -Math.PI / 2); m.scale.setScalar(s);
      boats.push({m, x, z, v: (R() < 0.5 ? -1 : 1) * lerp(0.6, 1.4, R()), ph: R() * 6.28});
    }
  }
  const birds = [], bgeo = birdGeo(), bmat = new THREE.MeshStandardMaterial({color: '#e9ecef', roughness: 0.9, side: THREE.DoubleSide, flatShading: true});
  const nb = preset === 'canyon' ? 5 : 8;
  for (let k = 0; k < nb; k++){
    const grp = new THREE.Group(), wl = new THREE.Mesh(bgeo, bmat), wr = new THREE.Mesh(bgeo, bmat); wr.scale.x = -1; grp.add(wl, wr);
    const s = lerp(0.9, 1.3, R()); grp.scale.setScalar(s); grp.userData.noAO = true; scene.add(grp);
    const d = lerp(55, 140, R()), ang = (R() - 0.5) * 1.1, [x, z] = at(ang, d);
    birds.push({grp, wl, wr, cx: x, cz: z, cy: elevY(d, lerp(1.0, 4.5, R())), rad: lerp(6, 16, R()), w: (R() < 0.5 ? -1 : 1) * lerp(0.12, 0.25, R()), ph: R() * 6.28, fl: lerp(5.5, 8, R())});
  }
  return {update(t){
    for (const c of clouds) c.m.position.x = c.x + c.v * t;
    for (const b of boats){ b.m.position.x = b.x + b.v * t; b.m.position.y = C.waterY + 0.08 * Math.sin(t * 0.9 + b.ph); b.m.rotation.z = 0.03 * Math.sin(t * 0.7 + b.ph); }
    for (const b of birds){
      const a = b.ph + b.w * t;
      b.grp.position.set(b.cx + b.rad * Math.cos(a), b.cy + 0.6 * Math.sin(t * 0.5 + b.ph), b.cz + b.rad * 0.5 * Math.sin(a));
      b.grp.rotation.y = Math.atan2(-b.rad * Math.sin(a) * b.w, b.rad * 0.5 * Math.cos(a) * b.w);
      const flap = Math.sin(t * b.fl + b.ph), glide = sstep(0.3, 0.7, 0.5 + 0.5 * Math.sin(t * 0.35 + b.ph * 2));
      const ang = lerp(0.55 * flap, 0.08, glide);
      b.wl.rotation.z = ang; b.wr.rotation.z = -ang;
    }
  }};
}

// ------------------------------------------------------------ 组装一个剪辑的世界
let renderer, composer, renderPass, gtaoPass, bloomPass, camera, envTex;
// 11-08 甲方3D：落石 cfg.pebbles = [{t, x, y, z, vx, vz, r}]：t 之前静止在 (x, y, z)，之后抛体下落、翻滚，落到谷底/水面后消失
function buildPebbles(C, scene){
  const L = C.cfg.pebbles || []; if (!L.length) return null;
  const mat = stdMat('#8d8172', {r: 0.95});
  const items = L.map((p, i) => { const m = new THREE.Mesh(new THREE.IcosahedronGeometry(p.r ?? 0.12, 0), mat); shadowAll(m); scene.add(m); return {p, m, i}; });
  const yEnd = Math.max(C.T.floor, C.waterY) + 0.05;
  return {update(t){ for (const {p, m, i} of items){
    const a = Math.max(0, t - p.t), y = p.y - 0.5 * 9.8 * a * a;
    m.visible = y > yEnd; m.position.set(p.x + (p.vx ?? 0.6) * a, Math.max(yEnd, y), p.z + (p.vz ?? 0) * a);
    m.rotation.set(a * (5 + i), a * (3 + 2 * i), a * 4);
  } }};
}
function buildWorld(C){
  baseFrame(C);
  const scene = new THREE.Scene();
  scene.fog = new THREE.Fog(FOG, 1.25 * C.frame.dist, C.cfg.hazeScale ?? 520);   // 见文件头：fogNear=起雾距离，fogFar=衰减尺度（米）
  scene.environment = envTex; scene.environmentIntensity = 0.6;
  const sky = new THREE.Mesh(new THREE.SphereGeometry(2500, 32, 16), skyMaterial()); sky.userData.noAO = true; sky.frustumCulled = false; scene.add(sky);
  const hemi = new THREE.HemisphereLight('#cfe1ff', '#8c7552', 0.72); scene.add(hemi);
  const sun = new THREE.DirectionalLight('#ffe2b8', 3.25);
  const cx = C.T.cx, ext = Math.max(34, C.frame.W * 0.9);
  sun.position.set(cx + SUN_DIR.x * 150, SUN_DIR.y * 150, SUN_DIR.z * 150); sun.target.position.set(cx, -1, -4); scene.add(sun, sun.target);
  sun.castShadow = true; sun.shadow.mapSize.set(8192, 8192);
  const sc = sun.shadow.camera; sc.left = -ext; sc.right = ext; sc.top = ext * 0.8; sc.bottom = -ext * 0.8; sc.near = 40; sc.far = 300; sc.updateProjectionMatrix();
  sun.shadow.bias = -0.0002; sun.shadow.normalBias = 0.025; sun.shadow.radius = 2;
  buildTerrain(C, scene);
  const water = buildWater(C, scene);
  buildProps(C, scene);
  const bridge = buildBridge(C, scene);
  const vehicles = buildVehicles(C, scene);
  const fx = buildFX(C, scene);
  const ship = buildShip(C, scene);
  const lights = buildLights(C, scene);
  const bgl = buildBackground(C, scene), smk = null, peb = buildPebbles(C, scene);
  return {scene, update(t, vt, P, fr){ bgl.update(t); if (peb) peb.update(t); if (smk) smk.update(P, t, fr); if (water) water.update(t); bridge.update(P, fr, t, vt); for (const v of vehicles) v.update(P, t); fx.update(t); if (ship) ship.update(t); if (lights) lights.update(fr); }};
}

// ------------------------------------------------------------ 镜头：稳定的斜俯视全景 + 关键时刻缓推
function camParams(C, vtAll){
  const c = C.cfg.cam || {}, fr = C.frame, pre = C.pre || 0, vt = Math.max(0, vtAll - pre);
  const t = simAt(C, vt);
  let {tz, fov} = fr, {tx, ty, dist, yaw, pitch} = camKeys(C, t);
  dist *= 1 - (c.push ?? 0.04) * sstep(0, C.vdur, vt);
  if (c.track){   // 跟车：cam.track = {v, t0, t1, w, wy, ramp}
    const tk = c.track, v = C.veh[tk.v], ramp = tk.ramp ?? 0.5;
    const k = sstep(tk.t0, tk.t0 + ramp, t) * (1 - sstep(tk.t1 - ramp, tk.t1, t));
    if (v && k > 0){ const cc = vehCenter(C, v, clamp(t, C.t0, C.t1)); tx = lerp(tx, cc[0] + (tk.dx ?? 0), k * (tk.w ?? 1)); ty = lerp(ty, cc[1] + (tk.dy ?? 0), k * (tk.wy ?? 0.3)); }
  }
  const sl = slowW(C, t);
  if (sl.win && sl.w > 0 && sl.win.fx != null && c.follow !== false){
    const w = sl.w;
    tx = lerp(tx, sl.win.fx, 0.3 * w); ty = lerp(ty, sl.win.fy, 0.25 * w); dist *= lerp(1, sl.win.zoom ?? 0.85, w);
  }
  if (pre > 0 && vtAll < pre){                    // 俯冲：高空、斜侧 → 常规机位
    const b = C.cfg.build, k = sstep(0, b.camEnd ?? pre, vtAll);
    dist *= lerp(b.dist0 ?? 2.6, 1, k); pitch = lerp(b.pitch0 ?? 0.85, pitch, k); yaw = lerp(b.yaw0 ?? -0.75, yaw, k); ty += (b.ty0 ?? 0) * (1 - k);
  }
  return [tx, ty, tz, dist, yaw, pitch, fov];
}
function camAt(C, vt){
  const acc = [0, 0, 0, 0, 0, 0, 0]; let sw = 0;
  const SM = ((C.cfg.cam || {}).smooth ?? 1.0) / 10; for (let k = -10; k <= 10; k++){ const w = 11 - Math.abs(k), p = camParams(C, clamp(vt + k * SM, 0, C.vdur + (C.pre || 0))); for (let q = 0; q < 7; q++) acc[q] += w * p[q]; sw += w; }
  let [tx, ty, tz, dist, yaw, pitch, fov] = acc.map(a => a / sw);
  // 震屏（11-07 冷开场）：cam.shake = [{t: 模拟秒, amp: 米, dur: 视频秒, freq}]，按视频时间衰减，不参与平滑
  let sx = 0, sy = 0, sz = 0;
  for (const k of ((C.cfg.cam || {}).shake || [])){
    const a = vt - (C.pre || 0) - vidAt(C, k.t); if (a < 0 || a > (k.dur ?? 0.5)) continue;
    const e = Math.pow(1 - a / (k.dur ?? 0.5), 2) * (k.amp ?? 0.3), f = k.freq ?? 22;
    const hz = k.y ? 0.15 : 1; sx += hz * e * Math.sin(a * f * 6.283 + 1.3); sy += e * Math.sin(a * f * 1.13 * 6.283 + (k.y ? 1.57 : 4.1)); sz += hz * 0.5 * e * Math.sin(a * f * 0.87 * 6.283 + 2.2);
  }
  camera.fov = fov; camera.aspect = WV / HV; camera.near = Math.max(0.3, dist * 0.02); camera.far = 4000; camera.updateProjectionMatrix();
  camera.position.set(tx + sx + dist * Math.sin(yaw) * Math.cos(pitch), ty + sy + dist * Math.sin(pitch), tz + sz + dist * Math.cos(yaw) * Math.cos(pitch));
  camera.lookAt(tx + sx * 0.6, ty + sy * 0.6, tz);
}

// ------------------------------------------------------------ 叠层（片头字条、慢放角标）
const ov = document.getElementById('ov'), og = ov.getContext('2d');
const FONT = '"A12Font","Noto Sans SC","Microsoft YaHei","PingFang SC",sans-serif';   // A12Font = 环境变量 A12_FONT 指的字体文件
function rr(x, y, w, h, r){ og.beginPath(); og.moveTo(x + r, y); og.arcTo(x + w, y, x + w, y + h, r); og.arcTo(x + w, y + h, x, y + h, r); og.arcTo(x, y + h, x, y, r); og.arcTo(x, y, x + w, y, r); og.closePath(); }
function drawOverlay(C, vt, t, fade){
  og.setTransform(1, 0, 0, 1, 0, 0); og.clearRect(0, 0, WV, HV);
  const cfg = C.cfg;
  if (cfg.title){
    const a = sstep(0.25, 0.75, vt) * (1 - sstep(3.4, 3.9, vt));
    if (a > 0.001){
      og.save(); og.globalAlpha = a; const x0 = 84 - 24 * (1 - a), y0 = HV - 196;
      og.font = `600 46px ${FONT}`; const w1 = og.measureText(cfg.title).width; og.font = `30px ${FONT}`; const w2 = cfg.sub ? og.measureText(cfg.sub).width : 0;
      const bw = Math.max(w1, w2) + 76, bh = cfg.sub ? 118 : 74;
      og.fillStyle = 'rgba(14,20,28,.62)'; rr(x0, y0, bw, bh, 10); og.fill();
      og.fillStyle = '#f2c14e'; og.fillRect(x0, y0, 6, bh);
      og.fillStyle = '#ffffff'; og.font = `600 46px ${FONT}`; og.textBaseline = 'alphabetic'; og.fillText(cfg.title, x0 + 34, y0 + 56);
      if (cfg.sub){ og.fillStyle = 'rgba(225,233,242,.92)'; og.font = `30px ${FONT}`; og.fillText(cfg.sub, x0 + 35, y0 + 98); }
      og.restore();
    }
  }
  const sl = slowW(C, t);
  if (sl.w > 0.02 && cfg.slowBadge !== false){
    og.save(); og.globalAlpha = sstep(0.02, 0.5, sl.w); og.font = `600 30px ${FONT}`;
    const txt = `慢放 ${sl.win ? sl.win.rate : 0.3}×`, w = og.measureText(txt).width + 44;
    og.fillStyle = 'rgba(14,20,28,.58)'; rr(WV - 70 - w, 64, w, 54, 27); og.fill();
    og.fillStyle = '#f2c14e'; og.beginPath(); og.arc(WV - 70 - w + 26, 91, 6, 0, 7); og.fill();
    og.fillStyle = '#fff'; og.textBaseline = 'middle'; og.fillText(txt, WV - 70 - w + 40, 92); og.restore();
  }
  if (cfg.hud !== false) drawStressHud(C, vt, t);
  for (const k of (cfg.impact || [])){   // 冲击波（11-07 冷开场）：cfg.impact = [{t: 模拟秒, v: 车号, dur: 视频秒, r: 像素半径, dy: 米}]
    const a = vt - vidAt(C, k.t), D = k.dur ?? 0.7; if (a < 0 || a > D) continue;
    const v = C.veh[k.v ?? 0]; if (!v) continue;
    const cc = vehCenter(C, v, Math.min(t, k.t + 0.02)), pp = new THREE.Vector3(cc[0], cc[1] + (k.dy ?? -0.1), 0).project(camera);
    const sx = (pp.x + 1) / 2 * WV, sy = (1 - pp.y) / 2 * HV, u = a / D, e = 1 - Math.pow(1 - u, 3), R0 = k.r ?? 420;
    og.save(); og.translate(sx, sy);
    for (const [rk, lw, al] of [[1, 14, 0.9], [0.62, 7, 0.6]]){   // 两道椭圆冲击环（贴路面压扁）
      og.globalAlpha = al * (1 - u); og.strokeStyle = '#fff6dc'; og.lineWidth = lw * (1 - 0.7 * u);
      og.beginPath(); og.ellipse(0, 0, R0 * rk * e, R0 * rk * e * 0.32, 0, 0, 7); og.stroke();
    }
    if (k.down){   // 向下砸的速度线（车顶上方落下）
      const ps = new THREE.Vector3(cc[0], cc[1] + 0.5, 0).project(camera), top = (1 - ps.y) / 2 * HV - sy;
      og.globalAlpha = Math.min(1, 1.6 * (1 - u)); og.lineCap = 'round';
      for (let q = 0; q < 9; q++){ const xo = (q - 4) * R0 * 0.085 * (1 + 0.25 * Math.sin(q * 5.3)), len = R0 * (0.5 + 0.35 * Math.abs(Math.sin(q * 2.7))), y1 = top - R0 * 0.08 - (1 - e) * R0 * 0.25;
        og.strokeStyle = q % 2 ? '#ffffff' : '#ffe066'; og.lineWidth = (q === 4 ? 12 : 6) * (1 - 0.6 * u);
        og.beginPath(); og.moveTo(xo, y1 - len); og.lineTo(xo, y1); og.stroke(); }
      og.fillStyle = '#ffffff'; og.globalAlpha = Math.min(1, 1.6 * (1 - u));   // 向下的大箭头
      const ay = top - R0 * 0.02 - (1 - e) * R0 * 0.2; og.beginPath(); og.moveTo(-R0 * 0.07, ay - R0 * 0.1); og.lineTo(R0 * 0.07, ay - R0 * 0.1); og.lineTo(0, ay); og.closePath(); og.fill();
    }
    og.globalAlpha = 0.95 * (1 - u) * (1 - u);   // 放射速度线
    for (let q = 0; q < 26; q++){
      const an = q / 26 * Math.PI * 2 + 0.37 * Math.sin(q * 7.1), r1 = R0 * (0.35 + 0.75 * e) * (0.8 + 0.3 * Math.abs(Math.sin(q * 3.3))), r2 = r1 + R0 * 0.28 * (1 - u);
      og.strokeStyle = q % 3 ? '#ffffff' : '#ffd36a'; og.lineWidth = 5 * (1 - u) + 1;
      og.beginPath(); og.moveTo(Math.cos(an) * r1, Math.sin(an) * r1 * 0.55); og.lineTo(Math.cos(an) * r2, Math.sin(an) * r2 * 0.55); og.stroke();
    }
    og.restore();
  }
  for (const k of (cfg.flash || [])){   // 闪白（11-07）：cfg.flash = [{t: 模拟秒, dur: 视频秒, a, color}]
    const a = vt - vidAt(C, k.t); if (a < 0 || a > (k.dur ?? 0.18)) continue;
    og.fillStyle = k.color || '#ffffff'; og.globalAlpha = (k.a ?? 0.55) * Math.pow(1 - a / (k.dur ?? 0.18), 1.5); og.fillRect(0, 0, WV, HV); og.globalAlpha = 1;
  }
  if (fade > 0){ og.fillStyle = `rgba(0,0,0,${clamp(fade, 0, 1)})`; og.fillRect(0, 0, WV, HV); }
}

// 搭桥价签：每种材料第一根出现时，在它旁边弹出"名字 + 单价"，停 1.8 秒后缩回
const PRICE = {0: ['路面', '150 / 米', '#cfd3d8'], 1: ['木杆', '40 / 米', '#e2a250'], 2: ['钢杆', '160 / 米', '#e4572e'], 3: ['钢索', '70 / 米', '#9aa3ad'], 4: ['弹簧', '', '#79c94a'], 5: ['液压杆', '', '#2eb7c4']};
function drawPriceTags(C, vtAll){
  if (!C.bt || !C.firstOfKind || C.cfg.build.tags === false) return;
  const P = C.F[0].p, R = C.R;
  for (const kk of Object.keys(C.firstOfKind)){
    const i = C.firstOfKind[kk], k = +kk, info = PRICE[k]; if (!info || !info[1]) continue;
    const a = vtAll - C.bt[i]; if (a < 0.15 || a > 3.2) continue;
    const m = R.members[i], x = (P[2 * m[0]] + P[2 * m[1]]) / 2, y = (P[2 * m[0] + 1] + P[2 * m[1] + 1]) / 2;
    const v = new THREE.Vector3(x, y, k === 0 ? 0 : C.D.TW).project(camera);
    const sx = (v.x + 1) / 2 * WV, sy = (1 - v.y) / 2 * HV;
    const pop = a < 0.45 ? 0.6 + 0.4 * Math.sin(Math.PI / 2 * clamp((a - 0.15) / 0.3, 0, 1)) * 1.12 : (a > 2.85 ? 1 - sstep(2.85, 3.2, a) : 1);
    const al = a > 2.85 ? 1 - sstep(2.85, 3.2, a) : sstep(0.15, 0.3, a);
    og.save(); og.globalAlpha = al; og.translate(sx, sy - 90); og.scale(1.3 * pop, 1.3 * pop);
    og.font = `700 38px ${FONT}`; const w1 = og.measureText(info[0]).width; og.font = `600 34px ${FONT}`; const w2 = og.measureText(info[1]).width;
    const bw = 64 + w1 + 18 + w2 + 28, bh = 66;
    og.strokeStyle = 'rgba(255,255,255,.85)'; og.lineWidth = 3; og.beginPath(); og.moveTo(0, 33); og.lineTo(0, 66); og.stroke();
    og.fillStyle = 'rgba(14,20,28,.82)'; rr(-bw / 2, -bh / 2, bw, bh, 33); og.fill();
    og.strokeStyle = info[2]; og.lineWidth = 3; rr(-bw / 2, -bh / 2, bw, bh, 33); og.stroke();
    og.fillStyle = '#f2c14e'; og.beginPath(); og.arc(-bw / 2 + 33, 0, 17, 0, 7); og.fill();
    og.fillStyle = '#7a5a10'; og.font = `800 22px ${FONT}`; og.textAlign = 'center'; og.textBaseline = 'middle'; og.fillText('¥', -bw / 2 + 33, 1);
    og.textAlign = 'left'; og.fillStyle = info[2]; og.font = `700 38px ${FONT}`; og.fillText(info[0], -bw / 2 + 60, 2);
    og.fillStyle = '#ffffff'; og.font = `600 34px ${FONT}`; og.fillText(info[1], -bw / 2 + 60 + w1 + 18, 3);
    og.restore();
  }
}

// ------------------------------------------------------------ 渲染器
function initRenderer(){
  const canvas = document.getElementById('gl');
  renderer = new THREE.WebGLRenderer({canvas, antialias: false, preserveDrawingBuffer: true, powerPreference: 'high-performance'});
  renderer.setPixelRatio(SS); renderer.setSize(WV, HV);
  renderer.toneMapping = THREE.ACESFilmicToneMapping; renderer.toneMappingExposure = 1.05;
  renderer.shadowMap.enabled = true; renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  camera = new THREE.PerspectiveCamera(26, WV / HV, 0.5, 4000);
  const pm = new THREE.PMREMGenerator(renderer), es = new THREE.Scene(); es.add(new THREE.Mesh(new THREE.SphereGeometry(100, 32, 16), skyMaterial()));
  const gnd = new THREE.Mesh(new THREE.CircleGeometry(90, 16).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({color: '#6f8796'})); gnd.position.y = -8; es.add(gnd);
  envTex = pm.fromScene(es, 0.02).texture;
  const rt = new THREE.WebGLRenderTarget(WV * SS, HV * SS, {type: THREE.HalfFloatType, samples: 4});
  composer = new EffectComposer(renderer, rt); composer.setPixelRatio(SS); composer.setSize(WV, HV);
  renderPass = new RenderPass(new THREE.Scene(), camera); composer.addPass(renderPass);
  gtaoPass = new GTAOPass(new THREE.Scene(), camera, WV * SS, HV * SS);
  gtaoPass.updateGtaoMaterial({radius: 0.6, distanceExponent: 1.5, thickness: 0.9, scale: 1.0, samples: 16, distanceFallOff: 1.0, screenSpaceRadius: false});
  gtaoPass.updatePdMaterial({lumaPhi: 10, depthPhi: 2, normalPhi: 3, radius: 6, rings: 2, samples: 16});
  gtaoPass.blendIntensity = 0.8;
  const ovis = gtaoPass.overrideVisibility.bind(gtaoPass);
  gtaoPass.overrideVisibility = function(){ ovis(); this.scene.traverse(o => { if (o.userData.noAO || o.isSprite) o.visible = false; }); };
  if (QS.get('ao') !== '0') composer.addPass(gtaoPass);
  bloomPass = new UnrealBloomPass(new THREE.Vector2(WV * SS, HV * SS), 0.28, 0.45, 0.95); composer.addPass(bloomPass);
  composer.addPass(new OutputPass());
  if (QS.get('grade') !== '0') composer.addPass(new ShaderPass({
    uniforms: {tDiffuse: {value: null}},
    vertexShader: 'varying vec2 vUv; void main(){ vUv = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
    fragmentShader: `uniform sampler2D tDiffuse; varying vec2 vUv;
      void main(){ vec3 c = texture2D(tDiffuse, vUv).rgb;
        float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
        c = mix(vec3(l), c, 1.07);
        c = mix(c, c * c * (3.0 - 2.0 * c), 0.14);
        c += vec3(0.014, 0.006, -0.012) * smoothstep(0.45, 1.0, l) + vec3(-0.01, 0.0, 0.014) * (1.0 - smoothstep(0.0, 0.45, l));
        vec2 d = vUv - 0.5; d.x *= 1.7778; c *= mix(0.84, 1.0, smoothstep(1.12, 0.42, length(d)));
        gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0); }`,
  }));
}

let PROG = null;
function setProgram(prog){
  if (!renderer) initRenderer();
  const clips = prog.clips.map(cfg => { const C = prepClip(cfg); C.world = buildWorld(C); return C; });
  let acc = 0; for (const c of clips){ c.start = acc; acc += c.nframes; }
  PROG = {clips, frames: acc, fade: prog.fade ?? 0.3};
  return {frames: acc, clips: clips.map(c => ({frames: c.nframes, vdur: c.vdur, t0: c.t0, t1: c.t1, start: c.start, span: c.T.span, frameW: c.frameW, dims: c.D, veh: c.veh.map(v => ({tp: v.tp, ...v.dims})), slow: c.slow.map(w => ({a: w.a, b: w.b, rate: w.rate, kind: w.kind}))}))};
}
function frameOf(ci, t){ const C = PROG.clips[ci]; return C.start + Math.round(vidAt(C, t) * FPS); }
function drawFrame(n){
  if (!PROG) return null;
  n = clamp(Math.round(n), 0, PROG.frames - 1);
  let ci = 0; while (ci + 1 < PROG.clips.length && n >= PROG.clips[ci + 1].start) ci++;
  const C = PROG.clips[ci], vtAll = (n - C.start) / FPS, vt = vtAll - (C.pre || 0), t = simAt(C, Math.max(0, vt));
  const P = allP(C, t), fr = frameAt(C, t);
  C.world.update(t, vtAll, P, fr);
  camAt(C, vtAll);
  renderPass.scene = C.world.scene; gtaoPass.scene = C.world.scene;
  composer.render();
  const fd = PROG.fade, end = C.nframes / FPS;
  let fa = 0; if (ci > 0) fa = Math.max(fa, 1 - vt / fd); if (ci < PROG.clips.length - 1) fa = Math.max(fa, 1 - (end - vt) / fd);
  drawOverlay(C, vt, t, fa);
  drawPriceTags(C, vtAll);
  // 11-08 甲方3D：cfg.pts = [{name, v: 车号, dx: 沿车头方向, dy: 车身法向(向上), dz}] 或 {name, p: [x, y, z]} → 屏幕像素坐标（叠 2D 层对位用）
  let pts = null;
  if (C.cfg.pts){
    pts = {};
    for (const q of C.cfg.pts){
      let w;
      if (q.v != null){
        const v = C.veh[q.v], k = v.p0;
        const rw = [P[2 * k], P[2 * k + 1]], fw = [P[2 * k + 2], P[2 * k + 3]], O = [(rw[0] + fw[0]) / 2, (rw[1] + fw[1]) / 2];
        let ux = fw[0] - rw[0], uy = fw[1] - rw[1]; const L = Math.hypot(ux, uy) || 1; ux /= L; uy /= L;
        let nx = -uy, ny = ux; if (ny < 0){ nx = -nx; ny = -ny; }
        const dx = (q.dx ?? 0), dy = (q.dy ?? 0);
        w = new THREE.Vector3(O[0] + ux * dx + nx * dy, O[1] + uy * dx + ny * dy, v.lane + (q.dz ?? 0));
      } else w = new THREE.Vector3(q.p[0], q.p[1], q.p[2]);
      const pp = w.project(camera);
      pts[q.name] = [Math.round((pp.x + 1) / 2 * WV * 10) / 10, Math.round((1 - pp.y) / 2 * HV * 10) / 10];
    }
  }
  return {clip: ci, t, vt: vtAll, pts};
}
// 素材库用：某段的关键信息（模拟秒）——车辆到达/失败/落水、断裂、慢放窗口
function clipInfo(ci){
  const C = PROG.clips[ci];
  return {t0: C.t0, t1: C.t1, vdur: C.vdur, nframes: C.nframes, start: C.start,
    veh: C.veh.map(v => ({tp: v.tp, sab: !!v.sab, dir: v.dir, tArr: v.tArr, tFail: v.tFail, reason: v.reason, entry: v.entry ? v.entry.t : null})),
    breaks: C.breaks.map(b => ({t: b.t, i: b.i})), breakHud: C.breakHud, slow: C.slow.map(w => ({a: w.a, b: w.b, rate: w.rate, kind: w.kind}))};
}
window.THEATER3D = {setProgram, draw: drawFrame, frameOf, clipInfo, ready: true};
