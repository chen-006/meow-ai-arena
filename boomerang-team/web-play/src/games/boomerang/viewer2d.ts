// 经典 2D 战术俯视查看器（Canvas 2D）：支持一键与 3D 渲染器互切，也作为无 WebGL2 浏览器的零依赖降级方案。
import type { ParsedReplay, ReplayFrame } from './types';

import { displayName, teamHex } from './teams';

export interface RenderOptions {
  showGhost: boolean;       // 0.2 秒 AI 延迟残影
  humanDelayView: boolean;  // 真人和 AI 一样晚 4 步看到对手
  aimWorld?: { x: number; y: number } | null;
}

// 2D 俯视只要几种颜色：地面、草丛、墙、坑里有没有液面（照各图主题的主色）
const PAL2D: Record<string, { ground: string; bush: string; stone: string; liquid: '' | 'lava' | 'water' }> = {
  meadow: { ground: '#9CCB6E', bush: '#4E9A45', stone: '#8E96A3', liquid: '' },
  blossom: { ground: '#A9CF7A', bush: '#E79AB6', stone: '#99909E', liquid: '' },
  heath: { ground: '#A7B871', bush: '#8D6BA8', stone: '#7F8796', liquid: '' },
  sunset: { ground: '#C9B36A', bush: '#C7783E', stone: '#9B7F73', liquid: '' },
  ruins: { ground: '#A5BE79', bush: '#5E8F4B', stone: '#A59F8E', liquid: '' },
  glacier: { ground: '#E8F2F8', bush: '#9CC3D9', stone: '#9FB2C8', liquid: '' },
  dunes: { ground: '#E3C985', bush: '#7EA35A', stone: '#B99A6E', liquid: '' },
  canyon: { ground: '#D59A62', bush: '#8F9A4E', stone: '#A2654B', liquid: '' },
  oldtown: { ground: '#D6CDBE', bush: '#6DA357', stone: '#B4563F', liquid: '' },
  forest: { ground: '#7FAE5A', bush: '#3F7F3C', stone: '#7A5B42', liquid: '' },
  caldera: { ground: '#6E6266', bush: '#B46A3C', stone: '#4A4862', liquid: 'lava' },
  harbor: { ground: '#E8D8A8', bush: '#7FA65A', stone: '#9A7A5A', liquid: 'water' },
  keeps: { ground: '#9CC86A', bush: '#4F9A45', stone: '#A9A2B2', liquid: 'water' },
};
const getTheme = (name: string) => { const p = PAL2D[name] || PAL2D.meadow; return { liquidKind: p.liquid, stone: { side: p.stone }, bush: [p.bush, p.bush], ground: { base: p.ground } }; };

export class BoomerangViewer2D {
  canvas: HTMLCanvasElement;
  private ctx: CanvasRenderingContext2D;
  private scale = 24;
  private offX = 0;
  private offY = 0;
  private mapW = 16;
  private mapH = 16;
  private trails: { x: number; y: number }[][] = [];

  constructor(private container: HTMLElement) {
    this.canvas = document.createElement('canvas');
    this.canvas.style.width = '100%';
    this.canvas.style.height = '100%';
    this.canvas.style.display = 'block';
    this.container.appendChild(this.canvas);
    this.ctx = this.canvas.getContext('2d')!;
    this.resize();
  }

  resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = Math.max(300, this.container.clientWidth || 640);
    const h = Math.max(220, this.container.clientHeight || 360);
    this.canvas.width = Math.round(w * dpr);
    this.canvas.height = Math.round(h * dpr);
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.fit(w, h);
  }

  private fit(w: number, h: number) {
    const pad = 16;
    this.scale = Math.min((w - pad * 2) / this.mapW, (h - pad * 2) / this.mapH);
    this.offX = (w - this.mapW * this.scale) * 0.5;
    this.offY = (h - this.mapH * this.scale) * 0.5;
  }

  screenToWorld(clientX: number, clientY: number): { x: number; y: number } | null {
    const rect = this.canvas.getBoundingClientRect();
    if (!rect.width || !rect.height) return null;
    return {
      x: (clientX - rect.left - this.offX) / this.scale,
      y: (clientY - rect.top - this.offY) / this.scale,
    };
  }

  update(rp: ParsedReplay, frame: ReplayFrame, opts: RenderOptions) {
    this.mapW = rp.nx * rp.cell;
    this.mapH = rp.ny * rp.cell;
    const w = this.container.clientWidth || 640;
    const h = this.container.clientHeight || 360;
    this.fit(w, h);

    const ctx = this.ctx;
    const theme = getTheme(rp.theme);
    const s = this.scale;
    const ox = this.offX, oy = this.offY;
    const tx = (x: number) => ox + x * s;
    const ty = (y: number) => oy + y * s;

    // 背景
    ctx.fillStyle = theme.liquidKind === 'lava' ? '#2B1618' : theme.liquidKind === 'water' ? '#135E84' : '#1E242E';
    ctx.fillRect(0, 0, w, h);

    // 网格地形
    const pulse = 0.5 + 0.5 * Math.sin(performance.now() * 0.015);
    const cs = rp.cell * s + 0.6;
    for (let y = 0; y < rp.ny; y++) {
      for (let x = 0; x < rp.nx; x++) {
        const idx = y * rp.nx + x;
        const wv = rp.wave[idx] ?? -1;
        let c = rp.grid[idx];
        if (wv >= 0 && wv < frame.fallen) c = 2;
        if (c === 2) {
          ctx.fillStyle = theme.liquidKind === 'lava' ? '#D94B18' : theme.liquidKind === 'water' ? '#189CD2' : '#161B24';
        } else if (c === 1) {
          ctx.fillStyle = theme.stone.side;
        } else if (c === 3) {
          ctx.fillStyle = theme.bush[1];
        } else {
          ctx.fillStyle = theme.ground.base;
        }
        const px = ox + x * rp.cell * s;
        const py = oy + y * rp.cell * s;
        ctx.fillRect(px, py, cs, cs);
        if (c !== 2 && wv >= 0 && wv === frame.warn) {
          ctx.fillStyle = `rgba(255, 65, 35, ${0.25 + pulse * 0.45})`;
          ctx.fillRect(px, py, cs, cs);
        }
      }
    }

    // 占点圈
    if (rp.mode === 4 && rp.points.length) {
      const ctl = frame.control;
      const capMax = rp.cfg.CAPTURE || 20;
      rp.points.forEach((pt, i) => {
        const own = ctl?.owner[i] ?? -1;
        const tk = ctl?.taking[i] ?? -1;
        const pr = (ctl?.prog[i] ?? 0) / capMax;
        ctx.save();
        ctx.beginPath();
        ctx.arc(tx(pt.x), ty(pt.y), pt.r * s, 0, Math.PI * 2);
        ctx.fillStyle = own >= 0 ? teamHex(rp, own) + '38' : 'rgba(255,255,255,0.14)';
        ctx.fill();
        ctx.lineWidth = 2.5;
        ctx.strokeStyle = own >= 0 ? teamHex(rp, own) : '#FFFFFF';
        ctx.stroke();
        if (tk >= 0 && pr > 0) {
          ctx.beginPath();
          ctx.arc(tx(pt.x), ty(pt.y), (pt.r + 0.16) * s, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * pr);
          ctx.lineWidth = 4;
          ctx.strokeStyle = teamHex(rp, tk);
          ctx.stroke();
        }
        ctx.fillStyle = '#FFFFFF';
        ctx.font = `bold ${Math.max(12, Math.round(s * 0.75))}px sans-serif`;
        ctx.textAlign = 'center'; ctx.textBaseline = 'middle';
        ctx.fillText(pt.name, tx(pt.x), ty(pt.y));
        ctx.restore();
      });
    }

    const isTeam = rp.mode >= 2;
    const humanSeat = rp.humanSeat ?? -1;

    // 0.2s AI 延迟残影
    if (opts.showGhost && !opts.humanDelayView && frame.ghosts) {
      frame.ghosts.forEach((g, k) => {
        if (!g.alive || k === humanSeat) return;
        const real = frame.players[k];
        ctx.save();
        ctx.strokeStyle = 'rgba(100,242,255,0.65)';
        ctx.fillStyle = 'rgba(100,242,255,0.22)';
        ctx.setLineDash([4, 3]);
        if (real?.alive) {
          ctx.beginPath();
          ctx.moveTo(tx(g.x), ty(g.y));
          ctx.lineTo(tx(real.x), ty(real.y));
          ctx.stroke();
        }
        ctx.beginPath();
        ctx.arc(tx(g.x), ty(g.y), 0.4 * s, 0, Math.PI * 2);
        ctx.fill();
        ctx.stroke();
        ctx.restore();
      });
    }

    // 玩家 + 飞镖
    if (this.trails.length !== frame.players.length) {
      this.trails = frame.players.map(() => []);
    }
    frame.players.forEach((rawP, k) => {
      const gh = frame.ghosts?.[k];
      const useDelay = opts.humanDelayView && humanSeat >= 0 && k !== humanSeat && gh;
      const px = useDelay ? gh.x : rawP.x;
      const py = useDelay ? gh.y : rawP.y;
      const face = useDelay ? gh.face : rawP.face;
      const alive = useDelay ? gh.alive : rawP.alive;
      const st = useDelay ? gh.st : rawP.st;
      const col = isTeam ? teamHex(rp, rp.players[k]?.team ?? 0) : (rp.players[k]?.color || '#10C08A');

      if (alive) {
        ctx.save();
        ctx.translate(tx(px), ty(py));
        // 挥砍扇形
        if (st === 3) {
          ctx.beginPath();
          ctx.moveTo(0, 0);
          ctx.arc(0, 0, 1.5 * s, face - Math.PI / 3, face + Math.PI / 3);
          ctx.closePath();
          ctx.fillStyle = 'rgba(255,255,255,0.35)';
          ctx.fill();
        }
        // 本体圆
        ctx.beginPath();
        ctx.arc(0, 0, 0.42 * s, 0, Math.PI * 2);
        ctx.fillStyle = col;
        ctx.fill();
        ctx.lineWidth = k === humanSeat ? 3 : 2;
        ctx.strokeStyle = k === humanSeat ? '#FFE066' : '#FFFFFF';
        ctx.stroke();
        // 朝向指针
        ctx.beginPath();
        ctx.moveTo(0, 0);
        ctx.lineTo(Math.cos(face) * 0.56 * s, Math.sin(face) * 0.56 * s);
        ctx.lineWidth = 2.5;
        ctx.strokeStyle = '#FFFFFF';
        ctx.stroke();
        // 国王标
        if (rp.mode === 5 && (rp.kings[0] === k || rp.kings[1] === k)) {
          // 简笔金冠（不用 emoji，各平台字体一致）
          const cw = 0.42 * s, ch = 0.26 * s, cy = -0.6 * s;
          ctx.beginPath();
          ctx.moveTo(-cw / 2, cy);
          ctx.lineTo(-cw / 2, cy - ch);
          ctx.lineTo(-cw / 4, cy - ch * 0.45);
          ctx.lineTo(0, cy - ch * 1.1);
          ctx.lineTo(cw / 4, cy - ch * 0.45);
          ctx.lineTo(cw / 2, cy - ch);
          ctx.lineTo(cw / 2, cy);
          ctx.closePath();
          ctx.fillStyle = '#FFC93A';
          ctx.fill();
          ctx.lineWidth = 1.5;
          ctx.strokeStyle = '#B9770E';
          ctx.stroke();
        }
        // 简称
        ctx.fillStyle = '#FFFFFF';
        ctx.font = `600 ${Math.max(10, Math.round(s * 0.42))}px sans-serif`;
        ctx.textAlign = 'center';
        ctx.fillText(rp.players[k] ? displayName(rp.players[k].name) : `P${k}`, 0, 0.78 * s);
        ctx.restore();
      }

      // 飞镖
      const rawD = frame.darts[k];
      const dSt = useDelay ? gh.dartSt : rawD.st;
      const dx = useDelay ? gh.dartX : rawD.x;
      const dy = useDelay ? gh.dartY : rawD.y;
      const flying = dSt === 1 || dSt === 2 || dSt === 5;
      const grounded = dSt === 3 || dSt === 4;
      if (flying) {
        const tr = this.trails[k];
        tr.push({ x: dx, y: dy });
        if (tr.length > 8) tr.shift();
        ctx.save();
        ctx.beginPath();
        tr.forEach((pt, idx) => idx === 0 ? ctx.moveTo(tx(pt.x), ty(pt.y)) : ctx.lineTo(tx(pt.x), ty(pt.y)));
        ctx.lineWidth = 3;
        ctx.strokeStyle = dSt === 1 ? '#38D8FF' : '#FFD147';
        ctx.stroke();
        ctx.beginPath();
        ctx.arc(tx(dx), ty(dy), 0.30 * s, 0, Math.PI * 2);
        ctx.fillStyle = dSt === 1 ? '#38D8FF' : '#FFD147';
        ctx.fill();
        ctx.strokeStyle = '#FFFFFF';
        ctx.lineWidth = 1.5;
        ctx.stroke();
        ctx.restore();
      } else if (grounded) {
        this.trails[k] = [];
        ctx.save();
        ctx.beginPath();
        ctx.arc(tx(dx), ty(dy), 0.28 * s, 0, Math.PI * 2);
        ctx.fillStyle = 'rgba(255,224,102,0.55)';
        ctx.fill();
        ctx.strokeStyle = '#FFE066';
        ctx.lineWidth = 2;
        ctx.stroke();
        ctx.restore();
      } else {
        this.trails[k] = [];
      }
    });

    // 真人瞄准线
    const me = humanSeat >= 0 ? frame.players[humanSeat] : null;
    if (me?.alive && opts.aimWorld) {
      ctx.save();
      ctx.setLineDash([6, 4]);
      ctx.strokeStyle = '#FFE066';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(tx(me.x), ty(me.y));
      ctx.lineTo(tx(opts.aimWorld.x), ty(opts.aimWorld.y));
      ctx.stroke();
      ctx.restore();
    }
  }

  destroy() {
    this.canvas.remove();
  }
}
