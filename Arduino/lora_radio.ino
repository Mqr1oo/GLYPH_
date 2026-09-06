//file name:lora_radio.ino
//
// Trimiterea unui mesaj. Criptografia sta in glyph_crypto.ino, formatul
// pachetului in glyph_protocol.ino - amandoua testabile nativ.

void executeSendMsg() {
    if (msgDraft.length() == 0 || currentPowerMode == STEALTH_MODE) return;
    if (!health.radioOk) { notifyPhone("[SYS] Radio unavailable, message not sent"); return; }

    double lat, lon;
    getGpsPosition(lat, lon);

    String fullMsg = myName + ": " + msgDraft;
    String payload = fullMsg + "|" + String(lat, 5) + "," + String(lon, 5);

    String packet = buildPacket(MSG_TEXT, payload, secureMode, MESH_HOPS_DEFAULT);
    if (packet.length() == 0) {
        // Criptarea a esuat. Nu trimitem in clar un mesaj pe care
        // utilizatorul il crede securizat.
        notifyPhone("[SYS] Encryption failed, message not sent");
        return;
    }

    radio.standby();
    radio.setSpreadingFactor(LORA_SPREADING_FACTOR);
    int txState = radio.transmit(packet);

    if (txState != RADIOLIB_ERR_NONE) {
        // Inainte, un esec de transmisie era invizibil: mesajul aparea in
        // istoric ca si cum ar fi plecat.
        notifyPhone("[SYS] Transmit failed (" + String(txState) + ")");
        pushLoraHistory("!! [" + formatLocalTime() + "] NOT SENT: " + msgDraft);
    } else {
        pushLoraHistory(">> [" + formatLocalTime() + "] " + fullMsg);
        notifyPhone("[TX]: " + fullMsg);
    }

    radio.startReceive();
    loraListening = true;

    msgDraft = "";
    currentState = PAGE_LORA;
    fullRefreshNeeded = false;
    requestUIUpdate = true;
    pingActivity();
}
