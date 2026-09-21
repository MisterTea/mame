// Arcade profile — used by /arcade/.
window.MAMEHUB_BROWSER = Object.assign({
  id: "arcade",
  mode: "arcade",
  title: "Arcade",
  noun: "arcade machine",
  relays: [
    "wss://relay.damus.io",
    "wss://nos.lol"
  ],
  iceServers: [
    { urls: "stun:stun.l.google.com:19302" }
  ],
  // Root-absolute so profile shells share one dist/ tree.
  wasmJs: "/dist/mamearcadehub.js",
  candyProxyBase: "/candy-proxy",
  romPath: "/roms",
  hashPath: "/hash",
  hashUrl: "",
  hashFiles: [],
  machinesUrl: "/arcade_top_mp.json",
  defaultMachine: "xmen",
  canvasWidth: 1152,
  canvasHeight: 448,
  keyboardLayout: "/layouts/arcade.png",
  keyboard: {
    "labels": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "Button 1",
      "a": "Button 2",
      "y": "Button 3",
      "x": "Button 4",
      "l": "Button 5",
      "r": "Button 6",
      "start": "1P Start",
      "select": "Coin"
    },
    "keycaps": {
      "up": "Up",
      "down": "Down",
      "left": "Left",
      "right": "Right",
      "b": "B1",
      "a": "B2",
      "y": "B3",
      "x": "B4",
      "l": "B5",
      "r": "B6",
      "start": "Start",
      "select": "Coin"
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
      "KeyC": "y",
      "KeyA": "x",
      "KeyS": "l",
      "KeyD": "r",
      "Enter": "start",
      "Digit5": "select"
    }
  },
  appTag: "mamehub-browser-arcade-v1"
}, window.MAMEHUB_BROWSER || {});
