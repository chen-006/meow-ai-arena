// 第 11–12 期《回旋镖大作战（OA 争霸）》期页面：
//   - 高质感首屏 + 四大可玩模式直达入口
//   - 第 12 集 5v5 团队赛专区：6 回合 5.5:0.5 总比分 + 交互式 6 回合 3×3“如果当时”矩阵探索器 + 10 人贡献榜 & 终版回炉升降榜 + 平行宇宙实验
//   - 第 11 集海选专区：13 人海选 BT 榜 + 13×13 1v1 矩阵 + 暗标拍卖复盘
//   - 第 11–12 集名场面 3D 回放画廊 + 26 份源码入口
import { avatarHtml, errorHtml, esc, prerendered, siteChrome } from '../common/site';
import { asset, heroBg } from '../common/ui';
import { genUrl } from '../common/wasm';
import './ep11.css';
import { loadEp11 } from '../games/boomerang/data';
import type { Ep11Data, Ep12Round } from '../games/boomerang/types';

const PLANS = ['正式', '备选1', '备选2'] as const;
const fmtSigned = (n: number, d = 1) => `${n > 0 ? '+' : ''}${n.toFixed(d)}`;
const rk = (n: number) => `<span class="rank${n <= 3 ? ' r' + n : ''}">${n}</span>`;

// 第 12 集 3×3 矩阵：>50% = Astra 队占优（Astra 队的蓝），<50% = Fable 队占优（Fable 队的红）
function wr12Color(wr: number): string {
  if (wr >= 50) {
    const t = Math.min(1, (wr - 50) / 48);
    return `rgb(${Math.round(240 - t * 128)},${Math.round(246 - t * 72)},255)`;
  }
  const t = Math.min(1, (50 - wr) / 35);
  return `rgb(255,${Math.round(244 - t * 120)},${Math.round(236 - t * 124)})`;
}

function wrColor(wr: number): string {
  // 第 11 集 1v1 矩阵（行选手视角）：50% 中性米白，>50% 暖橙（行选手占优），<50% 清蓝
  if (wr >= 50) {
    const t = Math.min(1, (wr - 50) / 48);
    const r = Math.round(252);
    const g = Math.round(244 - t * 115);
    const b = Math.round(234 - t * 125);
    return `rgb(${r},${g},${b})`;
  }
  const t = Math.min(1, (50 - wr) / 35);
  const r = Math.round(240 - t * 105);
  const g = Math.round(246 - t * 55);
  const b = Math.round(255);
  return `rgb(${r},${g},${b})`;
}

function matrixCellDetailHtml(rd: Ep12Round, ai: number, fi: number): string {
  const aKey = PLANS[ai], fKey = PLANS[fi];
  const cell = rd.matrix[ai]?.[fi];
  const pa = rd.plans.astra[aKey];
  const pf = rd.plans.fable[fKey];
  const fmtPos = (obj: Record<string, [number, number]>) =>
    Object.entries(obj || {}).map(([k, [x, y]]) => `${k}(${x},${y})`).join(' · ');

  let cliffNote = '';
  if (rd.r === 4) {
    if (ai === 1 && fi === 0) cliffNote = '本回合最低点：Astra 留守 B、Hydrogen 游走，被 Fable 的 3A-2C 压到 36.30%。';
    else if (ai === 2 && fi === 0) cliffNote = '六回合最大落差（+60.10pp）：只把 Astra 与 Hydrogen 的 HOLD / ROAM 互换，胜率到 96.40%。';
  } else if (rd.r === 1 && ai === 1 && fi === 0) {
    cliffNote = '胜负翻转：Astra 改用备选 1，胜率降到 45.00%，Fable 队反超。';
  } else if (rd.r === 2 && ai === 0 && fi === 1) {
    cliffNote = '如果 Fable 队当时没改阵（沿用 R1 的空白战术），胜率会从 19.9% 回到 47.3%。';
  }

  return `
    <h3>R${rd.r} ${esc(rd.mapCn)}（${esc(rd.modeCn)}）· Astra「${aKey}」vs Fable「${fKey}」</h3>
    <div style="display:flex;align-items:baseline;gap:12px;flex-wrap:wrap">
      <span style="font-size:24px;font-weight:900;color:${(cell?.wr ?? 50) >= 50 ? '#e04838' : '#2563eb'}">Astra 胜率 ${cell ? cell.wr.toFixed(2) : '—'}%</span>
      <span class="muted small">± ${cell ? cell.se.toFixed(2) : '—'}%（500 种子镜像 1,000 局）</span>
    </div>
    ${cliffNote ? `<div class="mx12-note">${cliffNote}</div>` : ''}
    <div class="mx12-plans">
      <div class="mx12-pbox">
        <h4><i class="tdot t0"></i>Astra 队 · ${aKey}${pa?.formation?.国王 ? `（国王：${esc(pa.formation.国王)}）` : ''}</h4>
        <div class="muted">${esc(fmtPos(pa?.formation?.站位 || {}))}</div>
        <pre>${esc(pa?.plan || '（无战术单）')}</pre>
      </div>
      <div class="mx12-pbox">
        <h4><i class="tdot t1"></i>Fable 队 · ${fKey}${pf?.formation?.国王 ? `（国王：${esc(pf.formation.国王)}）` : ''}</h4>
        <div class="muted">${esc(fmtPos(pf?.formation?.站位 || {}))}</div>
        <pre>${esc(pf?.plan || '（缺交 / 空白战术单）')}</pre>
      </div>
    </div>
    <div class="actions" style="margin-top:4px">
      <a class="btn btn-primary btn-sm" href="watch.html#mode=${rd.mode}&map=${rd.map}&preset=official-${rd.map}">在 3D 实验台看这张图的 5v5</a>
    </div>
  `;
}

function matrixSectionHtml(rd: Ep12Round, ai = 0, fi = 0): string {
  return `
    <div class="panel" style="padding:14px">
      <p class="muted small" style="margin:0 0 8px">行 = Astra 队方案，列 = Fable 队方案；点击任意格查看当时两队布阵坐标与战术单（数值为 Astra 队胜率 ± SE）：</p>
      <table class="mx12-table">
        <thead><tr><th>Astra ＼ Fable</th>${PLANS.map(p => `<th>Fable ${p}</th>`).join('')}</tr></thead>
        <tbody>
          ${PLANS.map((ap, r) => `<tr><th>Astra ${ap}</th>${PLANS.map((fp, c) => {
            const cell = rd.matrix[r]?.[c];
            const wr = cell?.wr ?? 50;
            const act = r === ai && c === fi ? ' active' : '';
            return `<td><button type="button" class="mx12-cell-btn${act}" data-mx-r="${r}" data-mx-c="${c}" style="background:${wr12Color(wr)}">
              <span class="v">${wr.toFixed(1)}%</span>
              <span class="se">±${cell?.se.toFixed(2) ?? '0'}%</span>
            </button></td>`;
          }).join('')}</tr>`).join('')}
        </tbody>
      </table>
      <p class="muted small" style="margin:8px 0 0">${esc(rd.highlight)}</p>
    </div>
    <div class="panel mx12-detail" id="mx12-detail">${matrixCellDetailHtml(rd, ai, fi)}</div>
  `;
}

function render(d: Ep11Data, page: HTMLElement | null): string {
  const S = (id: string) => d.standings11.find(x => x.id === id)!;
  const defRound = d.ep12.rounds.find(r => r.r === 4) || d.ep12.rounds[0];

  const teamMemberChip = (id: string) => {
    const s = S(id);
    return `<span class="score12-mem"><img src="${asset('img/avatars/' + s.avatar + '.webp')}" alt="">${esc(s.short)}</span>`;
  };

  // 10 人团队贡献榜
  const contribSorted = [...d.ep12.contrib].sort((a, b) => b.totalKills - a.totalKills);
  const contribTable = `<table class="grid"><thead><tr><th>排名</th><th>选手</th><th>队伍</th><th class="n">正赛总击杀</th><th class="n">总阵亡</th><th class="n">K/D</th><th class="n">占点（秒/局）</th><th class="n">场均误杀队友</th><th class="n">终局存活率</th></tr></thead><tbody>
    ${contribSorted.map((c, idx) => {
      const s = S(c.id);
      return `<tr>
        <td>${rk(idx + 1)}</td>
        <td><span class="who">${avatarHtml(s.avatar, s.short)}<span><b>${esc(s.short)}</b><small>${esc(s.model)}</small></span></span></td>
        <td><span class="tname t${c.team}"><i class="tdot t${c.team}"></i>${c.team === 0 ? 'Astra 队' : 'Fable 队'}</span></td>
        <td class="n"><b>${c.totalKills.toLocaleString()}</b></td>
        <td class="n">${c.totalDeaths.toLocaleString()}</td>
        <td class="n">${c.kd.toFixed(2)}</td>
        <td class="n">${c.avgCapSec.toFixed(1)}</td>
        <td class="n">${c.avgTK.toFixed(2)}</td>
        <td class="n">${c.avgSurvive.toFixed(1)}%</td>
      </tr>`;
    }).join('')}
  </tbody></table>`;

  // 终版回炉个人赛升降榜（实验 9）
  const retestRows = d.standings11.filter(s => s.retest).sort((a, b) => (b.retest!.delta) - (a.retest!.delta));
  const retestTable = `<table class="grid"><thead><tr><th>变化排位</th><th>选手</th><th class="n">海选实力分</th><th class="n">终版回炉分</th><th class="n">综合变化 ± SE</th><th class="n">单挑变化</th><th class="n">混战变化</th><th class="n">终版自测胜率</th><th>现场验证</th></tr></thead><tbody>
    ${retestRows.map((s, idx) => {
      const r = s.retest!;
      const cls = r.delta >= 0 ? 'delta-up' : 'delta-dn';
      return `<tr>
        <td>${rk(idx + 1)}</td>
        <td><span class="who">${avatarHtml(s.avatar, s.short)}<span><b>${esc(s.short)}</b><small>${esc(s.model)}</small></span></span></td>
        <td class="n">${fmtSigned(r.solo)}</td>
        <td class="n"><b>${fmtSigned(r.final)}</b></td>
        <td class="n ${cls}">${fmtSigned(r.delta)} ± ${r.se.toFixed(1)}</td>
        <td class="n">${fmtSigned(r.duelDelta)}</td>
        <td class="n">${fmtSigned(r.ffaDelta)}</td>
        <td class="n">${r.selfWr.toFixed(1)}%</td>
        <td><a class="btn btn-sm" href="watch.html#mode=0&map=yinyang&bots=${s.id}-t,${s.id}&go=1">终版打原版</a></td>
      </tr>`;
    }).join('')}
  </tbody></table>`;

  // 第 11 集海选总榜
  const standings11Table = `<table class="grid"><thead><tr><th>名次</th><th>选手</th><th class="n">综合实力分</th><th class="n">95% 区间</th><th class="n">1v1 实力分</th><th class="n">4人混战分</th><th class="n">1v1 胜率</th><th class="n">故障</th><th>操作</th></tr></thead><tbody>
    ${d.standings11.map(s => `<tr>
      <td>${rk(s.rank)}</td>
      <td><span class="who">${avatarHtml(s.avatar, s.short)}<span><b>${esc(s.short)}</b><small>${esc(s.model)} · ${esc(s.tool)}</small></span></span></td>
      <td class="n"><b>${fmtSigned(s.rating)}</b></td>
      <td class="n muted">[${fmtSigned(s.ratingCi[0])}, ${fmtSigned(s.ratingCi[1])}]</td>
      <td class="n">${fmtSigned(s.duelRating)}</td>
      <td class="n">${fmtSigned(s.ffaRating)}</td>
      <td class="n">${s.duelWr.toFixed(1)}%</td>
      <td class="n">${s.faults}</td>
      <td><a class="btn btn-sm" href="play.html#opp=${s.id}">挑战它</a> <a class="btn btn-sm" href="source.html#${s.id}">源码</a></td>
    </tr>`).join('')}
  </tbody></table>`;

  // 13x13 1v1 胜率矩阵
  const ids13 = d.standings11.map(s => s.id);
  const duelMatrixHtml = `<table class="mx11-table"><thead><tr><th>行 对 列</th>${ids13.map(id => `<th>${esc(S(id).short)}</th>`).join('')}</tr></thead><tbody>
    ${ids13.map(a => `<tr><th>${esc(S(a).short)}</th>${ids13.map(b => {
      if (a === b) return '<td class="self">—</td>';
      const [w, dr, l] = d.duelPairs[a]?.[b] || [0, 0, 0];
      const tot = w + dr + l;
      const pct = tot ? ((w + 0.5 * dr) / tot) * 100 : 50;
      return `<td title="${esc(S(a).short)} vs ${esc(S(b).short)}：${w}胜 ${dr}平 ${l}负"><span class="mx11-pt" style="background:${wrColor(pct)};color:#1c1917">${pct.toFixed(0)}%</span></td>`;
    }).join('')}</tr>`).join('')}
  </tbody></table>`;

  // 拍卖全记录卡片
  const auctionHtml = `<div class="auc11-grid">
    ${d.auction.events.filter(e => e.winner).map(e => {
      const s = S(e.candidate);
      const winTeam = e.winner === 'astra' ? 'Astra 队' : 'Fable 队';
      const winCol = e.winner === 'astra' ? '#e04838' : '#2563eb';
      const quote = e.winner === 'astra' ? e.astraNote : e.fableNote;
      return `<div class="panel auc11-card">
        <div class="auc11-head">
          <span class="who">${avatarHtml(s.avatar, s.short)}<span><b>${esc(s.short)}</b><small>海选第 ${s.rank} · ${fmtSigned(s.rating)}</small></span></span>
          <span class="auc11-price">¥${e.price}</span>
        </div>
        <div style="font-size:12px;font-weight:700;color:${winCol}">归属：${winTeam} · ${esc(e.reason)}（${e.rounds} 轮暗标）</div>
        <p class="auc11-quote">“${esc(quote)}”</p>
      </div>`;
    }).join('')}
  </div>`;

  const presetUrlByExp: Record<string, string> = {
    allstar_vs_5astra: 'watch.html#preset=allstar-vs-5astra',
    kimi_carbon_swap: 'watch.html#preset=auction-swap',
    carry_1v4: 'watch.html#preset=carry-1v4',
    no_friendly_fire: 'watch.html#preset=official-caldera',
  };

  const html = `
  <section class="hero ep11-hero">${heroBg('ep11')}
    <div class="wrap">
      <span class="eyebrow">第 11–12 期 · 回旋镖大作战（OA 争霸）</span>
      <h1>带着 0.2 秒延迟的实时乱斗<span class="hl">13 人海选，再拍卖组队打 5v5</span></h1>
      <p class="lead">一把回旋镖、一刀挥砍、一脚飞踢，AI 看到的永远是 0.2 秒前的战场。网页里跑的是比赛原版裁判引擎和全部 26 份选手程序（13 份海选版、10 份团队终版、3 档陪练），画面是正片的 3D 场景原样搬进网页（13 张图由正片渲染器导出、体素小人和特效逐行移植），60 帧实时渲染：可以亲自下场打 1v1、4 人混战、4v4 和 5v5，打几局定级插进总榜，也可以自己编排对局。</p>
      <div class="actions"><a class="btn btn-primary btn-lg" href="play.html">下场对战</a><a class="btn" href="play.html#tab=rating">多局定级</a><a class="btn" href="watch.html">程序互打 · 5v5</a><a class="btn" href="replay.html#f=r2_g1591">看回放</a><a class="btn" href="source.html#astra-t">源码</a></div>
      <div class="stats"><div><b>32,904</b><span>场第 11 集海选</span></div><div><b>10,600</b><span>场第 12 集 5v5 正赛</span></div><div><b>5.5 : 0.5</b><span>Astra 队夺冠</span></div></div>
    </div>
  </section>

  <div class="wrap">
    <section class="block" id="play-modes">
      <div class="block-head"><div><h2>四种玩法</h2><p>都在浏览器里实时运行原版引擎和选手程序，画面是正片同款体素 3D，随时可以打开 0.2 秒延迟残影。</p></div></div>
      <div class="modes11">
        <a class="panel mode11-card" href="play.html">
          <span class="m-tag">对战</span>
          <h3>下场对战</h3>
          <p>WASD 移动、鼠标瞄准、左键扔镖、右键近战、空格冲刺。第 11 集 1v1 / 4 人混战 / 4v4，第 12 集 5v5 三赛制都能打。</p>
          <span class="m-go">开始 →</span>
        </a>
        <a class="panel mode11-card" href="play.html#tab=rating">
          <span class="m-tag">定级</span>
          <h3>多局定级 · 插进总榜</h3>
          <p>由弱到强连打 6 或 12 局，按比赛同款 Bradley–Terry 公式算出你的实力分，和 23 个 AI 版本排在一起。</p>
          <span class="m-go">去定级 →</span>
        </a>
        <a class="panel mode11-card" href="watch.html">
          <span class="m-tag">实验台</span>
          <h3>程序互打 · 赛后实验</h3>
          <p>26 份程序任意编排 1v1、4 人、4v4、5v5；终版 GLM 打海选 GLM、全明星打 5 个 Astra、¥1 的 Kimi 和 ¥599 的 Carbon 互换，都能现场重跑。</p>
          <span class="m-go">进实验台 →</span>
        </a>
        <a class="panel mode11-card" href="replay.html#f=r2_g1591">
          <span class="m-tag">回放</span>
          <h3>名场面回放</h3>
          <p>一镖四穿、0.05 分绝杀、双王相距 2.25 厘米的决战、GLM 七刀误砍 Carbon，加上第 11 集海选和 4v4 样局，共 12 段，可以单步看。</p>
          <span class="m-go">看回放 →</span>
        </a>
      </div>
    </section>

    <section class="block" id="ep12-finals">
      <div class="block-head"><div><h2>第 12 集 · 5v5 团队总决赛（5.5 : 0.5）</h2><p>火山口歼灭、三座栈桥占点、双堡刺王三大赛制六回合决战，共 10,600 场正式赛 + 54,000 场赛后 3×3“如果当时”矩阵。</p></div></div>

      <div class="panel score12-hero">
        <div class="score12-team">
          <div class="score12-tname"><span class="score12-dot" style="background:#2E8CFF"></span>Astra 队（冠军）</div>
          <div class="score12-roster">${d.ep12.astraTeam.map(teamMemberChip).join('')}</div>
        </div>
        <div class="score12-mid">
          <div class="score12-big"><span style="color:#2E8CFF">5.5</span> : <span style="color:#FF5B4D">0.5</span></div>
          <div class="score12-sub">${d.ep12.totalWld[0].toLocaleString()} 胜 · ${d.ep12.totalWld[1].toLocaleString()} 负 · ${d.ep12.totalWld[2]} 平（${(((d.ep12.totalWld[0] + d.ep12.totalWld[2] / 2) / (d.ep12.totalWld[0] + d.ep12.totalWld[1] + d.ep12.totalWld[2])) * 100).toFixed(1)}%，平局记半胜）</div>
        </div>
        <div class="score12-team right">
          <div class="score12-tname">Fable 队<span class="score12-dot" style="background:#FF5B4D"></span></div>
          <div class="score12-roster">${d.ep12.fableTeam.map(teamMemberChip).join('')}</div>
        </div>
      </div>

      <div class="rounds12-strip" id="rd12-strip" role="tablist">
        ${d.ep12.rounds.map(r => `
          <button type="button" class="rd12-pill" role="tab" data-rd="${r.r}" aria-selected="${r.r === defRound.r}">
            <span class="rt"><b>R${r.r} ${esc(r.modeCn)}</b><span>${r.pts[0]}:${r.pts[1]}</span></span>
            <span class="rm">${esc(r.mapCn)} · ${r.seeds * 2} 局</span>
            <span class="rw" style="color:${r.wr[0] >= 52 ? '#e04838' : '#d97706'}">${r.wr[0].toFixed(1)}%</span>
          </button>
        `).join('')}
      </div>

      <h3 style="margin:0 0 10px;font-size:16px">交互式 3×3 “如果当时” 战术矩阵探索器</h3>
      <div class="mx12-wrap" id="mx12-box">${matrixSectionHtml(defRound, 2, 0)}</div>

      <div class="tabs btabs" id="ep12-tabs" role="tablist" style="margin-top:20px">
        <button role="tab" data-t12="contrib" aria-selected="true">10 人 5v5 贡献总榜</button>
        <button role="tab" data-t12="retest" aria-selected="false">终版回炉个人赛升降榜</button>
      </div>
      <div class="panel table-wrap t12-panel" data-t12-panel="contrib">${contribTable}</div>
      <div class="panel table-wrap t12-panel" data-t12-panel="retest" hidden>${retestTable}</div>

      <div class="exp12-grid">
        ${d.ep12.exp3.experiments.map(ex => `
          <div class="panel exp12-card">
            <div class="eh"><h3>${esc(ex.title)}</h3><span class="eres">${esc(ex.result)}</span></div>
            <p>${esc(ex.subtitle)} · ${esc(ex.detail)}</p>
            <div><a class="btn btn-sm" href="${presetUrlByExp[ex.id] || 'watch.html'}">去 5v5 实验台现场复现 →</a></div>
          </div>
        `).join('')}
      </div>
    </section>

    <section class="block" id="ep11-trials">
      <div class="block-head"><div><h2>第 11 集 · 13 人海选榜、1v1 矩阵与暗标拍卖</h2><p>13 位模型在 6 张图上完成 32,904 场单挑与四人混战，随后由 Astra、Fable 两位队长各持 ¥1,000 预算暗标抢人组队。</p></div></div>
      <div class="maps11">
        ${d.maps.filter(m => m.group !== 'team').map(m => `
          <a class="panel map11-card" href="play.html#mode=${m.modes[0]}&map=${m.id}">
            <img src="${asset('img/ep11/maps/' + m.id + '.webp')}" alt="${esc(m.name)}" loading="lazy" width="480" height="300">
            <span class="map11-meta"><b>${esc(m.name)}</b><span>${esc(m.theme)} · ${m.group === 'team4' ? '4v4' : '1v1 / 4 人'}</span></span>
          </a>`).join('')}
      </div>
      <p class="muted small maps11-note">第 11 集正式比赛的 10 张定稿图：6 张单挑 / 4 人混战图用于海选，4 张 4v4 专属图（沙漏、峡谷、老城、林地）。点图直接下场打。</p>
      <div class="tabs btabs" id="ep11-tabs" role="tablist">
        <button role="tab" data-t11="standings" aria-selected="true">海选 BT 总榜（13 人）</button>
        <button role="tab" data-t11="matrix" aria-selected="false">13×13 1v1 胜率矩阵</button>
        <button role="tab" data-t11="auction" aria-selected="false">暗标拍卖全记录</button>
      </div>
      <div class="panel table-wrap t11-panel" data-t11-panel="standings">${standings11Table}</div>
      <div class="panel table-wrap t11-panel" data-t11-panel="matrix" hidden>${duelMatrixHtml}</div>
      <div class="t11-panel" data-t11-panel="auction" hidden>${auctionHtml}</div>
    </section>

    <section class="block" id="replays">
      <div class="block-head"><div><h2>名场面 3D 回放精选</h2><p>点任意卡片立即在 3D 战场直达高光瞬间，可以 0.5 倍慢放、单步、拖动，打开 0.2 秒 AI 延迟残影。</p></div></div>
      <div class="rp11-grid">
        ${d.replays.map(r => `
          <a class="panel rp11-card" href="replay.html#f=${r.id}${r.focusSec > 0 ? '&focus=1' : ''}">
            <div class="rp11-top">
              <span class="rp11-tag">第 ${r.ep} 集 · ${esc(r.tag)}</span>
              <span class="muted small">${r.steps} 步 · ${esc(r.map)}</span>
            </div>
            <h3>${esc(r.title)}</h3>
            <p>${esc(r.desc)}</p>
          </a>
        `).join('')}
      </div>
    </section>

    <section class="block" id="docs">
      <div class="block-head"><div><h2>完整规则与 26 份源码</h2><p>全部 13 份海选原版、10 份团队赛终版、3 档陪练源码与原版裁判引擎 <code>dart.hpp</code> 均可在线阅读。</p></div></div>
      <div class="actions">
        <a class="btn btn-primary" href="source.html#astra-t">阅读 26 份选手源码</a>
        <a class="btn" href="source.html#engine">裁判引擎 dart.hpp</a>
        <a class="btn" href="source.html#rules11">第 11 集个人赛规则</a>
        <a class="btn" href="source.html#rules12">第 12 集 5v5 团队赛规则</a>
      </div>
    </section>
  </div>`;

  if (!page) return html;
  if (!prerendered(page)) page.innerHTML = html;

  // 绑定 6 回合 3x3 矩阵交互
  let curRd = defRound;
  let curAi = 2, curFi = 0;
  const mxBox = page.querySelector<HTMLElement>('#mx12-box')!;
  const bindMatrixCells = () => {
    mxBox.querySelectorAll<HTMLButtonElement>('.mx12-cell-btn').forEach(btn => {
      btn.onclick = () => {
        curAi = +btn.dataset.mxR!;
        curFi = +btn.dataset.mxC!;
        mxBox.querySelectorAll('.mx12-cell-btn').forEach(x => x.classList.remove('active'));
        btn.classList.add('active');
        const det = mxBox.querySelector('#mx12-detail');
        if (det) det.innerHTML = matrixCellDetailHtml(curRd, curAi, curFi);
      };
    });
  };
  bindMatrixCells();

  page.querySelectorAll<HTMLButtonElement>('#rd12-strip button').forEach(btn => {
    btn.onclick = () => {
      const rno = +btn.dataset.rd!;
      const found = d.ep12.rounds.find(x => x.r === rno);
      if (!found) return;
      curRd = found;
      curAi = rno === 4 ? 2 : 0;
      curFi = 0;
      page.querySelectorAll<HTMLButtonElement>('#rd12-strip button').forEach(x => x.setAttribute('aria-selected', String(x === btn)));
      mxBox.innerHTML = matrixSectionHtml(curRd, curAi, curFi);
      bindMatrixCells();
    };
  });

  // 绑定第 12 集与第 11 集标签页切换
  page.querySelectorAll<HTMLButtonElement>('#ep12-tabs button').forEach(btn => {
    btn.onclick = () => {
      page.querySelectorAll<HTMLButtonElement>('#ep12-tabs button').forEach(x => x.setAttribute('aria-selected', String(x === btn)));
      page.querySelectorAll<HTMLElement>('.t12-panel').forEach(p => { p.hidden = p.dataset.t12Panel !== btn.dataset.t12; });
    };
  });
  page.querySelectorAll<HTMLButtonElement>('#ep11-tabs button').forEach(btn => {
    btn.onclick = () => {
      page.querySelectorAll<HTMLButtonElement>('#ep11-tabs button').forEach(x => x.setAttribute('aria-selected', String(x === btn)));
      page.querySelectorAll<HTMLElement>('.t11-panel').forEach(p => { p.hidden = p.dataset.t11Panel !== btn.dataset.t11; });
    };
  });

  void genUrl;
  return html;
}

export const view = (d: Ep11Data) => render(d, null);

if (!import.meta.env.SSR) {
  siteChrome('ep11');
  const page = document.getElementById('page')!;
  loadEp11()
    .then(d => render(d, page))
    .catch(e => { page.innerHTML = errorHtml('第 11–12 期数据载入失败：' + e.message); });
}
