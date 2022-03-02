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
    base64 bs;
    String str = bs.encode(buffer, SHA256_LEN);

    str.replace("+", "-");
    str.replace("/", "_");
    str.replace("=", "");
    
    return str;
}

void SS3AuthManager::sha256(const char *inBuff, uint8_t *outBuff) {
    SHA256 hash;
    hash.reset();
    hash.update(inBuff, strlen(inBuff));
    hash.finalize(outBuff, SHA256_LEN);
}

String SS3AuthManager::getSS3AuthURL() {
    SS_LOG_LINE("Code Verifier:     %s", codeVerifier.c_str());
    SS_LOG_LINE("Code Challenge:    %s", codeChallenge.c_str());    
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
    
    DynamicJsonDocument res = request(SS_OAUTH + String("/token"), 3072, false, true, payload, headers);

    if (res.size() != 0)
        return storeAuthToken(res);
    else
        return false;
}

bool SS3AuthManager::refreshAuthToken() {
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

    DynamicJsonDocument res = request(SS_OAUTH + String("/token"), 3072, false, true, payload, headers);

    if (res.size() != 0)
        return storeAuthToken(res);
    else
        return false;
}

bool SS3AuthManager::storeAuthToken(const DynamicJsonDocument &doc) {
    accessToken = doc["access_token"].as<String>();
    refreshToken = doc["refresh_token"].as<String>();
    tokenType = doc["token_type"].as<String>();
    tokenIssueMS = millis();
    expiresInMS = doc["expires_in"].as<unsigned long>() * 1000;

    return writeUserData();
}

bool SS3AuthManager::writeUserData() {
    // store accessToken, codeVerifier, refreshToken here
    DynamicJsonDocument userData(1536);
    SS_LOG_LINE("Created user data object");

    userData["accessToken"] = accessToken;
    userData["refreshToken"] = refreshToken;
    userData["codeVerifier"] = codeVerifier;

    if (!SPIFFS.begin(true)) {
        SS_LOG_LINE("Error starting SPIFFS.");
        return false;
    }

    File file = SPIFFS.open(SS_USER_DATA_FILE, "w");
    if (!file) {
        SS_LOG_LINE("Failed to open %s.", SS_USER_DATA_FILE);
        SPIFFS.end();
        return false;
    }

    if (serializeJson(userData, file) == 0) {
        SS_LOG_LINE("Failed to write data to %s.", SS_USER_DATA_FILE);
        file.close();
        SPIFFS.end();
        return false;
    }

    SS_LOG_LINE("Wrote to user data file.");

    file.close();
    SPIFFS.end();
    return true;
}

bool SS3AuthManager::readUserData() {
    SS_LOG_LINE("Reading user data file.");
    if (!SPIFFS.begin(true)) {
        SS_LOG_LINE("Error starting SPIFFS.");
        return false;
    }

    File file = SPIFFS.open(SS_USER_DATA_FILE, "r");
    if (!file) {
        SS_LOG_LINE("Failed to open %s.", SS_USER_DATA_FILE);
        SPIFFS.end();
        return false;
    }

    DynamicJsonDocument userData(1536);
    DeserializationError err = deserializeJson(userData, file);
    if (err) {
        SS_LOG_LINE("Error deserializing %s.", SS_USER_DATA_FILE);
        SS_LOG_LINE("%s", err.f_str());
        file.close();
        SPIFFS.end();
        return false;
    }

    accessToken = userData["accessToken"].as<String>();
    refreshToken = userData["refreshToken"].as<String>();
    codeVerifier = userData["codeVerifier"].as<String>();

    SS_LOG_LINE("Found...");
    #if SS_DEBUG
        serializeJsonPretty(userData, Serial);
        Serial.println("");
    #endif

    file.close();
    SPIFFS.end();
    return true;
}

//
// Public Member Functions
//

SS3AuthManager::SS3AuthManager() {
    SS_LOG_LINE("Making Authorization Manager.");
    if(!readUserData()) {
        SS_LOG_LINE("No previous data, generating codes.");
        uint8_t randData[32]; // 32 bytes, u_int8_t is 1 byte
        esp_fill_random(randData, SHA256_LEN);
        codeVerifier = base64URLEncode(randData);

        uint8_t hashOut[SHA256_LEN];
        sha256(codeVerifier.c_str(), hashOut);
        codeChallenge = base64URLEncode(hashOut);
    }
}

bool SS3AuthManager::authorize(HardwareSerial *hwSerial, unsigned long baud) {
    if (refreshToken.length() == 0) {
        SS_LOG_LINE("Get that damn URL code:");
        SS_LOG_LINE("%s", getSS3AuthURL().c_str());
        if (!hwSerial) hwSerial->begin(baud);
        while (hwSerial->available() > 0) { hwSerial->read(); } // flush serial monitor
        while (hwSerial->available() == 0) { delay(100); } // wait for url input
        String code = hwSerial->readString();
        hwSerial->println();
        if (getAuthToken(code)) {
            SS_LOG_LINE("Successfully authorized Homekit with SimpliSafe.");
            return true;
        } else { 
            SS_LOG_LINE("Error authorizing Homekit with Simplisafe.");
            return false;
        }
    }
        
    return refreshAuthToken();
}

bool SS3AuthManager::isAuthorized() {
    SS_LOG_LINE("Issue: %u, expires: %u", tokenIssueMS, expiresInMS);
    if (tokenIssueMS == -1 || expiresInMS == -1) return false;

    unsigned long now = millis();
    unsigned long timeElapsed = max(now, tokenIssueMS) - min(now, tokenIssueMS);
    SS_LOG_LINE("time elapsed: %u < %u && refresh token: %s", timeElapsed, expiresInMS - SS_REFRESH_BUFFER, refreshToken.length() != 0 ? "true" : "false");
    return timeElapsed < (expiresInMS - SS_REFRESH_BUFFER) && refreshToken.length() != 0;
}

DynamicJsonDocument SS3AuthManager::request(
    String url, 
    int docSize, 
    bool auth, 
    bool post, 
    String payload, 
    const DynamicJsonDocument &headers, 
    const DynamicJsonDocument &filter,
    const DeserializationOption::NestingLimit &nestingLimit
) {
    SS_LOG_LINE("Requesting: %s %s", post ? "POST" : "GET", url.c_str());
    SS_LOG_LINE("Authorized: %s", auth ? "yes" : "no");
    SS_LOG_LINE("Payload: %s", payload.c_str());

    if (WiFi.status() != WL_CONNECTED) {
        SS_LOG_LINE("Not connected to WiFi.");
        return StaticJsonDocument<0>();
    }

    https = new HTTPClient();
    client = new WiFiClientSecure();
    https->useHTTP10(true); // for ArduinoJson

    if (url.indexOf("https://auth") >= 0) client->setCACert(SS_OAUTH_CA_CERT);
    else if (url.indexOf("https://api") >= 0) client->setCACert(SS_API_CERT);
    else client->setInsecure();

    if (!https->begin(*client, url)){
        SS_LOG_LINE("Could not connect to %s.", url.c_str());
        return StaticJsonDocument<0>();
    }

    if (auth) {
        SS_LOG_LINE("Setting auth creds.");
        https->setAuthorization(""); // clear it out
        https->addHeader("Authorization", tokenType + " " + accessToken);
    }

    if (headers.size() != 0) {
        SS_LOG_LINE("Headers is bigger than 0.");
        for (int x = 0; x < headers.size(); x++) {
            SS_LOG_LINE("Adding %i header.", x);
            https->addHeader(headers[x]["name"], headers[x]["value"]);
            SS_LOG_LINE(
                "Header added... %s: %s", 
                headers[x]["name"].as<const char*>(), 
                headers[x]["value"].as<const char*>()
            );
        }
    }

    int response;
    if (post)
        response = https->POST(payload);
    else
        response = https->GET();
    SS_LOG_LINE("Request sent.");

    if (response < 200 || response > 299) {
        SS_LOG_LINE("Error, code: %i.", response);
        SS_LOG_LINE("Response: %s", https->getString().c_str());
        return StaticJsonDocument<0>();
    }
    SS_LOG_LINE("Response: %i", response);
    
    DynamicJsonDocument doc(docSize);
    SS_LOG_LINE("Created doc of %i size", docSize);
    
    DeserializationError err;
    if (filter.size() != 0) err = deserializeJson(doc, https->getStream(), DeserializationOption::Filter(filter), nestingLimit);
    else err = deserializeJson(doc, https->getStream(), nestingLimit);
    SS_LOG_LINE("Desearialized stream.");
    
    if (err) {
        if (err == DeserializationError::EmptyInput) doc["response"] = response; // no json response
        else SS_LOG_LINE("API request deserialization error: %s", err.f_str());
    } else {
        #if SS_DEBUG
            serializeJsonPretty(doc, Serial);
            Serial.println("");
        #endif
    }

    client->stop();
    https->end();
    delete https;
    delete client;

    return doc;
}
