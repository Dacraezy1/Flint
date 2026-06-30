#pragma once

#include <QString>
#include <QVector>
#include <QObject>

namespace flint::accounts {

struct Account {
    QString username;
    QString uuid;
    QString type = "offline"; // "offline" or "microsoft"
    QString accessToken = "00000000000000000000000000000000";
    QString refreshToken;     // Microsoft OAuth refresh token
};

class AccountManager : public QObject {
    Q_OBJECT
public:
    explicit AccountManager(QObject* parent = nullptr);

    bool load_accounts();
    bool save_accounts();

    const QVector<Account>& get_accounts() const { return m_accounts; }
    const Account* get_active_account() const { return m_activeAccount; }
    
    bool add_offline_account(const QString& username);
    bool add_microsoft_account(const QString& username, const QString& uuid, const QString& accessToken, const QString& refreshToken);
    bool remove_account(const QString& uuid);
    bool set_active_account(const QString& uuid);
    
    // Dynamic token refresh for active Microsoft account before launch
    bool refresh_active_account_token();

    static QString generate_offline_uuid(const QString& username);

signals:
    void accounts_changed();
    void active_account_changed(const Account& active);

private:
    QVector<Account> m_accounts;
    const Account* m_activeAccount = nullptr;
    QString m_activeUuid;
};

} // namespace flint::accounts
