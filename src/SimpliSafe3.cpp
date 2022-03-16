#include "SimpliSafe3.h"
#include "common.h"
#include <ArduinoJson.h>
#include "AuthManager.h"
#include <WebSocketsClient.h>
#include <time.h>

const char* SS_GETSTATE_VALUES[7] = {
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

const char* SS_LOCKSTATE_VALUES[2] = {
    "unlock",
    "lock"
};

//
// Private Member Functions
//

String SimpliSafe3::getUserID() {
    SS_LOG_LINE("Getting user ID.");
    if (userId.length() != 0) {
        SS_LOG_LINE("User ID %s already exists.", userId.c_str());
        return userId;
    }

    StaticJsonDocument<64> data; 
    int res = authManager->request(String(SS3API) + "/api/authCheck", data);
    if (res >= 200 && res <= 299) {
        userId = data["userId"].as<String>();
        SS_LOG_LINE("Got user ID %s.", userId.c_str());
        return userId;
    }

    SS_ERROR_LINE("Error getting user ID.");
    return "";
}

bool SimpliSafe3::startListeningToEvents(void (*eventCallback)(int eventId), void (*connectCallback)(), void (*disconnectCallback)()) {
    SS_LOG_LINE("Starting WebSocket.");
    String userIdLocal;
    if (userId.length() == 0) {
        userIdLocal = getUserID();
        if (userIdLocal.length() == 0) {
            SS_ERROR_LINE("Cannot start WebSocket without userId.");
            return false;
        }
    }
    
    socket.beginSslWithCA(SS_WEBSOCKET_URL, 443, "/", SS_API_CERT, "");
    socket.onEvent([this, userIdLocal, eventCallback, connectCallback, disconnectCallback](WStype_t type, uint8_t * payload, size_t length) {
        switch(type) {
        case WStype_DISCONNECTED:
            SS_DETAIL_LINE("Websocket Disconnected.");
            if (disconnectCallback) disconnectCallback();
            break;
        case WStype_CONNECTED:
            SS_DETAIL_LINE("Websocket connected to url: %s",  payload);
            break;
        case WStype_TEXT: {
                SS_DETAIL_LINE("Websocket got text: %s", payload);
                DynamicJsonDocument res(2048);
                DeserializationError err = deserializeJson(res, payload);
                if (err) SS_ERROR_LINE("Error deserializing websocket response: %s", err.c_str());

                // listen for hello, then send identify
                String type = res["type"];
                if (type.equals("com.simplisafe.service.hello")) {
                    SS_DETAIL_LINE("SimpliSafe says hello.");

                    struct tm timeInfo;
                    time_t now;
                    char isoDate[20];
                    configTime(SS_TIME_GMT_OFFSET, SS_DST_OFFSET, SS_NTP_SERVER);
                    getLocalTime(&timeInfo);
                    time(&now);
                    sprintf(
                        isoDate,
                        "%04i-%02i-%02iT%02i:%02i:%02i",
                        timeInfo.tm_year + 1900,
                        timeInfo.tm_mon + 1,
                        timeInfo.tm_mday,
                        timeInfo.tm_hour,
                        timeInfo.tm_min,
                        timeInfo.tm_sec
                    );

                    DynamicJsonDocument ident(2048);
                    String identPayload;
                    ident["datacontenttype"] = "application/json";
                    ident["type"] = "com.simplisafe.connection.identify";
                    ident["time"] = isoDate; // "YYYY-MM-DDTHH:MM:SS";
                    ident["id"] = "ts:" + String(now);
                    ident["specversion"] = "1.0";
                    ident["source"] = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/98.0.4758.102 Safari/537.36 Edg/98.0.1108.56",
                    ident["data"]["auth"]["schema"] = "bearer";
                    ident["data"]["auth"]["token"] = authManager->accessToken;
                    ident["data"]["join"][0] = "uid:" + userIdLocal;
                    serializeJson(ident, identPayload);

                    if (!socket.sendTXT(identPayload)) {
                        SS_ERROR_LINE("Could not send identify message to websocket. %s", identPayload.c_str());
                    }
                    SS_DETAIL_LINE("Sent:");
                    #if SS_DEBUG >= SS_DEBUG_LEVEL_ALL
                        serializeJsonPretty(ident, Serial);
                        inSerial->println("");
                    #endif
                }

                // listen for registered
                if (type.equals("com.simplisafe.service.registered")) SS_LOG_LINE("Websocket registered.");
                
                // listen for subscribed
                if (type.equals("com.simplisafe.namespace.subscribed")) {
                    SS_DETAIL_LINE("Websocket subscribed.");
                    if (connectCallback) connectCallback();
                }
                
                // listen for events
                if (type.equals("com.simplisafe.event.standard")) {
                    SS_DETAIL_LINE("Event %i triggered, %s", res["data"]["eventCid"].as<int>(), res["data"]["messageSubject"].as<String>());
                    if (eventCallback) eventCallback(res["data"]["eventCid"]);
                }
            }
            break;
        case WStype_BIN: {
                SS_DETAIL_LINE("Websocket got binary length: %u", length);
                // hexdump(payload, length);
            }
            break;
		case WStype_ERROR:			
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
			break;
    }
    });

    return true;
}

StaticJsonDocument<256> SimpliSafe3::getSubscription() {
    SS_LOG_LINE("Getting subscription.");
    String userIdStr = getUserID();
    if (userIdStr.length() == 0) {
        SS_ERROR_LINE("Error getting userId.");
        return StaticJsonDocument<0>();
    }

    StaticJsonDocument<192> filter;
    filter["subscriptions"][0]["sid"] = true;
    filter["subscriptions"][0]["location"]["system"]["alarmState"] = true;
    filter["subscriptions"][0]["location"]["system"]["isAlarming"] = true;

    StaticJsonDocument<256> sub;
    int res = authManager->request(
        String(SS3API) + "/users/"+userIdStr+"/subscriptions?activeOnly=true", 
        sub, 
        true,
        false,
        "",
        StaticJsonDocument<0>(),
        filter,
        DeserializationOption::NestingLimit(11)
    );

    if (res >= 200 && res <= 299) {
        // TODO: Handle other situations
        subId = String(sub["subscriptions"][0]["sid"].as<int>());
        SS_LOG_LINE("Got subscription ID %s.", subId.c_str());

        return sub["subscriptions"][0];
    }

    SS_ERROR_LINE("Error getting all subscriptions.");
    return StaticJsonDocument<0>();
}

StaticJsonDocument<192> SimpliSafe3::getLock() {
    SS_LOG_LINE("Getting lock.");

    if (subId.length() == 0) {
        getSubscription();
    }

    StaticJsonDocument<96> filter;
    filter[0]["serial"] = true;
    filter[0]["status"]["lockState"] = true;
    filter[0]["status"]["lockJamState"] = true;

    StaticJsonDocument<192> data;
    int res = authManager->request(
        String(SS3API) + "/doorlock/" + subId, // url
        data,                                  // size
        true,                                  // auth
        false,                                 // post
        "",                                    // payload
        StaticJsonDocument<0>(),               // headers
        filter
    );

    if (res >= 200 && res <= 299) {
        lockId = data[0]["serial"].as<String>();
        SS_LOG_LINE("Got lock ID %s.", lockId.c_str());
        return data[0];
    }

    SS_ERROR_LINE("Error getting lock ID.");
    return StaticJsonDocument<0>();
}

//
// Public Member Functions
//

SimpliSafe3::SimpliSafe3() {
    SS_LOG_LINE("Making SimpliSafe3.");
    authManager = new SS3AuthManager();
}

bool SimpliSafe3::setup(bool forceReauth, HardwareSerial *hwSerial, unsigned long baud) {
    SS_LOG_LINE("Setting up SimpliSafe.");
    inSerial = hwSerial;
    inBaud = baud;

    // get authorized for api calls
    if (!authManager->authorize(forceReauth, inSerial, inBaud)) {
        SS_ERROR_LINE("Failed to authorize with SimpliSafe.");
        return false;
    }

    return true;
}

void SimpliSafe3::loop() {
    // poll for WebSocket
    socket.loop();

    // refresh auth token
    const unsigned long now = millis();
    const unsigned long diff = max(now, lastAuthCheck) - min(now, lastAuthCheck);
    if (diff >= SS_AUTH_CHECK_INTERVAL) {
        if (!authManager->isAuthorized()) {
            if (!authManager->authorize(false, inSerial, inBaud)) {
                SS_ERROR_LINE("Error refreshing authorization token.");
            }
        }

        lastAuthCheck = now;
    }
}

int SimpliSafe3::getAlarmState() {
    SS_LOG_LINE("Getting alarm state.");
    DynamicJsonDocument sub = getSubscription();

    if (sub.size() == 0) {
        SS_ERROR_LINE("Error getting subscription.");
        return SS_GETSTATE_UNKNOWN;
    }

    if (sub["location"] && sub["location"]["system"]) { 
        if (sub["location"]["system"]["isAlarming"].as<bool>())
            return SS_GETSTATE_ALARM;

        const char *resState = sub["location"]["system"]["alarmState"].as<const char*>();                
        for (int x = 0; x < sizeof(SS_GETSTATE_VALUES) / sizeof(SS_GETSTATE_VALUES[0]); x++) {
            if (strcmp(resState, SS_GETSTATE_VALUES[x]) == 0) {
                SS_DETAIL_LINE("Found state at index %i.", x);
                SS_LOG_LINE("Got alarm state: %s", SS_GETSTATE_VALUES[x]);
                return x;
            }
        }
    }
    
    SS_ERROR_LINE("Subscription doesn't have location or system.");
    return SS_GETSTATE_UNKNOWN;
}

int SimpliSafe3::setAlarmState(int newState) {
    SS_LOG_LINE("Setting alarm state.");

    if (subId.length() == 0) {
        getSubscription();
    }

    StaticJsonDocument<96> data;
    int res = authManager->request(
        String(SS3API) + "/ss3/subscriptions/" + subId + "/state/" + SS_SETSTATE_VALUES[newState], // url
        data, // size
        true, // auth
        true // post
    );

    if (res >= 200 && res <= 299) {
        const char *resState = data["state"].as<const char *>();
        for (int x = 0; x < sizeof(SS_GETSTATE_VALUES) / sizeof(SS_GETSTATE_VALUES[0]); x++) {
            if (strcmp(resState, SS_GETSTATE_VALUES[x]) == 0) {
                SS_DETAIL_LINE("Found state at index %i.", x);
                SS_LOG_LINE("Set alarm state to %s", SS_GETSTATE_VALUES[x]);
                return x;
            }
        }
    }

    SS_ERROR_LINE("Error setting alarm state.");
    return SS_GETSTATE_UNKNOWN;
}

int SimpliSafe3::getLockState() {
    SS_LOG_LINE("Getting lock state.");

    if (subId.length() == 0) {
        getSubscription();
    }

    StaticJsonDocument<192> lock = getLock();

    if (lock.size() > 0) {
        int resState = lock["status"]["lockState"].as<int>();
        SS_LOG_LINE("Got lock state: %s", SS_LOCKSTATE_VALUES[resState]);
        return resState;
    }

    SS_ERROR_LINE("Error getting lock state.");
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
    payloadDoc["state"] = SS_LOCKSTATE_VALUES[newState];
    serializeJson(payloadDoc, payload);

    StaticJsonDocument<256> data;
    int res = authManager->request(
        String(SS3API) + "/doorlock/" + subId + "/" + lockId + "/state", // url
        data,   // size
        true,   // auth
        true,   // post 
        payload,
        headers
    );

    if (res >= 200 && res <= 299) {
        SS_LOG_LINE("Set lock state to %s", SS_LOCKSTATE_VALUES[newState]);
        return newState; // api is async and doesn't tell us if it worked
    }

    SS_ERROR_LINE("Error setting lock state.");
    return SS_GETLOCKSTATE_UNKNOWN;
}