(function () {
  const logEl = document.getElementById("log");
  const log = (msg) => {
    logEl.textContent += msg + "\n";
    logEl.scrollTop = logEl.scrollHeight;
    console.log(msg);
  };

  const lobby = new MamehubNostrLobby();
  window.__mamehubActiveLobby = lobby;
  document.getElementById("pubkey").textContent = "npub… " + lobby.publicKey.slice(0, 12) + "…";

  let pendingRole = null; // null | "host" | "join"
  let remotePubkey = null;
  let pendingHelloPubkey = null;

  lobby.onSignal = async ({ type, payload, event }) => {
    log("Nostr signal " + type + " from " + event.pubkey.slice(0, 8));
    remotePubkey = event.pubkey;
    const net = window.__mamehubNet;
    try {
      if (type === "hello") {
        pendingHelloPubkey = event.pubkey;
        if (net && net.isHost) {
          log("Joiner hello via Nostr — sending WebRTC offer");
          await net.createAndSendOffer(event.pubkey);
        }
        return;
      }
      if (!net)
        return;
      if (type === "offer" && !net.isHost)
        await net.acceptOffer(payload, event.pubkey);
      else if (type === "answer" && net.isHost)
        await net.acceptAnswer(payload);
      else if (type === "candidate")
        await net.addIceCandidate(payload);
    } catch (err) {
      log("Signal handle error: " + (err && err.message ? err.message : err));
    }
  };

  lobby.onPresence = async ({ payload, event }) => {
    // Also handled via onSignal hello; keep for logging.
    log("Nostr presence from " + event.pubkey.slice(0, 8) +
      (payload && payload.software ? (" soft=" + payload.software) : ""));
  };

  lobby.onAnnounce = (ev) => {
    try {
      const body = JSON.parse(ev.content || "{}");
      log("Nostr announce room=" + (body.roomId || "?") +
        " soft=" + (body.software || "?") + " game=" + (body.game || "?"));
      if (body.software && document.getElementById("softwareCustom"))
        document.getElementById("softwareCustom").placeholder = "host plays " + body.software;
    } catch (_) {}
  };

  const SNES_TITLES = [
    { id: "mariokrt", title: "Super Mario Kart (2P)" },
    { id: "smw", title: "Super Mario World" },
    { id: "zelda3u", title: "The Legend of Zelda: A Link to the Past" },
    { id: "sf2u", title: "Street Fighter II" },
    { id: "dkongc", title: "Donkey Kong Country" },
    { id: "ff3", title: "Final Fantasy III (US) / VI" },
    { id: "chrono", title: "Chrono Trigger" },
    { id: "smas", title: "Super Mario All-Stars" },
    { id: "fzero", title: "F-Zero" },
    { id: "starfox", title: "Star Fox" }
  ];

  function showSoftwarePicker(mode) {
    const picker = document.getElementById("software-picker");
    const select = document.getElementById("softwareSelect");
    if (!select.options.length) {
      for (const entry of SNES_TITLES) {
        const opt = document.createElement("option");
        opt.value = entry.id;
        opt.textContent = entry.title + " (" + entry.id + ")";
        select.appendChild(opt);
      }
    }
    if (mode === "host" || mode === "join")
      select.value = "mariokrt";
    picker.hidden = false;
    if (mode === "host")
      log("Host — select SNES software, then Play. Room id is published on Nostr relays (no central server). Each browser tab has its own Nostr key.");
    else if (mode === "join")
      log("Join — enter room id, select the same cart as host, then Play. Signaling is Nostr-only.");
    else
      log("Offline — select SNES software. Candy fetches snes.zip + the cart.");
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

  async function bootSnes(software, opts) {
    const cfg = window.MAMEHUB_BROWSER || {};
    const jsPath = cfg.wasmJs || "dist/mamesneshub.js";
    const romPath = cfg.romPath || "/roms";
    const hashPath = cfg.hashPath || "/hash";
    const hashUrl = cfg.hashUrl || "hash/snes.xml";
    const canvas = document.getElementById("canvas");
    const placeholder = document.getElementById("canvas-placeholder");

    if (typeof Module !== "undefined" && Module.calledRun) {
      log("MAME already started");
      return;
    }

    software = (software || "").trim();
    if (!software)
      throw new Error("No software shortname");

    const hashData = await loadHashXml(hashUrl);
    placeholder.style.display = "none";
    canvas.style.display = "block";
    canvas.focus();
    if (window.MamehubVirtualGamepad && typeof window.MamehubVirtualGamepad.showForPlay === "function")
      window.MamehubVirtualGamepad.showForPlay();

    const args = [
      "snes",
      "-cart", software,
      "-rompath", romPath,
      "-hashpath", hashPath,
      "-window",
      opts.mamehub ? "-mamehub" : "-nomamehub",
      "-candy",
      "-skip_gameinfo",
      "-nomouse",
      "-nothrottle",
      "-video", "opengl",
      "-nowaitvsync",
      "-nosyncrefresh",
      "-nodiscord"
    ];
    if (opts.userId) {
      args.push("-user_id");
      args.push(opts.userId);
    }

    const net = opts.net || null;
    window.Module = {
      canvas,
      candyProxyBase: cfg.candyProxyBase || "/candy-proxy",
      candyProgress: setCandyProgress,
      arguments: args,
      mamehubNet: net,
      preRun: [
        function () {
          try {
            if (typeof FS !== "undefined" && FS.mkdirTree) {
              FS.mkdirTree(romPath);
              FS.mkdirTree(hashPath);
              FS.writeFile(hashPath + "/snes.xml", hashData);
            }
          } catch (err) {
            console.warn("FS preload:", err);
          }
        }
      ],
      locateFile: (path) => {
        if (path.endsWith(".wasm"))
          return jsPath.replace(/\.js$/, ".wasm");
        return path;
      },
      print: () => {},
      printErr: () => {},
      onRuntimeInitialized: () => log("WASM runtime initialized — candy loading " + software +
        (opts.mamehub ? " (netplay)" : "")),
      onAbort: (what) => log("WASM abort: " + what)
    };

    if (net) {
      Module.mamehubNet.isHost = !!opts.isHost;
      Module.mamehubNet.player = opts.player | 0;
      Module.mamehubNet.ready = !!net.ready;
    }

    log("Loading " + jsPath + " (snes -cart " + software +
      (opts.mamehub ? "; mamehub webrtc" : "; offline") + ") …");
    await new Promise((resolve, reject) => {
      const script = document.createElement("script");
      script.src = jsPath + "?v=" + Date.now();
      script.onload = () => { log("Loaded emulator script"); resolve(); };
      script.onerror = () => reject(new Error("Failed to load " + jsPath));
      document.body.appendChild(script);
    });
  }

  async function startNetplaySnes(software, role) {
    software = (software || "").trim();
    if (!software)
      throw new Error("No software shortname");

    const isHost = role === "host";
    let roomId = document.getElementById("roomId").value.trim();
    const relays = (window.MAMEHUB_BROWSER && window.MAMEHUB_BROWSER.relays) || [];
    log("Nostr discovery via " + relays.join(", "));

    if (isHost) {
      const userId = document.getElementById("userId").value.trim() || "host";
      const game = document.getElementById("game").value.trim() || "snes";
      const hosted = await lobby.host({ game, userId, software });
      roomId = hosted.roomId;
      document.getElementById("roomId").value = roomId;
      log("Published Nostr lobby announce room=" + roomId + " cart=" + software);
      log("Share this room id with the joiner (no central server).");
    } else {
      if (!roomId)
        throw new Error("Enter room id before joining");
      await lobby.join(roomId);
      log("Subscribed to Nostr room=" + roomId + " cart=" + software);
    }

    const net = new MamehubWebRtcNetplay({ roomId, isHost });
    window.__mamehubNet = net;
    net.onLocalSignal = async (type, payload, toPubkey) => {
      await lobby.signal(type, payload, toPubkey || remotePubkey || undefined);
      log("Published Nostr " + type + (toPubkey ? (" → " + toPubkey.slice(0, 8)) : ""));
    };

    if (isHost) {
      await net.prepareAsHost();
      log("WebRTC host ready — waiting for joiner hello on Nostr…");
      if (pendingHelloPubkey) {
        log("Applying queued joiner hello");
        await net.createAndSendOffer(pendingHelloPubkey);
      }
    } else {
      // Retry hello until the host's offer arrives (Nostr is best-effort).
      let helloTimer = null;
      const sendHello = async () => {
        try {
          await lobby.hello({ software });
          log("Published Nostr hello");
        } catch (err) {
          log("Nostr hello failed: " + (err && err.message ? err.message : err));
        }
      };
      await sendHello();
      helloTimer = setInterval(sendHello, 4000);
      try {
        await net.waitUntilReady(180000);
      } finally {
        clearInterval(helloTimer);
      }
      log("DataChannel open — booting synchronized emulator");
      await bootSnes(software, {
        mamehub: true,
        isHost,
        player: 1,
        userId: document.getElementById("userId").value.trim() || "join",
        net
      });
      return;
    }

    await net.waitUntilReady(180000);
    log("DataChannel open — booting synchronized emulator");

    await bootSnes(software, {
      mamehub: true,
      isHost,
      player: isHost ? 0 : 1,
      userId: document.getElementById("userId").value.trim() || (isHost ? "host" : "join"),
      net
    });
  }

  document.getElementById("hostBtn").onclick = () => {
    pendingRole = "host";
    showSoftwarePicker("host");
  };

  document.getElementById("joinBtn").onclick = async () => {
    const roomId = document.getElementById("roomId").value.trim();
    if (!roomId)
      return log("Enter a room id");
    pendingRole = "join";
    try {
      await lobby.join(roomId);
      log("Subscribed to room " + roomId);
      showSoftwarePicker("join");
    } catch (err) {
      log("Join failed: " + err.message);
    }
  };

  document.getElementById("startOfflineBtn").onclick = () => {
    pendingRole = null;
    showSoftwarePicker("offline");
  };

  document.getElementById("softwareCancelBtn").onclick = () => {
    document.getElementById("software-picker").hidden = true;
  };

  document.getElementById("softwarePlayBtn").onclick = () => {
    const shortname = document.getElementById("softwareCustom").value.trim()
      || document.getElementById("softwareSelect").value;
    if (!shortname)
      return log("Pick or type a softlist shortname");
    document.getElementById("software-picker").hidden = true;
    const run = pendingRole
      ? startNetplaySnes(shortname, pendingRole)
      : bootSnes(shortname, { mamehub: false });
    run.catch((err) => log("Start failed: " + (err && err.message ? err.message : err)));
  };
})();
