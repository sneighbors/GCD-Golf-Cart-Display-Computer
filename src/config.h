#ifndef CONFIG_H
#define CONFIG_H

// Debug Settings
#define DEBUG_GPS 0
// MT_DEBUGGING - LEAVE UNDEFINED (not 0) to disable low-level Meshtastic protocol debugging
// Upstream code uses #ifdef (not #if), so defining it to 0 still enables it!
// To enable: uncomment and set to 1
// #define MT_DEBUGGING 1
#define DEBUG_INIT 0                    // Initialization messages
#define DEBUG_ESP32_SLEEP 0
#define DEBUG_EEPROM 0                  // EEPROM read/write operations
#define DEBUG_SLEEP_STATE 0             // Sleep state machine transitions
#define DEBUG_MESHTASTIC_CONNECTION 0   // GCM connection/reconnection events
#define DEBUG_GCM_MESSAGES 0            // GCM (Meshtastic) messages sent and received
#define DEBUG_ESPNOW 0                  // ESP-NOW verbose messages (pairing & telemetry always shown)
#define DEBUG_OTA_TX_TEST 0             // Periodic T1,T2,T3... OTA test messages every 30s
#define DEBUG_OTA_TX_TEST_CHANNEL 3     // Channel to send OTA test messages on (corresponds to UI channel selector)

#define SHOW_HOT_PKTS_IN_MSGS 1         // 1 = show HoT packets in Messages screen; 0 = filter them out

// Speaker pin & default settings
#define SPEAKER_PIN 26
#define BEEP_FREQUENCY_HZ 2500
#define BEEP_DURATION_MS 100
#define BEEP_SPACING_MS BEEP_DURATION/2
#define SPEAKER_VOLUME 20

// Sleep pin (sleeps when LOW)
#define SLEEP_PIN 35

// Speaker LEDC configuration
#define SPEAKER_LEDC_CHANNEL 1
#define SPEAKER_LEDC_TIMER_BIT 8

// Meshtastic configuration
#define MT_SERIAL_TX_PIN 22
#define MT_SERIAL_RX_PIN 27
#define MT_DEV_BAUD_RATE 9600
#define MAX_MESHTASTIC_PAYLOAD 237
#define HOT_PKT_HEADER_OFFSET 5
#define SEND_PERIOD 300

// GPS configuration
#define GPS_RX_PIN 03
#define GPS_TX_PIN 01
#define GPS_BAUD 9600
#define MAX_GPS_TIME_STALENESS_SECS 60  // Show "NO GPS" if no time update for this many seconds
#define MIN_SPEED_FILTER_MPH 2.5        // Speeds below this are treated as 0 to filter GPS dither

// ESP-NOW configuration
#define ESPNOW_CHANNEL 1
#define ESPNOW_MAX_PEER_NUM 6
#define ESPNOW_MAX_PAYLOAD 240  // Max payload after wrapper overhead subtracted (ESP-NOW limit: 250 bytes, wrapper: 9 bytes, payload: 241 bytes)
#define ESPNOW_QUEUE_SIZE 10
#define ESPNOW_SEND_RETRY_COUNT 3
#define ESPNOW_SEND_RETRY_DELAY 100
#define ESPNOW_HEARTBEAT_INTERVAL 10000
#define ESPNOW_GPS_SEND_INTERVAL 60000
#define ESPNOW_PEER_TIMEOUT 40000  // 40 seconds - 4x heartbeat interval

// Default location (for sunrise/sunset before GPS lock)
#define MY_LATITUDE 28.8522f
#define MY_LONGITUDE -82.0028f

// Headlight offset: turn on this many seconds before sunset / off after sunrise
#define HEADLIGHT_SUNSET_OFFSET_SEC 600

// Sleep configuration
#define SLEEP_CHECK_INTERVAL_MS 100  // How often system task checks SLEEP_PIN (ms)

// EEPROM write debounce (prevents excessive writes when adjusting UI sliders/spinners)
#define EEPROM_DEBOUNCE_MS 2000  // Wait 2 seconds after last change before writing

// Hot packet double-buffer field sizes (char[] BSS, not heap String)
#define HP_RCV_TIME_SIZE     32   // "05/15/2026  4:43PM" + null
#define HP_CUR_TEMP_SIZE     12   // validated max 10 chars
#define HP_FCAST_HR_SIZE      8   // "12pm" max 6 chars
#define HP_FCAST_GLYPH_SIZE   4   // single digit, max 2 chars
#define HP_FCAST_TEMP_SIZE   12   // validated max 10 chars
#define HP_FCAST_PRECIP_SIZE  8   // "99.99" max 6 chars
#define HP_VENUE_DATA_SIZE  240   // MAX_MESHTASTIC_PAYLOAD(237) - HOT_PKT_HEADER_OFFSET(5) + margin

// Chat / messaging UI
#define CHAT_BUFFER_SIZE 32       // Ring depth for chat history (RAM-only)
#define CHAT_MAX_DISPLAY_ROWS 10  // Max rows rendered at once. Screen body ~200px, rows
                                  // 22px → 9 visible; 10 gives 1 extra scroll row. Rows
                                  // lazy-allocated on first Messages visit. 20 rows caused
                                  // OOM crash: ~15KB rows + ~30KB LVGL render transient
                                  // exhausted the ~46KB available at screen entry.
#define CANNED_REPLY_COUNT 8      // Number of canned reply slots
#define CANNED_REPLY_MAXLEN 60    // Max chars per canned reply (fits 64-byte stringVal)
// Shorter than MAX_MESHTASTIC_PAYLOAD (237) — chat rows are truncated to fit a 22 px
// display row anyway, and HoT/GC packets become ~12-char tags via chatAbbreviate().
// Saves ~7.5 KB of BSS (two 32-slot arrays) + ~2 KB of queue heap vs using 237.
#define CHAT_TEXT_SIZE 120        // Max text stored per chat message / TX / KB context

// BLE configuration
#define DEBUG_BLE 0                         // 1 = verbose BLE logging
#define BLE_DEVICE_NAME_PREFIX "GCD-"       // Runtime name is "GCD-XXYYZZ" (last 3 MAC bytes); never use this alone as the full name
#define BLE_NOTIFY_QUEUE_SIZE 20            // Max queued outbound notifications

// Heap monitoring (set DEBUG_HEAP to 0 to remove all logging once stable)
#define DEBUG_HEAP 0
#define DEBUG_HEAP_INTERVAL_MS 5000

// Task Stack Sizes (in bytes)
#define GPS_TASK_STACK_SIZE 4096
#define MESHTASTIC_TASK_STACK_SIZE 4096
#define MESHTASTIC_CALLBACK_TASK_STACK_SIZE 6144
#define EEPROM_TASK_STACK_SIZE 4096  // saveDmsToNvs() needs ~1.2KB beyond base overhead
#define SYSTEM_TASK_STACK_SIZE 4096  // Increased for GPS config init with debug output
#define ESPNOW_TASK_STACK_SIZE 4096
#define BLE_TASK_STACK_SIZE 6144

// Fuel / energy sensor type codes (must match EEZ Studio fuel_sense_type enum)
#define FUEL_SENSOR_NONE        0   // no sensor installed
#define FUEL_SENSOR_ADC_GAS     1   // analog voltage on ADC_FUEL_PIN (gasoline)
#define FUEL_SENSOR_GPIO_EXP    2   // MCP23008 I2C GPIO expander (gasoline, future)
#define FUEL_SENSOR_ADC_ELEC    3   // analog voltage (electric/battery, future)

// Task Priorities
#define GPS_TASK_PRIORITY 2
#define MESHTASTIC_TASK_PRIORITY 2
#define MESHTASTIC_CALLBACK_TASK_PRIORITY 2
#define ESPNOW_TASK_PRIORITY 2
#define BLE_TASK_PRIORITY 2
#define EEPROM_TASK_PRIORITY 1
#define SYSTEM_TASK_PRIORITY 1

#endif // CONFIG_H