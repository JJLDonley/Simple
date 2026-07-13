const navLinks = [...document.querySelectorAll('.doc-nav a')];
const sections = [...document.querySelectorAll('.doc-section')];
const current = document.getElementById('current-section');
const search = document.getElementById('doc-search');
const menu = document.querySelector('.menu-button');
const pages = {
  landing: document.getElementById('landing-page'),
  playground: document.getElementById('playground-page'),
  docs: document.getElementById('docs-page')
};
const pageLinks = [...document.querySelectorAll('[data-page-link]')];
const themeSelect = document.getElementById('theme-select');
const docThemeSelect = document.getElementById('doc-theme-select');

function applyTheme(theme) {
  const next = ['light', 'dark', 'system'].includes(theme) ? theme : 'system';
  document.documentElement.dataset.theme = next;
  localStorage.setItem('simple-theme', next);
  if (themeSelect) themeSelect.value = next;
  if (docThemeSelect) docThemeSelect.value = next;
}

applyTheme(localStorage.getItem('simple-theme') || 'system');
if (themeSelect) {
  themeSelect.addEventListener('change', () => applyTheme(themeSelect.value));
}
if (docThemeSelect) {
  docThemeSelect.addEventListener('change', () => applyTheme(docThemeSelect.value));
}

function setPage(name) {
  for (const [pageName, page] of Object.entries(pages)) {
    if (page) page.hidden = pageName !== name;
  }
  for (const link of pageLinks) {
    link.classList.toggle('active', link.dataset.pageLink === name);
  }
}

function setActive(id) {
  for (const link of navLinks) {
    link.classList.toggle('active', link.dataset.route === id);
  }
  const section = document.getElementById(id);
  if (section && current) current.textContent = section.dataset.title || section.querySelector('h1,h2')?.textContent || 'Simple';
}

function routeToHash() {
  const id = location.hash.replace('#', '') || 'landing';
  if (id === 'landing') {
    setPage('landing');
    return;
  }
  if (id === 'playground') {
    setPage('playground');
    return;
  }

  const target = document.getElementById(id);
  if (target && target.classList.contains('doc-section')) {
    setPage('docs');
    setActive(id);
    requestAnimationFrame(() => target.scrollIntoView({ block: 'start' }));
    return;
  }

  setPage('landing');
}

window.addEventListener('hashchange', routeToHash);
routeToHash();

const observer = new IntersectionObserver((entries) => {
  const visible = entries
    .filter((entry) => entry.isIntersecting)
    .sort((a, b) => a.boundingClientRect.top - b.boundingClientRect.top)[0];
  if (visible) setActive(visible.target.id);
}, { rootMargin: '-80px 0px -65% 0px', threshold: [0, 1] });
for (const section of sections) observer.observe(section);

if (search) {
  search.addEventListener('input', () => {
    const query = search.value.trim().toLowerCase();
    for (const link of navLinks) {
      const section = document.getElementById(link.dataset.route);
      const text = `${link.textContent} ${section?.textContent || ''}`.toLowerCase();
      link.classList.toggle('hidden', query !== '' && !text.includes(query));
    }
  });
}

if (menu) {
  menu.addEventListener('click', () => document.body.classList.toggle('nav-open'));
  for (const link of navLinks) {
    link.addEventListener('click', () => document.body.classList.remove('nav-open'));
  }
}

function escapeHtml(value) {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}

const simpleKeywords = new Set([
  'import', 'module', 'artifact', 'namespace', 'enum', 'extern', 'return', 'if', 'else',
  'for', 'while', 'break', 'skip', 'switch', 'case', 'default', 'true', 'false', 'self'
]);
const simpleTypes = new Set([
  'void', 'bool', 'char', 'string', 'i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64',
  'f32', 'f64', 'Result', 'Option', 'Promise', 'System', 'Standard'
]);
const modules = new Set(['System', 'Standard', 'FFI', 'IO', 'FS', 'HTTP', 'HTTPS', 'Console', 'Terminal', 'Path', 'Json', 'Buffer', 'Bytes', 'Channel', 'Log', 'Random', 'Time']);

function highlightSimple(source) {
  let out = '';
  let i = 0;

  while (i < source.length) {
    const ch = source[i];
    const next = source[i + 1];

    if (ch === '/' && next === '/') {
      const end = source.indexOf('\n', i);
      const stop = end === -1 ? source.length : end;
      out += `<span class="tok-comment">${escapeHtml(source.slice(i, stop))}</span>`;
      i = stop;
      continue;
    }

    if (ch === '"') {
      let j = i + 1;
      while (j < source.length) {
        if (source[j] === '\\') { j += 2; continue; }
        if (source[j] === '"') { j++; break; }
        j++;
      }
      out += `<span class="tok-string">${escapeHtml(source.slice(i, j))}</span>`;
      i = j;
      continue;
    }

    if (ch === "'") {
      let j = i + 1;
      while (j < source.length) {
        if (source[j] === '\\') { j += 2; continue; }
        if (source[j] === "'") { j++; break; }
        j++;
      }
      out += `<span class="tok-string">${escapeHtml(source.slice(i, j))}</span>`;
      i = j;
      continue;
    }

    if (/\d/.test(ch)) {
      const m = source.slice(i).match(/^(0x[0-9a-fA-F]+|0b[01]+|\d+(?:\.\d+)?)/);
      if (m) {
        out += `<span class="tok-number">${m[0]}</span>`;
        i += m[0].length;
        continue;
      }
    }

    if (/[A-Za-z_]/.test(ch)) {
      const m = source.slice(i).match(/^[A-Za-z_][A-Za-z0-9_]*/);
      const word = m[0];
      const rest = source.slice(i + word.length);
      if (simpleKeywords.has(word)) out += `<span class="tok-keyword">${word}</span>`;
      else if (simpleTypes.has(word)) out += `<span class="tok-type">${word}</span>`;
      else if (modules.has(word)) out += `<span class="tok-module">${word}</span>`;
      else if (/^\s*\(/.test(rest) || /^\s*:{1,2}\s*[A-Za-z_][A-Za-z0-9_.<>]*\s*\(/.test(rest)) out += `<span class="tok-function">${word}</span>`;
      else out += escapeHtml(word);
      i += word.length;
      continue;
    }

    if (/[{}()[\],.;]/.test(ch)) {
      out += `<span class="tok-punct">${escapeHtml(ch)}</span>`;
      i++;
      continue;
    }

    if (/[:=+\-*\/<>!@|&.]/.test(ch)) {
      const m = source.slice(i).match(/^(::|==|!=|<=|>=|->|=>|\.\.|\|>|\+=|-=|&&|\|\||[=:;+\-*\/<>!@|&.])/);
      out += `<span class="tok-operator">${escapeHtml(m ? m[0] : ch)}</span>`;
      i += m ? m[0].length : 1;
      continue;
    }

    out += escapeHtml(ch);
    i++;
  }
  return out;
}

function highlightBash(source) {
  return escapeHtml(source)
    .replace(/(^|\n)(#.*)/g, '$1<span class="tok-comment">$2</span>')
    .replace(/(^|\n)([A-Za-z_][A-Za-z0-9_./-]*)/g, '$1<span class="tok-command">$2</span>')
    .replace(/(&quot;.*?&quot;|'.*?')/g, '<span class="tok-string">$1</span>');
}

const exampleMeta = {
  toolScript: { wip: false },
  artifactModel: { wip: false },
  namespaceApi: { wip: false },
  moduleSurface: { wip: false },
  typedCollections: { wip: false },
  ffiBinding: { wip: false },
  systemFs: { wip: false },
  httpServer: { wip: true },
  terminalLoop: { wip: true },
  asyncFlow: { wip: true }
};

const examples = {
  toolScript: `module examples.tool

import Standard.IO
import Standard.FS
import System.OS

path :: string = "build.log"

if (Standard.FS.exists(path)) {
  Standard.IO.println("{} exists on {}", path, System.OS.platform())
} else {
  Standard.IO.println("{} missing", path)
}

main :: i32 () {
  return 0
}`,
  artifactModel: `module examples.artifacts

import Standard.IO

Vec2 :: artifact {
  x : f64
  y : f64

  lengthSquared :: f64 () {
    return self.x * self.x + self.y * self.y
  }
}

Actor :: artifact {
  pos : Vec2
  hp : i32

  moveBy :: void (delta : Vec2) {
    self.pos.x = self.pos.x + delta.x
    self.pos.y = self.pos.y + delta.y
  }

  alive :: bool () {
    return self.hp > 0
  }
}

main :: i32 () {
  start : Vec2 = { .x = 2.0, .y = 3.0 }
  delta : Vec2 = { .x = 1.0, .y = -1.0 }
  player : Actor = { start, 10 }
  player.moveBy(delta)

  isAlive : bool = player.alive()
  distanceSquared : f64 = player.pos.lengthSquared()
  Standard.IO.println("player status")
  Standard.IO.println(isAlive)
  Standard.IO.println(distanceSquared)
  return 0
}`,
  namespaceApi: `module examples.checksum

import Standard.IO

Checksum :: namespace {
  seed :: i32 = 17

  step :: i32 (hash : i32, value : i32) {
    return hash * 31 + value
  }

  list :: i32 (values : i32[]) {
    hash : i32 = Checksum.seed
    for (i : i32 = 0; i < len(values); i = i + 1) {
      |> (values[i] < 0) { skip }
      |> default { hash = Checksum.step(hash, values[i]) }
    }
    return hash
  }
}

main :: i32 () {
  values : i32[] = [4, 8, -1, 15, 16, 23, 42]
  Standard.IO.println("checksum={}", Checksum.list(values))
  return 0
}`,
  moduleSurface: `// file: tools/math.simple
module tools.math

MathTools :: namespace {
  clamp :: i32 (value : i32, lo : i32, hi : i32) {
    |> (value < lo) { return lo }
    |> (value > hi) { return hi }
    |> default { return value }
  }
}

// file: main.simple
module app.main

import Standard.IO
import tools.math

main :: i32 () {
  score : i32 = MathTools.clamp(128, 0, 100)
  Standard.IO.println("score={}", score)
  return score
}`,
  typedCollections: `module examples.collections

import Standard.IO

main :: i32 () {
  scores : i32[] = [14, 22, 18]

  best : i32 = 0
  for (i : i32 = 1; i < len(scores); i = i + 1) {
    if (scores[i] > scores[best]) {
      best = i
    }
  }

  Standard.IO.println("highest score")
  Standard.IO.println(scores[best])
  return scores[best]
}`,
  ffiBinding: `module examples.ffi

import System.FFI
import Standard.IO

extern raylib.InitWindow : void (w : i32, h : i32, title : string)
extern raylib.WindowShouldClose : bool ()
extern raylib.BeginDrawing : void ()
extern raylib.ClearBackground : void (color : i32)
extern raylib.EndDrawing : void ()
extern raylib.CloseWindow : void ()

main :: i32 () {
  lib : i64 = System.FFI.open("libraylib.so", raylib)
  if (lib == 0) {
    Standard.IO.println("raylib load failed: {}", System.FFI.lastError())
    return 1
  }

  lib.InitWindow(800, 600, "Simple + raylib")
  while (!lib.WindowShouldClose()) {
    lib.BeginDrawing()
    lib.ClearBackground(0)
    lib.EndDrawing()
  }
  lib.CloseWindow()
  System.FFI.close(lib)
  return 0
}`,
  systemFs: `module examples.fs

import System.FS
import Standard.IO

main :: i32 () {
  path :: string = "simple.toml"

  if (!System.FS.exists(path)) {
    Standard.IO.println("missing {}", path)
    return 1
  }

  text : string = System.FS.readText(path)
  Standard.IO.println("{} bytes", len(text))
  return 0
}`,
  httpServer: `// WIP domain: Standard.HTTP
module examples.http

import Standard.HTTP
import Standard.IO

main :: i32 () {
  server : Standard.HTTP.Server = Standard.HTTP.serve("127.0.0.1", 8080, handler)?
  Standard.IO.println("listening on http://127.0.0.1:8080")
  return server.wait()?
}

handler :: Standard.HTTP.Response (request : Standard.HTTP.Request) {
  if (request.path == "/health") {
    return Standard.HTTP.Response.text(200, "ok")
  }
  return Standard.HTTP.Response.json(200, Standard.Json.object({ "name": "Simple" }))
}`,
  terminalLoop: `// WIP domain: Standard.Terminal
module examples.terminal

import Standard.Terminal

main :: i32 () {
  term : Standard.Terminal.Handle = Standard.Terminal.open()?
  Standard.Terminal.enterRaw(term)?
  Standard.Terminal.enterAltScreen(term)?
  Standard.Terminal.hideCursor(term)?

  running : bool = true
  while (running) {
    event : Option<Standard.Terminal.Event> = Standard.Terminal.pollEvent(term)
    if (event.hasValue()) {
      if (event.value().key == "q") { running = false }
    }
    Standard.Terminal.writeAt(term, 2, 2, "press q to quit")?
    Standard.Terminal.flush(term)?
  }

  Standard.Terminal.showCursor(term)?
  Standard.Terminal.exitAltScreen(term)?
  Standard.Terminal.close(term)?
  return 0
}`,
  asyncFlow: `// WIP domain: Standard.Promise / Standard.HTTP.async
module examples.async

import Standard.HTTP
import Standard.IO

main :: i32 () {
  home : Promise<Standard.HTTP.Response> = Standard.HTTP.async.get("https://example.com")
  api : Promise<Standard.HTTP.Response> = Standard.HTTP.async.get("https://example.com/api")

  homeResult : Standard.HTTP.Response = home.await()?
  apiResult : Standard.HTTP.Response = api.await()?

  Standard.IO.println("home={} api={}", homeResult.status, apiResult.status)
  return 0
}`
};
function highlightCodeBlock(code) {
  if (code.classList.contains('language-simple')) code.innerHTML = highlightSimple(code.textContent);
  if (code.classList.contains('language-bash')) code.innerHTML = highlightBash(code.textContent);
}

const exampleSelect = document.getElementById('example-select');
const heroExample = document.getElementById('hero-example');
const exampleStatus = document.getElementById('example-status');
function updateHeroExample() {
  if (!exampleSelect || !heroExample) return;
  const key = exampleSelect.value;
  heroExample.textContent = examples[key] || examples.toolScript;
  if (exampleStatus) {
    const isWip = Boolean(exampleMeta[key]?.wip);
    exampleStatus.hidden = !isWip;
  }
  highlightCodeBlock(heroExample);
}
if (exampleSelect && heroExample) {
  exampleSelect.addEventListener('change', updateHeroExample);
  updateHeroExample();
}

for (const code of document.querySelectorAll('code.language-simple, code.language-bash')) {
  highlightCodeBlock(code);
}
