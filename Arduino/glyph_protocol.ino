//file name:glyph_protocol.ino
//
// Construirea si citirea pachetelor radio, separat de tot restul.
//
// Motivul separarii: astea sunt functii pure - primesc un sir, intorc un sir
// sau o structura, nu ating hardware. Adica pot fi testate nativ pe PC, fara
// aparat. Suita din test/ face exact asta, inclusiv round-trip complet prin
// AES-GCM, folosind acelasi mbedtls.

// Contorul propriu, pastrat peste deep sleep. Creste la fiecare mesaj trimis.
RTC_DATA_ATTR uint32_t txCounter = 0;

// ---------------------------------------------------------------------------
// Ajutoare de format
// ---------------------------------------------------------------------------

// Contorul se scrie ca 8 caractere hex, urmat de '~'. Am ales '~' fiindca nu
// apare in kbChars, deci nu poate fi tastat de utilizator intr-un mesaj si nu
// poate fi confundat cu continut.
static const char COUNTER_SEP = '~';

String encodeCounterPrefix(uint32_t counter) {
    char buf[12];
    snprintf(buf, sizeof(buf), "%08lX%c", (unsigned long)counter, COUNTER_SEP);
    return String(buf);
}

// Desparte "0000001A~restul" in contor si rest.
// Daca prefixul lipseste sau e malformat, intoarce false si lasa restul intact:
// asa un mesaj de la un aparat mai vechi ramane lizibil.
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

// Extrage "|lat,lon" de la coada mesajului si il scoate din corp.
// Formatul asta a ramas neschimbat fata de versiunea originala, ca partea de
// afisare si radarul de echipa sa nu trebuiasca rescrise.
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

    // Coordonate in afara intervalului valid inseamna pachet corupt.
    if (pLat < -90.0 || pLat > 90.0 || pLon < -180.0 || pLon > 180.0) return false;

    lat = pLat;
    lon = pLon;
    body = body.substring(0, pipeIdx);
    return true;
}

// Numele expeditorului e partea dinaintea primului ':'.
String extractSender(const String &body) {
    int colonIdx = body.indexOf(':');
    if (colonIdx <= 0) return "";
    String name = body.substring(0, colonIdx);
    name.trim();
    return name;
}

// ---------------------------------------------------------------------------
// Construire
// ---------------------------------------------------------------------------
//
// payload e forma finala afisata plus coordonatele: "nume: text|lat,lon".
// Intoarce "" daca criptarea a esuat - preferam sa nu trimitem nimic decat sa
// trimitem in clar un mesaj pe care utilizatorul il crede securizat.
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

// Scade contorul de salturi al unui pachet primit, ca sa poata fi retransmis.
// Se lucreaza pe pachetul BRUT, neatins: retransmitem exact octetii primiti, cu
// un singur octet schimbat. Daca l-am reconstrui din continutul decriptat, ar
// pleca cu semnatura NOASTRA si cu contorul NOSTRU - adica ar arata ca un mesaj
// scris de noi, si originalul n-ar mai putea fi confirmat expeditorului real.
bool decrementHops(String &raw) {
    if (raw.length() < 3) return false;
    uint8_t marker = (uint8_t)raw[0];
    if (marker != PKT_V3_SECURE && marker != PKT_V3_PUBLIC) return false;
    uint8_t hops = (uint8_t)raw[2];
    if (hops == 0) return false;
    raw.setCharAt(2, (char)(hops - 1));
    return true;
}

// ---------------------------------------------------------------------------
// Citire
// ---------------------------------------------------------------------------
//
// Nu decide nimic despre ce se face cu mesajul - doar spune ce este.
// Verificarea de reluare se face separat, fiindca are nevoie de starea
// expeditorilor cunoscuti.
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
            if (plain.length() == 0) return m;      // tag invalid -> respins
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
            // Aparat cu firmware anterior: GCM, dar fara antet si fara contor.
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
            // CBC nu autentifica nimic: continutul poate fi modificat pe drum
            // fara sa observam. Il afisam, dar nu il tratam ca autentic, deci
            // nu ajunge in radarul de echipa.
            m.authentic = false;
            rest = plain;
            break;
        }
#endif
        default:
            // Mesaj public de la un aparat vechi: text simplu, fara marcaj.
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

// ---------------------------------------------------------------------------
// Anti-reluare
// ---------------------------------------------------------------------------
//
// Acceptam un mesaj de la un expeditor cunoscut doar daca are contorul strict
// mai mare decat ultimul acceptat. Contorul 0 inseamna "expeditor fara contor"
// (firmware vechi) si nu poate fi verificat - il lasam sa treaca, dar nu are
// acces la radar, fiindca acele formate nu sunt marcate ca autentice.
//
// Contorul se reseteaza cand aparatul expeditorului e reprogramat sau ii cade
// bateria complet; de aceea un contor mult mai mic decat ultimul (peste
// jumatate din intervalul pe 32 de biti in urma) e tratat ca repornire, nu ca
// atac, si e acceptat.
bool isReplay(const String &sender, uint32_t counter, bool authentic) {
    if (!authentic || counter == 0 || sender.length() == 0) return false;

    for (int i = 0; i < teammateCount; i++) {
        if (teammates[i].name != sender) continue;

        uint32_t last = teammates[i].lastCounter;
        if (last == 0) return false;                    // primul mesaj cu contor
        if (counter > last) return false;               // normal
        if ((last - counter) > 0x80000000UL) return false; // repornire aparat
        return true;                                    // reluare
    }
    return false;   // expeditor necunoscut: nu avem cu ce compara
}
