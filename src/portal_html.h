#pragma once
#include <Arduino.h>

// Single-page settings form. %TOKEN% placeholders are filled by
// buildPage() in portal.cpp. Kept dependency-free: inline CSS, no JS.
inline const char PORTAL_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>%NAME% — EE02 Frame</title>
<style>
body{font-family:system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#222}
h1{font-size:1.3rem}fieldset{border:1px solid #ccc;border-radius:8px;margin:0 0 1rem;padding:1rem}
legend{font-weight:600;padding:0 .4rem}label{display:block;margin:.6rem 0 .2rem}
input[type=text],select{width:100%;padding:.45rem;border:1px solid #bbb;border-radius:6px;box-sizing:border-box}
.row{display:flex;gap:.8rem}.row>div{flex:1}
button{padding:.55rem 1.2rem;border-radius:6px;border:1px solid #888;background:#f4f4f4;cursor:pointer}
button.primary{background:#2563eb;color:#fff;border-color:#2563eb}
.msg{background:#fee;border:1px solid #c00;border-radius:6px;padding:.6rem;margin-bottom:1rem}
.note{color:#666;font-size:.85rem;margin-top:.2rem}
.danger{border-color:#c00}
</style></head><body>
<h1>%NAME% — settings</h1>
<p class="note" style="margin-top:-.6rem">%STATUS%</p>
%ERROR%
<form method="POST" action="/save">
<fieldset><legend>Pictures</legend>
<label>Refresh every</label><select name="sleep">%SLEEP_OPTS%</select>
<label>Image source URL</label>
<input type="text" name="url" value="%URL%" maxlength="512">
<div class="note">Must return a baseline (non-progressive) JPEG.
Tokens: {width} {height} {seed}</div>
<label><input type="checkbox" name="paused" %PAUSED%> Paused (pin current picture)</label>
</fieldset>
<fieldset><legend>Quiet hours</legend>
<label><input type="checkbox" name="quiet_en" %QUIET_EN%> Skip refreshes between</label>
<div class="row"><div><select name="quiet_start">%QS_OPTS%</select></div>
<div><select name="quiet_end">%QE_OPTS%</select></div></div>
</fieldset>
<fieldset><legend>Device</legend>
<label>Timezone</label><select name="tz">%TZ_OPTS%</select>
<label>Name (mDNS: <b>%NAME%.local</b>)</label>
<input type="text" name="name" value="%NAME%" maxlength="24">
<div class="note">lowercase letters, digits, hyphens</div>
<label>Orientation</label><select name="rot">%ROT_OPTS%</select>
</fieldset>
<button class="primary" type="submit">Save &amp; apply</button>
</form>
<form method="POST" action="/action/newpic" style="margin-top:1rem">
<button type="submit">Fetch new picture now</button></form>
<form method="POST" action="/action/forgetwifi" style="margin-top:1rem"
onsubmit="return confirm('Forget Wi-Fi and reopen the setup hotspot?')">
<button class="danger" type="submit">Forget Wi-Fi…</button></form>
</body></html>)HTML";

inline const char PORTAL_DONE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EE02 Frame</title>
<style>body{font-family:system-ui,sans-serif;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#222}
.note{color:#666;font-size:.85rem}</style>
</head><body><h1>%TITLE%</h1><div>%BODY%</div></body></html>)HTML";
