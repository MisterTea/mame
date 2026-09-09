#!/bin/bash
# macOS host + Android emulator joiner, Discord mock (no Chrome OAuth).
# Sets up emulator UDP redirs, syncs mock lobby files, then runs both peers.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ADB="${ADB:-/Users/jjg/Library/Android/sdk/platform-tools/adb}"
BIN="${MAMEHUB_BIN:-$ROOT/mamehub}"
ROMPATH="${MAMEHUB_ROMPATH:-roms}"
OUT="${MAMEHUB_E2E_OUT:-/tmp/mamehub-android-cross-e2e}"
HOST_MOCK="${MAMEHUB_MOCK:-/tmp/mamehub_mock}"
DEVICE_MOCK="/storage/emulated/0/Android/data/org.mamedev.mame/files/mamehub_mock"
DEVICE_FILES="/storage/emulated/0/Android/data/org.mamedev.mame/files"
PKG=org.mamedev.mame
LOBBY="${MAMEHUB_LOBBY:-android-cross-e2e-retest}"
HOST_PORT="${MAMEHUB_HOST_PORT:-5965}"
JOIN_PORT="${MAMEHUB_JOIN_PORT:-5975}"
DIR_PORT="${MAMEHUB_DIR_PORT:-5816}"
APK="${MAMEHUB_APK:-$ROOT/android-project/app/build/outputs/apk/debug/app-debug.apk}"
CONNECT_TIMEOUT="${MAMEHUB_CONNECT_TIMEOUT:-60}"
GAME_SYSTEM="${MAMEHUB_GAME_SYSTEM:-snes}"
# Bare system is enough to validate the UDP mesh; set e.g. snes:tmnt4 if hash XML is on device.
GAME_SOFTWARE="${MAMEHUB_GAME_SOFTWARE:-}"

HOST_LOG="$OUT/host.log"
JOIN_LOG="$OUT/device.logcat"
SYNC_LOG="$OUT/mock-sync.log"
ROMS_ON_DEVICE="$DEVICE_FILES/roms"

die() { echo "FAIL: $*"; tail -40 "$HOST_LOG" 2>/dev/null || true; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "missing $1"; }

need "$ADB"
[[ -x "$BIN" ]] || die "mamehub binary not found at $BIN"
"$ADB" devices | rg -q $'emulator-5554\tdevice' || die "emulator-5554 not connected"

mkdir -p "$OUT"
rm -rf "$HOST_MOCK"
mkdir -p "$HOST_MOCK"
: >"$HOST_LOG"
: >"$SYNC_LOG"
rm -f "$OUT"/*.png

pkill -f "$BIN" 2>/dev/null || true
"$ADB" shell am force-stop "$PKG" >/dev/null 2>&1 || true
sleep 1

echo "== install APK =="
"$ADB" install -r "$APK" || die "adb install failed"

echo "== adb/emulator UDP bridge =="
BRIDGE_SCRIPT="$SCRIPT_DIR/setup_android_adb_bridge.sh"
chmod +x "$BRIDGE_SCRIPT" 2>/dev/null || true
MAMEHUB_HOST_PORT="$HOST_PORT" MAMEHUB_JOIN_PORT="$JOIN_PORT" MAMEHUB_DIR_PORT= \
  ADB="$ADB" ANDROID_SERIAL="${ANDROID_SERIAL:-emulator-5554}" \
  "$BRIDGE_SCRIPT" || die "UDP bridge setup failed"

# Device mock + roms/hash for softlist if requested
"$ADB" shell "mkdir -p '$DEVICE_MOCK' '$ROMS_ON_DEVICE' '$DEVICE_FILES/hash'; rm -f '$DEVICE_MOCK'/*" || true
if [[ -n "$GAME_SOFTWARE" && -f "$ROOT/hash/snes.xml" ]]; then
  echo "== push softlist hash =="
  "$ADB" push "$ROOT/hash/snes.xml" "$DEVICE_FILES/hash/snes.xml" >/dev/null || true
fi
if [[ -f "$ROOT/roms/snes/tmnt4.zip" ]]; then
  "$ADB" push "$ROOT/roms/snes/tmnt4.zip" "$ROMS_ON_DEVICE/tmnt4.zip" >/dev/null || true
  "$ADB" shell "mkdir -p '$ROMS_ON_DEVICE/snes'" >/dev/null
  "$ADB" push "$ROOT/roms/snes/tmnt4.zip" "$ROMS_ON_DEVICE/snes/tmnt4.zip" >/dev/null || true
fi

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
trap 'kill $SYNC_PID $LOGCAT_PID 2>/dev/null; pkill -f "$BIN" 2>/dev/null; "$ADB" shell am force-stop "$PKG" >/dev/null 2>&1' EXIT

# Build game argv (omit empty software)
HOST_GAME_ARGS=("$GAME_SYSTEM")
ESA_GAME=",${GAME_SYSTEM}"
if [[ -n "$GAME_SOFTWARE" ]]; then
  HOST_GAME_ARGS+=("$GAME_SOFTWARE")
  ESA_GAME+=",${GAME_SOFTWARE}"
fi

# Start guest first so it is past APK/SDL startup before the host's
# direct_connect_timeout clock begins.
echo "== launching Android joiner first (mock AndroidGuest) =="
ESA_ARGS="-discord_auth,-discord_mock,AndroidGuest,-discord_lobby,${LOBBY},-discord_players,2,-discord_directory_port,${DIR_PORT},-port,${JOIN_PORT},-direct_connect_timeout,${CONNECT_TIMEOUT},-rompath,${ROMS_ON_DEVICE}${ESA_GAME}"
"$ADB" shell am start -n "$PKG/.MAME" --esa args "$ESA_ARGS" || die "am start failed"

echo "== wait for Android mock-auth (no Chrome) =="
AUTH_OK=0
for i in $(seq 1 120); do
  if rg -q 'Signed in to Discord as AndroidGuest' "$JOIN_LOG"; then
    AUTH_OK=1
    break
  fi
  if "$ADB" shell dumpsys activity activities | rg -q 'discord.com/oauth2|AuthenticationActivity'; then
    "$ADB" exec-out screencap -p >"$OUT/chrome-leak.png" || true
    die "Discord OAuth / Chrome opened despite -discord_mock"
  fi
  sleep 0.5
done
[[ "$AUTH_OK" -eq 1 ]] || die "Android guest never reached mock auth"
echo "guest mock-auth ok"

echo "== launching macOS host (mock MacHost, lobby $LOBBY) =="
cd "$ROOT"
"$BIN" -window -nomaximize -resolution 960x720 \
  -discord_auth -discord_mock MacHost \
  -discord_lobby "$LOBBY" -discord_host -discord_players 2 \
  -discord_directory_port "$DIR_PORT" -port "$HOST_PORT" \
  -direct_connect_timeout "$CONNECT_TIMEOUT" -rompath "$ROMPATH" \
  "${HOST_GAME_ARGS[@]}" \
  >"$HOST_LOG" 2>&1 &
HOST_PID=$!
echo "host pid=$HOST_PID"

for i in $(seq 1 60); do
  grep -q "Signed in to Discord as MacHost" "$HOST_LOG" && break
  kill -0 "$HOST_PID" 2>/dev/null || die "host died during startup"
  sleep 0.5
done
grep -q "Signed in to Discord as MacHost" "$HOST_LOG" || die "host did not mock-auth"
if rg -qi 'Waiting for Discord authorization in browser' "$HOST_LOG"; then
  die "host attempted real Discord browser auth"
fi
echo "host mock-auth ok"

for i in $(seq 1 80); do
  if [[ -f "$HOST_MOCK/$LOBBY.log" ]] && rg -q '"type":"host"' "$HOST_MOCK/$LOBBY.log"; then
    break
  fi
  kill -0 "$HOST_PID" 2>/dev/null || die "host died before lobby announce"
  sleep 0.25
done
[[ -f "$HOST_MOCK/$LOBBY.log" ]] || die "host never wrote lobby mock log"
echo "host lobby announced"

for i in $(seq 1 120); do
  if rg -q 'AndroidGuest' "$HOST_MOCK/$LOBBY.log" 2>/dev/null; then
    echo "guest join visible in mock lobby"
    break
  fi
  sleep 0.5
done

"$ADB" exec-out screencap -p >"$OUT/android-running.png" || true
screencapture -x "$OUT/host-running.png" 2>/dev/null || true

MESH_OK=0
for i in $(seq 1 "$CONNECT_TIMEOUT"); do
  ready_count="$(rg -c '"type":"ready"' "$HOST_MOCK/$LOBBY.log" 2>/dev/null || echo 0)"
  if [[ "$ready_count" -ge 2 ]]; then
    MESH_OK=1
    echo "mesh ready messages seen ($ready_count)"
    break
  fi
  if rg -q '\[INPUT_FRAME\] frame=1 ' "$HOST_LOG"; then
    MESH_OK=1
    echo "host reached gameplay (mesh initialized)"
    break
  fi
  if rg -q 'direct connection to every player' "$HOST_LOG"; then
    break
  fi
  if ! kill -0 "$HOST_PID" 2>/dev/null; then
    break
  fi
  sleep 1
done

ANR=0
for i in $(seq 1 10); do
  if "$ADB" shell dumpsys window windows 2>/dev/null | rg -q 'Application Not Responding|isn.t responding'; then
    ANR=1
    "$ADB" exec-out screencap -p >"$OUT/anr.png" || true
    break
  fi
  sleep 1
done

if "$ADB" shell dumpsys activity activities | rg -q 'discord.com/oauth2|ChromeLauncherActivity.*discord'; then
  die "Chrome Discord OAuth still present at end of test"
fi

echo "== results =="
echo "host log: $HOST_LOG"
echo "device log: $JOIN_LOG"
echo "mock lobby:"
tail -30 "$HOST_MOCK/$LOBBY.log" 2>/dev/null || true

[[ "$ANR" -eq 0 ]] || die "ANR dialog detected during run (see $OUT/anr.png)"
rg -q 'Signed in to Discord as MacHost' "$HOST_LOG" || die "missing host mock sign-in"
rg -q 'Signed in to Discord as AndroidGuest' "$JOIN_LOG" || die "missing guest mock sign-in"
rg -q 'AndroidGuest' "$HOST_MOCK/$LOBBY.log" 2>/dev/null || die "guest never joined mock lobby"
if rg -q 'direct connection to every player' "$HOST_LOG"; then
  die "peer mesh timed out (UDP path still blocked — check emulator redir)"
fi
[[ "$MESH_OK" -eq 1 ]] || die "mesh never initialized"

echo "PASS: macOS host + Android joiner mock lobby + UDP mesh OK (no Chrome, no ANR)"
exit 0
