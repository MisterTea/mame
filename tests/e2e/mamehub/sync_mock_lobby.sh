#!/bin/bash
# Append-only bidirectional sync for Discord mock lobby logs.
#
# Host (/tmp/mamehub_mock) and Android device mock dirs each APPEND to the
# same lobby file. Naive adb pull/push overwrites and drops discovery/ready
# messages, which breaks the UDP mesh. This helper only appends lines that
# the other side is missing so each peer's mock_lines_read offsets stay valid.
#
# Usage (sourced or executed in a loop by e2e scripts):
#   source sync_mock_lobby.sh
#   mock_lobby_sync_once "$HOST_MOCK" "$DEVICE_MOCK" "$LOBBY" "$ADB" "$OUT"
set -euo pipefail

mock_lobby_sync_once() {
  local host_mock="$1"
  local device_mock="$2"
  local lobby="$3"
  local adb="${4:-adb}"
  local work="${5:-/tmp/mamehub-mock-sync-work}"

  local host_file="$host_mock/${lobby}.log"
  local device_path="$device_mock/${lobby}.log"
  local device_tmp="$work/device-${lobby}.log"
  local new_to_device="$work/new-to-device-${lobby}.log"

  mkdir -p "$host_mock" "$work"
  touch "$host_file"

  "$adb" pull "$device_path" "$device_tmp" >/dev/null 2>&1 || : >"$device_tmp"
  touch "$device_tmp"

  # Device → host: append missing lines
  while IFS= read -r line || [[ -n "${line:-}" ]]; do
    [[ -z "${line:-}" ]] && continue
    if ! grep -Fxq -- "$line" "$host_file" 2>/dev/null; then
      printf '%s\n' "$line" >>"$host_file"
    fi
  done <"$device_tmp"

  # Host → device: collect missing lines, append on device (no overwrite)
  : >"$new_to_device"
  while IFS= read -r line || [[ -n "${line:-}" ]]; do
    [[ -z "${line:-}" ]] && continue
    if ! grep -Fxq -- "$line" "$device_tmp" 2>/dev/null; then
      printf '%s\n' "$line" >>"$new_to_device"
    fi
  done <"$host_file"

  if [[ -s "$new_to_device" ]]; then
    # Ensure remote file exists, then append bytes
    "$adb" shell "mkdir -p '$device_mock'; touch '$device_path'" >/dev/null 2>&1 || true
    "$adb" shell "cat >> '$device_path'" <"$new_to_device" >/dev/null 2>&1 || true
  fi
}

# If executed directly, run forever (optional LOBBY / paths via env)
if [[ "${BASH_SOURCE[0]:-}" == "$0" ]]; then
  HOST_MOCK="${MAMEHUB_MOCK:-/tmp/mamehub_mock}"
  DEVICE_MOCK="${MAMEHUB_DEVICE_MOCK:-/storage/emulated/0/Android/data/org.mamedev.mame/files/mamehub_mock}"
  LOBBY="${MAMEHUB_LOBBY:?set MAMEHUB_LOBBY}"
  ADB="${ADB:-adb}"
  WORK="${MAMEHUB_E2E_OUT:-/tmp/mamehub-mock-sync-work}"
  while true; do
    mock_lobby_sync_once "$HOST_MOCK" "$DEVICE_MOCK" "$LOBBY" "$ADB" "$WORK" || true
    sleep "${MAMEHUB_MOCK_SYNC_INTERVAL:-0.25}"
  done
fi
