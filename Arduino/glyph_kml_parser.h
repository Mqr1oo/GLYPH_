#pragma once
//
// Tokenizerul de fisiere KML, scris ca sablon peste tipul de flux.
//
// Pe aparat primeste un File de pe cardul SD. In teste primeste un flux fals
// alimentat dintr-un sir - acelasi cod, aceleasi reguli de parsare, verificate
// pe PC fara card si fara aparat.
//
// Bucla asta era duplicata identic in loadKMLCache() si in drawKMLOverlay():
// acelasi parser de tag-uri, acelasi tokenizer, aceeasi hranire a watchdog-ului.
// Singura diferenta reala era ce se face cu fiecare coordonata.

// Pe PC nu exista yield(); pe aparat, fara el, un KML mare declanseaza watchdog-ul.
#ifndef GLYPH_KML_YIELD
  #ifdef ARDUINO
    #define GLYPH_KML_YIELD() yield()
  #else
    #define GLYPH_KML_YIELD() ((void)0)
  #endif
#endif

// Stream trebuie sa ofere available() si read().
// onPoint(lat, lon, newSegment) se apeleaza pentru fiecare coordonata valida.
template <typename Stream, typename Fn>
void parseKmlStream(Stream &f, Fn onPoint) {
    bool inCoords = false;
    bool newSegment = true;
    String token = "";
    int watchdogFeeder = 0;

    // Interpreteaza un token "lon,lat[,alt]" si il paseaza mai departe.
    // Ordinea lon,lat e cea din standardul KML, nu o inversiune accidentala.
    auto emit = [&](String t) {
        t.trim();
        if (t.length() <= 3) return;
        int c1 = t.indexOf(',');
        if (c1 <= 0) return;
        float lon = t.substring(0, c1).toFloat();
        int c2 = t.indexOf(',', c1 + 1);
        float lat = (c2 > 0) ? t.substring(c1 + 1, c2).toFloat()
                             : t.substring(c1 + 1).toFloat();
        if (lon == 0.0f || lat == 0.0f) return;
        onPoint(lat, lon, newSegment);
        newSegment = false;
    };

    while (f.available()) {
        char c = (char)f.read();

        watchdogFeeder++;
        if (watchdogFeeder > 250) { GLYPH_KML_YIELD(); watchdogFeeder = 0; }

        if (c == '<') {
            if (inCoords && token.length() > 0) { emit(token); token = ""; }
            String tag = "<";
            while (f.available()) {
                c = (char)f.read();
                watchdogFeeder++;
                if (watchdogFeeder > 250) { GLYPH_KML_YIELD(); watchdogFeeder = 0; }
                tag += c;
                if (c == '>') break;
            }
            if (tag.indexOf("<coordinates>") != -1) {
                inCoords = true;
                newSegment = true;
            } else if (tag.indexOf("</coordinates>") != -1) {
                inCoords = false;
                newSegment = true;
            }
        } else if (inCoords) {
            if (isspace((unsigned char)c)) {
                if (token.length() > 0) { emit(token); token = ""; }
            } else {
                // Limita de 64 opreste un fisier corupt sa creasca la nesfarsit
                // un String in memorie.
                if (token.length() < 64) token += c;
            }
        }
    }
}
