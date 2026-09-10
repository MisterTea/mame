#!/bin/bash
# N-player arcade netplay e2e (macOS).
#
# Usage:
#   PLAYERS=2 GAME=xmen2pe ./tests/e2e/mamehub/run_arcade_nplayer.sh
#   PLAYERS=6 GAME=xmen6p  ./tests/e2e/mamehub/run_arcade_nplayer.sh
#
# Flow:
#   1) Launch N mamehub instances (unique peer + directory ports)
#   2) Host: Host Game -> type GAME -> lobby
#   3) Guests: Join Game -> select hosted lobby (sequentially)
#   4) Host Start Game; wait for boot / netplay clock
#   5) Every player: insert coins + press Start (no in-game "2 players" menu)
#   6) All players mash arcade pad; assert no INPUT DESYNC
#
# Env: MAMEHUB_BIN, MAMEHUB_ROMPATH, MAMEHUB_E2E_OUT, MASH_SECS, SKIP_MASH=1
set -uo pipefail

E2E_SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
# shellcheck source=common.sh
source "$E2E_SCRIPT_DIR/common.sh"
e2e_init_paths
e2e_require_macos

PLAYERS="${PLAYERS:?set PLAYERS=2 or PLAYERS=6}"
GAME="${GAME:?set GAME=xmen2pe or GAME=xmen6p}"
MASH_SECS="${MASH_SECS:-180}"

if ! [[ "$PLAYERS" =~ ^[0-9]+$ ]] || (( PLAYERS < 2 || PLAYERS > 10 )); then
  echo "FAIL: PLAYERS must be 2..10 (got $PLAYERS)"
  exit 1
fi

e2e_require_rom "$GAME"
e2e_cleanup

declare -a PIDS=()
declare -a LOGS=()
declare -a NAMES=()

launch_peer() {
  local idx=$1
  local name=$2
  local peer dir log
  peer="$(e2e_peer_port "$idx")"
  dir="$(e2e_dir_port "$idx")"
  log="$OUT/p${idx}-${name}.log"
  LOGS[$idx]="$log"
  NAMES[$idx]="$name"
  : >"$log"
  echo "launch idx=$idx name=$name peer=$peer dir=$dir"
  # Launch mamehub directly (no subshell) so $! is the process that owns the window.
  # Line-buffer logs so candy/peer progress is visible while waiting.
  cd "$ROOT"
  if command -v stdbuf >/dev/null 2>&1; then
    stdbuf -oL -eL "$BIN" -window -nomaximize -resolution 960x720 \
      -candy \
      -discord_auth -discord_mock "$name" \
      -discord_directory_port "$dir" -port "$peer" \
      -direct_connect_timeout 60 -rompath "$ROMPATH" \
      >"$log" 2>&1 &
  else
    "$BIN" -window -nomaximize -resolution 960x720 \
      -candy \
      -discord_auth -discord_mock "$name" \
      -discord_directory_port "$dir" -port "$peer" \
      -direct_connect_timeout 60 -rompath "$ROMPATH" \
      >"$log" 2>&1 &
  fi
  PIDS[$idx]=$!
  echo "  pid=${PIDS[$idx]}"
}

wait_signed_in() {
  local idx=$1
  local name=${NAMES[$idx]}
  local log=${LOGS[$idx]}
  local pid=${PIDS[$idx]}
  local i
  for i in $(seq 1 90); do
    grep -q "Signed in to Discord as $name" "$log" && return 0
    e2e_alive "$pid" || e2e_die "peer $idx ($name) died during sign-in"
    sleep 0.5
  done
  e2e_die "peer $idx ($name) never signed in"
}

park_all_except() {
  local keep=$1
  local i
  for i in $(seq 0 $((PLAYERS - 1))); do
    if [[ "$i" -eq "$keep" ]]; then
      e2e_place "${PIDS[$i]}" 80 40
    else
      e2e_place "${PIDS[$i]}" -1400 $((40 + i * 20))
    fi
  done
  sleep 0.35
}

echo "== launching $PLAYERS peers for $GAME =="
launch_peer 0 HostPlayer
for i in $(seq 1 $((PLAYERS - 1))); do
  launch_peer "$i" "Guest${i}"
done

for i in $(seq 0 $((PLAYERS - 1))); do
  wait_signed_in "$i"
done
sleep 2
park_all_except 0
e2e_shot host-main

HOST_PID=${PIDS[0]}
HOST_LOG=${LOGS[0]}

echo "== host: Host Game -> Arcade -> $GAME =="
# Candy saves downloads under rompath; only the first fetch is slow.
ANNOUNCE_SECS="${ANNOUNCE_SECS:-45}"
HOSTED=0
for attempt in 1 2 3 4 5 6; do
  echo "host selection attempt $attempt"
  park_all_except 0
  e2e_alive "$HOST_PID" || e2e_die "host died before selection attempt $attempt"
  # Back out of any wrong softlist/lobby from a prior attempt
  if [[ -f "$MOCK/mamehub-directory-v1.log" ]] && rg -q '"open":true' "$MOCK/mamehub-directory-v1.log" \
     && ! rg -q "\"game\":\"$GAME\"" "$MOCK/mamehub-directory-v1.log"; then
    echo "closing wrong lobby before retry"
    for _ in 1 2 3 4; do
      "$FK" "$HOST_PID" escape 1 || true
      sleep 0.35
    done
  fi
  # Host Game path (postToPid via key tool).
  KEY="$TOOLS_BIN/key"
  "$CH" "$HOST_PID" 0 0 || true
  sleep 0.35
  /usr/bin/python3 - <<'PY' 2>/dev/null || true
import Quartz
Quartz.CGEventPost(Quartz.kCGHIDEventTap, Quartz.CGEventCreateMouseEvent(None, Quartz.kCGEventMouseMoved, (5, 5), Quartz.kCGMouseButtonLeft))
PY
  "$KEY" "$HOST_PID" enter
  sleep 2.0
  "$KEY" "$HOST_PID" enter
  sleep 2.5
  e2e_shot "host-after-hostgame-$attempt"
  # On Select Machine: clear search so Arcade is SELECT_FIRST, then Enter.
  # (Typing "arcade" then failing Enter leaves search glued for the next type.)
  "$KEY" "$HOST_PID" escape
  sleep 0.5
  e2e_shot "host-machine-arcade-$attempt"
  "$KEY" "$HOST_PID" enter
  sleep 3.0
  e2e_shot "host-arcade-list-$attempt"
  "$KEY" "$HOST_PID" type "$GAME"
  sleep 3.5
  e2e_shot "host-search-$attempt"
  "$KEY" "$HOST_PID" enter
  echo "waiting up to ${ANNOUNCE_SECS}s for lobby announce..."
  for w in $(seq 1 "$ANNOUNCE_SECS"); do
    if [[ -f "$MOCK/mamehub-directory-v1.log" ]] && rg -q "\"game\":\"$GAME\"" "$MOCK/mamehub-directory-v1.log"; then
      echo "host lobby announced on attempt $attempt after ${w}s"
      HOSTED=1
      break
    fi
    if [[ -f "$MOCK/mamehub-directory-v1.log" ]] && rg -q '"open":true' "$MOCK/mamehub-directory-v1.log" \
       && ! rg -q "\"game\":\"$GAME\"" "$MOCK/mamehub-directory-v1.log"; then
      echo "wrong lobby announced; closing and retrying"
      break
    fi
    e2e_alive "$HOST_PID" || e2e_die "host died while waiting for $GAME announce"
    sleep 1
  done
  e2e_shot "host-lobby-$attempt"
  if [[ "$HOSTED" -eq 1 ]]; then
    break
  fi
  if [[ -f "$MOCK/mamehub-directory-v1.log" ]]; then
    echo "announce so far:"
    tail -5 "$MOCK/mamehub-directory-v1.log" || true
  fi
  # Do not Escape if peer already started — that kills the directory mid-poll (FATAL).
  if rg -q "STARTED SERVER ON PORT" "${LOGS[0]}" 2>/dev/null; then
    e2e_die "peer started but lobby never announced $GAME (see host log)"
  fi
  for _ in 1 2 3 4; do
    "$FK" "$HOST_PID" escape 1 || true
    sleep 0.35
  done
  sleep 1
done
[[ "$HOSTED" -eq 1 ]] || e2e_die "host never announced $GAME lobby"
cat "$MOCK/mamehub-directory-v1.log"

join_one_guest() {
  local idx=$1
  local pid=${PIDS[$idx]}
  local joined=0
  local attempt
  local KEY="$TOOLS_BIN/key"
  local base_y new_y queries_before queries_after
  echo "== guest idx=$idx (${NAMES[$idx]}): Join Game =="
  park_all_except "$idx"
  for attempt in 1 2 3 4 5; do
    echo "join attempt $attempt for idx=$idx"
    if [[ "$attempt" -gt 1 ]]; then
      "$KEY" "$pid" escape || true
      sleep 0.4
      "$KEY" "$pid" escape || true
      sleep 0.5
    fi
    park_all_except "$idx"
    e2e_shot "join${idx}-main-$attempt"
    # Keyboard join: content focus, park mouse, Down until highlight moves, Enter.
    "$CH" "$pid" 0 0 || true
    sleep 0.35
    /usr/bin/python3 - <<'PY' 2>/dev/null || true
import Quartz
Quartz.CGEventPost(Quartz.kCGHIDEventTap, Quartz.CGEventCreateMouseEvent(None, Quartz.kCGEventMouseMoved, (5, 5), Quartz.kCGMouseButtonLeft))
PY
    base_y=$("$CH" "$pid" probe 2>/dev/null || echo 0)
    "$KEY" "$pid" down 1
    sleep 0.55
    new_y=$("$CH" "$pid" probe 2>/dev/null || echo 0)
    if [[ "${new_y:-0}" -le $((base_y + 8)) ]]; then
      echo "Down did not move highlight (y ${base_y}->${new_y}); retry Down"
      "$KEY" "$pid" down 1
      sleep 0.55
      new_y=$("$CH" "$pid" probe 2>/dev/null || echo 0)
    fi
    echo "highlight y ${base_y}->${new_y}"
    e2e_shot "join${idx}-highlight-$attempt"
    if [[ "${new_y:-0}" -le $((base_y + 8)) ]]; then
      echo "still on Host Game; skip Enter to avoid hosting"
      continue
    fi
    queries_before=$(rg -c '"type":"query"' "$MOCK"/mamehub-*.log 2>/dev/null | awk -F: '{s+=$NF} END{print s+0}')
    "$KEY" "$pid" enter
    sleep 2.5
    e2e_shot "join${idx}-list-$attempt"
    if rg -q "\"host_name\":\"${NAMES[$idx]}\"" "$MOCK/mamehub-directory-v1.log" 2>/dev/null; then
      e2e_die "guest ${NAMES[$idx]} accidentally hosted"
    fi
    queries_after=$(rg -c '"type":"query"' "$MOCK"/mamehub-*.log 2>/dev/null | awk -F: '{s+=$NF} END{print s+0}')
    # Join Game list issues directory queries; Host->Select Machine does not.
    if [[ "${queries_after:-0}" -le "${queries_before:-0}" ]]; then
      echo "no directory query after Enter (likely Host/Select Machine); backing out"
      "$KEY" "$pid" escape || true
      sleep 0.4
      "$KEY" "$pid" escape || true
      sleep 0.4
      continue
    fi
    # Select the hosted lobby (first/only entry)
    "$KEY" "$pid" enter
    sleep 3.5
    e2e_shot "join${idx}-lobby-$attempt"
    if rg -q "\"name\":\"${NAMES[$idx]}\"" "$MOCK"/mamehub-*.log 2>/dev/null; then
      joined=1
      echo "guest ${NAMES[$idx]} joined on attempt $attempt"
      break
    fi
    e2e_alive "$HOST_PID" || e2e_die "host died while guest $idx joining"
    e2e_alive "$pid" || e2e_die "guest $idx died while joining"
  done
  [[ "$joined" -eq 1 ]] || e2e_die "guest ${NAMES[$idx]} never joined"
}

for i in $(seq 1 $((PLAYERS - 1))); do
  join_one_guest "$i"
done

echo "== wait for $PLAYERS players in announce =="
for i in $(seq 1 60); do
  if rg -q "\"players\":$PLAYERS" "$MOCK/mamehub-directory-v1.log" 2>/dev/null; then
    echo "announce shows players=$PLAYERS at t=$i"
    break
  fi
  sleep 1
done
cat "$MOCK/mamehub-directory-v1.log" | tail -20

echo "== wait for all peer endpoints =="
READY=0
for i in $(seq 1 120); do
  local_ok=1
  for j in $(seq 0 $((PLAYERS - 1))); do
    e2e_alive "${PIDS[$j]}" || e2e_die "peer $j died waiting endpoints"
    port="$(e2e_peer_port "$j")"
    if ! rg -q "127.0.0.1:$port" "$MOCK"/mamehub-*.log 2>/dev/null; then
      local_ok=0
    fi
  done
  if [[ "$local_ok" -eq 1 ]]; then
    READY=1
    echo "all peer endpoints ready at t=$i"
    break
  fi
  sleep 1
done
[[ "$READY" -eq 1 ]] || echo "WARN: not all peer ports seen yet; attempting start anyway"

echo "== host: Start Game =="
park_all_except 0
STARTED=0
for attempt in 1 2 3 4 5 6 8 10; do
  echo "start game attempt $attempt"
  "$CH" "$HOST_PID" 0 0 || true
  sleep 0.25
  "$FK" "$HOST_PID" raw_enter 1
  sleep 0.4
  "$PAD" "$HOST_PID" tap enter 100
  sleep 1.5
  e2e_shot "host-start-attempt-$attempt"
  if rg -q '"type":"start"' "$MOCK"/mamehub-*.log 2>/dev/null; then
    STARTED=1
    echo "start message published on attempt $attempt"
    break
  fi
  e2e_select_highlight "$HOST_PID" 0 || true
  sleep 2
  if rg -q '"type":"start"' "$MOCK"/mamehub-*.log 2>/dev/null; then
    STARTED=1
    echo "start via highlight on attempt $attempt"
    break
  fi
done
[[ "$STARTED" -eq 1 ]] || e2e_die "host never published start"

echo "== wait for mesh + launch =="
SUCCESS=0
for i in $(seq 1 180); do
  for j in $(seq 0 $((PLAYERS - 1))); do
    e2e_alive "${PIDS[$j]}" || e2e_die "peer $j DIED after start t=$i"
  done
  if rg -qi 'Fatal log|Aborting application|INPUT DESYNC' "$OUT"/*.log "$ROOT/MAMEHub.log" 2>/dev/null; then
    e2e_die "FATAL after start"
  fi
  if rg -q '"ready":true' "$ROOT/MAMEHub.log" 2>/dev/null \
     || rg -q 'Netplay clock started' "$HOST_LOG" 2>/dev/null; then
    SUCCESS=1
    echo "launch progress at t=$i"
    break
  fi
  sleep 1
done
[[ "$SUCCESS" -eq 1 ]] || e2e_die "game never launched after start"

if [[ "${SKIP_MASH:-0}" == "1" ]]; then
  echo "TEST PASSED ($GAME ${PLAYERS}p launch only; SKIP_MASH=1)"
  exit 0
fi

echo "== wait for boot / in-game frames =="
for i in $(seq 1 120); do
  e2e_check_desync
  frames_ok=1
  for j in $(seq 0 $((PLAYERS - 1))); do
    e2e_alive "${PIDS[$j]}" || e2e_die "peer $j died waiting boot"
    if ! rg -q '\[INPUT_FRAME\]' "${LOGS[$j]}"; then
      frames_ok=0
    fi
  done
  if rg -q 'Netplay clock started' "$HOST_LOG" && [[ "$frames_ok" -eq 1 ]]; then
    echo "all peers producing frames at t=$i"
    break
  fi
  sleep 1
done
# Extra settle for arcade attract / EEPROM
sleep 8
e2e_shot game-boot

echo "== all players: coin + start =="
# Bring windows on-screen in a grid for debugging; inputs use postToPid
for j in $(seq 0 $((PLAYERS - 1))); do
  e2e_place "${PIDS[$j]}" $((40 + (j % 3) * 320)) $((40 + (j / 3) * 280))
done
sleep 1
for round in 1 2 3 4; do
  for j in $(seq 0 $((PLAYERS - 1))); do
    "$PAD" "${PIDS[$j]}" tap coin 120
    sleep 0.15
  done
  sleep 0.4
done
e2e_shot after-coins
for round in 1 2 3 4 5 6; do
  for j in $(seq 0 $((PLAYERS - 1))); do
    "$PAD" "${PIDS[$j]}" tap start 120
    sleep 0.12
  done
  sleep 0.6
  e2e_shot "after-start-$round"
  e2e_check_desync
done

echo "== all players random arcade mash for ${MASH_SECS}s =="
declare -a MASH_PIDS=()
for j in $(seq 0 $((PLAYERS - 1))); do
  : >"$OUT/p${j}_mash.err"
  "$PAD" "${PIDS[$j]}" mash_arcade "$MASH_SECS" $((1000 + j * 111)) \
    >"$OUT/p${j}_mash.err" 2>&1 &
  MASH_PIDS[$j]=$!
done
echo "mash pids: ${MASH_PIDS[*]}"

SYNC_OK=1
for i in $(seq 1 "$MASH_SECS"); do
  for j in $(seq 0 $((PLAYERS - 1))); do
    if ! e2e_alive "${PIDS[$j]}"; then
      SYNC_OK=0
      echo "peer $j DIED during mash t=$i"
      break 2
    fi
  done
  if rg -qi 'INPUT DESYNC|Fatal log|Aborting application' "$OUT"/*.log "$ROOT/MAMEHub.log" 2>/dev/null; then
    SYNC_OK=0
    echo "DESYNC/FATAL at mash t=$i"
    break
  fi
  if (( i % 30 == 0 )); then
    echo "mash progress t=${i}s"
    e2e_shot "mash-$i"
    for j in $(seq 0 $((PLAYERS - 1))); do
      fc=$(rg -c '\[INPUT_FRAME\]' "${LOGS[$j]}" 2>/dev/null || echo 0)
      echo "  frames p$j=$fc"
    done
  fi
  all_done=1
  for j in $(seq 0 $((PLAYERS - 1))); do
    kill -0 "${MASH_PIDS[$j]}" 2>/dev/null && all_done=0
  done
  if [[ "$all_done" -eq 1 ]]; then
    echo "mashers exited at t=$i"
    break
  fi
  sleep 1
done

for j in $(seq 0 $((PLAYERS - 1))); do
  wait "${MASH_PIDS[$j]}" 2>/dev/null || true
done
e2e_shot mash-final
e2e_check_desync

echo "== last INPUT_FRAME lines =="
for j in $(seq 0 $((PLAYERS - 1))); do
  echo "-- p$j --"
  rg '\[INPUT_FRAME\]' "${LOGS[$j]}" | tail -3
done

if [[ "$SYNC_OK" -eq 1 ]]; then
  for j in $(seq 0 $((PLAYERS - 1))); do
    e2e_alive "${PIDS[$j]}" || e2e_die "peer $j dead at end"
  done
  if ! rg -qi 'INPUT DESYNC' "$OUT"/*.log "$ROOT/MAMEHub.log" 2>/dev/null; then
    echo "TEST PASSED ($GAME ${PLAYERS}p synced through ${MASH_SECS}s mash)"
    exit 0
  fi
fi
e2e_die "$GAME ${PLAYERS}p gameplay sync test failed"
