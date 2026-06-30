#pragma once

#include <QString>
#include <QMap>

namespace flint::accounts {

struct DeviceCodeResponse {
    QString deviceCode;
    QString userCode;
    QString verificationUri;
    int expiresIn = 0;
    int interval = 0;
    QString error;
};

struct TokenResponse {
    QString accessToken;
    QString refreshToken;
    QString error; // "authorization_pending", "expired_token", or other errors
};

class MicrosoftAuth {
public:
    // Request device login code from Microsoft
    static DeviceCodeResponse request_device_code();

    // Perform a single poll request to check if user has authenticated
    static TokenResponse poll_for_token(const QString& deviceCode);

    // Refresh the Microsoft access token using a refresh token
    static TokenResponse refresh_ms_token(const QString& refreshToken);

    // Authenticate with Xbox Live, XSTS, and Mojang to get Minecraft credentials
    static bool login_to_minecraft(const QString& msAccessToken, 
                                   QString& mcAccessToken, 
                                   QString& username, 
                                   QString& uuid, 
                                   QString& errorMsg);
};

} // namespace flint::accounts
