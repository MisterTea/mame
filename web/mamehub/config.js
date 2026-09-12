// Default self-host config. Override before loading the shell scripts.
window.MAMEHUB_BROWSER = Object.assign({
  // Public or self-hosted Nostr relays used for lobby + WebRTC signaling.
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  // Optional TURN servers for hard NATs (empty = STUN-only).
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Path to the Emscripten output (place build artifacts under web/mamehub/dist/).
  wasmJs: "dist/mamesneshub.js",
  // Same-origin proxy that fetches archive.org candy zips (see serve.py).
  candyProxyBase: "/candy-proxy",
  // Virtual rompath inside the Emscripten filesystem.
  romPath: "/roms",
  // Softlist XML served from web/mamehub/hash/ (symlink to repo hash/).
  hashPath: "/hash",
  hashUrl: "hash/snes.xml",
  // Application tag embedded in Nostr lobby events.
  appTag: "mamehub-browser-v1"
}, window.MAMEHUB_BROWSER || {});
