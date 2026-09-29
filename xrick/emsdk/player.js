// xrick web player -- wasm.md phase W1 (RD1).
// Loaded by the page before xrick.js (built by build.sh with -sINVOKE_RUN=0 and
// -sEXPORTED_RUNTIME_METHODS=callMain,FS): the game starts on a click or tap, which
// also lets the browser play sound.
//
// Works with index.html as built, and with any other page (e.g. a CMS template):
// such a page sets window.xrickPlayer BEFORE loading this file to say where its
// elements are and where xrick.wasm is served from -- see `defaults` below. The
// generated xrick.js is never edited.
//
//   <script>window.xrickPlayer = { start: '#xrick', label: '#xrick span',
//     status: '#player_console', buttons: '#controls1 div[data-code]',
//     wasmUrl: '/media/1vqhdi1t/xrick.wasm' };</script>
//   <script src="player.js"></script>
//   <script src="xrick.js" async></script>

'use strict';

// ---- settings ---------------------------------------------------------------------------
var defaults = {
  // start the attract demo instead of a playable game. Off (user decision,
  // 2026-09-28); `?demo` in the URL turns it on for one visit
  startInDemo: false,
  // element clicked/tapped to start; hidden (display:none) once the game runs
  start: '#start',
  // element(s) that show the "click or touch to play" prompt; null = the start element
  label: null,
  // element for status messages (loading, why the game stopped); optional
  status: '#status',
  // on-page buttons: elements with data-code (KeyboardEvent.code) and data-key
  buttons: 'button[data-code]',
  // touch pad (holds the arrow keys); optional
  pad: '#pad',
  // "download trace" button shown with ?trace; optional
  saveTrace: '#savetrace',
  // where xrick.wasm is served from; null = next to xrick.js (emscripten's default)
  wasmUrl: null
};
// The canvas must be <canvas id="canvas">: SDL3 looks it up as "#canvas" by default.
var cfg = Object.assign({}, defaults, window.xrickPlayer || {});

// ---- arguments ----------------------------------------------------------------------------
// query string -> xrick command line: ?demo -> -demo, ?speed=3 -> -speed 3, ...
// (only options the game knows; -data is gone since assets are compiled in)
function buildArgs() {
  var q = new URLSearchParams(window.location.search);
  var args = [];
  var flags = ['demo', 'nosound', 'fullscreen'];
  var values = ['speed', 'zoom', 'keys', 'vol', 'map', 'submap', 'rd'];
  flags.forEach(function (k) { if (q.has(k)) args.push('-' + k); });
  values.forEach(function (k) { if (q.get(k)) args.push('-' + k, q.get(k)); });
  if (cfg.startInDemo && args.indexOf('-demo') < 0) args.push('-demo');
  if (q.has('trace')) args.push('-trace', '/trace.txt');   // wasm.md W1.7
  return args;
}

// ---- page helpers -------------------------------------------------------------------------
// every element but the canvas is optional: a page without it just skips that part
function find(selector) { return selector ? document.querySelector(selector) : null; }
function findAll(selector) { return selector ? Array.prototype.slice.call(document.querySelectorAll(selector)) : []; }

var canvas = document.getElementById('canvas');
var startBox = find(cfg.start);
var statusBox = find(cfg.status);
var lastLines = [];

function setStatus(text) { if (statusBox) statusBox.textContent = text || ''; }

function setLabel(text) {
  var els = cfg.label ? findAll(cfg.label) : (startBox ? [startBox] : []);
  els.forEach(function (el) { el.textContent = text; });
}

function keepLine(text) {
  lastLines.push(text);
  if (lastLines.length > 40) lastLines.shift();
}

// SDL3 maps web key events from KeyboardEvent.code (SDL_emscriptenevents.c,
// Emscripten_HandleKey), so synthetic events must carry `code` -- keyCode alone is
// an unknown key. They are dispatched on window, where SDL listens by default.
function sendKey(down, code, key) {
  window.dispatchEvent(new KeyboardEvent(down ? 'keydown' : 'keyup',
    { code: code, key: key, bubbles: true, cancelable: true }));
}

function bindButton(el) {
  var code = el.dataset.code, key = el.dataset.key;
  if (!code) return;
  var press = function (e) { e.preventDefault(); sendKey(true, code, key); };
  var release = function (e) { e.preventDefault(); sendKey(false, code, key); };
  el.addEventListener('mousedown', press);
  el.addEventListener('mouseup', release);
  el.addEventListener('mouseleave', function (e) { if (e.buttons) release(e); });
  el.addEventListener('touchstart', press, { passive: false });
  el.addEventListener('touchend', release, { passive: false });
  el.addEventListener('touchcancel', release, { passive: false });
}

// touch pad: the offset from the pad's centre holds the arrow keys
function bindPad(pad) {
  var held = {};
  var dirs = ['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown'];
  function set(code, on) {
    if (!!held[code] === on) return;
    held[code] = on;
    sendKey(on, code, code);
  }
  function move(t) {
    var r = pad.getBoundingClientRect();
    var dx = t.clientX - (r.left + r.width / 2), dy = t.clientY - (r.top + r.height / 2);
    set('ArrowRight', dx > r.width / 6);
    set('ArrowLeft', dx < -r.width / 6);
    set('ArrowDown', dy > r.height / 4);
    set('ArrowUp', dy < -r.height / 4);
  }
  function stop(e) { e.preventDefault(); dirs.forEach(function (c) { set(c, false); }); }
  pad.addEventListener('touchstart', function (e) { e.preventDefault(); move(e.targetTouches[0]); }, { passive: false });
  pad.addEventListener('touchmove', function (e) { e.preventDefault(); move(e.targetTouches[0]); }, { passive: false });
  pad.addEventListener('touchend', stop, { passive: false });
  pad.addEventListener('touchcancel', stop, { passive: false });
}

findAll(cfg.buttons).forEach(bindButton);
var padEl = find(cfg.pad);
if (padEl) bindPad(padEl);
window.addEventListener('touchstart', function once() {
  document.body.classList.add('touch');
  window.removeEventListener('touchstart', once);
});

// ---- sound on iPhone / iPad ----------------------------------------------------------------
// iOS plays Web Audio as "ambient" sound, which the Ring/Silent switch mutes (the page
// shows the speaker icon, yet nothing is heard). Two ways to make it "playback" sound,
// both inside the start tap:
// - the Audio Session API, where the browser has it (recent Safari/WebKit);
// - where it does not, a silent looping <audio> element: playing a media element
//   switches the page to playback audio, which un-mutes Web Audio too -- what the 2019
//   player's startAudio() did, lost in the W1.5 rewrite.
function isIOS() {
  return /iPad|iPhone|iPod/.test(navigator.userAgent) ||
         (navigator.platform === 'MacIntel' && navigator.maxTouchPoints > 1);  // iPadOS
}

function silentWavUrl() {
  // 0.25 s of 8-bit mono silence at 8000 Hz (0x80 = the zero level of 8-bit PCM)
  var n = 2000, buf = new ArrayBuffer(44 + n), v = new DataView(buf), i;
  function str(o, s) { for (var k = 0; k < s.length; k++) v.setUint8(o + k, s.charCodeAt(k)); }
  str(0, 'RIFF'); v.setUint32(4, 36 + n, true); str(8, 'WAVE');
  str(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true);
  v.setUint32(24, 8000, true); v.setUint32(28, 8000, true); v.setUint16(32, 1, true); v.setUint16(34, 8, true);
  str(36, 'data'); v.setUint32(40, n, true);
  for (i = 0; i < n; i++) v.setUint8(44 + i, 0x80);
  return URL.createObjectURL(new Blob([buf], { type: 'audio/wav' }));
}

function unlockAudio() {
  try {
    if (navigator.audioSession) navigator.audioSession.type = 'playback';
  } catch (e) { /* not supported: the <audio> element below does it */ }
  if (isIOS() && !unlockAudio.el) {
    var a = document.createElement('audio');
    a.setAttribute('playsinline', '');
    a.loop = true;
    a.src = silentWavUrl();
    var p = a.play();
    if (p && p.catch) p.catch(function () { /* no media playback allowed: nothing more to do */ });
    unlockAudio.el = a;   // keep it playing (and referenced) for the whole session
  }
}

// SDL creates its AudioContext during callMain; resume it while still in the tap
function resumeSdlAudio() {
  var ctx = Module.SDL3 && Module.SDL3.audioContext;
  if (ctx && ctx.state !== 'running' && ctx.resume) ctx.resume();
}

// ---- trace (wasm.md W1.7) ------------------------------------------------------------------
// /trace.txt lives in MEMFS
function traceText() {
  try { Module._fflush(0); } catch (e) { /* runtime already exited: stdio was flushed */ }
  try { return Module.FS.readFile('/trace.txt', { encoding: 'utf8' }); } catch (e) { return null; }
}
window.xrickTrace = traceText;

// ?trace: a button saves the trace so far, to diff with a native `xrick -demo -trace`
var saveBtn = find(cfg.saveTrace);
if (saveBtn && new URLSearchParams(window.location.search).has('trace')) {
  saveBtn.hidden = false;
  saveBtn.style.display = '';
  saveBtn.addEventListener('click', function () {
    var t = traceText();
    if (!t) { setStatus('no trace yet'); return; }
    var a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([t], { type: 'text/plain' }));
    a.download = 'xrick-web.trace';
    a.click();
    URL.revokeObjectURL(a.href);
  });
}

// ---- emscripten Module ---------------------------------------------------------------------
var xrickArgs = buildArgs();   // given to callMain only (not Module.arguments)

var Module = {
  canvas: canvas,
  // xrick.js asks this first for every file it loads: serve xrick.wasm from cfg.wasmUrl
  // when the page sets one (e.g. a CMS media URL), else from next to xrick.js
  locateFile: function (path, scriptDirectory) {
    if (path === 'xrick.wasm' && cfg.wasmUrl) return cfg.wasmUrl;
    return scriptDirectory + path;
  },
  print: function (text) { console.log(text); keepLine(text); },
  printErr: function (text) { console.error(text); keepLine(text); },
  setStatus: function (text) { if (text) setStatus(text); },
  onRuntimeInitialized: function () {
    setLabel('click or touch to play');
    setStatus('');
    if (startBox) startBox.addEventListener('click', start, { once: true });
    else start();   // no start element: nothing to click, but then no sound on most browsers
  },
  onExit: function (status) {
    // an argument the game refuses (e.g. -rd 2 on the web, W1.4) ends in sysarg_fail,
    // whose first line is "xrick [version #...]: <reason>"; Esc (quit) exits with 0
    var fail = lastLines.filter(function (l) { return /^xrick \[version #[^\]]*\]: /.test(l); }).pop();
    var why = fail ? fail.replace(/^xrick \[version #[^\]]*\]: /, '') : '';
    setStatus((status ? 'the game could not start' + (why ? ': ' + why : ' (' + status + ')')
                      : 'game ended') + ' -- reload the page to play again');
  }
};

function start() {
  unlockAudio();                       // inside the tap: iOS playback audio
  if (startBox) startBox.style.display = 'none';
  canvas.style.display = 'block';      // pages may keep the canvas hidden until now
  canvas.focus();
  Module.callMain(xrickArgs);
  resumeSdlAudio();
}

canvas.addEventListener('webglcontextlost', function (e) {
  e.preventDefault();
  setStatus('WebGL context lost -- reload the page');
});
