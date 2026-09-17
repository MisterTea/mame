#!/usr/bin/env python3
"""Generate MAMEHub browser sites from web/mamehub/profiles.json.

Writes per-profile config JS, driver filters, hash symlinks, landing page,
and points /<id>/index.html at the shared web/mamehub/play.html shell.
Arcade .flt is owned by build_arcade_top_mp.py.
"""

from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path

from kle_layouts import write_layout_assets

ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "web" / "mamehub"
PROFILES_PATH = WEB / "profiles.json"
PLAY_SHELL = WEB / "play.html"
HASH_SRC = ROOT / "hash"
HASH_DST = WEB / "hash"
FLT_DIR = ROOT / "src" / "mame"


def load_catalog() -> dict:
    return json.loads(PROFILES_PATH.read_text(encoding="utf-8"))


def profiles(catalog: dict | None = None) -> list[dict]:
    cat = catalog or load_catalog()
    return list(cat["profiles"])


def profile_by_id(pid: str, catalog: dict | None = None) -> dict:
    for p in profiles(catalog):
        if p["id"] == pid:
            return p
    raise SystemExit(f"unknown profile {pid!r}")


def wasm_basename(subtarget: str) -> str:
    return f"mame{subtarget}hub"


def wasm_js_url(p: dict) -> str:
    return f"/dist/{wasm_basename(p['subtarget'])}.js"


def hash_files(p: dict) -> list[dict]:
    return list(p.get("hashFiles") or [])


def selected_profiles(catalog: dict, name: str) -> list[dict]:
    if name in ("all", "", None):
        return profiles(catalog)
    return [profile_by_id(name, catalog)]


def unique_subtargets(plist: list[dict]) -> list[str]:
    seen: list[str] = []
    for p in plist:
        st = p["subtarget"]
        if st not in seen:
            seen.append(st)
    return seen


def _js_string_list(values: list[str], indent: str) -> str:
    inner = ",\n".join(f'{indent}  "{v}"' for v in values)
    return "[\n" + inner + "\n" + indent + "]"


def _js_ice(servers: list[dict], indent: str) -> str:
    parts = []
    for s in servers:
        urls = s.get("urls", "")
        parts.append(f'{indent}  {{ urls: "{urls}" }}')
    return "[\n" + ",\n".join(parts) + "\n" + indent + "]"


def render_config_js(p: dict, catalog: dict) -> str:
    relays = catalog.get("relays") or []
    ice = catalog.get("iceServers") or []
    mode = p.get("mode") or "console"
    lines = [
        f"// {p['title']} profile — used by /{p['id']}/.",
        "window.MAMEHUB_BROWSER = Object.assign({",
        f'  id: "{p["id"]}",',
        f'  mode: "{mode}",',
        f'  title: {json.dumps(p["title"])},',
        f'  noun: {json.dumps(p.get("noun") or (p["title"] + " software"))},',
        "  relays: " + _js_string_list(relays, "  ") + ",",
        "  iceServers: " + _js_ice(ice, "  ") + ",",
        "  // Root-absolute so profile shells share one dist/ tree.",
        f'  wasmJs: "{wasm_js_url(p)}",',
        '  candyProxyBase: "/candy-proxy",',
        '  romPath: "/roms",',
        '  hashPath: "/hash",',
    ]
    hashes = hash_files(p)
    if hashes:
        first = hashes[0]
        lines.append(f'  hashUrl: "/hash/{first["file"]}",')
        hf_js = []
        for h in hashes:
            hf_js.append(
                "    { "
                f'file: "{h["file"]}", '
                f'url: "/hash/{h["file"]}", '
                f'machine: "{h.get("machine") or p.get("machine") or ""}"'
                " }"
            )
        lines.append("  hashFiles: [\n" + ",\n".join(hf_js) + "\n  ],")
    else:
        lines.append('  hashUrl: "",')
        lines.append("  hashFiles: [],")
    if p.get("machine"):
        lines.append(f'  machine: "{p["machine"]}",')
    if p.get("media"):
        lines.append(f'  media: "{p["media"]}",')
    if p.get("machinesUrl"):
        lines.append(f'  machinesUrl: "{p["machinesUrl"]}",')
    if p.get("defaultMachine"):
        lines.append(f'  defaultMachine: "{p["defaultMachine"]}",')
    if p.get("defaultSoftware"):
        lines.append(f'  defaultSoftware: "{p["defaultSoftware"]}",')
    if p.get("defaultHostSoftware"):
        lines.append(f'  defaultHostSoftware: "{p["defaultHostSoftware"]}",')
    lines.append(f'  canvasWidth: {int(p.get("canvasWidth") or 512)},')
    lines.append(f'  canvasHeight: {int(p.get("canvasHeight") or 448)},')
    kb = p.get("keyboard")
    if kb:
        lines.append(f'  keyboardLayout: "/layouts/{p["id"]}.png",')
        kb_js = json.dumps(kb, indent=2).replace("\n", "\n  ")
        lines.append(f"  keyboard: {kb_js},")
    tag = p.get("appTag") or f"mamehub-browser-{p['id']}-v1"
    lines.append(f'  appTag: "{tag}"')
    lines.append("}, window.MAMEHUB_BROWSER || {});")
    lines.append("")
    return "\n".join(lines)


def link_play_shell(profile_id: str) -> None:
    """Each /<id>/ serves the same play.html via a relative symlink."""
    if not PLAY_SHELL.is_file():
        raise SystemExit(f"missing shared shell {PLAY_SHELL}")
    shell_dir = WEB / profile_id
    shell_dir.mkdir(parents=True, exist_ok=True)
    dst = shell_dir / "index.html"
    if dst.is_symlink() or dst.exists():
        dst.unlink()
    dst.symlink_to("../play.html")


def render_landing_html(catalog: dict) -> str:
    rows = []
    for p in catalog["profiles"]:
        pid = html.escape(p["id"], quote=True)
        title = html.escape(p["title"])
        blurb = html.escape(p["blurb"])
        rows.append(
            "      <tr>\n"
            f'        <td class="thumb"><a href="/{pid}/">'
            f'<img src="/systems/{pid}.png" alt="{title}" width="320" height="240"></a></td>\n'
            f'        <th scope="row"><a href="/{pid}/">{title}</a></th>\n'
            f'        <td class="about"><a href="/{pid}/">{blurb}</a></td>\n'
            "      </tr>"
        )
    row_html = "\n".join(rows)
    ids = [p["id"] for p in catalog["profiles"]]
    ids_js = json.dumps(ids)
    default = catalog.get("defaultProfile") or "snes"
    return f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>MAMEHub Online</title>
  <style>
    :root {{
      --bg: #12141a;
      --fg: #e8e6e3;
      --muted: #9a9590;
      --accent: #3d9a6a;
      --panel: #151a22;
      --border: #2a303a;
    }}
    * {{ box-sizing: border-box; }}
    body {{
      margin: 0;
      min-height: 100vh;
      font-family: "IBM Plex Sans", "Segoe UI", sans-serif;
      background: radial-gradient(1200px 600px at 20% -10%, #1c2430, var(--bg));
      color: var(--fg);
      display: grid;
      place-items: start center;
      padding: 1.5rem;
    }}
    main {{
      width: min(56rem, 100%);
      border: 1px solid var(--border);
      background: var(--panel);
      border-radius: 10px;
      padding: 1.75rem 1.5rem 1.5rem;
    }}
    header {{
      margin-bottom: 1.4rem;
    }}
    h1 {{
      margin: 0;
      font-size: 1.85rem;
      letter-spacing: 0.04em;
    }}
    .byline {{
      color: var(--fg);
      margin: 0.4rem 0 0;
      font-size: 1rem;
    }}
    h2 {{
      margin: 1.35rem 0 0.45rem;
      font-size: 1.12rem;
      letter-spacing: 0.03em;
      color: var(--fg);
    }}
    p {{
      color: var(--muted);
      margin: 0.4rem 0 0;
      line-height: 1.55;
    }}
    section + section {{
      margin-top: 0.35rem;
    }}
    table.systems {{
      width: 100%;
      border-collapse: collapse;
      margin-top: 0.55rem;
    }}
    table.systems thead th {{
      text-align: left;
      font-size: 0.78rem;
      font-weight: 650;
      letter-spacing: 0.06em;
      text-transform: uppercase;
      color: var(--muted);
      border-bottom: 1px solid var(--border);
      padding: 0.45rem 0.7rem;
    }}
    table.systems tbody tr {{
      position: relative;
      border-bottom: 1px solid var(--border);
    }}
    table.systems tbody tr:last-child {{
      border-bottom: none;
    }}
    table.systems tbody tr:hover {{
      background: #1e2833;
    }}
    table.systems td,
    table.systems th {{
      vertical-align: middle;
      padding: 0.7rem;
      font-weight: 500;
    }}
    table.systems td.thumb {{
      width: 9.5rem;
      padding-left: 0.55rem;
    }}
    table.systems img {{
      display: block;
      width: 8.5rem;
      height: auto;
      aspect-ratio: 4 / 3;
      object-fit: cover;
      border-radius: 6px;
      border: 1px solid var(--border);
      background: #0e1218;
    }}
    table.systems th[scope="row"] {{
      white-space: nowrap;
      font-size: 1.05rem;
    }}
    table.systems a {{
      color: inherit;
      text-decoration: none;
    }}
    table.systems th[scope="row"] a {{
      color: var(--accent);
    }}
    table.systems th[scope="row"] a::after {{
      content: "";
      position: absolute;
      inset: 0;
    }}
    table.systems td.about a {{
      color: var(--muted);
      font-size: 0.94rem;
      line-height: 1.45;
      display: block;
    }}
    table.systems tbody tr:hover th[scope="row"] a,
    table.systems a:focus-visible {{
      text-decoration: underline;
      outline: none;
    }}
    footer {{
      margin-top: 1.2rem;
      color: var(--muted);
      font-size: 0.8rem;
      line-height: 1.45;
    }}
    @media (max-width: 640px) {{
      table.systems thead {{
        display: none;
      }}
      table.systems,
      table.systems tbody,
      table.systems tr,
      table.systems th,
      table.systems td {{
        display: block;
        width: 100%;
      }}
      table.systems td.thumb {{
        width: 100%;
        padding-bottom: 0.2rem;
      }}
      table.systems img {{
        width: 100%;
        max-width: 20rem;
      }}
      table.systems th[scope="row"] {{
        white-space: normal;
        padding-bottom: 0.15rem;
      }}
      table.systems td.about {{
        padding-top: 0;
      }}
    }}
  </style>
  <script>
    (function () {{
      var qs = window.location.search || "";
      if (!qs || qs === "?")
        return;
      var params = new URLSearchParams(qs);
      var room = (params.get("room") || "").trim();
      var soft = (params.get("soft") || params.get("software") || "").trim();
      var join = (params.get("join") || "").toLowerCase();
      if (!room || !soft)
        return;
      if (join === "0" || join === "false" || join === "no")
        return;
      var ids = {ids_js};
      var sys = (params.get("sys") || params.get("profile") || "").trim().toLowerCase();
      if (ids.indexOf(sys) < 0)
        sys = {json.dumps(default)};
      window.location.replace("/" + sys + "/" + qs);
    }})();
  </script>
</head>
<body>
  <main>
    <header>
      <h1>MAMEHub Online</h1>
      <p class="byline">Created by Jason Gauci</p>
    </header>
    <section>
      <h2>What is MAME?</h2>
      <p>MAME (Multiple Arcade Machine Emulator) is preservation software that recreates vintage hardware so original games can run on modern computers. It started with arcade cabinets, and the same project now also emulates home consoles and handhelds from that era. MAMEHub adds input synchronization and a latency predictor on top of MAME: your inputs are delayed just long enough that every peer is guaranteed to receive the same inputs, so all machines stay in lockstep.</p>
    </section>
    <section>
      <h2>How the browser version works</h2>
      <p>This build compiles MAME to WebAssembly and runs it in your browser. Lobbies are announced on public Nostr relays; gameplay inputs travel peer-to-peer over WebRTC. There is no central MAMEHub server. The site is static HTML, JavaScript, and WASM — anyone can copy those files and host them on their own machine or any static web host.</p>
    </section>
    <section>
      <h2>Choose a system</h2>
      <table class="systems">
        <thead>
          <tr>
            <th scope="col">Hardware</th>
            <th scope="col">System</th>
            <th scope="col">About</th>
          </tr>
        </thead>
        <tbody>
{row_html}
        </tbody>
      </table>
    </section>
    <footer>One local server hosts every profile. Leave the launcher window open while playing.</footer>
  </main>
</body>
</html>
"""


def write_flt(p: dict) -> None:
    if p.get("generateFlt") is False:
        return
    sources = p.get("flt") or []
    if not sources:
        return
    path = FLT_DIR / f"{p['subtarget']}.flt"
    path.write_text("\n".join(sources) + "\n", encoding="utf-8")


def link_hash(name: str) -> None:
    HASH_DST.mkdir(parents=True, exist_ok=True)
    src = HASH_SRC / name
    dst = HASH_DST / name
    if not src.is_file():
        raise SystemExit(f"missing hash list {src}")
    rel = os.path.relpath(src, HASH_DST)
    if dst.is_symlink() or dst.exists():
        dst.unlink()
    dst.symlink_to(rel)


def generate() -> None:
    catalog = load_catalog()
    play_ids = []
    for p in catalog["profiles"]:
        write_flt(p)
        cfg_path = WEB / f"config.{p['id']}.js"
        cfg_path.write_text(render_config_js(p, catalog), encoding="utf-8")
        write_layout_assets(p, WEB / "layouts")
        link_play_shell(p["id"])
        for h in hash_files(p):
            link_hash(h["file"])
        play_ids.append(p["id"])
        print(f"  {p['id']}: /{p['id']}/  wasm={wasm_basename(p['subtarget'])}.*")

    default_id = catalog.get("defaultProfile") or "snes"
    default = profile_by_id(default_id, catalog)
    (WEB / "config.js").write_text(render_config_js(default, catalog), encoding="utf-8")
    missing_art = [
        p["id"]
        for p in catalog["profiles"]
        if not (WEB / "systems" / f"{p['id']}.png").is_file()
    ]
    if missing_art:
        raise SystemExit("missing landing images: " + ", ".join(missing_art))
    (WEB / "index.html").write_text(render_landing_html(catalog), encoding="utf-8")
    print(f"landing + config.js default={default_id}")
    print(f"profiles: {', '.join(play_ids)}")


def cmd_package_files(profile: str) -> None:
    catalog = load_catalog()
    plist = selected_profiles(catalog, profile)
    files: list[str] = []
    for p in plist:
        files.append(f"config.{p['id']}.js")
        files.append(f"{p['id']}/index.html")
        if p.get("keyboard"):
            files.append(f"layouts/{p['id']}.png")
        base = wasm_basename(p["subtarget"])
        files.append(f"dist/{base}.js")
        files.append(f"dist/{base}.wasm")
        for h in hash_files(p):
            files.append(f"hash/{h['file']}")
        if p.get("machinesUrl"):
            files.append(p["machinesUrl"].lstrip("/"))
    # Landing page always lists every profile, so ship every system thumbnail.
    for p in profiles(catalog):
        files.append(f"systems/{p['id']}.png")
    # unique preserve order
    seen = set()
    out = []
    for f in files:
        if f not in seen:
            seen.add(f)
            out.append(f)
    print("\n".join(out))


def cmd_subtargets(profile: str) -> None:
    catalog = load_catalog()
    print("\n".join(unique_subtargets(selected_profiles(catalog, profile))))


def cmd_ids() -> None:
    print("\n".join(p["id"] for p in profiles()))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "command",
        nargs="?",
        default="gen",
        choices=["gen", "ids", "subtargets", "package-files"],
    )
    parser.add_argument("profile", nargs="?", default="all")
    args = parser.parse_args()
    if args.command == "gen":
        generate()
        return 0
    if args.command == "ids":
        cmd_ids()
        return 0
    if args.command == "subtargets":
        cmd_subtargets(args.profile)
        return 0
    if args.command == "package-files":
        cmd_package_files(args.profile)
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
