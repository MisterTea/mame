// Genesis profile — used by /genesis/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "genesis",
  mode: "console",
  title: "Genesis",
  noun: "Genesis software",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mamegenesishub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "/hash/megadriv.xml",
  hashFiles: [
    { file: "megadriv.xml", url: "/hash/megadriv.xml", machine: "genesis" }
  ],
  machine: "genesis",
  media: "cart",
  defaultSoftware: "sonic",
  defaultHostSoftware: "sor2",
  canvasWidth: 640,
  canvasHeight: 448,
  keyboardLayout: "/layouts/genesis.png",
  keyboard: {
    "labels": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "y": "A",
      "b": "B",
      "a": "C",
      "x": "X",
      "l": "Y",
      "r": "Z",
      "start": "Start",
      "select": "Mode"
    },
    "order": [
      "up",
      "down",
      "left",
      "right",
      "y",
      "b",
      "a",
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
      "KeyZ": "y",
      "KeyX": "b",
      "KeyC": "a",
      "KeyA": "x",
      "KeyS": "l",
      "KeyD": "r",
      "KeyV": "select",
      "Enter": "start"
    }
  },
  appTag: "mamehub-browser-genesis-v1"
}, window.MAMEHUB_BROWSER || {});
