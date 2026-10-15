# The plan format

How to write a plan that changes code, so the reviewer can read it as a picture first
and every name in it leads to the code. Every viewer draws the diagrams and resolves each
name, so hovering it shows its definition and clicking it opens it: the browser by
searching the code's text, VS Code through the language servers.

Plans that don't change code (a resume, a write-up) need none of this.

## Show the code flow

- **A code-flow flowchart** (a ```` ```mermaid ```` block, `flowchart`): the path a
  call, request or event takes through the code the change touches, from where it
  starts to where it ends. Each node is a real place in the code. Its first line is the
  name, `file: function()`, or `file: Type` for a type or a field; a second line after
  `<br/>` can say what happens there. Each
  edge says what passes along it. Mark what the change adds or alters, so new and changed
  steps stand out from untouched ones:

  ```
  classDef new stroke-width:3px,stroke-dasharray:6 3
  classDef changed stroke-width:3px
  ```

  applied with `:::new` or `:::changed`.
- **A description entry under the diagram for each node that changes**: a list item that
  *starts with the node's name* in a code span, then its path with line numbers, then
  what changes there and why. The viewer moves each entry into its box, so the flowchart
  reads as a set of cards: keep an entry to a few lines (sub-bullets are fine), and give
  its path as `file.ts:42-60` — a file name the repository has once is enough — so the
  box stays narrow.

  ```markdown
  - **`serve()`** (`server.ts:42-60`): answers `POST feedback` as well, because …
  ```

- **A `sequenceDiagram`** where the order of calls between processes, services or
  windows matters, and a before/after pair of flowcharts where the change reshapes the
  flow rather than adding to it.

Keep each diagram to one idea and about a dozen nodes; split anything bigger. Put a
label in quotes (`A["serve(): POST /open"]`) when it has punctuation, and use `<br/>` for
a line break inside it.

The viewer lays the plan out on one canvas: the text before the first `##` is the intro
card, and each `##` section is a frame holding its text and diagrams in order. Give each
diagram its own section, with a heading that says what it shows.

## Names

Write every function, type or file the plan talks about in one of these forms, in prose
and in node labels alike. These are what the viewers can find in the code:

| Form | Example | Found by |
| --- | --- | --- |
| `file: name()`, or `file: Name` for a type or a field | `server.ts: serve()`, `session.ts: Session` | the symbols of that file |
| `name()`, qualified or not | `serve()`, `Broker.render()`, `JParser::value()` | the files the plan mentions, then the workspace |
| a path, with lines or without | `src/server.ts:42-60`, `skill/graphify-review/page.html` | the file |

- In a code span, a bare identifier also counts when it is qualified or camelCase or
  snake_case (`findNames`, `g_root`); a plain word (`verdict`) stays text.
- A file needs a source extension (`.ts`, `.cpp`, `.md`, …), so `store.select` reads as
  a name and `page.html` as a file.
- Paths are relative to the repository root. Names that the code doesn't have stay
  plain text, so a plan can name things it is about to create.
