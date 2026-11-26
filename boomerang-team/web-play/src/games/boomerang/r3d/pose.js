// 照正片 src\pose.py（第 12 集版）逐行移植：正片的姿势、镖、特效时刻，同样的常量、同样的时间曲线。
// 底子是第 11 集网页查看器的 pose.js，加上第 12 集的改动（每条命一个子 Animator、复活落地、国王无敌闪烁、掉进岩浆 / 水）。
// 坐标：地图坐标（米，y 朝屏幕下方）；heading 是度（地图坐标里的方位角，0 = +x）。
// 实时对局：refresh() 在每一步新帧接上以后增量重建动作表、出局、特效时刻（几千条事件以内，零点几毫秒）。
import { ST_FREE, ST_THROW, ST_THROWN, D_HAND, D_BACK, D_GROUND, D_RISE, D_GONE, D_CALL, FLYING, CELL_GRASS, CELL_PIT } from './rp.js';
import * as LQ from './liquid.js';

export const FLY_Z = 0.66;
export const INTRO = 46;
export const OUTRO = 56;
export const SLICE_LIFE = 19.0;
export const SLICE_TILT = 0.0;
export const SLICE_POOF = 16.0;
export const SLICE_UP_GO = 1.3;
export const SLICE_LOW_GO = 0.32;
export const FALL_G = 0.05;
export const FALL_HANG = { walk: 6.0, kick: 0.4, ground: 0.0 };
export const FALL_SPLASH_Z = -10.5;

export const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x);
export const ease = (t) => { t = clamp(t); return t * t * (3 - 2 * t); };
export const eout = (t) => { t = clamp(t); return 1 - (1 - t) ** 3; };
export const bump = (t) => Math.sin(Math.PI * clamp(t));
export function lerp(a, b, w) {
  if (Array.isArray(a)) return a.map((x, i) => x + (b[i] - x) * w);
  return a + (b - a) * w;
}
const RAD = Math.PI / 180;
const isNum = (v) => typeof v === 'number';
export function mix(p, target, w) {
  if (w <= 0) return;
  for (const [k, v] of Object.entries(target)) {
    if (isNum(v) || Array.isArray(v)) {
      if (k in p) p[k] = lerp(p[k], v, w);
      else p[k] = lerp(Array.isArray(v) ? v.map(() => 0) : (k === 'sx' || k === 'sy' || k === 'sz' ? 1 : 0), v, w);
    } else if (w > 0.5) p[k] = v;
  }
}

export function slice_curve(u) {
  u = Math.max(0, u);
  const carry = 0.11 * (1 - Math.exp(-u / 0.3)) + (SLICE_UP_GO - 0.11) * (1 - Math.exp(-Math.max(0, u - 0.4) / 2.6));
  const lift = 0.08 * bump(u / 1.8);
  const v = u - 0.9;
  let th_up;
  if (v <= 0) th_up = 0.05 * clamp(u / 0.6);
  else if (v < 3.6) th_up = 0.05 + (Math.PI / 2 + 0.12 - 0.05) * ease(v / 3.6) ** 1.3;
  else th_up = Math.PI / 2 + 0.12 * Math.cos((v - 3.6) * 1.6) * Math.exp(-(v - 3.6) / 1.5);
  const yaw_up = 0.35 * eout(Math.max(0, u - 0.9) / 6);
  const w = u - 3.0;
  let th_low;
  if (w <= 0) th_low = 0.045 * Math.sin(u * 2.6) * clamp(u / 0.8);
  else if (w < 2.8) th_low = (Math.PI / 2) * ease(w / 2.8) ** 1.7;
  else th_low = Math.PI / 2 - 0.1 * Math.abs(Math.sin((w - 2.8) * 1.5)) * Math.exp(-(w - 2.8) / 1.2);
  return { carry, lift, th_up, yaw_up, leave: 0.9, th_low, scale: 1 - ease((u - SLICE_POOF) / (SLICE_LIFE - SLICE_POOF)),
    flash: 0.75 * clamp(1 - (u - 0.2) / 0.9) };
}

export const ang_of = (dx, dy) => Math.atan2(dy, dx) / RAD;
export const ang_lerp = (a, b, w) => { const d = (((b - a + 180) % 360) + 360) % 360 - 180; return a + d * w; };

export class Animator {
  /** life：第几条命（第 12 集占点有复活）；主 Animator 是第 0 条命，parent 给子 Animator 共用动作表 */
  constructor(rp, roster, life = 0, parent = null) {
    this.rp = rp; this.n = rp.n; this.cfg = rp.cfg; this.roster = roster;
    this.life = life; this.parent = parent;
    this.teams = rp.players.map((p) => p[1]);
    this.team_mode = rp.mode >= 2;
    if (!parent) {
      let order = [...Array(this.n).keys()];
      if (this.team_mode) {
        const a = order.filter((k) => this.teams[k] === 0), b = order.filter((k) => this.teams[k] === 1);
        const z = []; for (let i = 0; i < Math.min(a.length, b.length); i++) z.push(a[i], b[i]);
        order = z.concat(a.slice(b.length), b.slice(a.length));
      }
      const gap = this.n > 4 ? 2.2 : 3.2;
      this.t_land = {}; order.forEach((k, i) => { this.t_land[k] = -INTRO + 5 + i * gap; });
    } else this.t_land = parent.t_land;
    this.refresh();
  }
  cf(k, d) { return k in this.cfg ? this.cfg[k] : d; }

  /** 回放长了（实时对局每步一次）：重建依赖事件的一切 */
  refresh() {
    const rp = this.rp;
    if (!this.parent) {
      this._acts = []; this._land = [];
      for (let k = 0; k < this.n; k++) { this._acts.push(this._build_acts(k)); this._land.push(this._land_times(k)); }
    } else { this._acts = this.parent._acts; this._land = this.parent._land; }
    this.deaths = rp.deaths_of_life(this.life);
    this.sliced = new Set(Object.entries(this.deaths).filter(([, d]) => d[1] === 1).map(([k]) => +k));
    this._di = {}; this._si = {}; this._lz = {};
    this.winners = this.life === 0 ? this._winners() : new Set();
    if (!this.parent) {
      this.lives = this.lives || [this];
      this.lives[0] = this;
      for (let i = 1; i < rp.max_lives; i++) {
        if (!this.lives[i]) this.lives[i] = new Animator(rp, this.roster, i, this);
        else this.lives[i].refresh();
      }
      this.lives.length = Math.max(1, rp.max_lives);
      this.static_fx = this._build_fx();
      for (const A of this.lives.slice(1)) this.static_fx = this.static_fx.concat(A._build_fx());
      this.static_fx.sort((a, b) => a[0] - b[0]);
    }
  }

  L(k, gt) {
    const lives = (this.parent || this).lives;
    if (lives.length === 1) return this;
    return lives[Math.min(this.rp.life_index(k, gt), lives.length - 1)];
  }
  all_deaths() {
    const out = [];
    for (const A of (this.parent || this).lives) for (const k of Object.keys(A.deaths)) out.push([A, +k]);
    return out;
  }

  _winners() {
    const rp = this.rp;
    if (!rp.over || rp.winner < 0) return new Set();
    const last = rp.states[rp.T].chars;
    if (this.team_mode) return new Set([...Array(this.n).keys()].filter((k) => last[k].alive && this.teams[k] === rp.winner));
    return last[rp.winner] && last[rp.winner].alive ? new Set([rp.winner]) : new Set();
  }

  _build_acts(k) {
    const ev = this.rp.ev[k];
    const A = { throw: [], slash: [], kick: [], dash: [], recall: [], stun: [], catch: [], pick: [] };
    const rel = new Set(ev.release.map((e) => e[0]));
    for (const [t0, ax, ay] of ev.throw) { const tr = t0 + this.cf('THROW_PRE', 2) - 1; A.throw.push({ t0, T: rel.has(tr) ? tr + 1 : null, dir: [ax, ay] }); }
    const sw = new Map(ev.swing.map((e) => [e[0] + ',' + e[1], e[2]]));
    for (const [t0, ax, ay] of ev.slash) { const ts = t0 + this.cf('SLASH_PRE', 2) - 1; A.slash.push({ t0, T: sw.has(ts + ',1') ? ts + 1 : null, dir: [ax, ay], hit: sw.get(ts + ',1') || 0 }); }
    for (const [t0, ax, ay] of ev.kick) { const ts = t0 + this.cf('KICK_PRE', 2) - 1; A.kick.push({ t0, T: sw.has(ts + ',0') ? ts + 1 : null, dir: [ax, ay], hit: sw.get(ts + ',0') || 0 }); }
    const stops = ev.stop.map((e) => e[0]).concat(ev.slash.map((e) => e[0] - 1), ev.kick.map((e) => e[0] - 1), ev.kicked.map((e) => e[0])).sort((a, b) => a - b);
    for (const [t0, ax, ay] of ev.dash) {
      const pre = this.cf('DASH_PRE', 0), m0 = t0 + pre;
      const cut = stops.find((s) => t0 <= s && s < m0);
      if (cut !== undefined) { A.dash.push({ T0: cut + 1, T1: cut + 1, dir: [ax, ay] }); continue; }
      const DT = this.cf('DASH_T', 4);
      let t1 = stops.find((s) => m0 <= s && s <= m0 + DT); if (t1 === undefined) t1 = m0 + DT - 1;
      A.dash.push({ T0: m0, T1: t1 + 1, dir: [ax, ay] });
    }
    const ends = ev.call.map((e) => [e[0], true]).concat(ev.cancel.map((e) => [e[0], false])).sort((a, b) => a[0] - b[0] || (a[1] - b[1]));
    for (const [t0, dx, dy] of ev.recall) {
      const en = ends.find(([t]) => t > t0) || [t0 + this.cf('RECALL_PRE', 12), false];
      A.recall.push({ T0: t0 + 1, T1: en[0] + 1, ok: en[1], dart: [dx, dy] });
    }
    for (const [t0, kicker] of ev.kicked) A.stun.push({ T0: t0 + 1, T1: t0 + 1 + this.cf('KICK_STUN', 12), kicker });
    A.catch = ev.catch.map((e) => e[0] + 1);
    A.pick = ev.pick.map((e) => e[0] + 1);
    return A;
  }

  _land_times(k) {
    const rp = this.rp, out = [];
    for (let t = 1; t <= rp.T; t++) {
      const s = rp.states[t].darts[k].st;
      if ((s === D_GROUND || s === D_RISE) && FLYING.has(rp.states[t - 1].darts[k].st)) out.push(t);
    }
    return out;
  }

  face_vec(k, t) {
    const rp = this.rp;
    t = clamp(t, 0, rp.T);
    const i = Math.floor(t), a = t - i;
    const f0 = rp.states[i].chars[k].face / 100, f1 = rp.states[Math.min(i + 1, rp.T)].chars[k].face / 100;
    const f = ang_lerp(f0, f1, a) * RAD;
    return [Math.cos(f), Math.sin(f)];
  }
  heading(k, gt) {
    let sx = 0, sy = 0;
    for (const [dt, w] of [[-1.2, 0.2], [-0.6, 0.3], [0, 0.3], [0.5, 0.2]]) { const [x, y] = this.face_vec(k, gt + dt); sx += x * w; sy += y * w; }
    return ang_of(sx, sy);
  }
  // 实时对局里 gt + 0.6 常常还没算出来：沿最后一步的速度往前推一点，免得跑步动作在最新一步附近忽大忽小
  _pos_ahead(k, gt) {
    const rp = this.rp;
    if (gt <= rp.T || rp.over) return rp.char_pos(k, gt);
    const c = rp.states[rp.T].chars[k], u = Math.min(1.5, gt - rp.T);
    return [(c.x + c.vx * u) / 1000, (c.y + c.vy * u) / 1000];
  }
  speed(k, gt) {
    const [x0, y0] = this.rp.char_pos(k, gt - 0.6), [x1, y1] = this._pos_ahead(k, gt + 0.6);
    return Math.hypot(x1 - x0, y1 - y0) / 1.2;
  }
  hidden(k, gt) {
    const rp = this.rp;
    let tot = 0;
    for (const dt of [-1.5, -0.5, 0.5, 1.5]) {
      const t = Math.trunc(clamp(gt + dt, 0, rp.T));
      const c = rp.states[t].chars[k];
      if (!c.alive) continue;
      let opp = 0;
      for (let j = 0; j < this.n; j++) if (j !== k && (!this.team_mode || this.teams[j] !== this.teams[k]) && rp.states[t].chars[j].alive) opp |= 1 << j;
      if (opp && (c.vis & opp) === 0 && rp.cell_at(c.x / 1000, c.y / 1000)[0] === CELL_GRASS) tot += 0.25;
    }
    return tot;
  }
  dart_mode(k, gt) {
    const rp = this.rp;
    return gt >= rp.T ? rp.states[rp.T].darts[k].st : rp.states[Math.max(0, Math.trunc(gt))].darts[k].st;
  }

  char_pose(k, gt) {
    const A = this.L(k, gt);
    if (A !== this) return A.char_pose(k, gt);
    const rp = this.rp;
    const death = this.deaths[k];
    if (death && gt >= death[0] + 1) { const pp = this._dead_pose(k, gt, death); pp.life = this.life; return pp; }
    const [x, y] = rp.char_pos(k, gt);
    const v = 0 < gt && (gt < rp.T || !rp.over) ? this.speed(k, gt) : 0;
    const st = gt > 0 ? rp.S(gt).chars[k].st : ST_FREE;
    const rf = (st === ST_FREE || st === ST_THROW || st === ST_THROWN) ? clamp(v / 0.25, 0, 1.15) : 0;
    const ph = rp.dist_at(k, gt) / 0.8 * Math.PI;
    const s = Math.sin(ph), c = Math.cos(ph);
    const br = Math.sin(gt * 0.42 + k * 1.7);
    const p = { x, y, z: 0, heading: this.heading(k, gt), lean: 0.2 * rf, roll: 0.05 * s * rf, spin: 0,
      sx: 1, sy: 1, sz: 1 + 0.012 * br * (1 - rf), body_dz: 0.028 * Math.abs(s) * rf - 0.008 * rf, twist: 0,
      head: [-0.06 * rf, 0, 0.05 * s * rf], head_dz: 0,
      hand_a: [-0.11 * s * rf, 0.25 - 0.03 * rf, 0.29 + 0.02 * rf + 0.006 * br],
      hand_b: [0.11 * s * rf, -0.25 + 0.03 * rf, 0.29 + 0.02 * rf + 0.006 * br],
      foot_a: [0.12 * s * rf, 0.085, 0.04 + 0.075 * Math.max(0, c) * rf],
      foot_b: [-0.12 * s * rf, -0.085, 0.04 + 0.075 * Math.max(0, -c) * rf],
      hair: [0.5 * rf + 0.07 * Math.sin(2 * ph) * rf + 0.03 * br, 0.08 * s * rf],
      face: 'open', tint: [1, 1, 1], flash: 0, ground: 1, ring: 1, visible: true, hop: Math.abs(s) * rf };
    const holding = this.dart_mode(k, gt) === D_HAND;
    let dart = holding ? { off: [0.07, 0.07, 0.09], rot: [75 * RAD, 0, 15 * RAD] } : null;
    const Ac = this._acts[k];
    const hd = [null, 0];
    const face_dir = (d, w) => { if (w > hd[1]) { hd[0] = ang_of(d[0], d[1]); hd[1] = w; } };

    for (const a of Ac.throw) {
      const T = a.T !== null ? a.T : a.t0 + 2;
      if (T - 3.8 < gt && gt < T + 9) {
        const wind = { hand_a: [-0.3, 0.22, 0.76], hand_b: [0.24, -0.18, 0.4], twist: 0.78, lean: -0.17, sz: 0.9, sx: 1.05,
          body_dz: -0.028, foot_a: [0.07, 0.085, 0.04], foot_b: [-0.15, -0.085, 0.04], head: [0.05, -0.35, 0], hair: [0.05, 0.3] };
        const thrown = { hand_a: [0.46, 0.02, 0.4], hand_b: [-0.22, -0.28, 0.34], twist: -0.78, lean: 0.42, sz: 1.07,
          sy: 1.12, body_dz: 0.012, head: [-0.12, 0.22, 0], foot_a: [0.23, 0.085, 0.04], foot_b: [-0.15, -0.085, 0.09], foot_b_pitch: 0.55, hair: [0.42, -0.35] };
        if (gt < T) {
          const w = eout((gt - (T - 3.8)) / 3);
          mix(p, wind, w);
          if (dart) dart = { off: [-0.05, 0.05, 0.12], rot: [25 * RAD, 0, gt * 0.9] };
          face_dir(a.dir, w);
        } else if (a.T !== null) {
          const r = gt - T, whip = eout(r / 1.4), rec = ease((r - 3.2) / 5.6);
          mix(p, wind, (1 - whip) * (1 - rec));
          mix(p, thrown, whip * (1 - rec));
          const ov = bump(r / 3) * (1 - rec);
          p.lean += 0.13 * ov;
          p.z += 0.035 * bump(r / 2.2);
          p.sz *= 1 - 0.05 * bump((r - 1.6) / 2.6);
          face_dir(a.dir, 1 - rec);
        }
      }
    }
    for (const a of Ac.slash) {
      const T = a.T !== null ? a.T : a.t0 + 2;
      if (T - 3.8 < gt && gt < T + 7.5) {
        let th = 105 * RAD;
        if (gt < T - 0.6) {
          const w = eout((gt - (T - 3.8)) / 2.6);
          mix(p, { hand_a: [-0.16, 0.38, 0.52], hand_b: [0.18, 0, 0.4], twist: 1.05, lean: -0.1, sz: 0.9, sx: 1.05,
            body_dz: -0.025, foot_b: [-0.13, -0.085, 0.04], head: [0, -0.5, 0], hair: [0.1, 0.4] }, w);
          if (dart) dart = { off: [-0.12, 0.2, 0.03], rot: [0, 0, gt * 0.5], scale: 1.25 };
          face_dir(a.dir, w);
        } else if (a.T !== null) {
          const sw = ease((gt - (T - 0.6)) / 1.7), rec = ease((gt - (T + 3)) / 4.5);
          th = (105 - 190 * sw) * RAD;
          const w = 1 - rec;
          mix(p, { hand_a: [0.4 * Math.cos(th), 0.4 * Math.sin(th), 0.44], hand_b: [-0.12, -0.28, 0.36],
            twist: 1.05 - 2.2 * sw, lean: 0.38 * bump((gt - (T - 0.6)) / 4), sz: 1 - 0.06 * bump(sw),
            sy: 1 + 0.12 * bump(sw), body_dz: -0.02 * bump(sw), head: [0, 0.3 * sw, 0], foot_a: [0.2, 0.085, 0.04], foot_b: [-0.14, -0.085, 0.04],
            hair: [0.2, 0.4 - 0.9 * sw] }, w);
          if (dart) dart = { off: [0.2 * Math.cos(th), 0.2 * Math.sin(th), 0.02], rot: [0, 0, -th * 2], scale: 1.25 };
          face_dir(a.dir, w);
        }
      }
    }
    for (const a of Ac.kick) {
      const T = a.T !== null ? a.T : a.t0 + 2;
      if (T - 3.4 < gt && gt < T + 6.5) {
        if (gt < T - 0.5) {
          const w = eout((gt - (T - 3.4)) / 2.4);
          mix(p, { sz: 0.86, sx: 1.08, lean: 0.1, foot_a: [-0.12, 0.085, 0.06], hand_a: [0.1, 0.24, 0.4], hand_b: [0.1, -0.24, 0.4] }, w);
          face_dir(a.dir, w);
        } else if (a.T !== null) {
          const kk = eout((gt - (T - 0.5)) / 1.3), rec = ease((gt - (T + 2.2)) / 4), w = kk * (1 - rec);
          mix(p, { z: 0.12 * bump((gt - (T - 0.5)) / 3.5), lean: -0.42, sz: 1.04, sy: 1.12,
            foot_a: [0.44, 0.03, 0.34], foot_a_pitch: -1.85, foot_b: [-0.04, -0.085, 0.04],
            hand_a: [-0.2, 0.3, 0.42], hand_b: [-0.2, -0.3, 0.42], head: [0.25, 0, 0], hair: [-0.3, 0] }, w);
          face_dir(a.dir, 1 - rec);
        }
      }
    }
    for (const a of Ac.dash) {
      const { T0, T1 } = a;
      if (T0 - 2.4 < gt && gt < T1 + 3.5) {
        if (gt < T0) {
          const w = eout((gt - (T0 - 2.4)) / 2);
          mix(p, { sz: 0.82, sx: 1.1, lean: 0.14 }, w);
          face_dir(a.dir, w);
        } else if (gt < T1) {
          const w = eout((gt - T0) / 0.8);
          mix(p, { lean: 0.9, sz: 0.9, sx: 0.92, sy: 1.28, hand_a: [-0.26, 0.2, 0.4], hand_b: [-0.26, -0.2, 0.4],
            foot_a: [-0.16, 0.085, 0.12], foot_b: [-0.1, -0.085, 0.18], head: [-0.55, 0, 0], hair: [1.2, 0], body_dz: 0, roll: 0 }, w);
          face_dir(a.dir, 1);
        } else {
          const r = (gt - T1) / 3.5, q = Math.exp(-r * 3.2) * Math.cos(r * 5.5);
          mix(p, { lean: p.lean - 0.3 * q, sz: 1 - 0.12 * q, sx: 1 + 0.1 * q, hair: [p.hair[0] - 0.5 * q, 0] }, 1);
        }
      }
    }
    for (const a of Ac.recall) {
      const { T0, T1 } = a;
      if (T0 - 0.5 < gt && gt < T1 + 2.5) {
        const w = ease((gt - (T0 - 0.5)) / 2) * (1 - ease((gt - T1) / 2.5));
        mix(p, { hand_a: [0.1, 0.2, 0.86 + 0.03 * Math.sin(gt * 1.7)], hand_b: [0.06, -0.24, 0.33],
          head: [-0.25, 0, 0], sz: 1.04 + 0.015 * Math.sin(gt * 1.7), lean: -0.04, hair: [0.12 + 0.06 * Math.sin(gt * 1.1), 0] }, w);
      }
    }
    for (const T of Ac.catch) {
      if (T - 3 < gt && gt < T + 3.5) {
        if (gt < T) mix(p, { hand_a: [0.2, 0.2, 0.74], head: [-0.15, 0, 0] }, ease((gt - (T - 3)) / 2.4) * 0.9);
        else { const r = (gt - T) / 3.5, q = Math.exp(-r * 3.5) * Math.cos(r * 6); mix(p, { sz: p.sz * (1 - 0.12 * q), sx: 1 + 0.08 * q }, 1); }
      }
    }
    for (const T of Ac.pick) {
      if (T - 1.8 < gt && gt < T + 2.2) {
        const b = bump((gt - (T - 1.8)) / 4);
        mix(p, { sz: 1 - 0.24 * b, sx: 1 + 0.1 * b, lean: p.lean + 0.25 * b, hand_a: [0.16, 0.2, 0.12] }, b);
      }
    }
    for (const a of Ac.stun) {
      const { T0, T1 } = a;
      if (T0 - 0.3 < gt && gt < T1 + 2.5) {
        const u = gt - T0;
        const w = eout((u + 0.3) / 0.8) * (1 - ease((gt - (T1 - 2)) / 4.5));
        const sq = Math.exp(-Math.max(0, u) / 1.3), fl = Math.sin(u * 2.3);
        mix(p, { z: 0.24 * bump(u / 6), lean: -0.75, sz: 1 - 0.26 * sq + 0.06 * (1 - sq), sx: 1 + 0.2 * sq,
          hand_a: [-0.06, 0.32, 0.7 + 0.07 * fl], hand_b: [-0.06, -0.32, 0.7 - 0.07 * fl],
          foot_a: [0.18, 0.09, 0.16 + 0.04 * fl], foot_b: [0.14, -0.09, 0.1 - 0.04 * fl],
          head: [0.3, 0, 0.1 * fl], hair: [-0.6, 0.15 * fl], roll: 0.12 * fl }, w);
        if (w > 0.3) p.face = 'tight';
        const [kx, ky] = rp.char_pos(a.kicker, T0 - 1), [vx, vy] = rp.char_pos(k, T0 - 1);
        if (Math.hypot(kx - vx, ky - vy) > 1e-3) face_dir([kx - vx, ky - vy], w);
        p.flash = Math.max(p.flash, u >= 0 ? clamp(1 - u / 1.4) * 0.8 : 0);
      }
    }
    if (hd[0] !== null) p.heading = ang_lerp(p.heading, hd[0], hd[1]);
    if (0 < gt && gt < rp.T) {
      const [cell, wv] = rp.cell_at(x, y);
      if (cell === CELL_PIT || (wv >= 0 && rp.wave_steps[wv] <= gt)) p.ground = 0;
    }
    const hf = 0 < gt && gt < rp.T - 1 ? this.hidden(k, gt) : 0;
    if (hf > 0) {
      p.sz *= 1 - 0.2 * hf; p.lean += 0.1 * hf;
      p.tint = lerp([1, 1, 1], [0.55, 0.78, 0.7], hf);
      p.ring = 1 - 0.8 * hf;
    }
    if (gt < 0) {
      const u = gt - this.t_land[k];
      if (u < -9) p.visible = false;
      else if (u < 0) {
        mix(p, { z: -u * 1.15, sz: 1.22, sx: 0.9, hand_a: [0, 0.3, 0.72], hand_b: [0, -0.3, 0.72], hair: [1.5, 0],
          foot_a: [0.05, 0.085, 0.1], foot_b: [-0.05, -0.085, 0.06] }, 1);
        p.ground = clamp(1 + u / 6);
      } else {
        const q = Math.exp(-u / 2.4) * Math.cos(u * 1.25);
        mix(p, { sz: 1 - 0.4 * q, sx: 1 + 0.3 * q, hair: [0.03 - 0.5 * q, 0] }, 1);
        if (u < 9) p.face = 'happy';
      }
      if (gt > -7) { const b = bump((gt + 7) / 7); p.sz *= 1 - 0.1 * b; p.sx *= 1 + 0.05 * b; }
    }
    if (gt > rp.T && rp.over && this.winners.has(k)) {
      const u = gt - rp.T - 4;
      if (u > 0) {
        const w = ease(u / 3), jp = (u % 9) / 9, jz = Math.sin(Math.PI * jp);
        const land = Math.exp(-jp * 9) + Math.exp(-(1 - jp) * 9);
        mix(p, { z: 0.34 * jz, sz: 1 + 0.1 * jz - 0.16 * land, sx: 1 - 0.05 * jz + 0.1 * land,
          hand_a: [0.02, 0.28, 0.84], hand_b: [0.02, -0.28, 0.84], head: [-0.2, 0, 0.08 * Math.sin(u * 0.7)], hair: [0.25 * jz, 0], lean: -0.05 }, w);
        p.heading = ang_lerp(p.heading, 90, w);
        p.face = 'happy';
        if (dart) dart = { off: [0, 0.05, 0.14], rot: [90 * RAD, 0, u * 0.5] };
      }
    }
    // ---- 第 12 集：复活（从一道光里落下来，落地一压一弹）、国王挨打后的无敌闪烁
    if (this.life > 0) {
      const [a0] = rp.life_span(k, this.life);
      const u = gt - a0;
      if (u >= 0 && u < 12) {
        if (u < 3) {
          const w = 1 - u / 3;
          mix(p, { z: 1.4 * w * w, sz: 1.18, sx: 0.9, hand_a: [0, 0.3, 0.72], hand_b: [0, -0.3, 0.72], hair: [1.3, 0] }, w);
          p.scale = (p.scale ?? 1) * (0.55 + 0.45 * eout(u / 3));
        } else {
          const q = Math.exp(-(u - 3) / 2.2) * Math.cos((u - 3) * 1.3);
          p.sz *= 1 - 0.3 * q; p.sx *= 1 + 0.22 * q;
        }
        p.flash = Math.max(p.flash || 0, 0.7 * clamp(1 - u / 7));
        if (u < 10) p.face = 'happy';
      }
    }
    const kl = rp.king_lives(k, gt);
    if (kl && kl[1] > 0 && 0 < gt && gt < rp.T) {
      const on = Math.floor(gt / 1.5) % 2 === 0;
      p.flash = Math.max(p.flash || 0, on ? 0.62 : 0.08);
    }
    p.life = this.life;
    if (dart) { const ha = p.hand_a; p.dart = { pos: [ha[0] + dart.off[0], ha[1] + dart.off[1], ha[2] + dart.off[2]], rot: dart.rot, scale: dart.scale || 1 }; }
    return p;
  }

  death_info(k) {
    if (this._di[k]) return this._di[k];
    const rp = this.rp;
    const [t, cause, killer] = this.deaths[k];
    const Td = t + 1;
    const [px, py] = rp.char_pos(k, Td);
    let d = [0, 1], wg = false;
    if (cause === 1 && killer >= 0) {
      const a = rp.states[t].darts[killer], b = rp.states[Math.min(t + 1, rp.T)].darts[killer];
      let dx = b.x - a.x, dy = b.y - a.y;
      if (b.st === D_HAND || Math.hypot(dx, dy) < 1) { dx = px * 1000 - a.x; dy = py * 1000 - a.y; }
      const L = Math.hypot(dx, dy); if (L > 1e-6) d = [dx / L, dy / L];
    } else if (cause === 2 && killer >= 0) {
      const [kx, ky] = rp.char_pos(killer, Td), L = Math.hypot(px - kx, py - ky);
      if (L > 1e-6) d = [(px - kx) / L, (py - ky) / L];
    } else {
      const [x0, y0] = rp.char_pos(k, Td - 1);
      d = [px - x0, py - y0];
      const w = rp.cell_at(px, py)[1];
      if (w >= 0 && Td - 2.5 <= rp.wave_steps[w] && rp.wave_steps[w] <= Td + 1) wg = true;
    }
    return (this._di[k] = [Td, cause, d, wg]);
  }

  slice_info(k) {
    if (!this.sliced.has(k)) return null;
    if (this._si[k]) return this._si[k];
    const [Td, , d] = this.death_info(k);
    const base = this.char_pose(k, Td - 0.02);
    Object.assign(base, { face: 'tight', flash: 0, tint: [1, 1, 1], ground: 0, ring: 0, dart: null, z: 0, lean: 0, roll: 0, spin: 0, twist: 0,
      sx: 1, sy: 1, sz: 1, body_dz: 0, hop: 0, head: [0, 0, 0], head_dz: 0, hair: [0, 0], hand_a: [0, 0.36, 0.6], hand_b: [0, -0.36, 0.6],
      foot_a: [0, 0.085, 0.04], foot_b: [0, -0.085, 0.04], foot_a_pitch: 0, foot_b_pitch: 0, scale: 1 });
    delete base.flip; delete base.tail;
    let s = [-d[1], d[0]];
    if (s[1] < -1e-3 || (Math.abs(s[1]) <= 1e-3 && k % 2)) s = [d[1], -d[0]];
    const rp = this.rp, [px, py] = rp.char_pos(k, Td), land = [];
    for (const [ox, oy] of [[d[0] * SLICE_UP_GO, d[1] * SLICE_UP_GO], [s[0] * SLICE_LOW_GO, s[1] * SLICE_LOW_GO]]) {
      const [cell, w] = rp.cell_at(px + ox, py + oy);
      land.push(cell !== CELL_PIT && !(w >= 0 && rp.wave_steps[w] <= Td + 7));
    }
    return (this._si[k] = { Td, base, pos: [px, py], d, s, tilt: SLICE_TILT, land, life: this.life });
  }
  slice_spots(k) {
    const si = this.slice_info(k), [px, py] = si.pos, d = si.d, s = si.s;
    return [[px + d[0] * SLICE_UP_GO, py + d[1] * SLICE_UP_GO, si.land[0]], [px + s[0] * SLICE_LOW_GO * 0.6, py + s[1] * SLICE_LOW_GO * 0.6, si.land[1]]];
  }

  _dead_pose(k, gt) {
    const rp = this.rp;
    const [Td, cause, d] = this.death_info(k);
    const u = gt - Td;
    const [px, py] = rp.char_pos(k, Td);
    const fl = Math.sin(u * 2.2);
    const p = { x: px, y: py, z: 0, heading: this.heading(k, Td), lean: 0, roll: 0, spin: 0, sx: 1, sy: 1, sz: 1,
      body_dz: 0, twist: 0, head: [0.2, 0, 0.1 * fl], head_dz: 0,
      hand_a: [0, 0.32, 0.68 + 0.08 * fl], hand_b: [0, -0.32, 0.68 - 0.08 * fl],
      foot_a: [0.1 * fl, 0.09, 0.08], foot_b: [-0.1 * fl, -0.09, 0.08], hair: [0.9, 0.2 * fl], face: 'tight',
      tint: [1, 1, 1], flash: clamp(1 - (u - 0.65) / 0.7), ground: 0, ring: 0, visible: true, dart: null };
    if (this.sliced.has(k)) {
      if (u > SLICE_LIFE) { p.visible = false; return p; }
      p.slice = { u };
      p.ground = clamp(1 - (u - SLICE_POOF) / 2.5) * 0.6;
      p.ring = 0; p.scale = 1;
      return p;
    }
    if (cause === 1 || cause === 2) {
      if (u > 10.5) { p.visible = false; return p; }
      const dist = 2.7 * (1 - Math.exp(-u / 4.5));
      p.x = px + d[0] * dist; p.y = py + d[1] * dist;
      p.z = Math.max(0, 0.4 * u - 0.026 * u * u);
      p.heading = ang_of(-d[0], -d[1]);
      p.flip = -0.62 * u;
      p.scale = 1 - ease((u - 7.5) / 3);
      const sq = Math.exp(-u / 0.9);
      p.sz = 1 - 0.2 * sq; p.sx = 1 + 0.2 * sq;
      p.ground = clamp(1 - u / 9) * 0.8;
      p.gx = p.x; p.gy = p.y; p.ring = 0;
    } else {
      const kind = this.fall_kind(k), hang = FALL_HANG[kind];
      const f = Math.max(0, u - hang);
      let z = -0.5 * FALL_G * f * f - (kind === 'ground' ? 0.05 * f : 0);
      const lz = this.liquid_z(k);
      if (lz !== null && z < lz) {
        const ts = this.fall_splash_t(k);
        z = lz - 0.06 * Math.max(0, u - ts);
        if (z < lz - 0.6) { p.visible = false; return p; }
      }
      if (z < -13.5) { p.visible = false; return p; }
      const drift = (kind !== 'kick' ? 2.2 : 3.2) * (1 - Math.exp(-u / 3));
      p.x = px + d[0] * drift; p.y = py + d[1] * drift;
      p.flash = 0; p.hair = [1.4, 0.2 * fl];
      p.scale = 1 - 0.55 * clamp(-z / 12);
      if (kind === 'walk' && u < hang) {
        const a = u / hang, sw = Math.sin(u * 3.4) * (0.25 + 0.35 * a);
        p.z = 0.02 * bump(a); p.lean = sw + 0.5 * a * a; p.sz = 1.04; p.sx = 0.97;
        p.hand_a = [0.12 * Math.cos(u * 4.2), 0.36, 0.62 + 0.24 * Math.sin(u * 4.2)];
        p.hand_b = [0.12 * Math.cos(u * 4.2 + 2.6), -0.36, 0.62 + 0.24 * Math.sin(u * 4.2 + 2.6)];
        p.foot_a = [0.06, 0.085, 0.04];
        p.foot_b = [-0.16 - 0.06 * a, -0.085, 0.14 + 0.05 * Math.sin(u * 3.4)];
        p.head = [0.25 * sw, 0, 0.08 * fl];
        p.hair = [0.3 - sw, 0.2 * fl];
      } else {
        p.z = z; p.sz = 1.06; p.sx = 0.94; p.roll = 0.25 * fl;
        p.hand_a = [0, 0.3, 0.8 + 0.08 * fl]; p.hand_b = [0, -0.3, 0.8 - 0.08 * fl];
        if (kind === 'walk') { p.lean = 0.5; p.flip = 0.5 + 0.2 * f + 0.004 * f * f; p.spin = 0.05 * f; }
        else if (kind === 'kick') {
          p.lean = -0.75; p.flip = -(0.15 * u + 0.004 * u * u);
          p.foot_a = [0.18, 0.09, 0.16 + 0.05 * fl]; p.foot_b = [0.14, -0.09, 0.1 - 0.05 * fl]; p.head = [0.3, 0, 0.1 * fl];
        } else {
          const q = Math.exp(-u / 1.6);
          p.sz = 1.06 - 0.2 * q; p.sx = 0.94 + 0.12 * q; p.lean = -0.1 - 0.02 * f; p.spin = 0.08 * f;
          p.hand_a = [0.05, 0.26, 0.9 + 0.06 * fl]; p.hand_b = [0.05, -0.26, 0.9 - 0.06 * fl];
        }
      }
    }
    return p;
  }

  fall_kind(k) {
    const [, , killer] = this.deaths[k];
    const wg = this.death_info(k)[3];
    if (wg) return 'ground';
    return killer >= 0 ? 'kick' : 'walk';
  }
  /** 这次落坑掉进的是不是液面（岩浆湖、水）：液面高度或 null */
  liquid_z(k) {
    if (k in this._lz) return this._lz[k];
    const [t, cause] = this.deaths[k];
    const [px, py] = this.rp.char_pos(k, t + 1);
    return (this._lz[k] = cause === 3 ? LQ.z_at(this.rp, px, py) : null);
  }
  fall_splash_t(k) {
    const kind = this.fall_kind(k), g = FALL_G, v = kind === 'ground' ? 0.05 : 0;
    const lz = this.liquid_z(k), z = lz === null ? -FALL_SPLASH_Z : Math.max(0.05, -lz);
    return FALL_HANG[kind] + (-v + Math.sqrt(v * v + 2 * g * z)) / g;
  }

  dart_state(k, gt) {
    const rp = this.rp;
    if (gt < 0) return null;
    const i = Math.min(Math.trunc(gt), rp.T), a = i < rp.T ? gt - i : 0;
    const d0 = rp.states[i].darts[k], d1 = rp.states[Math.min(i + 1, rp.T)].darts[k];
    const st = d0.st;
    if (st === D_HAND || st === D_GONE) return null;
    const x0 = d0.x / 1000, y0 = d0.y / 1000;
    let x1 = d1.x / 1000, y1 = d1.y / 1000;
    let spin = 2.15 * gt + k * 1.3;
    if (FLYING.has(st)) {
      let z = FLY_Z, sc = 1, hang = 0;
      const H = this.cf('DART_HANG', 3);
      if (st === D_BACK && d0.age < H + 1) { hang = bump((d0.age + a) / (H + 0.6)); z += 0.22 * hang; }
      if (d1.st === D_HAND) { [x1, y1] = rp.char_pos(k, i + 1); z = lerp(FLY_Z, 0.5, a); }
      else if (d1.st === D_GROUND || d1.st === D_RISE) z = lerp(FLY_Z, 0.42, a);
      else if (d1.st === D_GONE) { x1 = x0; y1 = y0; sc = 1 - a; }
      return { mode: 'fly', x: x0 + (x1 - x0) * a, y: y0 + (y1 - y0) * a, z, spin, tilt: 0, st, scale: sc, glow: 0, hang };
    }
    if (st === D_GROUND) {
      const tl = Math.max(-99, ...this._land[k].filter((t) => t <= i));
      const u = gt - tl;
      let z = 0.045, tilt = 0;
      if (u < 9) {
        const e = Math.exp(-u / 3.2);
        z += 0.38 * Math.abs(Math.cos(u * 0.85)) * e;
        tilt = 0.5 * Math.sin(u * 1.4) * e;
        spin = 2.15 * tl + k * 1.3 + 6 * (1 - Math.exp(-u / 3));
      } else spin = 2.15 * tl + k * 1.3 + 6;
      return { mode: 'ground', x: x0, y: y0, z, spin, tilt, st, scale: 1, glow: 0, hang: 0 };
    }
    if (st === D_RISE) {
      const n = this.cf('RECALL_PRE', 12);
      const pr = clamp(1 - (d0.left - a) / n);
      const tl = Math.max(0, ...this._land[k].filter((t) => t <= i));
      let z = 0.06 + 0.34 * pr * pr + 0.03 * Math.sin(gt * 3);
      let x = x0, y = y0;
      if (d1.st === D_CALL) { x = x0 + (x1 - x0) * a; y = y0 + (y1 - y0) * a; z = lerp(z, FLY_Z, a); }
      return { mode: 'rise', x, y, z, spin: 2.15 * tl + k * 1.3 + 6 + 10 * pr * pr, tilt: 0.22 * Math.sin(gt * 2.7) * (1 - pr), st, scale: 1,
        glow: 0.35 + 0.35 * Math.sin(gt * 2.4) ** 2, hang: 0 };
    }
    return null;
  }

  trail(k, gt, n = 16, dt = 0.42) {
    const pts = [];
    for (let j = 0; j <= n; j++) {
      const t = gt - j * dt;
      if (t < 0) break;
      const d = this.dart_state(k, t);
      if (!d || d.mode !== 'fly') break;
      pts.push([d.x, d.y, d.z, d.st]);
    }
    return pts;
  }
  dash_trail(k, gt, n = 10, dt = 0.4) {
    const A = this.L(k, gt);
    if (A !== this) return A.dash_trail(k, gt, n, dt);
    for (const a of this._acts[k].dash) {
      if (a.T0 < gt && gt < a.T1 + 3) {
        const rp = this.rp, dth = this.deaths[k];
        if (gt >= (dth ? dth[0] : 1e9) + 1) return [[], 0];
        if (this.life > 0 && a.T0 < rp.life_span(k, this.life)[0]) return [[], 0];
        const pts = [];
        for (let j = 0; j <= n; j++) {
          const t = Math.max(a.T0, gt - j * dt);
          const [x, y] = rp.char_pos(k, gt > a.T1 ? Math.min(t, a.T1) : t);
          pts.push([x, y, 0.45, 0]);
          if (t <= a.T0) break;
        }
        return [pts, 1 - ease((gt - a.T1) / 3)];
      }
    }
    return [[], 0];
  }

  _build_fx() {
    const rp = this.rp, out = [];
    const main = this.life === 0;
    for (let k = 0; k < (main ? this.n : 0); k++) {
      const ev = rp.ev[k];
      out.push([this.t_land[k], 'land', { pos: rp.char_pos(k, 0), seat: k }]);
      for (const [t, x, y] of ev.turn) out.push([t + 1, 'turn', { pos: [x, y], seat: k }]);
      for (const [t] of ev.catch) out.push([t + 1, 'catch', { pos: rp.char_pos(k, t + 1), seat: k }]);
      for (const [t] of ev.pick) out.push([t + 1, 'catch', { pos: rp.char_pos(k, t + 1), seat: k }]);
      for (const [t, x, y] of ev.drop) out.push([t + 1, 'drop', { pos: [x, y], seat: k }]);
      for (const [t, x, y] of ev.lost) out.push([t + 1, 'poof', { pos: [x, y], z: FLY_Z, seat: k, size: 0.5 }]);
      for (const [t0] of ev.dash) { const t = t0 + this.cf('DASH_PRE', 0); out.push([t, 'dust', { pos: rp.char_pos(k, t), seat: k }]); }
      for (const [t] of ev.stop) out.push([t + 1, 'dust', { pos: rp.char_pos(k, t + 1), seat: k, size: 0.7 }]);
      for (const [t, x, y] of ev.call) out.push([t + 1, 'turn', { pos: [x, y], seat: k }]);
      for (const a of this._acts[k].slash) if (a.T !== null) out.push([a.T - 0.5, 'slash', { pos: rp.char_pos(k, a.T), dir: a.dir, seat: k }]);
      for (const a of this._acts[k].kick) if (a.T !== null) out.push([a.T - 0.3, 'kick', { pos: rp.char_pos(k, a.T), dir: a.dir, seat: k, hit: a.hit }]);
    }
    if (!main) {
      for (let k = 0; k < this.n; k++) if (this.life <= rp.respawns[k].length) {
        const [a0] = rp.life_span(k, this.life);
        out.push([a0, 'respawn', { pos: rp.char_pos(k, a0), seat: k, team: this.teams[k] }]);
      }
    }
    if (main) {
      for (const [t, x, y, a, b] of rp.clashes) out.push([t + 1, 'clash', { pos: [x, y], seats: [a, b] }]);
      for (const [t, x, y, a, b] of rp.blocks) out.push([t + 1, 'clash', { pos: [x, y], seats: [a, b] }]);
      for (const [t, i, , nw] of rp.captures) { const q = rp.points[i]; if (q) out.push([t + 1, 'capture', { pos: [q[1], q[2]], seat: 0, team: nw, r: q[3] }]); }
      const klt = new Set(rp.lifeloss.map((e) => e[0] + ',' + e[1]));
      for (const [t, k, left, by] of rp.lifeloss) out.push([t + 1, 'kinghit', { pos: rp.char_pos(k, t + 1), seat: by >= 0 ? by : k, victim: k, left, team: this.teams[k] }]);
      for (const [t, killer, victim, how] of rp.hits) {
        if (klt.has(t + ',' + victim)) { out.push([t + 1, 'hit', { pos: rp.char_pos(victim, t + 1), seat: killer, victim, how, sliced: true }]); continue; }
        out.push([t + 1, 'hit', { pos: rp.char_pos(victim, t + 1), seat: killer, victim, how, sliced: this.L(victim, t + 0.5).sliced.has(victim) }]);
      }
      for (const [t, kicker, victim] of rp.kickhits) out.push([t + 1, 'kickhit', { pos: rp.char_pos(victim, t + 1), seat: kicker, victim }]);
    }
    for (const ks of Object.keys(this.deaths)) {
      const k = +ks, [, , killer] = this.deaths[k];
      const [Td, cause, d] = this.death_info(k);
      if (this.sliced.has(k)) {
        const si = this.slice_info(k);
        out.push([Td, 'slice', { pos: si.pos, dir: d, side: si.s, seat: killer >= 0 ? killer : k, victim: k }]);
        for (const [qx, qy, ok] of this.slice_spots(k)) if (ok) out.push([Td + SLICE_POOF, 'poof', { pos: [qx, qy], z: 0.25, seat: k, size: 0.7 }]);
      } else if (cause === 1 || cause === 2) {
        const pp = this._dead_pose(k, Td + 8.6);
        out.push([Td + 8.6, 'poof', { pos: [pp.x, pp.y], z: pp.z + 0.5, seat: k, size: 1 }]);
      } else {
        out.push([Td + (this.fall_kind(k) === 'walk' ? 1.2 : 0), 'fallcry', { pos: rp.char_pos(k, Td), seat: k }]);
        const ts = this.fall_splash_t(k), pp = this._dead_pose(k, Td + ts), lz = this.liquid_z(k);
        out.push([Td + ts, 'splash', { pos: [pp.x, pp.y], z: lz === null ? FALL_SPLASH_Z : lz, seat: k, liquid: lz !== null ? LQ.kind(rp) : null }]);
      }
    }
    if (main) for (const k of this.winners) out.push([rp.T + 5, 'confetti', { pos: rp.char_pos(k, rp.T), seat: k }]);
    out.sort((a, b) => a[0] - b[0]);
    return out;
  }

  continuous_fx(gt) {
    const rp = this.rp, out = [];
    for (let k = 0; k < this.n; k++) {
      const A = this.L(k, gt), dth = A.deaths[k];
      if (dth && gt >= dth[0] + 1) continue;
      if (A.life > 0 && gt < rp.life_span(k, A.life)[0] + 1) continue;
      for (const a of this._acts[k].stun) if (a.T0 + 1 < gt && gt < a.T1 + 1) out.push(['stun', { pos: rp.char_pos(k, gt), seat: k, age: gt - a.T0 }]);
      for (const a of this._acts[k].recall) if (a.T0 < gt && gt < a.T1) out.push(['recall', { pos: rp.char_pos(k, gt), seat: k, age: gt - a.T0, dart: a.dart, prog: (gt - a.T0) / Math.max(1, a.T1 - a.T0) }]);
    }
    return out;
  }
}
