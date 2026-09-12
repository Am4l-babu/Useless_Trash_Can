/* ==========================================================================
   TRASHBOT OS - the phone is the eyes.

   Detection runs here, in the browser, with TensorFlow.js and the COCO-SSD
   model (people plus 80 everyday objects). Frames come from this device's
   camera, or from an ESP32-CAM's /capture endpoint polled a few times a
   second. Only the RESULT goes to the bin:

     {"type":"vision","persons":1,"objects":"cup,bottle","conf":83,"src":"phone"}

   The bin decides what is news (a person arriving, a cup it has not seen
   for a while) and reacts. This file never sends a frame anywhere.

   The model is fetched from the internet the first time (~6 MB) and lives in
   the browser cache after that. On the bin's own access point the phone has
   no internet unless it keeps mobile data on - so load it once at home.
   ========================================================================== */
'use strict';

(function () {
  var $ = function (id) { return document.getElementById(id); };
  var V = window.TrashVision = {};

  var TF_URL   = 'https://cdn.jsdelivr.net/npm/@tensorflow/tfjs@4.22.0/dist/tf.min.js';
  var COCO_URL = 'https://cdn.jsdelivr.net/npm/@tensorflow-models/coco-ssd@2.2.3/dist/coco-ssd.min.js';

  var INTERVAL_MS  = 250;      // detection cadence; the model itself takes 60-300 ms on a phone
  var HEARTBEAT_MS = 1000;     // resend an unchanged result this often so the bin knows we are alive
  var MIN_SCORE    = 0.45;
  var MAX_W        = 480;      // downscale before inference

  var model = null, running = false, source = 'phone';
  var stream = null, timer = null, busy = false;
  var video = $('camVideo'), img = $('camImg'), canvas = $('camCanvas');
  var ctx2d = canvas.getContext('2d', { willReadFrequently: true });
  var last = { key: '', at: 0 }, frames = 0, fpsAt = 0, fps = 0;
  var lastSeen = [];
  var secure = !!window.isSecureContext;

  /* ------------------------------------------------------------ loading */
  function loadScript(url) {
    return new Promise(function (resolve, reject) {
      var s = document.createElement('script');
      s.src = url; s.async = true;
      s.onload = resolve;
      s.onerror = function () { reject(new Error('could not load ' + url)); };
      document.head.appendChild(s);
    });
  }

  function loadModel() {
    if (model) return Promise.resolve(model);
    setState('MODEL', 'downloading…');
    var chain = window.tf ? Promise.resolve() : loadScript(TF_URL);
    return chain.then(function () { return window.cocoSsd ? null : loadScript(COCO_URL); })
      .then(function () { return window.cocoSsd.load({ base: 'lite_mobilenet_v2' }); })
      .then(function (m) { model = m; setState('MODEL', 'ready'); return m; });
  }

  /* ------------------------------------------------------------ sources */
  function openPhoneCamera() {
    if (!navigator.mediaDevices || !navigator.mediaDevices.getUserMedia) {
      $('visionSecureNote').hidden = secure;
      throw new Error(secure ? 'no camera API in this browser' : 'camera needs a secure page');
    }
    return navigator.mediaDevices.getUserMedia({
      video: { facingMode: 'environment', width: { ideal: 640 }, height: { ideal: 480 } },
      audio: false
    }).then(function (s) {
      stream = s;
      video.srcObject = s;
      video.hidden = false;
      return video.play();
    });
  }

  function camBase() {
    var u = ($('camUrl').value || 'http://trashcam.local').trim().replace(/\/+$/, '');
    if (!/^https?:\/\//i.test(u)) u = 'http://' + u;
    try { localStorage.setItem('tb-cam-url', u); } catch (e) {}
    return u;
  }

  // One JPEG from the ESP32-CAM. It sets Access-Control-Allow-Origin: *, so
  // the canvas stays readable and the model can look at the pixels.
  function fetchSnapshot() {
    return new Promise(function (resolve, reject) {
      var im = new Image();
      im.crossOrigin = 'anonymous';
      im.onload = function () { resolve(im); };
      im.onerror = function () { reject(new Error('snapshot failed')); };
      im.src = camBase() + '/capture?_=' + Date.now();
    });
  }

  /* ---------------------------------------------------------- the loop */
  function frameSize(w, h) {
    var scale = Math.min(1, MAX_W / w);
    return { w: Math.round(w * scale), h: Math.round(h * scale) };
  }

  function step() {
    if (!running || busy) return;
    busy = true;
    var getFrame = source === 'phone'
      ? Promise.resolve(video.readyState >= 2 ? video : null)
      : fetchSnapshot().catch(function () { setState('SNAPSHOT', 'failed - check the URL and that the cam is on'); return null; });

    getFrame.then(function (src) {
      if (!src) { busy = false; return; }
      var w = src.videoWidth || src.naturalWidth || src.width;
      var h = src.videoHeight || src.naturalHeight || src.height;
      if (!w || !h) { busy = false; return; }
      var sz = frameSize(w, h);
      if (canvas.width !== sz.w || canvas.height !== sz.h) { canvas.width = sz.w; canvas.height = sz.h; }
      ctx2d.drawImage(src, 0, 0, sz.w, sz.h);
      return model.detect(canvas, 10, MIN_SCORE).then(function (dets) {
        draw(dets);
        report(dets);
        frames++;
        var now = Date.now();
        if (now - fpsAt > 1000) { fps = frames * 1000 / (now - fpsAt); frames = 0; fpsAt = now; }
        setState('RATE', fps.toFixed(1) + ' detections/s');
      });
    }).catch(function (e) {
      setState('ERROR', String(e && e.message || e));
    }).then(function () { busy = false; });
  }

  function draw(dets) {
    ctx2d.lineWidth = 2;
    ctx2d.font = '12px ui-monospace, monospace';
    dets.forEach(function (d) {
      var b = d.bbox, person = d.class === 'person';
      ctx2d.strokeStyle = person ? '#3ddc84' : '#f5a623';
      ctx2d.strokeRect(b[0], b[1], b[2], b[3]);
      var label = d.class + ' ' + Math.round(d.score * 100) + '%';
      var tw = ctx2d.measureText(label).width + 8;
      ctx2d.fillStyle = person ? '#3ddc84' : '#f5a623';
      ctx2d.fillRect(b[0], Math.max(0, b[1] - 16), tw, 16);
      ctx2d.fillStyle = '#101316';
      ctx2d.fillText(label, b[0] + 4, Math.max(12, b[1] - 4));
    });
  }

  function report(dets) {
    var persons = 0, objs = {}, best = 0;
    dets.forEach(function (d) {
      if (d.score > best) best = d.score;
      if (d.class === 'person') persons++;
      else if (!objs[d.class] || objs[d.class] < d.score) objs[d.class] = d.score;
    });
    var names = Object.keys(objs).sort(function (a, b) { return objs[b] - objs[a]; }).slice(0, 4);
    var csv = names.join(',');
    lastSeen = dets;
    renderSeen(persons, names, objs);

    var key = persons + '|' + csv;
    var now = Date.now();
    if (key === last.key && now - last.at < HEARTBEAT_MS) return;
    last = { key: key, at: now };
    if (window.send) window.send({ type: 'vision', persons: Math.min(persons, 255), objects: csv,
                                   conf: Math.round(best * 100), src: source });
  }

  /* --------------------------------------------------------- start/stop */
  function start() {
    if (running) return;
    source = document.querySelector('input[name="vsrc"]:checked').value;
    $('visionStart').disabled = true;
    setState('STATUS', 'starting…');
    $('visionSecureNote').hidden = true;

    var open = source === 'phone' ? openPhoneCamera() : Promise.resolve();
    open.then(loadModel).then(function () {
      running = true;
      img.hidden = true;
      video.hidden = source !== 'phone';
      $('visionStop').disabled = false;
      setState('STATUS', 'running (' + (source === 'phone' ? 'this camera' : camBase()) + ')');
      fpsAt = Date.now(); frames = 0;
      timer = setInterval(step, INTERVAL_MS);
    }).catch(function (e) {
      $('visionStart').disabled = false;
      setState('STATUS', 'could not start: ' + (e && e.message || e));
      stopStream();
    });
  }

  function stopStream() {
    if (stream) { stream.getTracks().forEach(function (t) { t.stop(); }); stream = null; }
    video.srcObject = null;
    video.hidden = true;
  }

  function stop() {
    if (!running) return;
    running = false;
    clearInterval(timer); timer = null;
    stopStream();
    $('visionStart').disabled = false;
    $('visionStop').disabled = true;
    setState('STATUS', 'stopped');
    // Tell the bin the view is empty rather than letting it time out.
    if (window.send) window.send({ type: 'vision', persons: 0, objects: '', conf: 0, src: source });
    last = { key: '', at: 0 };
  }

  /* ---------------------------------------------------------- rendering */
  var state = {};
  function setState(k, v) {
    state[k] = v;
    var order = ['STATUS', 'MODEL', 'RATE', 'SNAPSHOT', 'ERROR'];
    $('visionState').innerHTML = order.filter(function (k) { return state[k]; }).map(function (k) {
      return '<div><span>' + k + '</span><b>' + esc(state[k]) + '</b></div>';
    }).join('');
  }

  function renderSeen(persons, names, objs) {
    var rows = [['PEOPLE', String(persons)]];
    if (names.length) rows.push(['OBJECTS', names.map(function (n) { return n + ' ' + Math.round(objs[n] * 100) + '%'; }).join(', ')]);
    else rows.push(['OBJECTS', 'none']);
    $('visionSeen').innerHTML = rows.map(function (r) {
      return '<div><span>' + r[0] + '</span><b>' + esc(r[1]) + '</b></div>';
    }).join('');
  }

  // What the bin reports back in telemetry - the same on every phone.
  V.onTelemetry = function (m) {
    var v = m.vision;
    if (!v) return;
    var rows = [
      ['SOURCE', v.source === 'none' ? 'nothing reporting' : v.source + (v.live ? '' : ' (stale)')],
      ['PERSON PRESENT', v.present ? 'YES (' + v.persons + ')' : 'no'],
      ['OBJECTS IN VIEW', v.objects || 'none'],
      ['CONFIDENCE', v.conf + '%'],
      ['TOTAL SEEN', v.humans + ' people, ' + v.things + ' things']
    ];
    $('visionBin').innerHTML = rows.map(function (r) {
      return '<div><span>' + r[0] + '</span><b>' + esc(r[1]) + '</b></div>';
    }).join('');
  };

  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  /* ------------------------------------------------------------------ UI */
  $('visionStart').addEventListener('click', start);
  $('visionStop').addEventListener('click', stop);
  document.querySelectorAll('input[name="vsrc"]').forEach(function (r) {
    r.addEventListener('change', function () {
      $('camUrlRow').hidden = this.value !== 'cam';
      if (running) { stop(); }
    });
  });
  document.addEventListener('visibilitychange', function () { if (document.hidden && running) stop(); });

  try {
    var saved = localStorage.getItem('tb-cam-url');
    if (saved) $('camUrl').value = saved;
  } catch (e) {}
  if (!$('camUrl').value) $('camUrl').value = 'http://trashcam.local';
  if (!secure) $('visionSecureNote').hidden = false;
  setState('STATUS', 'idle');
  renderSeen(0, [], {});
})();
