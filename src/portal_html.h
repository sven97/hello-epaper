#pragma once
#include <Arduino.h>

// Single settings page. %TOKEN% placeholders are filled by buildPage() in
// portal.cpp. Every control auto-saves via POST /set (one field per
// request); the small inline <script> at the bottom wires the events and
// the firmware "update to N" pill. No framework, no CDN — served from the
// device.
inline const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%NAME% — settings</title>
<style>
body{font-family:system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#222}
h1{font-size:1.3rem}fieldset{border:1px solid #ccc;border-radius:8px;margin:0 0 1rem;padding:1rem}
legend{font-weight:600;padding:0 .4rem}label{display:block;margin:.6rem 0 .2rem}
input[type=text],select{width:100%;padding:.45rem;border:1px solid #bbb;border-radius:6px;box-sizing:border-box}
.row{display:flex;gap:.8rem}.row>div{flex:1}
button{padding:.4rem 1rem;border-radius:6px;border:1px solid #888;background:#f4f4f4;cursor:pointer;font-size:.9rem}
button.primary{background:#2563eb;color:#fff;border-color:#2563eb}
button.danger{border-color:#c00;color:#c00}
.note{color:#666;font-size:.85rem;margin-top:.2rem}
.hint{font-size:.85rem;margin-left:.4rem}
summary{cursor:pointer;font-weight:600;margin:1.4rem 0 .6rem}
details>*:not(summary){margin-left:.2rem}
</style></head><body>
<h1>%NAME% — settings</h1>
<p class="note" style="margin-top:-.6rem" id="strip">%STATUS% &middot; build %BUILD%<span id="fw"></span></p>

<fieldset><legend>Image</legend>
<label>New image every</label>
<select name="sleep">%SLEEP_OPTS%</select><span class="hint" data-for="sleep"></span>
<label>Image source URL</label>
<input type="text" name="url" value="%URL%" maxlength="512">
<span class="hint" data-for="url"></span>
<div class="note">Must return a baseline (non-progressive) JPEG.
Tokens: {width} {height} {seed}</div>
<label><input type="checkbox" name="paused" %PAUSED%> Pause — keep the current image</label>
<span class="hint" data-for="paused"></span>
<label><input type="checkbox" name="quiet_en" %QUIET_EN%> Skip updates between</label>
<div class="row">
<div><select name="quiet_start">%QS_OPTS%</select><span class="hint" data-for="quiet_start"></span></div>
<div><select name="quiet_end">%QE_OPTS%</select><span class="hint" data-for="quiet_end"></span></div>
</div>
</fieldset>

<fieldset><legend>Device</legend>
<label>Timezone</label>
<select name="tz">%TZ_OPTS%</select><span class="hint" data-for="tz"></span>
<label>Device name</label>
<input type="text" name="name" value="%NAME%" maxlength="24">
<span class="hint" data-for="name"></span>
<div class="note">lowercase letters, digits, hyphens &middot; reachable at <b>%NAME%.local</b></div>
<label>Orientation</label>
<select name="rot">%ROT_OPTS%</select><span class="hint" data-for="rot"></span>
</fieldset>

<details><summary>Advanced</summary>
<label><input type="checkbox" name="ota_en" %OTA_EN%> Install updates automatically</label>
<span class="hint" data-for="ota_en"></span>
<div class="note" style="margin-top:.8rem">Running %HASH%</div>
<div id="fr" style="margin-top:1rem"><button type="button" class="danger" onclick="frExpand()">Factory reset</button></div>
<div class="note">Erases all settings and Wi-Fi. The device restarts into setup mode.</div>
</details>

<script>
var $=function(s){return document.querySelector(s)};
function hint(f,msg,ok){var h=$('.hint[data-for="'+f+'"]');if(!h)return;
 h.textContent=ok?' ✓':' '+msg;h.style.color=ok?'#0a0':'#c00';
 if(ok)setTimeout(function(){if(h.textContent===' ✓')h.textContent=''},1500)}
function save(f,v){fetch('/set',{method:'POST',
 headers:{'Content-Type':'application/x-www-form-urlencoded'},
 body:'f='+encodeURIComponent(f)+'&v='+encodeURIComponent(v)})
 .then(function(r){return r.text().then(function(t){hint(f,t,r.ok)})})
 .catch(function(){hint(f,'offline',false)})}
function isText(el){return el.tagName==='INPUT'&&el.type==='text'}
document.addEventListener('change',function(e){var el=e.target;
 if(!el.name||isText(el))return;
 save(el.name,el.type==='checkbox'?(el.checked?'1':'0'):el.value)});
document.addEventListener('blur',function(e){var el=e.target;
 if(!el.name||!isText(el)||el.value===el.defaultValue)return;
 el.defaultValue=el.value;save(el.name,el.value)},true);

fetch('/ota/peek').then(function(r){return r.text()}).then(function(t){
 var fw=$('#fw'),p=t.trim().split(' ');
 if(p[0]==='uptodate'){fw.textContent=' · up to date'}
 else if(p[0]==='available'){
  fw.innerHTML=' <button type="button" class="primary" id="upd">update to '+p[1]+'</button>';
  $('#upd').onclick=function(){
   fw.innerHTML=' Install build '+p[1]+'? '+
    '<button type="button" class="primary" id="doupd">Install</button> '+
    '<button type="button" id="noupd">Cancel</button>';
   $('#noupd').onclick=function(){location.reload()};
   $('#doupd').onclick=function(){
    fw.textContent=' Installing — the device restarts in ~30 s';
    fetch('/ota/install',{method:'POST'}).catch(function(){})}}}
 else{fw.innerHTML=' · <a href="/log">update check failed</a>'}})
 .catch(function(){$('#fw').textContent=' · update check failed'});

function frExpand(){$('#fr').innerHTML='Erase all settings and Wi-Fi? '+
 '<button type="button" class="danger" id="frgo">Erase everything</button> '+
 '<button type="button" id="frno" onclick="location.reload()">Cancel</button>';
 $('#frgo').onclick=function(){
  $('#fr').textContent='Erasing — the device restarts into setup mode.';
  fetch('/factory-reset',{method:'POST'}).catch(function(){})}}
</script>
</body></html>)HTML";

// Debug page: the currently-cached image plus the rolling log, readable
// over HTTP with no interactive serial-monitor TTY needed.
inline const char DEBUG_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Debug</title>
<style>body{font-family:system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#222}
img{max-width:100%;border:1px solid #ccc;border-radius:6px}
pre{white-space:pre-wrap;word-break:break-word;background:#f4f4f4;border-radius:6px;padding:.8rem;font-size:.8rem}
h2{font-size:1rem;margin:1.4rem 0 .3rem}</style>
</head><body>
<h1>Debug</h1>
<p class="note">%BOARD% &middot; firmware %HASH% &middot; build %BUILD%</p>
<p class="note">auto-update %OTA_STATE% &middot; last check %OTA_LAST% &middot; trial %OTA_TRIAL%</p>
<h2>Displayed now (last fetched image)</h2>
<img src="/last.jpg" alt="last fetched image">
<h2>On the display right now (full resolution)</h2>
<img src="/current" alt="live display capture">
<h2>On the display just before that (thumbnail)</h2>
<img src="/previous" alt="previous display capture">
<h2>Log</h2>
<pre>%LOG%</pre>
</body></html>)HTML";
