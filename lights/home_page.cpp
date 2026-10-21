#include "home_page.h"

namespace {

const char HOME_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#111318">
<title>Lights</title>
<style>
  * { box-sizing: border-box; }
  html { color-scheme: dark; -webkit-text-size-adjust: 100%; text-size-adjust: 100%; }
  body {
    margin: 0; padding: 16px 16px 48px;
    font: 16px system-ui, -apple-system, sans-serif;
    background: #111318; color: #eee;
    -webkit-tap-highlight-color: transparent;
  }
  nav { display: flex; flex-wrap: wrap; justify-content: center; gap: 4px; }
  nav a {
    padding: 7px 14px; border-radius: 999px;
    color: #b8bbc8; text-decoration: none; font-size: 14px;
  }
  main { max-width: 420px; margin: 22px auto 0; }
  .card {
    background: #20232b; border-radius: 20px;
    padding: 18px; margin-bottom: 12px;
  }
  .head { display: flex; align-items: baseline; justify-content: space-between; gap: 12px; }
  h2 { margin: 0; font-size: 20px; font-weight: 600; }
  .state { color: #8a8ea0; font-size: 14px; text-align: right; }
  .state.error { color: #ff9c90; }
  .row { display: flex; gap: 8px; margin-top: 14px; }
  .row button {
    flex: 1; padding: 14px 8px;
    font: inherit; color: inherit; border: 0; border-radius: 12px;
    background: #343844; cursor: pointer;
  }
  .row button.active { background: #7c83ff; color: #fff; }
  .row button:disabled { opacity: .45; cursor: default; }
  .row button:focus-visible { outline: 2px solid #b9bdff; outline-offset: 2px; }
  @media (max-width: 360px) {
    body { padding-left: 12px; padding-right: 12px; }
    .card { padding: 16px; }
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
  <section class="card" data-base="/led">
    <div class="head"><h2>LED Strip</h2><span class="state" aria-live="polite"></span></div>
    <div class="row">
      <button type="button" data-path="/on">On</button>
      <button type="button" data-path="/off">Off</button>
    </div>
  </section>

  <section class="card" data-base="/room">
    <div class="head"><h2>Room Light</h2><span class="state" aria-live="polite"></span></div>
    <div class="row">
      <button type="button" data-path="/on">On</button>
      <button type="button" data-path="/off">Off</button>
    </div>
  </section>
</main>

<script>
const cards = [...document.querySelectorAll('.card')];

// on: true, false or null (unknown). The matching button is highlighted.
function show(card, text, on, error) {
  const state = card.querySelector('.state');
  state.textContent = text;
  state.classList.toggle('error', !!error);
  card.querySelectorAll('button').forEach(b =>
    b.classList.toggle('active', on !== null && (b.dataset.path === '/on') === on));
}

let followUp = 0;

async function send(card, path) {
  // A status that is still on its way is older than this command.
  clearTimeout(followUp);
  card.sent = (card.sent || 0) + 1;

  const buttons = card.querySelectorAll('button');
  buttons.forEach(b => { b.disabled = true; });
  show(card, 'Sending...', null);
  try {
    // Connects first if needed, which can take a few seconds.
    const response = await fetch(card.dataset.base + path, { method: 'POST' });
    const data = await response.json();
    if (response.status === 503) throw new Error('Not connected');
    if (!data.ok) throw new Error(data.error || 'HTTP ' + response.status);
    show(card, path === '/on' ? 'On' : 'Off', path === '/on');
  } catch (e) {
    show(card, e.message, null, true);
  } finally {
    buttons.forEach(b => { b.disabled = false; });
  }
}

// Returns false if the light is not connected.
async function load(card) {
  const sent = card.sent;
  try {
    const data = await (await fetch(card.dataset.base + '/status')).json();
    if (card.sent !== sent) return true;  // a command was sent meanwhile
    if (!data.connected) {
      show(card, 'Not connected', null);
      return false;
    }
    // The LED strip has no state until its first status packet, the room
    // light sends null while it doesn't know.
    const s = data.state;
    const known = card.dataset.base === '/led' ? s.received : s.status !== null;
    show(card, known ? (s.is_on ? 'On' : 'Off') : 'Connected', known ? s.is_on : null);
    return true;
  } catch (e) {
    if (card.sent === sent) show(card, 'ESP not reachable', null, true);
    return true;
  }
}

// One light after the other: the ESP answers one request at a time.
// lookAgain: load once more a bit later. The LED strip answers /status
// with its last packet and sends a fresh one right after, and asking for
// the status starts connecting a light that isn't connected.
async function loadAll(lookAgain) {
  clearTimeout(followUp);
  let allConnected = true;
  for (const card of cards) {
    if (!await load(card)) allConnected = false;
  }
  if (lookAgain) followUp = setTimeout(() => loadAll(false), allConnected ? 1500 : 6000);
}

cards.forEach(card => {
  card.querySelectorAll('button').forEach(b =>
    b.addEventListener('click', () => send(card, b.dataset.path)));
});

loadAll(true);
document.addEventListener('visibilitychange', () => { if (!document.hidden) loadAll(true); });
</script>
</body>
</html>
)rawliteral";

}  // namespace

void registerHomeRoute(WebServer& server) {
  server.on("/", HTTP_GET, [&server]() { server.send_P(200, "text/html", HOME_PAGE); });
}
