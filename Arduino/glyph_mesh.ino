//file name:glyph_mesh.ino
// Mesh relaying, acknowledgements and range measurement.
//
// No routes and no neighbour tables. A device that hears a message not its own,
// with hops left, waits and rebroadcasts it - waiting LESS the FAINTER it heard
// it. See relayDelayMs in glyph_mesh_core.h for why that picks short paths.

#include "glyph_mesh_core.h"

static void transmitRaw(String pkt);
static void sendPongNow();

static DutyBudget duty;
static MeshSeen   seen;

// One pending relay only; a device in a spot that busy is not the one that should relay.
static bool     relayPending  = false;
static uint32_t relayDueAt    = 0;
static String   relayPacket   = "";
static uint16_t relayKey      = 0;
static uint32_t relayCounter  = 0;

// The ack is delayed and spread out, or simultaneous repliers drown each other.
static bool     ackPending    = false;
static uint32_t ackDueAt      = 0;
static String   ackTo         = "";
static uint32_t ackCounter    = 0;

// The pong is deferred the same way. It must not use delay(): blocking the loop
// for up to a second is what made the buttons dead while the GPS was running.
static bool     pongPending   = false;
static uint32_t pongDueAt     = 0;
static float    pongSnr       = 0.0f;
static float    pongRssi      = 0.0f;

void meshBegin() {
    duty.begin(DUTY_CYCLE_BUDGET_MS, millis());
    seen.begin();
}

int meshDutyPercent() { duty.tick(millis()); return duty.percentUsed(); }

// Routed through glyphAirtimeMs so the function the tests cover is the one that runs.
static uint32_t airtimeFor(int payloadLen) { return glyphAirtimeMs(payloadLen); }

bool meshCanSend(int payloadLen) {
    duty.tick(millis());
    return duty.canSend(airtimeFor(payloadLen));
}

void meshNoteTransmit(int payloadLen) {
    duty.tick(millis());
    duty.add(airtimeFor(payloadLen));
}

void meshOnReceived(const String &raw, const GlyphMessage &msg, float snr, float rssi) {
    (void)rssi;
    if (!msg.valid) return;

    const uint16_t key = meshSenderKey(msg.sender.c_str());

    // Already seen means someone else relayed it first, so drop our own pending
    // relay. This is why usually only one of several neighbours speaks.
    if (seen.seen(key, msg.counter) && msg.counter != 0) {
        if (relayPending && relayKey == key && relayCounter == msg.counter) {
            relayPending = false;
            relayPacket = "";
        }
        return;
    }
    seen.remember(key, msg.counter, millis());

    // Acks, pings and pongs are never relayed.
    if (msg.type == MSG_ACK || msg.type == MSG_PING || msg.type == MSG_PONG) return;

    const bool mine = (msg.sender == myName);

    // Ack only messages that decrypted with the team key. A stranger gets no
    // reply, so no one can make this device transmit on command.
    if (msg.authentic && !mine && msg.type == MSG_TEXT && msg.counter != 0) {
        ackTo = msg.sender;
        ackCounter = msg.counter;
        ackDueAt = millis() + ACK_DELAY_MIN_MS + (esp_random() % ACK_DELAY_SPREAD_MS);
        ackPending = true;
    }

    if (!msg.meshCapable) return;

    const bool isSos = (msg.type == MSG_SOS);
    if (!meshShouldRelay(msg.hopsLeft, msg.authentic, isSos, mine)) return;
    if (currentPowerMode == STEALTH_MODE) return;

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

// pkt is taken BY VALUE: RadioLib declares transmit(String&), a non-const
// reference, so a const String will not bind to it.
static void transmitRaw(String pkt) {
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

    // An active SOS owns the radio.
    if (sosActive()) return;

    if (ackPending && (int32_t)(millis() - ackDueAt) >= 0) {
        ackPending = false;
        sendAck(ackTo, ackCounter);
        return;                       // one transmission per pass through loop()
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

// Sent and heard are different things. The ack carries the counter of the
// message it confirms, so the phone knows which message arrived and from whom.
void sendAck(const String &toWhom, uint32_t counter) {
    if (currentPowerMode == STEALTH_MODE) return;

    char buf[24];
    snprintf(buf, sizeof(buf), "%08lX", (unsigned long)counter);
    String payload = myName + ": " + String(buf) + ">" + toWhom;

    // Acks are never relayed: zero hops.
    String pkt = buildPacket(MSG_ACK, payload, true, 0);
    if (pkt.length() == 0) return;
    if (!meshCanSend(pkt.length())) return;
    transmitRaw(pkt);
}

// Range ping: whoever hears it replies with the RSSI and SNR it measured plus
// its own position, and the requester logs the pair to the card.
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

void replyToPing(const GlyphMessage &msg, float snr, float rssi) {
    (void)msg;
    if (currentPowerMode == STEALTH_MODE) return;
    pongSnr = snr;
    pongRssi = rssi;
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
