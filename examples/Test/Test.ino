/*
TODO:
1. Check and refresh token in loop()
*/

#include <WiFi.h>
#include "secrets.h"
#include <SimpliSafe3.h>

#define LOG(message, ...) printf(">>> [%7d][%.2fkb] SimpliSafe: " message "\n", millis(), (esp_get_free_heap_size() * 0.001f), ##__VA_ARGS__)

SimpliSafe3 ss;
bool statusOk = false;

void setup()
{
    Serial.begin(115200);
    while (!Serial) { ; }; // wait for serial
    LOG("Starting...");

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
    LOG("Connected to %s.", WIFI_SSID);

    statusOk = ss.setup();
    if (statusOk) {
        statusOk = ss.startListeningToEvents([](int eventId) {
            LOG("I got an event: %i", eventId);
        });
    }

    int alarmState = ss.getAlarmState();
    LOG("Alarm state: %i (-1 UNKNOWN, 0 OFF, 1 HOME, 2 HOME_COUNT, 3 AWAY, 4 AWAY_COUNT, 5 ALARM, 6 ALARM_COUNT)", alarmState);
    int lockState = ss.getLockState();
    LOG("Lock state: %i (-1 UNKNOWN, 0 UNLOCKED, 1 LOCKED)", lockState);
    ss.setLockState(SS_SETLOCKSTATE_LOCK);
    LOG("Told SS to lock the front door..."); // need to impliment websocket to hear async if it worked
}

void loop(){
    if (statusOk) {
        ss.loop();
    }
}
