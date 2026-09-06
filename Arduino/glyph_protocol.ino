//file name:glyph_protocol.ino
// Packet build and parse. Pure functions, no hardware, so the PC test suite
// covers them, including a full AES-GCM round trip.

RTC_DATA_ATTR uint32_t txCounter = 0;

// '~' is not in kbChars, so a user cannot type it and it can never be mistaken for content.
static const char COUNTER_SEP = '~';

String encodeCounterPrefix(uint32_t counter) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%08lX%c", (unsigned long)counter, COUNTER_SEP);
    return String(buf);
}

// A malformed prefix returns false with rest intact, keeping older messages readable.
bool splitCounterPrefix(const String &in, uint32_t &counter, String &rest) {
    if (in.length() < 9 || in[8] != COUNTER_SEP) {
        counter = 0;
        rest = in;
        return false;
    }
    for (int i = 0; i < 8; i++) {
        if (!isHexadecimalDigit(in[i])) {
            counter = 0;
            rest = in;
            return false;
        }
    }
    counter = (uint32_t)strtoul(in.substring(0, 8).c_str(), NULL, 16);
    rest = in.substring(9);
    return true;
}

// The trailing "|lat,lon" format is frozen: display and team radar depend on it.
bool extractCoords(String &body, double &lat, double &lon) {
    int pipeIdx = body.lastIndexOf('|');
    if (pipeIdx <= 0) return false;

    String coords = body.substring(pipeIdx + 1);
    int commaIdx = coords.indexOf(',');
    if (commaIdx <= 0) return false;

    String latStr = coords.substring(0, commaIdx);
    String lonStr = coords.substring(commaIdx + 1);
    if (latStr.length() == 0 || lonStr.length() == 0) return false;

    double pLat = latStr.toDouble();
    double pLon = lonStr.toDouble();

    if (pLat < -90.0 || pLat > 90.0 || pLon < -180.0 || pLon > 180.0) return false;

    lat = pLat;
    lon = pLon;
    body = body.substring(0, pipeIdx);
    return true;
}

String extractSender(const String &body) {
    int colonIdx = body.indexOf(':');
    if (colonIdx <= 0) return "";
    String name = body.substring(0, colonIdx);
    name.trim();
    return name;
}

// payload is the displayed form plus coordinates: "name: text|lat,lon". Returns
// "" on encryption failure: better to send nothing than to send in clear a
// message the user believes is secure. The hop byte sits OUTSIDE the ciphertext
// because every relay decrements it, and anything under the GCM tag cannot be
// changed without invalidating the message.
String buildPacket(uint8_t msgType, const String &payload, bool secure, uint8_t hops) {
    if (payload.length() == 0) return "";

    if (!secure) {
        String pkt = "";
        pkt += (char)PKT_V3_PUBLIC;
        pkt += (char)msgType;
        pkt += (char)hops;
        pkt += payload;
        return pkt;
    }

    txCounter++;
    String withCounter = encodeCounterPrefix(txCounter) + payload;

    String ct = cryptMsg(withCounter);
    if (ct.length() == 0) return "";

    String pkt = "";
    pkt += (char)PKT_V3_SECURE;
    pkt += (char)msgType;
    pkt += (char)hops;
    pkt += ct;
    return pkt;
}

// Relay the RAW bytes with one byte changed. Rebuilt from the decrypted content,
// the packet would carry OUR signature and OUR counter: it would look like our
// own message, and the real sender would never get an ack.
bool decrementHops(String &raw) {
    if (raw.length() < 3) return false;
    uint8_t marker = (uint8_t)raw[0];
    if (marker != PKT_V3_SECURE && marker != PKT_V3_PUBLIC) return false;
    uint8_t hops = (uint8_t)raw[2];
    if (hops == 0) return false;
    raw.setCharAt(2, (char)(hops - 1));
    return true;
}

// Reports what a packet is and decides nothing. The replay check is separate because it needs sender state.
GlyphMessage parsePacket(const String &raw) {
    GlyphMessage m;
    if (raw.length() < 2) return m;

    uint8_t marker = (uint8_t)raw[0];
    String rest;

    switch (marker) {
        case PKT_V3_SECURE: {
            if (raw.length() < 3) return m;
            m.type = (uint8_t)raw[1];
            m.hopsLeft = (uint8_t)raw[2];
            m.meshCapable = true;
            String plain = decryptMsg(raw.substring(3));
            if (plain.length() == 0) return m;
            m.authentic = true;
            splitCounterPrefix(plain, m.counter, rest);
            break;
        }
        case PKT_V3_PUBLIC: {
            if (raw.length() < 3) return m;
            m.type = (uint8_t)raw[1];
            m.hopsLeft = (uint8_t)raw[2];
            m.meshCapable = true;
            rest = raw.substring(3);
            break;
        }
        case PKT_V2_SECURE: {
            m.type = (uint8_t)raw[1];
            String plain = decryptMsg(raw.substring(2));
            if (plain.length() == 0) return m;
            m.authentic = true;
            splitCounterPrefix(plain, m.counter, rest);
            break;
        }
        case PKT_V2_PUBLIC: {
            m.type = (uint8_t)raw[1];
            rest = raw.substring(2);
            break;
        }
        case PKT_V1_SECURE: {
            // Older firmware: GCM, but no header and no counter.
            m.type = MSG_TEXT;
            String plain = decryptMsg(raw.substring(1));
            if (plain.length() == 0) return m;
            m.authentic = true;
            rest = plain;
            break;
        }
#if GLYPH_ACCEPT_LEGACY_CBC
        case PKT_V0_SECURE: {
            m.type = MSG_TEXT;
            String plain = decryptLegacyCBC(raw.substring(1));
            if (plain.length() == 0) return m;
            // CBC authenticates nothing: content can be altered in flight. Show
            // it, but not as authentic, so it never reaches the team radar.
            m.authentic = false;
            rest = plain;
            break;
        }
#endif
        default:
            rest = raw;
            break;
    }

    if (rest.length() == 0) return m;

    m.hasCoords = extractCoords(rest, m.lat, m.lon);
    m.sender = extractSender(rest);
    m.body = rest;
    m.valid = true;
    return m;
}

// The counter lives inside the encrypted part: GCM proves a message was not
// altered, not that it is new, so without it a recorded packet replays cleanly.
// Accept only a counter strictly above the last one seen. Counter 0 means old
// firmware with no counter and cannot be checked. A counter far below the last
// (over half the 32-bit range back) is a device restart, not an attack.
bool isReplay(const String &sender, uint32_t counter, bool authentic) {
    if (!authentic || counter == 0 || sender.length() == 0) return false;

    for (int i = 0; i < teammateCount; i++) {
        if (teammates[i].name != sender) continue;

        uint32_t last = teammates[i].lastCounter;
        if (last == 0) return false;
        if (counter > last) return false;
        if ((last - counter) > 0x80000000UL) return false;
        return true;
    }
    return false;   // unknown sender: nothing to compare against
}
