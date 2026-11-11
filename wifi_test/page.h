#pragma once

#include <Arduino.h>

// The page of the Wi-Fi test. Its script asks /data once a second and
// measures how long the answer takes. Every request is a new connection,
// like one from a shortcut.
const char PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Wi-Fi test</title>
<style>
  body { font: 16px system-ui, sans-serif; margin: 0; padding: 16px; background: #111; color: #eee; }
  main { max-width: 480px; margin: auto; }
  h1 { font-size: 18px; font-weight: 600; margin: 0 0 16px; }
  h2 { font-size: 14px; font-weight: 600; color: #999; margin: 24px 0 4px; }
  .big { font-size: 56px; font-weight: 700; line-height: 1; }
  .big small { font-size: 20px; font-weight: 400; color: #999; }
  .row { display: flex; justify-content: space-between; gap: 16px; padding: 8px 0; border-top: 1px solid #333; }
  .row span:first-child { color: #999; }
  .row span:last-child { text-align: right; }
  #bars { display: flex; align-items: flex-end; gap: 2px; height: 80px; margin: 8px 0; }
  #bars i { flex: 1; background: #4caf50; }
  #bars i.slow { background: #ffb300; }
  #bars i.fail { background: #e53935; height: 100%; }
  p { color: #999; font-size: 14px; margin: 4px 0; }
</style>
</head>
<body>
<main>
  <h1>Wi-Fi test</h1>
  <div class="big"><span id="rssi">–</span> <small>dBm, <span id="rating">…</span></small></div>

  <h2>This device → ESP, one request per second</h2>
  <div id="bars"></div>
  <p>Green: answered within 0.5 s. Yellow: slower. Red: no answer within 5 s.</p>
  <div class="row"><span>Last</span><span id="last">–</span></div>
  <div class="row"><span>Average</span><span id="avg">–</span></div>
  <div class="row"><span>Slowest</span><span id="slowest">–</span></div>
  <div class="row"><span>No answer</span><span id="failed">–</span></div>

  <h2>ESP → router, two pings per second</h2>
  <div class="row"><span>Answered</span><span id="ping">–</span></div>
  <div class="row"><span>Average</span><span id="pingavg">–</span></div>

  <h2>Connection</h2>
  <div class="row"><span>Access point</span><span id="ap">–</span></div>
  <div class="row"><span>Lost</span><span id="drops">–</span></div>
  <div class="row"><span>Running for</span><span id="uptime">–</span></div>
  <div class="row"><span>Sending power</span><span id="tx">–</span></div>
  <div class="row"><span>Power saving</span><span id="saving">–</span></div>
</main>
<script>
const $ = id => document.getElementById(id);
const bars = [];
let total = 0, failed = 0, sum = 0, slowest = 0;

function draw() {
  $('bars').innerHTML = bars.map(ms => ms < 0
    ? '<i class="fail"></i>'
    : '<i class="' + (ms > 500 ? 'slow' : '') + '" style="height:' +
      Math.max(3, Math.min(100, ms / 20)) + '%"></i>').join('');
  const answered = total - failed;
  $('avg').textContent = answered ? Math.round(sum / answered) + ' ms' : '–';
  $('slowest').textContent = answered ? slowest + ' ms' : '–';
  $('failed').textContent = failed + ' of ' + total;
}

function show(d) {
  $('rssi').textContent = d.rssi;
  $('rating').textContent = d.rating;
  $('ap').textContent = d.ap + ', channel ' + d.channel;
  $('drops').textContent = d.drops + (d.drops == 1 ? ' time' : ' times');
  $('uptime').textContent = Math.floor(d.uptime / 60) + ' min ' + d.uptime % 60 + ' s';
  $('tx').textContent = d.tx + ' dBm';
  $('saving').textContent = d.saving ? 'on' : 'off';
  const lost = d.ping_done ? (100 * (d.ping_done - d.ping_back) / d.ping_done).toFixed(1) : 0;
  $('ping').textContent = d.ping_back + ' of ' + d.ping_done + ' (' + lost + ' % lost)';
  $('pingavg').textContent = d.ping_back ? d.ping_avg + ' ms' : '–';
}

async function tick() {
  const start = performance.now();
  const stop = new AbortController();
  const timer = setTimeout(() => stop.abort(), 5000);
  total++;
  try {
    const answer = await fetch('/data', { signal: stop.signal, cache: 'no-store' });
    const d = await answer.json();
    const ms = Math.round(performance.now() - start);
    sum += ms;
    slowest = Math.max(slowest, ms);
    bars.push(ms);
    $('last').textContent = ms + ' ms';
    show(d);
  } catch (e) {
    failed++;
    bars.push(-1);
    $('last').textContent = 'no answer';
  }
  clearTimeout(timer);
  if (bars.length > 60) bars.shift();
  draw();
  setTimeout(tick, 1000);
}

tick();
</script>
</body>
</html>
)rawliteral";
