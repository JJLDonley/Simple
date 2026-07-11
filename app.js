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
      else if (/^\s*\(/.test(rest) || /^\s*:\s*[A-Za-z_][A-Za-z0-9_.<>]*\s*\(/.test(rest)) out += `<span class="tok-function">${word}</span>`;
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
      const m = source.slice(i).match(/^(::|==|!=|<=|>=|->|\+=|-=|&&|\|\||[=:;+\-*\/<>!@|&.])/);
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

const examples = {
  toolScript: `import Standard.IO
import Standard.FS

main : i32 () {
  path :: string = "build.log"
  exists : bool = Standard.FS.exists(path)

  if (exists) {
    Standard.IO.println("{} exists", path)
    return 0
  }

  Standard.IO.println("{} missing", path)
  return 1
}`,
  artifactModel: `import Standard.IO

Vec2 :: artifact {
  x : f64
  y : f64

  lengthSquared : f64 () {
    return self.x * self.x + self.y * self.y
  }
}

Player :: artifact {
  name : string
  pos : Vec2
  hp : i32

  damage : void (amount : i32) {
    self.hp = self.hp - amount
  }

  alive : bool () {
    return self.hp > 0
  }
}

main : i32 () {
  p : Player = { .name = "Ada", .pos = { .x = 3.0, .y = 4.0 }, .hp = 10 }
  p.damage(3)
  Standard.IO.println("{} alive={} dist2={}", p.name, p.alive(), p.pos.lengthSquared())
  return 0
}`,
  namespaceApi: `import Standard.IO

Checksum :: namespace {
  step : i32 (hash : i32, value : i32) {
    return hash * 31 + value
  }

  list : i32 (values : i32[]) {
    hash : i32 = 17
    for (i : i32 = 0; i < len(values); i = i + 1) {
      hash = Checksum.step(hash, values[i])
    }
    return hash
  }
}

main : i32 () {
  values : i32[] = [4, 8, 15, 16, 23, 42]
  Standard.IO.println("checksum={}", Checksum.list(values))
  return 0
}`,
  moduleSurface: `module app.config

Config :: namespace {
  APP_NAME :: string = "simple-tool"
  VERSION :: string = "0.5.0"
  MAX_RETRIES :: i32 = 3
}

main : i32 () {
  // A real project imports this module by name.
  // import app.config
  return Config.MAX_RETRIES
}`,
  typedCollections: `import Standard.IO

Score :: artifact {
  name : string
  points : i32
}

main : i32 () {
  scores : Score[] = [
    { .name = "Ada", .points = 14 },
    { .name = "Lin", .points = 22 },
    { .name = "Ken", .points = 18 }
  ]

  best : i32 = 0
  for (i : i32 = 1; i < len(scores); i = i + 1) {
    if (scores[i].points > scores[best].points) {
      best = i
    }
  }

  Standard.IO.println("winner={} score={}", scores[best].name, scores[best].points)
  return scores[best].points
}`,
  ffiBinding: `import System.FFI
import Standard.IO

Raylib :: namespace {
  extern raylib.InitWindow : void (w : i32, h : i32, title : string)
  extern raylib.WindowShouldClose : bool ()
  extern raylib.BeginDrawing : void ()
  extern raylib.ClearBackground : void (color : i32)
  extern raylib.EndDrawing : void ()
  extern raylib.CloseWindow : void ()
}

main : i32 () {
  lib : i64 = System.FFI.open("libraylib.so", raylib)
  if (lib == 0) {
    Standard.IO.println("raylib load failed: {}", System.FFI.lastError())
    return 1
  }

  Raylib.InitWindow(800, 600, "Simple + raylib")
  while (!Raylib.WindowShouldClose()) {
    Raylib.BeginDrawing()
    Raylib.ClearBackground(0)
    Raylib.EndDrawing()
  }
  Raylib.CloseWindow()
  return 0
}`,
  systemFs: `// current canonical System.FS / Standard.IO shape
import System.FS
import Standard.IO

main : i32 () {
  path :: string = "simple.toml"
  if (System.FS.exists(path)) {
    text : string = System.FS.readText(path)
    Standard.IO.println(text)
    return 0
  }
  Standard.IO.println("missing {}", path)
  return 1
}`,
  httpServer: `// planned Standard.HTTP server shape
import Standard.HTTP
import Standard.IO

main : i32 () {
  server : Standard.HTTP.Server = Standard.HTTP.serve("127.0.0.1", 8080, handler)?
  Standard.IO.println("listening on http://127.0.0.1:8080")
  return server.wait()?
}

handler : Standard.HTTP.Response (request : Standard.HTTP.Request) {
  if (request.path == "/health") {
    return Standard.HTTP.Response.text(200, "ok")
  }
  return Standard.HTTP.Response.json(200, Standard.Json.object({ "name": "Simple" }))
}`,
  terminalLoop: `// planned Standard.Terminal primitives
import Standard.Terminal

main : i32 () {
  term : Standard.Terminal.Handle = Standard.Terminal.open()?
  Standard.Terminal.enterRaw(term)?
  Standard.Terminal.enterAltScreen(term)?
  Standard.Terminal.hideCursor(term)?

  running : bool = true
  while (running) {
    event : Option<Standard.Terminal.Event> = Standard.Terminal.pollEvent(term)
    if (event.hasValue()) {
      if (event.value().key == "q") {
        running = false
      }
    }
    Standard.Terminal.writeAt(term, 2, 2, "press q to quit")?
    Standard.Terminal.flush(term)?
  }

  Standard.Terminal.showCursor(term)?
  Standard.Terminal.exitAltScreen(term)?
  Standard.Terminal.close(term)?
  return 0
}`,
  asyncFlow: `// planned async result flow
import Standard.HTTP
import Standard.IO

main : i32 () {
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
if (exampleSelect && heroExample) {
  exampleSelect.addEventListener('change', () => {
    heroExample.textContent = examples[exampleSelect.value] || examples.hello;
    highlightCodeBlock(heroExample);
  });
}

for (const code of document.querySelectorAll('code.language-simple, code.language-bash')) {
  highlightCodeBlock(code);
}
