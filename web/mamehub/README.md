# MAMEHub Browser (self-host)

Static WASM shell coordinated over **Nostr** (no central MAMEHub backend, no Discord Social SDK).

## Profiles

Sites are generated from `web/mamehub/profiles.json` (arcade + the popular consoles):

```bash
python3 scripts/mamehub/gen_browser_profiles.py
# writes src/mame/<subtarget>.flt (except arcade), config.<id>.js,
# /<id>/index.html → play.html (symlink), hash/ symlinks, landing index.html, root config.js
```

Arcade’s driver list stays `python3 scripts/mamehub/build_arcade_top_mp.py` → `src/mame/arcade.flt`. Game Gear shares the Master System wasm (`mamesmshub.*`).

Every profile (`/snes/`, `/arcade/`, …) is the same `play.html` shell (symlink). Edit that file or `app.js` once; per-system differences live in `config.<id>.js` / `profiles.json`.

## Build

Requires [Emscripten](https://emscripten.org/) 3.1.35+ on your PATH (`emmake`, `emcc`).

**One subtarget** (`src/mame/<name>.flt` → `mame<name>hub.*`):

```bash
./web/mamehub/rebuild_fast.sh              # default SUBTARGET=snes
SUBTARGET=arcade ./web/mamehub/rebuild_fast.sh
# after genie.lua / link-flag / .flt changes:
REGENIE=1 ./web/mamehub/rebuild_fast.sh nes
```

**Every unique wasm** in `profiles.json`:

```bash
./web/mamehub/rebuild_fast.sh all
```

Uses all CPU cores. Switching subtarget forces `REGENIE=1`. Keep `OPTIMIZE=3` for object files (don’t flip OPTIMIZE every time — that forces a full recompile).

Copy artifacts and serve with the candy proxy:

```bash
cd web/mamehub && python3 serve.py --port 8765 --open
# http://127.0.0.1:8765/          → pick a system
# http://127.0.0.1:8765/snes/     → SNES shell
# http://127.0.0.1:8765/arcade/   → Arcade shell
# http://127.0.0.1:8765/nes/      → NES shell (etc.)
```

`serve.py` (and the desktop launcher) serve the static tree plus `/candy-proxy?url=…` (archive.org hosts only). Browser candy mode uses Asyncify `fetch` through that proxy into the Emscripten `/roms` path.

## Windows / macOS packages

Double-clickable launcher + shell/wasm (needs Go). **Default `PROFILE=all`** ships every profile in one zip:

```bash
./web/mamehub/package_windows.sh
# → web/mamehub/release/MAMEHubOnline-windows.zip

./web/mamehub/package_macos.sh
# → web/mamehub/release/MAMEHubOnline-macos.zip  (universal arm64+amd64)

# Thin single-profile packages (optional):
PROFILE=snes ./web/mamehub/package_windows.sh
PROFILE=arcade ./web/mamehub/package_macos.sh
```

Recipients unzip and run `MAMEHubOnline.exe` (Windows) or `MAMEHubOnline` (macOS). The browser opens the landing page; pick a system. Optional: `-profile snes` / `-profile arcade` / `-profile nes` to skip the chooser. Keep the process running while playing.

Local Python equivalent: `python3 serve.py --open`.

**Why builds feel slow:** every link runs Binaryen Asyncify over a large wasm (often many minutes at `-O2`). Avoid `REGENIE=1` unless needed, don’t delete existing wasm of the *same* subtarget, don’t change `OPTIMIZE=` between builds, and use `./web/mamehub/rebuild_fast.sh`.
## Identity

- Pass `-user_id <name>` to set an explicit id.
- If empty, desktop builds seed a 16-digit username from the host MAC address.
- Browser builds have no MAC; they fall back to random entropy. The shell keeps a **per-tab** Nostr keypair in `sessionStorage` so host + join in the same browser profile are distinct.

## Coordination (Nostr discovery + WebRTC)

There is **no central lobby server**. Discovery and WebRTC signaling use public (or self-hosted) **Nostr relays** in `config.<id>.js`.

1. Open **/snes/**, **/arcade/**, **/nes/**, … (or pick from the landing page) → **Host lobby** → pick game → **Play** — publishes a Nostr announce and shows a lobby with **Copy join link** and a **mesh graph**.
2. Share the join URL (`/snes/?join=1&room=…&soft=…` or `/arcade/?join=1…`). Joiners get seats in order (host P1, first joiner P2, …). Kick shifts later seats down.
3. Every peer builds a **full WebRTC mesh** (not host-star). The host can **Start game** only when every edge is green; red edges mean kick that peer. After start, new joiners are rejected.
4. SDP/ICE continue on Nostr (kind **30078**); **inputs** stay on the WebRTC DataChannels (each peer broadcasts puts to all others).

Room filter on relays is `#r` only (room id). The `app` tag is checked in the browser — combining `#app`+`#r` returns empty on some public relays (damus/nos.lol).

WGA (native UDP) is not used in the browser. Lockstep timing is reimplemented over the WebRTC DataChannel: shared start barrier, continuous host clock broadcast + RTT ping/pong with a sliding-window **95th‑percentile half-RTT** estimator (same ratchet as native `getLargestPing`), guest **EMA clock-skew** slew (±1 ms/sample), and `-throttle` so emulation waits on that clock via Asyncify `emscripten_sleep`. In-emulator overlay (toggle with **N**) shows `Peer <seat>: <mean> / <upper>` ms like desktop. Optional TURN in `iceServers` helps hard NATs; STUN alone is often enough on the same LAN.

Simulate latency in the browser (native `-fake_lag` only affects WGA): append `?fakelag=1` or `?lag=150&lagjitter=40` (outbound DataChannel delay; both peers ⇒ ~2× RTT). Optional `?lagdrop=0.01` drops non-critical puts only (start barriers / clock sync are never dropped). Also passes `-fake_lag` for a small local clock skew.

Offline remains **Start offline** (`-nomamehub`).

Mute for automated tests: append `?mute=1` (passes `-sound none`) or `?volume=-96`.

## ROM loading (candy)

With `-candy` (enabled by the shell), missing machine/softlist zips are fetched from archive.org the same way as desktop candy:

- Machines: `MAME220RomsOnlyMerged/<name>.zip` (e.g. `snes.zip` ≈ 209 bytes with `spc700.rom`)
- Softlists: `MAME_0.202_Software_List_ROMs_merged/<list>.zip/<list>%2F<software>.zip` (e.g. `snes/smw.zip`)

Enter a softlist shortname in the picker (from `hash/<list>.xml`). The shell loads that XML into the Emscripten FS, passes `-<media> <shortname>` (usually `-cart`), and candy downloads both the system zip and the chosen software zip.

Pure static hosting cannot reach archive.org from the browser (CORS); run `serve.py` or point `candyProxyBase` at another same-origin proxy you control.

## On-screen gamepad & keyboard

- **Instructions** opens a short how-to (host / join / inputs / gamepad).
- **Mobile** (UA / coarse pointer + no hover / iPadOS touch Mac): the on-screen SNES pad is shown by default. Desktop stays hidden until **Gamepad**.
- **Controls** opens MAME **Input Settings** once the emulator is running (same as Tab → Input Settings). Before play, it falls back to a shell remap panel (`force_input` / `localStorage`).
- Software picker loads every title from the profile’s hash XML into a prefix-search autocomplete.
- Shell defaults (pre-play `force_input`) are per system and avoid meta keys (Shift / Alt / Command). Arrows are the D-pad; **Enter** is Start; **V** is Select (arcade coin is **5**). Face buttons sit on **Z/X** (and **A/S/C/D** where the pad has more). Each play page shows a keyboard legend at the top.
- **In-emulator UI** (Tab menu, FPS, etc.) is soft-composited each frame and presented via WebGL2 (fixed-function GL hangs under Asyncify). Startup info / file-manager screens remain skipped.
