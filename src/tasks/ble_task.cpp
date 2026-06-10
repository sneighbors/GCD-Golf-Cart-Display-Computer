/*******************************************************************************
 *  BLE Task — GCD Golf Cart Display Computer
 *
 *  Acts as a NimBLE peripheral. Streams compressed JSON state to the Android
 *  tablet and receives command frames from it.
 *
 *  Protocol wire format (both directions):
 *    Byte 0      : MSG_TYPE / CMD_TYPE  (uint8)
 *    Bytes 1–2   : PAYLOAD_LEN          (uint16, big-endian)
 *    Bytes 3+    : PAYLOAD              (UTF-8 JSON, no null terminator)
 *
 *  See BLE_PROTOCOL.md at the project root for the full spec.
 *
 *  GATT UUIDs (128-bit, little-endian on the wire):
 *    Service  : a0f3f001-2b45-4c9a-8b6d-e1234f567890
 *    TX char  : a0f3f002-2b45-4c9a-8b6d-e1234f567890  (NOTIFY)
 *    RX char  : a0f3f003-2b45-4c9a-8b6d-e1234f567890  (WRITE)
 ******************************************************************************/

#include "ble_task.h"
#include "config.h"
#include "globals.h"
#include "get_set_vars.h"
#include "communication/chat_buffer.h"
#include "communication/espnow_handler.h"
#include "storage/preferences_manager.h"

#include <NimBLEDevice.h>

// ── GATT UUIDs ──────────────────────────────────────────────────────────────
#define GCD_BLE_SVC_UUID  "a0f3f001-2b45-4c9a-8b6d-e1234f567890"
#define GCD_BLE_TX_UUID   "a0f3f002-2b45-4c9a-8b6d-e1234f567890"
#define GCD_BLE_RX_UUID   "a0f3f003-2b45-4c9a-8b6d-e1234f567890"

// ── Module state ────────────────────────────────────────────────────────────
volatile bool bleClientConnected = false;

static NimBLEServer*         s_server  = nullptr;
static NimBLECharacteristic* s_txChar  = nullptr;
static char                  s_bleName[12];  // "GCD-XXYYZZ\0"

// Flag set by RX callback to trigger chat history sync from ble_task loop
// (avoids sending 32 large frames from inside the NimBLE callback context).
static volatile bool s_chatSyncRequested = false;

// ── Simple JSON field parsers (no ArduinoJson dependency) ──────────────────

static bool jsonStr(const char* json, const char* key, char* out, size_t outlen) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    const char* e = strchr(p, '"');
    if (!e) return false;
    size_t n = (size_t)(e - p);
    if (n >= outlen) n = outlen - 1;
    memcpy(out, p, n);
    out[n] = '\0';
    return true;
}

static bool jsonInt(const char* json, const char* key, int32_t* out) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    *out = (int32_t)strtol(p + strlen(pat), nullptr, 10);
    return true;
}

static bool jsonFloat(const char* json, const char* key, float* out) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    *out = strtof(p + strlen(pat), nullptr);
    return true;
}

static bool jsonBool(const char* json, const char* key, bool* out) {
    char pat[48];
    snprintf(pat, sizeof(pat), "\"%s\":", key);
    const char* p = strstr(json, pat);
    if (!p) return false;
    p += strlen(pat);
    *out = (strncmp(p, "true", 4) == 0);
    return true;
}

// ── Frame send (direct, from ble_task loop only) ────────────────────────────

static void bleSendFrame(uint8_t msg_type, const char* json) {
    if (!s_txChar || !bleClientConnected) return;
    size_t jlen = strlen(json);
    if (jlen > BLE_PAYLOAD_MAX) jlen = BLE_PAYLOAD_MAX;

    uint8_t frame[3 + BLE_PAYLOAD_MAX];
    frame[0] = msg_type;
    frame[1] = (uint8_t)((jlen >> 8) & 0xFF);
    frame[2] = (uint8_t)(jlen & 0xFF);
    memcpy(frame + 3, json, jlen);

    s_txChar->setValue(frame, 3 + jlen);
    s_txChar->notify();
}

// ── Public push API (queue from any task) ───────────────────────────────────

void blePush(uint8_t msg_type, const char* json) {
    if (!bleNotifyQueue || !bleClientConnected) return;
    ble_notify_item_t item;
    item.msg_type = msg_type;
    size_t jlen = strlen(json);
    if (jlen > BLE_PAYLOAD_MAX) jlen = BLE_PAYLOAD_MAX;
    item.payload_len = (uint16_t)jlen;
    memcpy(item.payload, json, jlen);
    xQueueSend(bleNotifyQueue, &item, 0);  // non-blocking — drop if full
}

// ── Serializers ─────────────────────────────────────────────────────────────

void blePushGps() {
    static char buf[BLE_PAYLOAD_MAX];
    if (xSemaphoreTake(gpsMutex, pdMS_TO_TICKS(50)) != pdTRUE) return;
    snprintf(buf, sizeof(buf),
        "{\"type\":\"GPS\","
        "\"spd\":%d,\"hdg\":\"%s\","
        "\"lat\":\"%s\",\"lng\":\"%s\","
        "\"date\":\"%s\",\"hhmm\":\"%s\",\"ampm\":\"%s\",\"secs\":\"%s\","
        "\"sats\":\"%s\","
        "\"odo\":\"%s\",\"trip\":\"%s\","
        "\"d_mi\":%.2f,\"t_mi\":%.2f,"
        "\"rise\":\"%s\",\"set\":\"%s\","
        "\"home\":%s,\"day\":%s,"
        "\"svc_h\":%d,\"svc_i\":%d}",
        (int)avg_speed,
        heading.c_str(),
        cur_lat.c_str(), cur_long.c_str(),
        cur_date.c_str(), hhmm_str.c_str(), am_pm_str.c_str(), hhmmss_str.c_str(),
        sats_hdop.c_str(),
        odometer.c_str(), trip_odometer.c_str(),
        accum_distance, trip_distance,
        sunrise_time_str.c_str(), sunset_time_str.c_str(),
        at_home     ? "true" : "false",
        is_daytime  ? "true" : "false",
        (int)hrs_since_svc, (int)svc_interval_hrs
    );
    xSemaphoreGive(gpsMutex);
    blePush(BLE_MSG_GPS, buf);
}

void blePushTelemetry() {
    static char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"TEL\","
        "\"fuel\":%.1f,\"batt\":%.3f,\"temp\":%.1f,"
        "\"lts\":%s,\"lux\":%d}",
        fuelLevel, battVoltage, airTemperature,
        headlights_on ? "true" : "false",
        (int)lux_now
    );
    blePush(BLE_MSG_TELEMETRY, buf);
}

void blePushWeather() {
    static char buf[BLE_PAYLOAD_MAX];
    // Hot-packet double buffer: read from the active (front) buffer; no mutex
    // needed for data reads — hotPacketMutex only guards the index swap itself.
    int wx = hotPacketActiveBufferWx;
    snprintf(buf, sizeof(buf),
        "{\"type\":\"WX\","
        "\"rcv\":\"%s\",\"cur\":\"%s\","
        "\"fc\":["
          "{\"hr\":\"%s\",\"gl\":\"%s\",\"t\":\"%s\",\"pr\":\"%s\"},"
          "{\"hr\":\"%s\",\"gl\":\"%s\",\"t\":\"%s\",\"pr\":\"%s\"},"
          "{\"hr\":\"%s\",\"gl\":\"%s\",\"t\":\"%s\",\"pr\":\"%s\"},"
          "{\"hr\":\"%s\",\"gl\":\"%s\",\"t\":\"%s\",\"pr\":\"%s\"}"
        "]}",
        hotPacketBuffer_wx_rcv_time[wx], hotPacketBuffer_cur_temp[wx],
        hotPacketBuffer_fcast_hr1[wx], hotPacketBuffer_fcast_glyph1[wx],
          hotPacketBuffer_fcast_temp1[wx], hotPacketBuffer_fcast_precip1[wx],
        hotPacketBuffer_fcast_hr2[wx], hotPacketBuffer_fcast_glyph2[wx],
          hotPacketBuffer_fcast_temp2[wx], hotPacketBuffer_fcast_precip2[wx],
        hotPacketBuffer_fcast_hr3[wx], hotPacketBuffer_fcast_glyph3[wx],
          hotPacketBuffer_fcast_temp3[wx], hotPacketBuffer_fcast_precip3[wx],
        hotPacketBuffer_fcast_hr4[wx], hotPacketBuffer_fcast_glyph4[wx],
          hotPacketBuffer_fcast_temp4[wx], hotPacketBuffer_fcast_precip4[wx]
    );
    blePush(BLE_MSG_WEATHER, buf);
}

void blePushVenue() {
    static char buf[BLE_PAYLOAD_MAX];
    int np = hotPacketActiveBufferNp;
    // Venue data can be up to HP_VENUE_DATA_SIZE (240) chars; truncate to fit frame.
    char data_trunc[200];
    strncpy(data_trunc, hotPacketBuffer_live_venue_event_data[np], sizeof(data_trunc) - 1);
    data_trunc[sizeof(data_trunc) - 1] = '\0';
    snprintf(buf, sizeof(buf),
        "{\"type\":\"NP\",\"rcv\":\"%s\",\"data\":\"%s\"}",
        hotPacketBuffer_np_rcv_time[np], data_trunc
    );
    blePush(BLE_MSG_VENUE, buf);
}

void blePushStatus() {
    static char buf[BLE_PAYLOAD_MAX];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"STS\","
        "\"gcd_v\":\"%s\",\"gci_v\":\"%s\","
        "\"gcm_id\":\"%s\","
        "\"bt_mac\":\"%s\","
        "\"en_conn\":%s,\"en_sts\":\"%s\","
        "\"en_mac\":\"%s\","
        "\"mesh_en\":%s,"
        "\"unrd\":%d}",
        gcd_version.c_str(), gci_version.c_str(),
        get_var_gcm_node_id(),
        cyd_mac_addr.c_str(),
        espnow_connected ? "true" : "false",
        espnow_status.c_str(),
        espnow_gci_mac_addr.c_str(),
        mesh_serial_enabled ? "true" : "false",
        (int)get_var_num_unread_direct_msgs()
    );
    blePush(BLE_MSG_STATUS, buf);
}

void blePushSettings() {
    static char buf[BLE_PAYLOAD_MAX];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"SET\","
        "\"day_bl\":%d,\"ngt_bl\":%d,"
        "\"spk_vol\":%d,\"bl_to\":%d,"
        "\"t_adj\":%.2f,\"fuel_low\":%.1f,"
        "\"fuel_type\":%d,\"home_r\":%d,"
        "\"lux_on\":%d,\"lux_off\":%d,"
        "\"svc_int\":%d,\"mesh_flt\":%d,"
        "\"gci_mac\":\"%s\"}",
        (int)day_backlight, (int)night_backlight,
        (int)speaker_volume, (int)backlight_timeout,
        temperature_adj, fuel_low_percent,
        (int)fuelSensorType, (int)home_gps_fence_radius_m,
        (int)lux_lights_on, (int)lux_lights_off,
        (int)svc_interval_hrs, (int)mesh_filter,
        espnow_gci_mac_addr.c_str()
    );
    blePush(BLE_MSG_SETTINGS, buf);
}

void blePushChat(const chatMessage_t* msg) {
    static char buf[BLE_PAYLOAD_MAX];
    // Escape any embedded double quotes in the text field.
    char txt[CHAT_TEXT_SIZE + 1];
    size_t ti = 0;
    for (size_t i = 0; i < CHAT_TEXT_SIZE && msg->text[i] && ti < sizeof(txt) - 2; i++) {
        if (msg->text[i] == '"' || msg->text[i] == '\\') txt[ti++] = '\\';
        txt[ti++] = msg->text[i];
    }
    txt[ti] = '\0';
    snprintf(buf, sizeof(buf),
        "{\"type\":\"CHAT\","
        "\"id\":%lu,\"frm\":%lu,\"to\":%lu,"
        "\"ch\":%d,\"ts\":%lu,"
        "\"out\":%s,\"rd\":%s,"
        "\"txt\":\"%s\"}",
        (unsigned long)msg->id,
        (unsigned long)msg->from,
        (unsigned long)msg->to,
        (int)msg->channel,
        (unsigned long)msg->timestamp,
        msg->outgoing ? "true" : "false",
        msg->read     ? "true" : "false",
        txt
    );
    blePush(BLE_MSG_CHAT, buf);
}

void blePushAlert(uint8_t code, const char* msg_text) {
    static char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"ALERT\",\"code\":%d,\"msg\":\"%s\"}",
        (int)code, msg_text
    );
    blePush(BLE_MSG_ALERT, buf);
}

void blePushAck(const char* cmd, bool ok, const char* msg_text) {
    static char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"ACK\",\"cmd\":\"%s\",\"ok\":%s,\"msg\":\"%s\"}",
        cmd, ok ? "true" : "false", msg_text
    );
    blePush(BLE_MSG_ACK, buf);
}

// ── Connection-time sequence ────────────────────────────────────────────────

static void bleSendHello() {
    static char buf[128];
    snprintf(buf, sizeof(buf),
        "{\"type\":\"HELLO\",\"fw\":\"%s\",\"mac\":\"%s\",\"name\":\"%s\"}",
        gcd_version.c_str(), cyd_mac_addr.c_str(), s_bleName
    );
    blePush(BLE_MSG_HELLO, buf);
}

// Queues the full state snapshot sent immediately after Android connects.
static void bleQueueConnectionSequence() {
    bleSendHello();
    blePushStatus();
    blePushGps();
    blePushTelemetry();
    blePushWeather();
    blePushVenue();
    blePushSettings();
}

// Sends the full chat ring buffer directly (not via queue, to handle up to 32
// messages without overflowing the queue). Called from ble_task loop only.
static void bleSendChatHistory() {
    static chatMessage_t snapshot[CHAT_BUFFER_SIZE];

    bleSendFrame(BLE_MSG_CHAT_CLEAR, "{}");
    vTaskDelay(pdMS_TO_TICKS(20));

    size_t count = 0;
    if (xSemaphoreTake(chatBufferMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
        count = chatBufferSnapshot(CHAT_FILTER_ALL, snapshot, CHAT_BUFFER_SIZE);
        xSemaphoreGive(chatBufferMutex);
    }

    for (size_t i = 0; i < count; i++) {
        static char buf[BLE_PAYLOAD_MAX];
        char txt[CHAT_TEXT_SIZE + 1];
        size_t ti = 0;
        for (size_t j = 0; j < CHAT_TEXT_SIZE && snapshot[i].text[j] && ti < sizeof(txt) - 2; j++) {
            if (snapshot[i].text[j] == '"' || snapshot[i].text[j] == '\\') txt[ti++] = '\\';
            txt[ti++] = snapshot[i].text[j];
        }
        txt[ti] = '\0';
        snprintf(buf, sizeof(buf),
            "{\"type\":\"CHAT\","
            "\"id\":%lu,\"frm\":%lu,\"to\":%lu,"
            "\"ch\":%d,\"ts\":%lu,"
            "\"out\":%s,\"rd\":%s,"
            "\"txt\":\"%s\"}",
            (unsigned long)snapshot[i].id,
            (unsigned long)snapshot[i].from,
            (unsigned long)snapshot[i].to,
            (int)snapshot[i].channel,
            (unsigned long)snapshot[i].timestamp,
            snapshot[i].outgoing ? "true" : "false",
            snapshot[i].read     ? "true" : "false",
            txt
        );
        bleSendFrame(BLE_MSG_CHAT, buf);
        vTaskDelay(pdMS_TO_TICKS(20));  // let BLE stack breathe between frames
    }
}

// ── Command handlers ─────────────────────────────────────────────────────────

static void handleCmdSendChat(const char* json) {
    char txt[CHAT_TEXT_SIZE];
    int32_t ch = 0, to = 0;
    if (!jsonStr(json, "txt", txt, sizeof(txt))) {
        blePushAck("CHAT", false, "Missing txt");
        return;
    }
    jsonInt(json, "ch", &ch);
    jsonInt(json, "to", &to);

    chatTxItem_t item;
    item.channel = (uint8_t)ch;
    item.dest    = (uint32_t)to;
    strncpy(item.text, txt, sizeof(item.text) - 1);
    item.text[sizeof(item.text) - 1] = '\0';

    if (xQueueSend(chatTxQueue, &item, pdMS_TO_TICKS(100)) == pdTRUE) {
        blePushAck("CHAT", true);
    } else {
        blePushAck("CHAT", false, "Queue full");
    }
}

static void handleCmdSet(const char* json) {
    char key[32];
    if (!jsonStr(json, "key", key, sizeof(key))) {
        blePushAck("SET", false, "Missing key");
        return;
    }

    int32_t ival;
    float   fval;
    bool    bval;

    if (strcmp(key, "day_bl") == 0 && jsonInt(json, "val", &ival)) {
        day_backlight = (int32_t)constrain(ival, 0, 255);
        set_var_cyd_day_backlight(day_backlight);
        queuePreferenceWrite("day_backlight", (int)day_backlight);
    } else if (strcmp(key, "ngt_bl") == 0 && jsonInt(json, "val", &ival)) {
        night_backlight = (int32_t)constrain(ival, 0, 255);
        set_var_cyd_night_backlight(night_backlight);
        queuePreferenceWrite("night_backlight", (int)night_backlight);
    } else if (strcmp(key, "spk_vol") == 0 && jsonInt(json, "val", &ival)) {
        speaker_volume = (int32_t)constrain(ival, 0, 100);
        set_var_speaker_volume(speaker_volume);
        queuePreferenceWrite("speaker_volume", (int)speaker_volume);
    } else if (strcmp(key, "bl_to") == 0 && jsonInt(json, "val", &ival)) {
        backlight_timeout = (int32_t)max((int32_t)0, ival);
        set_var_backlight_timeout(backlight_timeout);
        queuePreferenceWrite("backlight_timeout", (int)backlight_timeout);
    } else if (strcmp(key, "t_adj") == 0 && jsonFloat(json, "val", &fval)) {
        temperature_adj = fval;
        set_var_temperature_adj(temperature_adj);
        queuePreferenceWrite("temperature_adj", temperature_adj);
    } else if (strcmp(key, "fuel_low") == 0 && jsonFloat(json, "val", &fval)) {
        fuel_low_percent = constrain(fval, 0.0f, 100.0f);
        set_var_fuel_low_percent(fuel_low_percent);
        queuePreferenceWrite("fuel_low_percent", fuel_low_percent);
    } else if (strcmp(key, "fuel_type") == 0 && jsonInt(json, "val", &ival)) {
        fuelSensorType = (int32_t)constrain(ival, 0, 3);
        set_var_fuel_sense_type(fuelSensorType);
        queuePreferenceWrite("fuel_sense_type", (int)fuelSensorType);
        espNow.sendFuelConfig();  // push new type to GCI
    } else if (strcmp(key, "home_r") == 0 && jsonInt(json, "val", &ival)) {
        home_gps_fence_radius_m = (int32_t)max((int32_t)1, ival);
        set_var_home_gps_fence_radius_m(home_gps_fence_radius_m);
        queuePreferenceWrite("home_gps_fence_radius_m", (int)home_gps_fence_radius_m);
    } else if (strcmp(key, "lux_on") == 0 && jsonInt(json, "val", &ival)) {
        lux_lights_on = (int32_t)max((int32_t)0, ival);
        set_var_lux_lights_on(lux_lights_on);
        queuePreferenceWrite("lux_lights_on", (int)lux_lights_on);
        espNow.sendFuelConfig();  // CONFIG msg carries lux thresholds to GCI
    } else if (strcmp(key, "lux_off") == 0 && jsonInt(json, "val", &ival)) {
        lux_lights_off = (int32_t)max((int32_t)0, ival);
        set_var_lux_lights_off(lux_lights_off);
        queuePreferenceWrite("lux_lights_off", (int)lux_lights_off);
        espNow.sendFuelConfig();
    } else if (strcmp(key, "svc_int") == 0 && jsonInt(json, "val", &ival)) {
        svc_interval_hrs = (int32_t)max((int32_t)1, ival);
        set_var_svc_interval_hrs(svc_interval_hrs);
        queuePreferenceWrite("svc_interval_hrs", (int)svc_interval_hrs);
    } else if (strcmp(key, "mesh_flt") == 0 && jsonInt(json, "val", &ival)) {
        mesh_filter = (int32_t)constrain(ival, 0, 4);
        set_var_mesh_filter(mesh_filter);
        queuePreferenceWrite("mesh_filter", (int)mesh_filter);
    } else if (strcmp(key, "mesh_en") == 0 && jsonBool(json, "val", &bval)) {
        mesh_serial_enabled = bval;
        set_var_mesh_serial_enabled(mesh_serial_enabled);
        queuePreferenceWrite("mesh_serial_enabled", mesh_serial_enabled);
    } else if (strcmp(key, "gci_mac") == 0) {
        char mac[20];
        if (jsonStr(json, "val", mac, sizeof(mac))) {
            espnow_gci_mac_addr = String(mac);
            set_var_espnow_gci_mac_addr(mac);
            queuePreferenceWrite("espnow_gci_mac_addr", espnow_gci_mac_addr);
        }
    } else {
        blePushAck("SET", false, "Unknown key");
        return;
    }

    blePushAck("SET", true, key);
    blePushSettings();  // reflect updated value back to Android
}

static void handleCmdMarkRead(const char* json) {
    int32_t id;
    if (!jsonInt(json, "id", &id)) {
        blePushAck("MARK_RD", false, "Missing id");
        return;
    }
    if (xSemaphoreTake(chatBufferMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        chatBufferMarkRead((uint32_t)id);
        set_var_num_unread_direct_msgs((int32_t)chatBufferUnreadDmCount());
        xSemaphoreGive(chatBufferMutex);
    }
    blePushAck("MARK_RD", true);
    blePushStatus();  // refresh unread count on Android
}

// ── RX command dispatch (called from NimBLE callback context) ──────────────

static void bleDispatchCommand(const uint8_t* data, size_t len) {
    if (len < 3) return;

    uint8_t  cmd_type   = data[0];
    uint16_t plen       = ((uint16_t)data[1] << 8) | data[2];
    if (plen > len - 3) return;  // malformed frame

    char json[BLE_PAYLOAD_MAX + 1];
    size_t jlen = (plen < BLE_PAYLOAD_MAX) ? plen : BLE_PAYLOAD_MAX;
    memcpy(json, data + 3, jlen);
    json[jlen] = '\0';

#if DEBUG_BLE
    Serial.printf("[BLE] CMD 0x%02X payload: %s\n", cmd_type, json);
#endif

    switch (cmd_type) {
        case BLE_CMD_PING:
            blePushAck("PING", true);
            break;

        case BLE_CMD_REQ_STATE:
            bleQueueConnectionSequence();
            break;

        case BLE_CMD_REQ_CHAT:
            s_chatSyncRequested = true;
            break;

        case BLE_CMD_SEND_CHAT:
            handleCmdSendChat(json);
            break;

        case BLE_CMD_SET_HOME:
            set_home_loc = true;
            set_var_set_home_loc(true);
            blePushAck("SET_HOME", true, "Home location saved");
            break;

        case BLE_CMD_PAIR_GCI:
            espnow_pair_gci = true;
            set_var_espnow_pair_gci(true);
            blePushAck("PAIR_GCI", true);
            break;

        case BLE_CMD_UNPAIR_GCI: {
            espnow_gci_mac_addr = "NONE";
            set_var_espnow_gci_mac_addr("NONE");
            queuePreferenceWrite("espnow_gci_mac_addr", espnow_gci_mac_addr);
            blePushAck("UNPAIR_GCI", true);
            blePushStatus();
            break;
        }

        case BLE_CMD_REBOOT_MESH:
            reboot_meshtastic = true;
            set_var_reboot_meshtastic(true);
            blePushAck("RBT_MESH", true);
            break;

        case BLE_CMD_REBOOT_GCD:
            blePushAck("REBOOT", true);
            vTaskDelay(pdMS_TO_TICKS(500));
            ESP.restart();
            break;

        case BLE_CMD_RESET_PREFS:
            reset_preferences = true;
            set_var_reset_preferences(true);
            // No ACK — device reboots
            break;

        case BLE_CMD_TRIP_RESET:
            trip_distance = 0.0f;
            set_var_trip_distance(0.0f);
            set_var_trip_odometer("0.0");
            queuePreferenceWrite("trip_distance", 0.0f);
            blePushAck("TRIP_RST", true);
            blePushGps();
            break;

        case BLE_CMD_SVC_RESET:
            hrs_since_svc = 0;
            set_var_hrs_since_svc(0);
            queuePreferenceWrite("hrs_since_svc", 0);
            blePushAck("SVC_RST", true);
            blePushGps();
            break;

        case BLE_CMD_MARK_READ:
            handleCmdMarkRead(json);
            break;

        case BLE_CMD_SET:
            handleCmdSet(json);
            break;

        default:
#if DEBUG_BLE
            Serial.printf("[BLE] Unknown CMD 0x%02X\n", cmd_type);
#endif
            break;
    }
}

// ── NimBLE GATT callbacks ───────────────────────────────────────────────────

class GCDServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        bleClientConnected = true;
        // Request maximum data length and MTU from the Android side.
        pServer->setDataLen(connInfo.getConnHandle(), 512);
        NimBLEDevice::setMTU(512);
        Serial.println("[BLE] Client connected");
        // Queue the connection-time state burst from ble_task loop
        // (we're in the NimBLE task here — avoid sending directly).
        bleQueueConnectionSequence();
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        bleClientConnected = false;
        Serial.printf("[BLE] Client disconnected (reason=%d) — resuming advertising\n", reason);
        NimBLEDevice::startAdvertising();
    }
};

class GCDRxCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pChar, NimBLEConnInfo& connInfo) override {
        const uint8_t* data = pChar->getValue().data();
        size_t len = pChar->getValue().length();
        bleDispatchCommand(data, len);
    }
};

// ── BLE initialisation ──────────────────────────────────────────────────────

static void bleInit() {
    // Build unique device name from last 3 bytes of BT MAC: "GCD-XXYYZZ"
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_bleName, sizeof(s_bleName), "%s%02X%02X%02X",
             BLE_DEVICE_NAME_PREFIX, mac[3], mac[4], mac[5]);

    NimBLEDevice::init(s_bleName);
    NimBLEDevice::setMTU(512);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(new GCDServerCallbacks());

    NimBLEService* pSvc = s_server->createService(GCD_BLE_SVC_UUID);

    s_txChar = pSvc->createCharacteristic(GCD_BLE_TX_UUID, NIMBLE_PROPERTY::NOTIFY);

    NimBLECharacteristic* pRxChar = pSvc->createCharacteristic(
        GCD_BLE_RX_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pRxChar->setCallbacks(new GCDRxCallbacks());

    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
    pAdv->addServiceUUID(GCD_BLE_SVC_UUID);
    pAdv->start();

    Serial.printf("[BLE] Advertising as \"%s\"\n", s_bleName);
}

// ── Task entry point ─────────────────────────────────────────────────────────

void bleTask(void* parameter) {
    // Brief startup delay to let other tasks and WiFi/ESP-NOW init settle
    // before NimBLEDevice::init() claims heap.
    vTaskDelay(pdMS_TO_TICKS(1000));

    bleInit();

    ble_notify_item_t item;
    uint32_t lastStatusMs  = 0;
    uint32_t lastGpsPushMs = 0;
    uint32_t lastTelPushMs = 0;

    // Shadow values for change detection
    int32_t  last_speed        = -1;
    bool     last_at_home      = false;
    bool     last_espnow_conn  = false;
    float    last_fuel         = -999.0f;
    float    last_batt         = -999.0f;
    float    last_temp         = -999.0f;
    bool     last_headlights   = false;

    while (true) {
        uint32_t now = millis();

        // ── Drain the notify queue ──────────────────────────────────────────
        while (xQueueReceive(bleNotifyQueue, &item, 0) == pdTRUE) {
            if (bleClientConnected && s_txChar) {
                uint8_t frame[3 + BLE_PAYLOAD_MAX];
                frame[0] = item.msg_type;
                frame[1] = (uint8_t)((item.payload_len >> 8) & 0xFF);
                frame[2] = (uint8_t)(item.payload_len & 0xFF);
                memcpy(frame + 3, item.payload, item.payload_len);
                s_txChar->setValue(frame, 3 + item.payload_len);
                s_txChar->notify();
                vTaskDelay(pdMS_TO_TICKS(20));  // give BLE stack breathing room
            }
        }

        if (!bleClientConnected) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // ── Chat history sync (requested via CMD_REQ_CHAT) ─────────────────
        if (s_chatSyncRequested) {
            s_chatSyncRequested = false;
            bleSendChatHistory();
        }

        // ── GPS: push every second (Android shows live clock/speed) ─────────
        if ((now - lastGpsPushMs) >= 1000) {
            lastGpsPushMs = now;
            blePushGps();
        }

        // ── Telemetry: push on change (GCI data) ────────────────────────────
        bool tel_changed = (fabsf(fuelLevel    - last_fuel)  > 1.0f ||
                            fabsf(battVoltage  - last_batt)  > 0.05f ||
                            fabsf(airTemperature - last_temp) > 1.0f ||
                            headlights_on != last_headlights);
        if (tel_changed || (now - lastTelPushMs) >= 30000) {
            last_fuel       = fuelLevel;
            last_batt       = battVoltage;
            last_temp       = airTemperature;
            last_headlights = headlights_on;
            lastTelPushMs   = now;
            blePushTelemetry();

            // Fuel-low alert — tell the tablet once per threshold crossing.
            // The ESP32's own speaker is handled by the existing display/system layer.
            static bool fuel_low_alerted = false;
            if (fuelLevel >= 0 && fuelLevel <= fuel_low_percent && !fuel_low_alerted) {
                fuel_low_alerted = true;
                blePushAlert(BLE_ALERT_FUEL_LOW, "Fuel low");
            } else if (fuelLevel > fuel_low_percent) {
                fuel_low_alerted = false;
            }
        }

        // ── ESP-NOW connectivity change ──────────────────────────────────────
        if (espnow_connected != last_espnow_conn) {
            last_espnow_conn = espnow_connected;
            blePushStatus();
            blePushAlert(
                espnow_connected ? BLE_ALERT_GCI_CONNECTED : BLE_ALERT_GCI_LOST,
                espnow_connected ? "GCI connected" : "GCI disconnected"
            );
        }

        // ── Status heartbeat every 30 s ──────────────────────────────────────
        if ((now - lastStatusMs) >= 30000) {
            lastStatusMs = now;
            blePushStatus();
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
