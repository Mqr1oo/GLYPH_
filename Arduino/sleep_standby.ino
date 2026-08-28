//file name:sleep_standby.ino

// Un ecran scurt cu un singur mesaj, folosit inainte de opriri.
void showShutdownNotice(const char* line) {
    display.setRotation(currentScreenRotation);
    display.fillScreen(GxEPD_WHITE);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
    u8g2Fonts.setFont(u8g2_font_helvB12_tf);
    int w = u8g2Fonts.getUTF8Width(line);
    u8g2Fonts.setCursor((display.width() - w) / 2, display.height() / 2);
    u8g2Fonts.print(line);
    display.update();
}

// Inchide ordonat inregistrarea in curs.
//
// Fara asta, la caderea bateriei aparatul se opreste in mijlocul unei scrieri
// pe SD si poate lasa fisierul KML trunchiat sau tabela FAT corupta - adica
// exact traseul pe care tocmai l-ai inregistrat.
void stopRecordingSafely() {
    if (!isRecording) return;

    isRecording = false;
    notifyPhone("SYS_REC:0");

    if (!sdDetected || strlen(currentRecordDate) == 0) return;

    acquireSD();
    String kmlPath = "/route_" + String(currentRecordDate) + ".kml";

    char stopSuffix[32] = "";
    if ((gps_ok || simActive) && gpsTimeValid) {
        int y, mo, d, h, mi, sec;
        getLocalDateTime(y, mo, d, h, mi, sec);
        snprintf(stopSuffix, sizeof(stopSuffix), "_to_%02d-%02d", h, mi);
    } else {
        snprintf(stopSuffix, sizeof(stopSuffix), "_to_STOP");
    }

    String newKmlPath = "/route_" + String(currentRecordDate) + String(stopSuffix) + ".kml";
    SD.rename(kmlPath.c_str(), newKmlPath.c_str());
}

void enterDeepSleep8Min(bool showUI) {
    if (showUI) {
    
        display.setRotation(currentScreenRotation); 
        display.fillScreen(GxEPD_WHITE);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK); 
        u8g2Fonts.setBackgroundColor(GxEPD_WHITE); 
        u8g2Fonts.setFont(u8g2_font_helvB12_tf);
        int sw = u8g2Fonts.getUTF8Width("SHUTDOWN / SLEEP"); 
        u8g2Fonts.setCursor((display.width() - sw)/2, display.height()/2); 
        u8g2Fonts.print("SHUTDOWN / SLEEP");
        display.update(); 
    }

    display.powerDown(); 
    radio.sleep(); 
    
    uint32_t sleepTimeMs = DEEP_SLEEP_INTERVAL_MS;
    
    if(gps_ok) {
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            myGNSS.powerOff(sleepTimeMs); 
            xSemaphoreGive(i2cMutex);
        }
    }
    
    esp_sleep_enable_ext1_wakeup(1ULL << BTN_PIN, ESP_EXT1_WAKEUP_ALL_LOW);
    esp_sleep_enable_timer_wakeup((uint64_t)sleepTimeMs * 1000ULL); 

    inDeepSleepMode = true; 
    esp_deep_sleep_start();
}

void evaluateSleep() {
   
    bool manualShutdown = (raw_A && raw_B && raw_C);

    if (manualShutdown) {
        enterDeepSleep8Min(true); 
    }


    // Cat timp difuzeaza SOS, aparatul nu are voie sa adoarma.
    if (sosActive()) {
        sessionActivityTime = millis();
        return;
    }

    if (inSetupPage() || isRecording) {
        sessionActivityTime = millis(); 
        return; 
    }

    if (imu_ok && (millis() - lastPhysicalMovement < MOVEMENT_KEEPALIVE_MS)) {
        sessionActivityTime = millis();
        return;
    }

    unsigned long inactive = millis() - sessionActivityTime;
    

    if(inactive > STANDBY_AFTER_MS && currentState != STANDBY_MODE && !isRecording) {
        currentState = STANDBY_MODE; 
        display.setRotation(currentScreenRotation); 
        display.fillScreen(GxEPD_WHITE);
        display.update(); 
        
        int rectW = 160; 
        int rectH = 50; 
        display.fillRoundRect((display.width() - rectW)/2, (display.height() - rectH)/2, rectW, rectH, 8, GxEPD_BLACK);
        u8g2Fonts.setForegroundColor(GxEPD_WHITE); 
        u8g2Fonts.setBackgroundColor(GxEPD_BLACK); 
        u8g2Fonts.setFont(u8g2_font_helvB12_tf);
        
        int sw = u8g2Fonts.getUTF8Width(tr_standby[currentLang]); 
        u8g2Fonts.setCursor((display.width() - sw)/2, (display.height() - rectH)/2 + 30); 
        u8g2Fonts.print(tr_standby[currentLang]);
        
        display.update(); 

        delay(1000);
        display.powerDown();
        
        if(gps_ok) {
            if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                myGNSS.powerSaveMode(true);
                myGNSS.setMeasurementRate(GPS_RATE_STANDBY_MS);
                xSemaphoreGive(i2cMutex);
            }
        }
        
        if (deviceConnected && pServer) {
            pServer->disconnect(0);
            delay(50);
        }
        BLEDevice::deinit(true); 
        radio.sleep(); 
        setCpuFrequencyMhz(CPU_MHZ_STANDBY);

 
        while (currentState == STANDBY_MODE) {
            bool wakeUpTriggered = false;

            if (raw_A && raw_B && raw_C) {
                enterDeepSleep8Min(true);
            }

            if (latched_pA || latched_pB || latched_pC) {
                latched_pA = false; latched_pB = false; latched_pC = false;
                wakeUpTriggered = true;
            }


            if (imu_ok && (millis() - lastPhysicalMovement < STANDBY_WAKE_MOVEMENT_MS)) {
                wakeUpTriggered = true;
            }

            if (wakeUpTriggered) {
                currentState = PAGE_MENU_MAIN;
                sessionActivityTime = millis();
                break; 
            }
            
            vTaskDelay(pdMS_TO_TICKS(100));
        }

      
        setCpuFrequencyMhz(CPU_MHZ_NORMAL);
        // FIX: dupa BLEDevice::deinit() stiva BLE e complet noua, dar flag-urile
        // ramaneau pe "conectat" -> checkBLEInput() apela startAdvertising() pe un
        // pServer vechi si telefonul nu mai reusea sa se reconecteze dupa standby.
        deviceConnected = false;
        oldDeviceConnected = false;
        introMode = false;
        promptNeedsResend = false;
        pServer = NULL;
        pTxCharacteristic = NULL;
        initBLE();
        
        if(gps_ok) { 
            if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                if (currentPowerMode == NORMAL_MODE) {
                    myGNSS.powerSaveMode(false);
                    myGNSS.setMeasurementRate(GPS_RATE_NORMAL_MS);
                } else { 
                    myGNSS.powerSaveMode(true);
                    myGNSS.setMeasurementRate(GPS_RATE_SAVING_MS);
                }
                xSemaphoreGive(i2cMutex);
            }
        }
        if (currentPowerMode != STEALTH_MODE) {
            radio.standby(); 
            radio.startReceive();
            loraListening = true;
        }

        fullRefreshNeeded = true;
        requestUIUpdate = true;
    }
}

bool quickCheckActivity() {
    bool activityDetected = false;
    unsigned long checkStart = millis();

    while (millis() - checkStart < WAKE_CHECK_WINDOW_MS) {
        if (xSemaphoreTake(i2cMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            

            if (buttons_ok) {
                mButtons.update();
                if (mButtons.isPressed(0) || mButtons.isPressed(1) || mButtons.isPressed(2)) {
                    activityDetected = true;
                }
            }
            
            if (imu_ok && !activityDetected) {
                sensors_event_t a, g, tm;
                imu.getEvent(&a, &g, &tm);
                float acc_mag = sqrt(a.acceleration.x*a.acceleration.x + 
                                     a.acceleration.y*a.acceleration.y + 
                                     a.acceleration.z*a.acceleration.z);
                                     
                if (fabs(acc_mag - 9.8) > 2.0) { 
                    activityDetected = true;
                }
            }
            
            xSemaphoreGive(i2cMutex);
        }
        
        if (activityDetected) break; 
        
        delay(50); 
    }
    
   
    if (!activityDetected) {
        enterDeepSleep8Min(false); 
    }
    

    return true; 
}