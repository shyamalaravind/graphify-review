# The VS Code viewer (archived)

graphify-review's second viewer: the canvas in a VS Code tab, with the plan's names found through VS Code's language servers. It's archived and not maintained. The browser does the same review for the broker's few MB, and this viewer took about 2.8 GB on a large Angular repository.

[`ARCHITECTURE.md`](../../ARCHITECTURE.md) draws it in its archived frames A and B.

## What it did that the browser doesn't

- A hover showed the language server's hover: signature, types and docs. A click opened the definition beside the plan.
- **Plan Review: Comment on Selection** (editor context menu) turned the lines you'd selected into a comment, which reached the agent as `On src/a.ts:12-14: "…"`.
- The next round of the same plan replaced the plan in the same tab, and a review window closed itself once nothing in it was waiting.

## How it worked

The broker opened the plan's repository in the **review instance**, a VS Code of its own (`code --user-data-dir … <root>`), since language servers only answer for an open folder. The extension there found the review in the sessions folder and showed it.

- The review instance had only the Plan Viewer and VS Code's built-in languages, with `review-settings.json`: one TypeScript server without auto-import indexes or type downloads, no git, no AI features, nothing restored at startup. That was about 2.8 GB for a review, where a window with a full set of extensions took about 7 GB.
- It opened code in Restricted Mode without asking, so nothing from the repository ran.
- A window closed 1.5 seconds after you approved or ended the review, or 10 minutes after you requested changes if no revised plan came back. The last window quit the review instance.
- Without the review instance, the broker opened the folder in your own VS Code and handed the review over with a `vscode://shyamalaravind.plan-viewer/session` link.

The files:

- `extension.js`: the extension. It hosts `skill/graphify-review/page.html` in a webview and answers the page's `window.planHost` calls itself.
- `build.sh`: packs it into `plan-viewer.vsix`, with no npm.
- `install.sh`: builds it and installs it into your VS Code and into the review instance, in `$XDG_STATE_HOME/plan-review/vscode`.
- `review-settings.json`: the review instance's settings.

## Running it again

The broker no longer opens VS Code, and `PLAN_REVIEW_VIEWER` is gone. The viewer last worked at commit `b04dc5e`, so run that commit from a worktree of its own, which leaves your linked skills on the current code:

```sh
git worktree add ../plan-review-vscode b04dc5e
../plan-review-vscode/vscode/install.sh
PLAN_REVIEW_VIEWER=vscode ../plan-review-vscode/skill/graphify-review/review plan.md
```

To bring it forward instead, restore `open_vscode()` and `PLAN_REVIEW_VIEWER` in `skill/graphify-review/review.cpp` from that commit. The session files and the page's `window.planHost` hook are still there.

## Removing it

An extension that's still installed still finds waiting reviews in the sessions folder and offers them in VS Code's status bar. To remove it and the review instance:

```sh
code --uninstall-extension shyamalaravind.plan-viewer
rm -rf "${XDG_STATE_HOME:-$HOME/.local/state}/plan-review/vscode"
```
