#!/bin/bash
# macOS host + Android emulator joiner: launch snes:tmnt4 over Discord mock,
# navigate past the title, mash inputs on both peers, assert no INPUT DESYNC.
#
# Requires: emulator-5554, built APK, mamehub binary, pad tool, snes softlist + ROM.
#
# Env:
#   MASH_SECS          mash duration (default 60)
#   MAMEHUB_CONNECT_TIMEOUT  peer mesh timeout (default 90)
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ADB="${ADB:-/Users/jjg/Library/Android/sdk/platform-tools/adb}"
BIN="${MAMEHUB_BIN:-$ROOT/mamehub}"
ROMPATH="${MAMEHUB_ROMPATH:-roms}"
OUT="${MAMEHUB_E2E_OUT:-/tmp/mamehub-android-cross-play}"
HOST_MOCK="${MAMEHUB_MOCK:-/tmp/mamehub_mock}"
DEVICE_FILES="/storage/emulated/0/Android/data/org.mamedev.mame/files"
DEVICE_MOCK="$DEVICE_FILES/mamehub_mock"
ROMS_ON_DEVICE="$DEVICE_FILES/roms"
HASH_ON_DEVICE="$DEVICE_FILES/hash"
PKG=org.mamedev.mame
LOBBY="${MAMEHUB_LOBBY:-android-cross-play}"
HOST_PORT="${MAMEHUB_HOST_PORT:-5965}"
JOIN_PORT="${MAMEHUB_JOIN_PORT:-5975}"
DIR_PORT="${MAMEHUB_DIR_PORT:-5816}"
APK="${MAMEHUB_APK:-$ROOT/android-project/app/build/outputs/apk/debug/app-debug.apk}"
CONNECT_TIMEOUT="${MAMEHUB_CONNECT_TIMEOUT:-60}"
MASH_SECS="${MASH_SECS:-60}"
PAD="${PAD:-$SCRIPT_DIR/tools/bin/pad}"

HOST_LOG="$OUT/host.log"
JOIN_LOG="$OUT/device.logcat"
SYNC_LOG="$OUT/mock-sync.log"
HOST_MASH_LOG="$OUT/host_mash.err"
JOIN_MASH_LOG="$OUT/join_mash.err"
HOST_MASH_PID=
JOIN_MASH_PID=

die() { echo "FAIL: $*"; tail -50 "$HOST_LOG" 2>/dev/null || true; rg -i 'INPUT DESYNC|FATAL|Error:' "$JOIN_LOG" 2>/dev/null | tail -20 || true; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing $1"; }
alive() { kill -0 "$1" 2>/dev/null; }
check_desync() {
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$HOST_LOG" "$ROOT/MAMEHub.log" 2>/dev/null; then
    die "DESYNC/FATAL in host logs"
  fi
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$JOIN_LOG" 2>/dev/null; then
    die "DESYNC/FATAL in Android logcat"
  fi
}

need "$ADB"
[[ -x "$BIN" ]] || die "mamehub binary not found at $BIN"
[[ -x "$PAD" ]] || die "pad tool not found at $PAD (run tests/e2e/mamehub/tools/build.sh)"
[[ -f "$ROOT/hash/snes.xml" ]] || die "missing hash/snes.xml"
[[ -f "$ROOT/roms/snes/tmnt4.zip" ]] || die "missing roms/snes/tmnt4.zip"
"$ADB" devices | rg -q $'emulator-5554\tdevice' || die "emulator-5554 not connected"

mkdir -p "$OUT"
rm -rf "$HOST_MOCK"
mkdir -p "$HOST_MOCK"
: >"$HOST_LOG"
: >"$SYNC_LOG"
: >"$HOST_MASH_LOG"
: >"$JOIN_MASH_LOG"
rm -f "$OUT"/*.png "$ROOT/MAMEHub.log"

pkill -f "$BIN" 2>/dev/null || true
"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1 || true
sleep 1

echo "== install APK =="
"$ADB" install -r "$APK" || die "adb install failed"

echo "== UDP bridge =="
chmod +x "$SCRIPT_DIR/setup_android_adb_bridge.sh"
MAMEHUB_HOST_PORT="$HOST_PORT" MAMEHUB_JOIN_PORT="$JOIN_PORT" MAMEHUB_DIR_PORT= \
  ADB="$ADB" ANDROID_SERIAL=emulator-5554 \
  "$SCRIPT_DIR/setup_android_adb_bridge.sh" || die "bridge failed"

echo "== push ROMs + softlist hash =="
"$ADB" shell "mkdir -p '$DEVICE_MOCK' '$ROMS_ON_DEVICE/snes' '$HASH_ON_DEVICE'; rm -f '$DEVICE_MOCK'/*" || true
"$ADB" push "$ROOT/hash/snes.xml" "$HASH_ON_DEVICE/snes.xml" >/dev/null
"$ADB" push "$ROOT/roms/snes/tmnt4.zip" "$ROMS_ON_DEVICE/tmnt4.zip" >/dev/null
"$ADB" push "$ROOT/roms/snes/tmnt4.zip" "$ROMS_ON_DEVICE/snes/tmnt4.zip" >/dev/null
"$ADB" push "$ROOT/roms/snes.zip" "$ROMS_ON_DEVICE/snes.zip" >/dev/null || true

# Append-only mock lobby sync (pull/push overwrite drops discovery/ready).
# shellcheck source=sync_mock_lobby.sh
source "$SCRIPT_DIR/sync_mock_lobby.sh"
sync_loop() {
  while true; do
    mock_lobby_sync_once "$HOST_MOCK" "$DEVICE_MOCK" "$LOBBY" "$ADB" "$OUT" >>"$SYNC_LOG" 2>&1 || true
    sleep 0.25
  done
}
sync_loop &
SYNC_PID=$!

"$ADB" logcat -c
"$ADB" logcat -v time >"$JOIN_LOG" &
LOGCAT_PID=$!
trap 'kill $SYNC_PID $LOGCAT_PID $HOST_MASH_PID $JOIN_MASH_PID 2>/dev/null; pkill -f "$BIN" 2>/dev/null; "$ADB" shell am force-stop "$PKG" >/dev/null 2>&1' EXIT

# Hold a MAME gameplay key long enough to be sampled (adb keyevent is too brief).
# Uses the app's INJECT_KEY broadcast → VirtualControlsView.holdKey.
android_hold_key() {
  local code=$1 hold_ms=${2:-100}
  "$ADB" shell am broadcast -a org.mamedev.mame.INJECT_KEY -p "$PKG" \
    --ei code "$code" --ei hold_ms "$hold_ms" >/dev/null 2>&1 || true
}

# Android key mash using MAME keyboard bindings (not GAMEPAD 96-100 = SDL UNKNOWN).
# up down left right Ctrl(Y) Alt(B) Space(A) Shift(X) 1(Start) 5(Select)
android_mash() {
  local secs=$1 seed=$2
  local deadline=$((SECONDS + secs))
  local n=0
  local state=$seed
  rand() { state=$(( (state * 1103515245 + 12345) & 0x7fffffff )); echo $(( state % ($1 + 1) )); }
  local keys=(19 20 21 22 113 57 62 59 8 12)
  while (( SECONDS < deadline )); do
    local k=${keys[$(rand $((${#keys[@]} - 1)))]}
    android_hold_key "$k" "$(( 40 + $(rand 120) ))"
    sleep "0.$(( 4 + $(rand 12) ))"
    n=$((n + 1))
    if (( n % 40 == 0 )); then
      echo "android mash pulses=$n remain=$((deadline - SECONDS))s" >>"$JOIN_MASH_LOG"
    fi
  done
  echo "android mash done pulses=$n" >>"$JOIN_MASH_LOG"
}

echo "== launch Android guest first =="
ESA_ARGS="-discord_auth,-discord_mock,AndroidGuest,-discord_lobby,${LOBBY},-discord_players,2,-discord_directory_port,${DIR_PORT},-port,${JOIN_PORT},-direct_connect_timeout,${CONNECT_TIMEOUT},-rompath,${ROMS_ON_DEVICE},-hashpath,${HASH_ON_DEVICE},snes,snes:tmnt4"
"$ADB" shell am start -n "$PKG/.MAME" --esa args "$ESA_ARGS" || die "am start failed"

for i in $(seq 1 120); do
  rg -q 'Signed in to Discord as AndroidGuest' "$JOIN_LOG" && break
  if "$ADB" shell dumpsys activity activities | rg -q 'discord.com/oauth2|AuthenticationActivity'; then
    die "Chrome OAuth opened on guest"
  fi
  sleep 0.5
done
rg -q 'Signed in to Discord as AndroidGuest' "$JOIN_LOG" || die "guest mock-auth failed"
echo "guest mock-auth ok"

echo "== launch macOS host =="
cd "$ROOT"
# line-buffer stdout so INPUT_FRAME lines appear promptly for the waiter
stdbuf -oL -eL "$BIN" -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock MacHost \
  -discord_lobby "$LOBBY" -discord_host -discord_players 2 \
  -discord_directory_port "$DIR_PORT" -port "$HOST_PORT" \
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
  if rg -q 'AndroidGuest' "$HOST_MOCK/$LOBBY.log" 2>/dev/null; then
    echo "guest joined lobby"
    break
  fi
  alive "$HOST_PID" || die "host died waiting guest join"
  sleep 0.5
done
rg -q 'AndroidGuest' "$HOST_MOCK/$LOBBY.log" || die "guest never joined lobby"

echo "== wait for discovery/ready in mock lobby =="
for i in $(seq 1 40); do
  disc=$(rg -c '"type":"discovery"' "$HOST_MOCK/$LOBBY.log" 2>/dev/null || echo 0)
  ready=$(rg -c '"type":"ready"' "$HOST_MOCK/$LOBBY.log" 2>/dev/null || echo 0)
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
# Softlist ROM load on emulator can take well beyond the UDP mesh timeout;
# keep waiting for frames after peers are connected.
FRAME_WAIT=$(( CONNECT_TIMEOUT + 180 ))
for i in $(seq 1 "$FRAME_WAIT"); do
  check_desync
  alive "$HOST_PID" || die "host died waiting in-game t=$i"
  if rg -q 'direct connection to every player' "$HOST_LOG"; then
    echo "lobby dump:"; cat "$HOST_MOCK/$LOBBY.log" 2>/dev/null || true
    die "mesh timed out"
  fi
  if rg -q '\[INPUT_FRAME\]' "$HOST_LOG" && rg -q '\[INPUT_FRAME\]' "$JOIN_LOG"; then
    IN_GAME=1
    echo "both peers emitting INPUT_FRAME at t=${i}s"
    break
  fi
  # Host-only frames early is OK while guest boots ROM; keep waiting
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
  if rg -q 'Netplay clock started' "$HOST_LOG" "$ROOT/MAMEHub.log" 2>/dev/null; then
    CLOCK_OK=1
    echo "netplay clock started at settle t=${i}s"
    break
  fi
  sleep 1
done
[[ "$CLOCK_OK" -eq 1 ]] || die "netplay clock never started"

# Emulator guest is much slower; wait until both peers have a healthy
# frame count and host frames are still advancing (not wedged on peer lag).
SETTLED=0
prev_hf=0
for i in $(seq 1 120); do
  check_desync
  alive "$HOST_PID" || die "host died during catch-up wait"
  if ! "$ADB" shell pidof "$PKG" >/dev/null 2>&1; then
    die "android guest died during catch-up wait"
  fi
  hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
  jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
  # Treat non-numeric (rg missing) as 0
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
"$ADB" exec-out screencap -p >"$OUT/in-game.png" || true
screencapture -x "$OUT/host-in-game.png" 2>/dev/null || true
sleep 2

echo "== host: title / Two Players nav =="
for i in 1 2 3 4 5 6 8 10 12; do
  check_desync
  alive "$HOST_PID" || die "host died during title nav"
  "$PAD" "$HOST_PID" tap start 120
  sleep 1.2
done
"$PAD" "$HOST_PID" tap down 120
sleep 0.5
"$PAD" "$HOST_PID" tap start 150
sleep 2
for i in 1 2 3 4 5 6 8; do
  check_desync
  alive "$HOST_PID" || die "host died during char-select"
  "$PAD" "$HOST_PID" tap start 100
  sleep 0.35
  # Guest Start1 via held KEYCODE_1 (instant keyevent is too brief for MAME)
  android_hold_key 8 120
  sleep 0.7
done
"$ADB" exec-out screencap -p >"$OUT/after-title.png" || true
screencapture -x "$OUT/host-after-title.png" 2>/dev/null || true

echo "== mash both peers for ${MASH_SECS}s =="
"$PAD" "$HOST_PID" mash "$MASH_SECS" 1111 >"$HOST_MASH_LOG" 2>&1 &
HOST_MASH_PID=$!
android_mash "$MASH_SECS" 2222 &
JOIN_MASH_PID=$!
echo "host_mash=$HOST_MASH_PID android_mash=$JOIN_MASH_PID"

SYNC_OK=1
for i in $(seq 1 "$MASH_SECS"); do
  if ! alive "$HOST_PID"; then
    SYNC_OK=0
    echo "HOST DIED during mash t=$i"
    break
  fi
  if ! "$ADB" shell pidof "$PKG" >/dev/null 2>&1; then
    SYNC_OK=0
    echo "ANDROID GUEST DIED during mash t=$i"
    break
  fi
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$HOST_LOG" "$ROOT/MAMEHub.log" "$JOIN_LOG" 2>/dev/null; then
    SYNC_OK=0
    echo "DESYNC/FATAL at mash t=$i"
    break
  fi
  if (( i % 15 == 0 )); then
    hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
    jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
    echo "mash t=${i}s host_frames=$hf join_frames=$jf"
    "$ADB" exec-out screencap -p >"$OUT/mash-${i}.png" || true
  fi
  sleep 1
done

wait "$HOST_MASH_PID" 2>/dev/null || true
wait "$JOIN_MASH_PID" 2>/dev/null || true

check_desync
"$ADB" exec-out screencap -p >"$OUT/final.png" || true
screencapture -x "$OUT/host-final.png" 2>/dev/null || true

echo "== last INPUT_FRAME lines =="
rg '\[INPUT_FRAME\]' "$HOST_LOG" | tail -5 || true
rg '\[INPUT_FRAME\]' "$JOIN_LOG" | tail -5 || true
echo "== lobby tail =="
tail -20 "$HOST_MOCK/$LOBBY.log" 2>/dev/null || true

hf=$(rg -c '\[INPUT_FRAME\]' "$HOST_LOG" 2>/dev/null || echo 0)
jf=$(rg -c '\[INPUT_FRAME\]' "$JOIN_LOG" 2>/dev/null || echo 0)
[[ "$hf" -gt 100 ]] || die "too few host frames ($hf)"
[[ "$jf" -gt 100 ]] || die "too few android frames ($jf)"
[[ "$SYNC_OK" -eq 1 ]] || die "peers died or desynced during mash"

if rg -qi 'INPUT DESYNC' "$HOST_LOG" "$JOIN_LOG" "$ROOT/MAMEHub.log" 2>/dev/null; then
  die "INPUT DESYNC detected"
fi

echo "PASS: macOS+Android snes:tmnt4 synced through ${MASH_SECS}s mash (host_frames=$hf join_frames=$jf, no INPUT DESYNC)"
exit 0
