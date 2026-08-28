void acquireSD() { 
    digitalWrite(EDP_CS, HIGH); 
    delay(1); 
}

long long getKmlTimestamp(String fname) {
    String nameLower = fname;
    nameLower.toLowerCase();
    int start = nameLower.indexOf("route_");
    if (start < 0 || fname.length() < (start + 22)) return 0; 
    start += 6; 
    long long d   = fname.substring(start + 0,  start + 2).toInt();  
    long long m   = fname.substring(start + 3,  start + 5).toInt();  
    long long y   = fname.substring(start + 6,  start + 10).toInt(); 
    long long h   = fname.substring(start + 11, start + 13).toInt(); 
    long long min = fname.substring(start + 14, start + 16).toInt(); 
    return (y * 100000000LL) + (m * 1000000LL) + (d * 10000LL) + (h * 100) + min;
}

void scanKMLFiles() {
    kmlFileCount = 0; 
    kmlFiles[kmlFileCount++] = "[ NO OVERLAY ]"; 
    
    if (!sdDetected) return; 
    acquireSD(); 
    
    File root = SD.open("/"); 
    if (!root) return;
    
    String tempFiles[MAX_KML_FILES];
    int tempCount = 0;
    
    root.rewindDirectory();
    while (true) {
        File file = root.openNextFile();
        if (!file) break;
        if (!file.isDirectory()) {
            String fname = String(file.name()); 
            String fnameLower = fname; 
            fnameLower.toLowerCase();
            if (fnameLower.endsWith(".kml")) {
                if (tempCount < MAX_KML_FILES - 1) {
                    tempFiles[tempCount++] = fname;
                }
            }
        }
        file.close();
    }
    root.close();

    for (int i = 0; i < tempCount - 1; i++) {
        for (int j = i + 1; j < tempCount; j++) {
            if (getKmlTimestamp(tempFiles[i]) < getKmlTimestamp(tempFiles[j])) {
                String t = tempFiles[i];
                tempFiles[i] = tempFiles[j];
                tempFiles[j] = t;
            }
        }
    }
    
    for (int i = 0; i < tempCount; i++) {
        if (kmlFileCount < MAX_KML_FILES) {
            kmlFiles[kmlFileCount++] = tempFiles[i];
        }
    }
}

// Bucla de citire a unui KML de pe SD era copiata identic in loadKMLCache() si
// in drawKMLOverlay(): acelasi parser de tag-uri, acelasi tokenizer, aceeasi
// hranire a watchdog-ului la fiecare 250 de caractere. Singura diferenta reala
// era ce se face cu fiecare coordonata. Acum bucla e una singura si primeste
// un callback; newSegment marcheaza inceputul unui bloc <coordinates> nou.

static void forEachKmlPoint(String path, KmlPointFn onPoint) {
    if (path == "" || path == "[ NO OVERLAY ]" || !sdDetected) return;

    acquireSD();
    if (!path.startsWith("/")) path = "/" + path;
    path.replace("//", "/");

    File f = SD.open(path);
    if (!f) return;

    bool inCoords = false;
    bool newSegment = true;
    String token = "";
    int watchdogFeeder = 0;

    // Interpreteaza un token "lon,lat[,alt]" si il paseaza mai departe.
    auto emit = [&](String t) {
        t.trim();
        if (t.length() <= 3) return;
        int c1 = t.indexOf(',');
        if (c1 <= 0) return;
        float lon = t.substring(0, c1).toFloat();
        int c2 = t.indexOf(',', c1 + 1);
        float lat = (c2 > 0) ? t.substring(c1 + 1, c2).toFloat()
                             : t.substring(c1 + 1).toFloat();
        if (lon == 0.0f || lat == 0.0f) return;
        onPoint(lat, lon, newSegment);
        newSegment = false;
    };

    while (f.available()) {
        char c = f.read();

        // Fara asta, un KML mare declanseaza watchdog-ul in timpul citirii.
        watchdogFeeder++;
        if (watchdogFeeder > 250) { yield(); watchdogFeeder = 0; }

        if (c == '<') {
            if (inCoords && token.length() > 0) { emit(token); token = ""; }
            String tag = "<";
            while (f.available()) {
                c = f.read();
                watchdogFeeder++;
                if (watchdogFeeder > 250) { yield(); watchdogFeeder = 0; }
                tag += c;
                if (c == '>') break;
            }
            if (tag.indexOf("<coordinates>") != -1) {
                inCoords = true;
                newSegment = true;
            } else if (tag.indexOf("</coordinates>") != -1) {
                inCoords = false;
                newSegment = true;
            }
        } else if (inCoords) {
            if (isspace(c)) {
                if (token.length() > 0) { emit(token); token = ""; }
            } else {
                if (token.length() < 64) token += c;
            }
        }
    }
    f.close();
}

void loadKMLCache() {
    kmlCacheMinLat = 90;
    kmlCacheMaxLat = -90;
    kmlCacheMinLon = 180;
    kmlCacheMaxLon = -180;
    kmlCacheDistance = 0.0;
    kmlCacheValid = false;

    float prevLat = -999, prevLon = -999;

    forEachKmlPoint(currentKmlOverlay, [&](float lat, float lon, bool newSegment) {
        if (lat < kmlCacheMinLat) kmlCacheMinLat = lat;
        if (lat > kmlCacheMaxLat) kmlCacheMaxLat = lat;
        if (lon < kmlCacheMinLon) kmlCacheMinLon = lon;
        if (lon > kmlCacheMaxLon) kmlCacheMaxLon = lon;

        if (newSegment) { prevLat = -999; prevLon = -999; }

        if (prevLat != -999 && prevLon != -999) {
            float legDist = calculateDistance(prevLat, prevLon, lat, lon);
            // Sarituri de peste 50 km intre doua puncte consecutive inseamna
            // date corupte, nu deplasare - nu le adunam in total.
            if (legDist < 50.0) kmlCacheDistance += legDist;
        }
        prevLat = lat;
        prevLon = lon;
    });

    if (kmlCacheMinLat != 90 && kmlCacheMaxLat != -90) {
        kmlCacheValid = true;
    }
}

void drawKMLOverlay(String path, double centerLat, double centerLon, float scale,
                    float cosLat, int cx, int cy, int mx, int my, int mw, int mh) {
    bool isFirstPoint = true;
    int prevX = 0, prevY = 0;

    forEachKmlPoint(path, [&](float lat, float lon, bool newSegment) {
        if (newSegment) isFirstPoint = true;

        long px_raw = cx + (lon - centerLon) * scale * cosLat;
        long py_raw = cy - (lat - centerLat) * scale;

        // Coordonatele foarte departe de ecran ar depasi intervalul lui int.
        if (px_raw >  30000) px_raw =  30000;
        if (px_raw < -30000) px_raw = -30000;
        if (py_raw >  30000) py_raw =  30000;
        if (py_raw < -30000) py_raw = -30000;

        int px = (int)px_raw;
        int py = (int)py_raw;

        // Un punct care cade exact peste cel anterior nu adauga nimic vizual,
        // dar costa un drawLine - la trasee lungi economiseste mult timp.
        if (!isFirstPoint && px == prevX && py == prevY) return;

        if (!isFirstPoint) display.drawLine(prevX, prevY, px, py, GxEPD_BLACK);
        else isFirstPoint = false;

        prevX = px;
        prevY = py;
    });
}

void logSystemData(String sysLog) {
    if (!sdDetected) return;
    acquireSD(); 
    String dateStr = formatLocalDateFile();
    String timeStr = formatLocalTimeSec();
    int batPct = getBatteryPercent();

    String csvPath = "/sys_" + dateStr + ".csv";
    bool isNewFile = !SD.exists(csvPath); 

    File f = SD.open(csvPath, FILE_APPEND);
    if (f) {
        if (isNewFile) {
            f.println("Date,Time,Latitude,Longitude,Temp(C),Humidity(%),Accel_X(m/s2),Accel_Y(m/s2),Accel_Z(m/s2),Speed(km/h),Heading(deg),Battery(%),Log_System");
        }
    
        double cLat, cLon;
        getGpsPosition(cLat, cLon);
        f.printf("%s,%s,%.7f,%.7f,%.1f,%.1f,%.2f,%.2f,%.2f,%.1f,%.1f,%d,%s\n",
            dateStr.c_str(), timeStr.c_str(), cLat, cLon, currentTemp, currentHum,
            currentAx, currentAy, currentAz, currentSpeed, currentHeading, batPct, sysLog.c_str());
        f.flush(); 
        f.close();
    }
}

void logGPS(double lat, double lon, float alt) {
    if (lastAccuracy > 100.0) return;

    if (lastLoggedLat != 0.0 && lastLoggedLon != 0.0) { 
        double dKm = calculateDistance(lastLoggedLat, lastLoggedLon, lat, lon);
        double dMeters = dKm * 1000.0;
        
        bool imuMoving = (millis() - lastPhysicalMovement < 5000);
        
        if (imuMoving) {
            if (dMeters < 2.0) return;
        } else {
         
            if (dMeters < 25.0) return;
        }
        
        if (isRecording) {
            sessionTotalDistance += dKm; 
        }
        requestUIUpdate = true; 
    }
    
    lastLoggedLat = lat; 
    lastLoggedLon = lon;

    if (breadcrumbIdx < MAX_BREADCRUMBS) {
        breadcrumbs[breadcrumbIdx++] = {(float)lat, (float)lon}; 
    } else {
        for (int i = 1; i < MAX_BREADCRUMBS; i++) {
            breadcrumbs[i-1] = breadcrumbs[i];
        }
        breadcrumbs[MAX_BREADCRUMBS - 1] = {(float)lat, (float)lon};
    }

    if (isRecording && sdDetected && strlen(currentRecordDate) > 0) {
        acquireSD(); 
        String kmlPath = "/route_" + String(currentRecordDate) + ".kml";
        File f = SD.open(kmlPath, "r+"); 
        if (f) { 
            String footer = "</coordinates>";
            long fileSize = f.size();
            long searchPos = (fileSize > 256) ? fileSize - 256 : 0;
            f.seek(searchPos);
            
            String tail = "";
            while(f.available()) tail += (char)f.read();
            
            int tagIdx = tail.lastIndexOf(footer);
            if (tagIdx != -1) {
                f.seek(searchPos + tagIdx);
                f.print(String(lon, 7) + "," + String(lat, 7) + "," + String(alt, 1) + "\n"); 
                f.print(KML_CLOSING); 
            }
            f.close(); 
        }
    }
}

void logLoraMessage(String msg, bool isEncrypted) {
    if (!sdDetected) return; 
    acquireSD();
    String dateStr = formatLocalDateFile();
    String path = isEncrypted ? "/sec_" + dateStr + ".txt" : "/pub_" + dateStr + ".txt";
    File f = SD.open(path, FILE_APPEND); 
    if (f) { 
        f.println(msg); 
        f.flush(); 
        f.close(); 
    }
}

void checkSDHotplug() {
    acquireSD();
    if (sdDetected) { 
        File root = SD.open("/"); 
        if (root) { 
            root.close(); 
        } else { 
            SD.end(); 
            sdDetected = false; 
            return; 
        } 
    }
    if (!sdDetected) { 
        if (SD.begin(SDCARD_CS, SDSPI, 2000000)) {
            sdDetected = true;
        }
    }
}