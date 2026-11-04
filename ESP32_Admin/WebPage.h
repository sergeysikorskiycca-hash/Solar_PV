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
.widget{position:absolute;background:#101820;border:1px solid #52616d;border-radius:5px;padding:5px;overflow:hidden;cursor:grab;touch-action:none;user-select:none}.widget.selected{outline:2px solid #49b78b;outline-offset:1px}.widget.locked{cursor:default}.widget .name{padding-right:27px;font-size:11px;color:#fff;white-space:nowrap;overflow:hidden;outline:none}.widget .name:focus{user-select:text;cursor:text;background:#17212b;border-radius:2px}.widget .value{font-size:15px;color:#ffd400;font-weight:bold;white-space:normal;overflow-wrap:anywhere}.widget .wifi-lines{font-size:12px;line-height:1.2;color:#fff;white-space:normal;overflow-wrap:anywhere}.widget .free-text{font-size:15px;line-height:1.15;color:#fff;white-space:pre-wrap;overflow-wrap:anywhere;user-select:none}
.resize-handle{position:absolute;right:0;bottom:0;width:13px;height:13px;background:#49b78b;border:1px solid #eaf0f6;border-radius:3px;cursor:nwse-resize;touch-action:none}.widget.locked .resize-handle{display:none}.lock-toggle{position:absolute;z-index:3;right:2px;top:2px;width:23px;height:22px;padding:0;border:1px solid #65798c;border-radius:4px;background:#243447;font-size:13px;line-height:20px}.widget.locked .lock-toggle{background:#49b78b;color:#081b14}
.screen-footer{position:absolute;bottom:5px;left:8px;right:8px;border-top:1px solid #52616d;padding-top:4px;font-size:9px;color:#aebdca;display:flex;justify-content:space-between}
.preview-wrap{position:sticky;top:12px;align-self:start}.toolbar{display:grid;grid-template-columns:1fr 1fr;gap:8px}.toolbar .wide{grid-column:1/-1}.hint{margin-top:8px}.wifi-scan{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:end}
@media(max-width:780px){main{grid-template-columns:1fr}.preview-wrap{position:static;grid-row:1}.screen{margin:auto}}
</style></head><body>
<header><h1>Solar_PV — редактор экранов</h1><p>Универсальные страницы и повторно используемые блоки · TFT ST7789 240 × 320</p></header>
<main><div>
<section><h2>Страницы</h2><div class="page-tabs" id="pageTabs"></div>
<label for="pageTitle">Название выбранной страницы</label><input id="pageTitle" type="text" maxlength="24" value="Показатели"><label style="display:flex;align-items:center;gap:8px"><input id="showPageTitle" type="checkbox" checked> Показывать название страницы</label>
<div class="row"><button class="primary" id="newPage">＋ Новая страница</button><button id="followDevice">К странице дисплея</button><button class="danger" id="deletePage">Удалить страницу</button></div>
<p class="hint">Жёлтая рамка у вкладки показывает страницу, которая сейчас выведена на физический дисплей. Выбирать другую страницу для редактирования можно независимо.</p>
<div class="status" id="deviceStatus">Получение состояния ESP32…</div></section>
<section><h2>Подписи системных блоков</h2>
<p class="hint">Эти подписи выводятся внутри встроенных блоков TFT. Заголовки самих блоков по-прежнему меняются в редакторе страниц.</p>
<div class="row"><div><label for="uiStringPrefix">Название стринга</label><input id="uiStringPrefix" type="text" maxlength="24"></div><div><label for="uiCurrentLabel">Ток</label><input id="uiCurrentLabel" type="text" maxlength="24"></div></div>
<div class="row"><div><label for="uiVoltageLabel">Напряжение</label><input id="uiVoltageLabel" type="text" maxlength="24"></div><div><label for="uiRouterLabel">Роутер</label><input id="uiRouterLabel" type="text" maxlength="24"></div></div>
<div class="row"><div><label for="uiApLabel">Точка доступа</label><input id="uiApLabel" type="text" maxlength="24"></div><div><label for="uiNoRouterLabel">Нет подключения</label><input id="uiNoRouterLabel" type="text" maxlength="40"></div></div>
<div class="row"><div><label for="uiNetworkLabel">Сеть</label><input id="uiNetworkLabel" type="text" maxlength="24"></div><div><label for="uiSignalLabel">Сигнал</label><input id="uiSignalLabel" type="text" maxlength="24"></div></div>
<label for="uiApSetupLabel">Точка настройки</label><input id="uiApSetupLabel" type="text" maxlength="32">
<label for="uiSelectHint">Подсказка выбора</label><input id="uiSelectHint" type="text" maxlength="40">
<label for="uiChangeHint">Подсказка подтверждения</label><input id="uiChangeHint" type="text" maxlength="40">
<label for="uiApplyLabel">Надпись ожидания применения</label><input id="uiApplyLabel" type="text" maxlength="32">
<div class="row" style="margin-top:10px"><button class="primary" id="saveUiTexts">Сохранить подписи</button><button id="resetUiTexts">Вернуть исходные</button></div>
<div class="status" id="uiTextsStatus" style="margin-top:8px">Подписи загружаются…</div></section>
<section><h2>Библиотека блоков</h2><label for="widgetType">Тип нового блока</label><select id="widgetType">
<option value="power">Мощность PV</option><option value="voltage">Напряжение</option><option value="current">Ток</option><option value="battery">Аккумулятор</option><option value="wifiStatus">Состояние Wi-Fi</option><option value="wifiControl">Управление Wi-Fi</option><option value="calibration">Калибровка тока и напряжения</option><option value="stringsVoltage">Мониторинг напряжения стрингов</option><option value="stringsCurrent">Мониторинг тока стрингов</option><option value="stringsPower">Мониторинг мощности стрингов</option><option value="clock">Часы и дата</option><option value="text">Текстовый блок</option></select>
<div class="row" style="margin-top:8px"><button class="primary" id="addWidget">＋ Добавить на страницу</button><button id="deleteWidget">Удалить выбранный блок</button></div>
<p class="hint">Щёлкни по подписи блока, чтобы изменить её. Перетаскивай блок мышью, размер меняй за зелёный угол.</p><label style="display:flex;align-items:center;gap:8px"><input id="showWidgetHeading" type="checkbox" checked> Показывать заголовок блока</label><label style="display:flex;align-items:center;gap:8px"><input id="showWidgetBorder" type="checkbox" checked> Показывать рамку блока</label><label for="widgetText">Текст выбранного блока</label><textarea id="widgetText" rows="5" maxlength="240" placeholder="Введите комментарий, буквы, цифры и символы…" style="width:100%;padding:9px;border:1px solid #465c73;border-radius:7px;background:#101923;color:#fff;font:14px Arial,sans-serif;resize:vertical"></textarea><p class="hint">Поле активно для текстового блока. Поддерживаются переносы строк; максимум 240 символов.</p><div id="stringCountWrap" style="display:none"><label for="stringCount">Количество стрингов в блоке</label><select id="stringCount"><option value="1">1 стринг</option><option value="2">2 стринга</option><option value="3">3 стринга</option><option value="4">4 стринга</option><option value="5">5 стрингов</option><option value="6">6 стрингов</option></select><p class="hint">Значения поступают от датчиков ESP-NOW через ESP8266-шлюз. Стринг N соответствует sensorId N; если данные старше 3,5 секунды, показывается «—».</p></div>
<div id="calibrationEditor" style="display:none"><h3>Коэффициенты калибровки</h3><div id="calibrationRows"></div><button class="primary" id="applyCalibration" style="width:100%;margin-top:8px">Сохранить коэффициенты</button></div><h3>Оформление выбранного блока</h3>
<label for="styleTarget">Настраивать</label><select id="styleTarget"><option value="heading">Заголовок блока</option><option value="content">Содержимое блока</option></select>
<label for="styleSize">Размер текста</label><select id="styleSize"><option value="0">Компактный</option><option value="1">Небольшой</option><option value="2">Средний</option><option value="3">Крупный</option><option value="4">Очень крупный</option><option value="5">Максимальный</option></select>
<label for="styleAlign">Выравнивание</label><select id="styleAlign"><option value="0">Слева</option><option value="1">По центру</option><option value="2">Справа</option></select>
<label for="styleColor">Цвет текста</label><select id="styleColor"><option value="0">Белый</option><option value="1">Голубой</option><option value="2">Зелёный</option><option value="3">Жёлтый</option><option value="4">Красный</option><option value="5">Оранжевый</option></select>
<label for="backgroundColor">Фон блока</label><select id="backgroundColor"><option value="0">Почти чёрный</option><option value="1">Тёмно-синий</option><option value="2">Тёмно-зелёный</option><option value="3">Тёмно-фиолетовый</option><option value="4">Тёмно-красный</option><option value="5">Графитовый</option><option value="6">Чёрный</option></select>
<p class="hint">Настройки относятся только к выбранному блоку и выбранной части — заголовку или содержимому. Палитра ограничена шестью контрастными цветами.</p>
<div class="toolbar" style="margin-top:12px"><button class="primary wide" id="saveDisplay">Сохранить все страницы и применить</button><button id="showOnTft">Показать выбранную страницу на TFT</button><button id="resetLayout">Восстановить исходный макет</button></div>
<p class="hint">В обычных числовых блоках предпросмотра остаются демонстрационные значения; блоки мониторинга стрингов показывают последние данные датчиков.</p></section>
<section><h2>Часы и дата RTC</h2><p class="hint">Установи время и дату часов DS3231. Команда передаётся на ESP8266-шлюз и записывается в RTC.</p><div class="row"><div><label for="clockDate">Дата</label><input id="clockDate" type="date"></div><div><label for="clockTime">Время</label><input id="clockTime" type="time" step="1"></div></div><button class="primary" id="setClock" style="margin-top:8px;width:100%">Установить время и дату</button><div class="status" id="clockStatus" style="margin-top:8px">Чтение времени устройства…</div></section><section><h2>Временное управление Wi-Fi</h2><div class="status" id="wifiModeStatus">Загрузка режима…</div><div class="row" style="margin-top:8px"><button id="toggleRouter">Роутер: ВЫКЛ</button><button id="toggleAp">Точка: ВКЛ</button></div><button class="danger" id="wifiOff" style="width:100%;margin-top:8px">Выключить обе сети</button><p class="hint">После перезапуска включится только точка SolarPV-Setup. Если выключить обе сети, админка станет недоступна.</p></section><section><h2>Постоянные настройки запуска Wi-Fi</h2><p class="hint">Эти значения сохраняются во флеш-памяти и применяются после перезагрузки. Переключатели выше действуют только до перезапуска.</p><label><input id="defaultRouter" type="checkbox"> Подключаться к домашнему роутеру</label><label><input id="defaultAp" type="checkbox"> Включать точку SolarPV-Setup</label><button class="primary" id="saveWiFiDefaults" style="width:100%;margin-top:8px">Сохранить настройки запуска</button><div class="status" id="wifiDefaultsStatus" style="margin-top:8px">Настройки запуска…</div></section><section><h2>Подключение к Wi-Fi</h2><div class="status" id="netStatus">Получение состояния устройства…</div>
<label for="networkList">Найденные и сохранённые сети</label><div class="wifi-scan"><select id="networkList"><option value="">Нажмите «Сканировать»</option></select><button id="scanNetworks">Сканировать</button></div><p class="hint" id="scanStatus">Список ещё не обновлялся.</p>
<label for="ssid">SSID</label><input id="ssid" type="text" maxlength="32" autocomplete="off"><label for="password">Пароль Wi-Fi</label><input id="password" type="password" maxlength="63" autocomplete="new-password">
<p class="hint">Для сохранённой сети оставь пароль пустым. Можно хранить до 8 сетей.</p><button class="primary" id="saveWifi" style="margin-top:8px;width:100%">Сохранить Wi-Fi и перезапустить</button></section>
</div>
<div class="preview-wrap"><section><h2>Предпросмотр холста</h2><div class="screen" id="preview">
<div class="screen-title" id="prevTitle">Показатели</div>
<div class="screen-footer"><span id="prevNetwork">Wi-Fi: —</span><span id="prevPage">Стр. 1</span></div></div>
<p class="hint">Предпросмотр повторяет координаты TFT. Выбранная для редактирования страница не обязана совпадать с физическим экраном.</p></section></div></main>
<script>
const MAX_PAGES=8,MAX_WIDGETS=12;
const defaults=[
 {type:'power',label:'Мощность PV',x:8,y:55,w:224,h:58,enabled:true},
 {type:'voltage',label:'Напряжение',x:8,y:125,w:106,h:58,enabled:true},
 {type:'current',label:'Ток',x:125,y:125,w:106,h:58,enabled:true},
 {type:'battery',label:'Аккумулятор',x:8,y:205,w:224,h:58,enabled:true}
];
const backgroundPalette=['#101820','#001B2E','#073326','#302044','#481D20','#30343B','#000000'];
const typeLabels={power:'Мощность PV',voltage:'Напряжение',current:'Ток',battery:'Аккумулятор',wifiStatus:'Состояние Wi-Fi',wifiControl:'Управление Wi-Fi',calibration:'Калибровка тока и напряжения',strings:'Мониторинг напряжения стрингов',stringsVoltage:'Мониторинг напряжения стрингов',stringsCurrent:'Мониторинг тока стрингов',stringsPower:'Мониторинг мощности стрингов',clock:'Часы и дата',text:'Текст'};
const demoValues={power:'DEMO W',voltage:'DEMO V',current:'DEMO A',battery:'DEMO %'};
let pages=[],editorPage=0,devicePage=0,selectedWidget=-1,followDevice=true,pointerEdit=null;
let networkStatus='',networkLabel='Wi-Fi: —',savedNetworks=[],wifiMode={router:false,ap:true,apActive:true};
let stringTelemetry=[];
let currentCalibration=[1,1,1,1,1,1],voltageCalibration=[1,1,1,1,1,1];
const $=id=>document.getElementById(id);
function clone(x){return JSON.parse(JSON.stringify(x))}
function normalizeCalibration(values){return Array.from({length:6},(_,i)=>{const v=Number(Array.isArray(values)?values[i]:1);return Number.isFinite(v)&&v>=0.001&&v<=99.999?v:1})}
function renderCalibrationRows(count){const root=$('calibrationRows');root.innerHTML='';for(let i=0;i<count;i++){const row=document.createElement('div');row.className='row';row.style.margin='6px 0';const name=document.createElement('strong');name.textContent=uiTexts.stringPrefix+' '+(i+1);name.style.minWidth='75px';row.appendChild(name);const measured=stringTelemetry.find(v=>Number(v.id)===i+1);const currentLabel=document.createElement('label');currentLabel.style.flex='1';currentLabel.textContent=uiTexts.currentLabel+' сейчас: '+(measured&&measured.valid?Number(measured.current).toFixed(2)+' А':'нет данных');const currentTarget=document.createElement('input');currentTarget.type='number';currentTarget.min='0.01';currentTarget.max='9999';currentTarget.step='0.01';currentTarget.placeholder='Должно быть, А';currentTarget.dataset.cal='currentTarget';currentTarget.dataset.index=String(i);currentTarget.addEventListener('change',()=>{if(currentTarget.value!=='')currentTarget.value=Number(Number(currentTarget.value).toFixed(2)).toFixed(2)});currentLabel.appendChild(currentTarget);row.appendChild(currentLabel);const voltageLabel=document.createElement('label');voltageLabel.style.flex='1';voltageLabel.textContent='Напряжение сейчас: '+(measured&&measured.valid?Number(measured.voltage).toFixed(1)+' В':'нет данных');const voltageTarget=document.createElement('input');voltageTarget.type='number';voltageTarget.min='0.1';voltageTarget.max='9999';voltageTarget.step='0.1';voltageTarget.placeholder='Должно быть, В';voltageTarget.dataset.cal='voltageTarget';voltageTarget.dataset.index=String(i);voltageTarget.addEventListener('change',()=>{if(voltageTarget.value!=='')voltageTarget.value=Number(Number(voltageTarget.value).toFixed(1)).toFixed(1)});voltageLabel.appendChild(voltageTarget);row.appendChild(voltageLabel);root.appendChild(row)}}
function normalizePage(p,i){return {title:p.title||('Страница '+(i+1)),showTitle:p.showTitle!==false,widgets:(p.widgets||[]).slice(0,MAX_WIDGETS).map(w=>({type:typeLabels[w.type]?w.type:'power',label:w.label||typeLabels[w.type]||'Блок',text:String(w.text||''),headingSize:Math.max(0,Math.min(5,Number(w.headingSize??1))),contentSize:Math.max(0,Math.min(5,Number(w.contentSize??2))),headingAlign:Math.max(0,Math.min(2,Number(w.headingAlign??0))),contentAlign:Math.max(0,Math.min(2,Number(w.contentAlign??0))),headingColor:Math.max(0,Math.min(5,Number(w.headingColor??0))),contentColor:Math.max(0,Math.min(5,Number(w.contentColor??(w.type==='wifiStatus'?0:3)))),backgroundColor:Math.max(0,Math.min(6,Number(w.backgroundColor??0))),stringCount:Math.max(w.type==="calibration"?2:1,Math.min(6,Number(w.stringCount??2))),locked:w.locked===true,showHeading:w.showHeading!==false,showBorder:w.showBorder!==false,x:Number(w.x)||0,y:Number(w.y)||40,w:Math.max(40,Number(w.w)||106),h:Math.max(w.type==="calibration"?90:36,Number(w.h)||(w.type==="calibration"?250:58)),enabled:w.enabled!==false}))}}
function ensurePages(){if(!pages.length)pages=[{title:'Показатели',showTitle:true,widgets:clone(defaults)},{title:'Wi-Fi',showTitle:true,widgets:[{type:'wifiStatus',label:'Состояние Wi-Fi',x:8,y:55,w:224,h:220,enabled:true}]}];pages=pages.slice(0,MAX_PAGES).map(normalizePage)}
function widgetValue(w){if(w.type==='clock')return '<div class="value">12:34:56<br>22.10.2026</div>';if(w.type==='wifiStatus')return '<div class="wifi-lines"></div>';if(w.type==='strings'||w.type==='stringsVoltage'||w.type==='stringsCurrent'||w.type==='stringsPower')return '<div class="strings-lines"></div>';return '<div class="value">'+(demoValues[w.type]||'DEMO')+'</div>'}
function stringLinesText(w){const unit=w.type==='stringsCurrent'?'А':w.type==='stringsPower'?'Вт':'В';return Array.from({length:w.stringCount||2},(_,n)=>{const s=stringTelemetry.find(v=>Number(v.id)===n+1);if(!s||!s.valid)return uiTexts.stringPrefix+' '+(n+1)+': — '+unit;const value=w.type==='stringsCurrent'?Number(s.current).toFixed(2):w.type==='stringsPower'?Number(s.power).toFixed(1):Number(s.voltage).toFixed(1);return uiTexts.stringPrefix+' '+(n+1)+': '+value+' '+unit}).join('\n')}
function updateStringPreviews(){document.querySelectorAll('.strings-lines').forEach(el=>{const i=Number(el.closest('.widget')?.dataset.index);const w=pages[editorPage]?.widgets[i];if(w&&['strings','stringsVoltage','stringsCurrent','stringsPower'].includes(w.type))el.textContent=stringLinesText(w)})}
function renderTabs(){
 const tabs=$('pageTabs');tabs.innerHTML='';
 pages.forEach((p,i)=>{const b=document.createElement('button');b.className='page-tab'+(i===editorPage?' active':'')+(i===devicePage?' device':'');b.textContent=(i+1)+'. '+p.title;b.title=i===devicePage?'Сейчас на физическом дисплее':'Выбрать для редактирования';b.onclick=()=>{editorPage=i;followDevice=false;selectedWidget=-1;renderAll()};tabs.appendChild(b)});
 $('newPage').disabled=pages.length>=MAX_PAGES;$('deletePage').disabled=pages.length<=1;
}
function renderCanvas(){
 const c=$('preview');c.querySelectorAll('.widget').forEach(n=>n.remove());
 $('prevTitle').textContent=pages[editorPage].title||('Страница '+(editorPage+1));$('prevTitle').style.display=pages[editorPage].showTitle?'block':'none';$('prevTitle').style.fontSize='16px';const palette=['#FFFFFF','#00D5FF','#39E75F','#FFD400','#FF4D4D','#FF8C00'];const headingSizes=[9,11,13,15,17,19],contentSizes=[9,12,15,18,20,22];
 $('prevNetwork').textContent=networkLabel;$('prevPage').textContent='Стр. '+(editorPage+1);
 pages[editorPage].widgets.forEach((w,i)=>{
  if(!w.enabled)return;
  const node=document.createElement('div');node.className='widget'+(selectedWidget===i?' selected':'')+(w.locked?' locked':'');node.dataset.index=i;
  node.style.left=w.x+'px';node.style.top=w.y+'px';node.style.width=w.w+'px';node.style.height=w.h+'px';node.style.backgroundColor=backgroundPalette[w.backgroundColor??0];node.style.border=w.showBorder===false?'none':'1px solid #52616d';
  const name=document.createElement('div');name.className='name';name.style.display=w.showHeading?'block':'none';name.contentEditable='true';name.spellcheck=false;name.textContent=w.label;name.title='Щёлкните и измените подпись';
  name.style.fontSize=headingSizes[w.headingSize]+'px';name.style.textAlign=['left','center','right'][w.headingAlign];name.style.color=palette[w.headingColor];
  name.addEventListener('pointerdown',e=>e.stopPropagation());
  name.addEventListener('input',()=>{w.label=name.textContent.slice(0,54);renderTabs()});
  name.addEventListener('blur',()=>{w.label=name.textContent.trim().slice(0,54)||typeLabels[w.type];name.textContent=w.label;renderTabs()});
  node.appendChild(name);
  const lockButton=document.createElement('button');lockButton.type='button';lockButton.className='lock-toggle';lockButton.textContent=w.locked?'🔒':'🔓';lockButton.title=w.locked?'Разблокировать перемещение и размер':'Зафиксировать положение и размер';lockButton.setAttribute('aria-label',lockButton.title);
  lockButton.addEventListener('pointerdown',e=>e.stopPropagation());lockButton.addEventListener('click',e=>{e.stopPropagation();w.locked=!w.locked;selectedWidget=i;renderAll()});node.appendChild(lockButton);
  if(w.type==='wifiControl'){
   const lines=document.createElement('div');lines.className='wifi-lines';lines.textContent=uiTexts.routerLabel+': '+(wifiMode.router?'ВКЛ':'ВЫКЛ')+'\\n'+uiTexts.apLabel+': '+(wifiMode.ap?'ВКЛ':'ВЫКЛ')+'\\nПульт: SETUP, затем 1/2';lines.style.whiteSpace='pre-line';lines.style.fontSize=contentSizes[w.contentSize]+'px';lines.style.color=palette[w.contentColor];node.appendChild(lines);
  }else if(w.type==='wifiStatus'){
   const lines=document.createElement('div');lines.className='wifi-lines';lines.textContent=networkStatus||'Состояние сети появится здесь';node.appendChild(lines);
   lines.style.fontSize=contentSizes[w.contentSize]+'px';lines.style.textAlign=['left','center','right'][w.contentAlign];lines.style.color=palette[w.contentColor];
  }else if(w.type==='calibration'){
   const lines=document.createElement('div');lines.className='wifi-lines';const count=Math.max(1,Math.min(6,Number(w.stringCount)||2));lines.textContent=Array.from({length:count},(_,i)=>uiTexts.stringPrefix+' '+(i+1)+String.fromCharCode(10)+uiTexts.currentLabel+' = '+currentCalibration[i].toFixed(3)+String.fromCharCode(10)+uiTexts.voltageLabel+' = '+voltageCalibration[i].toFixed(3)).join(String.fromCharCode(10));lines.style.whiteSpace='pre-line';lines.style.fontSize='10px';lines.style.lineHeight='1.05';lines.style.color=palette[w.contentColor];node.appendChild(lines);
  }else if(w.type==='clock'){
   const value=document.createElement('div');value.className='value';value.textContent='12:34:56\n22.10.2026';value.style.whiteSpace='pre-line';value.style.fontSize=contentSizes[w.contentSize]+'px';value.style.textAlign=['left','center','right'][w.contentAlign];value.style.color=palette[w.contentColor];node.appendChild(value);
  }else if(w.type==='strings'||w.type==='stringsVoltage'||w.type==='stringsCurrent'||w.type==='stringsPower'){
   const lines=document.createElement('div');lines.className='strings-lines';lines.textContent=stringLinesText(w);node.appendChild(lines);
   lines.style.fontSize=contentSizes[w.contentSize]+'px';lines.style.lineHeight='1.2';lines.style.whiteSpace='pre-line';lines.style.textAlign=['left','center','right'][w.contentAlign];lines.style.color=palette[w.contentColor];
  }else if(w.type==='text'){
   const body=document.createElement('div');body.className='free-text';body.textContent=w.text||'Введите текст в поле ниже';node.appendChild(body);
   body.style.fontSize=contentSizes[w.contentSize]+'px';body.style.textAlign=['left','center','right'][w.contentAlign];body.style.color=palette[w.contentColor];
  }else{
   const value=document.createElement('div');value.className='value';value.textContent=demoValues[w.type]||'DEMO';value.style.fontSize=contentSizes[w.contentSize]+'px';value.style.textAlign=['left','center','right'][w.contentAlign];value.style.color=palette[w.contentColor];node.appendChild(value);
  }
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
   if(pointerEdit.resizing){w.w=Math.max(40,Math.min(240-pointerEdit.x,pointerEdit.w+dx));w.h=Math.max(w.type==="calibration"?90:36,Math.min(290-pointerEdit.y,pointerEdit.h+dy))}
   else{w.x=Math.max(0,Math.min(240-w.w,pointerEdit.x+dx));w.y=Math.max(40,Math.min(290-w.h,pointerEdit.y+dy))}
   node.style.left=w.x+'px';node.style.top=w.y+'px';node.style.width=w.w+'px';node.style.height=w.h+'px';
  });
  node.addEventListener('pointerup',()=>pointerEdit=null);node.addEventListener('pointercancel',()=>pointerEdit=null);
  node.addEventListener('click',e=>{if(e.target.closest('.name'))return;selectedWidget=i;renderAll()});c.appendChild(node);
 });
}
function syncStyleControls(){const p=pages[editorPage],w=p&&p.widgets[selectedWidget],target=$('styleTarget').value;const keys=[target+'Size',target+'Align',target+'Color'];$('styleSize').disabled=!w;$('styleAlign').disabled=!w;$('styleColor').disabled=!w;$('backgroundColor').disabled=!w;$('showWidgetBorder').disabled=!w;if(!w)return;$('styleSize').value=String(w[keys[0]]);$('styleAlign').value=String(w[keys[1]]);$('styleColor').value=String(w[keys[2]]);$('backgroundColor').value=String(w.backgroundColor??0);$('showWidgetBorder').checked=w.showBorder!==false;}
function renderAll(){renderTabs();$('pageTitle').value=pages[editorPage].title;$('showPageTitle').checked=pages[editorPage].showTitle!==false;const p=pages[editorPage],w=p.widgets[selectedWidget];$('showWidgetHeading').disabled=!w;$('showWidgetHeading').checked=!!(w&&w.showHeading!==false);$('widgetText').disabled=!w||w.type!=='text';$('widgetText').value=w&&w.type==='text'?w.text:'';const isCalibration=!!w&&w.type==='calibration';$('calibrationEditor').style.display=isCalibration?'block':'none';if(isCalibration)renderCalibrationRows(w.stringCount||2);const isStrings=!!w&&['strings','stringsVoltage','stringsCurrent','stringsPower'].includes(w.type);const hasStringCount=isStrings||isCalibration;$('stringCountWrap').style.display=hasStringCount?'block':'none';$('stringCount').disabled=!hasStringCount;if(hasStringCount)$('stringCount').value=String(w.stringCount||2);syncStyleControls();renderCanvas();$('showOnTft').textContent='Показать страницу '+(editorPage+1)+' на TFT'}
const uiTextDefaults={"stringPrefix":"Стринг","currentLabel":"Ток","voltageLabel":"Напр.","routerLabel":"Роутер","apLabel":"Точка","noRouterLabel":"Роутер не подключён","networkLabel":"Сеть","signalLabel":"Сигнал","apSetupLabel":"Точка настройки:","selectHint":"ВВЕРХ/ВНИЗ: выбор","changeHint":"OK: изменить (5 сек.)","applyLabel":"Применение через"};
let uiTexts={...uiTextDefaults};const uiTextKeys=Object.keys(uiTextDefaults);
function renderUiTexts(values){uiTexts={...uiTextDefaults,...(values||{})};uiTextKeys.forEach(key=>{const el=$('ui'+key[0].toUpperCase()+key.slice(1));if(el)el.value=uiTexts[key]||uiTextDefaults[key]});}
async function saveUiTexts(reset){
 const values={};uiTextKeys.forEach(key=>{const el=$('ui'+key[0].toUpperCase()+key.slice(1));values[key]=reset?uiTextDefaults[key]:el.value});
 try{const r=await fetch('/api/ui-texts',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(values)});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.message||'Ошибка сохранения');uiTexts=values;renderUiTexts(uiTexts);$('uiTextsStatus').textContent=j.message;renderCanvas();}
 catch(e){$('uiTextsStatus').textContent='Ошибка: '+e.message}
}
$('saveUiTexts').addEventListener('click',()=>saveUiTexts(false));
$('resetUiTexts').addEventListener('click',()=>{if(confirm('Вернуть исходные подписи?'))saveUiTexts(true)});
function applyActiveCalibration(c){
 const valid=Array.isArray(c.activeCalibrationValid)?c.activeCalibrationValid:[];
 const activeCurrent=normalizeCalibration(c.activeCurrentCalibration),activeVoltage=normalizeCalibration(c.activeVoltageCalibration);
 for(let i=0;i<2;i++)if(valid[i]===true){currentCalibration[i]=activeCurrent[i];voltageCalibration[i]=activeVoltage[i];}
}
function loadConfig(c){
 renderUiTexts(c.uiTexts||{});
 stringTelemetry=Array.isArray(c.stringTelemetry)?c.stringTelemetry:[];
 currentCalibration=normalizeCalibration(c.currentCalibration);voltageCalibration=normalizeCalibration(c.voltageCalibration);applyActiveCalibration(c);
 $('netStatus').textContent=c.networkStatus||'Wi-Fi не подключён';
 if(c.clock){$('clockDate').value=c.clock.date||'';$('clockTime').value=c.clock.time||'';$('clockStatus').textContent=c.clock.valid?'RTC: '+c.clock.date+' '+c.clock.time:'RTC не отвечает или время недоступно';}
 networkStatus=c.networkStatus||'';networkLabel=c.networkLabel||'Wi-Fi: —';savedNetworks=c.savedNetworks||[];wifiMode=c.wifiMode||wifiMode;renderWiFiMode();$('defaultRouter').checked=!!wifiMode.defaultRouter;$('defaultAp').checked=!!wifiMode.defaultAp;renderWiFiDefaults();
 if(Array.isArray(c.pages)&&c.pages.length){pages=c.pages.map(normalizePage)}else{pages=[];ensurePages()}
 devicePage=Math.max(0,Math.min((Number(c.tftPage)||1)-1,pages.length-1));
 if(followDevice)editorPage=devicePage;
 if(editorPage>=pages.length)editorPage=pages.length-1;
 $('ssid').value=c.ssid||'';$('password').value='';renderSavedNetworks(savedNetworks);renderAll();updateDeviceStatus();
}
function updateDeviceStatus(){ $('deviceStatus').textContent='Физический дисплей: страница '+(devicePage+1)+' из '+pages.length+'. Редактор: страница '+(editorPage+1)+'.'; }
function collectData(){
 return {title:'SOLAR PV',fontScale:1,currentCalibration,voltageCalibration,pageCount:pages.length,pages:pages.map(p=>({title:p.title,showTitle:p.showTitle!==false,widgets:p.widgets.map(w=>({type:w.type,label:w.label,text:w.text||'',headingSize:w.headingSize,contentSize:w.contentSize,headingAlign:w.headingAlign,contentAlign:w.contentAlign,headingColor:w.headingColor,contentColor:w.contentColor,backgroundColor:w.backgroundColor??0,stringCount:w.stringCount||2,locked:w.locked===true,showHeading:w.showHeading!==false,showBorder:w.showBorder!==false,x:w.x,y:w.y,w:w.w,h:w.h,enabled:w.enabled}))}))};
}
async function refreshDeviceState(){
 try{const r=await fetch('/api/config?t='+Date.now(),{cache:'no-store'});if(!r.ok)return;const c=await r.json();
  stringTelemetry=Array.isArray(c.stringTelemetry)?c.stringTelemetry:[];currentCalibration=normalizeCalibration(c.currentCalibration);voltageCalibration=normalizeCalibration(c.voltageCalibration);applyActiveCalibration(c);updateStringPreviews();
  const p=Math.max(0,Math.min((Number(c.tftPage)||1)-1,Math.max(0,pages.length-1)));
  if(p!==devicePage){devicePage=p;if(followDevice)editorPage=Math.min(devicePage,pages.length-1);renderAll()}
  if(c.clock){if(document.activeElement!==$('clockDate'))$('clockDate').value=c.clock.date||'';if(document.activeElement!==$('clockTime'))$('clockTime').value=c.clock.time||'';$('clockStatus').textContent=c.clock.valid?'RTC: '+c.clock.date+' '+c.clock.time:'RTC не отвечает или время недоступно';}
  networkStatus=c.networkStatus||networkStatus;networkLabel=c.networkLabel||networkLabel;$('netStatus').textContent=networkStatus;$('preview').querySelectorAll('.wifi-lines').forEach(el=>{if(!el.closest('.widget')||pages[editorPage].widgets[Number(el.closest('.widget').dataset.index)]?.type!=='wifiControl')el.textContent=networkStatus||'Состояние сети появится здесь';else el.textContent=uiTexts.routerLabel+': '+(wifiMode.router?'ВКЛ':'ВЫКЛ')+'\\n'+uiTexts.apLabel+': '+(wifiMode.ap?'ВКЛ':'ВЫКЛ')+'\\nПульт: SETUP, затем 1/2';});$('prevNetwork').textContent=networkLabel;updateDeviceStatus();
 }catch(e){}
}
$('styleTarget').addEventListener('change',syncStyleControls);
function applyStyleControl(which,value){const p=pages[editorPage],w=p&&p.widgets[selectedWidget];if(!w)return;const target=$('styleTarget').value;w[target+which]=Number(value);renderAll();}
$('styleSize').addEventListener('change',()=>applyStyleControl('Size',$('styleSize').value));
$('styleAlign').addEventListener('change',()=>applyStyleControl('Align',$('styleAlign').value));
$('styleColor').addEventListener('change',()=>applyStyleControl('Color',$('styleColor').value));
$('backgroundColor').addEventListener('change',()=>{const w=pages[editorPage].widgets[selectedWidget];if(!w)return;w.backgroundColor=Number($('backgroundColor').value);renderAll()});
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
$('deletePage').addEventListener('click',async()=>{
 if(pages.length<=1)return;if(!confirm('Удалить страницу '+(editorPage+1)+' и все её блоки?'))return;
 const removed=editorPage;pages.splice(removed,1);if(editorPage>=pages.length)editorPage=pages.length-1;selectedWidget=-1;followDevice=false;
 try{await saveAll(false)}catch(e){alert('Не удалось сохранить изменения: '+e.message)}renderAll();
});
$('followDevice').addEventListener('click',()=>{followDevice=true;editorPage=Math.min(devicePage,pages.length-1);selectedWidget=-1;renderAll()});
$('addWidget').addEventListener('click',()=>{
 const p=pages[editorPage];if(p.widgets.length>=MAX_WIDGETS){alert('На одной странице максимум '+MAX_WIDGETS+' блоков.');return}
 const type=$('widgetType').value;const n=p.widgets.length;
 const isStrings=['strings','stringsVoltage','stringsCurrent','stringsPower'].includes(type);const wide=type==='wifiStatus'||type==='wifiControl'||type==='calibration'||type==='text'||isStrings||type==='clock';
 p.widgets.push({type,label:typeLabels[type],text:'',headingSize:1,contentSize:type==='wifiStatus'?1:2,stringCount:2,locked:false,showHeading:type==='calibration'?false:true,headingAlign:0,contentAlign:0,headingColor:0,contentColor:type==='wifiStatus'?0:3,backgroundColor:0,showBorder:true,x:wide?8:8+(n%3)*76,y:wide?55+(n%3)*50:55+Math.floor(n/3)*58,w:wide?224:72,h:(type==='calibration'?250:((type==='wifiStatus'||type==='wifiControl')?120:((type==='text'||type==='clock')?80:(isStrings?110:58)))),enabled:true});
 selectedWidget=p.widgets.length-1;renderAll();
});
$('deleteWidget').addEventListener('click',()=>{const p=pages[editorPage];if(selectedWidget<0||selectedWidget>=p.widgets.length){alert('Сначала выбери блок на холсте.');return}p.widgets.splice(selectedWidget,1);selectedWidget=-1;renderAll()});
$('widgetText').addEventListener('input',()=>{const w=pages[editorPage].widgets[selectedWidget];if(!w||w.type!=='text')return;w.text=$('widgetText').value.slice(0,240);renderCanvas()});
$('stringCount').addEventListener('change',()=>{const w=pages[editorPage].widgets[selectedWidget];if(!w||!(['strings','stringsVoltage','stringsCurrent','stringsPower'].includes(w.type)||w.type==='calibration'))return;w.stringCount=Math.max(w.type==='calibration'?2:1,Math.min(6,Number($('stringCount').value)||2));renderAll()});
$('applyCalibration').addEventListener('click',async()=>{const w=pages[editorPage]?.widgets[selectedWidget];if(!w||w.type!=='calibration')return;for(const input of $('calibrationRows').querySelectorAll('input[data-cal="currentTarget"],input[data-cal="voltageTarget"]')){if(input.value.trim()==='')continue;const index=Number(input.dataset.index),isCurrent=input.dataset.cal==='currentTarget',decimals=isCurrent?2:1,entered=Number(input.value),target=Number(entered.toFixed(decimals)),measured=stringTelemetry.find(v=>Number(v.id)===index+1),actual=measured&&measured.valid?Number(isCurrent?measured.current:measured.voltage):NaN;if(!Number.isFinite(entered)||!Number.isFinite(target)||target<=0||target>9999){alert('Введите корректное требуемое '+(isCurrent?'значение тока':'напряжение')+' больше нуля.');input.focus();return}input.value=target.toFixed(decimals);if(!Number.isFinite(actual)||actual<=0.001){alert('Нет актуального измерения '+(isCurrent?'тока':'напряжения')+' для стринга '+(index+1)+'.');input.focus();return}const factors=isCurrent?currentCalibration:voltageCalibration;factors[index]=Math.max(0.001,Math.min(99.999,Math.round(factors[index]*target/actual*1000)/1000))}try{await saveAll(true)}catch(e){alert('Не удалось сохранить коэффициенты: '+e.message)}});
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
function renderWiFiMode(){ $('toggleRouter').textContent=uiTexts.routerLabel+': '+(wifiMode.router?'ВКЛ':'ВЫКЛ');$('toggleAp').textContent=uiTexts.apLabel+': '+(wifiMode.ap?'ВКЛ':'ВЫКЛ');$('wifiModeStatus').textContent=uiTexts.routerLabel+': '+(wifiMode.router?'включён':'выключен')+' · Точка доступа: '+(wifiMode.ap?'включена':'выключена')+(wifiMode.apActive?' · активна':'');}
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
</script></body></html>
)rawliteral";
