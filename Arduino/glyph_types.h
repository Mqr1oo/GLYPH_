#pragma once
// Project types live in a header, not in a .ino: the Arduino builder inserts
// generated prototypes right after the sketch include block, so a function
// taking an enum defined later in the .ino would fail to compile.

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

// newSegment marks the start of a new line, so two separate tracks are not
// joined by a straight line across the map.
struct KmlPoint { float lat; float lon; bool newSegment; };

struct Teammate {
    String name;
    double lat;
    double lon;
    unsigned long lastSeen;
    uint32_t lastCounter;   // last accepted counter, for replay rejection
};

// newSegment is true on the first point of each new <coordinates> block.
typedef std::function<void(float lat, float lon, bool newSegment)> KmlPointFn;

// --- Radio protocol ---
// On the wire: [0] format marker, [1] message type (v2 and later), then plain
// text (public) or hex(nonce|ciphertext|tag) (secure). The encrypted part
// starts with a 32-bit counter: GCM proves a message was not modified, not
// that it is new, so without it a recorded SOS could be replayed later and
// would still verify as authentic.

// v3 adds one byte: remaining hops. It sits BEFORE the encrypted part because
// every relay must decrement it; under the GCM tag any edit would invalidate
// the message. Only the hop count is in the clear.
static const uint8_t PKT_V3_SECURE = 0xFB;   // header + hops + AES-GCM + counter
static const uint8_t PKT_V3_PUBLIC = 0xFA;   // header + hops, unencrypted

static const uint8_t PKT_V2_SECURE = 0xFD;   // header + AES-GCM + counter
static const uint8_t PKT_V2_PUBLIC = 0xFC;   // header, unencrypted
static const uint8_t PKT_V1_SECURE = 0xFE;   // AES-GCM, no header (legacy)
static const uint8_t PKT_V0_SECURE = 0xFF;   // old AES-CBC (legacy)

enum MsgType : uint8_t {
    MSG_TEXT = 1,
    MSG_SOS  = 2,
    // Delivery confirmation, body "name: <counter in hex>". Never relayed.
    MSG_ACK  = 3,
    // Range ping; the reply carries the RSSI and SNR it was heard at.
    MSG_PING = 4,
    MSG_PONG = 5
};

struct GlyphMessage {
    bool     valid       = false;
    bool     authentic   = false;   // passed GCM decryption
    bool     replay      = false;
    uint8_t  type        = MSG_TEXT;
    uint32_t counter     = 0;
    String   sender;                // part before the ':'
    String   body;                  // "name: text", as displayed
    bool     hasCoords   = false;
    double   lat         = 0.0;
    double   lon         = 0.0;
    uint8_t  hopsLeft    = 0;
    bool     meshCapable = false;   // packet carries the v3 header
};

// Peripheral status. A radio that fails to init leaves the device looking
// normal while transmitting nothing, so the state is tracked and shown.
struct DeviceHealth {
    bool radioOk = false;
    bool gpsOk   = false;
    bool sdOk    = false;
    bool imuOk   = false;
    bool shtOk   = false;
    bool buttonsOk = false;
    int  radioError = 0;    // RadioLib return code, if init failed

    // What must work for the device to do its job.
    bool critical() const { return !radioOk || !buttonsOk; }
};
