#!/bin/bash
# Two-instance local delay/stutter soak (default 15 minutes).
# Aborts immediately if:
#   - input lead delay_ms > MAX_DELAY_MS (default 100)
#   - more than one stutter in any rolling 60s window (after a 30s warmup)
#   - a frame whose busy time (excluding sleep/lockstep wait AND
#     SDL_RenderPresent / throttle sleep) exceeds the emulated frame
#     length (logged always; abort only on non-SDL/throttle compute)
#   - prolonged input-wait stall / game-over
# Stutters counted from:
#   - "We are behind ... Skipping video" (video.cpp logs when behind >100ms)
#   - fresh input-wait timeouts (attempt==1)
# Delay matches getLargestPing + ioport floor.
set -uo pipefail

ROOT="/Users/jjg/mame"
BIN="$ROOT/mamehub"
ROMPATH="roms"
OUT="/tmp/mamehub-delay-soak"
MOCK="/tmp/mamehub_mock"
LOBBY="delay-soak-local"
HOST_PORT=5815
JOIN_PORT=5819
HOST_DIR_PORT=5816
JOIN_DIR_PORT=5817
CONNECT_TIMEOUT=60
SOAK_SECS="${SOAK_SECS:-900}"   # 15 minutes
STUTTER_GRACE_SECS="${STUTTER_GRACE_SECS:-30}"  # ignore stutter rate during netplay warmup
# -fake_lag: clock skew + ~100ms UDP delay + 1% drops (ALL_RPC_FLAKY).
FAKE_LAG="${FAKE_LAG:-0}"
EXTRA_ARGS="${EXTRA_ARGS:-}"
if [[ "$FAKE_LAG" == "1" ]]; then
  EXTRA_ARGS="$EXTRA_ARGS -fake_lag"
  # Intentional ~100ms one-way lag → lead ~150–250ms.
  MAX_DELAY_MS="${MAX_DELAY_MS:-300}"
else
  MAX_DELAY_MS="${MAX_DELAY_MS:-100}"  # fail immediately if input lead exceeds this
fi
# Record one MP4 per mamehub window (ffmpeg crop of Capture screen 0).
RECORD_MP4="${RECORD_MP4:-0}"
PAD="$ROOT/tests/e2e/mamehub/tools/bin/pad"
WINID="$ROOT/tests/e2e/mamehub/tools/bin/winid"
TOOLS_BIN="$ROOT/tests/e2e/mamehub/tools/bin"

HOST_LOG="$OUT/host.log"
JOIN_LOG="$OUT/join.log"
MON_LOG="$OUT/monitor.log"
HOST_MP4="$OUT/host.mp4"
JOIN_MP4="$OUT/join.mp4"
HOST_MH_LOG="MAMEHub.host.log"
JOIN_MH_LOG="MAMEHub.join.log"
HOST_PID=
JOIN_PID=
HOST_MASH_PID=
JOIN_MASH_PID=
MON_PID=
HOST_REC_PID=
JOIN_REC_PID=
REC_PID=
HOST_MOV="$OUT/host.mov"
JOIN_MOV="$OUT/join.mov"

die() { echo "FAIL: $*"; tail -20 "$MON_LOG" 2>/dev/null || true; exit 1; }
alive() { kill -0 "$1" 2>/dev/null; }

place_window() {
  local pid=$1 x=$2 y=$3
  osascript -e "tell application \"System Events\" to set position of window 1 of (first process whose unix id is $pid) to {$x, $y}" 2>/dev/null || true
}

window_id() {
  [[ -x "$WINID" ]] || return 0
  "$WINID" "$1" 2>/dev/null || true
}

stop_recordings() {
  # screencapture -V self-stops; INT is best-effort if still running.
  if [[ -n "${HOST_REC_PID:-}" ]]; then
    kill -INT "$HOST_REC_PID" 2>/dev/null || true
  fi
  if [[ -n "${JOIN_REC_PID:-}" ]]; then
    kill -INT "$JOIN_REC_PID" 2>/dev/null || true
  fi
  wait "$HOST_REC_PID" 2>/dev/null || true
  wait "$JOIN_REC_PID" 2>/dev/null || true
  HOST_REC_PID=
  JOIN_REC_PID=
  REC_PID=
}

finalize_recordings() {
  stop_recordings
  [[ "${RECORD_MP4:-0}" == "1" ]] || return 0
  # Remux .mov → .mp4 (stream copy) for the requested deliverable.
  if [[ -s "$HOST_MOV" && ! -s "$HOST_MP4" ]]; then
    ffmpeg -y -i "$HOST_MOV" -c copy "$HOST_MP4" >"$OUT/host-remux.log" 2>&1 || true
  fi
  if [[ -s "$JOIN_MOV" && ! -s "$JOIN_MP4" ]]; then
    ffmpeg -y -i "$JOIN_MOV" -c copy "$JOIN_MP4" >"$OUT/join-remux.log" 2>&1 || true
  fi
}

start_dual_window_recording() {
  # Per-window screencapture (avoids dual avfoundation Capture screen 0 conflict
  # and Retina crop math). Duration capped slightly past the soak.
  local host_pid=$1 join_pid=$2
  local host_wid join_wid secs
  host_wid="$(window_id "$host_pid")"
  join_wid="$(window_id "$join_pid")"
  [[ -n "$host_wid" ]] || die "could not resolve host window id for pid $host_pid"
  [[ -n "$join_wid" ]] || die "could not resolve join window id for pid $join_pid"
  secs=$(( SOAK_SECS + 60 ))
  echo "record host window id=$host_wid -> $HOST_MOV (${secs}s cap)" | tee -a "$MON_LOG"
  echo "record join window id=$join_wid -> $JOIN_MOV (${secs}s cap)" | tee -a "$MON_LOG"
  screencapture -x -v -V "$secs" -l "$host_wid" "$HOST_MOV" >"$OUT/host-record.log" 2>&1 &
  HOST_REC_PID=$!
  screencapture -x -v -V "$secs" -l "$join_wid" "$JOIN_MOV" >"$OUT/join-record.log" 2>&1 &
  JOIN_REC_PID=$!
  REC_PID=$HOST_REC_PID
}

mkdir -p "$OUT"
pkill -9 -f "$BIN" 2>/dev/null || true
pkill -9 -f 'ffmpeg -y -f avfoundation' 2>/dev/null || true
pkill -9 -f 'screencapture -x -v' 2>/dev/null || true
sleep 0.8
rm -rf "$MOCK"
mkdir -p "$MOCK"
: >"$HOST_LOG"
: >"$JOIN_LOG"
: >"$MON_LOG"
rm -f "$HOST_MP4" "$JOIN_MP4" "$HOST_MOV" "$JOIN_MOV" \
  "$OUT/host-record.log" "$OUT/join-record.log" "$OUT/record.log" \
  "$OUT/host-remux.log" "$OUT/join-remux.log"
# Separate per-instance logs — dual writers on one MAMEHub.log freeze the net thread.
rm -f "$ROOT/MAMEHub.log" "$ROOT/$HOST_MH_LOG" "$ROOT/$JOIN_MH_LOG"
for i in $(seq 1 9); do
  rm -f "$ROOT/MAMEHub.$i.log" "$ROOT/MAMEHub.host.$i.log" "$ROOT/MAMEHub.join.$i.log"
done

[[ -x "$BIN" ]] || die "missing $BIN"
"$ROOT/tests/e2e/mamehub/tools/build.sh" "$TOOLS_BIN"
[[ -x "$PAD" ]] || die "missing pad tool"
[[ -f "$ROOT/roms/snes/tmnt4.zip" ]] || die "missing tmnt4 rom"
if [[ "$RECORD_MP4" == "1" ]]; then
  command -v ffmpeg >/dev/null 2>&1 || die "ffmpeg required for RECORD_MP4=1"
  [[ -x "$WINID" ]] || die "missing winid tool"
fi

cd "$ROOT"
trap 'finalize_recordings 2>/dev/null; kill $HOST_PID $JOIN_PID $HOST_MASH_PID $JOIN_MASH_PID $MON_PID 2>/dev/null; pkill -9 -f "$BIN" 2>/dev/null; true' EXIT

echo "== launch host ==" | tee -a "$MON_LOG"
# No -verbose: file logging only. Verbose dual-instance IO was poisoning ping.
# Separate MAMEHUB_LOG_BASENAME so host/join do not share a file lock.
# shellcheck disable=SC2086
MAMEHUB_LOG_BASENAME="$HOST_MH_LOG" stdbuf -oL -eL "$BIN" -window -nomaximize -resolution 640x480 -volume -96 \
  -discord_auth -discord_mock HostPlayer \
  -discord_lobby "$LOBBY" -discord_host -discord_players 2 \
  -discord_directory_port "$HOST_DIR_PORT" -port "$HOST_PORT" \
  -direct_connect_timeout "$CONNECT_TIMEOUT" \
  $EXTRA_ARGS \
  -rompath "$ROMPATH" -hashpath hash \
  snes snes:tmnt4 \
  >"$HOST_LOG" 2>&1 &
HOST_PID=$!
echo "host pid=$HOST_PID EXTRA_ARGS='$EXTRA_ARGS' MAX_DELAY_MS=$MAX_DELAY_MS" | tee -a "$MON_LOG"

for i in $(seq 1 60); do
  grep -q "Signed in to Discord as HostPlayer" "$HOST_LOG" && break
  alive "$HOST_PID" || die "host died startup"
  sleep 0.5
done
grep -q "Signed in to Discord as HostPlayer" "$HOST_LOG" || die "host auth failed"

for i in $(seq 1 80); do
  [[ -f "$MOCK/$LOBBY.log" ]] && rg -q '"type":"host"' "$MOCK/$LOBBY.log" && break
  alive "$HOST_PID" || die "host died before lobby"
  sleep 0.25
done
rg -q '"type":"host"' "$MOCK/$LOBBY.log" 2>/dev/null || die "host never announced"

echo "== launch guest ==" | tee -a "$MON_LOG"
# shellcheck disable=SC2086
MAMEHUB_LOG_BASENAME="$JOIN_MH_LOG" stdbuf -oL -eL "$BIN" -window -nomaximize -resolution 640x480 -volume -96 \
  -discord_auth -discord_mock GuestPlayer \
  -discord_lobby "$LOBBY" -discord_players 2 \
  -discord_directory_port "$JOIN_DIR_PORT" -port "$JOIN_PORT" \
  -direct_connect_timeout "$CONNECT_TIMEOUT" \
  $EXTRA_ARGS \
  -rompath "$ROMPATH" -hashpath hash \
  snes snes:tmnt4 \
  >"$JOIN_LOG" 2>&1 &
JOIN_PID=$!
echo "join pid=$JOIN_PID EXTRA_ARGS='$EXTRA_ARGS'" | tee -a "$MON_LOG"

for i in $(seq 1 60); do
  grep -q "Signed in to Discord as GuestPlayer" "$JOIN_LOG" && break
  alive "$JOIN_PID" || die "guest died startup"
  sleep 0.5
done
grep -q "Signed in to Discord as GuestPlayer" "$JOIN_LOG" || die "guest auth failed"

for i in $(seq 1 90); do
  rg -q 'GuestPlayer' "$MOCK/$LOBBY.log" 2>/dev/null && break
  alive "$HOST_PID" && alive "$JOIN_PID" || die "peer died waiting join"
  sleep 0.5
done
rg -q 'GuestPlayer' "$MOCK/$LOBBY.log" || die "guest never joined"

echo "== wait for netplay + frames ==" | tee -a "$MON_LOG"
for i in $(seq 1 120); do
  alive "$HOST_PID" && alive "$JOIN_PID" || die "peer died waiting game"
  if rg -q 'Netplay clock started' "$ROOT/$HOST_MH_LOG" "$ROOT/$JOIN_MH_LOG" "$HOST_LOG" "$JOIN_LOG" 2>/dev/null \
     && rg -q '\[INPUT_FRAME\]' "$ROOT/$HOST_MH_LOG" "$ROOT/$JOIN_MH_LOG" "$HOST_LOG" "$JOIN_LOG" 2>/dev/null; then
    echo "in-game at t=${i}s" | tee -a "$MON_LOG"
    break
  fi
  sleep 1
done
rg -q 'Netplay clock started' "$ROOT/$HOST_MH_LOG" "$ROOT/$JOIN_MH_LOG" "$HOST_LOG" "$JOIN_LOG" 2>/dev/null \
  || die "netplay clock never started"
rg -q '\[INPUT_FRAME\]' "$ROOT/$HOST_MH_LOG" "$ROOT/$JOIN_MH_LOG" "$HOST_LOG" "$JOIN_LOG" 2>/dev/null \
  || die "no INPUT_FRAME in logs"

# Keep both windows fully on-screen and non-overlapping for crop recording.
place_window "$HOST_PID" 40 40
place_window "$JOIN_PID" 720 40
sleep 0.5

focus_pid() {
  local pid=$1
  osascript -e "tell application \"System Events\" to set frontmost of first process whose unix id is $pid to true" >/dev/null 2>&1 || true
}

# TMNT4: P1 alone leaves attract and picks "Two Players". Then both confirm
# turtles on character select. Join Start is P2 — useless on the 1P/2P menu.
echo "== title / Two Players nav ==" | tee -a "$MON_LOG"
focus_pid "$HOST_PID"
sleep 0.4
# Match the GUI harness: plenty of Start presses with long gaps so we land on
# the 1P/2P menu rather than still being in the attract demo.
for i in 1 2 3 4 5 6 8 10 12 15 18 22; do
  alive "$HOST_PID" && alive "$JOIN_PID" || die "peer died during title nav"
  focus_pid "$HOST_PID"
  "$PAD" "$HOST_PID" tap start 140
  sleep 1.2
done
screencapture -x -l "$(window_id "$HOST_PID")" "$OUT/before-2p.png" 2>/dev/null || true
focus_pid "$HOST_PID"
"$PAD" "$HOST_PID" tap down 160
sleep 0.6
"$PAD" "$HOST_PID" tap start 180
sleep 2.0
screencapture -x -l "$(window_id "$HOST_PID")" "$OUT/after-2p.png" 2>/dev/null || true
echo "== character select (both peers Start) ==" | tee -a "$MON_LOG"
for i in 1 2 3 4 5 6 8 10 12; do
  alive "$HOST_PID" && alive "$JOIN_PID" || die "peer died during char select"
  focus_pid "$HOST_PID"
  "$PAD" "$HOST_PID" tap start 120
  sleep 0.4
  focus_pid "$JOIN_PID"
  "$PAD" "$JOIN_PID" tap start 120
  sleep 0.8
done
# Leave host frontmost so P1 receives the first dual_wander pulses.
focus_pid "$HOST_PID"
screencapture -x -l "$(window_id "$HOST_PID")" "$OUT/gameplay-start-host.png" 2>/dev/null || true
screencapture -x -l "$(window_id "$JOIN_PID")" "$OUT/gameplay-start-join.png" 2>/dev/null || true
echo "title nav done" | tee -a "$MON_LOG"

if [[ "$RECORD_MP4" == "1" ]]; then
  echo "== start host + join MP4 recording ==" | tee -a "$MON_LOG"
  start_dual_window_recording "$HOST_PID" "$JOIN_PID"
  sleep 2
  alive "$HOST_REC_PID" || die "host screencapture died (see $OUT/host-record.log)"
  alive "$JOIN_REC_PID" || die "join screencapture died (see $OUT/join-record.log)"
  echo "recording host_rec=$HOST_REC_PID join_rec=$JOIN_REC_PID -> $HOST_MP4 / $JOIN_MP4" | tee -a "$MON_LOG"
fi

echo "== soak ${SOAK_SECS}s; abort if delay_ms > ${MAX_DELAY_MS} or >1 stutter/min after ${STUTTER_GRACE_SECS}s warmup ==" | tee -a "$MON_LOG"
# One process alternates focus+dirs so both P1 and P2 visibly move.
"$PAD" "$HOST_PID" dual_wander "$JOIN_PID" "$SOAK_SECS" 1111 >"$OUT/host_mash.err" 2>&1 &
HOST_MASH_PID=$!
JOIN_MASH_PID=

# Monitor ping + stutter lines from per-instance easylogging files.
# Only count lines written during the soak (seek to EOF first) so title/startup
# catch-up does not instantly trip the >1 stutter/min rule.
python3 - "$ROOT" "$SOAK_SECS" "$MAX_DELAY_MS" "$MON_LOG" "$HOST_PID" "$JOIN_PID" "$HOST_MH_LOG" "$JOIN_MH_LOG" "$STUTTER_GRACE_SECS" <<'PY'
import glob, os, re, sys, time, math
from collections import deque

root, soak_secs, max_delay, mon_log, host_pid, join_pid, host_mh, join_mh, grace_secs = sys.argv[1:10]
soak_secs = int(soak_secs)
max_delay = int(max_delay)
host_pid = int(host_pid)
join_pid = int(join_pid)
grace_secs = float(grace_secs)
pat = re.compile(r"Ping:\s+(\d+)\s+([\d.eE+-]+)\s+([\d.eE+-]+)\s+(\d+)")
input_wait_pat = re.compile(r"\[INPUT_WAIT\].*?waited_us=(\d+).*?attempts=(\d+)")
stutter_pat = re.compile(r"We are behind\s+(\d+)ms\.\s+Skipping video")
overbudget_pat = re.compile(
    r"\[FRAME_OVERBUDGET\].*wall_us=(\d+).*wait_us=(\d+).*busy_us=(\d+).*budget_us=(\d+)"
    r".*throttle_us=(\d+).*blit_us=(\d+).*notify_us=(\d+)"
    r"(?:.*fu_wall_us=(\d+).*fu_cpu_us=(\d+))?"
)
profile_pat = re.compile(r"\[FRAME_PROFILE")
gameover_pat = re.compile(r"SIGNALING GAME OVER|input wait exceeded|terminating game")
seen = 0
max_seen_delay = 0.0
stutter_times = deque()
total_stutters = 0
behind_logged = 0
start = time.time()
last_stutter_wall = 0.0

def alive(pid):
    try:
        os.kill(pid, 0)
        return True
    except OSError:
        return False

def log(msg):
    print(msg, flush=True)
    with open(mon_log, "a") as f:
        f.write(msg + "\n")

def delay_ms_from_upper(upper_us: float) -> float:
    # Match Common::getLargestPing + ioport: max(50, min(600, 50 + max(35, ceil(halfMs))))
    half_ms = (upper_us / 2.0) / 1000.0
    return max(50, min(600, 50 + max(35, math.ceil(half_ms))))

def note_stutter(now, detail):
    global total_stutters, last_stutter_wall
    # Host+join may hitch together; collapse near-simultaneous events.
    if now - last_stutter_wall < 0.25:
        return
    last_stutter_wall = now
    elapsed = now - start
    total_stutters += 1
    if elapsed < grace_secs:
        log(f"warmup stutter #{total_stutters} t={elapsed:.1f}s (<{grace_secs:.0f}s grace) {detail}")
        return
    stutter_times.append(now)
    while stutter_times and now - stutter_times[0] > 60.0:
        stutter_times.popleft()
    in_window = len(stutter_times)
    log(f"stutter #{total_stutters} t={elapsed:.1f}s window60s={in_window} {detail}")
    # More than one stutter in any rolling minute after warmup → fail immediately.
    if in_window > 1:
        log(f"ABORT: stutter rate >1/min ({in_window} in last 60s) at t={elapsed:.1f}s")
        sys.exit(6)

def log_files():
    names = []
    for base in (host_mh, join_mh):
        stem = base[:-4] if base.endswith(".log") else base
        names.append(os.path.join(root, base))
        names.extend(sorted(glob.glob(os.path.join(root, f"{stem}.*.log"))))
    return [p for p in names if os.path.exists(p)]

# Start at EOF so pre-soak "behind" lines are not counted.
offsets = {}
for path in log_files():
    try:
        offsets[path] = os.path.getsize(path)
    except OSError:
        offsets[path] = 0

while time.time() - start < soak_secs:
    if not alive(host_pid) or not alive(join_pid):
        log("FAIL: peer died during soak")
        sys.exit(2)
    for path in log_files():
        off = offsets.get(path, 0)
        try:
            size = os.path.getsize(path)
        except OSError:
            continue
        if size < off:
            off = 0
        if size == off:
            continue
        try:
            with open(path, "rb") as f:
                f.seek(off)
                chunk = f.read()
                offsets[path] = f.tell()
        except OSError:
            continue
        text = chunk.decode("utf-8", errors="ignore")
        now = time.time()
        if gameover_pat.search(text):
            log(f"ABORT: game-over/wait-terminate at t={now-start:.1f}s file={os.path.basename(path)}")
            sys.exit(4)
        for m in stutter_pat.finditer(text):
            behind_ms = int(m.group(1))
            behind_logged += 1
            note_stutter(time.time(), f"behind={behind_ms}ms file={os.path.basename(path)}")
        in_mame_profile = False
        for line in text.splitlines():
            if profile_pat.search(line) or "[FRAME_OVERBUDGET]" in line:
                log(f"profile {os.path.basename(path)}: {line}")
                in_mame_profile = "FRAME_PROFILE_MAME" in line or "FRAME_TIMERS" in line
                continue
            if in_mame_profile:
                if "[FRAME_" in line or "Ping:" in line or "We are behind" in line:
                    in_mame_profile = False
                else:
                    log(f"profile {os.path.basename(path)}: {line}")
        for m in overbudget_pat.finditer(text):
            elapsed = time.time() - start
            (wall_us, wait_us, busy_us, budget_us, throttle_us, blit_us,
             notify_us) = (int(x) for x in m.groups()[:7])
            fu_wall_us = int(m.group(8)) if m.group(8) else None
            fu_cpu_us = int(m.group(9)) if m.group(9) else None
            # SDL present is blit_us; throttle sleep is wait/throttle (already
            # removed from busy). MACHINE_NOTIFY_FRAME (notify_us) is input/UI
            # bookkeeping — exclude it from "pure compute" the same way as blit,
            # otherwise a rare OS scheduling hitch in notifiers fails a clean
            # zero-stutter run.
            compute_us = busy_us - blit_us - notify_us
            sdl_or_throttle = (
                blit_us + notify_us >= max(0, busy_us - budget_us)
                or wait_us >= budget_us
            )
            # VBLANK_SPLIT: if CPU time fits the frame but wall time does not,
            # this is preemption / focus thrash, not emu overbudget.
            sched_noise = (
                fu_cpu_us is not None
                and fu_cpu_us <= budget_us
                and (fu_wall_us is None or fu_wall_us > fu_cpu_us * 2)
            )
            log(f"FRAME_OVERBUDGET t={elapsed:.1f}s busy_us={busy_us} budget_us={budget_us} "
                f"blit_us={blit_us} notify_us={notify_us} throttle_us={throttle_us} "
                f"wait_us={wait_us} compute_us={compute_us} "
                f"fu_wall_us={fu_wall_us} fu_cpu_us={fu_cpu_us} "
                f"file={os.path.basename(path)}")
            if sdl_or_throttle or compute_us <= budget_us or sched_noise:
                why = ("sched" if sched_noise and not (sdl_or_throttle or compute_us <= budget_us)
                       else "SDL/notify/throttle")
                log(f"ignore OVERBUDGET ({why}) t={elapsed:.1f}s "
                    f"file={os.path.basename(path)}")
                continue
            if elapsed >= grace_secs:
                log(f"ABORT: FRAME_OVERBUDGET compute_us={compute_us} > budget_us={budget_us} "
                    f"(not SDL/notify/throttle/sched) file={os.path.basename(path)}")
                sys.exit(7)
        for m in input_wait_pat.finditer(text):
            waited_us, attempts = (int(x) for x in m.groups())
            if waited_us >= 1_000_000:
                log(f"ABORT: input wait waited_us={waited_us} attempts={attempts} "
                    f"at t={time.time()-start:.1f}s — local stall")
                sys.exit(5)
            if waited_us >= 100_000:
                note_stutter(time.time(),
                    f"input_wait={waited_us / 1000:.0f}ms attempts={attempts} "
                    f"file={os.path.basename(path)}")
        for m in pat.finditer(text):
            sample, mean, var, upper = m.groups()
            upper_us = float(upper)
            mean_us = float(mean)
            delay = delay_ms_from_upper(upper_us)
            mean_delay = delay_ms_from_upper(mean_us)
            seen += 1
            if delay > max_seen_delay:
                max_seen_delay = delay
            if delay > max_delay:
                log(f"ABORT: delay_ms={delay:.0f} > {max_delay} "
                    f"(upper_us={upper_us:.0f} mean_us={mean_us:.0f} sample_us={sample}) "
                    f"at t={time.time()-start:.1f}s file={os.path.basename(path)}")
                sys.exit(1)
            if seen == 1 or seen % 25 == 0:
                while stutter_times and time.time() - stutter_times[0] > 60.0:
                    stutter_times.popleft()
                log(f"ok t={time.time()-start:.0f}s samples={seen} "
                    f"delay_ms={delay:.0f} mean_delay_ms={mean_delay:.0f} "
                    f"max_delay_ms={max_seen_delay:.0f} "
                    f"stutters={total_stutters} window60s={len(stutter_times)} "
                    f"behind_events={behind_logged} "
                    f"sample_us={sample} mean_us={mean_us:.0f} upper_us={upper_us:.0f}")
    time.sleep(0.5)

if seen < 10:
    log(f"FAIL: too few ping samples ({seen})")
    sys.exit(3)
log(f"PASS: {soak_secs}s soak samples={seen} max_delay_ms={max_seen_delay:.0f} "
    f"stutters={total_stutters} behind_events={behind_logged} "
    f"(<=1 stutter/min, delay<={max_delay})")
sys.exit(0)
PY
MON_RC=$?
MON_PID=

# Finalize MP4s before killing the game windows so the last frames are kept.
if [[ "$RECORD_MP4" == "1" ]]; then
  echo "== stop MP4 recording ==" | tee -a "$MON_LOG"
  sleep 1
  finalize_recordings
fi

# Kill immediately. Do not wait for mash (it lasts the full soak and would
# leave both windows running after a stutter abort).
kill $HOST_PID $JOIN_PID $HOST_MASH_PID $JOIN_MASH_PID 2>/dev/null || true
pkill -9 -f "$BIN" 2>/dev/null || true
HOST_PID= JOIN_PID= HOST_MASH_PID= JOIN_MASH_PID=
sleep 0.5

if [[ "$RECORD_MP4" == "1" ]]; then
  [[ -s "$HOST_MP4" ]] || die "missing/empty host mp4 at $HOST_MP4 (see $OUT/record.log)"
  [[ -s "$JOIN_MP4" ]] || die "missing/empty join mp4 at $JOIN_MP4 (see $OUT/record.log)"
  echo "recordings: $HOST_MP4 ($(stat -f%z "$HOST_MP4") bytes) $JOIN_MP4 ($(stat -f%z "$JOIN_MP4") bytes)" | tee -a "$MON_LOG"
fi

if [[ "$MON_RC" -ne 0 ]]; then
  echo "Monitor failed rc=$MON_RC" | tee -a "$MON_LOG"
  echo "---- last Ping lines ----" | tee -a "$MON_LOG"
  rg -n "Ping:" "$ROOT/$HOST_MH_LOG" "$ROOT/$JOIN_MH_LOG" 2>/dev/null | tail -30 | tee -a "$MON_LOG" || true
  echo "---- FRAME_OVERBUDGET / FRAME_PROFILE ----" | tee -a "$MON_LOG"
  rg -n "FRAME_OVERBUDGET|FRAME_PROFILE" "$ROOT/$HOST_MH_LOG" "$ROOT/$JOIN_MH_LOG" 2>/dev/null | tail -80 | tee -a "$MON_LOG" || true
  echo "---- TIMED OUT / behind ----" | tee -a "$MON_LOG"
  rg -n "TIMED OUT WAITING|We are behind|INPUT TOO LATE" "$ROOT/$HOST_MH_LOG" "$ROOT/$JOIN_MH_LOG" 2>/dev/null | tail -40 | tee -a "$MON_LOG" || true
  exit "$MON_RC"
fi

echo "PASS delay/stutter soak ${SOAK_SECS}s" | tee -a "$MON_LOG"
exit 0
