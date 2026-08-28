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

// Deschide fisierul si lasa sablonul din glyph_kml_parser.h sa faca parsarea.
// Sablonul e separat ca sa poata fi rulat si pe PC, peste un flux fals - vezi test/.
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

// Overlay-ul incarcat, tinut in RAM.
//
// Inainte, drawKMLOverlay() recitea tot fisierul de pe card la FIECARE
// redesenare a hartii - o functie de desenare care face I/O de disc. Pe un
// traseu lung asta insemna secunde bune de asteptare la fiecare apasare de buton.
// Acum fisierul se citeste o singura data, la selectie.
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

    // Un traseu poate avea mai multe puncte decat incap. In loc sa il taiem la
    // jumatate, il subtiem: cand bufferul se umple, pastram fiecare al doilea
    // punct si dublam pasul. Rezultatul e traseul intreg, cu mai putine detalii.
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
            // Sarituri de peste 50 km intre doua puncte consecutive inseamna
            // date corupte, nu deplasare - nu le adunam in total.
            if (legDist < 50.0) kmlCacheDistance += legDist;
        }
        prevLat = lat;
        prevLon = lon;

        // Un inceput de segment se pastreaza intotdeauna: fara el, doua trasee
        // separate ar aparea unite printr-o linie dreapta.
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

            if (kmlCachePointCount >= MAX_KML_CACHE_POINTS) return; // buffer plin de inceputuri
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

// Deseneaza din RAM. Fara acces la card, deci poate fi apelata la fiecare cadru.
void drawKMLOverlay(double centerLat, double centerLon, float scale,
                    float cosLat, int cx, int cy) {
    if (!kmlCacheValid || kmlCachePointCount == 0) return;

    bool isFirstPoint = true;
    int prevX = 0, prevY = 0;

    for (int i = 0; i < kmlCachePointCount; i++) {
        if (kmlCachePoints[i].newSegment) isFirstPoint = true;

        long px_raw = cx + (kmlCachePoints[i].lon - centerLon) * scale * cosLat;
        long py_raw = cy - (kmlCachePoints[i].lat - centerLat) * scale;

        // Coordonatele foarte departe de ecran ar depasi intervalul lui int.
        if (px_raw >  30000) px_raw =  30000;
        if (px_raw < -30000) px_raw = -30000;
        if (py_raw >  30000) py_raw =  30000;
        if (py_raw < -30000) py_raw = -30000;

        int px = (int)px_raw;
        int py = (int)py_raw;

        // Un punct care cade exact peste cel anterior nu adauga nimic vizual,
        // dar costa un drawLine - la trasee lungi economiseste mult timp.
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
            // Fara miscare confirmata de IMU, pragul e mult mai mare: altfel am
            // inregistra drift-ul GPS ca pe o plimbare.
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