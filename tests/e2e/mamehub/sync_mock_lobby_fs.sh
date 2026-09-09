#!/bin/bash
# Append-only bidirectional sync for Discord mock lobby logs between two
# local directories (macOS host ↔ iOS Simulator sandbox). Same semantics as
# sync_mock_lobby.sh but without adb.
#
#   source sync_mock_lobby_fs.sh
#   mock_lobby_sync_fs_once "$HOST_MOCK" "$GUEST_MOCK" "$LOBBY"
set -euo pipefail

mock_lobby_sync_fs_once() {
  local host_mock="$1"
  local guest_mock="$2"
  local lobby="$3"

  local host_file="$host_mock/${lobby}.log"
  local guest_file="$guest_mock/${lobby}.log"

  mkdir -p "$host_mock" "$guest_mock"
  touch "$host_file" "$guest_file"

  # Guest → host
  while IFS= read -r line || [[ -n "${line:-}" ]]; do
    [[ -z "${line:-}" ]] && continue
    if ! grep -Fxq -- "$line" "$host_file" 2>/dev/null; then
      printf '%s\n' "$line" >>"$host_file"
    fi
  done <"$guest_file"

  # Host → guest
  while IFS= read -r line || [[ -n "${line:-}" ]]; do
    [[ -z "${line:-}" ]] && continue
    if ! grep -Fxq -- "$line" "$guest_file" 2>/dev/null; then
      printf '%s\n' "$line" >>"$guest_file"
    fi
  done <"$host_file"
}
