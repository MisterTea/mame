# MAMEHub Browser (self-host)

Static WASM shell coordinated over **Nostr** (no central MAMEHub backend, no Discord Social SDK).

## Build

Requires [Emscripten](https://emscripten.org/) 3.1.35+ on your PATH (`emmake`, `emcc`).

**SNES (default)** — `src/mame/snes.flt` → `mamesneshub.*`:

```bash
./web/mamehub/rebuild_fast.sh
# after genie.lua / link-flag / .flt changes:
REGENIE=1 ./web/mamehub/rebuild_fast.sh
```

**Arcade (100 popular 2P+ titles, includes `xmen6p`)** — `src/mame/arcade.flt` → `mamearcadehub.*` (does **not** overwrite SNES dist files):

```bash
# regenerate title list + .flt (needs a native mame/mamehub binary):
python3 scripts/mamehub/build_arcade_top_mp.py /path/to/mamehub
REGENIE=1 SUBTARGET=arcade ./web/mamehub/rebuild_fast.sh
SUBTARGET=arcade ./web/mamehub/rebuild_fast.sh
```

Serve with arcade config: copy `config.arcade.js` over `config.js`, or point a local override at `mode: "arcade"` / `wasmJs: "dist/mamearcadehub.js"`.

Uses all CPU cores, skips project regen by default. Keep `OPTIMIZE=3` for object files (don’t flip OPTIMIZE every time — that forces a full recompile).

First-time / after changing `scripts/genie.lua` options:

```bash
emmake make SUBTARGET=snes REGENIE=1 WEBASSEMBLY=1 -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
cp mamesneshub.js mamesneshub.wasm web/mamehub/dist/
```

Copy artifacts and serve with the candy proxy:

```bash
cp mamesneshub.js mamesneshub.wasm web/mamehub/dist/
cd web/mamehub && python3 serve.py --port 8765 --open
# open http://127.0.0.1:8765/
```

`serve.py` serves the static shell and `/candy-proxy?url=…` (archive.org hosts only). Browser candy mode uses Asyncify `fetch` through that proxy into the Emscripten `/roms` path.

## Windows / macOS packages

Double-clickable launcher + shell/wasm (needs Go):

```bash
./web/mamehub/package_windows.sh
# → web/mamehub/release/MAMEHubOnline-SNES-windows.zip

./web/mamehub/package_macos.sh
# → web/mamehub/release/MAMEHubOnline-SNES-macos.zip  (universal arm64+amd64)

PROFILE=arcade ./web/mamehub/package_windows.sh
PROFILE=arcade ./web/mamehub/package_macos.sh
# → MAMEHubOnline-Arcade-{windows,macos}.zip
```

Recipients unzip and run `MAMEHubOnline.exe` (Windows) or `MAMEHubOnline` (macOS). That starts the local server + candy proxy and opens the default browser. Keep the process running while playing.

Local Python equivalent: `python3 serve.py --open`.

**Why builds feel slow:** every link runs Binaryen Asyncify over a large wasm (often many minutes at `-O2`). Avoid `REGENIE=1` unless needed, don’t delete existing wasm of the *same* subtarget, don’t change `OPTIMIZE=` between builds, and use `./web/mamehub/rebuild_fast.sh`.
## Identity

- Pass `-user_id <name>` to set an explicit id.
- If empty, desktop builds seed a 16-digit username from the host MAC address.
- Browser builds have no MAC; they fall back to random entropy. The shell keeps a **per-tab** Nostr keypair in `sessionStorage` so host + join in the same browser profile are distinct.

## Coordination (Nostr discovery + WebRTC)

There is **no central lobby server**. Discovery and WebRTC signaling use public (or self-hosted) **Nostr relays** in `config.js`.

1. **Host lobby** → pick cart → **Play** — publishes a Nostr announce and shows a lobby with **Copy join link** and a **mesh graph**.
2. Share the join URL (`?join=1&room=…&soft=…`). Joiners get seats in order (host P1, first joiner P2, …). Kick shifts later seats down.
3. Every peer builds a **full WebRTC mesh** (not host-star). The host can **Start game** only when every edge is green; red edges mean kick that peer. After start, new joiners are rejected.
4. SDP/ICE continue on Nostr (kind **30078**); **inputs** stay on the WebRTC DataChannels (each peer broadcasts puts to all others).

Room filter on relays is `#r` only (room id). The `app` tag is checked in the browser — combining `#app`+`#r` returns empty on some public relays (damus/nos.lol).

WGA (native UDP) is not used in the browser. Lockstep timing is reimplemented over the WebRTC DataChannel: shared start barrier, continuous host clock broadcast + RTT ping/pong (guest slews toward host), and `-throttle` so emulation waits on that clock via Asyncify `emscripten_sleep`. Optional TURN in `iceServers` helps hard NATs; STUN alone is often enough on the same LAN.

Simulate latency in the browser (native `-fake_lag` only affects WGA): append `?fakelag=1` or `?lag=150&lagjitter=40` (outbound DataChannel delay; both peers ⇒ ~2× RTT). Optional `?lagdrop=0.01` drops non-critical puts only (start barriers / clock sync are never dropped). Also passes `-fake_lag` for a small local clock skew.

Offline remains **Start offline** (`-nomamehub`).

Mute for automated tests: append `?mute=1` (passes `-sound none`) or `?volume=-96`.

## ROM loading (candy)

With `-candy` (enabled by the shell), missing machine/softlist zips are fetched from archive.org the same way as desktop candy:

- Machines: `MAME220RomsOnlyMerged/<name>.zip` (e.g. `snes.zip` ≈ 209 bytes with `spc700.rom`)
- Softlists: `MAME_0.202_Software_List_ROMs_merged/<list>.zip/<list>%2F<software>.zip` (e.g. `snes/smw.zip`)

Enter a softlist shortname in the **cart** field (from `hash/snes.xml`). The shell loads that XML into the Emscripten FS, passes `-cart <shortname>`, and candy downloads both the system zip and the chosen software zip.

Pure static hosting cannot reach archive.org from the browser (CORS); run `serve.py` or point `candyProxyBase` at another same-origin proxy you control.

## On-screen gamepad & keyboard

- **Instructions** opens a short how-to (host / join / inputs / gamepad).
- **Mobile** (UA / coarse pointer + no hover / iPadOS touch Mac): the on-screen SNES pad is shown by default. Desktop stays hidden until **Gamepad**.
- **Controls** opens MAME **Input Settings** once the emulator is running (same as Tab → Input Settings). Before play, it falls back to a shell remap panel (`force_input` / `localStorage`).
- Software picker loads every title from `hash/snes.xml` into a prefix-search autocomplete.
- Shell defaults (pre-play force_input) and browser MAME defaults: arrows = D-pad, **Z/X/A/S = B/A/Y/X**, Q/W = L/R (avoids Alt/Option, which triggers browser back with arrows). Start/Select remain `1`/`5` in MAME; shell also maps Enter / Left Shift.
- **In-emulator UI** (Tab menu, FPS, etc.) is soft-composited each frame and presented via WebGL2 (fixed-function GL hangs under Asyncify). Startup info / file-manager screens remain skipped.
