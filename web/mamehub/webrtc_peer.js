// WebRTC DataChannel peer for MAMEHub browser netplay.
// Discovery + SDP/ICE signaling is exclusively via Nostr (onLocalSignal).

(function (global) {
  const cfg = () => global.MAMEHUB_BROWSER || {};

  class WebRtcNetplay {
    constructor(opts) {
      this.roomId = (opts && opts.roomId) || "";
      this.isHost = !!(opts && opts.isHost);
      this.player = this.isHost ? 0 : 1;
      this.pc = null;
      this.channel = null;
      this.ready = false;
      this.onMessage = null;
      this.onLocalSignal = null; // async (type, payload) => publish on Nostr
      this.onReady = null;
      this._receiveHook = null;
      this._pendingCandidates = [];
      this._offerSent = false;
      this.remotePubkey = null;
    }

    setReceiveHook(fn) {
      this._receiveHook = fn;
    }

    send(data) {
      if (this.channel && this.channel.readyState === "open")
        this.channel.send(typeof data === "string" ? data : JSON.stringify(data));
    }

    /** Host: create PC + datachannel; wait for joiner hello before offering. */
    async prepareAsHost() {
      this.pc = new RTCPeerConnection({ iceServers: cfg().iceServers || [] });
      this.channel = this.pc.createDataChannel("mamehub-inputs", { ordered: true });
      this._bindChannel(this.channel);
      this.pc.onicecandidate = (ev) => this._emitIce(ev);
    }

    /** Host: create and publish offer (call after joiner hello, or to refresh). */
    async createAndSendOffer(toPubkey) {
      if (!this.pc)
        await this.prepareAsHost();
      if (toPubkey)
        this.remotePubkey = toPubkey;
      const offer = await this.pc.createOffer();
      await this.pc.setLocalDescription(offer);
      this._offerSent = true;
      await this._emitSignal("offer", { sdp: offer.sdp, type: offer.type }, this.remotePubkey);
    }

    async acceptOffer(remote, fromPubkey) {
      if (fromPubkey)
        this.remotePubkey = fromPubkey;
      if (!this.pc) {
        this.pc = new RTCPeerConnection({ iceServers: cfg().iceServers || [] });
        this.pc.ondatachannel = (ev) => this._bindChannel(ev.channel);
        this.pc.onicecandidate = (ev) => this._emitIce(ev);
      }
      await this.pc.setRemoteDescription(remote);
      for (const c of this._pendingCandidates.splice(0)) {
        try { await this.pc.addIceCandidate(c); } catch (_) {}
      }
      const answer = await this.pc.createAnswer();
      await this.pc.setLocalDescription(answer);
      await this._emitSignal("answer", { sdp: answer.sdp, type: answer.type }, this.remotePubkey);
    }

    async acceptAnswer(remote) {
      if (!this.pc)
        throw new Error("no peer connection");
      await this.pc.setRemoteDescription(remote);
      for (const c of this._pendingCandidates.splice(0)) {
        try { await this.pc.addIceCandidate(c); } catch (_) {}
      }
    }

    async addIceCandidate(candidate) {
      if (!candidate)
        return;
      if (!this.pc || !this.pc.remoteDescription) {
        this._pendingCandidates.push(candidate);
        return;
      }
      try {
        await this.pc.addIceCandidate(candidate);
      } catch (err) {
        console.warn("ICE candidate error", err);
      }
    }

    waitUntilReady(timeoutMs) {
      const t = timeoutMs || 120000;
      if (this.ready)
        return Promise.resolve();
      return new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error("DataChannel timeout (Nostr signaling)")), t);
        const prev = this.onReady;
        this.onReady = () => {
          clearTimeout(timer);
          if (prev) prev();
          resolve();
        };
      });
    }

    _bindChannel(channel) {
      this.channel = channel;
      channel.binaryType = "arraybuffer";
      channel.onopen = () => {
        this.ready = true;
        console.info("DataChannel open");
        if (this.onReady)
          this.onReady();
      };
      channel.onmessage = (ev) => {
        const data = typeof ev.data === "string" ? ev.data : "";
        if (this._receiveHook)
          this._receiveHook(data);
        if (this.onMessage)
          this.onMessage(data);
      };
      channel.onclose = () => { this.ready = false; };
    }

    async _emitSignal(type, payload, toPubkey) {
      if (!this.onLocalSignal)
        throw new Error("Nostr signaling handler not set");
      await this.onLocalSignal(type, payload, toPubkey || this.remotePubkey || undefined);
    }

    _emitIce(ev) {
      if (ev.candidate)
        this._emitSignal("candidate", ev.candidate.toJSON(), this.remotePubkey);
    }

    close() {
      try { if (this.channel) this.channel.close(); } catch (_) {}
      try { if (this.pc) this.pc.close(); } catch (_) {}
      this.ready = false;
    }
  }

  global.MamehubWebRtcNetplay = WebRtcNetplay;
})(window);
