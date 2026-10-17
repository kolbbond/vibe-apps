#!/bin/bash
# Launch vvavviz as a floating window on Hyprland
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$PROJECT_DIR/build/bin/vvavviz"

if [ ! -x "$BIN" ]; then
    echo "vvavviz binary not found at $BIN — run 'make main' first"
    exit 1
fi

# Fix hyprctl in tmux: env var isn't propagated
if [ -z "$HYPRLAND_INSTANCE_SIGNATURE" ]; then
    sig=$(ls -1 "/run/user/$(id -u)/hypr/" 2>/dev/null | head -1)
    if [ -n "$sig" ]; then
        export HYPRLAND_INSTANCE_SIGNATURE="$sig"
    fi
fi

# Launch app, then dispatch float/resize/center once window appears
if command -v hyprctl &>/dev/null && [ -n "$HYPRLAND_INSTANCE_SIGNATURE" ]; then
    "$BIN" "$@" &
    APP_PID=$!

    # Wait for window to appear (poll up to 5s)
    for i in $(seq 1 50); do
        if hyprctl clients -j 2>/dev/null | python3 -c "
import json,sys
clients = json.load(sys.stdin)
sys.exit(0 if any(c.get('class')=='vvavviz' for c in clients) else 1)
" 2>/dev/null; then
            hyprctl dispatch focuswindow 'class:^(vvavviz)$'
            hyprctl dispatch togglefloating
            hyprctl dispatch resizeactive exact 1920 1080
            hyprctl dispatch centerwindow
            break
        fi
        sleep 0.1
    done

    wait $APP_PID
else
    exec "$BIN" "$@"
fi
