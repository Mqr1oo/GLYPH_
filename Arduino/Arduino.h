#pragma once
// Punte minima pentru testele native: glyph_types.h include <Arduino.h>, iar pe
// PC nu exista. Fisierul asta trimite mai departe la shim-ul nostru, ca headerul
// real al proiectului sa poata fi compilat neatins - fara #ifdef-uri prin el.
#include "arduino_shim.h"
