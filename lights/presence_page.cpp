#include "presence_page.h"

namespace {

const char PRESENCE_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#111318">
<title>Presence</title>
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
  button:focus-visible { outline: 2px solid #b9bdff; outline-offset: 2px; }
  .card {
    background: #20232b; border-radius: 20px;
    padding: 18px; margin-bottom: 12px;
  }
  .muted { color: #8a8ea0; font-size: 13px; line-height: 1.5; }
  #now { padding: 22px; }
  .big {
    margin-top: 14px;
    font-size: clamp(44px, 15vw, 60px); font-weight: 200; line-height: 1.05;
    letter-spacing: -1px; font-variant-numeric: tabular-nums;
  }
  .big.dim { color: #5d6173; }
  .sub { margin: 8px 0 0; color: #b8bbc8; font-size: 14px; line-height: 1.5; }
  .actions { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 18px; }
  .actions:empty { display: none; }
  .actions button { flex: 1 1 7.5em; padding: 11px 8px; background: #343844; }
  .actions button.primary { background: #7c83ff; color: #fff; }
  .actions button.danger { background: rgba(255, 122, 107, .12); color: #ff9c90; }

  /* Signal meter: -100 dBm on the left, -40 dBm on the right */
  .meter { margin-top: 18px; }
  .meter[hidden] { display: none; }
  .track {
    position: relative; height: 8px; border-radius: 4px;
    background: #343844;
  }
  .track span {
    display: block; height: 100%; border-radius: 4px;
    background: linear-gradient(90deg, #ff9a5a, #7c83ff);
    transition: width .4s;
  }
  .track i {
    position: absolute; top: -4px; width: 2px; height: 16px;
    margin-left: -1px; border-radius: 1px; background: #e9eaf2;
  }
  .track i[hidden] { display: none; }
  .ends {
    display: flex; justify-content: space-between;
    margin-top: 6px; font-size: 12px; color: #8a8ea0;
  }
  .ends b { color: #d4d6e0; font-weight: 500; }

  /* Pairing steps */
  .steps { margin: 12px 0 0; padding-left: 22px; color: #d4d6e0; font-size: 14px; line-height: 1.55; }
  .steps li { margin-bottom: 6px; }
  .steps b { color: #fff; font-weight: 600; }
  .countdown { margin: 10px 0 0; font-variant-numeric: tabular-nums; }

  #note { margin: 22px 6px 0; }
  #foot { text-align: center; margin-top: 18px; }

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
    #now { padding: 18px; }
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
    <h2>Presence</h2>
    <label class="switch" title="Presence checks on or off">
      <input id="master" type="checkbox" disabled aria-label="Presence checks">
      <span></span>
    </label>
  </header>

  <section id="now" class="card" aria-live="polite">
    <h3>Your phone</h3>
    <div id="state" class="big dim">--</div>
    <p id="detail" class="sub">Loading...</p>
    <div id="meter" class="meter" hidden>
      <div class="track"><span id="bar"></span><i id="limit" hidden></i></div>
      <div class="ends"><span>-100 dBm</span><b id="rssi"></b><span>-40 dBm</span></div>
    </div>
    <div id="checkActions" class="actions"></div>
  </section>

  <section id="phoneCard" class="card"></section>

  <p id="note" class="muted"></p>
  <p id="foot" class="muted"></p>
</main>

<div id="toast" role="status"></div>

<script>
const $ = id => document.getElementById(id);
let st = null;          // latest status from the ESP
let wasPairing = false;
let cancelled = false;  // pairing cancelled here, no message needed

// --- Talking to the ESP ---

// Every route answers with the full status.
async function call(path, payload) {
  const response = await fetch('/presence' + path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload || {})
  });
  const data = await response.json();
  if (!data.ok) throw new Error(data.error || 'HTTP ' + response.status);
  st = data;
  $('foot').textContent = '';
  render();
}

// Sends a change, then shows `done`. Returns true on success.
async function change(path, payload, done) {
  try {
    await call(path, payload);
    if (done) toast(done);
    return true;
  } catch (e) {
    render();  // flips the switch back
    toast(e.message, true);
    return false;
  }
}

let timer = 0;

// Every second while something runs, so the signal and countdowns stay live.
function schedule() {
  clearTimeout(timer);
  const live = st && (st.pairing || (st.check && st.check.running));
  timer = setTimeout(refresh, live ? 1000 : 15000);
}

async function refresh() {
  if (document.hidden) return;  // visibilitychange starts it again
  try {
    await call('/status');
  } catch (e) {
    $('foot').textContent = 'ESP not reachable';
    schedule();
  }
}

// --- Text helpers ---

const esc = s => String(s).replace(/[&<>"]/g, c =>
  ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);

function agoText(s) {
  if (s < 10) return 'just now';
  if (s < 60) return s + ' s ago';
  if (s < 3600) return Math.floor(s / 60) + ' min ago';
  return Math.floor(s / 3600) + ' h ago';
}

function strength(rssi) {
  if (rssi >= -60) return 'strong';
  if (rssi >= -75) return 'good';
  if (rssi >= -85) return 'weak';
  return 'very weak';
}

const toMeter = rssi => Math.max(0, Math.min(100, (rssi + 100) / 60 * 100));

// --- Rendering ---

function render() {
  if (!st) return;
  $('master').disabled = false;
  $('master').checked = st.enabled;

  renderNow();
  renderPhone();
  renderNote();

  // Pairing ended by itself: paired, or the window closed.
  if (wasPairing && !st.pairing && !cancelled) {
    toast(st.pair_ok ? 'Phone paired' : 'Pairing window closed, no phone paired', !st.pair_ok);
  }
  if (!st.pairing) cancelled = false;
  wasPairing = !!st.pairing;

  schedule();
}

function renderNow() {
  const c = st.check;
  const limited = st.min_rssi > -100;
  let state = '--', dim = true, detail = '', rssi = null, actions = '';

  if (!st.enabled) {
    state = 'Off';
    detail = 'The ESP doesn’t look for your phone, and alarms ring as usual.';
  } else if (!st.phone) {
    detail = 'Pair your phone below first.';
  } else if (c && c.running) {
    if (c.reason === 'watch') {
      state = c.seen ? c.rssi + ' dBm' : 'Listening';
      dim = !c.seen;
      rssi = c.seen ? c.rssi : null;
      detail = (c.seen ? 'Heard ' + agoText(c.heard_ago_s) + '. ' : 'Not heard yet. ') +
               'Walk around to see the signal change. ' + c.left_s + ' s left.';
    } else {
      state = 'Looking';
      detail = 'Listening for your phone' + (c.reason === 'alarm' ? ' before an alarm' : '') +
               ', at most ' + c.left_s + ' s more.';
    }
    actions = '<button onclick="stopCheck()">Stop</button>';
  } else {
    if (c) {
      dim = !c.found;
      rssi = c.seen ? c.best_rssi : null;
      const when = (c.reason === 'watch' ? 'Watched ' : 'Checked ') + agoText(c.ago_s) +
                   (c.reason === 'alarm' ? ' before an alarm' : '') + '. ';
      if (c.found) {
        state = 'Here';
        detail = when + 'Strongest signal ' + c.best_rssi + ' dBm.';
      } else if (c.seen) {
        state = 'Too far';
        detail = when + 'Heard at ' + c.best_rssi + ' dBm at most, weaker than the ' +
                 st.min_rssi + ' dBm limit.';
      } else {
        state = 'Not found';
        detail = when + 'Your phone wasn’t heard.';
      }
      if (!c.found && c.reason === 'alarm') detail += ' The alarm didn’t ring.';
    } else {
      detail = 'Not checked since the ESP started.';
    }
    actions = '<button class="primary" onclick="check(false)">Check now</button>' +
              '<button onclick="check(true)">Watch signal</button>';
  }

  $('state').textContent = state;
  $('state').className = 'big' + (dim ? ' dim' : '');
  $('detail').textContent = detail;
  $('checkActions').innerHTML = actions;

  $('meter').hidden = rssi === null;
  if (rssi !== null) {
    $('bar').style.width = toMeter(rssi) + '%';
    $('rssi').textContent = rssi + ' dBm, ' + strength(rssi);
    $('limit').hidden = !limited;
    $('limit').style.left = toMeter(st.min_rssi) + '%';
  }
}

function renderPhone() {
  const card = $('phoneCard');
  const name = '<b>' + esc(st.device_name) + '</b>';

  if (st.pairing) {
    const m = Math.floor(st.pairing / 60), s = String(st.pairing % 60).padStart(2, '0');
    card.innerHTML = `
      <h3>Pairing</h3>
      <ol class="steps">
        <li>On your iPhone, open a Bluetooth app like <b>nRF Connect</b> or <b>LightBlue</b> (both free).</li>
        <li>Connect to ${name}.</li>
        <li>Tap <b>Pair</b> when the iPhone asks. No prompt? Open the service and read its characteristic.</li>
      </ol>
      <p class="muted countdown">Ready for ${m}:${s}</p>
      <div class="actions"><button onclick="cancelPair()">Cancel</button></div>`;
    return;
  }

  const off = st.enabled ? '' : ' disabled';
  const hint = st.enabled ? '' : '<p class="muted">Turn presence on to pair.</p>';
  if (st.phone) {
    card.innerHTML = `
      <h3>Phone</h3>
      <p class="sub">Paired. The ESP recognizes it, even though it changes its Bluetooth address.</p>
      ${hint}
      <div class="actions">
        <button onclick="pair()"${off}>Pair again</button>
        <button class="danger" onclick="forget()">Forget</button>
      </div>`;
  } else {
    card.innerHTML = `
      <h3>Phone</h3>
      <p class="sub">No phone paired yet. Pair once, so the ESP can recognize your iPhone.</p>
      ${hint}
      <div class="actions"><button class="primary" onclick="pair()"${off}>Pair phone</button></div>`;
  }
}

function renderNote() {
  const lead = Math.round(st.lead_s / 60);
  $('note').hidden = !st.enabled;
  $('note').textContent =
    'Checks only run when needed: ' + lead + ' min before each alarm, and when you tap ' +
    'Check now. A check listens for at most ' + st.scan_s + ' s and stops as soon as ' +
    'your phone is heard. If it isn’t found, that alarm doesn’t ring.' +
    (st.min_rssi > -100 ? ' Signals weaker than ' + st.min_rssi + ' dBm count as not here.' : '');
}

// --- Actions ---

$('master').addEventListener('change', e => {
  const on = e.target.checked;
  change(on ? '/on' : '/off', null, on ? 'Presence on' : 'Presence off: alarms ring as usual');
});

const check = watch => change('/check', watch ? { watch: 1 } : null);
const stopCheck = () => change('/stop');
const pair = () => { cancelled = false; change('/pair'); };
const cancelPair = () => { cancelled = true; change('/pair/cancel'); };

function forget() {
  if (!confirm('Forget the paired phone?')) return;
  change('/forget', null,
         'Forgotten. Also remove ' + st.device_name + ' in the iPhone’s Bluetooth settings.');
}

// --- Messages ---

let toastTimer = 0;

function toast(text, error) {
  const t = $('toast');
  t.textContent = text;
  t.className = 'show' + (error ? ' error' : '');
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = ''; }, 3500);
}

document.querySelectorAll('nav a').forEach(a => {
  if (a.getAttribute('href') === location.pathname) {
    a.classList.add('active');
    a.setAttribute('aria-current', 'page');
  }
});

refresh();
document.addEventListener('visibilitychange', () => { if (!document.hidden) refresh(); });
</script>
</body>
</html>
)rawliteral";

}  // namespace

void sendPresencePage(WebServer& server) {
  server.send_P(200, "text/html", PRESENCE_PAGE);
}
