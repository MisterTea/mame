#!/bin/bash
# Bridge MAMEHub peer ports between the host machine and an Android emulator.
#
# MAMEHub's mesh is UDP. `adb forward`/`adb reverse` are TCP-only and cannot
# carry that traffic. This script uses the emulator console instead:
#
#   redir add udp:<hostPort>:<devicePort>
#     Host → 127.0.0.1:<joinPort> is delivered to the emulator's <joinPort>
#
# Guest → host usually works via the host LAN/public IP the host advertises
# (emulator can reach the host at those addresses, or at 10.0.2.2 for
# host-loopback services). No UDP "reverse" exists in adb.
#
# Usage:
#   ./tests/e2e/mamehub/setup_android_adb_bridge.sh
#   ./tests/e2e/mamehub/setup_android_adb_bridge.sh --clear
#
# Env:
#   ADB                path to adb
#   MAMEHUB_HOST_PORT  host peer UDP port (default 5965)
#   MAMEHUB_JOIN_PORT  guest peer UDP port (default 5975)
#   ANDROID_SERIAL     adb device serial (default: first emulator-*)
set -euo pipefail

ADB="${ADB:-/Users/jjg/Library/Android/sdk/platform-tools/adb}"
HOST_PORT="${MAMEHUB_HOST_PORT:-5965}"
JOIN_PORT="${MAMEHUB_JOIN_PORT:-5975}"

die() { echo "FAIL: $*" >&2; exit 1; }

[[ -x "$ADB" ]] || command -v "$ADB" >/dev/null 2>&1 || die "adb not found at $ADB"

if [[ -z "${ANDROID_SERIAL:-}" ]]; then
  ANDROID_SERIAL="$("$ADB" devices | awk '/^emulator-/{print $1; exit}')"
fi
[[ -n "$ANDROID_SERIAL" ]] || die "no emulator device connected"
export ANDROID_SERIAL

adb() { command "$ADB" -s "$ANDROID_SERIAL" "$@"; }

# adb emu prints "OK" / "KO: ..." on the console channel
emu() {
  local out
  out="$(adb emu "$@" 2>&1)" || true
  printf '%s\n' "$out"
  if printf '%s\n' "$out" | rg -q '^KO:'; then
    return 1
  fi
  return 0
}

clear_bridges() {
  echo "== clearing bridges for serial $ANDROID_SERIAL =="
  # Drop any leftover TCP forwards from earlier attempts
  adb forward --remove-all 2>/dev/null || true
  adb reverse --remove-all 2>/dev/null || true

  # Remove our UDP redirs if present (ignore errors if missing)
  emu redir del "udp:${JOIN_PORT}" >/dev/null || true
  emu redir del "udp:${HOST_PORT}" >/dev/null || true
}

if [[ "${1:-}" == "--clear" ]] || [[ "${1:-}" == "-c" ]]; then
  clear_bridges
  echo "cleared"
  emu redir list || true
  exit 0
fi

clear_bridges

echo "== setting up UDP redirs (device=$ANDROID_SERIAL) =="
echo "  host→guest: host UDP :${JOIN_PORT}  =>  emulator :${JOIN_PORT}"
emu redir add "udp:${JOIN_PORT}:${JOIN_PORT}" || die "failed to add udp redir for join port ${JOIN_PORT}"

# Optional: also publish host port in case something on the host dials the
# guest's view of the host port through a mis-ordered endpoint list.
# Harmless if unused.
echo "  (info) guest→host: emulator should dial host LAN/public IP or 10.0.2.2:${HOST_PORT}"
echo "         (no UDP reverse exists; host must advertise a host-reachable address)"

echo "== active UDP redirections =="
emu redir list
echo "OK: emulator UDP bridge ready"
