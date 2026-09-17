import { chromium } from "playwright";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const LOCAL_PAD_CANDIDATES = [
  path.resolve(__dirname, "../virtual_gamepad.js"),
  path.resolve("/Users/jjg/mame-worktrees/mamehub_browser/web/mamehub/virtual_gamepad.js"),
];
const LOCAL_PAD = LOCAL_PAD_CANDIDATES.find((p) => fs.existsSync(p));

const BASE = "https://lobby.mamehub.com/web/mamehub/snes/";
const SOFT = "smkart";
const STEP_MS = 60_000;
const PLAY_MS = 150_000; // attract + 2P menus + P2 wait + drive; host-lag needs catch-up headroom
const DRIVE_MIN_MS = 25_000;
const POLL_MS = 1000;
const MIN_SEND_DELTA = 80; // both seats must publish many input windows while driving
const MIN_PAD_PRESSES = 8;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function qs(extra = {}) {
  const u = new URLSearchParams({ mute: "1", autoscript: "1", dumpinputs: "1", soft: SOFT, ...extra });
  return u.toString();
}

function hostUrl(extra = {}) {
  return BASE + "?" + qs(extra);
}

function joinUrl(roomId, extra = {}) {
  return BASE + "?" + qs({ join: "1", room: roomId, ...extra });
}

async function withTimeout(label, ms, fn) {
  const t0 = Date.now();
  let timer;
  try {
    const result = await Promise.race([
      fn(),
      new Promise((_, reject) => {
        timer = setTimeout(() => reject(new Error(`TIMEOUT ${label} after ${ms}ms`)), ms);
      }),
    ]);
    console.log(`  OK ${label} (${Date.now() - t0}ms)`);
    return result;
  } catch (err) {
    console.error(`  FAIL ${label} (${Date.now() - t0}ms): ${err.message}`);
    throw err;
  } finally {
    clearTimeout(timer);
  }
}

async function pageSnapshot(page, name) {
  const snap = await page.evaluate(() => {
    const log = document.getElementById("log")?.textContent || "";
    const lines = log.split("\n").filter(Boolean);
    const dumpBuf = Array.isArray(window.__mamehubDumpLines) ? window.__mamehubDumpLines : [];
    const dumpLines = dumpBuf.length
      ? dumpBuf.slice()
      : lines.filter((l) => l.indexOf("INPUT_DUMP") === 0);
    const startBtn = document.getElementById("hostStartGameBtn");
    const hostLobbyHidden = !!document.getElementById("host-lobby")?.hidden;
    const startDisabled = hostLobbyHidden ? true : !!startBtn?.disabled;
    const waiting = document.getElementById("hostLobbyWaiting")?.textContent || "";
    const joinState = document.getElementById("joinLobbyState")?.textContent || "";
    const net = window.__mamehubNet;
    const lockstepOk = /lockstep clock ok/i.test(log);
    const raceInputs = /random race inputs|driving \(hold B/i.test(log);
    const driving = /driving \(hold B/i.test(log);
    const wasmUp = /WASM runtime initialized/i.test(log);
    const emulating = /MAMEHub/i.test(document.title) && wasmUp;
    const sendCount = (typeof Module !== "undefined" && Module.__mamehubSendCount) | 0;
    const padPresses = (typeof Module !== "undefined" && Module.__mamehubPadPressCount) | 0;
    const inputWaitEpisodes = (typeof Module !== "undefined" && Module.__mamehubInputWaitEpisodes) | 0;
    const inputWaitSleeps = (typeof Module !== "undefined" && Module.__mamehubInputWaitSleeps) | 0;
    const inputWaitsPerMin = (typeof Module !== "undefined" && Module.__mamehubInputWaitsPerMin) || 0;
    const inputWaitSoftPerMin = (typeof Module !== "undefined" && Module.__mamehubInputWaitSoftPerMin) || 0;
    const inputWaitPctBehind = (typeof Module !== "undefined" && Module.__mamehubInputWaitPctBehind) || 0;
    const inputWaitMinSlackMs = (typeof Module !== "undefined") ? Module.__mamehubInputWaitMinSlackMs : null;
    const inputWaitAvgSlackMs = (typeof Module !== "undefined" && Module.__mamehubInputWaitAvgSlackMs) || 0;

    // Expected SMK drive presses: accelerate (B) + steer (Left/Right), in pad/force/tx.
    const dumpBlob = dumpLines.join("\n");
    // ChronoMap publish/read evidence (not just pad/force sticky state).
    const sticky = window.__mamehubDumpSticky || {};
    const sawChronoStart = !!sticky.chronoStart ||
      /INPUT_DUMP cpp send .*P1 Start=1/i.test(dumpBlob) ||
      /INPUT_DUMP cpp chronomap .*P1 Start=\{p0:1/i.test(dumpBlob);
    const sawChronoB = !!sticky.chronoB ||
      /INPUT_DUMP cpp send .*P[12] B=1/i.test(dumpBlob) ||
      /INPUT_DUMP cpp chronomap .*P[12] B=\{p[01]:1/i.test(dumpBlob);
    const sawPadB = !!sticky.padB || /INPUT_DUMP pad b down/i.test(dumpBlob);
    const sawPadStart = !!sticky.padStart || /INPUT_DUMP pad start down/i.test(dumpBlob);
    const sawPadSteer = !!sticky.padSteer || /INPUT_DUMP pad (left|right) down/i.test(dumpBlob);
    const sawForceB = /INPUT_DUMP force .*P[12] B=1/i.test(dumpBlob);
    const sawForceStart = /INPUT_DUMP force .*P[12] Start=1/i.test(dumpBlob);
    const sawForceSteer = /INPUT_DUMP force .*P[12] (Left|Right)=1/i.test(dumpBlob);
    const sawTxB = !!sticky.txB || /INPUT_DUMP tx .*P[12] B=1/i.test(dumpBlob);
    const sawTxStart = /INPUT_DUMP tx .*P[12] Start=1/i.test(dumpBlob);
    const sawTxSteer = /INPUT_DUMP tx .*P[12] (Left|Right)=1/i.test(dumpBlob);
    const sawTxP1B = /INPUT_DUMP tx .*P1 B=1/i.test(dumpBlob);
    const sawTxP2B = /INPUT_DUMP tx .*P2 B=1/i.test(dumpBlob);
    const sawRxPeerB = /INPUT_DUMP rx .*P[12] B=1/i.test(dumpBlob);
    const sawAccel = sawPadB || sawForceB || sawTxB || sawChronoB;
    const sawSteer = sawPadSteer || sawForceSteer || sawTxSteer;
    const sawStart = sawPadStart || sawForceStart || sawTxStart || sawChronoStart;
    // Host must leave attract with Start; both must accel+steer while driving.
    const expectedPressesOk = sawAccel && sawSteer && dumpLines.length >= 8;
    // Past attract ⇒ script reached race/drive logging (not stuck on PUSH START).
    const pastAttract = !!(raceInputs || driving);

    return {
      title: document.title,
      room: window.__mamehubActiveLobby?.roomId || "",
      ready: !!net?.ready,
      peerCount: typeof net?.peerIds === "function" ? net.peerIds().length : (net?.peerIds?.length || 0),
      peerSlots: net?.peers ? [...net.peers.values()].map((s) => ({
        peerId: s.peerId,
        state: s.state,
        ch: s.channel ? s.channel.readyState : null,
        pc: s.pc ? s.pc.connectionState : null,
        ice: s.pc ? s.pc.iceConnectionState : null,
      })) : [],
      startDisabled,
      startHidden: hostLobbyHidden,
      waiting,
      joinState,
      desync: /INPUT DESYNC|DESYNC DETECTED|\bDESYNC\b/i.test(log),
      frames: (log.match(/emscripten_main_loop: frame=/g) || []).length,
      inputs: (log.match(/INPUT_FRAME/g) || []).length,
      sendCount,
      padPresses,
      inputWaitEpisodes,
      inputWaitSleeps,
      inputWaitsPerMin: Math.round(Number(inputWaitsPerMin) * 10) / 10,
      inputWaitSoftPerMin: Math.round(Number(inputWaitSoftPerMin) * 10) / 10,
      inputWaitPctBehind: Number(inputWaitPctBehind) || 0,
      inputWaitMinSlackMs: inputWaitMinSlackMs == null ? null : Number(inputWaitMinSlackMs),
      inputWaitAvgSlackMs: Number(inputWaitAvgSlackMs) || 0,
      inputWaitSource: (typeof Module !== "undefined" && Module.__mamehubInputWaitSource) || "",
      sleepBegin: log.includes("sleep begin"),
      lockstepOk,
      raceInputs,
      driving,
      inputDumps: dumpLines.length,
      dumpTail: dumpLines.slice(-12),
      sawAccel,
      sawSteer,
      sawStart,
      sawChronoStart,
      sawChronoB,
      pastAttract,
      sawTxB,
      sawTxP1B,
      sawTxP2B,
      sawRxPeerB,
      expectedPressesOk,
      emulating,
      hostStarted: /Host started|boot synchronized|Loading .*mame/i.test(log),
      joinFailed: /Join rejected|join failed|Start game failed|Fatal error|INPUT DESYNC/i.test(log),
      fakelag: /WebRTC fakelag/i.test(log),
      dumpEnabled: /INPUT_DUMP enabled/i.test(log) || !!window.__mamehubDumpInputs,
      meshReady: (!hostLobbyHidden && startBtn && !startBtn.disabled) ||
        /Full mesh connected|Mesh connected/i.test(log + "\n" + waiting + "\n" + joinState),
      roomAnnounce: /Published Nostr lobby announce room=/i.test(log),
      tail: lines.slice(-20),
    };
  }).catch((err) => ({ error: String(err) }));
  return { name, ...snap };
}

async function dumpAll(pages) {
  const snaps = [];
  for (const [name, page] of pages)
    snaps.push(await pageSnapshot(page, name));
  return snaps;
}

async function waitUntil(page, label, pred, ms = STEP_MS) {
  return withTimeout(label, ms, async () => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) {
      const snap = await pageSnapshot(page, label);
      if (snap.desync) throw new Error("DESYNC detected early");
      if (snap.joinFailed) throw new Error("fatal/join failure: " + (snap.tail || []).slice(-3).join(" | "));
      if (await pred(snap, page)) return snap;
      await sleep(POLL_MS);
    }
    throw new Error("predicate never true");
  });
}

async function pickAndHost(page) {
  await page.goto(page._mameUrl, { waitUntil: "domcontentloaded", timeout: STEP_MS });
  await page.waitForSelector("#hostBtn", { timeout: STEP_MS });
  await page.click("#hostBtn");
  await page.waitForSelector("#softwarePlayBtn", { state: "visible", timeout: STEP_MS });
  await page.evaluate((soft) => {
    const input = document.getElementById("softwareAcInput");
    if (input) {
      input.value = soft;
      input.dispatchEvent(new Event("input", { bubbles: true }));
    }
    const items = [...document.querySelectorAll("#softwareAcList [role='option'], #softwareAcList button, #softwareAcList li")];
    const hit = items.find((el) => (el.textContent || "").toLowerCase().includes(soft));
    if (hit) hit.click();
  }, SOFT);
  await page.click("#softwarePlayBtn");
  const snap = await waitUntil(page, "host-room-announce", (s) => !!s.room || s.roomAnnounce);
  return snap.room || (await page.evaluate(() => window.__mamehubActiveLobby?.roomId));
}

async function openJoiner(context, roomId, name, extra = {}) {
  const page = await context.newPage();
  page._mameName = name;
  page._mameUrl = joinUrl(roomId, extra);
  await page.goto(page._mameUrl, { waitUntil: "domcontentloaded", timeout: STEP_MS });
  await page.fill("#userId", name).catch(() => {});
  await waitUntil(page, `${name}-join-subscribe`, (s) =>
    /Subscribed to Nostr room=|Join link detected|Looking for host|Published Nostr hello/i.test((s.tail || []).join("\n")) || s.meshReady || s.ready
  );
  return page;
}

async function waitMeshAndStart(hostPage, peerCount, allPages) {
  await withTimeout(`host-mesh-${peerCount}p`, STEP_MS, async () => {
    let stable = 0;
    const need = 2;
    const t0 = Date.now();
    while (Date.now() - t0 < STEP_MS) {
      const snaps = await dumpAll(allPages);
      if (snaps.some((s) => s.desync)) throw new Error("DESYNC during mesh wait");
      const allReady = snaps.every((s) => s.ready);
      const hostOk = snaps.find((s) => s.name === "host");
      if (allReady && hostOk && !hostOk.startDisabled) {
        stable += 1;
        if (stable >= need) {
          console.log(`  OK host-mesh-${peerCount}p (${Date.now() - t0}ms, stable=${stable})`);
          return snaps;
        }
      } else {
        stable = 0;
      }
      await sleep(POLL_MS);
    }
    throw new Error("mesh never stayed ready on all peers");
  });

  await withTimeout("host-boot", STEP_MS, async () => {
    const t0 = Date.now();
    while (Date.now() - t0 < STEP_MS) {
      const before = await pageSnapshot(hostPage, "prestart");
      if (!before.startDisabled) {
        await hostPage.click("#hostStartGameBtn");
        await sleep(1500);
      }
      const s = await pageSnapshot(hostPage, "boot");
      if (s.desync) throw new Error("DESYNC on boot");
      if (
        s.lockstepOk || s.emulating || s.hostStarted ||
        /Loading .*mamesneshub|Loaded emulator script|Host started game/i.test((s.tail || []).join("\n"))
      ) {
        console.log(`  OK host-boot (${Date.now() - t0}ms)`);
        return s;
      }
      if (/Mesh is not fully connected/i.test((s.tail || []).join("\n"))) {
        await sleep(1000);
        continue;
      }
      await sleep(POLL_MS);
    }
    throw new Error("host never booted after Start");
  });
}

async function playAndWatch(pages, playMs = PLAY_MS) {
  return withTimeout(`play-drive-${playMs}ms`, playMs + STEP_MS, async () => {
    const t0 = Date.now();
    let last = null;
    let driveStart = null;
    let baseline = null;

    while (Date.now() - t0 < playMs) {
      last = await dumpAll(pages);
      const bad = last.find((s) => s.desync || s.joinFailed);
      if (bad) {
        throw new Error(`${bad.name}: ${bad.desync ? "DESYNC" : "fatal"} — ${JSON.stringify(bad.tail?.slice(-5))}`);
      }
      const missingDump = last.find((s) => !s.dumpEnabled);
      if (missingDump && Date.now() - t0 > 5000) {
        throw new Error(`${missingDump.name}: dumpinputs not enabled in log (need ?dumpinputs=1)`);
      }

      const bothRacing = last.length >= 2 && last.every((s) => s.raceInputs || s.driving);
      if (bothRacing && !driveStart) {
        driveStart = Date.now();
        baseline = Object.fromEntries(last.map((s) => [s.name, {
          send: s.sendCount | 0,
          pad: s.padPresses | 0,
          dumps: s.inputDumps | 0,
        }]));
        console.log(`  …both seats entered race/drive @${driveStart - t0}ms`, JSON.stringify(baseline));
      }

      if (driveStart && Date.now() - driveStart >= DRIVE_MIN_MS) {
        const host = last.find((s) => s.name === "host");
        const join = last.find((s) => s.name === "join1") || last.find((s) => s.name !== "host");
        const deltas = last.map((s) => ({
          name: s.name,
          sendDelta: (s.sendCount | 0) - (baseline[s.name]?.send || 0),
          padDelta: (s.padPresses | 0) - (baseline[s.name]?.pad || 0),
          dumpDelta: (s.inputDumps | 0) - (baseline[s.name]?.dumps || 0),
          sawAccel: s.sawAccel,
          sawSteer: s.sawSteer,
          sawTxB: s.sawTxB,
          sawTxP1B: s.sawTxP1B,
          sawTxP2B: s.sawTxP2B,
          expectedPressesOk: s.expectedPressesOk,
          dumpTail: s.dumpTail?.slice(-6),
        }));
        const bothSending = deltas.every((d) => d.sendDelta >= MIN_SEND_DELTA);
        const bothPressing = deltas.every((d) => d.padDelta >= MIN_PAD_PRESSES || d.sendDelta >= MIN_SEND_DELTA);
        const bothLoggedPresses = last.every((s) => s.expectedPressesOk);
        // Host must leave attract with Start; joiner must publish P2 B on the wire.
        const seatsOnWire = !!(host && join && host.sawTxP1B && join.sawTxP2B);
        const hostLeftAttract = !!(host && host.sawStart && host.pastAttract);
        const chronoOk = !!(host && host.sawChronoStart && host.sawChronoB &&
          join && join.sawChronoB);
        if (bothSending && bothPressing && bothLoggedPresses && seatsOnWire &&
            hostLeftAttract && chronoOk) {
          const waitStats = last.map((s) => ({
            name: s.name,
            waitEpisodes: s.inputWaitEpisodes | 0,
            waitsPerMin: s.inputWaitsPerMin || 0,
            softWaitsPerMin: s.inputWaitSoftPerMin || 0,
            pctTimeBehind: s.inputWaitPctBehind || 0,
            minSlackMs: s.inputWaitMinSlackMs,
            avgSlackMs: s.inputWaitAvgSlackMs || 0,
            waitSource: s.inputWaitSource || "",
            desync: !!s.desync,
          }));
          if (waitStats.some((w) => w.desync))
            throw new Error("DESYNC during drive: " + JSON.stringify(waitStats));
          console.log(`  OK dual-drive+ChronoMap+past-attract (${Date.now() - driveStart}ms)`,
            JSON.stringify({
              deltas,
              hostChrono: { start: host.sawChronoStart, b: host.sawChronoB, pastAttract: host.pastAttract },
              joinChrono: { b: join.sawChronoB, pastAttract: join.pastAttract },
              inputWaits: waitStats,
            }));
          return { snaps: last, deltas, drove: true, inputWaits: waitStats };
        }
      }
      await sleep(POLL_MS);
    }

    const summary = (last || []).map((s) => ({
      name: s.name,
      raceInputs: s.raceInputs,
      driving: s.driving,
      pastAttract: s.pastAttract,
      sendCount: s.sendCount,
      padPresses: s.padPresses,
      inputDumps: s.inputDumps,
      sawAccel: s.sawAccel,
      sawSteer: s.sawSteer,
      sawStart: s.sawStart,
      sawChronoStart: s.sawChronoStart,
      sawChronoB: s.sawChronoB,
      sawTxP1B: s.sawTxP1B,
      sawTxP2B: s.sawTxP2B,
      expectedPressesOk: s.expectedPressesOk,
      dumpTail: s.dumpTail?.slice(-8),
      lockstepOk: s.lockstepOk,
    }));
    throw new Error("expected ChronoMap presses / past-attract not seen: " + JSON.stringify(summary));
  });
}

async function installLocalShellRoutes(context) {
  // USE_LOBBY=1 forces production assets from lobby.mamehub.com (no local route overrides).
  if (process.env.USE_LOBBY === "1") {
    console.log("  using lobby assets (USE_LOBBY=1)");
    return;
  }
  const padPath = LOCAL_PAD && fs.existsSync(LOCAL_PAD) ? LOCAL_PAD : null;
  const appPath = padPath ? path.resolve(path.dirname(padPath), "app.js") : null;
  const distDir = padPath ? path.resolve(path.dirname(padPath), "dist") : null;
  const wasmJs = distDir && path.join(distDir, "mamesneshub.js");
  const wasmBin = distDir && path.join(distDir, "mamesneshub.wasm");
  if (!padPath) {
    console.warn("  warn: local virtual_gamepad.js not found; using lobby copy");
    return;
  }
  console.log(`  using local pad script: ${padPath}`);
  await context.route("**/virtual_gamepad.js*", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/javascript; charset=utf-8",
      body: fs.readFileSync(padPath, "utf8"),
    });
  });
  if (appPath && fs.existsSync(appPath)) {
    console.log(`  using local app.js: ${appPath}`);
    await context.route("**/web/mamehub/app.js*", async (route) => {
      await route.fulfill({
        status: 200,
        contentType: "application/javascript; charset=utf-8",
        body: fs.readFileSync(appPath, "utf8"),
      });
    });
  }
  const webrtcPath = padPath ? path.resolve(path.dirname(padPath), "webrtc_peer.js") : null;
  if (webrtcPath && fs.existsSync(webrtcPath)) {
    console.log(`  using local webrtc_peer.js: ${webrtcPath}`);
    await context.route("**/webrtc_peer.js*", async (route) => {
      await route.fulfill({
        status: 200,
        contentType: "application/javascript; charset=utf-8",
        body: fs.readFileSync(webrtcPath, "utf8"),
      });
    });
  }
  if (wasmJs && fs.existsSync(wasmJs) && wasmBin && fs.existsSync(wasmBin)) {
    console.log(`  using local wasm: ${wasmBin}`);
    await context.route("**/mamesneshub.js*", async (route) => {
      await route.fulfill({
        status: 200,
        contentType: "application/javascript; charset=utf-8",
        body: fs.readFileSync(wasmJs, "utf8"),
      });
    });
    await context.route("**/mamesneshub.wasm*", async (route) => {
      await route.fulfill({
        status: 200,
        contentType: "application/wasm",
        body: fs.readFileSync(wasmBin),
      });
    });
  } else {
    console.warn("  warn: local mamesneshub build missing; using lobby wasm");
  }
}

async function runScenario(browser, name, peers) {
  console.log(`\n=== ${name} ===`);
  const context = await browser.newContext({ viewport: { width: 1280, height: 800 } });
  await installLocalShellRoutes(context);
  const pages = [];
  try {
    const hostSpec = peers.find((p) => p.role === "host") || peers[0];
    const hostExtra = {};
    if (hostSpec.fakelag) hostExtra.fakelag = "1";
    if (hostSpec.lag != null) hostExtra.lag = String(hostSpec.lag);

    const hostPage = await context.newPage();
    hostPage._mameName = "host";
    hostPage._mameUrl = hostUrl(hostExtra);
    pages.push(["host", hostPage]);

    const roomId = await withTimeout("host-create", STEP_MS, () => pickAndHost(hostPage));
    console.log(`  room=${roomId}`);

    const joiners = peers.filter((p) => p.role === "join");
    for (let i = 0; i < joiners.length; i++) {
      const j = joiners[i];
      const extra = {};
      if (j.fakelag) extra.fakelag = "1";
      if (j.lag != null) extra.lag = String(j.lag);
      const page = await withTimeout(`open-joiner-${i + 1}`, STEP_MS, () =>
        openJoiner(context, roomId, `join${i + 1}`, extra)
      );
      pages.push([`join${i + 1}`, page]);
    }

    await waitMeshAndStart(hostPage, pages.length, pages);

    for (const [pname, page] of pages) {
      await waitUntil(
        page,
        `${pname}-emulating`,
        (s) => s.lockstepOk || s.emulating ||
          /Loaded emulator script|WASM runtime initialized|lockstep clock ok/i.test((s.tail || []).join("\n")),
        STEP_MS
      );
    }

    const final = await playAndWatch(pages, PLAY_MS);
    const waitReport = (final.inputWaits || final.snaps?.map((s) => ({
      name: s.name,
      waitEpisodes: s.inputWaitEpisodes | 0,
      waitSleeps: s.inputWaitSleeps | 0,
      waitsPerMin: s.inputWaitsPerMin || 0,
      desync: !!s.desync,
    }))) || [];
    const anyDesync = (final.snaps || []).some((s) => s.desync) ||
      waitReport.some((w) => w.desync);
    if (anyDesync)
      throw new Error("DESYNC detected: " + JSON.stringify(waitReport));
    const summary = final.snaps.map((s) => ({
      name: s.name,
      desync: s.desync,
      lockstepOk: s.lockstepOk,
      raceInputs: s.raceInputs,
      driving: s.driving,
      inputDumps: s.inputDumps,
      sawAccel: s.sawAccel,
      sawSteer: s.sawSteer,
      sawTxP1B: s.sawTxP1B,
      sawTxP2B: s.sawTxP2B,
      expectedPressesOk: s.expectedPressesOk,
      sendCount: s.sendCount,
      padPresses: s.padPresses,
      inputWaitEpisodes: s.inputWaitEpisodes,
      inputWaitSleeps: s.inputWaitSleeps,
      inputWaitsPerMin: s.inputWaitsPerMin,
      fakelag: s.fakelag,
    }));
    console.log(`  PASS ${name}`, JSON.stringify({ summary, deltas: final.deltas, inputWaits: waitReport }));
    console.log(`  input-wait rate (episodes/min):`, JSON.stringify(waitReport));
    return { name, ok: true, summary, deltas: final.deltas, inputWaits: waitReport };
  } catch (err) {
    const snaps = await dumpAll(pages).catch(() => []);
    console.error(`  FAIL ${name}: ${err.message}`);
    console.error("  snapshots:", JSON.stringify(snaps, null, 2).slice(0, 12000));
    return { name, ok: false, error: err.message, snaps };
  } finally {
    await context.close().catch(() => {});
  }
}

async function main() {
  const headed = process.env.HEADED === "1";
  const only = (process.env.ONLY || "").trim();
  console.log(`Playwright SMK 2P drive matrix (headed=${headed}, play=${PLAY_MS}ms, driveMin=${DRIVE_MIN_MS}ms)`);
  console.log(`  local pad: ${LOCAL_PAD && fs.existsSync(LOCAL_PAD) ? LOCAL_PAD : "(none)"}`);

  const browser = await chromium.launch({
    headless: !headed,
    args: ["--autoplay-policy=no-user-gesture-required"],
  });

  const scenarios = [
    ["2P host-lag", [
      { role: "host", fakelag: true },
      { role: "join", fakelag: false },
    ]],
    ["2P both-lag", [
      { role: "host", fakelag: true },
      { role: "join", fakelag: true },
    ]],
  ].filter(([name]) => !only || name.includes(only));

  const results = [];
  try {
    for (const [name, peers] of scenarios)
      results.push(await runScenario(browser, name, peers));
  } finally {
    await browser.close();
  }

  console.log("\n=== SUMMARY ===");
  for (const r of results)
    console.log(`${r.ok ? "PASS" : "FAIL"} ${r.name}${r.error ? " — " + r.error : ""}`);
  process.exit(results.some((r) => !r.ok) ? 1 : 0);
}

main().catch((err) => {
  console.error(err);
  process.exit(2);
});
