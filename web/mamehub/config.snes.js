// SNES profile snapshot (same as default config.js).
window.MAMEHUB_BROWSER = Object.assign({
  mode: "snes",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  wasmJs: "dist/mamesneshub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "hash/snes.xml",
  appTag: "mamehub-browser-v1"
}, window.MAMEHUB_BROWSER || {});
