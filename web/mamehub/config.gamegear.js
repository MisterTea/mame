// Game Gear profile — used by /gamegear/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "gamegear",
  mode: "console",
  title: "Game Gear",
  noun: "Game Gear software",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mamesmshub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "/hash/gamegear.xml",
  hashFiles: [
    { file: "gamegear.xml", url: "/hash/gamegear.xml", machine: "gamegear" }
  ],
  machine: "gamegear",
  media: "cart",
  defaultSoftware: "sonic",
  defaultHostSoftware: "sonic",
  canvasWidth: 320,
  canvasHeight: 288,
  keyboardLayout: "/layouts/gamegear.png",
  keyboard: {
    "labels": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "Button 1",
      "a": "Button 2",
      "start": "Start"
    },
    "keycaps": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "B1",
      "a": "B2",
      "start": "Start"
    },
    "order": [
      "up",
      "down",
      "left",
      "right",
      "b",
      "a",
      "start"
    ],
    "bindings": {
      "ArrowUp": "up",
      "ArrowDown": "down",
      "ArrowLeft": "left",
      "ArrowRight": "right",
      "KeyZ": "b",
      "KeyX": "a",
      "Enter": "start"
    }
  },
  appTag: "mamehub-browser-gamegear-v1"
}, window.MAMEHUB_BROWSER || {});
