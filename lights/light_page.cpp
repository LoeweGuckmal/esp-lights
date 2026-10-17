#include "light_page.h"

namespace {

// %TITLE%, %BASE% and %TEMP_HIDDEN% are filled in by sendLightPage().
const char LIGHT_PAGE[] = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%TITLE%</title>
<style>
  * { box-sizing: border-box; }
  body {
    margin: 0; padding: 24px;
    font: 16px system-ui, sans-serif;
    background: #111318; color: #eee;
  }
  main {
    max-width: 380px; margin: 30px auto;
    padding: 24px; border-radius: 18px;
    background: #20232b;
  }
  h2 { margin-top: 0; }
  .row { display: flex; gap: 8px; margin-bottom: 24px; }
  button, input {
    font: inherit; border-radius: 9px;
  }
  .row button {
    flex: 1; padding: 10px; border: 0;
    background: #343844; color: #eee; cursor: pointer;
  }
  .row button.active, #apply {
    background: #7c83ff; color: white;
  }
  .field { margin: 18px 0; }
  .field label {
    display: flex; justify-content: space-between;
    margin-bottom: 8px;
  }
  input[type=range] { width: 100%; accent-color: #8c92ff; }
  input[type=number] {
    width: 76px; padding: 6px;
    background: #111318; color: white;
    border: 1px solid #454958;
  }
  #preview {
    height: 65px; border-radius: 12px;
    margin: 20px 0; background: #000;
    border: 1px solid #454958;
  }
  #apply {
    width: 100%; padding: 12px;
    border: 0; cursor: pointer;
  }
  #status { color: #b8bbc8; font-size: 14px; }
</style>
</head>
<body>
<main>
  <h2>%TITLE%</h2>
  <div class="row">
    <button onclick="send('/on')">On</button>
    <button onclick="send('/off')">Off</button>
  </div>
  <div class="row">
    <button id="rgbMode" class="active" onclick="setMode('rgb')">RGB</button>
    <button id="whiteMode" onclick="setMode('white')">White</button>
  </div>

  <section id="rgbFields">
    <div class="field">
      <label>Red <input id="rNum" type="number" min="0" max="255" value="255"></label>
      <input id="r" type="range" min="0" max="255" value="255">
    </div>
    <div class="field">
      <label>Green <input id="gNum" type="number" min="0" max="255" value="0"></label>
      <input id="g" type="range" min="0" max="255" value="0">
    </div>
    <div class="field">
      <label>Blue <input id="bNum" type="number" min="0" max="255" value="0"></label>
      <input id="b" type="range" min="0" max="255" value="0">
    </div>
  </section>

  <section id="whiteFields" hidden>
    <div class="field" %TEMP_HIDDEN%>
      <label>Temperature (cold to warm) <input id="tNum" type="number" min="0" max="100" value="50"></label>
      <input id="t" type="range" min="0" max="100" value="50">
    </div>
  </section>

  <div class="field">
    <label>Brightness <input id="lNum" type="number" min="1" max="100" value="100"></label>
    <input id="l" type="range" min="1" max="100" value="100">
  </div>

  <div id="preview"></div>
  <button id="apply" onclick="applyColor()">Apply</button>
  <p id="status">Ready</p>
</main>

<script>
const base = '%BASE%';
let mode = 'rgb';

// Current value of an input, clamped to its min/max.
function value(id) {
  const el = document.getElementById(id);
  const v = Math.round(Number(el.value) || 0);
  return Math.max(Number(el.min), Math.min(Number(el.max), v));
}

function sync(id, source) {
  const v = value(source);
  document.getElementById(id).value = v;
  document.getElementById(source).value = v;
  updatePreview();
}

['r', 'g', 'b', 't', 'l'].forEach(id => {
  document.getElementById(id).addEventListener('input', () =>
    sync(id + 'Num', id));
  document.getElementById(id + 'Num').addEventListener('input', () =>
    sync(id, id + 'Num'));
});

function setMode(next) {
  mode = next;
  document.getElementById('rgbFields').hidden = mode !== 'rgb';
  document.getElementById('whiteFields').hidden = mode !== 'white';
  document.getElementById('rgbMode').classList.toggle('active', mode === 'rgb');
  document.getElementById('whiteMode').classList.toggle('active', mode === 'white');
  updatePreview();
}

function updatePreview() {
  let c;
  if (mode === 'rgb') {
    c = [value('r'), value('g'), value('b')];
  } else {
    const cold = [200, 220, 255], warm = [255, 180, 100];
    const t = value('t') / 100;
    c = cold.map((v, i) => v + (warm[i] - v) * t);
  }
  const l = value('l') / 100;

  document.getElementById('preview').style.background =
    `rgb(${c.map(v => Math.round(v * l)).join(',')})`;
}

async function send(path, payload) {
  const status = document.getElementById('status');
  status.textContent = 'Sending...';

  try {
    const response = await fetch(base + path, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload || {})
    });
    const data = await response.json();
    if (!data.ok) throw new Error(data.error || 'HTTP ' + response.status);
    status.textContent = 'Done';
  } catch (e) {
    status.textContent = 'Error: ' + e.message;
  }
}

function applyColor() {
  if (mode === 'rgb') {
    send('/rgb', { r: value('r'), g: value('g'), b: value('b'), brightness: value('l') });
  } else {
    send('/white', { temperature: value('t'), brightness: value('l') });
  }
}

async function showStatus() {
  const status = document.getElementById('status');
  try {
    const data = await (await fetch(base + '/status')).json();
    status.textContent = !data.connected ? 'Lamp not connected'
      : 'Connected, lamp is ' + (data.state.is_on ? 'on' : 'off');
  } catch (e) {
    status.textContent = 'Error: ' + e.message;
  }
}

updatePreview();
showStatus();
</script>
</body>
</html>
)rawliteral";

}  // namespace

void sendLightPage(WebServer& server, const char* title, const char* basePath,
                   bool hasTemperature) {
  String page = LIGHT_PAGE;
  page.replace("%TITLE%", title);
  page.replace("%BASE%", basePath);
  page.replace("%TEMP_HIDDEN%", hasTemperature ? "" : "hidden");
  server.send(200, "text/html", page);
}
