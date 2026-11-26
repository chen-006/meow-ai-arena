// 照正片 src\chars_voxel.py VoxRig 移植：体素小人的装配、逐帧摆放、被镖横切成两截；照 src\chars.py 的接触影、脚下色环；
// 照 chars_voxel.make_dart 的像素镖。全部在 Blender 坐标里做（挂在一个转了 -90° 的根节点下），apply() 和 Python 版一一对应。
// 底子是第 11 集网页查看器的 char2.js；第 12 集加：国王王冠（crown）、每条命各自一套被切开的两截（占点有复活）。
// 网站另加：ghost = 0.2 秒 AI 延迟残影（半透明青色轮廓，不画镖、影子和色环）。
//
// 朝向约定（防"倒着走"）：体素坐标正面朝 -y；装配时 map = ID 原样放进 Blender 坐标，root 先绕 Z 转 hz = (90 - heading)°。
// heading = 0（地图 +x）时 hz = 90°，体素的 -y 方向转到 Blender 的 +x = 地图 +x：脸朝前进方向。
import * as THREE from 'three';
import { VOX, buildGeom, dartPart, withCrown } from './vox.js';
import { slice_curve } from './pose.js';

export const CHAR_SCALE = 1.26;
const OUTLINE_W = 0.0125, TEAM_W = 0.024, VOX_EMIT = 0.16, DART_CELL = 0.058;
const TEAM_PARTS = new Set(['body', 'head', 'hairback', 'tailL', 'tailR', 'tail']);
const OUTLINE_COL = new THREE.Color('#2E2238');
const ID = (x, y, z) => [x, y, z];
const V3 = (x, y, z) => new THREE.Vector3(x, y, z);

// 体素材质：顶点色 × 物体色，物体色 alpha < 1 时往白闪；自发光 0.16（chars_voxel.mat_vox / scene_lib._objcol）
function objcolMat(emis, basic = false) {
  const m = basic ? new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.BackSide }) : new THREE.MeshLambertMaterial({ vertexColors: true });
  const u = { uTint: { value: new THREE.Vector4(1, 1, 1, 1) } };
  m.userData.u = u;
  m.onBeforeCompile = (sh) => {
    sh.uniforms.uTint = u.uTint;
    sh.fragmentShader = 'uniform vec4 uTint;\n' + sh.fragmentShader.replace('#include <color_fragment>', `#include <color_fragment>
      diffuseColor.rgb = mix(diffuseColor.rgb * uTint.rgb, vec3(1.0), clamp(1.0 - uTint.a, 0.0, 1.0));`)
      .replace('#include <emissivemap_fragment>', `#include <emissivemap_fragment>
      totalEmissiveRadiance += diffuseColor.rgb * ${emis.toFixed(3)};`);
  };
  m.customProgramCacheKey = () => 'objcol' + emis + (basic ? 'b' : '');
  return m;
}
// 残影材质：半透明青色，不写深度
const GHOST_FILL = () => new THREE.MeshBasicMaterial({ color: new THREE.Color('#7FF3FF'), transparent: true, opacity: 0.22, depthWrite: false });
const GHOST_LINE = () => new THREE.MeshBasicMaterial({ color: new THREE.Color('#1FD8F0'), transparent: true, opacity: 0.55, depthWrite: false, side: THREE.BackSide });

function outlineGeom(og) {
  const c = og.attributes.color.array;
  for (let i = 0; i < c.length; i += 3) { c[i] = c[i] * 0.2 + OUTLINE_COL.r * 0.8; c[i + 1] = c[i + 1] * 0.2 + OUTLINE_COL.g * 0.8; c[i + 2] = c[i + 2] * 0.2 + OUTLINE_COL.b * 0.8; }
  return og;
}

// 像素镖（chars_voxel.make_dart）
const dartGeoms = {};
export function makeDart(hex, scale, width) {
  const key = hex + scale + width;
  if (!dartGeoms[key]) {
    const P = dartPart(hex);
    const r = buildGeom(P, DART_CELL, [0.5, 0.5, 0.5], { outline: width / scale, map: ID });
    r.geom.scale(scale, scale, scale); r.outline.scale(scale, scale, scale);
    dartGeoms[key] = { geom: r.geom, outline: outlineGeom(r.outline) };
  }
  const G = dartGeoms[key];
  const grp = new THREE.Group(); grp.rotation.order = 'ZYX';
  const mat = objcolMat(0.35), om = objcolMat(0, true);
  const m = new THREE.Mesh(G.geom, mat); m.castShadow = true;
  grp.add(m, new THREE.Mesh(G.outline, om));
  grp.userData.mats = [mat, om];
  return grp;
}
export function setObjColor(grp, r, g, b, a) { for (const m of grp.userData.mats) m.userData.u.uTint.value.set(r, g, b, a); }

// 接触影（chars._blob）：一块方片，径向淡出
let blobTex = null;
function blobMat() {
  if (!blobTex) {
    const c = document.createElement('canvas'); c.width = c.height = 64;
    const g = c.getContext('2d'), im = g.createImageData(64, 64);
    for (let y = 0; y < 64; y++) for (let x = 0; x < 64; x++) {
      const d = Math.hypot((x + 0.5) / 64 - 0.5, (y + 0.5) / 64 - 0.5);
      let t = Math.max(0, Math.min(1, (d - 0.5) / (0.14 - 0.5))); t = t * t * (3 - 2 * t);
      const o = (y * 64 + x) * 4, v = Math.round(t * 0.5 * 255); im.data[o] = v; im.data[o + 1] = v; im.data[o + 2] = v; im.data[o + 3] = 255;
    }
    g.putImageData(im, 0, 0);
    blobTex = new THREE.CanvasTexture(c);
  }
  return new THREE.MeshLambertMaterial({ color: '#243A4E', alphaMap: blobTex, transparent: true, depthWrite: false });
}
const blobGeo = new THREE.PlaneGeometry(1, 1);
function makeBlob() {
  const m = new THREE.Mesh(blobGeo, blobMat());
  m.renderOrder = 1;
  return m;
}
// 脚下色环（chars._ring）；team = 团队模式（更宽的环 + 白边 + 淡淡的底盘）
function makeRing(hex, team) {
  const seg = 48, V = [], C = [], F = [];
  const colr = new THREE.Color(hex);
  const aR = team ? 1 : 0.8;
  let ro = 0.58, ri = 0.47;
  if (team) { ro = 0.62; ri = 0.45; } else ri = ro - 0.06;
  const push = (x, y, z, c, a) => { V.push(x, y, z); C.push(c.r, c.g, c.b, a); };
  for (let k = 0; k < seg; k++) { const a = 2 * Math.PI * k / seg; push(ro * Math.cos(a), ro * Math.sin(a), 0, colr, aR); push(ri * Math.cos(a), ri * Math.sin(a), 0, colr, aR); }
  for (let k = 0; k < seg; k++) { const k2 = (k + 1) % seg; F.push(2 * k, 2 * k2, 2 * k2 + 1, 2 * k, 2 * k2 + 1, 2 * k + 1); }
  const base = V.length / 3;
  push(0, 0, -0.001, colr, team ? 0.2 : 0.03);
  for (let k = 0; k < seg; k++) { const a = 2 * Math.PI * k / seg; push(ri * Math.cos(a), ri * Math.sin(a), -0.001, colr, team ? 0.42 : 0.1); }
  for (let k = 0; k < seg; k++) F.push(base, base + 1 + k, base + 1 + (k + 1) % seg);
  if (team) {
    const b3 = V.length / 3, w = new THREE.Color(1, 1, 1);
    for (let k = 0; k < seg; k++) { const a = 2 * Math.PI * k / seg; push((ro + 0.005) * Math.cos(a), (ro + 0.005) * Math.sin(a), -0.0005, w, 0.95); push((ro + 0.05) * Math.cos(a), (ro + 0.05) * Math.sin(a), -0.0005, w, 0.95); }
    for (let k = 0; k < seg; k++) { const k2 = (k + 1) % seg; F.push(b3 + 2 * k, b3 + 2 * k2, b3 + 2 * k2 + 1, b3 + 2 * k, b3 + 2 * k2 + 1, b3 + 2 * k + 1); }
  }
  const b2 = V.length / 3;
  push(-0.13, -ro + 0.015, 0, colr, aR); push(0.13, -ro + 0.015, 0, colr, aR); push(0, -ro - 0.17, 0, colr, aR);
  F.push(b2, b2 + 2, b2 + 1);
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(V, 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(C, 4));
  g.setIndex(F);
  const m = new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, depthWrite: false, side: THREE.DoubleSide, color: new THREE.Color(1.5, 1.5, 1.5) });
  const ms = new THREE.Mesh(g, m); ms.renderOrder = 2;
  return ms;
}

export class VoxRig {
  /** opts：crown（刺王国王戴王冠）、ghost（0.2 秒残影） */
  constructor(root, key, color, teamColor = null, ringColor = null, opts = {}) {
    this.bl = root; this.key = key; this.color = color; this.ghost = !!opts.ghost;
    let d = VOX[key] || VOX.astra;
    if (opts.crown) d = withCrown(d, teamColor || color);
    this.d = d;
    const U = this.U = d.unit;
    this.scale = CHAR_SCALE * (d.scale || 1);
    if (this.ghost) { this.mat = GHOST_FILL(); this.omat = GHOST_LINE(); }
    else { this.mat = objcolMat(VOX_EMIT); this.omat = objcolMat(0, true); }
    const tmat = teamColor && !this.ghost ? new THREE.MeshBasicMaterial({ color: teamColor, side: THREE.BackSide }) : null;
    this.root = new THREE.Group(); root.add(this.root);
    this.parts = {}; this.rest = {};
    for (const [nm, part] of Object.entries(d.parts)) {
      const r = buildGeom(part, U, part.pivot, { outline: part.outline ? OUTLINE_W * (this.ghost ? 1.6 : 1) : 0, map: ID });
      const g = new THREE.Group(); g.rotation.order = 'ZYX';
      const m = new THREE.Mesh(r.geom, this.mat); m.castShadow = !this.ghost; g.add(m);
      if (r.outline) g.add(new THREE.Mesh(this.ghost ? r.outline : outlineGeom(r.outline), this.omat));
      if (tmat && TEAM_PARTS.has(nm) && part.outline) {
        const r2 = buildGeom(part, U, part.pivot, { outline: OUTLINE_W + TEAM_W, map: ID });
        if (r2.outline) g.add(new THREE.Mesh(r2.outline, tmat));
      }
      this.parts[nm] = g;
      this.rest[nm] = part.pivot.map((v) => v * U);
      g.position.set(...this.rest[nm]);
    }
    const P = this.parts;
    for (const [nm, ob] of Object.entries(P)) {
      let par = this.root, ref = null;
      if (['hairback', 'tailL', 'tailR', 'face_open', 'face_tight', 'face_happy'].includes(nm)) { par = P.head; ref = 'head'; }
      else if (['armA', 'armB', 'tail'].includes(nm)) { par = P.body; ref = 'body'; }
      par.add(ob);
      if (ref) ob.position.set(...this.rest[nm].map((v, i) => v - this.rest[ref][i]));
    }
    this.arm_len = d.arm_len * U;
    if (!this.ghost) {
      this.dart = makeDart(color, 0.74, 0.011); this.root.add(this.dart);
      this.blob = makeBlob(); root.add(this.blob);
      this.ring = makeRing(ringColor || color, !!teamColor); root.add(this.ring);
    }
    this.halvesBy = {};       // 每条命一套被切开的两截
    this.halves = null;
    this.blob2 = null;
  }

  dispose() {
    this.bl.remove(this.root);
    if (this.blob) this.bl.remove(this.blob, this.ring);
    for (const H of Object.values(this.halvesBy)) for (const o of H.obs) this.bl.remove(o);
    if (this.blob2) this.bl.remove(this.blob2);
    this.root.traverse((o) => { if (o.geometry && o.geometry !== blobGeo) o.geometry.dispose(); });
  }

  setVisible(v) {
    this.root.visible = v;
    if (this.blob) { this.blob.visible = v; this.ring.visible = v; }
    if (!v) { for (const H of Object.values(this.halvesBy)) for (const h of H.obs) h.visible = false; if (this.blob2) this.blob2.visible = false; }
  }

  hasSlice(life) { return !!this.halvesBy[life || 0]; }

  // 被镖切成两半（build_slice）：按被切前一瞬的姿势，把每个部件的方块按水平面分成两堆，各烘成一个网格（Blender 世界坐标）
  buildSlice(info) {
    if (this.ghost) return;
    const life = info.life || 0;
    if (this.halvesBy[life]) return;
    this.apply(info.base);
    this.root.updateMatrixWorld(true);
    const inv = new THREE.Matrix4().copy(this.bl.matrixWorld).invert();
    const M0 = this.root.matrix.clone();
    const d = V3(info.d[0], -info.d[1], 0).normalize();
    const s = V3(info.s[0], -info.s[1], 0).normalize();
    const up = V3(0, 0, 1);
    const n = up.clone().multiplyScalar(Math.cos(info.tilt)).addScaledVector(s, Math.sin(info.tilt));
    const t0 = new THREE.Vector3().setFromMatrixPosition(M0);
    let p0 = t0.clone().addScaledVector(up, 0.5);
    const U = this.U;
    if (Math.abs(info.tilt) < 1e-3) {
      const bp = this.d.parts.body, zs = bp.voxels.map((v) => v[2]);
      const zmin = Math.min(...zs), zmax = Math.max(...zs);
      const zb = zmin + Math.floor((zmax + 1 - zmin) * 0.65 + 0.5);
      const Ab = new THREE.Matrix4().multiplyMatrices(inv, this.parts.body.matrixWorld);
      const w = V3(-bp.pivot[0] * U, -bp.pivot[1] * U, (zb - bp.pivot[2]) * U).applyMatrix4(Ab);
      p0 = V3(t0.x, t0.y, w.z);
    }
    const cut = this.cutColor();
    const halvesGeo = [[], []], halvesOut = [[], []];
    for (const [nm, ob] of Object.entries(this.parts)) {
      if (nm.startsWith('face_') && nm !== 'face_tight') continue;
      const A = new THREE.Matrix4().multiplyMatrices(inv, ob.matrixWorld);
      const part = this.d.parts[nm], piv = part.pivot;
      const side = new Map();
      for (const v of part.voxels) {
        const c = V3((v[0] + 0.5 - piv[0]) * U, (v[1] + 0.5 - piv[1]) * U, (v[2] + 0.5 - piv[2]) * U).applyMatrix4(A);
        side.set(v[0] + ',' + v[1] + ',' + v[2], c.sub(p0).dot(n) >= 0 ? 0 : 1);
      }
      const DIRS = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]];
      const subs = [{ voxels: [], paint: [], slabs: [] }, { voxels: [], paint: [], slabs: [] }];
      const pm = new Map((part.paint || []).map((p) => [p.slice(0, 4).join(','), p[4]]));
      for (const v of part.voxels) {
        const kk = v[0] + ',' + v[1] + ',' + v[2], sd = side.get(kk), sp = subs[sd];
        sp.voxels.push(v);
        DIRS.forEach(([ax, ay, az], dd) => {
          const nb = (v[0] + ax) + ',' + (v[1] + ay) + ',' + (v[2] + az);
          if (side.has(nb) && side.get(nb) !== sd) sp.paint.push([v[0], v[1], v[2], dd, cut]);
          else if (pm.has(kk + ',' + dd)) sp.paint.push([v[0], v[1], v[2], dd, pm.get(kk + ',' + dd)]);
        });
      }
      for (const sl of part.slabs || []) {
        const c = V3(((sl[0] + sl[1]) / 2 - piv[0]) * U, ((sl[2] + sl[3]) / 2 - piv[1]) * U, ((sl[4] + sl[5]) / 2 - piv[2]) * U).applyMatrix4(A);
        const dist = c.sub(p0).dot(n);
        if (Math.abs(dist) < 0.3 * U) continue;
        subs[dist >= 0 ? 0 : 1].slabs.push(sl);
      }
      subs.forEach((sp, i) => {
        if (!sp.voxels.length && !sp.slabs.length) return;
        const r = buildGeom(sp, U, piv, { outline: part.outline ? OUTLINE_W : 0, map: ID });
        r.geom.applyMatrix4(A); halvesGeo[i].push(r.geom);
        if (r.outline) { r.outline.applyMatrix4(A); halvesOut[i].push(outlineGeom(r.outline)); }
      });
    }
    const obs = [], cs = [], samp = [];
    for (let i = 0; i < 2; i++) {
      const g = mergeGeoms(halvesGeo[i]), og = mergeGeoms(halvesOut[i]);
      const grp = new THREE.Group();
      const mat = objcolMat(VOX_EMIT), om = objcolMat(0, true);
      if (g) { const m = new THREE.Mesh(g, mat); m.castShadow = true; grp.add(m); }
      if (og) grp.add(new THREE.Mesh(og, om));
      grp.userData.mats = [mat, om];
      grp.matrixAutoUpdate = false; grp.visible = false;
      this.bl.add(grp); obs.push(grp);
      const gg = g || new THREE.BufferGeometry().setAttribute('position', new THREE.Float32BufferAttribute([0, 0, 0], 3));
      gg.computeBoundingBox();
      cs.push(gg.boundingBox.getCenter(new THREE.Vector3()));
      const pa = gg.attributes.position.array, step = Math.max(1, Math.floor(pa.length / 3 / 400)), sm = [];
      for (let j = 0; j < pa.length / 3; j += step) sm.push(V3(pa[j * 3], pa[j * 3 + 1], pa[j * 3 + 2]));
      samp.push(sm);
    }
    const g0 = t0.z;
    let hw = 0.05;
    for (const v of samp[1]) hw = Math.max(hw, (v.x - p0.x) * s.x + (v.y - p0.y) * s.y);
    const H = { obs, c: cs, d, s, n, up, ground: g0, base: V3(p0.x, p0.y, g0), land: info.land || [true, true], samp, cut_z: p0.z, hw };
    H.sg = up.clone().applyAxisAngle(d, 0.3).dot(s) > 0 ? 1 : -1;
    this.halvesBy[life] = H;
    if (!this.blob2) { this.blob2 = makeBlob(); this.blob2.visible = false; this.bl.add(this.blob2); }
  }

  cutColor() {
    const rgb = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16));
    const count = (vs, skip) => { const c = {}; for (const v of vs) if (v[3] !== skip) c[v[3]] = (c[v[3]] || 0) + 1; return Object.entries(c).sort((a, b) => b[1] - a[1])[0][0]; };
    const hair = rgb(count(this.d.parts.head.voxels, this.d.skin || (this.d.palette && this.d.palette.skin)));
    const body = rgb(count(this.d.parts.body.voxels, null));
    const main = rgb(this.color);
    const cands = [main.map((v) => v * 0.2 + 255 * 0.8), main.map((v) => v * 0.4 + 255 * 0.6), [255, 246, 222]];
    const dist = (a, b) => a.reduce((s, v, i) => s + Math.abs(v - b[i]), 0);
    const best = cands.reduce((bb, c) => (Math.min(dist(c, hair), dist(c, body)) > Math.min(dist(bb, hair), dist(bb, body)) ? c : bb));
    return '#' + best.map((v) => Math.floor(v).toString(16).padStart(2, '0')).join('').toUpperCase();
  }

  applySlice(p, H) {
    this.root.visible = false; this.ring.visible = false;
    for (const [lf, HH] of Object.entries(this.halvesBy)) if (HH !== H) for (const o of HH.obs) o.visible = false;
    const u = p.slice.u, c = slice_curve(u);
    const { d, s, up } = H, g0 = H.ground;
    const G = 0.03;
    const minZ = (W, sm) => { let m = 1e9; const v = new THREE.Vector3(); for (const q of sm) { v.copy(q).applyMatrix4(W); m = Math.min(m, v.z); } return m; };
    const T = (v) => new THREE.Matrix4().makeTranslation(v.x, v.y, v.z);
    const S = new THREE.Matrix4().makeScale(c.scale, c.scale, c.scale);
    let ob = H.obs[0], ci = H.c[0];
    let R = new THREE.Matrix4().makeRotationAxis(up, c.yaw_up * H.sg).multiply(new THREE.Matrix4().makeRotationAxis(d, -H.sg * c.th_up));
    let W = T(d.clone().multiplyScalar(c.carry)).multiply(T(ci)).multiply(R).multiply(S).multiply(T(ci.clone().negate()));
    const m0 = minZ(W, H.samp[0]);
    const tau = u - c.leave;
    let dz = c.lift - (tau > 0 ? 0.5 * G * tau * tau : 0);
    let floor;
    if (tau <= 0 || c.carry < 0.24) floor = H.cut_z; else if (H.land[0]) floor = g0; else floor = null;
    if (floor !== null) dz = Math.max(dz, floor - m0);
    const topDz = dz;
    ob.visible = !(floor === null && dz < -9);
    ob.matrix.copy(T(up.clone().multiplyScalar(dz)).multiply(W)); ob.matrixWorldNeedsUpdate = true;
    setObjColor(ob, 1, 1, 1, 1 - c.flash);
    ob = H.obs[1]; ci = H.c[1];
    const piv = H.base.clone().addScaledVector(s, H.hw);
    R = new THREE.Matrix4().makeRotationAxis(d, H.sg * c.th_low);
    W = T(piv).multiply(R).multiply(T(piv.clone().negate())).multiply(T(ci)).multiply(S).multiply(T(ci.clone().negate()));
    if (!H.land[1]) { const v = Math.max(0, u - 4.5); dz = -0.5 * G * v * v; }
    else dz = Math.max(0, g0 - minZ(W, H.samp[1]));
    ob.visible = dz >= -9;
    ob.matrix.copy(T(up.clone().multiplyScalar(dz)).multiply(W)); ob.matrixWorldNeedsUpdate = true;
    setObjColor(ob, 1, 1, 1, 1 - c.flash);
    const ga = p.ground || 0;
    if (ga <= 0.01) this.blob.visible = false;
    else {
      this.blob.visible = true;
      const k = H.hw * 0.8 * Math.min(1, c.th_low / 1.5);
      this.blob.position.set(H.base.x + s.x * k, H.base.y + s.y * k, 0.012);
      this.blob.scale.set(1, 1, 1); this.blob.material.opacity = ga;
    }
    const hgt = topDz + H.cut_z - g0;
    if (H.land[0] && ga > 0.01 && c.carry >= 0.24) {
      this.blob2.visible = true;
      const q = H.base.clone().addScaledVector(d, c.carry);
      this.blob2.position.set(q.x, q.y, 0.012); this.blob2.scale.set(0.9, 0.9, 1);
      this.blob2.material.opacity = ga * Math.max(0.3, 1 - hgt * 1.5);
    } else this.blob2.visible = false;
  }

  apply(p) {
    if (p.visible === false) { this.setVisible(false); return; }
    this.setVisible(true);
    const H = p.slice ? this.halvesBy[p.life || 0] : null;
    if (p.slice && H) { this.applySlice(p, H); return; }
    for (const HH of Object.values(this.halvesBy)) for (const h of HH.obs) h.visible = false;
    if (this.blob2) this.blob2.visible = false;
    if (p.slice) { this.setVisible(false); return; }      // 两截还没建好（实时对局里出局的那一瞬）：先不画
    const s = this.scale, d = this.d;
    const hz = (90 - p.heading) * Math.PI / 180;
    const zz = (p.z || 0) + 0.055 * (p.hop || 0);
    const Mx = new THREE.Matrix4().makeTranslation(p.x, -p.y, zz)
      .multiply(new THREE.Matrix4().makeRotationZ(hz))
      .multiply(new THREE.Matrix4().makeRotationX((p.lean || 0) * 0.85))
      .multiply(new THREE.Matrix4().makeRotationY(p.roll || 0))
      .multiply(new THREE.Matrix4().makeRotationZ(p.spin || 0));
    if (p.flip) {
      const c = new THREE.Matrix4().makeTranslation(0, 0, 0.45 * s);
      Mx.multiply(c).multiply(new THREE.Matrix4().makeRotationX(p.flip)).multiply(new THREE.Matrix4().makeTranslation(0, 0, -0.45 * s));
    }
    const k = p.scale ?? 1;
    const sx = p.sx ?? 1, sy = p.sy ?? sx, sz = p.sz ?? 1;
    const r = this.root;
    Mx.decompose(r.position, r.quaternion, r.scale);
    r.scale.set(s * k * sx, s * k * sy, s * k * sz);
    const P = this.parts, R = this.rest;
    const bdz = p.body_dz || 0, tw = p.twist || 0;
    P.body.position.set(0, 0, bdz);
    P.body.rotation.set(0, 0, tw);
    const [hp, hy, hr] = p.head || [0, 0, 0];
    P.head.position.set(R.head[0], R.head[1], R.head[2] + bdz + (p.head_dz || 0));
    P.head.rotation.set(hp, hr, hy + tw * 0.5);
    const [hb, hs] = p.hair || [0, 0];
    if (P.hairback) P.hairback.rotation.set(-hb * (d.hair_swing ?? 1), 0, hs * 0.6);
    for (const [nm, sg] of [['tailL', 1], ['tailR', -1]]) if (P[nm]) P[nm].rotation.set(-hb * 1.1, sg * (0.04 + hb * 0.3) + hs * 0.5, hs * 0.5);
    if (P.tail) {
      const [tb, ts] = p.tail || [hb * 0.5, hs];
      const sw = d.tail_swing ?? 1;
      P.tail.rotation.set((d.tail_rest || 0) - tb * 0.6 * sw, 0, ts * 1.5 * sw);
    }
    const ct = Math.cos(-tw), st = Math.sin(-tw);
    let tipA = null;
    for (const [nm, key, sg] of [['armA', 'hand_a', 1], ['armB', 'hand_b', -1]]) {
      const [f, sd, u] = p[key];
      const dx = (sd * sg - 0.22) * sg, dy = -f, dzz = u - 0.4;
      let dv = V3(dx * ct - dy * st, dx * st + dy * ct, dzz);
      let L = dv.length();
      if (L < 1e-5) { dv = V3(0, 0, -1); L = 1; }
      const ob = P[nm];
      ob.quaternion.setFromUnitVectors(V3(0, 0, -1), dv.clone().divideScalar(L));
      const stretch = 1 + 0.3 * Math.min(1, Math.max(0, (L - 0.14) / 0.3));
      ob.scale.set(1, 1, stretch);
      if (sg > 0) {
        const sh = V3(R[nm][0], R[nm][1], R[nm][2] + bdz);
        const tip = dv.clone().divideScalar(L).multiplyScalar(this.arm_len * stretch);
        const c = Math.cos(tw), sn = Math.sin(tw);
        tipA = V3(sh.x * c - sh.y * sn, sh.x * sn + sh.y * c, sh.z).add(V3(tip.x * c - tip.y * sn, tip.x * sn + tip.y * c, tip.z));
      }
    }
    for (const [nm, key, sg] of [['footA', 'foot_a', 1], ['footB', 'foot_b', -1]]) {
      const [f, sd, u] = p[key];
      const th = -Math.max(-1.3, Math.min(1.3, f / 0.16)) * 0.7 + (p[key + '_pitch'] || 0) * 0.45;
      const ob = P[nm];
      ob.rotation.set(th, 0, 0);
      ob.position.set(R[nm][0] + (sd - sg * 0.085) * 0.6, R[nm][1], R[nm][2] + Math.max(0, u - 0.04) * 0.55);
    }
    const face = p.face || 'open';
    for (const nm of ['open', 'tight', 'happy']) if (P['face_' + nm]) P['face_' + nm].visible = nm === face;
    if (this.ghost) return;
    const dd = p.dart;
    if (!dd || !tipA) this.dart.visible = false;
    else {
      this.dart.visible = true;
      const [f, sd, u] = dd.pos, ha = p.hand_a;
      const off = V3(sd - ha[1], -(f - ha[0]), u - ha[2]);
      this.dart.position.copy(tipA.addScaledVector(off, 0.7));
      this.dart.rotation.set(dd.rot[0], dd.rot[1], dd.rot[2]);
      this.dart.scale.setScalar(dd.scale || 1);
    }
    const tint = p.tint || [1, 1, 1];
    this.mat.userData.u.uTint.value.set(tint[0], tint[1], tint[2], 1 - (p.flash || 0));
    this.omat.userData.u.uTint.value.set(tint[0], tint[1], tint[2], 1 - (p.flash || 0));
    setObjColor(this.dart, tint[0], tint[1], tint[2], 1 - (p.flash || 0));
    const gx = p.gx ?? p.x, gy = p.gy ?? p.y, ga = p.ground ?? 1;
    if (ga <= 0.01) { this.blob.visible = false; this.ring.visible = false; }
    else {
      const hgt = Math.max(0, zz), bs = Math.max(0.3, 1 - hgt * 0.35);
      this.blob.position.set(gx, -gy, 0.012); this.blob.scale.set(bs, bs, 1);
      this.blob.material.opacity = ga * Math.max(0.25, 1 - hgt * 0.4);
      this.ring.position.set(gx, -gy, 0.016); this.ring.rotation.set(0, 0, hz);
      this.ring.material.opacity = ga * (p.ring ?? 1);
      this.ring.visible = (p.ring ?? 1) * ga > 0.01;
    }
  }
}

function mergeGeoms(arr) {
  if (!arr.length) return null;
  const g = new THREE.BufferGeometry();
  for (const name of ['position', 'normal', 'color']) {
    if (!arr[0].attributes[name]) continue;
    let L = 0; for (const a of arr) L += a.attributes[name].array.length;
    const o = new Float32Array(L); let k = 0;
    for (const a of arr) { o.set(a.attributes[name].array, k); k += a.attributes[name].array.length; }
    g.setAttribute(name, new THREE.BufferAttribute(o, 3));
  }
  return g;
}
