/**
 * Boot SNES offline, enter fullscreen, and assert the emulator keeps
 * producing new canvas frames without resizing the drawing buffer.
 */
import { chromium } from "playwright";
import crypto from "crypto";
import fs from "fs";
import path from "path";
import { fileURLToPath } from "url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const BASE = process.env.MAMEHUB_BASE || "http://127.0.0.1:8765/snes/";
const SOFT = process.env.MAMEHUB_SOFT || "smkart";
const OUT = "/tmp/mamehub-fullscreen-frames";

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function digest(buf) {
  return crypto.createHash("sha1").update(buf).digest("hex");
}

async function emuState(page) {
  return page.evaluate(() => {
    const log = document.getElementById("log")?.textContent || "";
    const canvas = document.getElementById("canvas");
    const wrap = document.getElementById("canvas-wrap");
    const candy = document.getElementById("candy-progress");
    const fsEl = document.fullscreenElement || document.webkitFullscreenElement;
    return {
      wasm: /WASM runtime initialized/i.test(log),
      abort: /WASM abort|FATAL/i.test(log),
      candyHidden: !candy || candy.hidden,
      candyLabel: document.getElementById("candy-progress-label")?.textContent || "",
      canvasW: canvas ? canvas.width : 0,
      canvasH: canvas ? canvas.height : 0,
      canvasDisplay: canvas ? getComputedStyle(canvas).display : "",
      fullscreen: !!(fsEl || wrap?.classList.contains("is-fallback-fullscreen")),
      fsId: fsEl ? (fsEl.id || fsEl.tagName) : "",
      logTail: log.split("\n").filter(Boolean).slice(-10),
    };
  });
}

async function canvasShot(page, label) {
  const loc = page.locator("#canvas");
  const buf = await loc.screenshot({ type: "png" });
  if (label)
    fs.writeFileSync(path.join(OUT, label + ".png"), buf);
  return { buf, hash: digest(buf), bytes: buf.length };
}

async function waitFor(page, label, ms, fn) {
  const t0 = Date.now();
  while (Date.now() - t0 < ms) {
    if (await fn())
      return;
    await sleep(400);
  }
  const st = await emuState(page).catch(() => ({}));
  throw new Error(`TIMEOUT ${label} after ${ms}ms state=${JSON.stringify(st)}`);
}

async function sampleHashes(page, prefix, n, gapMs) {
  const hashes = [];
  for (let i = 0; i < n; i++) {
    const shot = await canvasShot(page, `${prefix}-${i}`);
    hashes.push(shot.hash);
    if (i + 1 < n)
      await sleep(gapMs);
  }
  return hashes;
}

async function main() {
  fs.rmSync(OUT, { recursive: true, force: true });
  fs.mkdirSync(OUT, { recursive: true });

  const browser = await chromium.launch({
    headless: true,
    args: ["--autoplay-policy=no-user-gesture-required"],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 800 } });
  page.on("pageerror", (err) => console.warn("[pageerror]", err.message));
  page.on("console", (msg) => {
    const t = msg.text();
    if (/WASM|abort|FATAL|Fullscreen|Candy|error/i.test(t))
      console.log("[console]", t.slice(0, 240));
  });

  const url = BASE + "?" + new URLSearchParams({ mute: "1", soft: SOFT }).toString();
  console.log("goto", url);
  await page.goto(url, { waitUntil: "domcontentloaded", timeout: 30000 });

  await page.click("#startOfflineBtn");
  await page.waitForSelector("#softwareAcInput", { state: "visible", timeout: 30000 });
  await page.fill("#softwareAcInput", SOFT);
  await page.click("#softwarePlayBtn");

  await waitFor(page, "wasm-init", 120000, async () => (await emuState(page)).wasm);
  console.log("wasm up");

  await waitFor(page, "candy-done", 120000, async () => {
    const st = await emuState(page);
    if (st.abort)
      return true;
    const label = st.candyLabel || "";
    const candyFinished = /100%|MiB \/ /.test(label) && st.candyHidden;
    return st.wasm && st.canvasDisplay === "block" && (candyFinished || st.candyHidden && /Candy:/i.test(label));
  });
  const afterCandy = await emuState(page);
  console.log("candy done", afterCandy.candyLabel, afterCandy.logTail.slice(-4));
  if (afterCandy.abort)
    throw new Error("emulator aborted during candy");

  await waitFor(page, "visible-pixels", 120000, async () => {
    const shot = await canvasShot(page, null);
    let bright = 0;
    for (let i = 24; i < shot.buf.length; i += 31) {
      if (shot.buf[i] > 16)
        bright++;
    }
    return bright > 40;
  });
  console.log("canvas has pixels");

  const native = await emuState(page);
  console.log("pre-fs", { canvasW: native.canvasW, canvasH: native.canvasH });
  if (native.canvasW !== 512 || native.canvasH !== 448)
    throw new Error(`unexpected canvas buffer ${native.canvasW}x${native.canvasH}`);

  const beforeHashes = await sampleHashes(page, "before", 5, 350);
  const beforeUnique = new Set(beforeHashes).size;
  console.log("before hashes", beforeUnique, beforeHashes);
  if (beforeUnique < 2)
    throw new Error("emulator not producing new frames before fullscreen: " + beforeHashes.join(","));

  await page.screenshot({ path: path.join(OUT, "page-before-fs.png") });
  await page.click("#fullscreenBtn");
  await sleep(500);

  const mid = await emuState(page);
  console.log("in-fs", mid);
  if (!mid.fullscreen)
    throw new Error("fullscreen did not activate");
  if (mid.abort)
    throw new Error("emulator aborted on fullscreen: " + mid.logTail.join(" | "));
  if (mid.canvasW !== native.canvasW || mid.canvasH !== native.canvasH)
    throw new Error(`canvas buffer resized in fullscreen: ${mid.canvasW}x${mid.canvasH}`);

  await page.screenshot({ path: path.join(OUT, "page-in-fs.png") });
  const afterHashes = await sampleHashes(page, "fullscreen", 6, 350);
  const afterUnique = new Set(afterHashes).size;
  console.log("fullscreen hashes", afterUnique, afterHashes);
  const afterState = await emuState(page);
  if (afterState.abort)
    throw new Error("emulator aborted while fullscreen: " + afterState.logTail.join(" | "));
  if (afterUnique < 2)
    throw new Error("emulator froze in fullscreen; hashes=" + afterHashes.join(","));
  if (afterState.canvasW !== native.canvasW)
    throw new Error(`canvas width changed after fullscreen: ${afterState.canvasW}`);

  await page.locator("#fullscreenExitBtn").click({ timeout: 3000 }).catch(async () => {
    await page.click("#fullscreenBtn");
  });
  await sleep(400);
  const done = await emuState(page);
  console.log("exited", { abort: done.abort, fullscreen: done.fullscreen, canvasW: done.canvasW });
  if (done.abort)
    throw new Error("emulator aborted after leaving fullscreen");

  await browser.close();
  console.log("PASS fullscreen keeps producing frames uniqueBefore=" + beforeUnique +
    " uniqueAfter=" + afterUnique);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
