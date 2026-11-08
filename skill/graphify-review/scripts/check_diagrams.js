#!/usr/bin/env node
// Draws every ```mermaid block of a canvas file with Pele (pele.min.js, the diagram parser
// the review page uses), and reports each block that doesn't draw.
//
//   node <skill-dir>/scripts/check_diagrams.js <file.md>
//
// Diagram numbers count the mermaid blocks from the top of the file, the intro's diagrams
// included, the way review comments number them.
//
// Exit 0: every block draws ("All N diagrams draw.", or "No diagrams.").
// Exit 1: one entry per failure, such as
//           Diagram 3, file line 43: Parse error on line 2:
//         followed by Pele's message. "file line" is where the error sits in the file;
//         Pele's own "line 2" counts from the block's first line.
// Blocks are found the way the page finds them, with its own marked: a ```mermaid fence
// anywhere, including inside lists and blockquotes, but not an indented code block.
// Exit 2: no failures found, but some blocks couldn't be checked (listed). Read those by eye.
'use strict';
const fs = require('fs');
const path = require('path');
const vm = require('vm');

const file = process.argv[2];
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

const src = fs.readFileSync(file, 'utf8').replace(/\r\n?/g, '\n');
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

if (!blocks.length) console.log('No diagrams.');
else if (broken) { console.log(`${broken} of ${blocks.length} diagrams don't draw.`); process.exit(1); }
else if (unchecked.length) { console.log(`${unchecked.length} of ${blocks.length} diagrams couldn't be checked.`); process.exit(2); }
else console.log(`All ${blocks.length} diagrams draw.`);
