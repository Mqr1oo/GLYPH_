//file name:ble_phone.ino
#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        BLEDevice::startAdvertising(); 
    };

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
    }
};

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
        // auto: getValue() intoarce std::string pe core 2.x si String pe core 3.x
        auto rxValue = pCharacteristic->getValue();
        const char* src = rxValue.c_str();
        size_t n = rxValue.length();
        if (n == 0) return;
        if (n >= BLE_RX_BUF_SIZE) n = BLE_RX_BUF_SIZE - 1;

        // Doar memcpy in sectiunea critica - fara alocari de memorie.
        portENTER_CRITICAL(&bleMux);
        memcpy((void*)bleRxBuffer, src, n);
        bleRxBuffer[n] = '\0';
        bleDataReceived = true;
        portEXIT_CRITICAL(&bleMux);
    }
};

void initBLE() {
    BLEDevice::init("GLYPH");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());
    BLEService *pService = pServer->createService(SERVICE_UUID);
    pTxCharacteristic = pService->createCharacteristic(
                            CHARACTERISTIC_UUID_TX,
                            BLECharacteristic::PROPERTY_NOTIFY
                        );
    pTxCharacteristic->addDescriptor(new BLE2902());
    BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                                CHARACTERISTIC_UUID_RX,
                                                BLECharacteristic::PROPERTY_WRITE
                                            );
    pRxCharacteristic->setCallbacks(new MyCallbacks());
    pService->start();
    pServer->getAdvertising()->start();
}

void notifyPhone(String msg) { 
    if (deviceConnected && pTxCharacteristic) {
        pTxCharacteristic->setValue(msg.c_str());
        pTxCharacteristic->notify();
    }
}

void sendTelemetryBLE() {
    static unsigned long lastBleTelemetry = 0;
    
    if (deviceConnected && millis() - lastBleTelemetry > BLE_TELEMETRY_INTERVAL_MS) {
        lastBleTelemetry = millis();

        // Buffere fixe in loc de concatenari de String. Blocul asta ruleaza la
        // fiecare 5 secunde cat timp telefonul e conectat - adica de sute de ori
        // pe ora - deci era una dintre principalele surse de fragmentare a
        // heap-ului, care se manifesta ca un reset "aleator" dupa multe ore.
        char buf[64];

        // Pauzele exista fiindca stiva BLE are o coada mica: sase notificari
        // trimise una dupa alta se pierdeau pe drum.
        snprintf(buf, sizeof(buf), "SYS_BATT:%d", getBatteryPercent());
        notifyPhone(buf); delay(BLE_NOTIFY_GAP_MS);

        snprintf(buf, sizeof(buf), "SYS_SATS:%u", (unsigned)lastSIV);
        notifyPhone(buf); delay(BLE_NOTIFY_GAP_MS);

        if (hasGpsFix || simActive) {
            double bLat, bLon;
            getGpsPosition(bLat, bLon);
            snprintf(buf, sizeof(buf), "SYS_GPS:%.6f,%.6f", bLat, bLon);
        } else {
            // Aplicatia cauta exact textul asta; firmware-ul nu il trimitea niciodata.
            snprintf(buf, sizeof(buf), "SYS_GPS:NO FIX");
        }
        notifyPhone(buf); delay(BLE_NOTIFY_GAP_MS);

        snprintf(buf, sizeof(buf), "SYS_ENV:%.1f,%.1f", currentTemp, currentHum);
        notifyPhone(buf); delay(BLE_NOTIFY_GAP_MS);

        if (gpsTimeValid) {
            snprintf(buf, sizeof(buf), "SYS_TIME:%s", formatLocalTime().c_str());
            notifyPhone(buf); delay(BLE_NOTIFY_GAP_MS);
        }

        // Ce nu merge, raportat si catre telefon - nu doar pe ecran.
        String bad = healthBadge();
        snprintf(buf, sizeof(buf), "SYS_HEALTH:%s", bad.length() ? bad.c_str() : "OK");
        notifyPhone(buf); delay(BLE_NOTIFY_GAP_MS);

        notifyPhone(isRecording ? "SYS_REC:1" : "SYS_REC:0");
    }
}

void sendInputPrompt(String msg) { 
    notifyPhone(">> " + msg); 
}

void checkBLEInput() {
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = true;
        phoneConnectedTime = millis();
        introMode = true;
        promptNeedsResend = true;

        notifyPhone(isRecording ? "SYS_REC:1" : "SYS_REC:0");
    }
    
    if (!deviceConnected && oldDeviceConnected) {
        delay(500);
        if (pServer) pServer->startAdvertising();
        oldDeviceConnected = false;
        introMode = false;
        promptNeedsResend = false;
    }

    if (introMode && (millis() - phoneConnectedTime > 10000)) {
        introMode = false;
    }

    if (bleDataReceived) {
        char localBuf[BLE_RX_BUF_SIZE];
        portENTER_CRITICAL(&bleMux);
        memcpy(localBuf, (const void*)bleRxBuffer, BLE_RX_BUF_SIZE);
        bleRxBuffer[0] = '\0';
        bleDataReceived = false;
        portEXIT_CRITICAL(&bleMux);

        localBuf[BLE_RX_BUF_SIZE - 1] = '\0';
        String btMsg = String(localBuf);
        btMsg.trim();
        if (btMsg.length() == 0) return;

        if (introMode) introMode = false;
        if (btMsg == "INPUT") return;
        
        pingActivity();
        
        if (!processVirtualCommand(btMsg)) {
            bool inSetup = inSetupPage();
            
            if (inSetup) {
                processInitInput(btMsg);
            }
            else if (currentState == PAGE_KEYBOARD) { 
                msgDraft = btMsg; 
                if(msgDraft.length() > MAX_MESSAGE_LEN) msgDraft = msgDraft.substring(0, MAX_MESSAGE_LEN); 
                requestUIUpdate = true; 
                fullRefreshNeeded = false; 
            }
            else if (currentState == PAGE_TEAM_NAME_EDIT) {
                teamDraft = btMsg;
                if(teamDraft.length() > MAX_TEAM_LEN) teamDraft = teamDraft.substring(0, MAX_TEAM_LEN);
                setTeamName(teamDraft);
                currentState = PAGE_MENU_TEAM;
                fullRefreshNeeded = true; 
                requestUIUpdate = true; 
            }
            else if (currentPowerMode != STEALTH_MODE) {
                String tmp = msgDraft;
                msgDraft = btMsg;
                if(msgDraft.length() > MAX_MESSAGE_LEN) msgDraft = msgDraft.substring(0, MAX_MESSAGE_LEN);
                executeSendMsg();
                msgDraft = tmp;
            }
        }
    }
}