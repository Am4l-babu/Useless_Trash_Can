/* ==========================================================================
   TRASHBOT OS - browser side.

   Two rules this file obeys, because the firmware depends on them:

     1. Hold to drive. While a control is held we repeat the intent every
        100 ms; on release we send an explicit stop. The ESP32 also stops on
        its own if we go quiet, so a dropped phone stops the robot even if
        this script never runs again.

     2. We display what the bin tells us, not what we asked for. The request
        and the execution are always shown as two separate values.
   ========================================================================== */
'use strict';

var DRIVE_REPEAT_MS = 100;
var PING_MS = 400;

var $ = function (id) { return document.getElementById(id); };

/* ----------------------------------------------------------------- state */
var ws = null;
var connected = false;
var intent = null;           // {dir:"forward"} or {x:.., y:..}
var lastVector = { x: 0, y: 0 };
var retry = 500;
var hwInfo = null;
var helloInfo = null;
var lastTelemetry = null;

/* ------------------------------------------------------------ web socket */
function connect() {
  ws = new WebSocket('ws://' + location.host + '/ws');

  ws.onopen = function () {
    connected = true;
    retry = 500;
    setLink(true);
  };

  ws.onclose = function () {
    connected = false;
    setLink(false);
    endIntent();               // never keep a control "held" across a drop
    setTimeout(connect, retry);
    retry = Math.min(retry * 2, 4000);
  };

  ws.onerror = function () { try { ws.close(); } catch (e) {} };

  ws.onmessage = function (ev) {
    var m;
    try { m = JSON.parse(ev.data); } catch (e) { return; }
    switch (m.type) {
      case 'hello':     onHello(m); break;
      case 'config':    onConfig(m); break;
      case 'telemetry': onTelemetry(m); break;
      case 'event':     onEvent(m); break;
      case 'command':   onCommand(m); break;
      case 'sound':     if (window.TrashAudio) TrashAudio.onSound(m, 'wifi'); break;
      case 'audio_changed': if (window.TrashAudio) TrashAudio.refresh(); break;
    }
  };
}

// Wi-Fi first. With the WebSocket down, audio.js can carry the same JSON
// over Bluetooth or USB if the user connected one. Returns true if anything
// took the message.
function send(obj) {
  var s = JSON.stringify(obj);
  if (ws && ws.readyState === 1) { ws.send(s); return true; }
  if (window.TrashAudio && TrashAudio.altSend) return TrashAudio.altSend(s);
  return false;
}

function setLink(up) {
  var el = $('link');
  el.classList.toggle('online', up);
  el.lastChild.nodeValue = up ? 'ONLINE' : 'OFFLINE';
  if (!up) {
    $('statusText').textContent = 'LINK LOST';
    $('faceCaption').textContent = 'OFFLINE';
  }
  if (window.TrashAudio) TrashAudio.renderLinks();
}

// The ping also tells the bin whether this device can make a noise.
setInterval(function () {
  send({ type: 'ping', snd: !!(window.TrashAudio && TrashAudio.ready()) });
}, PING_MS);

/* ------------------------------------------------------- drive intent */
setInterval(function () {
  if (!intent || !connected) return;
  var speed = parseInt($('speed').value, 10);
  if (intent.dir) send({ type: 'drive', dir: intent.dir, speed: speed });
  else send({ type: 'drive', x: intent.x, y: intent.y, speed: speed });
}, DRIVE_REPEAT_MS);

function startIntent(next) {
  intent = next;
  var speed = parseInt($('speed').value, 10);
  if (next.dir) send({ type: 'drive', dir: next.dir, speed: speed });
  else send({ type: 'drive', x: next.x, y: next.y, speed: speed });
}

function endIntent() {
  if (!intent) return;
  intent = null;
  send({ type: 'drive', dir: 'stop' });
  lastVector = { x: 0, y: 0 };
  $('knob').style.transform = '';
  $('stick').classList.remove('live');
  var held = document.querySelector('.dbtn.held');
  if (held) held.classList.remove('held');
}

// A locked screen or a backgrounded tab must not leave the bin driving.
document.addEventListener('visibilitychange', function () {
  if (document.hidden) endIntent();
});
window.addEventListener('blur', endIntent);

/* ---------------------------------------------------------------- stick */
(function () {
  var stick = $('stick'), knob = $('knob');
  var radius = 0, active = false;

  function toVector(ev) {
    var r = stick.getBoundingClientRect();
    var dx = ev.clientX - (r.left + r.width / 2);
    var dy = ev.clientY - (r.top + r.height / 2);
    var mag = Math.sqrt(dx * dx + dy * dy);
    if (mag > radius) { dx *= radius / mag; dy *= radius / mag; mag = radius; }
    knob.style.transform = 'translate(' + dx + 'px,' + dy + 'px)';
    // Screen y grows downwards; forward is up, so the sign flips here.
    return {
      x: Math.round(dx / radius * 100),
      y: Math.round(-dy / radius * 100)
    };
  }

  stick.addEventListener('pointerdown', function (ev) {
    ev.preventDefault();
    radius = stick.getBoundingClientRect().width / 2 - 30;
    active = true;
    stick.setPointerCapture(ev.pointerId);
    stick.classList.add('live');
    lastVector = toVector(ev);
    startIntent(lastVector);
  });

  stick.addEventListener('pointermove', function (ev) {
    if (!active) return;
    ev.preventDefault();
    lastVector = toVector(ev);
    intent = lastVector;
    updatePupils();
  });

  function release(ev) {
    if (!active) return;
    active = false;
    try { stick.releasePointerCapture(ev.pointerId); } catch (e) {}
    endIntent();
    updatePupils();
  }
  stick.addEventListener('pointerup', release);
  stick.addEventListener('pointercancel', release);
})();

/* ----------------------------------------------------------------- dpad */
var DIR_VECTORS = {
  forward: { x: 0, y: 100 },  backward: { x: 0, y: -100 },
  left: { x: -100, y: 0 },    right: { x: 100, y: 0 },
  fwd_left: { x: -70, y: 70 }, fwd_right: { x: 70, y: 70 },
  back_left: { x: -70, y: -70 }, back_right: { x: 70, y: -70 }
};

document.querySelectorAll('.dbtn').forEach(function (b) {
  if (b.dataset.stop) {
    b.addEventListener('click', function () { endIntent(); send({ type: 'drive', dir: 'stop' }); });
    return;
  }
  var dir = b.dataset.dir;
  b.addEventListener('pointerdown', function (ev) {
    ev.preventDefault();
    b.setPointerCapture(ev.pointerId);
    b.classList.add('held');
    lastVector = DIR_VECTORS[dir] || { x: 0, y: 0 };
    startIntent({ dir: dir });
    updatePupils();
  });
  function up(ev) {
    ev.preventDefault();
    b.classList.remove('held');
    endIntent();
    updatePupils();
  }
  b.addEventListener('pointerup', up);
  b.addEventListener('pointercancel', up);
});

/* -------------------------------------------------------------- buttons */
document.querySelectorAll('[data-action]').forEach(function (b) {
  b.addEventListener('click', function () {
    send({ type: 'action', action: b.dataset.action });
  });
});

document.querySelectorAll('.mbtn').forEach(function (b) {
  b.addEventListener('click', function () {
    send({ type: 'mode', mode: b.dataset.mode });
  });
});

$('estop').addEventListener('click', function () {
  endIntent();
  send({ type: 'estop' });
});
$('resetSafety').addEventListener('click', function () {
  send({ type: 'reset_safety' });
});

$('speed').addEventListener('input', function () {
  $('speedOut').textContent = this.value + '%';
});
$('speed').addEventListener('change', function () {
  send({ type: 'speed', value: parseInt(this.value, 10) });
});

/* ------------------------------------------------------------------ tabs */
document.querySelectorAll('.tab').forEach(function (t) {
  t.addEventListener('click', function () {
    document.querySelectorAll('.tab').forEach(function (x) { x.classList.remove('active'); });
    document.querySelectorAll('.panel').forEach(function (p) { p.classList.remove('active'); });
    t.classList.add('active');
    document.querySelector('.panel[data-panel="' + t.dataset.tab + '"]').classList.add('active');
  });
});

/* ----------------------------------------------------------- calibration */
function calSet(field, value) { send({ type: 'cal_set', field: field, value: value }); }

document.querySelectorAll('[data-jog]').forEach(function (b) {
  b.addEventListener('click', function () {
    send({ type: 'cal_jog', motor: b.dataset.jog, dir: b.dataset.jdir });
  });
});

$('invL').addEventListener('change', function () { calSet('left_invert', this.checked ? 1 : 0); });
$('invR').addEventListener('change', function () { calSet('right_invert', this.checked ? 1 : 0); });

[['maxDuty', 'max_duty'], ['minDuty', 'min_duty'], ['accel', 'accel'], ['trim', 'trim']]
  .forEach(function (pair) {
    var el = $(pair[0]), out = $(pair[0] + 'Out');
    el.addEventListener('input', function () { out.textContent = el.value; });
    el.addEventListener('change', function () { calSet(pair[1], parseInt(el.value, 10)); });
  });

$('saveCal').addEventListener('click', function () { calSet('save', 1); });

$('wifiForm').addEventListener('submit', function (ev) {
  ev.preventDefault();
  var body = 'ssid=' + encodeURIComponent($('ssid').value) +
             '&pass=' + encodeURIComponent($('pass').value);
  $('wifiResult').innerHTML = '<div><span>STATUS</span><b>saving...</b></div>';
  fetch('/api/wifi', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: body
  }).then(function (r) { return r.text(); })
    .then(function (t) {
      $('wifiResult').innerHTML = '<div><span>STATUS</span><b>' + esc(t) + '</b></div>';
    })
    .catch(function () {
      $('wifiResult').innerHTML = '<div><span>STATUS</span><b>request failed</b></div>';
    });
});

/* ------------------------------------------------------------------ face */
var MOUTHS = {
  NORMAL:     'M78 118 H142',
  HAPPY:      'M76 112 Q110 136 144 112',
  BORED:      'M80 120 H124',
  CONFUSED:   'M76 118 q11 -11 22 0 t22 0 t22 0',
  ANGRY:      'M76 128 Q110 106 144 128',
  REBELLIOUS: 'M78 124 Q112 110 142 126',
  SLEEPING:   'M86 116 q14 10 28 0',
  CHAOS:      'M74 116 l16 11 l14 -15 l16 13 l14 -11',
  PANIC:      'M88 108 h44 v22 h-44 z'
};

var FACE_STATE = {
  NORMAL:     { lid: 0,  browL: 0,   browR: 0,   show: 0 },
  HAPPY:      { lid: 0,  browL: -12, browR: 12,  show: 1 },
  BORED:      { lid: 20, browL: 6,   browR: -6,  show: 1 },
  CONFUSED:   { lid: 0,  browL: -18, browR: 6,   show: 1 },
  ANGRY:      { lid: 6,  browL: 22,  browR: -22, show: 1 },
  REBELLIOUS: { lid: 10, browL: 16,  browR: -16, show: 1 },
  SLEEPING:   { lid: 38, browL: 0,   browR: 0,   show: 0 },
  CHAOS:      { lid: 0,  browL: -20, browR: -20, show: 1 },
  PANIC:      { lid: 0,  browL: -26, browR: 26,  show: 1 }
};

var currentMood = 'NORMAL';

function setFace(mood) {
  if (!MOUTHS[mood]) mood = 'NORMAL';
  if (mood === currentMood) return;
  currentMood = mood;

  document.body.className = document.body.className
    .replace(/\bmood-\S+/g, '').trim() + ' mood-' + mood;

  $('mouth').setAttribute('d', MOUTHS[mood]);
  var s = FACE_STATE[mood];
  $('lidL').setAttribute('height', s.lid);
  $('lidR').setAttribute('height', s.lid);
  $('browL').style.opacity = s.show;
  $('browR').style.opacity = s.show;
  $('browL').style.transform = 'rotate(' + s.browL + 'deg)';
  $('browR').style.transform = 'rotate(' + s.browR + 'deg)';
  $('faceCaption').textContent = mood;
}

function updatePupils() {
  // The eyes follow the stick, not the motors: they show what you asked for,
  // which makes it more obvious when the bin does something else.
  var dx = Math.max(-1, Math.min(1, lastVector.x / 100)) * 7;
  var dy = -Math.max(-1, Math.min(1, lastVector.y / 100)) * 6;
  var t = 'translate(' + dx + 'px,' + dy + 'px)';
  $('pupilL').style.transform = t;
  $('pupilR').style.transform = t;
}

/* -------------------------------------------------------------- messages */
function onHello(m) {
  helloInfo = m;
  hwInfo = m.hw || {};
  $('version').textContent = 'v' + (m.version || '?');
  if (m.driveRepeatMs) DRIVE_REPEAT_MS = m.driveRepeatMs;
  renderHardware();
  renderDiag();
}

function onConfig(m) {
  if (!m.cal) return;
  var c = m.cal;
  $('invL').checked = !!c.left_invert;
  $('invR').checked = !!c.right_invert;
  setSlider('maxDuty', c.max_duty);
  setSlider('minDuty', c.min_duty);
  setSlider('accel', c.accel);
  setSlider('trim', c.trim);
}

function setSlider(id, v) {
  if (v === undefined || v === null) return;
  $(id).value = v;
  $(id + 'Out').textContent = v;
}

var TRAIT_ORDER = ['anger', 'trust', 'happiness', 'confusion', 'boredom', 'obedience', 'rebellion'];

function onTelemetry(m) {
  lastTelemetry = m;

  $('hdrMode').textContent = 'MODE ' + m.mode;
  $('hdrMood').textContent = 'MOOD ' + m.mood;
  setFace(m.mood);

  $('obedText').textContent = (m.traits ? m.traits.obedience : 0) + '%';
  $('statusText').textContent = m.estop ? 'EMERGENCY STOP'
                              : (m.inhibit ? m.inhibit : m.mode);
  $('actionText').textContent = describeAction(m);

  // Emergency stop banner
  $('estopBanner').hidden = !m.estop;
  $('estopReason').textContent = m.estopReason || '';

  document.body.classList.toggle('normal-live', !!m.normalGrace);

  // Mode buttons
  document.querySelectorAll('.mbtn').forEach(function (b) {
    b.classList.toggle('active', b.dataset.mode === String(m.mode).toLowerCase());
  });

  renderTraits(m.traits);
  renderMotor('mLeft', 'mLeftVal', m.leftMotor);
  renderMotor('mRight', 'mRightVal', m.rightMotor);

  // Lid is simulated until a servo exists; the UI must never imply otherwise.
  var note = $('lidNote');
  if (m.lid && m.lid.installed) {
    note.classList.remove('sim');
    note.textContent = 'LID: ' + (m.lid.open ? 'OPEN' : 'CLOSED');
  } else {
    note.classList.add('sim');
    note.textContent = 'LID: NOT INSTALLED — ' +
                       (m.lid && m.lid.open ? 'OPEN' : 'CLOSED') + ' (SIMULATED)';
  }

  renderLink(m);
  renderDiag();
  renderBorrowed(m);
  if (window.TrashVision) TrashVision.onTelemetry(m);
}

// The camera and the speaker are whichever phone is doing the job right now.
function renderBorrowed(m) {
  var v = m.vision || {};
  var vb = $('hwVision'), ab = $('hwAudio');
  var vLive = v.live && v.source !== 'none';
  vb.classList.toggle('live', !!vLive);
  vb.innerHTML = 'VISION<small>' + (vLive
    ? esc(v.source) + ' reporting · ' + (v.present ? v.persons + ' person' + (v.persons === 1 ? '' : 's') : 'nobody') +
      (v.objects ? ' · ' + esc(v.objects) : '')
    : 'no source reporting') + '</small>';
  ab.classList.toggle('live', !!m.audioReady);
  ab.innerHTML = 'AUDIO<small>' + (m.audioReady ? 'a phone has sound unlocked' : 'no phone has unlocked sound') + '</small>';
}

function describeAction(m) {
  if (m.estop) return 'MOTORS CUT';
  if (m.pendingCmd) return 'THINKING ABOUT IT';
  if (m.jog) return 'CALIBRATION JOG';
  var l = m.leftMotor || 0, r = m.rightMotor || 0;
  if (l === 0 && r === 0) return m.moodId === 6 ? 'ASLEEP' : 'IDLE';
  if (l > 0 && r > 0) return 'DRIVING FORWARD';
  if (l < 0 && r < 0) return 'DRIVING BACKWARD';
  if (l < r) return 'TURNING LEFT';
  return 'TURNING RIGHT';
}

function renderTraits(t) {
  if (!t) return;
  var box = $('traits');
  if (!box.children.length) {
    TRAIT_ORDER.forEach(function (k) {
      var row = document.createElement('div');
      row.className = 'trait ' + k;
      row.innerHTML = '<span>' + k.toUpperCase() + '</span>' +
                      '<div class="ttrack"><div class="tfill" id="tf-' + k + '"></div></div>' +
                      '<output id="tv-' + k + '">0%</output>';
      box.appendChild(row);
    });
  }
  TRAIT_ORDER.forEach(function (k) {
    var v = t[k] || 0;
    $('tf-' + k).style.width = v + '%';
    $('tv-' + k).textContent = v + '%';
  });
}

function renderMotor(fillId, valId, pct) {
  pct = Math.max(-100, Math.min(100, pct || 0));
  var el = $(fillId);
  var half = Math.abs(pct) / 2;
  el.style.left = (pct >= 0 ? 50 : 50 - half) + '%';
  el.style.width = half + '%';
  $(valId).textContent = pct + '%';
}

function renderLink(m) {
  var rows = [
    ['CLIENTS', m.clients],
    ['RSSI', m.rssi ? m.rssi + ' dBm' : 'n/a (AP mode)'],
    ['FREE HEAP', Math.round((m.heap || 0) / 1024) + ' KB'],
    ['UPTIME', formatUptime(m.uptime)],
    ['INHIBIT', m.inhibit || 'none']
  ];
  $('linkInfo').innerHTML = rows.map(function (r) {
    return '<div><span>' + r[0] + '</span><b>' + esc(String(r[1])) + '</b></div>';
  }).join('');
}

function renderDiag() {
  if (!helloInfo) return;
  var m = lastTelemetry || {};
  var rows = [
    ['FIRMWARE', 'v' + helloInfo.version],
    ['PERSONALITY SEED', helloInfo.seed],
    ['DRIVE TIMEOUT', helloInfo.driveTimeoutMs + ' ms'],
    ['DRIVE REPEAT', DRIVE_REPEAT_MS + ' ms'],
    ['MODE', m.mode || '-'],
    ['MOOD', m.mood || '-']
  ];
  $('diag').innerHTML = rows.map(function (r) {
    return '<div><span>' + r[0] + '</span><b>' + esc(String(r[1])) + '</b></div>';
  }).join('');
}

var HW_LABELS = {
  esp32: 'ESP32', l298n: 'L298N DRIVER', motorL: 'LEFT MOTOR', motorR: 'RIGHT MOTOR',
  ble: 'BLUETOOTH LINK', usb: 'USB SERIAL LINK', visionUart: 'CAMERA UART',
  lid: 'LID SERVO', camera: 'CAMERA', mic: 'MICROPHONE', leds: 'WS2812 LEDS',
  speaker: 'SPEAKER', display: 'DISPLAY', tof: 'ToF SENSORS', encoders: 'MOTOR ENCODERS',
  battery: 'BATTERY SENSE', finger: 'USELESS FINGER'
};

function renderHardware() {
  if (!hwInfo) return;
  var html = '';
  Object.keys(HW_LABELS).forEach(function (k) {
    var state = hwInfo[k] || 'not_installed';
    var cls = state === 'online' ? 'on' : (state === 'configured' ? 'cfg' : '');
    var text = state === 'see_telemetry' ? 'BORROWED FROM THE PHONE - SEE BELOW'
                                         : state.replace(/_/g, ' ').toUpperCase();
    html += '<div class="hwrow ' + cls + '"><i class="dot"></i>' +
            '<b>' + HW_LABELS[k] + '</b><em>' + text + '</em></div>';
  });
  $('hwlist').innerHTML = html;
}

function onEvent(m) {
  var box = $('log');
  var atBottom = box.scrollHeight - box.scrollTop - box.clientHeight < 40;
  var row = document.createElement('div');
  var cls = /SAFETY|REJECT|PANIC|CANCELLED/.test(m.code) ? 'safety'
          : (/MODIFIED|MOOD|MODE/.test(m.code) ? 'mod' : '');
  row.className = cls;
  row.innerHTML = '<span class="t">' + formatClock(m.ms) + '</span>' +
                  '<span class="c">' + esc(m.code) + '</span>' +
                  '<span class="m">' + esc(m.text) + '</span>';
  box.appendChild(row);
  while (box.children.length > 150) box.removeChild(box.firstChild);
  if (atBottom) box.scrollTop = box.scrollHeight;
}

function onCommand(m) {
  $('fbReq').textContent = m.requested;
  $('fbAct').textContent = m.phase === 'received' ? '...' : m.actual;

  var box = $('fbStatus');
  box.className = 'fb-status';

  if (m.phase === 'received' && m.delayMs > 0) {
    box.classList.add('waiting');
    box.textContent = 'COMMAND RECEIVED — holding ' + (m.delayMs / 1000).toFixed(1) + 's';
  } else if (m.phase === 'executing') {
    box.classList.add(m.verdict.toLowerCase());
    box.textContent = 'EXECUTING… ' + m.actual;
  } else {
    box.classList.add(m.verdict.toLowerCase());
    box.textContent = {
      OBEYED: 'STATUS: OBEYED',
      MODIFIED: 'STATUS: MODIFIED',
      IGNORED: 'COMMAND IGNORED',
      REJECTED: 'REJECTED BY SAFETY SYSTEM'
    }[m.verdict] || m.verdict;
    if (m.quip) box.textContent += '  ·  ' + m.quip;
  }

  if (m.simulated) box.textContent += '  [SIMULATED]';

  var why = $('whyBox');
  if (m.reason) {
    why.hidden = false;
    $('whyText').textContent = '“' + m.reason + '”';
  } else if (m.verdict === 'OBEYED') {
    why.hidden = true;
  }

  if (m.speedReq) {
    $('speedText').textContent = m.speedReq + '% / ' +
      (m.speedAct === undefined ? m.speedReq : m.speedAct) + '%';
  }

  if (m.requested === 'NORMAL MODE' && m.verdict === 'IGNORED') {
    var note = $('normalNote');
    note.textContent = (m.quip || '') + ' — ' + m.reason;
    setTimeout(function () { note.textContent = ''; }, 7000);
  }

  if (m.phase === 'done' || m.phase === 'executing') addHistory(m);
}

function addHistory(m) {
  var tbody = $('history');
  var empty = tbody.querySelector('.empty');
  if (empty) tbody.removeChild(empty);
  var tr = document.createElement('tr');
  tr.className = m.verdict.toLowerCase();
  tr.innerHTML = '<td>' + esc(m.requested) + '</td><td>→</td><td>' +
                 esc(m.verdict === 'IGNORED' ? 'nothing' : m.actual) + '</td>';
  tbody.insertBefore(tr, tbody.firstChild);
  while (tbody.children.length > 12) tbody.removeChild(tbody.lastChild);
}

/* ----------------------------------------------------------------- utils */
function esc(s) {
  return String(s).replace(/[&<>"]/g, function (c) {
    return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
  });
}

function formatClock(ms) {
  var t = Math.floor(ms / 1000);
  var h = Math.floor(t / 3600) % 24, m = Math.floor(t / 60) % 60, s = t % 60;
  return ('0' + h).slice(-2) + ':' + ('0' + m).slice(-2) + ':' + ('0' + s).slice(-2);
}

function formatUptime(ms) {
  var t = Math.floor((ms || 0) / 1000);
  var h = Math.floor(t / 3600), m = Math.floor(t / 60) % 60, s = t % 60;
  return (h ? h + 'h ' : '') + (m ? m + 'm ' : '') + s + 's';
}

/* ------------------------------------------------------------------ boot */
setFace('NORMAL');
updatePupils();
connect();
