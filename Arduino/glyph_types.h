#pragma once
//
// Tipurile proprii ale proiectului stau intr-un header, nu in .ino.
//
// Motiv: builder-ul Arduino genereaza automat prototipuri pentru toate functiile
// din fisierele .ino si le insereaza imediat dupa blocul de #include din sketch.
// Daca o functie are in semnatura un enum definit mai jos in .ino (de exemplu
// applyPowerMode(PowerMode)), prototipul generat apare INAINTE de definitia
// enum-ului si compilarea esueaza cu un mesaj greu de legat de cauza.
// Definit intr-un header inclus sus, tipul exista deja cand se insereaza
// prototipurile.

#include <Arduino.h>
#include <functional>

#include "glyph_config.h"

enum SystemState {
    PAGE_INIT_LANG,
    PAGE_INIT_FREQ,
    PAGE_INIT_NAME,
    PAGE_INIT_TIME,
    PAGE_MAP,
    PAGE_KML_LIST,
    PAGE_CLOCK,
    PAGE_LORA,
    PAGE_KEYBOARD,
    PAGE_MENU_MAIN,
    PAGE_MENU_TEAM,
    PAGE_TEAM_NAME_EDIT,
    PAGE_MENU_POWER,
    PAGE_MENU_LANG,
    PAGE_MENU_TIME,
    STANDBY_MODE
};

enum PowerMode { NORMAL_MODE, ECO_MODE, STEALTH_MODE };

struct GeoPoint { float lat; float lon; };

// Un punct din overlay-ul KML, tinut in RAM dupa incarcare.
// newSegment marcheaza inceputul unei linii noi, ca sa nu unim doua trasee
// separate printr-o dreapta care traverseaza harta.
struct KmlPoint { float lat; float lon; bool newSegment; };

struct Teammate {
    String name;
    double lat;
    double lon;
    unsigned long lastSeen;
    uint32_t lastCounter;   // ultimul contor acceptat, pentru anti-reluare
};

// Callback pentru parcurgerea punctelor dintr-un fisier KML.
// newSegment e true la primul punct dintr-un bloc <coordinates> nou.
typedef std::function<void(float lat, float lon, bool newSegment)> KmlPointFn;

// ---------------------------------------------------------------------------
// Protocolul radio
// ---------------------------------------------------------------------------
//
// Formatul mesajului era implicit: "nume: text|lat,lon", fara nimic care sa
// spuna ce versiune urmeaza. Migrarea de la CBC la GCM a trebuit facuta prin
// marcaje ad-hoc pe primul octet. Acum exista un antet propriu-zis, iar
// urmatoarea schimbare de format nu mai are nevoie de trucuri.
//
// Pe fir:
//   [0] marcaj de format
//   [1] tip de mesaj (doar la v2)
//   restul: text simplu (public) sau hex(nonce|ciphertext|tag) (securizat)
//
// In interiorul partii criptate, inaintea textului, sta un contor pe 32 de biti.
// GCM garanteaza ca mesajul nu a fost modificat, dar nu ca e nou: fara contor,
// cine iti inregistreaza un SOS azi il poate redifuza maine si ar fi acceptat
// ca autentic, pentru ca este autentic - doar vechi.

// v3 adauga UN SINGUR octet: cate salturi mai are voie sa faca mesajul. Sta
// INAINTEA partii criptate, si nu inauntrul ei, fiindca fiecare releu trebuie
// sa-l scada - iar daca ar fi sub semnatura GCM, orice modificare ar invalida
// mesajul. Continutul ramane autentificat; doar contorul de salturi e in clar.
// Cel mai rau lucru pe care il poate face cineva modificandu-l e sa opreasca
// un mesaj din drum, ceea ce oricum putea face pur si simplu bruind.
static const uint8_t PKT_V3_SECURE = 0xFB;   // antet + salturi + AES-GCM + contor
static const uint8_t PKT_V3_PUBLIC = 0xFA;   // antet + salturi, necriptat

static const uint8_t PKT_V2_SECURE = 0xFD;   // antet + AES-GCM + contor
static const uint8_t PKT_V2_PUBLIC = 0xFC;   // antet, necriptat
static const uint8_t PKT_V1_SECURE = 0xFE;   // AES-GCM fara antet (compatibilitate)
static const uint8_t PKT_V0_SECURE = 0xFF;   // AES-CBC vechi (compatibilitate)

enum MsgType : uint8_t {
    MSG_TEXT = 1,
    MSG_SOS  = 2,
    // Confirmare de primire. Corpul e "nume: <contorul confirmat in hex>".
    // Nu e retransmisa niciodata: ar dubla traficul fara sa aduca nimic.
    MSG_ACK  = 3,
    // Ping de masurare a razei; raspunsul cara RSSI si SNR cu care a fost auzit.
    MSG_PING = 4,
    MSG_PONG = 5
};

// Rezultatul decodarii unui pachet receptionat.
struct GlyphMessage {
    bool     valid       = false;   // s-a putut decoda si autentifica
    bool     authentic   = false;   // a trecut prin decriptare GCM reusita
    bool     replay      = false;   // contor deja vazut de la acest expeditor
    uint8_t  type        = MSG_TEXT;
    uint32_t counter     = 0;
    String   sender;                // partea dinaintea lui ':'
    String   body;                  // "nume: text", forma afisata
    bool     hasCoords   = false;
    double   lat         = 0.0;
    double   lon         = 0.0;
    uint8_t  hopsLeft    = 0;       // cate retransmisii mai are voie
    bool     meshCapable = false;   // pachetul poarta antetul v3
};

// ---------------------------------------------------------------------------
// Starea perifericelor
//
// Inainte, daca radioul nu initializa, aparatul arata perfect normal si nu
// transmitea nimic. Pentru un dispozitiv facut sa functioneze cand nimic
// altceva nu mai functioneaza, esecul in tacere e cel mai prost mod de a esua.
// ---------------------------------------------------------------------------
struct DeviceHealth {
    bool radioOk = false;
    bool gpsOk   = false;
    bool sdOk    = false;
    bool imuOk   = false;
    bool shtOk   = false;
    bool buttonsOk = false;
    int  radioError = 0;    // codul returnat de RadioLib, daca a esuat

    // Ce trebuie neaparat sa mearga ca aparatul sa-si faca treaba.
    bool critical() const { return !radioOk || !buttonsOk; }
};
