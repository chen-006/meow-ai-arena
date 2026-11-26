// 体素小人：读 public/gen/ep11/vox/<名字>.json（正片 voxkit.Model.to_json 导出的格式，第 12 集 src\voxchars 的最新版，13 人含小 Gemini），
// 按部件合并成网格。体素坐标：x 横、y 向后（正面朝 -y）、z 向上；一个方块 = unit。
// 照第 11 集网页查看器的 vox.js 移植；另加刺王的像素金冠（chars_voxel.add_crown）。
import * as THREE from 'three';
import { genUrl } from '../../../common/wasm';

/** 已载入的体素数据：key -> JSON */
export const VOX = {};
const loading = {};

/** 按需载入几个角色（已载入的直接跳过） */
export function loadVox(keys) {
  return Promise.all([...new Set(keys)].map((k) => {
    if (VOX[k]) return null;
    return (loading[k] ??= fetch(genUrl('ep11/vox/' + k + '.json')).then((r) => {
      if (!r.ok) throw new Error('角色模型 ' + k + ' 下载失败');
      return r.json();
    }).then((d) => { VOX[k] = d; }));
  }));
}

const DIRS = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]];
const QUAD = [
  [[1, 0, 0], [1, 1, 0], [1, 1, 1], [1, 0, 1]],
  [[0, 0, 0], [0, 0, 1], [0, 1, 1], [0, 1, 0]],
  [[0, 1, 0], [0, 1, 1], [1, 1, 1], [1, 1, 0]],
  [[0, 0, 0], [1, 0, 0], [1, 0, 1], [0, 0, 1]],
  [[0, 0, 1], [1, 0, 1], [1, 1, 1], [0, 1, 1]],
  [[0, 0, 0], [0, 1, 0], [1, 1, 0], [1, 0, 0]],
];
export const mapV = (x, y, z) => [-y, z, -x];

const colCache = {};
function col(h) {
  let c = colCache[h];
  if (!c) { c = new THREE.Color(h); colCache[h] = c; }
  return c;
}

// 把一组方块 / 贴片 / 面漆变成三角网格。filter(x,y,z) 可选：只取部分方块（切成两半用）。
// pivot：体素坐标里的枢轴；返回的顶点已减去枢轴、乘 unit。outline > 0 时另返回一个向外推开的外壳（画深色描边，背面渲染）。
export function buildGeom(part, unit, pivot, opts = {}) {
  const filter = opts.filter;
  const MV = opts.map || mapV;
  const vox = new Map();
  for (const v of part.voxels || []) {
    if (filter && !filter(v[0], v[1], v[2])) continue;
    vox.set(v[0] + ',' + v[1] + ',' + v[2], v[3]);
  }
  const paint = new Map();
  for (const p of part.paint || []) paint.set(p[0] + ',' + p[1] + ',' + p[2] + ',' + p[3], p[4]);
  const P = [], N = [], C = [];
  const px = pivot[0], py = pivot[1], pz = pivot[2];
  const corners = [];
  const cornerDirs = new Map();
  const va = new THREE.Vector3(), vb = new THREE.Vector3(), vc = new THREE.Vector3(), vn = new THREE.Vector3();
  function quad(pts, d, c) {
    const n = MV(...DIRS[d]);
    const q = pts.map((p) => MV((p[0] - px) * unit, (p[1] - py) * unit, (p[2] - pz) * unit));
    va.set(...q[0]); vb.set(...q[1]).sub(va); vc.set(...q[2]).sub(va);
    const order = vb.cross(vc).dot(vn.set(...n)) >= 0 ? [0, 1, 2, 0, 2, 3] : [0, 2, 1, 0, 3, 2];
    for (const i of order) { P.push(...q[i]); N.push(...n); C.push(c.r, c.g, c.b); }
  }
  for (const [k, h] of vox) {
    const [x, y, z] = k.split(',').map(Number);
    for (let d = 0; d < 6; d++) {
      const D = DIRS[d];
      if (vox.has((x + D[0]) + ',' + (y + D[1]) + ',' + (z + D[2]))) continue;
      const c = col(paint.get(k + ',' + d) || h);
      const pts = QUAD[d].map((o) => [x + o[0], y + o[1], z + o[2]]);
      quad(pts, d, c);
      if (opts.outline) {
        corners.push([pts, d, c]);
        for (const p of pts) {
          const kk = p.join(',');
          let s = cornerDirs.get(kk); if (!s) { s = new Set(); cornerDirs.set(kk, s); }
          s.add(d);
        }
      }
    }
  }
  for (const s of part.slabs || []) {
    const [x0, x1, y0, y1, z0, z1, h] = s;
    if (filter && !filter((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)) continue;
    const c = col(h);
    for (let d = 0; d < 6; d++) {
      const pts = QUAD[d].map((o) => [o[0] ? x1 : x0, o[1] ? y1 : y0, o[2] ? z1 : z0]);
      quad(pts, d, c);
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(P, 3));
  g.setAttribute('normal', new THREE.Float32BufferAttribute(N, 3));
  g.setAttribute('color', new THREE.Float32BufferAttribute(C, 3));
  let og = null;
  if (opts.outline && corners.length) {
    const w = opts.outline;
    const OPn = [], OCn = [];
    for (const [pts, , fc] of corners) {
      const q = pts.map((p) => {
        const s = cornerDirs.get(p.join(','));
        let ox = 0, oy = 0, oz = 0;
        for (const dd of s) { ox += DIRS[dd][0]; oy += DIRS[dd][1]; oz += DIRS[dd][2]; }
        ox = Math.max(-1, Math.min(1, ox)); oy = Math.max(-1, Math.min(1, oy)); oz = Math.max(-1, Math.min(1, oz));
        return MV((p[0] - px) * unit + ox * w, (p[1] - py) * unit + oy * w, (p[2] - pz) * unit + oz * w);
      });
      for (const i of [0, 1, 2, 0, 2, 3]) { OPn.push(...q[i]); OCn.push(fc.r, fc.g, fc.b); }
    }
    og = new THREE.BufferGeometry();
    og.setAttribute('position', new THREE.Float32BufferAttribute(OPn, 3));
    og.setAttribute('color', new THREE.Float32BufferAttribute(OCn, 3));
  }
  return { geom: g, outline: og };
}

// 三叶镖（照 chars_voxel.dart_part）：平放在 XZ 平面，一格厚
export function dartPart(hex, blades = 3) {
  const mix = (h, w, to = 255) => '#' + [1, 3, 5].map((i) => Math.round(parseInt(h.slice(i, i + 2), 16) * (1 - w) + to * w).toString(16).padStart(2, '0')).join('');
  const main = hex, light = mix(hex, 0.5), deep = mix(hex, 0.28, 20);
  const CELL = 0.058, n = 6, voxels = [];
  for (let i = -n; i <= n; i++) for (let j = -n; j <= n; j++) {
    const x = i * CELL, y = j * CELL, r = Math.hypot(x, y);
    if (r < 0.085) { voxels.push([i, j, 0, r > 0.03 ? '#FFFFFF' : main]); continue; }
    let best = null;
    for (let b = 0; b < blades; b++) {
      const a0 = 2 * Math.PI * b / blades;
      for (let k = 0; k <= 40; k++) {
        const t = k / 40, rr = 0.05 + 0.26 * t, ang = a0 + 0.8 * Math.pow(t, 1.25);
        const d = Math.hypot(x - rr * Math.cos(ang), y - rr * Math.sin(ang));
        const w = 0.074 * (1 - 0.32 * t);
        if (d < w && (!best || d / w < best[0])) best = [d / w, t, (x * Math.cos(ang) + y * Math.sin(ang)) - rr];
      }
    }
    if (best) voxels.push([i, j, 0, best[1] > 0.8 ? light : (best[2] < -0.012 ? deep : main)]);
  }
  return { voxels, paint: [], slabs: [], cell: CELL };
}

/** 刺王（第 12 集，照 chars_voxel.add_crown）：头顶一顶金色像素王冠、正面一颗队伍色宝石；返回改过头部的模型副本 */
export function withCrown(model, teamColor) {
  const m = { ...model, parts: { ...model.parts } };
  const H0 = model.parts.head;
  const vox = new Map(H0.voxels.map((v) => [v[0] + ',' + v[1] + ',' + v[2], v[3]]));
  const paint = new Map((H0.paint || []).map((p) => [p.slice(0, 4).join(','), p[4]]));
  const keys = [...vox.keys()].map((k) => k.split(',').map(Number));
  let ks = keys.filter((k) => k[0] >= -3 && k[0] <= 2);
  if (!ks.length) ks = keys;
  const xs = keys.map((k) => k[0]), ys = keys.map((k) => k[1]);
  const x0 = Math.max(Math.min(...xs), -4), x1 = Math.min(Math.max(...xs) + 1, 4);
  const y0 = Math.min(...ys), y1 = Math.max(...ys) + 1;
  const zc = ks.map((k) => k[2]).sort((a, b) => a - b);
  const ztop = zc[Math.floor(zc.length * 0.97)] + 1;
  const gold = '#FFC93A', hi = '#FFE68A', sh = '#D99A1C', gem = teamColor || '#E8384F';
  const xa = x0, xb = x1;
  const ya = y1 - y0 > 6 ? y0 + 1 : y0, yb = y1 - y0 > 6 ? y1 - 1 : y1;
  const z0 = ztop;
  const set = (x, y, z, c) => vox.set(x + ',' + y + ',' + z, c);
  for (let x = xa; x < xb; x++) for (let y = ya; y < yb; y++) {
    if (x === xa || x === xb - 1 || y === ya || y === yb - 1) { set(x, y, z0, sh); set(x, y, z0 + 1, gold); }
  }
  const mx = (xa + xb - 1) / 2, my = (ya + yb - 1) / 2;
  const tips = new Map();
  for (const x of [xa, xb - 1]) for (const y of [ya, yb - 1]) tips.set(x + ',' + y, 1);
  for (const x of [Math.floor(mx), Math.ceil(mx)]) { tips.set(x + ',' + ya, 2); tips.set(x + ',' + (yb - 1), 2); }
  for (const y of [Math.floor(my), Math.ceil(my)]) { if (!tips.has(xa + ',' + y)) tips.set(xa + ',' + y, 1); if (!tips.has((xb - 1) + ',' + y)) tips.set((xb - 1) + ',' + y, 1); }
  for (const [k, h] of tips) {
    const [x, y] = k.split(',').map(Number);
    for (let dz = 0; dz < h; dz++) set(x, y, z0 + 2 + dz, dz === h - 1 ? hi : gold);
  }
  for (const x of [Math.floor(mx), Math.ceil(mx)]) { set(x, ya, z0 + 1, gem); paint.set(x + ',' + ya + ',' + (z0 + 1) + ',3', gem); }
  m.parts.head = { ...H0,
    voxels: [...vox.entries()].map(([k, c]) => [...k.split(',').map(Number), c]),
    paint: [...paint.entries()].map(([k, c]) => [...k.split(',').map(Number), c]) };
  return m;
}
