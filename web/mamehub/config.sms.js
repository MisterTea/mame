// Master System profile — used by /sms/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "sms",
  mode: "console",
  title: "Master System",
  noun: "Master System software",
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
  hashUrl: "/hash/sms.xml",
  hashFiles: [
    { file: "sms.xml", url: "/hash/sms.xml", machine: "sms" }
  ],
  machine: "sms",
  media: "cart",
  defaultSoftware: "sonic",
  defaultHostSoftware: "sonic",
  canvasWidth: 512,
  canvasHeight: 384,
  keyboardLayout: "/layouts/sms.png",
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
  appTag: "mamehub-browser-sms-v1"
}, window.MAMEHUB_BROWSER || {});
