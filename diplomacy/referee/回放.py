"""赛后回放：逐阶段地图（带命令箭头）、补给中心曲线、各国命令与结果、当季全部外交消息（可按国家筛选），
消息旁标出承诺核对的疑似违约。含全部私信和模型名，只给管理员和观众看。

  python 回放.py [--match 对局目录]      输出单个网页 对局目录/回放.html（地图已内嵌）
"""
import argparse
import json
from pathlib import Path
import sys

from diplomacy import Game

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))
import 承诺核对  # noqa: E402

LABELS = {'bounce': '受阻', 'void': '无效', 'cut': '支援被切断', 'dislodged': '被击退', 'disrupted': '海运中断',
          'no convoy': '无海运', 'disband': '解散'}


def render(record):
    game = Game()
    game.set_state(record['before'])
    for power, orders in record['orders'].items():
        try:
            game.set_orders(power, orders)
        except Exception:  # 个别历史命令格式不被渲染器接受时，只画局面
            pass
    return game.render(incl_orders=True)


def build(match):
    state = json.loads((match / 'state.json').read_text(encoding='utf-8'))
    sched = json.loads((match / 'scheduler.json').read_text(encoding='utf-8'))
    promises = 承诺核对.check(state)
    phases = []
    for i, h in enumerate(state['history']):
        if not h.get('before'):
            continue
        results = {p: {u: [LABELS.get(x, x) for x in r] for u, r in (rs or {}).items() if r}
                   for p, rs in (h.get('results') or {}).items() if isinstance(rs, dict)}
        phases.append(dict(phase=h['phase'], map=render(h), orders=h['orders'], results=results, event=h.get('event'),
                           centers={p: len(c) for p, c in h['centers'].items()},
                           messages=[dict(sender=m['sender'], to=m['to'], text=m['text'], time=m['time'][11:19])
                                     for m in state['messages'] if m['phase'] == h['phase']]))
    players = {}
    for p, v in sched['players'].items():
        chain = [x.get('label') or x['model'] for x in v.get('model_history', [])] + [v['label']]
        players[p] = ' → '.join(chain)
    flags = [dict(phase=x['phase'], sender=x['sender'], clause=x['clause'],
                  broken=[f'{b["when"]} {b["phase"]}：{b["order"]}' for b in x['broken']])
             for x in promises if x['broken']]
    data = dict(match=match.name, players=players, phases=phases, result=state.get('result'), flags=flags)
    payload = json.dumps(data, ensure_ascii=False).replace('</', '<\\/')
    out = match / '回放.html'
    out.write_text(TEMPLATE.replace('__DATA__', payload), encoding='utf-8')
    return out, len(phases)


TEMPLATE = r'''<!doctype html>
<html lang="zh"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>外交回放</title>
<style>
:root{--bg:#0f141c;--panel:#18202c;--line:#2a3545;--text:#e6ebf2;--muted:#8d9aab;--accent:#e0b44c;--bad:#e5484d}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:15px/1.6 system-ui,"Microsoft YaHei",sans-serif}
header{position:sticky;top:0;z-index:5;background:var(--bg);border-bottom:1px solid var(--line);padding:10px 16px;display:flex;flex-wrap:wrap;gap:8px;align-items:center}
h1{font-size:17px;margin:0 12px 0 0}button,select{font:inherit;color:var(--text);background:var(--panel);border:1px solid var(--line);border-radius:6px;padding:5px 10px;cursor:pointer}
input[type=range]{flex:1;min-width:160px}#phase{font-weight:700;color:var(--accent);min-width:70px}
main{display:grid;grid-template-columns:minmax(0,1.25fr) minmax(0,1fr);gap:14px;padding:14px 16px}
.panel{background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:12px;margin-bottom:14px}
.map svg{width:100%;height:auto;background:#fff;border-radius:6px;display:block}body{overflow-x:hidden}h2{font-size:14px;margin:0 0 8px;color:var(--muted);font-weight:600}
.bar{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin:3px 0}.bar b{width:64px;font-weight:600}.bar span{height:14px;border-radius:3px}.bar i{font-style:normal;color:var(--muted);font-size:13px}
.orders{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:8px;font-size:13px}.orders div{background:var(--bg);border-radius:6px;padding:6px 8px}
.bad{color:var(--bad)}.msg{border-left:3px solid var(--line);padding:6px 10px;margin:8px 0;background:var(--bg);border-radius:0 6px 6px 0}
.msg .who{font-size:13px;color:var(--muted)}.msg.flag{border-left-color:var(--bad)}.flagnote{color:var(--bad);font-size:13px;margin-top:4px}
#result{color:var(--accent)}@media(max-width:900px){main{grid-template-columns:1fr}}
</style></head><body>
<header><h1>外交回放 · <span id="mid"></span></h1><button id="prev">◀</button><span id="phase"></span><button id="next">▶</button>
<button id="play">播放</button><input id="slider" type="range" min="0" value="0">
<select id="who"><option value="">全部国家</option></select><span id="result"></span></header>
<main><div><div class="panel map"><h2 id="maptitle"></h2><div id="map"></div></div>
<div class="panel"><h2>命令与结果</h2><div class="orders" id="orders"></div></div></div>
<div><div class="panel"><h2>补给中心</h2><div id="bars"></div><svg id="chart" width="100%" height="160"></svg></div>
<div class="panel"><h2>本阶段外交消息</h2><div id="msgs"></div></div></div></main>
<script>
const D=__DATA__;
const CN={AUSTRIA:'奥地利',ENGLAND:'英国',FRANCE:'法国',GERMANY:'德国',ITALY:'意大利',RUSSIA:'俄国',TURKEY:'土耳其',GLOBAL:'公开'};
const COLOR={AUSTRIA:'#c0392b',ENGLAND:'#6c5ce7',FRANCE:'#3498db',GERMANY:'#7f8c8d',ITALY:'#27ae60',RUSSIA:'#bdc3c7',TURKEY:'#f1c40f'};
const $=id=>document.getElementById(id);let i=0,timer=null;
$('mid').textContent=D.match;$('slider').max=D.phases.length-1;
for(const p in D.players){const o=document.createElement('option');o.value=p;o.textContent=CN[p]+' · '+D.players[p];$('who').append(o)}
if(D.result){const s=Object.entries(D.result.scores||{}).filter(x=>x[1]).sort((a,b)=>b[1]-a[1]).map(([p,v])=>CN[p]+' '+v).join('、');$('result').textContent='结果：'+({victory:'单独获胜',agreed_draw:'协议和局',turn_limit:'到期和局'}[D.result.reason]||D.result.reason)+(s?'（'+s+'）':'')}
function el(t,c,x){const e=document.createElement(t);if(c)e.className=c;if(x!=null)e.textContent=x;return e}
function chart(){const svg=$('chart'),W=svg.clientWidth||500,H=160,n=D.phases.length,max=18;svg.innerHTML='';
 const x=k=>8+k*(W-16)/Math.max(1,n-1),y=v=>H-8-v*(H-16)/max;
 svg.innerHTML+=`<line x1="0" x2="${W}" y1="${y(18)}" y2="${y(18)}" stroke="#e0b44c" stroke-dasharray="4 4"/>`;
 for(const p in D.players){let d='';D.phases.forEach((ph,k)=>{d+=(k?'L':'M')+x(k)+','+y(ph.centers[p]||0)});svg.innerHTML+=`<path d="${d}" fill="none" stroke="${COLOR[p]}" stroke-width="2"/>`}
 svg.innerHTML+=`<line x1="${x(i)}" x2="${x(i)}" y1="0" y2="${H}" stroke="#fff" stroke-opacity=".4"/>`}
function draw(){const P=D.phases[i],who=$('who').value;$('slider').value=i;$('phase').textContent=P.phase;
 $('maptitle').textContent=P.phase+(P.event==='agreed_draw'?' · 协议和局':'')+' · 结算前局面与命令';$('map').innerHTML=P.map;
 const bars=$('bars');bars.innerHTML='';Object.entries(P.centers).sort((a,b)=>b[1]-a[1]).forEach(([p,v])=>{const r=el('div','bar');r.append(el('b',null,CN[p]));const s=el('span');s.style.width=(v*14)+'px';s.style.background=COLOR[p];r.append(s,el('i',null,v+' · '+D.players[p]));bars.append(r)});
 chart();const box=$('orders');box.innerHTML='';
 for(const p in P.orders){if(who&&p!==who)continue;const d=el('div');d.append(el('b',null,CN[p]));const res=P.results[p]||{};
  for(const o of P.orders[p]){const u=o.split(' ').slice(0,2).join(' ');const r=res[u];const line=el('div',r?'bad':null,o+(r?' ✗ '+r.join('、'):''));d.append(line)}box.append(d)}
 const ms=$('msgs');ms.innerHTML='';const list=P.messages.filter(m=>!who||m.sender===who||m.to.includes(who)||m.to.includes('GLOBAL'));
 if(!list.length)ms.append(el('div','who','本阶段没有消息'));
 for(const m of list){const f=D.flags.filter(x=>x.phase===P.phase&&x.sender===m.sender&&m.text.includes(x.clause));const b=el('div','msg'+(f.length?' flag':''));
  b.append(el('div','who',m.time+'  '+CN[m.sender]+' → '+m.to.map(t=>CN[t]||t).join('、')),el('div',null,m.text));
  for(const x of f)b.append(el('div','flagnote','疑似违约：“'+x.clause+'” → '+x.broken.join('；')));ms.append(b)}}
$('prev').onclick=()=>{i=Math.max(0,i-1);draw()};$('next').onclick=()=>{i=Math.min(D.phases.length-1,i+1);draw()};
$('slider').oninput=e=>{i=+e.target.value;draw()};$('who').onchange=draw;
$('play').onclick=()=>{if(timer){clearInterval(timer);timer=null;$('play').textContent='播放';return}$('play').textContent='暂停';timer=setInterval(()=>{if(i>=D.phases.length-1){$('play').click();return}i++;draw()},2500)};
document.addEventListener('keydown',e=>{if(e.key==='ArrowLeft')$('prev').click();if(e.key==='ArrowRight')$('next').click()});
addEventListener('resize',chart);if(D.phases.length)draw();
</script></body></html>'''


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    a = argparse.ArgumentParser()
    a.add_argument('--match')
    o = a.parse_args()
    matches = ROOT / '管理员' / '对局'
    match = Path(o.match) if o.match else matches / (matches / '当前对局.txt').read_text(encoding='utf-8').strip()
    path, n = build(match.resolve())
    print(f'已生成 {n} 个阶段的回放：{path}')


if __name__ == '__main__':
    main()
