#!/usr/bin/env bash
# Launcher for the desktop entry: pull the latest commit, rebuild incrementally
# and only then start the app. Anything that fails falls back to the build that
# is already on disk, so a broken network never leaves you without the app.
set -u

REPO_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
cd "$REPO_DIR" || exit 1

BIN="$REPO_DIR/build/osu_render"
LOG_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/osu_render"
LOG="$LOG_DIR/launch.log"
mkdir -p "$LOG_DIR"
: >"$LOG"

log() {
    printf '%s %s\n' "$(date '+%H:%M:%S')" "$*" >>"$LOG"
}

notify() {
    if command -v notify-send >/dev/null 2>&1; then
        notify-send -a "osu! Replay Renderer" -t 4000 "$1" "${2:-}" >/dev/null 2>&1 || true
    fi
}

fail() {
    log "ERROR: $1"
    if command -v zenity >/dev/null 2>&1; then
        zenity --error --title="osu! Replay Renderer" \
            --text="$1\n\nПодробности: $LOG" --width=420 >/dev/null 2>&1 || true
    fi
}

# 1. Update. A public repository needs no credentials, and a failure here
#    (offline, local edits) is not fatal: we just build what is on disk.
before_rev=""
if [ "${OSU_RENDER_NO_UPDATE:-0}" != "1" ] && git rev-parse --git-dir >/dev/null 2>&1; then
    before_rev="$(git rev-parse HEAD 2>/dev/null || true)"
    if timeout 30 git pull --ff-only --quiet >>"$LOG" 2>&1; then
        log "pull: ok"
    else
        log "pull: skipped (offline or local changes)"
    fi
fi
after_rev="$(git rev-parse HEAD 2>/dev/null || true)"
if [ -n "$before_rev" ] && [ "$before_rev" != "$after_rev" ]; then
    log "update: $before_rev -> $after_rev"
    notify "Собираю новую версию…" "Обновление загружено, это займёт до минуты."
fi

# 2. First run: create the build directory.
if [ ! -f build/CMakeCache.txt ]; then
    log "configure: build/CMakeCache.txt missing"
    notify "Первая сборка…" "Настраиваю проект, это может занять пару минут."
    if ! cmake -S . -B build >>"$LOG" 2>&1; then
        fail "Не удалось настроить сборку."
        [ -x "$BIN" ] || exit 1
    fi
fi

# 3. Incremental build. Fast when nothing changed.
if ! cmake --build build -j"$(nproc)" >>"$LOG" 2>&1; then
    fail "Сборка не удалась — запускаю предыдущую версию."
    [ -x "$BIN" ] || exit 1
fi

# 4. Optional PP calculator: a small Go helper that reuses danser's own pp
#    code. Without it the app still publishes, just without PP.
if command -v go >/dev/null 2>&1 && [ -f tools/ppcalc/main.go ]; then
    stale="$(find tools/ppcalc \( -name '*.go' -o -name 'go.mod' -o -name 'go.sum' \) \
        -newer build/ppcalc -print -quit 2>/dev/null)"
    if [ ! -x build/ppcalc ] || [ -n "$stale" ]; then
        if [ ! -x build/ppcalc ]; then
            notify "Собираю калькулятор PP…" "Первый запуск, это может занять минуту."
        fi
        log "ppcalc: building"
        if (cd tools/ppcalc && go build -o ../../build/ppcalc . >>"$LOG" 2>&1); then
            log "ppcalc: ok"
        else
            log "ppcalc: build failed, publishing will continue without PP"
        fi
    fi
fi

if [ "${OSU_RENDER_NO_LAUNCH:-0}" = "1" ]; then
    log "launch: skipped (OSU_RENDER_NO_LAUNCH=1)"
    exit 0
fi

log "launch: $BIN"
exec "$BIN" "$@"
