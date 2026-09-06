//file name:sensors.ino
//
// Buttons are polled in their own high priority task, separate from everything
// slow that touches I2C. Buttons and GPS used to share one task and one mutex:
// getPositionAccuracy() sends UBX-NAV-HPPOSECEF, which the SAM-M8Q does not
// support, so the call always waited out its 1100 ms timeout holding the mutex
// and presses were lost.
//
// Two things must stay or the stall returns: accuracy from getHorizontalAccEst()
// (hAcc is already in NAV-PVT, no extra I2C), and setAutoPVT(true), which keeps
// getPVT() non-blocking.
void buttonTask(void * pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    bool lastA = false, lastB = false, lastC = false;

    for (;;) {
        if (buttons_ok) {
            // 60 ms is enough: no holder keeps the I2C mutex longer than ~30 ms.
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
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(BTN_POLL_PERIOD_MS));
    }
}

// Slow task. Every I2C block takes the mutex on its own and releases it at once,
// so it can never block the button task.
void sensorTask(void * pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    unsigned long lastSlowSensorTick = 0;

    for (;;) {

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

        if (millis() - lastSlowSensorTick > 1000) {
            lastSlowSensorTick = millis();

            if (gps_ok && currentPowerMode != STEALTH_MODE) {
                if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
                    // With setAutoPVT(true) this does not block: it returns true
                    // only if the module already delivered a fresh PVT packet.
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
                            // Atomic write: loop() on the other core cannot read half of
                            // a new latitude together with half of an old one.
                            setGpsPosition((double)myGNSS.getLatitude() / 1e7,
                                           (double)myGNSS.getLongitude() / 1e7,
                                           myGNSS.getAltitudeMSL() / 1e3);
                            lastSIV = myGNSS.getSIV();

                            lastAccuracy = myGNSS.getHorizontalAccEst() / 1000.0f;   // mm -> meters

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

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SENSOR_POLL_PERIOD_MS));
    }
}
