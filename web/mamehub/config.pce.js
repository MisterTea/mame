// PC Engine profile — used by /pce/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "pce",
  mode: "console",
  title: "PC Engine",
  noun: "PC Engine software",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mamepcehub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "/hash/pce.xml",
  hashFiles: [
    { file: "pce.xml", url: "/hash/pce.xml", machine: "pce" }
  ],
  machine: "pce",
  media: "cart",
  defaultSoftware: "outrun",
  defaultHostSoftware: "outrun",
  canvasWidth: 512,
  canvasHeight: 448,
  keyboardLayout: "/layouts/pce.png",
  keyboard: {
    "labels": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "II",
      "a": "I",
      "start": "Run",
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
  appTag: "mamehub-browser-pce-v1"
}, window.MAMEHUB_BROWSER || {});
