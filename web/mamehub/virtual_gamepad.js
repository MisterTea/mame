/**
 * On-screen SNES pad for touch devices. Drives MAME via sticky netplay force
 * inputs (preferred under -mamehub) and SDL scancodes when available.
 * Also: keyboard remaps (shell UI) and mobile default for the on-screen pad.
 */
(function (global) {
  function profileId() {
    try {
      return String((global.MAMEHUB_BROWSER || {}).id || "");
    } catch (_) {
      return "";
    }
  }

  function dirs(prefix) {
    return {
      up: "INPUT/0/" + prefix + "Up",
      down: "INPUT/0/" + prefix + "Down",
      left: "INPUT/0/" + prefix + "Left",
      right: "INPUT/0/" + prefix + "Right"
    };
  }

  /** P1 mamehub_id keys for the current shell profile. */
  function p1Maps() {
    const id = profileId();
    if (id === "arcade") {
      return Object.assign(dirs("P1 "), {
        b: "INPUT/0/P1 Button 1",
        a: "INPUT/0/P1 Button 2",
        y: "INPUT/0/P1 Button 3",
        x: "INPUT/0/P1 Button 4",
        l: "INPUT/0/P1 Button 5",
        r: "INPUT/0/P1 Button 6",
        start: "INPUT/0/1 Player Start",
        select: "INPUT/0/Coin 1"
      });
    }
    if (id === "gameboy") {
      return {
        up: "INPUT/0/Up",
        down: "INPUT/0/Down",
        left: "INPUT/0/Left",
        right: "INPUT/0/Right",
        b: "INPUT/0/Button B",
        a: "INPUT/0/Button A",
        y: "INPUT/0/Button B",
        x: "INPUT/0/Button A",
        l: "INPUT/0/Select",
        r: "INPUT/0/Start",
        start: "INPUT/0/Start",
        select: "INPUT/0/Select"
      };
    }
    if (id === "gba") {
      return Object.assign(dirs("P1 "), {
        b: "INPUT/0/B",
        a: "INPUT/0/A",
        y: "INPUT/0/B",
        x: "INPUT/0/A",
        l: "INPUT/0/P1 L",
        r: "INPUT/0/P1 R",
        start: "INPUT/0/P1 Start",
        select: "INPUT/0/P1 Select"
      });
    }
    if (id === "msx") {
      return Object.assign(dirs("P1 "), {
        b: "INPUT/0/P1 Button 1",
        a: "INPUT/0/P1 Button 2",
        y: "INPUT/0/P1 Button 1",
        x: "INPUT/0/P1 Button 2",
        l: "INPUT/0/P1 Button 1",
        r: "INPUT/0/P1 Button 2",
        start: "INPUT/0/P1 Button 1",
        select: "INPUT/0/P1 Button 2"
      });
    }
    if (id === "sms" || id === "gamegear") {
      return Object.assign(dirs("P1 "), {
        b: "INPUT/0/P1 Button 1",
        a: "INPUT/0/P1 Button 2",
        y: "INPUT/0/P1 Button 1",
        x: "INPUT/0/P1 Button 2",
        l: "INPUT/0/P1 Button 1",
        r: "INPUT/0/P1 Button 2",
        start: "INPUT/0/P1 Start",
        select: "INPUT/0/P1 Start"
      });
    }
    if (id === "pce") {
      return Object.assign(dirs("P1 "), {
        b: "INPUT/0/P1 Button II",
        a: "INPUT/0/P1 Button I",
        y: "INPUT/0/P1 Button II",
        x: "INPUT/0/P1 Button I",
        l: "INPUT/0/P1 Select",
        r: "INPUT/0/P1 Run",
        start: "INPUT/0/P1 Run",
        select: "INPUT/0/P1 Select"
      });
    }
    if (id === "genesis") {
      return Object.assign(dirs("P1 "), {
        b: "INPUT/0/P1 B",
        a: "INPUT/0/P1 C",
        y: "INPUT/0/P1 A",
        x: "INPUT/0/P1 X",
        l: "INPUT/0/P1 Y",
        r: "INPUT/0/P1 Z",
        start: "INPUT/0/P1 Start",
        select: "INPUT/0/P1 Mode"
      });
    }
    if (id === "a2600") {
      return Object.assign(dirs("P1 "), {
        b: "INPUT/0/P1 Button 1",
        a: "INPUT/0/P1 Button 1",
        y: "INPUT/0/P1 Button 1",
        x: "INPUT/0/P1 Button 1",
        l: "INPUT/0/Select Game",
        r: "INPUT/0/Reset Game",
        start: "INPUT/0/Reset Game",
        select: "INPUT/0/Select Game"
      });
    }
    // SNES / NES / default: face names match PORT_NAME("%p B") etc.
    return Object.assign(dirs("P1 "), {
      b: "INPUT/0/P1 B",
      y: "INPUT/0/P1 Y",
      a: "INPUT/0/P1 A",
      x: "INPUT/0/P1 X",
      l: "INPUT/0/P1 L",
      r: "INPUT/0/P1 R",
      start: "INPUT/0/P1 Start",
      select: "INPUT/0/P1 Select"
    });
  }

  const P1 = p1Maps();
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

  const FALLBACK_LABELS = {
    up: "Up", down: "Down", left: "Left", right: "Right",
    b: "B", y: "Y", a: "A", x: "X",
    l: "L", r: "R", start: "Start", select: "Select"
  };
  const FALLBACK_ORDER = [
    "up", "down", "left", "right",
    "b", "a", "y", "x",
    "l", "r", "select", "start"
  ];
  /** Fallback when a profile has no keyboard map (no meta keys). */
  const FALLBACK_BINDINGS = {
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
    KeyV: "select"
  };

  function keyboardConfig() {
    try {
      const kb = (global.MAMEHUB_BROWSER || {}).keyboard;
      if (kb && typeof kb === "object")
        return kb;
    } catch (_) { /* ignore */ }
    return {};
  }

  const ACTION_LABELS = (function () {
    const labels = keyboardConfig().labels;
    if (labels && typeof labels === "object")
      return labels;
    return FALLBACK_LABELS;
  })();
  const ACTION_ORDER = (function () {
    const order = keyboardConfig().order;
    if (Array.isArray(order) && order.length)
      return order.filter((action) => ACTION_LABELS[action]);
    return FALLBACK_ORDER.filter((action) => ACTION_LABELS[action]);
  })();
  const DEFAULT_BINDINGS = (function () {
    const bindings = keyboardConfig().bindings;
    if (bindings && typeof bindings === "object")
      return Object.assign({}, bindings);
    return Object.assign({}, FALLBACK_BINDINGS);
  })();
  const STORAGE_KEY = "mamehub.keyboardBindings.v2." + (profileId() || "default");

  const pressed = new Set();
  const pointerButtons = new Map();
  let playerMaps = p1Maps();
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
    playerMaps = p1Maps();
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
      try {
        if (typeof Module !== "undefined")
          Module.__mamehubPadPressCount = (Module.__mamehubPadPressCount | 0) + 1;
      } catch (_) { /* ignore */ }
    } else {
      if (!pressed.has(name))
        return;
      pressed.delete(name);
      force(name, false);
    }
    try {
      document.querySelectorAll('#virtual-gamepad [data-pad="' + name + '"]').forEach((el) => {
        el.classList.toggle("active", !!down);
      });
    } catch (_) { /* ignore */ }
    try {
      if (global.__mamehubDumpInputs && typeof global.__mamehubDumpInputLine === "function") {
        const held = [...pressed].sort().join(",") || "(none)";
        global.__mamehubDumpInputLine("pad " + name + (down ? " down" : " up") + " held=[" + held + "]");
      }
    } catch (_) { /* ignore */ }
  }

  function releaseAll() {
    pointerButtons.clear();
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
      pointerButtons.set(ev.pointerId, { name, el });
      el.classList.add("active");
      try { el.setPointerCapture(ev.pointerId); } catch (_) { /* ignore */ }
      setPressed(name, true);
    };
    const up = (ev) => {
      const held = pointerButtons.get(ev.pointerId);
      if (!held || held.name !== name)
        return;
      ev.preventDefault();
      ev.stopPropagation();
      pointerButtons.delete(ev.pointerId);
      el.classList.remove("active");
      setPressed(name, false);
    };

    el.addEventListener("pointerdown", down);
    el.addEventListener("pointerup", up);
    el.addEventListener("pointercancel", up);
    el.addEventListener("contextmenu", (ev) => ev.preventDefault());
  }

  function onGlobalPointerUp(ev) {
    const held = pointerButtons.get(ev.pointerId);
    if (!held)
      return;
    pointerButtons.delete(ev.pointerId);
    try { held.el.classList.remove("active"); } catch (_) { /* ignore */ }
    setPressed(held.name, false);
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
    window.addEventListener("pointerup", onGlobalPointerUp, true);
    window.addEventListener("pointercancel", onGlobalPointerUp, true);
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
    // Always reveal during play when asked — autoscript / tests need a visible pad.
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
    const needFrames = Math.max(8, Math.ceil((netplayMs || 800) / 16));
    const startSends = (typeof Module !== "undefined" && Module.__mamehubSendCount) | 0;
    // Cap wall wait so menu scripts stay snappy under fakelag / slow candy.
    const wallCap = Math.max(900, Math.min(4000, (netplayMs || 800) * 2));
    const deadline = Date.now() + wallCap;
    while (Date.now() < deadline) {
      const sends = (typeof Module !== "undefined" && Module.__mamehubSendCount) | 0;
      if (sends - startSends >= needFrames)
        break;
      await sleep(30);
    }
    setPressed(name, false);
    await sleep(60);
  }

  /**
   * Short ChronoMap tap for menus. holdNetplay() keeps the button down across
   * many send windows — on SMK that scrolls the mode list past 2P Match Race
   * (or wraps back to 1P) before confirm.
   */
  async function tapNetplay(name, frames, settleMs) {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    const need = Math.max(2, Math.min(6, frames == null ? 3 : frames | 0));
    setPressed(name, true);
    const startSends = (typeof Module !== "undefined" && Module.__mamehubSendCount) | 0;
    const deadline = Date.now() + 900;
    while (Date.now() < deadline) {
      const sends = (typeof Module !== "undefined" && Module.__mamehubSendCount) | 0;
      if (sends - startSends >= need)
        break;
      await sleep(16);
    }
    setPressed(name, false);
    await sleep(settleMs == null ? 280 : settleMs);
  }

  function scriptLog(prefix, m) {
    const line = prefix + m;
    try {
      const el = document.getElementById("log");
      if (el) {
        el.textContent += line + "\n";
        el.scrollTop = el.scrollHeight;
      }
    } catch (_) { /* ignore */ }
    console.log(line);
  }

  /** Both seats: hold accelerate (B) and weave — produces continuous dual-seat ChronoMap inputs. */
  async function driveAround(log, durationMs) {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    const end = Date.now() + (durationMs || 90000);
    log("driving (hold B + weave)");
    setPressed("b", true); // SMK accelerate
    let steerLeft = true;
    while (Date.now() < end) {
      setPressed(steerLeft ? "right" : "left", false);
      setPressed(steerLeft ? "left" : "right", true);
      // Pulse hop occasionally so input maps keep changing.
      if (((Date.now() / 900) | 0) % 5 === 0) {
        setPressed("y", true);
        await sleep(120);
        setPressed("y", false);
      }
      await sleep(280 + ((Math.random() * 220) | 0));
      steerLeft = !steerLeft;
    }
    releaseAll();
    log("driving done");
  }

  /**
   * Host / P1: Start → wait → Down (2P GAME) → B → Match Race menus → drive.
   *
   * Video analysis (/tmp/mamehub-smk-record): after Start the mode list is
   * "1P GAME / 2P GAME". Extra Start presses confirm 1P (seen entering
   * MARIOKART GP / TIME TRIAL). Do exactly one Start, wait for that menu,
   * one Down, then B to advance — never Start again on the mode list.
   */
  async function scriptMarioKartMatchRace() {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    const log = (m) => scriptLog("SMK script: ", m);
    const waitMenu = async (ms, why) => {
      log("wait " + ms + "ms — " + why);
      await sleep(ms);
    };
    setPlayer(0);
    applyPadVisibility(true);
    try {
      const t = await waitForNetplayClock(1200, 180000);
      log("lockstep clock ok (" + t + "ms)");
    } catch (err) {
      log((err && err.message ? err.message : err) + " — continuing anyway");
    }

    // Boot logos ignore Start; wait them out before the first press.
    await waitMenu(12000, "Nintendo / title logos before first Start");

    log("leave attract: single Start");
    await tapNetplay("start", 4, 200);
    await waitMenu(8000, "1P GAME / 2P GAME menu");

    log("2P GAME: single Down");
    await tapNetplay("down", 3, 200);
    await waitMenu(2500, "cursor on 2P GAME");

    log("advance: B (confirm 2P GAME)");
    await tapNetplay("b", 4, 200);
    await waitMenu(5000, "GP / MATCH RACE / BATTLE menu");

    // After 2P GAME the next list defaults to MARIO KART GP — one Down → MATCH RACE.
    log("MATCH RACE: single Down");
    await tapNetplay("down", 3, 200);
    await waitMenu(2500, "cursor on MATCH RACE");

    log("confirm Match Race");
    await tapNetplay("b", 4, 200);
    await waitMenu(5000, "CC class");

    log("CC class confirm");
    await tapNetplay("b", 4, 200);
    await waitMenu(5000, "P1 character select");

    log("P1 character confirm");
    await tapNetplay("b", 4, 200);
    await waitMenu(14000, "P2 character / course");

    log("confirm course / start race");
    await tapNetplay("b", 4, 300);
    await waitMenu(2000, "after course confirm");
    await tapNetplay("b", 4, 300);
    await waitMenu(8000, "race countdown");

    log("random race inputs");
    await driveAround(log, 90000);
    log("done");
  }

  /** Joiner / P2: idle until host reaches 2P character select, then confirm. */
  async function scriptMarioKartJoinMatchRace() {
    const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
    const log = (m) => scriptLog("SMK join script: ", m);
    setPlayer(0);
    applyPadVisibility(true);
    try {
      const t = await waitForNetplayClock(1200, 180000);
      log("lockstep clock ok (" + t + "ms)");
    } catch (err) {
      log(err && err.message ? err.message : err);
    }
    log("wait for host 2P menus (~45s wall)");
    await sleep(45000);
    log("P2 character / confirm");
    for (let i = 0; i < 8; i++) {
      await tapNetplay("b", 4, 400);
      await sleep(1500);
    }
    await sleep(5000);
    log("random race inputs");
    await driveAround(log, 90000);
    log("done");
  }

  global.MamehubVirtualGamepad = {
    mount,
    showForPlay,
    applyPadVisibility,
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
    tapNetplay,
    driveAround,
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
