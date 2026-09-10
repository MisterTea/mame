#!/bin/bash
# macOS host + iOS Simulator joiner: launch snes:tmnt4 over Discord mock,
# navigate past the title, mash inputs on both peers, assert no INPUT DESYNC.
#
# Requires: booted simulator with org.mistertea.mamehub installed, mamehub binary,
# pad tool, snes softlist + ROM.
#
# Env:
#   MASH_SECS                mash duration (default 45)
#   MAMEHUB_CONNECT_TIMEOUT  peer mesh timeout (default 60, max 60)
#   RECORD_MP4               record host+guest mp4 (default 1)
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BIN="${MAMEHUB_BIN:-$ROOT/mamehub}"
ROMPATH="${MAMEHUB_ROMPATH:-roms}"
OUT="${MAMEHUB_E2E_OUT:-/tmp/mamehub-ios-cross-play}"
HOST_MOCK="${MAMEHUB_MOCK:-/tmp/mamehub_mock}"
PKG="${MAMEHUB_IOS_PKG:-org.mistertea.mamehub}"
LOBBY="${MAMEHUB_LOBBY:-ios-cross-play}"
HOST_PORT="${MAMEHUB_HOST_PORT:-5966}"
JOIN_PORT="${MAMEHUB_JOIN_PORT:-5976}"
# Simulator shares the Mac network stack — each peer must bind its own
# local directory port (Android NAT made identical ports work there).
HOST_DIR_PORT="${MAMEHUB_HOST_DIR_PORT:-5817}"
JOIN_DIR_PORT="${MAMEHUB_JOIN_DIR_PORT:-5818}"
APP="${MAMEHUB_IOS_APP:-$ROOT/ios-project/build/ios-simulator/MAMEHub.app}"
CONNECT_TIMEOUT="${MAMEHUB_CONNECT_TIMEOUT:-60}"
MASH_SECS="${MASH_SECS:-45}"
RECORD_MP4="${RECORD_MP4:-1}"
PAD="${PAD:-$SCRIPT_DIR/tools/bin/pad}"
WINID="${WINID:-$SCRIPT_DIR/tools/bin/winid}"

HOST_LOG="$OUT/host.log"
JOIN_LOG="$OUT/guest.log"
SYNC_LOG="$OUT/mock-sync.log"
HOST_MASH_LOG="$OUT/host_mash.err"
JOIN_MASH_LOG="$OUT/join_mash.err"
HOST_MP4="$OUT/host.mp4"
GUEST_MP4="$OUT/guest.mp4"
HOST_MOV="$OUT/host.mov"
HOST_MASH_PID=
JOIN_MASH_PID=
GUEST_PID=
GUEST_LAUNCH_PID=
SYNC_PID=
HOST_REC_PID=
GUEST_REC_PID=

die() { echo "FAIL: $*"; tail -50 "$HOST_LOG" 2>/dev/null || true; rg -i 'INPUT DESYNC|FATAL|Error:' "$JOIN_LOG" 2>/dev/null | tail -20 || true; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing $1"; }
alive() { kill -0 "$1" 2>/dev/null; }
check_desync() {
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$HOST_LOG" "$ROOT/MAMEHub.log" "$JOIN_LOG" 2>/dev/null; then
    die "DESYNC/FATAL in logs"
  fi
}

guest_data_dir() {
  xcrun simctl get_app_container booted "$PKG" data 2>/dev/null
}

find_guest_pid() {
  # Simulator apps run as host processes.
  pgrep -n -f 'MAMEHub.app/MAMEHub' 2>/dev/null || true
}

# Focus-free iOS inject via Documents/mamehub_inject.log (polled by VirtualControls).
# Does not steal Simulator focus, so macOS pad + iOS mash can run concurrently.
GUEST_INJECT=""
ios_tap() {
  local key=$1
  local hold_ms=${2:-140}
  [[ -n "$GUEST_INJECT" ]] || { echo "ios_tap: GUEST_INJECT unset" >&2; return 1; }
  # Append is atomic enough for short lines on local FS; app polls every ~16ms.
  printf '%s %s\n' "$key" "$hold_ms" >>"$GUEST_INJECT"
}

ios_mash() {
  local secs=$1
  local seed=$2
  local deadline=$((SECONDS + secs))
  local n=0
  local state=$seed
  local keys=(up down left right a b x y)
  while (( SECONDS < deadline )); do
    state=$(( (state * 1103515245 + 12345) & 0x7fffffff ))
    local k=${keys[$(( state % ${#keys[@]} ))]}
    state=$(( (state * 1103515245 + 12345) & 0x7fffffff ))
    local hold=$(( 40 + state % 140 ))
    ios_tap "$k" "$hold"
    state=$(( (state * 1103515245 + 12345) & 0x7fffffff ))
    local gap
    gap=$(awk -v s="$state" 'BEGIN { printf "%.3f", (20 + (s % 80)) / 1000 }')
    sleep "$gap"
    n=$((n + 1))
    if (( n % 50 == 0 )); then
      echo "ios_mash pulses=$n remain=$((deadline - SECONDS))s" >&2
    fi
  done
  echo "ios_mash done pulses=$n" >&2
}

host_window_id() {
  [[ -x "$WINID" ]] || return 0
  "$WINID" "$1" 2>/dev/null || true
}

host_window_bounds() {
  # prints: id x y w h
  [[ -x "$WINID" ]] || return 0
  "$WINID" "$1" bounds 2>/dev/null || true
}

# Bring a unix-pid's app frontmost so SDL/Cocoa accepts keyboard (host only).
focus_pid() {
  local pid=$1
  osascript -e "tell application \"System Events\" to set frontmost of first process whose unix id is $pid to true" >/dev/null 2>&1 || true
}

count_nonempty_inputs() {
  rg -c '\[INPUT_FRAME\].*inputs=\[[^]]' "$HOST_LOG" 2>/dev/null || echo 0
}

count_input_token() {
  # e.g. count_input_token 'P1 Start'
  rg -c "inputs=\[[^\]]*${1}" "$HOST_LOG" 2>/dev/null || echo 0
}

stop_recordings() {
  # simctl recordVideo / ffmpeg finalize on SIGINT.
  if [[ -n "${GUEST_REC_PID:-}" ]]; then
    kill -INT "$GUEST_REC_PID" 2>/dev/null || true
  fi
  if [[ -n "${HOST_REC_PID:-}" ]]; then
    kill -INT "$HOST_REC_PID" 2>/dev/null || true
  fi
  wait "$GUEST_REC_PID" 2>/dev/null || true
  wait "$HOST_REC_PID" 2>/dev/null || true
  GUEST_REC_PID=
  HOST_REC_PID=
}

need xcrun
need rg
[[ -x "$BIN" ]] || die "mamehub binary not found at $BIN"
[[ -x "$PAD" ]] || die "pad tool not found at $PAD (run tests/e2e/mamehub/tools/build.sh)"
[[ -d "$APP" ]] || die "iOS app not found at $APP (build + package_app.sh first)"
[[ -f "$ROOT/hash/snes.xml" ]] || die "missing hash/snes.xml"
[[ -f "$ROOT/roms/snes/tmnt4.zip" ]] || die "missing roms/snes/tmnt4.zip"
xcrun simctl list devices | rg -q 'Booted' || die "no booted iOS simulator"

mkdir -p "$OUT"
rm -rf "$HOST_MOCK"
mkdir -p "$HOST_MOCK"
: >"$HOST_LOG"
: >"$JOIN_LOG"
: >"$SYNC_LOG"
: >"$HOST_MASH_LOG"
: >"$JOIN_MASH_LOG"
rm -f "$OUT"/*.png "$OUT"/*.mp4 "$OUT"/*.mov "$ROOT/MAMEHub.log"

pkill -f "$BIN" 2>/dev/null || true
pkill -f 'MAMEHub.app/MAMEHub' 2>/dev/null || true
xcrun simctl terminate booted "$PKG" >/dev/null 2>&1 || true
# Best-effort free leftover binds from prior simulator/host runs
for p in "$HOST_PORT" "$JOIN_PORT" "$HOST_DIR_PORT" "$JOIN_DIR_PORT"; do
  pids=$(lsof -nP -t -iTCP:"$p" -sTCP:LISTEN 2>/dev/null || true)
  [[ -n "${pids:-}" ]] && kill $pids 2>/dev/null || true
done
sleep 1

echo "== install iOS app =="
xcrun simctl install booted "$APP" || die "simctl install failed"

GUEST_DATA="$(guest_data_dir)"
[[ -n "$GUEST_DATA" && -d "$GUEST_DATA" ]] || die "could not resolve guest data container"
GUEST_DOCS="$GUEST_DATA/Documents"
GUEST_MOCK="$GUEST_DOCS/mamehub_mock"
GUEST_ROMS="$GUEST_DOCS/roms"
GUEST_HASH="$GUEST_DOCS/hash"
GUEST_INJECT="$GUEST_DOCS/mamehub_inject.log"
echo "guest data=$GUEST_DATA"
: >"$GUEST_INJECT"

echo "== stage ROMs + softlist into simulator Documents =="
mkdir -p "$GUEST_MOCK" "$GUEST_ROMS/snes" "$GUEST_HASH"
rm -f "$GUEST_MOCK"/* 2>/dev/null || true
cp "$ROOT/hash/snes.xml" "$GUEST_HASH/snes.xml"
cp "$ROOT/roms/snes/tmnt4.zip" "$GUEST_ROMS/tmnt4.zip"
cp "$ROOT/roms/snes/tmnt4.zip" "$GUEST_ROMS/snes/tmnt4.zip"
cp "$ROOT/roms/snes.zip" "$GUEST_ROMS/snes.zip" 2>/dev/null || true

# shellcheck source=sync_mock_lobby_fs.sh
source "$SCRIPT_DIR/sync_mock_lobby_fs.sh"
sync_loop() {
  while true; do
    mock_lobby_sync_fs_once "$HOST_MOCK" "$GUEST_MOCK" "$LOBBY" >>"$SYNC_LOG" 2>&1 || true
    sleep 0.25
  done
}
sync_loop &
SYNC_PID=$!

cleanup() {
  stop_recordings
  kill $SYNC_PID $HOST_MASH_PID $JOIN_MASH_PID $GUEST_LAUNCH_PID 2>/dev/null || true
  pkill -f "$BIN" 2>/dev/null || true
  xcrun simctl terminate booted "$PKG" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "== launch iOS guest first =="
# iOS Simulator shares the host network stack — no UDP redir needed.
# --stdout/--stderr often stay empty; --console captures osd_printf.
# Pass absolute container paths for rompath/hashpath.
(
  # -sound none: SDL audio init SEGV on iOS Simulator (null device name).
  xcrun simctl launch --console --terminate-running-process \
    booted "$PKG" \
    -discord_auth -discord_mock iOSGuest \
    -discord_lobby "$LOBBY" -discord_players 2 \
    -discord_directory_port "$JOIN_DIR_PORT" -port "$JOIN_PORT" \
    -direct_connect_timeout "$CONNECT_TIMEOUT" \
    -rompath "$GUEST_ROMS" -hashpath "$GUEST_HASH" \
    -sound none \
    -keepaspect -view "Screen 0 Standard (4:3)" \
    snes snes:tmnt4
) >"$JOIN_LOG" 2>&1 &
GUEST_LAUNCH_PID=$!

for i in $(seq 1 60); do
  GUEST_PID="$(find_guest_pid)"
  [[ -n "$GUEST_PID" ]] && break
  sleep 0.5
done
[[ -n "$GUEST_PID" ]] || die "iOS guest process not found"
echo "guest pid=$GUEST_PID launch=$GUEST_LAUNCH_PID"

for i in $(seq 1 120); do
  if rg -q 'Signed in to Discord as iOSGuest' "$JOIN_LOG" 2>/dev/null; then
    break
  fi
  if [[ -f "$GUEST_MOCK/$LOBBY.log" ]] && rg -q '"name":"iOSGuest"' "$GUEST_MOCK/$LOBBY.log" 2>/dev/null; then
    break
  fi
  alive "$GUEST_PID" || die "guest died during mock-auth"
  sleep 0.5
done
if ! rg -q 'Signed in to Discord as iOSGuest' "$JOIN_LOG" 2>/dev/null \
  && ! rg -q '"name":"iOSGuest"' "$GUEST_MOCK/$LOBBY.log" 2>/dev/null; then
  die "guest mock-auth failed"
fi
echo "guest mock-auth ok"

echo "== launch macOS host =="
cd "$ROOT"
stdbuf -oL -eL "$BIN" -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock MacHost \
  -discord_lobby "$LOBBY" -discord_host -discord_players 2 \
  -discord_directory_port "$HOST_DIR_PORT" -port "$HOST_PORT" \
  -direct_connect_timeout "$CONNECT_TIMEOUT" \
  -rompath "$ROMPATH" -hashpath hash \
  snes snes:tmnt4 \
  >"$HOST_LOG" 2>&1 &
HOST_PID=$!
echo "host pid=$HOST_PID"

for i in $(seq 1 60); do
  grep -q "Signed in to Discord as MacHost" "$HOST_LOG" && break
  alive "$HOST_PID" || die "host died startup"
  sleep 0.5
done
grep -q "Signed in to Discord as MacHost" "$HOST_LOG" || die "host mock-auth failed"
echo "host mock-auth ok"

for i in $(seq 1 80); do
  if [[ -f "$HOST_MOCK/$LOBBY.log" ]] && rg -q '"type":"host"' "$HOST_MOCK/$LOBBY.log"; then
    break
  fi
  alive "$HOST_PID" || die "host died before lobby"
  sleep 0.25
done
echo "host lobby announced"

for i in $(seq 1 90); do
  if rg -q 'iOSGuest' "$HOST_MOCK/$LOBBY.log" 2>/dev/null; then
    echo "guest joined lobby"
    break
  fi
  alive "$HOST_PID" || die "host died waiting guest join"
  sleep 0.5
done
rg -q 'iOSGuest' "$HOST_MOCK/$LOBBY.log" || die "guest never joined lobby"

echo "== wait for discovery/ready in mock lobby =="
for i in $(seq 1 40); do
  disc=$(rg -c '"type":"discovery"' "$HOST_MOCK/$LOBBY.log" 2>/dev/null || echo 0)
  ready=$(rg -c '"type":"ready"' "$HOST_MOCK/$LOBBY.log" 2>/dev/null || echo 0)
  disc=${disc:-0}; ready=${ready:-0}
  if (( disc >= 2 && ready >= 2 )); then
    echo "lobby discovery=$disc ready=$ready"
    break
  fi
  if (( i % 10 == 0 )); then
    echo "  wait discovery t=${i}s discovery=$disc ready=$ready"
    tail -5 "$HOST_MOCK/$LOBBY.log" 2>/dev/null || true
  fi
  alive "$HOST_PID" || die "host died waiting discovery"
  sleep 0.5
done

echo "== wait for mesh + in-game frames on both peers =="
IN_GAME=0
FRAME_WAIT=$(( CONNECT_TIMEOUT + 180 ))
for i in $(seq 1 "$FRAME_WAIT"); do
  check_desync
  alive "$HOST_PID" || die "host died waiting in-game t=$i"
  GUEST_PID="$(find_guest_pid)"
  [[ -n "$GUEST_PID" ]] || die "iOS guest died waiting in-game t=$i"
  if rg -q 'direct connection to every player' "$HOST_LOG" "$JOIN_LOG"; then
    echo "lobby dump:"; cat "$HOST_MOCK/$LOBBY.log" 2>/dev/null || true
    die "mesh timed out"
  fi
  if rg -q '\[INPUT_FRAME\]' "$HOST_LOG" && rg -q '\[INPUT_FRAME\]' "$JOIN_LOG"; then
    IN_GAME=1
    echo "both peers emitting INPUT_FRAME at t=${i}s"
    break
  fi
  if (( i % 10 == 0 )); then
    hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
    jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
    disc=$(rg -c '"type":"discovery"' "$HOST_MOCK/$LOBBY.log" 2>/dev/null || echo 0)
    echo "  wait in-game t=${i}s host_frames=$hf join_frames=$jf discovery=$disc"
  fi
  sleep 1
done
[[ "$IN_GAME" -eq 1 ]] || die "never got INPUT_FRAME on both peers"

echo "== wait for netplay clock + peer catch-up before inputs =="
CLOCK_OK=0
for i in $(seq 1 90); do
  check_desync
  alive "$HOST_PID" || die "host died waiting netplay clock"
  if rg -q 'Netplay clock started' "$HOST_LOG" "$JOIN_LOG" "$ROOT/MAMEHub.log" 2>/dev/null; then
    CLOCK_OK=1
    echo "netplay clock started at settle t=${i}s"
    break
  fi
  sleep 1
done
[[ "$CLOCK_OK" -eq 1 ]] || die "netplay clock never started"

SETTLED=0
prev_hf=0
for i in $(seq 1 120); do
  check_desync
  alive "$HOST_PID" || die "host died during catch-up wait"
  GUEST_PID="$(find_guest_pid)"
  [[ -n "$GUEST_PID" ]] || die "iOS guest died during catch-up wait"
  hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
  jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
  hf=${hf:-0}; jf=${jf:-0}
  if (( hf >= 120 && jf >= 60 && hf > prev_hf )); then
    SETTLED=1
    echo "peers settled host_frames=$hf join_frames=$jf t=${i}s"
    break
  fi
  if (( i % 10 == 0 )); then
    echo "  catch-up t=${i}s host_frames=$hf join_frames=$jf (need host>=120 join>=60, advancing)"
  fi
  prev_hf=$hf
  sleep 1
done
[[ "$SETTLED" -eq 1 ]] || die "peers never caught up enough to play safely"
xcrun simctl io booted screenshot "$OUT/in-game.png" >/dev/null 2>&1 || true
screencapture -x -l "$(host_window_id "$HOST_PID")" "$OUT/host-in-game.png" 2>/dev/null || true
sleep 2

GUEST_PID="$(find_guest_pid)"
[[ -n "$GUEST_PID" ]] || die "lost guest pid before title nav"
echo "using guest pid=$GUEST_PID for pad inject"

if [[ "$RECORD_MP4" == "1" ]]; then
  echo "== start host + guest MP4 recording =="
  need ffmpeg
  rm -f "$HOST_MP4" "$GUEST_MP4" "$HOST_MOV"
  # iOS Simulator display via simctl (does not steal host keyboard focus)
  xcrun simctl io booted recordVideo --codec=h264 --force "$GUEST_MP4" >"$OUT/guest-record.log" 2>&1 &
  GUEST_REC_PID=$!
  # Host: ffmpeg crops Capture screen 0 to the mamehub window.
  # Avoid screencapture -v -l, which previously correlated with zero MAME inputs.
  BOUNDS="$(host_window_bounds "$HOST_PID")"
  [[ -n "$BOUNDS" ]] || die "could not resolve host window bounds for pid $HOST_PID"
  # shellcheck disable=SC2086
  set -- $BOUNDS
  HOST_WID=$1; HX=$2; HY=$3; HW=$4; HH=$5
  echo "host window id=$HOST_WID bounds=${HX},${HY} ${HW}x${HH}"
  # Even crop dims required by libx264
  HW=$(( HW - HW % 2 )); HH=$(( HH - HH % 2 ))
  ffmpeg -y -f avfoundation -framerate 30 -capture_cursor 0 -i "Capture screen 0" \
    -vf "crop=${HW}:${HH}:${HX}:${HY}" -an -c:v libx264 -pix_fmt yuv420p -preset ultrafast \
    "$HOST_MP4" >"$OUT/host-record.log" 2>&1 &
  HOST_REC_PID=$!
  for i in $(seq 1 30); do
    rg -q 'Recording started' "$OUT/guest-record.log" 2>/dev/null && break
    alive "$GUEST_REC_PID" || die "guest recorder died"
    sleep 0.5
  done
  sleep 1
  alive "$HOST_REC_PID" || die "host ffmpeg recorder died (see $OUT/host-record.log)"
  echo "recording guest_pid=$GUEST_REC_PID host_pid=$HOST_REC_PID -> $GUEST_MP4 / $HOST_MP4"
fi

# Probe that CGEvent injection reaches MAME before relying on title nav.
echo "== input probe (host Start) =="
focus_pid "$HOST_PID"
sleep 0.3
before_ne=$(count_nonempty_inputs)
"$PAD" "$HOST_PID" tap start 200
sleep 0.8
after_ne=$(count_nonempty_inputs)
echo "nonempty inputs before=$before_ne after=$after_ne"
if (( after_ne <= before_ne )); then
  # Retry once with enter as well (SNES IPT_START default is often Enter)
  "$PAD" "$HOST_PID" tap enter 200
  "$PAD" "$HOST_PID" tap start 200
  sleep 1
  after_ne=$(count_nonempty_inputs)
fi
(( after_ne > before_ne )) || die "pad inject not reaching host MAME (nonempty inputs stuck at $after_ne) — check Accessibility for this terminal"

echo "== title / Two Players nav (both peers, concurrent inject) =="
# Host: CGEvent.postToPid. Guest: Documents/mamehub_inject.log (no focus steal).
[[ -n "$GUEST_INJECT" ]] || die "GUEST_INJECT unset"
# Wait for VirtualControls inject watcher (installs ~1.5s after launch)
for i in $(seq 1 40); do
  if rg -q 'watching .*mamehub_inject' "$JOIN_LOG" 2>/dev/null; then
    echo "guest inject watcher ready"
    break
  fi
  sleep 0.25
done
for i in 1 2 3 4 5 6 8 10 12 14 16; do
  check_desync
  alive "$HOST_PID" || die "host died during title nav"
  GUEST_PID="$(find_guest_pid)"
  [[ -n "$GUEST_PID" ]] || die "guest died during title nav"
  "$PAD" "$HOST_PID" tap start 150
  ios_tap start 150
  sleep 1.1
done
"$PAD" "$HOST_PID" tap down 150
sleep 0.5
"$PAD" "$HOST_PID" tap start 180
sleep 2
for i in 1 2 3 4 5 6 8 10; do
  check_desync
  alive "$HOST_PID" || die "host died during char-select"
  GUEST_PID="$(find_guest_pid)"
  [[ -n "$GUEST_PID" ]] || die "guest died during char-select"
  "$PAD" "$HOST_PID" tap start 140
  ios_tap start 140
  sleep 0.9
done
xcrun simctl io booted screenshot "$OUT/after-title.png" >/dev/null 2>&1 || true
screencapture -x -l "$(host_window_id "$HOST_PID")" "$OUT/host-after-title.png" 2>/dev/null || true

p1s=$(count_input_token 'P1 Start'); p2s=$(count_input_token 'P2 Start')
p1s=${p1s:-0}; p2s=${p2s:-0}
echo "after title nav: P1 Start frames=$p1s P2 Start frames=$p2s nonempty=$(count_nonempty_inputs)"
(( p1s >= 5 )) || die "P1 Start never registered in netplay inputs ($p1s) — still on attract likely"
if (( p2s < 1 )); then
  echo "P2 Start missing — retrying guest Start via inject file"
  for i in 1 2 3 4 5 6 8 10 12; do
    ios_tap start 180
    sleep 0.6
    p2s=$(count_input_token 'P2 Start'); p2s=${p2s:-0}
    (( p2s >= 1 )) && break
  done
fi
p2s=$(count_input_token 'P2 Start'); p2s=${p2s:-0}
(( p2s >= 1 )) || die "P2 Start never registered ($p2s) — iOS inject file not reaching VirtualControls"

echo "== mash both peers for ${MASH_SECS}s (concurrent, no focus steal) =="
GUEST_PID="$(find_guest_pid)"
[[ -n "$GUEST_PID" ]] || die "guest died before mash"
# Keep host frontmost once; guest inject is file-based and does not need focus.
focus_pid "$HOST_PID"
"$PAD" "$HOST_PID" mash "$MASH_SECS" 1111 >"$HOST_MASH_LOG" 2>&1 &
HOST_MASH_PID=$!
ios_mash "$MASH_SECS" 2222 >"$JOIN_MASH_LOG" 2>&1 &
JOIN_MASH_PID=$!
echo "host_mash=$HOST_MASH_PID ios_mash=$JOIN_MASH_PID guest_app=$GUEST_PID inject=$GUEST_INJECT"

SYNC_OK=1
MASH_INPUT_OK=0
for i in $(seq 1 "$MASH_SECS"); do
  if ! alive "$HOST_PID"; then
    SYNC_OK=0
    echo "HOST DIED during mash t=$i"
    break
  fi
  if ! alive "$GUEST_PID"; then
    GUEST_PID="$(find_guest_pid)"
    if [[ -z "$GUEST_PID" ]]; then
      SYNC_OK=0
      echo "IOS GUEST DIED during mash t=$i"
      break
    fi
  fi
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$HOST_LOG" "$ROOT/MAMEHub.log" "$JOIN_LOG" 2>/dev/null; then
    SYNC_OK=0
    echo "DESYNC/FATAL at mash t=$i"
    break
  fi
  # Face/dir activity beyond Start means we are past pure attract Start-spam.
  if rg -q 'P1 (A|B|X|Y|Up|Down|Left|Right)|P2 (A|B|X|Y|Up|Down|Left|Right)' "$HOST_LOG" 2>/dev/null; then
    MASH_INPUT_OK=1
  fi
  if (( i % 15 == 0 )); then
    hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
    jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
    echo "mash t=${i}s host_frames=$hf join_frames=$jf nonempty=$(count_nonempty_inputs)"
    xcrun simctl io booted screenshot "$OUT/mash-${i}.png" >/dev/null 2>&1 || true
    screencapture -x -l "$(host_window_id "$HOST_PID")" "$OUT/host-mash-${i}.png" 2>/dev/null || true
  fi
  sleep 1
done

wait "$HOST_MASH_PID" 2>/dev/null || true
wait "$JOIN_MASH_PID" 2>/dev/null || true

# Keep a couple seconds of post-mash footage, then finalize recordings
sleep 2
if [[ "$RECORD_MP4" == "1" ]]; then
  echo "== stop MP4 recording =="
  stop_recordings
fi

check_desync
xcrun simctl io booted screenshot "$OUT/final.png" >/dev/null 2>&1 || true
screencapture -x -l "$(host_window_id "$HOST_PID")" "$OUT/host-final.png" 2>/dev/null || true

echo "== last INPUT_FRAME lines =="
rg '\[INPUT_FRAME\]' "$HOST_LOG" | tail -5 || true
rg '\[INPUT_FRAME\]' "$JOIN_LOG" | tail -5 || true
echo "== input summary =="
rg -o 'inputs=\[[^\]]*\]' "$HOST_LOG" | sort | uniq -c | sort -rn | head -25 || true
echo "== lobby tail =="
tail -20 "$HOST_MOCK/$LOBBY.log" 2>/dev/null || true

hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
[[ "$hf" -gt 100 ]] || die "too few host frames ($hf)"
[[ "$jf" -gt 100 ]] || die "too few ios frames ($jf)"
[[ "$SYNC_OK" -eq 1 ]] || die "peers died or desynced during mash"
[[ "$MASH_INPUT_OK" -eq 1 ]] || die "mash never registered face/dir inputs — still stuck on attract/menu"

if rg -qi 'INPUT DESYNC' "$HOST_LOG" "$JOIN_LOG" "$ROOT/MAMEHub.log" 2>/dev/null; then
  die "INPUT DESYNC detected"
fi

if [[ "$RECORD_MP4" == "1" ]]; then
  [[ -f "$HOST_MP4" ]] || die "missing host mp4 at $HOST_MP4"
  [[ -f "$GUEST_MP4" ]] || die "missing guest mp4 at $GUEST_MP4"
  echo "recordings: $HOST_MP4 ($(stat -f%z "$HOST_MP4") bytes) $GUEST_MP4 ($(stat -f%z "$GUEST_MP4") bytes)"
fi

echo "PASS: macOS+iOS snes:tmnt4 synced through ${MASH_SECS}s mash (host_frames=$hf join_frames=$jf, no INPUT DESYNC)"
exit 0
