#pragma once
#include <Arduino.h>

// Admin UI (HTML/CSS/JavaScript) stored in flash rather than dynamic RAM.
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Solar_PV — редактор экранов</title>
<style>
:root{font-family:Arial,sans-serif;color:#eaf0f6;background:#111923}*{box-sizing:border-box}body{margin:0}
header{padding:16px 20px;background:#1c2938;border-bottom:1px solid #34465b}h1{font-size:21px;margin:0 0 5px}header p,.hint{color:#9fb0c2;font-size:12px;line-height:1.5;margin:5px 0}
main{max-width:1120px;margin:auto;padding:16px;display:grid;grid-template-columns:minmax(300px,1fr) 340px;gap:16px}
section{background:#1b2735;border:1px solid #34465b;border-radius:12px;padding:14px;margin-bottom:14px}h2{font-size:17px;margin:0 0 12px}h3{font-size:14px;margin:14px 0 8px;color:#b9c9da}
label{display:block;font-size:13px;margin:9px 0 5px;color:#c8d5e2}input[type=text],input[type=password],select{width:100%;padding:9px;border:1px solid #465c73;border-radius:7px;background:#101923;color:#fff;font-size:14px}
button{border:0;border-radius:8px;padding:10px 12px;font-size:13px;font-weight:bold;cursor:pointer;background:#344a61;color:#fff}button.primary{background:#49b78b;color:#081b14}button.danger{background:#7e3842}button:disabled{opacity:.45;cursor:not-allowed}
.row{display:flex;gap:8px;align-items:center;flex-wrap:wrap}.row>*{flex:1}.status{padding:9px;border-radius:8px;background:#243447;color:#bfead8;font-size:12px;line-height:1.5;overflow-wrap:anywhere}
.page-tabs{display:flex;gap:6px;flex-wrap:wrap;margin-bottom:10px}.page-tab{flex:0 0 auto}.page-tab.active{background:#49b78b;color:#081b14}.page-tab.device{outline:2px solid #ffd166;outline-offset:1px}
.screen{width:240px;height:320px;position:relative;overflow:hidden;margin:0 auto;background:#17212b;border:2px solid #64778a;border-radius:5px;color:#fff}
.screen-title{position:absolute;left:8px;top:7px;font-size:16px;font-weight:bold;white-space:nowrap;max-width:224px;overflow:hidden;text-overflow:ellipsis}
.widget{position:absolute;background:#101820;border:1px solid #52616d;border-radius:5px;padding:5px;overflow:hidden;cursor:grab;touch-action:none;user-select:none}.widget.selected{outline:2px solid #49b78b;outline-offset:1px}.widget.locked{cursor:default}.widget .name{padding-right:27px;font-size:11px;color:#fff;white-space:nowrap;overflow:hidden;outline:none}.widget .name:focus{user-select:text;cursor:text;background:#17212b;border-radius:2px}.widget .value{font-size:15px;color:#ffd400;font-weight:bold;white-space:normal;overflow-wrap:anywhere}.widget .wifi-lines{font-size:12px;line-height:1.2;color:#fff;white-space:normal;overflow-wrap:anywhere}.widget .free-text{font-size:15px;line-height:1.15;color:#fff;white-space:pre-wrap;overflow-wrap:anywhere;user-select:none}.element-editor{background:#101923;border:1px solid #465c73;border-radius:8px;padding:9px;margin:8px 0}.element-editor .row{gap:6px}.element-editor label{flex:1;min-width:105px;margin:4px 0}.element-editor input,.element-editor select{width:100%;min-width:0;padding:7px;border:1px solid #465c73;border-radius:6px;background:#1b2735;color:#fff}.custom-lines{display:flex;flex-direction:column;gap:2px;overflow:hidden}.custom-line{white-space:nowrap;overflow:hidden;text-overflow:clip}
.resize-handle{position:absolute;right:0;bottom:0;width:13px;height:13px;background:#49b78b;border:1px solid #eaf0f6;border-radius:3px;cursor:nwse-resize;touch-action:none}.widget.locked .resize-handle{display:none}.lock-toggle{position:absolute;z-index:3;right:2px;top:2px;width:23px;height:22px;padding:0;border:1px solid #65798c;border-radius:4px;background:#243447;font-size:13px;line-height:20px}.widget.locked .lock-toggle{background:#49b78b;color:#081b14}

.preview-wrap{position:sticky;top:12px;align-self:start}.toolbar{display:grid;grid-template-columns:1fr 1fr;gap:8px}.toolbar .wide{grid-column:1/-1}.hint{margin-top:8px}.wifi-scan{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:end}#energyHistoryRows{overflow-x:auto}#energyHistoryRows table{width:100%;border-collapse:collapse;font-size:12px}#energyHistoryRows th,#energyHistoryRows td{padding:7px;border-bottom:1px solid #34465b;text-align:left;vertical-align:top}#energyHistoryRows th{color:#9fb0c2}#energyHistorySummary{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0}#energyHistorySummary span{background:#243447;border-radius:7px;padding:8px;font-size:12px}
@media(max-width:780px){main{grid-template-columns:1fr}.preview-wrap{position:static;grid-row:1}.screen{margin:auto}}
</style></head><body>
<header><h1>Solar_PV — редактор экранов</h1><p>Универсальные страницы и повторно используемые блоки · TFT ST7789 240 × 320</p></header>
<main><div>
<section><h2>Учёт энергии и история</h2><div class="status" id="energyHistoryStatus">Загрузка календарной истории…</div><div id="energyHistorySummary"></div><label for="energyHistoryPeriod">Период отображения</label><select id="energyHistoryPeriod"><option value="all">Вся сохранённая история</option><option value="month">Текущий месяц</option><option value="year">Текущий год</option></select><div id="energyHistoryRows"></div><div id="energyHistoryByString"></div><p class="hint">Месячные и годовые итоги рассчитываются из сохранённых суточных записей. Срок истории зависит от количества обнаруженных стрингов и доступных 24C32.</p></section>
<section><h2>Страницы</h2><div class="page-tabs" id="pageTabs"></div>
<label for="pageTitle">Название выбранной страницы</label><input id="pageTitle" type="text" maxlength="24" value="Показатели"><label style="display:flex;align-items:center;gap:8px"><input id="showPageTitle" type="checkbox" checked> Показывать название страницы</label>
<div class="row"><button class="primary" id="newPage">＋ Новая страница</button><button id="followDevice">К странице дисплея</button><button class="danger" id="deletePage">Удалить страницу</button></div>
<p class="hint">Жёлтая рамка у вкладки показывает страницу, которая сейчас выведена на физический дисплей. Выбирать другую страницу для редактирования можно независимо.</p>
<div class="status" id="deviceStatus">Получение состояния ESP32…</div></section>

<section><h2>Библиотека блоков</h2>
<div class="row" style="margin-top:8px"><button class="primary" id="addWidget">＋ Добавить на страницу</button><button id="deleteWidget">Удалить выбранный блок</button></div>
<p class="hint">Перетаскивай блок мышью, размер меняй за зелёный угол. Подпись блока можно изменить прямо на холсте.</p><p class="hint">Содержимое и поведение блока задаются списком элементов ниже.</p>
<div id="customEditor" style="display:none"><h3>Элементы универсального блока</h3><p class="hint">Добавляй строки с любыми датчиками и параметрами. Подпись, размер и цвет настраиваются независимо.</p><div id="customElements"></div><div id="systemElementTools"><section id="clockSettingsPanel"><h2>Часы и дата RTC</h2><p class="hint">Установи время и дату часов DS3231. Команда передаётся на ESP8266-шлюз и записывается в RTC.</p><div class="row"><div><label for="clockDate">Дата</label><input id="clockDate" type="date"></div><div><label for="clockTime">Время</label><input id="clockTime" type="time" step="1"></div></div><button class="primary" id="setClock" style="margin-top:8px;width:100%">Установить время и дату</button><div class="status" id="clockStatus" style="margin-top:8px">Чтение времени устройства…</div></section><section id="wifiModePanel"><h2>Состояние и управление Wi-Fi</h2><div class="status" id="wifiModeStatus">Загрузка режима…</div><div class="row" style="margin-top:8px"><button id="toggleRouter">Роутер: ВЫКЛ</button><button id="toggleAp">Точка: ВКЛ</button></div><button class="danger" id="wifiOff" style="width:100%;margin-top:8px">Выключить обе сети</button><p class="hint">После перезапуска включится только точка SolarPV-Setup. Если выключить обе сети, админка станет недоступна.</p></section><section id="wifiDefaultsPanel"><h2>Постоянные настройки запуска Wi-Fi</h2><p class="hint">Эти значения сохраняются во флеш-памяти и применяются после перезагрузки. Переключатели выше действуют только до перезапуска.</p><label><input id="defaultRouter" type="checkbox"> Подключаться к домашнему роутеру</label><label><input id="defaultAp" type="checkbox"> Включать точку SolarPV-Setup</label><button class="primary" id="saveWiFiDefaults" style="width:100%;margin-top:8px">Сохранить настройки запуска</button><div class="status" id="wifiDefaultsStatus" style="margin-top:8px">Настройки запуска…</div></section>
<section id="wifiConnectionPanel"><h2>Подключение к Wi-Fi</h2><div class="status" id="netStatus">Получение состояния устройства…</div>
<label for="networkList">Найденные и сохранённые сети</label><div class="wifi-scan"><select id="networkList"><option value="">Нажмите «Сканировать»</option></select><button id="scanNetworks">Сканировать</button></div><p class="hint" id="scanStatus">Список ещё не обновлялся.</p>
<label for="ssid">SSID</label><input id="ssid" type="text" maxlength="32" autocomplete="off"><label for="password">Пароль Wi-Fi</label><input id="password" type="password" maxlength="63" autocomplete="new-password">
<p class="hint">Для сохранённой сети оставь пароль пустым. Можно хранить до 8 сетей.</p><button class="primary" id="saveWifi" style="margin-top:8px;width:100%">Сохранить Wi-Fi и перезапустить</button></section><section id="calibrationEditor" style="display:none"><h3>Коэффициенты калибровки</h3><div id="calibrationRows"></div><button class="primary" id="applyCalibration" style="width:100%;margin-top:8px">Сохранить коэффициенты</button></section></div><button class="primary" id="addCustomElement" style="width:100%;margin-top:8px">＋ Добавить элемент</button><p class="hint">До 12 элементов в одном блоке. Источник — физический номер датчика, подпись на экране может быть любой.</p></div>
<h3>Оформление выбранного блока</h3>
<div class="row"><label style="display:flex;align-items:center;gap:8px"><input id="showWidgetHeading" type="checkbox" checked> Показывать заголовок</label><label style="display:flex;align-items:center;gap:8px"><input id="showWidgetBorder" type="checkbox" checked> Показывать рамку</label></div>
<label for="rowSpacing">Расстояние между строками, px</label><input id="rowSpacing" type="number" min="0" max="20" step="1" value="4"><p class="hint">Один отступ применяется ко всем строкам выбранного блока.</p><label for="headingLetterSpacing">Интервал букв заголовка, px</label><input id="headingLetterSpacing" type="number" min="-2" max="8" step="1" value="0"><label for="contentLetterSpacing">Интервал букв содержимого, px</label><input id="contentLetterSpacing" type="number" min="-2" max="8" step="1" value="0"><p class="hint">0 — обычный интервал; положительное значение раздвигает буквы, отрицательное уплотняет.</p><label for="backgroundColor">Цвет фона блока</label><select id="backgroundColor"><option value="0">Почти чёрный</option><option value="1">Тёмно-синий</option><option value="2">Тёмно-зелёный</option><option value="3">Тёмно-фиолетовый</option><option value="4">Тёмно-красный</option><option value="5">Графитовый</option><option value="6">Чёрный</option></select>
<div class="toolbar" style="margin-top:12px"><button class="primary wide" id="saveDisplay">Сохранить все страницы и применить</button><button id="showOnTft">Показать выбранную страницу на TFT</button><button id="resetLayout">Восстановить исходный макет</button></div>
<p class="hint">В обычных числовых блоках предпросмотра остаются демонстрационные значения; блоки мониторинга стрингов показывают последние данные датчиков.</p></section>

</div>
<div class="preview-wrap"><section><h2>Предпросмотр холста</h2><div class="screen" id="preview">
<div class="screen-title" id="prevTitle">Показатели</div>
</div>
<p class="hint">Предпросмотр повторяет координаты TFT. Выбранная для редактирования страница не обязана совпадать с физическим экраном.</p></section></div></main>
<script>
const MAX_PAGES=8,MAX_WIDGETS=12,MAX_WIDGET_ELEMENTS=12;
const defaults=[ {label:'Блок 1',x:8,y:55,w:224,h:58,enabled:true},
 {label:'Блок 2',x:8,y:125,w:106,h:58,enabled:true},
 {label:'Блок 3',x:125,y:125,w:106,h:58,enabled:true},
 {label:'Блок 4',x:8,y:205,w:224,h:58,enabled:true}
];
const backgroundPalette=['#101820','#001B2E','#073326','#302044','#481D20','#30343B','#000000'];

let pages=[],editorPage=0,devicePage=0,selectedWidget=-1,followDevice=true,pointerEdit=null;
let networkStatus='',networkLabel='Wi-Fi: —',savedNetworks=[],configuredSsid='';
let wifiInfo={connected:false,rssi:0,routerIp:'—',apActive:false,apIp:'—'};
let wifiMode={router:false,ap:true,apActive:true};
let stringTelemetry=[],totalEnergyWh=0,gatewayTelemetry={valid:false,voltage:0,current:0,power:0};
let currentCalibration=[1,1,1,1,1,1],voltageCalibration=[1,1,1,1,1,1];
let energyHistory={ready:false,storageLimited:false,currentDate:0,current:[],days:[]};
const $=id=>document.getElementById(id);
function clone(x){return JSON.parse(JSON.stringify(x))}
function energyDateText(key){const s=String(key||'');return s.length===8?s.slice(0,4)+'-'+s.slice(4,6)+'-'+s.slice(6,8):'—'}
function energyKwh(wh){return (Number(wh||0)/1000).toFixed(3)+' кВт·ч'}
function renderEnergyHistory(){
 const status=$('energyHistoryStatus'),summary=$('energyHistorySummary'),root=$('energyHistoryRows'),byString=$('energyHistoryByString');
 if(!energyHistory.ready){status.textContent='Календарный архив ещё загружается или EEPROM недоступна.';summary.innerHTML='';root.innerHTML='';byString.innerHTML='';return}
 const currentDate=Number(energyHistory.currentDate)||0;
 const currentRows=Array.isArray(energyHistory.current)?energyHistory.current:[];
 const currentTotal=currentRows.reduce((a,x)=>a+Number(x.wh||0),0);
 const days=Array.isArray(energyHistory.days)?energyHistory.days.slice():[];
 const periodSelect=$('energyHistoryPeriod'),previousPeriod=periodSelect.value||'all';
 const monthKeys=new Set(),yearKeys=new Set();
 days.forEach(d=>{const k=String(d.dateKey||'');if(k.length===8){monthKeys.add(k.slice(0,6));yearKeys.add(k.slice(0,4))}});
 if(currentDate){monthKeys.add(String(currentDate).slice(0,6));yearKeys.add(String(currentDate).slice(0,4))}
 periodSelect.innerHTML='';periodSelect.add(new Option('Вся сохранённая история','all'));
 Array.from(yearKeys).sort().reverse().forEach(y=>periodSelect.add(new Option('Год '+y,'year:'+y)));
 Array.from(monthKeys).sort().reverse().forEach(m=>periodSelect.add(new Option('Месяц '+m.slice(4,6)+'.'+m.slice(0,4),'month:'+m)));
 if(Array.from(periodSelect.options).some(o=>o.value===previousPeriod))periodSelect.value=previousPeriod;
 const period=periodSelect.value||'all',parts=period.split(':'),periodType=parts[0],periodKey=parts[1]||'';
 const filtered=days.filter(d=>{const k=String(d.dateKey||'');return periodType==='month'?k.slice(0,6)===periodKey:periodType==='year'?k.slice(0,4)===periodKey:true});
 const rows=filtered.filter(d=>Number(d.dateKey)!==currentDate);
 const currentKey=String(currentDate||'');
 if(currentDate&&currentTotal&&(periodType==='all'||(periodType==='month'&&currentKey.slice(0,6)===periodKey)||(periodType==='year'&&currentKey.slice(0,4)===periodKey)))
  rows.push({date:energyDateText(currentDate),dateKey:currentDate,totalWh:currentTotal,strings:currentRows});
 const periodTotal=rows.reduce((a,d)=>a+Number(d.totalWh||0),0);
 summary.innerHTML='';
 const cards=[['Сегодня',energyKwh(currentTotal)],['Итого за период',energyKwh(periodTotal)],['Дней в периоде',String(rows.length)]];
 cards.forEach(([name,value])=>{const el=document.createElement('span');el.textContent=name+': '+value;summary.appendChild(el)});
 status.textContent='RTC: '+(currentDate?energyDateText(currentDate):'дата пока недоступна')+
  ' · архивных суток: '+days.length+' · обнаружено стрингов: '+Number(energyHistory.detectedStrings||0)+
  ' · ориентир хранения: '+Number(energyHistory.estimatedArchiveDays||0)+' дней'+
  ' · checkpoint: до 125 стрингов'+(energyHistory.storageLimited||Number(energyHistory.detectedStrings||0)>125?' · достигнуто ограничение ёмкости календарного хранилища':'');
 if(!rows.length){root.textContent='Для выбранного периода пока нет сохранённых суточных записей.';byString.innerHTML='';return}
 const table=document.createElement('table'),head=document.createElement('thead'),body=document.createElement('tbody');
 head.innerHTML='<tr><th>Дата</th><th>Выработка</th><th>По стрингам</th></tr>';
 rows.slice().reverse().forEach(d=>{
  const tr=document.createElement('tr'),date=document.createElement('td'),total=document.createElement('td'),strings=document.createElement('td');
  date.textContent=d.date||energyDateText(d.dateKey);total.textContent=energyKwh(d.totalWh);
  strings.textContent=(d.strings||[]).map(x=>'#'+x.id+' '+energyKwh(x.wh)).join(' · ')||'—';
  tr.append(date,total,strings);body.appendChild(tr);
 });
 table.append(head,body);root.innerHTML='';root.appendChild(table);
 const totals=new Map();
 rows.forEach(d=>(d.strings||[]).forEach(x=>totals.set(Number(x.id),(totals.get(Number(x.id))||0)+Number(x.wh||0))));
 if(!totals.size){byString.innerHTML='';return}
 const title=document.createElement('h3');title.textContent='Итого по стрингам за выбранный период';
 const byTable=document.createElement('table'),byHead=document.createElement('thead'),byBody=document.createElement('tbody');
 byHead.innerHTML='<tr><th>Стринг</th><th>Выработка</th></tr>';
 Array.from(totals.entries()).sort((a,b)=>a[0]-b[0]).forEach(([id,wh])=>{const tr=document.createElement('tr'),name=document.createElement('td'),value=document.createElement('td');name.textContent='#'+id;value.textContent=energyKwh(wh);tr.append(name,value);byBody.appendChild(tr)});
 byTable.append(byHead,byBody);byString.innerHTML='';byString.append(title,byTable);
}
async function refreshEnergyHistory(){
 try{const r=await fetch('/api/energy-history?t='+Date.now(),{cache:'no-store'});if(!r.ok)return;energyHistory=await r.json();renderEnergyHistory()}
 catch(e){$('energyHistoryStatus').textContent='Нет связи с календарным архивом.'}
}
function normalizeCalibration(values){return Array.from({length:10},(_,i)=>{const v=Number(Array.isArray(values)?values[i]:1);return Number.isFinite(v)&&v>=0.001&&v<=99.999?v:1})}
function renderCalibrationRows(elements){
 const root=$('calibrationRows');root.innerHTML='';
 (elements||[]).forEach((element)=>{
  const sensorId=Math.max(1,Math.min(10,Number(element.sourceId)||1)),index=sensorId-1;
  const row=document.createElement('div');row.className='row';row.style.margin='6px 0';
  const name=document.createElement('strong');name.textContent=element.label||uiTexts.stringPrefix+' '+sensorId;name.style.minWidth='75px';row.appendChild(name);
  const measured=stringTelemetry.find(v=>Number(v.id)===sensorId);
  const currentLabel=document.createElement('label');currentLabel.style.flex='1';currentLabel.textContent=uiTexts.currentLabel+' сейчас: '+(measured&&measured.valid?Number(measured.current).toFixed(2)+' А':'нет данных');
  const currentTarget=document.createElement('input');currentTarget.type='number';currentTarget.min='0.01';currentTarget.max='9999';currentTarget.step='0.01';currentTarget.placeholder='Должно быть, А';currentTarget.dataset.cal='currentTarget';currentTarget.dataset.sensorId=String(sensorId);
  currentTarget.addEventListener('change',()=>{if(currentTarget.value!=='')currentTarget.value=Number(Number(currentTarget.value).toFixed(2)).toFixed(2)});
  currentLabel.appendChild(currentTarget);row.appendChild(currentLabel);
  const voltageLabel=document.createElement('label');voltageLabel.style.flex='1';voltageLabel.textContent='Напряжение сейчас: '+(measured&&measured.valid?Number(measured.voltage).toFixed(1)+' В':'нет данных');
  const voltageTarget=document.createElement('input');voltageTarget.type='number';voltageTarget.min='0.1';voltageTarget.max='9999';voltageTarget.step='0.1';voltageTarget.placeholder='Должно быть, В';voltageTarget.dataset.cal='voltageTarget';voltageTarget.dataset.sensorId=String(sensorId);
  voltageTarget.addEventListener('change',()=>{if(voltageTarget.value!=='')voltageTarget.value=Number(Number(voltageTarget.value).toFixed(1)).toFixed(1)});
  voltageLabel.appendChild(voltageTarget);row.appendChild(voltageLabel);root.appendChild(row);
 });
}
function normalizePage(p,i){return {title:p.title||('Страница '+(i+1)),showTitle:p.showTitle!==false,widgets:(p.widgets||[]).slice(0,MAX_WIDGETS).map(w=>({label:w.label||'Блок',headingSize:Math.max(0,Math.min(5,Number(w.headingSize??1))),contentSize:Math.max(0,Math.min(5,Number(w.contentSize??2))),headingAlign:Math.max(0,Math.min(2,Number(w.headingAlign??0))),contentAlign:Math.max(0,Math.min(2,Number(w.contentAlign??0))),headingColor:Math.max(0,Math.min(5,Number(w.headingColor??0))),contentColor:Math.max(0,Math.min(5,Number(w.contentColor??3))),backgroundColor:Math.max(0,Math.min(6,Number(w.backgroundColor??0))),rowSpacing:Math.max(0,Math.min(20,Number(w.rowSpacing??4))),headingLetterSpacing:Math.max(-2,Math.min(8,Number(w.headingLetterSpacing??0))),contentLetterSpacing:Math.max(-2,Math.min(8,Number(w.contentLetterSpacing??0))),elements:(Array.isArray(w.elements)?w.elements:[]).slice(0,MAX_WIDGET_ELEMENTS).map((e,n)=>({label:Number(e.kind)===3?'Состояние точки':String(e.label??'').slice(0,54),kind:Number(e.kind)===3?2:Math.max(0,Math.min(6,Number(e.kind)||0)),sourceId:Number(e.kind)===4?Math.max(1,Math.min(10,Number(e.sourceId)||1)):((Number(e.sourceId)===7&&(Number(e.metric)===2||Number(e.metric)>=64))?11:Math.max(0,Math.min(11,Number(e.sourceId)||0))),metric:Number(e.kind)===3?4:((Number(e.sourceId)===11||(Number(e.sourceId)===7&&Number(e.metric)>=64))?Math.max(0,Math.min(127,Number(e.metric)||0)):Math.max(0,Math.min(5,Number(e.metric)||0))),fontSize:Math.max(0,Math.min(5,Number(e.fontSize??2))),color:Math.max(0,Math.min(5,Number(e.color??3)))})),locked:w.locked===true,showHeading:w.showHeading!==false,showBorder:w.showBorder!==false,x:Number(w.x)||0,y:Number(w.y)||40,w:Math.max(40,Number(w.w)||106),h:Math.max(36,Number(w.h)||58),enabled:w.enabled!==false}))}}
function ensurePages(){if(!pages.length)pages=[{title:'Показатели',showTitle:true,widgets:clone(defaults)},{title:'Система',showTitle:true,widgets:[{label:'Системные функции',elements:[{label:'Время',kind:1,sourceId:0,metric:0,fontSize:2,color:1},{label:'Дата',kind:1,sourceId:0,metric:1,fontSize:1,color:0},{label:'Состояние роутера',kind:2,sourceId:0,metric:0,fontSize:1,color:2},{label:'Состояние точки',kind:2,sourceId:0,metric:4,fontSize:1,color:3},{label:'Выбор сети',kind:5,sourceId:0,metric:0,fontSize:1,color:0}],x:8,y:55,w:224,h:220,enabled:true}]}];pages=pages.slice(0,MAX_PAGES).map(normalizePage)}

const customFontSizes=[9,12,15,20,24,27],customPalette=['#FFFFFF','#00D5FF','#39E75F','#FFD400','#FF4D4D','#FF8C00'];
function customElementText(element){const kind=Number(element.kind)||0,metric=Number(element.metric)||0;const prefix=element.label?element.label+': ':'';if(kind===6)return element.label;if(kind===1)return prefix+(metric===1?'--.--.----':(metric===2?'--:--:-- --.--.----':'--:--:--'));if(kind===2){let value=wifiMode.router?'ВКЛ':'ВЫКЛ';if(metric===1)value=configuredSsid||'—';else if(metric===2)value=wifiInfo.connected?Number(wifiInfo.rssi)+' dBm':'—';else if(metric===3)value=wifiInfo.routerIp||'—';else if(metric===4)value=wifiInfo.apActive?'ВКЛ':'ВЫКЛ';else if(metric===5)value=wifiInfo.apIp||'—';return prefix+value}if(kind===3)return prefix+uiTexts.routerLabel+' '+(wifiMode.router?'ВКЛ':'ВЫКЛ')+' / '+uiTexts.apLabel+' '+(wifiMode.ap?'ВКЛ':'ВЫКЛ');if(kind===4){const v=stringTelemetry.find(x=>Number(x.id)===Number(element.sourceId));return prefix+(v&&v.valid?Number(v.current).toFixed(2)+' А / '+Number(v.voltage).toFixed(1)+' В':'нет данных')}if(kind===5)return prefix+(configuredSsid||'сеть не выбрана');const unit=metric===1?'А':metric===2?'Вт':metric===3?'%':'В';if(metric===3)return prefix+'-- %';if(Number(element.sourceId)===0){if(!gatewayTelemetry.valid)return prefix+'— '+unit;const value=metric===1?Number(gatewayTelemetry.current).toFixed(2):metric===2?Number(gatewayTelemetry.power).toFixed(1):Number(gatewayTelemetry.voltage).toFixed(1);return prefix+value+' '+unit}if(Number(element.sourceId)===11&&metric===2){const total=stringTelemetry.filter(x=>x&&x.valid).reduce((a,x)=>a+Number(x.power||0),0);return prefix+total.toFixed(1)+' Вт'}if(Number(element.sourceId)===11&&metric===4)return prefix+(totalEnergyWh/1000).toFixed(3)+' кВт·ч';if(Number(element.sourceId)===11&&metric>=64){const mask=metric-64;const selected=stringTelemetry.filter(x=>x&&x.valid&&(mask&(1<<(Number(x.id)-1))));if(!selected.length)return prefix+'— Вт';const total=selected.reduce((a,x)=>a+Number(x.power||0),0);return prefix+total.toFixed(1)+' Вт'}const v=stringTelemetry.find(x=>Number(x.id)===Number(element.sourceId));if(metric===4)return prefix+(Number(v&&v.energyWh||0)/1000).toFixed(3)+' кВт·ч';if(!v||!v.valid)return prefix+'— '+unit;const value=metric===1?Number(v.current).toFixed(2):metric===2?Number(v.power).toFixed(1):Number(v.voltage).toFixed(1);return prefix+value+' '+unit}
function updateCustomPreviews(){document.querySelectorAll('.custom-lines').forEach(root=>{const i=Number(root.closest('.widget')?.dataset.index);const w=pages[editorPage]?.widgets[i];if(!w)return;root.innerHTML='';(w.elements||[]).forEach(e=>{const line=document.createElement('div');line.className='custom-line';line.textContent=customElementText(e);line.style.fontSize=customFontSizes[e.fontSize]+'px';line.style.letterSpacing=(w.contentLetterSpacing??0)+'px';line.style.color=customPalette[e.color];root.appendChild(line)})})}
function customField(title,control){const wrap=document.createElement('label');wrap.textContent=title;wrap.appendChild(control);return wrap}
function customSelect(options,value){const select=document.createElement('select');options.forEach(o=>select.add(new Option(o[1],String(o[0]))));select.value=String(value);return select}
function renderCustomEditor(w){
 const root=$('customElements');
 const tools=$('systemElementTools');
 const managedPanels=['clockSettingsPanel','wifiModePanel','wifiDefaultsPanel','wifiConnectionPanel','calibrationEditor'].map(id=>$(id)).filter(Boolean);
 managedPanels.forEach(panel=>tools.appendChild(panel));
 root.innerHTML='';
 w.elements=Array.isArray(w.elements)?w.elements.slice(0,MAX_WIDGET_ELEMENTS):[];
 const kinds=[[0,'Данные датчика'],[1,'Часы / дата'],[2,'Состояние Wi-Fi'],[4,'Калибровка датчика'],[5,'Выбор Wi-Fi сети'],[6,'Текстовая строка']];
 const sizeOptions=[[0,'Компактный'],[1,'Небольшой'],[2,'Средний'],[3,'Крупный'],[4,'Очень крупный'],[5,'Максимальный']];
 const colorOptions=[[0,'Белый'],[1,'Голубой'],[2,'Зелёный'],[3,'Жёлтый'],[4,'Красный'],[5,'Оранжевый']];
 let rtcAttached=false,wifiConnectionAttached=false,wifiModeAttached=false,wifiDefaultsAttached=false,calibrationAttached=false;
 w.elements.forEach((element,index)=>{
  const row=document.createElement('div');row.className='element-editor';
  const top=document.createElement('div');top.className='row';
  const kind=customSelect(kinds,element.kind??0);
  kind.addEventListener('change',()=>{element.kind=Number(kind.value);if(element.kind===3||element.kind===4)element.metric=2;else if(element.kind===1)element.metric=0;if(element.kind===4&&(Number(element.sourceId)<1||Number(element.sourceId)>10))element.sourceId=1;renderAll()});
  top.appendChild(customField('Тип элемента',kind));
  const label=document.createElement('input');label.type='text';label.maxLength=54;label.value=element.label??'';label.placeholder='Без подписи';
  label.addEventListener('input',()=>{element.label=label.value.slice(0,54);updateCustomPreviews()});
  top.appendChild(customField('Подпись',label));row.appendChild(top);
  const bottom=document.createElement('div');bottom.className='row';
  const k=Number(element.kind)||0;
  if(k===0||k===4){
   const sourceOptions=k===4?[[1,'Датчик 1'],[2,'Датчик 2'],[3,'Датчик 3'],[4,'Датчик 4'],[5,'Датчик 5'],[6,'Датчик 6'],[7,'Датчик 7'],[8,'Датчик 8'],[9,'Датчик 9'],[10,'Датчик 10']]:[[0,'ESP32 / шлюз'],[1,'Датчик 1'],[2,'Датчик 2'],[3,'Датчик 3'],[4,'Датчик 4'],[5,'Датчик 5'],[6,'Датчик 6'],[7,'Датчик 7'],[8,'Датчик 8'],[9,'Датчик 9'],[10,'Датчик 10'],[11,'Сумма мощности / энергии']];
   const source=customSelect(sourceOptions,element.sourceId??1);source.addEventListener('change',()=>{const previous=Number(element.sourceId);element.sourceId=Number(source.value);if(k===0&&element.sourceId===11)element.metric=2;else if(k===0&&previous===11&&element.metric>=64)element.metric=0;renderAll()});bottom.appendChild(customField('Источник',source));if(k===0&&Number(element.sourceId)===11){const sensorPicker=document.createElement('div');sensorPicker.style.flexBasis='100%';sensorPicker.style.display='grid';sensorPicker.style.gridTemplateColumns='repeat(3,minmax(0,1fr))';sensorPicker.style.gap='4px';const mask=element.metric>=64?element.metric-64:63;for(let sensorId=1;sensorId<=6;sensorId++){const option=document.createElement('label');option.style.display='flex';option.style.alignItems='center';option.style.gap='4px';const check=document.createElement('input');check.type='checkbox';check.checked=!!(mask&(1<<(sensorId-1)));check.addEventListener('change',()=>{let nextMask=element.metric>=64?element.metric-64:63;if(check.checked)nextMask|=1<<(sensorId-1);else nextMask&=~(1<<(sensorId-1));element.metric=64+nextMask;updateCustomPreviews()});option.append(check,document.createTextNode('Датчик '+sensorId));sensorPicker.appendChild(option)}bottom.appendChild(sensorPicker)}
  }
  let metricOptions=null,metricTitle='Параметр';
  if(k===0)metricOptions=[[0,'Напряжение'],[1,'Ток'],[2,'Мощность'],[3,'Заряд батареи'],[4,'Накопленная энергия']];
  else if(k===1){metricTitle='Отображать';metricOptions=[[0,'Время'],[1,'Дату'],[2,'Время и дату']]}
  else if(k===2){metricTitle='Информация';metricOptions=[[0,'Состояние роутера'],[1,'Имя сети'],[2,'Уровень сигнала'],[3,'IP роутера'],[4,'Состояние точки'],[5,'IP точки']]}
  else if(k===4){element.metric=2;metricOptions=null}
  if(metricOptions){const metric=customSelect(metricOptions,element.metric??0);metric.addEventListener('change',()=>{element.metric=Number(metric.value);updateCustomPreviews()});bottom.appendChild(customField(metricTitle,metric))}
  const size=customSelect(sizeOptions,element.fontSize);size.addEventListener('change',()=>{element.fontSize=Number(size.value);updateCustomPreviews()});bottom.appendChild(customField('Размер',size));
  const color=customSelect(colorOptions,element.color);color.addEventListener('change',()=>{element.color=Number(color.value);updateCustomPreviews()});bottom.appendChild(customField('Цвет',color));
  const actions=document.createElement('div');actions.className='row';
  const up=document.createElement('button');up.type='button';up.textContent='↑';up.disabled=index===0;up.title='Переместить выше';up.addEventListener('click',()=>{const a=w.elements;[a[index-1],a[index]]=[a[index],a[index-1]];renderAll()});
  const down=document.createElement('button');down.type='button';down.textContent='↓';down.disabled=index===w.elements.length-1;down.title='Переместить ниже';down.addEventListener('click',()=>{const a=w.elements;[a[index+1],a[index]]=[a[index],a[index+1]];renderAll()});
  const remove=document.createElement('button');remove.type='button';remove.className='danger';remove.textContent='Удалить';remove.addEventListener('click',()=>{w.elements.splice(index,1);renderAll()});
  actions.append(up,down,remove);bottom.appendChild(actions);row.appendChild(bottom);  if(k===1&&!rtcAttached) {const rtcPanel=$('clockSettingsPanel');rtcPanel.style.display='block';row.appendChild(rtcPanel);rtcAttached=true}
  if(k===2&&!wifiModeAttached) {const panel=$('wifiModePanel');panel.style.display='block';row.appendChild(panel);wifiModeAttached=true}
  if(k===2&&!wifiDefaultsAttached) {const panel=$('wifiDefaultsPanel');panel.style.display='block';row.appendChild(panel);wifiDefaultsAttached=true}

  if(k===4&&!calibrationAttached) {const panel=$('calibrationEditor');panel.style.display='block';row.appendChild(panel);calibrationAttached=true}
  if(k===5&&!wifiConnectionAttached) {const wifiPanel=$('wifiConnectionPanel');wifiPanel.style.display='block';row.appendChild(wifiPanel);wifiConnectionAttached=true}
  root.appendChild(row);
 });
 const kindsNow=new Set(w.elements.map(e=>Number(e.kind)||0));
 const rtcPanel=$('clockSettingsPanel');if(!rtcAttached)rtcPanel.style.display='none';
 const wifiPanel=$('wifiConnectionPanel');if(!wifiConnectionAttached)wifiPanel.style.display='none';
 if(!wifiModeAttached)$('wifiModePanel').style.display='none';
 if(!wifiDefaultsAttached)$('wifiDefaultsPanel').style.display='none';
 if(!calibrationAttached)$('calibrationEditor').style.display='none';

 tools.style.display='none';
 $('addCustomElement').disabled=w.elements.length>=MAX_WIDGET_ELEMENTS;
}

function renderTabs(){
 const tabs=$('pageTabs');tabs.innerHTML='';
 pages.forEach((p,i)=>{const b=document.createElement('button');b.className='page-tab'+(i===editorPage?' active':'')+(i===devicePage?' device':'');b.textContent=(i+1)+'. '+p.title;b.title=i===devicePage?'Сейчас на физическом дисплее':'Выбрать для редактирования';b.onclick=()=>{editorPage=i;followDevice=false;selectedWidget=-1;renderAll()};tabs.appendChild(b)});
 $('newPage').disabled=pages.length>=MAX_PAGES;$('deletePage').disabled=pages.length<=1;
}
function renderCanvas(){
 const c=$('preview');c.querySelectorAll('.widget').forEach(n=>n.remove());
 $('prevTitle').textContent=pages[editorPage].title||('Страница '+(editorPage+1));$('prevTitle').style.display=pages[editorPage].showTitle?'block':'none';$('prevTitle').style.fontSize='16px';const palette=['#FFFFFF','#00D5FF','#39E75F','#FFD400','#FF4D4D','#FF8C00'];const headingSizes=[9,11,13,17,20,23],contentSizes=[9,12,15,20,24,27];
 
 pages[editorPage].widgets.forEach((w,i)=>{
  if(!w.enabled)return;
  const node=document.createElement('div');node.className='widget'+(selectedWidget===i?' selected':'')+(w.locked?' locked':'');node.dataset.index=i;
  node.style.left=w.x+'px';node.style.top=w.y+'px';node.style.width=w.w+'px';node.style.height=w.h+'px';node.style.backgroundColor=backgroundPalette[w.backgroundColor??0];node.style.border=w.showBorder===false?'none':'1px solid #52616d';
  const name=document.createElement('div');name.className='name';name.style.display=w.showHeading?'block':'none';name.contentEditable='true';name.spellcheck=false;name.textContent=w.label;name.title='Щёлкните и измените подпись';
  name.style.fontSize=headingSizes[w.headingSize]+'px';name.style.letterSpacing=(w.headingLetterSpacing??0)+'px';name.style.textAlign=['left','center','right'][w.headingAlign];name.style.color=palette[w.headingColor];
  name.addEventListener('pointerdown',e=>e.stopPropagation());
  name.addEventListener('input',()=>{w.label=name.textContent.slice(0,54);renderTabs()});
  name.addEventListener('blur',()=>{w.label=name.textContent.trim().slice(0,54)||'Блок';name.textContent=w.label;renderTabs()});
  node.appendChild(name);
  const lockButton=document.createElement('button');lockButton.type='button';lockButton.className='lock-toggle';lockButton.textContent=w.locked?'🔒':'🔓';lockButton.title=w.locked?'Разблокировать перемещение и размер':'Зафиксировать положение и размер';lockButton.setAttribute('aria-label',lockButton.title);
  lockButton.addEventListener('pointerdown',e=>e.stopPropagation());lockButton.addEventListener('click',e=>{e.stopPropagation();w.locked=!w.locked;selectedWidget=i;renderAll()});node.appendChild(lockButton);
  const lines=document.createElement('div');lines.className='custom-lines';lines.style.gap=(w.rowSpacing??4)+'px';node.appendChild(lines);
  (w.elements||[]).forEach(e=>{const line=document.createElement('div');line.className='custom-line';line.textContent=customElementText(e);line.style.fontSize=customFontSizes[e.fontSize]+'px';line.style.letterSpacing=(w.contentLetterSpacing??0)+'px';line.style.color=customPalette[e.color];lines.appendChild(line)});

  const handle=document.createElement('span');handle.className='resize-handle';handle.title='Изменить размер';node.appendChild(handle);
  node.addEventListener('pointerdown',e=>{
   if(e.button!==0||e.target.isContentEditable||e.target.closest('.lock-toggle'))return;
   if(w.locked){selectedWidget=i;c.querySelectorAll('.widget').forEach(n=>n.classList.toggle('selected',n===node));syncStyleControls();e.preventDefault();return;}
   selectedWidget=i;c.querySelectorAll('.widget').forEach(n=>n.classList.toggle('selected',n===node));syncStyleControls();
   pointerEdit={index:i,resizing:e.target.classList.contains('resize-handle'),sx:e.clientX,sy:e.clientY,x:w.x,y:w.y,w:w.w,h:w.h};
   node.setPointerCapture(e.pointerId);e.preventDefault();
  });
  node.addEventListener('pointermove',e=>{
   if(!pointerEdit||pointerEdit.index!==i)return;
   const rect=c.getBoundingClientRect(),dx=Math.round((e.clientX-pointerEdit.sx)*240/rect.width),dy=Math.round((e.clientY-pointerEdit.sy)*320/rect.height);
   if(pointerEdit.resizing){w.w=Math.max(40,Math.min(240-pointerEdit.x,pointerEdit.w+dx));w.h=Math.max(w.elements.some(e=>Number(e.kind)===4)?90:36,Math.min(320-pointerEdit.y,pointerEdit.h+dy))}
   else{w.x=Math.max(0,Math.min(240-w.w,pointerEdit.x+dx));w.y=Math.max(0,Math.min(320-w.h,pointerEdit.y+dy))}
   node.style.left=w.x+'px';node.style.top=w.y+'px';node.style.width=w.w+'px';node.style.height=w.h+'px';
  });
  node.addEventListener('pointerup',()=>pointerEdit=null);node.addEventListener('pointercancel',()=>pointerEdit=null);
  node.addEventListener('click',e=>{if(e.target.closest('.name'))return;selectedWidget=i;renderAll()});c.appendChild(node);
 });
}
function syncStyleControls(){const p=pages[editorPage],w=p&&p.widgets[selectedWidget];$('showWidgetHeading').disabled=!w;$('showWidgetBorder').disabled=!w;$('backgroundColor').disabled=!w;$('rowSpacing').disabled=!w;$('headingLetterSpacing').disabled=!w;$('contentLetterSpacing').disabled=!w;if(!w)return;$('rowSpacing').value=String(w.rowSpacing??4);$('headingLetterSpacing').value=String(w.headingLetterSpacing??0);$('contentLetterSpacing').value=String(w.contentLetterSpacing??0);$('backgroundColor').value=String(w.backgroundColor??0);$('showWidgetBorder').checked=w.showBorder!==false;}
function renderAll(){
 renderTabs();$('pageTitle').value=pages[editorPage].title;$('showPageTitle').checked=pages[editorPage].showTitle!==false;
 const p=pages[editorPage],w=p.widgets[selectedWidget],isCustom=!!w;
 $('showWidgetHeading').disabled=!w;$('showWidgetHeading').checked=!!(w&&w.showHeading!==false);
 
 $('customEditor').style.display=isCustom?'block':'none';

 if(isCustom){renderCustomEditor(w);renderCalibrationRows(w.elements.filter(e=>Number(e.kind)===4))}
 syncStyleControls();renderCanvas();$('showOnTft').textContent='Показать страницу '+(editorPage+1)+' на TFT';
}
const uiTextDefaults={"stringPrefix":"Стринг","currentLabel":"Ток","voltageLabel":"Напр.","routerLabel":"Роутер","apLabel":"Точка","noRouterLabel":"Роутер не подключён","networkLabel":"Сеть","signalLabel":"Сигнал","apSetupLabel":"Точка настройки:","selectHint":"ВВЕРХ/ВНИЗ: выбор","changeHint":"OK: изменить (5 сек.)","applyLabel":"Применение через"};
let uiTexts={...uiTextDefaults};
function applyActiveCalibration(c){
 const valid=Array.isArray(c.activeCalibrationValid)?c.activeCalibrationValid:[];
 const activeCurrent=normalizeCalibration(c.activeCurrentCalibration),activeVoltage=normalizeCalibration(c.activeVoltageCalibration);
 for(let i=0;i<10;i++)if(valid[i]===true){currentCalibration[i]=activeCurrent[i];voltageCalibration[i]=activeVoltage[i];}
}
function loadConfig(c){
 stringTelemetry=Array.isArray(c.stringTelemetry)?c.stringTelemetry:[];totalEnergyWh=Number(c.totalEnergyWh)||0;gatewayTelemetry=c.gatewayTelemetry||gatewayTelemetry;
 currentCalibration=normalizeCalibration(c.currentCalibration);voltageCalibration=normalizeCalibration(c.voltageCalibration);applyActiveCalibration(c);
 $('netStatus').textContent=c.networkStatus||'Wi-Fi не подключён';
 if(c.clock){$('clockDate').value=c.clock.date||'';$('clockTime').value=c.clock.time||'';$('clockStatus').textContent=c.clock.valid?'RTC: '+c.clock.date+' '+c.clock.time:'RTC не отвечает или время недоступно';}
 networkStatus=c.networkStatus||'';networkLabel=c.networkLabel||'Wi-Fi: —';configuredSsid=c.ssid||'';wifiInfo=c.wifiInfo||wifiInfo;savedNetworks=c.savedNetworks||[];wifiMode=c.wifiMode||wifiMode;renderWiFiMode();$('defaultRouter').checked=!!wifiMode.defaultRouter;$('defaultAp').checked=!!wifiMode.defaultAp;renderWiFiDefaults();
 if(Array.isArray(c.pages)&&c.pages.length){pages=c.pages.map(normalizePage)}else{pages=[];ensurePages()}
 devicePage=Math.max(0,Math.min((Number(c.tftPage)||1)-1,pages.length-1));
 if(followDevice)editorPage=devicePage;
 if(editorPage>=pages.length)editorPage=pages.length-1;
 $('ssid').value=c.ssid||'';$('password').value='';renderSavedNetworks(savedNetworks);renderAll();updateDeviceStatus();
}
function updateDeviceStatus(){ $('deviceStatus').textContent='Физический дисплей: страница '+(devicePage+1)+' из '+pages.length+'. Редактор: страница '+(editorPage+1)+'.'; }
function collectData(){
 return {title:'SOLAR PV',currentCalibration,voltageCalibration,pageCount:pages.length,pages:pages.map(p=>({title:p.title,showTitle:p.showTitle!==false,widgets:p.widgets.map(w=>({label:w.label,headingSize:w.headingSize,contentSize:w.contentSize,rowSpacing:w.rowSpacing??4,headingLetterSpacing:w.headingLetterSpacing??0,contentLetterSpacing:w.contentLetterSpacing??0,headingAlign:w.headingAlign,contentAlign:w.contentAlign,headingColor:w.headingColor,contentColor:w.contentColor,backgroundColor:w.backgroundColor??0,elements:(w.elements||[]).map(e=>({label:e.label,kind:e.kind??0,sourceId:e.sourceId,metric:e.metric,fontSize:e.fontSize,color:e.color})),locked:w.locked===true,showHeading:w.showHeading!==false,showBorder:w.showBorder!==false,x:w.x,y:w.y,w:w.w,h:w.h,enabled:w.enabled}))}))};
}
async function refreshDeviceState(){
 try{const r=await fetch('/api/config?t='+Date.now(),{cache:'no-store'});if(!r.ok)return;const c=await r.json();
  stringTelemetry=Array.isArray(c.stringTelemetry)?c.stringTelemetry:[];gatewayTelemetry=c.gatewayTelemetry||gatewayTelemetry;currentCalibration=normalizeCalibration(c.currentCalibration);voltageCalibration=normalizeCalibration(c.voltageCalibration);applyActiveCalibration(c);
  const p=Math.max(0,Math.min((Number(c.tftPage)||1)-1,Math.max(0,pages.length-1)));
  if(p!==devicePage){devicePage=p;if(followDevice)editorPage=Math.min(devicePage,pages.length-1);renderAll()}
  if(c.clock){if(document.activeElement!==$('clockDate'))$('clockDate').value=c.clock.date||'';if(document.activeElement!==$('clockTime'))$('clockTime').value=c.clock.time||'';$('clockStatus').textContent=c.clock.valid?'RTC: '+c.clock.date+' '+c.clock.time:'RTC не отвечает или время недоступно';}
  networkStatus=c.networkStatus||networkStatus;networkLabel=c.networkLabel||networkLabel;configuredSsid=c.ssid||'';wifiInfo=c.wifiInfo||wifiInfo;wifiMode=c.wifiMode||wifiMode;renderWiFiMode();updateCustomPreviews();$('netStatus').textContent=networkStatus;updateDeviceStatus();
 }catch(e){}
}

$('backgroundColor').addEventListener('change',()=>{const w=pages[editorPage].widgets[selectedWidget];if(!w)return;w.backgroundColor=Number($('backgroundColor').value);renderAll()});
$('rowSpacing').addEventListener('input',()=>{const w=pages[editorPage].widgets[selectedWidget];if(!w)return;w.rowSpacing=Math.max(0,Math.min(20,Number($('rowSpacing').value)||0));renderCanvas()});
$('headingLetterSpacing').addEventListener('input',()=>{const w=pages[editorPage].widgets[selectedWidget];if(!w)return;w.headingLetterSpacing=Math.max(-2,Math.min(8,Number($('headingLetterSpacing').value)||0));renderCanvas()});
$('contentLetterSpacing').addEventListener('input',()=>{const w=pages[editorPage].widgets[selectedWidget];if(!w)return;w.contentLetterSpacing=Math.max(-2,Math.min(8,Number($('contentLetterSpacing').value)||0));renderCanvas()});
$('pageTitle').addEventListener('input',()=>{pages[editorPage].title=$('pageTitle').value.slice(0,54);renderTabs();renderCanvas()});
$('showPageTitle').addEventListener('change',()=>{pages[editorPage].showTitle=$('showPageTitle').checked;renderCanvas()});
$('showWidgetHeading').addEventListener('change',()=>{const w=pages[editorPage].widgets[selectedWidget];if(!w)return;w.showHeading=$('showWidgetHeading').checked;renderCanvas()});
$('showWidgetBorder').addEventListener('change',()=>{const w=pages[editorPage].widgets[selectedWidget];if(!w)return;w.showBorder=$('showWidgetBorder').checked;renderCanvas()});
$('newPage').addEventListener('click',async()=>{
 if(pages.length>=MAX_PAGES)return;
 try{const r=await fetch('/api/page?action=create&t='+Date.now(),{cache:'no-store'});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.message||'Не удалось создать страницу');
  const c=await (await fetch('/api/config?t='+Date.now(),{cache:'no-store'})).json();followDevice=false;loadConfig(c);editorPage=Math.min(Number(j.page)-1,pages.length-1);selectedWidget=-1;renderAll();
 }catch(e){alert(e.message||'Ошибка создания страницы')}
});
$('deletePage').addEventListener('click',async()=>{ if(pages.length<=1)return;if(!confirm('Удалить страницу '+(editorPage+1)+' и все её блоки?'))return;
 const removed=editorPage;pages.splice(removed,1);if(editorPage>=pages.length)editorPage=pages.length-1;selectedWidget=-1;followDevice=false;
 try{await saveAll(false)}catch(e){alert('Не удалось сохранить изменения: '+e.message)}renderAll();
});
$('followDevice').addEventListener('click',()=>{followDevice=true;editorPage=Math.min(devicePage,pages.length-1);selectedWidget=-1;renderAll()});
$('addWidget').addEventListener('click',()=>{
 const p=pages[editorPage];if(p.widgets.length>=MAX_WIDGETS){alert('На одной странице максимум '+MAX_WIDGETS+' блоков.');return}
 const n=p.widgets.length;
 p.widgets.push({label:'Блок '+(n+1),elements:[],headingSize:1,contentSize:2,rowSpacing:4,headingLetterSpacing:0,contentLetterSpacing:0,locked:false,showHeading:true,headingAlign:0,contentAlign:0,headingColor:0,contentColor:3,backgroundColor:0,showBorder:true,x:8,y:55,w:224,h:110,enabled:true});
 selectedWidget=p.widgets.length-1;renderAll();
});
$('deleteWidget').addEventListener('click',()=>{const p=pages[editorPage];if(selectedWidget<0||selectedWidget>=p.widgets.length){alert('Сначала выбери блок на холсте.');return}p.widgets.splice(selectedWidget,1);selectedWidget=-1;renderAll()});


$('addCustomElement').addEventListener('click',()=>{const w=pages[editorPage]?.widgets[selectedWidget];if(!w||w.elements.length>=MAX_WIDGET_ELEMENTS)return;const nextSource=(w.elements.length%10)+1;w.elements.push({label:'Стринг '+nextSource,kind:0,sourceId:nextSource,metric:0,fontSize:2,color:3});renderAll()});
$('applyCalibration').addEventListener('click',async()=>{const w=pages[editorPage]?.widgets[selectedWidget];if(!w||!w.elements.some(e=>Number(e.kind)===4))return;for(const input of $('calibrationRows').querySelectorAll('input[data-cal="currentTarget"],input[data-cal="voltageTarget"]')){if(input.value.trim()==='')continue;const sensorId=Number(input.dataset.sensorId),index=sensorId-1,isCurrent=input.dataset.cal==='currentTarget',decimals=isCurrent?2:1,entered=Number(input.value),target=Number(entered.toFixed(decimals)),measured=stringTelemetry.find(v=>Number(v.id)===sensorId),actual=measured&&measured.valid?Number(isCurrent?measured.current:measured.voltage):NaN;if(!Number.isFinite(entered)||!Number.isFinite(target)||target<=0||target>9999){alert('Введите корректное требуемое '+(isCurrent?'значение тока':'напряжение')+' больше нуля.');input.focus();return}input.value=target.toFixed(decimals);if(!Number.isFinite(actual)||actual<=0.001){alert('Нет актуального измерения '+(isCurrent?'тока':'напряжения')+' для стринга '+(index+1)+'.');input.focus();return}const factors=isCurrent?currentCalibration:voltageCalibration;factors[index]=Math.max(0.001,Math.min(99.999,Math.round(factors[index]*target/actual*1000)/1000))}try{await saveAll(true)}catch(e){alert('Не удалось сохранить коэффициенты: '+e.message)}});
async function saveAll(showMessage){
 const r=await fetch('/api/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(collectData())});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.message||('HTTP '+r.status));
 if(showMessage!==false)alert(j.message);const c=await (await fetch('/api/config?t='+Date.now(),{cache:'no-store'})).json();
 const oldEditor=editorPage,wasFollowing=followDevice;loadConfig(c);followDevice=wasFollowing;editorPage=followDevice?devicePage:Math.min(oldEditor,pages.length-1);renderAll();return true;
}
$('saveDisplay').addEventListener('click',async()=>{try{await saveAll(true)}catch(e){alert('Не удалось сохранить: '+e.message)}});
$('showOnTft').addEventListener('click',async()=>{
 try{await saveAll(false);const r=await fetch('/api/page?page='+(editorPage+1)+'&t='+Date.now(),{cache:'no-store'});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.message||'Не удалось переключить дисплей');devicePage=editorPage;followDevice=true;renderAll();updateDeviceStatus()}catch(e){alert('Ошибка переключения: '+e.message)}
});
$('resetLayout').addEventListener('click',()=>{if(!confirm('Восстановить исходный макет первой страницы?'))return;pages[editorPage].widgets=editorPage===0?clone(defaults):[];selectedWidget=-1;renderAll()});
function renderSavedNetworks(networks){const list=$('networkList');list.innerHTML='';list.add(new Option('— Выберите сеть —',''));networks.forEach(n=>list.add(new Option('★ '+n.ssid+' · сохранена',n.ssid)))}
function renderWiFiDefaults(){ $('wifiDefaultsStatus').textContent='После перезагрузки: роутер '+($('defaultRouter').checked?'ВКЛ':'ВЫКЛ')+' · точка '+($('defaultAp').checked?'ВКЛ':'ВЫКЛ');}
async function saveWiFiDefaults(){try{const router=$('defaultRouter').checked,ap=$('defaultAp').checked;const r=await fetch('/api/wifi-defaults',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({router,ap})});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.message||'Ошибка сохранения');$('wifiDefaultsStatus').textContent=j.message;await refreshDeviceState();}catch(e){alert('Не удалось сохранить настройки запуска: '+e.message)}}
$('defaultRouter').addEventListener('change',renderWiFiDefaults);$('defaultAp').addEventListener('change',renderWiFiDefaults);$('saveWiFiDefaults').addEventListener('click',saveWiFiDefaults);
function renderWiFiMode(){ $('toggleRouter').textContent=uiTexts.routerLabel+': '+(wifiMode.router?'ВКЛ':'ВЫКЛ');$('toggleAp').textContent=uiTexts.apLabel+': '+(wifiMode.ap?'ВКЛ':'ВЫКЛ');$('wifiModeStatus').textContent=uiTexts.routerLabel+': '+(wifiMode.router?'ВКЛ':'ВЫКЛ')+' · Точка доступа: '+(wifiMode.ap?'ВКЛ':'ВЫКЛ');}
async function setWiFiMode(router,ap){if(!router&&!ap&&!confirm('Выключить обе сети? Админка станет недоступна, пока Wi-Fi не будет включён другим способом.'))return;try{const r=await fetch('/api/wifi-mode',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({router,ap})});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.message||'Ошибка режима Wi-Fi');wifiMode={router,ap,apActive:ap};renderWiFiMode();$('wifiModeStatus').textContent=j.message;await refreshDeviceState();}catch(e){alert('Не удалось применить режим: '+e.message)}}
$('toggleRouter').addEventListener('click',()=>setWiFiMode(!wifiMode.router,wifiMode.ap));$('toggleAp').addEventListener('click',()=>setWiFiMode(wifiMode.router,!wifiMode.ap));$('wifiOff').addEventListener('click',()=>setWiFiMode(false,false));
$('networkList').addEventListener('change',()=>{if($('networkList').value)$('ssid').value=$('networkList').value});
$('scanNetworks').addEventListener('click',async()=>{
 $('scanNetworks').disabled=true;$('scanStatus').textContent='Идёт поиск сетей…';
 try{
  let r=await fetch('/api/scan?t='+Date.now(),{cache:'no-store'});let j=await r.json();
  if(!r.ok||!j.ok)throw new Error(j.message||'Ошибка запуска сканирования');
  const started=Date.now();
  while(j.status==='scanning'||j.status==='idle'){
   if(Date.now()-started>25000)throw new Error('Сканирование занимает слишком много времени');
   await new Promise(resolve=>setTimeout(resolve,350));
   r=await fetch('/api/scan-status?t='+Date.now(),{cache:'no-store'});j=await r.json();
   if(!r.ok||!j.ok)throw new Error(j.message||'Ошибка сканирования');
  }
  if(j.status!=='complete')throw new Error(j.message||'Не удалось выполнить сканирование Wi-Fi');
  const list=$('networkList');list.innerHTML='';list.add(new Option('— Выберите сеть —',''));savedNetworks.forEach(n=>list.add(new Option('★ '+n.ssid+' · сохранена',n.ssid)));
  (j.networks||[]).forEach(n=>{if(Array.from(list.options).some(o=>o.value===n.ssid))return;list.add(new Option(n.ssid+' · '+n.rssi+' dBm'+(n.secure?' · защищена':' · открыта'),n.ssid))});
  $('scanStatus').textContent='Найдено сетей: '+(j.networks||[]).length+'.';
 }catch(e){$('scanStatus').textContent='Ошибка сканирования: '+e.message}finally{$('scanNetworks').disabled=false}
});
$('saveWifi').addEventListener('click',async()=>{
 const body=new URLSearchParams({ssid:$('ssid').value,password:$('password').value});
 try{const r=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const j=await r.json();alert(j.message);if(j.ok)$('password').value=''}catch(e){alert('Устройство может перезапускаться. Подожди и открой страницу снова.')}
});
fetch('/api/config?t='+Date.now(),{cache:'no-store'}).then(r=>r.json()).then(loadConfig).catch(()=>$('netStatus').textContent='Нет связи с ESP32');
$('setClock').addEventListener('click',async()=>{const date=$('clockDate').value,time=$('clockTime').value;if(!date||!time){alert('Укажи дату и время.');return}try{const r=await fetch('/api/clock',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({date,time})});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.message||'Не удалось передать команду');$('clockStatus').textContent=j.message||'Команда передана шлюзу';}catch(e){$('clockStatus').textContent='Ошибка: '+e.message}});
setInterval(refreshDeviceState,2000);
setInterval(refreshEnergyHistory,10000);
$('energyHistoryPeriod').addEventListener('change',renderEnergyHistory);
refreshEnergyHistory();
</script></body></html>
)rawliteral";