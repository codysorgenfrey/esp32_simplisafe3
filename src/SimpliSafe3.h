#ifndef __SIMPLISAFE3_H__
#define __SIMPLISAFE3_H__

/*
Notes: 
    Does not currently support multiple locations, systems, or locks. Library defaults to the first of each.
*/

#include "common.h"
#include <ArduinoJson.h>
#include "AuthManager.h"

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
const char* SS_GETSTATE_VALUES[8] = {
    "UNKNOWN",
    "OFF",
    "HOME",
    "HOME_COUNT",
    "AWAY",
    "AWAY_COUNT",
    "ALARM",
    "ALARM_COUNT"
};

enum SS_SETSTATE {
    SS_SETSTATE_OFF = 0,
    SS_SETSTATE_HOME,
    SS_SETSTATE_AWAY
};
const char* SS_SETSTATE_VALUES[3] = {
    "off",
    "home",
    "away"
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
const char* SS_SETLOCKSTATE_VALUES[2] = {
    "unlock",
    "lock"
};

class SimpliSafe3 {
    private:
        String subId;
        String userId;
        String lockId;
        SS3AuthManager *authManager;

    public:
        SimpliSafe3() {
            SS_LOG_LINE("Making SS3");
            authManager = new SS3AuthManager();
        }

        bool authorize(HardwareSerial *hwSerial = &Serial, unsigned long baud = 115200) {
            if (!authManager->isAuthorized()) {
                SS_LOG_LINE("Get that damn URL code:");
                SS_LOG_LINE("%s", authManager->getSS3AuthURL().c_str());
                if (!hwSerial) hwSerial->begin(baud);
                while (hwSerial->available() > 0) { hwSerial->read(); } // flush serial monitor
                while (hwSerial->available() == 0) { delay(100); } // wait for url input
                String code = hwSerial->readString();
                hwSerial->println();
                if (authManager->getAuthToken(code)) {
                    SS_LOG_LINE("Successfully authorized Homekit with SimpliSafe.");
                    return true;
                } else { 
                    SS_LOG_LINE("Error authorizing Homekit with Simplisafe.");
                    return false;
                }
            }
                
            return authManager->refreshAuthToken();
        }

        String getUserID() {
            SS_LOG_LINE("Getting user ID");
            if (userId.length() != 0) {
                SS_LOG_LINE("Already had userID");
                return userId;
            }

            DynamicJsonDocument data = authManager->request(String(SS3API) + "/api/authCheck", 64);
            if (data.size() > 0) {
                userId = data["userId"].as<String>();

                return userId;
            }

            return "";
        }

        DynamicJsonDocument getSubscription() {
            String userIdStr = getUserID();
            if (userIdStr.length() == 0) {
                SS_LOG_LINE("Error getting userId.");
                return StaticJsonDocument<0>();
            }

            StaticJsonDocument<192> filter;
            filter["subscriptions"][0]["sid"] = true;
            filter["subscriptions"][0]["location"]["system"]["alarmState"] = true;
            filter["subscriptions"][0]["location"]["system"]["isAlarming"] = true;

            SS_LOG_LINE("Getting first subscription.");
            DynamicJsonDocument sub = authManager->request(
                String(SS3API) + "/users/"+userIdStr+"/subscriptions?activeOnly=true", 
                256, 
                true,
                false,
                "",
                StaticJsonDocument<0>(),
                filter,
                DeserializationOption::NestingLimit(11)
            );

            if (sub.size() == 0) {
                SS_LOG_LINE("Error getting all subscriptions.");
                return StaticJsonDocument<0>();
            }

             // TODO: Handle other situations
            subId = String(sub["subscriptions"][0]["sid"].as<int>());
            SS_LOG_LINE("Set subId %s.", subId.c_str());

            return sub["subscriptions"][0];
        }

        int getAlarmState() {
            SS_LOG_LINE("Getting alarm state.");
            DynamicJsonDocument sub = getSubscription();

            if (sub.size() == 0) {
                SS_LOG_LINE("Error getting subscription.");
                return SS_GETSTATE_UNKNOWN;
            }

            if (sub["location"] && sub["location"]["system"]) { 
                if (sub["location"]["system"]["isAlarming"].as<bool>())
                    return SS_GETSTATE_ALARM;

                const char *resState = sub["location"]["system"]["alarmState"].as<const char*>();                
                for (int x = 0; x < sizeof(SS_GETSTATE_VALUES) / sizeof(SS_GETSTATE_VALUES[0]); x++) {
                    if (strcmp(resState, SS_GETSTATE_VALUES[x]) == 0) {
                        SS_LOG_LINE("Found %s at index %i.", resState, x);
                        return x - 1;
                    }
                }
            }
            
            SS_LOG_LINE("Subscription doesn't have location or system.");
            return SS_GETSTATE_UNKNOWN;
        }

        int setAlarmState(int newState) {
            SS_LOG_LINE("Setting alarm state.");
    
            if (subId.length() == 0) {
                getSubscription();
            }

            DynamicJsonDocument data = authManager->request(
                String(SS3API) + "/ss3/subscriptions/" + subId + "/state/" + SS_SETSTATE_VALUES[newState], // url
                96, // size
                true, // auth
                true // post
            );

            if (data.size() > 0) {
                const char *resState = data["state"].as<const char *>();
                for (int x = 0; x < sizeof(SS_GETSTATE_VALUES) / sizeof(SS_GETSTATE_VALUES[0]); x++) {
                    if (strcmp(resState, SS_GETSTATE_VALUES[x]) == 0) {
                        SS_LOG_LINE("Found %s at index %i.", resState, x);
                        return x - 1;
                    }
                }
            }

            return SS_GETSTATE_UNKNOWN;
        }

        DynamicJsonDocument getLock() {
            SS_LOG_LINE("Getting lock.");

            if (subId.length() == 0) {
                getSubscription();
            }

            StaticJsonDocument<96> filter;
            filter[0]["serial"] = true;
            filter[0]["status"]["lockState"] = true;
            filter[0]["status"]["lockJamState"] = true;

            DynamicJsonDocument data = authManager->request(
                String(SS3API) + "/doorlock/" + subId, // url
                192,                                   // size
                true,                                  // auth
                false,                                 // post
                "",                                    // payload
                StaticJsonDocument<0>(),               // headers
                filter
            );

            if (data.size() > 0) {
                lockId = data[0]["serial"].as<String>();
                return data[0];
            }

            return StaticJsonDocument<0>();
        }

        int getLockState() {
            SS_LOG_LINE("Get lock state.");

            if (subId.length() == 0) {
                getSubscription();
            }

            DynamicJsonDocument lock = getLock();

            if (lock.size() > 0) {
                return lock["status"]["lockState"].as<int>();
            }

            return SS_GETLOCKSTATE_UNKNOWN;
        }

        int setLockState(int newState) {
            SS_LOG_LINE("Setting lock state.");
    
            if (subId.length() == 0) {
                getSubscription();
            }

            if (lockId.length() == 0) {
                getLock();
            }

            StaticJsonDocument<96> headers;
            headers[0]["name"] = "Content-Type";
            headers[0]["value"] = "application/json";

            StaticJsonDocument<96> payloadDoc;
            String payload;
            payloadDoc["state"] = SS_SETLOCKSTATE_VALUES[newState];
            serializeJson(payloadDoc, payload);

            DynamicJsonDocument data = authManager->request(
                String(SS3API) + "/doorlock/" + subId + "/" + lockId + "/state", // url
                256,    // size: optimise
                true,   // auth
                true,   // post 
                payload,
                headers
            );

            if (data.size() > 0) {
                return newState; // api is async and doesn't tell us if it worked
            }

            return SS_GETLOCKSTATE_UNKNOWN;
        }
};

#endif