// Game Boy profile — used by /gameboy/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "gameboy",
  mode: "console",
  title: "Game Boy",
  noun: "Game Boy software",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mamegameboyhub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "/hash/gameboy.xml",
  hashFiles: [
    { file: "gameboy.xml", url: "/hash/gameboy.xml", machine: "gameboy" },
    { file: "gbcolor.xml", url: "/hash/gbcolor.xml", machine: "gbcolor" }
  ],
  machine: "gameboy",
  media: "cart",
  defaultSoftware: "tetris",
  defaultHostSoftware: "tetris",
  canvasWidth: 320,
  canvasHeight: 288,
  keyboardLayout: "/layouts/gameboy.png",
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
  appTag: "mamehub-browser-gameboy-v1"
}, window.MAMEHUB_BROWSER || {});
