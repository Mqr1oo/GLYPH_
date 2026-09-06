#!/usr/bin/env bash
# Compileaza si ruleaza suita de teste native.
#
# Nu are nevoie de aparat, de card SD sau de radio - doar de g++ si mbedtls.
#   Ubuntu/Debian:  sudo apt install build-essential libmbedtls-dev
#   macOS:          brew install mbedtls
set -e
cd "$(dirname "$0")"

# -I. face ca <Arduino.h> sa se rezolve la puntea din acest folder, deci
# headerele reale ale proiectului se compileaza neatinse.
CXXFLAGS="-std=c++17 -O1 -Wall -Wno-unused-function -Wno-unused-variable -I."
LIBS="-lmbedcrypto"

# macOS (Homebrew) tine mbedtls in alt loc
if [ -d /opt/homebrew/include/mbedtls ]; then
    CXXFLAGS="$CXXFLAGS -I/opt/homebrew/include"
    LIBS="-L/opt/homebrew/lib $LIBS"
fi

g++ $CXXFLAGS test_main.cpp -o glyph_tests $LIBS
./glyph_tests
