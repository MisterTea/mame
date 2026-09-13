/**
 * On-screen SNES pad for touch devices. Drives MAME via sticky netplay force
 * inputs (preferred under -mamehub) and SDL scancodes when available.
 * Also: keyboard remaps (shell UI) and mobile default for the on-screen pad.
 */
(function (global) {
  // SNES → MAME defaults / mamehub_id names for P1
  const P1 = {
    up: "INPUT/0/P1 Up",
    down: "INPUT/0/P1 Down",
    left: "INPUT/0/P1 Left",
    right: "INPUT/0/P1 Right",
    b: "INPUT/0/P1 B",
    y: "INPUT/0/P1 Y",
    a: "INPUT/0/P1 A",
    x: "INPUT/0/P1 X",
    l: "INPUT/0/P1 L",
    r: "INPUT/0/P1 R",
    start: "INPUT/0/P1 Start",
    select: "INPUT/0/P1 Select"
  };
  const P2 = {
    up: "INPUT/1/P2 Up",
    down: "INPUT/1/P2 Down",
    left: "INPUT/1/P2 Left",
    right: "INPUT/1/P2 Right",
    b: "INPUT/1/P2 B",
    y: "INPUT/1/P2 Y",
    a: "INPUT/1/P2 A",
    x: "INPUT/1/P2 X",
    l: "INPUT/1/P2 L",
    r: "INPUT/1/P2 R",
    start: "INPUT/1/P2 Start",
    select: "INPUT/1/P2 Select"
  };

  // SDL scancodes for browser_key fallback
  const SCAN = {
    up: 82, down: 81, left: 80, right: 79,
    b: 226, y: 224, a: 44, x: 225,
    l: 29, r: 27, start: 30, select: 34
  };

  const ACTION_LABELS = {
    up: "Up", down: "Down", left: "Left", right: "Right",
    b: "B", y: "Y", a: "A", x: "X",
    l: "L", r: "R", start: "Start", select: "Select"
  };
  const ACTION_ORDER = [
    "up", "down", "left", "right",
    "b", "a", "y", "x",
    "l", "r", "select", "start"
  ];

  /** Default KeyboardEvent.code → pad action (SNES-ish layout). */
  const DEFAULT_BINDINGS = {
    ArrowUp: "up",
    ArrowDown: "down",
    ArrowLeft: "left",
    ArrowRight: "right",
    KeyZ: "b",
    KeyX: "a",
    KeyA: "y",
    KeyS: "x",
    KeyQ: "l",
    KeyW: "r",
    Enter: "start",
    ShiftLeft: "select"
  };

  const STORAGE_KEY = "mamehub.keyboardBindings.v1";

  const pressed = new Set();
  let playerMaps = P1;
  /** @type {Record<string, string>} code → action */
  let bindings = loadBindings();
  let listeningFor = null;
  let controlsMounted = false;

  function isMobile() {
    try {
      const ua = navigator.userAgent || "";
      if (/Android|iPhone|iPad|iPod|Mobile|IEMobile|Opera Mini/i.test(ua))
        return true;
      // iPadOS 13+ often reports as Mac with touch.
      if (/Macintosh/i.test(ua) && (navigator.maxTouchPoints || 0) > 1)
        return true;
    } catch (_) { /* ignore */ }
    try {
      const coarse = window.matchMedia("(pointer: coarse)").matches;
      const noHover = window.matchMedia("(hover: none)").matches;
      if (coarse && noHover)
        return true;
    } catch (_) { /* ignore */ }
    return false;
  }

  function prefersTouchUi() {
    return isMobile();
  }

  function shouldShowByDefault() {
    return isMobile();
  }

  function cloneDefaults() {
    return Object.assign({}, DEFAULT_BINDINGS);
  }

  function loadBindings() {
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw)
        return cloneDefaults();
      const parsed = JSON.parse(raw);
      if (!parsed || typeof parsed !== "object")
        return cloneDefaults();
      const out = cloneDefaults();
      for (const [code, action] of Object.entries(parsed)) {
        if (typeof code === "string" && ACTION_LABELS[action])
          out[code] = action;
      }
      return out;
    } catch (_) {
      return cloneDefaults();
    }
  }

  function saveBindings() {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(bindings));
    } catch (_) { /* ignore */ }
  }

  function codeToAction() {
    return bindings;
  }

  function actionToCodes() {
    const map = {};
    for (const [code, action] of Object.entries(bindings)) {
      if (!map[action])
        map[action] = [];
      map[action].push(code);
    }
    return map;
  }

  function formatCode(code) {
    if (!code)
      return "—";
    if (code.startsWith("Key") && code.length === 4)
      return code.slice(3);
    if (code.startsWith("Digit"))
      return code.slice(5);
    if (code.startsWith("Arrow"))
      return code.slice(5);
    if (code === "ShiftLeft")
      return "LShift";
    if (code === "ShiftRight")
      return "RShift";
    if (code === "ControlLeft")
      return "LCtrl";
    if (code === "ControlRight")
      return "RCtrl";
    if (code === "AltLeft")
      return "LAlt";
    if (code === "AltRight")
      return "RAlt";
    if (code === "Space")
      return "Space";
    if (code === "Enter")
      return "Enter";
    return code;
  }

  function setPlayer(/* playerIndex */) {
    // Always drive P1 control names. MAMEHub remaps P1 → P2/P3/… from
    // Module.mamehubNet.player / getMyPlayers().
    playerMaps = P1;
  }

  function force(name, down) {
    const key = playerMaps[name];
    if (!key)
      return;
    try {
      if (global.JSMAME && typeof global.JSMAME.force_input === "function")
        global.JSMAME.force_input(key, down ? "1" : "");
    } catch (_) { /* ignore */ }
    try {
      if (global.JSMAME && typeof global.JSMAME.browser_key === "function" && SCAN[name] != null)
        global.JSMAME.browser_key(SCAN[name], down ? 1 : 0);
    } catch (_) { /* ignore */ }
  }

  function setPressed(name, down) {
    if (down) {
      if (pressed.has(name))
        return;
      pressed.add(name);
      force(name, true);
    } else {
      if (!pressed.has(name))
        return;
      pressed.delete(name);
      force(name, false);
    }
  }

  function releaseAll() {
    for (const name of [...pressed])
      setPressed(name, false);
  }

  function tap(name, holdMs) {
    setPressed(name, true);
    return new Promise((resolve) => {
      setTimeout(() => {
        setPressed(name, false);
        resolve();
      }, holdMs || 120);
    });
  }

  function bindPadButton(el) {
    const name = el.getAttribute("data-pad");
    if (!name)
      return;

    const down = (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      el.classList.add("active");
      try { el.setPointerCapture(ev.pointerId); } catch (_) { /* ignore */ }
      setPressed(name, true);
    };
    const up = (ev) => {
      ev.preventDefault();
      ev.stopPropagation();
      el.classList.remove("active");
      setPressed(name, false);
    };

    el.addEventListener("pointerdown", down);
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
    el.addEventListener("lostpointercapture", () => {
      el.classList.remove("active");
      setPressed(name, false);
    });
    el.addEventListener("contextmenu", (ev) => ev.preventDefault());
  }

  function applyPadVisibility(show) {
    const root = document.getElementById("virtual-gamepad");
    const toggle = document.getElementById("gamepadToggleBtn");
    if (!root)
      return;
    root.hidden = !show;
    root.setAttribute("aria-hidden", show ? "false" : "true");
    if (toggle)
      toggle.setAttribute("aria-pressed", show ? "true" : "false");
    if (!show)
      releaseAll();
  }

  function renderControlsList() {
    const list = document.getElementById("controls-bind-list");
    if (!list)
      return;
    const byAction = actionToCodes();
    list.innerHTML = "";
    for (const action of ACTION_ORDER) {
      const btn = document.createElement("button");
      btn.type = "button";
      btn.className = "bind-row" + (listeningFor === action ? " listening" : "");
      btn.dataset.action = action;
      const codes = byAction[action] || [];
      const keyText = listeningFor === action
        ? "press key…"
        : (codes.length ? codes.map(formatCode).join(", ") : "—");
      btn.innerHTML =
        '<span class="bind-label">' + ACTION_LABELS[action] + "</span>" +
        '<span class="bind-key">' + keyText + "</span>";
      btn.addEventListener("click", () => {
        listeningFor = listeningFor === action ? null : action;
        renderControlsList();
      });
      list.appendChild(btn);
    }
  }

  function openControlsPanel() {
    const panel = document.getElementById("controls-panel");
    if (!panel)
      return;
    panel.hidden = false;
    listeningFor = null;
    renderControlsList();
  }

  function closeControlsPanel() {
    const panel = document.getElementById("controls-panel");
    if (panel)
      panel.hidden = true;
    listeningFor = null;
  }

  /** Prefer in-emulator Input Settings; fall back to shell remap before play. */
  function openControls() {
    closeControlsPanel();
    try {
      if (global.JSMAME && typeof global.JSMAME.show_input_settings === "function") {
        if (global.JSMAME.show_input_settings())
          return true;
      }
    } catch (_) { /* ignore */ }
    try {
      if (typeof Module !== "undefined" && typeof Module._mamehub_browser_show_input_settings === "function") {
        if (Module._mamehub_browser_show_input_settings())
          return true;
      }
    } catch (_) { /* ignore */ }
    openControlsPanel();
    return false;
  }

  function assignBinding(action, code) {
    if (!ACTION_LABELS[action] || !code)
      return;
    // Drop this code from any other action; drop other codes for this action (1 key each).
    const next = {};
    for (const [c, a] of Object.entries(bindings)) {
      if (c === code || a === action)
        continue;
      next[c] = a;
    }
    next[code] = action;
    bindings = next;
    saveBindings();
    listeningFor = null;
    renderControlsList();
  }

  function resetBindings() {
    bindings = cloneDefaults();
    saveBindings();
    listeningFor = null;
    releaseAll();
    renderControlsList();
  }

  function onKeyDown(ev) {
    if (ev.repeat)
      return;
    // Remap capture mode
    if (listeningFor) {
      ev.preventDefault();
      ev.stopPropagation();
      if (ev.code === "Escape") {
        listeningFor = null;
        renderControlsList();
        return;
      }
      if (ev.code === "Backspace" || ev.code === "Delete") {
        // Clear binding for this action
        const next = {};
        for (const [c, a] of Object.entries(bindings)) {
          if (a !== listeningFor)
            next[c] = a;
        }
        bindings = next;
        saveBindings();
        listeningFor = null;
        renderControlsList();
        return;
      }
      assignBinding(listeningFor, ev.code);
      return;
    }

    // Don't steal keys while typing in form fields
    const tag = (ev.target && ev.target.tagName) || "";
    if (tag === "INPUT" || tag === "TEXTAREA" || tag === "SELECT" || (ev.target && ev.target.isContentEditable))
      return;

    const action = codeToAction()[ev.code];
    if (!action)
      return;
    ev.preventDefault();
    setPressed(action, true);
  }

  function onKeyUp(ev) {
    if (listeningFor)
      return;
    const action = codeToAction()[ev.code];
    if (!action)
      return;
    ev.preventDefault();
    setPressed(action, false);
  }

  function mountControlsUi() {
    if (controlsMounted)
      return;
    controlsMounted = true;
    const openBtn = document.getElementById("controlsBtn");
    const closeBtn = document.getElementById("controlsCloseBtn");
    const resetBtn = document.getElementById("controlsResetBtn");
    if (openBtn)
      openBtn.onclick = () => openControls();
    if (closeBtn)
      closeBtn.onclick = () => closeControlsPanel();
    if (resetBtn)
      resetBtn.onclick = () => resetBindings();
    window.addEventListener("keydown", onKeyDown, true);
    window.addEventListener("keyup", onKeyUp, true);
  }

  function mount() {
    const root = document.getElementById("virtual-gamepad");
    if (!root)
      return;
    root.querySelectorAll("[data-pad]").forEach(bindPadButton);
    window.addEventListener("blur", releaseAll);
    document.addEventListener("visibilitychange", () => {
      if (document.hidden)
        releaseAll();
    });

    const toggle = document.getElementById("gamepadToggleBtn");
    const show = shouldShowByDefault();
    applyPadVisibility(show);
    if (toggle) {
      toggle.onclick = () => {
        applyPadVisibility(!!root.hidden);
      };
    }
    mountControlsUi();
  }

  function showForPlay() {
    setPlayer(0);
    if (shouldShowByDefault())
      applyPadVisibility(true);
  }

  function readNetplayTimeMs() {
    const toMs = (v) => {
      if (typeof v === "bigint")
        return Number(v);
      return Number(v) || 0;
    };
    try {
      if (global.JSMAME && typeof global.JSMAME.netplay_time_ms === "function")
        return toMs(global.JSMAME.netplay_time_ms());
    } catch (_) { /* ignore */ }
    try {
      if (typeof Module !== "undefined" && typeof Module._mamehub_browser_netplay_time_ms === "function")
        return toMs(Module._mamehub_browser_netplay_time_ms());
    } catch (_) { /* ignore */ }
    return 0;
  }

  async function waitForNetplayClock(minMs, timeoutMs) {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    const deadline = Date.now() + (timeoutMs || 120000);
    const need = minMs == null ? 1200 : minMs;
    while (Date.now() < deadline) {
      const t = readNetplayTimeMs();
      if (t >= need)
        return t;
      await sleep(250);
    }
    throw new Error("netplay clock did not reach " + need + "ms");
  }

  /** Hold a button across enough published input frames (not wall clock). */
  async function holdNetplay(name, netplayMs) {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    setPressed(name, true);
    const needFrames = Math.max(10, Math.ceil((netplayMs || 800) / 16));
    const startSends = (typeof Module !== "undefined" && Module.__mamehubSendCount) | 0;
    const deadline = Date.now() + 180000;
    while (Date.now() < deadline) {
      const sends = (typeof Module !== "undefined" && Module.__mamehubSendCount) | 0;
      if (sends - startSends >= needFrames)
        break;
      await sleep(40);
    }
    setPressed(name, false);
    await sleep(80);
  }

  /**
   * Host: attract → Match Race → race, then random P1 inputs.
   * Joiner: same P1 pad names; mamehub seat remaps to P2+.
   */
  async function scriptMarioKartMatchRace() {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    const log = (m) => {
      try {
        const el = document.getElementById("log");
        if (el) { el.textContent += m + "\n"; el.scrollTop = el.scrollHeight; }
      } catch (_) {}
      console.log(m);
    };
    setPlayer(0);
    try {
      const t = await waitForNetplayClock(1200, 180000);
      log("SMK script: lockstep clock ok (" + t + "ms)");
    } catch (err) {
      log("SMK script: " + (err && err.message ? err.message : err) + " — continuing anyway");
    }
    log("SMK script: leave attract (PUSH B)");
    for (let i = 0; i < 14; i++)
      await holdNetplay("b", 1000);
    log("SMK script: select 2P Match Race");
    await holdNetplay("down", 800);
    await holdNetplay("b", 1000);
    log("SMK script: CC class");
    await holdNetplay("b", 1000);
    log("SMK script: P1 character");
    await holdNetplay("b", 1000);
    log("SMK script: wait for P2 + confirm course");
    const waitStart = readNetplayTimeMs();
    while (readNetplayTimeMs() - waitStart < 8000)
      await sleep(200);
    await holdNetplay("b", 1000);
    await holdNetplay("b", 1000);
    log("SMK script: random race inputs");
    const end = Date.now() + 90000;
    const dirs = ["left", "right", "b", "a", "y"];
    while (Date.now() < end) {
      const name = dirs[(Math.random() * dirs.length) | 0];
      await holdNetplay(name, 350 + ((Math.random() * 450) | 0));
    }
    releaseAll();
    log("SMK script: done");
  }

  /** Joiner: P1 pad names (remapped by seat); answer menus, then random inputs. */
  async function scriptMarioKartJoinMatchRace() {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    const log = (m) => {
      try {
        const el = document.getElementById("log");
        if (el) { el.textContent += m + "\n"; el.scrollTop = el.scrollHeight; }
      } catch (_) {}
      console.log(m);
    };
    setPlayer(0);
    try {
      const t = await waitForNetplayClock(1200, 180000);
      log("SMK join script: lockstep clock ok (" + t + "ms)");
    } catch (err) {
      log("SMK join script: " + (err && err.message ? err.message : err));
    }
    log("SMK join script: wait for host menus, then character/course (P1 controls → seat)");
    const t0 = readNetplayTimeMs();
    while (readNetplayTimeMs() - t0 < 18000)
      await sleep(200);
    for (let i = 0; i < 12; i++)
      await holdNetplay("b", 1000);
    log("SMK join script: random race inputs");
    const end = Date.now() + 90000;
    const dirs = ["left", "right", "b", "a", "y"];
    while (Date.now() < end) {
      const name = dirs[(Math.random() * dirs.length) | 0];
      await holdNetplay(name, 350 + ((Math.random() * 450) | 0));
    }
    releaseAll();
    log("SMK join script: done");
  }

  global.MamehubVirtualGamepad = {
    mount,
    showForPlay,
    prefersTouchUi,
    isMobile,
    releaseAll,
    setPlayer,
    tap,
    setPressed,
    openControls,
    openControlsPanel,
    closeControlsPanel,
    getBindings: () => Object.assign({}, bindings),
    readNetplayTimeMs,
    waitForNetplayClock,
    holdNetplay,
    scriptMarioKartMatchRace,
    scriptMarioKartJoinMatchRace,
    P1,
    P2
  };

  if (document.readyState === "loading")
    document.addEventListener("DOMContentLoaded", mount);
  else
    mount();
})(window);
