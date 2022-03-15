#ifndef __SIMPLISAFE3_H__
#define __SIMPLISAFE3_H__

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
        unsigned long lastAuthCheck;

        String getUserID();
        StaticJsonDocument<256> getSubscription();
        StaticJsonDocument<192> getLock();

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