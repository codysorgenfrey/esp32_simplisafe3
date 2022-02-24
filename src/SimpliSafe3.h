#ifndef __SIMPLISAFE3_H__
#define __SIMPLISAFE3_H__

/* Notes: 
    Does not currently support multiple locations, systems, or locks. 
    Library defaults to the first of each.

    eventCids
    1110:
    1120:
    1132:
    1134:
    1154:
    1159:
    1162:
        ALARM_TRIGGER
    1170:
        CAMERA_MOTION
    1301:
        POWER_OUTAGE
    1350:
        Base station WiFi lost, this plugin cannot communicate with the base station until it is restored.
    1400:
    1407:
        1400 is disarmed with Master PIN, 1407 is disarmed with Remote
        ALARM_DISARM
    1406:
        ALARM_CANCEL
    1409:
        MOTION
    1429:
        ENTRY
    1458:
        DOORBELL
    1602:
        Automatic test
    3301:
        POWER_RESTORED
    3350:
        this.log.warn('Base station WiFi restored.');
    3401:
    3407:
    3487:
    3481:
        3401 is for Keypad, 3407 is for Remote
        AWAY_ARM
    3441:
    3491:
        HOME_ARM
    9401:
    9407:
        9401 is for Keypad, 9407 is for Remote
        AWAY_EXIT_DELAY
    9441:
        HOME_EXIT_DELAY
    9700:
        DOORLOCK_UNLOCKED
    9701:
        DOORLOCK_LOCKED
    9703:
        DOORLOCK_ERROR
*/

#include "AuthManager.h"
#include <ArduinoJson.h>
#include <WebSocketsClient.h>

enum SS_GETSTATE {
    SS_GETSTATE_UNKNOWN = -1,
    SS_GETSTATE_OFF,
    SS_GETSTATE_HOME,
    SS_GETSTATE_HOME_COUNT,
    SS_GETSTATE_AWAY,
    SS_GETSTATE_AWAY_COUNT,
    SS_GETSTATE_ALARM,
    SS_GETSTATE_ALARM_COUNT
};
enum SS_SETSTATE {
    SS_SETSTATE_OFF = 0,
    SS_SETSTATE_HOME,
    SS_SETSTATE_AWAY
};
enum SS_GETLOCKSTATE {
    SS_GETLOCKSTATE_UNKNOWN = -1,
    SS_GETLOCKSTATE_UNLOCKED,
    SS_GETLOCKSTATE_LOCKED
};
enum SS_SETLOCKSTATE {
    SS_SETLOCKSTATE_UNLOCK,
    SS_SETLOCKSTATE_LOCK
};

class SimpliSafe3 {
    private:
        String subId;
        String userId;
        String lockId;
        SS3AuthManager *authManager;
        WebSocketsClient socket;
        HardwareSerial *inSerial;
        unsigned long inBaud;

        String getUserID();
        DynamicJsonDocument getSubscription();
        DynamicJsonDocument getLock();

    public:
        SimpliSafe3();
        bool setup(HardwareSerial *hwSerial = &Serial, unsigned long baud = 115200);
        void loop();
        int  getAlarmState();
        int  setAlarmState(int newState);
        int  getLockState();
        int  setLockState(int newState);
        bool startListeningToEvents(void (*eventCallback)(int eventId), void (*connectCallback)(), void (*disconnectCallback)());
};

#endif