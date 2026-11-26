// 照正片 src\modes_bl.py 移植（第 12 集 5v5 占点）：地上三个圆圈。每个点三层，都是贴地的自发光半透明网格：
//   底    整个圈里一层很淡的归属色（中立 = 白）
//   边    圈的边线，归属色、实；翻点完成那一下闪白；外面再一道白边（在什么地面上都分得出）
//   进度  边线内侧一道弧，从 12 点钟方向顺时针长出来，颜色 = 正在翻点的队伍；进度清零就收回去
// 字母（A B C）和顶部比分由顶栏画。
import * as THREE from 'three';

const SEG = 72;
function emitMat() {
  return new THREE.MeshBasicMaterial({ transparent: true, depthWrite: false, side: THREE.DoubleSide, color: new THREE.Color(1, 1, 1) });
}
function annulus(rIn, rOut, z) {
  const V = [], I = [];
  for (let k = 0; k < SEG; k++) { const a = 2 * Math.PI * k / SEG; V.push(rOut * Math.cos(a), rOut * Math.sin(a), z, rIn * Math.cos(a), rIn * Math.sin(a), z); }
  for (let k = 0; k < SEG; k++) { const k2 = (k + 1) % SEG; I.push(2 * k, 2 * k2, 2 * k2 + 1, 2 * k, 2 * k2 + 1, 2 * k + 1); }
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.Float32BufferAttribute(V, 3)); g.setIndex(I);
  return g;
}
function disc(r, z) {
  const V = [0, 0, z], I = [];
  for (let k = 0; k < SEG; k++) { const a = 2 * Math.PI * k / SEG; V.push(r * Math.cos(a), r * Math.sin(a), z); }
  for (let k = 0; k < SEG; k++) I.push(0, 1 + k, 1 + (k + 1) % SEG);
  const g = new THREE.BufferGeometry(); g.setAttribute('position', new THREE.Float32BufferAttribute(V, 3)); g.setIndex(I);
  return g;
}

class Arc {
  constructor(rIn, rOut, z) {
    this.rIn = rIn; this.rOut = rOut; this.z = z; this.frac = -1;
    const n = (SEG + 1) * 2, I = [];
    for (let k = 0; k < SEG; k++) I.push(2 * k, 2 * k + 2, 2 * k + 3, 2 * k, 2 * k + 3, 2 * k + 1);
    this.g = new THREE.BufferGeometry(); this.g.setAttribute('position', new THREE.BufferAttribute(new Float32Array(n * 3), 3)); this.g.setIndex(I);
    this.ms = new THREE.Mesh(this.g, emitMat()); this.ms.renderOrder = 2; this.ms.frustumCulled = false;
  }
  set(frac) {
    if (frac <= 0.004) { this.ms.visible = false; this.frac = 0; return; }
    this.ms.visible = true;
    if (Math.abs(frac - this.frac) < 1e-4) return;
    this.frac = frac;
    const P = this.g.attributes.position.array;
    for (let k = 0; k <= SEG; k++) {
      const a = Math.PI / 2 - 2 * Math.PI * frac * k / SEG, c = Math.cos(a), s = Math.sin(a);
      P.set([this.rOut * c, this.rOut * s, this.z, this.rIn * c, this.rIn * s, this.z], k * 6);
    }
    this.g.attributes.position.needsUpdate = true;
  }
}

export class Points {
  /** root：Blender 坐标的根节点；teamColors：原始队号 0 / 1 的显示色 */
  constructor(root, rp, teamColors) {
    this.rp = rp;
    this.tc = teamColors.map((h) => new THREE.Color(h));
    this.items = [];
    for (const [, x, y, r] of rp.points) {
      const grp = new THREE.Group(); grp.position.set(x, -y, 0); root.add(grp);
      const mk = (g, ro) => { const m = new THREE.Mesh(g, emitMat()); m.renderOrder = ro; grp.add(m); return m; };
      const base = mk(disc(r, 0.006), 1), edge = mk(annulus(r - 0.11, r + 0.02, 0.010), 2), edge2 = mk(annulus(r + 0.02, r + 0.07, 0.009), 2);
      const arc = new Arc(r - 0.42, r - 0.17, 0.012); grp.add(arc.ms);
      edge2.material.color.setRGB(1, 1, 1); edge2.material.opacity = 0.7;
      this.items.push({ grp, base, edge, arc, r });
    }
  }
  apply(gt) {
    const rp = this.rp, c = gt >= 0 ? rp.ctl_at(gt) : null, cap = rp.cfg.CAPTURE ?? 20;
    this.items.forEach((it, i) => {
      const own = c ? c.owner[i] : -1, tk = c ? c.taking[i] : -1;
      const pr = c ? (c.prog[i] + (tk >= 0 && c.prog[i] > 0 ? gt - Math.trunc(gt) : 0)) / cap : 0;
      const col = own >= 0 ? this.tc[own] : new THREE.Color(1, 1, 1);
      let fl = 0;
      for (const [t, j] of rp.captures) { const u = gt - (t + 1); if (j === i && u >= 0 && u < 12) fl = Math.max(fl, 1 - u / 12); }
      it.base.material.color.copy(col).multiplyScalar(1.4); it.base.material.opacity = (own >= 0 ? 0.2 : 0.1) + 0.25 * fl;
      it.edge.material.color.copy(col).lerp(new THREE.Color(1, 1, 1), fl * 0.8).multiplyScalar(1.4); it.edge.material.opacity = 0.95;
      if (tk >= 0) { it.arc.ms.material.color.copy(this.tc[tk]).multiplyScalar(1.4); it.arc.ms.material.opacity = 0.95; }
      it.arc.set(tk >= 0 ? Math.min(1, pr) : 0);
    });
  }
}
