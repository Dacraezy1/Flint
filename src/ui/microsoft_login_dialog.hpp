#pragma once

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include "accounts/accounts.hpp"

namespace flint::ui {

class MicrosoftLoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit MicrosoftLoginDialog(accounts::AccountManager& accountManager, QWidget* parent = nullptr);
    ~MicrosoftLoginDialog();

private slots:
    void on_poll_timer();
    void on_cancel_clicked();

private:
    void start_device_flow();
    void handle_auth_success(const QString& msAccessToken, const QString& msRefreshToken);
    void update_status(const QString& text, bool isError = false);

    accounts::AccountManager& m_accountManager;
    
    QLabel* m_instructionLabel;
    QLabel* m_codeLabel;
    QLabel* m_statusLabel;
    QPushButton* m_cancelBtn;
    
    QTimer* m_pollTimer;
    QString m_deviceCode;
    int m_pollInterval = 5;
    int m_timeRemaining = 0;
};

} // namespace flint::ui
