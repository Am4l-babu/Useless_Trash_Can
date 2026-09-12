/* ==========================================================================
   TRASHBOT OS - the phone is the speaker.

   The bin never sends audio. It sends a "play this" message naming an event
   and a clip; the clip itself was fetched from the bin earlier and decoded
   here. The message can arrive on three links - the WebSocket, a BLE
   notification, a line on the USB serial port - and every copy carries the
   same sequence number, so a phone on two links plays it once.

   Clips are also kept in IndexedDB so a phone that is only on Bluetooth or
   USB (no Wi-Fi at all) still has them.
   ========================================================================== */
'use strict';

(function () {
  var $ = function (id) { return document.getElementById(id); };
  var A = window.TrashAudio = {};

  /* --------------------------------------------------------------- state */
  var ctx = null, gain = null;
  var unlocked = false;
  var current = null;                  // the BufferSource playing now
  var lib = { clips: [], map: {}, events: [], fs: null, maxClip: 0 };
  var buffers = {};                    // name|size -> AudioBuffer
  var undecodable = {};                // name -> true when decode failed
  var loading = {};                    // name|size -> Promise
  var seenSeq = [];                    // last N sequence numbers, any link
  var lastPlayed = null;
  var refreshTimer = null;

  var links = { wifi: false, bt: 'idle', usb: 'idle' };   // idle | connecting | on | unsupported | insecure
  var secure = !!window.isSecureContext;

  /* ---------------------------------------------------------- IndexedDB */
  var dbp = null;
  function db() {
    if (dbp) return dbp;
    dbp = new Promise(function (resolve) {
      try {
        var req = indexedDB.open('trashbot-audio', 1);
        req.onupgradeneeded = function () { req.result.createObjectStore('clips'); };
        req.onsuccess = function () { resolve(req.result); };
        req.onerror = function () { resolve(null); };
      } catch (e) { resolve(null); }
    });
    return dbp;
  }
  function idbGet(key) {
    return db().then(function (d) {
      if (!d) return null;
      return new Promise(function (resolve) {
        var tx = d.transaction('clips', 'readonly').objectStore('clips').get(key);
        tx.onsuccess = function () { resolve(tx.result || null); };
        tx.onerror = function () { resolve(null); };
      });
    });
  }
  function idbPut(key, buf) {
    return db().then(function (d) {
      if (!d) return;
      try { d.transaction('clips', 'readwrite').objectStore('clips').put(buf, key); } catch (e) {}
    });
  }
  function idbKeys() {
    return db().then(function (d) {
      if (!d) return [];
      return new Promise(function (resolve) {
        var tx = d.transaction('clips', 'readonly').objectStore('clips').getAllKeys();
        tx.onsuccess = function () { resolve(tx.result || []); };
        tx.onerror = function () { resolve([]); };
      });
    });
  }
  function idbDelete(key) {
    return db().then(function (d) {
      if (!d) return;
      try { d.transaction('clips', 'readwrite').objectStore('clips').delete(key); } catch (e) {}
    });
  }

  /* --------------------------------------------------------- the engine */
  function ensureCtx() {
    if (ctx) return ctx;
    var AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return null;
    ctx = new AC();
    gain = ctx.createGain();
    gain.connect(ctx.destination);
    setVolume(parseInt($('volume').value, 10));
    ctx.onstatechange = renderState;
    return ctx;
  }

  function unlock() {
    if (!ensureCtx()) { renderState(); return; }
    ctx.resume().then(function () {
      // iOS wants an actual (silent) play inside the gesture as well.
      var b = ctx.createBuffer(1, 1, 22050);
      var s = ctx.createBufferSource();
      s.buffer = b; s.connect(gain); s.start(0);
      unlocked = true;
      try { localStorage.setItem('tb-audio', '1'); } catch (e) {}
      renderState();
      prefetchAll();
    }).catch(renderState);
  }

  A.ready = function () { return unlocked && !!ctx && ctx.state === 'running'; };

  function setVolume(v) {
    v = Math.max(0, Math.min(100, v || 0));
    if (gain) gain.gain.value = v / 100;
    $('volumeOut').textContent = v + '%';
    try { localStorage.setItem('tb-volume', String(v)); } catch (e) {}
  }

  function keyOf(clip) {
    var c = findClip(clip);
    return clip + '|' + (c ? c.size : 0);
  }
  function findClip(name) {
    for (var i = 0; i < lib.clips.length; i++) if (lib.clips[i].name === name) return lib.clips[i];
    return null;
  }

  // memory -> IndexedDB -> the bin. Returns a Promise<AudioBuffer|null>.
  function getBuffer(clip) {
    var key = keyOf(clip);
    if (buffers[key]) return Promise.resolve(buffers[key]);
    if (loading[key]) return loading[key];
    if (!ensureCtx()) return Promise.resolve(null);

    loading[key] = idbGet(key).then(function (cached) {
      if (cached) return cached;
      var c = findClip(clip);
      return fetch('/audio/' + encodeURIComponent(clip) + '?v=' + (c ? c.size : 0))
        .then(function (r) { if (!r.ok) throw new Error('fetch ' + r.status); return r.arrayBuffer(); })
        .then(function (ab) { idbPut(key, ab); return ab; });
    }).then(function (ab) {
      // decodeAudioData consumes the buffer on some browsers; hand it a copy.
      return new Promise(function (resolve, reject) {
        var p = ctx.decodeAudioData(ab.slice(0), resolve, reject);
        if (p && p.then) p.then(resolve, reject);
      });
    }).then(function (buf) {
      buffers[key] = buf;
      delete undecodable[clip];
      delete loading[key];
      renderClips();
      return buf;
    }).catch(function (e) {
      undecodable[clip] = true;
      delete loading[key];
      renderClips();
      return null;
    });
    return loading[key];
  }

  function prefetchAll() {
    lib.clips.forEach(function (c) { getBuffer(c.name); });
  }

  function play(clip, why, via) {
    return getBuffer(clip).then(function (buf) {
      if (!buf) { setNow('CANNOT DECODE ' + clip, true); return false; }
      if (!A.ready()) { setNow('SOUND LOCKED - tap ENABLE SOUND', true); return false; }
      if (current) { try { current.stop(); } catch (e) {} }
      var s = ctx.createBufferSource();
      s.buffer = buf;
      s.connect(gain);
      s.onended = function () { if (current === s) { current = null; setNow('NOTHING PLAYING'); } };
      s.start(0);
      current = s;
      lastPlayed = { clip: clip, why: why, via: via, at: Date.now() };
      setNow((why ? why + '  ·  ' : '') + clip + (via ? '  (' + via + ')' : ''));
      renderState();
      return true;
    });
  }

  function setNow(text, warn) {
    var el = $('nowPlaying');
    el.textContent = text;
    el.classList.toggle('warn', !!warn);
  }

  /* ----------------------------------------------------- incoming sound */
  function seen(seq) {
    if (seq === undefined || seq === null) return false;
    if (seenSeq.indexOf(seq) >= 0) return true;
    seenSeq.push(seq);
    if (seenSeq.length > 64) seenSeq.shift();
    return false;
  }

  // WebSocket / USB flavour: {"type":"sound","seq":12,"event":"human_detected","clip":"hi.wav"}
  A.onSound = function (m, via) {
    if (seen(m.seq)) return;
    var clip = m.clip || lib.map[m.event] || '';
    var ev = eventById(m.event);
    var why = ev ? ev.label : m.event;
    if (!clip) { setNow('NO CLIP FOR ' + why, true); return; }
    play(clip, why, via);
  };

  // BLE flavour: "seq;eventNumber;clip" - the clip may be cut off by a small MTU.
  A.onSoundCompact = function (str, via) {
    var parts = String(str).split(';');
    if (parts.length < 2) return;
    var seq = parseInt(parts[0], 10), n = parseInt(parts[1], 10);
    if (seen(seq)) return;
    var ev = eventByNumber(n);
    var clip = parts[2] || '';
    if (!clip || !findClip(clip)) clip = ev ? (lib.map[ev.id] || '') : '';
    if (!clip) { setNow('NO CLIP FOR EVENT #' + n, true); return; }
    play(clip, ev ? ev.label : '#' + n, via);
  };

  function eventById(id) {
    for (var i = 0; i < lib.events.length; i++) if (lib.events[i].id === id) return lib.events[i];
    return null;
  }
  function eventByNumber(n) {
    for (var i = 0; i < lib.events.length; i++) if (lib.events[i].n === n) return lib.events[i];
    return null;
  }

  /* ------------------------------------------------------- the library */
  A.refresh = function () {
    clearTimeout(refreshTimer);
    refreshTimer = setTimeout(fetchLibrary, 150);   // several changes -> one fetch
  };

  function fetchLibrary() {
    return fetch('/api/audio').then(function (r) { return r.json(); }).then(function (j) {
      lib.clips = j.clips || [];
      lib.map = j.map || {};
      lib.events = j.events || [];
      lib.fs = j.fs || null;
      lib.maxClip = j.maxClip || 0;
      try { localStorage.setItem('tb-audio-lib', JSON.stringify({ clips: lib.clips, map: lib.map, events: lib.events })); } catch (e) {}
      renderAll();
      prefetchAll();
      pruneCache();
    }).catch(function () {
      // No Wi-Fi: whatever we last saw still lets BLE/USB events resolve.
      try {
        var saved = JSON.parse(localStorage.getItem('tb-audio-lib') || 'null');
        if (saved) { lib.clips = saved.clips; lib.map = saved.map; lib.events = saved.events; }
      } catch (e) {}
      renderAll();
    });
  }

  function pruneCache() {
    var keep = {};
    lib.clips.forEach(function (c) { keep[c.name + '|' + c.size] = true; });
    idbKeys().then(function (keys) {
      keys.forEach(function (k) { if (!keep[k]) idbDelete(k); });
    });
  }

  function renderAll() {
    renderState();
    renderClips();
    renderMap();
    renderLinks();
  }

  function fmtKB(n) { return (Math.round((n || 0) / 102.4) / 10) + ' KB'; }

  function renderState() {
    var rows = [
      ['SOUND', A.ready() ? 'UNLOCKED' : (ctx ? 'LOCKED (' + ctx.state + ')' : 'NOT STARTED')],
      ['CLIPS READY', Object.keys(buffers).length + ' / ' + lib.clips.length],
      ['LAST PLAYED', lastPlayed ? lastPlayed.clip + ' via ' + lastPlayed.via : '-']
    ];
    $('audioState').innerHTML = rows.map(function (r) {
      return '<div><span>' + r[0] + '</span><b>' + esc(r[1]) + '</b></div>';
    }).join('');
    $('audioUnlock').textContent = A.ready() ? 'SOUND IS ON HERE' : 'ENABLE SOUND ON THIS DEVICE';
    $('audioUnlock').classList.toggle('on', A.ready());
  }

  function renderClips() {
    var box = $('clipList');
    if (!lib.clips.length) { box.innerHTML = '<div class="empty">no clips yet - upload one</div>'; }
    else {
      box.innerHTML = lib.clips.map(function (c) {
        var key = c.name + '|' + c.size;
        var st = buffers[key] ? 'ready' : (undecodable[c.name] ? 'bad' : 'loading');
        var stTxt = { ready: 'READY', bad: 'CANNOT DECODE', loading: 'FETCHING' }[st];
        return '<div class="clip ' + st + '" data-clip="' + esc(c.name) + '">' +
               '<button class="pbtn" data-play="' + esc(c.name) + '" title="preview here">&#9654;</button>' +
               '<b>' + esc(c.name) + '</b><em>' + fmtKB(c.size) + ' · ' + stTxt + '</em>' +
               '<button class="xbtn" data-del="' + esc(c.name) + '" title="delete from the bin">&#10005;</button>' +
               '</div>';
      }).join('');
    }
    if (lib.fs) {
      var free = lib.fs.total - lib.fs.used;
      $('fsInfo').innerHTML =
        '<div><span>SPACE ON THE BIN</span><b>' + fmtKB(free) + ' free of ' + fmtKB(lib.fs.total) + '</b></div>';
    }
    if (lib.maxClip) $('clipLimit').textContent = fmtKB(lib.maxClip);
  }

  function renderMap() {
    var box = $('eventMap');
    if (!lib.events.length) { box.innerHTML = '<div class="empty">event list not loaded</div>'; return; }
    var opts = '<option value="">— nothing —</option>' + lib.clips.map(function (c) {
      return '<option value="' + esc(c.name) + '">' + esc(c.name) + '</option>';
    }).join('');
    var lastGroup = '';
    box.innerHTML = lib.events.map(function (e) {
      var group = e.id.indexOf('mood_') === 0 ? 'MOODS' : (e.id.indexOf('command_') === 0 ? 'COMMANDS' : 'EVENTS');
      var head = group !== lastGroup ? '<div class="evgroup">' + group + '</div>' : '';
      lastGroup = group;
      var assigned = lib.map[e.id] || '';
      return head + '<div class="evrow' + (assigned ? ' set' : '') + '">' +
        '<div class="evname"><b>' + esc(e.label) + '</b><small>' + esc(e.desc) + '</small></div>' +
        '<select data-ev="' + esc(e.id) + '">' + opts + '</select>' +
        '<button class="tbtn" data-test="' + esc(e.id) + '"' + (assigned ? '' : ' disabled') + '>TEST</button>' +
        '</div>';
    }).join('');
    box.querySelectorAll('select').forEach(function (s) { s.value = lib.map[s.dataset.ev] || ''; });
  }

  /* --------------------------------------------------------- uploading */
  function upload() {
    var f = $('clipFile').files[0];
    if (!f) { flash('choose a file first'); return; }
    if (lib.maxClip && f.size > lib.maxClip) { flash('too big: ' + fmtKB(f.size) + ' > ' + fmtKB(lib.maxClip)); return; }
    var fd = new FormData();
    fd.append('clip', f, f.name);
    flash('uploading ' + f.name + '...');
    fetch('/api/audio', { method: 'POST', body: fd })
      .then(function (r) { return r.text().then(function (t) { return { ok: r.ok, t: t }; }); })
      .then(function (r) {
        flash(r.ok ? 'uploaded' : r.t);
        if (r.ok) { $('clipFile').value = ''; A.refresh(); }
      })
      .catch(function () { flash('upload failed - is the link up?'); });
  }

  function del(name) {
    if (!window.confirm('Delete ' + name + ' from the bin?')) return;
    fetch('/api/audio?name=' + encodeURIComponent(name), { method: 'DELETE' })
      .then(function (r) { return r.text().then(function (t) { flash(r.ok ? 'deleted' : t); A.refresh(); }); })
      .catch(function () { flash('delete failed'); });
  }

  function assign(ev, clip) {
    var body = 'event=' + encodeURIComponent(ev) + '&clip=' + encodeURIComponent(clip);
    fetch('/api/audio/map', {
      method: 'POST',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: body
    }).then(function (r) { return r.text().then(function (t) { if (!r.ok) flash(t); }); })
      .catch(function () { flash('could not save - is the link up?'); });
  }

  function flash(text) {
    $('fsInfo').innerHTML = '<div><span>STATUS</span><b>' + esc(text) + '</b></div>';
  }

  /* ------------------------------------------------------------ links */
  function renderLinks() {
    $('lnkWifi').textContent = window.connected ? 'UP' : 'DOWN';
    $('lnkWifi').className = window.connected ? 'on' : '';

    var bt = $('lnkBt'), usb = $('lnkUsb');
    var names = { idle: 'NOT CONNECTED', connecting: 'CONNECTING…', on: 'UP',
                  unsupported: 'NOT IN THIS BROWSER', insecure: 'NEEDS SECURE PAGE', failed: 'FAILED' };
    bt.textContent = names[links.bt] || links.bt;
    bt.className = links.bt === 'on' ? 'on' : '';
    usb.textContent = names[links.usb] || links.usb;
    usb.className = links.usb === 'on' ? 'on' : '';

    $('btConnect').disabled = links.bt === 'on' || links.bt === 'connecting' || links.bt === 'unsupported' || links.bt === 'insecure';
    $('usbConnect').disabled = links.usb === 'on' || links.usb === 'connecting' || links.usb === 'unsupported' || links.usb === 'insecure';
    $('btConnect').textContent = links.bt === 'on' ? 'CONNECTED' : 'CONNECT';
    $('usbConnect').textContent = links.usb === 'on' ? 'CONNECTED' : 'CONNECT';

    var needFlag = !secure && (links.bt === 'insecure' || links.usb === 'insecure');
    $('secureNote').hidden = !needFlag;
    $('originText').textContent = location.origin;
  }
  A.renderLinks = renderLinks;

  // ---- Bluetooth (Web Bluetooth). The bin advertises its service UUID.
  var btDevice = null, btCmd = null, btQueue = Promise.resolve(), btBacklog = 0;

  function bleInfo() {
    var h = window.helloInfo && window.helloInfo.ble;
    return h || { name: 'TRASHBOT', service: '7a5b0001-8e6f-4c1d-9b2a-3f4e5d6c7b8a',
                  sound: '7a5b0002-8e6f-4c1d-9b2a-3f4e5d6c7b8a', command: '7a5b0003-8e6f-4c1d-9b2a-3f4e5d6c7b8a' };
  }

  function btConnect() {
    if (!navigator.bluetooth) { links.bt = secure ? 'unsupported' : 'insecure'; renderLinks(); return; }
    var info = bleInfo();
    links.bt = 'connecting'; renderLinks();
    navigator.bluetooth.requestDevice({
      filters: [{ namePrefix: info.name }, { services: [info.service] }],
      optionalServices: [info.service]
    }).then(function (dev) {
      btDevice = dev;
      dev.addEventListener('gattserverdisconnected', function () {
        links.bt = 'idle'; btCmd = null; renderLinks();
      });
      return dev.gatt.connect();
    }).then(function (server) {
      return server.getPrimaryService(info.service);
    }).then(function (svc) {
      return Promise.all([svc.getCharacteristic(info.sound), svc.getCharacteristic(info.command)]);
    }).then(function (chars) {
      btCmd = chars[1];
      var snd = chars[0];
      snd.addEventListener('characteristicvaluechanged', function (ev) {
        var s = new TextDecoder().decode(ev.target.value);
        A.onSoundCompact(s, 'bluetooth');
      });
      return snd.startNotifications();
    }).then(function () {
      links.bt = 'on'; renderLinks();
    }).catch(function (e) {
      links.bt = (e && e.name === 'NotFoundError') ? 'idle' : 'failed';
      renderLinks();
    });
  }

  function btSend(str) {
    if (!btCmd || links.bt !== 'on') return false;
    if (btBacklog > 8) return true;               // GATT is slower than 100 ms drives; drop, never queue up
    btBacklog++;
    var bytes = new TextEncoder().encode(str);
    btQueue = btQueue.then(function () {
      return (btCmd.writeValueWithoutResponse ? btCmd.writeValueWithoutResponse(bytes) : btCmd.writeValue(bytes));
    }).catch(function () {}).then(function () { btBacklog--; });
    return true;
  }

  // ---- USB. Web Serial where it exists (laptops), WebUSB with a tiny
  // CP210x / CH34x driver where it does not (Android).
  var usbWrite = null;

  function usbConnect() {
    if (!secure) { links.usb = 'insecure'; renderLinks(); return; }
    if (navigator.serial) return serialConnect();
    if (navigator.usb) return webUsbConnect();
    links.usb = 'unsupported'; renderLinks();
  }

  function lineSplitter(onLine) {
    var buf = '';
    return function (chunk) {
      buf += chunk;
      var i;
      while ((i = buf.indexOf('\n')) >= 0) {
        var line = buf.slice(0, i).replace(/\r$/, '');
        buf = buf.slice(i + 1);
        if (line) onLine(line);
      }
      if (buf.length > 4096) buf = '';
    };
  }

  function onUsbLine(line) {
    if (line.indexOf('SND ') !== 0) return;      // everything else is the boot log
    try { A.onSound(JSON.parse(line.slice(4)), 'usb'); } catch (e) {}
  }

  function serialConnect() {
    links.usb = 'connecting'; renderLinks();
    var port;
    navigator.serial.requestPort().then(function (p) {
      port = p;
      return port.open({ baudRate: 115200 });
    }).then(function () {
      // Both asserted keeps a dev board out of its bootloader; matches a serial monitor.
      return port.setSignals({ dataTerminalReady: true, requestToSend: true }).catch(function () {});
    }).then(function () {
      var dec = new TextDecoderStream();
      port.readable.pipeTo(dec.writable).catch(function () {});
      var reader = dec.readable.getReader();
      var split = lineSplitter(onUsbLine);
      var writer = port.writable.getWriter();
      var enc = new TextEncoder();
      usbWrite = function (s) { writer.write(enc.encode(s + '\n')).catch(function () {}); };
      links.usb = 'on'; renderLinks();
      (function pump() {
        reader.read().then(function (r) {
          if (r.done) { links.usb = 'idle'; usbWrite = null; renderLinks(); return; }
          split(r.value);
          pump();
        }).catch(function () { links.usb = 'idle'; usbWrite = null; renderLinks(); });
      })();
    }).catch(function (e) {
      links.usb = (e && e.name === 'NotFoundError') ? 'idle' : 'failed';
      renderLinks();
    });
  }

  function webUsbConnect() {
    links.usb = 'connecting'; renderLinks();
    var dev, epIn, epOut, iface;
    navigator.usb.requestDevice({ filters: [{ vendorId: 0x10C4 }, { vendorId: 0x1A86 }] }).then(function (d) {
      dev = d;
      return dev.open();
    }).then(function () {
      if (dev.configuration === null) return dev.selectConfiguration(1);
    }).then(function () {
      var ifaces = dev.configuration.interfaces;
      for (var i = 0; i < ifaces.length && !iface; i++) {
        var alt = ifaces[i].alternates[0];
        var ein = null, eout = null;
        alt.endpoints.forEach(function (e) {
          if (e.type !== 'bulk') return;
          if (e.direction === 'in') ein = e.endpointNumber; else eout = e.endpointNumber;
        });
        if (ein !== null && eout !== null) { iface = ifaces[i].interfaceNumber; epIn = ein; epOut = eout; }
      }
      if (iface === undefined) throw new Error('no bulk interface');
      return dev.claimInterface(iface);
    }).then(function () {
      if (dev.vendorId === 0x10C4) return cp210xInit(dev, iface);
      return ch34xInit(dev);
    }).then(function () {
      var enc = new TextEncoder(), dec = new TextDecoder();
      var split = lineSplitter(onUsbLine);
      usbWrite = function (s) { dev.transferOut(epOut, enc.encode(s + '\n')).catch(function () {}); };
      links.usb = 'on'; renderLinks();
      (function pump() {
        dev.transferIn(epIn, 64).then(function (r) {
          if (r.data && r.data.byteLength) split(dec.decode(r.data));
          pump();
        }).catch(function () { links.usb = 'idle'; usbWrite = null; renderLinks(); });
      })();
    }).catch(function (e) {
      links.usb = (e && e.name === 'NotFoundError') ? 'idle' : 'failed';
      renderLinks();
    });
  }

  // Silicon Labs CP210x: everything is a vendor request on the interface.
  function cp210xInit(dev, iface) {
    function ctl(req, value, data) {
      return dev.controlTransferOut({ requestType: 'vendor', recipient: 'interface', request: req, value: value, index: iface }, data);
    }
    var baud = new DataView(new ArrayBuffer(4)); baud.setUint32(0, 115200, true);
    return ctl(0x00, 0x0001)                    // IFC_ENABLE
      .then(function () { return ctl(0x1E, 0, baud.buffer); })   // SET_BAUDRATE
      .then(function () { return ctl(0x03, 0x0800); })           // SET_LINE_CTL: 8N1
      .then(function () { return ctl(0x07, 0x0303); });          // SET_MHS: DTR+RTS on (with mask)
  }

  // WCH CH340/CH341: register pokes lifted from the Linux ch341 driver.
  function ch34xInit(dev) {
    function ctl(req, value, index) {
      return dev.controlTransferOut({ requestType: 'vendor', recipient: 'device', request: req, value: value, index: index });
    }
    return ctl(0xA1, 0, 0)                                      // SERIAL_INIT
      .then(function () { return ctl(0x9A, 0x1312, 0xCC03); })  // baud 115200, prescaler
      .then(function () { return ctl(0x9A, 0x0F2C, 0x0008); })  // baud 115200, divisor
      .then(function () { return ctl(0x9A, 0x2518, 0x00C3); })  // LCR: 8N1, rx+tx enable
      .then(function () { return ctl(0xA4, 0xFF9F, 0); });      // MODEM_CTRL: ~(DTR|RTS) -> both asserted
  }

  // app.js calls this when the WebSocket is down: the bin then hears us over
  // BLE or USB, including the pings that keep the link alive.
  A.altSend = function (str) {
    if (btSend(str)) return true;
    if (usbWrite) { usbWrite(str); return true; }
    return false;
  };

  /* ---------------------------------------------------------------- UI */
  $('audioUnlock').addEventListener('click', unlock);
  $('volume').addEventListener('input', function () { setVolume(parseInt(this.value, 10)); });
  $('clipUpload').addEventListener('click', upload);
  $('btConnect').addEventListener('click', btConnect);
  $('usbConnect').addEventListener('click', usbConnect);

  $('clipList').addEventListener('click', function (ev) {
    var b = ev.target.closest('button');
    if (!b) return;
    if (b.dataset.play) { if (!A.ready()) unlock(); play(b.dataset.play, 'preview', 'here'); }
    if (b.dataset.del) del(b.dataset.del);
  });

  $('eventMap').addEventListener('change', function (ev) {
    var s = ev.target.closest('select');
    if (!s) return;
    lib.map[s.dataset.ev] = s.value;
    s.closest('.evrow').classList.toggle('set', !!s.value);
    s.closest('.evrow').querySelector('.tbtn').disabled = !s.value;
    assign(s.dataset.ev, s.value);
  });

  $('eventMap').addEventListener('click', function (ev) {
    var b = ev.target.closest('button[data-test]');
    if (!b) return;
    if (!A.ready()) unlock();
    if (!window.send || !window.send({ type: 'sound_test', event: b.dataset.test })) {
      // No link to the bin at all: at least prove the clip on this phone.
      var clip = lib.map[b.dataset.test];
      if (clip) play(clip, 'test (local only)', 'here');
    }
  });

  function esc(s) {
    return String(s).replace(/[&<>"]/g, function (c) {
      return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c];
    });
  }

  /* -------------------------------------------------------------- boot */
  try {
    var v = parseInt(localStorage.getItem('tb-volume'), 10);
    if (!isNaN(v)) { $('volume').value = v; $('volumeOut').textContent = v + '%'; }
  } catch (e) {}
  if (!secure) {
    if (!navigator.bluetooth) links.bt = 'insecure';
    if (!navigator.serial && !navigator.usb) links.usb = 'insecure';
  } else {
    if (!navigator.bluetooth) links.bt = 'unsupported';
    if (!navigator.serial && !navigator.usb) links.usb = 'unsupported';
  }
  ensureCtx();
  renderAll();
  fetchLibrary();
})();
