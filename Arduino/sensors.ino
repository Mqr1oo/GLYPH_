//file name:sensors.ino
//
// FIX MAJOR (scroll greoi cand GPS-ul e conectat):
// Vechiul cod avea butoanele SI GPS-ul in ACELASI task, sub ACELASI mutex I2C.
// Odata pe secunda se apela myGNSS.getPositionAccuracy(), care trimite
// UBX-NAV-HPPOSECEF si asteapta implicit 1100 ms. SAM-M8Q nu suporta acel mesaj,
// deci apelul astepta MEREU timeout-ul complet -> ~1.2 s pe secunda in care
// mutexul era ocupat si butoanele NU erau citite deloc.
// Rezultat: apasarile se pierdeau, scroll-ul parea "mort".
//
// Solutia are 3 parti:
//   1) getPositionAccuracy() -> getHorizontalAccEst() (hAcc vine deja in pachetul
//      NAV-PVT, zero tranzactii I2C in plus, zero asteptare).
//   2) setAutoPVT(true): modulul impinge singur PVT-ul, getPVT() devine
//      ne-blocant in loc sa faca poll cu asteptare.
//   3) Butoanele au task propriu, cu prioritate mai mare si perioada 15 ms,
//      complet separat de task-ul lent de senzori.

// ---------------------------------------------------------------------------
// TASK RAPID: doar butoanele. Prioritate mare, 15 ms, hold I2C foarte scurt.
// ---------------------------------------------------------------------------
void buttonTask(void * pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    bool lastA = false, lastB = false, lastC = false;

    for (;;) {
        if (buttons_ok) {
            // 60 ms e suficient: nimeni nu mai tine mutexul mai mult de ~30 ms.
            if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(60)) == pdTRUE) {
                mButtons.update();
                bool a = mButtons.isPressed(0);
                bool b = mButtons.isPressed(1);
                bool c = mButtons.isPressed(2);
                xSemaphoreGive(i2cMutex);

                raw_A = a;
                raw_B = b;
                raw_C = c;

                if (a && !lastA) { latched_pA = true; lastPhysicalMovement = millis(); }
                if (b && !lastB) { latched_pB = true; lastPhysicalMovement = millis(); }
                if (c && !lastC) { latched_pC = true; lastPhysicalMovement = millis(); }

                lastA = a;
                lastB = b;
                lastC = c;
            }
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(15));
    }
}

// ---------------------------------------------------------------------------
// TASK LENT: IMU la 100 ms, GPS + SHT40 la 1 s. Prioritate mica.
// Fiecare bloc I2C isi ia mutexul separat si il elibereaza imediat,
// ca sa nu blocheze niciodata task-ul de butoane.
// ---------------------------------------------------------------------------
void sensorTask(void * pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    unsigned long lastSlowSensorTick = 0;

    for (;;) {

        // ---- IMU (detectie miscare) ----
        if (imu_ok) {
            if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                sensors_event_t a, g, tm;
                imu.getEvent(&a, &g, &tm);
                xSemaphoreGive(i2cMutex);

                currentAx = a.acceleration.x;
                currentAy = a.acceleration.y;
                currentAz = a.acceleration.z;
                float acc_mag = sqrtf(currentAx*currentAx + currentAy*currentAy + currentAz*currentAz);
                if (fabsf(acc_mag - 9.8f) > 1.2f) {
                    lastPhysicalMovement = millis();
                }
            }
        }

        // ---- GPS + mediu, o data pe secunda ----
        if (millis() - lastSlowSensorTick > 1000) {
            lastSlowSensorTick = millis();

            if (gps_ok && currentPowerMode != STEALTH_MODE) {
                if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                    // Cu setAutoPVT(true) apelul asta NU blocheaza: intoarce true
                    // doar daca modulul a livrat deja un pachet PVT proaspat.
                    bool fresh = myGNSS.getPVT(0);
                    if (fresh) {
                        hasGpsFix   = myGNSS.getGnssFixOk();
                        gpsTimeValid = myGNSS.getTimeValid();

                        if (gpsTimeValid) {
                            gpsYear   = myGNSS.getYear();
                            gpsMonth  = myGNSS.getMonth();
                            gpsDay    = myGNSS.getDay();
                            gpsHour   = myGNSS.getHour();
                            gpsMinute = myGNSS.getMinute();
                            gpsSecond = myGNSS.getSecond();
                        }

                        if (hasGpsFix) {
                            // Scriere atomica: loop() de pe celalalt core nu mai poate
                            // citi jumatate din latitudinea noua cu jumatate din cea veche.
                            setGpsPosition((double)myGNSS.getLatitude() / 1e7,
                                           (double)myGNSS.getLongitude() / 1e7,
                                           myGNSS.getAltitudeMSL() / 1e3);
                            lastSIV = myGNSS.getSIV();

                            // hAcc vine din acelasi pachet NAV-PVT, in mm -> metri.
                            // (inainte: getPositionAccuracy(), 1100 ms de blocaj)
                            lastAccuracy = myGNSS.getHorizontalAccEst() / 1000.0f;

                            currentSpeed   = (float)myGNSS.getGroundSpeed() * 0.0036f;
                            currentHeading = (float)myGNSS.getHeading() / 100000.0f;

                            new_gps_data = true;
                        }
                    }
                    xSemaphoreGive(i2cMutex);
                }
            }

            if (sht_ok) {
                if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                    sensors_event_t he, te;
                    sht40.getEvent(&he, &te);
                    xSemaphoreGive(i2cMutex);
                    currentTemp = te.temperature;
                    currentHum  = he.relative_humidity;
                }
            }
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(100));
    }
}
