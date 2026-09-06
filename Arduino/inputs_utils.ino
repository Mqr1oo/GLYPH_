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
        // Inainte trimiteam "[SYS] Secure Mode: ON", iar aplicatia il afisa ca
        // pe un mesaj in chat - la fiecare comutare, doua randuri de gunoi in
        // conversatie. Acum e o notificare de stare, pe care aplicatia o
        // foloseste ca sa aprinda butonul si insigna din antet.
        notifyPhone(secureMode ? "SYS_SEC:1" : "SYS_SEC:0");
        return true;
    }

    // --- cardul SD, vazut de pe telefon ---
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

    // --- ruta planificata pe telefon, scrisa pe cardul aparatului ---
    if (cmd.startsWith("CMD_PUT:"))  { startUpload(cmd.substring(8));  return true; }
    if (cmd.startsWith("CMD_PUTD:")) { uploadChunk(cmd.substring(9));  return true; }
    if (cmd == "CMD_PUTEND")         { finishUpload();                 return true; }
    if (cmd == "CMD_PUTABORT")       { abortUpload(NULL);              return true; }

    // --- actualizare firmware prin Bluetooth ---
    if (cmd.startsWith("CMD_OTA:"))  { startOta((uint32_t)cmd.substring(8).toInt()); return true; }
    if (cmd.startsWith("CMD_OTAD:")) { otaChunk(cmd.substring(9));                   return true; }
    if (cmd == "CMD_OTAEND")         { finishOta();                                  return true; }
    if (cmd == "CMD_OTAABORT")       { abortOta(NULL);                               return true; }

    // --- masurarea razei ---
    if (cmd == "CMD_PING")           { sendRangePing();                              return true; }

    // Oprirea SOS-ului de pe telefon. Exista pentru ca SOS-ul nu se mai
    // opreste singur: daca esti imobilizat si aparatul e in rucsac, butoanele
    // lui nu-ti sunt de niciun folos.
    if (cmd == "CMD_SOS_STOP")       { stopSosCompletely();                          return true; }
    if (cmd == "CMD_SOS_START")      { startSosBroadcast();                          return true; }

    // --- diagnostic: ce raspunde pe magistrala I2C ---
    if (cmd == "CMD_DIAG")           { reportDiagnostics();                          return true; }

    // Telefonul cere starea imediat dupa conectare, ca sa nu astepte ciclul de
    // telemetrie de 5 secunde ca sa afle in ce mod e aparatul.
    if (cmd == "CMD_STATE") {
        notifyPhone(secureMode ? "SYS_SEC:1" : "SYS_SEC:0"); delay(BLE_NOTIFY_GAP_MS);
        notifyPhone("SYS_TEAMNAME:" + myTeam);            delay(BLE_NOTIFY_GAP_MS);
        notifyPhone("SYS_PWR:" + String((int)currentPowerMode)); delay(BLE_NOTIFY_GAP_MS);
        notifyPhone(sdDetected ? "SYS_SD:1" : "SYS_SD:0"); delay(BLE_NOTIFY_GAP_MS);
        // Versiunea, ca telefonul sa poata spune daca aparatul a ramas in urma
        // fata de binarul publicat pe site.
        notifyPhone("SYS_VER:" + OS_VERSION);
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

// Cele doua editoare de text (mesaj si nume de echipa) aveau blocuri identice
// de tratare a butoanelor; difereau doar prin variabila editata si prin limita
// de lungime.
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

// Selectorul de limba apare de doua ori: la prima pornire si in meniu.
// Singura diferenta e pagina catre care se iese dupa confirmare.
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

// La fel pentru selectorul de fus orar - ambele variante ies in meniul principal.
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

    // Orice apasare in timpul difuzarii SOS o opreste. Inainte nu aveai cum:
    // bucla de 10 secunde nu returna in loop(), deci butoanele nu erau citite.
    // Orice apasare in timpul SOS il opreste definitiv, nu doar runda curenta:
    // altfel ar reporni singur peste un minut si omul ar crede ca l-a oprit.
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
                            
      
                            // FIX: strncpy nu pune '\0' daca sursa umple bufferul.
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

            // Difuzarea nu se mai face intr-o bucla care tine aparatul ostatic
            // 10 secunde. startSosBroadcast() doar porneste, iar
            // serviceSosBroadcast() trimite cate un pachet pe tura de loop().
            // Intre pachete, butoanele si ecranul raspund normal - si poti anula.
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

                // FIX FUNCTIONAL MAJOR: A+C facea tot un SOS, identic cu B+C.
                // README-ul si butonul "Shutdown" din aplicatia de telefon (care
                // trimite exact "AC@") asteapta deep sleep. Apasarea butonului de
                // shutdown din telefon declansa in realitate un SOS pe 10 secunde.
                notifyPhone("[SYS] Shutting down...");
                enterDeepSleep8Min(true);
                // enterDeepSleep8Min() nu se intoarce niciodata.
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
            // FIX: aici se seta SF9 / 17 dBm, iar peste tot in rest SF11 / 22 dBm.
            // Un aparat configurat de la butoane nu putea comunica cu unul
            // configurat prin serial/telefon.
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
// Difuzarea SOS
// ---------------------------------------------------------------------------
//
// Varianta veche era o bucla while care transmitea 10 secunde. In tot acel timp
// loop() nu mai rula: butoanele nu raspundeau, ecranul era inghetat si nu aveai
// cum sa opresti difuzarea daca ai apasat din greseala.
//
// Acum e o stare: startSosBroadcast() o porneste, serviceSosBroadcast() e
// apelata din loop() si trimite cel mult un pachet pe tura. Intre pachete,
// restul sistemului merge normal.

static bool          sosRunning     = false;
static unsigned long sosStartedAt   = 0;
static unsigned long sosLastPacket  = 0;
static int           sosPacketCount = 0;
static String        sosPayload     = "";
static String        sosDisplayMsg  = "";

// SOS-ul nu se mai opreste dupa rafala initiala. Intra intr-o stare de veghe si
// se repeta la intervale care cresc, pana il anulezi tu sau moare bateria. Un
// SOS de zece secunde care prinde exact momentul in care nimeni nu asculta e un
// SOS pierdut - iar cine il asteapta nu are de unde sti asta.
static bool          sosStandby     = false;   // in pauza intre reluari
static unsigned long sosNextRepeat  = 0;
static int           sosRepeatIndex = 0;
static int           sosTotalRounds = 0;

// Aparatul e "in SOS" si cat timp asteapta urmatoarea reluare: asa nu adoarme
// si nu isi stinge radioul intre reluari.
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

    // Ramanem pe acelasi spreading factor ca restul sistemului. Varianta veche
    // trecea pe SF12 si nu il mai punea niciodata inapoi, deci dupa un SOS
    // aparatul transmitea pe un SF pe care nimeni nu il asculta, pana la reboot.
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

// Reluare: acelasi text, dar cu pozitia de ACUM. Daca te-ai miscat sau ai
// prins fix intre timp, reluarea cara informatia noua - o pozitie veche de o
// ora trimite oamenii unde nu mai esti.
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

// Oprirea definitiva, ceruta de om.
void stopSosCompletely() {
    if (!sosRunning && !sosStandby) return;
    cancelSosBroadcast(true);
    sosStandby = false;
    sosRepeatIndex = 0;
    notifyPhone("SYS_SOS:0|" + String(sosTotalRounds));
    notifyPhone("[SYS] SOS stopped after " + String(sosTotalRounds) + " rounds");
}

// Oprire manuala: orice apasare in timpul difuzarii.
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
    // In pauza dintre reluari: asteptam scadenta si repornim.
    if (sosStandby) {
        if ((int32_t)(millis() - sosNextRepeat) >= 0) restartSosRound();
        return;
    }

    if (!sosRunning) return;

    if (millis() - sosStartedAt >= SOS_BROADCAST_MS) {
        cancelSosBroadcast(false);

        // Nu s-a terminat - doar intra in veghe pana la urmatoarea reluare.
        // Intervalele cresc: des la inceput, cand sansa ca cineva sa fie in
        // raza e mai mare, apoi tot mai rar ca bateria sa tina ore intregi.
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
    if (packet.length() == 0) {          // criptarea a esuat
        sosRunning = false;
        notifyPhone("[SYS] SOS encryption failed");
        return;
    }

    radio.standby();
    int st = radio.transmit(packet);
    if (st == RADIOLIB_ERR_NONE) sosPacketCount++;

    sosLastPacket = millis();
}
