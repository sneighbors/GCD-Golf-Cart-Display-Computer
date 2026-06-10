#include "globals.h"
#include "config.h"

// FreeRTOS handles
TaskHandle_t gpsTaskHandle = NULL;
TaskHandle_t meshtasticTaskHandle = NULL;
TaskHandle_t meshtasticCallbackTaskHandle = NULL;
TaskHandle_t eepromTaskHandle = NULL;
TaskHandle_t systemTaskHandle = NULL;
TaskHandle_t espnowTaskHandle = NULL;
TaskHandle_t bleTaskHandle = NULL;

// Synchronization objects
SemaphoreHandle_t gpsMutex;
SemaphoreHandle_t eepromMutex;
SemaphoreHandle_t hotPacketMutex;  // Protects hot packet buffer swapping (not data reads)
SemaphoreHandle_t chatBufferMutex; // Guards the chat ring buffer
QueueHandle_t eepromWriteQueue;
QueueHandle_t meshtasticCallbackQueue;
QueueHandle_t espnowRecvQueue;
QueueHandle_t gpsConfigCallbackQueue;
QueueHandle_t chatTxQueue;
QueueHandle_t bleNotifyQueue = nullptr;

// Double buffering for hot packet data (eliminates blocking reads)
volatile int hotPacketActiveBufferWx = 0;  // 0 or 1, weather fields only
volatile int hotPacketActiveBufferNp = 0;  // 0 or 1, venue/event data only
char hotPacketBuffer_wx_rcv_time[2][HP_RCV_TIME_SIZE];
char hotPacketBuffer_cur_temp[2][HP_CUR_TEMP_SIZE];
char hotPacketBuffer_fcast_hr1[2][HP_FCAST_HR_SIZE];
char hotPacketBuffer_fcast_glyph1[2][HP_FCAST_GLYPH_SIZE];
char hotPacketBuffer_fcast_temp1[2][HP_FCAST_TEMP_SIZE];
char hotPacketBuffer_fcast_precip1[2][HP_FCAST_PRECIP_SIZE];
char hotPacketBuffer_fcast_hr2[2][HP_FCAST_HR_SIZE];
char hotPacketBuffer_fcast_glyph2[2][HP_FCAST_GLYPH_SIZE];
char hotPacketBuffer_fcast_temp2[2][HP_FCAST_TEMP_SIZE];
char hotPacketBuffer_fcast_precip2[2][HP_FCAST_PRECIP_SIZE];
char hotPacketBuffer_fcast_hr3[2][HP_FCAST_HR_SIZE];
char hotPacketBuffer_fcast_glyph3[2][HP_FCAST_GLYPH_SIZE];
char hotPacketBuffer_fcast_temp3[2][HP_FCAST_TEMP_SIZE];
char hotPacketBuffer_fcast_precip3[2][HP_FCAST_PRECIP_SIZE];
char hotPacketBuffer_fcast_hr4[2][HP_FCAST_HR_SIZE];
char hotPacketBuffer_fcast_glyph4[2][HP_FCAST_GLYPH_SIZE];
char hotPacketBuffer_fcast_temp4[2][HP_FCAST_TEMP_SIZE];
char hotPacketBuffer_fcast_precip4[2][HP_FCAST_PRECIP_SIZE];
char hotPacketBuffer_np_rcv_time[2][HP_RCV_TIME_SIZE];
char hotPacketBuffer_live_venue_event_data[2][HP_VENUE_DATA_SIZE];

// GPS objects
HardwareSerial &gpsSerial = Serial;
NMEAGPS gps;
gps_fix fix;

// Time zone definitions
TimeChangeRule mySTD = {"EST", First, Sun, Nov, 2, -300};
TimeChangeRule myDST = {"EDT", Second, Sun, Mar, 2, -240};
TimeChangeRule *tcr;
Timezone myTZ(myDST, mySTD);
JC_Sunrise sun{MY_LATITUDE, MY_LONGITUDE, JC_Sunrise::officialZenith};

// Time variables
time_t localTime, utcTime;
int timesetinterval = 60;

// Preferences
Preferences prefs;

// GPS and time variables (NOT in get_set_vars.h)
int localYear, localMonth, localDay = 0, old_localDay = 0;
int localHour, localMinute, localSecond, localDayOfWeek;
String latitude;
String longitude;
String altitude;
float hdop;
int old_day_backlight, old_night_backlight;
unsigned long lastGpsTimeUpdate = 0;  // Tracks when GPS time was last received
float avg_speed_calc = 0.0;  // Float version for GPS calculations

// Home location variables
float homeLatitude = 0.0;
float homeLongitude = 0.0;
bool homeLocationSet = false;

// Old tracking variables
String old_espnow_gci_mac_addr;
bool old_flip_screen = false;
int32_t old_speaker_volume = 10;
int32_t old_svc_interval_hrs = 100;
float old_temperature_adj = 0.0;

// Now playing variables
int np_stored_date = 0;
String np_stored_data = "";
String np_stored_timestamp = "";
bool np_eeprom_loaded = false;
bool np_data_is_stored = false;

// Weather EEPROM persistence variables
int wx_stored_date = 0;
String wx_stored_data = "";
String wx_stored_timestamp = "";
bool wx_eeprom_loaded = false;
bool wx_data_is_stored = false;

// Meshtastic variables
uint32_t next_send_time = 0;
bool not_yet_connected = true;
bool old_mesh_serial_enabled = true;
bool wakeNotificationSent = false;
bool reqWxEntNeeded = false;
bool reqWxEntSent = false;
bool gpsConfigAttempted = false;
bool handshakeComplete = false;
bool nodeListRefreshRequested = false;

// Inactivity timeout variables
uint32_t lastTouchActivity = 0;

// ESP-NOW variables
bool espnow_enabled = true;
bool old_espnow_enabled = false;
int espnow_peer_count = 0;

// Golf cart interface variables for incoming data
int modeHeadLights = -99;
int outdoorLux = -99;
float rawAirTemperature = -99;
float airTemperature = -99;
float battVoltage = -99;
float fuelLevel = -99;  // -99 = no valid reading; updated when GCI telemetry arrives
float fuel_low_percent = 20.0f;
float old_fuel_low_percent = 20.0f;
structMsgFromGci dataFromGci;

// Golf cart interface variables for outbound data
int cmdToGci;
structMsgToGci dataToGci;

// Sleep mode state machine variables
sleep_operating_mode_t sleep_operating_mode = SLEEP_MODE_STARTUP_GRACE;
uint32_t startup_time_ms = 0;
bool gci_communicated_flag = false;
bool backlight_dimmed = false;
uint32_t last_activity_time_ms = 0;
uint32_t gci_disconnect_time_ms = 0;  // 0 = GCI connected, non-zero = time of disconnect
int32_t old_backlight_timeout = 5;

// Pending audio notification: one double-beep after splash exits if DMs were restored from NVS
bool pendingDmRestoreBeep = false;

// EEPROM debounce timestamps (0 = no pending write, non-zero = time of last change)
uint32_t debounce_day_backlight = 0;
uint32_t debounce_night_backlight = 0;
uint32_t debounce_speaker_volume = 0;
uint32_t debounce_svc_interval_hrs = 0;
uint32_t debounce_temperature_adj = 0;
uint32_t debounce_backlight_timeout = 0;
uint32_t debounce_fuel_low_percent = 0;
int32_t fuelSensorType = 0;

// Note: All EEZ Studio shared variables (cur_date, version, etc.)
// are defined in get_set_vars.cpp via get_set_vars.h