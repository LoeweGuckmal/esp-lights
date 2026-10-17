#pragma once

#include <Arduino.h>

// Control page served at GET /led
const char LED_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>LED Control</title>
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
  .modes { display: flex; gap: 8px; margin-bottom: 24px; }
  button, input {
    font: inherit; border-radius: 9px;
  }
  .modes button {
    flex: 1; padding: 10px; border: 0;
    background: #343844; color: #eee;
  }
  .modes button.active, #apply {
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
  <h2>LED Control</h2>
  <div class="modes">
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
    <div class="field">
      <label>White intensity <input id="wNum" type="number" min="0" max="255" value="255"></label>
      <input id="w" type="range" min="0" max="255" value="255">
    </div>
  </section>

  <div id="preview"></div>
  <button id="apply" onclick="applyColor()">Apply</button>
  <p id="status">Ready</p>
</main>

<script>
let mode = 'rgb';

function value(id) {
  return Math.max(0, Math.min(255,
    Number(document.getElementById(id).value) || 0));
}

function sync(id, source) {
  const v = value(source);
  document.getElementById(id).value = v;
  document.getElementById(source).value = v;
  updatePreview();
}

['r', 'g', 'b', 'w'].forEach(id => {
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
  const c = mode === 'rgb'
    ? [value('r'), value('g'), value('b')]
    : [value('w'), value('w'), value('w')];

  document.getElementById('preview').style.background =
    `rgb(${c.join(',')})`;
}

async function applyColor() {
  const payload = mode === 'rgb'
    ? { r: value('r'), g: value('g'), b: value('b') }
    : { white: value('w') };

  const status = document.getElementById('status');
  status.textContent = 'Sending...';

  try {
    const response = await fetch('/led/color', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });

    if (!response.ok) throw new Error('HTTP ' + response.status);
    status.textContent = 'Color applied successfully';
  } catch (e) {
    status.textContent = 'Error: ' + e.message;
  }
}

updatePreview();
</script>
</body>
</html>
)rawliteral";
