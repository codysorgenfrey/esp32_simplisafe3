#include "SimpliSafe3.h"
#include "common.h"
#include <ArduinoJson.h>
#include "AuthManager.h"
#include <WebSocketsClient.h>
#include <SocketIOclient.h>

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
const char* SS_SETSTATE_VALUES[3] = {
    "off",
    "home",
    "away"
};
const char* SS_SETLOCKSTATE_VALUES[2] = {
    "unlock",
    "lock"
};

//
// Private Member Functions
//

String SimpliSafe3::getUserID() {
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

    SS_LOG_LINE("Error getting user ID.");
    return "";
}

bool SimpliSafe3::startListening() {
    return true;
}

DynamicJsonDocument SimpliSafe3::getSubscription() {
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

DynamicJsonDocument SimpliSafe3::getLock() {
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

//
// Public Member Functions
//

SimpliSafe3::SimpliSafe3() {
    SS_LOG_LINE("Making SimpliSafe3.");
    authManager = new SS3AuthManager();
}

bool SimpliSafe3::setup(HardwareSerial *hwSerial, unsigned long baud) {
    // get authorized for api calls
    if (!authManager->authorize(hwSerial, baud)) {
        SS_LOG_LINE("Failed to authorize with SimpliSafe.");
        return false;
    }

    // set up socket to listen for api changes
    if (!startListening()) {
        SS_LOG_LINE("Error starting SocketIO.");
        return false;
    }

    return true;
}

void SimpliSafe3::loop() {
    // poll here for socketIO
    // check here for refreshing auth token
}

int SimpliSafe3::getAlarmState() {
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

int SimpliSafe3::setAlarmState(int newState) {
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

int SimpliSafe3::getLockState() {
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

int SimpliSafe3::setLockState(int newState) {
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