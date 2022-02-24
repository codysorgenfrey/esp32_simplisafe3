/*
TODO:
1. Get SocketIO working for callbacks
2. Check and refresh token in loop()
3. Copy Crypto library locally for change needed and restore global version
4. Break into library package
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

    // int alarmState = ss.getAlarmState();
    // LOG("Alarm state: %i (UNKNOWN,OFF,HOME,HOME_COUNT,AWAY,AWAY_COUNT,ALARM,ALARM_COUNT)", alarmState);
    // int lockState = ss.getLockState();
    // LOG("Lock state: %i (UNKNOWN,UNLOCKED,LOCKED)", lockState);
    // ss.setLockState(SS_SETLOCKSTATE_LOCK);
    // LOG("Told SS to lock the front door..."); // need to impliment websocket to hear async if it worked
}

void loop(){
    if (statusOk) {
        ss.loop();
    }
}
