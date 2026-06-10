# GCD Bluetooth LE Protocol Specification

**Version:** 0.1  
**Applies to:** GCD firmware replacing local CYD TFT display with Android tablet display

---

## Overview

The GCD ESP32 acts as a BLE **peripheral** (server). The Android tablet acts as a BLE **central** (client). The ESP32 keeps all existing logic (GPS, Meshtastic, ESP-NOW to GCI) and streams state to the tablet. The tablet renders the UI and sends user commands back.

The protocol uses a **two-characteristic NUS-style channel** (Nordic UART Service pattern): one notify characteristic for ESP32 → Android, one write characteristic for Android → ESP32. Messages are length-prefixed binary frames carrying JSON payloads.

---

## BLE Stack: NimBLE (required)

Use `h2zero/NimBLE-Arduino`, **not** the stock Arduino BLE library.

Reasons:
- NimBLE and ESP-NOW (WiFi) coexist reliably via hardware time-division; the Bluedroid stack is much harder to tune for this
- NimBLE heap footprint is ~60 KB vs. ~100 KB for Bluedroid — critical given the GCD's flash/RAM pressure
- NimBLE has cleaner FreeRTOS integration

`platformio.ini` dependency to add:
```ini
h2zero/NimBLE-Arduino @ ^2.2.3
```

---

## GATT Service Definition

| Role          | UUID                                   | Properties                |
|---------------|----------------------------------------|---------------------------|
| **Service**   | `a0f3f001-2b45-4c9a-8b6d-e1234f567890` | —                         |
| **TX Char**   | `a0f3f002-2b45-4c9a-8b6d-e1234f567890` | NOTIFY                    |
| **RX Char**   | `a0f3f003-2b45-4c9a-8b6d-e1234f567890` | WRITE / WRITE_NO_RESPONSE |

**Device name advertised:** `GCD-XXXXXX` where `XXXXXX` is the last 3 bytes of the ESP32's WiFi/BLE MAC address in uppercase hex (no colons). Example: MAC `AA:BB:CC:DD:EE:FF` → device name `GCD-DDEEFF`. This ensures each cart is uniquely identifiable when multiple are nearby.  
**Security:** Just-works (no passkey required); bonding optional.  
**Target negotiated MTU:** 512 bytes (517 on most Android devices after negotiation, minus 3-byte ATT header = 514 usable bytes per notification).

---

## Frame Format

Every message in both directions uses this binary envelope:

```
Byte 0      : MSG_TYPE   (uint8, see tables below)
Bytes 1–2   : PAYLOAD_LEN (uint16, big-endian, length of JSON payload in bytes)
Bytes 3+    : PAYLOAD    (UTF-8 JSON, no null terminator)
```

**Overhead:** 3 bytes per message.  
**Max payload:** 511 bytes at 514-byte MTU (all defined messages fit in one BLE packet at this MTU; no chunking required in normal operation).

If MTU negotiation yields less than ~350 bytes (some older devices), the MSG_CHAT_SYNC sequence (one MSG_CHAT per call) is still fine, but MSG_WEATHER may need splitting. Reserve MSG_TYPE bit 7 for a future `MORE` continuation flag — for now it must always be 0.

---

## MSG_TYPE Values

### ESP32 → Android (0x01–0x7F)

| Value  | Name           | Description                                      |
|--------|----------------|--------------------------------------------------|
| `0x01` | MSG_HELLO      | Sent immediately after Android subscribes        |
| `0x02` | MSG_GPS        | Navigation, time, odometer, service data         |
| `0x03` | MSG_TELEMETRY  | GCI sensor data (fuel, battery, temp, lights)    |
| `0x04` | MSG_WEATHER    | Hot-packet weather data                          |
| `0x05` | MSG_VENUE      | Hot-packet venue / now-playing data              |
| `0x06` | MSG_STATUS     | System connectivity and version info             |
| `0x07` | MSG_SETTINGS   | All persisted settings                           |
| `0x08` | MSG_CHAT       | One chat ring-buffer entry                       |
| `0x09` | MSG_CHAT_CLEAR | Tell Android to clear its chat list (before sync)|
| `0x0A` | MSG_ALERT      | Trigger notification / audio on tablet           |
| `0x0B` | MSG_ACK        | Result of a command sent by Android              |

### Android → ESP32 (0x80–0xFF)

| Value  | Name            | Description                              |
|--------|-----------------|------------------------------------------|
| `0x80` | CMD_PING        | Keepalive / latency test                 |
| `0x81` | CMD_REQ_STATE   | Request full state dump (MSG_GPS + MSG_TELEMETRY + MSG_WEATHER + MSG_VENUE + MSG_STATUS + MSG_SETTINGS) |
| `0x82` | CMD_REQ_CHAT    | Request full chat history sync           |
| `0x83` | CMD_SEND_CHAT   | Send a Meshtastic text message           |
| `0x84` | CMD_SET_HOME    | Save current GPS position as home        |
| `0x85` | CMD_PAIR_GCI    | Initiate ESP-NOW pairing with GCI        |
| `0x86` | CMD_UNPAIR_GCI  | Remove saved GCI peer MAC                |
| `0x87` | CMD_REBOOT_MESH | Reboot the GCM Meshtastic radio          |
| `0x88` | CMD_REBOOT_GCD  | Reboot the GCD ESP32                     |
| `0x89` | CMD_RESET_PREFS | Factory-reset all preferences            |
| `0x8A` | CMD_TRIP_RESET  | Reset trip odometer to 0                 |
| `0x8B` | CMD_SVC_RESET   | Reset service-hours counter to 0         |
| `0x8C` | CMD_MARK_READ   | Mark a DM as read                        |
| `0x8D` | CMD_SET         | Change one setting value                 |

---

## ESP32 → Android Message Schemas

All payloads are UTF-8 JSON objects. Keys are intentionally short to conserve BLE bandwidth.

---

### 0x01 MSG_HELLO

Sent once when Android subscribes to notifications.

```json
{
  "type": "HELLO",
  "fw":   "v1.2.3",
  "mac":  "AA:BB:CC:DD:EE:FF",
  "name": "GCD-GolfCart"
}
```

| Key    | Type   | Source variable    | Description               |
|--------|--------|--------------------|---------------------------|
| `fw`   | string | `gcd_version`      | GCD firmware version      |
| `mac`  | string | `cyd_mac_addr`     | ESP32 WiFi/BLE MAC        |
| `name` | string | (constant)         | BLE device name           |

---

### 0x02 MSG_GPS

Sent at ~1 Hz when GPS data changes. Also sent as part of CMD_REQ_STATE response.

```json
{
  "type":  "GPS",
  "spd":   12,
  "hdg":   "NE",
  "lat":   "28.8522",
  "lng":   "-82.0028",
  "date":  "06/07/26",
  "hhmm":  "2:34",
  "ampm":  "PM",
  "secs":  "2:34:56",
  "sats":  "8  1.2",
  "odo":   "123.4",
  "trip":  "5.6",
  "d_mi":  123.4,
  "t_mi":  5.6,
  "rise":  "6:42AM",
  "set":   "8:15PM",
  "home":  true,
  "day":   true,
  "svc_h": 245,
  "svc_i": 500
}
```

| Key     | Type   | Source variable        | Notes                                     |
|---------|--------|------------------------|-------------------------------------------|
| `spd`   | int    | `avg_speed`            | MPH, filtered                             |
| `hdg`   | string | `heading`              | Compass direction string                  |
| `lat`   | string | `cur_lat`              | Formatted latitude                        |
| `lng`   | string | `cur_long`             | Formatted longitude                       |
| `date`  | string | `cur_date`             | "MM/DD/YY" or "NO GPS"                    |
| `hhmm`  | string | `hhmm_str`             | "H:MM" local time                         |
| `ampm`  | string | `am_pm_str`            | "AM" or "PM"                              |
| `secs`  | string | `hhmmss_str`           | "H:MM:SS" local time                      |
| `sats`  | string | `sats_hdop`            | "N sats X.X" satellite/HDOP status        |
| `odo`   | string | `odometer`             | Formatted odometer display string         |
| `trip`  | string | `trip_odometer`        | Formatted trip display string             |
| `d_mi`  | float  | `accum_distance`       | Raw accumulated miles (for Android calcs) |
| `t_mi`  | float  | `trip_distance`        | Raw trip miles                            |
| `rise`  | string | `sunrise_time_str`     | Local sunrise time                        |
| `set`   | string | `sunset_time_str`      | Local sunset time                         |
| `home`  | bool   | `at_home`              | Within home geofence                      |
| `day`   | bool   | `is_daytime`           | Between sunrise and sunset                |
| `svc_h` | int    | `hrs_since_svc`        | **Tenths of hours** (divide by 10 to display) |
| `svc_i` | int    | `svc_interval_hrs`     | Service interval in hours                 |

---

### 0x03 MSG_TELEMETRY

Sent when GCI data changes (GCI already change-detects before sending ESP-NOW). Also sent as part of CMD_REQ_STATE response.

```json
{
  "type": "TEL",
  "fuel": 75.5,
  "batt": 2.690,
  "temp": 82.3,
  "lts":  true,
  "lux":  450
}
```

| Key    | Type  | Source variable    | Notes                                          |
|--------|-------|--------------------|------------------------------------------------|
| `fuel` | float | `fuelLevel`        | Fuel level % (-99 = no sensor / invalid)       |
| `batt` | float | `battVoltage`      | Raw ADC volts from GCI (same as GCI display)   |
| `temp` | float | `airTemperature`   | °F after `temperature_adj` applied             |
| `lts`  | bool  | `headlights_on`    | Headlight relay state                          |
| `lux`  | int   | `lux_now`          | Ambient lux from BH1750 (-99 = no sensor)      |

---

### 0x04 MSG_WEATHER

Sent when a new weather hot-packet arrives. Also sent as part of CMD_REQ_STATE response (last known data).

```json
{
  "type": "WX",
  "rcv":  "05/15  4:43PM",
  "cur":  "82°",
  "fc": [
    {"hr": "4pm",  "gl": "2", "t": "85°", "pr": "0.10"},
    {"hr": "7pm",  "gl": "1", "t": "80°", "pr": "0.00"},
    {"hr": "10pm", "gl": "3", "t": "75°", "pr": "0.50"},
    {"hr": "1am",  "gl": "2", "t": "72°", "pr": "0.20"}
  ]
}
```

| Key       | Type   | Source variable            | Notes                          |
|-----------|--------|----------------------------|--------------------------------|
| `rcv`     | string | `wx_rcv_time`              | Receive timestamp              |
| `cur`     | string | `cur_temp`                 | Current temperature            |
| `fc[].hr` | string | `fcast_hr1`–`fcast_hr4`    | Forecast hour label            |
| `fc[].gl` | string | `fcast_glyph1`–`4`         | Weather icon code (single digit)|
| `fc[].t`  | string | `fcast_temp1`–`4`          | Forecast temperature           |
| `fc[].pr` | string | `fcast_precip1`–`4`        | Precip probability             |

---

### 0x05 MSG_VENUE

Sent when a new venue/now-playing hot-packet arrives. Also sent as part of CMD_REQ_STATE response.

```json
{
  "type": "NP",
  "rcv":  "05/15  4:43PM",
  "data": "The Eagles at Amalie Arena — 7:30PM — Doors 6PM — Sec 103 Row J ..."
}
```

| Key    | Type   | Source variable                    | Notes                       |
|--------|--------|------------------------------------|-----------------------------|
| `rcv`  | string | `np_rcv_time`                      | Receive timestamp           |
| `data` | string | `hotPacketBuffer_live_venue_event_data` | Raw venue packet string (max 240 chars) |

---

### 0x06 MSG_STATUS

Sent on connect, when connectivity changes, and every 30 seconds as a heartbeat.

```json
{
  "type":    "STS",
  "gcd_v":   "v1.2.3",
  "gci_v":   "v0.5.1",
  "gcm_id":  "!ab12cd34",
  "bt_mac":  "AA:BB:CC:DD:EE:FF",
  "en_conn": true,
  "en_sts":  "Connected",
  "en_mac":  "A1:B2:C3:D4:E5:F6",
  "mesh_en": true,
  "unrd":    2
}
```

| Key       | Type   | Source variable          | Notes                                   |
|-----------|--------|--------------------------|-----------------------------------------|
| `gcd_v`   | string | `gcd_version`            | GCD firmware version                    |
| `gci_v`   | string | `gci_version`            | GCI firmware version (from GCI via ESP-NOW) |
| `gcm_id`  | string | `gcm_node_id`            | Meshtastic radio node ID                |
| `bt_mac`  | string | `cyd_mac_addr`           | ESP32's own MAC                         |
| `en_conn` | bool   | `espnow_connected`       | GCI is connected via ESP-NOW            |
| `en_sts`  | string | `espnow_status`          | ESP-NOW status text                     |
| `en_mac`  | string | `espnow_gci_mac_addr`    | Saved GCI peer MAC ("NONE" if unpaired) |
| `mesh_en` | bool   | `mesh_serial_enabled`    | Meshtastic radio enabled                |
| `unrd`    | int    | `num_unread_direct_msgs` | Unread DM count (for badge)             |

---

### 0x07 MSG_SETTINGS

Sent once after MSG_HELLO and as part of CMD_REQ_STATE response.  
Also sent (only the changed key) any time a setting is saved to NVS.

```json
{
  "type":      "SET",
  "day_bl":    200,
  "ngt_bl":    50,
  "spk_vol":   20,
  "bl_to":     60,
  "t_adj":     0.0,
  "fuel_low":  10.0,
  "fuel_type": 1,
  "home_r":    100,
  "lux_on":    200,
  "lux_off":   400,
  "svc_int":   500,
  "mesh_flt":  0,
  "gci_mac":   "A1:B2:C3:D4:E5:F6"
}
```

| Key         | Type   | Source variable           | Notes                                  |
|-------------|--------|---------------------------|----------------------------------------|
| `day_bl`    | int    | `day_backlight`           | 0–255                                  |
| `ngt_bl`    | int    | `night_backlight`         | 0–255                                  |
| `spk_vol`   | int    | `speaker_volume`          | Speaker volume                         |
| `bl_to`     | int    | `backlight_timeout`       | Seconds until sleep (0 = never)        |
| `t_adj`     | float  | `temperature_adj`         | °F offset applied to GCI air temp      |
| `fuel_low`  | float  | `fuel_low_percent`        | Alert threshold %                      |
| `fuel_type` | int    | `fuelSensorType`          | 0=none, 1=ADC gas, 2=GPIO exp, 3=ADC elec |
| `home_r`    | int    | `home_gps_fence_radius_m` | Geofence radius in meters              |
| `lux_on`    | int    | `lux_lights_on`           | Lux to turn headlights ON              |
| `lux_off`   | int    | `lux_lights_off`          | Lux to turn headlights OFF             |
| `svc_int`   | int    | `svc_interval_hrs`        | Service interval in hours              |
| `mesh_flt`  | int    | `mesh_filter`             | Chat filter (0=all, 1=ch0…4=DM)        |
| `gci_mac`   | string | `espnow_gci_mac_addr`     | "NONE" or "XX:XX:XX:XX:XX:XX"          |

---

### 0x08 MSG_CHAT

One chat ring-buffer entry. Sent one at a time during sync (after MSG_CHAT_CLEAR), and pushed for each new incoming or outgoing message.

```json
{
  "type": "CHAT",
  "id":   42,
  "frm":  2882400001,
  "to":   0,
  "ch":   0,
  "ts":   1748340000,
  "out":  false,
  "rd":   false,
  "txt":  "Hello world"
}
```

| Key   | Type   | Source (`chatMessage_t`) | Notes                                          |
|-------|--------|--------------------------|------------------------------------------------|
| `id`  | uint32 | `.id`                    | Monotonic message ID                           |
| `frm` | uint32 | `.from`                  | Sender node ID; `0` = us (outgoing)            |
| `to`  | uint32 | `.to`                    | Dest node ID; `0` = broadcast                  |
| `ch`  | uint8  | `.channel`               | Meshtastic channel index                       |
| `ts`  | uint32 | `.timestamp`             | Unix epoch at receive/send                     |
| `out` | bool   | `.outgoing`              | True if we sent this message                   |
| `rd`  | bool   | `.read`                  | True if DM has been read                       |
| `txt` | string | `.text`                  | Message text (max 120 chars)                   |

---

### 0x09 MSG_CHAT_CLEAR

No payload (PAYLOAD_LEN = 0). Tells Android to wipe its local chat list before the sync sequence.

---

### 0x0A MSG_ALERT

Push notification / audio trigger. Android plays sound and/or vibrates.

```json
{
  "type": "ALERT",
  "code": 2,
  "msg":  "New direct message from !ab12cd34"
}
```

| Code | Event                  |
|------|------------------------|
| 1    | New broadcast message  |
| 2    | New direct message     |
| 3    | Fuel low warning       |
| 4    | Battery low (future)   |
| 5    | GCI disconnected       |
| 6    | GCI reconnected        |

---

### 0x0B MSG_ACK

Result of a command sent by Android. The `cmd` field echoes the command name.

```json
{
  "type": "ACK",
  "cmd":  "SET_HOME",
  "ok":   true,
  "msg":  "Home location saved"
}
```

---

## Android → ESP32 Command Schemas

---

### 0x80 CMD_PING

```json
{ "cmd": "PING" }
```
ESP32 responds with `MSG_ACK { "cmd": "PING", "ok": true }`.

---

### 0x81 CMD_REQ_STATE

```json
{ "cmd": "REQ_STATE" }
```
ESP32 responds with: MSG_GPS, MSG_TELEMETRY, MSG_WEATHER, MSG_VENUE, MSG_STATUS, MSG_SETTINGS — one packet each in that order.

---

### 0x82 CMD_REQ_CHAT

```json
{ "cmd": "REQ_CHAT" }
```
ESP32 responds with MSG_CHAT_CLEAR, then one MSG_CHAT per ring-buffer entry (oldest first).

---

### 0x83 CMD_SEND_CHAT

```json
{
  "cmd": "CHAT",
  "ch":  0,
  "to":  0,
  "txt": "Hello from the tablet"
}
```

| Key   | Type   | Notes                              |
|-------|--------|------------------------------------|
| `ch`  | uint8  | Meshtastic channel (0–7)           |
| `to`  | uint32 | Dest node ID; 0 = broadcast        |
| `txt` | string | Text (max 120 chars)               |

ESP32 queues to `chatTxQueue` and responds `MSG_ACK { "cmd": "CHAT", "ok": true/false }`.

---

### 0x84 CMD_SET_HOME

```json
{ "cmd": "SET_HOME" }
```
Saves current GPS fix as home location. Responds with `MSG_ACK`.

---

### 0x85 CMD_PAIR_GCI

```json
{ "cmd": "PAIR_GCI" }
```
Sets `espnow_pair_gci = true` to trigger GCI pairing in espnow_task. Responds with `MSG_ACK`.

---

### 0x86 CMD_UNPAIR_GCI

```json
{ "cmd": "UNPAIR_GCI" }
```
Removes saved GCI peer MAC and clears NVS key. Responds with `MSG_ACK`.

---

### 0x87 CMD_REBOOT_MESH

```json
{ "cmd": "RBT_MESH" }
```
Sets `reboot_meshtastic = true`. Responds with `MSG_ACK`.

---

### 0x88 CMD_REBOOT_GCD

```json
{ "cmd": "REBOOT" }
```
Calls `ESP.restart()` after a 500ms delay to allow the ACK to be sent.

---

### 0x89 CMD_RESET_PREFS

```json
{ "cmd": "RESET_PREFS" }
```
Sets `reset_preferences = true`. **No ACK** — device reboots.

---

### 0x8A CMD_TRIP_RESET

```json
{ "cmd": "TRIP_RST" }
```
Resets `trip_distance` and `trip_odometer` to zero, saves to NVS. Responds with `MSG_ACK`.

---

### 0x8B CMD_SVC_RESET

```json
{ "cmd": "SVC_RST" }
```
Resets `hrs_since_svc` to zero, saves to NVS. Responds with `MSG_ACK`.

---

### 0x8C CMD_MARK_READ

```json
{
  "cmd": "MARK_RD",
  "id":  42
}
```
Marks the DM with the given `id` as read in the chat ring buffer. Decrements `num_unread_direct_msgs` if applicable. Responds with `MSG_ACK`.

---

### 0x8D CMD_SET

Change one setting. The `key` must match a key from the MSG_SETTINGS schema. The `val` type must match (int, float, bool, or string as appropriate).

```json
{
  "cmd": "SET",
  "key": "day_bl",
  "val": 180
}
```

Valid keys and value types:

| Key         | Type   | Range / Notes                          |
|-------------|--------|----------------------------------------|
| `day_bl`    | int    | 0–255                                  |
| `ngt_bl`    | int    | 0–255                                  |
| `spk_vol`   | int    | 0–100                                  |
| `bl_to`     | int    | 0 = never, otherwise seconds           |
| `t_adj`     | float  | °F offset, e.g. -2.5                   |
| `fuel_low`  | float  | 0.0–100.0                              |
| `fuel_type` | int    | 0–3                                    |
| `home_r`    | int    | Meters, e.g. 50                        |
| `lux_on`    | int    | Lux (e.g. 200)                         |
| `lux_off`   | int    | Lux (e.g. 400); must be > `lux_on`     |
| `svc_int`   | int    | Hours                                  |
| `mesh_flt`  | int    | 0=all, 1=ch0, 2=ch1, 3=ch2, 4=DM only |
| `gci_mac`   | string | "XX:XX:XX:XX:XX:XX" or "NONE"          |
| `mesh_en`   | bool   | true/false                             |

ESP32 applies the change, saves to NVS via `queuePreferenceWrite()`, and responds with `MSG_ACK { "cmd": "SET", "ok": true, "key": "day_bl" }`.

---

## Connection and Update Flow

### Android Pairing Flow

**First time (no saved MAC):**
1. App scans for any device whose name starts with `GCD-`
2. Found devices appear in a list; user taps their cart
3. App saves `device.remoteId` (MAC) to SharedPreferences key `paired_mac`
4. App connects and proceeds to the normal connect sequence below

**Subsequent connections (saved MAC):**
1. App scans for `GCD-` prefix devices; auto-connects when it finds the saved MAC
2. No user interaction required unless the saved device is not found (timeout → "Not found")

**Forget / re-pair:**
- User taps "Forget this cart" on the connect screen
- App clears `paired_mac` from SharedPreferences
- Next scan re-enters the first-time flow above

---

### On Android Connect

```
Android connects
Android subscribes to TX characteristic (enables notifications)
ESP32: sends MSG_HELLO
ESP32: sends MSG_STATUS
ESP32: sends MSG_GPS (last known)
ESP32: sends MSG_TELEMETRY (last known)
ESP32: sends MSG_WEATHER (last known, if any)
ESP32: sends MSG_VENUE (last known, if any)
ESP32: sends MSG_SETTINGS

[Optional: Android sends CMD_REQ_CHAT]
ESP32: sends MSG_CHAT_CLEAR
ESP32: sends MSG_CHAT × N  (full ring buffer, oldest first)
```

### During Normal Operation

| Event                           | ESP32 sends         | Cadence                          |
|---------------------------------|---------------------|----------------------------------|
| GPS data changes                | MSG_GPS             | ~1 Hz max (suppress if no change)|
| GCI telemetry received          | MSG_TELEMETRY       | On receipt from GCI              |
| New weather hot-packet          | MSG_WEATHER         | On receipt                       |
| New venue hot-packet            | MSG_VENUE           | On receipt                       |
| New Meshtastic message          | MSG_CHAT            | Immediately on receipt           |
| Message sent successfully       | MSG_CHAT            | After chatTxQueue drains         |
| Connectivity change             | MSG_STATUS          | Immediately on change            |
| Heartbeat                       | MSG_STATUS          | Every 30 s                       |
| Setting saved to NVS            | MSG_SETTINGS        | Within 2 s (after debounce)      |
| Fuel low threshold crossed      | MSG_ALERT (code 3)  | Once per threshold crossing      |
| New DM arrives                  | MSG_ALERT (code 2)  | On receipt                       |
| GCI connects/disconnects        | MSG_ALERT + MSG_STATUS | On state change               |

### On Android Disconnect / Reconnect

The ESP32 BLE task detects disconnect and waits. On reconnect it repeats the "On Android Connect" sequence above. No state is lost on the ESP32 side between connections.

---

## Implementation Notes

### ESP32 BLE Task

Add a new FreeRTOS task: `ble_task` (suggested stack: 6144 bytes, priority 2).

Responsibilities:
- Initialize NimBLE server, create service and characteristics
- Start advertising
- On client connect: send the connection-time sequence
- Maintain a send queue (`bleNotifyQueue`, size 20) fed by other tasks
- Drain the queue in `ble_task` loop: serialize each item into the frame format and call `pTxCharacteristic->notify(data, len)`
- On write from Android: parse frame header + JSON, dispatch command to appropriate handler

Other tasks feed the queue exactly as they currently feed the GUI update path. The `gui_task` is removed; `ble_task` takes its place as the "display output" consumer.

### Mutual Exclusion

The existing mutexes (`gpsMutex`, `hotPacketMutex`, `chatBufferMutex`) remain. The BLE task takes the relevant mutex when reading state for a push, same as gui_task does today.

### Send Queue Item

```cpp
typedef struct {
    uint8_t  msg_type;
    uint16_t payload_len;
    uint8_t  payload[512];   // pre-serialized JSON
} ble_notify_item_t;
```

### Backlight / Speaker Without the CYD Display

- **Backlight**: `day_backlight` / `night_backlight` settings are no longer meaningful for a physical backlight. Keep in NVS for compatibility; the Android tablet controls its own brightness. The GCD firmware can simply not call `initBacklight()` or `updateBacklight()`.
- **Speaker**: The CYD's piezo speaker remains on the ESP32 (`SPEAKER_PIN 26`) and can still produce tones (startup beep, DM alert beep, fuel-low beep). The Android tablet triggers its own audio via MSG_ALERT. Both play simultaneously — acceptable behavior.

### ESP-NOW + BLE Coexistence

ESP-NOW uses the WiFi radio; NimBLE uses the Bluetooth radio. On the ESP32 these are two separate physical radios sharing one antenna with time-division. Both can run simultaneously — this is a supported and tested configuration in ESP-IDF. Set the coexistence mode in `sdkconfig` (or via PlatformIO build flags):

```ini
build_flags =
    -DCONFIG_BT_NIMBLE_ENABLED=1
    -DCONFIG_ESP_COEX_ENABLED=1
    -DCONFIG_ESP32_WIFI_SW_COEXIST_ENABLE=1
```

### Advertising

Advertise continuously while no client is connected. Stop advertising while connected (single-client device). Resume advertising if client disconnects.

### BLE Device Name on Android

The Android Bluetooth settings will show `GCD-DDEEFF` (last 3 MAC bytes). The Android app scans for any device whose advertised name starts with `BLE_DEVICE_NAME_PREFIX` (`"GCD-"`) **or** whose service UUID matches `gcdServiceUuid`. On first connect the user selects their cart from a list; the app saves the MAC to SharedPreferences and auto-connects to that MAC on subsequent launches. A "Forget cart" option in the connect screen clears the saved MAC so the user can re-pair.

---

## Approximate Packet Size Reference

| Message       | Estimated JSON size | Fits in 514-byte MTU? |
|---------------|--------------------|-----------------------|
| MSG_HELLO     | ~80 bytes          | Yes (1 packet)        |
| MSG_GPS       | ~250 bytes         | Yes (1 packet)        |
| MSG_TELEMETRY | ~70 bytes          | Yes (1 packet)        |
| MSG_WEATHER   | ~280 bytes         | Yes (1 packet)        |
| MSG_VENUE     | ~270 bytes         | Yes (1 packet)        |
| MSG_STATUS    | ~150 bytes         | Yes (1 packet)        |
| MSG_SETTINGS  | ~180 bytes         | Yes (1 packet)        |
| MSG_CHAT      | ~175 bytes max     | Yes (1 packet)        |
| CMD_SEND_CHAT | ~140 bytes max     | Yes (1 packet)        |
| CMD_SET       | ~50 bytes          | Yes (1 packet)        |

All messages fit in a single BLE notification at typical negotiated MTU. No chunking implementation is needed for the initial version.
