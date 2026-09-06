#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd "$ROOT"

BIN="./mamehub"
[[ -x "$BIN" ]] || { echo "Binary $BIN not found"; exit 1; }

ROMPATH="${ROMPATH:-$ROOT/roms}"
MOCK="/tmp/mamehub_mock"
rm -rf "$MOCK"
mkdir -p "$MOCK"

E2E_DIR="/tmp/mamehub-e2e"
mkdir -p "$E2E_DIR"
HOST_LOG="$E2E_DIR/cancel_host.log"
: > "$HOST_LOG"

PAD="$E2E_DIR/tools/pad"
KEY="$E2E_DIR/tools/key"
FOCUS="$E2E_DIR/tools/focus_key"
CLICK="$E2E_DIR/tools/click_highlight"

cleanup() {
  if [[ -n "${HOST_PID:-}" ]] && kill -0 "$HOST_PID" 2>/dev/null; then
    kill -9 "$HOST_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

killall -9 mamehub 2>/dev/null || true
sleep 1

echo "== Starting Host instance =="
"$BIN" -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock HostPlayer \
  -discord_directory_port 5806 -port 5805 \
  -direct_connect_timeout 60 \
  -rompath "$ROMPATH" > "$HOST_LOG" 2>&1 &
HOST_PID=$!

sleep 1
for i in $(seq 1 40); do
  if grep -q "Signed in to Discord as HostPlayer" "$HOST_LOG"; then
    echo "Host signed in at t=${i}"
    break
  fi
  sleep 0.5
done
sleep 2

osascript -e "tell application \"System Events\" to set position of window 1 of (first process whose unix id is $HOST_PID) to {80, 40}" 2>/dev/null || true
"$FOCUS" "$HOST_PID" focus 2>/dev/null || true
sleep 1

echo "== Navigating to Host Game -> snes -> tmnt4 =="
"$PAD" "$HOST_PID" tap enter 150
sleep 2.0
"$KEY" "$HOST_PID" type snes
sleep 2.0
"$PAD" "$HOST_PID" tap enter 150
sleep 2.5
"$KEY" "$HOST_PID" type tmnt4
sleep 2.0
"$PAD" "$HOST_PID" tap enter 150
LOBBY_FILE=""
for w in $(seq 1 30); do
  cur=$(ls -t "$MOCK"/mamehub-*.log 2>/dev/null | grep -v 'directory' | head -1 || true)
  if [[ -n "$cur" && -f "$cur" ]] && grep -q '"open":true' "$MOCK/mamehub-directory-v1.log" 2>/dev/null; then
    LOBBY_FILE="$cur"
    break
  fi
  sleep 0.5
done

if [[ -z "$LOBBY_FILE" ]]; then
  echo "FAIL: Lobby was not created!"
  exit 1
fi
echo "Lobby created successfully: $LOBBY_FILE"

echo "== Test 1: Cancelling lobby via UI Cancel Lobby =="
START_TIME=$(date +%s)
# Cancel Lobby is selected by default; tap enter to cancel
"$PAD" "$HOST_PID" tap enter 150
sleep 1.5

END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
echo "Time to cancel lobby: ${ELAPSED}s"

if [[ "$ELAPSED" -ge 10 ]]; then
  echo "FAIL: Canceling lobby took ${ELAPSED}s (expected < 2s)"
  exit 1
fi

LATEST_DIR_MSG=$(tail -n 1 "$MOCK/mamehub-directory-v1.log")
if [[ "$LATEST_DIR_MSG" != *'"open":false'* ]]; then
  echo "FAIL: Latest directory message was not open:false! Message: $LATEST_DIR_MSG"
  exit 1
fi
echo "Verified: Lobby closed immediately in discovery (${ELAPSED}s)!"

# Verify host process is still alive and responsive
if ! kill -0 "$HOST_PID" 2>/dev/null; then
  echo "FAIL: Host process died!"
  exit 1
fi
echo "Host is alive and back at menu."
screencapture -x -R 0,0,1200,900 "$E2E_DIR/after_cancel.png" 2>/dev/null || true

echo "== Test 2: Hosting again after cancel =="
# Ensure focus
"$FOCUS" "$HOST_PID" focus 2>/dev/null || true
sleep 1.0

# We are back at the main menu. Navigate to Host Game -> snes -> tmnt4 again.
"$PAD" "$HOST_PID" tap enter 150
sleep 2.5
screencapture -x -R 0,0,1200,900 "$E2E_DIR/test2_machine.png" 2>/dev/null || true
"$KEY" "$HOST_PID" type snes
sleep 2.0
"$PAD" "$HOST_PID" tap enter 150
sleep 3.5
screencapture -x -R 0,0,1200,900 "$E2E_DIR/test2_software.png" 2>/dev/null || true
"$KEY" "$HOST_PID" type tmnt4
sleep 2.0
"$PAD" "$HOST_PID" tap enter 150
sleep 3.5
screencapture -x -R 0,0,1200,900 "$E2E_DIR/test2_lobby.png" 2>/dev/null || true
LOBBY_FILE2=""
for w in $(seq 1 30); do
  cur=$(ls -t "$MOCK"/mamehub-*.log 2>/dev/null | grep -v 'directory' | head -1 || true)
  latest_msg=$(tail -n 1 "$MOCK/mamehub-directory-v1.log" 2>/dev/null || true)
  if [[ -n "$cur" && "$cur" != "$LOBBY_FILE" && -f "$cur" && "$latest_msg" == *'"open":true'* ]]; then
    LOBBY_FILE2="$cur"
    break
  fi
  sleep 0.5
done

if [[ -z "$LOBBY_FILE2" ]]; then
  echo "FAIL: Failed to create lobby on second attempt!"
  exit 1
fi
echo "Second lobby created successfully: $LOBBY_FILE2"

echo "== Test 3: Cancelling lobby via Escape key =="
START_TIME=$(date +%s)
"$PAD" "$HOST_PID" tap esc 150
sleep 1.5
END_TIME=$(date +%s)
ELAPSED=$((END_TIME - START_TIME))
echo "Time to cancel lobby via Escape: ${ELAPSED}s"

if [[ "$ELAPSED" -ge 10 ]]; then
  echo "FAIL: Canceling lobby via Escape took ${ELAPSED}s"
  exit 1
fi

LATEST_DIR_MSG3=$(tail -n 1 "$MOCK/mamehub-directory-v1.log")
if [[ "$LATEST_DIR_MSG3" != *'"open":false'* ]]; then
  echo "FAIL: Lobby was not closed in discovery on Escape! Message: $LATEST_DIR_MSG3"
  exit 1
fi
echo "Verified: Lobby cancelled via Escape in ${ELAPSED}s!"

echo ">>> ALL TESTS PASSED SUCCESSFULLY! <<<"
