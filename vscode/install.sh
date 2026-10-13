#!/bin/sh
# Builds the Plan Viewer and installs it twice: into your VS Code, and into the review
# instance, a VS Code of its own that graphify-review opens plans in. The review instance
# has only the Plan Viewer (plus VS Code's built-in languages) and review-settings.json, so
# a review doesn't start your whole setup's language servers, and leaves your own VS Code
# untouched. Run it again after changing the extension or review-settings.json.
set -e
cd "$(dirname "$0")"
./build.sh

code --install-extension plan-viewer.vsix --force

instance=${XDG_STATE_HOME:-$HOME/.local/state}/plan-review/vscode
code --user-data-dir "$instance/data" --extensions-dir "$instance/extensions" \
  --install-extension plan-viewer.vsix --force
mkdir -p "$instance/data/User"
cp review-settings.json "$instance/data/User/settings.json"
echo "review instance ready in $instance"
