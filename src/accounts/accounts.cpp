#include "accounts.hpp"
#include "microsoft_auth.hpp"
#include "filesystem/filesystem.hpp"
#include "logging/logging.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <nlohmann/json.hpp>
#include <fstream>

using json = nlohmann::json;

namespace flint::accounts {

QString AccountManager::generate_offline_uuid(const QString& username) {
    QByteArray data = ("OfflinePlayer:" + username).toUtf8();
    QByteArray hash = QCryptographicHash::hash(data, QCryptographicHash::Md5);
    
    // Set version to 3 (UUID v3, name-based MD5)
    hash[6] = (hash[6] & 0x0f) | 0x30;
    // Set variant to RFC 4122
    hash[8] = (hash[8] & 0x3f) | 0x80;
    
    QString hex = hash.toHex().toLower();
    return hex.mid(0, 8) + "-" +
           hex.mid(8, 4) + "-" +
           hex.mid(12, 4) + "-" +
           hex.mid(16, 4) + "-" +
           hex.mid(20, 12);
}

AccountManager::AccountManager(QObject* parent) : QObject(parent) {}

bool AccountManager::load_accounts() {
    std::string path = flint::fs::get_config_dir() + "/accounts.json";
    std::ifstream file(path);
    if (!file.is_open()) {
        flint::logging::info("No accounts.json found, starting fresh.");
        return false;
    }
    
    try {
        json j;
        file >> j;
        
        m_accounts.clear();
        m_activeAccount = nullptr;
        
        if (j.contains("accounts") && j["accounts"].is_array()) {
            for (const auto& item : j["accounts"]) {
                Account acc;
                acc.username = QString::fromStdString(item.value("username", ""));
                acc.uuid = QString::fromStdString(item.value("uuid", ""));
                acc.type = QString::fromStdString(item.value("type", "offline"));
                acc.accessToken = QString::fromStdString(item.value("accessToken", ""));
                acc.refreshToken = QString::fromStdString(item.value("refreshToken", ""));
                m_accounts.append(acc);
            }
        }
        
        m_activeUuid = QString::fromStdString(j.value("activeUuid", ""));
        if (!m_activeUuid.isEmpty()) {
            set_active_account(m_activeUuid);
        } else if (!m_accounts.isEmpty()) {
            set_active_account(m_accounts[0].uuid);
        }
        
        emit accounts_changed();
        return true;
    } catch (const std::exception& e) {
        flint::logging::error("Failed to parse accounts.json: {}", e.what());
        return false;
    }
}

bool AccountManager::save_accounts() {
    QString qpath = QString::fromStdString(flint::fs::get_config_dir()) + "/accounts.json";
    json j;
    j["accounts"] = json::array();
    
    for (const auto& acc : m_accounts) {
        json item;
        item["username"] = acc.username.toStdString();
        item["uuid"] = acc.uuid.toStdString();
        item["type"] = acc.type.toStdString();
        item["accessToken"] = acc.accessToken.toStdString();
        item["refreshToken"] = acc.refreshToken.toStdString();
        j["accounts"].push_back(item);
    }
    j["activeUuid"] = m_activeUuid.toStdString();
    
    try {
        std::ofstream file(qpath.toStdString());
        if (!file.is_open()) {
            flint::logging::error("Failed to open accounts.json for writing.");
            return false;
        }
        file << j.dump(4);
        file.close();
        
        // Harden permissions: Owner Read & Write (0600)
        QFile::setPermissions(qpath, QFile::ReadOwner | QFile::WriteOwner);
        return true;
    } catch (const std::exception& e) {
        flint::logging::error("Exception saving accounts: {}", e.what());
        return false;
    }
}

bool AccountManager::add_offline_account(const QString& username) {
    if (username.trimmed().isEmpty()) return false;
    
    QString uuid = generate_offline_uuid(username);
    
    // Check if account already exists
    for (const auto& acc : m_accounts) {
        if (acc.uuid == uuid) {
            set_active_account(uuid);
            return true;
        }
    }
    
    Account acc;
    acc.username = username;
    acc.uuid = uuid;
    acc.type = "offline";
    
    m_accounts.append(acc);
    set_active_account(uuid);
    save_accounts();
    emit accounts_changed();
    return true;
}

bool AccountManager::remove_account(const QString& uuid) {
    for (int i = 0; i < m_accounts.size(); ++i) {
        if (m_accounts[i].uuid == uuid) {
            m_accounts.removeAt(i);
            if (m_activeUuid == uuid) {
                m_activeAccount = nullptr;
                m_activeUuid = "";
                if (!m_accounts.isEmpty()) {
                    set_active_account(m_accounts[0].uuid);
                }
            }
            save_accounts();
            emit accounts_changed();
            return true;
        }
    }
    return false;
}

bool AccountManager::set_active_account(const QString& uuid) {
    for (const auto& acc : m_accounts) {
        if (acc.uuid == uuid) {
            m_activeAccount = &acc;
            m_activeUuid = uuid;
            emit active_account_changed(acc);
            return true;
        }
    }
    return false;
}

bool AccountManager::add_microsoft_account(const QString& username, const QString& uuid, const QString& accessToken, const QString& refreshToken) {
    if (username.trimmed().isEmpty() || uuid.trimmed().isEmpty()) return false;
    
    // Check if account already exists
    for (auto& acc : m_accounts) {
        if (acc.uuid == uuid) {
            acc.username = username;
            acc.accessToken = accessToken;
            acc.refreshToken = refreshToken;
            acc.type = "microsoft";
            set_active_account(uuid);
            save_accounts();
            emit accounts_changed();
            return true;
        }
    }
    
    Account acc;
    acc.username = username;
    acc.uuid = uuid;
    acc.accessToken = accessToken;
    acc.refreshToken = refreshToken;
    acc.type = "microsoft";
    
    m_accounts.append(acc);
    set_active_account(uuid);
    save_accounts();
    emit accounts_changed();
    return true;
}

bool AccountManager::refresh_active_account_token() {
    if (!m_activeAccount || m_activeAccount->type != "microsoft") {
        return true; // Offline accounts do not need refresh
    }
    
    flint::logging::info("Refreshing Microsoft access token...");
    auto tokenResp = MicrosoftAuth::refresh_ms_token(m_activeAccount->refreshToken);
    if (!tokenResp.error.isEmpty()) {
        flint::logging::error("Failed to refresh Microsoft token: {}", tokenResp.error.toStdString());
        return false;
    }
    
    QString mcToken, username, uuid, errorMsg;
    bool ok = MicrosoftAuth::login_to_minecraft(tokenResp.accessToken, mcToken, username, uuid, errorMsg);
    if (!ok) {
        flint::logging::error("Failed to login to Minecraft with refreshed token: {}", errorMsg.toStdString());
        return false;
    }
    
    // Update active account in the list
    for (auto& acc : m_accounts) {
        if (acc.uuid == m_activeUuid) {
            acc.accessToken = mcToken;
            acc.refreshToken = tokenResp.refreshToken;
            acc.username = username;
            acc.uuid = uuid;
            break;
        }
    }
    
    save_accounts();
    set_active_account(uuid); // Refresh pointer and active variables
    emit accounts_changed();
    return true;
}

} // namespace flint::accounts
