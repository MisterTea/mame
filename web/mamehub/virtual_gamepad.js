/**
 * On-screen SNES pad for touch devices. Synthesizes keyboard events that match
 * MAME default bindings for snes_joypad (see inpttype.ipp + joypad.cpp).
 */
(function (global) {
  // SNES → MAME defaults: B=Alt Y=Ctrl A=Space X=Shift L=Z R=X Start=1 Select=5
  const BUTTONS = {
    up:     { key: "ArrowUp",    code: "ArrowUp",    keyCode: 38 },
    down:   { key: "ArrowDown",  code: "ArrowDown",  keyCode: 40 },
    left:   { key: "ArrowLeft",  code: "ArrowLeft",  keyCode: 37 },
    right:  { key: "ArrowRight", code: "ArrowRight", keyCode: 39 },
    b:      { key: "Alt",        code: "AltLeft",    keyCode: 18 },
    y:      { key: "Control",    code: "ControlLeft",keyCode: 17 },
    a:      { key: " ",          code: "Space",      keyCode: 32 },
    x:      { key: "Shift",      code: "ShiftLeft",  keyCode: 16 },
    l:      { key: "z",          code: "KeyZ",       keyCode: 90 },
    r:      { key: "x",          code: "KeyX",       keyCode: 88 },
    start:  { key: "1",          code: "Digit1",     keyCode: 49 },
    select: { key: "5",          code: "Digit5",     keyCode: 53 }
  };

  const pressed = new Set();

  function prefersTouchUi() {
    try {
      if (window.matchMedia("(pointer: coarse)").matches)
        return true;
      if (window.matchMedia("(hover: none)").matches)
        return true;
    } catch (_) { /* ignore */ }
    if ((navigator.maxTouchPoints || 0) > 0)
      return true;
    if ("ontouchstart" in window)
      return true;
    return false;
  }

  function shouldShowByDefault() {
    // Prefer showing only when a touchscreen-ish input is available.
    // If detection is inconclusive, show anyway (mobile-first shell).
    if (prefersTouchUi())
      return true;
    try {
      const fine = window.matchMedia("(pointer: fine)").matches;
      const coarse = window.matchMedia("(pointer: coarse)").matches;
      if (fine && !coarse && (navigator.maxTouchPoints || 0) === 0)
        return false;
    } catch (_) { /* ignore */ }
    return true;
  }

  function dispatchKey(spec, down) {
    const type = down ? "keydown" : "keyup";
    const init = {
      key: spec.key,
      code: spec.code,
      keyCode: spec.keyCode,
      which: spec.keyCode,
      bubbles: true,
      cancelable: true,
      view: window
    };
    const targets = [];
    const canvas = document.getElementById("canvas");
    if (canvas)
      targets.push(canvas);
    targets.push(document, window);
    for (const t of targets) {
      try {
        t.dispatchEvent(new KeyboardEvent(type, init));
      } catch (_) { /* ignore */ }
    }
    // Prefer SDL's direct path when the wasm runtime has exported it.
    try {
      if (global.JSMAME && typeof global.JSMAME.sdl_sendkeyboardkey === "function") {
        // SDL_SendKeyboardKey(state, scancode) — approximate via common scancodes
        const scancodes = {
          ArrowUp: 82, ArrowDown: 81, ArrowLeft: 80, ArrowRight: 79,
          AltLeft: 226, ControlLeft: 224, Space: 44, ShiftLeft: 225,
          KeyZ: 29, KeyX: 27, Digit1: 30, Digit5: 34
        };
        const sc = scancodes[spec.code];
        if (sc != null)
          global.JSMAME.sdl_sendkeyboardkey(down ? 1 : 0, sc);
      }
    } catch (_) { /* ignore */ }
  }

  function setPressed(name, down) {
    const spec = BUTTONS[name];
    if (!spec)
      return;
    if (down) {
      if (pressed.has(name))
        return;
      pressed.add(name);
      dispatchKey(spec, true);
    } else {
      if (!pressed.has(name))
        return;
      pressed.delete(name);
      dispatchKey(spec, false);
    }
  }

  function releaseAll() {
    for (const name of [...pressed])
      setPressed(name, false);
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
    // Avoid generating synthetic mouse clicks after touch.
    el.addEventListener("contextmenu", (ev) => ev.preventDefault());
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
    root.hidden = !show;
    root.setAttribute("aria-hidden", show ? "false" : "true");
    if (toggle) {
      toggle.setAttribute("aria-pressed", show ? "true" : "false");
      toggle.onclick = () => {
        const next = root.hidden;
        root.hidden = !next;
        root.setAttribute("aria-hidden", next ? "false" : "true");
        toggle.setAttribute("aria-pressed", next ? "true" : "false");
        if (!next)
          releaseAll();
      };
    }
  }

  function showForPlay() {
    const root = document.getElementById("virtual-gamepad");
    if (!root)
      return;
    // On play, ensure touch users see the pad even if they toggled shell chrome.
    if (prefersTouchUi() || shouldShowByDefault()) {
      root.hidden = false;
      root.setAttribute("aria-hidden", "false");
      const toggle = document.getElementById("gamepadToggleBtn");
      if (toggle)
        toggle.setAttribute("aria-pressed", "true");
    }
  }

  global.MamehubVirtualGamepad = {
    mount,
    showForPlay,
    prefersTouchUi,
    releaseAll
  };

  if (document.readyState === "loading")
    document.addEventListener("DOMContentLoaded", mount);
  else
    mount();
})(typeof window !== "undefined" ? window : globalThis);
