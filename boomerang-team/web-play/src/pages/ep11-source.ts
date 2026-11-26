// 第 11–12 期源码浏览页（ep11/source.html#<botId|engine|rules11|rules12>）
// 支持自由切换 13 位海选原版、10 位团队赛终版、3 档陪练、原版裁判引擎 dart.hpp 与两集完整规则！
import { avatarHtml, errorHtml, esc, loadingHtml, siteChrome } from '../common/site';
import { genUrl } from '../common/wasm';
import { loadEp11 } from '../games/boomerang/data';

siteChrome('ep11');
const page = document.getElementById('page')!;

const KW = new Set(('auto bool break case char const constexpr continue default do double else enum extern false float for if inline int long namespace ' +
  'nullptr return short signed sizeof static static_cast struct switch template true typedef typename uint8_t uint16_t uint32_t uint64_t int8_t int16_t ' +
  'int32_t int64_t unsigned using void while class public private operator size_t').split(' '));

function highlight(src: string): string {
  const out: string[] = [];
  const re = /(\/\/[^\n]*|\/\*[\s\S]*?\*\/)|("(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*')|(^[ \t]*#[^\n]*)|([A-Za-z_]\w*)/gm;
  let last = 0, m: RegExpExecArray | null;
  while ((m = re.exec(src))) {
    out.push(esc(src.slice(last, m.index)));
    const t = esc(m[0]);
    if (m[1]) out.push(`<span class="c">${t}</span>`);
    else if (m[2]) out.push(`<span class="s">${t}</span>`);
    else if (m[3]) out.push(`<span class="p">${t}</span>`);
    else out.push(KW.has(m[0]) ? `<span class="k">${t}</span>` : t);
    last = m.index + m[0].length;
  }
  out.push(esc(src.slice(last)));
  const lines = out.join('').split('\n');
  let open = '';
  return lines.map(l => {
    const line = open + l;
    const opens = (line.match(/<span class="[a-z]">/g) || []).length;
    const closes = (line.match(/<\/span>/g) || []).length;
    const unclosed = opens > closes;
    const res = `<span class="l">${line}${unclosed ? '</span>' : ''}</span>`;
    open = unclosed ? (line.match(/<span class="[a-z]">(?![\s\S]*<span class="[a-z]">)/) || [''])[0] : '';
    return res;
  }).join('');
}

async function show() {
  const id = decodeURIComponent(location.hash.slice(1)) || 'astra-t';
  const d = await loadEp11();

  let title = '', file = '', sub = '', isMd = false, av = '', altVer = '';
  if (id === 'rules11') {
    title = '第 11 集 · 个人赛完整规则';
    file = 'RULES_ep11.md.txt';
    sub = '海选单挑 + 四人混战规则原文';
    isMd = true;
  } else if (id === 'rules12') {
    title = '第 12 集 · 5v5 团队赛完整规则';
    file = 'RULES_ep12.md.txt';
    sub = '歼灭 / 占点 / 刺王三大赛制 + 16 位电台 + 战术单协议';
    isMd = true;
  } else if (id === 'engine') {
    title = '原版裁判引擎 · dart.hpp';
    file = 'src/dart.hpp.txt';
    sub = '双模式规则引擎唯一权威实现（支持 Mode 0..5）';
  } else {
    const b = d.bots.find(x => x.id === id);
    if (!b) { page.innerHTML = errorHtml(`找不到编号为 ${id} 的程序`); return; }
    title = b.name;
    file = `src/${b.id}.cpp.txt`;
    av = avatarHtml(b.avatar, b.short);
    sub = `${b.version} · ${b.full} · ${b.lines} 行${b.note ? ' · ' + b.note : ''}`;
    // 同选手另一版本快捷切换（海选版 <-> 团队终版）
    const peerId = b.group === 'team' ? b.baseId : `${b.baseId}-t`;
    if (d.bots.some(x => x.id === peerId)) altVer = peerId;
  }

  page.innerHTML = loadingHtml('正在读取源码…');
  const r = await fetch(genUrl('ep11/' + file));
  const text = r.ok ? await r.text() : '文件不存在';
  document.title = `${title} · 第 11–12 期 · AI 算法对抗大赛`;

  const grp = (label: string, list: typeof d.bots) =>
    `<optgroup label="${label}">${list.map(b => `<option value="${b.id}"${b.id === id ? ' selected' : ''}>${esc(b.name)} (${b.lines}行)</option>`).join('')}</optgroup>`;

  page.innerHTML = `<div class="wrap">
    <div class="src-head">
      ${av}
      <div class="grow">
        <h1>${esc(title)}</h1>
        <div class="muted small">${esc(sub)}</div>
      </div>
      <select class="select" id="pick" aria-label="选择源码或文档">
        <optgroup label="规则与引擎">
          <option value="rules11"${id === 'rules11' ? ' selected' : ''}>第 11 集个人赛规则</option>
          <option value="rules12"${id === 'rules12' ? ' selected' : ''}>第 12 集 5v5 团队赛规则</option>
          <option value="engine"${id === 'engine' ? ' selected' : ''}>裁判引擎 dart.hpp</option>
        </optgroup>
        ${grp('第 12 集团队终版（10 份）', d.bots.filter(x => x.group === 'team'))}
        ${grp('第 11 集海选原版（13 份）', d.bots.filter(x => x.group === 'solo'))}
        ${grp('官方陪练（3 档）', d.bots.filter(x => x.group === 'ref'))}
      </select>
      ${altVer ? `<a class="btn" href="#${altVer}">切换${altVer.endsWith('-t') ? '团队终版' : '海选原版'}</a>` : ''}
      ${!isMd && id !== 'engine' ? `<a class="btn btn-primary" href="play.html#opp=${id}">真人挑战它</a><a class="btn" href="watch.html#mode=0&bots=${id},astra-t">看它互打</a>` : ''}
    </div>
    ${isMd ? `<pre class="md">${esc(text)}</pre>` : `<pre class="src">${highlight(text.replace(/\r\n/g, '\n'))}</pre>`}
  </div>`;

  (document.getElementById('pick') as HTMLSelectElement).onchange = (e) => {
    location.hash = (e.target as HTMLSelectElement).value;
  };
  scrollTo(0, 0);
}

addEventListener('hashchange', show);
show().catch(e => { page.innerHTML = errorHtml('加载失败：' + e.message); });
