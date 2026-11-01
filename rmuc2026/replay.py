"""A silent, dependency-free 2D viewer; samples come from the actual scored match."""
import json

TEMPLATE = r'''<!doctype html><html lang="zh-CN"><meta charset="utf-8">
<title>RMUC 2026 replay</title><style>
body{font:15px system-ui;margin:24px;background:#13171c;color:#eee;max-width:1100px}
svg{width:100%;background:#303940}button,input{margin:8px}a{color:#9ed3ff}
#status{white-space:pre-wrap}input{width:60%}
</style><h1>RMUC 2026 · 6v6</h1>
<p>静音俯视回放 · 1 秒采样 · 不是逐帧物理校验文件。红蓝双方 3 / 4 / 7 号均为 305 串联腿。</p>
<svg id="field" viewBox="0 0 1120 600" aria-label="28×15 米场地"></svg>
<button id="play">播放 / 暂停</button><input id="seek" type="range" min="0" value="0"><span id="time"></span>
<pre id="status"></pre><details><summary>对局身份、结果与诊断</summary><pre id="result"></pre></details>
<script id="data" type="application/json">__DATA__</script><script>
const D=JSON.parse(document.getElementById('data').textContent), ns='http://www.w3.org/2000/svg';
const field=document.getElementById('field'),seek=document.getElementById('seek');
const colors={red:'#ff7777',blue:'#78bbff'};let index=0,playing=false;
function shape(tag,attrs,text){const e=document.createElementNS(ns,tag);for(const [k,v] of Object.entries(attrs))e.setAttribute(k,v);if(text!==undefined)e.textContent=text;field.appendChild(e);return e}
function draw(){field.replaceChildren();const f=D.frames[index];if(!f){document.getElementById('status').textContent='未进入比赛：参见结果中的启动错误。';return}
for(const w of D.arena.obstacles||[])shape('rect',{x:w.x*40,y:w.y*40,width:w.w*40,height:w.h*40,fill:'#67717c',transform:`rotate(${(w.rotation||0)*180/Math.PI} ${(w.x+w.w/2)*40} ${(w.y+w.h/2)*40})`});
for(const z of D.arena.zones||[])if(z.x!==undefined)shape('circle',{cx:z.x*40,cy:z.y*40,r:10,fill:'none',stroke:'#bbb'});
for(const [kind,objects] of [['基地',f.bases],['前哨',f.outposts]])for(const [side,b] of Object.entries(objects||{})){shape('rect',{x:b.x*40-15,y:b.y*40-15,width:30,height:30,fill:colors[side]||'#aaa',opacity:b.hp>0?.8:.2});shape('text',{x:b.x*40+18,y:b.y*40-15,fill:colors[side]||'#eee','font-size':13},`${kind} ${Math.round(b.hp||0)}`)}
for(const r of f.robots){shape('circle',{cx:r.x*40,cy:r.y*40,r:r.role==='aerial'?13:10,fill:colors[r.team],opacity:r.alive?1:.25});shape('text',{x:r.x*40+12,y:r.y*40+4,fill:colors[r.team],'font-size':13},`${r.slot} ${Math.round(r.hp||0)}`)}
document.getElementById('time').textContent=f.time.toFixed(1)+' s';seek.value=index;
document.getElementById('status').textContent=`红方：${D.result.strategies.red.name}　蓝方：${D.result.strategies.blue.name}\n`+JSON.stringify({bases:f.bases,outposts:f.outposts});}
seek.max=Math.max(0,D.frames.length-1);seek.oninput=()=>{index=Number(seek.value);draw()};
document.getElementById('play').onclick=()=>playing=!playing;
document.getElementById('result').textContent=JSON.stringify(D.result,null,2);
setInterval(()=>{if(playing&&index<D.frames.length-1){index++;draw()}},150);draw();
</script></html>'''


def write_viewer(path, payload):
    # JSON inside an HTML script must escape '<' even for player-controlled filenames/logs.
    data = json.dumps(payload, ensure_ascii=False, allow_nan=False, separators=(",", ":")).replace("<", "\\u003c")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(TEMPLATE.replace("__DATA__", data), encoding="utf-8")
