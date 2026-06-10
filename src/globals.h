#ifndef GLOBALS_H
#define GLOBALS_H

#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <NMEAGPS.h>
#include <Timezone.h>
#include <Preferences.h>
#include <JC_Sunrise.h>
#include "types.h"

// FreeRTOS handles
extern TaskHandle_t gpsTaskHandle;
extern TaskHandle_t meshtasticTaskHandle;
extern TaskHandle_t meshtasticCallbackTaskHandle;
extern TaskHandle_t eepromTaskHandle;
extern TaskHandle_t systemTaskHandle;
extern TaskHandle_t espnowTaskHandle;
extern TaskHandle_t bleTaskHandle;

// Synchronization objects
extern SemaphoreHandle_t gpsMutex;
extern SemaphoreHandle_t eepromMutex;
extern SemaphoreHandle_t hotPacketMutex;  // Protects hot packet buffer swapping (not data reads)
extern SemaphoreHandle_t chatBufferMutex;  // Guards the chat ring buffer
extern QueueHandle_t eepromWriteQueue;
extern QueueHandle_t meshtasticCallbackQueue;
extern QueueHandle_t espnowRecvQueue;
extern QueueHandle_t gpsConfigCallbackQueue;
extern QueueHandle_t chatTxQueue;       // UI -> meshtasticTask send pipe
extern QueueHandle_t bleNotifyQueue;    // ble_task outbound notification queue

// Double buffering for hot packet data (eliminates blocking reads)
// Parser writes to back buffer, swaps atomically, GUI reads from front buffer
// Front buffer = hotPacketBuffer_xxx[hotPacketActiveBuffer_Wx/Np] (current data for GUI reads)
// Back buffer = hotPacketBuffer_xxx[1 - hotPacketActiveBuffer_Wx/Np] (next data being written)
// Weather and venue use SEPARATE indices so a weather swap never disturbs the venue front buffer.
// Only the buffer pointer swap is protected by mutex (~10ms), not the data reads/writes
extern volatile int hotPacketActiveBufferWx;  // 0 or 1, weather fields only
extern volatile int hotPacketActiveBufferNp;  // 0 or 1, venue/event data only
extern char hotPacketBuffer_wx_rcv_time[2][HP_RCV_TIME_SIZE];
extern char hotPacketBuffer_cur_temp[2][HP_CUR_TEMP_SIZE];
extern char hotPacketBuffer_fcast_hr1[2][HP_FCAST_HR_SIZE];
extern char hotPacketBuffer_fcast_glyph1[2][HP_FCAST_GLYPH_SIZE];
extern char hotPacketBuffer_fcast_temp1[2][HP_FCAST_TEMP_SIZE];
extern char hotPacketBuffer_fcast_precip1[2][HP_FCAST_PRECIP_SIZE];
extern char hotPacketBuffer_fcast_hr2[2][HP_FCAST_HR_SIZE];
extern char hotPacketBuffer_fcast_glyph2[2][HP_FCAST_GLYPH_SIZE];
extern char hotPacketBuffer_fcast_temp2[2][HP_FCAST_TEMP_SIZE];
extern char hotPacketBuffer_fcast_precip2[2][HP_FCAST_PRECIP_SIZE];
extern char hotPacketBuffer_fcast_hr3[2][HP_FCAST_HR_SIZE];
extern char hotPacketBuffer_fcast_glyph3[2][HP_FCAST_GLYPH_SIZE];
extern char hotPacketBuffer_fcast_temp3[2][HP_FCAST_TEMP_SIZE];
extern char hotPacketBuffer_fcast_precip3[2][HP_FCAST_PRECIP_SIZE];
extern char hotPacketBuffer_fcast_hr4[2][HP_FCAST_HR_SIZE];
extern char hotPacketBuffer_fcast_glyph4[2][HP_FCAST_GLYPH_SIZE];
extern char hotPacketBuffer_fcast_temp4[2][HP_FCAST_TEMP_SIZE];
extern char hotPacketBuffer_fcast_precip4[2][HP_FCAST_PRECIP_SIZE];
extern char hotPacketBuffer_np_rcv_time[2][HP_RCV_TIME_SIZE];
extern char hotPacketBuffer_live_venue_event_data[2][HP_VENUE_DATA_SIZE];

// GPS objects
extern HardwareSerial &gpsSerial;
extern NMEAGPS gps;
extern gps_fix fix;

// Time objects
extern Timezone myTZ;
extern JC_Sunrise sun;
extern time_t localTime, utcTime;
extern TimeChangeRule *tcr;

// Preferences
extern Preferences prefs;

// GPS and time variables (NOT in get_set_vars.h)
extern int localYear, localMonth, localDay, old_localDay;
extern int localHour, localMinute, localSecond, localDayOfWeek;
extern String latitude, longitude, altitude;
extern float hdop;
extern int old_day_backlight, old_night_backlight;
extern unsigned long lastGpsTimeUpdate;  // Tracks when GPS time was last received
extern float avg_speed_calc;  // Float version for GPS calculations

// Home location variables (NOT in get_set_vars.h)
extern float homeLatitude;    // Home location latitude
extern float homeLongitude;   // Home location longitude
extern bool homeLocationSet;  // True if home location has been saved

// Old tracking variables (NOT in get_set_vars.h)
extern String old_espnow_gci_mac_addr;
extern bool old_flip_screen;
extern float old_temperature_adj;

// Now playing variables (NOT in get_set_vars.h)
extern int np_stored_date;          // YYYYMMDD of entertainment data in NVS (0 = none)
extern String np_stored_data;       // Raw entertainment packet loaded from NVS at boot
extern String np_stored_timestamp;  // Receive timestamp of entertainment data in NVS
extern bool np_eeprom_loaded;       // True once GPS has validated boot-loaded np data
extern bool np_data_is_stored;      // True while displaying EEPROM data; clears on live packet

// Weather EEPROM persistence variables (NOT in get_set_vars.h)
extern int wx_stored_date;          // YYYYMMDD of weather data in NVS (0 = none)
extern String wx_stored_data;       // Raw weather packet loaded from NVS at boot
extern String wx_stored_timestamp;  // Receive timestamp of weather data in NVS
extern bool wx_eeprom_loaded;       // True once GPS has validated boot-loaded wx data
extern bool wx_data_is_stored;      // True while displaying EEPROM data; clears on live packet

// Meshtastic variables (NOT in get_set_vars.h)
extern uint32_t next_send_time;
extern bool not_yet_connected;
extern bool old_mesh_serial_enabled;
extern bool wakeNotificationSent;
extern bool reqWxEntNeeded;
extern bool reqWxEntSent;
extern bool gpsConfigAttempted;
extern bool handshakeComplete;
extern bool nodeListRefreshRequested;

// Inactivity timeout variables (NOT in get_set_vars.h)
extern uint32_t lastTouchActivity;

// ESP-NOW variables (NOT in get_set_vars.h)
extern bool espnow_enabled;
extern bool old_espnow_enabled;
extern int espnow_peer_count;

// Golf cart interface variables for incoming data
extern int modeHeadLights;
extern int outdoorLux;
extern float rawAirTemperature;
extern float airTemperature;
extern float battVoltage;
extern float fuelLevel;
extern float fuel_low_percent;
extern float old_fuel_low_percent;
extern structMsgFromGci dataFromGci;

// Golf cart interface variables for outbound data
extern int cmdToGci;
extern structMsgToGci dataToGci;

// Speaker
extern int32_t old_speaker_volume;

// Service interval (manually adjustable, needs old value tracking)
extern int32_t old_svc_interval_hrs;

// Sleep mode state machine variables
extern sleep_operating_mode_t sleep_operating_mode;
extern uint32_t startup_time_ms;
extern bool gci_communicated_flag;
extern bool backlight_dimmed;
extern uint32_t last_activity_time_ms;
extern uint32_t gci_disconnect_time_ms;  // 0 = GCI connected, non-zero = time of disconnect
extern int32_t old_backlight_timeout;

// Pending audio notification: one double-beep after splash exits if DMs were restored from NVS
extern bool pendingDmRestoreBeep;

// EEPROM debounce timestamps (0 = no pending write, non-zero = time of last change)
extern uint32_t debounce_day_backlight;
extern uint32_t debounce_night_backlight;
extern uint32_t debounce_speaker_volume;
extern uint32_t debounce_svc_interval_hrs;
extern uint32_t debounce_temperature_adj;
extern uint32_t debounce_backlight_timeout;
extern uint32_t debounce_fuel_low_percent;
extern int32_t fuelSensorType;

// All EEZ Studio variables are defined in get_set_vars.h
// Include it here so all files can access them
#include "get_set_vars.h"

#endif // GLOBALS_H