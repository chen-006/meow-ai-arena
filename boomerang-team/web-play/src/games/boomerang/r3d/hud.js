// 顶栏和画面上的提示：照正片 src\hud.py（第 12 集版）逐项移植（同样的 1920 版面坐标、字号、颜色、头像裁切），用 Canvas 2D 画，按画面宽度缩放。
// 底子是第 11 集网页查看器的 hud.js；第 12 集加：5v5 顶栏（歼灭存活比分 / 占点积分和 A B C 圆片 / 刺王两王的心）、国王头像上的小王冠、
// 场上跟着镜头的点名牌、复活倒计时、国王头顶的心，播报加"占领了 X 点""击中国王，还剩 N 条命"。
// 网站加：窄屏（手机）压成细条（同样的信息，只留头像、击杀数和中间的比分）；"你"在名字后面标出来。
// 队伍色一律按显示位：左 = Astra 队（蓝），右 = Fable 队（红）；回放里的原始队号先经 slots 换算。
const W = 1920;
const INK = 'rgb(46,40,36)', SUB = 'rgb(128,118,108)', PANEL = 'rgba(255,252,246,0.933)';
const FONT = '"Noto Sans SC","PingFang SC","Microsoft YaHei",system-ui,sans-serif';
const NEUTRAL = 'rgb(196,190,182)', GOLD = 'rgb(255,196,46)';
const MODE_NAME = { 0: '单挑', 1: '四人乱斗', 2: '4 v 4', 3: '歼灭', 4: '占点', 5: '刺王' };
const CAUSE_SHORT = { 1: '镖', 2: '砍', 3: '坠落' };
const ease = (t) => { t = Math.max(0, Math.min(1, t)); return t * t * (3 - 2 * t); };
const clamp = (x, a = 0, b = 1) => Math.max(a, Math.min(b, x));
const font = (size, weight = 'Bold') => `${{ Bold: 700, Medium: 500, Regular: 400, Black: 900 }[weight] || 700} ${size}px ${FONT}`;

// 头像：活着原图；出局灰度、发白（hud.avatar dead=True）
const AV = new Map();
function avatarImg(url) {
  let a = AV.get(url);
  if (a) return a;
  a = { img: new Image(), gray: null };
  a.img.decoding = 'async';
  a.img.onload = () => {
    try {
      const c = document.createElement('canvas'); c.width = a.img.naturalWidth; c.height = a.img.naturalHeight;
      const g = c.getContext('2d'); g.drawImage(a.img, 0, 0);
      const im = g.getImageData(0, 0, c.width, c.height), d = im.data;
      for (let i = 0; i < d.length; i += 4) { const v = 0.3 * d[i] + 0.59 * d[i + 1] + 0.11 * d[i + 2]; const w = v * 0.55 + 255 * 0.45; d[i] = d[i + 1] = d[i + 2] = w; }
      g.putImageData(im, 0, 0); a.gray = c;
    } catch { a.gray = null; }
    a.ready = true;
  };
  a.img.src = url;
  AV.set(url, a);
  return a;
}

export class Hud {
  /** info：{ mode, n, labels[], avatars[]（图片地址）, colors[]（主色）, slots[]（每个座位的显示位 0 左 1 右）, teamHex[]（按显示位）, teamNames[]（按显示位）, you（真人座位或 -1） } */
  constructor(canvas, info) {
    this.cv = canvas; this.ctx = canvas.getContext('2d');
    this.info = info; this.key = '';
    this.av = info.avatars.map(avatarImg);
    this.team = info.mode >= 2;
  }

  resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = this.cv.clientWidth, h = this.cv.clientHeight;
    if (this.cv.width !== Math.round(w * dpr) || this.cv.height !== Math.round(h * dpr)) {
      this.cv.width = Math.round(w * dpr); this.cv.height = Math.round(h * dpr); this.key = '';
    }
    this.compact = w < 760;
    this.VW = this.compact ? 760 : W;
    this.s = w / this.VW; this.dpr = dpr;
    this.H = h / this.s;
  }
  /** 顶栏底边（CSS 像素）：镜头取景时让开这一条 */
  barBottom() { return this.cv.clientWidth < 760 ? Math.round(this.cv.clientWidth / 760 * (this.info.mode === 4 ? 112 : 84)) : Math.round(this.cv.clientWidth / W * (this.info.mode === 4 ? 160 : 118)); }

  panel(x0, y0, x1, y1, r, fill = PANEL, blur = 10, alpha = 55) {
    const c = this.ctx;
    c.save();
    c.shadowColor = `rgba(40,50,90,${alpha / 255})`; c.shadowBlur = blur * 2 * this.s * this.dpr; c.shadowOffsetY = 4 * this.s * this.dpr;
    c.fillStyle = fill; c.beginPath(); c.roundRect(x0, y0, x1 - x0, y1 - y0, r); c.fill();
    c.restore();
  }
  text(s, x, y, f, col) { const c = this.ctx; c.font = f; c.fillStyle = col; c.textBaseline = 'top'; c.fillText(s, x, y + 2); }
  tw(s, f) { this.ctx.font = f; return this.ctx.measureText(s).width; }
  avatar(k, x, y, size, dead) {
    const c = this.ctx, a = this.av[k];
    c.save(); c.beginPath(); c.arc(x + size / 2, y + size / 2, size / 2, 0, Math.PI * 2); c.clip();
    c.fillStyle = '#EEE6DC'; c.fillRect(x, y, size, size);
    if (a && a.ready) c.drawImage(dead && a.gray ? a.gray : a.img, x, y, size, size);
    c.restore();
  }
  ellipse(x0, y0, x1, y1, col) { const c = this.ctx; c.fillStyle = col; c.beginPath(); c.ellipse((x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2, 0, 0, Math.PI * 2); c.fill(); }
  label(k) { const l = this.info.labels[k]; return l + (k === this.info.you && l !== '你' ? '（你）' : ''); }
  tcol(k) { return this.info.teamHex[this.info.slots[k]]; }

  card(x, y, w, h, k, alive, status, ring, av = 62, ns = 26, ss = 18, center = false, pop = 0) {
    let fn = font(ns);
    const fs = font(ss, 'Medium');
    const name = this.label(k);
    let ax = x + 10;
    if (center) { const cw = av + 12 + Math.max(this.tw(name, fn), this.tw(status, fs) + 16); ax = Math.floor(x + Math.max(10, (w - cw) / 2)); }
    const ay = y + Math.floor((h - av) / 2);
    const g = Math.floor(4 + 5 * pop);
    this.ellipse(ax - g, ay - g, ax + av + g - 1, ay + av + g - 1, alive ? ring : 'rgb(196,190,184)');
    this.ellipse(ax - 1, ay - 1, ax + av, ay + av, '#fff');
    this.avatar(k, ax, ay, av, !alive);
    const tx = ax + av + 12;
    let sz = ns;
    while (this.tw(name, fn) > w - (tx - x) - 8 && sz > 13) { sz--; fn = font(sz); }
    const nc = alive ? INK : 'rgb(168,160,152)';
    this.text(name, tx, y + Math.floor(h / 2) - ns + 1, fn, nc);
    if (!alive) {
      const t = this.tw(name, fn), ly = y + Math.floor(h / 2) - Math.floor(ns / 2) + 2, c = this.ctx;
      c.strokeStyle = 'rgb(168,160,152)'; c.lineWidth = 2; c.beginPath(); c.moveTo(tx, ly); c.lineTo(tx + t, ly); c.stroke();
    }
    const sy = y + Math.floor(h / 2) + 4;
    if (alive) {
      this.ellipse(tx, sy + 5, tx + 9, sy + 14, this.info.colors[k] || '#888');
      this.text(status, tx + 15, sy - 1, fs, SUB);
    } else {
      const c = this.ctx; c.fillStyle = 'rgb(236,230,224)'; c.beginPath(); c.roundRect(tx - 1, sy, this.tw(status, fs) + 14, ss + 6, 8); c.fill();
      this.text(status, tx + 6, sy + 1, fs, 'rgb(150,140,130)');
    }
    return [ax, ay, av];
  }

  heart(x, y, s, filled = true, col = '#FF5B4D', alpha = 1, broken = false, glow = false) {
    const c = this.ctx, r = s / 34;
    const shape = (rr, off = 0) => {
      c.beginPath();
      for (let i = 0; i <= 60; i++) {
        const t = 2 * Math.PI * i / 60, hx = 16 * Math.sin(t) ** 3, hy = -(13 * Math.cos(t) - 5 * Math.cos(2 * t) - 2 * Math.cos(3 * t) - Math.cos(4 * t));
        if (i === 0) c.moveTo(x + hx * rr + off, y + hy * rr); else c.lineTo(x + hx * rr + off, y + hy * rr);
      }
      c.closePath();
    };
    c.save(); c.globalAlpha *= alpha;
    shape(r * 1.18); c.fillStyle = '#fff'; c.fill();
    if (filled) {
      shape(r); c.fillStyle = glow ? mixHex(col, '#FFFFFF', 0.55) : col; c.fill();
      c.fillStyle = 'rgba(255,255,255,0.6)'; c.beginPath(); c.arc(x - s * 0.18, y - s * 0.2, s * 0.09, 0, Math.PI * 2); c.fill();
    } else { shape(r); c.fillStyle = 'rgb(206,198,190)'; c.fill(); }
    if (broken) {
      c.strokeStyle = '#fff'; c.lineWidth = s * 0.09; c.beginPath();
      c.moveTo(x, y - s * 0.4); c.lineTo(x - s * 0.08, y - s * 0.08); c.lineTo(x + s * 0.08, y + s * 0.12); c.lineTo(x, y + s * 0.45); c.stroke();
    }
    c.restore();
  }
  crown(x, y, s, dim = false) {
    const c = this.ctx, w = s, h = s * 0.75, o = 0;
    const pts = [[o, h], [o, h * 0.3], [w * 0.25, h * 0.6], [w * 0.5, 0], [w * 0.75, h * 0.6], [w, h * 0.3], [w, h]];
    c.save(); c.translate(x - w / 2, y);
    c.beginPath(); pts.forEach(([px, py], i) => (i ? c.lineTo(px, py) : c.moveTo(px, py))); c.closePath();
    c.lineWidth = 6; c.strokeStyle = '#fff'; c.lineJoin = 'round'; c.stroke();
    const gold = dim ? 'rgb(180,172,164)' : GOLD;
    c.fillStyle = gold; c.fill();
    c.fillStyle = dim ? 'rgb(148,141,134)' : 'rgb(209,161,38)'; c.fillRect(0, h * 0.78, w, h * 0.22);
    c.fillStyle = '#fff';
    for (const [cx, cy] of [[0, h * 0.3], [w * 0.5, 0], [w, h * 0.3]]) { c.beginPath(); c.arc(cx, cy, 2.4, 0, Math.PI * 2); c.fill(); }
    c.restore();
  }

  // ---- 状态（hud.Hud.state + 播报）
  state(rp, A, gt) {
    const n = rp.n, alive = [], kills = [], cause = [], pops = [];
    for (let k = 0; k < n; k++) {
      const B = A.L(k, gt), d = B.deaths[k];
      const dead = !!d && gt >= d[0] + 1;
      alive.push(!dead); cause.push(dead ? d[1] : 0);
      if (rp.mode >= 3) kills.push(gt >= 1 ? rp.S(gt - 1).chars[k].kills : 0);
      else { let kc = 0; for (const [t, v, , kl] of rp.death_list) if (kl === k && v !== k && gt >= t + 1) kc++; kills.push(kc); }
      pops.push(dead ? Math.round(clamp(1 - (gt - d[0] - 1) / 8) * 100) / 100 : 0);
    }
    const left = Math.ceil(Math.max(0, (rp.cfg.STEPS ?? 800) - clamp(gt, 0, rp.T)) / 20 - 1e-6);
    return { alive, kills, cause, pops, left };
  }
  feed(rp, A) {
    if (this._feedN === rp._nev && this._feedT === rp.T) return this._feed;
    const out = [], kings = rp.kings || [];
    for (const [B, k] of A.all_deaths()) {
      const [, cause0, killer] = B.deaths[k];
      const [Td, cause, , wg] = B.death_info(k);
      let segs;
      if (rp.mode === 5 && kings.includes(k)) {
        segs = cause === 1 && killer >= 0 ? [['av', killer], ['name', killer], ['txt', '的飞镖终结了国王'], ['av', k], ['name', k]]
          : cause === 2 && killer >= 0 ? [['av', killer], ['name', killer], ['txt', '一刀终结了国王'], ['av', k], ['name', k]]
            : killer >= 0 ? [['av', killer], ['name', killer], ['txt', '把国王'], ['av', k], ['name', k], ['txt', '踢下了悬崖']]
              : [['av', k], ['name', k], ['txt', '国王坠落']];
      } else segs = cause === 1 ? [['av', killer], ['name', killer], ['txt', '的飞镖击中'], ['av', k], ['name', k]]
        : cause === 2 ? [['av', killer], ['name', killer], ['txt', '一刀砍中'], ['av', k], ['name', k]]
          : killer >= 0 ? [['av', killer], ['name', killer], ['txt', '把'], ['av', k], ['name', k], ['txt', '踢下了悬崖']]
            : wg ? [['av', k], ['name', k], ['txt', '随地块坠落']] : [['av', k], ['name', k], ['txt', '失足坠落']];
      void cause0;
      out.push([Td, segs]);
    }
    for (const [t, i, , nw] of rp.captures) out.push([t + 1, [['team', nw], ['txt', '占领了 ' + (rp.points[i] ? rp.points[i][0] : '?') + ' 点']]]);
    for (const [t, k, left, by] of rp.lifeloss) {
      if (left <= 0) continue;
      if (by >= 0 && by !== k && rp.players[by][1] === rp.players[k][1]) out.push([t + 1, [['av', by], ['name', by], ['txt', '误伤自家国王'], ['av', k], ['name', k], ['txt', '还剩 ' + left + ' 条命']]]);
      else if (by >= 0 && by !== k) out.push([t + 1, [['av', by], ['name', by], ['txt', '击中国王'], ['av', k], ['name', k], ['txt', '还剩 ' + left + ' 条命']]]);
      else out.push([t + 1, [['av', k], ['name', k], ['txt', '误伤，国王还剩 ' + left + ' 条命']]]);
    }
    out.sort((a, b) => a[0] - b[0]);
    this._feedN = rp._nev; this._feedT = rp.T; this._feed = out;
    return out;
  }

  // ---- 主画法
  /** project(x, y, z) -> [CSS px, CSS py] 或 null：地图坐标投影到画面（场上名牌、心、复活倒计时用） */
  draw(rp, A, gt, project, extra = {}) {
    this.resize();
    this.A = A;
    const S = this.state(rp, A, gt);
    const first = Math.min(...Object.values(A.t_land));
    const slide = gt < 0 ? ease((gt - (first - 6)) / 7) : 1;
    const toasts = [];
    const endT = rp.over ? rp.T : 1e9;
    for (const [T, segs] of this.feed(rp, A)) {
      const u = gt - T;
      if (u >= 0 && u < 52) {
        const a = ease(u / 3) * (1 - ease((u - 44) / 8)) * (1 - ease((gt - endT - 4) / 5));
        if (a >= 0.02) toasts.push([segs, a, 1 - ease(u / 4)]);
      }
    }
    toasts.splice(0, Math.max(0, toasts.length - 2));
    let warn = null;
    const wn = rp.cfg.COL_WARN ?? 30;
    rp.wave_steps.forEach((s, w) => {
      if (s - wn <= gt && gt < s + 8 && (s <= endT || gt <= endT + 1)) warn = gt < s ? ['地块即将崩落', 0.5 + 0.5 * Math.sin(gt * 0.9) ** 2, false] : ['第 ' + (w + 1) + ' 波崩落', 1 - ease((gt - s - 3) / 5), true];
    });
    let banner = null;
    if (rp.over && gt > rp.T + 8 && !extra.noBanner) {
      const a = ease((gt - rp.T - 8) / 6);
      let sc = '';
      if (rp.mode === 4) { const c = rp.ctl_at(rp.T); if (c) sc = '  ' + Math.floor(c.score[this.rawOfSlot(0)] / 20) + ' : ' + Math.floor(c.score[this.rawOfSlot(1)] / 20); }
      if (rp.winner < 0) banner = ['平局' + sc, '#8A8078', a, null];
      else if (this.team) banner = [this.info.teamNames[this.info.slotOfTeam[rp.winner]] + ' 获胜' + sc, this.info.teamHex[this.info.slotOfTeam[rp.winner]], a, null];
      else banner = [this.label(rp.winner) + ' 获胜', this.info.colors[rp.winner], a, rp.winner];
    }
    const mode = this.modeItems(rp, A, gt, project);
    const tags = [];
    if (gt < 0 && project) for (let k = 0; k < rp.n; k++) {
      const u = gt - A.t_land[k], a = ease((u - 2) / 3) * (1 - ease((gt + 9) / 5));
      if (a <= 0.02) continue;
      const [x, y] = rp.char_pos(k, 0), q = project(x, y, 1.55);
      if (q) tags.push([k, q[0], q[1], a]);
    }
    const key = JSON.stringify([this.cv.width, this.cv.height, S, Math.round(slide * 100), toasts.map((t) => [t[1].toFixed(2), t[2].toFixed(2), t[0].length]),
      warn && [warn[0], warn[1].toFixed(2)], banner && banner[2].toFixed(2), mode, tags.map((t) => [t[0], t[1] | 0, t[2] | 0, t[3].toFixed(2)]),
      this.av.map((a) => !!a.ready), rp.mode === 4 ? rp.ctl_at(Math.max(0, gt)) : 0, rp.mode === 5 ? rp.kst_at(gt) : 0, Math.floor(gt / 1.5)]);
    if (key === this.key) return;
    this.key = key;
    const c = this.ctx;
    c.setTransform(1, 0, 0, 1, 0, 0);
    c.clearRect(0, 0, this.cv.width, this.cv.height);
    c.setTransform(this.s * this.dpr, 0, 0, this.s * this.dpr, 0, 0);
    const oy = Math.floor(-130 * (1 - slide));
    this.paintMode(rp, mode);
    if (this.compact) this.barCompact(rp, S, oy, gt);
    else if (rp.mode >= 3) this.bar5v5(rp, S, oy, gt);
    else if (this.team) this.barTeam(rp, S, oy);
    else this.barFFA(rp, S, oy);
    if (rp.mode === 4) this.pointChips(rp, gt, oy);
    let y = this.compact ? (rp.mode === 4 ? 116 : 92) : (rp.mode === 4 ? 166 : 122);
    if (warn) { this.warn(y, ...warn); y += 58; }
    for (const [segs, a, off] of toasts) { this.toast(y - Math.floor(14 * off), segs, a); y += 60; }
    for (const [k, px, py, a] of tags) this.tag(k, px / this.s, py / this.s, a);
    if (banner) this.banner(...banner);
  }
  rawOfSlot(slot) { return this.info.slotOfTeam[0] === slot ? 0 : 1; }

  barFFA(rp, S, oy) {
    const n = rp.n, box = [24, 14 + oy, W - 24, 106 + oy];
    this.panel(...box, 26);
    const midW = 190, cw = Math.floor((box[2] - box[0] - midW - 30) / n);
    let x = box[0] + 12;
    for (let i = 0; i < n; i++) {
      if (i === Math.floor(n / 2)) x += midW;
      const s = S.alive[i] ? '击杀 ' + S.kills[i] : '出局 · ' + (CAUSE_SHORT[S.cause[i]] || '');
      this.card(x, box[1] + 8, cw - 10, box[3] - box[1] - 16, i, S.alive[i], s, this.info.colors[i], 62, 26, 18, true, S.pops[i]);
      x += cw;
    }
    const cx = box[0] + 12 + cw * Math.floor(n / 2) + midW / 2, c = this.ctx;
    c.fillStyle = 'rgb(246,240,230)'; c.beginPath(); c.roundRect(cx - 74, box[1] + 14, 148, box[3] - box[1] - 28, 20); c.fill();
    const tt = Math.floor(S.left / 60) + ':' + String(S.left % 60).padStart(2, '0');
    const f1 = font(34), f2 = font(15, 'Medium');
    this.text(tt, cx - this.tw(tt, f1) / 2, box[1] + 18, f1, INK);
    const lab = MODE_NAME[rp.mode] || '';
    this.text(lab, cx - this.tw(lab, f2) / 2, box[1] + 60, f2, SUB);
  }

  teamZones(box, cw, per, midW, x0off, zh) {
    const c = this.ctx;
    for (const slot of [0, 1]) {
      const tc = this.info.teamHex[slot];
      const zx0 = box[0] + x0off + (slot === 1 ? cw * per + midW : 0), zx1 = zx0 + cw * per + 2, zy = box[1] + 6;
      c.save(); c.beginPath(); c.roundRect(zx0, zy, zx1 - zx0, zh, 18); c.clip();
      c.fillStyle = mixHex(tc, '#FFFFFF', 0.8); c.fillRect(zx0, zy, zx1 - zx0, zh);
      c.fillStyle = tc; c.fillRect(zx0, zy, zx1 - zx0, 8);
      c.restore();
    }
  }
  seatsBySlot(rp) {
    const L = [[], []];
    for (let k = 0; k < rp.n; k++) L[this.info.slots[k]].push(k);
    return L;
  }

  barTeam(rp, S, oy) {
    const n = rp.n, box = [20, 12 + oy, W - 20, 108 + oy];
    this.panel(...box, 24);
    const midW = 214, per = Math.floor(n / 2), cw = Math.floor((box[2] - box[0] - midW - 20) / n);
    this.teamZones(box, cw, per, midW, 8, box[3] - box[1] - 12);
    const cnt = [0, 0], L = this.seatsBySlot(rp);
    for (const slot of [0, 1]) L[slot].forEach((k, i) => {
      if (S.alive[k]) cnt[slot]++;
      const x = box[0] + 10 + cw * (i + (slot === 1 ? per : 0)) + (slot === 1 ? midW : 0);
      const s = S.alive[k] ? '击杀 ' + S.kills[k] : '出局 · ' + (CAUSE_SHORT[S.cause[k]] || '');
      this.card(x, box[1] + 14, cw - 8, box[3] - box[1] - 22, k, S.alive[k], s, this.info.teamHex[slot], 54, 22, 16, false, S.pops[k]);
    });
    const cx = box[0] + 10 + cw * per + midW / 2 - 4;
    this.centerScore(cx, box, cnt, 42);
    this.teamPills(cx, box[1] + 58, 62);
    const lab = Math.floor(S.left / 60) + ':' + String(S.left % 60).padStart(2, '0'), f2 = font(15, 'Medium');
    this.text(lab, cx - this.tw(lab, f2) / 2, box[1] + 62, f2, SUB);
  }
  centerScore(cx, box, cnt, size) {
    const f1 = font(size, 'Black'), a = String(cnt[0]), b = String(cnt[1]);
    this.text(a, cx - 18 - this.tw(a, f1), box[1] + 3, f1, this.info.teamHex[0]);
    this.text(':', cx - 7, box[1] + 5, font(size - 6), 'rgb(170,160,150)');
    this.text(b, cx + 18, box[1] + 3, f1, this.info.teamHex[1]);
  }
  teamPills(cx, y, gap) {
    const f3 = font(17), c = this.ctx;
    for (const [slot, sg] of [[0, -1], [1, 1]]) {
      const nm = this.info.teamNames[slot], t = this.tw(nm, f3);
      const px = sg < 0 ? cx - gap - t / 2 : cx + gap - t / 2;
      c.fillStyle = this.info.teamHex[slot]; c.beginPath(); c.roundRect(px - 9, y, t + 18, 26, 12); c.fill();
      this.text(nm, px, y + 1, f3, '#fff');
    }
  }

  bar5v5(rp, S, oy, gt) {
    const n = rp.n, box = [16, 10 + oy, W - 16, 110 + oy];
    this.panel(...box, 24);
    const midW = 300, per = Math.floor(n / 2), cw = Math.floor((box[2] - box[0] - midW - 16) / n);
    this.teamZones(box, cw, per, midW, 6, box[3] - box[1] - 12);
    const cnt = [0, 0], L = this.seatsBySlot(rp), A = this.A;
    for (const slot of [0, 1]) L[slot].forEach((k, i) => {
      if (S.alive[k]) cnt[slot]++;
      const x = box[0] + 8 + cw * (i + (slot === 1 ? per : 0)) + (slot === 1 ? midW : 0);
      let st;
      if (S.alive[k]) st = '击杀 ' + S.kills[k];
      else if (rp.mode === 4) { const B = A.L(k, gt), nxt = rp.life_span(k, B.life)[1]; st = nxt !== null ? '复活 ' + Math.max(1, Math.ceil((nxt - gt) / 20 - 1e-6)) : '出局'; }
      else st = '出局 · ' + (CAUSE_SHORT[S.cause[k]] || '');
      const [ax, ay] = this.card(x, box[1] + 15, cw - 6, box[3] - box[1] - 24, k, S.alive[k], st, this.info.teamHex[slot], 50, 20, 15, false, S.pops[k]);
      if (rp.mode === 5 && rp.kings.includes(k)) this.crown(ax + 25, ay - 6, 30, !S.alive[k]);
    });
    const cx = box[0] + 8 + cw * per + midW / 2 - 2, f2 = font(15, 'Medium');
    this.teamPills(cx, box[1] + 62, 80);
    if (rp.mode === 4) {
      const c = rp.ctl_at(gt) || { score: [0, 0] };
      const a = String(Math.floor(c.score[this.rawOfSlot(0)] / 20)), b = String(Math.floor(c.score[this.rawOfSlot(1)] / 20)), f1 = font(40, 'Black');
      this.text(a, cx - 16 - this.tw(a, f1), box[1] + 2, f1, this.info.teamHex[0]);
      this.text(b, cx + 16, box[1] + 2, f1, this.info.teamHex[1]);
      this.text(':', cx - 6, box[1] + 4, font(32), 'rgb(170,160,150)');
      const lab = Math.floor(S.left / 60) + ':' + String(S.left % 60).padStart(2, '0');
      this.text(lab, cx - this.tw(lab, f2) / 2, box[1] + 66, f2, SUB);
    } else if (rp.mode === 5) {
      const tot = rp.cfg.KING_LIVES ?? 3;
      for (const slot of [0, 1]) {
        const kk = rp.kings[this.rawOfSlot(slot)], kl = kk >= 0 ? rp.king_lives(kk, gt) : null;
        const n_ = kl && S.alive[kk] ? kl[0] : 0, gap = 26, x0 = slot === 0 ? cx - 30 - gap * (tot - 1) : cx + 30;
        for (let h = 0; h < tot; h++) this.heart(x0 + h * gap, box[1] + 30, 22, h < n_, this.info.teamHex[slot]);
      }
      const f = font(18, 'Black');
      this.text('VS', cx - this.tw('VS', f) / 2, box[1] + 19, f, 'rgb(170,160,150)');
      this.text(MODE_NAME[5], cx - this.tw(MODE_NAME[5], f2) / 2, box[1] + 66, f2, SUB);
    } else {
      this.centerScore(cx, box, cnt, 42);
      this.text(MODE_NAME[rp.mode], cx - this.tw(MODE_NAME[rp.mode], f2) / 2, box[1] + 66, f2, SUB);
    }
  }

  // 手机细条：两边头像排一排（出局变灰）、中间比分 / 时间
  barCompact(rp, S, oy, gt) {
    const n = rp.n, box = [8, 6 + oy, this.VW - 8, 80 + oy], c = this.ctx;
    this.panel(...box, 18);
    const midW = 150, cx = this.VW / 2;
    const L = this.team ? this.seatsBySlot(rp) : [[...Array(n).keys()].slice(0, Math.ceil(n / 2)), [...Array(n).keys()].slice(Math.ceil(n / 2))];
    const sideW = (box[2] - box[0] - midW) / 2 - 8;
    for (const slot of [0, 1]) {
      const list = L[slot], m = Math.max(1, list.length), cell = Math.min(80, sideW / m), av = Math.min(46, cell - 8);
      const x0 = slot === 0 ? box[0] + 8 + (sideW - cell * m) / 2 : cx + midW / 2 + (sideW - cell * m) / 2;
      if (this.team) { c.fillStyle = mixHex(this.info.teamHex[slot], '#FFFFFF', 0.82); c.beginPath(); c.roundRect(x0 - 4, box[1] + 6, cell * m + 8, box[3] - box[1] - 12, 14); c.fill(); }
      list.forEach((k, i) => {
        const x = x0 + i * cell + (cell - av) / 2, y = box[1] + 10;
        const ring = this.team ? this.info.teamHex[slot] : this.info.colors[k];
        this.ellipse(x - 3, y - 3, x + av + 3, y + av + 3, S.alive[k] ? ring : 'rgb(196,190,184)');
        this.avatar(k, x, y, av, !S.alive[k]);
        if (rp.mode === 5 && rp.kings.includes(k)) this.crown(x + av / 2, y - 9, 20, !S.alive[k]);
        const s = S.alive[k] ? String(S.kills[k]) : (rp.mode === 4 ? '复活' : '×'), f = font(15, 'Black');
        const tw = this.tw(s, f) + 10;
        c.fillStyle = S.alive[k] ? INK : 'rgb(168,160,152)'; c.beginPath(); c.roundRect(x + av / 2 - tw / 2, y + av - 2, tw, 19, 9); c.fill();
        this.text(s, x + av / 2 - tw / 2 + 5, y + av - 3, f, '#fff');
        if (k === this.info.you) { c.strokeStyle = '#F5A524'; c.lineWidth = 3; c.beginPath(); c.arc(x + av / 2, y + av / 2, av / 2 + 4.5, 0, Math.PI * 2); c.stroke(); }
      });
    }
    const f1 = font(30, 'Black'), f2 = font(14, 'Medium');
    let big;
    if (rp.mode === 4) { const cc = rp.ctl_at(gt) || { score: [0, 0] }; big = [Math.floor(cc.score[this.rawOfSlot(0)] / 20), Math.floor(cc.score[this.rawOfSlot(1)] / 20)]; }
    else if (this.team && rp.mode !== 5) { big = [0, 0]; for (let k = 0; k < n; k++) if (S.alive[k]) big[this.info.slots[k]]++; }
    if (big) {
      const a = String(big[0]), b = String(big[1]);
      this.text(a, cx - 12 - this.tw(a, f1), box[1] + 8, f1, this.info.teamHex[0]);
      this.text(':', cx - 5, box[1] + 8, f1, 'rgb(170,160,150)');
      this.text(b, cx + 12, box[1] + 8, f1, this.info.teamHex[1]);
    } else if (rp.mode === 5) {
      const tot = rp.cfg.KING_LIVES ?? 3;
      for (const slot of [0, 1]) {
        const kk = rp.kings[this.rawOfSlot(slot)], kl = kk >= 0 ? rp.king_lives(kk, gt) : null, n_ = kl && S.alive[kk] ? kl[0] : 0;
        for (let h = 0; h < tot; h++) this.heart(slot === 0 ? cx - 14 - h * 19 : cx + 14 + h * 19, box[1] + 24, 16, (slot === 0 ? h : h) < n_, this.info.teamHex[slot]);
      }
    } else {
      const tt = Math.floor(S.left / 60) + ':' + String(S.left % 60).padStart(2, '0');
      this.text(tt, cx - this.tw(tt, f1) / 2, box[1] + 8, f1, INK);
    }
    const lab = (this.team || rp.mode === 5 ? Math.floor(S.left / 60) + ':' + String(S.left % 60).padStart(2, '0') + ' · ' : '') + (MODE_NAME[rp.mode] || '');
    this.text(lab, cx - this.tw(lab, f2) / 2, box[1] + 48, f2, SUB);
  }

  // 占点：顶栏下沿挂着的三个小圆片 A B C
  pointChips(rp, gt, oy) {
    const c = rp.ctl_at(Math.max(0, gt)) || { owner: [-1, -1, -1], taking: [-1, -1, -1], prog: [0, 0, 0] };
    const y = (this.compact ? 98 : 132) + oy, cap = rp.cfg.CAPTURE ?? 20;
    rp.points.forEach(([nm], i) => {
      const x = this.VW / 2 - 2 + (i - 1) * 44, r = 17, own = c.owner[i], tk = c.taking[i], pr = c.prog[i] / cap;
      this.pointDisc(x, y, r, nm, own, tk, pr);
    });
  }
  pointDisc(x, y, r, nm, own, tk, pr, ringW = 5) {
    const g = this.ctx;
    this.ellipse(x - r - 4, y - r - 4, x + r + 4, y + r + 4, 'rgb(255,252,246)');
    this.ellipse(x - r, y - r, x + r, y + r, own >= 0 ? this.info.teamHex[this.info.slotOfTeam[own]] : NEUTRAL);
    if (tk >= 0 && pr > 0) {
      g.strokeStyle = this.info.teamHex[this.info.slotOfTeam[tk]]; g.lineWidth = ringW; g.beginPath();
      g.arc(x, y, r + 1.5, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * Math.min(1, pr)); g.stroke();
    }
    const f = font(Math.round(r * 1.25), 'Black');
    this.text(nm, x - this.tw(nm, f) / 2, y - r * 0.86, f, '#fff');
  }

  // 场上跟着镜头的东西（hud._mode_parts）：占点名牌、复活倒计时、国王头顶的心
  modeItems(rp, A, gt, project) {
    const items = [];
    if (!project) return items;
    if (rp.mode === 4 && rp.points.length) {
      const c = gt >= 0 ? rp.ctl_at(gt) : null, cap = rp.cfg.CAPTURE ?? 20;
      rp.points.forEach(([nm, px, py, r], i) => {
        const q = project(px, py - r - 0.15, 0);
        if (!q) return;
        let pop = 0;
        for (const [t, j] of rp.captures) if (j === i && gt - (t + 1) >= 0 && gt - (t + 1) < 10) pop = 1 - (gt - t - 1) / 10;
        items.push(['pt', nm, q[0] | 0, q[1] | 0, c ? c.owner[i] : -1, c ? c.taking[i] : -1, c ? Math.round(c.prog[i] / cap * 100) / 100 : 0, Math.round(pop * 100) / 100]);
      });
    }
    if (rp.mode === 4 && gt >= 0) {
      for (let k = 0; k < rp.n; k++) {
        const B = A.L(k, gt), d = B.deaths[k];
        if (!d || gt < d[0] + 1) continue;
        const nxt = rp.life_span(k, B.life)[1];
        if (nxt === null) continue;
        const a = ease((gt - d[0] - 12) / 6) * (1 - ease((gt - nxt + 1) / 2));
        if (a < 0.02) continue;
        const [x, y] = rp.char_pos(k, nxt), q = project(x, y, 0.9);
        if (!q) continue;
        const tot = rp.cfg.RESPAWN ?? 60;
        items.push(['rs', k, q[0] | 0, q[1] | 0, Math.round(clamp((nxt - gt) / tot) * 100) / 100, Math.max(1, Math.ceil((nxt - gt) / 20 - 1e-6)), Math.round(a * 100) / 100]);
      }
    }
    if (rp.mode === 5 && gt >= -6) {
      for (const k of rp.kings) {
        if (k < 0) continue;
        const B = A.L(k, gt), d = B.deaths[k], kl = rp.king_lives(k, gt);
        const dead = !!d && gt >= d[0] + 1;
        if (dead && gt > d[0] + 14) continue;
        const pz = A.char_pose(k, gt);
        if (pz.visible === false) continue;
        const q = project(pz.x, pz.y, (pz.z || 0) + 1.62);
        if (!q) continue;
        const n_ = dead ? 0 : (kl ? kl[0] : (rp.cfg.KING_LIVES ?? 3));
        let brk = 0;
        for (const [t, kk] of rp.lifeloss) if (kk === k && gt - (t + 1) >= 0 && gt - (t + 1) < 14) brk = 1 - (gt - t - 1) / 14;
        if (dead) brk = Math.max(brk, 1 - (gt - d[0] - 1) / 14);
        const inv = kl && !dead ? kl[1] : 0;
        items.push(['kh', this.info.slots[k], q[0] | 0, q[1] | 0, n_, Math.round(brk * 100) / 100, inv > 0 && Math.floor(gt / 1.5) % 2 === 0, Math.round(clamp((gt + 6) / 6) * 100) / 100]);
      }
    }
    return items;
  }
  paintMode(rp, items) {
    const s = this.s, k0 = this.compact ? 0.62 : 1;
    for (const it of items) {
      const c = this.ctx;
      c.save();
      const px = it[2] / s, py = it[3] / s;
      c.translate(px, py); c.scale(k0, k0); c.translate(-px, -py);
      if (it[0] === 'pt') {
        const [, nm, , , own, tk, pr, pop] = it, r = 22 + 6 * pop;
        c.save(); c.shadowColor = 'rgba(30,40,70,0.3)'; c.shadowBlur = 8 * this.s * this.dpr; c.shadowOffsetY = 4 * this.s * this.dpr;
        this.ellipse(px - r - 5, py - 30 - r - 5, px + r + 5, py - 30 + r + 5, 'rgba(255,252,246,0.92)'); c.restore();
        this.pointDisc(px, py - 30, r, nm, own, tk, pr, 7);
      } else if (it[0] === 'rs') {
        const [, k, , , rem, sec, a] = it, r = 30, cy = py - 40, tc = this.tcol(k);
        c.globalAlpha = a;
        this.ellipse(px - r - 8, cy - r - 8, px + r + 8, cy + r + 8, 'rgba(255,252,246,0.9)');
        c.strokeStyle = tc; c.lineWidth = 7; c.beginPath(); c.arc(px, cy, r + 6, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * rem); c.stroke();
        this.avatar(k, px - r, cy - r, 2 * r, true);
        this.ellipse(px + r - 14, cy + r - 22, px + r + 16, cy + r + 8, tc);
        const f = font(26, 'Black'), sS = String(sec);
        this.text(sS, px + r + 1 - this.tw(sS, f) / 2, cy + r - 24, f, '#fff');
      } else if (it[0] === 'kh') {
        const [, slot, , , n_, brk, blink, a] = it, tot = rp.cfg.KING_LIVES ?? 3, gap = 30, x0 = px - gap * (tot - 1) / 2, col = this.info.teamHex[slot];
        c.globalAlpha = a;
        for (let i = 0; i < tot; i++) {
          if (i < n_) this.heart(x0 + i * gap, py - 14, 26, true, col, 1, false, blink);
          else if (i === n_ && brk > 0) { const u = 1 - brk; this.heart(x0 + i * gap, py - 14 - 34 * u, 26 * (1 + 0.5 * u), true, col, brk, true); }
          else this.heart(x0 + i * gap, py - 14, 26, false, col);
        }
      }
      c.restore();
    }
  }

  layer(a, fn) { const c = this.ctx; c.save(); c.globalAlpha = a; fn(); c.restore(); }

  toast(y, segs, a) {
    this.layer(a, () => {
      const f = font(24), ft = font(22, 'Medium'), av = 36, ftm = font(19);
      const tName = (t) => this.info.teamNames[this.info.slotOfTeam[t]];
      let wd = 0;
      for (const [kind, v] of segs) wd += kind === 'av' ? av + 8 : kind === 'team' ? this.tw(tName(v), ftm) + 28 : this.tw(kind === 'name' ? this.label(v) : v, kind === 'name' ? f : ft) + 10;
      const x0 = this.VW / 2 - wd / 2;
      this.panel(x0 - 22, y, x0 + wd + 14, y + 50, 25, PANEL, 8, 45);
      let x = x0;
      for (const [kind, v] of segs) {
        if (kind === 'av') {
          const col = this.team ? this.tcol(v) : this.info.colors[v];
          this.ellipse(x - 3, y + 4, x + av + 2, y + 9 + av, col);
          this.avatar(v, x, y + 7, av, false);
          x += av + 8;
        } else if (kind === 'name') { const s = this.label(v); this.text(s, x, y + 8, f, INK); x += this.tw(s, f) + 10; }
        else if (kind === 'team') {
          const s = tName(v), t = this.tw(s, ftm), c = this.ctx;
          c.fillStyle = this.info.teamHex[this.info.slotOfTeam[v]]; c.beginPath(); c.roundRect(x, y + 11, t + 18, 28, 14); c.fill();
          this.text(s, x + 9, y + 12, ftm, '#fff'); x += t + 28;
        } else { this.text(v, x, y + 10, ft, SUB); x += this.tw(v, ft) + 10; }
      }
    });
  }

  warn(y, text, a, done) {
    this.layer(done ? a : 0.6 + 0.4 * a, () => {
      const f = font(24), t = this.tw(text, f), x0 = this.VW / 2 - t / 2 - 16, c = this.ctx;
      this.panel(x0 - 24, y, x0 + t + 56, y + 48, 24, done ? PANEL : 'rgba(255,244,232,0.94)', 8, 45);
      const cx = x0 + 2, cy = y + 24;
      c.fillStyle = 'rgb(255,122,60)'; c.beginPath(); c.moveTo(cx - 12, cy + 10); c.lineTo(cx + 12, cy + 10); c.lineTo(cx, cy - 12); c.closePath(); c.fill();
      this.text('!', cx - 3.5, cy - 9, font(17, 'Black'), '#fff');
      this.text(text, x0 + 26, y + 7, f, done ? INK : 'rgb(200,80,40)');
    });
  }

  tag(k, px, py, a) {
    this.layer(a, () => {
      const f = font(22), s = this.label(k), t = this.tw(s, f), c = this.ctx;
      const col = this.team ? this.tcol(k) : this.info.colors[k];
      const x0 = px - t / 2 - 14, y0 = py - 40;
      c.fillStyle = col; c.globalAlpha *= 0.96;
      c.beginPath(); c.roundRect(x0, y0, t + 28, 34, 17); c.fill();
      c.beginPath(); c.moveTo(px - 7, y0 + 33); c.lineTo(px + 7, y0 + 33); c.lineTo(px, y0 + 42); c.closePath(); c.fill();
      this.text(s, x0 + 14, y0 + 1, f, '#fff');
    });
  }

  banner(text, col, a, seat) {
    this.layer(a, () => {
      const f = font(this.compact ? 44 : 58, 'Black'), t = this.tw(text, f), av = seat != null ? (this.compact ? 64 : 84) : 0;
      const wd = t + (av ? av + 22 : 0), x0 = this.VW / 2 - wd / 2, y = (this.compact ? 104 : 136) + Math.floor(18 * (1 - a)), c = this.ctx;
      const hh = this.compact ? 92 : 116;
      this.panel(x0 - 44, y, x0 + wd + 44, y + hh, hh / 2, PANEL, 14, 70);
      c.fillStyle = col; c.beginPath(); c.roundRect(x0 - 44, y + hh - 12, wd + 88, 12, 6); c.fill();
      let x = x0;
      if (av) { this.ellipse(x - 5, y + 11, x + av + 4, y + 20 + av, col); this.avatar(seat, x, y + 16, av, false); x += av + 22; }
      this.text(text, x, y + (this.compact ? 10 : 12), f, INK);
    });
  }
}

function mixHex(a, b, w) {
  const p = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16));
  const x = p(a), y = p(b);
  return 'rgb(' + x.map((v, i) => Math.round(v + (y[i] - v) * w)).join(',') + ')';
}
