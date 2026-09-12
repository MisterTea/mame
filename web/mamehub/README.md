# MAMEHub Browser (self-host)

Static WASM shell coordinated over **Nostr** (no central MAMEHub backend, no Discord Social SDK).

## Build (SNES only)

Requires [Emscripten](https://emscripten.org/) 3.1.35+ on your PATH (`emmake`, `emcc`).

**Fast iteration** (after the first full genie run):

```bash
./web/mamehub/rebuild_fast.sh
# after genie.lua / link-flag changes only:
REGENIE=1 ./web/mamehub/rebuild_fast.sh
```

Uses all CPU cores, skips project regen by default, and links with `-O0` so Asyncify `wasm-opt` is much cheaper than `-O2`. Keep `OPTIMIZE=3` for object files (don’t flip OPTIMIZE every time — that forces a full recompile).

First-time / after changing `scripts/genie.lua` options:

```bash
emmake make SUBTARGET=snes REGENIE=1 WEBASSEMBLY=1 -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
cp mamesneshub.js mamesneshub.wasm web/mamehub/dist/
```

Slower “release-ish” link (set link back to `-O2` in `scripts/genie.lua` if you need it):

```bash
emmake make SUBTARGET=snes WEBASSEMBLY=1 -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
```

`src/mame/snes.flt` limits the driver set to SNES.

Copy artifacts and serve with the candy proxy:

```bash
cp mamesneshub.js mamesneshub.wasm web/mamehub/dist/
cd web/mamehub && python3 serve.py --port 8765
# open http://127.0.0.1:8765/
```

`serve.py` serves the static shell and `/candy-proxy?url=…` (archive.org hosts only). Browser candy mode uses Asyncify `fetch` through that proxy into the Emscripten `/roms` path.

**Why builds feel slow:** every link runs Binaryen Asyncify over a ~30–40MB wasm (often 2–10+ minutes at `-O2`). Avoid `REGENIE=1`, don’t `rm mamesneshub.wasm`, don’t change `OPTIMIZE=` between builds, and use `./web/mamehub/rebuild_fast.sh`.

## Identity

- Pass `-user_id <name>` to set an explicit id.
- If empty, desktop builds seed a 16-digit username from the host MAC address.
- Browser builds have no MAC; they fall back to random entropy. The shell keeps a **per-tab** Nostr keypair in `sessionStorage` so host + join in the same browser profile are distinct.

## Coordination (Nostr discovery + WebRTC)

There is **no central lobby server**. Discovery and WebRTC signaling use public (or self-hosted) **Nostr relays** in `config.js`.

1. **Host lobby** → pick cart → **Play** — publishes a Nostr announce (kind 30078) and waits for joiner.
2. Share the **room id** out-of-band (chat, URL, etc.).
3. **Join** with that room id → same cart → **Play** — publishes a Nostr `hello`; host replies with a WebRTC offer over Nostr.
4. SDP/ICE continue on Nostr (kind **30078** addressable events + live subscribe/`querySync` poll); **inputs** stay on the WebRTC DataChannel.

Room filter on relays is `#r` only (room id). The `app` tag is checked in the browser — combining `#app`+`#r` returns empty on some public relays (damus/nos.lol).

WGA (native UDP) is not used in the browser. Optional TURN in `iceServers` helps hard NATs; STUN alone is often enough on the same LAN.

Offline remains **Start offline** (`-nomamehub`).

## ROM loading (candy)

With `-candy` (enabled by the shell), missing machine/softlist zips are fetched from archive.org the same way as desktop candy:

- Machines: `MAME220RomsOnlyMerged/<name>.zip` (e.g. `snes.zip` ≈ 209 bytes with `spc700.rom`)
- Softlists: `MAME_0.202_Software_List_ROMs_merged/<list>.zip/<list>%2F<software>.zip` (e.g. `snes/smw.zip`)

Enter a softlist shortname in the **cart** field (from `hash/snes.xml`). The shell loads that XML into the Emscripten FS, passes `-cart <shortname>`, and candy downloads both the system zip and the chosen software zip.

Pure static hosting cannot reach archive.org from the browser (CORS); run `serve.py` or point `candyProxyBase` at another same-origin proxy you control.
