# The canvas format

How to write anything with logic in it so the reader understands it from the canvas: the
whole shape first, then the details, with every name one hover or click away from the
code. It is for any logic: a plan that changes code, how existing code works, or a
protocol or process that isn't code. A document that isn't logic (a resume, a letter)
needs none of it.

## What the page does with it

- The text before the first `##` is the **intro card**, at the top. Each `##` section is a
  **frame** below it, one under another, read top to bottom.
- The canvas opens at the zoom where the frames fit beside the comments panel, showing the
  intro and about one frame. **Fit** shows everything, too small to read. So the intro has
  to work on its own, as the map of the rest.
- Each ```` ```mermaid ```` block is drawn by Pele, which reads Mermaid's syntax.
- In a flowchart, a box named after code can carry a **card**: a list entry under the
  diagram that starts with the box's name moves into the box (see "Cards").
- Comments come back as "On diagram N", where N counts the mermaid blocks from the top,
  the intro's included.

## 1. Open with the whole picture

The intro card holds, in this order:

1. **What this is**, in two or three sentences. For a plan: what changes, and why.
2. **An overview**: the main flows, each on a line of its own as a text arrow flow
   (`You → ask for a review → Agent → writes → the canvas file`), or one diagram of at most
   seven boxes when a single flow covers everything. Never merge several flows into one
   diagram: the reader has to untangle them before reading anything. The frames below
   zoom into the flows.
3. **The frames in reading order**, one line: "Read the frames in order: **1** how a round
   starts, **2** …".
4. **A legend**, in one line, for any marks the diagrams use (see "Marks").

## 2. One frame per idea, in reading order

- Number the `##` headings (`## 2. How a round ends`), and make each say what its frame
  shows.
- Put the normal path first, then failures, edge cases and concurrency. A reader who
  meets the exceptions before the rule can't place them.
- A frame holds its flow or diagram (with the diagram's cards), then the text: why it
  works this way, what's risky, what was ruled out.
- Keep a diagram to one idea and about a dozen boxes. Split anything bigger into an
  overview and details.

## 3. Pick the form by the shape of the logic

| The logic is | Use |
| --- | --- |
| a short chain of steps | a text arrow flow, one line: `Agent → runs review → Broker → opens → the tab` |
| something moving between states | one text arrow flow per path, each line starting with what triggers it: **Tab closed:** `Waiting → goodbye sent → Leaving → 2 s pass → Ended` |
| messages between processes, services or people, where order matters | `sequenceDiagram`, with `alt` or `opt` for failures |
| a path a call, request or event takes through code | `flowchart` of real places in the code, each box with its card |
| rules and checks that branch and join again | `flowchart TD`, with `{"…?"}` decisions and yes / no on the edges |
| data with fields | a table |

A diagram earns its place when its boxes carry cards, or when its shape is the point:
branches that join again, loops, several parties talking. A diagram whose boxes hold a few
words each is a text arrow flow drawn small, and harder to read.

Write each text arrow flow as a paragraph of its own (a blank line before and after), not
in a code block or a tight list, with names in code spans so they link. Keep it to one
line, about 85 characters: a flow that wraps reads as a block of text. A flow that needs
more becomes numbered steps, one short line each, with a blank line between them.

Draw the failure, concurrency and edge paths too, not only the normal one: a rule left in
a sentence, or in a box's sub-bullet, is the one the reader misses. When a plan reshapes a
flow rather than adding to it, show a before and after pair.

## 4. Diagram and text each do their own job

- The **diagram** shows the shape: what talks to what, in what order, and which way each
  decision goes. Each edge says what passes along it (`|"verdict"|`), not just that
  something does.
- The **text** says what the diagram can't: why, what's risky, what was ruled out, and
  what happens at the limits.
- Say each fact once. Text that walks along the arrows again ("then A calls B, which…")
  is a second copy for the reader to compare.
- Explain every term a diagram uses that the reader can't click: a file written while it
  runs, a queue, a header. One line each, or a table: what it is, what writes it, what
  reads it, with those functions named so they link.

## 5. A plan that changes code

- **A code-flow flowchart** of the path a call, request or event takes through the code
  the change touches, from where it starts to where it ends. Each box is a real place in
  the code, labelled with its name: `file: function()`, or `file: Type` for a type or a
  field.
- **Marks** for what the change adds or alters (see "Marks"), so new and changed steps
  stand out from the ones it leaves alone.
- **A card for each box that changes**, saying what changes there and why.

## Cards

A card is a list item that moves into its box, which turns a flowchart into a set of
cards:

```markdown
- **`serve()`**: answers `POST feedback` as well, because …
```

- Put it in the stretch **between its diagram and the next diagram or heading**, and start
  it with the box's name in a code span. The page looks for a box's card only there, so
  give each diagram its own.
- Keep it to two or three short lines: what happens there, and in a plan, what changes and
  why. Sub-bullets are fine.
- The box's label keeps just the name. Give `file.ts:42-60` only for a block inside a
  longer function: the name already opens the code.
- Begin no other list item in that stretch with a box's name: the first one is the card.
- Only a box named after code can take a card. A box like "Authorization server" keeps a
  short label (a second line after `<br/>` at most), and its detail goes in the text.
- A diagram with its cards must fit one screen. If it doesn't, split it: an overview
  without cards, and details with them.

## Marks

```
classDef new stroke-width:3px,stroke-dasharray:6 3
classDef changed stroke-width:3px
classDef fail stroke:#dc2626,color:#dc2626
```

Apply them with `:::new`, `:::changed` or `:::fail`. Dashes mean new and nothing else, so
a lost message or a failure outcome takes `fail`. Say what each mark you use means in the
intro's legend: "Dashed border: new. Thick border: changed. Red: a failure. Orange name:
exists in the code."

## Make sure it draws

A block that Pele can't parse reaches the reader as raw source with an error under it.
The review's result tells you afterwards ("Diagrams that didn't draw"), but by then the
reader has seen it. Before opening the review, read each block for these:

- No `;` in a sequence diagram's messages or notes: it ends the statement. Write "," or
  "and".
- A label with punctuation goes in quotes (`A["serve(): POST /open"]`), and a line break
  inside it is `<br/>`.
- An edge label goes in quotes too: `A -->|"verdict, comments"| B`.

## Names

Write every function, type or file the canvas talks about in one of these forms, in prose
and in box labels alike. These are what the page can find in the code:

| Form | Example | Found by |
| --- | --- | --- |
| `file: name()`, or `file: Name` for a type or a field | `server.ts: serve()`, `session.ts: Session` | the symbols of that file |
| `name()`, qualified or not | `serve()`, `Broker.render()`, `JParser::value()` | the files the canvas mentions, then the workspace |
| a path, with lines or without | `src/server.ts:42-60`, `skill/graphify-review/page.html` | the file |

- In a code span, a bare identifier also counts when it is qualified or camelCase or
  snake_case (`findNames`, `g_root`); a plain word (`verdict`) stays text.
- A file needs a source extension (`.ts`, `.cpp`, `.md`, …), so `store.select` reads as
  a name and `page.html` as a file.
- Paths are relative to the repository root. Names that the code doesn't have stay
  plain text, so a plan can name things it is about to create. An orange name exists in
  the code; a plain one doesn't, yet.
