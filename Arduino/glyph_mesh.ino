//file name:glyph_mesh.ino
//
// RETEAUA MESH, CONFIRMARILE SI MASURAREA RAZEI
//
// Cum functioneaza, pe scurt: nu exista rute, nu exista tabele de vecini si nu
// exista nimic de reglat. Fiecare aparat care aude un mesaj care nu e al lui si
// care mai are salturi ramase asteapta putin si il retransmite - dar asteapta
// cu ATAT MAI PUTIN cu cat l-a auzit mai SLAB.
//
// De ce asta alege drumul scurt: cine aude slab e departe de emitator, deci
// retransmisia lui acopera cel mai mult teren nou; el vorbeste primul. Cine
// aude tare e langa emitator si n-ar aduce nimic; el asteapta, aude
// retransmisia celui indepartat, si renunta la a lui. Din cateva aparate
// rezulta salturi lungi in directia buna, fara ca vreunul sa calculeze ceva.
//
// Aceeasi idee o folosesc si alte retele LoRa; e mai robusta decat rutarea
// clasica exact pentru ca nu tine minte nimic, deci n-are ce sa devina gresit
// cand oamenii se misca.

#include "glyph_mesh_core.h"

static void sendPongNow();

static DutyBudget duty;
static MeshSeen   seen;

// O singura retransmisie in asteptare. Doua ar insemna doua emisii una peste
// alta, si oricum un aparat aflat intr-un loc atat de aglomerat nu e cel care
// trebuie sa retransmita.
static bool     relayPending  = false;
static uint32_t relayDueAt    = 0;
static String   relayPacket   = "";
static uint16_t relayKey      = 0;
static uint32_t relayCounter  = 0;

// Confirmarea de primire, tot cu o mica intarziere: daca trei aparate din
// echipa aud acelasi mesaj si raspund toate in aceeasi milisecunda, niciunul
// nu se aude.
static bool     ackPending    = false;
static uint32_t ackDueAt      = 0;
static String   ackTo         = "";
static uint32_t ackCounter    = 0;

// Raspunsul la ping, tot amanat. Prima varianta folosea delay() ca sa imprastie
// raspunsurile - adica bloca bucla pana la o secunda, exact bug-ul pentru care
// nu mergeau butoanele cand GPS-ul era pornit. Se rezolva la fel: o stare si o
// scadenta, servite din loop().
static bool     pongPending   = false;
static uint32_t pongDueAt     = 0;
static float    pongSnr       = 0.0f;
static float    pongRssi      = 0.0f;

void meshBegin() {
    duty.begin(DUTY_CYCLE_BUDGET_MS, millis());
    seen.begin();
}

int meshDutyPercent() { duty.tick(millis()); return duty.percentUsed(); }

// Lungimea folosita la calculul timpului de emisie e a pachetului intreg.
// Trece prin glyphAirtimeMs din header, ca sa fie exact functia acoperita de
// teste - nu o copie a ei care poate diverge in tacere.
static uint32_t airtimeFor(int payloadLen) { return glyphAirtimeMs(payloadLen); }

bool meshCanSend(int payloadLen) {
    duty.tick(millis());
    return duty.canSend(airtimeFor(payloadLen));
}

void meshNoteTransmit(int payloadLen) {
    duty.tick(millis());
    duty.add(airtimeFor(payloadLen));
}

// ---------------------------------------------------------------------------
// La receptie
// ---------------------------------------------------------------------------
void meshOnReceived(const String &raw, const GlyphMessage &msg, float snr, float rssi) {
    (void)rssi;
    if (!msg.valid) return;

    const uint16_t key = meshSenderKey(msg.sender.c_str());

    // Am mai vazut mesajul asta? Atunci cineva l-a retransmis deja - si daca
    // asteptam noi sa-l retransmitem, renuntam. Asta e mecanismul care face ca
    // dintre cinci vecini sa vorbeasca in general doar unul.
    if (seen.seen(key, msg.counter) && msg.counter != 0) {
        if (relayPending && relayKey == key && relayCounter == msg.counter) {
            relayPending = false;
            relayPacket = "";
        }
        return;
    }
    seen.remember(key, msg.counter, millis());

    // Confirmarile si masuratorile de raza nu se retransmit niciodata: ar dubla
    // traficul fara sa ajute pe nimeni.
    if (msg.type == MSG_ACK || msg.type == MSG_PING || msg.type == MSG_PONG) return;

    const bool mine = (msg.sender == myName);

    // Confirmam doar mesajele de la echipa noastra - adica cele care au trecut
    // prin decriptare reusita cu cheia echipei. Un strain nu primeste raspuns,
    // deci nu ne poate face sa emitem la comanda.
    if (msg.authentic && !mine && msg.type == MSG_TEXT && msg.counter != 0) {
        ackTo = msg.sender;
        ackCounter = msg.counter;
        ackDueAt = millis() + ACK_DELAY_MIN_MS + (esp_random() % ACK_DELAY_SPREAD_MS);
        ackPending = true;
    }

    if (!msg.meshCapable) return;    // aparat cu firmware vechi: nimic de retransmis

    const bool isSos = (msg.type == MSG_SOS);
    if (!meshShouldRelay(msg.hopsLeft, msg.authentic, isSos, mine)) return;
    if (currentPowerMode == STEALTH_MODE) return;   // modul silentios nu emite deloc

    duty.tick(millis());
    if (!duty.canRelay(airtimeFor(raw.length()))) return;

    String out = raw;
    if (!decrementHops(out)) return;

    relayPacket  = out;
    relayKey     = key;
    relayCounter = msg.counter;
    relayDueAt   = millis() + relayDelayMs(snr, esp_random());
    relayPending = true;
}

// ---------------------------------------------------------------------------
// Emisia amanata, servita din loop()
// ---------------------------------------------------------------------------
static void transmitRaw(const String &pkt) {
    radio.standby();
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
    int st = radio.transmit(pkt);
    if (st == RADIOLIB_ERR_NONE) meshNoteTransmit(pkt.length());
    radio.startReceive();
    loraListening = true;
}

void serviceMesh() {
    if (!health.radioOk) return;
    duty.tick(millis());

    // Un SOS in curs are prioritate absoluta pe radio; nu ne bagam peste el.
    if (sosActive()) return;

    if (ackPending && (int32_t)(millis() - ackDueAt) >= 0) {
        ackPending = false;
        sendAck(ackTo, ackCounter);
        return;                       // o singura emisie per trecere prin loop
    }

    if (pongPending && (int32_t)(millis() - pongDueAt) >= 0) {
        pongPending = false;
        sendPongNow();
        return;
    }

    if (relayPending && (int32_t)(millis() - relayDueAt) >= 0) {
        relayPending = false;
        String pkt = relayPacket;
        relayPacket = "";
        if (pkt.length()) transmitRaw(pkt);
    }
}

// ---------------------------------------------------------------------------
// Confirmarea de primire
//
// "Trimis" si "auzit" sunt lucruri diferite, si pana acum aplicatia le
// confunda. Confirmarea cara contorul mesajului confirmat, ca telefonul sa
// stie exact care mesaj a ajuns si la cine.
// ---------------------------------------------------------------------------
void sendAck(const String &toWhom, uint32_t counter) {
    if (currentPowerMode == STEALTH_MODE) return;

    char buf[24];
    snprintf(buf, sizeof(buf), "%08lX", (unsigned long)counter);
    String payload = myName + ": " + String(buf) + ">" + toWhom;

    // Confirmarile nu se retransmit: zero salturi.
    String pkt = buildPacket(MSG_ACK, payload, true, 0);
    if (pkt.length() == 0) return;
    if (!meshCanSend(pkt.length())) return;
    transmitRaw(pkt);
}

// ---------------------------------------------------------------------------
// Masurarea razei
//
// Un ping care cere raspuns. Cine il aude raspunde cu RSSI si SNR-ul cu care
// l-a auzit, plus pozitia lui. Aparatul care a cerut scrie totul pe card:
// dupa o plimbare ai date reale despre antena ta, in loc de "merge cam bine".
// ---------------------------------------------------------------------------
void sendRangePing() {
    if (currentPowerMode == STEALTH_MODE) { notifyPhone("[SYS] Stealth mode: radio silent"); return; }

    double lat, lon;
    getGpsPosition(lat, lon);
    String payload = myName + ": PING|" + String(lat, 5) + "," + String(lon, 5);

    String pkt = buildPacket(MSG_PING, payload, true, 0);
    if (pkt.length() == 0) { notifyPhone("[SYS] Encryption failed"); return; }
    if (!meshCanSend(pkt.length())) {
        notifyPhone("[SYS] Airtime budget spent, wait a few minutes");
        return;
    }
    transmitRaw(pkt);
    notifyPhone("[SYS] Range ping sent");
}

// Raspunsul la un ping primit, cu calitatea semnalului cu care l-am auzit.
void replyToPing(const GlyphMessage &msg, float snr, float rssi) {
    (void)msg;
    if (currentPowerMode == STEALTH_MODE) return;
    pongSnr = snr;
    pongRssi = rssi;
    // Aceeasi imprastiere ca la confirmari: daca raspund trei aparate deodata,
    // nu se aude niciunul.
    pongDueAt = millis() + ACK_DELAY_MIN_MS + (esp_random() % ACK_DELAY_SPREAD_MS);
    pongPending = true;
}

static void sendPongNow() {
    double lat, lon;
    getGpsPosition(lat, lon);
    String payload = myName + ": PONG " + String(pongRssi, 0) + "dBm "
                   + String(pongSnr, 1) + "dB|" + String(lat, 5) + "," + String(lon, 5);

    String pkt = buildPacket(MSG_PONG, payload, true, 0);
    if (pkt.length() == 0) return;
    if (!meshCanSend(pkt.length())) return;
    transmitRaw(pkt);
}

// Un rand in fisierul de masuratori de pe card.
void logRangeSample(const String &peer, float rssi, float snr,
                    double peerLat, double peerLon) {
    if (!sdDetected) return;
    acquireSD();
    File f = SD.open("/range_" + formatLocalDateFile() + ".csv", FILE_APPEND);
    if (!f) return;

    if (f.size() == 0) f.println("time,peer,rssi_dbm,snr_db,my_lat,my_lon,peer_lat,peer_lon,distance_m");

    double myLat, myLon;
    getGpsPosition(myLat, myLon);
    double distM = (peerLat != 0.0 || peerLon != 0.0)
                 ? calculateDistance(myLat, myLon, peerLat, peerLon) * 1000.0 : -1.0;

    f.println(formatLocalTimeSec() + "," + peer + "," + String(rssi, 0) + "," + String(snr, 1)
              + "," + String(myLat, 6) + "," + String(myLon, 6)
              + "," + String(peerLat, 6) + "," + String(peerLon, 6)
              + "," + String(distM, 0));
    f.flush();
    f.close();
}
