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
    radio.setSpreadingFactor(11);
    radio.setBandwidth(125.0);
    radio.setCodingRate(8);
    radio.setSyncWord(0x12);
    radio.setOutputPower(22);
}

void beginRadio(float freq) {
    radio.begin(freq);
    applyRadioProfile();
}

// ---------------------------------------------------------------------------
// Modul de putere. Ordinea operatiilor e cea din meniu, pastrata identic.
// ---------------------------------------------------------------------------
void applyPowerMode(PowerMode mode) {
    if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) != pdTRUE) return;

    if (mode == NORMAL_MODE) {
        setCpuFrequencyMhz(80);
        if (gps_ok) { myGNSS.powerSaveMode(false); myGNSS.setMeasurementRate(1000); }
    } else if (mode == ECO_MODE) {
        if (gps_ok) { myGNSS.powerSaveMode(true); myGNSS.setMeasurementRate(10000); }
        setCpuFrequencyMhz(40);
    } else if (mode == STEALTH_MODE) {
        if (gps_ok) { myGNSS.powerSaveMode(true); myGNSS.setMeasurementRate(10000); }
        radio.standby();
        loraListening = false;
        setCpuFrequencyMhz(40);
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
    if (name.length() > 16) name = name.substring(0, 16);
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
// Ora locala.
//
// Peste tot in cod se scria (gpsHour + timeOffset + 24) % 24, ceea ce corecteaza
// ora dar lasa ziua pe cea UTC. Pentru UTC+2, dupa ora 22:00 numele fisierelor
// KML si CSV primeau data de ieri; pentru offset negativ, data de maine.
// Aici se roteste si ziua, si luna, si anul.
// ---------------------------------------------------------------------------
static bool isLeapYear(int y) {
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

static int daysInMonth(int y, int m) {
    static const int d[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2 && isLeapYear(y)) return 29;
    if (m < 1 || m > 12) return 30;
    return d[m - 1];
}

void getLocalDateTime(int &year, int &month, int &day,
                      int &hour, int &minute, int &second) {
    year   = gpsYear;
    month  = gpsMonth;
    day    = gpsDay;
    hour   = gpsHour + timeOffset;
    minute = gpsMinute % 60;
    second = gpsSecond % 60;

    if (month < 1 || month > 12) month = 1;
    if (day < 1) day = 1;

    while (hour < 0) {
        hour += 24;
        day--;
        if (day < 1) {
            month--;
            if (month < 1) { month = 12; year--; }
            day = daysInMonth(year, month);
        }
    }
    while (hour > 23) {
        hour -= 24;
        day++;
        if (day > daysInMonth(year, month)) {
            day = 1;
            month++;
            if (month > 12) { month = 1; year++; }
        }
    }
}

// "HH:MM" sau "HH:MMAM/PM", in functie de setare.
String formatLocalTime() {
    if (!gpsTimeValid && !simActive) return "--:--";

    int y, mo, d, h, mi, s;
    getLocalDateTime(y, mo, d, h, mi, s);

    char buf[12];
    if (useAmPmFormat) {
        int ampmH = h % 12;
        if (ampmH == 0) ampmH = 12;
        snprintf(buf, sizeof(buf), "%02d:%02d%s", ampmH, mi, h >= 12 ? "PM" : "AM");
    } else {
        snprintf(buf, sizeof(buf), "%02d:%02d", h, mi);
    }
    return String(buf);
}

// "DD_MM_YYYY" - folosit in numele fisierelor de pe SD.
String formatLocalDateFile() {
    if (!((gps_ok || simActive) && gpsTimeValid)) return String("00_00_0000");

    int y, mo, d, h, mi, s;
    getLocalDateTime(y, mo, d, h, mi, s);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d_%02d_%04d", d, mo, y);
    return String(buf);
}

// "HH:MM:SS"
String formatLocalTimeSec() {
    if (!((gps_ok || simActive) && gpsTimeValid)) return String("00:00:00");

    int y, mo, d, h, mi, s;
    getLocalDateTime(y, mo, d, h, mi, s);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, mi, s);
    return String(buf);
}
