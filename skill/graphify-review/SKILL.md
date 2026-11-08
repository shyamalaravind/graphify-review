---
name: graphify-review
description: Open the current plan, an explanation, or any markdown file as a canvas of diagrams in the browser, so the user can comment on its prose and its diagram nodes, then act on the feedback they send back. Use only when the user asks for it by name (e.g. /graphify-review, "graphify review", "graphify this plan"). A plain plan review, and the automatic review before leaving plan mode, belong to plan-review.
argument-hint: "[file.md]"
---

# Graphify review

The user wants to understand and review a plan, or any logic, as a picture: one canvas
in the browser, with the intro as a card, each `##` section a frame, and each flowchart
box carrying its description inside it.

## 0. In plan mode

If the user asks for a graphify review while you're in plan mode, it takes the place of
plan-review's checkpoint in front of your host's native plan-approval gate (a tool such as
`exit_plan_mode`, or any equivalent "plan ready for review" card). Don't run plan-review
as well:

1. Reuse the plan file if your host already keeps one for this conversation,
   and rewrite it there in the canvas format (step 1), keeping every decision
   and fact. Only write a fresh one (see step 1 below) if none exists.
2. Open it with this skill (steps 1-2 below) and act on the feedback (step
   3). `CHANGES REQUESTED` or `ENDED` means don't invoke the native gate yet —
   revise the plan and try again, or stop and wait, per step 3's rules.
3. Once the review resolves to `APPROVED` or `APPROVED WITH NOTES`, go ahead
   and invoke the native plan-approval gate the way you normally would
   (same summary/fleet/autopilot fields, folding in any notes). The canvas
   review is an extra human checkpoint in front of that gate, not a
   replacement for it — don't drop whatever options it normally offers.

Outside plan mode, do steps 1-3 below; step 3 says when to stop. There's no native gate
to invoke afterward.

## 1. Pick the file

- If the user named a code file, or any other file that isn't markdown,
  explain that file: write the explanation into a new temp file
  (`"$(mktemp -d)/<name>.md"`, named as below).
- If the user named a markdown file, review a copy of it: copy it to
  `"$(mktemp -d)/<its name>"` and rewrite the copy as the canvas. Every later
  round of the review (step 2) reuses that same copy. The original stays as it
  is unless the user says to edit it in place. A plan file your host keeps is
  the exception: section 0 and the naming rules below say how to handle it.
- Otherwise review the most recent plan in this conversation. If your tool
  already saved it as a markdown file, use that file. If not, write the plan
  verbatim to a new temp file (`"$(mktemp -d)/<name>.md"`, named as below). If
  there is no plan, use your last substantial message.

### Name it for the session

The result line shows the file's name, and so do the review page and its tab
unless you pass `--title` (step 2). So the name must
say what this session is working on: 2-5 lowercase words in kebab-case, such
as `herdr-status-fix.md` or `migrate-auth-to-oauth.md`. Generic or random
names don't count: `plan.md`, `untitled.md`, or a host's generated slug like
`cosmic-meandering-pancake.md` or `build-a-plan-sunny-barto.md`.

- A file you write yourself gets that name from the start.
- The copy of a file the user named keeps that file's name. They chose it.
- A plan file your host keeps for this conversation (in Claude Code, the one
  under `~/.claude/plans/`) keeps its path: the host finds the plan there when
  you exit plan mode, so never rename or move it. If its name isn't relevant,
  review it through a symlink that has a relevant name:
  `d=$(mktemp -d) && ln -s <host-file> "$d/<name>.md"`. Pass the symlink to
  `review`, and make any edits to the host file itself.
- Keep the same name for every round of review in this session, so the user
  can tell it's the same plan.

### Write it as a canvas

The user reads the canvas by its diagrams first, and follows its names into
the code. Read [FORMAT.md](FORMAT.md), next to this file, and write the file in
its shape whatever it is: a plan that changes code, an explanation of code, or
logic that isn't code. If you're asked to explain something and there's no
file, write the explanation straight into one.

Rewrite an existing plan into the shape rather than adding diagrams to it: keep
every decision and fact, but say each once. Before you rewrite a file in place,
save a copy of it to compare against in check 1. Only a document that isn't logic (a
resume, a letter) opens as it is.

**The checks before opening the review.** Read the canvas against this list
and fix each item that fails; after a round of fixes, go through the whole
list again, until a pass needs none. A document that opens as it is skips them.

1. Every decision, rule, number, branch and failure path from the source is in
   the canvas. For a rewrite, the source is the original file as it was before
   you rewrote it: check it section by section. For an explanation, it is what
   the user asked about: every rule, number and failure path that changes what
   that thing does, not every line of its code.
2. The intro's first sentence is the answer, the decision or the file's main
   point, and the intro's reading-order line names the core frames.
3. Every `##` heading states its frame's conclusion, in one line of about 70
   characters.
4. The happy path's diagram has no failure branches (a branch the logic
   chooses is fine), and each failure path starts at a step named in that
   diagram.
5. Every term the reader can't click is defined where it is first used.
6. Every diagram draws, because one that doesn't reaches the user as raw
   source. Run `node <this-skill-dir>/scripts/check_diagrams.js <file>`. It
   draws each block with Pele, the parser the review page uses, and exits 0
   when all of them draw. Exit 1 lists each failure with its diagram number,
   the file line of the error and Pele's message: fix those and run it again.
   Exit 2 lists blocks it couldn't check: read those against FORMAT.md's
   "Make sure it draws". Without `node`, read every block against it.

## 2. Open the review

`review` sits next to this SKILL.md. Run it with the absolute path to this
skill's directory:

```
<this-skill-dir>/review <file> [--title "<tab title>"] [--root <dir>] [--comments <file.json>]
```

`--title` names the review (the browser tab and the page
header); without it they show the file's name. Pass it whenever the user or
the skill that sent you here asks for a tab title, exactly as given, and keep
it the same every round.

Run it from the project the plan is about: that git repository is the code
the review looks names up in. If the plan is about code
somewhere else, pass `--root <dir>`.

`--comments <file.json>` puts an earlier round's comments back on the page,
for a user who wants to pick up a review later. When they ask to keep a
review's comments for later, save the file and its comments in a folder that
lasts (not a temp folder) and give them both paths. The comments file is
`{"general": "…", "comments": [{"note": "…", "anchor": {…}}]}`, with one anchor
per comment the verdict printed (step 3): `On: "…"` is
`{"kind": "quote", "quote": "…"}`, `On diagram 2, node "X":` is
`{"kind": "diagram", "diagram": 2, "node": "X"}`, and `On diagram 2: "…"` is
`{"kind": "diagram", "diagram": 2, "quote": "…"}`. A comment whose text has
changed since still shows, attached to nothing.

It compiles on first use (about a second, once) and from then on starts
instantly. It opens the review in the user's browser and blocks until the user
submits or ends the review. Each round of the same file is the next round
of the same session. Run it as a background command if your tool supports that,
and you'll be notified when it exits. Tell the user in one line that the
review is open, then stop and don't poll. If your tool runs it in the
background but won't notify you when it exits (it hands back a session or job
to wait on instead), wait on that with the longest timeout it allows, and wait
again each time that ends without a verdict. If background commands aren't
supported, run it in the foreground with the longest timeout available. If `review`
ends without printing a verdict line (a foreground timeout, a crash, a failed
build), say so in one line with any error it printed, and stop: running it
again opens a new round in a new tab, so the user decides when.

## 3. Act on the feedback

The command prints one of:

- `APPROVED: ...`: the user approved with no notes. In plan mode, go on to the
  native gate (section 0). Outside it, say in one line that it's approved and
  stop, unless the user's request, or the skill that sent you here, says what
  comes after approval: then do that. For a copy of a file the user named, first
  give the copy's path and ask whether to overwrite the original with it.
- `APPROVED WITH NOTES: ...`: approved. The canvas is the user's source of
  truth, so carry out in it every note about its content, and rerun step 1's
  checks on what changed. Then go on as for `APPROVED`, following any note
  about what to do next.
- `CHANGES REQUESTED: ...`: numbered comments, each quoting the passage it
  refers to, plus an optional general comment; "every comment" below includes
  the general one. In plan mode, address every comment and revise the canvas file (for a host plan file reviewed through a
  symlink, edit the host file itself), rerun step 1's checks on what changed,
  show what changed in one line per comment, and run the review again
  (section 0). Outside plan mode, answer each comment in one line, saying what
  you would change, and stop: the user says whether to change the canvas,
  iterate, or reopen the review. Either way, don't start implementing.
- `ENDED: ...`: the user ended the review without feedback, by clicking **End
  review** or by closing the review's tab. Treat it as an abrupt end with no
  instructions. Say in at most one short line that the review ended with no
  feedback, and stop. Don't revise the plan, don't start implementing, don't
  guess at what they wanted, and don't ask a follow-up question — wait for them
  to say what's next.

Each comment says what it is about:

- `On: "…"` quotes the plan's prose.
- `On diagram 2, node "serve() GET session":` is about that node of the plan's
  second diagram; `On diagram 2: "…"` quotes other text in it, such as an edge
  label. Diagrams count from the top, the intro's included.

Whatever the verdict, the result ends with `Diagrams that didn't draw:` if any
didn't, each with its number and the parser's error. The user saw those as raw
source. Fix them in the file: with your revision after `CHANGES REQUESTED`, and
before going on after an approval. After `ENDED`, say so in your one line and
leave the file as it is.
