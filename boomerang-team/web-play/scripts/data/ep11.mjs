// 从仓库源码与 scripts/data/ 生成 public/gen/ep11/ 下的所有前端静态数据：
//   - data.json（选手元数据、海选 BT 榜、1v1 矩阵、拍卖全记录、第 12 集 6 回合 & 3×3 矩阵 & 10 人贡献榜 & 实验 3 回炉升降榜、地图列表、精选回放清单）
//   - maps/*.txt（13 张官方地图，直接读取仓库 boomerang/ 与 boomerang-team/）
//   - vox/<角色>.json（13 位体素小人）
//   - scenes/<图>.json/.bin/.webp（若已按 scripts/ep11-r3d/README.md 导出，自动解压复制）
//   - replays/*.txt（12 局精选回放）
//   - src/*.cpp.txt、src/dart.hpp.txt、RULES_ep11.md.txt、RULES_ep12.md.txt（源码浏览页用）
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';
import ep, { ARENA_REPO } from '../episodes/ep11.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const SRC = path.join(ROOT, 'scripts', 'data', 'ep11_src');
const R3D = path.join(ROOT, 'scripts', 'data', 'ep11_r3d');
const OUT = path.join(ROOT, 'public', 'gen', 'ep11');

const readJson = (rel) => JSON.parse(fs.readFileSync(path.join(SRC, rel), 'utf8'));

export const MAPS = [
  { id: 'twinlakes',   name: '双湖', theme: '山间草甸', group: 'solo', modes: [0, 1], desc: '两口圆湖斜着摆，湖心小岛长草藏人；两湖之间的地峡是最后的决战场。' },
  { id: 'yinyang',     name: '阴阳', theme: '春樱',     group: 'solo', modes: [0, 1], desc: 'S 形深沟把椭圆岛劈成两半，一边草地一边石林，冲刺才跳得过沟。' },
  { id: 'stonecircle', name: '石阵', theme: '石阵荒原', group: 'solo', modes: [0, 1], desc: '没有坑的六边形高台，六块立石围成一圈，塌陷从外往里收。' },
  { id: 'skybridge',   name: '天桥', theme: '晚霞',     group: 'solo', modes: [0, 1], desc: '两座峭壁夹着深谷，一座一米宽的窄桥正中托着圆台。' },
  { id: 'ruins',       name: '古塔', theme: '古垣',     group: 'solo', modes: [0, 1], desc: '塌了一半的圆塔，两圈断墙缺口错开，绕墙回镖、贴身挥砍。' },
  { id: 'icefloes',    name: '裂冰', theme: '冰原',     group: 'solo', modes: [0, 1], desc: '冰面被裂缝切成七块浮冰，哪里都能冲刺跳过去，也最容易被踢下去。' },
  { id: 'hourglass',   name: '沙漏', theme: '沙洲',     group: 'team4', modes: [2], desc: '两队各守一块梯形高台，只在正中 3 米宽的沙漏颈相连。' },
  { id: 'canyon',      name: '峡谷', theme: '红岩峡谷', group: 'team4', modes: [2], desc: 'S 形深峡谷把场地劈成两块台地，隔谷对扔，过石桥才能近身。' },
  { id: 'oldtown',     name: '老城', theme: '老城',     group: 'team4', modes: [2], desc: '房子把石板广场切成街巷，正中一座喷泉，巷战绕房互相埋伏。' },
  { id: 'forest',      name: '林地', theme: '深林',     group: 'team4', modes: [2], desc: '没有坑的林间高地，十二根树干成丛挡镖，灌木贴着树丛藏人。' },
  { id: 'caldera',     name: '火山口', theme: '火山口', group: 'team', modes: [3], desc: '5v5 歼灭：火山岩环岛围着发光的岩浆湖，南北窄桥与湖心岩台决死。' },
  { id: 'piers',       name: '三座栈桥', theme: '港湾', group: 'team', modes: [4], desc: '5v5 占点：碧海三座栈桥 A / B / C，阵亡 3 秒复活，60 秒积分战。' },
  { id: 'keeps',       name: '双堡', theme: '双堡',     group: 'team', modes: [5], desc: '5v5 刺王：东西两座石堡，国王 3 条命，后期塌陷把人逼到中央广场。' },
];

function findMapFile(id) {
  const candidates = [
    path.join(ARENA_REPO, 'boomerang-team', 'workspace', 'maps', `${id}.txt`),
    path.join(ARENA_REPO, 'boomerang', 'workspace', 'maps', `${id}.txt`),
    path.join(ARENA_REPO, 'boomerang', 'tournament', 'hidden', `${id}.txt`),
    path.join(ARENA_REPO, 'boomerang', 'renderer', 'maps', `${id}.txt`),
  ];
  for (const p of candidates) {
    if (fs.existsSync(p)) return p;
  }
  throw new Error(`找不到地图文件：${id}.txt`);
}

function writeOut(rel, data) {
  const dst = path.join(OUT, rel);
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  fs.writeFileSync(dst, data);
}

export default async function build() {
  fs.mkdirSync(OUT, { recursive: true });

  // 1. 13 张官方地图（确保均含 CELL 500 头部）
  for (const m of MAPS) {
    let txt = fs.readFileSync(findMapFile(m.id), 'utf8');
    if (!/^CELL\s+\d+/m.test(txt)) txt = 'CELL 500\n' + txt;
    writeOut(`maps/${m.id}.txt`, txt);
  }

  // 2. 3D 场景包（可选：未用 Blender 导出时给出提示并跳过，2D 视图与页面榜单照常可用）
  const sceneSrc = path.join(R3D, 'scenes');
  let sceneCount = 0;
  if (fs.existsSync(sceneSrc)) {
    fs.rmSync(path.join(OUT, 'scenes'), { recursive: true, force: true });
    fs.mkdirSync(path.join(OUT, 'scenes'), { recursive: true });
    for (const f of fs.readdirSync(sceneSrc)) {
      const src = path.join(sceneSrc, f);
      if (f.endsWith('.bin.br')) {
        fs.writeFileSync(path.join(OUT, 'scenes', f.slice(0, -3)), zlib.brotliDecompressSync(fs.readFileSync(src)));
        sceneCount++;
      } else {
        fs.copyFileSync(src, path.join(OUT, 'scenes', f));
      }
    }
  } else {
    console.log('[ep11] 未检测到 scripts/data/ep11_r3d/scenes/（3D 场景包不入库）；如需 3D 场景请按 scripts/ep11-r3d/README.md 导出。');
  }

  // 3. 13 位体素小人
  fs.rmSync(path.join(OUT, 'vox'), { recursive: true, force: true });
  fs.mkdirSync(path.join(OUT, 'vox'), { recursive: true });
  const charFiles = fs.readdirSync(path.join(R3D, 'vox'));
  for (const f of charFiles) fs.copyFileSync(path.join(R3D, 'vox', f), path.join(OUT, 'vox', f));

  // 4. 精选回放
  const replays = readJson('data/replays.json');
  const repDir = path.join(OUT, 'replays');
  fs.mkdirSync(repDir, { recursive: true });
  for (const r of replays) {
    const raw = zlib.gunzipSync(fs.readFileSync(path.join(SRC, `replays/${r.id}.txt.gz`)));
    fs.writeFileSync(path.join(repDir, `${r.id}.txt`), raw);
  }

  // 5. 规则、引擎与 26 份源码（直接读取本仓库）
  writeOut('RULES_ep11.md.txt', fs.readFileSync(path.join(ARENA_REPO, 'boomerang', 'workspace', 'RULES.md')));
  writeOut('RULES_ep12.md.txt', fs.readFileSync(path.join(ARENA_REPO, 'boomerang-team', 'workspace', 'RULES.md')));
  writeOut('src/dart.hpp.txt', fs.readFileSync(path.join(ARENA_REPO, 'boomerang-team', 'workspace', 'dart.hpp')));

  const botMeta = [];
  for (const b of ep.bots) {
    let raw = fs.readFileSync(path.join(ARENA_REPO, b.src), 'utf8');
    if (b.patch) raw = b.patch(raw);
    const lines = raw.split(/\r?\n/).length;
    writeOut(`src/${b.id}.cpp.txt`, raw);
    botMeta.push({
      id: b.id,
      baseId: b.baseId || b.id,
      short: b.short,
      name: b.name,
      full: b.full,
      model: b.model,
      tool: b.tool,
      group: b.group,
      version: b.version,
      charKey: b.charKey,
      color: b.color,
      team12: b.team12,
      avatar: b.avatar,
      note: b.note || '',
      lines,
      file: b.src,
    });
  }

  // 6. 榜单、矩阵与实验元数据
  const haixuan = readJson('data/haixuan_score.json');
  const duelMatrix = readJson('data/duel_matrix.json');
  const auction = readJson('data/auction.json');
  const ep12 = readJson('data/ep12_rounds.json');
  const exp3 = readJson('data/ep12_exp3.json');

  const standings11 = haixuan.ranking.map((id) => {
    const p = haixuan.players[id];
    const b = botMeta.find(x => x.id === id);
    const st = duelMatrix.stats[id] || {};
    const fl = haixuan.faults?.[id] || {};
    const re = exp3.retest[id] || null;
    const duelGames = (st.duelW || 0) + (st.duelL || 0) + (st.duelD || 0);
    return {
      id,
      short: b?.short || id,
      model: b?.model || id,
      tool: b?.tool || '',
      avatar: b?.avatar || `ep11/${id}`,
      color: b?.color || '#888',
      charKey: b?.charKey || id,
      team12: b?.team12 ?? -1,
      rank: p.rank,
      rating: +p.rating.toFixed(1),
      ratingCi: p.rating_ci95.map(v => +v.toFixed(1)),
      duelRating: +p.duel_rating.toFixed(1),
      ffaRating: +p.ffa_rating.toFixed(1),
      duelW: st.duelW || 0,
      duelD: st.duelD || 0,
      duelL: st.duelL || 0,
      duelWr: duelGames ? +(((st.duelW + 0.5 * st.duelD) / duelGames) * 100).toFixed(1) : 0,
      ffaGames: st.games || 0,
      ffaWins: st.wins || 0,
      ffaAvgRank: st.games ? +(st.rankSum / st.games).toFixed(2) : 0,
      faults: fl.faults || 0,
      maxms: fl.maxms || 0,
      retest: re,
    };
  });

  const btAnchors = {};
  for (const s of standings11) {
    btAnchors[s.id] = { rating: s.rating, duel: s.duelRating, ffa: s.ffaRating, short: s.short, version: '海选版', group: 'solo', avatar: s.avatar, color: s.color };
    if (s.retest) {
      btAnchors[`${s.id}-t`] = {
        rating: s.retest.final,
        duel: +(s.duelRating + s.retest.duelDelta).toFixed(1),
        ffa: +(s.ffaRating + s.retest.ffaDelta).toFixed(1),
        short: `${s.short}·终版`,
        version: '团队终版',
        group: 'team',
        avatar: s.avatar,
        color: s.color,
      };
    }
  }
  btAnchors.weak   = { rating: -360, duel: -380, ffa: -340, short: '弱陪练', version: '官方陪练', group: 'ref', avatar: 'ep11/weak', color: '#6E8CA5' };
  btAnchors.mid    = { rating: -180, duel: -195, ffa: -165, short: '中陪练', version: '官方陪练', group: 'ref', avatar: 'ep11/mid', color: '#488AC6' };
  btAnchors.strong = { rating: -40,  duel: -35,  ffa: -45,  short: '强陪练', version: '官方陪练', group: 'ref', avatar: 'ep11/strong', color: '#DA5C44' };

  const data = {
    episode: 'ep11',
    title: '回旋镖大作战（OA 争霸）',
    totalGames11: duelMatrix.totalGames,
    totalGames12: 10600,
    totalMatrixGames12: 54000,
    maps: MAPS,
    bots: botMeta,
    standings11,
    gaps11: haixuan.gaps,
    duelPairs: duelMatrix.pairs,
    btAnchors,
    auction: {
      teams: auction.teams,
      rejected: auction.rejected,
      events: auction.events.filter(e => e.type === 'result').map(e => {
        const bids = auction.events.filter(x => x.type === 'bids' && x.candidate === e.candidate);
        const last = bids[bids.length - 1]?.bids || {};
        return {
          candidate: e.candidate,
          winner: e.winner,
          price: e.price,
          reason: e.reason,
          rounds: bids.length,
          astraBid: last.astra?.price ?? null,
          fableBid: last.fable?.price ?? null,
          astraNote: last.astra?.reason ?? '',
          fableNote: last.fable?.reason ?? '',
        };
      }),
    },
    ep12: {
      score: [5.5, 0.5],
      totalWld: [6915, 3630, 55],
      astraTeam: ['astra', 'sol', 'hydrogen', 'grok', 'kimi'],
      fableTeam: ['fable', 'opus', 'carbon', 'glm', 'deepseek'],
      rounds: ep12.rounds,
      contrib: ep12.contrib,
      exp3,
    },
    replays,
  };

  writeOut('data.json', JSON.stringify(data));
  console.log(`[ep11] data.json (${(fs.statSync(path.join(OUT, 'data.json')).size / 1024).toFixed(1)} KB), ${MAPS.length} maps, ${charFiles.length} vox, ${sceneCount} scenes, ${replays.length} replays, ${botMeta.length} bots`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(fileURLToPath(import.meta.url))) {
  await build();
}
