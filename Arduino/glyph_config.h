#pragma once

// --- Power and sleep ---

static const unsigned long STANDBY_AFTER_MS = 240000;   // 4 min

// How long after the last IMU-detected motion the device counts as in use.
static const unsigned long MOVEMENT_KEEPALIVE_MS = 30000;

static const unsigned long STANDBY_WAKE_MOVEMENT_MS = 2000;

static const uint32_t DEEP_SLEEP_INTERVAL_MS = 8UL * 60UL * 1000UL;

static const unsigned long WAKE_CHECK_WINDOW_MS = 4000;

static const uint32_t CPU_MHZ_NORMAL  = 80;
static const uint32_t CPU_MHZ_SAVING  = 40;
static const uint32_t CPU_MHZ_STANDBY = 20;

// --- Battery ---
// An 18650 counts as empty around 3.0-3.2 V, but voltage sags hard under load.
// Cutoff at 3.40 V leaves reserve to close files cleanly: an interrupted SD
// write can leave the track truncated or the FAT corrupt.
static const float BATTERY_WARN_V     = 3.55f;   // on-screen warning
static const float BATTERY_CUTOFF_V   = 3.40f;   // controlled shutdown

// Voltage must hold below the threshold this long, so a transmit current spike
// does not shut the device down for nothing.
static const unsigned long BATTERY_CUTOFF_HOLD_MS = 30000;

// ADC samples averaged. The ESP32 ADC is noisy; one reading can be off 100 mV.
static const int BATTERY_ADC_SAMPLES = 16;

// --- charge detection -------------------------------------------------------
// The board has no charge-status line, so charging is inferred from the
// voltage itself. A pack that is discharging never rises; a charger holds the
// terminal above the cell's true resting voltage, which is why a 40% pack
// reads like 80% while plugged in.
//
// A step up this large between two samples means a cable was just connected.
static const float BATTERY_CHARGE_STEP_V   = 0.08f;

// End-of-charge terminal voltage, used to work out how far a charge has got.
static const float BATTERY_FULL_V          = 4.20f;

// A slow rise of at least this much over the window below also means charging,
// for the case where the cable was already in at power-on.
static const float BATTERY_CHARGE_TREND_V  = 0.03f;
static const unsigned long BATTERY_TREND_MS = 120000;

// A step down this large means the cable came out; readings are trustworthy
// again after the pack settles.
static const float BATTERY_UNPLUG_STEP_V   = 0.06f;

// After unplugging, the cell relaxes downward for a while. Percentages during
// this window are still inflated, so they are reported as recovering.
static const unsigned long BATTERY_SETTLE_MS = 90000;

// Board resistor divider. Vbat = Vadc * BATTERY_DIVIDER.
static const float BATTERY_DIVIDER = 2.0f;

// --- GPS and track logging ---

static const float GPS_MAX_ACCURACY_M = 100.0f;

// Minimum step to log a new point while the IMU confirms movement.
static const double GPS_MOVING_MIN_STEP_M = 2.0;

// Same while still: much larger, so GPS drift is not logged as a walk.
static const double GPS_STATIC_MIN_STEP_M = 25.0;

static const unsigned long GPS_IMU_MOVING_WINDOW_MS = 5000;

static const double UI_POSITION_REDRAW_DEG = 0.0001;

static const uint16_t GPS_RATE_NORMAL_MS  = 1000;
static const uint16_t GPS_RATE_SAVING_MS  = 10000;
static const uint16_t GPS_RATE_STANDBY_MS = 60000;

// --- Radio ---
static const int   LORA_SPREADING_FACTOR = 11;
static const float LORA_BANDWIDTH_KHZ    = 125.0f;
static const int   LORA_CODING_RATE      = 8;
static const uint8_t LORA_SYNC_WORD      = 0x12;
static const int   LORA_OUTPUT_POWER_DBM = 22;

// --- Mesh network ---
// Relays a message may pass through. Three covers a realistic team without
// becoming a retransmission storm.
static const uint8_t MESH_HOPS_DEFAULT = 3;

// SOS gets one extra hop: the only message worth paying extra airtime for.
static const uint8_t MESH_HOPS_SOS     = 4;

// Ack jitter. If three units hear the same message and answer in the same
// millisecond, none is heard.
static const unsigned long ACK_DELAY_MIN_MS    = 250;
static const unsigned long ACK_DELAY_SPREAD_MS = 900;

// --- Legal transmit budget ---
// In Europe on 868 MHz most sub-bands allow a 1% duty cycle: 36 seconds of
// airtime per hour. 30 is kept as margin. At SF11 one message occupies nearly a
// second of air, so the limit is reached far sooner than it looks.
static const uint32_t DUTY_CYCLE_BUDGET_MS = 30000;

// --- SOS ---
// Initial burst. After it the SOS repeats at growing intervals until cancelled:
// a 10-second SOS that hits a moment when nobody listens is lost.
static const unsigned long SOS_BROADCAST_MS   = 10000;

// Retry intervals in seconds; the last value repeats forever.
static const uint16_t SOS_REPEAT_SECONDS[] = { 60, 60, 120, 120, 300, 300, 600, 900 };
static const int SOS_REPEAT_STEPS = sizeof(SOS_REPEAT_SECONDS) / sizeof(SOS_REPEAT_SECONDS[0]);
static const unsigned long SOS_PACKET_GAP_MS  = 100;

// --- UI and storage ---

// E-ink accumulates ghosting on partial refresh; force a full one this often.
static const unsigned long FULL_REFRESH_INTERVAL_MS = 900000;   // 15 min

static const unsigned long TELEMETRY_LOG_INTERVAL_MS = 60000;

static const unsigned long SD_HOTPLUG_CHECK_MS = 5000;

static const unsigned long BLE_TELEMETRY_INTERVAL_MS = 5000;

// Gap between consecutive BLE notifications, to stay inside the stack queue.
static const unsigned long BLE_NOTIFY_GAP_MS = 15;

// --- SD card access from the phone ---
// Chunk of file per notification. Base64 grows it by a third (160 -> 216 bytes);
// with the prefix and the 3-byte ATT header it fits the requested MTU of 247.
static const int SD_XFER_CHUNK_BYTES     = 160;

// Chunks per loop() pass. More is faster but buttons lag; 4 keeps presses
// under ~80 ms.
static const int SD_XFER_CHUNKS_PER_LOOP = 4;

static const int SD_LIST_MAX_FILES       = 60;

// MTU requested at BLE init. Without it only 20 bytes per notification are
// usable and a 40 KB track takes minutes instead of seconds.
static const int BLE_REQUESTED_MTU       = 247;

// --- Route upload from the phone ---
// Chunk size BEFORE base64. Encoded plus the "CMD_OTAD:" prefix it reaches 233,
// which fits both the receive buffer (BLE_RX_BUF_SIZE = 256) and a SINGLE BLE
// write at MTU 247, avoiding the long-write mechanism, unreliable on some
// phones. Raising this without raising the buffer truncates chunks silently.
static const int SD_UPLOAD_CHUNK_BYTES = 168;

static const uint32_t SD_UPLOAD_MAX_BYTES = 262144;

// Track points kept in RTC memory, so they survive sleep.
static const int MAX_BREADCRUMBS = 350;

// KML overlay points kept in RAM. Past this the track is resampled evenly.
static const int MAX_KML_CACHE_POINTS = 600;

static const int MAX_TEAMMATES = 5;

static const int MAX_LORA_MSGS = 5;

static const int MAX_KML_FILES = 12;

static const unsigned int MAX_MESSAGE_LEN = 30;
static const unsigned int MAX_NAME_LEN    = 16;
static const unsigned int MAX_TEAM_LEN    = 16;

// --- Buttons ---
static const unsigned long BTN_COMBO_HOLD_MS   = 1000;   // A+B, B+C, A+C
static const unsigned long BTN_LONG_PRESS_MS   = 800;    // long press A / C
static const unsigned long BTN_DELETE_HOLD_MS  = 600;    // B held = delete
static const unsigned long BTN_DELETE_REPEAT_MS = 150;   // delete repeat rate
static const unsigned long BTN_KML_EXIT_HOLD_MS = 5000;  // A held in the KML list
static const TickType_t    BTN_POLL_PERIOD_MS  = 15;
static const TickType_t    SENSOR_POLL_PERIOD_MS = 100;

// --- Legacy packet compatibility ---
// While 1, the device READS pre-fix AES-CBC messages so units can be updated one
// at a time. It never SENDS the old format. Set to 0 once every unit runs new
// firmware: CBC authenticates nothing, so while this stays open someone can
// inject a modified message.
#define GLYPH_ACCEPT_LEGACY_CBC 1
