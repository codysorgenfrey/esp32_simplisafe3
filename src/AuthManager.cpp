#include "AuthManager.h"
#include "common.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <base64.h>
#include <SHA256.h> // had to change define for ESP32 to use default AES
#include <SPIFFS.h>
#include <time.h>

class AllowAllFilter : ArduinoJson6191_F1::Filter {
    bool allow() const {
        return true;
    }

    bool allowArray() const {
        return true;
    }

    bool allowObject() const {
        return true;
    }

    bool allowValue() const {
        return true;
    }

    template <typename TKey>
    AllowAllFilter operator[](const TKey &) const {
        return AllowAllFilter();
    }
};

//
// Private Member Functions
//

String SS3AuthManager::base64URLEncode(uint8_t *buffer) {
    SS_LOG_LINE("Base64 URL encoding.");
    base64 bs;
    String str = bs.encode(buffer, SHA256_LEN);

    str.replace("+", "-");
    str.replace("/", "_");
    str.replace("=", "");
    
    return str;
}

void SS3AuthManager::sha256(const char *inBuff, uint8_t *outBuff) {
    SS_LOG_LINE("Doing SHA256.");
    SHA256 hash;
    hash.reset();
    hash.update(inBuff, strlen(inBuff));
    hash.finalize(outBuff, SHA256_LEN);
}

String SS3AuthManager::getSS3AuthURL() {
    SS_LOG_LINE("Getting authorization URL.");
    SS_DETAIL_LINE("Code Verifier:  %s", codeVerifier.c_str());
    SS_DETAIL_LINE("Code Challenge: %s", codeChallenge.c_str());    
    String ecnodedRedirect = String(SS_OAUTH_REDIRECT_URI);
    ecnodedRedirect.replace(":", "%3A");
    ecnodedRedirect.replace("/", "%2F");
    return String(SS_OAUTH_AUTH_URL) +
        "?client_id=" + SS_OAUTH_CLIENT_ID +
        "&scope=" + SS_OAUTH_SCOPE +
        "&response_type=code" +
        "&redirect_uri=" + ecnodedRedirect +
        "&code_challenge_method=S256" +
        "&code_challenge=" + codeChallenge +
        "&audience=" + SS_OAUTH_AUDIENCE +
        "&auth0Client=" + SS_OAUTH_AUTH0_CLIENT
    ;
}

bool SS3AuthManager::getAuthToken(String code) {
    SS_LOG_LINE("Getting authorization tokens.");
    StaticJsonDocument<256> headers;
    headers[0]["name"] = "Host";
    headers[0]["value"] = "auth.simplisafe.com";
    headers[1]["name"] = "Content-Type";
    headers[1]["value"] = "application/json";
    headers[2]["name"] = "Content-Length";
    headers[2]["value"] = "186";
    headers[3]["name"] = "Auth0-Client";
    headers[3]["value"] = SS_OAUTH_AUTH0_CLIENT;

    StaticJsonDocument<256> payloadDoc;
    String payload;
    payloadDoc["grant_type"] = "authorization_code";
    payloadDoc["client_id"] = SS_OAUTH_CLIENT_ID;
    payloadDoc["code_verifier"] = codeVerifier;
    code.replace("\n", "");
    code.replace("\r", "");
    payloadDoc["code"] = code;
    payloadDoc["redirect_uri"] = SS_OAUTH_REDIRECT_URI;
    serializeJson(payloadDoc, payload);
    
    DynamicJsonDocument resDoc(3072);
    int res = request(SS_OAUTH + String("/token"), resDoc, false, true, payload, headers);

    if (res >= 200 && res <= 299) {
        SS_LOG_LINE("Got authorization tokens.");
        return storeAuthToken(resDoc);
    }

    SS_ERROR_LINE("Error getting authorization tokens.");
    return false;
}

bool SS3AuthManager::refreshAuthToken() {
    SS_LOG_LINE("Getting refresh token.");
    StaticJsonDocument<256> headers;
    headers[0]["name"] = "Host";
    headers[0]["value"] = "auth.simplisafe.com";
    headers[1]["name"] = "Content-Type";
    headers[1]["value"] = "application/json";
    headers[2]["name"] = "Content-Length";
    headers[2]["value"] = "186";
    headers[3]["name"] = "Auth0-Client";
    headers[3]["value"] = SS_OAUTH_AUTH0_CLIENT;

    StaticJsonDocument<256> payloadDoc;
    String payload;
    payloadDoc["grant_type"] = "refresh_token";
    payloadDoc["client_id"] = SS_OAUTH_CLIENT_ID;
    payloadDoc["refresh_token"] = refreshToken;
    serializeJson(payloadDoc, payload);

    DynamicJsonDocument resDoc(3072);
    int res = request(SS_OAUTH + String("/token"), resDoc, false, true, payload, headers);

    if (res >= 200 && res <= 299) {
        SS_LOG_LINE("Got refresh token.");
        return storeAuthToken(resDoc);
    } else if (res == 403 || res == 401) {
        refreshToken = ""; // clear to re-auth
        accessToken = "";
    }
    
    SS_ERROR_LINE("Error getting refresh token.");
    return false;
}

bool SS3AuthManager::storeAuthToken(const DynamicJsonDocument &doc) {
    SS_LOG_LINE("Storing authorization tokens.");
    accessToken = doc["access_token"].as<String>();
    refreshToken = doc["refresh_token"].as<String>();
    tokenType = doc["token_type"].as<String>();
    tokenIssueMS = millis();
    expiresInMS = doc["expires_in"].as<unsigned long>() * 1000;

    if (
        !accessToken.equals("null") &&
        !refreshToken.equals("null") &&
        !tokenType.equals("null") &&
        expiresInMS != 0
    ) {
        SS_LOG_LINE("Stored authorization tokens.");
        return writeUserData();
    }

    SS_ERROR_LINE("Error storing authorization tokens.");
    return false;
}

bool SS3AuthManager::writeUserData() {
    SS_LOG_LINE("Writing authorization tokens to file.");
    bool success = true;

    DynamicJsonDocument userData(1536);
    userData["accessToken"] = accessToken;
    userData["refreshToken"] = refreshToken;
    userData["codeVerifier"] = codeVerifier;

    if (SPIFFS.begin(true)) {
        File file = SPIFFS.open(SS_USER_DATA_FILE, "w");
        if (file) {
            if (serializeJson(userData, file) > 0) {
                SS_LOG_LINE("Wrote authorization tokens to file.");
            } else {
                SS_ERROR_LINE("Failed to write data to %s.", SS_USER_DATA_FILE);
                success = false;
            }

            file.close();
        } else {
            SS_ERROR_LINE("Failed to open %s.", SS_USER_DATA_FILE);
            success = false;
        }

        SPIFFS.end();
    } else {
        SS_ERROR_LINE("Error starting SPIFFS.");
        success = false;
    }

    return success;
}

bool SS3AuthManager::readUserData() {
    SS_LOG_LINE("Reading authorization tokens from file.");
    bool success = true;

    if (SPIFFS.begin(true)) {
        File file = SPIFFS.open(SS_USER_DATA_FILE, "r");
        if (file) {
            DynamicJsonDocument userData(1536);
            DeserializationError err = deserializeJson(userData, file);
            if (err) {
                SS_ERROR_LINE("Error deserializing %s.", SS_USER_DATA_FILE);
                SS_ERROR_LINE("%s", err.c_str());
                success = false;
            } else {
                accessToken = userData["accessToken"].as<String>();
                refreshToken = userData["refreshToken"].as<String>();
                codeVerifier = userData["codeVerifier"].as<String>();

                if (
                    accessToken.equals("null") ||
                    refreshToken.equals("null") ||
                    codeVerifier.equals("null")
                ) {
                    SS_ERROR_LINE("Found file but contents are empty.");
                    accessToken = "";
                    refreshToken = "";
                    codeVerifier = "";
                    success = false;
                }

                SS_LOG_LINE("Read authorization tokens from file.");
                #if SS_DEBUG >= SS_DEBUG_LEVEL_ALL
                    serializeJsonPretty(userData, Serial);
                    Serial.println("");
                #endif
            }

            file.close();
        } else {
            SS_ERROR_LINE("Failed to open %s.", SS_USER_DATA_FILE);
            success = false;
        }

        SPIFFS.end();
    } else {
        SS_ERROR_LINE("Error starting SPIFFS.");
        success = false;
    }

    return success;
}

//
// Public Member Functions
//

SS3AuthManager::SS3AuthManager() {
    SS_LOG_LINE("Making Authorization Manager.");
    if(!readUserData()) {
        SS_LOG_LINE("No previous authorization tokens, generating codes.");
        uint8_t randData[32]; // 32 bytes, u_int8_t is 1 byte
        esp_fill_random(randData, SHA256_LEN);
        codeVerifier = base64URLEncode(randData);

        uint8_t hashOut[SHA256_LEN];
        sha256(codeVerifier.c_str(), hashOut);
        codeChallenge = base64URLEncode(hashOut);
    }
}

bool SS3AuthManager::authorize(HardwareSerial *hwSerial, unsigned long baud) {
    SS_LOG_LINE("Authorizing.");
    if (refreshToken.length() == 0) {
        if (!hwSerial) hwSerial->begin(baud);
        while (!hwSerial) { ; }
        hwSerial->println("Get that damn URL code:");
        hwSerial->println(getSS3AuthURL().c_str());
        while (hwSerial->available() > 0) { hwSerial->read(); } // flush serial monitor
        while (hwSerial->available() == 0) { delay(100); } // wait for url input
        String code = hwSerial->readString();
        hwSerial->println();
        if (getAuthToken(code)) {
            SS_LOG_LINE("Successfully authorized with SimpliSafe.");
            return true;
        } else { 
            SS_ERROR_LINE("Error authorizing with Simplisafe.");
            return false;
        }
    }

    bool refreshed = refreshAuthToken();
    if (refreshToken.length() == 0) refreshed = authorize(hwSerial, baud);
        
    return refreshed;
}

bool SS3AuthManager::isAuthorized() {
    SS_LOG_LINE("Checking if authorized...");
    if (tokenIssueMS == -1 || expiresInMS == -1) return false;

    unsigned long now = millis();
    unsigned long timeElapsed = max(now, tokenIssueMS) - min(now, tokenIssueMS);
    bool authorized = timeElapsed < (expiresInMS - SS_AUTH_REFRESH_BUFFER) && refreshToken.length() != 0;
    SS_LOG_LINE("%s", authorized ? "Authorized." : "Not authorized.");
    return authorized;
}

int SS3AuthManager::request(
    String url, 
    JsonDocument &doc, 
    bool auth, 
    bool post, 
    String payload, 
    const DynamicJsonDocument &headers, 
    const DynamicJsonDocument &filter,
    const DeserializationOption::NestingLimit &nestingLimit
) {
    SS_LOG_LINE("Making a request.");
    SS_DETAIL_LINE("Requesting: %s %s", post ? "POST" : "GET", url.c_str());
    SS_DETAIL_LINE("Authori    zed: %s", auth ? "yes" : "no");
    SS_DETAIL_LINE("Payload: %s", payload.c_str());

    int res = -1;

    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient https;
        WiFiClientSecure client;
        https.useHTTP10(true); // for ArduinoJson

        if (url.indexOf("https://auth") >= 0) client.setCACert(SS_OAUTH_CA_CERT);
        else if (url.indexOf("https://api") >= 0) client.setCACert(SS_API_CERT);
        else client.setInsecure();

        if (https.begin(client, url)){
            if (auth) {
                SS_DETAIL_LINE("Setting authorization credentials.");
                https.setAuthorization(""); // clear it out
                https.addHeader("Authorization", tokenType + " " + accessToken);
            }

            if (headers.size() != 0) {
                SS_DETAIL_LINE("Detected headers.");
                for (int x = 0; x < headers.size(); x++) {
                    SS_DETAIL_LINE("Adding header %i.", x);
                    https.addHeader(headers[x]["name"], headers[x]["value"]);
                    SS_DETAIL_LINE(
                        "Added header: \"%s: %s\"", 
                        headers[x]["name"].as<const char*>(), 
                        headers[x]["value"].as<const char*>()
                    );
                }
            }

            if (post) res = https.POST(payload);
            else res = https.GET();
            SS_DETAIL_LINE("Request sent. Response: %i", response);

            if (res >= 200 && res <= 299) {
                DeserializationError err;
                if (filter.size() != 0) err = deserializeJson(doc, client, DeserializationOption::Filter(filter), nestingLimit);
                else err = deserializeJson(doc, client, nestingLimit);
                
                if (err) {
                    SS_ERROR_LINE("API request deserialization error: %s", err.c_str());
                } else {
                    SS_DETAIL_LINE("Desearialized stream to json.");
                    #if SS_DEBUG >= SS_DEBUG_LEVEL_ALL
                        serializeJsonPretty(doc, Serial);
                        Serial.println("");
                    #endif
                }
            } else {
                SS_ERROR_LINE("Error, code: %i.", res);
                SS_ERROR_LINE("Response: %s", https.getString().c_str());
            }

            client.stop();
            https.end();
        } else SS_ERROR_LINE("Could not connect to %s.", url.c_str());
    } else SS_ERROR_LINE("Not connected to WiFi.");

    return res;
}
