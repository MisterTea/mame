// Atari 2600 profile — used by /a2600/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "a2600",
  mode: "console",
  title: "Atari 2600",
  noun: "Atari 2600 software",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mamea2600hub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "/hash/a2600.xml",
  hashFiles: [
    { file: "a2600.xml", url: "/hash/a2600.xml", machine: "a2600" }
  ],
  machine: "a2600",
  media: "cart",
  defaultSoftware: "combat",
  defaultHostSoftware: "combat",
  canvasWidth: 320,
  canvasHeight: 384,
  keyboardLayout: "/layouts/a2600.png",
  keyboard: {
    "labels": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "Fire",
      "start": "Reset",
      "select": "Select"
    },
    "order": [
      "up",
      "down",
      "left",
      "right",
      "b",
      "select",
      "start"
    ],
    "bindings": {
      "ArrowUp": "up",
      "ArrowDown": "down",
      "ArrowLeft": "left",
      "ArrowRight": "right",
      "Space": "b",
      "KeyV": "select",
      "Enter": "start"
    }
  },
  appTag: "mamehub-browser-a2600-v1"
}, window.MAMEHUB_BROWSER || {});
