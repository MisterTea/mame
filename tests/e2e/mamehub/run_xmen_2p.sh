#!/bin/bash
# X-Men arcade 2-player e2e (macOS).
# ROMs need not be local: candy mode (-candy, default on) downloads on load.
set -uo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
export PLAYERS=2
export GAME="${GAME:-xmen2pe}"
export MASH_SECS="${MASH_SECS:-180}"
exec "$DIR/run_arcade_nplayer.sh"
