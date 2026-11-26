// 照正片 src\fx.py（第 12 集版）逐行移植：特效对象池，每帧按"事件发生后过了几步"摆位置、大小、透明度（Blender 坐标，挂在转了 -90° 的根节点下）。
// 第 12 集加：复活光柱、国王掉命（金色冲击环）、翻点、掉进岩浆 / 水的水花。
import * as THREE from 'three';
import { FLY_Z, clamp, ease, eout, bump } from './pose.js';

const WHITE = [1, 1, 1];
export const SLICE_FX_Z = 0.36;
const lin = (hex) => { const c = new THREE.Color(hex); return [c.r, c.g, c.b]; };
const mixc = (a, b, w) => a.map((x, i) => x + (b[i] - x) * w);
const frac = (h) => h - Math.floor(h);

function meshGeo(V, F, C) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(V.flat(), 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(C.flat(), 4));
  const idx = [];
  for (const f of F) { if (f.length === 3) idx.push(...f); else idx.push(f[0], f[1], f[2], f[0], f[2], f[3]); }
  g.setIndex(idx);
  return g;
}
function ringMesh(r_in = 0.9) {
  const seg = 56, V = [], F = [], C = [];
  for (const [r, a] of [[r_in - 0.08, 0], [r_in, 1], [1, 1], [1.06, 0]]) for (let k = 0; k < seg; k++) { const t = 2 * Math.PI * k / seg; V.push([r * Math.cos(t), r * Math.sin(t), 0]); C.push([1, 1, 1, a]); }
  for (let j = 0; j < 3; j++) for (let k = 0; k < seg; k++) { const k2 = (k + 1) % seg; F.push([j * seg + k, j * seg + k2, (j + 1) * seg + k2, (j + 1) * seg + k]); }
  return meshGeo(V, F, C);
}
function discMesh() {
  const seg = 40, V = [[0, 0, 0]], F = [], C = [[1, 1, 1, 1]];
  for (const [r, a] of [[0.5, 0.75], [1, 0]]) for (let k = 0; k < seg; k++) { const t = 2 * Math.PI * k / seg; V.push([r * Math.cos(t), r * Math.sin(t), 0]); C.push([1, 1, 1, a]); }
  for (let k = 0; k < seg; k++) { const k2 = (k + 1) % seg; F.push([0, 1 + k, 1 + k2]); F.push([1 + k, 1 + seg + k, 1 + seg + k2, 1 + k2]); }
  return meshGeo(V, F, C);
}
function starMesh(spikes = 8, r_in = 0.3, alt = 0.62) {
  const V = [[0, 0, 0]], F = [], C = [[1, 1, 1, 1]], n = spikes * 2;
  for (let k = 0; k < n; k++) {
    const t = 2 * Math.PI * k / n, r = k % 2 === 0 ? (Math.floor(k / 2) % 2 === 0 ? 1 : alt) : r_in;
    V.push([r * Math.cos(t), r * Math.sin(t), 0]); C.push([1, 1, 1, k % 2 ? 1 : 0.85]);
  }
  for (let k = 0; k < n; k++) F.push([0, 1 + k, 1 + (k + 1) % n]);
  return meshGeo(V, F, C);
}
function arcMesh(half = 62 * Math.PI / 180, r0 = 0.5) {
  const seg = 28, V = [], F = [], C = [];
  for (let i = 0; i <= seg; i++) {
    const u = i / seg, t = -half + 2 * half * u, wid = Math.sin(Math.PI * u) ** 0.7, ri = 1 - (1 - r0) * wid;
    const a = (0.15 + 0.85 * u) * Math.min(1, wid * 3);
    for (const [r, al] of [[ri, 0], [ri * 0.5 + 0.5, a * 0.75], [0.985, a], [1.03, 0]]) { V.push([r * Math.cos(t), r * Math.sin(t), 0]); C.push([1, 1, 1, al]); }
  }
  for (let i = 0; i < seg; i++) for (let j = 0; j < 3; j++) { const a0 = i * 4 + j; F.push([a0, a0 + 1, a0 + 5, a0 + 4]); }
  return meshGeo(V, F, C);
}
function streakMesh() {
  const V = [], F = [], C = [], n = 12;
  for (let i = 0; i <= n; i++) {
    const u = i / n, x = -1 + 2 * u, w = Math.sin(Math.PI * u) ** 0.6;
    for (const [yy, a] of [[-1, 0], [0, 1], [1, 0]]) { V.push([x, yy * w, 0]); C.push([1, 1, 1, a * Math.min(1, w * 2.5)]); }
  }
  for (let i = 0; i < n; i++) for (let j = 0; j < 2; j++) { const a0 = i * 3 + j; F.push([a0, a0 + 3, a0 + 4, a0 + 1]); }
  return meshGeo(V, F, C);
}
function quadMesh(w = 1, h = 0.5) { return meshGeo([[-w, -h, 0], [w, -h, 0], [w, h, 0], [-w, h, 0]], [[0, 1, 2, 3]], [[1, 1, 1, 1], [1, 1, 1, 1], [1, 1, 1, 1], [1, 1, 1, 1]]); }
// 复活光柱（fx._beam_mesh）：开口圆筒（半径 1、高 1），顶点 alpha 从底下 1 渐到顶上 0
function beamMesh(seg = 14) {
  const V = [], F = [], C = [], rows = [0, 0.25, 0.6, 1], al = [1, 0.7, 0.3, 0];
  rows.forEach((z, zi) => { for (let k = 0; k < seg; k++) { const a = 2 * Math.PI * k / seg; V.push([Math.cos(a), Math.sin(a), z]); C.push([1, 1, 1, al[zi]]); } });
  for (let zi = 0; zi < rows.length - 1; zi++) for (let k = 0; k < seg; k++) { const k2 = (k + 1) % seg; F.push([zi * seg + k, zi * seg + k2, (zi + 1) * seg + k2, (zi + 1) * seg + k]); }
  return meshGeo(V, F, C);
}

function emitMat(strength) {
  const m = new THREE.MeshBasicMaterial({ vertexColors: true, transparent: true, depthWrite: false, side: THREE.DoubleSide });
  m.userData.strength = strength;
  return m;
}
function puffMat() {
  return new THREE.ShaderMaterial({
    transparent: true, depthWrite: false,
    uniforms: { uCol: { value: new THREE.Color(1, 1, 1) }, uA: { value: 1 } },
    vertexShader: 'varying vec3 vN; varying vec3 vV; void main(){ vec4 mv = modelViewMatrix * vec4(position,1.0); vN = normalize(normalMatrix * normal); vV = normalize(-mv.xyz); gl_Position = projectionMatrix * mv; }',
    fragmentShader: 'uniform vec3 uCol; uniform float uA; varying vec3 vN; varying vec3 vV; void main(){ float f = abs(dot(normalize(vN), normalize(vV))); float soft = pow(f, 2.0); gl_FragColor = vec4(uCol * vec3(1.0, 0.97, 0.9) * 0.9, soft * 0.85 * uA);\n#include <colorspace_fragment>\n}',
  });
}

class Pool {
  constructor(root, n, geo, makeMat) {
    this.obs = []; this.i = 0;
    for (let k = 0; k < n; k++) {
      const m = new THREE.Mesh(geo, makeMat()); m.rotation.order = 'ZYX'; m.visible = false; m.renderOrder = 5; m.frustumCulled = false;
      root.add(m); this.obs.push(m);
    }
  }
  reset() { for (let k = 0; k < this.i; k++) this.obs[k].visible = false; this.i = 0; }
  take() { if (this.i >= this.obs.length) return null; const ob = this.obs[this.i++]; ob.visible = true; return ob; }
}

export class FX {
  /** colors：每个座位的主色；teamColors：原始队号 0 / 1 的显示色 */
  constructor(root, colors, teamColors = ['#FF5B4D', '#2E8CFF'], pitch = 58) {
    this.col = colors.map(lin);
    this.teamc = teamColors.map(lin);
    const ico = new THREE.IcosahedronGeometry(1, 2);
    this.ring = new Pool(root, 36, ringMesh(), () => emitMat(1.25));
    this.disc = new Pool(root, 16, discMesh(), () => emitMat(2.2));
    this.star = new Pool(root, 14, starMesh(), () => emitMat(2.2));
    this.spark = new Pool(root, 130, starMesh(4, 0.22, 1.0), () => emitMat(2.2));
    this.arc = new Pool(root, 8, arcMesh(), () => emitMat(2.2));
    this.conf = new Pool(root, 200, quadMesh(0.1, 0.05), () => emitMat(1.0));
    this.streak = new Pool(root, 8, streakMesh(), () => emitMat(2.2));
    this.cube = new Pool(root, 48, new THREE.BoxGeometry(1, 1, 1), () => new THREE.MeshLambertMaterial({ color: '#fff' }));
    this.puff = new Pool(root, 140, ico, puffMat);
    this.beam = new Pool(root, 10, beamMesh(), () => emitMat(1.0));
    this.tilt = (90 - pitch) * Math.PI / 180;
  }
  reset() { for (const p of [this.ring, this.disc, this.star, this.spark, this.arc, this.conf, this.puff, this.streak, this.cube, this.beam]) p.reset(); }

  put(pool, x, y, z, s, col, a, rot = 0, flat = true, sy = null) {
    const ob = pool.take();
    if (!ob || a <= 0.003 || s <= 0) { if (ob) ob.visible = false; return; }
    ob.position.set(x, -y, z);
    ob.scale.set(s, sy === null ? s : sy, s);
    ob.rotation.set(flat ? 0 : this.tilt, 0, rot);
    const m = ob.material;
    if (m.isShaderMaterial) { m.uniforms.uCol.value.setRGB(col[0], col[1], col[2]); m.uniforms.uA.value = a; }
    else { const k = m.userData.strength || 1; m.color.setRGB(col[0] * k, col[1] * k, col[2] * k); m.opacity = a; }
  }
  beamAt(x, y, rr, h, col, a) {
    const ob = this.beam.take();
    if (!ob) return;
    if (a <= 0.003) { ob.visible = false; return; }
    ob.position.set(x, -y, 0); ob.scale.set(rr, rr, h); ob.rotation.set(0, 0, 0);
    ob.material.color.setRGB(col[0], col[1], col[2]); ob.material.opacity = a;
  }
  sparks(x, y, z, u, n, speed, col, seed = 0, life = 7, size = 0.12, col2 = null, up = 0.5) {
    for (let j = 0; j < n; j++) {
      const r1 = frac(Math.sin(seed * 12.9898 + j * 78.233) * 43758.5453), r2 = frac(Math.sin(seed * 39.3 + j * 11.7) * 24634.63);
      const ang = 2 * Math.PI * (j + r1) / n;
      const d = speed * (0.6 + 0.6 * r2) * (1 - Math.exp(-u / 2.5)) * 2.5;
      const zz = z + up * (0.5 + r1) * u * 0.25 - 0.012 * u * u;
      const a = clamp(1 - u / life) ** 0.7;
      const c = col2 !== null && j % 2 ? col2 : col;
      this.put(this.spark, x + Math.cos(ang) * d, y + Math.sin(ang) * d, Math.max(0.05, zz), size * (0.7 + 0.6 * r2) * (0.4 + 0.6 * a), c, a, u * 0.7 + j, false);
    }
  }
  puffs(x, y, z, u, n, spread, size, life = 9, seed = 0, rise = 0.03, alpha = 0.8) {
    for (let j = 0; j < n; j++) {
      const r1 = frac(Math.sin(seed * 7.13 + j * 91.7) * 15731.743);
      const ang = 2 * Math.PI * (j + r1 * 0.7) / n;
      const d = spread * (1 - Math.exp(-u / 3)) * (0.6 + 0.8 * r1);
      const s = size * (0.55 + 0.45 * r1) * (0.5 + 0.5 * eout(u / 3)) * (1 + 0.04 * u);
      const a = alpha * clamp(1 - u / life) ** 1.3;
      this.put(this.puff, x + Math.cos(ang) * d, y + Math.sin(ang) * d, z + s * 0.5 + rise * u, s, WHITE, a);
    }
  }

  // pieces：塌陷碎块（SceneTerrain.pieces 的 o），没有就不冒尘土
  apply(anim, gt, pieces = null) {
    this.reset();
    const rp = anim.rp, Y = lin('#FFE066');
    for (const [T, ty, P] of anim.static_fx) {
      const u = gt - T;
      if (ty === 'respawn' && u >= -12 && u < 0) {
        // 复活前：复活点上落下一道队伍色的光柱，越来越亮、越来越细，地上一圈往里收
        const [x, y] = P.pos, tc = this.teamc[(P.team || 0) % 2], w = clamp((u + 12) / 10);
        this.beamAt(x, y, 0.75 - 0.35 * w, 4.5, mixc(tc, WHITE, 0.35), 0.55 * w);
        this.put(this.ring, x, y, 0.04, 1.3 - 0.75 * w, mixc(tc, WHITE, 0.3), 0.9 * w);
        continue;
      }
      if (u < 0 || u > 44) continue;
      const [x, y] = P.pos, seat = P.seat || 0;
      const c = seat < this.col.length ? this.col[seat] : WHITE, cl = mixc(c, WHITE, 0.45);
      if (ty === 'land' && u < 10) {
        this.put(this.ring, x, y, 0.03, 0.4 + 1.1 * eout(u / 6), WHITE, 0.75 * clamp(1 - u / 7));
        this.puffs(x, y, 0, u, 5, 0.7, 0.34, 9, seat);
      } else if (ty === 'dust' && u < 9) this.puffs(x, y, 0, u, 3, 0.35, 0.3 * (P.size || 1), 8, T);
      else if (ty === 'turn' && u < 6) this.put(this.ring, x, y, FLY_Z + 0.2, 0.3 + 0.55 * eout(u / 5), cl, 0.8 * clamp(1 - u / 6));
      else if (ty === 'catch' && u < 6) {
        this.put(this.ring, x, y, 0.75, 0.75 - 0.5 * eout(u / 4), cl, 0.85 * clamp(1 - u / 5));
        this.sparks(x, y, 0.8, u, 4, 0.22, WHITE, T, 5, 0.1);
      } else if (ty === 'drop' && u < 9) {
        this.put(this.star, x, y, 0.3, 0.42 * bump(u / 3) ** 0.5, mixc(Y, WHITE, 0.5), clamp(1 - u / 3), T, false);
        this.puffs(x, y, 0, u, 3, 0.3, 0.24, 8, T);
      } else if (ty === 'clash' && u < 10) {
        const [a, b] = P.seats;
        const ca = this.col[a] || WHITE, cb = this.col[b] || WHITE;
        this.put(this.disc, x, y, FLY_Z, 0.95 * eout(u / 1), WHITE, 0.6 * clamp(1 - u / 2.6));
        this.put(this.star, x, y, FLY_Z + 0.02, 1.05 * eout(u / 1.2) * (1 - 0.3 * ease((u - 2) / 4)), mixc(Y, WHITE, 0.6), clamp(1 - (u - 2.2) / 3.2), 0.4 + T, false);
        this.put(this.ring, x, y, FLY_Z, 0.4 + 1.3 * eout(u / 5), WHITE, 0.8 * clamp(1 - u / 6) ** 1.5);
        this.sparks(x, y, FLY_Z, u, 10, 0.5, mixc(ca, WHITE, 0.25), T, 9, 0.17, mixc(cb, WHITE, 0.25));
      } else if (ty === 'hit' && u < 9) {
        const z = 0.6;
        if (!P.sliced) {
          this.put(this.disc, x, y, z, 1.05 * eout(u / 1), WHITE, 0.38 * clamp(1 - u / 2.8));
          this.put(this.star, x, y, z + 0.02, 1.3 * eout(u / 1), mixc(c, WHITE, 0.2), clamp(1 - (u - 2.4) / 3), T * 1.7, false);
          this.put(this.star, x, y, z + 0.05, 0.72 * eout(u / 1), WHITE, clamp(1 - (u - 2) / 2.6), T * 1.7 + 0.39, false);
        }
        this.put(this.ring, x, y, 0.05, 0.5 + 1.5 * eout(u / 5), cl, 0.85 * clamp(1 - u / 6.5) ** 1.5);
        this.sparks(x, y, z, u, 8, 0.55, cl, T, 8, 0.17, WHITE);
      } else if (ty === 'slice' && u < 13) {
        const d = P.dir, sd = P.side, ang = Math.atan2(-d[1], d[0]);
        const vc = P.victim < this.col.length ? this.col[P.victim] : WHITE;
        const zc = SLICE_FX_Z, L = 0.5 + 1.9 * eout(u / 0.7), a = clamp(1 - (u - 1.4) / 2.6);
        this.put(this.streak, x + d[0] * 0.25, y + d[1] * 0.25, zc, L, mixc(c, WHITE, 0.55), a * 0.9, ang, true, 0.2);
        this.put(this.streak, x + d[0] * 0.25, y + d[1] * 0.25, zc + 0.02, L * 0.92, WHITE, a, ang, true, 0.075);
        this.put(this.ring, x, y, zc, 0.35 + 0.75 * eout(u / 2.2), WHITE, 0.7 * clamp(1 - u / 3.2) ** 1.4);
        const pale = mixc(vc, WHITE, 0.7);
        for (let j = 0; j < 9; j++) {
          const r1 = frac(Math.sin(T * 5.3 + j * 12.9898) * 43758.5453), r2 = frac(Math.sin(T * 2.1 + j * 4.51) * 9631.77);
          const along = (r1 - 0.3) * 0.25 + 0.11 * u * (0.4 + r2), sg = j % 2 ? 1 : -1;
          const out = sg * (0.04 + 0.05 * u * (0.3 + r2));
          let zz = zc + (0.08 + 0.1 * r1) * u - 0.016 * u * u;
          const ob = this.cube.take(); if (!ob) break;
          if (zz < 0.05) zz = 0.05;
          const k = clamp(1 - (u - 8) / 4.5);
          if (k <= 0.01) { ob.visible = false; continue; }
          const sz = 0.088 * (0.6 + 0.6 * r2) * k;
          ob.position.set(x + d[0] * along + sd[0] * out, -(y + d[1] * along + sd[1] * out), zz);
          if (zz > 0.06) ob.rotation.set(u * (0.3 + r1), u * (0.2 + r2), j); else ob.rotation.set(0, 0, j);
          ob.scale.set(sz, sz, sz);
          const cc = j % 3 ? pale : vc;
          ob.material.color.setRGB(cc[0], cc[1], cc[2]); ob.material.emissive.setRGB(cc[0] * 0.25, cc[1] * 0.25, cc[2] * 0.25);
        }
      } else if (ty === 'kickhit' && u < 7) {
        this.put(this.star, x, y, 0.55, 0.8 * eout(u / 1.2), WHITE, clamp(1 - (u - 1.8) / 2.5), T, false);
        this.put(this.ring, x, y, 0.05, 0.4 + 1.0 * eout(u / 5), WHITE, 0.7 * clamp(1 - u / 6));
        this.sparks(x, y, 0.55, u, 5, 0.4, Y, T, 6, 0.13);
      } else if (ty === 'slash' && u < 7) {
        const d = P.dir, ang = Math.atan2(-d[1], d[0]);
        const reach = ('SLASH_REACH' in rp.cfg ? rp.cfg.SLASH_REACH : 1500) / 1000;
        const sw = eout(u / 2), a = clamp(u / 0.5) * clamp(1 - (u - 2) / 4.5);
        this.put(this.arc, x, y, 0.55, reach * (0.86 + 0.14 * sw), WHITE, a, ang + (38 - 60 * sw) * Math.PI / 180);
        this.put(this.arc, x, y, 0.5, reach * (0.74 + 0.16 * sw), cl, a * 0.8, ang + (52 - 60 * sw) * Math.PI / 180);
      } else if (ty === 'kick' && u < 5) {
        const d = P.dir, ang = Math.atan2(-d[1], d[0]), a = clamp(u / 0.4) * clamp(1 - (u - 1.2) / 3.2);
        this.put(this.arc, x, y, 0.4, 1.15 * (0.7 + 0.3 * eout(u / 2)), WHITE, a * 0.85, ang - 0.15, true, 0.75);
      } else if (ty === 'poof' && u < 10) {
        const z = P.z ?? 0.5, sz = P.size ?? 1;
        this.puffs(x, y, z - 0.25, u, 6, 0.55 * sz, 0.5 * sz, 9, T, 0.02, 0.95);
        this.put(this.ring, x, y, z, (0.3 + 1.0 * eout(u / 6)) * sz, cl, 0.8 * clamp(1 - u / 7), 0, false);
        this.sparks(x, y, z, u, 7, 0.45 * sz, cl, T, 9, 0.16 * sz, WHITE);
      } else if (ty === 'splash' && u < 16 && !P.liquid) {
        const z = P.z ?? -10.5;
        this.puffs(x, y, z - 0.4, u, 9, 1.6, 1.15, 15, T, 0.05, 0.95);
        this.puffs(x, y, z - 0.2, u * 0.7, 4, 0.5, 0.8, 11, T + 3, 0.09, 0.9);
        this.put(this.ring, x, y, z + 0.1, 0.6 + 2.2 * eout(u / 6), WHITE, 0.75 * clamp(1 - u / 8) ** 1.3);
      } else if (ty === 'fallcry' && u < 7) this.sparks(x, y, 1.35, u, 3, 0.16, WHITE, T, 6, 0.11, null, 1.0);
      else if (ty === 'confetti') {
        const cols = [c, WHITE, Y, lin('#FF8FA8'), lin('#7FD8FF')];
        for (let j = 0; j < 40; j++) {
          const r1 = frac(Math.sin(seat * 3.1 + j * 12.9898) * 43758.5453), r2 = frac(Math.sin(seat * 1.7 + j * 4.51) * 9631.77);
          const ang = 2 * Math.PI * r1, sp = 0.05 + 0.13 * r2, d = sp * 12 * (1 - Math.exp(-u / 9));
          let zz = 1.2 + (0.42 + 0.3 * r1) * u - 0.013 * u * u * (0.7 + 0.5 * r2);
          if (zz < 0.03) zz = 0.03;
          const ob = this.conf.take(); if (!ob) break;
          ob.position.set(x + Math.cos(ang) * d, -(y + Math.sin(ang) * d), zz);
          if (zz > 0.04) ob.rotation.set(u * (0.5 + r1), u * (0.3 + r2), ang); else ob.rotation.set(0, 0, ang);
          ob.scale.set(1, 1, 1);
          const cc = cols[j % cols.length];
          ob.material.color.setRGB(cc[0], cc[1], cc[2]); ob.material.opacity = clamp(1 - (u - 34) / 10);
        }
      } else if (ty === 'respawn' && u < 12) {
        // 复活落地：光柱散开、地上一圈冲出去、几粒光点
        const tc = this.teamc[(P.team || 0) % 2];
        if (u < 5) this.beamAt(x, y, 0.4 + 0.5 * eout(u / 5), 4.5 * (1 - u / 5), mixc(tc, WHITE, 0.5), 0.6 * (1 - u / 5));
        this.put(this.ring, x, y, 0.04, 0.4 + 1.5 * eout(u / 6), mixc(tc, WHITE, 0.4), 0.9 * clamp(1 - u / 8) ** 1.3);
        this.sparks(x, y, 0.3, u, 8, 0.4, mixc(tc, WHITE, 0.5), T, 10, 0.13, WHITE, 1.2);
      } else if (ty === 'kinghit' && u < 16) {
        // 国王掉一条命：金色的大冲击环 + 金星 + 金色火花
        const G = lin('#FFC93A');
        this.put(this.ring, x, y, 0.05, 0.6 + 2.4 * eout(u / 7), G, 0.95 * clamp(1 - u / 10) ** 1.3);
        this.put(this.ring, x, y, 0.9, 0.4 + 1.4 * eout(u / 5), WHITE, 0.8 * clamp(1 - u / 7), 0, false);
        this.put(this.star, x, y, 1.0, 1.5 * eout(u / 1.2), mixc(G, WHITE, 0.3), clamp(1 - (u - 2.5) / 3.5), T, false);
        this.sparks(x, y, 1.0, u, 12, 0.7, G, T, 12, 0.18, WHITE, 1.0);
      } else if (ty === 'capture' && u < 22) {
        // 翻点完成：圈的边上冲出一圈新主人颜色的环，绕圈一串光点
        const tc = this.teamc[(P.team || 0) % 2], r = P.r || 2;
        this.put(this.ring, x, y, 0.05, r * (1 + 0.5 * eout(u / 8)), mixc(tc, WHITE, 0.2), 0.95 * clamp(1 - u / 14) ** 1.2);
        this.put(this.disc, x, y, 0.03, r * 1.05, mixc(tc, WHITE, 0.5), 0.35 * clamp(1 - u / 6));
        for (let j = 0; j < 10; j++) {
          const a = 2 * Math.PI * j / 10 + u * 0.08, zz = 0.1 + 0.09 * u - 0.004 * u * u;
          if (zz > 0.04) this.put(this.spark, x + r * Math.cos(a), y + r * Math.sin(a), zz, 0.17 * clamp(1 - u / 20), mixc(tc, WHITE, 0.4), clamp(1 - u / 20), u * 0.5 + j, false);
        }
      } else if (ty === 'splash' && u < 16 && P.liquid) {
        // 掉进液面：岩浆 = 橙红的火星 + 暗红的烟；水 = 白色水花 + 一圈涟漪
        const z = P.z ?? -0.55;
        if (P.liquid === 'lava') {
          const O = lin('#FF8A2A'), R2 = lin('#FFD45A');
          this.sparks(x, y, z + 0.1, u, 14, 0.5, O, T, 13, 0.16, R2, 1.6);
          this.put(this.ring, x, y, z + 0.04, 0.4 + 1.5 * eout(u / 6), R2, 0.9 * clamp(1 - u / 9) ** 1.3);
          this.put(this.disc, x, y, z + 0.03, 0.9 * eout(u / 2), lin('#FFF0B0'), 0.7 * clamp(1 - u / 5));
          this.puffs(x, y, z, u, 5, 0.6, 0.55, 15, T, 0.07, 0.6);
        } else {
          const Wt = lin('#E8F6FF');
          this.sparks(x, y, z + 0.1, u, 12, 0.35, Wt, T, 11, 0.15, WHITE, 1.8);
          this.put(this.ring, x, y, z + 0.03, 0.4 + 1.8 * eout(u / 7), WHITE, 0.85 * clamp(1 - u / 11) ** 1.3);
          this.put(this.ring, x, y, z + 0.03, 0.2 + 1.0 * eout(u / 9), WHITE, 0.6 * clamp(1 - (u - 3) / 10));
          this.puffs(x, y, z, u * 0.8, 6, 0.5, 0.45, 12, T, 0.05, 0.85);
        }
      }
    }
    for (const [ty, P] of anim.continuous_fx(gt)) {
      const [x, y] = P.pos, c = this.col[P.seat], u = P.age;
      if (ty === 'stun') {
        for (let j = 0; j < 3; j++) { const a = u * 0.55 + j * 2.094; this.put(this.spark, x + 0.34 * Math.cos(a), y + 0.34 * Math.sin(a) * 0.9, 1.5 + 0.05 * Math.sin(a * 2), 0.14, Y, 0.95, u * 0.4 + j, false); }
      } else if (ty === 'recall') {
        const cl = mixc(c, WHITE, 0.4);
        for (let j = 0; j < 2; j++) { const ph = ((u / 6) + j * 0.5) % 1; this.put(this.ring, x, y, 0.04 + ph, 0.85 - 0.6 * ph, cl, 0.75 * Math.sin(Math.PI * ph)); }
        const [dx, dy] = P.dart, ph = (u / 5) % 1;
        this.put(this.ring, dx, dy, 0.03, 0.25 + 0.5 * ph, cl, 0.8 * (1 - ph));
        this.put(this.disc, dx, dy, 0.02, 0.55, cl, 0.25 + 0.25 * P.prog);
      }
    }
    if (pieces) {
      const ws = rp.wave_steps, warnN = 'COL_WARN' in rp.cfg ? rp.cfg.COL_WARN : 30;
      if (!this._dmax) { this._dmax = {}; for (const p of pieces) this._dmax[p.wave] = Math.max(this._dmax[p.wave] || 0.1, p.d_in); }
      const dmax = this._dmax;
      for (const p of pieces) {
        const w = p.wave; if (w >= ws.length) continue;
        const u = gt - ws[w], r = p.rnd;
        if (u < -warnN || u > 20) continue;
        if (u < -warnN + 5) this.puffs(p.cx, p.cy, 0, u + warnN, 1, 0.1, 0.3, 5, p.id, 0.03, 0.5);
        const delay = (1 - p.d_in / dmax[w]) * 5 + r[0] * 2, v = u - delay;
        if (v >= 0 && v < 10) this.puffs(p.cx, p.cy, -0.15, v, 2, 0.5, 0.5 + 0.25 * r[1], 10, p.id, -0.02, 0.75);
      }
    }
  }
}
