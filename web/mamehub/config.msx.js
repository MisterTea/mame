// MSX profile — used by /msx/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "msx",
  mode: "console",
  title: "MSX",
  noun: "MSX software",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mamemsxhub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "/hash/msx1_cart.xml",
  hashFiles: [
    { file: "msx1_cart.xml", url: "/hash/msx1_cart.xml", machine: "hb75p" },
    { file: "msx2_cart.xml", url: "/hash/msx2_cart.xml", machine: "nms8250" }
  ],
  machine: "hb75p",
  media: "cart",
  defaultSoftware: "pengadv",
  defaultHostSoftware: "bombman",
  canvasWidth: 512,
  canvasHeight: 448,
  keyboardLayout: "/layouts/msx.png",
  keyboard: {
    "labels": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "Button 1",
      "a": "Button 2"
    },
    "keycaps": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "B1",
      "a": "B2"
    },
    "order": [
      "up",
      "down",
      "left",
      "right",
      "b",
      "a"
    ],
    "bindings": {
      "ArrowUp": "up",
      "ArrowDown": "down",
      "ArrowLeft": "left",
      "ArrowRight": "right",
      "KeyZ": "b",
      "KeyX": "a"
    }
  },
  appTag: "mamehub-browser-msx-v1"
}, window.MAMEHUB_BROWSER || {});
