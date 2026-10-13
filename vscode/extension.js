// Plan Viewer: a plan-review session inside VS Code. The review page runs in a webview, and
// every name in the plan's prose and diagrams is resolved through VS Code's own language
// servers: hovering shows their hover, clicking opens the definition beside the plan.
//
// It finds sessions two ways: the broker hands one over by URI
// (vscode://shyamalaravind.plan-viewer/session?key=<key>), and every waiting session is
// announced in $XDG_STATE_HOME/plan-review/sessions/<key>.json, which this watches.
//
// In the review instance (a VS Code that vscode/install.sh sets up for reviews alone, where
// planViewer.reviewWindow is on), a window exists for the reviews of its folder: it shows
// them without a URI, and once none is waiting it closes, so its language servers go too.
const vscode = require('vscode');
const crypto = require('crypto');
const fs = require('fs');
const os = require('os');
const path = require('path');

const SESSIONS = path.join(process.env.XDG_STATE_HOME || path.join(os.homedir(), '.local', 'state'),
  'plan-review', 'sessions');

const reviews = new Map();  // session key → Review shown in this window
const dismissed = new Set();  // URLs of rounds whose tab was closed here, not to reopen
let status;
let log;  // "Plan Viewer" in the Output panel, also kept in the window's log folder

// ---------------------------------------------------------------- sessions

function alive(pid) {
  try { process.kill(pid, 0); return true; } catch (e) { return e.code === 'EPERM'; }
}

function readSession(key) {
  try {
    const s = JSON.parse(fs.readFileSync(path.join(SESSIONS, key + '.json'), 'utf8'));
    return alive(s.pid) ? s : null;
  } catch { return null; }
}

function waitingSessions() {
  let names = [];
  try { names = fs.readdirSync(SESSIONS).filter(n => n.endsWith('.json')); } catch {}
  return names.map(n => readSession(n.slice(0, -5))).filter(Boolean);
}

function show(session) {
  log.info(`showing ${session.key}, round ${session.round}, from ${session.url}`);
  const review = reviews.get(session.key);
  if (!review) return new Review(session);
  if (review.session.url !== session.url) review.revise(session);
  review.panel.reveal();
  return review;
}

// Brings the next round of a plan shown here into its panel, notices rounds that ended
// without this window, and counts the reviews waiting that this window doesn't show. A
// review window shows the ones for its folder instead.
function refresh() {
  const waiting = waitingSessions();
  for (const s of waiting) {
    const review = reviews.get(s.key);
    if (review && review.session.url !== s.url) review.revise(s);
  }
  for (const review of reviews.values())
    if (review.open && !waiting.some(s => s.url === review.session.url)) review.ended(verdictOf(review.session));
  if (reviewWindow()) {
    for (const s of waiting)
      if (!reviews.has(s.key) && !dismissed.has(s.url) && ownFolder(s.root)) show(s);
    return status.hide();
  }
  const unseen = waiting.filter(s => !reviews.has(s.key));
  status.text = `$(comment-discussion) ${unseen.length} plan${unseen.length === 1 ? '' : 's'} to review`;
  status.tooltip = unseen.map(s => s.title || s.name).join('\n');
  unseen.length ? status.show() : status.hide();
}

// How a session's round ended, from the broker's <key>.round: the verdict on its second
// line, or `changes` once the next round has begun. A broker that was stopped leaves none.
function verdictOf(session) {
  let lines = [];
  try { lines = fs.readFileSync(path.join(SESSIONS, session.key + '.round'), 'utf8').split('\n'); } catch {}
  if (parseInt(lines[0], 10) > session.round) return 'changes';
  return lines[1] || 'end';
}

function updateContext() {
  vscode.commands.executeCommand('setContext', 'planViewer.reviewing', [...reviews.values()].some(r => r.open));
}

// ---------------------------------------------------------------- review windows

const REVISION_WAIT = 10 * 60 * 1000;  // how long a window waits for a revised plan

const reviewWindow = () => vscode.workspace.getConfiguration('planViewer').get('reviewWindow', false);

function realpath(p) {
  try { return fs.realpathSync(p); } catch { return p; }
}

// The broker gives the root as a real path.
const ownFolder = root => (vscode.workspace.workspaceFolders || []).some(f => realpath(f.uri.fsPath) === root);

// One file per review window, named by its extension host's pid, so the last window to
// close can quit the review instance: on macOS a VS Code with no window keeps running.
let windowsDir;

function liveWindows() {
  let names = [];
  try { names = fs.readdirSync(windowsDir); } catch {}
  return names.map(Number).filter(pid => {
    if (pid && alive(pid)) return true;
    try { fs.unlinkSync(path.join(windowsDir, String(pid))); } catch {}
    return false;
  });
}

let closeTimer;

// Once no review here waits for feedback, a review window closes: at once when they're
// done, or after REVISION_WAIT when changes were asked for and no revised plan came back.
function settle() {
  clearTimeout(closeTimer);
  if (!reviewWindow()) return;
  const all = [...reviews.values()];
  if (all.some(r => r.open)) return;
  const wait = all.some(r => r.verdict === 'changes') ? REVISION_WAIT : 1500;
  closeTimer = setTimeout(() => {
    const last = !liveWindows().some(pid => pid !== process.pid);
    log.info(`no review waiting: ${last ? 'quitting the review instance' : 'closing this window'}`);
    vscode.commands.executeCommand(last ? 'workbench.action.quit' : 'workbench.action.closeWindow');
  }, wait);
}

// ---------------------------------------------------------------- code lookup

const bare = name => name.replace(/\(.*$/, '').trim();
const escapeRe = s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');

async function findFile(file, root) {
  if (path.isAbsolute(file)) return fs.existsSync(file) ? vscode.Uri.file(file) : null;
  const direct = path.join(root, file);
  if (fs.existsSync(direct)) return vscode.Uri.file(direct);
  const found = await vscode.workspace.findFiles('**/' + file, '**/{node_modules,.git,dist,out,bin,obj}/**', 20);
  // Under the plan's root first, then the shortest path.
  found.sort((a, b) => (b.fsPath.startsWith(root) - a.fsPath.startsWith(root)) || a.fsPath.length - b.fsPath.length);
  return found[0] || null;
}

// A document's symbols (a tree, or a flat list) searched for `name`, inside `parent` when
// the plan qualified it (Broker.render, JParser::value).
function findSymbol(symbols, parts) {
  const last = parts[parts.length - 1], parent = parts[parts.length - 2];
  for (const strict of parent ? [true, false] : [false]) {
    let found = null;
    const walk = (list, container) => {
      for (const s of list) {
        if (found) return;
        if (bare(s.name) === last && (!strict || container === parent || s.containerName === parent)) found = s;
        else if (s.children) walk(s.children, bare(s.name));
      }
    };
    walk(symbols, '');
    if (found) return found;
  }
  return null;
}

// Where a symbol's name starts, so a hover there is about the symbol rather than a keyword.
async function nameStart(uri, range, name) {
  const doc = await vscode.workspace.openTextDocument(uri);
  const at = doc.getText(range).search(new RegExp('\\b' + escapeRe(name) + '\\b'));
  return at < 0 ? range.start : doc.positionAt(doc.offsetAt(range.start) + at);
}

// ---------------------------------------------------------------- the panel

const BRIDGE = mermaid => `(() => {
  const vscode = acquireVsCodeApi();
  const waiting = new Map();
  let seq = 0;
  addEventListener('message', e => {
    const m = e.data;
    if (m.reply) { waiting.get(m.reply)?.[m.ok ? 0 : 1](m.value); waiting.delete(m.reply); }
    else if (m.type === 'comment') window.planPage?.comment(m.anchor);
  });
  const ask = (type, args) => new Promise((ok, fail) => {
    const id = ++seq;
    waiting.set(id, [ok, fail]);
    vscode.postMessage({ id, type, args });
  });
  window.planHost = {
    // The key is the tab's saved state, so VS Code can restore the tab after a restart.
    session: () => ask('session').then(s => { vscode.setState({ key: s.key }); return s; }),
    feedback: body => ask('feedback', body),
    leave: () => {},
    asset: () => ${JSON.stringify(mermaid)},
    resolve: (keys, files) => ask('resolve', { keys, files }),
    hover: key => ask('hover', key),
    open: key => ask('open', key),
    onRevised: () => {},  // a revised round reloads the page in this tab
    shown: stats => vscode.postMessage({ type: 'shown', args: stats }),
  };
})();`;

class Review {
  // A new tab, or `panel`: one VS Code restored from before it quit.
  constructor(session, panel = null) {
    this.session = session;
    this.open = true;            // this round still waits for feedback
    this.verdict = null;         // how the round ended: approve, changes or end
    this.locations = new Map();  // name key → Promise<Location | null>
    this.mentioned = [];         // the files the plan mentions, searched first for bare names
    this.symbols = new Map();    // file → Promise<[uri, document symbols]>
    this.shown = null;           // what the page reported once drawn
    const options = { enableScripts: true, localResourceRoots: [vscode.Uri.file(session.assets)] };
    this.panel = panel || vscode.window.createWebviewPanel('planViewer', this.title(), vscode.ViewColumn.Active,
      { ...options, retainContextWhenHidden: true });
    if (panel) {
      panel.webview.options = options;
      panel.title = this.title();
    }
    this.panel.webview.html = this.html();
    this.panel.webview.onDidReceiveMessage(m => this.handle(m));
    this.panel.onDidDispose(() => this.dispose());
    reviews.set(session.key, this);
    updateContext();
    refresh();
    settle();
  }

  title() { return 'Review · ' + (this.session.title || this.session.name); }

  // The same page the browser shows, with a host bridge in place of fetch, under a
  // policy that runs only its own scripts and Mermaid from the skill's folder.
  html() {
    const dir = this.session.assets, webview = this.panel.webview;
    const nonce = crypto.randomBytes(16).toString('base64');
    const mermaid = webview.asWebviewUri(vscode.Uri.file(path.join(dir, 'mermaid.min.js'))).toString();
    const csp = `default-src 'none'; img-src ${webview.cspSource} data: https:; `
      + `style-src ${webview.cspSource} 'unsafe-inline'; font-src ${webview.cspSource}; `
      + `script-src 'nonce-${nonce}' ${webview.cspSource};`;
    const marked = fs.readFileSync(path.join(dir, 'marked.min.js'), 'utf8');
    return fs.readFileSync(path.join(dir, 'page.html'), 'utf8')
      .replace(/<script>/g, `<script nonce="${nonce}">`)
      .replace('<head>', `<head>\n<meta http-equiv="Content-Security-Policy" content="${csp}">`
        + `\n<script nonce="${nonce}">${BRIDGE(mermaid)}</script>`)
      .replace('/*MARKED*/', () => marked);
  }

  async handle({ id, type, args }) {
    if (type === 'shown') { this.shown = args; return; }
    let ok = true, value = null;
    try {
      if (type === 'session') value = await this.fetchSession();
      else if (type === 'feedback') await this.post(args);
      else if (type === 'resolve') {
        this.mentioned = args.files || [];
        value = await Promise.all(args.keys.map(k => this.locate(k).then(Boolean)));
        log.info(`${this.session.key}: ${value.filter(Boolean).length} of ${value.length} names found in the code`);
      }
      else if (type === 'hover') value = await this.hover(args);
      else if (type === 'open') await this.reveal(args);
    } catch (e) {
      ok = false;
      value = String(e.message || e);
      log.warn(`${this.session.key}: ${type} failed: ${value}`);
    }
    if (id) this.panel.webview.postMessage({ reply: id, ok, value });
  }

  async fetchSession() {
    const r = await fetch(this.session.url + 'session');
    if (!r.ok) throw new Error('the review has stopped');
    return r.json();
  }

  async post(body) {
    const r = await fetch(this.session.url + 'feedback', {
      method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body),
    });
    if (!r.ok) throw new Error('the review has stopped');
    this.ended(body.verdict);
  }

  // This round has its answer, given here or elsewhere.
  ended(verdict) {
    log.info(`${this.session.key}: round ${this.session.round} ended: ${verdict}`);
    this.open = false;
    this.verdict = verdict;
    updateContext();
    settle();
  }

  // The next round of the same plan: same panel, new broker. The page loads afresh, so it
  // starts from a clean view and from whatever page.html the skill has now.
  revise(session) {
    log.info(`round ${session.round} of ${session.key}`);
    this.session = session;
    this.open = true;
    this.verdict = null;
    this.locations.clear();
    this.symbols.clear();
    this.panel.title = this.title();
    this.panel.webview.html = this.html();
    this.panel.reveal(undefined, true);
    updateContext();
    settle();
  }

  // ---- names → code

  locate(key) {
    const id = JSON.stringify(key);
    if (!this.locations.has(id)) {
      const found = this.find(key).catch(() => null);
      this.locations.set(id, found);
      // Not found may only mean the language server isn't ready: ask again next time.
      found.then(loc => { if (!loc) this.locations.delete(id); });
    }
    return this.locations.get(id);
  }

  // A file's symbols, asked for once per round. Document symbols need no project, so they
  // work where workspace search can't (plain JavaScript with no jsconfig, files not open).
  fileSymbols(file) {
    if (!this.symbols.has(file)) {
      const found = findFile(file, this.session.root).then(async uri => uri
        ? [uri, await vscode.commands.executeCommand('vscode.executeDocumentSymbolProvider', uri) || []]
        : [null, []]).catch(() => [null, []]);
      this.symbols.set(file, found);
      // None yet may mean the language server is still starting: ask again next time.
      found.then(([, symbols]) => { if (!symbols.length) this.symbols.delete(file); });
    }
    return this.symbols.get(file);
  }

  async inFile(file, parts) {
    const [uri, symbols] = await this.fileSymbols(file);
    const s = uri && findSymbol(symbols, parts);
    if (!s) return null;
    const start = s.selectionRange ? s.selectionRange.start : await nameStart(uri, s.location.range, parts[parts.length - 1]);
    return new vscode.Location(uri, start);
  }

  async find({ file, name, line }) {
    const root = this.session.root;
    if (!name) {
      const uri = await findFile(file, root);
      return uri && new vscode.Location(uri, new vscode.Position(Math.max((line || 1) - 1, 0), 0));
    }
    const parts = name.split(/::|\.|#/), last = parts[parts.length - 1];
    if (file) return this.inFile(file, parts);
    // A bare name: the files the plan mentions first, then the whole workspace.
    for (const f of this.mentioned) {
      const found = await this.inFile(f, parts);
      if (found) return found;
    }
    const symbols = await vscode.commands.executeCommand('vscode.executeWorkspaceSymbolProvider', last) || [];
    const parent = parts[parts.length - 2];
    const hits = symbols.filter(s => bare(s.name) === last && !/[\\/]node_modules[\\/]/.test(s.location.uri.fsPath)
      && (!parent || (s.containerName || '').split(/[.:]/).pop() === parent));
    hits.sort((a, b) => (b.location.uri.fsPath.startsWith(root) - a.location.uri.fsPath.startsWith(root))
      || a.location.uri.fsPath.length - b.location.uri.fsPath.length);
    if (!hits.length) return null;
    const best = hits[0].location;
    return new vscode.Location(best.uri, await nameStart(best.uri, best.range, last));
  }

  async hover(key) {
    const loc = await this.locate(key);
    if (!loc) return null;
    const where = vscode.workspace.asRelativePath(loc.uri) + ':' + (loc.range.start.line + 1);
    if (!key.name) return { markdown: '', where };
    const hovers = await vscode.commands.executeCommand('vscode.executeHoverProvider', loc.uri, loc.range.start) || [];
    const parts = hovers.flatMap(h => h.contents).map(c =>
      typeof c === 'string' ? c : c.language ? '```' + c.language + '\n' + c.value + '\n```' : c.value);
    return { markdown: parts.filter(Boolean).join('\n\n---\n\n'), where };
  }

  // Opens the code in the editor group beside the plan, so the two sit side by side.
  async reveal(key) {
    const loc = await this.locate(key);
    if (!loc) throw new Error('not found in the code');
    const start = loc.range.start;
    const range = key.line && key.end > key.line
      ? new vscode.Range(start, new vscode.Position(key.end - 1, Number.MAX_SAFE_INTEGER))
      : new vscode.Range(start, start);
    const editor = await vscode.window.showTextDocument(loc.uri, {
      viewColumn: vscode.ViewColumn.Beside, preserveFocus: false, selection: range,
    });
    editor.revealRange(range, vscode.TextEditorRevealType.InCenterIfOutsideViewport);
  }

  // Closing the panel mid-round ends the review, as closing the browser tab does.
  dispose() {
    log.info(`closed ${this.session.key}${this.open ? ', which ends its round' : ''}`);
    reviews.delete(this.session.key);
    if (this.open) {
      dismissed.add(this.session.url);
      fetch(this.session.url + 'feedback', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ verdict: 'end', leaving: true }),
      }).catch(() => {});
    }
    updateContext();
    refresh();
    settle();
  }
}

// ---------------------------------------------------------------- commands

// The selected lines become a comment in the review whose code they belong to.
function commentOnSelection() {
  const editor = vscode.window.activeTextEditor;
  if (!editor) return;
  const file = editor.document.uri.fsPath;
  const open = [...reviews.values()].filter(r => r.open);
  const review = open.find(r => file.startsWith(r.session.root + path.sep)) || open[open.length - 1];
  if (!review) return;
  const sel = editor.selection;
  const end = sel.end.character === 0 && sel.end.line > sel.start.line ? sel.end.line : sel.end.line + 1;
  const anchor = {
    kind: 'code',
    file: file.startsWith(review.session.root + path.sep) ? path.relative(review.session.root, file)
      : vscode.workspace.asRelativePath(editor.document.uri),
    start: sel.start.line + 1,
    end,
    quote: editor.document.getText(sel).trim().slice(0, 500),
  };
  review.panel.reveal(undefined, false);
  review.panel.webview.postMessage({ type: 'comment', anchor });
}

async function openWaiting() {
  const waiting = waitingSessions();
  if (!waiting.length) return vscode.window.showInformationMessage('No plan is waiting for review.');
  const pick = waiting.length === 1 ? { session: waiting[0] } : await vscode.window.showQuickPick(
    waiting.map(s => ({ label: s.title || s.name, description: s.root, session: s })),
    { placeHolder: 'Plan to review' });
  if (pick) show(pick.session);
}

function activate(context) {
  log = vscode.window.createOutputChannel('Plan Viewer', { log: true });
  fs.mkdirSync(SESSIONS, { recursive: true });
  if (reviewWindow()) {
    windowsDir = path.join(context.globalStorageUri.fsPath, 'windows');
    fs.mkdirSync(windowsDir, { recursive: true });
    fs.writeFileSync(path.join(windowsDir, String(process.pid)), '');
    log.info('a review window');
  }
  status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
  status.command = 'planViewer.openWaiting';
  let timer;
  const watcher = fs.watch(SESSIONS, () => { clearTimeout(timer); timer = setTimeout(refresh, 150); });
  context.subscriptions.push(
    log,
    status,
    { dispose: () => watcher.close() },
    vscode.commands.registerCommand('planViewer.openWaiting', openWaiting),
    vscode.commands.registerCommand('planViewer.commentOnSelection', commentOnSelection),
    vscode.window.registerUriHandler({
      handleUri(uri) {
        const key = new URLSearchParams(uri.query).get('key') || '';
        log.info(`asked by URI for ${key}`);
        const session = readSession(key);
        if (session) show(session);
        else vscode.window.showWarningMessage(`No plan review "${key}" is waiting.`);
      },
    }),
    // A review tab open when VS Code quit comes back with it: on the same round if that is
    // still waiting, and closed if the review has ended since.
    vscode.window.registerWebviewPanelSerializer('planViewer', {
      async deserializeWebviewPanel(panel, state) {
        const session = state?.key && readSession(state.key);
        log.info(`restoring ${state?.key || 'a review tab'}: ${session ? 'still waiting' : 'ended, closing it'}`);
        if (session && !reviews.has(session.key)) new Review(session, panel);
        else panel.dispose();
      },
    }),
  );
  refresh();
  settle();  // a review window with no review to show closes, too
  return { show, reviews, SESSIONS };  // for the integration test
}

function deactivate() {
  if (windowsDir) try { fs.unlinkSync(path.join(windowsDir, String(process.pid))); } catch {}
}

module.exports = { activate, deactivate };
