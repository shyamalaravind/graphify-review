---
name: graphify-review
description: Open the current plan, an explanation, or any markdown file as a canvas of diagrams in the browser, so the user can comment on its prose and its diagram nodes, then act on the feedback they send back. Writes it in the FORMAT.md canvas shape first (an intro with an overview diagram, one numbered frame per idea, the diagram that fits the logic, cards inside code boxes). Use only when the user asks for it by name (e.g. /graphify-review, "graphify review", "graphify this plan"). A plain plan review, and the automatic review before leaving plan mode, belong to plan-review.
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

Outside plan mode, just do steps 1-3 below on the target file and stop after acting on
the feedback; there's no native gate to invoke afterward.

## 1. Pick the file

- If the user named a file, review that file.
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
- A file the user named keeps its name. They chose it.
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
every decision and fact, but say each once. Only a document that isn't logic (a
resume, a letter) opens as it is.

Before opening the review, check every diagram the way FORMAT.md's "Make sure it
draws" says. The user sees a diagram that doesn't parse as raw source.

## 2. Open the review

`review` sits next to this SKILL.md. Run it with the absolute path to this
skill's directory:

```
<this-skill-dir>/review <file> [--title "<tab title>"] [--root <dir>]
```

`--title` names the review (the browser tab and the page
header); without it they show the file's name. Pass it whenever the user or
the skill that sent you here asks for a tab title, exactly as given, and keep
it the same every round.

Run it from the project the plan is about: that git repository is the code
the review looks names up in. If the plan is about code
somewhere else, pass `--root <dir>`.

It compiles on first use (about a second, once) and from then on starts
instantly. It opens the review in the user's browser and blocks until the user
submits or ends the review. Each round of the same file is the next round
of the same session. Run it as a background command if your tool supports that,
and you'll be notified when it exits. Tell the user in one line that the
review is open, then stop and don't poll. If background commands aren't
supported, run it in the foreground with the longest timeout available.

## 3. Act on the feedback

The command prints one of:

- `APPROVED: ...`: the user approved with no notes. Treat it as the user saying
  "looks good", and continue with whatever the plan was for.
- `APPROVED WITH NOTES: ...`: approved. Fold the notes in as you go.
- `CHANGES REQUESTED: ...`: numbered comments, each quoting the passage it
  refers to, plus an optional general comment. Address every comment and
  revise the plan. If the plan came from a file, update that file. Show what
  changed, one line per comment. Don't start implementing; the user will review
  again or approve.
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
