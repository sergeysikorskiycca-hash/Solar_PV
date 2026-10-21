#pragma once
#include <Arduino.h>

// Admin UI (HTML/CSS/JavaScript) stored in flash rather than dynamic RAM.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="ru">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Solar_PV — админка</title>
<style>
:root{font-family:Arial,sans-serif;color:#eaf0f6;background:#111923}
*{box-sizing:border-box}body{margin:0}header{padding:18px 20px;background:#1c2938;border-bottom:1px solid #34465b}
h1{font-size:22px;margin:0 0 5px}header p{margin:0;color:#b8c6d5;font-size:14px}
main{max-width:1000px;margin:auto;padding:16px;display:grid;grid-template-columns:minmax(280px,1fr) minmax(260px,320px);gap:16px}
section{background:#1b2735;border:1px solid #34465b;border-radius:12px;padding:16px;margin-bottom:14px}
h2{font-size:18px;margin:0 0 14px}h3{font-size:15px;margin:14px 0 8px;color:#b9c9da}
label{display:block;font-size:14px;margin:10px 0 5px;color:#c8d5e2}
input[type=text],input[type=password],input[type=number],select{width:100%;padding:10px;border:1px solid #465c73;border-radius:7px;background:#101923;color:#fff;font-size:15px}.wifi-scan{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:end}.wifi-scan button{white-space:nowrap}.wifi-scan-status{margin-top:8px;font-size:12px;color:#9fb0c2;line-height:1.5}
.checks{display:grid;grid-template-columns:1fr 1fr;gap:10px}.check{display:flex;align-items:center;gap:8px;background:#243447;padding:10px;border-radius:7px;font-size:14px}
.check input{width:18px;height:18px}.positions{display:grid;grid-template-columns:1fr 1fr;gap:10px}
button{border:0;border-radius:8px;padding:11px 14px;font-size:15px;font-weight:bold;cursor:pointer}
.primary{background:#49b78b;color:#081b14}.secondary{background:#344a61;color:#fff}
.actions{display:flex;gap:9px;flex-wrap:wrap;margin-top:14px}
.hint{font-size:12px;line-height:1.5;color:#9fb0c2}.status{padding:10px;border-radius:8px;background:#243447;color:#bfead8;font-size:13px;line-height:1.5;overflow-wrap:anywhere}
.preview-wrap{position:sticky;top:12px;align-self:start}
.screen{width:240px;height:320px;position:relative;overflow:hidden;margin:0 auto;background:#17212b;border:2px solid #64778a;border-radius:5px;color:#fff}
.screen-title{position:absolute;left:8px;top:8px;font-size:16px;font-weight:bold;white-space:nowrap;max-width:224px;overflow:hidden}
.screen-status{position:absolute;left:8px;top:32px;color:#73d6a0;font-size:10px}
.demo{position:absolute;background:#263746;border:1px solid #52616d;border-radius:5px;padding:5px;overflow:hidden;cursor:grab;touch-action:none;user-select:none}.demo.selected{outline:2px solid #49b78b;outline-offset:1px}.resize-handle{position:absolute;right:0;bottom:0;width:13px;height:13px;background:#49b78b;border:1px solid #eaf0f6;border-radius:3px;cursor:nwse-resize;touch-action:none}.positions{display:none}
.demo .name{font-size:10px;color:#aebdca;white-space:nowrap}
.demo .value{font-size:17px;color:#ffd166;font-weight:bold;white-space:nowrap}
#pPower{width:224px;height:58px}#pVoltage,#pCurrent{width:106px;height:58px}#pBattery{width:224px;height:58px}
.screen-footer{position:absolute;bottom:6px;left:8px;right:8px;border-top:1px solid #52616d;padding-top:4px;font-size:9px;color:#aebdca;display:flex;justify-content:space-between}
.hidden{display:none!important}
@media(max-width:760px){main{grid-template-columns:1fr}.preview-wrap{position:static;grid-row:1}.screen{margin:0 auto}}
</style>
</head>
<body>
<header><h1>Solar_PV — админка</h1><p>Настройка тестового экрана ESP32 · TFT ST7789 240 × 320</p></header>
<main>
<div>
<section>
<h2>Оформление экрана</h2>
<label for="title">Заголовок TFT (до 18 символов, русский и английский)</label>
<input id="title" type="text" maxlength="18" value="SOLAR PV">
<label for="fontScale">Размер текста</label>
<select id="fontScale"><option value="1">Обычный</option><option value="2">Крупный</option></select>
<h3>Отображаемые блоки</h3>
<div class="checks">
<label class="check"><input id="showPower" type="checkbox"> Мощность PV</label>
<label class="check"><input id="showVoltage" type="checkbox"> Напряжение</label>
<label class="check"><input id="showCurrent" type="checkbox"> Ток</label>
<label class="check"><input id="showBattery" type="checkbox"> Аккумулятор</label>
</div>
<h3>Расположение блоков (X / Y)</h3>
<p class="hint">Перетаскивай блоки мышкой. Чтобы изменить размер, потяни за зелёный угол выбранного блока. Координаты и размеры сохраняются автоматически вместе с макетом.</p>
<div class="positions">
<div><label for="powerX">Мощность — X</label><input id="powerX" type="number" min="0" max="16"></div>
<div><label for="powerY">Мощность — Y</label><input id="powerY" type="number" min="40" max="230"></div>
<div><label for="voltageX">Напряжение — X</label><input id="voltageX" type="number" min="0" max="128"></div>
<div><label for="voltageY">Напряжение — Y</label><input id="voltageY" type="number" min="40" max="240"></div>
<div><label for="currentX">Ток — X</label><input id="currentX" type="number" min="0" max="128"></div>
<div><label for="currentY">Ток — Y</label><input id="currentY" type="number" min="40" max="240"></div>
<div><label for="batteryX">Аккумулятор — X</label><input id="batteryX" type="number" min="0" max="16"></div>
<div><label for="batteryY">Аккумулятор — Y</label><input id="batteryY" type="number" min="40" max="240"></div>
</div>
<div class="actions"><button class="primary" id="saveDisplay">Сохранить и применить</button><button class="secondary" id="defaults">Сбросить макет</button></div>
<h3>Страница дисплея</h3>
<p class="hint">Выберите страницу для физического дисплея. Автоматического переключения нет.</p>
<div class="actions"><button class="secondary" id="page1">Страница 1 · Показатели</button><button class="secondary" id="page2">Страница 2 · Wi-Fi</button></div>
<p class="status" id="pageStatus">Активна страница 1</p>
<p class="hint">Значения на экране демонстрационные. На первом этапе датчики и ESP32 не подключаются.</p>
</section>
<section>
<h2>Подключение к Wi-Fi</h2>
<div class="status" id="netStatus">Получение состояния устройства…</div>
<label for="networkList">Найденные сети Wi-Fi</label><div class="wifi-scan"><select id="networkList"><option value="">Сначала нажмите «Сканировать сети»</option></select><button class="secondary" id="scanNetworks">Сканировать сети</button></div><p class="wifi-scan-status" id="scanStatus">Список ещё не обновлялся.</p><label for="ssid">Выбранная сеть (SSID)</label><input id="ssid" type="text" maxlength="32" autocomplete="off">
<label for="password">Пароль Wi-Fi</label><input id="password" type="text" maxlength="63" autocomplete="new-password">
<p class="hint">Пароль отображается при вводе. Для сохранённой сети оставьте поле пустым — устройство использует сохранённый пароль. Можно сохранить до 8 сетей; настройки переживают перезагрузку и отключение питания.</p>
<div class="actions"><button class="primary" id="saveWifi">Сохранить Wi-Fi и перезапустить</button></div>
</section>
</div>
<div class="preview-wrap">
<section>
<h2>Предпросмотр</h2>
<div class="screen" id="preview">
<div class="screen-title" id="prevTitle">SOLAR PV</div>
<div class="screen-status">DEMO DATA · Wi-Fi</div>
<div class="demo" id="pPower"><div class="name">Мощность PV</div><div class="value">DEMO W</div><span class="resize-handle" title="Изменить размер"></span></div>
<div class="demo" id="pVoltage"><div class="name">Напряжение</div><div class="value">DEMO V</div><span class="resize-handle" title="Изменить размер"></span></div>
<div class="demo" id="pCurrent"><div class="name">Ток</div><div class="value">DEMO A</div><span class="resize-handle" title="Изменить размер"></span></div>
<div class="demo" id="pBattery"><div class="name">Аккумулятор</div><div class="value">DEMO %</div><span class="resize-handle" title="Изменить размер"></span></div>
<div class="screen-footer"><span id="prevNetwork">Wi-Fi: —</span><span>Стр. 1</span></div>
</div>
<p class="hint">Предпросмотр приблизительный. Итоговый вид отображается на физическом TFT.</p>
</section>
</div>
</main>
<script>
const ids=['powerX','powerY','voltageX','voltageY','currentX','currentY','batteryX','batteryY'];
const defaults={title:'SOLAR PV',fontScale:1,showPower:true,showVoltage:true,showCurrent:true,showBattery:true,powerX:8,powerY:55,voltageX:8,voltageY:125,currentX:125,currentY:125,batteryX:8,batteryY:205,powerW:224,powerH:58,voltageW:106,voltageH:58,currentW:106,currentH:58,batteryW:224,batteryH:58};
const dims={power:{w:224,h:58},voltage:{w:106,h:58},current:{w:106,h:58},battery:{w:224,h:58}};
function val(id){return document.getElementById(id).value}
function checked(id){return document.getElementById(id).checked}
function setVal(id,v){document.getElementById(id).value=v}
function updatePreview(){
 document.getElementById('prevTitle').textContent=val('title')||'SOLAR PV';
 document.getElementById('prevTitle').style.fontSize=(val('fontScale')==='2'?'19px':'16px');
 const map=[['power','pPower'],['voltage','pVoltage'],['current','pCurrent'],['battery','pBattery']];
 map.forEach(([key,el])=>{
  const node=document.getElementById(el);
  node.classList.toggle('hidden',!checked('show'+key[0].toUpperCase()+key.slice(1)));
  node.style.left=val(key+'X')+'px';node.style.top=val(key+'Y')+'px';
  node.style.width=dims[key].w+'px';node.style.height=dims[key].h+'px';
  node.querySelector('.value').style.fontSize=(val('fontScale')==='2'?'21px':'17px');
 });
}
function loadConfig(c){
 setVal('title',c.title);setVal('fontScale',c.fontScale);
 ['Power','Voltage','Current','Battery'].forEach(k=>document.getElementById('show'+k).checked=!!c['show'+k]);
 ids.forEach(id=>setVal(id,c[id]));
 ['power','voltage','current','battery'].forEach(k=>{dims[k].w=Number(c[k+'W']||defaults[k+'W']);dims[k].h=Number(c[k+'H']||defaults[k+'H']);});
 setVal('ssid',c.ssid||'');setVal('password','');
 renderSavedNetworks(c.savedNetworks||[]);
 document.getElementById('pageStatus').textContent='Активна страница '+(c.tftPage===2?'2 · Wi-Fi':'1 · Показатели');
 document.getElementById('netStatus').textContent=c.networkStatus;
 document.getElementById('prevNetwork').textContent=c.networkLabel;
 updatePreview();
}
function collectDisplay(){
 const d={title:val('title'),fontScale:val('fontScale')};
 ['Power','Voltage','Current','Battery'].forEach(k=>d['show'+k]=checked('show'+k));
 ids.forEach(id=>d[id]=val(id));
 ['power','voltage','current','battery'].forEach(k=>{d[k+'W']=dims[k].w;d[k+'H']=dims[k].h;});
 return d;
}
document.querySelectorAll('input,select').forEach(el=>el.addEventListener('input',updatePreview));

// Drag and resize widgets directly on the TFT preview. All measurements use TFT pixels.
const preview=document.getElementById('preview');
const widgetKeys={pPower:'power',pVoltage:'voltage',pCurrent:'current',pBattery:'battery'};
let pointerEdit=null;
Object.keys(widgetKeys).forEach(id=>{
 const node=document.getElementById(id), key=widgetKeys[id];
 node.addEventListener('pointerdown',e=>{
  if(e.button!==0)return;
  const resizing=e.target.classList.contains('resize-handle');
  document.querySelectorAll('.demo').forEach(n=>n.classList.remove('selected'));
  node.classList.add('selected');
  pointerEdit={id,key,resizing,startX:e.clientX,startY:e.clientY,x:Number(val(key+'X'))||0,y:Number(val(key+'Y'))||0,w:dims[key].w,h:dims[key].h};
  node.setPointerCapture(e.pointerId);e.preventDefault();
 });
 node.addEventListener('pointermove',e=>{
  if(!pointerEdit||pointerEdit.id!==id)return;
  const rect=preview.getBoundingClientRect(),sx=240/rect.width,sy=320/rect.height;
  const dx=Math.round((e.clientX-pointerEdit.startX)*sx),dy=Math.round((e.clientY-pointerEdit.startY)*sy);
  if(pointerEdit.resizing){
   dims[key].w=Math.max(40,Math.min(240-pointerEdit.x,pointerEdit.w+dx));
   dims[key].h=Math.max(36,Math.min(290-pointerEdit.y,pointerEdit.h+dy));
  }else{
   setVal(key+'X',Math.max(0,Math.min(240-dims[key].w,pointerEdit.x+dx)));
   setVal(key+'Y',Math.max(40,Math.min(290-dims[key].h,pointerEdit.y+dy)));
  }
  updatePreview();
 });
 const finish=()=>{pointerEdit=null;};
 node.addEventListener('pointerup',finish);node.addEventListener('pointercancel',finish);
});
document.getElementById('saveDisplay').addEventListener('click',async()=>{
 const data=collectDisplay();const body=new URLSearchParams();
 Object.keys(data).forEach(k=>{if(typeof data[k]==='boolean'){if(data[k])body.set(k,'1')}else body.set(k,data[k])});
 try{const r=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json();alert(j.message);if(j.ok)loadConfig(await (await fetch('/api/config')).json());
 }catch(e){alert('Не удалось связаться с ESP32. Проверьте подключение.')}
});
document.getElementById('defaults').addEventListener('click',()=>{
 loadConfig(Object.assign({},defaults,{ssid:val('ssid'),networkStatus:document.getElementById('netStatus').textContent,networkLabel:document.getElementById('prevNetwork').textContent}));
});
async function choosePage(page){
 const status=document.getElementById('pageStatus');
 status.textContent='Переключаю на страницу '+page+'…';
 try{
  const r=await fetch('/api/page?page='+page+'&t='+Date.now(),{cache:'no-store'});
  const j=await r.json();
  if(!r.ok||!j.ok) throw new Error(j.message||('HTTP '+r.status));
  status.textContent=j.message;
 }catch(e){
  status.textContent='Ошибка переключения: '+(e.message||'нет ответа от ESP32');
 }
}
document.getElementById('page1').addEventListener('click',()=>choosePage(1));
document.getElementById('page2').addEventListener('click',()=>choosePage(2));
function renderSavedNetworks(networks){
 const list=document.getElementById('networkList');list.innerHTML='';
 list.add(new Option('— Сохранённые сети и сканирование —',''));
 networks.forEach(n=>list.add(new Option('★ '+n.ssid+' · сохранена',n.ssid)));
 if(!networks.length)list.add(new Option('Сохранённых сетей пока нет',''));
}
document.getElementById('networkList').addEventListener('change',()=>{if(val('networkList'))setVal('ssid',val('networkList'));});
document.getElementById('scanNetworks').addEventListener('click',async()=>{
 const button=document.getElementById('scanNetworks');const status=document.getElementById('scanStatus');const list=document.getElementById('networkList');
 button.disabled=true;status.textContent='Идёт поиск сетей… это может занять несколько секунд.';
 try{
  const r=await fetch('/api/scan?t='+Date.now(),{cache:'no-store'});const j=await r.json();
  if(!r.ok||!j.ok)throw new Error(j.message||('HTTP '+r.status));
  const config=(await (await fetch('/api/config')).json());
  const saved=config.savedNetworks||[];
  list.innerHTML='';list.add(new Option('— Выберите сеть —',''));
  saved.forEach(n=>list.add(new Option('★ '+n.ssid+' · сохранена',n.ssid)));
  j.networks.forEach(n=>{if(Array.from(list.options).some(o=>o.value===n.ssid))return;const bars=n.rssi>=-55?'▂▄▆█':n.rssi>=-67?'▂▄▆':n.rssi>=-75?'▂▄':'▂';const lock=n.secure?'🔒':'без пароля';list.add(new Option(bars+'  '+n.ssid+'  ·  '+n.rssi+' dBm  ·  '+lock,n.ssid));});
  if(!j.networks.length)status.textContent='Сети не найдены. Сохранённые сети остаются доступны в списке.';
  else status.textContent='Найдено сетей: '+j.networks.length+'. ★ — сети, пароль которых уже сохранён.';
 }catch(e){status.textContent='Ошибка сканирования: '+(e.message||'нет ответа от ESP32');}
 finally{button.disabled=false;}
});
document.getElementById('saveWifi').addEventListener('click',async()=>{
 const body=new URLSearchParams({ssid:val('ssid'),password:val('password')});
 try{const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});
  const j=await r.json();alert(j.message);if(j.ok)document.getElementById('password').value='';
 }catch(e){alert('Ответ не получен. Устройство может перезапускаться; подождите и откройте страницу снова.')}
});
fetch('/api/config').then(r=>r.json()).then(loadConfig).catch(()=>{document.getElementById('netStatus').textContent='Нет связи с устройством';});
</script>
</body>
</html>
)rawliteral";
