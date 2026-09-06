//file name:inputs_utils.ino
void pingActivity() { 
    sessionActivityTime = millis(); 
}

double getTraveledDistance() {
    return sessionTotalDistance; 
}

bool processVirtualCommand(String cmd) {
    if (cmd.equalsIgnoreCase("bypass")) {
        currentLang = 1; 
        prefs.putInt("lang", currentLang);

        currentFreq = 868.0;
        prefs.putFloat("freq", currentFreq);
        prefs.putString("version", OS_VERSION);

        beginRadio(currentFreq);

        myName = "username";
        prefs.putString("name", myName);

        timeOffset = 2; 
        prefs.putInt("gmt", timeOffset);

        currentState = PAGE_MENU_MAIN;
        fullRefreshNeeded = false;
        requestUIUpdate = true;
        
        notifyPhone("[GLYPH] Setup BYPASSED!");
        return true;
    }

    if (cmd.startsWith("CMD_PWR:")) {
        int mode = cmd.substring(8).toInt();
        if (mode >= 0 && mode <= 2) {
            currentPowerMode = (PowerMode)mode;
            menuSelection = mode;
            
            applyPowerMode(currentPowerMode);
            
            requestUIUpdate = true;
            fullRefreshNeeded = false;
            notifyPhone("[SYS] Power Mode Updated");
        }
        return true;
    }

    if (cmd.startsWith("CMD_SEC:")) {
        String state = cmd.substring(8);
        secureMode = (state == "ON");
        requestUIUpdate = true;
        fullRefreshNeeded = false;
        notifyPhone(secureMode ? "SYS_SEC:1" : "SYS_SEC:0");
        return true;
    }

    if (cmd == "CMD_LS") {
        sendSDListing();
        return true;
    }

    if (cmd.startsWith("CMD_GET:")) {
        startFileTransfer(cmd.substring(8));
        return true;
    }

    if (cmd == "CMD_GET_ABORT") {
        abortFileTransfer(NULL);
        return true;
    }

    if (cmd.startsWith("CMD_PUT:"))  { startUpload(cmd.substring(8));  return true; }
    if (cmd.startsWith("CMD_PUTD:")) { uploadChunk(cmd.substring(9));  return true; }
    if (cmd == "CMD_PUTEND")         { finishUpload();                 return true; }
    if (cmd == "CMD_PUTABORT")       { abortUpload(NULL);              return true; }

    if (cmd.startsWith("CMD_OTA:"))  { startOta((uint32_t)cmd.substring(8).toInt()); return true; }
    if (cmd.startsWith("CMD_OTAD:")) { otaChunk(cmd.substring(9));                   return true; }
    if (cmd == "CMD_OTAEND")         { finishOta();                                  return true; }
    if (cmd == "CMD_OTAABORT")       { abortOta(NULL);                               return true; }

    if (cmd == "CMD_PING")           { sendRangePing();                              return true; }

    // Phone-side SOS stop. SOS never stops on its own, and the device buttons
    // are out of reach if the user is immobilised and the device is in a pack.
    if (cmd == "CMD_SOS_STOP")       { stopSosCompletely();                          return true; }
    if (cmd == "CMD_SOS_START")      { startSosBroadcast();                          return true; }

    if (cmd == "CMD_DIAG")           { reportDiagnostics();                          return true; }

    if (cmd == "CMD_STATE") {
        notifyPhone(secureMode ? "SYS_SEC:1" : "SYS_SEC:0"); delay(BLE_NOTIFY_GAP_MS);
        notifyPhone("SYS_TEAMNAME:" + myTeam);            delay(BLE_NOTIFY_GAP_MS);
        notifyPhone("SYS_PWR:" + String((int)currentPowerMode)); delay(BLE_NOTIFY_GAP_MS);
        notifyPhone(sdDetected ? "SYS_SD:1" : "SYS_SD:0"); delay(BLE_NOTIFY_GAP_MS);
        notifyPhone("SYS_VER:" + OS_VERSION); delay(BLE_NOTIFY_GAP_MS);
        // Chunk size in raw bytes. The phone must not guess it: anything larger
        // than the receive buffer arrives truncated and base64 decoding fails.
        // Old firmware does not answer, and the app keeps its safe default.
        notifyPhone("SYS_BUF:" + String(SD_UPLOAD_CHUNK_BYTES)); delay(BLE_NOTIFY_GAP_MS);

        // The file the device is writing the track into. The phone needs the
        // name to fetch the stretch it missed while the link was down: its own
        // copy has a hole, the card does not.
        if (isRecording && strlen(currentRecordDate) > 0) {
            notifyPhone("SYS_RECFILE:route_" + String(currentRecordDate) + ".kml");
        } else {
            notifyPhone("SYS_RECFILE:");
        }
        return true;
    }

    if (cmd.startsWith("CMD_TEAM:")) {
        setTeamName(cmd.substring(9));
        requestUIUpdate = true;
        fullRefreshNeeded = false;
        notifyPhone("[SYS] Team updated");
        return true;
    }

    if (cmd == "A@") { virt_pA = true; return true; }
    if (cmd == "B@") { virt_pB = true; return true; }
    if (cmd == "C@") { virt_pC = true; return true; }
    if (cmd == "AL@") { virt_longA = true; return true; }
    if (cmd == "BL@") { virt_longB = true; return true; }
    if (cmd == "CL@") { virt_longC = true; return true; }
    
    if (cmd == "AB@") { virt_comboAB = true; return true; }
    if (cmd == "AC@") { virt_comboAC = true; return true; }
    if (cmd == "BC@") { virt_comboBC = true; return true; }

    bool inSetup = inSetupPage();

    if (!inSetup && cmd.length() == 2 && cmd[1] == '@') {
        char c = cmd[0];
        if (c >= '1' && c <= '6') {
            if (c == '1') currentState = PAGE_MAP;
            else if (c == '2') { 
                currentState = PAGE_LORA; 
                if(currentPowerMode != STEALTH_MODE) { radio.startReceive(); loraListening = true; } 
            }
            else if (c == '3') currentState = PAGE_CLOCK;
            else if (c == '4') currentState = PAGE_MENU_TEAM;
            else if (c == '5') currentState = PAGE_MENU_POWER;
            else if (c == '6') currentState = PAGE_MENU_LANG;
            
            fullRefreshNeeded = false; 
            requestUIUpdate = true;
            pingActivity();
            return true;
        }
    }
    return false;
}

void processInitInput(String inputStr) {
    if (currentState == PAGE_INIT_LANG) {
        int l = inputStr.toInt();
        if (l >= 0 && l < LANG_COUNT) {
            currentLang = l; 
            prefs.putInt("lang", currentLang); 
            currentState = PAGE_INIT_FREQ; 
            fullRefreshNeeded = false; 
            requestUIUpdate = true; 
            notifyPhone("[GLYPH] Language set!"); 
        }
    }
    else if (currentState == PAGE_INIT_FREQ) {
        if (inputStr.indexOf("868") != -1) {
            currentFreq = 868.0; 
            prefs.putFloat("freq", currentFreq); 
            prefs.putString("version", OS_VERSION); 
            beginRadio(currentFreq);
            currentState = PAGE_INIT_NAME;
            fullRefreshNeeded = false;
            requestUIUpdate = true;
            notifyPhone("[GLYPH] 868MHz Set!");
            sendInputPrompt(tr_prompt_name[currentLang]);
        } else if (inputStr.indexOf("915") != -1) {
            currentFreq = 915.0; 
            prefs.putFloat("freq", currentFreq); 
            prefs.putString("version", OS_VERSION); 
            beginRadio(currentFreq);
            currentState = PAGE_INIT_NAME;
            fullRefreshNeeded = false;
            requestUIUpdate = true;
            notifyPhone("[GLYPH] 915MHz Set!");
            sendInputPrompt(tr_prompt_name[currentLang]);
        }
    }
    else if (currentState == PAGE_INIT_NAME) {
        myName = inputStr; 
        if(myName.length() > MAX_NAME_LEN) myName = myName.substring(0, MAX_NAME_LEN);
        prefs.putString("name", myName); 
        currentState = PAGE_INIT_TIME; 
        fullRefreshNeeded = false; 
        requestUIUpdate = true; 
        notifyPhone("[GLYPH] Saved: " + myName); 
        sendInputPrompt(tr_prompt_time[currentLang]);
    }
    else if (currentState == PAGE_INIT_TIME) {
    int offset = inputStr.toInt();
    if (offset >= -12 && offset <= 14) { 
        timeOffset = offset; 
        prefs.putInt("gmt", timeOffset); 
        currentState = PAGE_MENU_MAIN;
        fullRefreshNeeded = true;
        requestUIUpdate = true; 
        notifyPhone("[GLYPH] UTC Set!"); 
        }
    }
}

void checkSerialInput() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n'); 
        input.trim();
        while(input.startsWith("\n") || input.startsWith("\r")) { input.remove(0, 1); }
        while(input.endsWith("\n") || input.endsWith("\r")) { input.remove(input.length()-1); }
        if (input.length() == 0) return; 
        pingActivity();
        
        if (processVirtualCommand(input)) return;

        if (input == "SIMOFF") { 
            simActive = false; 
            hasGpsFix = false; 
            requestUIUpdate = true; 
            fullRefreshNeeded = false; 
            return; 
        }
        
        int commaIdx = input.indexOf(',');
        if (commaIdx > 0 && input.length() < 25 && input.substring(0, commaIdx).toFloat() != 0) {
            float parsedLat = input.substring(0, commaIdx).toFloat(); 
            float parsedLon = input.substring(commaIdx + 1).toFloat();
            if (parsedLat != 0.0 && parsedLon != 0.0) { 
                simActive = true;
                hasGpsFix = true;
                setGpsPosition(parsedLat, parsedLon, 300.0f + random(-15, 15));
                lastSIV = 12;
                requestUIUpdate = true; 
                return; 
            }
        }

        bool inSetup = inSetupPage();

        if (inSetup) { 
            processInitInput(input); 
        }
        else if (currentState == PAGE_KEYBOARD) { 
            msgDraft = input; 
            if(msgDraft.length() > MAX_MESSAGE_LEN) msgDraft = msgDraft.substring(0, MAX_MESSAGE_LEN); 
            requestUIUpdate = true; 
            fullRefreshNeeded = false; 
        }
        else if (currentState == PAGE_TEAM_NAME_EDIT) { 
            teamDraft = input;
            if(teamDraft.length() > MAX_TEAM_LEN) teamDraft = teamDraft.substring(0, MAX_TEAM_LEN);
            setTeamName(teamDraft);
            currentState = PAGE_MENU_TEAM;
            fullRefreshNeeded = false; 
            requestUIUpdate = true; 
        }
        else if (currentPowerMode != STEALTH_MODE) {
            String tmp = msgDraft;
            msgDraft = input;
            if(msgDraft.length() > MAX_MESSAGE_LEN) msgDraft = msgDraft.substring(0, MAX_MESSAGE_LEN);
            executeSendMsg();
            msgDraft = tmp;
        }
    }
}

void handleTextEditorButtons(bool pA, bool pB, bool pC, bool bLongPressed,
                             String &draft, unsigned int maxLen) {
    int nChars = (int)strlen(kbChars);

    if (pA) {
        kbCursor = (kbCursor > 0) ? kbCursor - 1 : nChars - 1;
        requestUIUpdate = true; fullRefreshNeeded = false;
    }
    if (pC) {
        kbCursor = (kbCursor < nChars - 1) ? kbCursor + 1 : 0;
        requestUIUpdate = true; fullRefreshNeeded = false;
    }
    if (pB && !bLongPressed) {
        char selected = kbChars[kbCursor];
        if (selected == '<') {
            if (draft.length() > 0) draft.remove(draft.length() - 1);
        } else if (draft.length() < maxLen) {
            draft += selected;
        }
        requestUIUpdate = true; fullRefreshNeeded = false;
    }
}

void handleLanguageButtons(bool pA, bool pB, bool pC, SystemState nextState) {
    if (pA) {
        if (tempLangSelection > 0) tempLangSelection--; else tempLangSelection = LANG_COUNT - 1;
        requestUIUpdate = true; fullRefreshNeeded = false;
    } else if (pC) {
        if (tempLangSelection < LANG_COUNT - 1) tempLangSelection++; else tempLangSelection = 0;
        requestUIUpdate = true; fullRefreshNeeded = false;
    } else if (pB) {
        currentLang = tempLangSelection;
        prefs.putInt("lang", currentLang);
        currentState = nextState;
        fullRefreshNeeded = false;
        requestUIUpdate = true;
    }
}

void handleUtcOffsetButtons(bool pA, bool pB, bool pC) {
    if (pA) {
        if (tempTimeSelection > -12) tempTimeSelection--;
        requestUIUpdate = true; fullRefreshNeeded = false;
    } else if (pC) {
        if (tempTimeSelection < 14) tempTimeSelection++;
        requestUIUpdate = true; fullRefreshNeeded = false;
    } else if (pB) {
        timeOffset = tempTimeSelection;
        prefs.putInt("gmt", timeOffset);
        currentState = PAGE_MENU_MAIN;
        fullRefreshNeeded = false;
        requestUIUpdate = true;
    }
}

void handleButtons() {
    bool cA = raw_A;
    bool cB = raw_B;
    bool cC = raw_C;
    
    if (virt_pA) { latched_pA = true; virt_pA = false; }
    if (virt_pB) { latched_pB = true; virt_pB = false; }
    if (virt_pC) { latched_pC = true; virt_pC = false; }

    bool pA = latched_pA; latched_pA = false;
    bool pB = latched_pB; latched_pB = false;
    bool pC = latched_pC; latched_pC = false;
    
    bool inSetup = inSetupPage();

    // Any press during SOS stops it for good, not just the current round.
    // Otherwise it restarts a minute later and the user thinks it is off.
    if (sosArmed() && (pA || pB || pC || cA || cB || cC)) {
        stopSosCompletely();
        latched_pA = false; latched_pB = false; latched_pC = false;
        pingActivity();
        return;
    }

    static unsigned long comboABStart = 0; 
    static bool abTriggered = false;
    
    if (virt_comboAB || (cA && cB && !cC)) {
        
        pA = false; pB = false; pC = false;
        latched_pA = false; latched_pB = false; latched_pC = false;

        if (inSetup) {
             comboABStart = 0; 
        } else {
            if (virt_comboAB) {
                abTriggered = true; 
            } else if (comboABStart == 0) {
                comboABStart = millis();
            } 
            
            if (virt_comboAB || (millis() - comboABStart > BTN_COMBO_HOLD_MS && !abTriggered)) {
                abTriggered = true; 
                virt_comboAB = false;
                
                isRecording = !isRecording;
                notifyPhone(isRecording ? "SYS_REC:1" : "SYS_REC:0");
                if(isRecording) {
                    breadcrumbIdx = 0;          
                    sessionTotalDistance = 0.0; 
                    lastLoggedLat = 0.0; 
                    lastLoggedLon = 0.0; 

                    char baseDate[32];
                    if((gps_ok || simActive) && gpsTimeValid) {
                        int y, mo, d, h, mi, sec;
                        getLocalDateTime(y, mo, d, h, mi, sec);
                        snprintf(baseDate, sizeof(baseDate), "%02d_%02d_%04d_%02d-%02d", d, mo, y, h, mi);
                    } else {
                        snprintf(baseDate, sizeof(baseDate), "OFFLINE_%lu", millis()/1000);
                    }

                    if (sdDetected) {
                            acquireSD();
                            

                            String finalName = baseDate;
                            int fileIndex = 1;
                            
                            while (SD.exists(("/route_" + finalName + ".kml").c_str())) {
                                finalName = String(baseDate) + "-" + String(fileIndex);
                                fileIndex++;
                            }
                            
      
                            // strncpy writes no '\0' when the source fills the buffer.
                            strncpy(currentRecordDate, finalName.c_str(), sizeof(currentRecordDate) - 1);
                            currentRecordDate[sizeof(currentRecordDate) - 1] = '\0';


                            String kmlPath = "/route_" + String(currentRecordDate) + ".kml";
                            File f = SD.open(kmlPath, FILE_WRITE);
                            if (f) {
                                f.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
                                f.println("<kml xmlns=\"http://www.opengis.net/kml/2.2\">");
                                f.println("  <Document>");
                                f.println("    <name>GLYPH " + String(currentRecordDate) + "</name>");
                                f.print("    <Placemark><LineString><coordinates>\n");
                                f.print(KML_CLOSING); 
                                f.close();
                            }

                            
                        }else {
                            strncpy(currentRecordDate, baseDate, sizeof(currentRecordDate) - 1);
                            currentRecordDate[sizeof(currentRecordDate) - 1] = '\0';
                        }

                } else {
                    if (sdDetected) {
                        acquireSD();
                        String kmlPath = "/route_" + String(currentRecordDate) + ".kml";
                        
                        char stopSuffix[32] = "";
                        if((gps_ok || simActive) && gpsTimeValid) {
                            int y, mo, d, h, mi, sec;
                            getLocalDateTime(y, mo, d, h, mi, sec);
                            snprintf(stopSuffix, sizeof(stopSuffix), "_to_%02d-%02d", h, mi);
                        } else {
                            snprintf(stopSuffix, sizeof(stopSuffix), "_to_STOP");
                        }

                        String newKmlPath = "/route_" + String(currentRecordDate) + String(stopSuffix) + ".kml";

                        SD.rename(kmlPath.c_str(), newKmlPath.c_str());
                    }
                }
                
                display.setRotation(currentScreenRotation); 
                display.fillScreen(GxEPD_WHITE); 
                display.fillRect(0, 0, display.width(), 20, GxEPD_BLACK);
                u8g2Fonts.setForegroundColor(GxEPD_BLACK); 
                u8g2Fonts.setBackgroundColor(GxEPD_WHITE); 
                u8g2Fonts.setFont(u8g2_font_helvB12_tf);
                
                String recTxt = isRecording ? tr_route_start[currentLang] : tr_route_stop[currentLang];
                u8g2Fonts.setCursor((display.width() - u8g2Fonts.getUTF8Width(recTxt.c_str()))/2, 60); 
                u8g2Fonts.print(recTxt.c_str());
                display.updateWindow(0, 0, display.width(), display.height(), true); 
                
                delay(500); 
                
                requestUIUpdate = true; 
                fullRefreshNeeded = false;
                pingActivity();
            }
        }
        return; 
    } else { 
        comboABStart = 0; 
        abTriggered = false; 
    }

    static unsigned long comboBCStart = 0;
    static bool bcTriggered = false;
    if (virt_comboBC || (cB && cC && !cA)) {
        pA = false; pB = false; pC = false;
        latched_pA = false; latched_pB = false; latched_pC = false;

        if (virt_comboBC) { bcTriggered = true; } 
        else if (comboBCStart == 0) { comboBCStart = millis(); } 
        
        if (virt_comboBC || (millis() - comboBCStart > BTN_COMBO_HOLD_MS && !bcTriggered)) {
            bcTriggered = true;
            virt_comboBC = false;
            
            notifyPhone("[SYS] SOS BROADCASTING!");

            display.setRotation(currentScreenRotation);
            display.fillScreen(GxEPD_WHITE);
            display.fillRect(0, 0, display.width(), 20, GxEPD_BLACK);
            u8g2Fonts.setForegroundColor(GxEPD_BLACK);
            u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
            u8g2Fonts.setFont(u8g2_font_helvB12_tf);
            u8g2Fonts.setCursor((display.width() - u8g2Fonts.getUTF8Width("BROADCASTING SOS"))/2, 50);
            u8g2Fonts.print("BROADCASTING SOS");
            u8g2Fonts.setFont(u8g2_font_helvB10_tf);
            u8g2Fonts.setCursor((display.width() - u8g2Fonts.getUTF8Width("FOR 10 SECONDS..."))/2, 80);
            u8g2Fonts.print("FOR 10 SECONDS...");
            display.updateWindow(0, 0, display.width(), display.height(), true);

            startSosBroadcast();

            requestUIUpdate = true; fullRefreshNeeded = false; pingActivity();
        }
        return; 
    } else { comboBCStart = 0; bcTriggered = false; }

    static unsigned long comboACStart = 0;
    static bool acTriggered = false;
    if (virt_comboAC || (cA && cC && !cB)) {
        
        pA = false; pB = false; pC = false;
        latched_pA = false; latched_pB = false; latched_pC = false;

        if (inSetup) {
             comboACStart = 0; 
        } else {
            if (virt_comboAC) {
                acTriggered = true;
            } else if (comboACStart == 0) {
                comboACStart = millis();
            } 
            
            if (virt_comboAC || (millis() - comboACStart > BTN_COMBO_HOLD_MS && !acTriggered)) {
                acTriggered = true;
                virt_comboAC = false;

                // A+C is shutdown, not SOS. The README and the phone app's
                // Shutdown button both send exactly "AC@" and expect deep sleep.
                notifyPhone("[SYS] Shutting down...");
                enterDeepSleep8Min(true);
                // enterDeepSleep8Min() never returns.
            }
        }
        return; 
    } else {
        comboACStart = 0; 
        acTriggered = false;
    }

    static unsigned long holdMapExitA = 0;
    static bool handledLongA = false;
    if (currentState == PAGE_KML_LIST) {
        if (cA || virt_longA) {
            if (virt_longA) {
                handledLongA = true;
                currentState = PAGE_MAP; 
                fullRefreshNeeded = false; 
                requestUIUpdate = true; 
                pingActivity();
                virt_longA = false;
            } else {
                if (holdMapExitA == 0) { 
                    holdMapExitA = millis(); 
                    handledLongA = false; 
                } else if (!handledLongA && millis() - holdMapExitA > BTN_KML_EXIT_HOLD_MS) { 
                    handledLongA = true; 
                    currentState = PAGE_MAP; 
                    fullRefreshNeeded = false; 
                    requestUIUpdate = true; 
                    pingActivity();
                }
            }
        } else {
            if (holdMapExitA > 0 && !handledLongA) { 
                if (kmlSelectionIndex > 0) kmlSelectionIndex--; 
                else kmlSelectionIndex = kmlFileCount - 1; 
                requestUIUpdate = true;
                fullRefreshNeeded = false; 
                pingActivity(); 
            }
            holdMapExitA = 0; 
            handledLongA = false;
        }
        
        if (pC) { 
            if (kmlSelectionIndex < kmlFileCount - 1) kmlSelectionIndex++; 
            else kmlSelectionIndex = 0; 
            requestUIUpdate = true;
            fullRefreshNeeded = false; 
            pingActivity(); 
        }
        if (pB) {
            if (kmlSelectionIndex == 0) { 
                currentKmlOverlay = ""; 
                kmlCacheValid = false; 
            } else { 
                currentKmlOverlay = "/" + kmlFiles[kmlSelectionIndex]; 
                loadKMLCache(); 
            }
            currentState = PAGE_MAP; 
            fullRefreshNeeded = false; 
            requestUIUpdate = true; 
            pingActivity();
        }
        return; 
    }

    static unsigned long bTimer = 0; 
    static bool bLongPressed = false;
    static unsigned long lastDeleteTime = 0;

    if ((cB && !pB) || virt_longB) {
        if (virt_longB) {
            bLongPressed = true; 
            if (currentState == PAGE_KEYBOARD && msgDraft.length() > 0) msgDraft.remove(msgDraft.length()-1);
            else if (currentState == PAGE_TEAM_NAME_EDIT && teamDraft.length() > 0) teamDraft.remove(teamDraft.length()-1);
            requestUIUpdate = true; 
            fullRefreshNeeded = false; 
            pingActivity(); 
            virt_longB = false;
        } else {
            if (bTimer == 0) { 
                bTimer = millis(); 
                bLongPressed = false; 
            } else if (millis() - bTimer > BTN_DELETE_HOLD_MS) { 
                bLongPressed = true; 
                
                if ((currentState == PAGE_KEYBOARD || currentState == PAGE_TEAM_NAME_EDIT) && (millis() - lastDeleteTime > BTN_DELETE_REPEAT_MS)) {
                    lastDeleteTime = millis();
                    if (currentState == PAGE_KEYBOARD && msgDraft.length() > 0) msgDraft.remove(msgDraft.length()-1);
                    else if (currentState == PAGE_TEAM_NAME_EDIT && teamDraft.length() > 0) teamDraft.remove(teamDraft.length()-1);
                    requestUIUpdate = true; 
                    fullRefreshNeeded = false; 
                    pingActivity();
                }
            }
        }
    } else if (!cB) { 
        bTimer = 0; 
        bLongPressed = false; 
        lastDeleteTime = 0;
    }

    static unsigned long aTimer = 0;
    if ((cA && !pA) || virt_longA) {
        if (virt_longA) {
            if (currentState == PAGE_KEYBOARD) { 
                msgDraft = ""; currentState = PAGE_LORA; fullRefreshNeeded = false; requestUIUpdate = true; pingActivity(); 
            } else if (currentState == PAGE_TEAM_NAME_EDIT) { 
                currentState = PAGE_MENU_TEAM; fullRefreshNeeded = false; requestUIUpdate = true; pingActivity(); 
            }
            virt_longA = false;
        } else {
            if(aTimer == 0) aTimer = millis();
            if(millis() - aTimer > BTN_LONG_PRESS_MS) { 
                if (currentState == PAGE_KEYBOARD) { 
                    msgDraft = ""; 
                    currentState = PAGE_LORA; 
                    fullRefreshNeeded = false; 
                    requestUIUpdate = true; 
                    aTimer = 0; 
                    pingActivity(); 
                } else if (currentState == PAGE_TEAM_NAME_EDIT) { 
                    currentState = PAGE_MENU_TEAM; 
                    fullRefreshNeeded = false; 
                    requestUIUpdate = true; 
                    aTimer = 0; 
                    pingActivity(); 
                }
            }
        }
    } else if (!cA) {
        aTimer = 0;
    }

    static unsigned long cTimer = 0;
    if ((cC && !pC) || virt_longC) {
        if (virt_longC) {
            if (currentState == PAGE_KEYBOARD) { 
                executeSendMsg(); 
            } else if (currentState == PAGE_TEAM_NAME_EDIT) {
                setTeamName(teamDraft);
                currentState = PAGE_MENU_TEAM;
                fullRefreshNeeded = false;
                requestUIUpdate = true;
                pingActivity();
            }
            virt_longC = false;
        } else {
            if(cTimer == 0) cTimer = millis();
            if(millis() - cTimer > BTN_LONG_PRESS_MS) { 
                if (currentState == PAGE_KEYBOARD) { 
                    cTimer = 0; 
                    executeSendMsg();
                } else if (currentState == PAGE_TEAM_NAME_EDIT) {
                    setTeamName(teamDraft);
                    currentState = PAGE_MENU_TEAM;
                    fullRefreshNeeded = false; 
                    requestUIUpdate = true; 
                    cTimer = 0; 
                    pingActivity(); 
                }
            }
        }
    } else if (!cC) {
        cTimer = 0;
    }

    if (!pA && !pB && !pC) return;
    
    pingActivity(); 
    
    if (currentState == STANDBY_MODE) { 
        currentState = PAGE_MENU_MAIN; 
        fullRefreshNeeded = false; 
        requestUIUpdate = true; 
        return; 
    }

    if (currentState == PAGE_INIT_LANG) {
        handleLanguageButtons(pA, pB, pC, PAGE_INIT_FREQ);
    }
    else if (currentState == PAGE_INIT_FREQ) {
        if (pA) { currentFreq = 868.0; requestUIUpdate = true; fullRefreshNeeded = false; } 
        else if (pC) { currentFreq = 915.0; requestUIUpdate = true; fullRefreshNeeded = false; }
        else if (pB) { 
            prefs.putFloat("freq", currentFreq); 
            prefs.putString("version", OS_VERSION); 
            // beginRadio(), not hand-set SF/power. Setting them here once left
            // button-configured devices unable to talk to serial-configured ones.
            beginRadio(currentFreq);
            currentState = PAGE_INIT_NAME;
            fullRefreshNeeded = false; 
            requestUIUpdate = true; 
            sendInputPrompt(tr_prompt_name[currentLang]); 
        }
    }
    else if (currentState == PAGE_INIT_TIME) {
        handleUtcOffsetButtons(pA, pB, pC);
    }
    // else if (currentState == PAGE_INIT_COMPASS) {
    //     if (pA || pB || pC) { 
    //         currentState = PAGE_MENU_MAIN; 
    //         fullRefreshNeeded = false; 
    //         requestUIUpdate = true; 
    //     }
    // }
    else if (currentState == PAGE_MENU_MAIN) {
        if (pA) { if (mainMenuItem > 0) mainMenuItem--; else mainMenuItem = 6; requestUIUpdate = true; fullRefreshNeeded = false; } 
        else if (pC) { if (mainMenuItem < 6) mainMenuItem++; else mainMenuItem = 0; requestUIUpdate = true; fullRefreshNeeded = false; } 
        else if (pB) { 
            if (mainMenuItem == 0) currentState = PAGE_MAP; 
            else if (mainMenuItem == 1) { 
                currentState = PAGE_LORA; 
                if(currentPowerMode!=STEALTH_MODE){
                    radio.startReceive(); 
                    loraListening = true;
                } 
            } 
            else if (mainMenuItem == 2) currentState = PAGE_CLOCK; 
            else if (mainMenuItem == 3) { currentState = PAGE_MENU_TEAM; } 
            else if (mainMenuItem == 4) currentState = PAGE_MENU_POWER; 
            else if (mainMenuItem == 5) currentState = PAGE_MENU_LANG;
            else if (mainMenuItem == 6) { currentState = PAGE_MENU_TIME; tempTimeSelection = timeOffset; }
            fullRefreshNeeded = false; 
            requestUIUpdate = true;
        }
    }
    else if (currentState == PAGE_MAP) {
        if (pA) { currentState = PAGE_MENU_MAIN; fullRefreshNeeded = false; requestUIUpdate = true; } 
        if (pC) { scanKMLFiles(); currentState = PAGE_KML_LIST; fullRefreshNeeded = false; requestUIUpdate = true; }
        if (pB) { mapZoomLevel = (mapZoomLevel + 1) % 2; fullRefreshNeeded = false; requestUIUpdate = true; }
    }
    else if (currentState == PAGE_CLOCK) { 
        if (pA) { currentState = PAGE_MENU_MAIN; fullRefreshNeeded = false; requestUIUpdate = true; } 
    }
    else if (currentState == PAGE_LORA) {
        if (pB) { currentState = PAGE_KEYBOARD; fullRefreshNeeded = false; requestUIUpdate = true; sendInputPrompt(tr_prompt_msg[currentLang]); }
        if (pA) { currentState = PAGE_MENU_MAIN; fullRefreshNeeded = false; requestUIUpdate = true; }
        if (pC) { secureMode = !secureMode; fullRefreshNeeded = false; requestUIUpdate = true; }
    }
    else if (currentState == PAGE_KEYBOARD) {
        handleTextEditorButtons(pA, pB, pC, bLongPressed, msgDraft, MAX_MESSAGE_LEN);
    }
    else if (currentState == PAGE_MENU_TEAM) {
        if (pA) { currentState = PAGE_MENU_MAIN; fullRefreshNeeded = false; requestUIUpdate = true; }
        if (pB) { 
            currentState = PAGE_TEAM_NAME_EDIT; 
            teamDraft = myTeam; 
            kbCursor = 0; 
            sendInputPrompt(tr_prompt_team[currentLang]); 
            fullRefreshNeeded = false; 
            requestUIUpdate = true; 
        }
    }
    else if (currentState == PAGE_TEAM_NAME_EDIT) {
        handleTextEditorButtons(pA, pB, pC, bLongPressed, teamDraft, MAX_TEAM_LEN);
    }
    else if (currentState == PAGE_MENU_POWER) {
        if (pA) { if (menuSelection > 0) menuSelection--; else menuSelection = 2; requestUIUpdate = true; fullRefreshNeeded = false; } 
        else if (pC) { if (menuSelection < 2) menuSelection++; else menuSelection = 0; requestUIUpdate = true; fullRefreshNeeded = false; }
        else if (pB) { 
            currentPowerMode = (PowerMode)menuSelection; 
            
            applyPowerMode(currentPowerMode);
            
            currentState = PAGE_MENU_MAIN; 
            fullRefreshNeeded = false; 
            requestUIUpdate = true; 
        }
    }
    else if (currentState == PAGE_MENU_LANG) {
        handleLanguageButtons(pA, pB, pC, PAGE_MENU_MAIN);
    }
    else if (currentState == PAGE_MENU_TIME) {
        handleUtcOffsetButtons(pA, pB, pC);
    }
}

// ---------------------------------------------------------------------------
// SOS broadcast
// ---------------------------------------------------------------------------
//
// A state machine, not a blocking burst. startSosBroadcast() only arms it;
// serviceSosBroadcast() runs from loop() and sends at most one packet per pass,
// so buttons and screen keep working and an accidental SOS can be cancelled.
// The old version transmitted inside a 10 second while loop, during which
// loop() never ran.

static bool          sosRunning     = false;
static unsigned long sosStartedAt   = 0;
static unsigned long sosLastPacket  = 0;
static int           sosPacketCount = 0;
static String        sosPayload     = "";
static String        sosDisplayMsg  = "";

// SOS does not stop after the first burst. It goes to standby and repeats on
// widening intervals until the user cancels or the battery dies. A ten second
// SOS that lands while nobody is listening is a lost SOS.
static bool          sosStandby     = false;   // waiting between rounds
static unsigned long sosNextRepeat  = 0;
static int           sosRepeatIndex = 0;
static int           sosTotalRounds = 0;

// Armed covers the wait between rounds too, so the device neither sleeps nor
// shuts the radio down in the gaps.
bool sosActive()  { return sosRunning; }
bool sosArmed()   { return sosRunning || sosStandby; }
int  sosRounds()  { return sosTotalRounds; }

void startSosBroadcast() {
    if (currentPowerMode == STEALTH_MODE) {
        notifyPhone("[SYS] SOS blocked: STEALTH mode");
        return;
    }
    if (!health.radioOk) {
        notifyPhone("[SYS] SOS failed: radio unavailable");
        pushLoraHistory("!! [" + formatLocalTime() + "] SOS NOT SENT - NO RADIO");
        return;
    }
    if (sosRunning) return;

    double sLat, sLon;
    getGpsPosition(sLat, sLon);

    sosDisplayMsg = myName + " SOS! LAT:" + String(sLat, 5) + " LON:" + String(sLon, 5);
    sosPayload    = sosDisplayMsg + "|" + String(sLat, 5) + "," + String(sLon, 5);

    // Stay on the system spreading factor. An older version switched to SF12 and
    // never set it back, so after an SOS the device transmitted on an SF nobody
    // was listening to, until reboot.
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);

    sosRunning     = true;
    sosStandby     = false;
    sosStartedAt   = millis();
    sosLastPacket  = 0;
    sosPacketCount = 0;
    sosRepeatIndex = 0;
    sosTotalRounds = 1;

    notifyPhone("SYS_SOS:1|1");
    notifyPhone("[SYS] SOS BROADCASTING!");
}

// A repeat carries the position as of now. An hour-old position sends people
// where the user no longer is.
static void restartSosRound() {
    double sLat, sLon;
    getGpsPosition(sLat, sLon);
    sosDisplayMsg = myName + " SOS! LAT:" + String(sLat, 5) + " LON:" + String(sLon, 5)
                  + " BAT:" + String(getBatteryPercent()) + "%";
    sosPayload    = sosDisplayMsg + "|" + String(sLat, 5) + "," + String(sLon, 5);

    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
    sosRunning     = true;
    sosStandby     = false;
    sosStartedAt   = millis();
    sosLastPacket  = 0;
    sosPacketCount = 0;
    sosTotalRounds++;
    notifyPhone("SYS_SOS:1|" + String(sosTotalRounds));
}

void stopSosCompletely() {
    if (!sosRunning && !sosStandby) return;
    cancelSosBroadcast(true);
    sosStandby = false;
    sosRepeatIndex = 0;
    notifyPhone("SYS_SOS:0|" + String(sosTotalRounds));
    notifyPhone("[SYS] SOS stopped after " + String(sosTotalRounds) + " rounds");
}

void cancelSosBroadcast(bool byUser) {
    if (!sosRunning) return;
    sosRunning = false;

    String note = byUser ? " (cancelled after x" : " (x";
    String screenMsg = ">> [" + formatLocalTime() + "] " + sosDisplayMsg +
                       note + String(sosPacketCount) + ")";
    pushLoraHistory(screenMsg);
    logLoraMessage(screenMsg, secureMode);
    notifyPhone("[TX SOS]: " + sosDisplayMsg);

    radio.startReceive();
    loraListening = true;

    requestUIUpdate = true;
    fullRefreshNeeded = false;
}

void serviceSosBroadcast() {
    if (sosStandby) {
        if ((int32_t)(millis() - sosNextRepeat) >= 0) restartSosRound();
        return;
    }

    if (!sosRunning) return;

    if (millis() - sosStartedAt >= SOS_BROADCAST_MS) {
        cancelSosBroadcast(false);

        // Not finished - it waits for the next round. Intervals widen: frequent
        // at first, when someone is likelier to be in range, then rarer so the
        // battery lasts for hours.
        int idx = sosRepeatIndex;
        if (idx >= SOS_REPEAT_STEPS) idx = SOS_REPEAT_STEPS - 1;
        sosNextRepeat = millis() + (unsigned long)SOS_REPEAT_SECONDS[idx] * 1000UL;
        if (sosRepeatIndex < SOS_REPEAT_STEPS - 1) sosRepeatIndex++;
        sosStandby = true;
        notifyPhone("SYS_SOS:2|" + String(SOS_REPEAT_SECONDS[idx]));
        return;
    }

    if (sosLastPacket != 0 && millis() - sosLastPacket < SOS_PACKET_GAP_MS) return;

    String packet = buildPacket(MSG_SOS, sosPayload, secureMode, MESH_HOPS_SOS);
    if (packet.length() == 0) {          // encryption failed
        sosRunning = false;
        notifyPhone("[SYS] SOS encryption failed");
        return;
    }

    radio.standby();
    int st = radio.transmit(packet);
    if (st == RADIOLIB_ERR_NONE) sosPacketCount++;

    sosLastPacket = millis();
}
