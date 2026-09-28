// xrick web player -- wasm.md phase W1 (RD1).
// Loaded by index.html before xrick.js (built by build.sh with -sINVOKE_RUN=0 and
// -sEXPORTED_RUNTIME_METHODS=callMain,FS): the game starts on a click, which also
// lets the browser play sound.

'use strict';

// ---- player settings --------------------------------------------------------------------
// startInDemo: start the attract demo instead of a playable game. Off by default
// (user decision, 2026-09-28); `?demo` in the URL turns it on for one visit.
var startInDemo = false;

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
  if (startInDemo && args.indexOf('-demo') < 0) args.push('-demo');
  if (q.has('trace')) args.push('-trace', '/trace.txt');   // wasm.md W1.7
  return args;
}

// ---- page helpers -------------------------------------------------------------------------
var canvas = document.getElementById('canvas');
var startBox = document.getElementById('start');
var statusBox = document.getElementById('status');
var lastLines = [];

function setStatus(text) { statusBox.textContent = text || ''; }

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
  var dirs = { ArrowLeft: false, ArrowRight: false, ArrowUp: false, ArrowDown: false };
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
  function stop(e) { e.preventDefault(); Object.keys(dirs).forEach(function (c) { set(c, false); }); }
  pad.addEventListener('touchstart', function (e) { e.preventDefault(); move(e.targetTouches[0]); }, { passive: false });
  pad.addEventListener('touchmove', function (e) { e.preventDefault(); move(e.targetTouches[0]); }, { passive: false });
  pad.addEventListener('touchend', stop, { passive: false });
  pad.addEventListener('touchcancel', stop, { passive: false });
}

document.querySelectorAll('button[data-code]').forEach(bindButton);
bindPad(document.getElementById('pad'));
window.addEventListener('touchstart', function once() {
  document.body.classList.add('touch');
  window.removeEventListener('touchstart', once);
});

// trace download (wasm.md W1.7): /trace.txt lives in MEMFS
function traceText() {
  try { Module._fflush(0); } catch (e) { /* runtime already exited: stdio was flushed */ }
  try { return Module.FS.readFile('/trace.txt', { encoding: 'utf8' }); } catch (e) { return null; }
}
window.xrickTrace = traceText;

// ?trace: a button saves the trace so far, to diff with a native `xrick -demo -trace`
if (new URLSearchParams(window.location.search).has('trace')) {
  var saveBtn = document.getElementById('savetrace');
  saveBtn.hidden = false;
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
  print: function (text) { console.log(text); keepLine(text); },
  printErr: function (text) { console.error(text); keepLine(text); },
  setStatus: function (text) { if (text) setStatus(text); },
  onRuntimeInitialized: function () {
    startBox.textContent = 'click or touch to play';
    setStatus('');
    startBox.addEventListener('click', start, { once: true });
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
  startBox.hidden = true;
  canvas.focus();
  Module.callMain(xrickArgs);
}

canvas.addEventListener('webglcontextlost', function (e) {
  e.preventDefault();
  setStatus('WebGL context lost -- reload the page');
});
