(function () {
  const logEl = document.getElementById("log");
  const log = (msg) => {
    logEl.textContent += msg + "\n";
    logEl.scrollTop = logEl.scrollHeight;
    console.log(msg);
  };

  const qs = new URLSearchParams(window.location.search || "");
  const muteRequested = (() => {
    const mute = (qs.get("mute") || "").toLowerCase();
    if (mute === "1" || mute === "true" || mute === "yes" || mute === "on")
      return true;
    if (qs.has("nosound") || qs.get("sound") === "0" || qs.get("sound") === "none")
      return true;
    const vol = qs.get("volume");
    return vol !== null && vol !== "";
  })();
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
  const MAX_LOBBY_PLAYERS = 5;

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

  function buildJoinLink(roomId, software) {
    const u = new URL(window.location.href);
    u.search = "";
    u.hash = "";
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
        game: "snes",
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
    await bootSnes(soft, {
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

  let snesCatalog = null;
  let snesCatalogPromise = null;
  let softwareAcActive = -1;
  let softwareAcFiltered = [];

  function decodeXmlText(s) {
    return String(s || "")
      .replace(/&amp;/g, "&")
      .replace(/&lt;/g, "<")
      .replace(/&gt;/g, ">")
      .replace(/&quot;/g, '"')
      .replace(/&#39;/g, "'");
  }

  async function loadSnesCatalog() {
    if (snesCatalog)
      return snesCatalog;
    if (snesCatalogPromise)
      return snesCatalogPromise;
    const cfg = window.MAMEHUB_BROWSER || {};
    const hashUrl = cfg.hashUrl || "hash/snes.xml";
    snesCatalogPromise = (async () => {
      log("Loading SNES softlist catalog…");
      const resp = await fetch(hashUrl);
      if (!resp.ok)
        throw new Error("softlist fetch HTTP " + resp.status);
      const text = await resp.text();
      const entries = [];
      const re = /<software\s+name="([^"]+)"[^>]*>[\s\S]*?<description>([^<]*)<\/description>/gi;
      let m;
      while ((m = re.exec(text)) !== null) {
        const id = m[1];
        const title = decodeXmlText(m[2]).trim() || id;
        entries.push({ id, title, titleLower: title.toLowerCase(), idLower: id.toLowerCase() });
      }
      entries.sort((a, b) => a.title.localeCompare(b.title) || a.id.localeCompare(b.id));
      snesCatalog = entries;
      log("SNES catalog: " + entries.length + " titles");
      return entries;
    })().catch((err) => {
      snesCatalogPromise = null;
      throw err;
    });
    return snesCatalogPromise;
  }

  function softwareAcPrefixMatch(entry, q) {
    if (!q)
      return true;
    if (entry.idLower.startsWith(q) || entry.titleLower.startsWith(q))
      return true;
    // Match word prefixes inside the title ("mario" → "Super Mario World")
    const parts = entry.titleLower.split(/[^a-z0-9+]+/);
    for (const p of parts) {
      if (p && p.startsWith(q))
        return true;
    }
    return false;
  }

  function closeSoftwareAcList() {
    const list = document.getElementById("softwareAcList");
    const input = document.getElementById("softwareAcInput");
    if (list)
      list.hidden = true;
    if (input)
      input.setAttribute("aria-expanded", "false");
    softwareAcActive = -1;
  }

  function renderSoftwareAcList(query) {
    const list = document.getElementById("softwareAcList");
    const input = document.getElementById("softwareAcInput");
    if (!list || !input || !snesCatalog)
      return;
    const q = (query || "").trim().toLowerCase();
    softwareAcFiltered = [];
    for (const entry of snesCatalog) {
      if (softwareAcPrefixMatch(entry, q)) {
        softwareAcFiltered.push(entry);
        if (softwareAcFiltered.length >= 80)
          break;
      }
    }
    list.innerHTML = "";
    if (!softwareAcFiltered.length) {
      const li = document.createElement("li");
      li.className = "ac-empty";
      li.textContent = q ? "No matches" : "No software loaded";
      list.appendChild(li);
    } else {
      softwareAcFiltered.forEach((entry, idx) => {
        const li = document.createElement("li");
        li.setAttribute("role", "option");
        li.id = "softwareAcOpt" + idx;
        li.dataset.id = entry.id;
        li.innerHTML = '<span class="ac-title"></span><span class="ac-id"></span>';
        li.querySelector(".ac-title").textContent = entry.title;
        li.querySelector(".ac-id").textContent = entry.id;
        li.addEventListener("mousedown", (ev) => {
          ev.preventDefault();
          selectSoftwareAc(entry);
        });
        list.appendChild(li);
      });
    }
    softwareAcActive = softwareAcFiltered.length ? 0 : -1;
    updateSoftwareAcActive();
    list.hidden = false;
    input.setAttribute("aria-expanded", "true");
  }

  function updateSoftwareAcActive() {
    const list = document.getElementById("softwareAcList");
    if (!list)
      return;
    const items = list.querySelectorAll('li[role="option"]');
    items.forEach((li, idx) => {
      const on = idx === softwareAcActive;
      li.setAttribute("aria-selected", on ? "true" : "false");
      if (on)
        li.scrollIntoView({ block: "nearest" });
    });
  }

  function selectSoftwareAc(entry) {
    const input = document.getElementById("softwareAcInput");
    if (!input || !entry)
      return;
    input.value = entry.title + " (" + entry.id + ")";
    input.dataset.shortname = entry.id;
    closeSoftwareAcList();
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
      if (snesCatalog)
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
    if (mode === "host")
      title.textContent = "Host game — Select SNES software";
    else if (mode === "join")
      title.textContent = "Join game — Select SNES software";
    else
      title.textContent = "Offline — Select SNES software";
    picker.hidden = false;
    try {
      await loadSnesCatalog();
      if (input) {
        const prefer = (mode === "host" || mode === "join") ? "smkart" : "smw";
        const entry = snesCatalog.find((e) => e.id === prefer) || snesCatalog[0];
        if (entry)
          selectSoftwareAc(entry);
        input.focus();
        renderSoftwareAcList("");
      }
    } catch (err) {
      log("Catalog load failed: " + (err && err.message ? err.message : err));
    }
    if (mode === "host")
      log("Host — select SNES software, then Play. Share the join link; start when a player connects.");
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
      "-throttle",
      "-video", "opengl",
      "-nowaitvsync",
      "-nosyncrefresh",
      "-nodiscord"
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
          return jsPath.replace(/\.js$/, ".wasm") + "?v=" + Date.now();
        return path;
      },
      print: () => {},
      printErr: (text) => {
        const s = String(text || "");
        if (/DESYNC|FATAL|abort|error/i.test(s))
          log(s);
      },
      onRuntimeInitialized: () => {
        log("WASM runtime initialized — candy loading " + software +
          (opts.mamehub ? " (netplay)" : ""));
        if (opts.mamehub)
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

  async function startNetplaySnes(software, role, roomOverride) {
    software = (software || "").trim();
    if (!software)
      throw new Error("No software shortname");

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

    if (isHost) {
      const userId = document.getElementById("userId").value.trim() || "host";
      const hosted = await lobby.host({ game: "snes", userId, software, started: false });
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
      log("Published Nostr lobby announce room=" + roomId + " cart=" + software);
      log("Join link ready — Start when the mesh is fully green.");
    } else {
      if (!roomId)
        throw new Error("Missing room id (open a join link)");
      await lobby.join(roomId);
      rosterMembers = [];
      showJoinLobby(software, "Looking for host…");
      log("Subscribed to Nostr room=" + roomId + " cart=" + software);
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
    await bootSnes(software, {
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
      return log("Pick a SNES title (or type a softlist shortname)");
    document.getElementById("software-picker").hidden = true;
    closeSoftwareAcList();
    const run = pendingRole
      ? startNetplaySnes(shortname, pendingRole)
      : bootSnes(shortname, { mamehub: false });
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
  });

  // Auto-join from shared link: ?join=1&room=…&soft=…
  if (joinFromQuery) {
    pendingRole = "join";
    hideModeButtons();
    showJoinLobby(joinFromQuery.software, "Opening join link…");
    log("Join link detected room=" + joinFromQuery.roomId + " soft=" + joinFromQuery.software);
    startNetplaySnes(joinFromQuery.software, "join", joinFromQuery.roomId).catch((err) => {
      const msg = err && err.message ? err.message : String(err);
      if (!document.getElementById("joinLobbyState")?.textContent?.includes("kicked") &&
          !document.getElementById("joinLobbyState")?.textContent?.includes("cannot join") &&
          !document.getElementById("joinLobbyState")?.textContent?.includes("full") &&
          !document.getElementById("joinLobbyState")?.textContent?.includes("rejected"))
        setJoinLobbyState("Join failed: " + msg);
      log("Join failed: " + msg);
    });
  }
})();
