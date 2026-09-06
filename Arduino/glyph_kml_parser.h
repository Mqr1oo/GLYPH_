#pragma once
//
// KML tokenizer, written as a template over the stream type. On the device the
// stream is a File from the SD card; in tests it is a fake stream fed from a
// string. Same code, same parse rules, verified on a PC with no card and no
// device.

// There is no yield() on a PC. On the device, without it, a large KML trips the
// watchdog.
#ifndef GLYPH_KML_YIELD
  #ifdef ARDUINO
    #define GLYPH_KML_YIELD() yield()
  #else
    #define GLYPH_KML_YIELD() ((void)0)
  #endif
#endif

// Stream must provide available() and read().
// onPoint(lat, lon, newSegment) is called for every valid coordinate.
template <typename Stream, typename Fn>
void parseKmlStream(Stream &f, Fn onPoint) {
    bool inCoords = false;
    bool newSegment = true;
    String token = "";
    int watchdogFeeder = 0;

    // Token is "lon,lat[,alt]". The lon,lat order comes from the KML standard,
    // it is not an accidental swap.
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
                // The 64 char cap stops a corrupt file from growing a String
                // without bound.
                if (token.length() < 64) token += c;
            }
        }
    }
}
