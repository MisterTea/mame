/**
 * Record host+joiner SMK autoscript with video + periodic canvas screenshots
 * so we can see whether 1P or 2P mode was selected.
 */
import { chromium } from "playwright";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(__dirname, "..");
const BASE = "https://lobby.mamehub.com/web/mamehub/snes/";
const OUT = "/tmp/mamehub-smk-record";
const RECORD_MS = 75_000;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function qs(extra = {}) {
  return new URLSearchParams({
    mute: "1",
    autoscript: "1",
    dumpinputs: "1",
    soft: "smkart",
    ...extra,
  }).toString();
}

async function routeLocal(ctx) {
  for (const f of ["virtual_gamepad.js", "app.js", "webrtc_peer.js"]) {
    const p = path.join(ROOT, f);
    await ctx.route(`**/${f}*`, async (route) => {
      await route.fulfill({
        status: 200,
        contentType: "application/javascript; charset=utf-8",
        body: fs.readFileSync(p, "utf8"),
      });
    });
  }
  const js = path.join(ROOT, "dist/mamesneshub.js");
  const wasm = path.join(ROOT, "dist/mamesneshub.wasm");
  await ctx.route("**/mamesneshub.js*", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/javascript; charset=utf-8",
      body: fs.readFileSync(js, "utf8"),
    });
  });
  await ctx.route("**/mamesneshub.wasm*", async (route) => {
    await route.fulfill({
      status: 200,
      contentType: "application/wasm",
      body: fs.readFileSync(wasm),
    });
  });
}

async function shot(page, label, i) {
  const file = path.join(OUT, `frame_${String(i).padStart(3, "0")}_${label}.png`);
  try {
    const canvas = page.locator("#canvas, canvas").first();
    if (await canvas.count())
      await canvas.screenshot({ path: file });
    else
      await page.screenshot({ path: file });
    console.log("shot", file);
  } catch (err) {
    console.warn("shot fail", label, err.message);
  }
}

async function main() {
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });

  const browser = await chromium.launch({
    headless: false,
    args: ["--autoplay-policy=no-user-gesture-required"],
  });
  const context = await browser.newContext({
    viewport: { width: 1280, height: 800 },
    recordVideo: { dir: OUT, size: { width: 1280, height: 800 } },
  });
  await routeLocal(context);

  const host = await context.newPage();
  host.on("console", (msg) => {
    const t = msg.text();
    if (/SMK script|INPUT_DUMP cpp (send|chronomap)|leave attract|2P|Match Race|wait /i.test(t))
      console.log("[host]", t.slice(0, 220));
  });

  await host.goto(BASE + "?" + qs({ fakelag: "1" }), {
    waitUntil: "domcontentloaded",
    timeout: 60000,
  });
  await host.fill("#userId", "host");
  await host.click("#hostBtn");
  await host.waitForSelector("#softwarePlayBtn", { state: "visible", timeout: 30000 });
  await host.evaluate(() => {
    const input = document.getElementById("softwareAcInput");
    if (input) {
      input.value = "smkart";
      input.dispatchEvent(new Event("input", { bubbles: true }));
    }
  });
  await sleep(400);
  await host.click("#softwarePlayBtn");

  let room = "";
  for (let i = 0; i < 60; i++) {
    room = await host.evaluate(() => window.__mamehubActiveLobby?.roomId || "");
    if (room) break;
    await sleep(500);
  }
  if (!room) throw new Error("no room");
  console.log("room", room);

  const join = await context.newPage();
  join.on("console", (msg) => {
    const t = msg.text();
    if (/SMK join|lockstep/i.test(t))
      console.log("[join]", t.slice(0, 180));
  });
  await join.goto(BASE + "?" + qs({ join: "1", room }), {
    waitUntil: "domcontentloaded",
    timeout: 60000,
  });
  await join.fill("#userId", "join1").catch(() => {});

  for (let i = 0; i < 90; i++) {
    const ready = await host.evaluate(() => {
      const btn = document.getElementById("hostStartGameBtn");
      return !!(window.__mamehubNet?.ready && btn && !btn.disabled);
    });
    if (ready) break;
    await sleep(500);
  }
  await host.click("#hostStartGameBtn");
  console.log("started game");

  // Wait for lockstep, then sample canvas every 2s through menu phase.
  for (let i = 0; i < 45; i++) {
    const lock = await host.evaluate(() =>
      /lockstep clock ok/i.test(document.getElementById("log")?.textContent || "")
    );
    if (lock) {
      console.log("lockstep at", i);
      break;
    }
    await sleep(1000);
  }

  const t0 = Date.now();
  let n = 0;
  while (Date.now() - t0 < RECORD_MS) {
    const scriptTail = await host.evaluate(() => {
      const log = document.getElementById("log")?.textContent || "";
      return log.split("\n").filter((l) => /SMK script/i.test(l)).slice(-3);
    });
    const label = (scriptTail[scriptTail.length - 1] || "tick")
      .replace(/[^a-zA-Z0-9]+/g, "_")
      .slice(0, 60);
    await shot(host, label || "tick", n++);
    if (n % 5 === 0)
      console.log("t+", ((Date.now() - t0) / 1000) | 0, "s", scriptTail);
    await sleep(2000);
  }

  await context.close();
  await browser.close();

  const videos = fs.readdirSync(OUT).filter((f) => f.endsWith(".webm"));
  console.log("done. out=", OUT, "videos=", videos, "frames=", n);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
