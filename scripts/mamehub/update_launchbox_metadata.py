#!/usr/bin/env python3
"""Build partitioned LaunchBox metadata databases used by MAMEHub."""

import argparse
import hashlib
import os
import sqlite3
import tempfile
import unicodedata
import urllib.request
import xml.etree.ElementTree as ET
import zipfile

DEFAULT_URL = "http://gamesdb.launchbox-app.com/Metadata.zip"
GAME_COLUMNS = """normalized_name TEXT NOT NULL, name TEXT NOT NULL, platform TEXT,
 overview TEXT, developer TEXT, publisher TEXT, release_date TEXT, genres TEXT,
 max_players TEXT, cooperative TEXT, rating TEXT, rating_count TEXT, esrb TEXT,
 release_type TEXT"""


def normalized(value):
    value = unicodedata.normalize("NFKD", value or "").encode("ascii", "ignore").decode().lower()
    return " ".join("".join(c if c.isalnum() else " " for c in value).split())


def text(node, name):
    child = node.find(name)
    return (child.text or "").strip() if child is not None else ""


def elements(stream, tag):
    for _, node in ET.iterparse(stream, events=("end",)):
        if node.tag == tag:
            yield node
            node.clear()


def configure(db):
    db.executescript("PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF;")


def platform_filename(platform):
    readable = normalized(platform).replace(" ", "-")[:48] or "unknown"
    return f"{readable}-{hashlib.sha1(platform.encode()).hexdigest()[:10]}.sqlite3"


def build(archive_name, output_dir):
    software_dir = os.path.join(output_dir, "software")
    os.makedirs(software_dir, exist_ok=True)

    arcade_name = os.path.join(output_dir, "arcade.sqlite3")
    if os.path.exists(arcade_name):
        os.unlink(arcade_name)
    arcade = sqlite3.connect(arcade_name)
    configure(arcade)
    arcade.execute("""CREATE TABLE arcade (
        file_name TEXT PRIMARY KEY, name TEXT, overview TEXT, developer TEXT,
        publisher TEXT, year TEXT, genre TEXT, play_mode TEXT, region TEXT,
        version TEXT, language TEXT, series TEXT, status TEXT)""")

    staging_name = os.path.join(tempfile.gettempdir(), "launchbox-staging.sqlite3")
    if os.path.exists(staging_name):
        os.unlink(staging_name)
    staging = sqlite3.connect(staging_name)
    configure(staging)
    staging.execute(f"CREATE TABLE game ({GAME_COLUMNS})")

    with zipfile.ZipFile(archive_name) as archive:
        with archive.open("Mame.xml") as source:
            rows = ((text(n, "FileName"), text(n, "Name"), text(n, "Overview"),
                     text(n, "Developer"), text(n, "Publisher"), text(n, "Year"),
                     text(n, "Genre"), text(n, "PlayMode"), text(n, "Region"),
                     text(n, "Version"), text(n, "Language"), text(n, "Series"),
                     text(n, "Status")) for n in elements(source, "MameFile"))
            arcade.executemany("INSERT OR REPLACE INTO arcade VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)", rows)
        with archive.open("Metadata.xml") as source:
            rows = ((normalized(text(n, "Name")), text(n, "Name"), text(n, "Platform"),
                     text(n, "Overview"), text(n, "Developer"), text(n, "Publisher"),
                     text(n, "ReleaseDate") or text(n, "ReleaseYear"), text(n, "Genres"),
                     text(n, "MaxPlayers"), text(n, "Cooperative"), text(n, "CommunityRating"),
                     text(n, "CommunityRatingCount"), text(n, "ESRB"), text(n, "ReleaseType"))
                    for n in elements(source, "Game"))
            staging.executemany("INSERT INTO game VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)", rows)
    arcade.commit()
    arcade.execute("VACUUM")
    arcade.close()
    staging.commit()
    staging.execute("CREATE INDEX game_platform ON game(platform)")

    manifest_name = os.path.join(output_dir, "platforms.sqlite3")
    if os.path.exists(manifest_name):
        os.unlink(manifest_name)
    manifest = sqlite3.connect(manifest_name)
    manifest.execute("CREATE TABLE platform (name TEXT PRIMARY KEY, filename TEXT NOT NULL)")
    platforms = [row[0] for row in staging.execute("SELECT DISTINCT platform FROM game ORDER BY platform")]
    for platform in platforms:
        filename = platform_filename(platform)
        target_name = os.path.join(software_dir, filename)
        if os.path.exists(target_name):
            os.unlink(target_name)
        target = sqlite3.connect(target_name)
        configure(target)
        target.execute(f"CREATE TABLE game ({GAME_COLUMNS})")
        target.executemany("INSERT INTO game VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                           staging.execute("SELECT * FROM game WHERE platform=?", (platform,)))
        target.execute("CREATE INDEX game_name ON game(normalized_name)")
        target.commit()
        target.execute("VACUUM")
        target.close()
        manifest.execute("INSERT INTO platform VALUES (?,?)", (platform, filename))
    manifest.commit()
    manifest.close()
    staging.close()
    os.unlink(staging_name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", nargs="?", default="launchbox", help="output directory")
    parser.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--archive", help="use an already-downloaded Metadata.zip")
    args = parser.parse_args()
    if args.archive:
        build(args.archive, args.output)
        return
    with tempfile.NamedTemporaryFile(suffix=".zip") as archive:
        print("Downloading", args.url)
        request = urllib.request.Request(args.url, headers={"User-Agent": "MAMEHub metadata updater"})
        with urllib.request.urlopen(request) as response:
            while chunk := response.read(1024 * 1024):
                archive.write(chunk)
        archive.flush()
        build(archive.name, args.output)
    print("Wrote", args.output)


if __name__ == "__main__":
    main()
