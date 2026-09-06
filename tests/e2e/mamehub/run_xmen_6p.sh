#!/bin/bash
# X-Men arcade 6-player e2e (macOS).
# ROMs need not be local: candy mode downloads on load. Prefer run_xmen_2p.sh first.
# Launches 6 mamehub instances with unique -port / -discord_directory_port pairs.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
export PLAYERS=6
export GAME="${GAME:-xmen6p}"
export MASH_SECS="${MASH_SECS:-180}"
exec "$DIR/run_arcade_nplayer.sh"
