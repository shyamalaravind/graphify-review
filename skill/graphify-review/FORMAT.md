# The canvas format

How to write anything with logic in it so the reader understands it from the canvas: the
answer first, then the shape, then the details, with every name one hover or click away
from the code. It is for any logic: a plan that changes code, how existing code works, or
a protocol or process that isn't code. A document that isn't logic (a resume, a letter)
needs none of it.

Contents: The reader · 1. Open with the answer · 2. Core frames, then reference ·
3. The happy path, then the failure paths · 4. Pick the form by the shape of the logic ·
5. Diagram and text each do their own job · 6. A plan that changes code · Cards · Marks ·
Make sure it draws · Names · What the page does with it

## The reader

The reader is the user. They know software well and haven't read this code. Write as if
they don't remember this session's details: they come back to the canvas later, and it is
their source of truth, so a fact it leaves out is a fact they don't have.

- **Complete over short.** Every decision, rule, number, branch and failure path from the
  source goes in. The canvas is as long as that takes; length is never a reason to leave
  something out. What you cut is a second copy of a fact, never the fact.
- **The intro is read first, alone.** The intro (the text before the first `##`) is a
  card at the top of the canvas. The canvas opens at the zoom where the frames fit beside
  the comments panel, showing the intro and about one frame; the page's **Fit** button
  shows every frame at once, too small to read. So the intro has to work on its own, as
  the map of the rest.

## 1. Open with the answer

The intro card holds, in this order:

1. **The answer**, as its first sentence: for an explanation, the answer to what the user
   asked; for a plan, the decision; for any other file, its main point. Then one or two
   sentences: for an explanation, what problem the thing explained solves; for a plan,
   what changes and why.
2. **An overview**: the main flows, each on a line of its own as a text arrow flow
   (You → ask for a review → Agent → writes → the canvas file), or one diagram of at most
   seven boxes when a single flow covers everything. Never merge several flows into one
   diagram: the reader has to untangle them before reading anything. The frames below
   zoom into the flows.
3. **The frames in reading order**, one line, core frames first: "Core: **1** a round
   starts when you run `review`, **2** … Reference: **5** four verdicts can end a round, **6** …".
4. **A legend**, in one line, for any marks the diagrams use (see "Marks").

## 2. Core frames, then reference

- Pick the ideas the reader must hold to understand the whole, usually two to four. Each
  gets a core frame, in reading order. Everything else (full rule lists, every number,
  each edge case) goes in reference frames after them: placed later, never left out.
- Number the `##` headings, and make each state its frame's conclusion, not its topic:
  `## 2. A round ends when the tab says goodbye`, not `## 2. How a round ends`. Skimming
  the headings then gives the whole argument. Keep each to one line, about 70
  characters: the detail goes in the frame.
- A frame holds its flow or diagram (with the diagram's cards), then the text: why it
  works this way, what's risky, what was ruled out.
- Keep a diagram to one idea and about a dozen boxes. Split anything bigger into an
  overview and details.

## 3. The happy path, then the failure paths

- Show the happy path first and on its own: what happens when nothing goes wrong, with no
  failure branches in its diagram. A reader who meets the exceptions before the rule
  can't place them. With several main flows, give each its happy path, then its failure
  paths.
- Follow one concrete case along it: label the diagram's arrows with that case's values
  (`|"verdict: APPROVED, 2 comments"|`), or use them in its numbered steps. On a
  decision's arrow, the value goes after the branch: `|"yes: 7 days old"|`. In a plan the
  code doesn't exist yet, so say the values are an example.
- A failure path is where something breaks: an exception, a timeout, a crash, a race, a
  lost message. Failure paths come after the happy path, in frames of their own: one per
  failure, or one frame shared by short ones. Each starts at the step where it leaves the
  happy path, named as that step is in the happy path's diagram, and is drawn through to
  its outcome: retried, reported or stopped. Draw each one (a text arrow flow counts): a
  rule left in a sentence, or in a box's sub-bullet, is the one the reader misses. A
  failure frame is core when the reader must understand it to understand the whole;
  otherwise it is reference.
- A branch the logic chooses stays in the diagram: a check that turns something away, a
  user's choice, each "no" in a set of checks. Only a path where something breaks moves
  to its own frame.
- Where the reader would guess wrong, draw that path as well, and say in one sentence what
  they'd expect and what happens instead.

## 4. Pick the form by the shape of the logic

| The logic is | Use |
| --- | --- |
| a short chain of steps | a text arrow flow, one line: Agent → runs `review` → Broker → opens → the tab |
| something moving between states | one text arrow flow per path, each line starting with what triggers it: **Tab closed:** Waiting → goodbye sent → Leaving → 2 s pass → Ended |
| messages between processes, services or people, where order matters | `sequenceDiagram`, with `alt` or `opt` for a branch that is part of the protocol; a failure goes in its own frame (see 3) |
| a path a call, request or event takes through code | `flowchart` of real places in the code, each box with its card |
| rules and checks that branch and join again | `flowchart TD`, with `{"…?"}` decisions and yes / no on the arrows |
| data with fields | a table |

A diagram earns its place when its boxes carry cards, or when its shape is the point:
branches that join again, loops, several parties talking. A diagram whose boxes hold a few
words each is a text arrow flow drawn small, and harder to read.

Write each text arrow flow as a paragraph of its own (a blank line before and after), not
in a code block or a tight list, with names in code spans so they link. Keep it to one
line, about 85 characters: a flow that wraps reads as a block of text. A flow that needs
more becomes numbered steps, one short line each, with a blank line between them.

When a plan reshapes a flow rather than adding to it, show a before and after pair.

## 5. Diagram and text each do their own job

- The **diagram** shows the shape: what talks to what, in what order, and which way each
  decision goes. Each arrow's label, drawn on the arrow, says what passes along it, what
  triggers it, or which way a decision went: `|"verdict"|`, `|"tab closed"|`,
  `|"yes: sends the verdict"|`. Keep it to a few words; anything more goes in the box's
  card or the text, never left out.
- The **text** says what the diagram can't: why, what's risky, what was ruled out, and
  what happens at the limits.
- Say each fact once. Text that walks along the arrows again ("then A calls B, which…")
  is a second copy for the reader to compare.
- Define every term the reader can't click where it is first used, in the intro or the
  first frame that uses it: a file the program writes while it runs, a queue, a header.
  One plain line each, or a table: what it is, what writes it, what reads it, with those
  functions named so they link. A later frame that depends on it may restate it in a few
  words; that isn't a second copy.

## 6. A plan that changes code

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
- A card says what happens there and, in a plan, what changes and why: as many short
  lines as that takes, up to about five. Sub-bullets are fine. A card that needs more
  means its box is more than one step: split the box, each part with its own card. When
  the box is one step that can't be split, the card holds the summary and the rest goes
  in the text after the diagram's cards. Nothing is dropped to make a card fit.
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

A block that Pele (the diagram parser the page uses) can't parse reaches the reader as raw
source with an error under it. The review's result tells you afterwards ("Diagrams that
didn't draw"), but by then the reader has seen it. SKILL.md's check 6 draws every block
before the review opens. Write each block with the rules below in mind; when check 6
can't run, check each block against them.

- No `;` anywhere in a sequence diagram (messages, notes, `alt` and `opt` labels): it
  ends the statement. Write "," or "and".
- No node ID that Mermaid reserves: `end`, `subgraph`, `graph`, `flowchart`, `style`,
  `class`, `classDef`, `click`, `linkStyle` or `call`. `A --> end` fails, so call it
  `done`; a label can say anything (`A["end"]` draws).
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

## What the page does with it

- Each `##` section is a **frame** below the intro card, one under another, read top to
  bottom.
- Each ```` ```mermaid ```` block is drawn by Pele, which reads Mermaid's syntax.
- In a flowchart, a box named after code can carry a **card**: a list entry under the
  diagram that starts with the box's name moves into the box (see "Cards").
