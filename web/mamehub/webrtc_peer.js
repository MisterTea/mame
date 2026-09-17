// Full-mesh WebRTC DataChannels for MAMEHub browser netplay.
// Discovery + SDP/ICE signaling is exclusively via Nostr (onLocalSignal).
// Lower seat index always creates the offer toward a higher seat (no glare).

(function (global) {
  const cfg = () => global.MAMEHUB_BROWSER || {};
  const LINK_FAIL_MS = 60000;
  const LINK_RETRY_MS = 8000;

  function edgeKey(a, b) {
    return a < b ? (a + "|" + b) : (b + "|" + a);
  }

  function inputMapSignature(payload) {
    try {
      const o = typeof payload === "string" ? JSON.parse(payload) : payload;
      if (!o || o.t !== "put" || !o.m || typeof o.m !== "object")
        return null;
      const parts = [];
      for (const k of Object.keys(o.m)) {
        if (k.indexOf("INPUT/") !== 0)
          continue;
        if (k.indexOf("Start") < 0 && !/\/P[123] B$/.test(k))
          continue;
        const v = o.m[k];
        if (v === undefined || v === null || v === "")
          continue;
        parts.push(k.replace(/^INPUT\/\d+\//, "") + "=" + v);
      }
      parts.sort();
      return parts.length ? parts.join(" ") : "(idle)";
    } catch (_) {
      return null;
    }
  }

  function dumpInputChange(dir, peerId, payload, lastRef) {
    if (!global.__mamehubDumpInputs)
      return;
    const sig = inputMapSignature(payload);
    if (sig === null || sig === lastRef.v)
      return;
    lastRef.v = sig;
    const line = "INPUT_DUMP " + dir + " " + (peerId || "?") + " " + sig;
    try {
      if (typeof global.__mamehubDumpInputLine === "function")
        global.__mamehubDumpInputLine(dir + " " + (peerId || "?") + " " + sig);
      else
        console.log(line);
    } catch (_) {
      console.log(line);
    }
  }

  class WebRtcMeshNetplay {
    constructor(opts) {
      this.roomId = (opts && opts.roomId) || "";
      this.isHost = !!(opts && opts.isHost);
      this.player = (opts && opts.player != null) ? (opts.player | 0) : (this.isHost ? 0 : 1);
      this.peerId = (opts && opts.peerId) || ("p" + this.player);
      this.selfPubkey = (opts && opts.selfPubkey) || "";
      /** @type {Map<string, object>} pubkey → peer slot */
      this.peers = new Map();
      /** @type {Array<{pubkey:string,peerId:string,player:number,userId?:string}>} */
      this.roster = [];
      this.ready = false;
      this.onMessage = null;
      this.onLocalSignal = null; // async (type, payload, toPubkey) =>
      this.onReady = null;
      this.onLinkState = null; // (aPeerId, bPeerId, state) =>
      this.onMeshChange = null;
      this._receiveHook = null;
      // Browser fakelag (native -fake_lag is WGA-only). Applies to DataChannel
      // payloads only — Nostr signaling is not delayed.
      this.fakeLagMs = Math.max(0, (opts && opts.fakeLagMs) | 0);
      this.fakeLagJitterMs = Math.max(0, (opts && opts.fakeLagJitterMs) | 0);
      this.fakeLagDrop = Math.max(0, Math.min(1, Number((opts && opts.fakeLagDrop) || 0) || 0));
      this._sendQ = [];
      this._lagTimer = null;
      this._lagStats = { sent: 0, recv: 0, dropped: 0 };
      this._lastTxInputSig = { v: "" };
      this._lastRxInputSig = { v: "" };
      /** @type {Map<string, string>} edgeKey → connecting|open|failed */
      this.linkStates = new Map();
      /** @type {Map<string, number>} peerId → ChronoMap expiration (put end ms) */
      this._peerCoverage = new Map();
      this._waitStats = {
        episodes: 0,
        softEpisodes: 0,
        samplesBehind: 0,
        samplesSoft: 0,
        samplesTotal: 0,
        lastBehind: false,
        lastSoft: false,
        minSlackMs: null,
        sumSlackMs: 0,
        epochMs: 0,
        timer: null,
      };
    }

    setIdentity({ player, peerId, isHost, selfPubkey } = {}) {
      if (player != null)
        this.player = player | 0;
      if (peerId)
        this.peerId = peerId;
      if (isHost != null)
        this.isHost = !!isHost;
      if (selfPubkey)
        this.selfPubkey = selfPubkey;
    }

    setFakeLag(opts) {
      if (!opts)
        return;
      if (opts.ms != null)
        this.fakeLagMs = Math.max(0, opts.ms | 0);
      if (opts.jitterMs != null)
        this.fakeLagJitterMs = Math.max(0, opts.jitterMs | 0);
      if (opts.drop != null)
        this.fakeLagDrop = Math.max(0, Math.min(1, Number(opts.drop) || 0));
    }

    setReceiveHook(fn) {
      this._receiveHook = fn;
    }

    _notePutCoverage(text, peerIdHint) {
      try {
        if (!text || text.indexOf('"t":"put"') < 0)
          return;
        const idMatch = /"id":"([^"]+)"/.exec(text);
        const bMatch = /"b":(\d+)/.exec(text);
        if (!bMatch)
          return;
        const id = (idMatch && idMatch[1]) || peerIdHint || "";
        if (!id)
          return;
        const b = +bMatch[1];
        if (!(b > 0))
          return;
        const prev = this._peerCoverage.get(id) || 0;
        if (b > prev)
          this._peerCoverage.set(id, b);
      } catch (_) { /* ignore */ }
    }

    _readNetplayMs() {
      try {
        if (typeof Module !== "undefined" && typeof Module._mamehub_browser_netplay_time_ms === "function")
          return Number(Module._mamehub_browser_netplay_time_ms()) || 0;
      } catch (_) { /* ignore */ }
      try {
        if (global.JSMAME && typeof global.JSMAME.netplay_time_ms === "function")
          return Number(global.JSMAME.netplay_time_ms()) || 0;
      } catch (_) { /* ignore */ }
      return 0;
    }

    /**
     * Approx remotes_cover stalls: remote ChronoMap expiration behind shared
     * netplay clock (same predicate as C++ remotes_cover(ts≈now)).
     */
    _publishWaitStats() {
      const st = this._waitStats;
      if (!st.epochMs || st.epochMs < 0)
        return;
      const elapsedMin = Math.max(1 / 60, (Date.now() - st.epochMs) / 60000);
      const perMin = st.episodes / elapsedMin;
      const softPerMin = st.softEpisodes / elapsedMin;
      const pctBehind = st.samplesTotal
        ? Math.round((1000 * st.samplesBehind) / st.samplesTotal) / 10
        : 0;
      const avgSlack = st.samplesTotal ? Math.round(st.sumSlackMs / st.samplesTotal) : 0;
      try {
        if (typeof Module !== "undefined") {
          if (Module.__mamehubInputWaitFromCpp)
            return;
          Module.__mamehubInputWaitEpisodes = st.episodes;
          Module.__mamehubInputWaitSleeps = st.samplesBehind;
          Module.__mamehubInputWaitsPerMin = perMin;
          Module.__mamehubInputWaitSoftPerMin = softPerMin;
          Module.__mamehubInputWaitPctBehind = pctBehind;
          Module.__mamehubInputWaitMinSlackMs = st.minSlackMs;
          Module.__mamehubInputWaitAvgSlackMs = avgSlack;
          Module.__mamehubInputWaitSource = "js-coverage";
        }
      } catch (_) { /* ignore */ }
    }

    _tickInputWaitMonitor() {
      const now = this._readNetplayMs();
      if (now < 1000)
        return;
      // Start the rate clock at first valid netplay time (post barrier).
      if (!this._waitStats.epochMs || this._waitStats.epochMs < 0) {
        this._waitStats.epochMs = Date.now();
        this._waitStats.episodes = 0;
        this._waitStats.softEpisodes = 0;
        this._waitStats.samplesBehind = 0;
        this._waitStats.samplesSoft = 0;
        this._waitStats.samplesTotal = 0;
        this._waitStats.lastBehind = false;
        this._waitStats.lastSoft = false;
        this._waitStats.minSlackMs = null;
        this._waitStats.sumSlackMs = 0;
      }
      let minRemote = Infinity;
      for (const [id, exp] of this._peerCoverage) {
        if (id === this.peerId)
          continue;
        if (exp < minRemote)
          minRemote = exp;
      }
      if (minRemote === Infinity)
        return;
      const slack = minRemote - now;
      const st = this._waitStats;
      st.samplesTotal++;
      st.sumSlackMs += slack;
      if (st.minSlackMs == null || slack < st.minSlackMs)
        st.minSlackMs = slack;
      // Hard: remotes_cover(now) would fail. Soft: <1 frame of slack left.
      const behind = slack <= 0;
      const soft = slack < 16;
      if (behind) {
        st.samplesBehind++;
        if (!st.lastBehind) {
          st.episodes++;
          st.lastBehind = true;
        }
      } else {
        st.lastBehind = false;
      }
      if (soft) {
        st.samplesSoft++;
        if (!st.lastSoft) {
          st.softEpisodes++;
          st.lastSoft = true;
        }
      } else {
        st.lastSoft = false;
      }
      this._publishWaitStats();
      if ((st.samplesTotal % 100) === 0) {
        const elapsedMin = Math.max(1 / 60, (Date.now() - st.epochMs) / 60000);
        console.log("INPUT_WAIT hard_per_min=" + (st.episodes / elapsedMin).toFixed(1) +
          " soft_per_min=" + (st.softEpisodes / elapsedMin).toFixed(1) +
          " pct_behind=" + (st.samplesTotal ? (100 * st.samplesBehind / st.samplesTotal).toFixed(1) : 0) +
          "% min_slack_ms=" + st.minSlackMs +
          " avg_slack_ms=" + Math.round(st.sumSlackMs / st.samplesTotal));
      }
    }

    startInputWaitMonitor() {
      const st = this._waitStats;
      if (st.timer)
        return;
      st.epochMs = -1;
      st.episodes = 0;
      st.softEpisodes = 0;
      st.samplesBehind = 0;
      st.samplesSoft = 0;
      st.samplesTotal = 0;
      st.lastBehind = false;
      st.lastSoft = false;
      st.minSlackMs = null;
      st.sumSlackMs = 0;
      st.timer = setInterval(() => this._tickInputWaitMonitor(), 50);
    }

    stopInputWaitMonitor() {
      const st = this._waitStats;
      if (st.timer) {
        clearInterval(st.timer);
        st.timer = null;
      }
      this._publishWaitStats();
    }

    peerIds() {
      return this.roster.map((m) => m.peerId);
    }

    expectedRemoteCount() {
      return Math.max(0, this.roster.length - 1);
    }

    meshFullyConnected() {
      if (this.expectedRemoteCount() < 1)
        return false;
      for (const m of this.roster) {
        if (m.pubkey === this.selfPubkey || m.peerId === this.peerId)
          continue;
        const slot = this.peers.get(m.pubkey);
        if (!slot || !slot.channel || slot.channel.readyState !== "open")
          return false;
      }
      return true;
    }

    _refreshReady() {
      const was = this.ready;
      this.ready = this.meshFullyConnected();
      if (this.onMeshChange)
        this.onMeshChange();
      if (this.ready && !was) {
        this.startInputWaitMonitor();
        if (this.onReady)
          this.onReady();
      }
    }

    _setLink(a, b, state) {
      if (!a || !b || a === b)
        return;
      const key = edgeKey(a, b);
      const prev = this.linkStates.get(key);
      if (prev === state)
        return;
      this.linkStates.set(key, state);
      if (this.onLinkState)
        this.onLinkState(a, b, state);
      if (this.onMeshChange)
        this.onMeshChange();
    }

    getLinkState(a, b) {
      return this.linkStates.get(edgeKey(a, b)) || "connecting";
    }

    _lagDelayMs() {
      let d = this.fakeLagMs;
      if (this.fakeLagJitterMs > 0)
        d += (Math.random() * this.fakeLagJitterMs) | 0;
      return d;
    }

    _pumpLagQueues() {
      const now = Date.now();
      while (this._sendQ.length && this._sendQ[0].at <= now) {
        const item = this._sendQ.shift();
        this._broadcastRaw(item.data);
      }
      if (this._lagTimer) {
        clearTimeout(this._lagTimer);
        this._lagTimer = null;
      }
      if (this._sendQ.length) {
        this._lagTimer = setTimeout(() => this._pumpLagQueues(),
          Math.max(1, this._sendQ[0].at - Date.now()));
      }
    }

    _broadcastRaw(payload) {
      for (const slot of this.peers.values()) {
        try {
          if (slot.channel && slot.channel.readyState === "open")
            slot.channel.send(payload);
        } catch (_) { /* ignore */ }
      }
    }

    send(data) {
      const payload = typeof data === "string" ? data : JSON.stringify(data);
      dumpInputChange("tx", this.peerId, payload, this._lastTxInputSig);
      this._notePutCoverage(payload, this.peerId);
      const critical = /__NETPLAY_|\"t\":\"ping\"|\"t\":\"pong\"|\"t\":\"clock\"/.test(payload);
      if (!critical && this.fakeLagDrop > 0 && Math.random() < this.fakeLagDrop) {
        this._lagStats.dropped++;
        return;
      }
      this._lagStats.sent++;
      const delay = this._lagDelayMs();
      if (delay <= 0) {
        this._broadcastRaw(payload);
        return;
      }
      this._sendQ.push({ at: Date.now() + delay, data: payload });
      this._pumpLagQueues();
    }

    async _emitSignal(type, payload, toPubkey) {
      if (!this.onLocalSignal)
        throw new Error("Nostr signaling handler not set");
      await this.onLocalSignal(type, payload, toPubkey || undefined);
    }

    _clearFailTimer(slot) {
      if (slot && slot.failTimer) {
        clearTimeout(slot.failTimer);
        slot.failTimer = null;
      }
    }

    _armFailTimer(slot) {
      this._clearFailTimer(slot);
      slot.failTimer = setTimeout(() => {
        if (slot.state === "open")
          return;
        slot.state = "failed";
        this._setLink(this.peerId, slot.peerId, "failed");
        this._refreshReady();
        this._scheduleRetry(slot);
      }, LINK_FAIL_MS);
    }

    _scheduleRetry(slot) {
      if (slot.retryTimer)
        return;
      slot.retryTimer = setTimeout(() => {
        slot.retryTimer = null;
        if (slot.state === "open")
          return;
        this._retryPeer(slot).catch((err) =>
          console.warn("mesh retry failed", slot.peerId, err));
      }, LINK_RETRY_MS);
    }

    async _retryPeer(slot) {
      if (!slot || slot.state === "open")
        return;
      const member = this.roster.find((m) => m.pubkey === slot.pubkey);
      if (!member)
        return;
      // Tear down and renegotiate.
      this._clearFailTimer(slot);
      if (slot.retryTimer) {
        clearTimeout(slot.retryTimer);
        slot.retryTimer = null;
      }
      try { if (slot.channel) slot.channel.close(); } catch (_) {}
      try { if (slot.pc) slot.pc.close(); } catch (_) {}
      slot.pc = null;
      slot.channel = null;
      slot.offerSent = false;
      slot.pendingCandidates = [];
      slot.state = "connecting";
      this._setLink(this.peerId, slot.peerId, "connecting");
      if ((this.player | 0) < (member.player | 0))
        await this._createOfferFor(slot);
      else
        this._armFailTimer(slot);
    }

    _bindChannel(slot, channel) {
      slot.channel = channel;
      channel.binaryType = "arraybuffer";
      channel.onopen = () => {
        slot.state = "open";
        this._clearFailTimer(slot);
        this._setLink(this.peerId, slot.peerId, "open");
        console.info("DataChannel open", slot.peerId,
          this.fakeLagMs ? ("fakelag=" + this.fakeLagMs + "ms") : "");
        this._refreshReady();
      };
      channel.onmessage = (ev) => {
        const data = typeof ev.data === "string" ? ev.data : "";
        this._lagStats.recv++;
        this._notePutCoverage(data, slot.peerId);
        dumpInputChange("rx", this.peerId, data, this._lastRxInputSig);
        if (this._receiveHook)
          this._receiveHook(data);
        if (this.onMessage)
          this.onMessage(data);
      };
      channel.onclose = () => {
        if (slot.state === "open") {
          slot.state = "failed";
          this._setLink(this.peerId, slot.peerId, "failed");
        }
        this._refreshReady();
      };
      channel.onerror = () => {
        if (slot.state !== "open") {
          slot.state = "failed";
          this._setLink(this.peerId, slot.peerId, "failed");
          this._refreshReady();
        }
      };
    }

    _makePc(slot, asOfferer) {
      if (slot.pc)
        return slot.pc;
      const pc = new RTCPeerConnection({ iceServers: cfg().iceServers || [] });
      slot.pc = pc;
      pc.onicecandidate = (ev) => {
        if (!ev.candidate)
          return;
        this._emitSignal("candidate", {
          candidate: ev.candidate.toJSON(),
          fromPeerId: this.peerId,
          toPeerId: slot.peerId
        }, slot.pubkey);
      };
      pc.onconnectionstatechange = () => {
        const st = pc.connectionState;
        if (st === "failed" || st === "closed") {
          slot.state = "failed";
          this._setLink(this.peerId, slot.peerId, "failed");
          this._refreshReady();
        }
      };
      if (asOfferer) {
        const ch = pc.createDataChannel("mamehub-inputs", { ordered: true });
        this._bindChannel(slot, ch);
      } else {
        pc.ondatachannel = (ev) => this._bindChannel(slot, ev.channel);
      }
      return pc;
    }

    _getOrCreateSlot(member) {
      let slot = this.peers.get(member.pubkey);
      if (!slot) {
        slot = {
          pubkey: member.pubkey,
          peerId: member.peerId,
          player: member.player | 0,
          pc: null,
          channel: null,
          state: "connecting",
          offerSent: false,
          pendingCandidates: [],
          failTimer: null,
          retryTimer: null
        };
        this.peers.set(member.pubkey, slot);
      } else {
        slot.peerId = member.peerId;
        slot.player = member.player | 0;
      }
      return slot;
    }

    async _createOfferFor(slot) {
      if (slot.channel && slot.channel.readyState === "open")
        return;
      if (slot.offerSent && slot.pc)
        return;
      if (slot.pc && slot.pc.signalingState !== "stable" && slot.pc.signalingState !== "have-local-offer")
        return;
      this._makePc(slot, true);
      this._setLink(this.peerId, slot.peerId, "connecting");
      this._armFailTimer(slot);
      const offer = await slot.pc.createOffer();
      await slot.pc.setLocalDescription(offer);
      slot.offerSent = true;
      await this._emitSignal("offer", {
        sdp: offer.sdp,
        type: offer.type,
        fromPeerId: this.peerId,
        toPeerId: slot.peerId
      }, slot.pubkey);
    }

    /**
     * Align mesh to roster. Lower seat offers to higher seats.
     * @param {Array<{pubkey:string,peerId:string,player:number,userId?:string}>} roster
     */
    async ensureMesh(roster) {
      this.roster = Array.isArray(roster) ? roster.slice() : [];
      const self = this.roster.find((m) => m.pubkey === this.selfPubkey) ||
        this.roster.find((m) => m.peerId === this.peerId);
      if (self) {
        this.player = self.player | 0;
        this.peerId = self.peerId;
      }

      const want = new Set();
      for (const m of this.roster) {
        if (!m.pubkey || m.pubkey === this.selfPubkey)
          continue;
        want.add(m.pubkey);
        const slot = this._getOrCreateSlot(m);
        if ((this.player | 0) < (m.player | 0)) {
          try {
            await this._createOfferFor(slot);
          } catch (err) {
            console.warn("mesh offer failed", m.peerId, err);
            slot.state = "failed";
            this._setLink(this.peerId, slot.peerId, "failed");
          }
        } else {
          // Wait for their offer; mark connecting until then.
          if (!slot.pc) {
            this._setLink(this.peerId, slot.peerId, slot.state === "open" ? "open" : "connecting");
            this._armFailTimer(slot);
          }
        }
      }

      for (const [pubkey, slot] of [...this.peers.entries()]) {
        if (!want.has(pubkey))
          this.removePeerByPubkey(pubkey);
      }
      // After reseat, refresh edge labels for already-open channels.
      for (const slot of this.peers.values()) {
        if (slot.channel && slot.channel.readyState === "open") {
          slot.state = "open";
          this._setLink(this.peerId, slot.peerId, "open");
        }
      }
      this._refreshReady();
    }

    async acceptOffer(remote, fromPubkey) {
      const fromPeerId = (remote && remote.fromPeerId) || "";
      const toPeerId = (remote && remote.toPeerId) || "";
      if (toPeerId && toPeerId !== this.peerId)
        return;
      let slot = fromPubkey ? this.peers.get(fromPubkey) : null;
      if (!slot && fromPeerId) {
        for (const s of this.peers.values()) {
          if (s.peerId === fromPeerId) {
            slot = s;
            break;
          }
        }
      }
      if (!slot && fromPubkey) {
        slot = this._getOrCreateSlot({
          pubkey: fromPubkey,
          peerId: fromPeerId || ("p?"),
          player: 999
        });
      }
      if (!slot)
        return;
      if (fromPubkey)
        slot.pubkey = fromPubkey;
      if (fromPeerId)
        slot.peerId = fromPeerId;
      // Ignore duplicate offers once connected.
      if (slot.channel && slot.channel.readyState === "open")
        return;
      // If a prior attempt is half-open / failed, tear down and accept fresh offer.
      const ice = slot.pc && slot.pc.iceConnectionState;
      const stale = slot.state === "failed" ||
        ice === "failed" || ice === "disconnected" || ice === "closed" ||
        (slot.pc && slot.pc.connectionState === "failed") ||
        (slot.pc && slot.pc.signalingState === "have-local-offer");
      if (stale && slot.pc) {
        this._clearFailTimer(slot);
        if (slot.retryTimer) {
          clearTimeout(slot.retryTimer);
          slot.retryTimer = null;
        }
        try { if (slot.channel) slot.channel.close(); } catch (_) {}
        try { if (slot.pc) slot.pc.close(); } catch (_) {}
        slot.pc = null;
        slot.channel = null;
        slot.offerSent = false;
        slot.pendingCandidates = [];
        slot.state = "connecting";
      } else if (slot.pc && slot.pc.remoteDescription && slot.pc.signalingState === "stable") {
        return;
      } else if (slot.pc && (slot.pc.signalingState === "have-remote-offer" ||
          slot.pc.signalingState === "have-local-pranswer")) {
        return;
      }
      this._makePc(slot, false);
      this._setLink(this.peerId, slot.peerId, "connecting");
      this._armFailTimer(slot);
      await slot.pc.setRemoteDescription({ type: remote.type || "offer", sdp: remote.sdp });
      for (const c of slot.pendingCandidates.splice(0)) {
        try { await slot.pc.addIceCandidate(c); } catch (_) {}
      }
      const answer = await slot.pc.createAnswer();
      await slot.pc.setLocalDescription(answer);
      await this._emitSignal("answer", {
        sdp: answer.sdp,
        type: answer.type,
        fromPeerId: this.peerId,
        toPeerId: slot.peerId
      }, slot.pubkey);
    }

    async acceptAnswer(remote, fromPubkey) {
      const toPeerId = (remote && remote.toPeerId) || "";
      if (toPeerId && toPeerId !== this.peerId)
        return;
      let slot = fromPubkey ? this.peers.get(fromPubkey) : null;
      if (!slot && remote && remote.fromPeerId) {
        for (const s of this.peers.values()) {
          if (s.peerId === remote.fromPeerId) {
            slot = s;
            break;
          }
        }
      }
      if (!slot || !slot.pc)
        return;
      if (slot.pc.signalingState !== "have-local-offer")
        return;
      await slot.pc.setRemoteDescription({ type: remote.type || "answer", sdp: remote.sdp });
      for (const c of slot.pendingCandidates.splice(0)) {
        try { await slot.pc.addIceCandidate(c); } catch (_) {}
      }
    }

    async addIceCandidate(payload, fromPubkey) {
      if (!payload)
        return;
      const cand = payload.candidate || payload;
      const toPeerId = payload.toPeerId || "";
      if (toPeerId && toPeerId !== this.peerId)
        return;
      let slot = fromPubkey ? this.peers.get(fromPubkey) : null;
      if (!slot && payload.fromPeerId) {
        for (const s of this.peers.values()) {
          if (s.peerId === payload.fromPeerId) {
            slot = s;
            break;
          }
        }
      }
      if (!slot)
        return;
      if (!slot.pc || !slot.pc.remoteDescription) {
        slot.pendingCandidates.push(cand);
        return;
      }
      try {
        await slot.pc.addIceCandidate(cand);
      } catch (err) {
        console.warn("ICE candidate error", err);
      }
    }

    removePeerByPubkey(pubkey) {
      const slot = this.peers.get(pubkey);
      if (!slot)
        return;
      this._clearFailTimer(slot);
      if (slot.retryTimer) {
        clearTimeout(slot.retryTimer);
        slot.retryTimer = null;
      }
      try { if (slot.channel) slot.channel.close(); } catch (_) {}
      try { if (slot.pc) slot.pc.close(); } catch (_) {}
      this.linkStates.delete(edgeKey(this.peerId, slot.peerId));
      this.peers.delete(pubkey);
      this._refreshReady();
    }

    removePeer(peerId) {
      for (const [pubkey, slot] of this.peers) {
        if (slot.peerId === peerId) {
          this.removePeerByPubkey(pubkey);
          return;
        }
      }
    }

    waitUntilReady(timeoutMs) {
      const t = timeoutMs || 180000;
      if (this.ready)
        return Promise.resolve();
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("Mesh DataChannel timeout")), t);
        const prev = this.onReady;
        this.onReady = () => {
          clearTimeout(timer);
          if (prev)
            prev();
          resolve();
        };
      });
    }

    close() {
      this.stopInputWaitMonitor();
      if (this._lagTimer) {
        clearTimeout(this._lagTimer);
        this._lagTimer = null;
      }
      this._sendQ = [];
      for (const pubkey of [...this.peers.keys()])
        this.removePeerByPubkey(pubkey);
      this.peers.clear();
      this.linkStates.clear();
      this._peerCoverage.clear();
      this.ready = false;
      this.roster = [];
    }

    /** @deprecated single-peer API — mesh uses ensureMesh */
    async prepareAsHost() {}

    /** @deprecated */
    async createAndSendOffer() {}

    /** @deprecated */
    async resetAsHost() {
      this.close();
    }

    /** Compat: first remote pubkey if any */
    get remotePubkey() {
      for (const s of this.peers.values())
        return s.pubkey;
      return null;
    }
  }

  global.MamehubWebRtcNetplay = WebRtcMeshNetplay;
})(window);
