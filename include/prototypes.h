/***********************
 *     PROTOTYPES      *
 ***********************/

#include "Meshtastic.h"

void connected_callback(mt_node_t *node, mt_nr_progress_t progress);
void text_message_callback(uint32_t from, uint32_t to,  uint8_t channel, const char* text);

// Utility function prototypes
String prefix_zero(int max2digits);
int make12hr(int time_hr);
String am_pm(int time_hr);
const char* getMonthAbbr(int monthNumber);
const char* getDayAbbr(int weekday_num);


// Task function prototypes
void gpsTask(void *parameter);
void meshtasticTask(void *parameter);
void meshtasticCallbackTask(void *parameter);
void eepromTask(void *parameter);
void systemTask(void *parameter);
void espnowTask(void *parameter);

// Callback function prototypes
void connected_callback(mt_node_t *node, mt_nr_progress_t progress);
void text_message_callback(uint32_t from, uint32_t to, uint8_t channel, const char* text);

// Parsing functions prototypes
int parseWeatherData(char* input);

// Speaker/beep function prototypes
void initSpeaker();
void beep(int numBeeps, uint32_t frequency, uint32_t duration, uint32_t pauseMs);

// Sleep manager function prototypes
void initSleepPin();
bool shouldEnterSleep();
void enterDeepSleep();
