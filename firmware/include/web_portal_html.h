#pragma once
// Páginas servidas pelo dongle ao celular (HTML/CSS/JS embutidos).
// No ESP32 PROGMEM é só compatibilidade: dados const já ficam na flash.
#include <pgmspace.h>

// -----------------------------------------------------------------------------
// Página de envio: seleção múltipla, envio sequencial com progresso.
// O arquivo vai como corpo cru (application/octet-stream) em POST /upload,
// com nome/tamanho/índice no cabeçalho X-Upload-Metadata (URL-encoded, UTF-8).
// -----------------------------------------------------------------------------
static const char PORTAL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>T-Dongle Bridge</title>
<style>
:root{--bg:#0b0f14;--card:#131a22;--card2:#1a232d;--bd:#243140;--fg:#e6edf3;--mu:#8b9aab;--ac:#22d3ee;--acf:#071016;--ok:#34d399;--er:#f87171;--wa:#fbbf24}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 system-ui,-apple-system,Roboto,sans-serif}
main{max-width:560px;margin:0 auto;padding:16px;display:flex;flex-direction:column;gap:14px}
h1{font-size:1.2rem;margin:4px 0 0}.sub{color:var(--mu);font-size:.85rem;margin:0}
.card{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px}
.banner{border-radius:10px;padding:12px 14px;font-weight:600}
.b-ok{background:#0f2a20;color:var(--ok)}.b-wait{background:#2a2310;color:var(--wa)}.b-err{background:#2a1717;color:var(--er)}
.banner small{display:block;font-weight:400;color:var(--mu);margin-top:2px;word-break:break-all}
.pick{display:block;text-align:center;border:2px dashed var(--bd);border-radius:12px;padding:22px 10px;cursor:pointer;color:var(--mu)}
.pick b{color:var(--fg);font-size:1.05rem}input[type=file]{display:none}
button{font:inherit;width:100%;padding:14px;border-radius:10px;border:0;background:var(--ac);color:var(--acf);font-weight:700}
button:disabled{opacity:.4}button.sec{background:var(--card2);color:var(--fg);border:1px solid var(--bd);font-weight:600}
.row{display:flex;gap:10px}
ul{list-style:none;margin:0;padding:0;display:flex;flex-direction:column;gap:8px}
li{background:var(--card2);border:1px solid var(--bd);border-radius:10px;padding:10px 12px}
.top{display:flex;gap:8px}.nm{flex:1;min-width:0;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;font-weight:600}
.sz{color:var(--mu);font-size:.85rem;white-space:nowrap}
.bar{height:6px;background:var(--bd);border-radius:3px;overflow:hidden;margin:8px 0 4px}.bar i{display:block;height:100%;width:0;background:var(--ac)}
.st{font-size:.85rem;color:var(--mu)}li.ok{border-color:#34d39966}li.ok .bar i{background:var(--ok)}li.ok .st{color:var(--ok)}
li.er{border-color:#f8717166}li.er .bar i{background:var(--er)}li.er .st{color:var(--er)}
.tot{font-size:.9rem;color:var(--mu);margin:0 0 6px}.hint{color:var(--mu);font-size:.8rem;margin:0}a{color:var(--ac)}
</style></head><body><main>
<div><h1>T-Dongle Bridge</h1><p class="sub">Envie arquivos para o computador. Nada fica guardado no dongle.</p></div>
<div id="ban" class="banner b-wait">Verificando o PC…</div>
<label class="pick card"><input id="inp" type="file" multiple><b>Escolher arquivos</b><br>toque para selecionar (vários de uma vez)</label>
<div class="card" id="qcard" hidden>
<p class="tot" id="tot"></p><div class="bar"><i id="totbar"></i></div>
<ul id="list"></ul>
<div class="row" style="margin-top:12px"><button id="go">Enviar</button><button id="stop" class="sec" hidden>Cancelar</button></div>
</div>
<p class="hint">Mantenha esta tela aberta durante o envio. Se o botão de escolher arquivos não abrir, abra <b>http://192.168.4.1</b> no Chrome ou Safari. · <a href="/config">Configurações</a></p>
</main><script>
const $=i=>document.getElementById(i);let items=[],busy=false,cur=null,stopAll=false,host=false;
const fmt=n=>n<1024?n+' B':n<1048576?(n/1024).toFixed(1)+' KB':n<1073741824?(n/1048576).toFixed(1)+' MB':(n/1073741824).toFixed(2)+' GB';
async function status(){try{const r=await fetch('/status',{cache:'no-store'});return await r.json()}catch(e){return null}}
function banner(s){const b=$('ban');if(!s){b.className='banner b-err';b.innerHTML='Sem contato com o dongle<small>Confira se o celular ainda está no Wi-Fi do dongle.</small>';return}
host=s.host;if(s.host){b.className='banner b-ok';b.textContent='✓ PC pronto para receber'}else{b.className='banner b-wait';b.innerHTML='Conecte o dongle na página do PC<small></small>';b.querySelector('small').textContent='Clique em Conectar, escolha T-Dongle Bridge e libere a pasta, se solicitado. '+(s.url||'')}}
async function poll(){if(!busy)banner(await status());setTimeout(poll,2000)}poll();
$('inp').onchange=e=>{if(busy)return;items=[...e.target.files].map(f=>({f,el:null,state:'wait'}));render();e.target.value=''};
function render(){const ul=$('list');ul.innerHTML='';$('qcard').hidden=!items.length;
for(const it of items){const li=document.createElement('li');li.innerHTML='<div class="top"><span class="nm"></span><span class="sz"></span></div><div class="bar"><i></i></div><div class="st">Na fila</div>';
li.querySelector('.nm').textContent=it.f.name;li.querySelector('.sz').textContent=fmt(it.f.size);it.el=li;ul.appendChild(li)}totals(0)}
function setItem(it,cls,pct,txt){it.el.className=cls;it.el.querySelector('.bar i').style.width=pct+'%';it.el.querySelector('.st').textContent=txt}
function totals(extra){const all=items.reduce((a,i)=>a+i.f.size,0),done=items.filter(i=>i.state=='ok').reduce((a,i)=>a+i.f.size,0)+extra;
const n=items.filter(i=>i.state=='ok').length;$('tot').textContent=`${n} de ${items.length} enviados · ${fmt(done)} de ${fmt(all)}`;$('totbar').style.width=(all?done/all*100:0)+'%'}
function send(it,idx,cnt){return new Promise(res=>{const x=new XMLHttpRequest();cur=x;const t0=performance.now();let lt=t0,lb=0,bps=0;
const metadata='name='+encodeURIComponent(it.f.name)+'&size='+it.f.size+'&index='+idx+'&count='+cnt;
x.open('POST','/upload');
x.setRequestHeader('Content-Type','application/octet-stream');
x.setRequestHeader('X-Upload-Metadata',metadata);
x.upload.onprogress=e=>{const now=performance.now();if(now-lt>=700){const v=(e.loaded-lb)/(now-lt)*1000;bps=bps?bps*.6+v*.4:v;lt=now;lb=e.loaded}
const p=it.f.size?e.loaded/it.f.size*100:100;setItem(it,'',p,p>=100?'Confirmando no PC…':`${p.toFixed(1)}% · ${fmt(bps)}/s`);totals(e.loaded)};
x.onload=()=>{let j={};try{j=JSON.parse(x.responseText)}catch(e){}const s=(performance.now()-t0)/1000;
if(x.status==200&&j.ok)res({ok:true,txt:`✓ Salvo no PC · ${fmt(it.f.size)} em ${s.toFixed(1)} s (${fmt(it.f.size/Math.max(s,.001))}/s)`});
else res({ok:false,txt:'✕ '+(j.error||('Erro HTTP '+x.status)),noHost:j.code==5})};
x.onerror=async()=>{const st=await status();res({ok:false,txt:st&&!st.host?'✕ No PC, clique em Conectar e libere a pasta':'✕ Conexão interrompida',noHost:st&&!st.host})};
x.onabort=()=>res({ok:false,txt:'✕ Cancelado'});x.send(it.f)})}
$('go').onclick=async()=>{if(busy)return;busy=true;stopAll=false;$('go').disabled=true;$('stop').hidden=false;$('inp').disabled=true;
const todo=items.filter(i=>i.state!='ok');let k=0;
for(const it of todo){k++;if(stopAll)break;const st=await status();banner(st);
if(!st||!st.host){setItem(it,'er',0,'✕ No PC, clique em Conectar e libere a pasta. Depois tente de novo');it.state='err';break}
setItem(it,'',0,'Enviando…');const r=await send(it,k,todo.length);cur=null;it.state=r.ok?'ok':'err';setItem(it,r.ok?'ok':'er',r.ok?100:0,r.txt);totals(0);if(r.noHost)break}
busy=false;$('go').disabled=false;$('go').textContent=items.some(i=>i.state!='ok')?'Tentar de novo':'Enviar';$('stop').hidden=true;$('inp').disabled=false;banner(await status())};
$('stop').onclick=()=>{stopAll=true;if(cur)cur.abort()};
addEventListener('beforeunload',e=>{if(busy)e.preventDefault()});
</script></body></html>)HTML";

// -----------------------------------------------------------------------------
// Página de configurações (SO alvo, layout, URL do PC, comando do Linux)
// -----------------------------------------------------------------------------
static const char CONFIG_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>Configurações · T-Dongle Bridge</title>
<style>
:root{--bg:#0b0f14;--card:#131a22;--bd:#243140;--fg:#e6edf3;--mu:#8b9aab;--ac:#22d3ee;--acf:#071016;--ok:#34d399}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.45 system-ui,-apple-system,Roboto,sans-serif}
main{max-width:560px;margin:0 auto;padding:16px;display:flex;flex-direction:column;gap:14px}
h1{font-size:1.2rem;margin:4px 0 0}form{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:14px;display:flex;flex-direction:column;gap:12px}
label{display:flex;flex-direction:column;gap:4px;font-size:.9rem;color:var(--mu)}
input,select{font:inherit;color:var(--fg);background:#0b0f14;border:1px solid var(--bd);border-radius:8px;padding:10px}
button{font:inherit;padding:14px;border-radius:10px;border:0;background:var(--ac);color:var(--acf);font-weight:700}
small{color:var(--mu)}#msg{color:var(--ok);min-height:1.2em;margin:0}a{color:var(--ac)}
</style></head><body><main>
<h1>Configurações</h1>
<form id="f">
<label>SO do computador<select name="os"><option value="3">Automático (USB)</option><option value="0">Windows</option><option value="1">macOS</option><option value="2">Linux</option></select>
<small>Se a detecção não reconhecer o computador, escolha o sistema manualmente.</small></label>
<label>Layout do teclado do computador<select name="layout"><option value="0">US</option><option value="1">ABNT2</option></select>
<small>Só importa para comandos com símbolos como : ; ? ' "</small></label>
<label>URL da página no PC<input name="url" maxlength="127" required></label>
<label>Linux: como abrir<select name="lxmode"><option value="0">Alt+F2 (executar comando)</option><option value="1">Ctrl+Alt+T (terminal)</option></select></label>
<label>Linux: comando (%URL% vira a URL)<input name="lxcmd" maxlength="127" required></label>
<button>Salvar</button><p id="msg"></p>
</form>
<a href="/">← Voltar para o envio</a>
</main><script>
const f=document.getElementById('f');
fetch('/api/config').then(r=>r.json()).then(c=>{for(const k in c)if(f.elements[k])f.elements[k].value=c[k]});
f.onsubmit=async e=>{e.preventDefault();const r=await fetch('/api/config',{method:'POST',body:new URLSearchParams(new FormData(f))});
document.getElementById('msg').textContent=r.ok?'✓ Salvo':'Erro ao salvar'};
</script></body></html>)HTML";
