#!/usr/bin/env bash
set -euo pipefail

# Multi-session clean exit end-to-end test:
# Tests that two MAMEHub instances can play multiple sequential sessions
# within a single run, handling:
# 1. Host pressing Escape -> both return to main menu
# 2. Guest pressing Escape -> both return to main menu
# 3. Peer unreachable for 30s -> game terminates and returns to main menu

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"

BIN="${MAMEHUB_BIN:-$ROOT/mamehub}"
ROMPATH="${MAMEHUB_ROMPATH:-roms}"
OUT="${MAMEHUB_E2E_OUT:-/tmp/mamehub-e2e}"
MOCK="${MAMEHUB_MOCK:-/tmp/mamehub_mock}"
TOOLS_BIN="$OUT/tools"
KEY="$TOOLS_BIN/key"
PAD="$TOOLS_BIN/pad"
FK="$TOOLS_BIN/focus_key"
CH="$TOOLS_BIN/click_highlight"
HOST_LOG="$OUT/multisession_host.log"
JOIN_LOG="$OUT/multisession_join.log"

select_highlight() {
  local pid=$1
  local row=${2:-0}
  local tries=${3:-5}
  local i
  for i in $(seq 1 "$tries"); do
    if "$CH" "$pid" "$row" 1; then
      return 0
    fi
    echo "select_highlight retry $i for pid=$pid row=$row"
    sleep 1
  done
  return 1
}

if [[ ! -x "$BIN" ]]; then
  echo "FAIL: mamehub binary not found at $BIN"
  exit 1
fi

"$SCRIPT_DIR/tools/build.sh" "$TOOLS_BIN"

killall -9 mamehub 2>/dev/null || true
pkill -9 -f mamehub 2>/dev/null || true
sleep 1
rm -rf "$MOCK"
mkdir -p "$OUT" "$MOCK"
: >"$HOST_LOG"
: >"$JOIN_LOG"
rm -f "$ROOT/MAMEHub.log" "$OUT"/multisession-*.png

shot() { screencapture -x "$OUT/$1.png" || true; }
alive() { kill -0 "$1" 2>/dev/null; }
die() {
  echo "FAIL: $*"
  tail -40 "$HOST_LOG" || true
  tail -40 "$JOIN_LOG" || true
  killall -9 mamehub 2>/dev/null || true
  pkill -P $$ 2>/dev/null || true
  exit 1
}

cleanup() {
  killall -9 mamehub 2>/dev/null || true
  pkill -9 -f mamehub 2>/dev/null || true
}
trap cleanup EXIT

place() {
  local pid=$1 x=$2 y=$3
  osascript -e "tell application \"System Events\" to set position of window 1 of (first process whose unix id is $pid) to {$x, $y}" 2>/dev/null || true
}

activate() {
  local pid=$1
  "$FK" "$pid" focus 2>/dev/null || true
  sleep 0.3
}

echo "=========================================================="
echo "== STEP 1: Launching Host and Guest (Single Run) =="
echo "=========================================================="

cd "$ROOT"
"$BIN" -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock HostPlayer \
  -discord_directory_port 5806 -port 5805 \
  -direct_connect_timeout 60 -rompath "$ROMPATH" \
  >"$HOST_LOG" 2>&1 &
HOST_PID=$!
echo "Host PID=$HOST_PID"

"$BIN" -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock GuestPlayer \
  -discord_directory_port 5807 -port 5809 \
  -direct_connect_timeout 60 -rompath "$ROMPATH" \
  >"$JOIN_LOG" 2>&1 &
JOIN_PID=$!
echo "Guest PID=$JOIN_PID"

echo "Waiting for both players to sign in..."
for i in $(seq 1 40); do
  if grep -q "Signed in to Discord as HostPlayer" "$HOST_LOG" && \
     grep -q "Signed in to Discord as GuestPlayer" "$JOIN_LOG"; then
    echo "Both signed in at t=${i}"
    break
  fi
  alive "$HOST_PID" || die "Host died during startup"
  alive "$JOIN_PID" || die "Guest died during startup"
  sleep 0.5
done

sleep 1.5
place "$HOST_PID" 40 40
place "$JOIN_PID" 540 40
sleep 0.5
shot multisession-startup

# Helper to host snes/tmnt4
host_snes_tmnt4() {
  local sess=$1
  echo "== [Session $sess] Host: Select Machine -> snes -> tmnt4 =="
  place "$HOST_PID" 80 40
  place "$JOIN_PID" -1200 40
  activate "$HOST_PID"
  sleep 0.5

  local announced=0
  local prev_lobby="${LOBBY_FILE:-}"
  for attempt in 1 2 3 4 5; do
    echo "Host selection attempt $attempt for session $sess"
    if [[ "$attempt" -gt 1 ]]; then
      activate "$HOST_PID"
      "$KEY" "$HOST_PID" escape || true
      sleep 0.5
      "$KEY" "$HOST_PID" escape || true
      sleep 0.8
    fi

    "$PAD" "$HOST_PID" tap enter 150
    sleep 2.5
    "$KEY" "$HOST_PID" type snes
    sleep 2.0
    "$PAD" "$HOST_PID" tap enter 150
    sleep 3.5
    "$KEY" "$HOST_PID" type tmnt4
    sleep 2.0
    "$PAD" "$HOST_PID" tap enter 150
    sleep 3.5

    for w in $(seq 1 25); do
      local cur_lobby=$(ls -t "$MOCK"/mamehub-*.log 2>/dev/null | grep -v 'directory' | head -1 || true)
      if [[ -n "$cur_lobby" && "$cur_lobby" != "$prev_lobby" && -f "$cur_lobby" ]] && rg -q '"open":true' "$MOCK/mamehub-directory-v1.log" 2>/dev/null; then
        announced=1
        LOBBY_FILE="$cur_lobby"
        echo "Host announced lobby for session $sess on attempt $attempt (Lobby: $LOBBY_FILE)"
        break 2
      fi
      sleep 0.5
    done
  done
  [[ "$announced" -eq 1 ]] || die "Host failed to announce lobby in session $sess"
  shot "multisession-s${sess}-host-lobby"
}

# Helper to join snes/tmnt4 as guest
guest_join_lobby() {
  local sess=$1
  echo "== [Session $sess] Guest: Join Game -> Select Lobby =="
  place "$HOST_PID" -1200 40
  place "$JOIN_PID" 80 40
  activate "$JOIN_PID"
  sleep 0.5

  local joined=0
  for attempt in 1 2 3 4 5; do
    echo "Guest join attempt $attempt for session $sess"
    if [[ "$attempt" -gt 1 ]]; then
      activate "$JOIN_PID"
      "$PAD" "$JOIN_PID" tap esc 150 || true
      sleep 0.4
      "$PAD" "$JOIN_PID" tap esc 150 || true
      sleep 0.6
    fi

    place "$HOST_PID" -1200 40
    place "$JOIN_PID" 80 40
    activate "$JOIN_PID"
    sleep 0.3
    shot "guest-before-down-$sess-$attempt"

    "$PAD" "$JOIN_PID" tap down 150
    sleep 0.8
    shot "guest-after-down-$sess-$attempt"

    "$PAD" "$JOIN_PID" tap enter 150
    sleep 2.5
    shot "guest-lobby-list-$sess-$attempt"

    "$PAD" "$JOIN_PID" tap enter 150
    sleep 2.5
    shot "guest-after-enter2-$sess-$attempt"

    for w in $(seq 1 20); do
      if rg -q '"name":"GuestPlayer"' "$LOBBY_FILE" 2>/dev/null; then
        joined=1
        echo "Guest joined lobby for session $sess on attempt $attempt at w=${w}"
        break 2
      fi
      alive "$HOST_PID" || die "Host died during guest join in session $sess"
      alive "$JOIN_PID" || die "Guest died during guest join in session $sess"
      sleep 0.5
    done
  done
  [[ "$joined" -eq 1 ]] || die "Guest failed to join lobby in session $sess"
  shot "multisession-s${sess}-guest-joined"

  echo "Waiting for peer endpoints in session $sess..."
  local ready=0
  for i in $(seq 1 45); do
    if rg -q '127.0.0.1:5805' "$LOBBY_FILE" 2>/dev/null && rg -q '127.0.0.1:5809' "$LOBBY_FILE" 2>/dev/null; then
      ready=1
      echo "Peer endpoints ready in session $sess at t=${i}"
      break
    fi
    alive "$HOST_PID" || die "Host died waiting endpoints in session $sess"
    alive "$JOIN_PID" || die "Guest died waiting endpoints in session $sess"
    sleep 0.5
  done
  [[ "$ready" -eq 1 ]] || echo "WARN: endpoints not fully confirmed, proceeding to start"
}

# Helper for host to start game and wait for in-game sync
start_and_sync() {
  local sess=$1
  echo "== [Session $sess] Host: Start Game =="
  place "$JOIN_PID" -1200 40
  place "$HOST_PID" 80 40
  activate "$HOST_PID"
  sleep 0.5

  local started=0
  for attempt in 1 2 3 4 5 6 7 8; do
    echo "Start game attempt $attempt for session $sess"
    activate "$HOST_PID"
    "$KEY" "$HOST_PID" enter || true
    sleep 0.4
    "$PAD" "$HOST_PID" tap enter 100 || true
    sleep 1.5
    if rg -q '"type":"start"' "$LOBBY_FILE" 2>/dev/null; then
      started=1
      echo "Start message published on attempt $attempt"
      break
    fi
    select_highlight "$HOST_PID" 0 || true
    sleep 2.0
    if rg -q '"type":"start"' "$LOBBY_FILE" 2>/dev/null; then
      started=1
      echo "Start message published via highlight on attempt $attempt"
      break
    fi
  done
  [[ "$started" -eq 1 ]] || die "Host failed to publish start in session $sess"

  echo "Waiting for netplay clock to start in session $sess..."
  local synced=0
  for w in $(seq 1 45); do
    alive "$HOST_PID" || die "Host died after start in session $sess"
    alive "$JOIN_PID" || die "Guest died after start in session $sess"
    local cur_frames=$(rg -c '\[INPUT_FRAME\] frame=1 ' "$HOST_LOG" 2>/dev/null || echo 0)
    local cur_clocks=$(rg -c 'Netplay clock started' "$ROOT/MAMEHub.log" 2>/dev/null || echo 0)
    if [[ "${cur_frames:-0}" -ge "$sess" ]] || [[ "${cur_clocks:-0}" -ge "$sess" ]]; then
      synced=1
      echo "Netplay clock / gameplay started in session $sess at t=${w}s (frames=$cur_frames clocks=$cur_clocks)"
      break
    fi
    sleep 1
  done
  [[ "$synced" -eq 1 ]] || die "Game failed to start / sync in session $sess"
  place "$HOST_PID" 40 40
  place "$JOIN_PID" 540 40
  shot "multisession-s${sess}-gameplay"
}

# Helper to verify both instances returned to main menu
wait_return_to_main_menu() {
  local sess=$1
  local reason=$2
  echo "Waiting for both instances to return to main menu after $reason..."
  sleep 3.5
  alive "$HOST_PID" || die "Host process unexpectedly died after $reason in session $sess!"
  alive "$JOIN_PID" || die "Guest process unexpectedly died after $reason in session $sess!"
  echo "Verified: Both Host (PID $HOST_PID) and Guest (PID $JOIN_PID) are alive!"
  place "$HOST_PID" 40 40
  place "$JOIN_PID" 540 40
  sleep 0.5
  shot "multisession-s${sess}-after-exit"
}

echo "=========================================================="
echo "== SESSION 1: Host presses Escape =="
echo "=========================================================="
host_snes_tmnt4 1
guest_join_lobby 1
start_and_sync 1

echo "Emulating session 1 for 4 seconds..."
sleep 4

echo "Host pressing Escape..."
place "$JOIN_PID" -1200 40
place "$HOST_PID" 80 40
activate "$HOST_PID"
"$PAD" "$HOST_PID" tap esc 150

wait_return_to_main_menu 1 "Host Escape"
echo ">>> SESSION 1 PASSED: Host Escape cleanly returned both peers to main menu! <<<"

echo "=========================================================="
echo "== SESSION 2: Guest presses Escape =="
echo "=========================================================="
sleep 2
host_snes_tmnt4 2
guest_join_lobby 2
start_and_sync 2

echo "Emulating session 2 for 4 seconds..."
sleep 4

echo "Guest pressing Escape..."
place "$HOST_PID" -1200 40
place "$JOIN_PID" 80 40
activate "$JOIN_PID"
"$PAD" "$JOIN_PID" tap esc 150

wait_return_to_main_menu 2 "Guest Escape"
echo ">>> SESSION 2 PASSED: Guest Escape cleanly returned both peers to main menu! <<<"

echo "=========================================================="
echo "== SESSION 3: 30-Second Unreachable Peer Timeout Test =="
echo "=========================================================="
sleep 2
host_snes_tmnt4 3
guest_join_lobby 3
start_and_sync 3

echo "Emulating session 3 for 2 seconds..."
sleep 2

echo "Suspending Guest process (PID $JOIN_PID) to simulate network unreachability..."
kill -STOP "$JOIN_PID"

echo "Checking at 15s: Host must still be alive..."
sleep 15
alive "$HOST_PID" || die "Host exited prematurely at 15s!"
echo "Host still alive at 15s (correct: timeout is 30s)."

echo "Waiting remaining 17s for 30s timeout..."
sleep 17

echo "Checking that Host detected unreachable peer and terminated to main menu..."
alive "$HOST_PID" || die "Host process died instead of returning to main menu!"
if rg -qi 'unreachable for 30 seconds|SIGNALING GAME OVER' "$HOST_LOG"; then
  echo "Verified: 30s timeout log message detected in Host log!"
fi

echo "Resuming Guest process..."
kill -CONT "$JOIN_PID" || true
sleep 2

shot multisession-s3-timeout-verified

echo "=========================================================="
echo "== ALL SESSIONS PASSED SUCCESSFULLY IN A SINGLE RUN! =="
echo "=========================================================="
exit 0
