// Arcade profile — load as config.js in Arcade packages (or override before shell scripts).
window.MAMEHUB_BROWSER = Object.assign({
  mode: "arcade",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  wasmJs: "dist/mamearcadehub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  // Softlist unused in arcade mode; kept for API compatibility.
  hashPath: "/hash",
  hashUrl: "",
  machinesUrl: "arcade_top_mp.json",
  defaultMachine: "xmen6p",
  appTag: "mamehub-browser-arcade-v1"
}, window.MAMEHUB_BROWSER || {});
