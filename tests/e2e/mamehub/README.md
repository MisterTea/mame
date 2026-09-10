# MAMEHub end-to-end harness
#
# macOS UI automation for Discord-mock netplay sessions.
# Unit tests under `tests/frontend` cover lobby logic; these harnesses exercise
# the real UI + peer mesh + in-game input sync path.

## Requirements

- macOS (CGEvent / screencapture / System Events)
- Built `mamehub` binary at repo root (or `MAMEHUB_BIN`)
- `swiftc` (Xcode Command Line Tools)
- `rg` (ripgrep)
- Accessibility permission for Terminal/Cursor (key/mouse injection)
- Network access for candy-mode ROM fetch when sets are not already local

## Tests

### SNES two-player (Turtles in Time)

```bash
./tests/e2e/mamehub/run_twop_snes_tmnt4.sh
SKIP_MASH=1 ./tests/e2e/mamehub/run_twop_snes_tmnt4.sh   # launch-only
MASH_SECS=60 ./tests/e2e/mamehub/run_twop_snes_tmnt4.sh
```

Needs `roms/snes` softlist + `tmnt4`. After launch, P1 navigates to Two Players.

### X-Men arcade (2 players)

```bash
./tests/e2e/mamehub/run_xmen_2p.sh
SKIP_MASH=1 ./tests/e2e/mamehub/run_xmen_2p.sh
MASH_SECS=60 ./tests/e2e/mamehub/run_xmen_2p.sh
```

Needs `xmen2pe` (fetched via candy mode if not already under `roms/`). Arcade flow:
boot → all players coin+start → mash. No in-game “two players” menu.

### X-Men arcade (6 players)

```bash
# Prefer proving 2p first, then:
./tests/e2e/mamehub/run_xmen_6p.sh
```

Needs `xmen6p` (candy downloads if missing). Launches **6** `mamehub` processes with unique
`-port` / `-discord_directory_port` pairs (`5900+i` / `6000+i`).

Candy mode (`-candy`, default on) downloads missing ROM sets on first load and
keeps them under `rompath` for later runs. E2E does not require zips up front.
so local zips are optional.
Generic N-player arcade entrypoint:

```bash
PLAYERS=2 GAME=xmen2pe ./tests/e2e/mamehub/run_arcade_nplayer.sh
PLAYERS=6 GAME=xmen6p  ./tests/e2e/mamehub/run_arcade_nplayer.sh
```

Artifacts default to `/tmp/mamehub-e2e` (`MAMEHUB_E2E_OUT`).

## Layout

| Path | Purpose |
|------|---------|
| `run_twop_snes_tmnt4.sh` | SNES softlist 2p + title “Two Players” + mash |
| `run_xmen_2p.sh` / `run_xmen_6p.sh` | X-Men arcade wrappers |
| `run_arcade_nplayer.sh` | Parameterized arcade N-peer harness |
| `common.sh` | Shared helpers |
| `tools/*.swift` | UI helpers (highlight click, focus+keys, pad) |
| `tools/build.sh` | Compiles helpers into `tools/bin/` (gitignored) |

## What arcade tests assert

1. Host announces the selected machine
2. Guests join (do not host); announce reaches `players=N`
3. Each peer’s port appears in the mock lobby; Start Game + mesh ready
4. After boot, every peer coins + starts
5. No `INPUT DESYNC` / FATAL while all peers mash D-pad + attack buttons
