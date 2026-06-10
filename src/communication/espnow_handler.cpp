#include "espnow_handler.h"
#include "config.h"
#include "globals.h"
#include "get_set_vars.h"
#include "../hardware/display.h"
#include "../storage/preferences_manager.h"
#include <esp_wifi.h>

// Global instance
ESPNowHandler espNow;

bool ESPNowHandler::init() {
    if (initialized) {
        return true;
    }

    // If WiFi was not pre-initialized in setup(), do the full init here.
    // Never call WiFi.mode(): it uses default buffer counts which OOM on this device.
    wifi_mode_t _wm;
    if (esp_wifi_get_mode(&_wm) != ESP_OK) {
        // Not yet initialized (espnow_enabled was false at boot, user just enabled)
        wifi_init_config_t wCfg = WIFI_INIT_CONFIG_DEFAULT();
        wCfg.static_rx_buf_num = 3;
        wCfg.tx_buf_type = 1;
        wCfg.static_tx_buf_num = 0;
        wCfg.dynamic_tx_buf_num = 4;
        if (esp_wifi_init(&wCfg) != ESP_OK) {
            Serial.println("ESP-NOW: WiFi init failed");
            return false;
        }
        esp_wifi_set_mode(WIFI_MODE_STA);
    }
    // Always attempt esp_wifi_start(). esp_wifi_get_channel() returns ESP_OK with ch=0
    // on some IDF versions even when WiFi is only initialized (not started), making it
    // unreliable as a started-vs-not check. ESP_ERR_WIFI_STATE (0x3006) means already
    // running — that is fine. Any other failure is a real error.
    {
        esp_err_t startErr = esp_wifi_start();
        if (startErr != ESP_OK && startErr != ESP_ERR_WIFI_STATE) {
            Serial.printf("ESP-NOW: WiFi start failed (0x%x)\n", (int)startErr);
            return false;
        }
    }

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
        status = "Init failed";
        return false;
    }

    // Lock the radio onto ESPNOW_CHANNEL. esp_wifi_set_channel() only works in AP/APSTA
    // mode normally; enabling promiscuous mode briefly lets it work in STA mode too.
    // Must be called AFTER esp_now_init() — by then the driver is fully started.
    // peerInfo.channel=0 already bypasses the IDF strict equality check, but both
    // devices still need the same RF channel.
    esp_wifi_set_promiscuous(true);
    esp_err_t chErr = ESP_FAIL;
    for (int i = 0; i < 5; i++) {
        chErr = esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
        if (chErr == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    esp_wifi_set_promiscuous(false);
    uint8_t home_ch = 0;
    wifi_second_chan_t home_sec = WIFI_SECOND_CHAN_NONE;
    esp_wifi_get_channel(&home_ch, &home_sec);
    if (chErr != ESP_OK || home_ch != ESPNOW_CHANNEL) {
        Serial.printf("ESP-NOW: channel set FAILED (err=%d, home=%u, want=%u)\n",
                      (int)chErr, (unsigned)home_ch, (unsigned)ESPNOW_CHANNEL);
    }
#if DEBUG_ESPNOW == 1
    else {
        Serial.printf("ESP-NOW: home channel = %u\n", (unsigned)home_ch);
    }
#endif
    
    // Register callbacks
    esp_now_register_send_cb(espnowOnDataSent);
    esp_now_register_recv_cb(espnowOnDataRecv);
    
    // Set ESP-NOW rate for long range
    esp_wifi_config_espnow_rate(WIFI_IF_STA, WIFI_PHY_RATE_LORA_250K);
    
    initialized = true;
    status = "Ready";
    espnow_connected = false;  // No peers yet
    set_var_espnow_connected(false);  // Update UI variable
#if DEBUG_ESPNOW == 1
    Serial.println("ESP-NOW initialized");
    Serial.printf("GCD MAC: %s\n", getMyMacAddress().c_str());
#endif

    return true;
}

void ESPNowHandler::deinit() {
    if (!initialized) {
        return;
    }

    esp_now_deinit();
    initialized = false;
    peer_count = 0;
    status = "Disabled";
    espnow_connected = false;
    set_var_espnow_connected(false);  // Update UI variable
#if DEBUG_ESPNOW == 1
    Serial.println("ESP-NOW deinitialized");
#endif
}

bool ESPNowHandler::restart() {
#if DEBUG_ESPNOW == 1
    Serial.println("ESP-NOW: Restarting...");
#endif
    deinit();
    return init();
}

bool ESPNowHandler::addPeer(const uint8_t *mac_addr, const char* name) {
    if (peer_count >= ESPNOW_MAX_PEER_NUM) {
#if DEBUG_ESPNOW == 1
        Serial.println("ESP-NOW: Max peers reached");
#endif
        return false;
    }

    // Check if already exists
    if (isPeerRegistered(mac_addr)) {
#if DEBUG_ESPNOW == 1
        Serial.println("ESP-NOW: Peer already registered");
#endif
        return true;
    }

    // Remove peer first if it exists (ESP-NOW might have auto-added it)
    esp_now_del_peer(mac_addr);

    // Add to ESP-NOW with correct settings.
    // peerInfo.channel = 0 means "use whatever the local home channel is" —
    // avoids the IDF's strict equality check (which logs
    // "Peer channel is not equal to the home channel, send fail!" if
    // home_ch ever drifts off ESPNOW_CHANNEL). Both devices still need
    // to be tuned to the same actual channel via esp_wifi_set_channel.
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, mac_addr, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    esp_err_t addErr = esp_now_add_peer(&peerInfo);
    if (addErr != ESP_OK) {
        Serial.printf("ESP-NOW: esp_now_add_peer failed (err=0x%x, ch=%u)\n",
                      (int)addErr, (unsigned)peerInfo.channel);
        return false;
    }

    // Store in our list
    memcpy(peers[peer_count].mac_addr, mac_addr, 6);
    if (name) {
        strncpy(peers[peer_count].name, name, 31);
    } else {
        strcpy(peers[peer_count].name, "Unknown");
    }
    peers[peer_count].is_online = false;
    peers[peer_count].last_seen = 0;
    peers[peer_count].last_rssi = 0;
    peer_count++;

#if DEBUG_ESPNOW == 1
    Serial.printf("ESP-NOW: Peer added - %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac_addr[0], mac_addr[1], mac_addr[2],
                  mac_addr[3], mac_addr[4], mac_addr[5]);
#endif

    espnow_peer_count = peer_count;
    status = String("Connected (") + String(peer_count) + " peers)";

    return true;
}

bool ESPNowHandler::addPeerFromString(const String& mac_str, const char* name) {
    uint8_t mac_bytes[6];
    if (macStringToBytes(mac_str, mac_bytes)) {
        return addPeer(mac_bytes, name);
    }
    return false;
}

bool ESPNowHandler::removePeer(const uint8_t *mac_addr) {
    if (esp_now_del_peer(mac_addr) != ESP_OK) {
        return false;
    }
    
    // Remove from our list
    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].mac_addr, mac_addr, 6) == 0) {
            // Shift remaining peers
            for (int j = i; j < peer_count - 1; j++) {
                peers[j] = peers[j + 1];
            }
            peer_count--;
            espnow_peer_count = peer_count;
            status = String("Connected (") + String(peer_count) + " peers)";

            // If no peers left, definitely disconnect
            if (peer_count == 0) {
                bool was_connected = espnow_connected;
                espnow_connected = false;
                set_var_espnow_connected(false);  // Update UI variable

#if DEBUG_ESPNOW == 1
                if (was_connected) {
                    Serial.println("ESP-NOW: Disconnected (no peers)");
                }
#endif
            }

            break;
        }
    }
    
    return true;
}

bool ESPNowHandler::isPeerRegistered(const uint8_t *mac_addr) {
    return esp_now_is_peer_exist(mac_addr);
}

espnow_peer_info_t* ESPNowHandler::getPeerInfo(int index) {
    if (index >= 0 && index < peer_count) {
        return &peers[index];
    }
    return nullptr;
}

bool ESPNowHandler::sendMessage(const uint8_t *mac_addr, espnow_msg_type_t type,
                                const uint8_t *data, size_t len) {
    if (!initialized || len > ESPNOW_MAX_PAYLOAD) {
        return false;
    }

    espnow_message_t msg;
    msg.type = type;
    msg.timestamp = millis();
    msg.msg_seq_num = getNextMsgSeqNum();
    msg.data_len = len;
    if (len > 0) {
        memcpy(msg.data, data, len);
    }

    // Send only header + actual payload (not full 249-byte structure)
    return sendRawData(mac_addr, (uint8_t*)&msg, ESPNOW_PACKET_SIZE(len));
}

bool ESPNowHandler::sendTextMessage(const uint8_t *mac_addr, const String& text) {
    return sendMessage(mac_addr, ESPNOW_MSG_TEXT, 
                      (uint8_t*)text.c_str(), text.length());
}

bool ESPNowHandler::broadcast(espnow_msg_type_t type, const uint8_t *data, size_t len) {
    if (peer_count == 0) {
        return false; // No peers to send to
    }

    bool success = true;
    for (int i = 0; i < peer_count; i++) {
        // Verify peer is still registered in ESP-NOW before sending
        if (esp_now_is_peer_exist(peers[i].mac_addr)) {
            // Use sendMessage to wrap data in espnow_message_t
            if (!sendMessage(peers[i].mac_addr, type, data, len)) {
                success = false;
            }
        } else {
#if DEBUG_ESPNOW == 1
            Serial.printf("ESP-NOW: Peer not in list: %02X:%02X:%02X:%02X:%02X:%02X\n",
                         peers[i].mac_addr[0], peers[i].mac_addr[1], peers[i].mac_addr[2],
                         peers[i].mac_addr[3], peers[i].mac_addr[4], peers[i].mac_addr[5]);
#endif
            success = false;
        }
    }
    return success;
}

bool ESPNowHandler::broadcastText(const String& text) {
    return broadcast(ESPNOW_MSG_TEXT, (uint8_t*)text.c_str(), text.length());
}

bool ESPNowHandler::sendGolfCartCommand(const uint8_t *mac_addr, gci_command_t cmdNumber, const void *payload, size_t payloadSize) {
    if (!initialized) {
        return false;
    }

    // Prepare command data in golf cart format
    dataToGci.cmdNumber = cmdNumber;
    cmdToGci = cmdNumber;  // Update global variable

    // Copy payload if provided (up to available space in macAddr field)
    if (payload != nullptr && payloadSize > 0) {
        size_t copySize = min(payloadSize, sizeof(dataToGci.macAddr));
        memcpy(dataToGci.macAddr, payload, copySize);

        // Clear remaining bytes if payload is smaller than field
        if (copySize < sizeof(dataToGci.macAddr)) {
            memset(&dataToGci.macAddr[copySize], 0, sizeof(dataToGci.macAddr) - copySize);
        }
    } else {
        memset(dataToGci.macAddr, 0, sizeof(dataToGci.macAddr));  // Clear if no payload
    }

    // Send raw struct data (no wrapper)
    return sendRawData(mac_addr, (uint8_t*)&dataToGci, sizeof(structMsgToGci));
}

bool ESPNowHandler::sendIsHome(bool is_home) {
    uint8_t value = is_home ? 1 : 0;
    bool result = broadcast(ESPNOW_MSG_IS_HOME, &value, 1);
    // Always show (actionable state change)
    Serial.printf("ESP-NOW: Sent is_home=%s (%s)\n",
                  is_home ? "HOME" : "AWAY", result ? "OK" : "FAIL");
    return result;
}

bool ESPNowHandler::sendIsDaytime(bool is_daytime) {
    uint8_t value = is_daytime ? 1 : 0;
    bool result = broadcast(ESPNOW_MSG_IS_DAYTIME, &value, 1);
    // Always show (actionable state change)
    Serial.printf("ESP-NOW: Sent is_daytime=%s (%s)\n",
                  is_daytime ? "DAYTIME" : "NIGHTTIME", result ? "OK" : "FAIL");
    return result;
}

bool ESPNowHandler::sendFuelConfig() {
    structMsgConfig cfg;
    cfg.fuelSensorType = fuelSensorType;
    cfg.luxLightsOn    = (int32_t)lux_lights_on;
    cfg.luxLightsOff   = (int32_t)lux_lights_off;
    bool result = broadcast(ESPNOW_MSG_CONFIG, (uint8_t*)&cfg, sizeof(cfg));
    Serial.printf("ESP-NOW: Sent config fuel=%d lux_on=%d lux_off=%d (%s)\n",
                  fuelSensorType, lux_lights_on, lux_lights_off, result ? "OK" : "FAIL");
    return result;
}

bool ESPNowHandler::sendRawData(const uint8_t *mac_addr, const uint8_t *data, size_t len) {
    for (int retry = 0; retry < ESPNOW_SEND_RETRY_COUNT; retry++) {
        esp_err_t result = esp_now_send(mac_addr, data, len);
        if (result == ESP_OK) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(ESPNOW_SEND_RETRY_DELAY));
    }
    return false;
}

String ESPNowHandler::getMyMacAddress() {
    return WiFi.macAddress();
}

bool ESPNowHandler::macStringToBytes(const String& mac_str, uint8_t* mac_bytes) {
    if (mac_str.length() != 17) return false;
    
    int values[6];
    if (sscanf(mac_str.c_str(), "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            mac_bytes[i] = (uint8_t)values[i];
        }
        return true;
    }
    return false;
}

void ESPNowHandler::processReceivedMessage(espnow_recv_item_t &item) {
    // Update peer info and connection status
    for (int i = 0; i < peer_count; i++) {
        if (memcmp(peers[i].mac_addr, item.mac_addr, 6) == 0) {
            peers[i].is_online = true;
            peers[i].last_seen = millis();
            peers[i].last_rssi = item.rssi;

            // Set connected status when we receive data from a known peer
            if (!espnow_connected) {
                espnow_connected = true;
                set_var_espnow_connected(true);
                Serial.println("ESP-NOW: Connected to GCI");

                // Send initial status to GCI
                sendIsHome(at_home);
                sendIsDaytime(!headlights_due);
                sendFuelConfig();
            }
            break;
        }
    }
    
    // Process based on message type
    char mac_str[18];
    sprintf(mac_str, "%02X:%02X:%02X:%02X:%02X:%02X",
            item.mac_addr[0], item.mac_addr[1], item.mac_addr[2],
            item.mac_addr[3], item.mac_addr[4], item.mac_addr[5]);
    
    switch (item.message.type) {
        case ESPNOW_MSG_TEXT: {
            String text((char*)item.message.data);
            espnow_last_received = String(mac_str) + ": " + text;
#if DEBUG_ESPNOW == 1
            Serial.printf("ESP-NOW Text from %s: %s\n", mac_str, text.c_str());
#endif
            break;
        }

        case ESPNOW_MSG_GPS_DATA: {
#if DEBUG_ESPNOW == 1
            Serial.printf("ESP-NOW GPS from %s\n", mac_str);
#endif
            // Parse and use GPS data if needed
            break;
        }

        case ESPNOW_MSG_TELEMETRY: {
            // Extract telemetry data from message
            memcpy(&dataFromGci, item.message.data, sizeof(structMsgFromGci));

            // Update individual variables for compatibility
            modeHeadLights = dataFromGci.modeLights;
            outdoorLux = dataFromGci.outdoorLux;
            rawAirTemperature = dataFromGci.airTemp;
            // Apply correction only when sensor is present (-99 means no sensor)
            airTemperature = (rawAirTemperature == -99.0f) ? -99.0f : (rawAirTemperature + temperature_adj);
            battVoltage = dataFromGci.battVolts;

            float newFuelLevel = dataFromGci.fuelPct;
            bool wasFuelLow = (fuelLevel != -99.0f) && (fuelLevel <= fuel_low_percent);
            fuelLevel = newFuelLevel;
            // Alert only if: sensor configured, valid reading, and threshold newly crossed
            if (fuelSensorType != FUEL_SENSOR_NONE && newFuelLevel != -99.0f) {
                bool isFuelLow = (fuelLevel <= fuel_low_percent);
                if (isFuelLow && !wasFuelLow) {
                    tone_alert();
                }
            }

            // Map lux and headlight state into EEZ-accessible variables
            set_var_lux_now((int32_t)outdoorLux);
            headlights_on = (modeHeadLights == 1);

            // Always show telemetry (confirms GCI communication is working)
            Serial.printf("Telemetry: Lights=%d Lux=%d Temp=%.1f Batt=%.2f Fuel=%.1f\n",
                         modeHeadLights, outdoorLux, rawAirTemperature, battVoltage, fuelLevel);
            break;
        }

        case ESPNOW_MSG_COMMAND: {
            // Always show commands (actionable information)
            Serial.printf("ESP-NOW Command from %s\n", mac_str);
            // Handle commands
            break;
        }

        case ESPNOW_MSG_ACK: {
            // Always show pairing success (important user feedback)
            Serial.printf("ESP-NOW: Paired with %s\n", mac_str);

            // Add GCI as peer if not already added
            if (!isPeerRegistered(item.mac_addr)) {
                if (!addPeer(item.mac_addr, "GCI")) {
                    Serial.println("ESP-NOW: Failed to add GCI as peer");
                }
            }

            // Save peer MAC to EEPROM when ACK is received during pairing
            // Update variable directly without triggering ESP-NOW restart
            if (espnow_gci_mac_addr != String(mac_str)) {
                espnow_gci_mac_addr = String(mac_str);
                // Variable change will be detected by system_task and saved to EEPROM
            }

            // Close the pairing window now that peer is added
            espnow_pair_gci = false;
            set_var_espnow_pair_gci(false);

            break;
        }

        case ESPNOW_MSG_HEARTBEAT: {
            // Heartbeat response received from GCI (closed-loop keepalive)
            #if DEBUG_ESPNOW == 1
            Serial.print("ESP-NOW Heartbeat response from ");
            Serial.println(mac_str);
            #endif
            // Note: last_seen timestamp already updated in lines 278-291 above
            break;
        }

        case ESPNOW_MSG_GCI_VERSION: {
            if (item.message.data_len > 0) {
                char buf[32];
                size_t len = min((int)item.message.data_len, 31);
                memcpy(buf, item.message.data, len);
                buf[len] = '\0';
                String received = String(buf);
                if (received != gci_version) {
                    gci_version = received;
                    queuePreferenceWrite("gci_version", gci_version);
                    Serial.printf("ESP-NOW: GCI version updated = %s\n", gci_version.c_str());
                }
            }
            break;
        }
    }
}

// Callback functions
void espnowOnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    #if DEBUG_ESPNOW == 1
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("ESP-NOW: Send failed");
    } else {
        Serial.println("ESP-NOW: Send success");
    }
    #endif
}

void espnowOnDataRecv(const uint8_t *mac_addr, const uint8_t *data, int data_len) {
    // Accept wrapped messages (header is 9 bytes minimum)
    if (data_len >= ESPNOW_PACKET_HEADER_SIZE) {
        // Filter wrapped messages - only accept from registered peers
        bool is_known_peer = false;
        for (int i = 0; i < espNow.getPeerCount(); i++) {
            espnow_peer_info_t* peer = espNow.getPeerInfo(i);
            if (peer && memcmp(peer->mac_addr, mac_addr, 6) == 0) {
                is_known_peer = true;
                break;
            }
        }

        if (!is_known_peer) {
            // Special case: Accept ACK messages from unknown peers during pairing window
            // (espnow_pair_gci will be true for a short time after pairing is initiated)
            espnow_message_t* msg = (espnow_message_t*)data;
            if (msg->type == ESPNOW_MSG_ACK && espnow_pair_gci) {
                // This is an ACK response to our pairing request - allow it
            } else {
                return;
            }
        }

        espnow_recv_item_t item;
        memcpy(item.mac_addr, mac_addr, 6);

        // Copy the received data (variable size, not full structure)
        memcpy(&item.message, data, data_len);
        item.rssi = -50; // Default value if RSSI not available

        // Queue for processing
        if (espnowRecvQueue != NULL) {
            xQueueSend(espnowRecvQueue, &item, 0);
        }
    } else {
#if DEBUG_ESPNOW == 1
        // Too small to be a valid wrapped message
        Serial.printf("ESP-NOW: Message too small: %d bytes\n", data_len);
#endif
    }
}