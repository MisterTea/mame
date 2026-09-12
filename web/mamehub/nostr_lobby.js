// Nostr lobby: discovery + WebRTC signaling (no central server).
// Game packets stay on the WebRTC DataChannel.
//
// Uses addressable kinds (30078) so relays *store* events — ephemeral 2xxxx
// kinds are push-only and unreliable when a peer subscribes slightly late.

(function (global) {
  const {
    finalizeEvent,
    generateSecretKey,
    getPublicKey,
    SimplePool,
    verifyEvent
  } = global.NostrTools;

  const cfg = () => global.MAMEHUB_BROWSER || {};
  // Application-specific addressable events (NIP-33). Stored by relays.
  const KIND_APP = 30078;

  function bytesToHex(bytes) {
    return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
  }

  function hexToBytes(hex) {
    const out = new Uint8Array(hex.length / 2);
    for (let i = 0; i < out.length; i++)
      out[i] = parseInt(hex.substr(i * 2, 2), 16);
    return out;
  }

  function randomRoomId() {
    const bytes = new Uint8Array(8);
    crypto.getRandomValues(bytes);
    return bytesToHex(bytes);
  }

  function loadOrCreateSecretKey() {
    // Per-tab identity so host + joiner in the same browser profile are distinct.
    const storageKey = "mamehub_nostr_sk_hex";
    let hex = sessionStorage.getItem(storageKey);
    if (!hex) {
      hex = bytesToHex(generateSecretKey());
      sessionStorage.setItem(storageKey, hex);
    }
    return hexToBytes(hex);
  }

  class NostrLobby {
    constructor() {
      this.relays = cfg().relays || [];
      this.sk = loadOrCreateSecretKey();
      this.pubkey = getPublicKey(this.sk);
      this.roomId = null;
      this.pool = new SimplePool();
      this._subs = [];
      this._seen = new Set();
      this.onSignal = null;
      this.onAnnounce = null;
      this.onPresence = null;
      this._pollTimer = null;
    }

    get publicKey() {
      return this.pubkey;
    }

    async publish(template) {
      if (!this.relays.length)
        throw new Error("No Nostr relays configured (MAMEHUB_BROWSER.relays)");
      const event = finalizeEvent(template, this.sk);
      if (!verifyEvent(event))
        throw new Error("signed event failed verification");
      const pubs = this.pool.publish(this.relays, event);
      const results = await Promise.allSettled(
        pubs.map((p) => Promise.race([
          p,
          new Promise((_, rej) => setTimeout(() => rej(new Error("relay timeout")), 8000))
        ]))
      );
      const ok = results.some((r) => r.status === "fulfilled");
      if (!ok)
        throw new Error("Failed to publish to any Nostr relay (" + this.relays.join(", ") + ")");
      return event;
    }

    async host({ game, userId, software }) {
      this.roomId = randomRoomId();
      const content = JSON.stringify({
        type: "announce",
        roomId: this.roomId,
        game: game || "snes",
        software: software || "",
        userId: userId || "",
        pubkey: this.pubkey
      });
      const event = await this.publish({
        kind: KIND_APP,
        created_at: Math.floor(Date.now() / 1000),
        tags: [
          ["d", "announce:" + this.roomId],
          ["app", cfg().appTag || "mamehub-browser-v1"],
          ["r", this.roomId],
          ["type", "announce"],
          ["game", game || "snes"],
          ["soft", software || ""],
          ["uid", userId || ""],
          ["expiration", String(Math.floor(Date.now() / 1000) + 3600)]
        ],
        content
      });
      this._subscribeRoom(this.roomId);
      this._startPoll(this.roomId);
      return { roomId: this.roomId, event };
    }

    async join(roomId) {
      this.roomId = roomId;
      this._subscribeRoom(roomId);
      this._startPoll(roomId);
      return { roomId };
    }

    async hello({ software } = {}) {
      return this.signal("hello", {
        software: software || "",
        pubkey: this.pubkey,
        ts: Date.now()
      });
    }

    async signal(type, payload, toPubkey) {
      if (!this.roomId)
        throw new Error("no room");
      const d = type + ":" + this.roomId + ":" + Date.now() + ":" + Math.random().toString(16).slice(2, 8);
      const tags = [
        ["d", d],
        ["app", cfg().appTag || "mamehub-browser-v1"],
        ["r", this.roomId],
        ["type", type],
        ["expiration", String(Math.floor(Date.now() / 1000) + 600)]
      ];
      if (toPubkey)
        tags.push(["p", toPubkey]);
      const body = Object.assign({}, payload || {}, { type });
      return this.publish({
        kind: KIND_APP,
        created_at: Math.floor(Date.now() / 1000),
        tags,
        content: JSON.stringify(body)
      });
    }

    _appTag() {
      return cfg().appTag || "mamehub-browser-v1";
    }

    _isOurApp(ev) {
      const app = this._appTag();
      const tag = (ev.tags.find((t) => t[0] === "app") || [])[1];
      return tag === app;
    }

    _handleEvent(ev) {
      if (!ev || ev.pubkey === this.pubkey)
        return;
      if (!this._isOurApp(ev))
        return;
      if (this._seen.has(ev.id))
        return;
      this._seen.add(ev.id);
      const typeTag = (ev.tags.find((t) => t[0] === "type") || [])[1];
      let payload = {};
      try { payload = JSON.parse(ev.content); } catch (_) {}
      const type = typeTag || payload.type;
      if (type === "announce" && this.onAnnounce)
        this.onAnnounce(ev);
      if (type === "hello" && this.onPresence)
        this.onPresence({ payload, event: ev });
      if (type && type !== "announce" && this.onSignal)
        this.onSignal({ type, payload, event: ev });
    }

    // Many public relays mishandle AND of multiple #tag filters (e.g. #app+#r
    // returns empty while #r alone works). Filter room on-relay; app client-side.
    _roomFilter(roomId, sinceSec) {
      return {
        kinds: [KIND_APP],
        "#r": [roomId],
        since: sinceSec
      };
    }

    _subscribeRoom(roomId) {
      for (const sub of this._subs) {
        try { sub.close(); } catch (_) {}
      }
      this._subs = [];
      const since = Math.floor(Date.now() / 1000) - 900;
      const sub = this.pool.subscribeMany(this.relays, this._roomFilter(roomId, since), {
        onevent: (ev) => this._handleEvent(ev),
        oneose: () => {},
        onclose: () => {}
      });
      this._subs.push(sub);
    }

    _startPoll(roomId) {
      if (this._pollTimer)
        clearInterval(this._pollTimer);
      const poll = async () => {
        try {
          const since = Math.floor(Date.now() / 1000) - 120;
          const events = await this.pool.querySync(
            this.relays,
            this._roomFilter(roomId, since),
            { maxWait: 4000 }
          );
          for (const ev of events || [])
            this._handleEvent(ev);
        } catch (_) {}
      };
      poll();
      this._pollTimer = setInterval(poll, 3000);
    }

    close() {
      if (this._pollTimer)
        clearInterval(this._pollTimer);
      this._pollTimer = null;
      for (const sub of this._subs) {
        try { sub.close(); } catch (_) {}
      }
      this._subs = [];
      try { this.pool.close(this.relays); } catch (_) {}
    }
  }

  global.MamehubNostrLobby = NostrLobby;
})(window);
