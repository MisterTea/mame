(function () {
  const logEl = document.getElementById("log");
  const log = (msg) => {
    const s = String(msg || "");
    // High-frequency INPUT_DUMP: keep a ring buffer for tests; skip DOM append
    // (append+scroll starved emscripten catch-up under dumpinputs=1).
    if (/^INPUT_DUMP\b/.test(s)) {
      try {
        const buf = (window.__mamehubDumpLines = window.__mamehubDumpLines || []);
        buf.push(s);
        if (buf.length > 2500)
          buf.splice(0, buf.length - 2500);
        // Sticky evidence — ring buffer alone drops early Start/B under long play.
        const sticky = (window.__mamehubDumpSticky = window.__mamehubDumpSticky || {
          chronoStart: false, chronoB: false, txB: false, padB: false, padStart: false, padSteer: false
        });
        if (/cpp chronomap .*P1 Start=\{p0:1/i.test(s) || /cpp send .*P1 Start=1/i.test(s))
          sticky.chronoStart = true;
        if (/cpp chronomap .*P[12] B=\{p[01]:1/i.test(s) || /cpp send .*P[12] B=1/i.test(s))
          sticky.chronoB = true;
        if (/INPUT_DUMP tx .*P[12] B=1/i.test(s))
          sticky.txB = true;
        if (/INPUT_DUMP pad b down/i.test(s))
          sticky.padB = true;
        if (/INPUT_DUMP pad start down/i.test(s))
          sticky.padStart = true;
        if (/INPUT_DUMP pad (left|right) down/i.test(s))
          sticky.padSteer = true;
      } catch (_) { /* ignore */ }
      console.log(s);
      return;
    }
    logEl.textContent += s + "\n";
    logEl.scrollTop = logEl.scrollHeight;
    console.log(s);
  };

  const qs = new URLSearchParams(window.location.search || "");
  function queryPreferredGame() {
    return (qs.get("game") || qs.get("machine") || qs.get("soft") || qs.get("software") || "").trim();
  }
  const dumpInputsRequested = (() => {
    const v = (qs.get("dumpinputs") || qs.get("dump_inputs") || "").toLowerCase();
    return v === "1" || v === "true" || v === "yes" || v === "on";
  })();
  if (dumpInputsRequested) {
    window.__mamehubDumpInputs = true;
    log("INPUT_DUMP enabled (?dumpinputs=1) — logging force/ChronoMap input changes");
  }

  function dumpInputLine(msg) {
    if (!window.__mamehubDumpInputs)
      return;
    const line = "INPUT_DUMP " + msg;
    try {
      const buf = (window.__mamehubDumpLines = window.__mamehubDumpLines || []);
      buf.push(line);
      if (buf.length > 2500)
        buf.splice(0, buf.length - 2500);
      const sticky = (window.__mamehubDumpSticky = window.__mamehubDumpSticky || {
        chronoStart: false, chronoB: false, txB: false, padB: false, padStart: false, padSteer: false
      });
      if (/pad b down/i.test(line)) sticky.padB = true;
      if (/pad start down/i.test(line)) sticky.padStart = true;
      if (/pad (left|right) down/i.test(line)) sticky.padSteer = true;
      if (/tx .*P[12] B=1/i.test(line)) sticky.txB = true;
    } catch (_) { /* ignore */ }
    console.log(line);
  }
  window.__mamehubDumpInputLine = dumpInputLine;

  function installForceInputDumpHook() {
    if (!dumpInputsRequested || !window.JSMAME || typeof window.JSMAME.force_input !== "function")
      return;
    if (window.JSMAME.__mamehubDumpWrapped)
      return;
    window.JSMAME.__mamehubDumpWrapped = true;
    const orig = window.JSMAME.force_input.bind(window.JSMAME);
    const lastByKey = Object.create(null);
    window.JSMAME.force_input = (key, value) => {
      const k = String(key);
      const v = value ? String(value) : "";
      if (lastByKey[k] !== v) {
        lastByKey[k] = v;
        dumpInputLine("force " + (v ? (k + "=" + v) : (k + " clear")));
      }
      return orig(key, value);
    };
  }

  const showFpsRequested = (() => {
    const v = (qs.get("showfps") || qs.get("fps") || "1").toLowerCase();
    return v !== "0" && v !== "false" && v !== "off" && v !== "no";
  })();

  function installFpsHud() {
    const hud = document.getElementById("fps-hud");
    if (!hud || hud.dataset.mounted === "1")
      return;
    hud.dataset.mounted = "1";
    let presents = 0;
    let lastMs = performance.now();
    let lastCount = 0;
    let presentFps = 0;
    const bump = () => { presents++; };
    window.__mamehubOnPresent = bump;
    const hookGl = () => {
      const ov = document.getElementById("mame-webgl");
      if (!ov || ov._fpsHooked)
        return;
      const gl = ov._gl;
      if (!gl || typeof gl.drawArrays !== "function")
        return;
      ov._fpsHooked = true;
      const orig = gl.drawArrays.bind(gl);
      gl.drawArrays = function () {
        bump();
        return orig.apply(this, arguments);
      };
    };
    const enableMameFps = () => {
      try {
        const M = window.Module;
        if (!M)
          return;
        const getUi = (window.JSMAME && typeof JSMAME.get_ui === "function")
          ? JSMAME.get_ui
          : M.__ZN15running_machine17emscripten_get_uiEv;
        const setFps = (window.JSMAME && typeof JSMAME.ui_set_show_fps === "function")
          ? JSMAME.ui_set_show_fps
          : M.__ZN15mame_ui_manager12set_show_fpsEb;
        if (typeof getUi !== "function" || typeof setFps !== "function")
          return;
        const ui = getUi();
        if (!ui)
          return;
        setFps(ui, 1);
      } catch (_) { /* UI not ready yet */ }
    };
    const tick = () => {
      hookGl();
      if (window.Module)
        Module._mamehubOnPresent = bump;
      if (showFpsRequested)
        enableMameFps();
      const now = performance.now();
      const dt = now - lastMs;
      if (dt >= 500) {
        presentFps = (presents - lastCount) * 1000 / dt;
        lastCount = presents;
        lastMs = now;
        window.__mamehubHudFps = presentFps;
        window.__mamehubPresentCount = presents;
        if (showFpsRequested) {
          hud.hidden = false;
          hud.style.display = "block";
          hud.textContent = presentFps.toFixed(1) + " FPS";
        }
      }
    };
    setInterval(tick, 250);
    if (showFpsRequested) {
      hud.hidden = false;
      hud.style.display = "block";
      hud.textContent = "FPS …";
    }
  }
  installFpsHud();

  const muteRequested = (() => {
    const mute = (qs.get("mute") || "").toLowerCase();
    if (mute === "1" || mute === "true" || mute === "yes" || mute === "on")
      return true;
    if (qs.has("nosound") || qs.get("sound") === "0" || qs.get("sound") === "none")
      return true;
    const vol = qs.get("volume");
    return vol !== null && vol !== "";
  })();

  // iOS Safari starts AudioContext as "suspended" unless resume() runs inside a
  // user gesture. MAME's js_sound creates the context when the first samples
  // arrive (long after Start/Play), so sound stays silent. Share one context
  // created/resumed on tap; js_sound's `new AudioContext()` then reuses it.
  const iosLike = /iPad|iPhone|iPod/.test(navigator.userAgent || "") ||
    (navigator.platform === "MacIntel" && (navigator.maxTouchPoints || 0) > 1);
  const silentWav = "data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAAABkYXRhAgAAAAEA";
  function nativeAudioContext() {
    const cur = window.AudioContext || window.webkitAudioContext;
    return (cur && cur.__mamehubOrig) || cur || null;
  }
  function wrapAudioContextCtor() {
    const Orig = nativeAudioContext();
    if (!Orig || Orig.__mamehubWrapped)
      return;
    function Wrapped(opts) {
      if (window.__mamehubAudioContext)
        return window.__mamehubAudioContext;
      const ctx = opts !== undefined ? new Orig(opts) : new Orig();
      window.__mamehubAudioContext = ctx;
      return ctx;
    }
    Wrapped.prototype = Orig.prototype;
    Wrapped.__mamehubOrig = Orig;
    Wrapped.__mamehubWrapped = true;
    if (window.AudioContext)
      window.AudioContext = Wrapped;
    if (window.webkitAudioContext)
      window.webkitAudioContext = Wrapped;
  }
  function hideAudioUnlockBanner() {
    const el = document.getElementById("audio-unlock-banner");
    if (el)
      el.hidden = true;
  }
  function showAudioUnlockBanner() {
    if (muteRequested || !iosLike)
      return;
    let el = document.getElementById("audio-unlock-banner");
    if (!el) {
      el = document.createElement("button");
      el.id = "audio-unlock-banner";
      el.type = "button";
      el.textContent = "Tap to enable sound";
      el.setAttribute("aria-label", "Tap to enable sound");
      el.style.cssText = [
        "position:fixed", "left:50%", "bottom:1.1rem", "transform:translateX(-50%)",
        "z-index:40", "border:0", "border-radius:999px", "padding:0.65rem 1.1rem",
        "font:inherit", "color:#fff", "background:#3d9a6a", "box-shadow:0 4px 16px #0008",
        "cursor:pointer"
      ].join(";");
      el.addEventListener("click", () => unlockWebAudio());
      document.body.appendChild(el);
    }
    el.hidden = false;
  }
  function unlockWebAudio() {
    if (muteRequested)
      return;
    wrapAudioContextCtor();
    const Orig = nativeAudioContext();
    if (!Orig)
      return;
    try {
      if (!window.__mamehubAudioContext)
        window.__mamehubAudioContext = new Orig();
      const ctx = window.__mamehubAudioContext;
      if (ctx.state === "suspended")
        ctx.resume();
      const buf = ctx.createBuffer(1, 1, ctx.sampleRate || 22050);
      const src = ctx.createBufferSource();
      src.buffer = buf;
      src.connect(ctx.destination);
      src.start(0);
      if (!window.__mamehubSilentAudio) {
        const a = new Audio(silentWav);
        a.setAttribute("playsinline", "true");
        a.loop = false;
        window.__mamehubSilentAudio = a;
      }
      window.__mamehubSilentAudio.play().catch(() => {});
      const done = () => {
        if (ctx.state === "running")
          hideAudioUnlockBanner();
      };
      if (ctx.state === "running")
        done();
      else if (ctx.resume)
        ctx.resume().then(done).catch(() => {});
    } catch (_) { /* ignore */ }
  }
  wrapAudioContextCtor();
  if (!muteRequested) {
    ["pointerdown", "touchstart", "click", "keydown"].forEach((ev) => {
      document.addEventListener(ev, unlockWebAudio, { capture: true, passive: true });
    });
    document.addEventListener("visibilitychange", () => {
      if (document.visibilityState === "visible")
        unlockWebAudio();
    });
    if (iosLike)
      showAudioUnlockBanner();
  }
  window.__mamehubUnlockAudio = unlockWebAudio;
  const volumeDb = (() => {
    const vol = qs.get("volume");
    if (vol !== null && vol !== "")
      return String(parseInt(vol, 10) || 0);
    return muteRequested ? "-96" : null;
  })();
  const autoscriptRequested = (() => {
    const v = (qs.get("autoscript") || "").toLowerCase();
    if (v === "0" || v === "false" || v === "off" || v === "no")
      return false;
    if (v === "1" || v === "true" || v === "yes" || v === "on" || v === "smk")
      return true;
    // Default on for muted automated netplay tests.
    return muteRequested;
  })();
  // Browser fakelag: native -fake_lag only affects WGA. Here we delay DataChannel I/O.
  // ?fakelag=1 → 100ms one-way (≈200ms RTT). ?lag=N overrides ms. ?lagjitter=N ?lagdrop=0.01
  const fakeLagConfig = (() => {
    const flag = (qs.get("fakelag") || "").toLowerCase();
    const lagQ = qs.get("lag");
    let ms = 0;
    if (lagQ !== null && lagQ !== "")
      ms = Math.max(0, parseInt(lagQ, 10) || 0);
    else if (flag === "1" || flag === "true" || flag === "yes" || flag === "on")
      ms = 100;
    const jitter = Math.max(0, parseInt(qs.get("lagjitter") || "0", 10) || 0);
    let drop = parseFloat(qs.get("lagdrop") || "0");
    if (!isFinite(drop) || drop < 0)
      drop = 0;
    if (drop > 1)
      drop = drop > 100 ? 0.01 : drop / 100;
    // Do not auto-enable drops with fakelag=1 — losing start-barrier puts deadlocks.
    return { ms, jitterMs: jitter, drop, enabled: ms > 0 || drop > 0 };
  })();

  function maybeStartInputScript(role) {
    if (!autoscriptRequested)
      return;
    const pad = window.MamehubVirtualGamepad;
    if (!pad)
      return;
    const run = role === "join"
      ? pad.scriptMarioKartJoinMatchRace
      : pad.scriptMarioKartMatchRace;
    if (typeof run !== "function")
      return;
    const sys = (window.MAMEHUB_BROWSER || {}).id || "";
    if (sys && sys !== "snes")
      return;
    // Show on-screen pad during autoscript so headed Playwright runs are visible
    // (inputs are force_input, not OS key events).
    if (typeof pad.applyPadVisibility === "function")
      pad.applyPadVisibility(true);
    else if (typeof pad.showForPlay === "function")
      pad.showForPlay();
    // Defer until lockstep clock is live (candy + barriers). Scripts wait internally too.
    log("Autoscript: armed Mario Kart " + (role === "join" ? "joiner" : "host") + " Match Race script");
    const kick = () => {
      Promise.resolve()
        .then(() => run.call(pad))
        .catch((err) => log("Autoscript error: " + (err && err.message ? err.message : err)));
    };
    // Give WASM a moment to finish soft_reset → startNetplayClock barriers.
    setTimeout(kick, 1500);
  }

  const cfg0 = window.MAMEHUB_BROWSER || {};
  const isArcade = cfg0.mode === "arcade";
  const gameTag = cfg0.id || (isArcade ? "arcade" : "snes");
  const pickNoun = cfg0.noun || (isArcade ? "arcade machine" : "software");
  const brandTitle = cfg0.title || (isArcade ? "Arcade" : "SNES");

  function applyShellBranding() {
    document.title = "MAMEHub Online (" + brandTitle + ")";
    const h1 = document.querySelector("header h1");
    if (h1)
      h1.textContent = "MAMEHub Online (" + brandTitle + ")";
    const placeholder = document.getElementById("canvas-placeholder");
    if (placeholder)
      placeholder.textContent = "Start offline → pick " + pickNoun + " → play.";
    const input = document.getElementById("softwareAcInput");
    if (input) {
      input.placeholder = "Search " + pickNoun + "…";
      input.setAttribute("aria-label", pickNoun);
    }
    const pickerTitle = document.getElementById("softwarePickerTitle");
    if (pickerTitle)
      pickerTitle.textContent = "Offline — Select " + pickNoun;
    const pad = document.getElementById("virtual-gamepad");
    if (pad)
      pad.setAttribute("aria-label", "On-screen " + brandTitle + " gamepad");
    const layoutImg = document.getElementById("keyboardLayoutImg");
    if (layoutImg) {
      layoutImg.src = cfg0.keyboardLayout || ("/layouts/" + gameTag + ".png");
      layoutImg.alt = brandTitle + " keyboard controls";
    }
    const layoutCap = document.getElementById("keyboardLayoutCap");
    if (layoutCap)
      layoutCap.textContent = brandTitle + " — Player 1 keys. Green keys are active. Controls can remap them.";
    const canvas = document.getElementById("canvas");
    if (canvas) {
      const cw = (cfg0.canvasWidth | 0) || (isArcade ? 1152 : 512);
      const ch = (cfg0.canvasHeight | 0) || 448;
      canvas.width = cw;
      canvas.height = ch;
    }
  }
  applyShellBranding();

  const NVRAM_STORAGE_PREFIX = "mamehub.nvram.v1.";
  const NVRAM_FS_ROOT = "/nvram";

  function nvramStorageKey(software) {
    return NVRAM_STORAGE_PREFIX + gameTag + "." + String(software || "").trim();
  }

  function nvramU8ToB64(u8) {
    let s = "";
    const chunk = 0x8000;
    for (let i = 0; i < u8.length; i += chunk)
      s += String.fromCharCode.apply(null, u8.subarray(i, i + chunk));
    return btoa(s);
  }

  function nvramB64ToU8(b64) {
    const s = atob(b64);
    const u8 = new Uint8Array(s.length);
    for (let i = 0; i < s.length; i++)
      u8[i] = s.charCodeAt(i);
    return u8;
  }

  function restoreNvramFromStorage(software) {
    try {
      if (typeof FS === "undefined" || !FS.mkdirTree)
        return 0;
      FS.mkdirTree(NVRAM_FS_ROOT);
      const raw = localStorage.getItem(nvramStorageKey(software));
      if (!raw)
        return 0;
      const parsed = JSON.parse(raw);
      const files = parsed && parsed.files;
      if (!files || typeof files !== "object")
        return 0;
      let n = 0;
      for (const [rel, b64] of Object.entries(files)) {
        if (typeof rel !== "string" || typeof b64 !== "string")
          continue;
        if (!rel || rel.includes("..") || rel.startsWith("/"))
          continue;
        const path = NVRAM_FS_ROOT + "/" + rel;
        const slash = path.lastIndexOf("/");
        if (slash > 0)
          FS.mkdirTree(path.slice(0, slash));
        FS.writeFile(path, nvramB64ToU8(b64));
        n++;
      }
      return n;
    } catch (err) {
      console.warn("NVRAM restore:", err);
      return 0;
    }
  }

  function collectNvramFiles() {
    const files = {};
    if (typeof FS === "undefined")
      return files;
    let exists = false;
    try {
      exists = FS.analyzePath(NVRAM_FS_ROOT).exists;
    } catch (_) {
      return files;
    }
    if (!exists)
      return files;
    const walk = (dir) => {
      let names;
      try {
        names = FS.readdir(dir);
      } catch (_) {
        return;
      }
      for (const name of names) {
        if (name === "." || name === "..")
          continue;
        const p = dir + "/" + name;
        let st;
        try {
          st = FS.stat(p);
        } catch (_) {
          continue;
        }
        if (FS.isDir(st.mode))
          walk(p);
        else if (FS.isFile(st.mode)) {
          const rel = p.slice(NVRAM_FS_ROOT.length + 1);
          try {
            files[rel] = nvramU8ToB64(FS.readFile(p, { encoding: "binary" }));
          } catch (_) { /* skip */ }
        }
      }
    };
    walk(NVRAM_FS_ROOT);
    return files;
  }

  function persistNvramToStorage() {
    const mod = window.Module;
    if (!mod || mod.__mamehubNvramOffline === false)
      return false;
    const software = mod.__mamehubNvramSoftware;
    if (!software)
      return false;
    try {
      const files = collectNvramFiles();
      if (!Object.keys(files).length)
        return false;
      const payload = JSON.stringify({
        v: 1,
        software: software,
        savedAt: Date.now(),
        files: files
      });
      mod.__mamehubNvramPayload = payload;
      localStorage.setItem(nvramStorageKey(software), payload);
      return true;
    } catch (err) {
      console.warn("NVRAM persist:", err);
      return false;
    }
  }

  function persistNvramOnPageExit() {
    try {
      const mod = window.Module;
      if (!mod || mod.__mamehubNvramOffline === false)
        return;
      const software = mod.__mamehubNvramSoftware;
      const payload = mod.__mamehubNvramPayload;
      if (software && payload)
        localStorage.setItem(nvramStorageKey(software), payload);
      else
        persistNvramToStorage();
    } catch (_) { /* ignore quota / private mode */ }
  }

  window.addEventListener("pagehide", persistNvramOnPageExit);
  window.addEventListener("beforeunload", persistNvramOnPageExit);
  document.addEventListener("visibilitychange", () => {
    if (document.visibilityState === "hidden")
      persistNvramToStorage();
  });

  const STATE_STORAGE_PREFIX = "mamehub.state.v1.";
  const STATE_FS_ROOT = "/sta";
  const STATE_SLOT = "1";
  let currentPlaySoftware = "";

  function saveStateStorageKey(software) {
    return STATE_STORAGE_PREFIX + gameTag + "." + String(software || "").trim();
  }

  function collectFsTree(root) {
    const files = {};
    if (typeof FS === "undefined")
      return files;
    let exists = false;
    try {
      exists = FS.analyzePath(root).exists;
    } catch (_) {
      return files;
    }
    if (!exists)
      return files;
    const walk = (dir) => {
      let names;
      try {
        names = FS.readdir(dir);
      } catch (_) {
        return;
      }
      for (const name of names) {
        if (name === "." || name === "..")
          continue;
        const p = dir + "/" + name;
        let st;
        try {
          st = FS.stat(p);
        } catch (_) {
          continue;
        }
        if (FS.isDir(st.mode))
          walk(p);
        else if (FS.isFile(st.mode)) {
          const rel = p.slice(root.length + 1);
          try {
            files[rel] = nvramU8ToB64(FS.readFile(p, { encoding: "binary" }));
          } catch (_) { /* skip */ }
        }
      }
    };
    walk(root);
    return files;
  }

  function restoreFsTree(root, files) {
    if (typeof FS === "undefined" || !FS.mkdirTree || !files)
      return 0;
    FS.mkdirTree(root);
    let n = 0;
    for (const [rel, b64] of Object.entries(files)) {
      if (typeof rel !== "string" || typeof b64 !== "string")
        continue;
      if (!rel || rel.includes("..") || rel.startsWith("/"))
        continue;
      const path = root + "/" + rel;
      const slash = path.lastIndexOf("/");
      if (slash > 0)
        FS.mkdirTree(path.slice(0, slash));
      FS.writeFile(path, nvramB64ToU8(b64));
      n++;
    }
    return n;
  }

  function persistSaveStateToStorage() {
    const software = currentPlaySoftware || (window.Module && Module.__mamehubNvramSoftware) || "";
    if (!software)
      return false;
    try {
      const files = collectFsTree(STATE_FS_ROOT);
      if (!Object.keys(files).length)
        return false;
      localStorage.setItem(saveStateStorageKey(software), JSON.stringify({
        v: 1,
        software: software,
        slot: STATE_SLOT,
        savedAt: Date.now(),
        files: files
      }));
      updateSaveStateButtons();
      return true;
    } catch (err) {
      const quota = err && (err.name === "QuotaExceededError" || err.code === 22);
      log(quota
        ? "Save state is too large for browser storage"
        : ("Save state storage failed: " + (err && err.message ? err.message : err)));
      console.warn("Save state persist:", err);
      return false;
    }
  }

  function restoreSaveStateFromStorage(software) {
    try {
      const raw = localStorage.getItem(saveStateStorageKey(software));
      if (!raw)
        return 0;
      const parsed = JSON.parse(raw);
      const files = parsed && parsed.files;
      if (!files || typeof files !== "object")
        return 0;
      return restoreFsTree(STATE_FS_ROOT, files);
    } catch (err) {
      console.warn("Save state restore:", err);
      return 0;
    }
  }

  function hasStoredSaveState(software) {
    try {
      return !!(software && localStorage.getItem(saveStateStorageKey(software)));
    } catch (_) {
      return false;
    }
  }

  function emulatorSaveLoadReady() {
    return typeof Module !== "undefined" && !!Module.calledRun;
  }

  function updateSaveStateButtons() {
    const saveBtn = document.getElementById("saveStateBtn");
    const loadBtn = document.getElementById("loadStateBtn");
    const ready = emulatorSaveLoadReady();
    if (saveBtn)
      saveBtn.disabled = !ready;
    if (loadBtn)
      loadBtn.disabled = !ready || !hasStoredSaveState(currentPlaySoftware);
  }

  function requestSaveState() {
    if (!emulatorSaveLoadReady())
      return log("Start a game before saving state");
    log("Saving state…");
    window.Module.__mamehubWantSave = 1;
  }

  function requestLoadState() {
    if (!emulatorSaveLoadReady())
      return log("Start a game before loading state");
    const software = currentPlaySoftware;
    if (!hasStoredSaveState(software))
      return log("No save state for this game");
    const n = restoreSaveStateFromStorage(software);
    if (!n)
      return log("Failed to restore save state from browser storage");
    log("Loading state…");
    window.Module.__mamehubWantLoad = 1;
  }

  const saveStateBtn = document.getElementById("saveStateBtn");
  const loadStateBtn = document.getElementById("loadStateBtn");
  if (saveStateBtn)
    saveStateBtn.onclick = () => requestSaveState();
  if (loadStateBtn)
    loadStateBtn.onclick = () => requestLoadState();
  updateSaveStateButtons();

  function fullscreenElement() {
    return document.fullscreenElement || document.webkitFullscreenElement || null;
  }

  function nativeCanvasSize() {
    const cfg = window.MAMEHUB_BROWSER || {};
    return {
      w: (cfg.canvasWidth | 0) || (isArcade ? 1152 : 512),
      h: (cfg.canvasHeight | 0) || 448
    };
  }

  function pinCanvasCssBox(canvas) {
    if (!canvas || canvas.__mamehubCssPin)
      return;
    canvas.__mamehubCssPin = true;
    const proto = HTMLElement.prototype.getBoundingClientRect;
    canvas.getBoundingClientRect = function () {
      const r = proto.call(this);
      return new DOMRect(r.left, r.top, this.clientWidth, this.clientHeight);
    };
  }

  function guardCanvasBufferSize(canvas) {
    if (!canvas || canvas.__mamehubBufGuard)
      return;
    canvas.__mamehubBufGuard = true;
    const protoW = Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype, "width");
    const protoH = Object.getOwnPropertyDescriptor(HTMLCanvasElement.prototype, "height");
    if (!protoW || !protoH || !protoW.get || !protoH.get)
      return;
    Object.defineProperty(canvas, "width", {
      configurable: true,
      get() { return protoW.get.call(this); },
      set(v) {
        const want = isGameFullscreen() ? nativeCanvasSize().w : (v | 0);
        if (protoW.get.call(this) === want)
          return;
        protoW.set.call(this, want);
      }
    });
    Object.defineProperty(canvas, "height", {
      configurable: true,
      get() { return protoH.get.call(this); },
      set(v) {
        const want = isGameFullscreen() ? nativeCanvasSize().h : (v | 0);
        if (protoH.get.call(this) === want)
          return;
        protoH.set.call(this, want);
      }
    });
  }

  function exitDocumentFullscreen() {
    const fn = document.exitFullscreen || document.webkitExitFullscreen || document.webkitCancelFullScreen;
    if (!fn)
      return Promise.resolve();
    try {
      const result = fn.call(document);
      return result && typeof result.then === "function" ? result : Promise.resolve();
    } catch (err) {
      return Promise.reject(err);
    }
  }

  function isFallbackFullscreen() {
    const wrap = document.getElementById("canvas-wrap");
    return !!(wrap && wrap.classList.contains("is-fallback-fullscreen"));
  }

  function isGameFullscreen() {
    const fs = fullscreenElement();
    return fs === document.documentElement || isFallbackFullscreen();
  }

  function restoreCanvasLayout() {
    const canvas = document.getElementById("canvas");
    if (!canvas)
      return;
    const cw = nativeCanvasSize().w;
    canvas.style.transform = "";
    canvas.style.transformOrigin = "";
    canvas.style.width = "";
    canvas.style.height = "";
    canvas.style.maxWidth = cw + "px";
    canvas.style.maxHeight = "";
  }

  function fitCanvasInFullscreen() {
    const wrap = document.getElementById("canvas-wrap");
    const canvas = document.getElementById("canvas");
    if (!wrap || !canvas || !isGameFullscreen())
      return;
    // Pin the CSS box to the GL backbuffer. Scaling is transform-only so
    // orientation/resize cannot change clientWidth and freeze WebGL.
    const native = nativeCanvasSize();
    canvas.style.width = native.w + "px";
    canvas.style.height = native.h + "px";
    canvas.style.maxWidth = "none";
    canvas.style.maxHeight = "none";
    const scale = Math.min(wrap.clientWidth / native.w, wrap.clientHeight / native.h);
    canvas.style.transformOrigin = "center center";
    canvas.style.transform = "scale(" + scale + ")";
  }

  let fullscreenFitObserver = null;

  function startFullscreenFit() {
    const wrap = document.getElementById("canvas-wrap");
    if (!wrap)
      return;
    if (typeof ResizeObserver === "function") {
      if (!fullscreenFitObserver)
        fullscreenFitObserver = new ResizeObserver(() => fitCanvasInFullscreen());
      fullscreenFitObserver.observe(wrap);
    }
    fitCanvasInFullscreen();
  }

  function stopFullscreenFit() {
    if (fullscreenFitObserver) {
      fullscreenFitObserver.disconnect();
      fullscreenFitObserver = null;
    }
    restoreCanvasLayout();
  }

  function syncFullscreenUi() {
    const on = isGameFullscreen();
    const wrap = document.getElementById("canvas-wrap");
    const btn = document.getElementById("fullscreenBtn");
    const exitBtn = document.getElementById("fullscreenExitBtn");
    if (wrap)
      wrap.classList.toggle("is-fullscreen", on);
    document.documentElement.classList.toggle("mamehub-fs-fallback", isFallbackFullscreen());
    if (btn) {
      btn.setAttribute("aria-pressed", on ? "true" : "false");
      btn.textContent = on ? "Exit fullscreen" : "Fullscreen";
    }
    if (exitBtn)
      exitBtn.hidden = !on;
    if (on)
      startFullscreenFit();
    else
      stopFullscreenFit();
    if (on) {
      const canvas = document.getElementById("canvas");
      if (canvas)
        canvas.focus();
    }
  }

  function enterFallbackFullscreen() {
    const wrap = document.getElementById("canvas-wrap");
    if (!wrap)
      return;
    wrap.classList.add("is-fallback-fullscreen");
    syncFullscreenUi();
  }

  function exitFallbackFullscreen() {
    const wrap = document.getElementById("canvas-wrap");
    if (wrap)
      wrap.classList.remove("is-fallback-fullscreen");
    syncFullscreenUi();
  }

  function isOurFullscreenEvent() {
    const fs = fullscreenElement();
    return fs === document.documentElement || isFallbackFullscreen();
  }

  document.addEventListener("fullscreenchange", (ev) => {
    if (isOurFullscreenEvent() || isFallbackFullscreen())
      ev.stopImmediatePropagation();
    if (!fullscreenElement() && isFallbackFullscreen())
      exitFallbackFullscreen();
    else
      syncFullscreenUi();
  }, true);
  document.addEventListener("webkitfullscreenchange", (ev) => {
    if (isOurFullscreenEvent() || isFallbackFullscreen())
      ev.stopImmediatePropagation();
    if (!fullscreenElement() && isFallbackFullscreen())
      exitFallbackFullscreen();
    else
      syncFullscreenUi();
  }, true);
  window.addEventListener("resize", (ev) => {
    if (!isGameFullscreen())
      return;
    ev.stopImmediatePropagation();
    fitCanvasInFullscreen();
  }, true);
  window.addEventListener("orientationchange", () => {
    if (isGameFullscreen())
      fitCanvasInFullscreen();
  });
  try {
    if (window.visualViewport) {
      window.visualViewport.addEventListener("resize", () => {
        if (isGameFullscreen())
          fitCanvasInFullscreen();
      });
    }
  } catch (_) { /* ignore */ }

  async function enterGameFullscreen() {
    enterFallbackFullscreen();
    const root = document.documentElement;
    const fn = root.requestFullscreen || root.webkitRequestFullscreen || root.webkitRequestFullScreen;
    if (!fn)
      return;
    try {
      fn.call(root);
    } catch (_) { /* tab-fill fallback already applied */ }
  }

  async function exitGameFullscreen() {
    if (fullscreenElement()) {
      try {
        await exitDocumentFullscreen();
      } catch (_) { /* ignore */ }
    }
    if (isFallbackFullscreen())
      exitFallbackFullscreen();
    else
      syncFullscreenUi();
  }

  async function toggleGameFullscreen() {
    if (isGameFullscreen())
      await exitGameFullscreen();
    else
      await enterGameFullscreen();
  }

  pinCanvasCssBox(document.getElementById("canvas"));

  const lobby = new MamehubNostrLobby();
  window.__mamehubActiveLobby = lobby;
  document.getElementById("pubkey").textContent = "npub… " + lobby.publicKey.slice(0, 12) + "…";

  let pendingRole = null; // null | "host" | "join"
  let hostJoinLink = "";
  let hostSoftware = "";
  let gameStarted = false;
  let hostBootSoftware = "";
  let joinStartWaiters = [];
  let joinRejected = false;
  let hostStartReceived = false;
  let joinAbortReject = null;
  let joinWelcomed = false;
  let mySeatPlayer = 0;
  let mySeatPeerId = "p0";
  /** Host + joiners: ordered by seat. Host always index 0 when hosting. */
  /** @type {Array<{pubkey:string,peerId:string,player:number,userId:string,software:string}>} */
  let rosterMembers = [];
  /** @type {Map<string, string>} edgeKey → connecting|open|failed (host view) */
  const meshLinkStates = new Map();
  const MAX_LOBBY_PLAYERS = 6;

  const joinFromQuery = (() => {
    const join = (qs.get("join") || "").toLowerCase();
    const room = (qs.get("room") || "").trim();
    const soft = (qs.get("soft") || qs.get("software") || "").trim();
    if (!room || !soft)
      return null;
    if (join === "0" || join === "false" || join === "no")
      return null;
    return { roomId: room, software: soft };
  })();

  function edgeKey(a, b) {
    return a < b ? (a + "|" + b) : (b + "|" + a);
  }

  function hideModeButtons() {
    const offline = document.getElementById("startOfflineBtn");
    const host = document.getElementById("hostBtn");
    if (offline)
      offline.hidden = true;
    if (host)
      host.hidden = true;
  }

  function restoreModeButtons() {
    if (joinFromQuery)
      return;
    const offline = document.getElementById("startOfflineBtn");
    const host = document.getElementById("hostBtn");
    if (offline)
      offline.hidden = false;
    if (host)
      host.hidden = false;
  }

  function profileBasePath() {
    // Keep join links under /<profile>/ so peers load the matching config.
    let path = window.location.pathname || "/";
    if (path.endsWith(".html"))
      path = path.replace(/[^/]+$/, "");
    if (!path.endsWith("/"))
      path += "/";
    const id = String(gameTag || "").replace(/[^a-z0-9_-]/gi, "");
    if (id) {
      const marker = "/" + id + "/";
      const idx = path.indexOf(marker);
      if (idx >= 0)
        return path.slice(0, idx + marker.length);
    }
    // Legacy single-profile packages served from site root.
    return "/";
  }

  function buildJoinLink(roomId, software) {
    const u = new URL(window.location.origin + profileBasePath());
    u.searchParams.set("join", "1");
    u.searchParams.set("room", roomId);
    u.searchParams.set("soft", software);
    return u.toString();
  }

  function reseatRoster() {
    rosterMembers.forEach((m, i) => {
      m.player = i;
      m.peerId = "p" + i;
    });
  }

  function rosterPayload() {
    return rosterMembers.map((m) => ({
      pubkey: m.pubkey,
      peerId: m.peerId,
      player: m.player,
      userId: m.userId || ""
    }));
  }

  function applyRoster(members, { isHostSide } = {}) {
    if (!Array.isArray(members) || !members.length)
      return;
    const byPk = new Map(rosterMembers.map((m) => [m.pubkey, m]));
    rosterMembers = members.map((m) => {
      const prev = byPk.get(m.pubkey);
      return {
        pubkey: m.pubkey,
        peerId: m.peerId || ("p" + (m.player | 0)),
        player: m.player | 0,
        userId: m.userId || (prev && prev.userId) || "",
        software: (prev && prev.software) || ""
      };
    });
    rosterMembers.sort((a, b) => a.player - b.player);
    const self = rosterMembers.find((m) => m.pubkey === lobby.pubkey);
    if (self) {
      mySeatPlayer = self.player;
      mySeatPeerId = self.peerId;
    }
    if (!isHostSide)
      renderHostLobby();
  }

  function meshFullyConnected() {
    if (rosterMembers.length < 2)
      return false;
    for (let i = 0; i < rosterMembers.length; i++) {
      for (let j = i + 1; j < rosterMembers.length; j++) {
        const st = meshLinkStates.get(edgeKey(rosterMembers[i].peerId, rosterMembers[j].peerId));
        if (st !== "open")
          return false;
      }
    }
    return true;
  }

  function setMeshLink(a, b, state) {
    if (!a || !b || a === b)
      return;
    meshLinkStates.set(edgeKey(a, b), state);
    renderHostMesh();
    updateHostStartButton();
  }

  function updateHostStartButton() {
    const btn = document.getElementById("hostStartGameBtn");
    if (!btn)
      return;
    const ok = !gameStarted && meshFullyConnected();
    btn.disabled = !ok;
    btn.title = ok
      ? "Full mesh connected — start the game"
      : (rosterMembers.length < 2
        ? "Wait for at least one joiner"
        : "Wait until every peer pair shows a green link (kick failed peers)");
  }

  function renderHostMesh() {
    const svg = document.getElementById("hostMeshGraph");
    if (!svg)
      return;
    const n = rosterMembers.length;
    if (n < 1) {
      svg.innerHTML = "";
      svg.hidden = true;
      return;
    }
    svg.hidden = false;
    const W = 320;
    const H = 200;
    svg.setAttribute("viewBox", "0 0 " + W + " " + H);
    const cx = W / 2;
    const cy = H / 2;
    const r = n === 1 ? 0 : Math.min(70, 40 + n * 8);
    const pos = rosterMembers.map((m, i) => {
      const ang = -Math.PI / 2 + (2 * Math.PI * i) / Math.max(n, 1);
      return {
        m,
        x: cx + r * Math.cos(ang),
        y: cy + r * Math.sin(ang)
      };
    });
    let html = "";
    for (let i = 0; i < pos.length; i++) {
      for (let j = i + 1; j < pos.length; j++) {
        const a = pos[i];
        const b = pos[j];
        const st = meshLinkStates.get(edgeKey(a.m.peerId, b.m.peerId)) || "connecting";
        const color = st === "open" ? "#3dd68c" : (st === "failed" ? "#e85d5d" : "#5a6578");
        const width = st === "connecting" ? 1.5 : 2.5;
        html += '<line x1="' + a.x.toFixed(1) + '" y1="' + a.y.toFixed(1) +
          '" x2="' + b.x.toFixed(1) + '" y2="' + b.y.toFixed(1) +
          '" stroke="' + color + '" stroke-width="' + width + '" />';
      }
    }
    for (const p of pos) {
      const label = "P" + (p.m.player + 1);
      const name = (p.m.userId || "").trim() || p.m.peerId;
      html += '<circle cx="' + p.x.toFixed(1) + '" cy="' + p.y.toFixed(1) +
        '" r="16" fill="#1a2030" stroke="#8ab4ff" stroke-width="2" />';
      html += '<text x="' + p.x.toFixed(1) + '" y="' + (p.y + 4).toFixed(1) +
        '" text-anchor="middle" fill="#e8eaed" font-size="11" font-family="ui-monospace,monospace">' +
        label + "</text>";
      html += '<text x="' + p.x.toFixed(1) + '" y="' + (p.y + 28).toFixed(1) +
        '" text-anchor="middle" fill="#9aa0a6" font-size="9">' +
        name.slice(0, 12).replace(/[<>&]/g, "") + "</text>";
    }
    svg.innerHTML = html;
  }

  function renderHostLobby() {
    const panel = document.getElementById("host-lobby");
    const waiting = document.getElementById("hostLobbyWaiting");
    const list = document.getElementById("hostLobbyMembers");
    const gameEl = document.getElementById("hostLobbyGame");
    if (!panel || !list || !waiting)
      return;
    if (gameEl)
      gameEl.textContent = hostSoftware
        ? ("Cart: " + hostSoftware + (lobby.roomId ? (" · Room " + lobby.roomId) : ""))
        : "";
    list.innerHTML = "";
    const joiners = rosterMembers.filter((m) => m.pubkey !== lobby.pubkey);
    if (!joiners.length && rosterMembers.length <= 1) {
      waiting.hidden = false;
      list.hidden = true;
      waiting.textContent = "Waiting for players to join…";
      renderHostMesh();
      updateHostStartButton();
      return;
    }
    waiting.hidden = true;
    list.hidden = false;
    for (const info of rosterMembers) {
      if (info.pubkey === lobby.pubkey)
        continue;
      const li = document.createElement("li");
      const name = (info.userId || "").trim() || ("player " + info.pubkey.slice(0, 8));
      const left = document.createElement("span");
      left.textContent = "P" + (info.player + 1) + " · " + name;
      const mid = document.createElement("span");
      const openToHost = meshLinkStates.get(edgeKey("p0", info.peerId)) === "open";
      mid.className = "member-status" + (openToHost ? " connected" : "");
      mid.textContent = openToHost ? "mesh ok" : "linking…";
      const kick = document.createElement("button");
      kick.type = "button";
      kick.className = "kick-btn";
      kick.textContent = "Kick";
      kick.disabled = gameStarted;
      kick.addEventListener("click", () => {
        kickLobbyMember(info.pubkey).catch((err) =>
          log("Kick failed: " + (err && err.message ? err.message : err)));
      });
      li.appendChild(left);
      li.appendChild(mid);
      li.appendChild(kick);
      list.appendChild(li);
    }
    renderHostMesh();
    updateHostStartButton();
    const meshHint = document.getElementById("hostMeshHint");
    if (meshHint) {
      meshHint.textContent = meshFullyConnected()
        ? "Full mesh connected — you can start."
        : "Green = connected, red = failed, gray = connecting. Kick anyone with red links.";
    }
  }

  function showHostLobby(software) {
    hostSoftware = software;
    document.getElementById("software-picker").hidden = true;
    document.getElementById("join-lobby").hidden = true;
    const panel = document.getElementById("host-lobby");
    panel.hidden = false;
    renderHostLobby();
  }

  function hideHostLobby() {
    const panel = document.getElementById("host-lobby");
    if (panel)
      panel.hidden = true;
  }

  function showJoinLobby(software, stateText) {
    hideModeButtons();
    document.getElementById("software-picker").hidden = true;
    document.getElementById("host-lobby").hidden = true;
    const panel = document.getElementById("join-lobby");
    panel.hidden = false;
    document.getElementById("joinLobbyGame").textContent = software
      ? ("Cart: " + software)
      : "";
    if (stateText)
      document.getElementById("joinLobbyState").textContent = stateText;
  }

  function setJoinLobbyState(text) {
    const el = document.getElementById("joinLobbyState");
    if (el)
      el.textContent = text;
  }

  function hideJoinLobby() {
    const panel = document.getElementById("join-lobby");
    if (panel)
      panel.hidden = true;
  }

  function signalTargetsMe(event, payload) {
    const pTags = (event.tags || []).filter((t) => t[0] === "p").map((t) => t[1]);
    if (pTags.length && !pTags.includes(lobby.pubkey))
      return false;
    const target = (payload && payload.target) || "";
    if (target && target !== lobby.pubkey)
      return false;
    return true;
  }

  function abortJoin(reason) {
    joinRejected = true;
    const err = new Error(reason || "Join aborted");
    if (joinAbortReject) {
      try { joinAbortReject(err); } catch (_) {}
      joinAbortReject = null;
    }
    const waiters = joinStartWaiters.splice(0);
    for (const w of waiters) {
      try { w.reject(err); } catch (_) {}
    }
    const net = window.__mamehubNet;
    if (net)
      net.close();
  }

  function resolveJoinStart() {
    hostStartReceived = true;
    const waiters = joinStartWaiters.splice(0);
    for (const w of waiters) {
      try { w.resolve(); } catch (_) {}
    }
  }

  function waitForHostStart(timeoutMs) {
    return new Promise((resolve, reject) => {
      if (joinRejected)
        return reject(new Error("Join rejected"));
      if (hostStartReceived || gameStarted)
        return resolve();
      const timer = setTimeout(() => {
        joinStartWaiters = joinStartWaiters.filter((w) => w !== entry);
        reject(new Error("Timed out waiting for host to start"));
      }, timeoutMs || 600000);
      const entry = {
        resolve: () => { clearTimeout(timer); resolve(); },
        reject: (err) => { clearTimeout(timer); reject(err); }
      };
      joinStartWaiters.push(entry);
    });
  }

  async function publishRoster() {
    try {
      await lobby.signal("roster", { members: rosterPayload() });
    } catch (err) {
      log("Roster publish failed: " + (err && err.message ? err.message : err));
    }
  }

  async function syncMeshFromRoster() {
    const net = window.__mamehubNet;
    if (!net)
      return;
    net.setIdentity({
      player: mySeatPlayer,
      peerId: mySeatPeerId,
      isHost: pendingRole === "host" || net.isHost,
      selfPubkey: lobby.pubkey
    });
    net.onLinkState = (a, b, state) => {
      setMeshLink(a, b, state);
      // Guests report link state so the host can draw guest–guest edges.
      if (!net.isHost) {
        lobby.signal("link", { a, b, state }).catch(() => {});
      }
    };
    net.onMeshChange = () => {
      if (net.isHost) {
        for (const [key, st] of net.linkStates)
          meshLinkStates.set(key, st);
        // Mark members linked to host
        for (const m of rosterMembers) {
          if (m.pubkey === lobby.pubkey)
            continue;
          const st = meshLinkStates.get(edgeKey("p0", m.peerId));
          if (st === "open")
            /* listed as mesh ok in render */;
        }
        renderHostLobby();
      } else if (net.meshFullyConnected()) {
        setJoinLobbyState("Mesh connected — waiting for host to start…");
      }
      updateHostStartButton();
    };
    await net.ensureMesh(rosterPayload());
  }

  async function kickLobbyMember(pubkey) {
    if (!pubkey || gameStarted)
      return;
    const victim = rosterMembers.find((m) => m.pubkey === pubkey);
    log("Kicking " + (victim ? victim.peerId : pubkey.slice(0, 8)));
    try {
      await lobby.signal("kick", { target: pubkey }, pubkey);
    } catch (err) {
      log("Kick signal failed: " + (err && err.message ? err.message : err));
    }
    rosterMembers = rosterMembers.filter((m) => m.pubkey !== pubkey);
    reseatRoster();
    // Drop link states; rebuild from mesh
    meshLinkStates.clear();
    const net = window.__mamehubNet;
    if (net)
      net.removePeerByPubkey(pubkey);
    await publishRoster();
    await syncMeshFromRoster();
    renderHostLobby();
  }

  async function hostStartGame() {
    if (gameStarted)
      return;
    if (!meshFullyConnected())
      return log("Mesh is not fully connected — wait for green links or kick failed peers");
    const net = window.__mamehubNet;
    if (!net || !net.meshFullyConnected())
      return log("WebRTC mesh not ready yet");
    gameStarted = true;
    updateHostStartButton();
    const userId = document.getElementById("userId").value.trim() || "host";
    const soft = hostBootSoftware || hostSoftware;
    const members = rosterPayload();
    try {
      await lobby.signal("start", { software: soft, members });
      await lobby.markStarted({
        game: gameTag,
        userId,
        software: soft
      });
    } catch (err) {
      log("Start signal failed: " + (err && err.message ? err.message : err));
    }
    hideHostLobby();
    log("Host started game — booting emulator (" + members.length + " peers)");
    mySeatPlayer = 0;
    mySeatPeerId = "p0";
    await bootEmulator(soft, {
      mamehub: true,
      isHost: true,
      player: 0,
      peerId: "p0",
      peerIds: members.map((m) => m.peerId),
      userId,
      net
    });
  }

  lobby.onSignal = async ({ type, payload, event }) => {
    log("Nostr signal " + type + " from " + event.pubkey.slice(0, 8));
    const net = window.__mamehubNet;
    try {
      if (type === "kick") {
        if (!signalTargetsMe(event, payload))
          return;
        setJoinLobbyState("You were kicked from the lobby.");
        log("Kicked by host");
        abortJoin("kicked");
        return;
      }
      if (type === "reject") {
        if (!signalTargetsMe(event, payload))
          return;
        const reason = (payload && payload.reason) || "rejected";
        setJoinLobbyState(reason === "started"
          ? "Game already started — cannot join."
          : reason === "full"
            ? "Lobby is full."
            : ("Join rejected (" + reason + ")."));
        log("Join rejected: " + reason);
        abortJoin(reason);
        return;
      }
      if (type === "welcome") {
        if (!signalTargetsMe(event, payload))
          return;
        joinWelcomed = true;
        if (payload && Array.isArray(payload.members))
          applyRoster(payload.members);
        setJoinLobbyState("Got roster — building mesh…");
        await syncMeshFromRoster();
        return;
      }
      if (type === "roster") {
        if (gameStarted)
          return;
        if (payload && Array.isArray(payload.members)) {
          const hadSeat = rosterMembers.some((m) => m.pubkey === lobby.pubkey);
          applyRoster(payload.members);
          if (hadSeat && !rosterMembers.some((m) => m.pubkey === lobby.pubkey) &&
              pendingRole === "join") {
            setJoinLobbyState("You were removed from the lobby.");
            abortJoin("kicked");
            return;
          }
          await syncMeshFromRoster();
          if (pendingRole === "join" || (net && !net.isHost))
            setJoinLobbyState("Roster updated (P" + (mySeatPlayer + 1) + ") — meshing…");
        }
        return;
      }
      if (type === "link") {
        // Host aggregates guest-reported edges for the mesh graph.
        if (!net || !net.isHost)
          return;
        const a = payload && payload.a;
        const b = payload && payload.b;
        const state = (payload && payload.state) || "connecting";
        if (a && b)
          setMeshLink(a, b, state);
        return;
      }
      if (type === "start") {
        if (!net || net.isHost)
          return;
        if (payload && Array.isArray(payload.members))
          applyRoster(payload.members);
        gameStarted = true;
        setJoinLobbyState("Host started — launching…");
        resolveJoinStart();
        return;
      }
      if (type === "hello") {
        if (net && !net.isHost)
          return;
        if (!net && pendingRole !== "host")
          return;
        if (gameStarted) {
          try {
            await lobby.signal("reject", { reason: "started" }, event.pubkey);
          } catch (_) {}
          return;
        }
        if (rosterMembers.some((m) => m.pubkey === event.pubkey)) {
          // Re-hello: only re-send welcome (no remesh — avoids SDP glare).
          try {
            await lobby.signal("welcome", { members: rosterPayload() }, event.pubkey);
          } catch (_) {}
          return;
        }
        if (rosterMembers.length >= MAX_LOBBY_PLAYERS) {
          try {
            await lobby.signal("reject", { reason: "full" }, event.pubkey);
          } catch (_) {}
          return;
        }
        rosterMembers.push({
          pubkey: event.pubkey,
          peerId: "p" + rosterMembers.length,
          player: rosterMembers.length,
          userId: (payload && payload.userId) || "",
          software: (payload && payload.software) || ""
        });
        reseatRoster();
        log("Joiner hello — seat P" + (rosterMembers[rosterMembers.length - 1].player + 1));
        await lobby.signal("welcome", { members: rosterPayload() }, event.pubkey);
        await publishRoster();
        await syncMeshFromRoster();
        renderHostLobby();
        return;
      }
      if (!net)
        return;
      if (type === "offer") {
        if (joinRejected)
          return;
        await net.acceptOffer(payload, event.pubkey);
      } else if (type === "answer")
        await net.acceptAnswer(payload, event.pubkey);
      else if (type === "candidate")
        await net.addIceCandidate(payload, event.pubkey);
    } catch (err) {
      log("Signal handle error: " + (err && err.message ? err.message : err));
    }
  };

  lobby.onPresence = async ({ payload, event }) => {
    if (gameStarted || pendingRole !== "host")
      return;
    log("Nostr presence from " + event.pubkey.slice(0, 8) +
      (payload && payload.userId ? (" user=" + payload.userId) : "") +
      (payload && payload.software ? (" soft=" + payload.software) : ""));
  };

  lobby.onAnnounce = (ev) => {
    try {
      const body = JSON.parse(ev.content || "{}");
      log("Nostr announce room=" + (body.roomId || "?") +
        " soft=" + (body.software || "?") +
        (body.started ? " (started)" : ""));
      if (body.started && joinFromQuery && !gameStarted && !window.__mamehubNet?.ready) {
        setJoinLobbyState("Game already started — cannot join.");
        abortJoin("started");
      }
    } catch (_) {}
  };

  let softwareCatalog = null;
  let softwareCatalogPromise = null;
  let softwareAcActive = -1;
  let softwareAcFiltered = [];
  let softwareAcQuery = null;
  let softwareAcWinStart = -1;
  let softwareAcWinEnd = -1;
  let softwareAcPaintRaf = 0;
  const SOFTWARE_AC_ITEM_H = 36;
  const SOFTWARE_AC_OVERSCAN = 10;

  function decodeXmlText(s) {
    return String(s || "")
      .replace(/&amp;/g, "&")
      .replace(/&lt;/g, "<")
      .replace(/&gt;/g, ">")
      .replace(/&quot;/g, '"')
      .replace(/&#39;/g, "'");
  }

  async function loadSoftwareCatalog() {
    if (softwareCatalog)
      return softwareCatalog;
    if (softwareCatalogPromise)
      return softwareCatalogPromise;
    const cfg = window.MAMEHUB_BROWSER || {};
    softwareCatalogPromise = (async () => {
      if (isArcade) {
        const url = cfg.machinesUrl || "arcade_top_mp.json";
        log("Loading arcade machine catalog…");
        const resp = await fetch(url, { cache: "no-store" });
        if (!resp.ok)
          throw new Error("arcade catalog HTTP " + resp.status);
        const data = await resp.json();
        const games = Array.isArray(data.games) ? data.games : [];
        const entries = games.map((g) => {
          const id = String(g.mame || g.id || "").trim();
          const title = String(g.title || g.description || id).trim() || id;
          return {
            id,
            title,
            titleLower: title.toLowerCase(),
            idLower: id.toLowerCase(),
            players: g.players | 0
          };
        }).filter((e) => e.id);
        softwareCatalog = entries;
        log("Arcade catalog: " + entries.length + " machines");
        return entries;
      }
      const hashFiles = Array.isArray(cfg.hashFiles) && cfg.hashFiles.length
        ? cfg.hashFiles
        : [{ url: cfg.hashUrl || ("hash/" + gameTag + ".xml"), file: "", machine: cfg.machine || gameTag }];
      log("Loading " + brandTitle + " softlist catalog…");
      const entries = [];
      const re = /<software\s+name="([^"]+)"[^>]*>[\s\S]*?<description>([^<]*)<\/description>/gi;
      for (const hf of hashFiles) {
        const hashUrl = hf.url || cfg.hashUrl;
        const resp = await fetch(hashUrl);
        if (!resp.ok)
          throw new Error("softlist fetch HTTP " + resp.status + " for " + hashUrl);
        const text = await resp.text();
        let m;
        re.lastIndex = 0;
        while ((m = re.exec(text)) !== null) {
          const id = m[1];
          const title = decodeXmlText(m[2]).trim() || id;
          entries.push({
            id,
            title,
            titleLower: title.toLowerCase(),
            idLower: id.toLowerCase(),
            machine: hf.machine || cfg.machine || gameTag,
            hashFile: hf.file || String(hashUrl).split("/").pop()
          });
        }
      }
      entries.sort((a, b) => a.title.localeCompare(b.title) || a.id.localeCompare(b.id));
      softwareCatalog = entries;
      log(brandTitle + " catalog: " + entries.length + " titles");
      return entries;
    })().catch((err) => {
      softwareCatalogPromise = null;
      throw err;
    });
    return softwareCatalogPromise;
  }

  function softwareAcPrefixMatch(entry, q) {
    if (!q)
      return true;
    if (entry.idLower.startsWith(q) || entry.titleLower.startsWith(q))
      return true;
    if (entry.idLower.includes(q) || entry.titleLower.includes(q))
      return true;
    const parts = entry.titleLower.split(/[^a-z0-9+]+/).filter(Boolean);
    for (const p of parts) {
      if (p.startsWith(q))
        return true;
    }
    const tokens = q.split(/[^a-z0-9+]+/).filter(Boolean);
    if (tokens.length > 1) {
      const hay = [entry.idLower, ...parts];
      return tokens.every((tok) => hay.some((p) => p.startsWith(tok) || p.includes(tok)));
    }
    return false;
  }

  function softwareAcMatchList(query) {
    const q = (query || "").trim().toLowerCase();
    if (!q)
      return softwareCatalog;
    const out = [];
    for (const entry of softwareCatalog) {
      if (softwareAcPrefixMatch(entry, q))
        out.push(entry);
    }
    return out;
  }

  function ensureSoftwareAcListHandlers(list) {
    if (list.dataset.acVirtual)
      return;
    list.dataset.acVirtual = "1";
    list.addEventListener("scroll", () => {
      if (softwareAcPaintRaf)
        return;
      softwareAcPaintRaf = requestAnimationFrame(() => {
        softwareAcPaintRaf = 0;
        paintSoftwareAcWindow();
      });
    });
    list.addEventListener("mousedown", (ev) => {
      const li = ev.target.closest("li[role='option']");
      if (!li)
        return;
      ev.preventDefault();
      const idx = parseInt(li.dataset.idx, 10);
      if (softwareAcFiltered[idx])
        selectSoftwareAc(softwareAcFiltered[idx]);
    });
  }

  function paintSoftwareAcWindow(force) {
    const list = document.getElementById("softwareAcList");
    if (!list || list.hidden)
      return;
    if (!softwareAcFiltered.length) {
      softwareAcWinStart = -1;
      softwareAcWinEnd = -1;
      list.innerHTML = "";
      const li = document.createElement("li");
      li.className = "ac-empty";
      li.textContent = softwareAcQuery ? "No matches" : "No software loaded";
      list.appendChild(li);
      return;
    }
    const itemH = SOFTWARE_AC_ITEM_H;
    const total = softwareAcFiltered.length;
    const viewH = Math.max(list.clientHeight || 0, 256);
    const start = Math.max(0, Math.floor(list.scrollTop / itemH) - SOFTWARE_AC_OVERSCAN);
    const end = Math.min(total, start + Math.ceil(viewH / itemH) + SOFTWARE_AC_OVERSCAN * 2);
    if (!force && start === softwareAcWinStart && end === softwareAcWinEnd) {
      list.querySelectorAll('li[role="option"]').forEach((li) => {
        const idx = parseInt(li.dataset.idx, 10);
        li.setAttribute("aria-selected", idx === softwareAcActive ? "true" : "false");
      });
      return;
    }
    softwareAcWinStart = start;
    softwareAcWinEnd = end;
    const frag = document.createDocumentFragment();
    const top = document.createElement("li");
    top.className = "ac-spacer";
    top.setAttribute("aria-hidden", "true");
    top.style.height = (start * itemH) + "px";
    frag.appendChild(top);
    for (let i = start; i < end; i++) {
      const entry = softwareAcFiltered[i];
      const li = document.createElement("li");
      li.setAttribute("role", "option");
      li.id = "softwareAcOpt" + i;
      li.dataset.id = entry.id;
      li.dataset.idx = String(i);
      li.setAttribute("aria-selected", i === softwareAcActive ? "true" : "false");
      li.innerHTML = '<span class="ac-title"></span><span class="ac-id"></span>';
      li.querySelector(".ac-title").textContent = entry.title;
      li.querySelector(".ac-id").textContent = entry.id;
      frag.appendChild(li);
    }
    const bot = document.createElement("li");
    bot.className = "ac-spacer";
    bot.setAttribute("aria-hidden", "true");
    bot.style.height = ((total - end) * itemH) + "px";
    frag.appendChild(bot);
    list.innerHTML = "";
    list.appendChild(frag);
  }

  function closeSoftwareAcList() {
    const list = document.getElementById("softwareAcList");
    const input = document.getElementById("softwareAcInput");
    if (list)
      list.hidden = true;
    if (input)
      input.setAttribute("aria-expanded", "false");
    softwareAcActive = -1;
    softwareAcWinStart = -1;
    softwareAcWinEnd = -1;
    softwareAcQuery = null;
  }

  function softwareAcIndexOfShortname() {
    const input = document.getElementById("softwareAcInput");
    const id = ((input && input.dataset.shortname) || "").trim().toLowerCase();
    if (!id)
      return -1;
    for (let i = 0; i < softwareAcFiltered.length; i++) {
      if (softwareAcFiltered[i].idLower === id)
        return i;
    }
    return -1;
  }

  function renderSoftwareAcList(query) {
    const list = document.getElementById("softwareAcList");
    const input = document.getElementById("softwareAcInput");
    if (!list || !input || !softwareCatalog)
      return;
    const q = (query || "").trim().toLowerCase();
    const queryChanged = q !== softwareAcQuery;
    softwareAcQuery = q;
    softwareAcFiltered = softwareAcMatchList(query);
    ensureSoftwareAcListHandlers(list);
    list.hidden = false;
    input.setAttribute("aria-expanded", "true");
    const selectedIdx = softwareAcIndexOfShortname();
    softwareAcActive = selectedIdx >= 0 ? selectedIdx : (softwareAcFiltered.length ? 0 : -1);
    softwareAcWinStart = -1;
    void list.offsetHeight;
    if (queryChanged && selectedIdx < 0)
      list.scrollTop = 0;
    if (softwareAcActive >= 0)
      updateSoftwareAcActive();
    else
      paintSoftwareAcWindow(true);
  }

  function scrollSoftwareAcActiveIntoView() {
    const list = document.getElementById("softwareAcList");
    if (!list || softwareAcActive < 0 || !softwareAcFiltered.length)
      return;
    const itemH = SOFTWARE_AC_ITEM_H;
    const top = softwareAcActive * itemH;
    const viewH = Math.max(list.clientHeight || 0, 256);
    if (top < list.scrollTop)
      list.scrollTop = top;
    else if (top + itemH > list.scrollTop + viewH)
      list.scrollTop = Math.max(0, top + itemH - viewH);
  }

  function updateSoftwareAcActive() {
    const list = document.getElementById("softwareAcList");
    if (!list)
      return;
    // Paint first so the virtual list has a real scrollHeight, then scroll.
    paintSoftwareAcWindow(true);
    scrollSoftwareAcActiveIntoView();
    paintSoftwareAcWindow();
  }

  function selectSoftwareAc(entry) {
    const input = document.getElementById("softwareAcInput");
    if (!input || !entry)
      return;
    input.value = entry.title + " (" + entry.id + ")";
    input.dataset.shortname = entry.id;
    input.dataset.machine = entry.machine || "";
    closeSoftwareAcList();
    prefetchCandyRoms(entry.id, entry.machine || (isArcade ? entry.id : machineForSoftware(entry.id)));
  }

  function machineForSoftware(software) {
    const cfg = window.MAMEHUB_BROWSER || {};
    const id = String(software || "").trim();
    if (softwareCatalog && id) {
      const hit = softwareCatalog.find((e) => e.id === id);
      if (hit && hit.machine)
        return hit.machine;
    }
    return cfg.machine || gameTag;
  }

  function selectedSoftwareMachine() {
    const input = document.getElementById("softwareAcInput");
    const fromData = input && (input.dataset.machine || "").trim();
    if (fromData)
      return fromData;
    return machineForSoftware(selectedSoftwareShortname());
  }

  function selectedSoftwareShortname() {
    const input = document.getElementById("softwareAcInput");
    if (!input)
      return "";
    const fromData = (input.dataset.shortname || "").trim();
    if (fromData)
      return fromData;
    const raw = input.value.trim();
    const m = raw.match(/\(([a-z0-9_]+)\)\s*$/i);
    if (m)
      return m[1];
    // Bare shortname typed by the user
    if (/^[a-z0-9_]+$/i.test(raw))
      return raw;
    return "";
  }

  function mountSoftwareAutocomplete() {
    const input = document.getElementById("softwareAcInput");
    if (!input || input.dataset.acMounted)
      return;
    input.dataset.acMounted = "1";
    input.addEventListener("input", () => {
      input.dataset.shortname = "";
      renderSoftwareAcList(input.value);
    });
    input.addEventListener("focus", () => {
      if (softwareCatalog)
        renderSoftwareAcList(input.value);
    });
    input.addEventListener("blur", () => {
      setTimeout(closeSoftwareAcList, 120);
    });
    input.addEventListener("keydown", (ev) => {
      const list = document.getElementById("softwareAcList");
      const open = list && !list.hidden;
      if (ev.key === "ArrowDown") {
        ev.preventDefault();
        if (!open)
          renderSoftwareAcList(input.value);
        else if (softwareAcFiltered.length) {
          softwareAcActive = (softwareAcActive + 1) % softwareAcFiltered.length;
          updateSoftwareAcActive();
        }
      } else if (ev.key === "ArrowUp") {
        ev.preventDefault();
        if (open && softwareAcFiltered.length) {
          softwareAcActive = (softwareAcActive - 1 + softwareAcFiltered.length) % softwareAcFiltered.length;
          updateSoftwareAcActive();
        }
      } else if (ev.key === "Enter") {
        if (open && softwareAcActive >= 0 && softwareAcFiltered[softwareAcActive]) {
          ev.preventDefault();
          selectSoftwareAc(softwareAcFiltered[softwareAcActive]);
        }
      } else if (ev.key === "Escape") {
        closeSoftwareAcList();
      }
    });
  }

  async function showSoftwarePicker(mode) {
    const picker = document.getElementById("software-picker");
    const title = document.getElementById("softwarePickerTitle");
    const input = document.getElementById("softwareAcInput");
    mountSoftwareAutocomplete();
    const noun = pickNoun;
    if (mode === "host")
      title.textContent = "Host game — Select " + noun;
    else if (mode === "join")
      title.textContent = "Join game — Select " + noun;
    else
      title.textContent = "Offline — Select " + noun;
    picker.hidden = false;
    try {
      await loadSoftwareCatalog();
      if (input) {
        const cfg = window.MAMEHUB_BROWSER || {};
        const fromQuery = queryPreferredGame();
        let entry = fromQuery ? (softwareAcMatchList(fromQuery)[0] || null) : null;
        if (!entry) {
          const prefer = isArcade
            ? (cfg.defaultMachine || "xmen")
            : ((mode === "host" || mode === "join")
              ? (cfg.defaultHostSoftware || cfg.defaultSoftware)
              : (cfg.defaultSoftware || cfg.defaultHostSoftware));
          const preferLower = String(prefer || "").toLowerCase();
          entry = (preferLower && softwareCatalog.find((e) =>
            e.idLower === preferLower || e.titleLower === preferLower)) || softwareCatalog[0] || null;
        }
        if (entry)
          selectSoftwareAc(entry);
        input.focus();
        renderSoftwareAcList("");
      }
    } catch (err) {
      log("Catalog load failed: " + (err && err.message ? err.message : err));
    }
    if (mode === "host")
      log("Host — select " + noun + ", then Play. Share the join link; start when a player connects.");
    else if (isArcade)
      log("Offline — select an arcade machine. Candy downloads the zip before the emulator starts.");
    else
      log("Offline — select " + noun + ". Candy downloads the system zip + cart before the emulator starts.");
  }

  function candyProxyFetchUrl(archiveUrl) {
    const cfg = window.MAMEHUB_BROWSER || {};
    const proxyBase = cfg.candyProxyBase || "/candy-proxy";
    return proxyBase + (proxyBase.indexOf("?") >= 0 ? "&" : "?") + "url=" + encodeURIComponent(archiveUrl);
  }

  function candyListName(software, machine) {
    const cfg = window.MAMEHUB_BROWSER || {};
    const id = String(software || "").trim();
    if (softwareCatalog && id) {
      const hit = softwareCatalog.find((e) => e.id === id);
      if (hit && hit.hashFile)
        return String(hit.hashFile).replace(/\.xml$/i, "");
    }
    const hashFiles = Array.isArray(cfg.hashFiles) ? cfg.hashFiles : [];
    const hf = hashFiles.find((h) => (h.machine || cfg.machine || gameTag) === machine) || hashFiles[0];
    if (hf && hf.file)
      return String(hf.file).replace(/\.xml$/i, "");
    if (cfg.hashUrl)
      return String(cfg.hashUrl).split("/").pop().replace(/\.xml$/i, "");
    return machine || gameTag;
  }

  function candyJobsFor(software, machine) {
    const cfg = window.MAMEHUB_BROWSER || {};
    const romPath = cfg.romPath || "/roms";
    const jobs = [];
    if (isArcade) {
      jobs.push({
        name: software + ".zip",
        url: "https://archive.org/download/MAME220RomsOnlyMerged/" + software + ".zip",
        out: romPath + "/" + software + ".zip"
      });
      return jobs;
    }
    jobs.push({
      name: machine + ".zip",
      url: "https://archive.org/download/MAME220RomsOnlyMerged/" + machine + ".zip",
      out: romPath + "/" + machine + ".zip"
    });
    if (machine === "snes") {
      jobs.push({
        name: "s_smp.zip",
        copyFrom: romPath + "/snes.zip",
        out: romPath + "/s_smp.zip"
      });
    }
    const list = candyListName(software, machine);
    jobs.push({
      name: software + ".zip",
      url: "https://archive.org/download/MAME_0.202_Software_List_ROMs_merged/" +
        list + ".zip/" + list + "%2F" + software + ".zip",
      altUrl: "https://archive.org/download/MAME_0.202_Software_List_ROMs_merged/" +
        list + ".zip/" + list + "/" + software + ".zip",
      out: romPath + "/" + list + "/" + software + ".zip"
    });
    return jobs;
  }

  async function fetchCandyBytes(archiveUrl, name, altUrl) {
    const tryUrl = async (url) => {
      const resp = await fetch(candyProxyFetchUrl(url));
      if (!resp.ok)
        throw new Error("HTTP " + resp.status);
      const total = Number(resp.headers.get("Content-Length")) || 0;
      if (resp.body && typeof resp.body.getReader === "function") {
        const reader = resp.body.getReader();
        const chunks = [];
        let loaded = 0;
        while (true) {
          const { done, value } = await reader.read();
          if (done)
            break;
          chunks.push(value);
          loaded += value.length;
          setCandyProgress(loaded, total, name);
        }
        const data = new Uint8Array(loaded);
        let offset = 0;
        for (const chunk of chunks) {
          data.set(chunk, offset);
          offset += chunk.length;
        }
        return data;
      }
      const buf = new Uint8Array(await resp.arrayBuffer());
      setCandyProgress(buf.length, buf.length, name);
      return buf;
    };
    try {
      return await tryUrl(archiveUrl);
    } catch (err) {
      if (!altUrl)
        throw err;
      log("Candy retry " + name);
      return await tryUrl(altUrl);
    }
  }

  const candyFileCache = new Map();
  let candyPrefetchKey = "";
  let candyPrefetchPromise = null;

  async function ensureCandyRoms(software, machine) {
    software = (software || "").trim();
    machine = (machine || (isArcade ? software : "")).trim();
    if (!software)
      return [];
    if (!isArcade && !machine)
      machine = machineForSoftware(software);
    const key = isArcade ? software : (machine + ":" + software);
    if (candyPrefetchKey === key && candyPrefetchPromise)
      return candyPrefetchPromise;
    candyPrefetchKey = key;
    candyPrefetchPromise = (async () => {
      const jobs = candyJobsFor(software, machine);
      const files = [];
      for (const job of jobs) {
        if (job.copyFrom)
          continue;
        if (candyFileCache.has(job.out)) {
          files.push({ out: job.out, data: candyFileCache.get(job.out) });
          continue;
        }
        try {
          const data = await fetchCandyBytes(job.url, job.name, job.altUrl);
          candyFileCache.set(job.out, data);
          files.push({ out: job.out, data });
          log("Candy ready " + job.name + " (" + data.length + " bytes)");
        } catch (err) {
          log("Candy prefetch failed for " + job.name + ": " +
            (err && err.message ? err.message : err) + " (emulator candy will retry)");
        }
      }
      for (const job of jobs) {
        if (!job.copyFrom)
          continue;
        if (candyFileCache.has(job.out)) {
          files.push({ out: job.out, data: candyFileCache.get(job.out) });
          continue;
        }
        const src = candyFileCache.get(job.copyFrom);
        if (!src)
          continue;
        candyFileCache.set(job.out, src);
        files.push({ out: job.out, data: src });
        log("Candy aliased " + job.name);
      }
      return files;
    })();
    return candyPrefetchPromise;
  }

  function prefetchCandyRoms(software, machine) {
    ensureCandyRoms(software, machine).catch((err) => {
      log("Candy prefetch: " + (err && err.message ? err.message : err));
    });
  }

  function setCandyProgress(loaded, total, name) {
    const wrap = document.getElementById("candy-progress");
    const bar = document.getElementById("candy-progress-bar");
    const label = document.getElementById("candy-progress-label");
    if (!wrap || !bar || !label)
      return;
    wrap.hidden = false;
    const pct = total > 0 ? Math.min(100, Math.round((loaded / total) * 100)) : (loaded > 0 ? 50 : 0);
    bar.style.width = pct + "%";
    const base = (name || "").split("/").pop() || "ROM";
    const mb = (n) => (n / (1024 * 1024)).toFixed(2) + " MiB";
    label.textContent = total > 0
      ? `Candy: ${base} (${mb(loaded)} / ${mb(total)}, ${pct}%)`
      : `Candy: ${base} (${mb(loaded)})`;
    if (total > 0 && loaded >= total)
      setTimeout(() => { wrap.hidden = true; }, 800);
  }

  async function loadHashXml(url) {
    log("Loading softlist hash " + url + " …");
    const resp = await fetch(url);
    if (!resp.ok)
      throw new Error("hash fetch HTTP " + resp.status + " for " + url);
    const buf = new Uint8Array(await resp.arrayBuffer());
    log("Loaded hash XML (" + buf.length + " bytes)");
    return buf;
  }

  async function bootEmulator(software, opts) {
    const cfg = window.MAMEHUB_BROWSER || {};
    const jsPath = cfg.wasmJs || ("dist/mame" + gameTag + "hub.js");
    const romPath = cfg.romPath || "/roms";
    const hashPath = cfg.hashPath || "/hash";
    const media = cfg.media || "cart";
    const canvas = document.getElementById("canvas");
    const placeholder = document.getElementById("canvas-placeholder");

    if (typeof Module !== "undefined" && Module.calledRun) {
      log("MAME already started");
      return;
    }

    software = (software || "").trim();
    if (!software)
      throw new Error(isArcade ? "No machine shortname" : "No software shortname");

    if (!isArcade && !opts.machine)
      await loadSoftwareCatalog().catch(() => {});
    const machine = (opts.machine || machineForSoftware(software) || cfg.machine || gameTag).trim();
    log("Candy: ensuring ROMs before emulator start…");
    const candyFiles = await ensureCandyRoms(software, machine);

    const hashFiles = Array.isArray(cfg.hashFiles) && cfg.hashFiles.length
      ? cfg.hashFiles
      : (cfg.hashUrl ? [{ url: cfg.hashUrl, file: String(cfg.hashUrl).split("/").pop() }] : []);
    const hashDataByFile = {};
    if (!isArcade) {
      for (const hf of hashFiles) {
        const file = hf.file || String(hf.url || "").split("/").pop();
        hashDataByFile[file] = await loadHashXml(hf.url);
      }
    }
    placeholder.style.display = "none";
    canvas.style.display = "block";
    // Keep the SDL/WebGL window near native aspect so soft-composite stays cheap.
    const cw = (cfg.canvasWidth | 0) || (isArcade ? 1152 : 512);
    const ch = (cfg.canvasHeight | 0) || (isArcade ? 448 : 448);
    canvas.width = cw;
    canvas.height = ch;
    canvas.style.maxWidth = cw + "px";
    canvas.style.aspectRatio = cw + " / " + ch;
    pinCanvasCssBox(canvas);
    guardCanvasBufferSize(canvas);
    if (isGameFullscreen())
      fitCanvasInFullscreen();
    canvas.focus();
    if (window.MamehubVirtualGamepad && typeof window.MamehubVirtualGamepad.showForPlay === "function")
      window.MamehubVirtualGamepad.showForPlay();

    const args = isArcade
      ? [
          software,
          "-rompath", romPath,
          "-window",
          // Cap OSD window / soft-composite size (xmen6p dual layout is wide).
          "-resolution", cw + "x" + ch,
          opts.mamehub ? "-mamehub" : "-nomamehub",
          "-candy",
          "-skip_gameinfo",
          "-nomouse",
          "-throttle",
          "-video", "opengl",
          "-nowaitvsync",
          "-nosyncrefresh",
          "-nodiscord",
          "-nvram_directory", "/nvram",
          "-state_directory", "/sta"
        ]
      : [
          machine,
          "-" + media, software,
          "-rompath", romPath,
          "-hashpath", hashPath,
          "-window",
          "-resolution", cw + "x" + ch,
          opts.mamehub ? "-mamehub" : "-nomamehub",
          "-candy",
          "-skip_gameinfo",
          "-nomouse",
          "-throttle",
          "-video", "opengl",
          "-nowaitvsync",
          "-nosyncrefresh",
          "-nodiscord",
          "-nvram_directory", "/nvram",
          "-state_directory", "/sta"
        ];
    if (opts.mamehub)
      log("Netplay throttle enabled (shared WebRTC clock)");
    else
      log("Offline throttle enabled (local realtime clock)");
    if (fakeLagConfig.enabled && opts.mamehub) {
      args.push("-fake_lag");
      log("Fake lag enabled (−fake_lag + DataChannel delay " +
        fakeLagConfig.ms + "ms one-way" +
        (fakeLagConfig.jitterMs ? (", jitter +" + fakeLagConfig.jitterMs + "ms") : "") +
        (fakeLagConfig.drop ? (", drop " + Math.round(fakeLagConfig.drop * 1000) / 10 + "%") : "") +
        ")");
    }
    if (muteRequested) {
      args.push("-sound", "none");
      if (volumeDb !== null) {
        args.push("-volume");
        args.push(volumeDb);
      }
      log("Audio muted (?mute=1 / -sound none)");
    } else if (volumeDb !== null) {
      args.push("-volume");
      args.push(volumeDb);
      log("Volume set to " + volumeDb + " dB");
    }
    if (opts.userId) {
      args.push("-user_id");
      args.push(opts.userId);
    }

    const net = opts.net || null;
    const nvramOffline = !opts.mamehub;
    currentPlaySoftware = software;
    window.Module = {
      canvas,
      candyProxyBase: cfg.candyProxyBase || "/candy-proxy",
      candyProgress: setCandyProgress,
      arguments: args,
      mamehubNet: net,
      __mamehubDumpInputs: !!dumpInputsRequested,
      __mamehubNvramSoftware: software,
      __mamehubNvramOffline: nvramOffline,
      __mamehubPersistNvram: function () {
        if (!nvramOffline)
          return;
        if (persistNvramToStorage())
          log("Saved NVRAM to browser storage");
      },
      __mamehubPersistSaveState: function () {
        if (persistSaveStateToStorage())
          log("Saved state to browser storage");
      },
      preRun: [
        function () {
          try {
            if (typeof FS !== "undefined" && FS.mkdirTree) {
              FS.mkdirTree(romPath);
              FS.mkdirTree(NVRAM_FS_ROOT);
              FS.mkdirTree(STATE_FS_ROOT);
              if (!isArcade) {
                const names = Object.keys(hashDataByFile);
                if (names.length) {
                  FS.mkdirTree(hashPath);
                  names.forEach((file) => {
                    FS.writeFile(hashPath + "/" + file, hashDataByFile[file]);
                  });
                }
              }
              candyFiles.forEach((file) => {
                try {
                  const dir = file.out.includes("/") ? file.out.slice(0, file.out.lastIndexOf("/")) : "";
                  if (dir)
                    FS.mkdirTree(dir);
                  FS.writeFile(file.out, file.data);
                } catch (err) {
                  console.warn("Candy FS write " + file.out + ":", err);
                }
              });
              if (nvramOffline) {
                const n = restoreNvramFromStorage(software);
                if (n)
                  log("Restored NVRAM from browser storage (" + n + " file" + (n === 1 ? "" : "s") + ")");
              }
            }
          } catch (err) {
            console.warn("FS preload:", err);
          }
        }
      ],
      locateFile: (path) => {
        if (path.endsWith(".wasm"))
          return jsPath.replace(/\.js$/, ".wasm") + "?v=" + Date.now();
        return path;
      },
      print: (text) => {
        const s = String(text || "");
        if (dumpInputsRequested && /INPUT_DUMP|INPUT_FRAME/i.test(s))
          log(s);
      },
      printErr: (text) => {
        const s = String(text || "");
        if (/DESYNC|FATAL|abort|error/i.test(s))
          log(s);
        else if (dumpInputsRequested && /INPUT_DUMP|INPUT_FRAME/i.test(s))
          log(s);
      },
      onRuntimeInitialized: () => {
        log("WASM runtime initialized — starting " + software +
          (opts.mamehub ? " (netplay)" : ""));
        installForceInputDumpHook();
        updateSaveStateButtons();
        if (opts.mamehub && !isArcade)
          maybeStartInputScript(opts.isHost ? "host" : "join");
      },
      onAbort: (what) => log("WASM abort: " + what)
    };

    if (net) {
      Module.mamehubNet.isHost = !!opts.isHost;
      Module.mamehubNet.player = opts.player | 0;
      Module.mamehubNet.peerId = opts.peerId || ("p" + (opts.player | 0));
      Module.mamehubNet.peerIds = Array.isArray(opts.peerIds)
        ? opts.peerIds.slice()
        : (typeof net.peerIds === "function" ? net.peerIds() : []);
      Module.mamehubNet.ready = !!net.ready;
    }

    const bootLabel = isArcade
      ? (software + (opts.mamehub ? "; mamehub webrtc" : "; offline"))
      : (machine + " -" + media + " " + software + (opts.mamehub ? "; mamehub webrtc" : "; offline"));
    log("Loading " + jsPath + " (" + bootLabel + ") …");
    await new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = jsPath + "?v=" + Date.now();
      script.onload = () => { log("Loaded emulator script"); resolve(); };
      script.onerror = () => reject(new Error("Failed to load " + jsPath));
      document.body.appendChild(script);
    });
    const readyTick = setInterval(() => {
      updateSaveStateButtons();
      if (emulatorSaveLoadReady())
        clearInterval(readyTick);
    }, 400);
    setTimeout(() => clearInterval(readyTick), 120000);
  }

  async function startNetplay(software, role, roomOverride) {
    software = (software || "").trim();
    if (!software)
      throw new Error(isArcade ? "No machine shortname" : "No software shortname");

    const isHost = role === "host";
    let roomId = (roomOverride || "").trim();
    const relays = (window.MAMEHUB_BROWSER && window.MAMEHUB_BROWSER.relays) || [];
    log("Nostr discovery via " + relays.join(", "));

    gameStarted = false;
    joinRejected = false;
    hostStartReceived = false;
    joinWelcomed = false;
    joinStartWaiters = [];
    hostBootSoftware = software;
    meshLinkStates.clear();
    prefetchCandyRoms(software, machineForSoftware(software));

    if (isHost) {
      const userId = document.getElementById("userId").value.trim() || "host";
      const hosted = await lobby.host({ game: gameTag, userId, software, started: false });
      roomId = hosted.roomId;
      hostJoinLink = buildJoinLink(roomId, software);
      rosterMembers = [{
        pubkey: lobby.pubkey,
        peerId: "p0",
        player: 0,
        userId,
        software
      }];
      mySeatPlayer = 0;
      mySeatPeerId = "p0";
      showHostLobby(software);
      log("Published Nostr lobby announce room=" + roomId + " soft=" + software);
      log("Join link ready — Start when the mesh is fully green.");
    } else {
      if (!roomId)
        throw new Error("Missing room id (open a join link)");
      await lobby.join(roomId);
      rosterMembers = [];
      showJoinLobby(software, "Looking for host…");
      log("Subscribed to Nostr room=" + roomId + " soft=" + software);
    }

    const net = new MamehubWebRtcNetplay({
      roomId,
      isHost,
      player: isHost ? 0 : 1,
      peerId: isHost ? "p0" : "p1",
      selfPubkey: lobby.pubkey,
      fakeLagMs: fakeLagConfig.ms,
      fakeLagJitterMs: fakeLagConfig.jitterMs,
      fakeLagDrop: fakeLagConfig.drop
    });
    window.__mamehubNet = net;
    if (fakeLagConfig.enabled)
      log("WebRTC fakelag: " + fakeLagConfig.ms + "ms one-way ≈ " +
        (fakeLagConfig.ms * 2) + "ms RTT" +
        (fakeLagConfig.drop ? (" drop≈" + Math.round(fakeLagConfig.drop * 100) + "%") : ""));
    net.onLocalSignal = async (type, payload, toPubkey) => {
      await lobby.signal(type, payload, toPubkey || undefined);
      log("Published Nostr " + type + (toPubkey ? (" → " + toPubkey.slice(0, 8)) : ""));
    };
    net.onReady = () => {
      if (isHost) {
        const waiting = document.getElementById("hostLobbyWaiting");
        if (waiting)
          waiting.textContent = "Full mesh connected — press Start game when ready.";
        renderHostLobby();
      } else {
        setJoinLobbyState("Mesh connected — waiting for host to start…");
      }
    };

    if (isHost) {
      log("WebRTC mesh host ready — waiting for joiners on Nostr…");
      await syncMeshFromRoster();
      return;
    }

    let helloTimer = null;
    const userId = document.getElementById("userId").value.trim() || "join";
    const sendHello = async () => {
      if (joinRejected || gameStarted || joinWelcomed)
        return;
      try {
        await lobby.hello({ software, userId });
        log("Published Nostr hello");
        setJoinLobbyState("Waiting for host welcome / mesh…");
      } catch (err) {
        log("Nostr hello failed: " + (err && err.message ? err.message : err));
      }
    };
    await sendHello();
    helloTimer = setInterval(sendHello, 4000);
    try {
      if (joinRejected)
        throw new Error("Join rejected");
      const aborted = new Promise((_, reject) => {
        joinAbortReject = reject;
      });
      await Promise.race([net.waitUntilReady(180000), aborted]);
    } finally {
      joinAbortReject = null;
      clearInterval(helloTimer);
    }
    if (joinRejected)
      throw new Error("Join rejected");
    setJoinLobbyState("Mesh connected — waiting for host to start…");
    await waitForHostStart(600000);
    if (joinRejected)
      throw new Error("Join rejected");
    log("Host started — booting synchronized emulator as P" + (mySeatPlayer + 1));
    hideJoinLobby();
    const peerIds = rosterMembers.map((m) => m.peerId);
    await bootEmulator(software, {
      mamehub: true,
      isHost: false,
      player: mySeatPlayer,
      peerId: mySeatPeerId,
      peerIds: peerIds.length ? peerIds : net.peerIds(),
      userId,
      net
    });
  }

  document.getElementById("hostBtn").onclick = () => {
    pendingRole = "host";
    hideModeButtons();
    showSoftwarePicker("host");
  };

  document.getElementById("hostStartGameBtn").onclick = () => {
    hostStartGame().catch((err) =>
      log("Start game failed: " + (err && err.message ? err.message : err)));
  };

  document.getElementById("startOfflineBtn").onclick = () => {
    pendingRole = null;
    hideModeButtons();
    showSoftwarePicker("offline");
  };

  document.getElementById("softwareCancelBtn").onclick = () => {
    document.getElementById("software-picker").hidden = true;
    closeSoftwareAcList();
    if (!joinFromQuery)
      restoreModeButtons();
  };

  document.getElementById("softwarePlayBtn").onclick = () => {
    const shortname = selectedSoftwareShortname();
    if (!shortname)
      return log("Pick a " + pickNoun + " (or type a shortname)");
    document.getElementById("software-picker").hidden = true;
    closeSoftwareAcList();
    const machine = selectedSoftwareMachine();
    const run = pendingRole
      ? startNetplay(shortname, pendingRole)
      : bootEmulator(shortname, { mamehub: false, machine });
    run.catch((err) => log("Start failed: " + (err && err.message ? err.message : err)));
  };

  document.getElementById("copyJoinLinkBtn").onclick = async () => {
    const status = document.getElementById("copyJoinLinkStatus");
    const link = hostJoinLink || (lobby.roomId && hostSoftware
      ? buildJoinLink(lobby.roomId, hostSoftware)
      : "");
    if (!link) {
      if (status)
        status.textContent = "No link yet";
      return;
    }
    try {
      await navigator.clipboard.writeText(link);
      if (status)
        status.textContent = "Copied";
      log("Join link copied");
    } catch (_) {
      // Fallback: select via prompt
      window.prompt("Copy join link:", link);
      if (status)
        status.textContent = "Copy from prompt";
    }
    setTimeout(() => {
      if (status && status.textContent === "Copied")
        status.textContent = "";
    }, 2000);
  };

  const instructionsModal = document.getElementById("instructions-modal");
  document.getElementById("instructionsBtn").onclick = () => {
    instructionsModal.hidden = false;
  };
  document.getElementById("instructionsCloseBtn").onclick = () => {
    instructionsModal.hidden = true;
  };
  instructionsModal.addEventListener("click", (ev) => {
    if (ev.target === instructionsModal)
      instructionsModal.hidden = true;
  });
  window.addEventListener("keydown", (ev) => {
    if (ev.key === "Escape" && !instructionsModal.hidden)
      instructionsModal.hidden = true;
    if (ev.key === "Escape" && isFallbackFullscreen())
      exitFallbackFullscreen();
  });

  const fullscreenBtn = document.getElementById("fullscreenBtn");
  if (fullscreenBtn) {
    fullscreenBtn.onclick = () => {
      toggleGameFullscreen().catch((err) =>
        log("Fullscreen failed: " + (err && err.message ? err.message : err)));
    };
  }
  const fullscreenExitBtn = document.getElementById("fullscreenExitBtn");
  if (fullscreenExitBtn) {
    fullscreenExitBtn.onclick = () => {
      exitGameFullscreen().catch(() => {});
    };
  }

  // Auto-join from shared link: ?join=1&room=…&soft=…
  if (joinFromQuery) {
    pendingRole = "join";
    hideModeButtons();
    showJoinLobby(joinFromQuery.software, "Opening join link…");
    log("Join link detected room=" + joinFromQuery.roomId + " soft=" + joinFromQuery.software);
    startNetplay(joinFromQuery.software, "join", joinFromQuery.roomId).catch((err) => {
      const msg = err && err.message ? err.message : String(err);
      if (!document.getElementById("joinLobbyState")?.textContent?.includes("kicked") &&
          !document.getElementById("joinLobbyState")?.textContent?.includes("cannot join") &&
          !document.getElementById("joinLobbyState")?.textContent?.includes("full") &&
          !document.getElementById("joinLobbyState")?.textContent?.includes("rejected"))
        setJoinLobbyState("Join failed: " + msg);
      log("Join failed: " + msg);
    });
  } else {
    const autoGame = queryPreferredGame();
    const autostart = /^(1|true|yes|on)$/i.test(qs.get("autostart") || qs.get("autoplay") || "");
    if (autostart && autoGame) {
      hideModeButtons();
      log("Autostart " + autoGame);
      bootEmulator(autoGame, { mamehub: false }).catch((err) =>
        log("Autostart failed: " + (err && err.message ? err.message : err)));
    }
  }
})();
