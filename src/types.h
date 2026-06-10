#ifndef TYPES_H
#define TYPES_H

#include <Arduino.h>
#include "config.h"
#include "meshtastic/config.pb.h"

// EEPROM Write Queue Item
typedef enum {
    EEPROM_FLOAT,
    EEPROM_INT,
    EEPROM_STRING,
    EEPROM_BOOL,
    EEPROM_SAVE_DMS  // no key/value; eeprom_task calls saveDmsToNvs()
} eepromType_t;

typedef struct {
    eepromType_t type;
    char key[32];
    union {
        float floatVal;
        int intVal;
        char stringVal[64];
        bool boolVal;
    } value;
} eepromWriteItem_t;

// Meshtastic callback item
typedef struct {
    uint32_t from;
    uint32_t to;
    uint8_t channel;
    uint32_t timestamp;  // epoch (time(NULL)) at receive
    char text[MAX_MESHTASTIC_PAYLOAD];
} meshtasticCallbackItem_t;

// Chat ring-buffer entry (RAM-only; lost on reboot)
typedef struct {
    uint32_t id;          // monotonic; used for selection
    uint32_t from;        // node ID; 0 means us (outgoing)
    uint32_t to;          // dest node ID
    uint8_t  channel;
    uint32_t timestamp;   // epoch (time(NULL)) at receive/send
    bool     outgoing;
    bool     read;        // true once user has tapped or replied (DMs only)
    char     text[CHAT_TEXT_SIZE];
} chatMessage_t;

// UI -> meshtasticTask send queue payload
typedef struct {
    uint8_t  channel;
    uint32_t dest;        // BROADCAST_ADDR or specific node
    char     text[CHAT_TEXT_SIZE];
} chatTxItem_t;

// GPS Config callback item (for position config responses)
typedef struct {
    meshtastic_Config_PositionConfig config;
} gpsConfigCallbackItem_t;

// Hot Packet Types
enum HotPacketType {
    HOT_PACKET_WEATHER = 1,
    HOT_PACKET_VENUE_EVENT = 2
};

// ESP-NOW message types (must match GCI)
typedef enum {
    ESPNOW_MSG_TEXT = 0,
    ESPNOW_MSG_GPS_DATA = 1,
    ESPNOW_MSG_TELEMETRY = 2,
    ESPNOW_MSG_COMMAND = 3,
    ESPNOW_MSG_ACK = 4,
    ESPNOW_MSG_HEARTBEAT = 5,
    ESPNOW_MSG_IS_HOME = 6,
    ESPNOW_MSG_IS_DAYTIME = 7,
    ESPNOW_MSG_CONFIG = 8,        // GCD → GCI configuration (must match GCI)
    ESPNOW_MSG_GCI_VERSION = 9   // GCI → GCD version string
} espnow_msg_type_t;

// Config message payload (must match GCI main.cpp structMsgConfig)
typedef struct __attribute__((packed)) {
    int32_t fuelSensorType;
    int32_t luxLightsOn;   // lux threshold to turn headlights ON
    int32_t luxLightsOff;  // lux threshold to turn headlights OFF
} structMsgConfig;

// ESP-NOW message structure
typedef struct __attribute__((packed)) {
    uint8_t type;
    uint32_t timestamp;
    uint16_t msg_seq_num;
    uint16_t data_len;
    uint8_t data[ESPNOW_MAX_PAYLOAD];
} espnow_message_t;

// Calculate actual packet size for sending (header + payload)
#define ESPNOW_PACKET_HEADER_SIZE 9  // type(1) + timestamp(4) + msg_seq_num(2) + data_len(2)
#define ESPNOW_PACKET_SIZE(data_len) (ESPNOW_PACKET_HEADER_SIZE + (data_len))

// ESP-NOW queue item for received messages
typedef struct {
    uint8_t mac_addr[6];
    espnow_message_t message;
    int rssi;
} espnow_recv_item_t;

// ESP-NOW peer info
typedef struct {
    uint8_t mac_addr[6];
    char name[32];
    bool is_online;
    uint32_t last_seen;
    int last_rssi;
} espnow_peer_info_t;

// Golf cart command codes
typedef enum {
    GCI_CMD_NONE = 0,
    GCI_CMD_ADD_PEER = 1,     // Add GCD MAC to GCI peer list
    GCI_CMD_REMOVE_PEER = 2,  // Future: remove peer
    GCI_CMD_REBOOT = 3,       // Future: reboot GCI
    // Add more commands as needed
} gci_command_t;

// BLE outbound notify queue item (pre-serialized frame ready for pTxChar->notify())
#define BLE_PAYLOAD_MAX 256
typedef struct {
    uint8_t  msg_type;
    uint16_t payload_len;
    uint8_t  payload[BLE_PAYLOAD_MAX];
} ble_notify_item_t;

// Sleep/Operating mode state machine
typedef enum {
    SLEEP_MODE_STARTUP_GRACE = 0,  // Initial grace period on startup
    SLEEP_MODE_GCI = 1,            // GCI is paired and communicating - normal sleep allowed
    SLEEP_MODE_NO_GCI = 2          // No GCI - never sleep, backlight dimming only
} sleep_operating_mode_t;

// Golf cart interface message structures
typedef struct struct_msg_from_gci {
    int modeLights;
    int outdoorLux;
    float airTemp;
    float battVolts;
    float fuelPct;
} structMsgFromGci;

typedef struct struct_msg_to_gci {
    int cmdNumber;
    uint8_t macAddr[6];  // For pairing command and future use
} structMsgToGci;

#endif // TYPES_H