// 第 11–12 期《回旋镖大作战（OA 争霸）》前端核心类型定义

export type GameMode = 0 | 1 | 2 | 3 | 4 | 5; // 0=1v1 1=4人混战 2=4v4 3=5v5歼灭 4=5v5占点 5=5v5刺王

export const MODE_LABEL: Record<GameMode, string> = {
  0: '1v1 单挑',
  1: '4 人混战',
  2: '4v4 团队乱斗',
  3: '5v5 歼灭（火山口）',
  4: '5v5 占点（三座栈桥）',
  5: '5v5 刺王（双堡）',
};

export const MODE_SHORT: Record<GameMode, string> = {
  0: '单挑', 1: '四人混战', 2: '4v4', 3: '歼灭', 4: '占点', 5: '刺王',
};

export const MODE_PLAYERS: Record<GameMode, number> = {
  0: 2, 1: 4, 2: 8, 3: 10, 4: 10, 5: 10,
};

export const THEME_BY_MAP: Record<string, string> = {
  twinlakes: 'meadow', yinyang: 'blossom', icefloes: 'glacier', ruins: 'ruins', skybridge: 'sunset', stonecircle: 'heath',
  hourglass: 'dunes', canyon: 'canyon', oldtown: 'oldtown', forest: 'forest',
  caldera: 'caldera', piers: 'harbor', keeps: 'keeps',
};

export interface BotMeta {
  id: string;
  baseId: string;
  short: string;
  name: string;
  full: string;
  model: string | null;
  tool: string;
  group: 'solo' | 'team' | 'ref';
  version: string;
  charKey: string;
  color: string;
  team12: number;
  avatar: string;
  note: string;
  lines: number;
  file: string;
}

export interface MapMeta {
  id: string;
  name: string;
  theme: string;
  group: 'solo' | 'team4' | 'team';
  modes: number[];
  desc: string;
}

export interface Standing11 {
  id: string;
  short: string;
  model: string;
  tool: string;
  avatar: string;
  color: string;
  charKey: string;
  team12: number;
  rank: number;
  rating: number;
  ratingCi: [number, number];
  duelRating: number;
  ffaRating: number;
  duelW: number;
  duelD: number;
  duelL: number;
  duelWr: number;
  ffaGames: number;
  ffaWins: number;
  ffaAvgRank: number;
  faults: number;
  maxms: number;
  retest: {
    solo: number;
    final: number;
    delta: number;
    se: number;
    duelDelta: number;
    ffaDelta: number;
    selfWr: number;
  } | null;
}

export interface BtAnchor {
  rating: number;
  duel: number;
  ffa: number;
  short: string;
  version: string;
  group: 'solo' | 'team' | 'ref';
  avatar: string;
  color: string;
}

export interface MatrixCell {
  wr: number;
  se: number;
}

export interface RoundPlanSpec {
  formation: { 站位: Record<string, [number, number]>; 国王?: string };
  plan: string;
}

export interface Ep12Round {
  r: number;
  map: string;
  mode: GameMode;
  modeCn: string;
  mapCn: string;
  highlight: string;
  seeds: number;
  wld: [number, number, number];
  wr: [number, number];
  se: number;
  pts: [number, number];
  cumPts: [number, number];
  faults: number;
  notes: string[];
  matrix: MatrixCell[][];
  plans: {
    astra: Record<string, RoundPlanSpec>;
    fable: Record<string, RoundPlanSpec>;
  };
  playerStats: Record<string, {
    kills: number;
    deaths: number;
    ff: number;
    tk: number;
    capSec: number;
    kingHits: number;
    survive: number;
  }>;
}

export interface Ep12Contrib {
  id: string;
  team: number;
  games: number;
  totalKills: number;
  totalDeaths: number;
  totalTK: number;
  avgKills: number;
  avgDeaths: number;
  kd: number;
  avgTK: number;
  avgCapSec: number;
  avgSurvive: number;
  faults?: number;
}

export interface CuratedReplayMeta {
  id: string;
  ep: number;
  tag: string;
  title: string;
  desc: string;
  focusSec: number;
  mode: GameMode;
  modeName: string;
  map: string;
  steps: number;
  winner: number;
  digest: string;
  reason: string;
  players: { seat: number; team: number; name: string }[];
  kb: number;
}

export interface Ep11Data {
  episode: string;
  title: string;
  totalGames11: number;
  totalGames12: number;
  totalMatrixGames12: number;
  maps: MapMeta[];
  bots: BotMeta[];
  standings11: Standing11[];
  gaps11: { higher: string; lower: string; diff: number; ci95: [number, number]; within_error: boolean }[];
  duelPairs: Record<string, Record<string, [number, number, number]>>;
  btAnchors: Record<string, BtAnchor>;
  auction: {
    teams: any[];
    rejected: any[];
    events: {
      candidate: string;
      winner: string | null;
      price: number;
      reason: string;
      rounds: number;
      astraBid: number | null;
      fableBid: number | null;
      astraNote: string;
      fableNote: string;
    }[];
  };
  ep12: {
    score: [number, number];
    totalWld: [number, number, number];
    astraTeam: string[];
    fableTeam: string[];
    rounds: Ep12Round[];
    contrib: Ep12Contrib[];
    exp3: {
      retest: Record<string, { solo: number; final: number; delta: number; se: number; duelDelta: number; ffaDelta: number; selfWr: number }>;
      experiments: { id: string; title: string; subtitle: string; result: string; detail: string }[];
    };
  };
  replays: CuratedReplayMeta[];
}

// ---- 运行时对局帧结构（供 3D 渲染器与 2D 查看器共用）----

export interface PlayerState {
  alive: number;
  x: number; y: number;       // 米
  vx: number; vy: number;     // 米/步
  face: number;               // 弧度 (0 = +x, pi/2 = +y)
  st: number;                 // 0=free 1=throw 2=throw_post 3=slash 4=slash_post 5=kick 6=kick_post 7=dash 8=stun 9=recall 10=dead
  timer: number;
  ax: number; ay: number;
  cd: number;
  expose: number;
  kills: number;
  msg: number;
  visMask: number;
  deadT: number;
  cause: number;              // 0=none 1=dart 2=slash 3=fall
  killer: number;
  lives: number;              // 刺王模式国王剩命
  inv: number;                // 无敌帧
}

export interface DartState {
  st: number;                 // 0=hand 1=out 2=back 3=ground 4=rise 5=call 6=gone
  x: number; y: number;       // 米
  vx: number; vy: number;
  left: number;
  age: number;
}

export interface GhostPlayer {
  alive: number;
  x: number; y: number;       // 米
  face: number;               // 弧度
  st: number;
  dartSt: number;
  dartX: number; dartY: number;
}

export interface ControlState {
  owner: [number, number, number];
  taking: [number, number, number];
  prog: [number, number, number];
  score: [number, number];    // 原始步单位，除以 20 为分
}

export interface FrameEvent {
  t: number;
  type: string;
  a: number; b: number; c: number; d: number;
}

export interface ReplayFrame {
  t: number;
  players: PlayerState[];
  darts: DartState[];
  fallen: number;
  warn: number;
  control?: ControlState;
  ghostT?: number;
  ghosts?: GhostPlayer[];
  events: FrameEvent[];
}

export interface ControlPointDef {
  name: string;
  x: number; y: number; r: number; // 米
}

export interface ParsedReplay {
  mode: GameMode;
  modeName: string;
  mapName: string;
  theme: string;
  seed: number;
  nx: number;
  ny: number;
  cell: number;               // 0.5 米
  grid: number[];             // 0=floor 1=wall 2=pit 3=grass
  wave: number[];             // -1=never, >=0 wave index
  waveSteps: number[];        // 每波塌陷步号
  cfg: Record<string, number>;
  points: ControlPointDef[];
  kings: [number, number];    // 刺王模式两队国王座位号
  players: { seat: number; team: number; name: string; botId?: string; charKey: string; color: string; avatar: string }[];
  frames: ReplayFrame[];
  end?: { steps: number; winner: number; digest: string; reason: string; results: { seat: number; rank: number; deadT: number; cause: number; killer: number; kills: number }[] };
  live?: boolean;
  finished?: boolean;
  humanSeat?: number;
  status?: string;
}
