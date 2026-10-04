# plan-review

Review coding-agent plans in the browser with inline comments. It's an [Agent Skill](https://agentskills.io), so it works in Claude Code, Codex, Gemini CLI, Cursor and any other tool that reads `SKILL.md`.

Ask for a plan review (`/plan-review`, or `/plan-review path/to/file.md`), and the plan opens in a local page.
Select text → **Comment**. Add an optional general comment, then **Approve** or **Request changes**.
The agent receives each comment with the passage it quotes, and revises the plan.

Don't want to say anything? **End review without feedback** — or just close the tab — ends it. The agent is told the review stopped with no instructions, and waits for you instead of guessing.

Everything runs locally and offline. The page is served from `127.0.0.1` in a single response, with the plan and the markdown renderer built into the HTML, so there are no network requests.

## Layout

- `skill/plan-review/SKILL.md`: instructions the agent follows.
- `skill/plan-review/review`: launcher. Compiles the server on first use (or after `review.cpp` changes) into `bin/review`, then runs it.
- `skill/plan-review/review.cpp`: the server — C++17, standard library and POSIX sockets only. It serves the page, blocks until you submit or end the review, and prints the feedback.
- `skill/plan-review/page.html`: the review page.
- `skill/plan-review/marked.min.js`: a bundled copy of [marked](https://github.com/markedjs/marked) v12.0.2 (MIT) that renders the markdown.

## Install

Clone the repo, then link the skill into your tool's skills directory:

```sh
git clone https://github.com/shyamalaravind/plan-review.git
SKILL="$PWD/plan-review/skill/plan-review"

ln -s "$SKILL" ~/.claude/skills/plan-review   # Claude Code
ln -s "$SKILL" ~/.codex/skills/plan-review    # Codex (Cursor reads this too)
ln -s "$SKILL" ~/.gemini/skills/plan-review   # Gemini CLI
```

Create the skills directory first if it doesn't exist. Requires a C++17 compiler — `clang++` from the Xcode command line tools on macOS (`xcode-select --install`), or `g++` on Linux. No packages, no runtime.

The first review builds the binary (~1s); after that it starts in a few milliseconds. To build it ahead of time: `make -C skill/plan-review`.

The review opens in your default browser. To use a different one, set `PLAN_REVIEW_BROWSER` to an app name on macOS or a command on Linux. If that browser can't be found, the default browser opens instead. Put the variable in `~/.zshenv` (or your shell's equivalent), not `~/.zshrc`. Agents run commands in non-interactive shells, and those skip `~/.zshrc`.

```sh
export PLAN_REVIEW_BROWSER="Brave Browser"   # macOS
export PLAN_REVIEW_BROWSER=brave-browser     # Linux
```

You can also run it without an agent: `skill/plan-review/review plan.md` prints the feedback to stdout.

The browser tab shows the file's name. To name it something else, add `--title`: `review resume.md --title "Apollo.io Resume Senior Software Engineer"`. Another skill that hands its document to plan-review can ask for a title this way.
