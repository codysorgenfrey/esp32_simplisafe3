#ifndef __SS3AUTHMANAGER_H__
#define __SS3AUTHMANAGER_H__

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define SHA256_LEN 32

class SS3AuthManager {
    private:
        HTTPClient *https;
        WiFiClientSecure *client;
        String refreshToken;
        String codeVerifier;
        String codeChallenge;
        unsigned long tokenIssueMS;
        unsigned long expiresInMS;

        String base64URLEncode(uint8_t *buffer);
        void sha256(const char *inBuff, uint8_t *outBuff);
        String getSS3AuthURL();
        bool getAuthToken(String code);
        bool refreshAuthToken();
        bool storeAuthToken(const DynamicJsonDocument &doc);
        bool writeUserData();
        bool readUserData();

    public:
        String tokenType = "Bearer";
        String accessToken;

        SS3AuthManager();
        bool isAuthorized();
        bool authorize(HardwareSerial *hwSerial, unsigned long baud);
        bool isAuthenticated();
        DynamicJsonDocument request(
            String url, 
            int docSize = 3072, 
            bool auth = true, 
            bool post = false, 
            String payload = "", 
            const DynamicJsonDocument &headers = StaticJsonDocument<0>(), 
            const DynamicJsonDocument &filter = StaticJsonDocument<0>(),
            const DeserializationOption::NestingLimit &nestingLimit = DeserializationOption::NestingLimit()
        );
};

#endif