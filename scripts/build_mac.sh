#!/bin/bash
# Builds Galpon.app on a Mac (Apple Silicon or Intel).
# Usage:  ./scripts/build_mac.sh            -> build + run the tests
#         ./scripts/build_mac.sh --install  -> also copy Galpon.app to /Applications
set -euo pipefail
cd "$(dirname "$0")/.."

say() { printf "\n\033[1;33m▸ %s\033[0m\n" "$1"; }

if ! xcode-select -p >/dev/null 2>&1; then
  say "Faltan las herramientas de desarrollo de Apple. Se abre el instalador: acepta y vuelve a correr este script."
  xcode-select --install || true
  exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    say "Instalando CMake con Homebrew…"
    brew install cmake
  else
    say "Falta CMake. Instálalo desde https://cmake.org/download/ (o instala Homebrew) y vuelve a correr este script."
    exit 1
  fi
fi

JOBS=$(sysctl -n hw.ncpu)
mkdir -p build
LOG="build/ultimo-build.log"
: > "$LOG"
fail() { say "Algo falló. Lo que pasó quedó en $LOG (Claude lo puede leer)."; exit 1; }
say "Preparando (la primera vez descarga JUCE y Neural Amp Modeler, tarda unos minutos)…"
cmake -S . -B build -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$(uname -m)" 2>&1 | tee -a "$LOG" || fail

say "Compilando…"
cmake --build build --parallel "$JOBS" 2>&1 | tee -a "$LOG" || fail

say "Probando el motor…"
./build/GalponTests_artefacts/Release/GalponTests 2>&1 | tee -a "$LOG" || fail

APP="build/Galpon_artefacts/Release/Galpon.app"
# Ad-hoc signature so macOS lets it use the microphone and load plugins
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true

if [[ "${1:-}" == "--install" ]]; then
  say "Copiando a Aplicaciones…"
  rm -rf /Applications/Galpon.app
  cp -R "$APP" /Applications/
  APP="/Applications/Galpon.app"
fi

say "Listo: $APP"
open "$APP"
