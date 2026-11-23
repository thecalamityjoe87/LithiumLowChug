#!/usr/bin/env bash
# Usage: scripts/build.sh [debug|release] [extra cmake --build args...]
#
# Configures and builds with the presets in CMakePresets.json: the build goes
# in build/<preset>/, the fetched dependencies in build/_deps/.
set -euo pipefail

preset="${1:-debug}"
shift || true

cd "$(dirname "$0")/.."
cmake --preset "$preset"
cmake --build --preset "$preset" "$@"

echo
echo "Built into build/$preset/src/plugin/LithiumLowChug_artefacts/"
