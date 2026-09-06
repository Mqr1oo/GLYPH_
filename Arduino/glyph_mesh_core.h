#pragma once
//file name:glyph_mesh_core.h
//
// Miezul retelei mesh, fara nimic care atinge hardware-ul: timp de emisie,
// bugetul legal de emisie, memoria de pachete deja vazute si intarzierea
// dinaintea retransmiterii. Sta intr-un header separat exact ca parserul KML,
// ca sa poata fi compilat si testat pe PC, unde greselile se vad in secunde in
// loc de ore de umblat pe deal cu doua aparate.

#include <stdint.h>
#include <string.h>

#include "glyph_config.h"

// ---------------------------------------------------------------------------
// TIMPUL DE EMISIE
//
// Formula oficiala Semtech pentru LoRa. Conteaza din doua motive: in Europa,
// pe 868 MHz, ai voie sa emiti cel mult 1% din timp; si fiecare milisecunda de
// emisie inseamna ~120 mA din baterie. Un mesaj la SF11 sta in aer aproape o
// secunda - de zece ori cat crede lumea.
// ---------------------------------------------------------------------------
inline uint32_t loraTimeOnAirMs(int payloadBytes, int sf, int bwKhz, int cr,
                                int preamble = 8, bool explicitHeader = true,
                                bool crcOn = true) {
    if (sf < 6 || sf > 12 || bwKhz <= 0 || payloadBytes < 0) return 0;

    // Durata unui simbol, in microsecunde: 2^SF / BW.
    const double tSymUs = ((double)(1UL << sf) * 1000.0) / (double)bwKhz;

    // La SF11 si SF12 pe 125 kHz, radioul foloseste obligatoriu "low data rate
    // optimization", care schimba numarul de simboluri. Fara termenul asta,
    // estimarea iese cu ~8% mai mica exact la setarile pe care le folosim.
    const int de = (sf >= 11 && bwKhz <= 125) ? 1 : 0;
    const int ih = explicitHeader ? 0 : 1;

    double num = 8.0 * payloadBytes - 4.0 * sf + 28.0 + (crcOn ? 16.0 : 0.0) - 20.0 * ih;
    double den = 4.0 * (sf - 2.0 * de);
    long nPayload = (long)(num / den);
    // Rotunjire in sus, si niciodata sub zero simboluri de date.
    if (num > 0 && (double)nPayload * den < num) nPayload++;
    if (nPayload < 0) nPayload = 0;
    nPayload = nPayload * (cr + 4) + 8;

    const double tPreambleUs = (preamble + 4.25) * tSymUs;
    const double tPayloadUs  = nPayload * tSymUs;
    return (uint32_t)((tPreambleUs + tPayloadUs) / 1000.0 + 0.5);
}

// Timpul de emisie pentru pachetele ACESTUI aparat, cu setarile din
// glyph_config.h. Exista ca functie separata, si nu ca apel direct in
// firmware, dintr-un motiv foarte concret: LORA_CODING_RATE e 8, adica rata
// 4/8, dar formula Semtech vrea doar numitorul minus 4. Trecerea directa a lui
// 8 dadea un timp cu ~40% prea mare, aparatul isi credea bugetul epuizat si
// inceta sa mai retransmita degeaba. Greseala a trecut neprinsa de teste exact
// pentru ca statea in codul de firmware, care nu se compileaza pe PC. Aici, se
// vede.
inline uint32_t glyphAirtimeMs(int payloadLen) {
    return loraTimeOnAirMs(payloadLen, LORA_SPREADING_FACTOR,
                           (int)LORA_BANDWIDTH_KHZ, LORA_CODING_RATE - 4);
}

// ---------------------------------------------------------------------------
// BUGETUL DE EMISIE
//
// Fereastra glisanta de o ora. Nu e o formalitate: fara ea, un SOS care se
// repeta ar depasi limita legala in cateva minute si ar tine radioul cald
// exact cand ai nevoie de baterie.
//
// Emisia ceruta de om (mesaj, SOS) are prioritate fata de retransmisii: cand
// bugetul se apropie de capat, aparatul inceteaza sa mai fie releu pentru
// altii inainte sa refuze sa vorbeasca pentru tine.
// ---------------------------------------------------------------------------
struct DutyBudget {
    static const int SLOTS = 12;          // 12 x 5 minute = o ora
    uint32_t usedMs[SLOTS];
    uint32_t slotStartMs;
    int      slot;
    uint32_t limitMs;                     // cat ai voie intr-o ora

    void begin(uint32_t hourlyLimitMs, uint32_t nowMs) {
        memset(usedMs, 0, sizeof(usedMs));
        slotStartMs = nowMs;
        slot = 0;
        limitMs = hourlyLimitMs;
    }

    // Se cheama des; muta fereastra si sterge sferturile expirate.
    void tick(uint32_t nowMs) {
        const uint32_t SLOT_MS = 5UL * 60UL * 1000UL;
        while ((uint32_t)(nowMs - slotStartMs) >= SLOT_MS) {
            slotStartMs += SLOT_MS;
            slot = (slot + 1) % SLOTS;
            usedMs[slot] = 0;             // sfertul care intra in fereastra e gol
        }
    }

    uint32_t usedInWindow() const {
        uint32_t t = 0;
        for (int i = 0; i < SLOTS; i++) t += usedMs[i];
        return t;
    }

    void add(uint32_t airMs) { usedMs[slot] += airMs; }

    bool canSend(uint32_t airMs) const { return usedInWindow() + airMs <= limitMs; }

    // Retransmisiile se opresc la 70% din buget, ca sa ramana loc pentru
    // mesajele tale si pentru SOS.
    bool canRelay(uint32_t airMs) const {
        return usedInWindow() + airMs <= (limitMs * 7) / 10;
    }

    int percentUsed() const {
        if (limitMs == 0) return 0;
        uint32_t u = usedInWindow();
        return (int)((u * 100) / limitMs);
    }
};

// ---------------------------------------------------------------------------
// PACHETE DEJA VAZUTE
//
// Un inel mic de perechi (expeditor, contor). Fara el, doua aparate care se
// aud reciproc si-ar pasa acelasi mesaj la infinit si ar bloca banda intr-un
// minut. Cheia e (expeditor, contor) pentru ca exista deja - e chiar contorul
// anti-reluare - deci nu adaugam niciun octet in pachet pentru asta.
// ---------------------------------------------------------------------------
inline uint16_t meshSenderKey(const char *name) {
    // FNV-1a pe 16 biti. Coliziunile intre doua nume diferite ar face ca un
    // mesaj sa fie ignorat gresit; cu cateva zeci de aparate riscul e sub 1%,
    // si consecinta e "un mesaj nu e retransmis", nu "un mesaj e falsificat".
    uint32_t h = 2166136261u;
    for (const char *p = name; p && *p; p++) {
        h ^= (uint8_t)(*p);
        h *= 16777619u;
    }
    return (uint16_t)((h >> 16) ^ h);
}

struct MeshSeen {
    static const int N = 24;
    uint16_t key[N];
    uint32_t ctr[N];
    uint32_t at[N];
    int      next;

    void begin() { memset(key, 0, sizeof(key)); memset(ctr, 0, sizeof(ctr));
                   memset(at, 0, sizeof(at)); next = 0; }

    bool seen(uint16_t k, uint32_t c) const {
        for (int i = 0; i < N; i++) if (key[i] == k && ctr[i] == c) return true;
        return false;
    }

    void remember(uint16_t k, uint32_t c, uint32_t nowMs) {
        if (seen(k, c)) return;
        key[next] = k; ctr[next] = c; at[next] = nowMs;
        next = (next + 1) % N;
    }
};

// ---------------------------------------------------------------------------
// CAT ASTEPTI INAINTE SA RETRANSMITI
//
// Aici sta tot ce face reteaua sa aleaga singura drumul scurt, fara nicio
// setare in aplicatie.
//
// Cine a auzit mesajul SLAB e departe de emitator - deci retransmisia lui
// acopera cel mai mult teren nou. Ala asteapta PUTIN si vorbeste primul. Cine
// l-a auzit tare e aproape de emitator si n-ar aduce nimic nou; ala asteapta
// MULT, aude retransmisia celui indepartat, si renunta la a lui.
//
// Rezultatul: din toti vecinii, retransmite in general doar cel mai util, iar
// mesajul inainteaza in salturi lungi. Nimeni nu calculeaza vreo ruta si nu
// exista tabele de vecini - comportamentul iese din fizica si din ceas.
// ---------------------------------------------------------------------------
inline uint32_t relayDelayMs(float snrDb, uint32_t randomness,
                             uint32_t minMs = 120, uint32_t maxMs = 2600) {
    // Domeniul util al SNR-ului LoRa: de la -20 dB (abia auzit) la +10 dB (langa tine).
    float s = snrDb;
    if (s < -20.0f) s = -20.0f;
    if (s > 10.0f)  s = 10.0f;

    // 0 pentru cel mai slab semnal, 1 pentru cel mai tare.
    const float t = (s + 20.0f) / 30.0f;

    const uint32_t span = maxMs - minMs;
    uint32_t base = minMs + (uint32_t)(t * (float)span);

    // Un pic de aleatoriu, altfel doua aparate cu SNR aproape egal ar porni
    // exact in aceeasi milisecunda si s-ar bruia reciproc.
    const uint32_t jitter = randomness % 180;
    uint32_t d = base + jitter;
    if (d > maxMs + 180) d = maxMs + 180;
    return d;
}

// Un mesaj mai are voie sa fie retransmis?
inline bool meshShouldRelay(uint8_t hopsLeft, bool isForMyTeam, bool isSos,
                            bool fromMyself) {
    if (fromMyself) return false;        // ecoul propriu nu se retransmite
    if (hopsLeft == 0) return false;     // si-a trait salturile
    return isSos || isForMyTeam;         // SOS trece intotdeauna
}
