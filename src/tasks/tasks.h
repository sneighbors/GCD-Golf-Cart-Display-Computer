#ifndef TASKS_H
#define TASKS_H

// Include all task headers for convenience
#include "tasks/gps_task.h"
#include "tasks/meshtastic_task.h"
#include "tasks/meshtastic_callback_task.h"
#include "tasks/espnow_task.h"
#include "tasks/eeprom_task.h"
#include "tasks/system_task.h"
#include "tasks/ble_task.h"

// Task creation helper
void createAllTasks();

#endif // TASKS_H.