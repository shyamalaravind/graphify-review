---
name: plan-review
description: Open the current plan (or any markdown file) in the browser so the user can highlight text and leave inline comments, then act on the feedback they send back. Use whenever the user explicitly asks for a plan review (e.g. /plan-review), AND automatically before ever finishing plan mode via whatever native plan-approval/plan-mode-exit tool this host provides (e.g. a tool literally named `exit_plan_mode`, or an equivalent "plan ready for review" gate) — run this skill first instead of just presenting that native card.
argument-hint: "[file.md]"
---

# Plan review

The user wants to review a plan in the browser instead of the terminal.

## 0. Before exiting plan mode

Whatever your host calls its native plan-approval gate (a tool such as
`exit_plan_mode`, or any equivalent "plan ready for review" card that hands a
plan to the user for a yes/no), run this skill **first** instead of invoking
that gate right away:

1. Reuse the plan file if your host already keeps one for this conversation —
   don't rewrite it. Only write a fresh one (see step 1 below) if none
   exists; that's a one-time cost, not a per-review cost, since the plan text
   is already in context either way.
2. Open it with this skill (steps 1-2 below) and act on the feedback (step
   3). `CHANGES REQUESTED` or `ENDED` means don't invoke the native gate yet —
   revise the plan and try again, or stop and wait, per step 3's rules.
3. Once the review resolves to `APPROVED` or `APPROVED WITH NOTES`, go ahead
   and invoke the native plan-approval gate the way you normally would
   (same summary/fleet/autopilot fields, folding in any notes). The browser
   review is an extra human checkpoint in front of that gate, not a
   replacement for it — don't drop whatever options it normally offers.

If the user explicitly runs `/plan-review` directly rather than as part of
this pre-gate handoff, just do steps 1-3 below on their target file and stop
after acting on the feedback; there's no native gate to invoke afterward.

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

## 2. Open the review

`review` sits next to this SKILL.md. Run it with the absolute path to this
skill's directory:

```
<this-skill-dir>/review <file> [--title "<tab title>"]
```

`--title` names the browser tab and the page header; without it they show
the file's name. Pass it whenever the user or the skill that sent you here
asks for a tab title, exactly as given, and keep it the same every round.

It compiles the server on first use (about a second, once) and from then on
starts instantly. It opens the browser and blocks until the user submits or
ends the review. Run it as a background command if your tool supports that,
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
  review** or by closing the tab. Treat it as an abrupt end with no
  instructions. Say in at most one short line that the review ended with no
  feedback, and stop. Don't revise the plan, don't start implementing, don't
  guess at what they wanted, and don't ask a follow-up question — wait for them
  to say what's next.
