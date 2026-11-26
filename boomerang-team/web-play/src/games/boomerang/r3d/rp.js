// 照正片 src\replay.py（第 12 集版）逐行移植：回放数据给 pose.js（正片动画）用。字段名、单位（毫米 / 0.01 度 / 步）和 Python 版一致。
// 和第 11 集网页查看器的 rp.js 不同的地方：
//   - 数据来源是网站的 ParsedReplay（engine.ts 解析的回放或实时对局逐步追加的帧），不是回放原文；
//   - 实时对局里帧是一步步长出来的：sync() 把新帧接上去、重建事件索引（几千条事件，每步重建也只要零点几毫秒）；
//   - 第 12 集：占点（POINTS、每步 11 个数）、刺王（KING、国王命数）、复活（RESPAWN，同一个人可以出局好几次）、翻点、国王掉命。
export const CELL_GROUND = 0, CELL_WALL = 1, CELL_PIT = 2, CELL_GRASS = 3;
export const [ST_FREE, ST_THROW, ST_THROWN, ST_SLASH, ST_SLASHED, ST_KICK, ST_KICKED, ST_DASH, ST_STUN, ST_RECALL, ST_DEAD] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
export const [D_HAND, D_OUT, D_BACK, D_GROUND, D_RISE, D_GONE, D_CALL] = [0, 1, 2, 3, 4, 5, 6];
export const FLYING = new Set([D_OUT, D_BACK, D_CALL]);

const TAU = Math.PI * 2;

export class Replay {
  /** P：engine.ts 的 ParsedReplay */
  constructor(P) {
    this.P = P;
    this.cfg = { ...P.cfg };
    this.mode = P.mode;
    this.mapname = P.mapName;
    this.W = P.nx; this.H = P.ny;
    this.cell = P.cell;
    this.Wm = this.W * this.cell; this.Hm = this.H * this.cell;
    this.grid = []; this.wave = [];
    for (let y = 0; y < this.H; y++) {
      this.grid.push(P.grid.slice(y * this.W, (y + 1) * this.W));
      this.wave.push(P.wave.length ? P.wave.slice(y * this.W, (y + 1) * this.W) : new Array(this.W).fill(-1));
    }
    this.wave_steps = P.waveSteps.slice();
    this.players = P.players.map((p) => [p.seat, p.team, p.name]);
    this.n = this.players.length;
    this.points = (P.points || []).map((q) => [q.name, q.x, q.y, q.r]);
    this.kings = P.kings ? [P.kings[0], P.kings[1]] : [-1, -1];
    this.states = [];
    this.ctl = []; this.kst = [];
    this.dist = Array.from({ length: this.n }, () => [0]);
    this.liquid = null;          // 场景载入后由 scene.js 填（主题里的液面设置）
    this.T = -1;
    this.winner = -1; this.reason = ''; this.over = false;
    this._nev = -1;
    this.sync();
  }

  /** 把 ParsedReplay 里新长出来的帧接上；有变化返回 true */
  sync() {
    const P = this.P, fr = P.frames;
    let changed = false;
    while (this.states.length < fr.length) {
      const f = fr[this.states.length];
      const chars = f.players.map((p) => ({
        alive: p.alive, x: Math.round(p.x * 1000), y: Math.round(p.y * 1000), vx: Math.round(p.vx * 1000), vy: Math.round(p.vy * 1000),
        face: p.face / TAU * 36000, st: p.st, timer: p.timer, ax: p.ax, ay: p.ay, cd: p.cd, expose: p.expose, kills: p.kills,
        msg: p.msg, vis: p.visMask, dead_t: p.deadT, cause: p.cause, killer: p.killer,
      }));
      const darts = f.darts.map((d) => ({ st: d.st, x: Math.round(d.x * 1000), y: Math.round(d.y * 1000), vx: d.vx, vy: d.vy, left: d.left, age: d.age }));
      const t = this.states.length;
      this.states.push({ t, chars, darts, collapsed: f.fallen, warn: f.warn });
      if (f.control) this.ctl[t] = { owner: f.control.owner, taking: f.control.taking, prog: f.control.prog, score: f.control.score };
      if (this.mode === 5) {
        const a = this.kings[0], b = this.kings[1];
        this.kst[t] = [a >= 0 ? f.players[a].lives : 0, a >= 0 ? f.players[a].inv : 0, b >= 0 ? f.players[b].lives : 0, b >= 0 ? f.players[b].inv : 0];
      }
      if (t > 0) {
        const s0 = this.states[t - 1].chars, s1 = chars;
        for (let k = 0; k < this.n; k++) {
          const a = s0[k], b = s1[k];
          let acc = this.dist[k][t - 1];
          if ((b.st === ST_FREE || b.st === ST_THROW || b.st === ST_THROWN) && a.alive && b.alive) acc += Math.hypot(b.x - a.x, b.y - a.y) / 1000;
          this.dist[k][t] = acc;
        }
      }
      changed = true;
    }
    this.T = this.states.length - 1;
    let nev = 0;
    for (const f of fr) nev += f.events.length;
    const end = P.end;
    if (end && !this.over) { this.over = true; this.winner = end.winner; this.reason = end.reason || ''; changed = true; }
    if (nev !== this._nev || changed) { this._nev = nev; this._indexEvents(); changed = true; }
    return changed;
  }

  _indexEvents() {
    const n = this.n, fr = this.P.frames;
    const ev = Array.from({ length: n }, () => ({ throw: [], release: [], slash: [], kick: [], swing: [], dash: [], stop: [], recall: [], cancel: [],
      call: [], catch: [], pick: [], drop: [], clash: [], turn: [], kicked: [], lost: [] }));
    this.hits = []; this.deaths = {}; this.kickhits = []; this.clashes = []; this.blocks = []; this.collapses = []; this.warns = [];
    this.death_list = []; this.respawns = Array.from({ length: n }, () => []); this.captures = []; this.lifeloss = [];
    const seen = new Set();
    const m = (v) => v / 1000;
    for (const f of fr) {
      const t = f.t;
      for (const e of f.events) {
        const ty = e.type, a = [e.a, e.b, e.c, e.d];
        if (ty === 'THROW') ev[a[0]].throw.push([t, m(a[1]), m(a[2])]);
        else if (ty === 'RELEASE') ev[a[0]].release.push([t, m(a[1]), m(a[2])]);
        else if (ty === 'SLASH') ev[a[0]].slash.push([t, m(a[1]), m(a[2])]);
        else if (ty === 'KICK') ev[a[0]].kick.push([t, m(a[1]), m(a[2])]);
        else if (ty === 'SWING') ev[a[0]].swing.push([t, a[1], a[2]]);
        else if (ty === 'DASH') ev[a[0]].dash.push([t, m(a[1]), m(a[2])]);
        else if (ty === 'STOP') ev[a[0]].stop.push([t]);
        else if (ty === 'RECALL') ev[a[0]].recall.push([t, m(a[1]), m(a[2])]);
        else if (ty === 'CANCEL') ev[a[0]].cancel.push([t]);
        else if (ty === 'CALL') ev[a[0]].call.push([t, m(a[1]), m(a[2])]);
        else if (ty === 'CATCH') ev[a[0]].catch.push([t]);
        else if (ty === 'PICK') ev[a[0]].pick.push([t]);
        else if (ty === 'TURN') ev[a[0]].turn.push([t, m(a[1]), m(a[2])]);
        else if (ty === 'LOST') ev[a[0]].lost.push([t, m(a[1]), m(a[2])]);
        else if (ty === 'DROP' || ty === 'BLOCK') {
          const o = ty === 'DROP' ? a[0] : a[1];
          const [x, y] = ty === 'DROP' ? [a[1], a[2]] : [a[2], a[3]];
          if (o >= 0 && o < n) ev[o].drop.push([t, m(x), m(y)]);
          if (ty === 'BLOCK') this.blocks.push([t, m(x), m(y), a[0], a[1]]);
        } else if (ty === 'CLASH') {
          ev[a[0]].clash.push([t, m(a[2]), m(a[3]), a[1]]);
          const key = t + ',' + Math.min(a[0], a[1]) + ',' + Math.max(a[0], a[1]);
          if (!seen.has(key)) { seen.add(key); this.clashes.push([t, m(a[2]), m(a[3]), a[0], a[1]]); }
          else for (const c of this.clashes) if (c[0] + ',' + Math.min(c[3], c[4]) + ',' + Math.max(c[3], c[4]) === key) { c[1] = (c[1] + m(a[2])) / 2; c[2] = (c[2] + m(a[3])) / 2; }
        } else if (ty === 'HIT') this.hits.push([t, a[0], a[1], 1, a[2]]);
        else if (ty === 'CUT') this.hits.push([t, a[0], a[1], 2, 0]);
        else if (ty === 'KICKHIT') { this.kickhits.push([t, a[0], a[1]]); ev[a[1]].kicked.push([t, a[0]]); }
        else if (ty === 'DEATH') { this.deaths[a[0]] = [t, a[1], a[2]]; this.death_list.push([t, a[0], a[1], a[2]]); }
        else if (ty === 'RESPAWN') this.respawns[a[0]].push(t);
        else if (ty === 'CAPTURE') this.captures.push([t, a[0], a[1], a[2]]);
        else if (ty === 'LIFE') this.lifeloss.push([t, a[0], a[1], a[2], a[3]]);
        else if (ty === 'WARN') this.warns.push([t, a[0]]);
        else if (ty === 'COLLAPSE') this.collapses.push([t, a[0]]);
      }
    }
    this.ev = ev;
    this.deaths_by_seat = Array.from({ length: n }, (_, k) => this.death_list.filter((d) => d[1] === k).map((d) => [d[0], d[2], d[3]]));
    this.max_lives = Math.max(1, ...this.respawns.map((r) => r.length + 1));
  }

  cell_at(mx, my) {
    const cx = Math.floor(mx / this.cell), cy = Math.floor(my / this.cell);
    if (cx >= 0 && cx < this.W && cy >= 0 && cy < this.H) return [this.grid[cy][cx], this.wave[cy][cx]];
    return [CELL_PIT, -1];
  }
  S(t) { return this.states[Math.min(Math.max(Math.trunc(t), 0), this.T)]; }
  char_pos(k, gt) {
    if (gt <= 0) { const c = this.states[0].chars[k]; return [c.x / 1000, c.y / 1000]; }
    if (gt >= this.T) { const c = this.states[this.T].chars[k]; return [c.x / 1000, c.y / 1000]; }
    const i = Math.floor(gt), a = gt - i, c0 = this.states[i].chars[k], c1 = this.states[i + 1].chars[k];
    return [(c0.x + (c1.x - c0.x) * a) / 1000, (c0.y + (c1.y - c0.y) * a) / 1000];
  }
  dist_at(k, gt) {
    if (gt <= 0) return 0;
    if (gt >= this.T) return this.dist[k][this.T];
    const i = Math.floor(gt), a = gt - i;
    return this.dist[k][i] + (this.dist[k][i + 1] - this.dist[k][i]) * a;
  }

  // ------------------------------------------------------------ 第 12 集：复活（每条命）、模式状态
  life_index(k, gt) { let c = 0; for (const r of this.respawns[k]) if (gt >= r + 1) c++; return c; }
  life_span(k, i) {
    const r = this.respawns[k];
    return [i === 0 ? -1e9 : r[i - 1] + 1, i < r.length ? r[i] + 1 : null];
  }
  deaths_of_life(i) {
    const out = {};
    for (let k = 0; k < this.n; k++) if (i < this.deaths_by_seat[k].length) out[k] = this.deaths_by_seat[k][i];
    return out;
  }
  team_of(k) { return this.players[k][1]; }
  ctl_at(gt) { if (!this.ctl.length) return null; return this.ctl[Math.min(Math.max(Math.trunc(gt), 0), this.T)] || null; }
  kst_at(gt) { if (!this.kst.length) return null; return this.kst[Math.min(Math.max(Math.trunc(gt), 0), this.T)] || null; }
  king_lives(k, gt) {
    if (this.mode !== 5 || (k !== this.kings[0] && k !== this.kings[1]) || !this.kst.length) return null;
    const s = this.kst_at(gt), j = k === this.kings[0] ? 0 : 1;
    return s ? [s[2 * j], s[2 * j + 1]] : null;
  }
}
