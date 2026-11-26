// 轻量合成音效（Web Audio，不下载任何音频文件）：扔镖呼啸、挥砍破风、踢飞闷响、镖撞镖脆响、出局提示、翻点、国王掉命、冲刺、接镖。
// 默认打开，工具栏一键静音（记在 localStorage）。浏览器要求用户先有一次点击 / 按键才能出声，第一次 resume() 在开局时调用。
const LS = 'ep11_sfx_on';
let ctx: AudioContext | null = null;
let master: GainNode | null = null;
let noiseBuf: AudioBuffer | null = null;
let on = (() => { try { return localStorage.getItem(LS) !== '0'; } catch { return true; } })();
const last = new Map<string, number>();

function ac(): AudioContext | null {
  if (ctx) return ctx;
  try {
    const C = (window as any).AudioContext || (window as any).webkitAudioContext;
    if (!C) return null;
    ctx = new C() as AudioContext;
    master = ctx.createGain(); master.gain.value = 0.55; master.connect(ctx.destination);
    noiseBuf = ctx.createBuffer(1, ctx.sampleRate * 0.6, ctx.sampleRate);
    const d = noiseBuf.getChannelData(0);
    for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
  } catch { ctx = null; }
  return ctx;
}

export const sfx = {
  get on() { return on; },
  set(v: boolean) { on = v; try { localStorage.setItem(LS, v ? '1' : '0'); } catch { /* 无痕模式等 */ } if (v) this.resume(); },
  resume() { const c = ac(); if (c && c.state === 'suspended') c.resume().catch(() => {}); },
  /** kind：throw slash kick kickhit clash ko koYou capture king dash catch drop; vol 0..1（离"你"越远越小） */
  play(kind: string, vol = 1) {
    if (!on || vol <= 0.02) return;
    const c = ac();
    if (!c || !master || c.state !== 'running') return;
    const now = c.currentTime;
    const prev = last.get(kind) || 0;
    if (now - prev < 0.035) return;          // 同一帧里好几个人同时扔：只响一次
    last.set(kind, now);
    const out = c.createGain(); out.gain.value = vol; out.connect(master);
    const noise = (dur: number, f0: number, f1: number, q: number, g0: number, type: BiquadFilterType = 'bandpass') => {
      const s = c.createBufferSource(); s.buffer = noiseBuf;
      const f = c.createBiquadFilter(); f.type = type; f.Q.value = q;
      f.frequency.setValueAtTime(f0, now); f.frequency.exponentialRampToValueAtTime(Math.max(40, f1), now + dur);
      const g = c.createGain(); g.gain.setValueAtTime(0.0001, now); g.gain.exponentialRampToValueAtTime(g0, now + Math.min(0.03, dur * 0.25)); g.gain.exponentialRampToValueAtTime(0.0001, now + dur);
      s.connect(f); f.connect(g); g.connect(out); s.start(now); s.stop(now + dur + 0.02);
    };
    const tone = (dur: number, f0: number, f1: number, type: OscillatorType, g0: number, delay = 0) => {
      const o = c.createOscillator(); o.type = type;
      o.frequency.setValueAtTime(f0, now + delay); o.frequency.exponentialRampToValueAtTime(Math.max(20, f1), now + delay + dur);
      const g = c.createGain(); g.gain.setValueAtTime(0.0001, now + delay); g.gain.exponentialRampToValueAtTime(g0, now + delay + 0.012); g.gain.exponentialRampToValueAtTime(0.0001, now + delay + dur);
      o.connect(g); g.connect(out); o.start(now + delay); o.stop(now + delay + dur + 0.02);
    };
    switch (kind) {
      case 'throw': noise(0.26, 900, 2600, 2.2, 0.32); tone(0.18, 520, 900, 'triangle', 0.05); break;
      case 'slash': noise(0.2, 3200, 900, 1.4, 0.42); break;
      case 'kick': noise(0.14, 600, 220, 1.2, 0.3); break;
      case 'kickhit': tone(0.22, 150, 55, 'sine', 0.55); noise(0.12, 900, 300, 1.0, 0.25, 'lowpass'); break;
      case 'clash': tone(0.32, 1960, 1880, 'triangle', 0.22); tone(0.25, 2930, 2900, 'sine', 0.12); noise(0.08, 5000, 3000, 2, 0.2); break;
      case 'ko': tone(0.28, 330, 140, 'square', 0.08); noise(0.22, 1200, 200, 0.8, 0.3, 'lowpass'); break;
      case 'koYou': tone(0.12, 660, 660, 'triangle', 0.14); tone(0.16, 990, 990, 'triangle', 0.14, 0.09); break;
      case 'capture': tone(0.18, 784, 784, 'triangle', 0.12); tone(0.22, 1175, 1175, 'triangle', 0.12, 0.1); break;
      case 'king': tone(0.4, 523, 392, 'square', 0.07); tone(0.5, 1047, 784, 'triangle', 0.1); noise(0.3, 3000, 800, 1, 0.18); break;
      case 'dash': noise(0.22, 400, 1800, 1.0, 0.22); break;
      case 'catch': tone(0.06, 1400, 1600, 'sine', 0.08); break;
      case 'drop': tone(0.09, 420, 300, 'triangle', 0.08); break;
      case 'start': tone(0.12, 523, 523, 'triangle', 0.12); tone(0.12, 659, 659, 'triangle', 0.12, 0.1); tone(0.2, 784, 784, 'triangle', 0.14, 0.2); break;
      case 'win': [523, 659, 784, 1047].forEach((f, i) => tone(0.22, f, f, 'triangle', 0.13, i * 0.1)); break;
      case 'lose': [392, 330, 262].forEach((f, i) => tone(0.25, f, f * 0.98, 'triangle', 0.12, i * 0.14)); break;
    }
  },
};
