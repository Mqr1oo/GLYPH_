//file name:core_helpers.ino
//
// Blocuri care erau copiate identic in mai multe locuri, stranse intr-un
// singur loc. Comportamentul e neschimbat - doar sursa adevarului e una singura.
//
//   applyRadioProfile()  inlocuia 5 copii ale aceleiasi configurari de radio
//   applyPowerMode()     inlocuia 3 copii ale blocului de mod de putere
//   pushLoraHistory()    inlocuia 3 copii ale shift-ului prin istoric
//   inSetupPage()        inlocuia 5 copii ale aceleiasi conditii
//   getLocalDateTime()   inlocuia 6 copii ale conversiei UTC -> ora locala
//   setGpsPosition()/getGpsPosition()  acces coerent la pozitie intre core-uri

// ---------------------------------------------------------------------------
// Starea de configurare initiala
// ---------------------------------------------------------------------------
bool inSetupPage() {
    return currentState == PAGE_INIT_LANG ||
           currentState == PAGE_INIT_FREQ ||
           currentState == PAGE_INIT_NAME ||
           currentState == PAGE_INIT_TIME;
}

// ---------------------------------------------------------------------------
// Configurarea radioului. Aceiasi parametri peste tot: doua aparate configurate
// pe cai diferite trebuie sa ajunga la exact aceeasi configuratie.
// ---------------------------------------------------------------------------
void applyRadioProfile() {
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
    radio.setBandwidth(LORA_BANDWIDTH_KHZ);
    radio.setCodingRate(LORA_CODING_RATE);
    radio.setSyncWord(LORA_SYNC_WORD);
    radio.setOutputPower(LORA_OUTPUT_POWER_DBM);
}

// Intoarce codul RadioLib, ca apelantul sa poata sti daca radioul chiar merge.
// Inainte, un esec la radio.begin() era complet invizibil: aparatul arata
// perfect normal si nu transmitea nimic.
int beginRadio(float freq) {
    int state = radio.begin(freq);
    health.radioOk = (state == RADIOLIB_ERR_NONE);
    health.radioError = state;
    if (health.radioOk) applyRadioProfile();
    return state;
}

// ---------------------------------------------------------------------------
// Modul de putere. Ordinea operatiilor e cea din meniu, pastrata identic.
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Istoricul de mesaje LoRa
// ---------------------------------------------------------------------------
void pushLoraHistory(String screenMsg) {
    for (int i = MAX_LORA_MSGS - 1; i > 0; i--) {
        loraHistory[i] = loraHistory[i - 1];
    }
    loraHistory[0] = screenMsg;
    if (loraMsgCount < MAX_LORA_MSGS) loraMsgCount++;
}

// ---------------------------------------------------------------------------
// Numele echipei. Erau 5 copii ale acestui bloc (comanda BLE, editorul de la
// butoane x2, intrarea seriala, intrarea BLE), iar de cand cheia se deriva cu
// PBKDF2 si se pastreaza in cache, fiecare dintre ele trebuia sa invalideze
// cache-ul - exact genul de pas care se uita intr-una din cele 5 copii.
// ---------------------------------------------------------------------------
void setTeamName(String name) {
    if (name.length() > MAX_TEAM_LEN) name = name.substring(0, MAX_TEAM_LEN);
    if (name.length() == 0) name = "ALPHA";
    myTeam = name;
    prefs.putString("team", myTeam);
    invalidateTeamKey();
}

// ---------------------------------------------------------------------------
// Pozitia GPS, coerenta intre core-uri.
//
// lastLat/lastLon sunt double (8 octeti). Scrierea lor nu e atomica pe ESP32,
// iar sensorTask (core 0) scria in timp ce loop() (core 1) citea. Rezultatul:
// din cand in cand se citea jumatatea noua a latitudinii cu jumatatea veche a
// longitudinii - o coordonata aberanta, aparuta "din senin" pe harta sau
// trimisa intr-un mesaj.
//
// Un portMUX e suficient: sectiunea critica are cateva instructiuni.
// ---------------------------------------------------------------------------
portMUX_TYPE gpsPosMux = portMUX_INITIALIZER_UNLOCKED;

void setGpsPosition(double lat, double lon, float alt) {
    portENTER_CRITICAL(&gpsPosMux);
    lastLat = lat;
    lastLon = lon;
    lastAlt = alt;
    portEXIT_CRITICAL(&gpsPosMux);
}

// Citeste cele trei valori ca un tot unitar.
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

// ---------------------------------------------------------------------------
// Coechipieri
// ---------------------------------------------------------------------------
//
// Se apeleaza doar pentru mesaje autentificate (vezi filtrul din loop()).
// Cand tabelul e plin, evacuam cel mai vechi - dar acum toti cei din tabel au
// dovedit ca detin cheia echipei, deci nu mai poate fi umplut de un strain.
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

// ---------------------------------------------------------------------------
// Bateria
// ---------------------------------------------------------------------------
//
// O singura citire pe ADC-ul ESP32-S3 poate sari cu peste 100 mV, iar curba lui
// nu e liniara. analogReadMilliVolts() aplica automat calibrarea din eFuse;
// media peste mai multe citiri scoate zgomotul.
void sampleBattery() {
    uint32_t sum = 0;
    for (int i = 0; i < BATTERY_ADC_SAMPLES; i++) {
        sum += analogReadMilliVolts(PIN_BAT_ADC);
    }
    batVoltage = (sum / (float)BATTERY_ADC_SAMPLES) / 1000.0f * BATTERY_DIVIDER;
}

// Curba reala Li-Ion 18650 in gol, aproximata pe segmente.
// Inainte existau trei formule diferite in proiect: ecranul arata un procent,
// telefonul altul, iar CSV-ul al treilea.
int getBatteryPercent() {
    float v = batVoltage;
    if (v >= 4.15f) return 100;
    if (v <= 3.30f) return 0;
    if (v > 3.90f) return (int)(80.0f + (v - 3.90f) * (20.0f / 0.25f));
    if (v > 3.70f) return (int)(45.0f + (v - 3.70f) * (35.0f / 0.20f));
    if (v > 3.55f) return (int)(15.0f + (v - 3.55f) * (30.0f / 0.15f));
    return (int)((v - 3.30f) * (15.0f / 0.25f));
}

bool batteryLow() {
    return batVoltage > 0.5f && batVoltage < BATTERY_WARN_V;
}

// Sub pragul de oprire, sustinut, inchidem ordonat.
//
// Inainte, batVoltage era doar afisat si logat: aparatul mergea pana cadea
// regulatorul, posibil in mijlocul unei scrieri pe SD, ceea ce putea lasa
// traseul trunchiat sau tabela FAT corupta. Pentru un aparat al carui rost e sa
// inregistreze unde ai fost, a pierde traseul exact la sfarsitul lui e cel mai
// prost moment posibil.
void checkBatteryCutoff() {
    static unsigned long belowSince = 0;

    // Sub 0.5 V inseamna ca ADC-ul nu citeste corect (alimentare pe USB fara
    // acumulator, de exemplu), nu ca bateria e goala.
    if (batVoltage < 0.5f || batVoltage >= BATTERY_CUTOFF_V) {
        belowSince = 0;
        return;
    }

    if (belowSince == 0) {
        belowSince = millis();
        return;
    }

    // Un varf de consum (transmisia LoRa trage cateva sute de mA) nu trebuie sa
    // opreasca aparatul degeaba, de aceea cerem ca pragul sa fie depasit continuu.
    if (millis() - belowSince < BATTERY_CUTOFF_HOLD_MS) return;

    notifyPhone("[SYS] Battery critical, shutting down");
    stopRecordingSafely();
    showShutdownNotice(tr_batt_empty[currentLang]);
    enterDeepSleep8Min(false);
}

// ---------------------------------------------------------------------------
// Starea perifericelor
// (obiectul `health` e definit in GLYPH_.ino, fiindca sketch-ul principal e
//  concatenat primul si il foloseste deja in setup())
// ---------------------------------------------------------------------------

// Text scurt pentru antetul ecranului: ce lipseste, nu ce merge.
String healthBadge() {
    String bad = "";
    if (!health.radioOk)   bad += "RADIO ";
    if (!health.buttonsOk) bad += "BTN ";
    if (!health.gpsOk)     bad += "GPS ";
    if (!sdDetected)       bad += "SD ";
    bad.trim();
    return bad;
}
