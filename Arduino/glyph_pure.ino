//file name:glyph_pure.ino
//
// Logica fara hardware: conversia UTC -> ora locala si calculul distantei.
//
// Sta separat pentru ca poate fi compilata si rulata pe PC. Suita din test/
// verifica getLocalDateTime() pe mii de combinatii de ora, offset si date de
// granita (schimbari de luna, de an, ani bisecti) contra bibliotecii standard -
// exact genul de cod unde o greseala nu se vede pana cand cineva inregistreaza
// un traseu la 23:30 si fisierul primeste data de ieri.

double calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    const double R = 6371.0; 
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * M_PI / 180.0) * cos(lat2 * M_PI / 180.0) *
               sin(dLon / 2) * sin(dLon / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    return R * c;
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

