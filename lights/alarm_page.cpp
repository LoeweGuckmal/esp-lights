#include "alarm_page.h"

namespace {

const char ALARM_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#111318">
<title>Alarms</title>
<style>
  * { box-sizing: border-box; }
  html { color-scheme: dark; -webkit-text-size-adjust: 100%; text-size-adjust: 100%; }
  body {
    margin: 0; padding: 16px 16px 48px;
    font: 16px system-ui, -apple-system, sans-serif;
    background: #111318; color: #eee;
    -webkit-tap-highlight-color: transparent;
  }
  main { max-width: 420px; margin: 0 auto; }
  nav { display: flex; flex-wrap: wrap; justify-content: center; gap: 4px; }
  nav a {
    padding: 7px 14px; border-radius: 999px;
    color: #b8bbc8; text-decoration: none; font-size: 14px;
  }
  nav a.active { background: #20232b; color: #fff; }
  header {
    display: flex; align-items: center; justify-content: space-between;
    margin: 22px 4px 14px;
  }
  h2 { margin: 0; font-size: 30px; }
  h3 {
    margin: 0; font-size: 12px; font-weight: 600;
    letter-spacing: .09em; text-transform: uppercase; color: #9a9eb0;
  }
  button {
    font: inherit; color: inherit; border: 0;
    border-radius: 12px; cursor: pointer;
  }
  button:disabled { opacity: .45; cursor: default; }
  button:focus-visible, [tabindex]:focus-visible {
    outline: 2px solid #b9bdff; outline-offset: 2px;
  }
  .card {
    background: #20232b; border-radius: 20px;
    padding: 18px; margin-bottom: 12px;
  }
  .muted { color: #8a8ea0; font-size: 13px; }

  /* Next alarm */
  #next {
    padding: 22px; min-height: 236px;
    background:
      radial-gradient(120% 80% at 50% 125%, rgba(255, 160, 90, .45),
                      rgba(124, 131, 255, .12) 55%, transparent 78%),
      #20232b;
  }
  #next.idle { background: #20232b; }
  .label { display: flex; align-items: center; gap: 8px; min-height: 22px; }
  .chip {
    font-size: 12px; padding: 3px 10px; border-radius: 999px;
    background: rgba(124, 131, 255, .2); color: #c5c8ff;
  }
  .chip.warn { background: rgba(255, 122, 107, .18); color: #ffb1a6; }
  .chip.sun { background: rgba(255, 176, 112, .2); color: #ffcf9e; }
  .when { margin-top: 14px; color: #d4d6e0; }
  .big {
    font-size: clamp(52px, 18vw, 68px); font-weight: 200; line-height: 1.05;
    letter-spacing: -1px; font-variant-numeric: tabular-nums;
  }
  .big.dim { color: #5d6173; }
  .big.struck {
    color: #6d7184;
    text-decoration: line-through; text-decoration-thickness: 3px;
  }
  .sub { margin: 6px 0 0; color: #b8bbc8; font-size: 14px; line-height: 1.5; }
  .actions { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 18px; }
  .actions button { flex: 1 1 7.5em; padding: 11px 8px; background: #343844; }
  .actions button.primary { background: #7c83ff; color: #fff; }
  .bar {
    height: 6px; border-radius: 3px; overflow: hidden;
    background: #343844; margin-top: 14px;
  }
  .bar span {
    display: block; height: 100%; border-radius: 3px;
    background: linear-gradient(90deg, #ff9a5a, #ffdcaa);
    transition: width .5s;
  }

  /* Schedules */
  .section {
    display: flex; align-items: baseline; justify-content: space-between;
    margin: 28px 4px 10px;
  }
  /* The switch sits in the corner, so the day dots get the full width. */
  .schedule { position: relative; cursor: pointer; transition: background .15s; }
  .schedule:hover { background: #252832; }
  .schedule .switch { position: absolute; top: 18px; right: 18px; }
  .schedule .time {
    padding-right: 64px;
    font-size: 38px; font-weight: 300; line-height: 1.1;
    font-variant-numeric: tabular-nums;
  }
  .schedule.disabled .info { opacity: .4; }
  .dots {
    display: grid; grid-template-columns: repeat(7, minmax(0, 28px));
    gap: 5px; margin: 10px 0 8px;
  }
  .dots span {
    width: 100%; aspect-ratio: 1; border-radius: 50%;
    display: grid; place-items: center;
    font-size: 11px; background: #2a2d37; color: #6f7386;
  }
  .dots span.on { background: rgba(124, 131, 255, .25); color: #d0d3ff; }
  .meta { display: flex; align-items: center; gap: 7px; font-size: 14px; color: #b8bbc8; }
  .swatch {
    width: 12px; height: 12px; border-radius: 50%; flex: none;
    box-shadow: 0 0 0 1px rgba(255, 255, 255, .15);
  }
  .empty { text-align: center; color: #8a8ea0; padding: 26px 18px; line-height: 1.5; }
  .add {
    width: 100%; padding: 15px;
    background: transparent; color: #b9bdff;
    border: 1.5px dashed #3c404d; border-radius: 20px;
  }
  .add:hover { border-color: #7c83ff; }
  #list.off { opacity: .5; }
  #clock { text-align: center; margin-top: 26px; }

  /* Switch */
  .switch { position: relative; width: 51px; height: 31px; flex: none; }
  .switch input {
    position: absolute; inset: 0; z-index: 1;
    width: 100%; height: 100%; margin: 0;
    opacity: 0; cursor: pointer;
  }
  .switch span {
    position: absolute; inset: 0; border-radius: 999px;
    background: #3a3e4b; transition: background .2s;
  }
  .switch span::after {
    content: ''; position: absolute; top: 3px; left: 3px;
    width: 25px; height: 25px; border-radius: 50%; background: #fff;
    box-shadow: 0 1px 3px rgba(0, 0, 0, .4); transition: transform .2s;
  }
  .switch input:checked + span { background: #7c83ff; }
  .switch input:checked + span::after { transform: translateX(20px); }
  .switch input:focus-visible + span { outline: 2px solid #b9bdff; outline-offset: 2px; }
  .switch input:disabled { cursor: default; }

  /* Sheets: slide up from the bottom on phones, centered on wide screens */
  dialog {
    width: 100%; max-width: 440px; max-height: 92vh; max-height: 92dvh;
    margin: auto auto 0; padding: 0; border: 0;
    border-radius: 22px 22px 0 0;
    background: #1b1e25; color: #eee;
  }
  dialog[open] { animation: rise .22s ease-out; }
  dialog::backdrop { background: rgba(5, 6, 10, .65); }
  @keyframes rise { from { transform: translateY(30px); opacity: 0; } }
  @media (min-width: 560px) {
    dialog { margin: auto; border-radius: 22px; }
  }
  .sheet { padding: 0 20px 26px; }
  .sheetHead {
    display: flex; align-items: center; justify-content: space-between;
    position: sticky; top: 0; z-index: 2;
    padding: 14px 0 10px; background: #1b1e25;
  }
  .sheetHead h4 { margin: 0; font-size: 17px; }
  .sheetHead h4:focus-visible { outline: none; }
  .link { background: none; padding: 6px 2px; color: #b9bdff; }
  .link.strong { font-weight: 600; }
  .field { margin: 18px 0 22px; }
  .fieldHead {
    display: flex; justify-content: space-between; align-items: baseline;
    margin-bottom: 10px; color: #d4d6e0;
  }
  .days { display: grid; grid-template-columns: repeat(7, minmax(0, 1fr)); gap: 6px; }
  .day {
    position: relative; justify-self: center;
    width: 100%; max-width: 44px; aspect-ratio: 1; padding: 0;
    border-radius: 50%; background: #2a2d37; color: #b8bbc8;
    font-size: clamp(12px, 3.8vw, 14px);
  }
  .day.on { background: #7c83ff; color: #fff; }
  .day.taken::after {
    content: ''; position: absolute; left: 50%; bottom: -9px;
    width: 5px; height: 5px; margin-left: -2.5px;
    border-radius: 50%; background: #8a8ea0;
  }
  .hint { min-height: 18px; margin: 16px 0 0; font-size: 13px; color: #a6aabb; line-height: 1.45; }
  /* Safari on iPhone draws its own time control, which ignores the width
     and the centering unless appearance is reset. The box around it clips
     whatever still sticks out. */
  .timeBox {
    overflow: hidden;
    background: #111318; border: 1px solid #343844; border-radius: 16px;
  }
  input[type=time] {
    display: block; width: 100%; min-width: 0; max-width: 100%;
    -webkit-appearance: none; appearance: none;
    margin: 0; padding: 8px 12px; line-height: 1.2;
    font: 200 clamp(38px, 14vw, 52px) system-ui, -apple-system, sans-serif;
    font-variant-numeric: tabular-nums; text-align: center;
    background: transparent; color: #fff; border: 0;
  }
  input[type=time]::-webkit-date-and-time-value { margin: 0; text-align: center; }
  input[type=time]::-webkit-calendar-picker-indicator {
    width: 22px; height: 22px; opacity: .45; cursor: pointer;
  }
  .segments { display: flex; background: #111318; border-radius: 12px; padding: 3px; }
  .segments button {
    flex: 1; padding: 8px 0; border-radius: 9px;
    background: none; color: #b8bbc8; font-size: 15px;
  }
  .segments button.on { background: #343844; color: #fff; }
  input[type=range] { width: 100%; margin: 0; accent-color: #8c92ff; }
  .ends {
    display: flex; justify-content: space-between;
    margin-top: 4px; font-size: 12px; color: #8a8ea0;
  }
  .preview { height: 54px; border-radius: 14px; border: 1px solid #343844; }
  .danger {
    width: 100%; padding: 12px; margin-top: 24px;
    background: rgba(255, 122, 107, .12); color: #ff9c90;
  }
  .center { text-align: center; }

  #toast {
    position: fixed; left: 50%; bottom: 24px; z-index: 10;
    max-width: calc(100% - 32px); padding: 11px 18px;
    border-radius: 14px; background: #e9eaf2; color: #15171c;
    font-size: 15px; box-shadow: 0 8px 24px rgba(0, 0, 0, .4);
    opacity: 0; pointer-events: none;
    transform: translate(-50%, 20px);
    transition: opacity .2s, transform .2s;
  }
  #toast.show { opacity: 1; transform: translate(-50%, 0); }
  #toast.error { background: #ff7a6b; color: #fff; }

  /* Small phones, like an iPhone mini with larger display zoom */
  @media (max-width: 360px) {
    body { padding-left: 12px; padding-right: 12px; }
    .card { padding: 16px; }
    #next { padding: 18px; }
    .schedule .switch { top: 16px; right: 16px; }
    .sheet { padding: 0 16px 22px; }
  }
</style>
</head>
<body>
<nav>
  <a href="/led">LED Strip</a>
  <a href="/room">Room Light</a>
  <a href="/alarm">Alarms</a>
</nav>

<main>
  <header>
    <h2>Alarms</h2>
    <label class="switch" title="All alarms on or off">
      <input id="master" type="checkbox" disabled aria-label="All alarms">
      <span></span>
    </label>
  </header>

  <section id="next" class="card idle" aria-live="polite">
    <h3>Next alarm</h3>
    <div class="big dim">--:--</div>
    <p class="sub">Loading...</p>
  </section>

  <div class="section">
    <h3>Schedule</h3>
    <span class="muted">Repeats every week</span>
  </div>
  <div id="list"></div>
  <button type="button" class="add" onclick="openEditor(null)">+ Add schedule</button>

  <p id="clock" class="muted"></p>
</main>

<dialog id="editor" aria-labelledby="edTitle">
  <div class="sheet">
    <div class="sheetHead">
      <button type="button" class="link" onclick="closeSheet('editor')">Cancel</button>
      <h4 id="edTitle" tabindex="-1" autofocus>New schedule</h4>
      <button type="button" class="link strong" id="edSave" onclick="saveSchedule()">Save</button>
    </div>

    <div class="field">
      <div class="fieldHead">Days</div>
      <div id="edDays" class="days"></div>
      <p id="edHint" class="hint"></p>
    </div>

    <div class="field">
      <div class="fieldHead"><label for="edTime">Wake up</label></div>
      <div class="timeBox"><input id="edTime" type="time" required></div>
    </div>

    <div class="field">
      <div class="fieldHead">Sunrise <span id="edSunrise" class="muted"></span></div>
      <div id="edFade" class="segments" role="group" aria-label="Sunrise length in minutes"></div>
    </div>

    <div class="field">
      <div class="fieldHead">
        <label for="edBright">Brightness</label>
        <span id="edBrightText" class="muted"></span>
      </div>
      <input id="edBright" type="range" min="1" max="100">
    </div>

    <div class="field">
      <div class="fieldHead"><label for="edTemp">Temperature</label></div>
      <input id="edTemp" type="range" min="0" max="100">
      <div class="ends"><span>Cold</span><span>Warm</span></div>
    </div>

    <div id="edPreview" class="preview" aria-hidden="true"></div>
    <div class="ends"><span id="edFrom"></span><span id="edTo"></span></div>

    <button type="button" id="edDelete" class="danger" onclick="deleteSchedule()">Delete schedule</button>
  </div>
</dialog>

<dialog id="nextSheet" aria-labelledby="nxTitle">
  <div class="sheet">
    <div class="sheetHead">
      <button type="button" class="link" onclick="closeSheet('nextSheet')">Cancel</button>
      <h4 id="nxTitle" tabindex="-1" autofocus>Next alarm only</h4>
      <button type="button" class="link strong" id="nxSave" onclick="saveNext()">Save</button>
    </div>
    <p id="nxDay" class="center"></p>
    <div class="timeBox"><input id="nxTime" type="time" required aria-label="Alarm time"></div>
    <p id="nxNote" class="hint center"></p>
  </div>
</dialog>

<div id="toast" role="status"></div>

<script>
const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const DAY_NAMES = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const WEEK = [1, 2, 3, 4, 5, 6, 0];  // Monday first
const FADES = [0, 5, 10, 15, 20, 30];
// Same as AlarmClock::DEFAULT_* in alarm_clock.h
const DEFAULTS = { fade: 15, brightness: 70, temperature: 50 };

const $ = id => document.getElementById(id);
let st = null;  // latest status from the ESP
let ed = null;  // schedule in the editor
let nx = null;  // alarm in the "next alarm only" sheet

// --- Talking to the ESP ---

// Every route answers with the full status.
async function call(path, payload) {
  const response = await fetch('/alarm' + path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload || {})
  });
  const data = await response.json();
  if (!data.ok) throw new Error(data.error || 'HTTP ' + response.status);
  st = data;
  render();
}

// Sends a change, then shows `done` (a text or a function returning one).
// Returns true on success.
async function change(path, payload, done) {
  try {
    await call(path, payload);
    if (done) toast(typeof done === 'function' ? done() : done);
    return true;
  } catch (e) {
    listKey = '';
    render();  // flips switches back
    toast(e.message, true);
    return false;
  }
}

async function refresh() {
  try {
    await call('/status');
  } catch (e) {
    $('clock').textContent = 'ESP not reachable';
  }
}

// --- Time and text helpers ---

const pad = n => String(n).padStart(2, '0');
const bit = d => 1 << d;

function toMinutes(time) {
  const [h, m] = time.split(':').map(Number);
  return h * 60 + m;
}

function fromMinutes(min) {
  min = (min % 1440 + 1440) % 1440;
  return pad(Math.floor(min / 60)) + ':' + pad(min % 60);
}

// Dates come as yyyymmdd in the ESP's time zone. Date objects are only
// used for names and to count days.
const toDate = n => new Date(Math.floor(n / 10000), Math.floor(n / 100) % 100 - 1, n % 100, 12);
const toNumber = d => d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
const daysFromToday = n => Math.round((toDate(n) - toDate(st.now.date)) / 86400000);

// "today", "tomorrow" or "Wednesday"
function dayWord(n) {
  const diff = daysFromToday(n);
  return diff === 0 ? 'today' : diff === 1 ? 'tomorrow' : DAY_NAMES[toDate(n).getDay()];
}

// "Tomorrow, Mon 19 Oct" or "Wednesday, 21 Oct"
function dayText(n) {
  const d = toDate(n), diff = daysFromToday(n);
  if (diff > 1) return DAY_NAMES[d.getDay()] + ', ' + d.getDate() + ' ' + MONTHS[d.getMonth()];
  return (diff === 0 ? 'Today' : 'Tomorrow') + ', ' +
         DAYS[d.getDay()] + ' ' + d.getDate() + ' ' + MONTHS[d.getMonth()];
}

function inText(min) {
  if (min < 1) return 'now';
  if (min < 60) return 'in ' + min + ' min';
  const h = Math.floor(min / 60), m = min % 60;
  if (h < 24) return 'in ' + h + ' h' + (m ? ' ' + m + ' min' : '');
  const d = Math.floor(h / 24);
  return 'in ' + d + (d === 1 ? ' day' : ' days') + (h % 24 ? ' ' + h % 24 + ' h' : '');
}

// "Weekdays", "Every day" or "Mon, Wed – Fri"
function daysText(bits) {
  if (bits === 127) return 'Every day';
  if (bits === 62) return 'Weekdays';
  if (bits === 65) return 'Weekends';
  const parts = [];
  for (let i = 0; i < 7; i++) {
    if (!(bits & bit(WEEK[i]))) continue;
    let j = i;
    while (j < 6 && (bits & bit(WEEK[j + 1]))) j++;
    if (j - i >= 2) parts.push(DAYS[WEEK[i]] + ' – ' + DAYS[WEEK[j]]);
    else for (let k = i; k <= j; k++) parts.push(DAYS[WEEK[k]]);
    i = j;
  }
  return parts.join(', ');
}

// White light like on the room light page; level 0..1
function lightColor(temperature, level) {
  const cold = [200, 220, 255], warm = [255, 180, 100], t = temperature / 100;
  return 'rgb(' + cold.map((c, i) => Math.round((c + (warm[i] - c) * t) * level)).join(',') + ')';
}

// "Next alarm tomorrow at 07:00"
function nextAlarmText(fallback) {
  const a = st.upcoming.find(x => !x.skipped);
  return a ? 'Next alarm ' + dayWord(a.date) + ' at ' + a.time : fallback;
}

// --- Page ---

function render() {
  if (!st) return;
  $('master').checked = st.enabled;
  $('master').disabled = false;
  renderNext();
  renderList();
  $('clock').textContent = st.time_valid
    ? 'Time on the ESP: ' + DAYS[st.now.weekday] + ' ' + st.now.time
    : 'Waiting for the time from the internet...';
}

function renderNext() {
  const [a, b] = st.upcoming;
  const usual = a && a.schedule !== null ? st.schedules[a.schedule].time : null;
  let idle = false, html;

  if (!st.time_valid) {
    idle = true;
    html = `<h3>Next alarm</h3>
      <div class="big dim">--:--</div>
      <p class="sub">The ESP is waiting for the time from the internet.
        Alarms ring once it knows it.</p>`;
  } else if (!st.enabled) {
    idle = true;
    html = `<h3>Alarms are off</h3>
      <div class="big dim">--:--</div>
      <p class="sub">No alarm rings until you turn them back on.</p>
      <div class="actions"><button class="primary" onclick="setMaster(true)">Turn on</button></div>`;
  } else if (st.sunrise) {
    const s = st.sunrise;
    html = `<div class="label"><h3>Good morning</h3><span class="chip sun">Sunrise ${s.progress} %</span></div>
      <div class="when">Alarm at</div>
      <div class="big">${s.time}</div>
      <div class="bar"><span style="width:${s.progress}%"></span></div>
      <p class="sub">${s.lamp_connected ? 'The room light is slowly getting brighter.'
                                        : 'Trying to reach the room light...'}</p>
      <div class="actions"><button onclick="stopSunrise()">Stop sunrise</button></div>`;
  } else if (!a) {
    idle = true;
    html = `<h3>Next alarm</h3>
      <div class="big dim">--:--</div>
      <p class="sub">No alarm in the next 7 days.</p>
      <div class="actions">
        <button class="primary" onclick="openNext()">One-time alarm</button>
        <button onclick="openEditor(null)">Add schedule</button>
      </div>`;
  } else if (a.skipped) {
    html = `<div class="label"><h3>Next alarm</h3><span class="chip warn">Skipped</span></div>
      <div class="when">${dayText(a.date)}</div>
      <div class="big struck">${a.time}</div>
      <p class="sub">${b ? 'Then ' + dayWord(b.date) + ' at ' + b.time
                         : 'No other alarm in the next 7 days.'}</p>
      <div class="actions">
        <button class="primary" onclick="resetNext('The alarm is back on')">Undo skip</button>
        <button onclick="openNext()">Change time</button>
      </div>`;
  } else {
    const chip = !a.changed ? '' : '<span class="chip">' + (usual ? 'Only this time' : 'One-time') + '</span>';
    const sunrise = a.fade ? 'Sunrise from ' + fromMinutes(toMinutes(a.time) - a.fade) : 'No sunrise';
    const second = !a.changed ? '<button onclick="skipNext()">Skip</button>'
      : usual ? `<button onclick="resetNext('Back to ${usual}')">Back to ${usual}</button>`
      : `<button onclick="resetNext('One-time alarm removed')">Remove</button>`;
    html = `<div class="label"><h3>Next alarm</h3>${chip}</div>
      <div class="when">${dayText(a.date)}</div>
      <div class="big">${a.time}</div>
      <p class="sub">${sunrise} · ${inText(a.in_minutes)}${a.changed && usual ? '<br>Usually ' + usual : ''}</p>
      <div class="actions">
        <button class="primary" onclick="openNext()">Change</button>
        ${second}
      </div>`;
  }

  $('next').classList.toggle('idle', idle);
  $('next').innerHTML = html;
}

let listKey = '';

function renderList() {
  const list = $('list');
  list.classList.toggle('off', !st.enabled);

  // Only rebuild when something changed, so focus isn't lost on refresh.
  const key = JSON.stringify(st.schedules);
  if (key === listKey) return;
  listKey = key;

  if (!st.schedules.length) {
    list.innerHTML = `<div class="card empty">No schedule yet.<br>
      Add one to wake up with the room light every week.</div>`;
    return;
  }

  list.innerHTML = st.schedules.map(s => `
    <div class="card schedule${s.enabled ? '' : ' disabled'}" role="button" tabindex="0"
         aria-label="Edit ${s.time}, ${daysText(s.days)}"
         onclick="openEditor(${s.id})" onkeydown="cardKey(event, ${s.id})">
      <div class="info">
        <div class="time">${s.time}</div>
        <div class="dots" title="${daysText(s.days)}">${WEEK.map(d =>
          `<span class="${s.days & bit(d) ? 'on' : ''}">${DAYS[d].slice(0, 2)}</span>`).join('')}</div>
        <div class="meta">
          <i class="swatch" style="background:${lightColor(s.temperature, .4 + .6 * s.brightness / 100)}"></i>
          ${s.fade ? s.fade + ' min sunrise' : 'No sunrise'} · ${s.brightness} %
        </div>
      </div>
      <label class="switch" onclick="event.stopPropagation()" onkeydown="event.stopPropagation()">
        <input type="checkbox" ${s.enabled ? 'checked' : ''}
               aria-label="Alarm ${s.time}, ${daysText(s.days)}"
               onchange="toggleSchedule(${s.id}, this.checked)">
        <span></span>
      </label>
    </div>`).join('');
}

function cardKey(event, id) {
  if (event.key !== 'Enter' && event.key !== ' ') return;
  event.preventDefault();
  openEditor(id);
}

// --- Actions ---

$('master').addEventListener('change', () => setMaster($('master').checked));

function setMaster(on) {
  change(on ? '/on' : '/off', null, on ? () => nextAlarmText('Alarms on') : 'All alarms off');
}

function toggleSchedule(id, on) {
  change('/save', { id, enabled: on ? 1 : 0 }, on ? () => nextAlarmText('Schedule on') : 'Schedule off');
}

function skipNext() {
  change('/next', { date: st.upcoming[0].date, skip: 1 },
         () => 'Skipped. ' + nextAlarmText('No other alarm this week'));
}

function resetNext(message) {
  change('/next/reset', null, message);
}

function stopSunrise() {
  change('/stop', null, 'Sunrise stopped');
}

// --- Schedule editor ---

// A new schedule gets the days no other schedule has (weekdays for the first).
function freeDays() {
  if (!st.schedules.length) return 62;
  return st.schedules.reduce((free, s) => free & ~s.days, 127);
}

function openEditor(id) {
  if (!st) return;
  const s = st.schedules.find(x => x.id === id);
  ed = s ? { ...s } : { id: null, days: freeDays(), time: '07:00', ...DEFAULTS };

  $('edTitle').textContent = s ? 'Edit schedule' : 'New schedule';
  $('edTime').value = ed.time;
  $('edBright').value = ed.brightness;
  $('edTemp').value = ed.temperature;
  $('edDelete').hidden = !s;
  disarmDelete();

  $('edDays').innerHTML = WEEK.map(d =>
    `<button type="button" class="day" data-day="${d}" onclick="toggleDay(${d})">${DAYS[d].slice(0, 2)}</button>`
  ).join('');

  const fades = FADES.includes(ed.fade) ? FADES : [...FADES, ed.fade].sort((x, y) => x - y);
  $('edFade').innerHTML = fades.map(f =>
    `<button type="button" data-fade="${f}" onclick="setFade(${f})">${f || 'Off'}</button>`
  ).join('');

  renderEditor();
  $('editor').showModal();
}

function renderEditor() {
  // The other schedules, by weekday
  const owner = {};
  st.schedules.forEach(s => {
    if (s.id !== ed.id) WEEK.forEach(d => { if (s.days & bit(d)) owner[d] = s; });
  });

  document.querySelectorAll('#edDays .day').forEach(button => {
    const d = Number(button.dataset.day), on = (ed.days & bit(d)) !== 0;
    button.classList.toggle('on', on);
    button.classList.toggle('taken', !on && !!owner[d]);
    button.setAttribute('aria-pressed', on);
    button.setAttribute('aria-label', DAY_NAMES[d]);
    button.title = owner[d] && !on ? DAY_NAMES[d] + ': in your ' + owner[d].time + ' schedule' : DAY_NAMES[d];
  });

  // A day can only be in one schedule, so picking one moves it here.
  const moves = new Map();
  WEEK.forEach(d => {
    if (!(ed.days & bit(d)) || !owner[d]) return;
    if (!moves.has(owner[d])) moves.set(owner[d], []);
    moves.get(owner[d]).push(DAYS[d]);
  });
  let hint = '';
  moves.forEach((days, other) => {
    const removed = (other.days & ~ed.days) === 0;
    hint += days.join(', ') + (days.length > 1 ? ' move' : ' moves') + ' here from your ' +
            other.time + ' schedule' + (removed ? ', which is then deleted' : '') + '. ';
  });
  if (!ed.days) hint = 'Pick at least one day.';
  else if (!hint && Object.keys(owner).length) hint = 'A dot marks a day of another schedule.';
  $('edHint').textContent = hint;
  $('edSave').disabled = !ed.days || !ed.time;

  document.querySelectorAll('#edFade button').forEach(button =>
    button.classList.toggle('on', Number(button.dataset.fade) === ed.fade));

  const start = fromMinutes(toMinutes(ed.time) - ed.fade);
  $('edSunrise').textContent = ed.fade ? start + ' – ' + ed.time : 'Off';
  $('edBrightText').textContent = ed.brightness + ' %';

  // From dim and warm to the chosen light. The lamp's brightness rises
  // with the square, which looks about linear to the eye.
  const target = .3 + .7 * ed.brightness / 100;
  const stops = [0, .25, .5, .75, 1].map(p =>
    lightColor(100 + (ed.temperature - 100) * p, .08 + (target - .08) * p) + ' ' + p * 100 + '%');
  $('edPreview').style.background = ed.fade
    ? 'linear-gradient(90deg, ' + stops.join(', ') + ')'
    : lightColor(ed.temperature, target);
  $('edFrom').textContent = ed.fade ? start : 'Switches on at ' + ed.time;
  $('edTo').textContent = ed.fade ? ed.time : '';
}

$('edTime').addEventListener('input', () => {
  ed.time = $('edTime').value;
  if (ed.time) renderEditor();
  else $('edSave').disabled = true;
});
$('edBright').addEventListener('input', () => {
  ed.brightness = Number($('edBright').value);
  renderEditor();
});
$('edTemp').addEventListener('input', () => {
  ed.temperature = Number($('edTemp').value);
  renderEditor();
});

function toggleDay(d) {
  ed.days ^= bit(d);
  renderEditor();
}

function setFade(fade) {
  ed.fade = fade;
  renderEditor();
}

async function saveSchedule() {
  if (!ed.days || !ed.time) return;
  const [hour, minute] = ed.time.split(':').map(Number);
  const payload = {
    days: ed.days, hour, minute,
    fade: ed.fade, brightness: ed.brightness, temperature: ed.temperature
  };
  if (ed.id !== null) payload.id = ed.id;

  $('edSave').disabled = true;
  if (await change('/save', payload, () => nextAlarmText('Saved'))) closeSheet('editor');
  $('edSave').disabled = false;
}

let deleteArmed = false, deleteTimer = 0;

function disarmDelete() {
  deleteArmed = false;
  clearTimeout(deleteTimer);
  $('edDelete').textContent = 'Delete schedule';
}

async function deleteSchedule() {
  if (!deleteArmed) {
    deleteArmed = true;
    $('edDelete').textContent = 'Tap again to delete';
    deleteTimer = setTimeout(disarmDelete, 3000);
    return;
  }
  disarmDelete();
  if (await change('/delete', { id: ed.id }, 'Schedule deleted')) closeSheet('editor');
}

// --- Next alarm only ---

function openNext() {
  const a = st.upcoming[0];
  const usual = a && a.schedule !== null ? st.schedules[a.schedule].time : null;
  nx = { date: a ? a.date : 0, usual };

  $('nxTitle').textContent = usual ? 'Next alarm only' : 'One-time alarm';
  $('nxTime').value = a ? a.time : '07:00';
  renderNextSheet();
  $('nextSheet').showModal();
}

function renderNextSheet() {
  const time = $('nxTime').value;
  let date = nx.date;
  if (!date) {
    // Like the ESP: today if that time is still ahead, else tomorrow
    const d = toDate(st.now.date);
    if (time && time <= st.now.time) d.setDate(d.getDate() + 1);
    date = toNumber(d);
  }
  $('nxDay').textContent = dayText(date);
  $('nxNote').textContent = nx.usual
    ? 'Only this alarm changes. Your schedule stays at ' + nx.usual + '.'
    : 'Rings once and doesn\'t repeat.';
  $('nxSave').disabled = !time;
}

$('nxTime').addEventListener('input', renderNextSheet);

async function saveNext() {
  const time = $('nxTime').value;
  if (!time) return;
  const [hour, minute] = time.split(':').map(Number);
  const payload = { hour, minute };
  if (nx.date) payload.date = nx.date;

  $('nxSave').disabled = true;
  if (await change('/next', payload, () => nextAlarmText('Saved'))) closeSheet('nextSheet');
  $('nxSave').disabled = false;
}

// --- Sheets and messages ---

function closeSheet(id) {
  $(id).close();
}

document.querySelectorAll('dialog').forEach(dialog => {
  // A tap next to the sheet closes it.
  dialog.addEventListener('click', e => { if (e.target === dialog) dialog.close(); });
  dialog.addEventListener('close', () => document.body.appendChild($('toast')));
});

let toastTimer = 0;

function toast(text, error) {
  const t = $('toast');
  // An open sheet covers everything else, so show it inside the sheet.
  (document.querySelector('dialog[open]') || document.body).appendChild(t);
  t.textContent = text;
  t.className = 'show' + (error ? ' error' : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = ''; }, 2800);
}

document.querySelectorAll('nav a').forEach(a => {
  if (a.getAttribute('href') === location.pathname) {
    a.classList.add('active');
    a.setAttribute('aria-current', 'page');
  }
});

refresh();
setInterval(() => { if (!document.hidden) refresh(); }, 15000);
document.addEventListener('visibilitychange', () => { if (!document.hidden) refresh(); });
</script>
</body>
</html>
)rawliteral";

}  // namespace

void sendAlarmPage(WebServer& server) {
  server.send_P(200, "text/html", ALARM_PAGE);
}
