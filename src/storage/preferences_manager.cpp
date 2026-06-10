#include "preferences_manager.h"
#include "config.h"
#include "globals.h"
#include "types.h"
#include "communication/chat_buffer.h"
#include <Meshtastic.h>

void initPreferences() {
    prefs.begin("eeprom", false);
#if DEBUG_INIT == 1
    Serial.println("Preferences initialized");
#endif

#if DEBUG_EEPROM == 1
    // Check NVS stats
    size_t usedEntries = prefs.freeEntries();
    Serial.printf("NVS Free Entries: %d\n", usedEntries);
#endif
}

void loadPreferences() {
    day_backlight = prefs.getInt("day_backlight", 10);
    old_day_backlight = day_backlight;

    night_backlight = prefs.getInt("night_backlight", 5);
    old_night_backlight = night_backlight;

    // Key name must be 15 chars or less for NVS - use "gci_mac" (7 chars)
    espnow_gci_mac_addr = prefs.getString("gci_mac", "NONE");
    old_espnow_gci_mac_addr = espnow_gci_mac_addr;

    flip_screen = prefs.getBool("flip_screen", false);
    set_var_flip_screen(flip_screen);
    old_flip_screen = flip_screen;

    speaker_volume = prefs.getInt("speaker_volume", 10);
    set_var_speaker_volume(speaker_volume);
    old_speaker_volume = speaker_volume;

    backlight_timeout = prefs.getInt("bklt_timeout", 5);
    old_backlight_timeout = backlight_timeout;

    accum_distance = prefs.getFloat("accumDistance", 0.0);
    odometer = String(accum_distance, 1);
    set_var_odometer(odometer.c_str());

    trip_distance = prefs.getFloat("tripDistance", 0.0);
    trip_odometer = String(trip_distance, 1);
    set_var_trip_odometer(trip_odometer.c_str());

    // hrs_since_svc is stored as TENTHS of hours everywhere (0.1 hr = 6 min resolution)
    // Load directly without calling setter to avoid premature EEPROM queue write
    hrs_since_svc = prefs.getInt("hrs_since_svc", 0);

    svc_interval_hrs = prefs.getInt("svc_interval_hrs", 100);
    set_var_svc_interval_hrs(svc_interval_hrs);
    old_svc_interval_hrs = svc_interval_hrs;

    temperature_adj = prefs.getFloat("temperature_adj", 0.0);
    set_var_temperature_adj(temperature_adj);
    old_temperature_adj = temperature_adj;

    fuel_low_percent = prefs.getFloat("fuel_low_pct", 20.0);
    set_var_fuel_low_percent(fuel_low_percent);
    old_fuel_low_percent = fuel_low_percent;

    fuelSensorType = prefs.getInt("fuel_sense_type", FUEL_SENSOR_NONE);

    // Load stored entertainment (now playing) data for boot-time validation
    np_stored_date = prefs.getInt("np_date", 0);
    np_stored_data = prefs.getString("np_data", "");
    np_stored_timestamp = prefs.getString("np_time", "");

    // Load stored weather data for boot-time validation
    wx_stored_date = prefs.getInt("wx_date", 0);
    wx_stored_data = prefs.getString("wx_data", "");
    wx_stored_timestamp = prefs.getString("wx_time", "");

    // Load home location coordinates
    homeLatitude = prefs.getFloat("home_lat", 0.0);
    homeLongitude = prefs.getFloat("home_lon", 0.0);
    home_gps_fence_radius_m = prefs.getInt("home_fence_m", 500);
    homeLocationSet = (homeLatitude != 0.0 || homeLongitude != 0.0);

    // Chat / messaging UI
    mesh_filter = prefs.getInt("mesh_filter", 0);  // default = DIRECT MSGS

    // Headlight auto-on/off lux thresholds
    lux_lights_on  = prefs.getInt("lux_lights_on",  200);
    lux_lights_off = prefs.getInt("lux_lights_off", 400);

    // Last known GCI firmware version (updated when GCI connects)
    gci_version = prefs.getString("gci_version", "");

#if DEBUG_EEPROM == 1
    Serial.println("EEPROM values loaded:");
    Serial.printf("  day_backlight=%d, night_backlight=%d\n", day_backlight, night_backlight);
    Serial.printf("  gci_mac=%s, flip_screen=%s\n", espnow_gci_mac_addr.c_str(), flip_screen ? "true" : "false");
    Serial.printf("  speaker_volume=%d, backlight_timeout=%d\n", speaker_volume, backlight_timeout);
    Serial.printf("  accum_distance=%.3f, trip_distance=%.3f\n", accum_distance, trip_distance);
    Serial.printf("  hrs_since_svc=%d (%.1f hrs), svc_interval_hrs=%d\n", hrs_since_svc, hrs_since_svc / 10.0, svc_interval_hrs);
    Serial.printf("  temperature_adj=%.1f\n", temperature_adj);
    if (homeLocationSet) {
        Serial.printf("  home: %.6f, %.6f (fence=%dm)\n", homeLatitude, homeLongitude, home_gps_fence_radius_m);
    } else {
        Serial.println("  home: not set");
    }
#endif
}

void queuePreferenceWrite(const char* key, float value) {
    eepromWriteItem_t item;
    item.type = EEPROM_FLOAT;
    strcpy(item.key, key);
    item.value.floatVal = value;
    xQueueSend(eepromWriteQueue, &item, 0);
}

void queuePreferenceWrite(const char* key, int value) {
    eepromWriteItem_t item;
    item.type = EEPROM_INT;
    strcpy(item.key, key);
    item.value.intVal = value;
    xQueueSend(eepromWriteQueue, &item, 0);
}

void queuePreferenceWrite(const char* key, const String& value) {
    eepromWriteItem_t item;
    item.type = EEPROM_STRING;
    strcpy(item.key, key);
    strcpy(item.value.stringVal, value.c_str());
    xQueueSend(eepromWriteQueue, &item, 0);
}

void queuePreferenceWrite(const char* key, bool value) {
    eepromWriteItem_t item;
    item.type = EEPROM_BOOL;
    strcpy(item.key, key);
    item.value.boolVal = value;
    xQueueSend(eepromWriteQueue, &item, 0);
}

void clearAllPreferences() {
    Serial.println("Clearing all EEPROM preferences...");
    prefs.clear();
    Serial.println("All preferences cleared. Restarting system...");
    ESP.restart();
}

// NVS layout for one persisted DM (outgoing=false and read=false are implied on restore)
typedef struct __attribute__((packed)) {
    uint32_t from;
    uint32_t to;
    uint8_t  channel;
    uint32_t timestamp;
    char     text[CHAT_TEXT_SIZE];
} nvsDmEntry_t;

void saveDmsToNvs(void) {
    chatMessage_t buf[DM_SLOT_MAX];
    size_t n = chatBufferSnapshotUnreadDms(buf, DM_SLOT_MAX);

    prefs.putUInt("my_nd_num", my_node_num);  // restored at load so dmPredicate filter works
    prefs.putInt("dm_count", (int)n);
    for (size_t i = 0; i < n; i++) {
        char key[6];
        snprintf(key, sizeof(key), "dm_%u", (unsigned)i);
        nvsDmEntry_t entry;
        entry.from      = buf[i].from;
        entry.to        = buf[i].to;
        entry.channel   = buf[i].channel;
        entry.timestamp = buf[i].timestamp;
        memcpy(entry.text, buf[i].text, CHAT_TEXT_SIZE);
        prefs.putBytes(key, &entry, sizeof(nvsDmEntry_t));
    }
    Serial.printf("saveDmsToNvs: saved %u unread DM(s)\n", (unsigned)n);
}

void loadDmsFromNvs(void) {
    int n = prefs.getInt("dm_count", 0);
    if (n <= 0) return;
    if (n > DM_SLOT_MAX) n = DM_SLOT_MAX;

    // Restore node num so chatBufferAppend's dmPredicate filter accepts these messages.
    // The real value arrives from GCM shortly after boot and overwrites this.
    uint32_t stored_node_num = prefs.getUInt("my_nd_num", 0);
    if (stored_node_num != 0) my_node_num = stored_node_num;

    // NVS is NOT cleared here. saveDmsToNvs() writes dm_count=0 only when
    // the user marks all DMs read, so unread DMs survive any unexpected power loss.

    int restored = 0;
    for (int i = 0; i < n; i++) {
        char key[6];
        snprintf(key, sizeof(key), "dm_%u", (unsigned)i);
        nvsDmEntry_t entry;
        size_t bytes_read = prefs.getBytes(key, &entry, sizeof(nvsDmEntry_t));
        if (bytes_read != sizeof(nvsDmEntry_t)) continue;

        chatMessage_t cm = {};
        cm.from      = entry.from;
        cm.to        = entry.to;
        cm.channel   = entry.channel;
        cm.timestamp = entry.timestamp;
        cm.outgoing  = false;
        cm.read      = false;
        memcpy(cm.text, entry.text, CHAT_TEXT_SIZE);
        chatBufferAppend(&cm);
        restored++;
    }

    if (restored > 0) pendingDmRestoreBeep = true;
    Serial.printf("loadDmsFromNvs: restored %d unread DM(s)\n", restored);
}