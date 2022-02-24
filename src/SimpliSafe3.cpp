#include "SimpliSafe3.h"
#include "common.h"
#include <ArduinoJson.h>
#include "AuthManager.h"
#include <WebSocketsClient.h>
#include <time.h>

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

bool SimpliSafe3::startListeningToEvents(void (*eventCallback)(int eventId), void (*connectCallback)(), void (*disconnectCallback)()) {
    SS_LOG_LINE("Starting WebSocket.");
    String userIdLocal;
    if (userId.length() == 0) {
        userIdLocal = getUserID();
        if (userIdLocal.length() == 0) {
            SS_LOG_LINE("Cannot start WebSocket without userId.");
            return false;
        }
    }
    
    socket.beginSslWithCA(SS_WEBSOCKET_URL, 443, "/", SS_API_CERT, "");
    socket.onEvent([this, userIdLocal, eventCallback, connectCallback, disconnectCallback](WStype_t type, uint8_t * payload, size_t length) {
        switch(type) {
        case WStype_DISCONNECTED:
            SS_LOG_LINE("Websocket Disconnected.");
            disconnectCallback();
            break;
        case WStype_CONNECTED:
            SS_LOG_LINE("Websocket connected to url: %s",  payload);
            break;
        case WStype_TEXT: {
                SS_LOG_LINE("Websocket got text: %s", payload);
                DynamicJsonDocument res(2048);
                DeserializationError err = deserializeJson(res, payload);
                if (err) SS_LOG_LINE("Error deserializing websocket response: %s", err.c_str());

                // listen for hello, then send identify
                String type = res["type"];
                if (type.equals("com.simplisafe.service.hello")) {
                    SS_LOG_LINE("SimpliSafe says hello.");

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
                        SS_LOG_LINE("Could not send identify message to websocket. %s", identPayload.c_str());
                    }
                    SS_LOG_LINE("Sent:");
                    #if SS_DEBUG
                        serializeJsonPretty(ident, Serial);
                        inSerial->println("");
                    #endif
                }

                // listen for registered
                if (type.equals("com.simplisafe.service.registered")) SS_LOG_LINE("Websocket registered.");
                
                // listen for subscribed
                if (type.equals("com.simplisafe.namespace.subscribed")) {
                    SS_LOG_LINE("Websocket subscribed.");
                    connectCallback();
                }
                
                // listen for events
                if (type.equals("com.simplisafe.event.standard")) {
                    SS_LOG_LINE("Event triggered: %s, %s", res["data"]["eventCid"], res["data"]["messageSubject"]);
                    eventCallback(res["data"]["eventCid"]);
                }
            }
            break;
        case WStype_BIN: {
                SS_LOG_LINE("Websocket got binary length: %u", length);
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
    inSerial = hwSerial;
    inBaud = baud;

    // get authorized for api calls
    if (!authManager->authorize(inSerial, inBaud)) {
        SS_LOG_LINE("Failed to authorize with SimpliSafe.");
        return false;
    }

    return true;
}

void SimpliSafe3::loop() {
    // poll for WebSocket
    socket.loop();

    // refresh auth token
    if (millis() % 60000 == 0) { // check every minute
        if (!authManager->isAuthorized()) authManager->authorize(inSerial, inBaud);
    }
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