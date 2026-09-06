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

#include "glyph_kml_parser.h"

// The parser template is separate so it can also run on a PC. See test/.
static void forEachKmlPoint(String path, KmlPointFn onPoint) {
    if (path == "" || path == "[ NO OVERLAY ]" || !sdDetected) return;

    acquireSD();
    if (!path.startsWith("/")) path = "/" + path;
    path.replace("//", "/");

    File f = SD.open(path);
    if (!f) return;

    parseKmlStream(f, onPoint);
    f.close();
}

// Overlay held in RAM: the file is read once, at selection. Drawing must not
// touch the card, or every map redraw costs seconds on a long route.
KmlPoint kmlCachePoints[MAX_KML_CACHE_POINTS];
int  kmlCachePointCount = 0;

void loadKMLCache() {
    kmlCacheMinLat = 90;
    kmlCacheMaxLat = -90;
    kmlCacheMinLon = 180;
    kmlCacheMaxLon = -180;
    kmlCacheDistance = 0.0;
    kmlCacheValid = false;
    kmlCachePointCount = 0;

    float prevLat = -999, prevLon = -999;

    // A route can have more points than fit. Instead of cutting it short, thin
    // it: when the buffer fills, keep every second point and double the stride.
    int stride = 1;
    int seen   = 0;

    forEachKmlPoint(currentKmlOverlay, [&](float lat, float lon, bool newSegment) {
        if (lat < kmlCacheMinLat) kmlCacheMinLat = lat;
        if (lat > kmlCacheMaxLat) kmlCacheMaxLat = lat;
        if (lon < kmlCacheMinLon) kmlCacheMinLon = lon;
        if (lon > kmlCacheMaxLon) kmlCacheMaxLon = lon;

        if (newSegment) { prevLat = -999; prevLon = -999; }

        if (prevLat != -999 && prevLon != -999) {
            float legDist = calculateDistance(prevLat, prevLon, lat, lon);
            // A jump over 50 km between points is corrupt data, not travel.
            if (legDist < 50.0) kmlCacheDistance += legDist;
        }
        prevLat = lat;
        prevLon = lon;

        // Segment starts are always kept, or separate tracks get joined by a line.
        bool mustKeep = newSegment;

        if (!mustKeep && (seen % stride) != 0) { seen++; return; }
        seen++;

        if (kmlCachePointCount >= MAX_KML_CACHE_POINTS) {
            int kept = 0;
            for (int i = 0; i < kmlCachePointCount; i++) {
                if (i % 2 == 0 || kmlCachePoints[i].newSegment) {
                    kmlCachePoints[kept++] = kmlCachePoints[i];
                }
            }
            kmlCachePointCount = kept;
            stride *= 2;

            if (kmlCachePointCount >= MAX_KML_CACHE_POINTS) return; // buffer full of segment starts
        }

        kmlCachePoints[kmlCachePointCount].lat = lat;
        kmlCachePoints[kmlCachePointCount].lon = lon;
        kmlCachePoints[kmlCachePointCount].newSegment = newSegment;
        kmlCachePointCount++;
    });

    if (kmlCacheMinLat != 90 && kmlCacheMaxLat != -90) {
        kmlCacheValid = true;
    }
}

void drawKMLOverlay(double centerLat, double centerLon, float scale,
                    float cosLat, int cx, int cy) {
    if (!kmlCacheValid || kmlCachePointCount == 0) return;

    bool isFirstPoint = true;
    int prevX = 0, prevY = 0;

    for (int i = 0; i < kmlCachePointCount; i++) {
        if (kmlCachePoints[i].newSegment) isFirstPoint = true;

        long px_raw = cx + (kmlCachePoints[i].lon - centerLon) * scale * cosLat;
        long py_raw = cy - (kmlCachePoints[i].lat - centerLat) * scale;

        // Coordinates far off screen would overflow int.
        if (px_raw >  30000) px_raw =  30000;
        if (px_raw < -30000) px_raw = -30000;
        if (py_raw >  30000) py_raw =  30000;
        if (py_raw < -30000) py_raw = -30000;

        int px = (int)px_raw;
        int py = (int)py_raw;

        if (!isFirstPoint && px == prevX && py == prevY) continue;

        if (!isFirstPoint) display.drawLine(prevX, prevY, px, py, GxEPD_BLACK);
        else isFirstPoint = false;

        prevX = px;
        prevY = py;
    }
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
    if (lastAccuracy > GPS_MAX_ACCURACY_M) return;

    if (lastLoggedLat != 0.0 && lastLoggedLon != 0.0) { 
        double dKm = calculateDistance(lastLoggedLat, lastLoggedLon, lat, lon);
        double dMeters = dKm * 1000.0;
        
        bool imuMoving = (millis() - lastPhysicalMovement < GPS_IMU_MOVING_WINDOW_MS);

        if (imuMoving) {
            if (dMeters < GPS_MOVING_MIN_STEP_M) return;
        } else {
            // Without IMU-confirmed movement the threshold is much larger, or
            // GPS drift gets logged as walking.
            if (dMeters < GPS_STATIC_MIN_STEP_M) return;
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