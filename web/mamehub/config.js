// SNES profile — used by /snes/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "snes",
  mode: "console",
  title: "SNES",
  noun: "SNES software",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mamesneshub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "/hash/snes.xml",
  hashFiles: [
    { file: "snes.xml", url: "/hash/snes.xml", machine: "snes" }
  ],
  machine: "snes",
  media: "cart",
  defaultSoftware: "smw",
  defaultHostSoftware: "smkart",
  canvasWidth: 512,
  canvasHeight: 448,
  keyboardLayout: "/layouts/snes.png",
  keyboard: {
    "labels": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "B",
      "a": "A",
      "y": "Y",
      "x": "X",
      "l": "L",
      "r": "R",
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
      "y",
      "x",
      "l",
      "r",
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
      "KeyA": "y",
      "KeyS": "x",
      "KeyQ": "l",
      "KeyW": "r",
      "KeyV": "select",
      "Enter": "start"
    }
  },
  appTag: "mamehub-browser-snes-v1"
}, window.MAMEHUB_BROWSER || {});
