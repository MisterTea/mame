// Game Boy Advance profile — used by /gba/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "gba",
  mode: "console",
  title: "Game Boy Advance",
  noun: "GBA software",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mamegbahub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "/hash/gba.xml",
  hashFiles: [
    { file: "gba.xml", url: "/hash/gba.xml", machine: "gba" }
  ],
  machine: "gba",
  media: "cart",
  defaultSoftware: "mariokrt",
  defaultHostSoftware: "mariokrt",
  canvasWidth: 480,
  canvasHeight: 320,
  keyboardLayout: "/layouts/gba.png",
  keyboard: {
    "labels": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "B",
      "a": "A",
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
      "KeyQ": "l",
      "KeyW": "r",
      "KeyV": "select",
      "Enter": "start"
    }
  },
  appTag: "mamehub-browser-gba-v1"
}, window.MAMEHUB_BROWSER || {});
