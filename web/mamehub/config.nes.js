// NES profile — used by /nes/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "nes",
  mode: "console",
  title: "NES",
  noun: "NES software",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mameneshub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "/hash/nes.xml",
  hashFiles: [
    { file: "nes.xml", url: "/hash/nes.xml", machine: "nes" }
  ],
  machine: "nes",
  media: "cart",
  defaultSoftware: "smb",
  defaultHostSoftware: "tmnt",
  canvasWidth: 512,
  canvasHeight: 480,
  keyboardLayout: "/layouts/nes.png",
  keyboard: {
    "labels": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "B",
      "a": "A",
      "start": "Start",
      "select": "Select"
    },
    "order": [
      "up",
      "down",
      "left",
      "right",
      "b",
      "a",
      "select",
      "start"
    ],
    "bindings": {
      "ArrowUp": "up",
      "ArrowDown": "down",
      "ArrowLeft": "left",
      "ArrowRight": "right",
      "KeyZ": "b",
      "KeyX": "a",
      "KeyV": "select",
      "Enter": "start"
    }
  },
  appTag: "mamehub-browser-nes-v1"
}, window.MAMEHUB_BROWSER || {});
