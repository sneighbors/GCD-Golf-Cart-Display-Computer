#ifndef BLE_TASK_H
#define BLE_TASK_H

#include <Arduino.h>
#include "types.h"

// ── BLE protocol message types (ESP32 → Android) ──────────────────────────
#define BLE_MSG_HELLO       0x01
#define BLE_MSG_GPS         0x02
#define BLE_MSG_TELEMETRY   0x03
#define BLE_MSG_WEATHER     0x04
#define BLE_MSG_VENUE       0x05
#define BLE_MSG_STATUS      0x06
#define BLE_MSG_SETTINGS    0x07
#define BLE_MSG_CHAT        0x08
#define BLE_MSG_CHAT_CLEAR  0x09
#define BLE_MSG_ALERT       0x0A
#define BLE_MSG_ACK         0x0B

// ── BLE protocol command types (Android → ESP32) ───────────────────────────
#define BLE_CMD_PING        0x80
#define BLE_CMD_REQ_STATE   0x81
#define BLE_CMD_REQ_CHAT    0x82
#define BLE_CMD_SEND_CHAT   0x83
#define BLE_CMD_SET_HOME    0x84
#define BLE_CMD_PAIR_GCI    0x85
#define BLE_CMD_UNPAIR_GCI  0x86
#define BLE_CMD_REBOOT_MESH 0x87
#define BLE_CMD_REBOOT_GCD  0x88
#define BLE_CMD_RESET_PREFS 0x89
#define BLE_CMD_TRIP_RESET  0x8A
#define BLE_CMD_SVC_RESET   0x8B
#define BLE_CMD_MARK_READ   0x8C
#define BLE_CMD_SET         0x8D

// ── Alert codes (MSG_ALERT payload "code" field) ───────────────────────────
#define BLE_ALERT_NEW_BROADCAST 1
#define BLE_ALERT_NEW_DM        2
#define BLE_ALERT_FUEL_LOW      3
#define BLE_ALERT_GCI_LOST      5
#define BLE_ALERT_GCI_CONNECTED 6

// ── FreeRTOS task entry point ──────────────────────────────────────────────
void bleTask(void* parameter);

// ── Push API (safe to call from any task) ─────────────────────────────────
// Enqueues a pre-serialized JSON payload for notify delivery.
// Drops silently if queue is full or no client is subscribed.
void blePush(uint8_t msg_type, const char* json);

// Convenience push helpers — each serializes from globals and calls blePush().
// These are called by ble_task internally; may also be called from other tasks
// when they want to push an update immediately on state change.
void blePushGps();
void blePushTelemetry();
void blePushWeather();
void blePushVenue();
void blePushStatus();
void blePushSettings();
void blePushChat(const chatMessage_t* msg);
void blePushAlert(uint8_t code, const char* msg_text);
void blePushAck(const char* cmd, bool ok, const char* msg_text = "");

// True when an Android client is connected and subscribed to notifications.
extern volatile bool bleClientConnected;

#endif // BLE_TASK_H
