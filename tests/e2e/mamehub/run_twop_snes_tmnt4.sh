#!/bin/bash
# End-to-end two-player MAMEHub test (macOS):
#   Host Game -> snes -> tmnt4, guest Join Game, Start Game,
#   P1 picks Two Players, both peers mash pad inputs, assert no INPUT DESYNC.
#
# Usage (from anywhere):
#   ./tests/e2e/mamehub/run_twop_snes_tmnt4.sh
#
# Env overrides:
#   MAMEHUB_BIN     path to mamehub binary (default: <repo>/mamehub)
#   MAMEHUB_ROMPATH rompath relative to repo or absolute (default: roms)
#   MAMEHUB_E2E_OUT artifact directory (default: /tmp/mamehub-e2e)
#   MAMEHUB_MOCK    mock discord directory (default: /tmp/mamehub_mock)
#   MASH_SECS       random pad mash duration (default: 300)
#   SKIP_MASH=1     stop after launch (no title nav / mash)
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${MAMEHUB_BIN:-$ROOT/mamehub}"
ROMPATH="${MAMEHUB_ROMPATH:-roms}"
OUT="${MAMEHUB_E2E_OUT:-/tmp/mamehub-e2e}"
MOCK="${MAMEHUB_MOCK:-/tmp/mamehub_mock}"
MASH_SECS="${MASH_SECS:-300}"
TOOLS_BIN="$SCRIPT_DIR/tools/bin"
HOST_LOG="$OUT/host.log"
JOIN_LOG="$OUT/join.log"
FK="$TOOLS_BIN/focus_key"
CH="$TOOLS_BIN/click_highlight"
PAD="$TOOLS_BIN/pad"
KEY="$TOOLS_BIN/key"

export MAMEHUB_E2E_OUT="$OUT"

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "FAIL: this e2e harness requires macOS (CGEvent / screencapture automation)"
  exit 1
fi
if [[ ! -x "$BIN" ]]; then
  echo "FAIL: mamehub binary not found at $BIN (build mamehub or set MAMEHUB_BIN)"
  exit 1
fi
if ! command -v rg >/dev/null 2>&1; then
  echo "FAIL: ripgrep (rg) is required"
  exit 1
fi

"$SCRIPT_DIR/tools/build.sh" "$TOOLS_BIN"

pkill -f "$BIN" 2>/dev/null || true
sleep 0.8
rm -rf "$MOCK"
mkdir -p "$OUT" "$MOCK"
: >"$HOST_LOG"
: >"$JOIN_LOG"
rm -f "$ROOT/MAMEHub.log" "$OUT"/*.png

shot() { screencapture -x "$OUT/$1.png" || true; }
alive() { kill -0 "$1" 2>/dev/null; }
die() { echo "FAIL: $*"; tail -40 "$HOST_LOG" || true; tail -40 "$JOIN_LOG" || true; exit 1; }

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

place() {
  local pid=$1 x=$2 y=$3
  osascript -e "tell application \"System Events\" to set position of window 1 of (first process whose unix id is $pid) to {$x, $y}" 2>/dev/null || true
}

check_desync() {
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$HOST_LOG" "$JOIN_LOG" "$ROOT/MAMEHub.log" 2>/dev/null; then
    die "DESYNC or FATAL during gameplay"
  fi
}

echo "== launching host =="
cd "$ROOT"
"$BIN" -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock HostPlayer \
  -discord_directory_port 5806 -port 5805 \
  -direct_connect_timeout 60 -rompath "$ROMPATH" \
  >"$HOST_LOG" 2>&1 &
HOST_PID=$!
echo "host pid=$HOST_PID"

for i in $(seq 1 60); do
  grep -q "Signed in to Discord as HostPlayer" "$HOST_LOG" && break
  alive "$HOST_PID" || die "host died startup"
  sleep 0.5
done
sleep 2
place "$HOST_PID" 40 60
sleep 0.3
shot host-main

echo "== host: Host Game -> snes -> tmnt4 =="
for attempt in 1 2 3 4 5; do
  echo "host selection attempt $attempt"
  if [[ -f "$MOCK/mamehub-directory-v1.log" ]] && rg -q '"open":true' "$MOCK/mamehub-directory-v1.log" \
     && ! rg -q 'snes:tmnt4.*"open":true|"open":true.*"snes:tmnt4' "$MOCK/mamehub-directory-v1.log"; then
    echo "closing wrong lobby before retry"
    "$FK" "$HOST_PID" escape 1 || true
    sleep 0.4
    "$FK" "$HOST_PID" escape 1 || true
    sleep 0.4
    "$FK" "$HOST_PID" escape 1 || true
    sleep 1
  fi
  "$KEY" "$HOST_PID" enter
  sleep 2.5
  shot "host-after-hostgame-$attempt"
  "$KEY" "$HOST_PID" type snes
  sleep 2.0
  shot "host-snes-search-$attempt"
  "$KEY" "$HOST_PID" enter
  sleep 3.5
  shot "host-software-$attempt"
  "$KEY" "$HOST_PID" type tmnt4
  sleep 2.0
  shot "host-tmnt4-search-$attempt"
  "$KEY" "$HOST_PID" enter
  sleep 3.5
  shot "host-lobby-$attempt"
  if [[ -f "$MOCK/mamehub-directory-v1.log" ]] && rg -q 'snes:tmnt4' "$MOCK/mamehub-directory-v1.log"; then
    echo "host lobby announced on attempt $attempt"
    break
  fi
  "$KEY" "$HOST_PID" escape || true
  sleep 0.5
  "$KEY" "$HOST_PID" escape || true
  sleep 0.5
  "$KEY" "$HOST_PID" escape || true
  sleep 1
done
alive "$HOST_PID" || die "host died after selecting software"
shot host-lobby

echo "== wait for host announce =="
ANNOUNCED=0
for i in $(seq 1 40); do
  if [[ -f "$MOCK/mamehub-directory-v1.log" ]] && rg -q 'snes:tmnt4' "$MOCK/mamehub-directory-v1.log"; then
    ANNOUNCED=1
    echo "announced at t=${i}"
    break
  fi
  alive "$HOST_PID" || die "host died waiting announce"
  sleep 0.5
done
[[ "$ANNOUNCED" -eq 1 ]] || die "host never announced snes:tmnt4 lobby"
cat "$MOCK/mamehub-directory-v1.log"

echo "== launching join =="
"$BIN" -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock GuestPlayer \
  -discord_directory_port 5807 -port 5809 \
  -direct_connect_timeout 60 -rompath "$ROMPATH" \
  >"$JOIN_LOG" 2>&1 &
JOIN_PID=$!
echo "join pid=$JOIN_PID"

for i in $(seq 1 60); do
  grep -q "Signed in to Discord as GuestPlayer" "$JOIN_LOG" && break
  alive "$JOIN_PID" || die "join died startup"
  sleep 0.5
done
sleep 2
place "$HOST_PID" -1200 40
place "$JOIN_PID" 80 40
sleep 0.5
shot join-main

echo "== join: content-focus Host Game, Down to Join Game, Enter =="
JOINED_MENU=0
for attempt in 1 2 3 4 5; do
  echo "join menu attempt $attempt"
  if [[ "$attempt" -gt 1 ]]; then
    "$KEY" "$JOIN_PID" escape || true
    sleep 0.4
    "$KEY" "$JOIN_PID" escape || true
    sleep 0.6
  fi
  place "$HOST_PID" -1200 40
  place "$JOIN_PID" 80 40
  sleep 0.3
  shot "join-main-$attempt"
  "$KEY" "$JOIN_PID" down 1
  sleep 0.7
  shot "join-highlight-$attempt"
  "$KEY" "$JOIN_PID" enter
  sleep 2.5
  shot "join-list-$attempt"
  if rg -q '"host_name":"GuestPlayer"' "$MOCK/mamehub-directory-v1.log" 2>/dev/null; then
    die "guest accidentally hosted instead of joining"
  fi
  echo "== join: select hosted lobby (attempt $attempt) =="
  "$KEY" "$JOIN_PID" enter
  sleep 3.5
  shot "join-lobby-$attempt"
  shot host-lobby2
  if rg -q '"name":"GuestPlayer"' "$MOCK"/mamehub-*.log 2>/dev/null; then
    JOINED_MENU=1
    echo "GuestPlayer join seen on attempt $attempt"
    break
  fi
  JOINS=$(rg -c '"type":"join"' "$MOCK"/mamehub-*.log 2>/dev/null | awk -F: '{s+=$NF} END{print s+0}')
  if [[ "${JOINS:-0}" -ge 3 ]]; then
    JOINED_MENU=1
    echo "lobby join count=$JOINS on attempt $attempt"
    break
  fi
  alive "$HOST_PID" || die "host died during guest join"
  alive "$JOIN_PID" || die "join died during join"
done

JOINED=0
if [[ "$JOINED_MENU" -eq 1 ]]; then
  JOINED=1
else
  for i in $(seq 1 45); do
    if rg -q '"name":"GuestPlayer"' "$MOCK"/mamehub-*.log 2>/dev/null; then
      JOINED=1
      echo "guest joined snes lobby at t=${i}"
      break
    fi
    JOINS=$(rg -c '"type":"join"' "$MOCK"/mamehub-*.log 2>/dev/null | awk -F: '{s+=$NF} END{print s+0}')
    if [[ "${JOINS:-0}" -ge 3 ]]; then
      JOINED=1
      echo "lobby join count=$JOINS at t=${i}"
      break
    fi
    if rg -q '"host_name":"GuestPlayer"' "$MOCK/mamehub-directory-v1.log" 2>/dev/null; then
      die "guest accidentally hosted instead of joining"
    fi
    alive "$HOST_PID" || die "host died waiting guest join"
    alive "$JOIN_PID" || die "join died waiting guest join"
    sleep 1
  done
fi
[[ "$JOINED" -eq 1 ]] || die "guest never joined host snes lobby"
cat "$MOCK"/mamehub-*.log | tail -40

echo "== wait for peer crypto endpoints =="
READY=0
for i in $(seq 1 90); do
  alive "$HOST_PID" || die "host died waiting endpoints"
  alive "$JOIN_PID" || die "join died waiting endpoints"
  if rg -q '127.0.0.1:5805' "$MOCK"/mamehub-*.log && rg -q '127.0.0.1:5809' "$MOCK"/mamehub-*.log; then
    READY=1
    echo "peer endpoints ready at t=${i}"
    break
  fi
  sleep 1
done
shot host-before-start
shot join-before-start
[[ "$READY" -eq 1 ]] || echo "WARN: peer ports not both seen yet; attempting start anyway"

echo "== host: Start Game =="
place "$JOIN_PID" -1200 40
place "$HOST_PID" 80 40
sleep 0.5
STARTED=0
for attempt in 1 2 3 4 5 6 8 10; do
  echo "start game attempt $attempt"
  "$KEY" "$HOST_PID" enter || true
  sleep 0.4
  "$PAD" "$HOST_PID" tap enter 100
  sleep 1.5
  shot "host-start-attempt-$attempt"
  if rg -q '"type":"start"' "$MOCK"/mamehub-*.log 2>/dev/null; then
    STARTED=1
    echo "start message published on attempt $attempt"
    break
  fi
  select_highlight "$HOST_PID" 0 || true
  sleep 2
  if rg -q '"type":"start"' "$MOCK"/mamehub-*.log 2>/dev/null; then
    STARTED=1
    echo "start message published via highlight on attempt $attempt"
    break
  fi
done
[[ "$STARTED" -eq 1 ]] || die "host never published start"
shot host-after-start
shot join-after-start

echo "== wait for mesh + launch =="
SUCCESS=0
for i in $(seq 1 120); do
  alive "$HOST_PID" || die "HOST DIED after start t=$i"
  alive "$JOIN_PID" || die "JOIN DIED after start t=$i"
  if rg -qi 'Fatal log|Aborting application|INPUT DESYNC' "$HOST_LOG" "$JOIN_LOG" "$ROOT/MAMEHub.log" 2>/dev/null; then
    die "FATAL after start"
  fi
  if rg -q '"ready":true' "$ROOT/MAMEHub.log" 2>/dev/null \
     || rg -q 'Netplay clock started' "$HOST_LOG" 2>/dev/null; then
    SUCCESS=1
    echo "launch progress at t=${i}"
    break
  fi
  if rg -c 'Using public key' "$ROOT/MAMEHub.log" 2>/dev/null | awk '{exit !($1>=2)}'; then
    if [[ "$i" -gt 8 ]]; then
      SUCCESS=1
      echo "both peers registered keys at t=${i}"
      break
    fi
  fi
  sleep 1
done
[[ "$SUCCESS" -eq 1 ]] || die "game never launched after start"

if [[ "${SKIP_MASH:-0}" == "1" ]]; then
  echo "TEST PASSED (launch only; SKIP_MASH=1)"
  exit 0
fi

echo "== wait for netplay clock / in-game =="
for i in $(seq 1 90); do
  alive "$HOST_PID" || die "host died waiting in-game"
  alive "$JOIN_PID" || die "join died waiting in-game"
  check_desync
  if rg -q 'Netplay clock started' "$HOST_LOG" 2>/dev/null \
     && rg -q '\[INPUT_FRAME\]' "$HOST_LOG" \
     && rg -q '\[INPUT_FRAME\]' "$JOIN_LOG"; then
    echo "in-game frames at t=${i}"
    break
  fi
  sleep 1
done
shot game-boot
place "$HOST_PID" 40 40
place "$JOIN_PID" 520 40
sleep 1

echo "== P1: pass intros / reach Two Players / Start =="
for i in 1 2 3 4 5 6 8 10 12 15 18 22; do
  check_desync
  alive "$HOST_PID" || die "host died during title nav"
  alive "$JOIN_PID" || die "join died during title nav"
  "$PAD" "$HOST_PID" tap start 120
  sleep 1.2
  shot "title-start-$i"
done
"$PAD" "$HOST_PID" tap down 120
sleep 0.5
shot title-two-players-highlight
"$PAD" "$HOST_PID" tap start 150
sleep 2
shot title-after-2p
for i in 1 2 3 4 5 6 8 10; do
  "$PAD" "$HOST_PID" tap start 100
  sleep 0.35
  "$PAD" "$JOIN_PID" tap start 100
  sleep 0.7
  shot "char-select-$i"
  check_desync
done
shot gameplay-start

echo "== both players random pad mash for ${MASH_SECS}s =="
: >"$OUT/host_mash.err"
: >"$OUT/join_mash.err"
"$PAD" "$HOST_PID" mash "$MASH_SECS" 1111 >"$OUT/host_mash.err" 2>&1 &
HOST_MASH_PID=$!
"$PAD" "$JOIN_PID" mash "$MASH_SECS" 2222 >"$OUT/join_mash.err" 2>&1 &
JOIN_MASH_PID=$!
echo "host_mash=$HOST_MASH_PID join_mash=$JOIN_MASH_PID"

SYNC_OK=1
for i in $(seq 1 "$MASH_SECS"); do
  if ! alive "$HOST_PID"; then
    SYNC_OK=0
    echo "HOST DIED during mash t=$i"
    break
  fi
  if ! alive "$JOIN_PID"; then
    SYNC_OK=0
    echo "JOIN DIED during mash t=$i"
    break
  fi
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$HOST_LOG" "$JOIN_LOG" "$ROOT/MAMEHub.log" 2>/dev/null; then
    SYNC_OK=0
    echo "DESYNC/FATAL at mash t=$i"
    break
  fi
  if (( i % 30 == 0 )); then
    echo "mash progress t=${i}s"
    shot "mash-$i"
    HFRAMES=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
    JFRAMES=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
    echo "frames host=$HFRAMES join=$JFRAMES"
  fi
  if ! kill -0 "$HOST_MASH_PID" 2>/dev/null && ! kill -0 "$JOIN_MASH_PID" 2>/dev/null; then
    echo "mashers exited at t=$i"
    break
  fi
  sleep 1
done

wait "$HOST_MASH_PID" 2>/dev/null || true
wait "$JOIN_MASH_PID" 2>/dev/null || true
shot mash-final
check_desync

echo "== mash stderr =="
tail -20 "$OUT/host_mash.err" || true
tail -20 "$OUT/join_mash.err" || true
echo "== last INPUT_FRAME lines =="
rg '\[INPUT_FRAME\]' "$HOST_LOG" | tail -5
rg '\[INPUT_FRAME\]' "$JOIN_LOG" | tail -5

if [[ "$SYNC_OK" -eq 1 ]] && alive "$HOST_PID" && alive "$JOIN_PID"; then
  if ! rg -qi 'INPUT DESYNC' "$HOST_LOG" "$JOIN_LOG" "$ROOT/MAMEHub.log" 2>/dev/null; then
    echo "TEST PASSED (synced through ${MASH_SECS}s mash)"
    exit 0
  fi
fi
die "gameplay sync test failed"
