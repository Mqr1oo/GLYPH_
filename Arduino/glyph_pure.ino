//file name:glyph_pure.ino
//
// Hardware-free logic: UTC to local time, and distance. Separate so it compiles
// and runs on a PC. The suite in test/ checks getLocalDateTime() against the
// standard library over thousands of hour, offset and boundary combinations
// (month, year, leap year). Mistakes here stay invisible until someone records
// a track at 23:30 and the file gets yesterday's date.

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
// Local time.
//
// (gpsHour + timeOffset + 24) % 24 fixes the hour but leaves the date on UTC.
// At UTC+2, after 22:00 the KML and CSV file names got yesterday's date; with a
// negative offset, tomorrow's. This rolls the day, the month and the year too.
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

// "DD_MM_YYYY", used in SD file names.
String formatLocalDateFile() {
    if (!((gps_ok || simActive) && gpsTimeValid)) return String("00_00_0000");

    int y, mo, d, h, mi, s;
    getLocalDateTime(y, mo, d, h, mi, s);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d_%02d_%04d", d, mo, y);
    return String(buf);
}

String formatLocalTimeSec() {
    if (!((gps_ok || simActive) && gpsTimeValid)) return String("00:00:00");

    int y, mo, d, h, mi, s;
    getLocalDateTime(y, mo, d, h, mi, s);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, mi, s);
    return String(buf);
}

