#!/usr/bin/env bash
# Builds dist/steam_api64.dll and dist/nms-ime-panel.exe, then stages everything
# needed for distribution in dist/ (binaries, config, installer, READMEs).
# Works in an MSYS2 UCRT64 shell (g++ on PATH) or Git Bash (falls back to C:\msys64).
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$(cygpath -w "$PWD")"

GXX="${GXX:-$(command -v g++ || true)}"
[ -n "$GXX" ] || GXX="C:/msys64/ucrt64/bin/g++.exe"
OBJDUMP="${OBJDUMP:-$(command -v objdump || true)}"
[ -n "$OBJDUMP" ] || OBJDUMP="C:/msys64/ucrt64/bin/objdump.exe"
CSC="${CSC:-$(command -v csc.exe || true)}"
[ -n "$CSC" ] || CSC="C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe"

mkdir -p dist
rm -f dist/steam_api64.dll dist/nms-ime-panel.exe

"$GXX" -O2 -shared -static-libgcc -static-libstdc++ -Wall -Wextra \
  -o dist/steam_api64.dll steam_api64_proxy.cpp

"$CSC" -nologo -codepage:65001 -target:winexe \
  -out:"$ROOT\\dist\\nms-ime-panel.exe" -reference:System.dll -reference:System.Drawing.dll \
  -reference:System.Windows.Forms.dll "$ROOT\\panel\\panel.cs"

cp -f nms-ime-proxy.ini install.ps1 README.md README.en.md dist/
"$OBJDUMP" -p dist/steam_api64.dll | sed -n '/Ordinal\/Name Pointer/,/Function Table/p'