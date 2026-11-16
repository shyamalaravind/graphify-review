#!/usr/bin/env node
// Draws every ```mermaid block of a canvas with Pele, the review page's parser, and reports
// each block that doesn't draw; then one line per writing warning:
//   warn: line <N>: <what is past its limit, by how much, and what to change>: <the text>
// Each warning says what to change, so an author never needs to read this file.
//
//   node <skill-dir>/scripts/check_diagrams.js <file.md>
//
// Exit 0: every block draws. Exit 1: one entry per failure ("Diagram 3, file line 43: …",
// diagrams counted from the top of the file, Pele's message after it). Exit 2: some blocks,
// the file or the writing limits couldn't be checked ("couldn't check …"). Warnings never
// change the exit code. The 40-word limit covers a frame's bullets and paragraphs, not the
// intro. (Maintainers: --heights also prints each flowchart's height with its cards.)
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const args = process.argv.slice(2), HEIGHTS = args.includes('--heights');
const file = args.find(a => a !== '--heights');
if (!file) { console.error('usage: node check_diagrams.js <file.md>'); process.exit(2); }

// Pele draws the diagrams; marked (marked.min.js) turns the file into the page, and the page
// takes every <code class="language-mermaid"> it makes as a diagram, in order. So the same
// marked finds the blocks here, wherever they sit: in a list, a blockquote, or at the top.
const ctx = { console };
ctx.window = ctx.self = ctx.globalThis = ctx;
let pele, marked;
try {
  vm.createContext(ctx);
  for (const lib of ['pele.min.js', 'marked.min.js'])
    vm.runInContext(fs.readFileSync(path.join(__dirname, '..', lib), 'utf8'), ctx);
  ({ pele, marked } = ctx);
  if (!pele || typeof pele.render !== 'function') throw new Error('pele.render not found');
  if (!marked || typeof marked.lexer !== 'function') throw new Error('marked.lexer not found');
} catch (e) {
  console.error(`couldn't check: loading the page's libraries failed: ${e.message}`);
  process.exit(2);
}

let src;
try { src = fs.readFileSync(file, 'utf8').replace(/\r\n?/g, '\n'); }
catch (e) { console.error(`couldn't check: reading ${file} failed: ${e.message}`); process.exit(2); }
const blocks = [];
let cursor = 0;
marked.walkTokens(marked.lexer(src), t => {
  if (t.type !== 'code' || (t.lang || '').match(/^\S*/)[0] !== 'mermaid') return;
  // The line the block's first line of source sits on, found after the previous block.
  const first = t.text.split('\n')[0];
  const at = first ? src.indexOf(first, cursor) : -1;
  if (at >= 0) cursor = at + first.length;
  const line = src.slice(0, at >= 0 ? at : cursor).split('\n').length;
  blocks.push({ line, source: t.text });
});

let broken = 0;
const unchecked = [];
blocks.forEach((b, n) => {
  try {
    pele.render(b.source, { idPrefix: `check${n}-` });
  } catch (e) {
    if (!e || e.name !== 'PeleError') {
      unchecked.push(`Diagram ${n + 1}, file line ${b.line}: couldn't check (${e && e.name}: ${e && e.message})`);
      return;
    }
    broken++;
    const [first, ...rest] = String(e.message).split('\n');
    const inBlock = Number.isInteger(e.line) ? e.line : Number((first.match(/line (\d+)/) || [])[1]);
    console.log(`Diagram ${n + 1}, file line ${inBlock ? b.line + inBlock - 1 : b.line}: ${first}`);
    for (const r of rest) if (r.trim()) console.log(`  ${r}`);
  }
});
for (const u of unchecked) console.log(u);

let code = 0;
if (!blocks.length) console.log('No diagrams.');
else if (broken) { console.log(`${broken} of ${blocks.length} diagrams don't draw.`); code = 1; }
else if (unchecked.length) { console.log(`${unchecked.length} of ${blocks.length} diagrams couldn't be checked.`); code = 2; }
else console.log(`All ${blocks.length} diagrams draw.`);
const heights = [];
try { for (const w of [...writingWarnings(src), ...cardWarnings(src), ...frameWarnings(src), ...coreWarnings(src)].sort((a, b) => a[0] - b[0])) console.log(`warn: line ${w[0]}: ${w[1]}`); }
catch (e) { console.log(`couldn't check the writing limits: ${e.message}`); if (!code) code = 2; }
for (const h of heights) console.log(h);
process.exit(code);

// --- Writing warnings ---------------------------------------------------------------

// Text as the page draws it: code-span backticks, emphasis marks and link syntax removed.
function drawn(text) {
  return text
    .replace(/!?\[([^\]]*)\]\([^)]*\)/g, '$1')
    .replace(/`+/g, '')
    .replace(/\*\*|__/g, '')
    .replace(/(^|[^\w*])\*(?=\S)([^*]*?\S)\*(?![\w*])/g, '$1$2')
    .replace(/(^|[^\w])_(?=\S)([^_]*?\S)_(?!\w)/g, '$1$2')
    .replace(/<br\s*\/?>/gi, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

// The words of a text as drawn, lower-cased, without punctuation.
function words(text) {
  return drawn(text).toLowerCase().replace(/[^\p{L}\p{N}\s]+/gu, ' ').split(/\s+/).filter(Boolean);
}

function writingWarnings(src) {
  const lineOf = offset => src.slice(0, offset).split('\n').length;
  const warnings = [];   // [line, text]
  const units = [];      // a paragraph, list item or table cell: its words, and each word's line

  // A unit's words, each with the file line it sits on (counted line by line of its raw text).
  const addUnit = (line, text, raw) => {
    const w = words(text);
    if (w.length < 8) return;
    let lines = raw.split('\n').flatMap((l, k) => words(l).map(() => line + k));
    if (lines.length < w.length) lines = w.map(() => line);
    units.push({ words: w, lines });
  };

  // Where a token starts: its first non-empty line, searched for from the last token on.
  let cursor = 0;
  const locate = raw => {
    const first = (raw.split('\n').find(l => l.trim()) || '').trim();
    const at = first ? src.indexOf(first, cursor) : -1;
    if (at >= 0) cursor = at;
    return cursor;
  };

  // A frame's text is short bullets, with detail in sub-bullets (rule 14). The intro may run
  // in sentences, so the limit starts at the first ## heading. The setup line and the done
  // check may restate what cards and arrows say (rules 8 and 11), so they stay out of the
  // repeat index.
  const exempt = text => /^(in the example\b|\*\*done when:?\*\*)/i.test(text.trim());
  const MAX_WORDS = 40;
  let inFrame = false;
  // A code span counts as one word, so a path or a command reads at its length on screen.
  const long = (line, text, what) => {
    const w = words(text.replace(/`[^`]*`/g, 'code'));
    if (inFrame && w.length > MAX_WORDS)
      warnings.push([line, `${what} is ${w.length} words, ${MAX_WORDS} fit: move its detail into sub-bullets: "${w.slice(0, 8).join(' ')} …"`]);
  };

  const walk = (tokens, inList) => {
    for (const t of tokens) {
      const line = lineOf(t.raw ? locate(t.raw) : cursor);
      if (t.type === 'heading') {
        const h = drawn(t.text);
        if (t.depth === 2 && h.length > 70)
          warnings.push([line, `heading is ${h.length} characters, 70 fit: cut ${h.length - 70}: "${h}"`]);
        if (t.depth === 2) inFrame = true;
      } else if (t.type === 'paragraph') {
        const p = drawn(t.text);
        if (!inList && p.includes('→') && p.length > 85)
          warnings.push([line, `text arrow flow is ${p.length} characters, 85 fit: cut ${p.length - 85}, or make it numbered steps: "${p}"`]);
        if (!inList) long(line, t.text, 'paragraph');
        if (!exempt(t.text)) addUnit(line, t.text, t.raw);
      } else if (t.type === 'list') {
        for (const item of t.items) {
          const itemLine = lineOf(locate(item.raw));
          const own = item.tokens.filter(c => c.type === 'text' || c.type === 'paragraph');
          if (!exempt(item.text)) addUnit(itemLine, own.map(c => c.text).join('\n'), item.raw);
          long(itemLine, own.map(c => c.text).join('\n'), 'bullet');
          walk(item.tokens.filter(c => !own.includes(c)), true);
        }
      } else if (t.type === 'table') {
        t.header.forEach(c => addUnit(line, c.text, c.text));
        t.rows.forEach((row, r) => row.forEach(c => addUnit(line + 2 + r, c.text, c.text)));
      } else if (t.type === 'blockquote') {
        walk(t.tokens, inList);
      }
    }
  };
  walk(marked.lexer(src), false);

  // 8 words repeated. Index every run of 8 words; for each place a run comes back, extend it
  // to the longest shared run and report it once, against the run's first place.
  const N = 8, first = new Map(), reported = new Set();
  units.forEach((u, ui) => {
    for (let i = 0; i + N <= u.words.length; i++) {
      const key = u.words.slice(i, i + N).join(' ');
      const f = first.get(key);
      if (!f) { first.set(key, [ui, i]); continue; }
      let [ua, ia] = f, ib = i;
      if (ua === ui && ib - ia < N) continue;
      const A = units[ua].words, B = u.words;
      while (ia > 0 && ib > 0 && A[ia - 1] === B[ib - 1]) { ia--; ib--; }
      const id = `${ua}:${ia}:${ui}:${ib}`;
      if (reported.has(id)) continue;
      reported.add(id);
      let n = 0;
      while (ia + n < A.length && ib + n < B.length && A[ia + n] === B[ib + n]) n++;
      warnings.push([u.lines[ib],
        `${n} words also at line ${units[ua].lines[ia]}: say it once, where the reader first needs it: "${A.slice(ia, ia + n).join(' ')}"`]);
    }
  });

  return warnings;
}

// --- Card warnings (FORMAT.md "The page", rules 9, 10 and 20) -----------------------

// A flowchart's boxes, as the page reads them: each id's first label line, and whether
// it is a decision. The same pattern as page.html's SHAPE.
function plainLabel(s) { return drawn(s).replace(/[:.]\s*$/, '').replace(/\s+/g, ' ').trim().toLowerCase(); }
function boxesOf(source) {
  const SHAPE = /\b([A-Za-z_][\w-]*)(\[\[|\[\(|\(\[|\(\(|\{\{|\[|\(|\{)"([^"]*)"(\]\]|\)\]|\]\)|\)\)|\}\}|\]|\)|\})/g;
  const boxes = new Map();
  for (const m of source.matchAll(SHAPE)) {
    if (boxes.has(m[1])) continue;
    const title = m[3].replace(/<br\s*\/?>/gi, '\n').replace(/#quot;/g, '"').split('\n')[0];
    boxes.set(m[1], { id: m[1], title, plain: plainLabel(title), decision: m[2] === '{' || m[2] === '{{' });
  }
  return [...boxes.values()];
}

// Names as page.html finds them (FILE, IDENT, IN_TEXT, WHOLE_CODE, WHOLE_FILE, keyOf,
// codeKey, keyName), each reduced to the key a card and its box are matched by.
function nameRx() {
  if (!nameRx.r) {
    const FILE = String.raw`[\w@.\-/]*\w\.(?:[cm]?[jt]sx?|json|html?|s?css|less|md|mdx|vue|svelte|py|rb|go|rs|java|kt|swift|cs|c|cc|cpp|h|hpp|m|mm|php|sh|zsh|bash|toml|ya?ml|xml|sql|ini|conf|txt|lock|gradle|proto|graphql)\b`;
    const IDENT = String.raw`[A-Za-z_$][\w$]*(?:(?:::|\.|#)[A-Za-z_$][\w$]*)*`;
    nameRx.r = {
      IN_TEXT: new RegExp(String.raw`(${FILE}):\s*(${IDENT})(?:\(\))?|(${FILE}):(\d+)(?:-(\d+))?|\b(${IDENT})\(\)|(${FILE})`, 'g'),
      WHOLE_CODE: new RegExp(String.raw`^(?:(${FILE}):\s*)?(${IDENT})(\(\))?$`),
      WHOLE_FILE: new RegExp(String.raw`^(${FILE})(?::(\d+)(?:-(\d+))?)?$`),
    };
  }
  return nameRx.r;
}
function firstName(text) { return [...text.matchAll(nameRx().IN_TEXT)][0]; }
function nameKey(m) {
  if (m[1]) return m[2];
  if (m[3]) return `${m[3]}:${+m[4]}-${+(m[5] || m[4])}`;
  return m[6] || m[7];
}
function codeKey(text) {
  const f = nameRx().WHOLE_FILE.exec(text.trim());
  if (f) return f[2] ? `${f[1]}:${+f[2]}-${+(f[3] || f[2])}` : f[1];
  const m = nameRx().WHOLE_CODE.exec(text.trim());
  return m && (m[1] || m[3] || /[A-Z_.:#]/.test(m[2].slice(1))) ? m[2] : null;
}

// The list items the page reads as a diagram's cards: every item after it, up to the next
// heading or diagram (a ```text block doesn't stop them).
function isDiagram(t) { return t.type === 'code' && (t.lang || '').match(/^\S*/)[0] === 'mermaid'; }
function itemsAfter(tokens, i) {
  const items = [];
  for (let j = i + 1; j < tokens.length && tokens[j].type !== 'heading' && !isDiagram(tokens[j]); j++)
    if (tokens[j].type === 'list') items.push(...tokens[j].items);
  return items;
}

// Box id → its card, as page.html's entriesAfter() and describeNodes() pick it: an item is
// keyed by its bold label, and by a name it opens with (a code span the page links, or a
// plain-text name); the first item per key wins. A box takes the item with its full first
// label line, else the item named by the first name in its label.
function matchCards(boxes, items) {
  const entries = new Map();
  for (const it of items) {
    const text = it.text.split('\n')[0].trim();
    const lead = (text.match(/^\*\*(.+?)\*\*/) || [])[1];
    const span = text.match(/^(?:\*\*)?`([^`]+)`/);
    let name = null;
    if (span) name = codeKey(span[1]);
    else { const m = firstName(drawn(text)); if (m && m.index === 0) name = nameKey(m); }
    for (const k of [lead && 'label:' + plainLabel(lead), name]) if (k && !entries.has(k)) entries.set(k, it);
  }
  const byBox = new Map();
  for (const b of boxes) {
    const m = firstName(drawn(b.title));
    const it = entries.get('label:' + b.plain) || (m && entries.get(nameKey(m)));
    if (it) byBox.set(b.id, it);
  }
  return byBox;
}

// Lines as the tester counts them: an item and each of its sub-items, a paragraph, a table row.
function itemLines(item) {
  return 1 + item.tokens.filter(t => t.type === 'list')
    .reduce((n, l) => n + l.items.reduce((k, i) => k + itemLines(i), 0), 0);
}

// For each frame with a flowchart: a card whose label matches no box (a near miss stays
// outside), a decision's card over 2 lines, any card over 5, a card that starts
// mid-sentence, and too many lines outside the diagram. Rule 10's named outside lines (the
// setup line, a table of up to two rows, the done check) don't count toward that limit.
function cardWarnings(src) {
  const lineOf = offset => src.slice(0, offset).split('\n').length;
  const out = [];
  let cursor = 0;
  const at = raw => { const f = (raw.split('\n').find(l => l.trim()) || '').trim();
    const i = f ? src.indexOf(f, cursor) : -1; if (i >= 0) cursor = i; return lineOf(cursor); };
  let frame = null, boxes = null, cards = null;
  const close = () => {
    // Past one line, outside lines may be at most a quarter of the frame's lines.
    if (frame && frame.flowchart && frame.outside > 1 && frame.outside * 3 > frame.cards)
      out.push([frame.line, `${frame.outside} lines outside the diagram, ${frame.cards} in its cards; ${Math.max(1, Math.floor(frame.cards / 3))} may sit outside: move ${frame.outside - Math.max(1, Math.floor(frame.cards / 3))} into the cards of the boxes they're about: "${frame.heading}"`]);
  };
  const tokens = marked.lexer(src);
  tokens.forEach((t, ti) => {
    const line = t.raw ? at(t.raw) : lineOf(cursor);
    if (t.type === 'heading') {
      if (t.depth === 2) { close(); frame = { line, heading: drawn(t.text), cards: 0, outside: 0 }; }
      boxes = null;
      return;
    }
    if (!frame) return;
    if (isDiagram(t)) {
      boxes = /^\s*(flowchart|graph)\b/.test(t.text) ? boxesOf(t.text) : null;
      if (boxes) {
        frame.flowchart = true;
        cards = new Map();
        for (const [id, it] of matchCards(boxes, itemsAfter(tokens, ti)))
          if (!cards.has(it)) cards.set(it, boxes.find(b => b.id === id));
      }
      return;
    }
    if (t.type === 'paragraph') { if (!/^in the example\b/i.test(drawn(t.text))) frame.outside++; return; }
    if (t.type === 'table') { if (t.rows.length > 2) frame.outside += t.rows.length; return; }
    if (t.type !== 'list') return;
    for (const item of t.items) {
      const n = itemLines(item), itemLine = at(item.raw);
      const lead = (item.text.match(/^\*\*(.+?)\*\*/) || [])[1];
      const code = (item.text.match(/^(?:\*\*)?`([^`]+)`/) || [])[1];
      const box = boxes && cards.get(item);
      if (!box) {
        if (!(lead && plainLabel(lead) === 'done when')) frame.outside += n;
        if (boxes && lead && !code) {
          // Near: at least 2 words in common, by their first 5 letters, and half the longer label.
          const stems = l => l.split(' ').map(x => x.slice(0, 5));
          const w = new Set(stems(plainLabel(lead)));
          const near = boxes.map(b => { const shared = stems(b.plain).filter(x => w.has(x)).length;
            return [b, shared, shared / Math.max(w.size, stems(b.plain).length)]; })
            .sort((a, b) => b[2] - a[2])[0];
          if (near && near[1] >= 2 && near[2] >= 0.5)
            out.push([itemLine, `card label "${drawn(lead)}" matches no box, so it stays outside: make it the box's first label line exactly, "${drawn(near[0].title)}"`]);
        }
        continue;
      }
      frame.cards += n;
      // The page drops the bold label, so what follows it must start the card cleanly.
      const after = item.text.split('\n')[0].replace(/^\*\*.+?\*\*/, '').trim();
      if (/^[,;.]/.test(after) || (/:$/.test(after) && after.split(/\s+/).length <= 3 && !/^:/.test(after)))
        out.push([itemLine, `card "${drawn(box.title)}" draws as "${drawn(after).slice(0, 40)}" once its label is the title: follow the label with ": " and a clause that reads alone`]);
      if (box.decision && n > 2) out.push([itemLine, `decision card "${drawn(box.title)}" is ${n} lines, 2 fit: say what counts as yes; move what a branch does to its arrow or the next box's card`]);
      else if (n > 5) out.push([itemLine, `card "${drawn(box.title)}" is ${n} lines, 5 fit: split the box into two steps; a step that can't split keeps its summary in its card, the rest in a reference frame`]);
    }
  });
  close();
  return out;
}

// --- Frame warnings (FORMAT.md rules 8 and 12) --------------------------------------

// Each flowchart drawn with its cards put in its boxes, with page.html's options, so its
// height is the one the reader sees: 1100 and its heading fit one screen at the usual
// opening zoom (77%). Too tall, it is drawn with wider cards, as the page does, up to 1090
// wide. Also a frame that draws nothing (no diagram, text arrow flow or table), which a skim
// reads as its heading alone, and a later frame's setup line ("In the example, …") whose
// numbers all sit on the frame's arrows (frame 1's is the one rule 8 asks for).
function frameWarnings(src) {
  const SHAPE = /\b([A-Za-z_][\w-]*)(\[\[|\[\(|\(\[|\(\(|\{\{|\[|\(|\{)"([^"]*)"(\]\]|\)\]|\]\)|\)\)|\}\}|\]|\)|\})/g;
  const SCREEN = 1100, WIDE = 1090;
  const draw = (text, wrap, id) => pele.render(text, { fontSize: 14, config: { flowchart: { wrappingWidth: wrap } }, idPrefix: id });
  const numbers = text => text.match(/\d+(?:[.,]\d+)*/g) || [];
  const mermaidText = s => s.replace(/#/g, '#35;').replace(/"/g, '#quot;').replace(/</g, '#lt;')
    .replace(/>/g, '#gt;').replace(/`/g, "'");
  const cardBody = item => {
    const own = item.tokens.filter(t => t.type === 'text' || t.type === 'paragraph').map(t => drawn(t.text)).join(' ');
    const subs = item.tokens.filter(t => t.type === 'list').flatMap(l => l.items.map(i => '• ' + cardBody(i)));
    return [own, ...subs].join('\n');
  };
  const out = [], tokens = marked.lexer(src), core = coreFrames(src);
  let cursor = 0, position = 0;
  const lineAt = raw => { const f = (raw.split('\n').find(l => l.trim()) || '').trim();
    const i = f ? src.indexOf(f, cursor) : -1; if (i >= 0) cursor = i; return src.slice(0, cursor).split('\n').length; };
  let frame = null;
  const close = () => {
    if (frame && !frame.draws) out.push([frame.line, `frame draws nothing, so a skim sees only its heading: give it a diagram, a text arrow flow or a table: "${frame.heading}"`]);
    const own = frame && frame.setup && frame.num !== 1 ? numbers(frame.setup.text) : [];
    if (own.length && own.every(n => frame.arrows.has(n)))
      out.push([frame.setup.line, `setup line's numbers (${own.join(', ')}) are all on the frame's arrows: delete it, or keep only an input no arrow carries: "${frame.setup.text.slice(0, 60)}"`]);
  };
  tokens.forEach((t, i) => {
    const line = t.raw ? lineAt(t.raw) : 0;
    if (t.type === 'heading' && t.depth === 2) {
      close();
      const heading = drawn(t.text);
      // A frame's number is its heading's, else its place among the ## headings.
      frame = { line, heading, num: +(heading.match(/^(\d+)\./) || [])[1] || ++position, draws: false, arrows: new Set(), first: true };
      if (/^\d+\./.test(heading)) position = frame.num;
      return;
    }
    if (!frame) return;
    if (frame.first && t.type === 'paragraph' && /^in the example\b/i.test(drawn(t.text))) frame.setup = { line, text: drawn(t.text) };
    if (t.type !== 'space') frame.first = false;
    if (t.type === 'code') for (const m of t.text.matchAll(/\|"?([^|"]*)"?\||--\s*"([^"]*)"\s*--/g)) numbers(m[1] || m[2] || '').forEach(n => frame.arrows.add(n));
    if (t.type === 'table' || (t.type === 'paragraph' && drawn(t.text).includes('→'))) frame.draws = true;
    if (!isDiagram(t)) return;
    frame.draws = true;
    if (!/^\s*(flowchart|graph)\b/.test(t.text)) return;
    const items = itemsAfter(tokens, i);
    const byBox = matchCards(boxesOf(t.text), items);
    const seen = new Set();
    const carded = t.text.replace(SHAPE, (whole, id, open, label, close) => {
      if (seen.has(id)) return whole;
      seen.add(id);
      const item = byBox.get(id);
      if (!item) return whole;
      const title = label.replace(/<br\s*\/?>/gi, '\n').split('\n')[0];
      const body = cardBody(item).replace(/^[^\n]*?(:\s+|\?\s+)/, '');
      if (open === '{') { open = '{{'; close = '}}'; }
      return `${id}${open}"\`**${mermaidText(title)}**\n\n${mermaidText(body)}\`"${close}`;
    });
    let height = 0, bare = 0;
    try {
      height = Math.round(draw(carded, 340, `h${i}-`).height);
      bare = Math.round(draw(t.text, 340, `b${i}-`).height);
      for (const wrap of [460, 580, 700]) {
        if (height <= SCREEN) break;
        const r = draw(carded, wrap, `w${wrap}-${i}-`);
        if (r.width > WIDE) break;
        height = Math.min(height, Math.round(r.height));
      }
    } catch (e) { return; }
    if (HEIGHTS) heights.push(`height: line ${lineAt(t.raw) || frame.line}: ${height} of ${SCREEN}: "${frame.heading}"`);
    // Only a core frame must fit one screen; a reference frame may run longer.
    if (height > SCREEN && (!core || core.has(frame.num))) {
      // What one card line adds, on average, plus one line to spare, so one cut is enough.
      const lines = items.reduce((n, it) => n + itemLines(it), 0);
      const per = lines ? (height - bare) / lines : 0;
      const cut = per > 0 ? Math.ceil((height - SCREEN) / per) + 1 : 0;
      const longest = items.map(it => [(it.text.match(/^\*\*`?(.+?)`?\*\*|^`([^`]+)`/) || []).slice(1).find(Boolean), itemLines(it)])
        .filter(c => c[0]).sort((a, b) => b[1] - a[1]).slice(0, 3).map(c => `"${c[0]}" ${c[1]}`).join(', ');
      const fix = bare > SCREEN ? `the boxes alone are too tall: split it where the flow branches, the second part a reference frame; add it to the intro's Reference line`
        : `cut ${cut} of its ${lines} card lines, longest cards first (${longest}): move that detail to a reference frame, then stop`;
      out.push([lineAt(t.raw) || frame.line, `core diagram with its cards is ${height} tall, ${SCREEN} fits (${Math.round((height / SCREEN - 1) * 100)}% over): ${fix}: "${frame.heading}"`]);
    }
  });
  close();
  return out;
}

// --- Core frames (FORMAT.md rule 4) -------------------------------------------------

// The frame numbers on the intro's "Core: frames **1** …, **2–3** … Reference: …" line, a
// range counting each frame in it: the bold numbers, else every number on it (so "Core:
// frames 1 and 2" counts too). Null when the intro has no Core line.
function coreNumbers(text) {
  const bold = [...text.matchAll(/\*\*(\d+)(?:\s*[–-]\s*(\d+))?\*\*/g)];
  const runs = bold.length ? bold : [...text.matchAll(/\b(\d+)(?:\s*[–-]\s*(\d+))?\b/g)];
  const nums = [];
  for (const r of runs) for (let k = +r[1]; k <= +(r[2] || r[1]); k++) nums.push(k);
  return nums;
}
function coreFrames(src) {
  const m = src.split(/^## /m)[0].match(/\bCore:(.*?)(?:\bReference:|$)/m);
  return m ? new Set(coreNumbers(m[1])) : null;
}

function coreWarnings(src) {
  const head = src.split(/^## /m)[0].split('\n');
  for (let i = 0; i < head.length; i++) {
    const m = head[i].match(/\bCore:(.*?)(?:\bReference:|$)/);
    if (!m) continue;
    const nums = coreNumbers(m[1]);
    const n = nums.length, out = [];
    if (n > 4) out.push([i + 1, `intro lists ${n} core frames, 4 fit: keep the main flow in four, move ${n - 4} to reference; edit this line, each heading and every "frame N" that moves`]);
    if (nums.some((k, j) => k !== j + 1))
      out.push([i + 1, `core frames are ${nums.join(', ')}: core comes first, as frames 1 to ${n}; renumber so the main flow opens the canvas, editing each heading, this line and every "frame N" that moves`]);
    return out;
  }
  return [];
}
