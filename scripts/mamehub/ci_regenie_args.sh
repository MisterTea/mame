#!/bin/bash
# Print REGENIE=0/1 for CI make, or write the stamp after a successful build.
# Run from the MAME root with GENIE_INPUT_HASH set to
# hashFiles(makefile, scripts/**, genie sources, this workflow).
#
# REGENIE only regenerates GENie project files. It is needed when those inputs
# change (new/removed sources, toolchain flags, makefile options). It is not
# needed for ordinary .cpp/.h edits. Skipping it when the hash matches keeps
# generated Makefiles' timestamps stable; GENie lists $(MAKEFILE) as a
# prerequisite of every .o, so rewriting an unchanged Makefile would rebuild
# the whole tree.
set -euo pipefail

stamp=build/.genie-input-hash

if [[ "${1:-}" == --stamp ]]; then
  mkdir -p build
  printf '%s\n' "${GENIE_INPUT_HASH:-}" > "$stamp"
  exit 0
fi

if [[ -z "${GENIE_INPUT_HASH:-}" ]]; then
  echo "REGENIE=1"
  exit 0
fi
if [[ -f "$stamp" && "$(cat "$stamp")" == "$GENIE_INPUT_HASH" ]]; then
  echo "REGENIE=0"
else
  echo "REGENIE=1"
fi
