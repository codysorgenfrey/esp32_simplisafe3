#ifndef __SS3AUTHMANAGER_H__
#define __SS3AUTHMANAGER_H__

#include <ArduinoJson.h>

#define SHA256_LEN 32

class SS3AuthManager {
    private:
        String refreshToken;
        String codeVerifier;
        String codeChallenge;
        unsigned long tokenIssueMS = -1;
        unsigned long expiresInMS = -1;

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
        bool authorize(HardwareSerial *hwSerial, unsigned long baud);
        bool isAuthorized();
        int request(
            String url, 
            JsonDocument &doc, 
            bool auth = true, 
            bool post = false, 
            String payload = "", 
            const DynamicJsonDocument &headers = StaticJsonDocument<0>(), 
            const DynamicJsonDocument &filter = StaticJsonDocument<0>(),
            const DeserializationOption::NestingLimit &nestingLimit = DeserializationOption::NestingLimit()
        );
};

#endif