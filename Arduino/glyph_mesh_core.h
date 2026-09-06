#pragma once
//file name:glyph_mesh_core.h
// Mesh core with no hardware dependencies: airtime, duty budget, seen-packet
// memory, relay delay. Separate header so the PC tests can compile it.

#include <stdint.h>
#include <string.h>

#include "glyph_config.h"

// Semtech time-on-air formula. Airtime is tracked because of the EU 868 MHz 1%
// duty cycle, and because transmitting costs ~120 mA: at SF11 one message
// occupies the air for about a second.
inline uint32_t loraTimeOnAirMs(int payloadBytes, int sf, int bwKhz, int cr,
                                int preamble = 8, bool explicitHeader = true,
                                bool crcOn = true) {
    if (sf < 6 || sf > 12 || bwKhz <= 0 || payloadBytes < 0) return 0;

    const double tSymUs = ((double)(1UL << sf) * 1000.0) / (double)bwKhz;

    // SF11 and SF12 at 125 kHz or less force low data rate optimization, which
    // changes the symbol count. Without this term the estimate is ~8% low at
    // exactly the settings in use.
    const int de = (sf >= 11 && bwKhz <= 125) ? 1 : 0;
    const int ih = explicitHeader ? 0 : 1;

    double num = 8.0 * payloadBytes - 4.0 * sf + 28.0 + (crcOn ? 16.0 : 0.0) - 20.0 * ih;
    double den = 4.0 * (sf - 2.0 * de);
    long nPayload = (long)(num / den);
    if (num > 0 && (double)nPayload * den < num) nPayload++;
    if (nPayload < 0) nPayload = 0;
    nPayload = nPayload * (cr + 4) + 8;

    const double tPreambleUs = (preamble + 4.25) * tSymUs;
    const double tPayloadUs  = nPayload * tSymUs;
    return (uint32_t)((tPreambleUs + tPayloadUs) / 1000.0 + 0.5);
}

// Airtime for this device's packets. It is a wrapper because of a trap:
// LORA_CODING_RATE is 8, meaning rate 4/8, but the Semtech formula wants the
// denominator minus 4. Passing 8 straight through overstates airtime by ~40%,
// the budget then looks spent, and the device stops relaying for no reason.
inline uint32_t glyphAirtimeMs(int payloadLen) {
    return loraTimeOnAirMs(payloadLen, LORA_SPREADING_FACTOR,
                           (int)LORA_BANDWIDTH_KHZ, LORA_CODING_RATE - 4);
}

// Sliding one-hour window. Human traffic (message, SOS) outranks relaying, so
// relaying is the first thing to stop when the budget runs low.
struct DutyBudget {
    static const int SLOTS = 12;          // 12 x 5 min = one hour
    uint32_t usedMs[SLOTS];
    uint32_t slotStartMs;
    int      slot;
    uint32_t limitMs;

    void begin(uint32_t hourlyLimitMs, uint32_t nowMs) {
        memset(usedMs, 0, sizeof(usedMs));
        slotStartMs = nowMs;
        slot = 0;
        limitMs = hourlyLimitMs;
    }

    void tick(uint32_t nowMs) {
        const uint32_t SLOT_MS = 5UL * 60UL * 1000UL;
        while ((uint32_t)(nowMs - slotStartMs) >= SLOT_MS) {
            slotStartMs += SLOT_MS;
            slot = (slot + 1) % SLOTS;
            usedMs[slot] = 0;
        }
    }

    uint32_t usedInWindow() const {
        uint32_t t = 0;
        for (int i = 0; i < SLOTS; i++) t += usedMs[i];
        return t;
    }

    void add(uint32_t airMs) { usedMs[slot] += airMs; }

    bool canSend(uint32_t airMs) const { return usedInWindow() + airMs <= limitMs; }

    // Relaying stops at 70% of the budget, leaving room for own messages and SOS.
    bool canRelay(uint32_t airMs) const {
        return usedInWindow() + airMs <= (limitMs * 7) / 10;
    }

    int percentUsed() const {
        if (limitMs == 0) return 0;
        uint32_t u = usedInWindow();
        return (int)((u * 100) / limitMs);
    }
};

// Ring of (sender, counter) pairs. Without it, two devices that hear each other
// pass the same message back and forth forever. The counter is already in the
// packet for anti-replay, so this costs no extra bytes on air.
inline uint16_t meshSenderKey(const char *name) {
    // FNV-1a, 16 bit. A collision costs one un-relayed message, never a forged one.
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

// A device that heard the packet FAINTLY is far from the sender, so its
// rebroadcast covers the most new ground: it waits the SHORTEST time and speaks
// first. One that heard it loudly is close by and adds nothing, so it waits,
// hears the distant relay, and drops its own. This is what makes the mesh pick
// short paths with no routing table and no neighbour list.
inline uint32_t relayDelayMs(float snrDb, uint32_t randomness,
                             uint32_t minMs = 120, uint32_t maxMs = 2600) {
    // Useful LoRa SNR range: -20 dB (barely heard) to +10 dB (next to you).
    float s = snrDb;
    if (s < -20.0f) s = -20.0f;
    if (s > 10.0f)  s = 10.0f;

    const float t = (s + 20.0f) / 30.0f;

    const uint32_t span = maxMs - minMs;
    uint32_t base = minMs + (uint32_t)(t * (float)span);

    // Jitter, or two devices with near-equal SNR start in the same millisecond.
    const uint32_t jitter = randomness % 180;
    uint32_t d = base + jitter;
    if (d > maxMs + 180) d = maxMs + 180;
    return d;
}

inline bool meshShouldRelay(uint8_t hopsLeft, bool isForMyTeam, bool isSos,
                            bool fromMyself) {
    if (fromMyself) return false;
    if (hopsLeft == 0) return false;
    return isSos || isForMyTeam;         // SOS is relayed regardless of team
}
