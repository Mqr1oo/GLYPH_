// Suita de teste native pentru GLYPH.
//
// Ce testeaza: exact codul care ruleaza pe aparat. Fisierele .ino cu logica
// pura sunt incluse direct - nu sunt copii, nu sunt reimplementari.
//
// Ruleaza cu:  cd test && ./run.sh
//
// Nu ai nevoie de aparat, de card SD sau de radio. Dureaza cateva secunde.

#include "arduino_shim.h"

#include <cassert>
#include <iostream>
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// Globalele de care are nevoie codul testat.
// Pe aparat sunt definite in GLYPH_.ino; aici le declaram noi, ca sa nu tragem
// dupa noi tot firmware-ul cu tot cu drivere.
// ---------------------------------------------------------------------------
unsigned long g_fakeMillis = 100000;

String myTeam = "ALPHA";
bool   simActive = false;
bool   gps_ok = true;
bool   gpsTimeValid = true;
bool   useAmPmFormat = false;
int    timeOffset = 2;

uint16_t gpsYear = 2026;
uint8_t  gpsMonth = 8, gpsDay = 28, gpsHour = 12, gpsMinute = 30, gpsSecond = 15;

struct Teammate {
    String name;
    double lat = 0, lon = 0;
    unsigned long lastSeen = 0;
    uint32_t lastCounter = 0;
};
static const int MAX_TEAMMATES = 5;
Teammate teammates[MAX_TEAMMATES];
int teammateCount = 0;

// Tipurile si constantele de protocol, identice cu cele din glyph_types.h.
static const uint8_t PKT_V2_SECURE = 0xFD;
static const uint8_t PKT_V2_PUBLIC = 0xFC;
static const uint8_t PKT_V1_SECURE = 0xFE;
static const uint8_t PKT_V0_SECURE = 0xFF;
enum MsgType : uint8_t { MSG_TEXT = 1, MSG_SOS = 2 };

struct GlyphMessage {
    bool     valid     = false;
    bool     authentic = false;
    bool     replay    = false;
    uint8_t  type      = MSG_TEXT;
    uint32_t counter   = 0;
    String   sender;
    String   body;
    bool     hasCoords = false;
    double   lat = 0.0, lon = 0.0;
};

#define GLYPH_ACCEPT_LEGACY_CBC 1

// ---------------------------------------------------------------------------
// Codul real, inclus ca sursa.
// ---------------------------------------------------------------------------
#include "../Arduino/glyph_pure.ino"
#include "../Arduino/glyph_crypto.ino"
#include "../Arduino/glyph_protocol.ino"
#include "../Arduino/glyph_kml_parser.h"

// ---------------------------------------------------------------------------
// Micro-framework: fara dependinte, doar numaratoare.
// ---------------------------------------------------------------------------
static int g_checks = 0, g_failures = 0;
static const char* g_group = "";

#define GROUP(name) do { g_group = name; std::cout << "\n== " << name << "\n"; } while (0)

#define CHECK(cond, what) do {                                                  \
    g_checks++;                                                                 \
    if (!(cond)) {                                                              \
        g_failures++;                                                           \
        std::cout << "  FAIL  " << what << "   [" << __FILE__ << ":"            \
                  << __LINE__ << "]\n";                                         \
    }                                                                           \
} while (0)

#define CHECK_EQ_STR(a, b, what) do {                                           \
    g_checks++;                                                                 \
    std::string A = std::string((a).c_str()), B = std::string(b);               \
    if (A != B) {                                                               \
        g_failures++;                                                           \
        std::cout << "  FAIL  " << what << "\n         got: \"" << A            \
                  << "\"\n         want: \"" << B << "\"\n";                    \
    }                                                                           \
} while (0)

// ---------------------------------------------------------------------------
// 1. Conversia UTC -> ora locala
//
// Verificata contra bibliotecii standard pe toate combinatiile de ora si offset,
// pe date de granita: sfarsit de an, sfarsit de luna, 28 si 29 februarie in an
// bisect si nebisect.
// ---------------------------------------------------------------------------
static void expectLocal(int Y, int Mo, int D, int H, int Mi, int off,
                        int& gy, int& gmo, int& gd, int& gh) {
    struct tm t = {};
    t.tm_year = Y - 1900; t.tm_mon = Mo - 1; t.tm_mday = D;
    t.tm_hour = H; t.tm_min = Mi; t.tm_sec = 0;
    time_t base = timegm(&t) + (time_t)off * 3600;
    struct tm out;
    gmtime_r(&base, &out);
    gy = out.tm_year + 1900; gmo = out.tm_mon + 1; gd = out.tm_mday; gh = out.tm_hour;
}

static void testLocalTime() {
    GROUP("Conversia UTC -> ora locala");

    struct { int y, mo, d; } dates[] = {
        {2026, 8, 28}, {2026, 12, 31}, {2026, 1, 1},
        {2024, 2, 28}, {2024, 2, 29}, {2024, 3, 1},
        {2025, 2, 28}, {2025, 3, 1},  {2100, 2, 28},
        {2026, 4, 30}, {2026, 5, 1},
    };

    int mismatches = 0, total = 0;
    for (auto& dt : dates) {
        for (int h = 0; h < 24; h++) {
            for (int off = -12; off <= 14; off++) {
                gpsYear = (uint16_t)dt.y; gpsMonth = (uint8_t)dt.mo; gpsDay = (uint8_t)dt.d;
                gpsHour = (uint8_t)h; gpsMinute = 30; gpsSecond = 0;
                timeOffset = off;

                int y, mo, d, hh, mi, ss;
                getLocalDateTime(y, mo, d, hh, mi, ss);

                int ey, emo, ed, eh;
                expectLocal(dt.y, dt.mo, dt.d, h, 30, off, ey, emo, ed, eh);

                total++;
                if (y != ey || mo != emo || d != ed || hh != eh) {
                    if (mismatches < 3) {
                        std::cout << "  FAIL  " << dt.y << "-" << dt.mo << "-" << dt.d
                                  << " " << h << ":30 UTC" << (off >= 0 ? "+" : "") << off
                                  << "  got " << y << "-" << mo << "-" << d << " " << hh
                                  << "  want " << ey << "-" << emo << "-" << ed << " " << eh << "\n";
                    }
                    mismatches++;
                }
            }
        }
    }
    g_checks++;
    if (mismatches) { g_failures++; std::cout << "  " << mismatches << "/" << total << " nepotriviri\n"; }
    else std::cout << "  " << total << " combinatii de data/ora/offset, toate corecte\n";

    // Cazul concret care era gresit inainte: UTC+2, dupa ora 22:00 fisierul
    // primea data de ieri.
    gpsYear = 2026; gpsMonth = 8; gpsDay = 28; gpsHour = 23; gpsMinute = 30; gpsSecond = 0;
    timeOffset = 2;
    CHECK_EQ_STR(formatLocalDateFile(), "29_08_2026", "trecerea peste miezul noptii schimba data");
    CHECK_EQ_STR(formatLocalTime(), "01:30", "ora locala dupa miezul noptii");

    // Offset negativ, inapoi peste inceputul de an.
    gpsYear = 2026; gpsMonth = 1; gpsDay = 1; gpsHour = 2; gpsMinute = 0;
    timeOffset = -5;
    CHECK_EQ_STR(formatLocalDateFile(), "31_12_2025", "offset negativ trece inapoi in anul precedent");

    // Format AM/PM.
    gpsYear = 2026; gpsMonth = 8; gpsDay = 28; gpsHour = 13; gpsMinute = 5;
    timeOffset = 0; useAmPmFormat = true;
    CHECK_EQ_STR(formatLocalTime(), "01:05PM", "format AM/PM dupa-amiaza");
    gpsHour = 0; gpsMinute = 5;
    CHECK_EQ_STR(formatLocalTime(), "12:05AM", "miezul noptii e 12 AM, nu 00 AM");
    useAmPmFormat = false;

    // Fara ora valida de la GPS nu inventam nimic.
    gpsTimeValid = false; simActive = false;
    CHECK_EQ_STR(formatLocalTime(), "--:--", "fara fix, ceasul arata --:--");
    gpsTimeValid = true;

    timeOffset = 2;
}

// ---------------------------------------------------------------------------
// 2. Distanta geografica
// ---------------------------------------------------------------------------
static void testDistance() {
    GROUP("Calculul distantei");

    CHECK(calculateDistance(44.4268, 26.1025, 44.4268, 26.1025) == 0.0,
          "distanta de la un punct la el insusi e zero");

    // Bucuresti - Cluj, aproximativ 325 km in linie dreapta.
    double d = calculateDistance(44.4268, 26.1025, 46.7712, 23.6236);
    CHECK(d > 315.0 && d < 335.0, "Bucuresti-Cluj este in intervalul asteptat");

    // Simetrie.
    double a = calculateDistance(44.0, 26.0, 45.0, 27.0);
    double b = calculateDistance(45.0, 27.0, 44.0, 26.0);
    CHECK(std::fabs(a - b) < 1e-9, "distanta e simetrica");

    // Un grad de latitudine este ~111 km oriunde pe glob.
    double oneDeg = calculateDistance(0.0, 0.0, 1.0, 0.0);
    CHECK(oneDeg > 110.0 && oneDeg < 112.0, "un grad de latitudine ~111 km");

    // Peste antimeridian nu trebuie sa dea jumatate de planeta.
    double meridian = calculateDistance(0.0, 179.9, 0.0, -179.9);
    CHECK(meridian < 30.0, "traversarea antimeridianului nu explodeaza");
}

// ---------------------------------------------------------------------------
// 3. Criptografie: round-trip, autenticitate, chei diferite
// ---------------------------------------------------------------------------
static void testCrypto() {
    GROUP("Criptografie AES-256-GCM");

    myTeam = "ALPHA";
    invalidateTeamKey();

    String plain = "mario: ne vedem la creasta|45.12345,25.54321";
    String ct = cryptMsg(plain);
    CHECK(ct.length() > 0, "criptarea produce ceva");
    CHECK_EQ_STR(decryptMsg(ct), std::string(plain.c_str()), "round-trip complet");

    // Acelasi text, criptat de doua ori, da rezultate diferite: nonce aleator.
    String ct2 = cryptMsg(plain);
    CHECK(!(ct == ct2), "acelasi mesaj criptat de doua ori difera (nonce aleator)");
    CHECK_EQ_STR(decryptMsg(ct2), std::string(plain.c_str()), "si a doua varianta se decripteaza");

    // Un bit modificat trebuie sa fie respins. Asta e diferenta fata de CBC,
    // unde mesajul modificat era livrat ca text alterat.
    String tampered = ct;
    unsigned mid = tampered.length() / 2;
    char c = tampered.s[mid];
    tampered.s[mid] = (c == '0') ? '1' : '0';
    CHECK_EQ_STR(decryptMsg(tampered), "", "un singur caracter modificat -> mesaj respins");

    // Tag-ul taiat.
    CHECK_EQ_STR(decryptMsg(ct.substring(0, ct.length() - 4)), "", "tag trunchiat -> respins");

    // Alta echipa, alta cheie.
    myTeam = "BRAVO"; invalidateTeamKey();
    CHECK_EQ_STR(decryptMsg(ct), "", "alta echipa nu poate citi mesajul");
    myTeam = "ALPHA"; invalidateTeamKey();
    CHECK_EQ_STR(decryptMsg(ct), std::string(plain.c_str()), "revenind la echipa corecta, merge din nou");

    // Intrari malformate nu trebuie sa dea crash.
    CHECK_EQ_STR(decryptMsg(""), "", "sir gol");
    CHECK_EQ_STR(decryptMsg("zzzz"), "", "caractere non-hex");
    CHECK_EQ_STR(decryptMsg("abc"), "", "lungime impara");
    CHECK_EQ_STR(decryptMsg(String(std::string(200, 'f'))), "", "hex valid dar continut aleator");

    // Mesaj lung, la limita a ce incape intr-un pachet LoRa.
    String longMsg = "user1234567890ab: " + String(std::string(30, 'X')) + "|45.12345,25.54321";
    String lct = cryptMsg(longMsg);
    CHECK_EQ_STR(decryptMsg(lct), std::string(longMsg.c_str()), "mesaj de lungime maxima");
    CHECK(lct.length() + 2 < 250, "pachetul incape in limita LoRa de 255 octeti");
}

// ---------------------------------------------------------------------------
// 4. Derivarea cheii
// ---------------------------------------------------------------------------
static void testKeyDerivation() {
    GROUP("Derivarea cheii (PBKDF2)");

    byte k1[32], k2[32], k3[32];

    myTeam = "ALPHA"; invalidateTeamKey(); getAESKey(k1);
    myTeam = "ALPHA"; invalidateTeamKey(); getAESKey(k2);
    CHECK(memcmp(k1, k2, 32) == 0, "aceeasi echipa da aceeasi cheie");

    myTeam = "ALPHB"; invalidateTeamKey(); getAESKey(k3);
    CHECK(memcmp(k1, k3, 32) != 0, "un singur caracter diferit schimba cheia complet");

    // Cheia nu trebuie sa fie nici pe departe simplul SHA-256 al numelui,
    // ceea ce era problema originala.
    bool allZero = true;
    for (int i = 0; i < 32; i++) if (k1[i] != 0) allZero = false;
    CHECK(!allZero, "cheia nu e zero");

    // Numele gol cade pe valoarea implicita, nu pe o cheie nula.
    myTeam = ""; invalidateTeamKey();
    byte k4[32]; getAESKey(k4);
    myTeam = "ALPHA"; invalidateTeamKey();
    byte k5[32]; getAESKey(k5);
    CHECK(memcmp(k4, k5, 32) == 0, "echipa goala inseamna ALPHA");
}

// ---------------------------------------------------------------------------
// 5. Formatul pachetului
// ---------------------------------------------------------------------------
static void testPacketFormat() {
    GROUP("Formatul pachetului");

    myTeam = "ALPHA"; invalidateTeamKey();
    txCounter = 0;
    teammateCount = 0;

    String payload = "mario: test|45.12345,25.54321";

    // --- pachet public ---
    String pub = buildPacket(MSG_TEXT, payload, false);
    CHECK((uint8_t)pub[0] == PKT_V2_PUBLIC, "pachetul public are marcajul corect");
    CHECK((uint8_t)pub[1] == MSG_TEXT, "tipul de mesaj e in antet");

    GlyphMessage m = parsePacket(pub);
    CHECK(m.valid, "pachetul public se decodeaza");
    CHECK(!m.authentic, "pachetul public NU e autentic");
    CHECK_EQ_STR(m.sender, "mario", "expeditorul e extras");
    CHECK_EQ_STR(m.body, "mario: test", "corpul e curatat de coordonate");
    CHECK(m.hasCoords, "coordonatele sunt gasite");
    CHECK(std::fabs(m.lat - 45.12345) < 1e-5, "latitudinea e corecta");
    CHECK(std::fabs(m.lon - 25.54321) < 1e-5, "longitudinea e corecta");

    // --- pachet securizat ---
    String sec = buildPacket(MSG_TEXT, payload, true);
    CHECK((uint8_t)sec[0] == PKT_V2_SECURE, "pachetul securizat are marcajul corect");

    GlyphMessage sm = parsePacket(sec);
    CHECK(sm.valid, "pachetul securizat se decodeaza");
    CHECK(sm.authentic, "pachetul securizat e autentic");
    CHECK(sm.counter == 1, "contorul incepe de la 1");
    CHECK_EQ_STR(sm.sender, "mario", "expeditorul supravietuieste criptarii");
    CHECK_EQ_STR(sm.body, "mario: test", "corpul supravietuieste criptarii");
    CHECK(std::fabs(sm.lat - 45.12345) < 1e-5, "coordonatele supravietuiesc criptarii");

    // Contorul creste la fiecare mesaj.
    GlyphMessage sm2 = parsePacket(buildPacket(MSG_TEXT, payload, true));
    CHECK(sm2.counter == 2, "contorul creste");

    // --- SOS ---
    GlyphMessage sos = parsePacket(buildPacket(MSG_SOS, payload, true));
    CHECK(sos.type == MSG_SOS, "tipul SOS e pastrat");

    // --- un pachet stricat nu produce mesaj valid ---
    String broken = sec;
    broken.s[10] = (broken.s[10] == 'a') ? 'b' : 'a';
    GlyphMessage bm = parsePacket(broken);
    CHECK(!bm.valid || !bm.authentic, "pachetul modificat nu trece ca autentic");

    // --- pachete degenerate ---
    CHECK(!parsePacket("").valid, "pachet gol");
    CHECK(!parsePacket("x").valid, "pachet de un octet");

    // --- text simplu de la un aparat vechi, fara marcaj ---
    GlyphMessage legacy = parsePacket("ana: salut|44.0,26.0");
    CHECK(legacy.valid, "textul simplu vechi e inteles");
    CHECK(!legacy.authentic, "textul simplu nu e autentic");
    CHECK_EQ_STR(legacy.sender, "ana", "expeditorul din formatul vechi");
}

// ---------------------------------------------------------------------------
// 6. Extragerea coordonatelor
// ---------------------------------------------------------------------------
static void testCoordParsing() {
    GROUP("Extragerea coordonatelor");

    double lat = 0, lon = 0;

    String a = "mario: text|45.5,25.5";
    CHECK(extractCoords(a, lat, lon), "format normal");
    CHECK_EQ_STR(a, "mario: text", "coordonatele sunt scoase din corp");

    String b = "fara coordonate";
    CHECK(!extractCoords(b, lat, lon), "mesaj fara coordonate");
    CHECK_EQ_STR(b, "fara coordonate", "corpul ramane neatins");

    // Valorile in afara intervalului valid inseamna pachet corupt.
    String c = "x: y|999.0,25.0";
    CHECK(!extractCoords(c, lat, lon), "latitudine imposibila e respinsa");
    String d = "x: y|45.0,999.0";
    CHECK(!extractCoords(d, lat, lon), "longitudine imposibila e respinsa");

    // Coordonate negative, cazul emisferei sudice si vestice.
    String e = "x: y|-33.86,-151.20";
    CHECK(extractCoords(e, lat, lon), "coordonate negative");
    CHECK(lat < 0 && lon < 0, "semnele sunt pastrate");

    // Un mesaj care contine el insusi caracterul '|' - se ia ultimul.
    String f = "x: a|b|45.0,25.0";
    CHECK(extractCoords(f, lat, lon), "se foloseste ultimul separator");
    CHECK_EQ_STR(f, "x: a|b", "textul cu | ramane intreg");

    String g = "x: y|abc";
    CHECK(!extractCoords(g, lat, lon), "coordonate malformate");

    // Expeditor
    CHECK_EQ_STR(extractSender("mario: salut"), "mario", "expeditor simplu");
    CHECK_EQ_STR(extractSender("fara doua puncte"), "", "fara expeditor");
    CHECK_EQ_STR(extractSender(": gol"), "", "expeditor gol");
}

// ---------------------------------------------------------------------------
// 7. Contorul si anti-reluarea
// ---------------------------------------------------------------------------
static void testReplayProtection() {
    GROUP("Protectia la reluare");

    uint32_t c; String rest;

    CHECK(splitCounterPrefix("0000000A~mesaj", c, rest), "prefixul de contor e recunoscut");
    CHECK(c == 10, "contorul e citit corect");
    CHECK_EQ_STR(rest, "mesaj", "restul mesajului e separat");

    CHECK(!splitCounterPrefix("mesaj fara contor", c, rest), "mesaj fara prefix");
    CHECK_EQ_STR(rest, "mesaj fara contor", "mesajul ramane intreg daca nu are prefix");

    CHECK(!splitCounterPrefix("ZZZZZZZZ~x", c, rest), "prefix non-hex respins");

    // Tabelul de coechipieri
    teammateCount = 1;
    teammates[0].name = "ana";
    teammates[0].lastCounter = 100;

    CHECK(!isReplay("ana", 101, true), "contor mai mare este acceptat");
    CHECK(isReplay("ana", 100, true),  "acelasi contor este reluare");
    CHECK(isReplay("ana", 50, true),   "contor mai mic este reluare");
    CHECK(!isReplay("ana", 101, false),"mesajele neautentificate nu sunt verificate");
    CHECK(!isReplay("necunoscut", 5, true), "expeditor nou nu poate fi verificat");
    CHECK(!isReplay("ana", 0, true),   "contor zero inseamna firmware vechi");

    // Aparatul expeditorului a fost reprogramat: contorul o ia de la capat.
    teammates[0].lastCounter = 4000000000u;
    CHECK(!isReplay("ana", 5, true), "repornirea expeditorului nu e tratata ca atac");

    teammateCount = 0;
}

// ---------------------------------------------------------------------------
// 8. Parserul KML
// ---------------------------------------------------------------------------
struct FakeStream {
    std::string data;
    size_t pos = 0;
    explicit FakeStream(const std::string& d) : data(d) {}
    bool available() const { return pos < data.size(); }
    int  read() { return pos < data.size() ? (unsigned char)data[pos++] : -1; }
};

static void testKmlParser() {
    GROUP("Parserul KML");

    struct P { float lat, lon; bool seg; };

    {
        FakeStream f(
            "<?xml version=\"1.0\"?><kml><Document><Placemark><LineString>"
            "<coordinates>\n"
            "25.1,45.1,300\n25.2,45.2,310\n25.3,45.3,320\n"
            "</coordinates></LineString></Placemark></Document></kml>");
        std::vector<P> pts;
        parseKmlStream(f, [&](float lat, float lon, bool seg) { pts.push_back({lat, lon, seg}); });

        CHECK(pts.size() == 3, "trei puncte citite");
        if (pts.size() == 3) {
            CHECK(std::fabs(pts[0].lat - 45.1f) < 1e-4, "latitudinea primului punct");
            CHECK(std::fabs(pts[0].lon - 25.1f) < 1e-4, "longitudinea primului punct (KML e lon,lat)");
            CHECK(pts[0].seg, "primul punct incepe un segment");
            CHECK(!pts[1].seg, "al doilea punct continua segmentul");
        }
    }

    {
        // Doua trasee separate: al doilea trebuie sa inceapa un segment nou,
        // altfel apar unite printr-o linie dreapta peste toata harta.
        FakeStream f(
            "<coordinates>25.1,45.1 25.2,45.2</coordinates>"
            "<coordinates>26.1,46.1 26.2,46.2</coordinates>");
        std::vector<P> pts;
        parseKmlStream(f, [&](float lat, float lon, bool seg) { pts.push_back({lat, lon, seg}); });

        CHECK(pts.size() == 4, "patru puncte din doua segmente");
        if (pts.size() == 4) {
            CHECK(pts[0].seg && !pts[1].seg, "primul segment");
            CHECK(pts[2].seg && !pts[3].seg, "al doilea segment porneste separat");
        }
    }

    {
        // Text in afara blocului <coordinates> nu trebuie interpretat.
        FakeStream f("<name>99.9,99.9</name><coordinates>25.1,45.1</coordinates>");
        std::vector<P> pts;
        parseKmlStream(f, [&](float lat, float lon, bool seg) { pts.push_back({lat, lon, seg}); });
        CHECK(pts.size() == 1, "doar coordonatele din blocul corect sunt luate");
    }

    {
        FakeStream f("");
        int n = 0;
        parseKmlStream(f, [&](float, float, bool) { n++; });
        CHECK(n == 0, "fisier gol");
    }

    {
        // Fisier trunchiat la mijloc - nu trebuie sa duca la bucla infinita.
        FakeStream f("<coordinates>25.1,45.1 25.2,4");
        int n = 0;
        parseKmlStream(f, [&](float, float, bool) { n++; });
        CHECK(n >= 1, "fisier trunchiat: se citeste ce se poate");
    }

    {
        // Coordonate cu zero: sarite intentionat (0,0 e in ocean, langa Africa,
        // si aproape intotdeauna inseamna date lipsa).
        FakeStream f("<coordinates>0,0 25.1,45.1</coordinates>");
        int n = 0;
        parseKmlStream(f, [&](float, float, bool) { n++; });
        CHECK(n == 1, "punctul 0,0 e ignorat");
    }
}

// ---------------------------------------------------------------------------
int main() {
    std::cout << "GLYPH - teste native\n";

    testLocalTime();
    testDistance();
    testKeyDerivation();
    testCrypto();
    testPacketFormat();
    testCoordParsing();
    testReplayProtection();
    testKmlParser();

    std::cout << "\n----------------------------------------\n";
    if (g_failures == 0) {
        std::cout << "TOATE TESTELE AU TRECUT  (" << g_checks << " verificari)\n";
        return 0;
    }
    std::cout << g_failures << " ESECURI din " << g_checks << " verificari\n";
    return 1;
}
