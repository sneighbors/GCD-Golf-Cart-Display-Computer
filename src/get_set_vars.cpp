#include "get_set_vars.h"
#include <Arduino.h>
#include "storage/preferences_manager.h"
#include "communication/espnow_handler.h"
#include "communication/chat_buffer.h"
#include "globals.h"

// String variable definitions
String cur_date;
String heading;
String hhmmss_str;
String hhmm_str;
String am_pm_str;
String sats_hdop;
String gcd_version;
String gci_version;
String cyd_mac_addr;
String espnow_gci_mac_addr;
String wx_rcv_time;
String cur_temp;
String fcast_hr1;
String fcast_glyph1;
String fcast_temp1;
String fcast_precip1;
String fcast_hr2;
String fcast_glyph2;
String fcast_temp2;
String fcast_precip2;
String fcast_hr3;
String fcast_glyph3;
String fcast_temp3;
String fcast_precip3;
String fcast_hr4;
String fcast_glyph4;
String fcast_temp4;
String fcast_precip4;
String espnow_status;
String espnow_last_received;
String gcm_node_id;
String text_message;

// Numeric variable definitions
int32_t avg_speed = 0;
int32_t day_backlight = 10;
int32_t night_backlight = 5;
bool manual_reboot = false;
bool new_rx_data_flag = false;
bool mesh_serial_enabled = true;
bool espnow_connected = false;
bool espnow_pair_gci = false;
int32_t screen_inactivity_countdown = -1;
bool flip_screen = false;
int32_t speaker_volume = 10;
int32_t backlight_timeout = 5;  // Default 5 minutes
String odometer = "0.0";
String trip_odometer = "0.0";
int32_t hrs_since_svc = 0;
int32_t svc_interval_hrs = 100;
float accum_distance = 0.0;
float trip_distance = 0.0;
bool reset_preferences = false;
float temperature_adj = 0;
bool reboot_meshtastic = false;
bool set_home_loc = false;
int32_t home_gps_fence_radius_m = 500;  // Default 500 meter radius
bool at_home = false;
bool is_daytime = true;  // Default to daytime
bool headlights_due = true;  // Default on until GPS provides time
String cur_lat;
String cur_long;
String sunrise_time_str = "_:__";
String sunset_time_str  = "_:__";
int32_t mesh_filter = 0;  // 0 = DIRECT MSGS
bool headlights_on = false;
int32_t lux_now = -99;
int32_t lux_lights_on  = 200;  // Default: turn headlights on below 200 lux
int32_t lux_lights_off = 400;  // Default: turn headlights off above 400 lux

// Static buffers for C string returns
static char temp_buffer[256];

// Function implementations with C linkage
extern "C" {

int32_t get_var_mesh_filter() {
    return mesh_filter;
}

void set_var_mesh_filter(int32_t value) {
    if (value != mesh_filter) {
        mesh_filter = value;
        queuePreferenceWrite("mesh_filter", (int)value);
    }
}

const char* get_var_cur_date() {
    return cur_date.c_str();
}

void set_var_cur_date(const char* value) {
    cur_date = String(value);
}

const char* get_var_heading() {
    return heading.c_str();
}

void set_var_heading(const char* value) {
    heading = String(value);
}

const char* get_var_hhmmss_str() {
    return hhmmss_str.c_str();
}

void set_var_hhmmss_str(const char* value) {
    hhmmss_str = String(value);
}

const char* get_var_hhmm_str() {
    return hhmm_str.c_str();
}

void set_var_hhmm_str(const char* value) {
    hhmm_str = String(value);
}

const char* get_var_am_pm_str() {
    return am_pm_str.c_str();
}

void set_var_am_pm_str(const char* value) {
    am_pm_str = String(value);
}

const char* get_var_sats_hdop() {
    return sats_hdop.c_str();
}

void set_var_sats_hdop(const char* value) {
    sats_hdop = String(value);
}

int32_t get_var_avg_speed() {
    return avg_speed;
}

void set_var_avg_speed(int32_t value) {
    avg_speed = value;
}

const char* get_var_gcd_version() {
    return gcd_version.c_str();
}

void set_var_gcd_version(const char* value) {
    gcd_version = String(value);
}

const char* get_var_gci_version() {
    return gci_version.c_str();
}

void set_var_gci_version(const char* value) {
    gci_version = String(value);
}

int32_t get_var_cyd_day_backlight() {
    return day_backlight;
}

void set_var_cyd_day_backlight(int32_t value) {
    day_backlight = value;
}

int32_t get_var_cyd_night_backlight() {
    return night_backlight;
}

void set_var_cyd_night_backlight(int32_t value) {
    night_backlight = value;
}

const char* get_var_cyd_mac_addr() {
    return cyd_mac_addr.c_str();
}

void set_var_cyd_mac_addr(const char* value) {
    cyd_mac_addr = String(value);
}

bool get_var_manual_reboot() {
    return manual_reboot;
}

void set_var_manual_reboot(bool value) {
    manual_reboot = value;
}

bool get_var_new_rx_data_flag() {
    return new_rx_data_flag;
}

void set_var_new_rx_data_flag(bool value) {
    new_rx_data_flag = value;
}

const char* get_var_espnow_gci_mac_addr() {
    return espnow_gci_mac_addr.c_str();
}

void set_var_espnow_gci_mac_addr(const char* value) {
    String new_mac = String(value);

    if (espnow_gci_mac_addr != new_mac) {
        Serial.print("ESP-NOW GCI MAC address changed from ");
        Serial.print(espnow_gci_mac_addr);
        Serial.print(" to ");
        Serial.println(new_mac);

        espnow_gci_mac_addr = new_mac;

        // Restart ESP-NOW if it's currently enabled and initialized
        if (espnow_enabled && espNow.isInitialized()) {
            Serial.println("Restarting ESP-NOW with new peer MAC address...");

            if (espNow.restart()) {
                // Add the new peer if it's valid
                if (espnow_gci_mac_addr != "NONE" && espnow_gci_mac_addr.length() == 17) {
                    if (espNow.addPeerFromString(espnow_gci_mac_addr, "New Peer")) {
                        Serial.println("ESP-NOW: New peer added successfully");
                    } else {
                        Serial.println("ESP-NOW: Failed to add new peer");
                    }
                }
            } else {
                Serial.println("ESP-NOW: Restart failed");
            }
        }

        // Queue the preference write to save to EEPROM
        queuePreferenceWrite("espnow_gci_mac_addr", espnow_gci_mac_addr);
    }
}

bool get_var_mesh_serial_enabled() {
    return mesh_serial_enabled;
}

void set_var_mesh_serial_enabled(bool value) {
    mesh_serial_enabled = value;
}

bool get_var_espnow_connected() {
    return espnow_connected;
}

void set_var_espnow_connected(bool value) {
    espnow_connected = value;
}

const char* get_var_wx_rcv_time() {
    if (lastGpsTimeUpdate == 0) return "";
    if (wx_data_is_stored) {
        static String storedStr;
        storedStr = wx_rcv_time + " (stored)";
        return storedStr.c_str();
    }
    return wx_rcv_time.c_str();
}

void set_var_wx_rcv_time(const char* value) {
    wx_rcv_time = String(value);
}

const char* get_var_cur_temp() {
    // Prefer ESP-NOW air temperature if available, fallback to Meshtastic weather data
    static String tempStr;

    if (rawAirTemperature != -99) {
        // ESP-NOW temperature is available (has real-time data); apply adjustment dynamically
        tempStr = String((int)round(rawAirTemperature + temperature_adj));
        return tempStr.c_str();
    } else if (lastGpsTimeUpdate != 0) {
        // Fallback to Meshtastic weather data (only when GPS has synced)
        // Note: cur_temp is protected by hotPacketMutex in parseWeatherData
        // For UI getter, we skip mutex to avoid blocking LVGL refresh
        return cur_temp.c_str();
    } else {
        return "";
    }
}

void set_var_cur_temp(const char* value) {
    cur_temp = String(value);
}

const char* get_var_fcast_hr1() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_hr1.c_str();
}

void set_var_fcast_hr1(const char* value) {
    fcast_hr1 = String(value);
}

const char* get_var_fcast_glyph1() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_glyph1.c_str();
}

void set_var_fcast_glyph1(const char* value) {
    fcast_glyph1 = String(value);
}

const char* get_var_fcast_temp1() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_temp1.c_str();
}

void set_var_fcast_temp1(const char* value) {
    fcast_temp1 = String(value);
}

const char* get_var_fcast_precip1() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_precip1.c_str();
}

void set_var_fcast_precip1(const char* value) {
    fcast_precip1 = String(value);
}

const char* get_var_fcast_hr2() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_hr2.c_str();
}

void set_var_fcast_hr2(const char* value) {
    fcast_hr2 = String(value);
}

const char* get_var_fcast_glyph2() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_glyph2.c_str();
}

void set_var_fcast_glyph2(const char* value) {
    fcast_glyph2 = String(value);
}

const char* get_var_fcast_temp2() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_temp2.c_str();
}

void set_var_fcast_temp2(const char* value) {
    fcast_temp2 = String(value);
}

const char* get_var_fcast_precip2() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_precip2.c_str();
}

void set_var_fcast_precip2(const char* value) {
    fcast_precip2 = String(value);
}

const char* get_var_fcast_hr3() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_hr3.c_str();
}

void set_var_fcast_hr3(const char* value) {
    fcast_hr3 = String(value);
}

const char* get_var_fcast_glyph3() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_glyph3.c_str();
}

void set_var_fcast_glyph3(const char* value) {
    fcast_glyph3 = String(value);
}

const char* get_var_fcast_temp3() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_temp3.c_str();
}

void set_var_fcast_temp3(const char* value) {
    fcast_temp3 = String(value);
}

const char* get_var_fcast_precip3() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_precip3.c_str();
}

void set_var_fcast_precip3(const char* value) {
    fcast_precip3 = String(value);
}

const char* get_var_fcast_hr4() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_hr4.c_str();
}

void set_var_fcast_hr4(const char* value) {
    fcast_hr4 = String(value);
}

const char* get_var_fcast_glyph4() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_glyph4.c_str();
}

void set_var_fcast_glyph4(const char* value) {
    fcast_glyph4 = String(value);
}

const char* get_var_fcast_temp4() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_temp4.c_str();
}

void set_var_fcast_temp4(const char* value) {
    fcast_temp4 = String(value);
}

const char* get_var_fcast_precip4() {
    if (lastGpsTimeUpdate == 0) return "";
    return fcast_precip4.c_str();
}

void set_var_fcast_precip4(const char* value) {
    fcast_precip4 = String(value);
}

const char* get_var_np_rcv_time() {
    const char* t = hotPacketBuffer_np_rcv_time[hotPacketActiveBufferNp];
    if (np_data_is_stored && t[0] != '\0') {
        static char storedBuf[HP_RCV_TIME_SIZE + 10];
        snprintf(storedBuf, sizeof(storedBuf), "%s (stored)", t);
        return storedBuf;
    }
    return t;
}

void set_var_np_rcv_time(const char* value) {
    (void)value;
}


const char* get_var_espnow_status() {
    return espnow_status.c_str();
}

void set_var_espnow_status(const char* value) {
    espnow_status = String(value);
}

const char* get_var_espnow_last_received() {
    return espnow_last_received.c_str();
}

void set_var_espnow_last_received(const char* value) {
    espnow_last_received = String(value);
}

const char* get_var_gcm_node_id() {
    return gcm_node_id.c_str();
}

void set_var_gcm_node_id(const char* value) {
    gcm_node_id = String(value);
}

int32_t get_var_screen_inactivity_countdown() {
    return screen_inactivity_countdown;
}

void set_var_screen_inactivity_countdown(int32_t value) {
    screen_inactivity_countdown = value;
}

bool get_var_flip_screen() {
    return flip_screen;
}

void set_var_flip_screen(bool value) {
    if (flip_screen != value) {
        Serial.print("flip_screen changed from ");
        Serial.print(flip_screen ? "true" : "false");
        Serial.print(" to ");
        Serial.println(value ? "true" : "false");

        flip_screen = value;

        // Queue the preference write to save to EEPROM
        queuePreferenceWrite("flip_screen", value);
    }
}

int32_t get_var_speaker_volume() {
    return speaker_volume;
}

void set_var_speaker_volume(int32_t value) {
    speaker_volume = value;
}

int32_t get_var_backlight_timeout() {
    return backlight_timeout;
}

void set_var_backlight_timeout(int32_t value) {
    backlight_timeout = value;
}

const char* get_var_odometer() {
    return odometer.c_str();
}

void set_var_odometer(const char* value) {
    odometer = String(value);
}

const char* get_var_trip_odometer() {
    return trip_odometer.c_str();
}

void set_var_trip_odometer(const char* value) {
    trip_odometer = String(value);
}

/**
 * Hours Since Service - Storage Format Documentation
 *
 * IMPORTANT: hrs_since_svc is stored EVERYWHERE as tenths of hours (0.1 hour = 6 minute resolution)
 *
 * Storage Locations (all as tenths):
 *   - EEPROM: int32_t stored as tenths (e.g., 15 = 1.5 hours)
 *   - Global variable hrs_since_svc: int32_t tenths
 *   - GPS task accumulation: converts to/from seconds for calculation
 *
 * Conversions:
 *   - UI Display: hrs_since_svc / 10 → whole hours
 *   - UI Input: whole hours * 10 → hrs_since_svc
 *   - GPS Init: hrs_since_svc * 360 → accumSeconds (for tracking)
 *   - GPS Accumulate: accumSeconds / 360 → hrs_since_svc
 *
 * Example: 125 tenths = 12.5 hours = 12 hours 30 minutes
 */

int32_t get_var_hrs_since_svc() {
    // Convert tenths of hours to whole hours for UI display
    return hrs_since_svc / 10;
}

void set_var_hrs_since_svc(int32_t value) {
    // Convert whole hours from UI to tenths for internal storage
    hrs_since_svc = value * 10;
    // Save immediately when user resets from UI (typically to 0 after service)
    queuePreferenceWrite("hrs_since_svc", hrs_since_svc);
}

int32_t get_var_svc_interval_hrs() {
    return svc_interval_hrs;
}

void set_var_svc_interval_hrs(int32_t value) {
    svc_interval_hrs = value;
}

float get_var_accum_distance() {
    return accum_distance;
}

void set_var_accum_distance(float value) {
    accum_distance = value;
    // Update formatted display string
    odometer = String(accum_distance, 1);
}

float get_var_trip_distance() {
    return trip_distance;
}

void set_var_trip_distance(float value) {
    trip_distance = value;
    // Update formatted display string
    trip_odometer = String(trip_distance, 1);
}

bool get_var_reset_preferences() {
    return reset_preferences;
}

void set_var_reset_preferences(bool value) {
    reset_preferences = value;
}

bool get_var_espnow_pair_gci() {
    return espnow_pair_gci;
}

void set_var_espnow_pair_gci(bool value) {
    espnow_pair_gci = value;
}

float get_var_temperature_adj() {
    return temperature_adj;
}

void set_var_temperature_adj(float value) {
    temperature_adj = value;
}

float get_var_fuel_level() {
    if (fuelSensorType == FUEL_SENSOR_NONE) return -99.0f;
    return fuelLevel;
}

void set_var_fuel_level(float value) {
    fuelLevel = value;
}

float get_var_fuel_low_percent() {
    return fuel_low_percent;
}

void set_var_fuel_low_percent(float value) {
    fuel_low_percent = value;
}

int32_t get_var_fuel_sense_type() {
    return fuelSensorType;
}

void set_var_fuel_sense_type(int32_t value) {
    fuelSensorType = value;
    queuePreferenceWrite("fuel_sense_type", (int)value);
    espNow.sendFuelConfig();
    // GPIO EXPANDER only reports 25/50/75/100; a threshold below 25 would never trigger.
    if (value == FUEL_SENSOR_GPIO_EXP && fuel_low_percent < 25.0f)
        set_var_fuel_low_percent(25.0f);
}

bool get_var_reboot_meshtastic() {
    return reboot_meshtastic;
}

void set_var_reboot_meshtastic(bool value) {
    reboot_meshtastic = value;
}

const char* get_var_text_message() {
    return text_message.c_str();
}

void set_var_text_message(const char* value) {
    text_message = String(value);
}

bool get_var_set_home_loc() {
    return set_home_loc;
}

void set_var_set_home_loc(bool value) {
    set_home_loc = value;
}

int32_t get_var_home_gps_fence_radius_m() {
    return home_gps_fence_radius_m;
}

void set_var_home_gps_fence_radius_m(int32_t value) {
    // Round down to nearest 100 meters
    int32_t rounded_value = value - (value % 100);

    if (home_gps_fence_radius_m != rounded_value) {
        Serial.print("home_gps_fence_radius_m changed from ");
        Serial.print(home_gps_fence_radius_m);
        Serial.print(" to ");
        Serial.print(value);
        Serial.print(" (rounded to ");
        Serial.print(rounded_value);
        Serial.println(")");

        // Store the rounded value for both calculations and EEPROM
        home_gps_fence_radius_m = rounded_value;

        // Save to preferences
        queuePreferenceWrite("home_fence_m", rounded_value);
    }
}

bool get_var_at_home() {
    return at_home;
}

void set_var_at_home(bool value) {
    at_home = value;
}

bool get_var_is_daytime() {
    return is_daytime;
}

void set_var_is_daytime(bool value) {
    is_daytime = value;
}

const char* get_var_cur_lat() {
    return cur_lat.c_str();
}

void set_var_cur_lat(const char* value) {
    cur_lat = String(value);
}

const char* get_var_cur_long() {
    return cur_long.c_str();
}

void set_var_cur_long(const char* value) {
    cur_long = String(value);
}

const char* get_var_sunrise_time_str() {
    return sunrise_time_str.c_str();
}

void set_var_sunrise_time_str(const char* value) {
    sunrise_time_str = String(value);
}

const char* get_var_sunset_time_str() {
    return sunset_time_str.c_str();
}

void set_var_sunset_time_str(const char* value) {
    sunset_time_str = String(value);
}

int32_t get_var_num_unread_direct_msgs() {
    return (int32_t)chatBufferUnreadDmCount();
}

void set_var_num_unread_direct_msgs(int32_t value) {
    (void)value;
}

bool get_var_headlights_on() {
    return headlights_on;
}

void set_var_headlights_on(bool value) {
    headlights_on = value;
}

int32_t get_var_lux_now() {
    return lux_now;
}

void set_var_lux_now(int32_t value) {
    lux_now = value;
}

int32_t get_var_lux_lights_on() {
    return lux_lights_on;
}

void set_var_lux_lights_on(int32_t value) {
    if (lux_lights_on != value) {
        lux_lights_on = value;
        queuePreferenceWrite("lux_lights_on", (int)value);
        espNow.sendFuelConfig();
        if (lux_now >= 0) {
            if (lux_now < lux_lights_on && !headlights_on)
                headlights_on = true;
            else if (lux_now >= lux_lights_off && headlights_on)
                headlights_on = false;
        }
    }
}

int32_t get_var_lux_lights_off() {
    return lux_lights_off;
}

void set_var_lux_lights_off(int32_t value) {
    if (lux_lights_off != value) {
        lux_lights_off = value;
        queuePreferenceWrite("lux_lights_off", (int)value);
        espNow.sendFuelConfig();
        if (lux_now >= 0) {
            if (lux_now < lux_lights_on && !headlights_on)
                headlights_on = true;
            else if (lux_now >= lux_lights_off && headlights_on)
                headlights_on = false;
        }
    }
}

} // extern "C"