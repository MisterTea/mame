#!/bin/bash
# End-to-end two-player MAMEHub test (macOS+macOS) via Discord mock CLI lobbies.
# Skips UI menu automation (unreliable under SDL3 / Accessibility from agents).
#
# Host and guest launch snes:tmnt4 with -discord_lobby, wait for mesh + frames,
# navigate title with pad, mash both peers, assert no INPUT DESYNC.
#
# Usage:
#   ./tests/e2e/mamehub/run_twop_snes_tmnt4_cli.sh
#
# Env:
#   MASH_SECS                  mash duration (default 60)
#   MAMEHUB_CONNECT_TIMEOUT    peer mesh timeout (default 60)
#   MAMEHUB_E2E_OUT            artifact dir (default /tmp/mamehub-e2e-cli)
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BIN="${MAMEHUB_BIN:-$ROOT/mamehub}"
ROMPATH="${MAMEHUB_ROMPATH:-roms}"
OUT="${MAMEHUB_E2E_OUT:-/tmp/mamehub-e2e-cli}"
MOCK="${MAMEHUB_MOCK:-/tmp/mamehub_mock}"
LOBBY="${MAMEHUB_LOBBY:-mac-mac-cli}"
HOST_PORT="${MAMEHUB_HOST_PORT:-5805}"
JOIN_PORT="${MAMEHUB_JOIN_PORT:-5809}"
HOST_DIR_PORT="${MAMEHUB_HOST_DIR_PORT:-5806}"
JOIN_DIR_PORT="${MAMEHUB_JOIN_DIR_PORT:-5807}"
CONNECT_TIMEOUT="${MAMEHUB_CONNECT_TIMEOUT:-60}"
MASH_SECS="${MASH_SECS:-60}"
PAD="${PAD:-$SCRIPT_DIR/tools/bin/pad}"

HOST_LOG="$OUT/host.log"
JOIN_LOG="$OUT/join.log"
HOST_MASH_LOG="$OUT/host_mash.err"
JOIN_MASH_LOG="$OUT/join_mash.err"
HOST_MASH_PID=
JOIN_MASH_PID=
# Shared exe-dir easylogging file (and rotations). Still checked for desync /
# population even when -verbose mirrors INFO to per-peer stdout logs.
MH_LOGS=("$ROOT/MAMEHub.log")
for i in $(seq 1 9); do MH_LOGS+=("$ROOT/MAMEHub.$i.log"); done

die() { echo "FAIL: $*"; tail -40 "$HOST_LOG" 2>/dev/null || true; tail -40 "$JOIN_LOG" 2>/dev/null || true; exit 1; }
alive() { kill -0 "$1" 2>/dev/null; }
check_desync() {
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$HOST_LOG" "$JOIN_LOG" "${MH_LOGS[@]}" 2>/dev/null; then
    die "DESYNC/FATAL in logs"
  fi
}

[[ "$(uname -s)" == "Darwin" ]] || die "requires macOS"
[[ -x "$BIN" ]] || die "mamehub binary not found at $BIN"
[[ -x "$PAD" ]] || { "$SCRIPT_DIR/tools/build.sh" "$SCRIPT_DIR/tools/bin"; [[ -x "$PAD" ]] || die "pad missing"; }
[[ -f "$ROOT/hash/snes.xml" ]] || die "missing hash/snes.xml"
[[ -f "$ROOT/roms/snes/tmnt4.zip" ]] || die "missing roms/snes/tmnt4.zip"

pkill -f "$BIN" 2>/dev/null || true
sleep 0.8
rm -rf "$MOCK"
mkdir -p "$OUT" "$MOCK"
: >"$HOST_LOG"
: >"$JOIN_LOG"
: >"$HOST_MASH_LOG"
: >"$JOIN_MASH_LOG"
rm -f "$ROOT/MAMEHub.log" "$OUT"/*.png
# Clear rotated logs without tripping zsh nomatch.
for i in $(seq 1 9); do rm -f "$ROOT/MAMEHub.$i.log"; done

trap 'kill $HOST_MASH_PID $JOIN_MASH_PID 2>/dev/null; pkill -f "$BIN" 2>/dev/null' EXIT

place() {
  local pid=$1 x=$2 y=$3
  osascript -e "tell application \"System Events\" to set position of window 1 of (first process whose unix id is $pid) to {$x, $y}" 2>/dev/null || true
}

cd "$ROOT"

echo "== launch host (CLI lobby) =="
stdbuf -oL -eL "$BIN" -verbose -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock HostPlayer \
  -discord_lobby "$LOBBY" -discord_host -discord_players 2 \
  -discord_directory_port "$HOST_DIR_PORT" -port "$HOST_PORT" \
  -direct_connect_timeout "$CONNECT_TIMEOUT" \
  -rompath "$ROMPATH" -hashpath hash \
  snes snes:tmnt4 \
  >"$HOST_LOG" 2>&1 &
HOST_PID=$!
echo "host pid=$HOST_PID"

for i in $(seq 1 60); do
  grep -q "Signed in to Discord as HostPlayer" "$HOST_LOG" && break
  alive "$HOST_PID" || die "host died startup"
  sleep 0.5
done
grep -q "Signed in to Discord as HostPlayer" "$HOST_LOG" || die "host mock-auth failed"
place "$HOST_PID" 40 60

for i in $(seq 1 80); do
  if [[ -f "$MOCK/$LOBBY.log" ]] && rg -q '"type":"host"' "$MOCK/$LOBBY.log"; then
    echo "host lobby announced"
    break
  fi
  alive "$HOST_PID" || die "host died before lobby"
  sleep 0.25
done
[[ -f "$MOCK/$LOBBY.log" ]] && rg -q '"type":"host"' "$MOCK/$LOBBY.log" || die "host never announced lobby"

echo "== launch guest (CLI lobby) =="
stdbuf -oL -eL "$BIN" -verbose -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock GuestPlayer \
  -discord_lobby "$LOBBY" -discord_players 2 \
  -discord_directory_port "$JOIN_DIR_PORT" -port "$JOIN_PORT" \
  -direct_connect_timeout "$CONNECT_TIMEOUT" \
  -rompath "$ROMPATH" -hashpath hash \
  snes snes:tmnt4 \
  >"$JOIN_LOG" 2>&1 &
JOIN_PID=$!
echo "join pid=$JOIN_PID"

for i in $(seq 1 60); do
  grep -q "Signed in to Discord as GuestPlayer" "$JOIN_LOG" && break
  alive "$JOIN_PID" || die "guest died startup"
  sleep 0.5
done
grep -q "Signed in to Discord as GuestPlayer" "$JOIN_LOG" || die "guest mock-auth failed"
place "$HOST_PID" 40 40
place "$JOIN_PID" 1020 40

for i in $(seq 1 90); do
  if rg -q 'GuestPlayer' "$MOCK/$LOBBY.log" 2>/dev/null; then
    echo "guest joined lobby"
    break
  fi
  alive "$HOST_PID" || die "host died waiting guest"
  alive "$JOIN_PID" || die "guest died waiting join"
  sleep 0.5
done
rg -q 'GuestPlayer' "$MOCK/$LOBBY.log" || die "guest never joined lobby"

echo "== wait for mesh + INPUT_FRAME on both peers =="
IN_GAME=0
FRAME_WAIT=$(( CONNECT_TIMEOUT + 120 ))
for i in $(seq 1 "$FRAME_WAIT"); do
  check_desync
  alive "$HOST_PID" || die "host died waiting in-game t=$i"
  alive "$JOIN_PID" || die "guest died waiting in-game t=$i"
  if rg -q 'direct connection to every player' "$HOST_LOG" "$JOIN_LOG"; then
    cat "$MOCK/$LOBBY.log" 2>/dev/null || true
    die "mesh timed out"
  fi
  # -verbose mirrors INFO (including INPUT_FRAME) to per-peer stdout logs.
  if rg -q '\[INPUT_FRAME\]' "$HOST_LOG" && rg -q '\[INPUT_FRAME\]' "$JOIN_LOG"; then
    IN_GAME=1
    echo "both peers emitting INPUT_FRAME at t=${i}s"
    break
  fi
  if (( i % 10 == 0 )); then
    hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
    jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
    echo "  wait in-game t=${i}s host_frames=$hf join_frames=$jf"
  fi
  sleep 1
done
[[ "$IN_GAME" -eq 1 ]] || die "never got INPUT_FRAME on both peers"

echo "== wait for netplay clock =="
CLOCK_OK=0
for i in $(seq 1 90); do
  check_desync
  if rg -q 'Netplay clock started' "$HOST_LOG" "$JOIN_LOG" "${MH_LOGS[@]}" 2>/dev/null; then
    CLOCK_OK=1
    echo "netplay clock started at t=${i}s"
    break
  fi
  sleep 1
done
[[ "$CLOCK_OK" -eq 1 ]] || die "netplay clock never started"

SETTLED=0
prev_hf=0
for i in $(seq 1 90); do
  check_desync
  hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
  jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
  hf=${hf:-0}; jf=${jf:-0}
  # INPUT_FRAME is EVERY_N(60), so ~10 lines ~= 600 emu frames.
  if (( hf >= 10 && jf >= 10 && hf > prev_hf )); then
    SETTLED=1
    echo "peers settled host_frames=$hf join_frames=$jf t=${i}s"
    break
  fi
  prev_hf=$hf
  sleep 1
done
[[ "$SETTLED" -eq 1 ]] || die "peers never settled"
screencapture -x "$OUT/in-game.png" 2>/dev/null || true

echo "== title / Two Players nav (both peers) =="
for i in 1 2 3 4 5 6 8 10 12; do
  check_desync
  "$PAD" "$HOST_PID" tap start 120
  "$PAD" "$JOIN_PID" tap start 120
  sleep 1.0
done
"$PAD" "$HOST_PID" tap down 120
sleep 0.4
"$PAD" "$HOST_PID" tap start 150
sleep 1.5
for i in 1 2 3 4 5 6 8; do
  check_desync
  "$PAD" "$HOST_PID" tap start 100
  "$PAD" "$JOIN_PID" tap start 100
  sleep 0.8
done
screencapture -x "$OUT/after-title.png" 2>/dev/null || true

# Baseline non-empty inputs before mash (prove injection works)
before_host=$(rg -c '\[INPUT_FRAME\].*inputs=\[[^]]' "$HOST_LOG" 2>/dev/null || echo 0)
before_join=$(rg -c '\[INPUT_FRAME\].*inputs=\[[^]]' "$JOIN_LOG" 2>/dev/null || echo 0)

echo "== mash both peers for ${MASH_SECS}s =="
"$PAD" "$HOST_PID" mash "$MASH_SECS" 1111 >"$HOST_MASH_LOG" 2>&1 &
HOST_MASH_PID=$!
"$PAD" "$JOIN_PID" mash "$MASH_SECS" 2222 >"$JOIN_MASH_LOG" 2>&1 &
JOIN_MASH_PID=$!

SYNC_OK=1
for i in $(seq 1 "$MASH_SECS"); do
  if ! alive "$HOST_PID" || ! alive "$JOIN_PID"; then
    SYNC_OK=0
    echo "PEER DIED during mash t=$i"
    break
  fi
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$HOST_LOG" "$JOIN_LOG" "${MH_LOGS[@]}" 2>/dev/null; then
    SYNC_OK=0
    echo "DESYNC/FATAL at mash t=$i"
    break
  fi
  if (( i % 15 == 0 )); then
    hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
    jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
    hi=$(rg -c '\[INPUT_FRAME\].*inputs=\[[^]]' "$HOST_LOG" 2>/dev/null || echo 0)
    ji=$(rg -c '\[INPUT_FRAME\].*inputs=\[[^]]' "$JOIN_LOG" 2>/dev/null || echo 0)
    echo "mash t=${i}s host_frames=$hf join_frames=$jf host_input_frames=$hi join_input_frames=$ji"
  fi
  sleep 1
done

wait "$HOST_MASH_PID" 2>/dev/null || true
wait "$JOIN_MASH_PID" 2>/dev/null || true
HOST_MASH_PID=
JOIN_MASH_PID=

check_desync
screencapture -x "$OUT/final.png" 2>/dev/null || true

hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
hi=$(rg -c '\[INPUT_FRAME\].*inputs=\[[^]]' "$HOST_LOG" 2>/dev/null || echo 0)
ji=$(rg -c '\[INPUT_FRAME\].*inputs=\[[^]]' "$JOIN_LOG" 2>/dev/null || echo 0)
mh_bytes=$(wc -c <"$ROOT/MAMEHub.log" 2>/dev/null || echo 0)
[[ "$hf" -gt 10 ]] || die "too few host frames ($hf)"
[[ "$jf" -gt 10 ]] || die "too few join frames ($jf)"
[[ "$mh_bytes" -gt 1000 ]] || die "MAMEHub.log not populated ($mh_bytes bytes)"
[[ "$SYNC_OK" -eq 1 ]] || die "peers died or desynced during mash"
[[ "$hi" -gt "$before_host" ]] || die "host never showed non-empty inputs (before=$before_host after=$hi)"
[[ "$ji" -gt "$before_join" ]] || die "guest never showed non-empty inputs (before=$before_join after=$ji)"

if rg -qi 'INPUT DESYNC' "$HOST_LOG" "$JOIN_LOG" "${MH_LOGS[@]}" 2>/dev/null; then
  die "INPUT DESYNC detected"
fi

echo "PASS: macOS+macOS snes:tmnt4 synced through ${MASH_SECS}s mash (host_frames=$hf join_frames=$jf host_inputs=$hi join_inputs=$ji, MAMEHub.log=${mh_bytes}B, no INPUT DESYNC)"
ls -la "$ROOT/MAMEHub.log" 2>/dev/null
for i in $(seq 1 9); do ls -la "$ROOT/MAMEHub.$i.log" 2>/dev/null || true; done
exit 0
