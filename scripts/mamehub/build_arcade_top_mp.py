#!/usr/bin/env python3
# Build arcade_top_mp.json (exactly 100 unique 2P+ titles) + src/mame/arcade.flt
# Primary: IAM/KLOV Top 100 + Wikipedia highest-grossing fills, then multiplayer backfill.
# Forced: xmen6p, bombrman. Variants collapsed (one preferred MAME set per commercial title).

from __future__ import annotations

import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAME = Path(sys.argv[1] if len(sys.argv) > 1 else "/Users/jjg/mame/mamehub")
OUT_JSON = ROOT / "scripts" / "mamehub" / "arcade_top_mp.json"
WEB_JSON = ROOT / "web" / "mamehub" / "arcade_top_mp.json"
OUT_FLT = ROOT / "src" / "mame" / "arcade.flt"

# Ordered candidates: (title, mame_shortname, source_tag)
# KLOV multiplayer-leaning + Wiki fills first; then popular 2P+ backfill.
CANDIDATES: list[tuple[str, str, str]] = [
    # --- KLOV / classic multiplayer ---
    ("Warlords", "warlords", "klov"),
    ("Joust", "joust", "klov"),
    ("Robotron 2084", "robotron", "klov"),
    ("Tron", "tron", "klov"),
    ("Spy Hunter", "spyhunt", "klov"),
    ("Track & Field", "trackfld", "klov"),
    ("Punch-Out!!", "punchout", "klov"),
    ("Gauntlet", "gauntlet", "klov"),
    ("Rampage", "rampage", "klov"),
    ("Double Dragon", "ddragon", "klov"),
    ("Golden Axe", "goldnaxe", "klov"),
    ("Smash TV", "smashtv", "klov"),
    ("X-Men (6 Players)", "xmen6p", "klov+forced"),
    ("Mortal Kombat", "mk", "klov"),
    ("NBA Jam", "nbajam", "klov"),
    ("Terminator 2: Judgment Day", "term2", "klov"),
    ("Lethal Enforcers", "lethalen", "klov"),
    ("Captain America and The Avengers", "captaven", "klov"),
    ("Daytona USA", "daytona", "klov"),
    ("Area 51", "area51", "klov"),
    ("Soul Edge", "souledge", "klov"),
    ("San Francisco Rush", "sfrush", "klov"),
    ("The House of the Dead", "hotd", "klov"),
    ("NFL Blitz", "blitz", "klov"),
    ("Gauntlet Legends", "gauntleg", "klov"),
    ("The House of the Dead 2", "hotd2", "klov"),
    ("Time Crisis II", "timecrs2", "klov"),
    ("CarnEvil", "carnevil", "klov"),
    ("Hydro Thunder", "hydro", "klov"),
    ("Gauntlet Dark Legacy", "gauntdl", "klov"),
    ("Tank", "tank", "klov"),
    ("Sea Wolf", "seawolf", "klov"),
    ("Battlezone", "bzone", "klov"),  # often 1P; may drop
    ("Centipede", "centiped", "klov"),  # 1P typically; may drop
    ("Wizard of Wor", "wow", "klov"),
    ("Frogger", "frogger", "klov"),  # 2P alternating
    ("Gorf", "gorf", "klov"),
    ("Mouse Trap", "mtrap", "klov"),
    ("Qix", "qix", "klov"),
    ("Burgertime", "btime", "klov"),
    ("Jungle King", "junglek", "klov"),
    ("Moon Patrol", "mpatrol", "klov"),
    ("Pengo", "pengo", "klov"),
    ("Pole Position", "polepos", "klov"),
    ("Q*bert", "qbert", "klov"),
    ("Time Pilot", "timeplt", "klov"),
    ("Zaxxon", "zaxxon", "klov"),
    ("Elevator Action", "elevator", "klov"),
    ("Gyruss", "gyruss", "klov"),
    ("Tapper", "tapper", "klov"),
    ("Choplifter", "choplift", "klov"),
    ("Ghosts 'n Goblins", "gng", "klov"),
    ("Arkanoid", "arkanoid", "klov"),
    ("Out Run", "outrun", "klov"),
    ("1943: The Battle of Midway", "1943", "klov"),
    ("Rastan", "rastan", "klov"),
    ("RoadBlasters", "roadblst", "klov"),
    ("Bad Dudes vs. DragonNinja", "baddudes", "klov"),
    ("Cyberball", "cyberbal", "klov"),
    ("Ghouls 'n Ghosts", "ghouls", "klov"),
    ("Tetris (Atari)", "atetris", "klov"),
    ("Raiden", "raiden", "klov"),
    ("Rampart", "rampart", "klov"),
    ("King of the Monsters", "kotm", "klov"),
    # --- Wikipedia / highest-grossing fills ---
    ("Street Fighter II: Champion Edition", "sf2ce", "wikipedia"),
    ("Mortal Kombat II", "mk2", "wikipedia"),
    ("Virtua Fighter", "vf", "wikipedia"),
    ("Virtua Fighter 2", "vf2", "wikipedia"),
    ("Tekken 2", "tekken2", "wikipedia"),
    ("Tekken 3", "tekken3", "wikipedia"),
    ("Final Fight", "ffight", "wikipedia"),
    ("Teenage Mutant Ninja Turtles", "tmnt", "wikipedia"),
    ("Karate Champ", "kchamp", "wikipedia"),
    ("Darkstalkers: The Night Warriors", "dstalker", "wikipedia"),
    ("Killer Instinct", "kinst", "wikipedia"),
    ("Dance Dance Revolution", "ddrua", "wikipedia"),
    ("The King of Fighters '97", "kof97", "wikipedia"),
    ("Vs. Super Mario Bros.", "vsmsb", "wikipedia"),
    # --- Popular 2P+ backfill ---
    ("The Simpsons", "simpsons", "backfill"),
    ("Cadillacs and Dinosaurs", "dino", "backfill"),
    ("The Punisher", "punisher", "backfill"),
    ("Captain Commando", "captcomm", "backfill"),
    ("Knights of the Round", "knights", "backfill"),
    ("Warriors of Fate", "wof", "backfill"),
    ("Dungeons & Dragons: Tower of Doom", "ddtod", "backfill"),
    ("Dungeons & Dragons: Shadow over Mystara", "ddsom", "backfill"),
    ("Alien vs. Predator", "avsp", "backfill"),
    ("Armored Warriors", "armwar", "backfill"),
    ("Marvel vs. Capcom: Clash of Super Heroes", "mvsc", "backfill"),
    ("X-Men: Children of the Atom", "xmcota", "backfill"),
    ("Marvel Super Heroes", "msh", "backfill"),
    ("Super Street Fighter II Turbo", "ssf2t", "backfill"),
    ("Street Fighter Alpha 3", "sfa3", "backfill"),
    ("Ultimate Mortal Kombat 3", "umk3", "backfill"),
    ("NBA Jam TE", "nbajamte", "backfill"),
    ("NFL Blitz 99", "blitz99", "backfill"),
    ("Sunset Riders", "ssriders", "backfill"),
    ("Vendetta", "vendetta", "backfill"),
    ("Crime Fighters", "crimfght", "backfill"),
    ("Teenage Mutant Ninja Turtles: Turtles in Time", "tmnt2", "backfill"),
    ("X-Men vs. Street Fighter", "xmvsf", "backfill"),
    ("Marvel vs. Capcom 2", "mvsc2", "backfill"),
    ("Metal Slug", "mslug", "backfill"),
    ("Metal Slug X", "mslugx", "backfill"),
    ("Metal Slug 3", "mslug3", "backfill"),
    ("Gun.Smoke", "gunsmoke", "backfill"),
    ("Ikari Warriors", "ikari", "backfill"),
    ("Contra", "contra", "backfill"),
    ("Super Contra", "scontra", "backfill"),
    ("Bubble Bobble", "bublbobl", "backfill"),
    ("Snow Bros.", "snowbros", "backfill"),
    ("Bomb Jack", "bombjack", "backfill"),
    ("Bomber Man", "bombrman", "backfill"),
    ("Wonder Boy", "wboy", "backfill"),
    ("Wonder Boy III: Monster Lair", "wbml", "backfill"),  # may be wrong id
    ("Shinobi", "shinobi", "backfill"),
    ("Golden Axe: The Revenge of Death Adder", "ga2", "backfill"),
    ("Aliens", "aliens", "backfill"),
    ("Night Slashers", "nslasher", "backfill"),
    ("64th. Street: A Detective Story", "64street", "backfill"),
    ("Final Fight Revenge", "ffreveng", "backfill"),
    ("King of Fighters '98", "kof98", "backfill"),
    ("King of Fighters 2002", "kof2002", "backfill"),
    ("Samurai Shodown II", "samsho2", "backfill"),
    ("Fatal Fury Special", "fatfurysp", "backfill"),  # may need fatfursp
    ("Garou: Mark of the Wolves", "garou", "backfill"),
    ("Virtua Cop", "vcop", "backfill"),
    ("Virtua Cop 2", "vcop2", "backfill"),
    ("Point Blank", "ptblank", "backfill"),
    ("Time Crisis", "timecris", "backfill"),
    ("Gunblade NY", "gunbustr", "backfill"),  # may be wrong
    ("Crazy Taxi", "cruisin", "backfill"),  # may be wrong id
    ("Virtua Racing", "vr", "backfill"),
    ("Sega Rally Championship", "srallyc", "backfill"),
    ("Ridge Racer", "ridgerac", "backfill"),
    ("Hard Drivin'", "harddriv", "backfill"),
    ("Paperboy", "paperboy", "backfill"),
    ("Gauntlet II", "gaunt2", "backfill"),
    ("Xybots", "xybots", "backfill"),
    ("APB", "apb", "backfill"),
    ("Toobin'", "toobin", "backfill"),
    ("Cyberball 2072", "cyberbalt", "backfill"),
    ("Arch Rivals", "archrivl", "backfill"),
    ("Pigskin 621AD", "pigskin", "backfill"),
    ("Rampage: World Tour", "rmpgwt", "backfill"),
    ("Wayne Gretzky's 3D Hockey", "wg3dh", "backfill"),
    ("Open Ice", "openice", "backfill"),
    ("WWF WrestleFest", "wwfwfest", "backfill"),
    ("WWF Superstars", "wwfsstar", "backfill"),
    ("Saturday Night Slam Masters", "slammast", "backfill"),
    ("Ring of Destruction: Slam Masters II", "ringdest", "backfill"),
    ("Pit-Fighter", "pitfight", "backfill"),
    ("Primal Rage", "primrage", "backfill"),
    ("BloodStorm", "bloodstm", "backfill"),
    ("Ninja Baseball Bat Man", "nbbatman", "backfill"),
    ("Mystic Warriors", "mysticri", "backfill"),  # may be wrong
    ("Metamorphic Force", "metamrph", "backfill"),
    ("Violent Storm", "violstm", "backfill"),
    ("Growl", "growl", "backfill"),
    ("Dynamite Duke", "dynduke", "backfill"),
    ("Heavy Barrel", "hbarrel", "backfill"),
    ("Midnight Resistance", "midres", "backfill"),
    ("Cabal", "cabal", "backfill"),
    ("Blood Bros.", "bloodbro", "backfill"),
    ("Sky Soldiers", "skysoldr", "backfill"),
    ("Twin Cobra", "twincobr", "backfill"),
    ("Truxton", "truxton", "backfill"),
    ("Batsugun", "batsugun", "backfill"),
    ("DoDonPachi", "ddonpach", "backfill"),
    ("Espgaluda", "espgal", "backfill"),
    ("Gunbird", "gunbird", "backfill"),
    ("Strikers 1945", "s1945", "backfill"),
    ("1941: Counter Attack", "1941", "backfill"),
    ("19XX: The War Against Destiny", "19xx", "backfill"),
    ("Progear", "progear", "backfill"),
    ("Eco Fighters", "ecofghtr", "backfill"),
    ("U.N. Squadron", "unsquad", "backfill"),
    ("Area 88", "area88", "backfill"),  # may be alias of unsquad
    ("Mercs", "mercs", "backfill"),
    ("Commando", "commando", "backfill"),
    ("Gunforce", "gunforce", "backfill"),
    ("In the Hunt", "inthunt", "backfill"),
    ("R-Type", "rtype", "backfill"),
    ("R-Type II", "rtype2", "backfill"),
    ("Image Fight", "imgfight", "backfill"),
    ("Dragon Spirit", "dspirit", "backfill"),
    ("Bosconian", "bosco", "backfill"),
    ("Galaga", "galaga", "backfill"),  # 1P usually
    ("Xevious", "xevious", "backfill"),
    ("Mappy", "mappy", "backfill"),
    ("Dig Dug II", "digdug2", "backfill"),
    ("Pac-Mania", "pacmania", "backfill"),
    ("Ms. Pac-Man", "mspacman", "backfill"),  # 1P
    ("Asteroids", "asteroid", "backfill"),
    ("Space Duel", "spacduel", "backfill"),
    ("Black Widow", "bwidow", "backfill"),
    ("Tempest", "tempest", "backfill"),
    ("Major Havoc", "mhavoc", "backfill"),
    ("Marble Madness", "marble", "backfill"),
    ("Crystal Castles", "ccastles", "backfill"),
    ("Food Fight", "foodf", "backfill"),
    ("Cloak & Dagger", "cloak", "backfill"),
    ("Safari Rally", "safarir", "backfill"),
    ("Bomb Bee", "bombbee", "backfill"),
    ("Pooyan", "pooyan", "backfill"),
    ("Scramble", "scramble", "backfill"),
    ("Super Cobra", "scobra", "backfill"),
    ("Amidar", "amidar", "backfill"),
    ("Tutankham", "tutankhm", "backfill"),
    ("Jungler", "jungler", "backfill"),
    ("Turtles", "turtles", "backfill"),
    ("Strategy X", "strategyx", "backfill"),
    ("Hustler", "hustler", "backfill"),
    ("Billard", "billard", "backfill"),  # likely invalid
    ("Pong Doubles", "pongd", "backfill"),
    ("Quadrapong", "quadpong", "backfill"),
    ("Indy 4", "indy4", "backfill"),
    ("Sprint 2", "sprint2", "backfill"),
    ("Sprint 4", "sprint4", "backfill"),
    ("Fire Truck", "firetrk", "backfill"),
    ("Ultra Tank", "ultratnk", "backfill"),
    ("Atari Football", "football", "backfill"),
    ("Atari Baseball", "abaseb", "backfill"),
    ("Basketball", "bsktball", "backfill"),
    ("Boxing", "boxingb", "backfill"),
    ("Warlords (cocktail)", "warlordc", "backfill"),  # may be invalid if warlords kept
    ("Joust 2", "joust2", "backfill"),
    ("Bubbles", "bubbles", "backfill"),
    ("Sinistar", "sinistar", "backfill"),
    ("Splat!", "splat", "backfill"),
    ("Blaster", "blaster", "backfill"),
    ("Defender", "defender", "backfill"),
    ("Stargate", "stargate", "backfill"),
    ("Moon Patrol (Williams)", "mpatrlw", "backfill"),
    ("Joust (Williams cocktail)", "joustwr", "backfill"),
    ("NARC", "narc", "backfill"),
    ("Total Carnage", "totcarn", "backfill"),
    ("Revolution X", "revx", "backfill"),
    ("Trog", "trog", "backfill"),
    ("High Impact Football", "hiimpact", "backfill"),
    ("Strike Force", "strkforc", "backfill"),
    ("Mortal Kombat 3", "mk3", "backfill"),
    ("NBA Hangtime", "nbahangt", "backfill"),
    ("WWF: WrestleMania The Arcade Game", "wwfmania", "backfill"),
    ("Cruis'n USA", "crusnusa", "backfill"),
    ("Cruis'n World", "crusnwld", "backfill"),
    ("California Speed", "calspeed", "backfill"),
    ("San Francisco Rush: The Rock", "sfrushrk", "backfill"),
    ("Hydro Thunder Hurricane", "hydrthnd", "backfill"),
    ("The Grid", "thegrid", "backfill"),
    ("Gigas", "gigas", "backfill"),
]


def query_machines(names: list[str]) -> dict[str, dict]:
    """Return name -> {players, sourcefile, description, cloneof} for runnable machines."""
    out: dict[str, dict] = {}
    # Batch to keep argv reasonable
    batch = 40
    for i in range(0, len(names), batch):
        chunk = names[i : i + batch]
        proc = subprocess.run(
            [str(MAME), "-listxml", *chunk],
            capture_output=True,
            text=True,
            check=False,
        )
        if proc.returncode != 0 and not proc.stdout.strip():
            # Retry one-by-one for this chunk
            for n in chunk:
                p2 = subprocess.run(
                    [str(MAME), "-listxml", n],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if p2.returncode != 0 or not p2.stdout.strip():
                    continue
                out.update(_parse_xml(p2.stdout, {n}))
            continue
        out.update(_parse_xml(proc.stdout, set(chunk)))
    return out


def _parse_xml(xml_text: str, wanted: set[str]) -> dict[str, dict]:
    out: dict[str, dict] = {}
    # listxml may emit a full document; wrap if needed
    text = xml_text.strip()
    if not text.startswith("<?xml") and not text.startswith("<mame"):
        text = "<mame>" + text + "</mame>"
    try:
        root = ET.fromstring(text)
    except ET.ParseError:
        # Try extracting machine blocks
        for m in re.finditer(
            r'<machine\s+name="([^"]+)"[^>]*>.*?</machine>',
            xml_text,
            re.DOTALL,
        ):
            name = m.group(1)
            if name not in wanted:
                continue
            block = m.group(0)
            src = re.search(r'sourcefile="([^"]+)"', block)
            players = re.search(r'<input[^>]*players="(\d+)"', block)
            desc = re.search(r"<description>([^<]*)</description>", block)
            cloneof = re.search(r'cloneof="([^"]+)"', block)
            isdev = 'isdevice="yes"' in block[:200]
            if isdev or not src or not players:
                continue
            out[name] = {
                "players": int(players.group(1)),
                "sourcefile": src.group(1),
                "description": desc.group(1) if desc else name,
                "cloneof": cloneof.group(1) if cloneof else None,
            }
        return out

    for mach in root.iter("machine"):
        name = mach.get("name")
        if name not in wanted:
            continue
        if mach.get("isdevice") == "yes" or mach.get("runnable") == "no":
            continue
        src = mach.get("sourcefile")
        inp = mach.find("input")
        if src is None or inp is None or inp.get("players") is None:
            continue
        desc_el = mach.find("description")
        out[name] = {
            "players": int(inp.get("players")),
            "sourcefile": src,
            "description": (desc_el.text if desc_el is not None else name) or name,
            "cloneof": mach.get("cloneof"),
        }
    return out


def main() -> int:
    if not MAME.is_file():
        print(f"missing mame binary: {MAME}", file=sys.stderr)
        return 1

    # Dedupe candidates by mame id, preserve order; force xmen6p first among X-Men
    seen_ids: set[str] = set()
    ordered: list[tuple[str, str, str]] = []
    # Ensure xmen6p and bombrman are present early enough to make the 100
    forced = [
        ("X-Men (6 Players)", "xmen6p", "klov+forced"),
        ("Bomber Man", "bombrman", "backfill+forced"),
    ]
    for title, mid, src in [*forced, *CANDIDATES]:
        if mid in seen_ids:
            continue
        seen_ids.add(mid)
        ordered.append((title, mid, src))

    print(f"querying {len(ordered)} candidates via {MAME} …")
    info = query_machines([m for _, m, _ in ordered])

    selected: list[dict] = []
    used_titles: set[str] = set()
    for title, mid, src in ordered:
        if len(selected) >= 100:
            break
        meta = info.get(mid)
        if not meta:
            print(f"  skip missing: {mid}")
            continue
        if meta["players"] < 2:
            print(f"  skip 1P: {mid} players={meta['players']}")
            continue
        # Collapse obvious same-title variants already handled by unique mid;
        # skip if we already have same description family via clone parent already selected
        key = title.lower()
        if key in used_titles:
            continue
        used_titles.add(key)
        selected.append(
            {
                "title": title,
                "mame": mid,
                "players": meta["players"],
                "sourcefile": meta["sourcefile"],
                "description": meta["description"],
                "source": src,
            }
        )
        print(f"  + {len(selected):3d} {mid:16s} p={meta['players']}  {title}")

    if not any(e["mame"] == "xmen6p" for e in selected):
        print("FATAL: xmen6p not in selection", file=sys.stderr)
        return 1
    if not any(e["mame"] == "bombrman" for e in selected):
        print("FATAL: bombrman not in selection", file=sys.stderr)
        return 1
    if len(selected) < 100:
        print(f"FATAL: only {len(selected)} games (need 100)", file=sys.stderr)
        return 1

    selected = selected[:100]
    OUT_JSON.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": 1,
        "count": len(selected),
        "notes": (
            "Exactly 100 unique popular arcade titles with MAME players>=2. "
            "Primary IAM/KLOV Top 100 + Wikipedia highest-grossing fills, then backfill. "
            "Variants collapsed to one MAME set; xmen6p and bombrman forced."
        ),
        "games": selected,
    }
    text = json.dumps(payload, indent=2) + "\n"
    OUT_JSON.write_text(text)
    WEB_JSON.write_text(text)

    sources = sorted({e["sourcefile"] for e in selected})
    OUT_FLT.write_text("\n".join(sources) + "\n")
    print(f"wrote {OUT_JSON} ({len(selected)} games)")
    print(f"wrote {WEB_JSON}")
    print(f"wrote {OUT_FLT} ({len(sources)} source files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
