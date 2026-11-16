---
name: graphify-review
description: Open the current plan, an explanation, or any markdown file as a canvas of diagrams in the browser, so the user can comment on its prose and its diagram nodes, then act on the feedback they send back. Use only when the user asks for it by name (e.g. /graphify-review, "graphify review", "graphify this plan"). A plain plan review, and the automatic review before leaving plan mode, belong to plan-review.
argument-hint: "[file.md]"
---

# Graphify review

You write one canvas: a markdown file the review page draws as diagrams, with each box's
detail inside the box, so the user reads along the arrows instead of matching text to a
picture. You open it for review and act on the verdict it prints. It is done when the
review has printed a verdict and you have acted on it as section 3 says.

- **When unsure whether a line goes in a box or outside,** put it in the card of the box
  it is about.
- **When unsure whether to keep a fact,** keep it: a canvas may be long, but never lose
  a fact.
- **Without asking,** you may write temp files and run `review`. You edit the user's own
  file only when they say so (section 1).

## 0. In plan mode

If the user asks for a graphify review while you're in plan mode, it takes the place of
plan-review's checkpoint in front of your host's native plan-approval gate (a tool such as
`exit_plan_mode`, or any equivalent "plan ready for review" card). Don't run plan-review
as well:

1. Reuse the plan file if your host already keeps one for this conversation, and rewrite
   it there in the canvas format (section 1), keeping every decision and fact. Only write a
   fresh one (section 1) if none exists.
2. Open it with this skill (sections 1-2) and act on the feedback (section 3). `CHANGES
   REQUESTED` or `ENDED` means don't invoke the native gate yet: revise the plan and try
   again, or stop and wait, per section 3's rules.
3. Once the review resolves to `APPROVED` or `APPROVED WITH NOTES`, invoke the native
   plan-approval gate the way you normally would, folding any notes into the gate's message,
   not the plan file, with the options
   it normally offers: the canvas review stands in for plan-review, not for the gate.

Outside plan mode, do sections 1-3, with no gate after.

## 1. Pick the file

Pick by what the user asks, not by the file's type. Make each new folder with `mktemp -d`
and use the path it prints.

- **How something works, or a file that isn't markdown:** explain it, in a new file in a
  new folder, named as below.
- **A review of a markdown file they named:** review a copy of it, with the same name in a
  new folder, rewritten as the canvas. Every later round of the review (section 2) reuses
  that same copy. The original stays as it is unless the user says to edit it in
  place. A plan file your host keeps is the exception: section 0 and the naming rules
  below say how to handle it.
- Otherwise review the most recent plan in this conversation. If your host already saved
  it as a markdown file, use that file. If not, write it as the canvas into a new file in a
  new folder, named as below. If there is no plan, use your last substantial message.

### Settle how deep it goes

Only for an explanation, and only when the request doesn't already say how deep; a plan,
or a markdown file you rewrite, covers its own text. The
user decides how many of the source's *linked files* (those it links to, reads or runs)
the canvas covers.

1. **List the named file and its linked files,** one hop, each with its size in lines.
2. **Ask once** through the ask-question tool (such as AskUserQuestion), with the list in
   the question. Options: the named file in full and each linked file in one line (first,
   recommended); every file in full; the named file alone.
   - Without an ask-question tool, take the recommended depth and say so in one line when
     the review opens.

Done when the depth is settled: it is the intro's covers line (FORMAT rule 4).

### Name it for the session

Name it for what this session works on, in 2-5 lowercase kebab-case words, such as
`herdr-status-fix.md`; not `plan.md`, `untitled.md` or a host's generated slug like
`cosmic-meandering-pancake.md`. The verdict shows the name, and so do the page and its tab
unless you pass `--title` (section 2).

- A file you write yourself gets that name from the start.
- The copy of a file the user named keeps that file's name. They chose it.
- A plan file your host keeps for this conversation (in Claude Code, the one under
  `~/.claude/plans/`) keeps its path: the host finds the plan there when you exit plan
  mode, so never rename or move it. If its name isn't relevant, review it through a
  symlink that has a relevant name: `d=$(mktemp -d) && ln -s <host-file> "$d/<name>.md"`.
  Pass the symlink to `review`, and make any edits to the host file itself.
- Keep the same name for every round of review in this session, so the user can tell it's
  the same plan.

### Write it as a canvas

A document that isn't logic, such as a resume or a letter, opens as it is, with no checks.
For any other file, read [FORMAT.md](FORMAT.md), next to this file, all of it, then:

1. **Read the source for its facts.** A fact changes what the thing does; not every line
   of code is one, and a linked file covered in one line gives one fact: what the source
   says it holds.
   - For a rewrite, the source is the original as it was: save a copy before rewriting in
     place.
2. **List the frames:** group the facts by flow (a main flow's happy path, a branch, a
   failure path) under headings, core first (FORMAT rule 4). Keep the list in your
   reasoning, one line per frame: heading, then boxes; settle each card while writing it.
3. **Write the whole file once**, straight from the list: the intro, then each frame (its
   shape first, then each box's card in the order the arrows reach it, then the frame's few
   outside lines). Rewrite an existing plan into this shape rather than adding diagrams to
   it.

**The checks before opening the review,** in order. Fix what each finds by editing the
lines it names, never by rewriting the file.

1. **The script.** Run `node <this-skill-dir>/scripts/check_diagrams.js <file>`.
   - Exit 0: all of them draw. Exit 1: it lists each failure with its diagram number,
     file line and Pele's message.
   - Exit 2, or a `couldn't check` line: read each block it names against FORMAT rule 20;
     when the libraries failed, or without `node`, every block and line.
   - Each `warn:` line names a line or frame past a limit, by how much, and what to
     change. Don't read the script's code or measure yourself. Fix it, or keep it and give
     the user a one-line reason when the review opens.
2. **One read against the source,** section by section, not from memory: find each of its
   facts in the canvas, there, right and once (FORMAT rules 3 and 7–11). As you go, fix:
   - a card that disagrees with its diagram, or a way out of a decision or loop not drawn;
   - a term used before its definition, read in order (the lines under the heading, the
     cards along the arrows, the outside lines);
   - a mention that names the wrong frame, step or file.
   - Check too that the intro alone gives what, how and why, and the intro, headings and
     diagrams each flow and how each failure ends.
   - In a later round, check only the lines you edited: each of their facts stays unless
     a comment removed it.
3. **The script again,** until every diagram draws. Then stop, whatever warnings remain.

## 2. Open the review

`review` sits next to this SKILL.md. Run it with the absolute path to this skill's
directory:

```
sh <this-skill-dir>/review <file> [--title "<tab title>"] [--root <dir>] [--comments <file.json>]
```

- `--title` names the review (the browser tab and the page header). Pass it whenever the user or the skill that sent you here asks for a
  tab title, exactly as given, and keep it the same every round.
- Run it from the project the plan is about: that git repository is the code the review
  looks names up in. If the plan is about code somewhere else, pass `--root <dir>`.
- `--comments <file.json>` puts an earlier round's comments back on the page. When the user asks to keep a review's comments for
  later, save the file and its comments in a folder that lasts (not a temp folder) and
  give them both paths. The comments file is `{"general": "…", "comments": [{"note": "…",
  "anchor": {…}}]}`, with one anchor per comment the verdict printed, in the form section 3
  lists. A comment whose anchored text has changed since still shows, attached to nothing.

It compiles once on first use, then opens the review
in the user's browser and blocks until the user submits or ends it. Each run on the same
file is the next round of the same session.

- Run it in the background if your host can. If the host notifies you when it exits, tell
  the user in one line that the review is open, then stop: don't poll.
- If the host hands back a session or job to wait on instead, wait on that with the
  longest timeout it allows, and again each time that ends without a verdict.
- If background commands aren't supported, run it in the foreground with the longest
  timeout available.
- If `review` ends without printing a verdict line (a foreground timeout, a crash, a
  failed build), say so in one line with any error it printed, and stop: running it again
  opens a new round in a new tab, so the user decides when.

## 3. Act on the feedback

Only `CHANGES REQUESTED` changes the canvas. When you revise it, fix the line each comment
is about (FORMAT rule 17) and rerun section 1's checks on what changed. An approval, with or
without notes, leaves the canvas as the user approved it. The command prints one of:

- `APPROVED: ...`: the user approved with no notes. For a copy of a file the user named,
  give the copy's path and ask whether to overwrite the original with it. In plan mode, go
  on to the native gate (section 0). Outside it, say in one line that it's approved and
  stop, unless the user's request, or the skill that sent you here, says what comes after
  approval: then do that.
- `APPROVED WITH NOTES: ...`: approved, so the canvas stays as it is. Each note is a
  follow-up: carry out what it asks outside the canvas (hand it to the skill or agent it
  names, or apply it when you implement the plan), and say in one line per note what you
  did with it. A note that rewrites a line of the canvas shows how the user wants such text
  written: write that way from now on. Then go on as for `APPROVED`, following any note
  about what to do next.
- `CHANGES REQUESTED: ...`: numbered comments, plus an optional general comment. Address
  every comment, the general one included, and revise the canvas
  file. Then reply in one line per comment with what changed; a comment that asks a
  question gets its answer in that line. Then run the
  review again on the same file (section 2; in plan mode, section 0). Don't start
  implementing.
- `ENDED: ...`: the user ended the review without feedback, by clicking **End review without feedback** or
  by closing the review's tab. Say in one short line that it ended with no feedback, and
  stop: don't revise, implement, guess at what they wanted, or ask a follow-up question.
  Wait for them to say what's next.

Each comment opens with what it is about, and maps to an anchor for `--comments` (section 2).
Diagrams count from the top.

| The comment opens with | It is about | Its anchor |
| --- | --- | --- |
| `On: "…"` | the quoted prose | `{"kind": "quote", "quote": "…"}` |
| `On diagram 2, node "X":` | that node of the second diagram | `{"kind": "diagram", "diagram": 2, "node": "X"}` |
| `On diagram 2: "…"` | other quoted text in it, such as an edge label | `{"kind": "diagram", "diagram": 2, "quote": "…"}` |

Whatever the verdict, the result ends with `Diagrams that didn't draw …` if any didn't,
each with its number and the parser's error. After `CHANGES REQUESTED`, fix them with
your revision. After an approval or `ENDED`, name them in your reply and leave the file as
it is.
