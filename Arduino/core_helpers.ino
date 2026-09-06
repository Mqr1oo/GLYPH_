//file name:core_helpers.ino
//
// Blocks that were duplicated across the sketch, kept here so each has a single
// source of truth.

bool inSetupPage() {
    return currentState == PAGE_INIT_LANG ||
           currentState == PAGE_INIT_FREQ ||
           currentState == PAGE_INIT_NAME ||
           currentState == PAGE_INIT_TIME;
}

// Same parameters everywhere: two devices configured by different paths must
// end up with an identical radio configuration.
void applyRadioProfile() {
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
    radio.setBandwidth(LORA_BANDWIDTH_KHZ);
    radio.setCodingRate(LORA_CODING_RATE);
    radio.setSyncWord(LORA_SYNC_WORD);
    radio.setOutputPower(LORA_OUTPUT_POWER_DBM);
}

// Returns the RadioLib code: a failed radio.begin() is otherwise invisible, the
// device looks normal and transmits nothing.
int beginRadio(float freq) {
    int state = radio.begin(freq);
    health.radioOk = (state == RADIOLIB_ERR_NONE);
    health.radioError = state;
    if (health.radioOk) applyRadioProfile();
    return state;
}

// The order of operations matches the menu; changing it changes behaviour.
void applyPowerMode(PowerMode mode) {
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    if (mode == NORMAL_MODE) {
        setCpuFrequencyMhz(CPU_MHZ_NORMAL);
        if (gps_ok) { myGNSS.powerSaveMode(false); myGNSS.setMeasurementRate(GPS_RATE_NORMAL_MS); }
    } else if (mode == ECO_MODE) {
        if (gps_ok) { myGNSS.powerSaveMode(true); myGNSS.setMeasurementRate(GPS_RATE_SAVING_MS); }
        setCpuFrequencyMhz(CPU_MHZ_SAVING);
    } else if (mode == STEALTH_MODE) {
        if (gps_ok) { myGNSS.powerSaveMode(true); myGNSS.setMeasurementRate(GPS_RATE_SAVING_MS); }
        radio.standby();
        loraListening = false;
        setCpuFrequencyMhz(CPU_MHZ_SAVING);
    }

    xSemaphoreGive(i2cMutex);
}

void pushLoraHistory(String screenMsg) {
    for (int i = MAX_LORA_MSGS - 1; i > 0; i--) {
        loraHistory[i] = loraHistory[i - 1];
    }
    loraHistory[0] = screenMsg;
    if (loraMsgCount < MAX_LORA_MSGS) loraMsgCount++;
}

// The team key is derived with PBKDF2 and cached, so every change of the name
// must invalidate that cache. Single entry point so the step cannot be missed.
void setTeamName(String name) {
    if (name.length() > MAX_TEAM_LEN) name = name.substring(0, MAX_TEAM_LEN);
    if (name.length() == 0) name = "ALPHA";
    myTeam = name;
    prefs.putString("team", myTeam);
    invalidateTeamKey();
}

// lastLat/lastLon are doubles and writes to them are not atomic on the ESP32.
// sensorTask (core 0) writes while loop() (core 1) reads, so an unguarded read
// can mix the new half of the latitude with the old half of the longitude and
// produce a bogus coordinate. Touch the three values only through these.
portMUX_TYPE gpsPosMux = portMUX_INITIALIZER_UNLOCKED;

void setGpsPosition(double lat, double lon, float alt) {
    portENTER_CRITICAL(&gpsPosMux);
    lastLat = lat;
    lastLon = lon;
    lastAlt = alt;
    portEXIT_CRITICAL(&gpsPosMux);
}

void getGpsPosition(double &lat, double &lon, float &alt) {
    portENTER_CRITICAL(&gpsPosMux);
    lat = lastLat;
    lon = lastLon;
    alt = lastAlt;
    portEXIT_CRITICAL(&gpsPosMux);
}

void getGpsPosition(double &lat, double &lon) {
    float ignored;
    getGpsPosition(lat, lon, ignored);
}

// Only called for authenticated messages (see the filter in loop()). The oldest
// entry is evicted when full; every entry has proven it holds the team key, so
// a stranger cannot flood the table.
void upsertTeammate(const String &name, double lat, double lon, uint32_t counter) {
    for (int i = 0; i < teammateCount; i++) {
        if (teammates[i].name == name) {
            teammates[i].lat = lat;
            teammates[i].lon = lon;
            teammates[i].lastSeen = millis();
            if (counter > 0) teammates[i].lastCounter = counter;
            return;
        }
    }

    int slot;
    if (teammateCount < MAX_TEAMMATES) {
        slot = teammateCount++;
    } else {
        slot = 0;
        for (int i = 1; i < MAX_TEAMMATES; i++) {
            if (teammates[i].lastSeen < teammates[slot].lastSeen) slot = i;
        }
    }

    teammates[slot].name = name;
    teammates[slot].lat = lat;
    teammates[slot].lon = lon;
    teammates[slot].lastSeen = millis();
    teammates[slot].lastCounter = counter;
}

// A single read on the ESP32-S3 ADC can jump by over 100 mV and its curve is not
// linear. analogReadMilliVolts() applies the eFuse calibration; averaging
// removes the noise.
// Charging state, inferred from the voltage trace. There is no charge-status
// pin on this board, so this is a heuristic; it is stated as such rather than
// presented as a measurement.
bool  batCharging   = false;
bool  batSettling   = false;   // just unplugged, cell still relaxing
float batRestingV   = 0.0f;    // last voltage believed to reflect real charge
static unsigned long batUnplugAt   = 0;
static unsigned long batTrendAt    = 0;
static float         batTrendStart = 0.0f;

// Charge progress. The terminal voltage jumps the instant a cable goes in, so
// that first jump says nothing about the pack. What does say something is the
// climb AFTER it: in the constant-current phase the voltage rises steadily
// toward the end-of-charge point. Progress is measured from the post-jump
// voltage, and starts from the last resting percentage, so the number begins
// at the truth and moves up instead of leaping to a wrong one.
static float chgStartV   = 0.0f;
static int   chgStartPct = 0;

void sampleBattery() {
    uint32_t sum = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
        sum += analogReadMilliVolts(PIN_BAT_ADC);
    }
    const float v = (sum / (float)BATTERY_ADC_SAMPLES) / 1000.0f * BATTERY_DIVIDER;
    const float prev = batVoltage;
    batVoltage = v;

    if (prev < 0.5f) {                  // first reading after boot
        batRestingV = v;
        batTrendStart = v;
        batTrendAt = millis();
        return;
    }

    if (!batCharging) {
        // A sharp rise can only come from an external supply.
        if (v - prev >= BATTERY_CHARGE_STEP_V) {
            batCharging = true;
            batSettling = false;
            chgStartV   = v;
            chgStartPct = getBatteryPercent();
        } else if (v - batTrendStart >= BATTERY_CHARGE_TREND_V
                   && millis() - batTrendAt >= BATTERY_TREND_MS) {
            // Cable was already in at power-on: no step to catch, but the pack
            // still climbs, which discharging never does.
            batCharging = true;
            chgStartV   = batTrendStart;
            chgStartPct = getBatteryPercent();
        } else if (!batSettling) {
            // Discharging and settled: this reading is the truth worth keeping.
            batRestingV = v;
        }
    } else if (prev - v >= BATTERY_UNPLUG_STEP_V) {
        batCharging = false;
        batSettling = true;
        batUnplugAt = millis();
    }

    if (batSettling && millis() - batUnplugAt >= BATTERY_SETTLE_MS) {
        batSettling = false;
        batRestingV = v;
    }

    // Restart the trend window regularly so a slow drift is not read as a rise.
    if (millis() - batTrendAt >= BATTERY_TREND_MS) {
        batTrendAt = millis();
        batTrendStart = v;
    }
}

bool batteryCharging() { return batCharging; }

// Where the charge has got to, as a percentage. Only meaningful while a
// charger is attached; it saturates near the end because the last stretch is
// constant-voltage, where the voltage stops moving and only the current falls,
// which this board cannot measure.
int batteryChargePercent() {
    if (!batCharging) return getBatteryPercent();
    const float span = BATTERY_FULL_V - chgStartV;
    if (span <= 0.02f) return 100;
    float progress = (batVoltage - chgStartV) / span;
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    int pct = chgStartPct + (int)((100 - chgStartPct) * progress);
    if (pct < chgStartPct) pct = chgStartPct;
    if (pct > 100) pct = 100;
    return pct;
}

// The percentage the pack really has. While a charger is attached the terminal
// voltage says nothing about state of charge, so the last resting reading is
// reported instead of a number that would be wrong by 40 points.
float batteryTrustedVoltage() {
    return (batCharging || batSettling) ? batRestingV : batVoltage;
}

// Piecewise approximation of the 18650 open-circuit curve. The only formula in
// the project: screen, phone and CSV must report the same percent.
int getBatteryPercent() {
    float v = batteryTrustedVoltage();
    if (v >= 4.15f) return 100;
    if (v <= 3.30f) return 0;
    if (v > 3.90f) return (int)(80.0f + (v - 3.90f) * (20.0f / 0.25f));
    if (v > 3.70f) return (int)(45.0f + (v - 3.70f) * (35.0f / 0.20f));
    if (v > 3.55f) return (int)(15.0f + (v - 3.55f) * (30.0f / 0.15f));
    return (int)((v - 3.30f) * (15.0f / 0.25f));
}

// Never warn while charging: the pack is on its way up, and a warning that
// cannot be acted on is noise.
bool batteryLow() {
    if (batCharging) return false;
    const float v = batteryTrustedVoltage();
    return v > 0.5f && v < BATTERY_WARN_V;
}

// Sustained undervoltage forces an ordered shutdown. Running until the regulator
// drops can cut an SD write in half and corrupt the track or the FAT.
void checkBatteryCutoff() {
    static unsigned long belowSince = 0;

    // Never shut down while a charger is attached: the pack is going up, and a
    // shutdown would strand the device exactly when it is being rescued.
    if (batCharging) { belowSince = 0; return; }

    // Below 0.5 V the ADC is not reading a battery (USB power, no cell).
    if (batVoltage < 0.5f || batVoltage >= BATTERY_CUTOFF_V) {
        belowSince = 0;
        return;
    }

    if (belowSince == 0) {
        belowSince = millis();
        return;
    }

    // A current spike (LoRa TX draws hundreds of mA) must not shut the device
    // down, so the threshold has to be held continuously.
    if (millis() - belowSince < BATTERY_CUTOFF_HOLD_MS) return;

    notifyPhone("[SYS] Battery critical, shutting down");
    stopRecordingSafely();
    showShutdownNotice(tr_batt_empty[currentLang]);
    enterDeepSleep8Min(false);
}

String healthBadge() {
    String bad = "";
    if (!health.radioOk)   bad += "RADIO ";
    if (!health.buttonsOk) bad += "BTN ";
    if (!health.gpsOk)     bad += "GPS ";
    if (!sdDetected)       bad += "SD ";
    bad.trim();
    return bad;
}

// DIAGNOSTICS
//
// When a sensor seems dead, the useful question is whether the chip answers on
// the bus at all. If it answers, the fault is calibration or mounting; if not,
// it is the address, the solder joint or a dead chip. Reads and reports only:
// initializes nothing, changes nothing.
void reportDiagnostics() {
    notifyPhone("SYS_DIAG_BEGIN");
    delay(BLE_NOTIFY_GAP_MS);

    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(300)) == pdTRUE) {
        String found = "";
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                char b[8];
                snprintf(b, sizeof(b), "0x%02X ", addr);
                found += b;
            }
        }
        xSemaphoreGive(i2cMutex);
        notifyPhone("SYS_DIAG:i2c|" + (found.length() ? found : String("(nothing)")));
    } else {
        notifyPhone("SYS_DIAG:i2c|bus busy");
    }
    delay(BLE_NOTIFY_GAP_MS);

    // The LIS3MDL sits at 0x1C or 0x1E depending on the address pin.
    bool magAt1C = false, magAt1E = false;
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        Wire.beginTransmission(0x1C); magAt1C = (Wire.endTransmission() == 0);
        Wire.beginTransmission(0x1E); magAt1E = (Wire.endTransmission() == 0);
        xSemaphoreGive(i2cMutex);
    }
    String magNote;
    if (magAt1C || magAt1E) {
        magNote = String("answers at ") + (magAt1C ? "0x1C" : "0x1E")
                + " - the chip is alive; the trouble is calibration or a magnet nearby";
    } else {
        magNote = "no answer at 0x1C or 0x1E - wrong address, bad solder joint, or dead chip";
    }
    notifyPhone("SYS_DIAG:compass|" + magNote);
    delay(BLE_NOTIFY_GAP_MS);

    // This firmware never brings the compass up: mag_ok is not set true anywhere.
    notifyPhone(String("SYS_DIAG:compass_init|") + (mag_ok ? "started" : "NOT started by this firmware"));
    delay(BLE_NOTIFY_GAP_MS);

    // Both ternary branches must be String; a const char* branch will not compile.
    String radioNote = health.radioOk ? String("ok")
                                      : String("FAILED err=") + String(health.radioError);
    notifyPhone("SYS_DIAG:radio|" + radioNote);
    delay(BLE_NOTIFY_GAP_MS);
    notifyPhone("SYS_DIAG:sensors|gps=" + String(gps_ok ? "ok" : "no")
                + " imu=" + String(imu_ok ? "ok" : "no")
                + " sht=" + String(sht_ok ? "ok" : "no")
                + " sd="  + String(sdDetected ? "ok" : "no"));
    delay(BLE_NOTIFY_GAP_MS);

    notifyPhone("SYS_DIAG:memory|free " + String(ESP.getFreeHeap() / 1024) + " KB, largest block "
                + String(ESP.getMaxAllocHeap() / 1024) + " KB");
    delay(BLE_NOTIFY_GAP_MS);
    notifyPhone("SYS_DIAG:uptime|" + String(millis() / 60000) + " min");
    delay(BLE_NOTIFY_GAP_MS);
    notifyPhone("SYS_DIAG:airtime|" + String(meshDutyPercent()) + "% of the hourly budget");
    delay(BLE_NOTIFY_GAP_MS);
    notifyPhone("SYS_DIAG:version|" + String(OS_VERSION));

    notifyPhone("SYS_DIAG_END");
}
