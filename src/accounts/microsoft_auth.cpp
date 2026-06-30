#include "microsoft_auth.hpp"
#include "network/network.hpp"
#include "logging/logging.hpp"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace flint::accounts {

namespace {
const QString CLIENT_ID = "00000000402b5328"; // Official pre-approved Microsoft Client ID for Minecraft launchers
}

DeviceCodeResponse MicrosoftAuth::request_device_code() {
    DeviceCodeResponse resp;
    QString url = "https://login.microsoftonline.com/consumers/oauth2/v2.0/devicecode";
    
    QByteArray payload = QString("client_id=%1&scope=XboxLive.signin%%20offline_access")
                         .arg(CLIENT_ID).arg("20").toUtf8();
    
    QMap<QString, QString> headers;
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    
    bool ok = false;
    QByteArray result = flint::network::post(url, payload, headers, &ok);
    if (!ok) {
        resp.error = "Network error requesting device code.";
        return resp;
    }
    
    try {
        json j = json::parse(result.constData());
        if (j.contains("error")) {
            resp.error = QString::fromStdString(j.value("error_description", "Unknown Microsoft OAuth error"));
        } else {
            resp.deviceCode = QString::fromStdString(j.value("device_code", ""));
            resp.userCode = QString::fromStdString(j.value("user_code", ""));
            resp.verificationUri = QString::fromStdString(j.value("verification_uri", ""));
            resp.expiresIn = j.value("expires_in", 0);
            resp.interval = j.value("interval", 5);
        }
    } catch (const std::exception& e) {
        resp.error = QString("JSON Parse error: %1").arg(e.what());
    }
    
    return resp;
}

TokenResponse MicrosoftAuth::poll_for_token(const QString& deviceCode) {
    TokenResponse resp;
    QString url = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
    
    QByteArray payload = QString("client_id=%1&grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code=%2")
                         .arg(CLIENT_ID).arg(deviceCode).toUtf8();
    
    QMap<QString, QString> headers;
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    
    bool ok = false;
    QByteArray result = flint::network::post(url, payload, headers, &ok);
    
    try {
        json j = json::parse(result.constData());
        if (j.contains("error")) {
            resp.error = QString::fromStdString(j.value("error", ""));
        } else {
            resp.accessToken = QString::fromStdString(j.value("access_token", ""));
            resp.refreshToken = QString::fromStdString(j.value("refresh_token", ""));
        }
    } catch (const std::exception& e) {
        resp.error = QString("JSON Parse error: %1").arg(e.what());
    }
    
    return resp;
}

TokenResponse MicrosoftAuth::refresh_ms_token(const QString& refreshToken) {
    TokenResponse resp;
    QString url = "https://login.microsoftonline.com/consumers/oauth2/v2.0/token";
    
    QByteArray payload = QString("client_id=%1&grant_type=refresh_token&refresh_token=%2")
                         .arg(CLIENT_ID).arg(refreshToken).toUtf8();
    
    QMap<QString, QString> headers;
    headers["Content-Type"] = "application/x-www-form-urlencoded";
    
    bool ok = false;
    QByteArray result = flint::network::post(url, payload, headers, &ok);
    
    try {
        json j = json::parse(result.constData());
        if (j.contains("error")) {
            resp.error = QString::fromStdString(j.value("error", ""));
        } else {
            resp.accessToken = QString::fromStdString(j.value("access_token", ""));
            resp.refreshToken = QString::fromStdString(j.value("refresh_token", ""));
        }
    } catch (const std::exception& e) {
        resp.error = QString("JSON Parse error: %1").arg(e.what());
    }
    
    return resp;
}

bool MicrosoftAuth::login_to_minecraft(const QString& msAccessToken, 
                                       QString& mcAccessToken, 
                                       QString& username, 
                                       QString& uuid, 
                                       QString& errorMsg) {
    // --------------------------------------------------
    // Step 1: Authenticate with Xbox Live
    // --------------------------------------------------
    QString xblUrl = "https://user.auth.xboxlive.com/user/authenticate";
    json xblPayload = {
        {"Properties", {
            {"AuthMethod", "RPS"},
            {"SiteName", "user.auth.xboxlive.com"},
            {"RpsTicket", "d=" + msAccessToken.toStdString()}
        }},
        {"RelyingParty", "http://auth.xboxlive.com"},
        {"TokenType", "JWT"}
    };
    
    bool ok = false;
    QByteArray xblRes = flint::network::post(xblUrl, QByteArray::fromStdString(xblPayload.dump()), {}, &ok);
    if (!ok) {
        errorMsg = "Xbox Live authentication failed.";
        return false;
    }
    
    std::string xblToken;
    std::string userHash;
    try {
        json j = json::parse(xblRes.constData());
        xblToken = j.value("Token", "");
        userHash = j["DisplayClaims"]["xui"][0].value("uhs", "");
    } catch (...) {
        errorMsg = "Failed to parse Xbox Live response.";
        return false;
    }
    
    if (xblToken.empty() || userHash.empty()) {
        errorMsg = "Invalid Xbox Live credentials.";
        return false;
    }
    
    // --------------------------------------------------
    // Step 2: Get XSTS Token
    // --------------------------------------------------
    QString xstsUrl = "https://xsts.auth.xboxlive.com/xsts/authorize";
    json xstsPayload = {
        {"Properties", {
            {"SandboxId", "RETAIL"},
            {"UserTokens", { xblToken }}
        }},
        {"RelyingParty", "rp://api.minecraftservices.com/"},
        {"TokenType", "JWT"}
    };
    
    QByteArray xstsRes = flint::network::post(xstsUrl, QByteArray::fromStdString(xstsPayload.dump()), {}, &ok);
    if (!ok) {
        // XSTS might fail with 401 if child account isn't in a family, etc.
        try {
            json j = json::parse(xstsRes.constData());
            if (j.contains("XErr")) {
                long long err = j.value("XErr", 0ll);
                if (err == 2148916238ll) {
                    errorMsg = "Account is under 18. Please add it to an Xbox Family Group.";
                    return false;
                }
            }
        } catch (...) {}
        errorMsg = "XSTS Authorization failed.";
        return false;
    }
    
    std::string xstsToken;
    try {
        json j = json::parse(xstsRes.constData());
        xstsToken = j.value("Token", "");
    } catch (...) {
        errorMsg = "Failed to parse XSTS response.";
        return false;
    }
    
    // --------------------------------------------------
    // Step 3: Login to Minecraft services
    // --------------------------------------------------
    QString mcLoginUrl = "https://api.minecraftservices.com/authentication/login_with_xbox";
    json mcLoginPayload = {
        {"identityToken", "XBL3.0 x=" + userHash + ";" + xstsToken}
    };
    
    QByteArray mcRes = flint::network::post(mcLoginUrl, QByteArray::fromStdString(mcLoginPayload.dump()), {}, &ok);
    if (!ok) {
        errorMsg = "Minecraft Services authentication failed.";
        return false;
    }
    
    std::string mcToken;
    try {
        json j = json::parse(mcRes.constData());
        mcToken = j.value("access_token", "");
    } catch (...) {
        errorMsg = "Failed to parse Minecraft Services login response.";
        return false;
    }
    
    if (mcToken.empty()) {
        errorMsg = "Empty Minecraft access token.";
        return false;
    }
    
    mcAccessToken = QString::fromStdString(mcToken);
    
    // --------------------------------------------------
    // Step 4: Get Minecraft Profile details
    // --------------------------------------------------
    QString profileUrl = "https://api.minecraftservices.com/minecraft/profile";
    QMap<QString, QString> headers;
    headers["Authorization"] = "Bearer " + mcAccessToken;
    
    QByteArray profileRes = flint::network::get(profileUrl, headers, &ok);
    if (!ok) {
        errorMsg = "Failed to fetch Minecraft profile.";
        return false;
    }
    
    try {
        json j = json::parse(profileRes.constData());
        std::string nameStr = j.value("name", "");
        std::string uuidStr = j.value("id", "");
        
        if (nameStr.empty() || uuidStr.empty()) {
            errorMsg = "Minecraft profile contains invalid name or UUID.";
            return false;
        }
        
        username = QString::fromStdString(nameStr);
        QString rawUuid = QString::fromStdString(uuidStr);
        
        // Format UUID as standard 8-4-4-4-12
        if (rawUuid.length() == 32) {
            uuid = rawUuid.mid(0, 8) + "-" +
                   rawUuid.mid(8, 4) + "-" +
                   rawUuid.mid(12, 4) + "-" +
                   rawUuid.mid(16, 4) + "-" +
                   rawUuid.mid(20, 12);
        } else {
            uuid = rawUuid;
        }
    } catch (...) {
        errorMsg = "Failed to parse Minecraft profile JSON.";
        return false;
    }
    
    return true;
}

} // namespace flint::accounts
